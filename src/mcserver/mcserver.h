/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2014 Couchbase, Inc.
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

#ifndef LCB_MCSERVER_H
#define LCB_MCSERVER_H
#include <libcouchbase/couchbase.h>
#include <lcbio/lcbio.h>
#include <lcbio/timer-ng.h>
#include <mc/mcreq.h>
#include <netbuf/netbuf.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lcb_settings_st;
struct lcb_server_st;
struct mc_SERVER_st;
struct mc_SERVER_PROTOFUNCS_st;

/**
 * The structure representing each couchbase server
 */
typedef struct mc_SERVER_st {
    /** Pipeline object for command queues */
    mc_PIPELINE pipeline;

    struct mc_SERVER_PROTOFUNCS_st *procs;

    /** The server endpoint as hostname:port */
    char *datahost;

    /** Pointer back to the instance */
    lcb_t instance;

    lcb_settings *settings;

    /* Defined in mcserver.c */
    int state;

    /** Whether compression is supported */
    short compsupport;

    /** Whether extended 'UUID' and 'seqno' are available for each mutation */
    short synctokens;

    /** IO/Operation timer */
    lcbio_pTIMER io_timer;

    lcbio_CTX *connctx;
    lcbio_CONNREQ connreq;

    /** Request for current connection */
    lcb_host_t *curhost;

    /** Server for secondary index requests */
    struct mc_SERVER_st *ixserver;
} mc_SERVER;

typedef struct mc_SERVER_PROTOFUNCS_st {
    /**
     * Process a single packet
     * @param The server object
     * @param The I/O context
     * @param The read buffer
     * @return MCSERVER_PKT_READ_COMPLETE or MCSERVER_PKT_READ_PARTIAL
     */
    int (*process)(mc_SERVER*, lcbio_pCTX, rdb_IOROPE*);

    /**
     * Perform any initial negotiation for the given socket
     * @param server The server which might need negotiation
     * @param The socket
     * @return True if negotiated, false otherwise. If false, the
     * implementation shall begin the negotiation process and call back to
     * mcserver_on_connected() when done.
     */
    int (*negotiate)(mc_SERVER *, lcbio_SOCKET *);

    /**
     * Called when this server is being destroyed. This is called from within
     * mcserver_close. The implementation should proxy this down the stack.
     */
    void (*clean)(mc_SERVER *);

    /** Called when a single packet has failed */
    mcreq_pktfail_fn fail_packet;
} mc_SERVER_PROTOFUNCS;

#define MCSERVER_TIMEOUT(c) (c)->settings->operation_timeout
#define MCSERVER_PKT_READ_COMPLETE 1
#define MCSERVER_PKT_READ_PARTIAL 0

/**
 * Allocate and initialize a new server object. The object will not be
 * connected
 * @param instance the instance to which the server belongs
 * @param vbc the vbucket configuration
 * @param ix the server index in the configuration
 * @return the new object or NULL on allocation failure.
 */
mc_SERVER *
mcserver_memd_alloc(lcb_t instance, lcbvb_CONFIG* vbc, int ix);

mc_SERVER *
mcserver_q2i_alloc(lcb_t instance, const char *hostport);

/* Internal callback for connection completion */
void
mcserver_on_connected(lcbio_SOCKET *sock,
    void *data, lcb_error_t err, lcbio_OSERR syserr);

void
mcserver_base_init(mc_SERVER *server, lcb_t instance, const char *hostport);

/**
 * Close the server. The resources of the server may still continue to persist
 * internally for a bit until all callbacks have been delivered and all buffers
 * flushed and/or failed.
 * @param server the server to release
 */
void
mcserver_close(mc_SERVER *server);

/**
 * Schedule a flush and potentially flush some immediate data on the server.
 * This is safe to call multiple times, however performance considerations
 * should be taken into account
 */
void
mcserver_flush(mc_SERVER *server);

/**
 * Wrapper around mcreq_pipeline_timeout() and/or mcreq_pipeline_fail(). This
 * function will purge all pending requests within the server and invoke
 * their callbacks with the given error code passed as `err`. Depending on
 * the error code, some operations may be retried.
 * @param server the server to fail
 * @param err the error code by which to fail the commands
 *
 * @note This function does not modify the server's socket or state in itself,
 * but rather simply wipes the commands from its queue
 */
void
mcserver_fail_chain(mc_SERVER *server, lcb_error_t err);

/**
 * Returns true or false depending on whether there are pending commands on
 * this server
 */
LCB_INTERNAL_API
int
mcserver_has_pending(mc_SERVER *server);

#define mcserver_get_host(server) (server)->curhost->host
#define mcserver_get_port(server) (server)->curhost->port

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* LCB_MCSERVER_H */
