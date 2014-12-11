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
#include "vbucket/aliases.h"
#include "settings.h"
#include "negotiate.h"
#include "bucketconfig/clconfig.h"
#include "ctx-log-inl.h"

#define LOGARGS(c, lvl) (c)->settings, "server", LCB_LOG_##lvl, __FILE__, __LINE__
#define LOGFMT "<%s:%s> (SRV=%p,IX=%d) "
#define LOGID(server) get_ctx_host(server->connctx), get_ctx_port(server->connctx), (void*)server, server->pipeline.index


/**
 * Invoked when get a NOT_MY_VBUCKET response. If the response contains a JSON
 * payload then we refresh the configuration with it.
 *
 * This function returns 1 if the operation was successfully rescheduled;
 * otherwise it returns 0. If it returns 0 then we give the error back to the
 * user.
 */
static int
handle_nmv(mc_SERVER *oldsrv, packet_info *resinfo, mc_PACKET *oldpkt)
{
    mc_PACKET *newpkt;
    protocol_binary_request_header hdr;
    lcb_error_t err = LCB_ERROR;
    lcb_t instance = oldsrv->instance;
    lcb_U16 vbid;
    int tmpix;
    clconfig_provider *cccp = lcb_confmon_get_provider(instance->confmon,
        LCB_CLCONFIG_CCCP);

    mcreq_read_hdr(oldpkt, &hdr);
    vbid = ntohs(hdr.request.vbucket);
    lcb_log(LOGARGS(oldsrv, WARN), LOGFMT "NOT_MY_VBUCKET. Packet=%p (S=%u). VBID=%u", LOGID(oldsrv), (void*)oldpkt, oldpkt->opaque, vbid);

    /* Notify of new map */
    tmpix = lcb_vbguess_remap(LCBT_VBCONFIG(instance),
        instance->vbguess, vbid, oldsrv->pipeline.index);
    if (tmpix > -1 && tmpix != oldsrv->pipeline.index) {
        lcb_log(LOGARGS(oldsrv, TRACE), LOGFMT "Heuristically set IX=%d as master for VBID=%u", LOGID(oldsrv), tmpix, vbid);
    }

    if (PACKET_NBODY(resinfo) && cccp->enabled) {
        lcb_string s;

        lcb_string_init(&s);
        lcb_string_append(&s, PACKET_VALUE(resinfo), PACKET_NVALUE(resinfo));
        err = lcb_cccp_update(cccp, mcserver_get_host(oldsrv), &s);
        lcb_string_release(&s);
    }

    if (err != LCB_SUCCESS) {
        lcb_bootstrap_common(instance, LCB_BS_REFRESH_ALWAYS);
    }

    if (!lcb_should_retry(oldsrv->settings, oldpkt, LCB_NOT_MY_VBUCKET)) {
        return 0;
    }

    /** Reschedule the packet again .. */
    newpkt = mcreq_renew_packet(oldpkt);
    newpkt->flags &= ~MCREQ_STATE_FLAGS;
    lcb_retryq_add(instance->retryq, (mc_EXPACKET*)newpkt, LCB_NOT_MY_VBUCKET);
    return 1;
}

/* This function is called within a loop to process a single packet.
 *
 * If a full packet is available, it will process the packet and return
 * PKT_READ_COMPLETE, resulting in the `on_read()` function calling this
 * function in a loop.
 *
 * When a complete packet is not available, PKT_READ_PARTIAL will be returned
 * and the `on_read()` loop will exit, scheduling any required pending I/O.
 */
static int
try_read(mc_SERVER *server, lcbio_CTX *ctx, rdb_IOROPE *ior)
{
    packet_info info_s, *info = &info_s;
    mc_PACKET *request;
    mc_PIPELINE *pl = &server->pipeline;
    unsigned pktsize = 24, is_last = 1;

    #define RETURN_NEED_MORE(n) \
        if (mcserver_has_pending(server)) { \
            lcbio_ctx_rwant(ctx, n); \
        } \
        return MCSERVER_PKT_READ_PARTIAL; \

    #define DO_ASSIGN_PAYLOAD() \
        rdb_consumed(ior, sizeof(info->res.bytes)); \
        if (PACKET_NBODY(info)) { \
            info->payload = rdb_get_consolidated(ior, PACKET_NBODY(info)); \
        } {

    #define DO_SWALLOW_PAYLOAD() \
        } if (PACKET_NBODY(info)) { \
            rdb_consumed(ior, PACKET_NBODY(info)); \
        }

    if (rdb_get_nused(ior) < pktsize) {
        RETURN_NEED_MORE(pktsize)
    }

    /* copy bytes into the info structure */
    rdb_copyread(ior, info->res.bytes, sizeof info->res.bytes);

    pktsize += PACKET_NBODY(info);
    if (rdb_get_nused(ior) < pktsize) {
        RETURN_NEED_MORE(pktsize);
    }

    /* Find the packet */
    if (PACKET_OPCODE(info) == PROTOCOL_BINARY_CMD_STAT && PACKET_NKEY(info) != 0) {
        is_last = 0;
        request = mcreq_pipeline_find(pl, PACKET_OPAQUE(info));
    } else {
        is_last = 1;
        request = mcreq_pipeline_remove(pl, PACKET_OPAQUE(info));
    }

    if (!request) {
        lcb_log(LOGARGS(server, WARN), LOGFMT "Found stale packet (OP=0x%x, RC=0x%x, SEQ=%u)", LOGID(server), PACKET_OPCODE(info), PACKET_STATUS(info), PACKET_OPAQUE(info));
        rdb_consumed(ior, pktsize);
        return MCSERVER_PKT_READ_COMPLETE;
    }

    if (PACKET_STATUS(info) == PROTOCOL_BINARY_RESPONSE_NOT_MY_VBUCKET) {
        /* consume the header */
        DO_ASSIGN_PAYLOAD()
        if (!handle_nmv(server, info, request)) {
            mcreq_dispatch_response(pl, request, info, LCB_NOT_MY_VBUCKET);
        }
        DO_SWALLOW_PAYLOAD()
        return MCSERVER_PKT_READ_COMPLETE;
    }

    /* Figure out if the request is 'ufwd' or not */
    if (!(request->flags & MCREQ_F_UFWD)) {
        DO_ASSIGN_PAYLOAD();
        info->bufh = rdb_get_first_segment(ior);
        mcreq_dispatch_response(pl, request, info, LCB_SUCCESS);
        DO_SWALLOW_PAYLOAD()

    } else {
        /* figure out how many buffers we want to use as an upper limit for the
         * IOV arrays. Currently we'll keep it simple and ensure the entire
         * response is contiguous. */
        lcb_PKTFWDRESP resp = { 0 };
        rdb_ROPESEG *segs;
        nb_IOV iov;

        rdb_consolidate(ior, pktsize);
        rdb_refread_ex(ior, &iov, &segs, 1, pktsize);

        resp.bufs = &segs;
        resp.iovs = (lcb_IOV*)&iov;
        resp.nitems = 1;
        resp.header = info->res.bytes;
        server->instance->callbacks.pktfwd(
            server->instance, MCREQ_PKT_COOKIE(request), LCB_SUCCESS, &resp);
        rdb_consumed(ior, pktsize);
    }

    if (is_last) {
        mcreq_packet_handled(pl, request);
    }
    return MCSERVER_PKT_READ_COMPLETE;
}

static int
maybe_retry(mc_PIPELINE *pipeline, mc_PACKET *pkt, lcb_error_t err)
{
    mc_SERVER *srv = (mc_SERVER *)pipeline;
    mc_PACKET *newpkt;
    lcb_t instance = pipeline->parent->cqdata;
    lcbvb_DISTMODE dist_t = lcbvb_get_distmode(pipeline->parent->config);

    if (dist_t != LCBVB_DIST_VBUCKET) {
        /** memcached bucket */
        return 0;
    }
    if (!lcb_should_retry(srv->settings, pkt, err)) {
        return 0;
    }

    newpkt = mcreq_renew_packet(pkt);
    newpkt->flags &= ~MCREQ_STATE_FLAGS;
    lcb_retryq_add(instance->retryq, (mc_EXPACKET *)newpkt, err);
    return 1;
}

static void
fail_callback(mc_PIPELINE *pipeline, mc_PACKET *pkt, lcb_error_t err, void *arg)
{
    int rv;
    mc_SERVER *server = (mc_SERVER *)pipeline;
    packet_info info;
    protocol_binary_request_header hdr;
    protocol_binary_response_header *res = &info.res;

    if (maybe_retry(pipeline, pkt, err)) {
        return;
    }

    if (err == LCB_AUTH_ERROR) {
        /* In-situ auth errors are actually dead servers. Let's provide this
         * as the actual error code. */
        err = LCB_MAP_CHANGED;
    }

    if (err == LCB_ETIMEDOUT) {
        lcb_error_t tmperr = lcb_retryq_origerr(pkt);
        if (tmperr != LCB_SUCCESS) {
            err = tmperr;
        }
    }

    memset(&info, 0, sizeof(info));
    memcpy(hdr.bytes, SPAN_BUFFER(&pkt->kh_span), sizeof(hdr.bytes));

    res->response.status = ntohs(PROTOCOL_BINARY_RESPONSE_EINVAL);
    res->response.opcode = hdr.request.opcode;
    res->response.opaque = hdr.request.opaque;

    lcb_log(LOGARGS(server, WARN), LOGFMT "Failing command (pkt=%p, opaque=%lu, opcode=0x%x) with error 0x%x", LOGID(server), (void*)pkt, (unsigned long)pkt->opaque, hdr.request.opcode, err);
    rv = mcreq_dispatch_response(pipeline, pkt, &info, err);
    lcb_assert(rv == 0);
    (void)arg;
}

static int
maybe_negotiate(mc_SERVER *server, lcbio_SOCKET *sock)
{
    mc_pSESSINFO sessinfo = NULL;
    /** Do we need sasl? */
    sessinfo = mc_sess_get(sock);
    if (sessinfo == NULL) {
        mc_pSESSREQ sreq;
        lcb_log(LOGARGS(server, TRACE), "<%s:%s> (SRV=%p) Session not yet negotiated. Negotiating", server->curhost->host, server->curhost->port, (void*)server);
        sreq = mc_sessreq_start(sock, server->settings,
            MCSERVER_TIMEOUT(server), mcserver_on_connected, server);
        LCBIO_CONNREQ_MKGENERIC(&server->connreq, sreq, mc_sessreq_cancel);
        return 0;
    } else {
        server->compsupport = mc_sess_chkfeature(sessinfo,
            PROTOCOL_BINARY_FEATURE_DATATYPE);
        server->synctokens = mc_sess_chkfeature(sessinfo,
            PROTOCOL_BINARY_FEATURE_MUTATION_SEQNO);
        return 1;
    }
}


static void
buf_done_cb(mc_PIPELINE *pl, const void *cookie, void *kbuf, void *vbuf)
{
    mc_SERVER *server = (mc_SERVER*)pl;
    server->instance->callbacks.pktflushed(server->instance, cookie);
    (void)kbuf; (void)vbuf;
}

static char *
dupstr_or_null(const char *s) {
    if (s) {
        return strdup(s);
    }
    return NULL;
}

static void
server_clear(mc_SERVER *server)
{
    if (server->ixserver) {
        mcserver_close(server->ixserver);
        server->ixserver = NULL;
    }
}

static struct mc_SERVER_PROTOFUNCS_st McProcs = {
        try_read,
        maybe_negotiate,
        server_clear,
        fail_callback
};

mc_SERVER *
mcserver_memd_alloc(lcb_t instance, lcbvb_CONFIG* vbc, int ix)
{
    mc_SERVER *ret;
    lcbvb_SVCMODE mode;
    const char *qhost;

    ret = calloc(1, sizeof(*ret));
    if (!ret) {
        return ret;
    }

    mode = LCBT_SETTING(instance, sslopts) & LCB_SSL_ENABLED
            ? LCBVB_SVCMODE_SSL : LCBVB_SVCMODE_PLAIN;
    ret->procs = &McProcs;
    ret->datahost = dupstr_or_null(VB_MEMDSTR(vbc, ix, mode));
    mcserver_base_init(ret, instance, ret->datahost);
    ret->pipeline.buf_done_callback = buf_done_cb;

    qhost = lcbvb_get_hostport(vbc, ix, LCBVB_SVCTYPE_IXQUERY, mode);
    if (qhost != NULL) {
        ret->ixserver = mcserver_q2i_alloc(instance, qhost);
    }

    return ret;
}

