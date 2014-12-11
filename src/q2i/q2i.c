#include "internal.h"
#include "aspend.h"
#include <lcbio/lcbio.h>
#include <libcouchbase/q2i.h>
#include "mcserver/mcserver.h"
#include "query.pb-c.h"
#include "sllist-inl.h"
#include <netbuf/netbuf.h>

static void
invoke_final(mc_SERVER *server, mc_PACKET *req, lcb_error_t  rc)
{
    lcb_RESPCALLBACK cb = lcb_find_callback(server->instance, LCB_CALLBACK_Q2I);
    lcb_RESPIXQUERY resp = { 0 };
    resp.rc = rc;
    resp.cookie = (void*)MCREQ_PKT_COOKIE(req);
    resp.rflags = LCB_RESP_F_FINAL;
    cb(server->instance, LCB_CALLBACK_Q2I, (lcb_RESPBASE*)&resp);
}

static void
invoke_entry(mc_SERVER *server, mc_PACKET *req, const Q2i__IndexEntry *ent)
{
    lcb_RESPIXQUERY resp = { 0 };
    lcb_RESPCALLBACK cb = lcb_find_callback(server->instance, LCB_CALLBACK_Q2I);
    resp.cookie = (void*)MCREQ_PKT_COOKIE(req);
    resp.docid = (const char *)ent->primarykey.data;
    resp.ndocid = ent->primarykey.len;
    resp.key = (const char *)ent->entrykey.data;
    resp.nkey = ent->entrykey.len;
    cb(server->instance, LCB_CALLBACK_Q2I, (lcb_RESPBASE*)&resp);
}

static int
process_response(mc_SERVER *server, lcbio_CTX *ctx, rdb_IOROPE *ior)
{
    size_t nbytes = rdb_get_nused(ior);
    lcb_U32 rlen;
    lcb_U16 rflags;
    const char *frame, *body;
    Q2i__QueryPayload *pbmsg;
    mc_PACKET *req;

    if (nbytes < 6) {
        lcbio_ctx_rwant(ctx, 6);
        lcbio_ctx_schedule(ctx);
        return MCSERVER_PKT_READ_PARTIAL;
    }
    frame = (const char *)rdb_get_consolidated(ior, 6);

    memcpy(&rlen, frame, 4);
    memcpy(&rflags, frame + 4, 2);
    rlen = ntohl(rlen);
    rflags = htons(rflags);

    rlen += 6;
    if (rlen > nbytes) {
        lcbio_ctx_rwant(ctx, rlen);
        lcbio_ctx_schedule(ctx);
        return MCSERVER_PKT_READ_PARTIAL;
    }

    /* Consume the header */
    rdb_consumed(ior, 6);
    rlen -= 6;
    body = rdb_get_consolidated(ior, rlen);
    pbmsg = q2i__query_payload__unpack(NULL, rlen, (const uint8_t*)body);
    if (!pbmsg) {
        assert(0 && "failed to parse response");
    }

    rdb_consumed(ior, rlen);
    req = SLLIST_ITEM(SLLIST_FIRST(&server->pipeline.requests), mc_PACKET, slnode);
    if (pbmsg->streamend) {
        lcb_error_t rc = LCB_SUCCESS;
        if (pbmsg->streamend->err) {
            const Q2i__Error *err = pbmsg->streamend->err;
            printf("Got error! %s\n", err->error);
            rc = LCB_ERROR;
        }

        sllist_remove_head(&server->pipeline.requests);
        invoke_final(server, req, rc);
        mcreq_packet_handled(&server->pipeline, req);
        q2i__query_payload__free_unpacked(pbmsg, NULL);
        return MCSERVER_PKT_READ_COMPLETE;
    }

    if (pbmsg->stream) {
        if (pbmsg->stream->err) {
            printf("Have error (%s)!\n", pbmsg->stream->err->error);
        }
        size_t ii = 0;
        for (ii = 0; ii < pbmsg->stream->n_indexentries; ii++) {
            const Q2i__IndexEntry *ent = pbmsg->stream->indexentries[ii];
            invoke_entry(server, req, ent);
        }
    }

    q2i__query_payload__free_unpacked(pbmsg, NULL);
    return 1;
}

static int
do_negotiate(mc_SERVER *server, lcbio_SOCKET *sock)
{
    (void)server;
    (void)sock;
    return 1;
}

static struct mc_SERVER_PROTOFUNCS_st Q2iProcs = {
        process_response,
        do_negotiate,
        NULL,
        NULL
};

mc_SERVER *
mcserver_q2i_alloc(lcb_t instance, const char *hostport)
{
    mc_SERVER *ret = calloc(1, sizeof(*ret));
    mcserver_base_init(ret, instance, hostport);
    ret->procs = &Q2iProcs;
    return ret;
}

static mc_SERVER *
find_ixserver(lcb_t instance)
{
    unsigned ii;
    for (ii = 0; ii < LCBT_NSERVERS(instance); ii++) {
        mc_SERVER *server = LCBT_GET_SERVER(instance, ii);
        if (server->ixserver) {
            return server->ixserver;
        }
    }
    return NULL;
}

static lcb_error_t
write_request(mc_SERVER *server,
    const Q2i__QueryPayload *pbreq, const void *cookie)
{
    mc_PACKET *req;
    char *wbuf;
    unsigned pbsz;
    lcb_U32 msglen;
    lcb_U16 msgflags;

    pbsz = q2i__query_payload__get_packed_size(pbreq);
    req = mcreq_allocate_packet(&server->pipeline);
    mcreq_reserve_blob(&server->pipeline, req, pbsz + 6);

    wbuf = SPAN_MBUFFER_NC(&req->kh_span);

    msglen = htonl(pbsz);
    msgflags = htons(0x10);

    memcpy(wbuf, &msglen, 4);
    wbuf += 4;
    memcpy(wbuf, &msgflags, 2);
    wbuf += 2;

    q2i__query_payload__pack(pbreq, (uint8_t*)wbuf);
    MCREQ_PKT_COOKIE(req) = cookie;
    MCREQ_PKT_RDATA(req)->start = gethrtime();

    /* Append to the request list.. */
    mcreq_enqueue_packet(&server->pipeline, req);
    server->pipeline.flush_start(&server->pipeline);

    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
lcb_error_t
lcb_index_query(lcb_t instance, const void *cookie, const lcb_CMDIXQUERY *cmd)
{
    Q2i__QueryPayload pbreq = Q2I__QUERY_PAYLOAD__INIT;
    Q2i__ScanRequest sreq = Q2I__SCAN_REQUEST__INIT;
    Q2i__Span span = Q2I__SPAN__INIT;
    Q2i__Range range = Q2I__RANGE__INIT;
    ProtobufCBinaryData equalbuf;

    mc_SERVER *server;
    char ixname_s[128] = { 0 };

    server = find_ixserver(instance);

    if (!server) {
        return LCB_NO_MATCHING_SERVER;
    }

    pbreq.scanrequest = &sreq;
    pbreq.version = 1;

    sreq.span = &span;
    sreq.bucket = LCBT_SETTING(server->instance, bucket);
    sreq.distinct = cmd->cmdflags & LCB_CMDIXQUERY_F_DISTINCT;

    sreq.bucket = LCBT_SETTING(server->instance, bucket);
    sreq.indexname = ixname_s;
    sreq.limit = cmd->limit;
    sreq.pagesize = cmd->pagesize;

    memcpy(ixname_s, cmd->ixname, cmd->nixname);

    if (cmd->cmdflags & LCB_CMDIXQUERY_F_LOOKUP) {
        /* Lookup a single key */
        span.equal = &equalbuf;
        span.equal->data = (uint8_t *)cmd->start_key;
        span.equal->len = cmd->nstart_key;
        span.n_equal = 1;
    } else {
        span.range = &range;
        span.n_equal = 0;
        range.low.data = (uint8_t *)cmd->start_key;
        range.low.len = cmd->nstart_key;
        range.high.data = (uint8_t *)cmd->end_key;
        range.high.len = cmd->nend_key;
    }

    write_request(server, &pbreq, cookie);
    return LCB_SUCCESS;
}
