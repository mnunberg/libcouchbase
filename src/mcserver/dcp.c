#include "internal.h"
#include "simplestring.h"
#include "dcp-internal.h"
#include "ctx-log-inl.h"
#include <libcouchbase/dcp.h>
#include <stdint.h>

#define LOGARGS(c, lvl) (c)->parent->instance->settings, "DCP", LCB_LOG_##lvl, __FILE__, __LINE__
#define LOGFMT "<%s:%s> (SRV=%p,IX=%d) "
#define LOGID(c) get_ctx_host((c)->parent->connctx), get_ctx_port((c)->parent->connctx), (void*)&(c)->parent->pipeline, (c)->parent->pipeline.index

dcp_CONNECTION *
dcp_connection_alloc(mc_SERVER *server)
{
    dcp_CONNECTION *dcp = calloc(1, sizeof(*dcp));
    lcbvb_CONFIG *vbc = LCBT_VBCONFIG(server->instance);
    dcp->parent = server;
    dcp->streams = calloc(vbc->nvb, sizeof *dcp->streams);
    return dcp;
}

static int
init_dcp_key(dcp_CONNECTION *conn, char **key, lcb_SIZE *nkey)
{
    /* Initialize the buffer for the key data */
    char numbuf[16] = { 0 };
    lcb_t instance = conn->parent->instance;
    lcb_string *scratch = instance->scratch;

    if (!scratch) {
        scratch = instance->scratch = calloc(1, sizeof(*instance->scratch));
    }

    lcb_string_clear(scratch);
    if (0 != lcb_string_appendz(scratch, "libcouchbase:")) {
        return -1;
    }
    if (LCBT_SETTING(instance, dcpkey) != NULL) {
        if (0 != lcb_string_appendz(scratch, LCBT_SETTING(instance, dcpkey))) {
            return -1;
        }
    }

    if (0 != lcb_string_appendz(scratch, "#")) {
        return -1;
    }
    sprintf(numbuf, "%d", conn->parent->pipeline.index);

    if (0 != lcb_string_appendz(scratch, numbuf)) {
        return -1;
    }

    *key = scratch->base;
    *nkey = scratch->nused;

    return 0;
}

static lcb_error_t
open_connection(dcp_CONNECTION *conn)
{
    lcb_KEYBUF kbuf;
    mc_SERVER *server = conn->parent;
    mc_PIPELINE *pl = &server->pipeline;
    char *key;
    lcb_SIZE nkey;

    protocol_binary_request_dcp_open req;
    protocol_binary_request_header *hdr = &req.message.header;
    lcb_error_t rc;

    mc_PACKET *pkt = mcreq_allocate_packet(pl);
    if (!pkt) {
        rc = LCB_CLIENT_ENOMEM;
        goto GT_FAIL;
    }


    if (0 != init_dcp_key(conn, &key, &nkey)) {
        rc = LCB_CLIENT_ENOMEM;
        goto GT_FAIL;
    }

    memset(&kbuf, 0, sizeof kbuf);
    kbuf.type = LCB_KV_COPY;
    kbuf.contig.bytes = key;
    kbuf.contig.nbytes = nkey;

    rc = mcreq_reserve_key(pl, pkt, sizeof req.bytes, &kbuf);
    if (rc != LCB_SUCCESS) {
        goto GT_FAIL;
    }

    memset(&req, 0, sizeof req);
    hdr->request.opaque = pkt->opaque;
    hdr->request.magic = PROTOCOL_BINARY_REQ;
    hdr->request.opcode = PROTOCOL_BINARY_CMD_DCP_OPEN;
    hdr->request.extlen = 8;
    hdr->request.bodylen = htonl(hdr->request.extlen + nkey);
    hdr->request.keylen = htons(nkey);

    /* Producer mode. We _connect_ to a producer */
    req.message.body.flags = ntohl(DCP_OPEN_PRODUCER);

    memcpy(SPAN_BUFFER(&pkt->kh_span), req.bytes, sizeof(req.bytes));
    memcpy(SPAN_BUFFER(&pkt->kh_span) + sizeof req.bytes, key, nkey);
    MCREQ_PKT_RDATA(pkt)->start = gethrtime();

    mcreq_sched_enter(pl->parent);
    mcreq_sched_add(pl, pkt);
    mcreq_sched_leave(pl->parent, 1);
    return LCB_SUCCESS;

    GT_FAIL:
    if (pkt) {
        mcreq_wipe_packet(pl, pkt);
        mcreq_release_packet(pl, pkt);
    }
    return rc;
}

static lcb_error_t
add_stream(dcp_CONNECTION *dcp, const dcp_VBSTREAM *orig)
{
    /* Get the packet stuff */
    protocol_binary_request_dcp_stream_req req;
    protocol_binary_request_header *hdr = &req.message.header;
    mc_PACKET *pkt = NULL;
    mc_PIPELINE *pl = &dcp->parent->pipeline;

    dcp_VBSTREAM *strm = (dcp_VBSTREAM *)malloc(sizeof(*strm));
    if (!strm) {
        return LCB_CLIENT_ENOMEM;
    }
    *strm = *orig;
    memset(&req, 0, sizeof req);

    hdr->request.opcode = PROTOCOL_BINARY_CMD_DCP_STREAM_REQ;
    hdr->request.magic = PROTOCOL_BINARY_REQ;
    hdr->request.extlen = 48;
    hdr->request.bodylen = htonl(48);
    hdr->request.vbucket = htons(orig->vbid);

    /* Initialize the body members */
    req.message.body.vbucket_uuid = htonll(strm->uuid);
    req.message.body.start_seqno = htonll(strm->seqno);
    req.message.body.end_seqno = htonll(-1);

    /* Set the snapshot fields */
    req.message.body.snap_start_seqno = req.message.body.start_seqno;
    req.message.body.snap_end_seqno = req.message.body.start_seqno;

    /* Reserve the packet */
    pkt = mcreq_allocate_packet(pl);
    if (!pkt) {
        goto GT_FAIL;
    }

    if (0 != mcreq_reserve_header(pl, pkt, sizeof req.bytes)) {
        goto GT_FAIL;
    }

    MCREQ_PKT_RDATA(pkt)->start = gethrtime();

    hdr->request.opaque = pkt->opaque;
    strm->opaque = pkt->opaque;
    memcpy(SPAN_BUFFER(&pkt->kh_span), req.bytes, sizeof req.bytes);

    /* Set the packet with the new opaque */
    assert(dcp->streams[strm->vbid] == NULL);
    dcp->streams[strm->vbid] = strm;
    mcreq_sched_add(pl, pkt);
    return LCB_SUCCESS;

    GT_FAIL:
    dcp->streams[orig->vbid] = NULL;
    if (pkt) {
        mcreq_wipe_packet(pl, pkt);
        mcreq_release_packet(pl, pkt);
    }
    if (strm) {
        free(strm);
    }
    return LCB_CLIENT_ENOMEM;
}

static lcb_error_t
request_fo_log(dcp_CONNECTION *conn, int vbid)
{
    mc_PACKET *pkt;
    mc_PIPELINE *pl = &conn->parent->pipeline;
    protocol_binary_request_header hdr;

    pkt = mcreq_allocate_packet(pl);
    assert(pkt != NULL);
    mcreq_reserve_header(pl, pkt, sizeof hdr.bytes);

    memset(&hdr, 0, sizeof hdr);

    hdr.request.opcode = PROTOCOL_BINARY_CMD_DCP_GET_FAILOVER_LOG;
    hdr.request.magic = PROTOCOL_BINARY_REQ;
    hdr.request.opaque = pkt->opaque;
    hdr.request.vbucket = ntohs(vbid);
    memcpy(SPAN_BUFFER(&pkt->kh_span), hdr.bytes, sizeof hdr.bytes);
    MCREQ_PKT_RDATA(pkt)->start = gethrtime();
    mcreq_sched_add(pl, pkt);
    return LCB_SUCCESS;
}

static lcb_error_t
got_vbucket_stats(dcp_CONNECTION *conn, packet_info *info)
{
    lcb_string str_key, str_val;
    mc_PIPELINE *pl = &conn->parent->pipeline;
    lcbvb_CONFIG *vbc = LCBT_VBCONFIG(conn->parent->instance);
    dcp_VBSTREAM *streams;
    mc_PACKET *req;
    int vb;
    lcb_U64 val;
    int is_seqno;

    assert(PACKET_STATUS(info) == PROTOCOL_BINARY_RESPONSE_SUCCESS);

    req = mcreq_pipeline_find(pl, PACKET_OPAQUE(info));
    streams = (dcp_VBSTREAM *)MCREQ_PKT_RDATA(req)->cookie;

    if (!PACKET_NKEY(info)) {
        /* End of response */
        unsigned ii;

        mcreq_sched_enter(pl->parent);
        for (ii = 0; ii < vbc->nvb; ii++) {
            dcp_VBSTREAM *cur = &streams[ii];
            if (lcbvb_vbmaster(vbc, ii) != pl->index) {
                continue;
            }
            cur->vbid = ii;
            add_stream(conn, cur);
        }
        mcreq_sched_leave(pl->parent, 1);
        mcreq_packet_handled(pl, req);
        free(streams);

    } else {
        lcb_string_init(&str_key);
        lcb_string_init(&str_val);
        lcb_string_append(&str_key, PACKET_KEY(info), PACKET_NKEY(info));
        lcb_string_append(&str_val, PACKET_VALUE(info), PACKET_NVALUE(info));

        char *colon = strstr(str_key.base, ":");
        if (!colon) {
            abort();
        }
        *colon = '\0';
        if (1 != sscanf(str_key.base, "vb_%d", &vb)) {
            abort();
        }
        colon++;
        if (strcmp(colon, "high_seqno") == 0) {
            is_seqno = 1;
        } else if (strcmp(colon, "vb_uuid") == 0 || strcmp(colon, "uuid") == 0) {
            is_seqno = 0;
        } else if (strcmp(colon, "purge_seqno") == 0 ||
                strcmp(colon, "abs_high_seqno") == 0) {
            return LCB_SUCCESS;
        } else {
            printf("Got unknown stats response '%s'!\n", colon);
            return LCB_SUCCESS;
        }

        val = strtoull(str_val.base, NULL, 10);

        /* Get the info structure */
        dcp_VBSTREAM *cur_stream = &streams[vb];
        if (is_seqno) {
            cur_stream->seqno = val;
        } else {
            cur_stream->uuid = val;
        }
    }
    return LCB_SUCCESS;
}

static lcb_error_t
request_vbucket_stats(dcp_CONNECTION *conn)
{
    const char *key = "vbucket-seqno";
    mc_PACKET *pkt;
    mc_PIPELINE *pl = &conn->parent->pipeline;
    lcbvb_CONFIG *vbc = LCBT_VBCONFIG(conn->parent->instance);
    dcp_VBSTREAM *infos = (dcp_VBSTREAM*) calloc(vbc->nvb, sizeof(*infos));
    protocol_binary_request_header hdr;

    mcreq_sched_enter(pl->parent);
    pkt = mcreq_allocate_packet(pl);
    mcreq_reserve_header(pl, pkt, sizeof hdr.bytes + strlen(key));

    memset(&hdr, 0, sizeof hdr);
    hdr.request.opcode = PROTOCOL_BINARY_CMD_STAT;
    hdr.request.magic = PROTOCOL_BINARY_REQ;
    hdr.request.opaque = pkt->opaque;
    hdr.request.keylen = htons(strlen(key));
    hdr.request.bodylen = htonl(strlen(key));

    memcpy(SPAN_BUFFER(&pkt->kh_span), hdr.bytes, sizeof hdr.bytes);
    memcpy(SPAN_BUFFER(&pkt->kh_span) + sizeof hdr.bytes, key, strlen(key));

    MCREQ_PKT_RDATA(pkt)->start = gethrtime();
    MCREQ_PKT_RDATA(pkt)->cookie = infos;

    mcreq_sched_add(pl, pkt);
    mcreq_sched_leave(pl->parent, 1);
    return LCB_SUCCESS;
}

static lcb_error_t
start_streams(dcp_CONNECTION *conn)
{
    return request_vbucket_stats(conn);
}

static void
handle_mutation(dcp_CONNECTION *conn, packet_info *info)
{
    protocol_binary_request_header *req = PACKET_REQUEST(info);
    protocol_binary_request_dcp_mutation *meta = PACKET_EPHEMERAL_START(info);

    const char *buf = (const char *)info->payload;
    const char *curkey, *value;
    buf += PACKET_EXTLEN(info); // Skip the extras
    curkey = buf;
    buf += PACKET_NKEY(info);
    value = buf;
    printf("Got mutation. Key Size=%u, Value Size=%u\n", PACKET_NKEY(info), PACKET_NVALUE(info));
    /*TODO: Handle the data. Invoke a callback maybe? */
}

static void
assignInfoBody(packet_info *info, rdb_IOROPE *ior, unsigned *pktsize)
{
    if (!PACKET_NBODY(info)) {
        return;
    }

    rdb_consumed(ior, 24);
    *pktsize -= 24;
    info->payload = rdb_get_consolidated(ior, *pktsize);
}

static void
handle_remove_request(dcp_CONNECTION *conn, packet_info *info)
{
    mc_SERVER *server = conn->parent;
    mc_PACKET *req = mcreq_pipeline_remove(&server->pipeline, PACKET_OPAQUE(info));
    if (!req) {
        lcb_log(LOGARGS(conn, WARN), LOGFMT "Found stale packet (OPAQUE=%u)", LOGID(conn), PACKET_OPAQUE(info));
        return;
    }
    mcreq_packet_handled(&server->pipeline, req);
}

static lcb_error_t
handle_failover_log(dcp_CONNECTION *conn, packet_info *info)
{
    /* This will give us the vBucket UUID */
    mc_SERVER *server = conn->parent;
    mc_PACKET *pkt;
    dcp_VBSTREAM stream = { 0 };

    lcb_U16 vbid;
    lcb_U64 uuid = 0;
    lcb_U64 seqno = 0;
    const char *cur, *end;

    if (PACKET_STATUS(info) != PROTOCOL_BINARY_RESPONSE_SUCCESS) {
        abort();
    }

    end = (const char *)info->payload + PACKET_NBODY(info);
    while (cur < end) {
        memcpy(&uuid, cur, sizeof uuid);
        cur += 8;
        memcpy(&seqno, cur, sizeof seqno);
        cur += 8;
    }

    pkt = mcreq_pipeline_remove(&server->pipeline, PACKET_OPAQUE(info));
    vbid = mcreq_get_vbucket(pkt);
    mcreq_packet_handled(&server->pipeline, pkt);

    stream.seqno = htonll(seqno);
    stream.uuid = htonll(uuid);
    stream.vbid = vbid;

    mcreq_sched_enter(server->pipeline.parent);
    add_stream(conn, &stream);
    mcreq_sched_leave(server->pipeline.parent, 1);
    return LCB_SUCCESS;
}

static void
stream_req_handler(dcp_CONNECTION *dcp, packet_info  *info)
{
    lcb_U64 cur_seqno = 0;
    lcb_U64 cur_uuid = 0;
    lcb_U16 vbid;
    const char *body;
    mc_PACKET *req;
    dcp_VBSTREAM *stream;
    dcp_VBSTREAM newstream;
    mc_PIPELINE *pl = &dcp->parent->pipeline;

    /* Find the request */
    req = mcreq_pipeline_find(&dcp->parent->pipeline, PACKET_OPAQUE(info));

    if (!req) {
        /* This might be a response to an existing stream we already closed.
         * We don't want to get all the mutations! */
        abort();
    }

    vbid = mcreq_get_vbucket(req);
    stream = dcp->streams[vbid];

    if (PACKET_STATUS(info) == PROTOCOL_BINARY_RESPONSE_SUCCESS) {
        assert(PACKET_NBODY(info) >= 16);
        body = (const char *)info->payload;
        memcpy(&cur_uuid, body, 8);
        body += 8;
        memcpy(&cur_seqno, body, 8);

        cur_uuid = htonll(cur_uuid);
        cur_seqno = htonll(cur_seqno);
        stream->seqno = cur_seqno;
        stream->uuid = cur_uuid;
        lcb_log(LOGARGS(dcp, DEBUG), LOGFMT "Stream open for VB=%d. SEQNO=0x%lx, UUID=0x%lx", LOGID(dcp), vbid, cur_seqno, cur_uuid);

    } else if (PACKET_STATUS(info) == PROTOCOL_BINARY_RESPONSE_ROLLBACK) {
        assert(PACKET_NBODY(info) >= 8);
        memcpy(&cur_seqno, info->payload, 8);
        cur_seqno = htonll(cur_seqno);

        lcb_log(LOGARGS(dcp, INFO), LOGFMT "Server responsed with ROLLBACK for VB=%d. SEQNO=0x%lx", LOGID(dcp), vbid, cur_seqno);

        newstream = *stream;
        newstream.seqno = cur_seqno;

        mcreq_pipeline_remove(pl, PACKET_OPAQUE(info));
        mcreq_packet_handled(pl, req);
        free(dcp->streams[vbid]);
        dcp->streams[vbid] = NULL;

        mcreq_sched_enter(pl->parent);
        add_stream(dcp, &newstream);
        mcreq_sched_leave(pl->parent, 1);
    } else {
        abort();
    }
}

int
mcserver_dcp_read(lcbio_CTX *ctx, mc_SERVER *server, rdb_IOROPE *ior)
{
    packet_info info_s, *info = &info_s;
    unsigned pktsize = 24;
    dcp_CONNECTION *dcp = (dcp_CONNECTION *)server->dcpctx;

    #define RETURN_NEED_MORE(n) \
        if (mcserver_has_pending(server)) { \
            lcbio_ctx_rwant(ctx, n); \
        } \
        return 0; \

    if (rdb_get_nused(ior) < pktsize) {
        RETURN_NEED_MORE(pktsize)
    }

    /* copy bytes into the info structure */
    rdb_copyread(ior, info->res.bytes, sizeof info->res.bytes);

    pktsize += PACKET_NBODY(info);
    if (rdb_get_nused(ior) < pktsize) {
        RETURN_NEED_MORE(pktsize);
    }

    assignInfoBody(info, ior, &pktsize);

    // Now we figure out the kind of connection this is:
    switch (PACKET_OPCODE(info)) {
    case PROTOCOL_BINARY_CMD_DCP_OPEN:
        handle_remove_request(dcp, info);

        if (PACKET_STATUS(info) != PROTOCOL_BINARY_RESPONSE_SUCCESS) {
            lcb_log(LOGARGS(dcp, ERROR), LOGFMT "DCP_OPEN FAILED (0x%x)", LOGID(dcp), PACKET_STATUS(info));
            /* TODO: Invoke callback */
        } else {
            start_streams(dcp);
        }
        break;

    case PROTOCOL_BINARY_CMD_DCP_STREAM_REQ:
        stream_req_handler(dcp, info);
        break;

    case PROTOCOL_BINARY_CMD_DCP_MUTATION:
        handle_mutation(dcp, info);
        break;

    case PROTOCOL_BINARY_CMD_DCP_STREAM_END:
    {
        uint32_t reason;
        memcpy(&reason, info->payload, sizeof reason);
        reason = htonl(reason);

        lcb_log(LOGARGS(dcp, WARN), LOGFMT "Got STREAM_END. Status=0x%x, Reason=0x%x", LOGID(dcp), PACKET_STATUS(info), reason);

        if (PACKET_STATUS(info) != PROTOCOL_BINARY_RESPONSE_SUCCESS) {
            abort();
        }
        break;
    }

    case PROTOCOL_BINARY_CMD_DCP_SNAPSHOT_MARKER:
        printf("Got snapshot marker!\n");
        break;

    case PROTOCOL_BINARY_CMD_DCP_GET_FAILOVER_LOG:
        handle_failover_log(dcp, info);
        break;

    case PROTOCOL_BINARY_CMD_STAT:
        got_vbucket_stats(dcp, info);
        break;

    default:
        printf("Unhandled OP!\n");
        abort();
    }
    rdb_consumed(ior, pktsize);
    return 1;
}

LIBCOUCHBASE_API
lcb_error_t lcb_start_dcp(lcb_t instance)
{
    LCBT_SETTING(instance, is_dcp) = 1;

    for (size_t ii = 0; ii < LCBT_NSERVERS(instance); ii++) {
        mc_SERVER *server = LCBT_GET_SERVER(instance, ii);
        if (server->dcpctx) {
            // already has DCP?
            continue;
        }
        dcp_CONNECTION *conn = dcp_connection_alloc(server);
        server->dcpctx = conn;
        lcb_error_t rv = open_connection(conn);
        if (rv != LCB_SUCCESS) {
            return rv;
        }
    }
    return LCB_SUCCESS;
}
