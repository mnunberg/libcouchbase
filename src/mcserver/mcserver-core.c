/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2011-2015 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "internal.h"
#include "logging.h"
#include "settings.h"
#include "mc/mcreq-flush-inl.h"
#include <lcbio/ssl.h>
#include "ctx-log-inl.h"

#define LOGARGS(c, lvl) (c)->settings, "server", LCB_LOG_##lvl, __FILE__, __LINE__
#define LOGFMT "<%s:%s> (SRV=%p,IX=%d) "
#define LOGID(server) get_ctx_host(server->connctx), get_ctx_port(server->connctx), (void*)server, server->pipeline.index
#define MCREQ_MAXIOV 32

typedef enum {
    /* There are no known errored commands on this server */
    S_CLEAN,

    /* In the process of draining remaining commands to be flushed. The commands
     * being drained may have already been rescheduled to another server or
     * placed inside the error queue, but are pending being flushed. This will
     * only happen in completion-style I/O plugins. When this state is in effect,
     * subsequent attempts to connect will be blocked until all commands have
     * been properly drained.
     */
    S_ERRDRAIN,

    /* The server object has been closed, either because it has been removed
     * from the cluster or because the related lcb_t has been destroyed.
     */
    S_CLOSED
} mcserver_STATE;

static int check_closed(mc_SERVER *);
static void start_errored_ctx(mc_SERVER *server, mcserver_STATE next_state);
static void finalize_errored_ctx(mc_SERVER *server);
static void on_error(lcbio_CTX *ctx, lcb_error_t err);
static void server_socket_failed(mc_SERVER *server, lcb_error_t err);

static void
on_flush_ready(lcbio_CTX *ctx)
{
    mc_SERVER *server = lcbio_ctx_data(ctx);
    nb_IOV iov[MCREQ_MAXIOV];
    int ready;

    do {
        int niov = 0;
        unsigned nb;
        nb = mcreq_flush_iov_fill(&server->pipeline, iov, MCREQ_MAXIOV, &niov);
        if (!nb) {
            return;
        }
        ready = lcbio_ctx_put_ex(ctx, (lcb_IOV *)iov, niov, nb);
    } while (ready);
    lcbio_ctx_wwant(ctx);
}

static void
on_flush_done(lcbio_CTX *ctx, unsigned expected, unsigned actual)
{
    mc_SERVER *server = lcbio_ctx_data(ctx);
    mcreq_flush_done(&server->pipeline, actual, expected);
    check_closed(server);
}

void
mcserver_flush(mc_SERVER *server)
{
    /** Call into the wwant stuff.. */
    if (!server->connctx->rdwant) {
        lcbio_ctx_rwant(server->connctx, 24);
    }

    lcbio_ctx_wwant(server->connctx);
    lcbio_ctx_schedule(server->connctx);

    if (!lcbio_timer_armed(server->io_timer)) {
        /**
         * XXX: Maybe use get_next_timeout(), although here we can assume
         * that a command was just scheduled
         */
        lcbio_timer_rearm(server->io_timer, MCSERVER_TIMEOUT(server));
    }
}

LIBCOUCHBASE_API
void
lcb_sched_flush(lcb_t instance)
{
    unsigned ii;
    for (ii = 0; ii < LCBT_NSERVERS(instance); ii++) {
        mc_SERVER *server = LCBT_GET_SERVER(instance, ii);

        if (!mcserver_has_pending(server)) {
            continue;
        }
        server->pipeline.flush_start(&server->pipeline);
    }
}

static void
on_read(lcbio_CTX *ctx, unsigned nb)
{
    mc_SERVER *server = lcbio_ctx_data(ctx);
    rdb_IOROPE *ior = &ctx->ior;

    if (check_closed(server)) {
        return;
    }

    while (server->procs->process(server, ctx, ior) ==
            MCSERVER_PKT_READ_COMPLETE);
    lcbio_ctx_schedule(ctx);
    lcb_maybe_breakout(server->instance);

    (void)nb;
}

LCB_INTERNAL_API
int
mcserver_has_pending(mc_SERVER *server)
{
    return !SLLIST_IS_EMPTY(&server->pipeline.requests);
}

static void flush_noop(mc_PIPELINE *pipeline) {
    (void)pipeline;
}
static void server_connect(mc_SERVER *server);

typedef enum {
    REFRESH_ALWAYS,
    REFRESH_ONFAILED,
    REFRESH_NEVER
} mc_REFRESHPOLICY;

static int
purge_single_server(mc_SERVER *server, lcb_error_t error,
                    hrtime_t thresh, hrtime_t *next, int policy)
{
    unsigned affected;
    mc_PIPELINE *pl = &server->pipeline;

    if (thresh) {
        affected = mcreq_pipeline_timeout(
                pl, error, server->procs->fail_packet, NULL, thresh, next);

    } else {
        mcreq_pipeline_fail(pl, error, server->procs->fail_packet, NULL);
        affected = -1;
    }

    if (policy == REFRESH_NEVER) {
        return affected;
    }

    if (affected || policy == REFRESH_ALWAYS) {
        lcb_bootstrap_common(server->instance,
            LCB_BS_REFRESH_THROTTLE|LCB_BS_REFRESH_INCRERR);
    }
    return affected;
}

static void flush_errdrain(mc_PIPELINE *pipeline)
{
    /* Called when we are draining errors. */
    mc_SERVER *server = (mc_SERVER *)pipeline;
    if (!lcbio_timer_armed(server->io_timer)) {
        lcbio_timer_rearm(server->io_timer, MCSERVER_TIMEOUT(server));
    }
}

void
mcserver_fail_chain(mc_SERVER *server, lcb_error_t err)
{
    purge_single_server(server, err, 0, NULL, REFRESH_NEVER);
}


static uint32_t
get_next_timeout(mc_SERVER *server)
{
    hrtime_t now, expiry, diff;
    mc_PACKET *pkt = mcreq_first_packet(&server->pipeline);

    if (!pkt) {
        return MCSERVER_TIMEOUT(server);
    }

    now = gethrtime();
    expiry = MCREQ_PKT_RDATA(pkt)->start + LCB_US2NS(MCSERVER_TIMEOUT(server));
    if (expiry <= now) {
        diff = 0;
    } else {
        diff = expiry - now;
    }

    return LCB_NS2US(diff);
}

static void
timeout_server(void *arg)
{
    mc_SERVER *server = arg;
    hrtime_t now, min_valid, next_ns = 0;
    uint32_t next_us;
    int npurged;

    now = gethrtime();
    min_valid = now - LCB_US2NS(MCSERVER_TIMEOUT(server));
    npurged = purge_single_server(server,
        LCB_ETIMEDOUT, min_valid, &next_ns, REFRESH_ONFAILED);
    if (npurged) {
        lcb_log(LOGARGS(server, ERROR), LOGFMT "Server timed out. Some commands have failed", LOGID(server));
    }

    next_us = get_next_timeout(server);
    lcb_log(LOGARGS(server, DEBUG), LOGFMT "Scheduling next timeout for %u ms", LOGID(server), next_us / 1000);
    lcbio_timer_rearm(server->io_timer, next_us);
    lcb_maybe_breakout(server->instance);
}

void
mcserver_on_connected(lcbio_SOCKET *sock,
    void *data, lcb_error_t err, lcbio_OSERR syserr)
{
    mc_SERVER *server = data;
    lcbio_CTXPROCS procs;
    uint32_t tmo;
    int rv;
    LCBIO_CONNREQ_CLEAR(&server->connreq);

    if (err != LCB_SUCCESS) {
        lcb_log(LOGARGS(server, ERR), LOGFMT "Got error for connection! (OS=%d)", LOGID(server), syserr);
        server_socket_failed(server, err);
        return;
    }

    lcb_assert(sock);
    rv = server->procs->negotiate(server, sock);
    if (!rv) {
        return;
    }

    procs.cb_err = on_error;
    procs.cb_read = on_read;
    procs.cb_flush_done = on_flush_done;
    procs.cb_flush_ready = on_flush_ready;
    server->connctx = lcbio_ctx_new(sock, server, &procs);
    server->connctx->subsys = "memcached";
    server->pipeline.flush_start = (mcreq_flushstart_fn)mcserver_flush;

    tmo = get_next_timeout(server);
    lcb_log(LOGARGS(server, DEBUG), LOGFMT "Setting initial timeout=%ums", LOGID(server), tmo/1000);
    lcbio_timer_rearm(server->io_timer, get_next_timeout(server));
    mcserver_flush(server);
}

static void
server_connect(mc_SERVER *server)
{
    lcbio_pMGRREQ mr;
    mr = lcbio_mgr_get(server->instance->memd_sockpool, server->curhost,
                       MCSERVER_TIMEOUT(server),
                       mcserver_on_connected, server);
    LCBIO_CONNREQ_MKPOOLED(&server->connreq, mr);
    server->pipeline.flush_start = flush_noop;
    server->state = S_CLEAN;
}

void
mcserver_base_init(mc_SERVER *server, lcb_t instance, const char *hostport)
{
    server->instance = instance;
    server->settings = instance->settings;
    server->curhost = calloc(1, sizeof(*server->curhost));

    if (hostport) {
        lcb_host_parsez(server->curhost, hostport, 65530);
    }

    lcb_settings_ref(server->settings);
    mcreq_pipeline_init(&server->pipeline);
    server->pipeline.flush_start = (mcreq_flushstart_fn)server_connect;
    server->io_timer = lcbio_timer_new(instance->iotable, server, timeout_server);
}

static void
server_free(mc_SERVER *server)
{
    server->procs->clean(server);
    mcreq_pipeline_cleanup(&server->pipeline);

    if (server->io_timer) {
        lcbio_timer_destroy(server->io_timer);
    }

    free(server->datahost);
    free(server->curhost);
    lcb_settings_unref(server->settings);
    free(server);
}

static void
close_cb(lcbio_SOCKET *sock, int reusable, void *arg)
{
    lcbio_ref(sock);
    lcbio_mgr_discard(sock);
    (void)reusable;(void)arg;
}


/**Marks any unflushed data inside this server as being already flushed. This
 * should be done within error handling. If subsequent data is flushed on this
 * pipeline to the same connection, the results are undefined. */
static void
release_unflushed_packets(mc_SERVER *server)
{
    unsigned toflush;
    nb_IOV iov;
    mc_PIPELINE *pl = &server->pipeline;
    while ((toflush = mcreq_flush_iov_fill(pl, &iov, 1, NULL))) {
        mcreq_flush_done(pl, toflush, toflush);
    }
}

static void
on_error(lcbio_CTX *ctx, lcb_error_t err)
{
    mc_SERVER *server = lcbio_ctx_data(ctx);
    lcb_log(LOGARGS(server, WARN), LOGFMT "Got socket error 0x%x", LOGID(server), err);
    if (check_closed(server)) {
        return;
    }
    server_socket_failed(server, err);
}

/**Handle a socket error. This function will close the current connection
 * and trigger a failout of any pending commands.
 * This function triggers a configuration refresh */
static void
server_socket_failed(mc_SERVER *server, lcb_error_t err)
{
    if (check_closed(server)) {
        return;
    }

    purge_single_server(server, err, 0, NULL, REFRESH_ALWAYS);
    lcb_maybe_breakout(server->instance);
    start_errored_ctx(server, S_ERRDRAIN);
}

void
mcserver_close(mc_SERVER *server)
{
    /* Should never be called twice */
    lcb_assert(server->state != S_CLOSED);
    start_errored_ctx(server, S_CLOSED);
}

/**
 * Call to signal an error or similar on the current socket.
 * @param server The server
 * @param next_state The next state (S_CLOSED or S_ERRDRAIN)
 */
static void
start_errored_ctx(mc_SERVER *server, mcserver_STATE next_state)
{
    lcbio_CTX *ctx = server->connctx;

    server->state = next_state;
    /* Cancel any pending connection attempt? */
    lcbio_connreq_cancel(&server->connreq);

    /* If the server is being destroyed, silence the timer */
    if (next_state == S_CLOSED && server->io_timer != NULL) {
        lcbio_timer_destroy(server->io_timer);
        server->io_timer = NULL;
    }

    if (ctx == NULL) {
        if (next_state == S_CLOSED) {
            server_free(server);
            return;
        } else {
            /* Not closed but don't have a current context */
            server->pipeline.flush_start = (mcreq_flushstart_fn)server_connect;
            if (mcserver_has_pending(server)) {
                if (!lcbio_timer_armed(server->io_timer)) {
                    /* TODO: Maybe throttle reconnection attempts? */
                    lcbio_timer_rearm(server->io_timer, MCSERVER_TIMEOUT(server));
                }
                server_connect(server);
            }
        }

    } else {
        if (ctx->npending) {
            /* Have pending items? */

            /* Flush any remaining events */
            lcbio_ctx_schedule(ctx);

            /* Close the socket not to leak resources */
            lcbio_shutdown(lcbio_ctx_sock(ctx));
            if (next_state == S_ERRDRAIN) {
                server->pipeline.flush_start = (mcreq_flushstart_fn)flush_errdrain;
            }
        } else {
            finalize_errored_ctx(server);
        }
    }
}

/**
 * This function actually finalizes a ctx which has an error on it. If the
 * ctx has pending operations remaining then this function returns immediately.
 * Otherwise this will either reinitialize the connection or free the server
 * object depending on the actual object state (i.e. if it was closed or
 * simply errored).
 */
static void
finalize_errored_ctx(mc_SERVER *server)
{
    if (server->connctx->npending) {
        return;
    }

    lcb_log(LOGARGS(server, DEBUG), LOGFMT "Finalizing ctx %p", LOGID(server), (void*)server->connctx);

    /* Always close the existing context. */
    lcbio_ctx_close(server->connctx, close_cb, NULL);
    server->connctx = NULL;

    /* And pretend to flush any outstanding data. There's nothing pending! */
    release_unflushed_packets(server);

    if (server->state == S_CLOSED) {
        /* If the server is closed, time to free it */
        server_free(server);
    } else {
        /* Otherwise, cycle the state back to CLEAN and reinit
         * the connection */
        server->state = S_CLEAN;
        server->pipeline.flush_start = (mcreq_flushstart_fn)server_connect;
        server_connect(server);
    }
}

/**
 * This little function checks to see if the server struct is still valid, or
 * whether it should just be cleaned once no pending I/O remainds.
 *
 * If this function returns false then the server is still valid; otherwise it
 * is invalid and must not be used further.
 */
static int
check_closed(mc_SERVER *server)
{
    if (server->state == S_CLEAN) {
        return 0;
    }
    lcb_log(LOGARGS(server, INFO), LOGFMT "Got handler after close. Checking pending calls", LOGID(server));
    finalize_errored_ctx(server);
    return 1;
}
