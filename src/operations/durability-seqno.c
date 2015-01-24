#define LCBDUR_PRIV_SYMS

#include "internal.h"
#include <libcouchbase/api3.h>
#include "durability_internal.h"

#define ENT_SEQNO(ent) (ent)->reqcas

static void
seqno_callback(lcb_t instance, int ign, const lcb_RESPBASE *rb)
{
    const lcb_RESPOBSEQNO *resp = (const lcb_RESPOBSEQNO*)rb;
    char *pp = resp->cookie;
    lcb_DURITEM *ent;
    int flags = 0;
    lcb_U64 seqno_mem, seqno_disk;

    pp -= offsetof(lcb_DURITEM, callback);
    ent = (lcb_DURITEM *)pp;
    /* Now, process the response */

    if (resp->rc != LCB_SUCCESS) {
        RESFLD(ent, rc) = resp->rc;
        goto GT_TALLY;
    }

    if (resp->old_uuid) {
        /* Failover! */
        seqno_mem = seqno_disk = resp->old_seqno;
        if (seqno_mem < ENT_SEQNO(ent)) {
            RESFLD(ent, rc) = LCB_MUTATION_LOST;
            lcbdur_ent_finish(ent);
            goto GT_TALLY;
        }
    } else {
        seqno_mem = resp->mem_seqno;
        seqno_disk = resp->persisted_seqno;
    }

    if (seqno_mem < ENT_SEQNO(ent)) {
        goto GT_TALLY;
    }

    flags = LCBDUR_UPDATE_REPLICATED;
    if (seqno_disk >= ENT_SEQNO(ent)) {
        flags |= LCBDUR_UPDATE_PERSISTED;
    }

    lcbdur_update_item(ent, flags, resp->server_index);

    GT_TALLY:
    if (!--ent->parent->waiting) {
        /* avoid ssertion (wait==0)! */
        ent->parent->waiting = 1;
        lcbdur_reqs_done(ent->parent);
    }

    (void)ign; (void)instance;
}

static lcb_error_t
seqno_poll(lcb_DURSET *dset)
{
    lcb_error_t ret_err = LCB_EINTERNAL; /* This should never be returned */
    size_t ii;
    int has_ops = 0;
    lcb_t instance = dset->instance;

    lcb_sched_enter(instance);
    for (ii = 0; ii < DSET_COUNT(dset); ii++) {
        lcb_DURITEM *ent = DSET_ENTRIES(dset) + ii;
        size_t jj, nservers = 0;
        lcb_U16 servers[4];
        lcb_CMDOBSEQNO cmd = { 0 };

        if (ent->done) {
            continue;
        }

        cmd.uuid = ent->uuid;
        cmd.vbid = ent->vbid;
        cmd.cmdflags = LCB_CMD_F_INTERNAL_CALLBACK;
        ent->callback = seqno_callback;

        lcbdur_prepare_item(ent, servers, &nservers);
        for (jj = 0; jj < nservers; jj++) {
            lcb_error_t err;
            cmd.server_index = servers[jj];
            err = lcb_observe_seqno3(instance, &ent->callback, &cmd);
            if (err == LCB_SUCCESS) {
                dset->waiting++;
                has_ops = 1;
            } else {
                RESFLD(ent, rc) = ret_err = err;
            }
        }
    }
    lcb_sched_leave(instance);
    if (!has_ops) {
        return ret_err;
    } else {
        return LCB_SUCCESS;
    }
}

static lcb_error_t
seqno_ent_add(lcb_DURSET *dset, lcb_DURITEM *item, const lcb_CMDENDURE *cmd)
{
    const lcb_SYNCTOKEN *stok = cmd->synctoken;
    if (stok == NULL) {
        lcb_t instance = dset->instance;
        if (!instance->dcpinfo) {
            return LCB_DURABILITY_NO_SYNCTOKEN;
        }
        if (item->vbid >= LCBT_VBCONFIG(instance)->nvb) {
            return LCB_EINVAL;
        }
        stok = instance->dcpinfo + item->vbid;
        if (LCB_SYNCTOKEN_ID(stok) == 0) {
            return LCB_DURABILITY_NO_SYNCTOKEN;
        }
    }

    /* Set the fields */
    memset(item->sinfo, 0, sizeof(item->sinfo[0]) * 4);
    item->uuid = LCB_SYNCTOKEN_ID(stok);
    ENT_SEQNO(item) = LCB_SYNCTOKEN_SEQ(stok);
    return LCB_SUCCESS;
}

lcbdur_PROCS lcbdur_seqno_procs = {
        seqno_poll,
        seqno_ent_add,
        NULL, /*schedule*/
        NULL /*clean*/
};

lcb_error_t
lcb_observe_seqno3(lcb_t instance, const void *cookie, const lcb_CMDOBSEQNO *cmd)
{
    mc_PACKET *pkt;
    mc_SERVER *server;
    mc_PIPELINE *pl;
    protocol_binary_request_header hdr;
    lcb_U64 uuid;

    if (cmd->server_index > LCBT_NSERVERS(instance)) {
        return LCB_EINVAL;
    }

    server = LCBT_GET_SERVER(instance, cmd->server_index);
    pl = &server->pipeline;
    pkt = mcreq_allocate_packet(pl);
    mcreq_reserve_header(pl, pkt, MCREQ_PKT_BASESIZE);
    mcreq_reserve_value2(pl, pkt, 8);

    /* Set the static fields */
    MCREQ_PKT_RDATA(pkt)->cookie = cookie;
    MCREQ_PKT_RDATA(pkt)->start = gethrtime();
    if (cmd->cmdflags & LCB_CMD_F_INTERNAL_CALLBACK) {
        pkt->flags |= MCREQ_F_PRIVCALLBACK;
    }

    memset(&hdr, 0, sizeof hdr);
    hdr.request.opaque = pkt->opaque;
    hdr.request.magic = PROTOCOL_BINARY_REQ;
    hdr.request.opcode = PROTOCOL_BINARY_CMD_OBSERVE_SEQNO;
    hdr.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    hdr.request.bodylen = htonl((lcb_U32)8);
    hdr.request.vbucket = htons(cmd->vbid);
    memcpy(SPAN_BUFFER(&pkt->kh_span), hdr.bytes, sizeof hdr.bytes);

    uuid = htonll(cmd->uuid);
    memcpy(SPAN_BUFFER(&pkt->u_value.single), &uuid, sizeof uuid);
    mcreq_sched_add(pl, pkt);
    return LCB_SUCCESS;
}

lcb_error_t
lcb_get_synctoken(lcb_t instance, const void *key, size_t nkey,
    lcb_U16 *vbid, const lcb_SYNCTOKEN **stok)
{
    int vbix, srvix;
    const lcb_SYNCTOKEN *existing;
    if (!LCBT_VBCONFIG(instance)) {
        return LCB_CLIENT_ETMPFAIL;
    }
    if (LCBT_VBCONFIG(instance)->dtype != LCBVB_DIST_VBUCKET) {
        return LCB_NOT_SUPPORTED;
    }
    if (!LCBT_SETTING(instance, fetch_synctokens)) {
        return LCB_NOT_SUPPORTED;
    }
    if (!nkey) {
        return LCB_EMPTY_KEY;
    }

    lcbvb_map_key(LCBT_VBCONFIG(instance), key, nkey, &vbix, &srvix);

    if (!instance->dcpinfo) {
        return LCB_DURABILITY_NO_SYNCTOKEN;
    }

    existing = instance->dcpinfo + vbix;
    if (!LCB_SYNCTOKEN_ID(existing)) {
        return LCB_DURABILITY_NO_SYNCTOKEN;
    }
    *stok = existing;
    *vbid = vbix;
    return LCB_SUCCESS;
}
