#include "viewreq.h"
#include "sllist-inl.h"
#include "http/http.h"

#define MAX_GET_URI_LENGTH 2048
#define MAX_PENDING_DOCREQ 100

static void chunk_callback(lcb_t, int, const lcb_RESPBASE*);
static void doc_callback(lcb_t, int, const lcb_RESPBASE*);
static void row_callback(lcbvrow_PARSER*, const lcbvrow_ROW*);
static void invoke_pending_docreq(lcbview_REQUEST *req);
static void schedule_docreqs(lcbview_REQUEST *req);
static void invoke_row(lcbview_REQUEST *req, lcb_RESPVIEWQUERY *resp);
static void unref_request(lcbview_REQUEST *req);

#define IOV2PTRLEN(iov, ptr, len) do { \
    ptr = (iov)->iov_base; \
    len = (iov)->iov_len; \
} while (0);

/* Whether the request (from the user side) is still ongoing */
#define CAN_CONTINUE(req) ((req)->callback != NULL)

/* Whether there are pending documents to fetch */
#define HAVE_PENDING_DOCREQS(req) \
    (SLLIST_IS_EMPTY(&(req)->cb_queue) == 0 || SLLIST_IS_EMPTY(&(req)->pending_gets) == 0)

static void
invoke_last(lcbview_REQUEST *req, lcb_error_t err)
{
    lcb_RESPVIEWQUERY resp = { 0 };
    if (req->callback == NULL || HAVE_PENDING_DOCREQS(req)) {
        return;
    }

    resp.rc = err;
    resp.htresp = req->cur_htresp;
    resp.cookie = (void *)req->cookie;
    resp.rflags = LCB_RESP_F_FINAL;
    if (req->parser && req->parser->meta_complete) {
        resp.value = req->parser->meta_buf.base;
        resp.nvalue = req->parser->meta_buf.nused;
    } else {
        resp.rflags |= LCB_RESP_F_CLIENTGEN;
    }
    req->callback(req->instance, -1, &resp);
    req->callback = NULL;

}

static void
invoke_row(lcbview_REQUEST *req, lcb_RESPVIEWQUERY *resp)
{
    if (req->callback == NULL) {
        return;
    }
    resp->htresp = req->cur_htresp;
    resp->cookie = (void *)req->cookie;
    req->callback(req->instance, -1, resp);
}

static void
chunk_callback(lcb_t instance, int cbtype, const lcb_RESPBASE *rb)
{
    const lcb_RESPHTTP *rh = (const lcb_RESPHTTP *)rb;
    lcbview_REQUEST *req = rh->cookie;

    (void)cbtype;
    req->cur_htresp = rh;

    if (rh->rc != LCB_SUCCESS || rh->htstatus != 200 || (rh->rflags & LCB_RESP_F_FINAL)) {
        if (req->lasterr == LCB_SUCCESS && rh->htstatus != 200) {
            req->lasterr = LCB_HTTP_ERROR;
        }
        invoke_last(req, req->lasterr);

        if (rh->rflags & LCB_RESP_F_FINAL) {
            req->htreq = NULL;
            unref_request(req);
        }
        return;
    }

    if (!CAN_CONTINUE(req)) {
        return;
    }

    lcbvrow_feed(req->parser, rh->body, rh->nbody);
    /* Process any get requests for include_docs, if needed: */
    if (CAN_CONTINUE(req) && req->include_docs) {
        schedule_docreqs(req);
    }

    req->cur_htresp = NULL;
}

static void
schedule_docreqs(lcbview_REQUEST *req)
{
    sllist_iterator iter = { NULL };
    lcb_t instance = req->instance;
    lcb_sched_enter(instance);

    SLLIST_ITERFOR(&req->pending_gets, &iter) {
        lcbview_DOCREQ *cont = SLLIST_ITEM(iter.cur, lcbview_DOCREQ, slnode);
        lcb_CMDGET gcmd = { 0 };
        lcb_error_t rc;

        if (req->ndocs_pending > MAX_PENDING_DOCREQ && req->htreq) {
            lcb_htreq_pause(req->htreq);
            break;
        }

        cont->callback = doc_callback;
        LCB_CMD_SET_KEY(&gcmd, cont->docid.iov_base, cont->docid.iov_len);
        gcmd.cmdflags |= LCB_CMD_F_INTERNAL_CALLBACK;
        rc = lcb_get3(instance, &cont->callback, &gcmd);

        if (rc != LCB_SUCCESS) {
            /* Set the error status */
            cont->docresp.rc = rc;
            cont->ready = 1;
        } else {
            req->refcount++;
            req->ndocs_pending++;
        }
        sllist_iter_remove(&req->pending_gets, &iter);
        sllist_append(&req->cb_queue, &cont->slnode);
    }

    lcb_sched_leave(instance);
    lcb_sched_flush(instance);

    /* It's possible that none of the items here got scheduled. If so,
     * invoke any of these, so we don't end up with hanging requests that
     * will never get a callback */
    invoke_pending_docreq(req);
}

static void
invoke_pending_docreq(lcbview_REQUEST *req)
{
    sllist_iterator iter = { NULL };
    SLLIST_ITERFOR(&req->cb_queue, &iter) {
        lcb_RESPVIEWQUERY resp = { 0 };
        lcbview_DOCREQ *dreq = SLLIST_ITEM(iter.cur, lcbview_DOCREQ, slnode);

        if (dreq->ready == 0) {
            break;
        }

        IOV2PTRLEN(&dreq->key, resp.key, resp.nkey);
        IOV2PTRLEN(&dreq->value, resp.value, resp.nvalue);
        IOV2PTRLEN(&dreq->docid, resp.docid, resp.ndocid);
        resp.docresp = &dreq->docresp;

        invoke_row(req, &resp);
        sllist_iter_remove(&req->cb_queue, &iter);

        if (dreq->docresp.bufh) {
            lcb_backbuf_unref(dreq->docresp.bufh);
        }
        free(dreq->rowbuf);
        free(dreq);
    }
}

static void
doc_callback(lcb_t instance, int cbtype, const lcb_RESPBASE *rb)
{
    const lcb_RESPGET *rg = (const lcb_RESPGET *)rb;
    lcbview_DOCREQ *dreq = rb->cookie;
    lcbview_REQUEST *req = dreq->parent;

    (void)instance; (void)cbtype;

    req->ndocs_pending--;
    dreq->docresp = *rg;
    dreq->ready = 1;
    IOV2PTRLEN(&dreq->docid, dreq->docresp.key, dreq->docresp.nkey);
    if (rg->rc == LCB_SUCCESS) {
        lcb_backbuf_ref(dreq->docresp.bufh);
    }

    if (req->ndocs_pending < MAX_PENDING_DOCREQ && req->htreq != NULL) {
        lcb_htreq_resume(req->htreq);
        schedule_docreqs(req);
    }

    invoke_pending_docreq(req);
    unref_request(req);
}

static void
mk_docreq_iov(lcbview_DOCREQ *dr,
    const lcb_IOV *src, lcb_IOV *dst, const char *buf)
{
    dst->iov_len = src->iov_len;
    if (dst->iov_len != 0) {
        size_t diff = ((const char *)src->iov_base) - buf;
        dst->iov_base = dr->rowbuf + diff;
    }
}

static void
row_callback(lcbvrow_PARSER *parser, const lcbvrow_ROW *datum)
{
    lcbview_REQUEST *req = parser->data;
    if (datum->type == LCB_VRESP_ROW) {
        if (!req->no_parse_rows) {
            lcbvrow_parse_row(req->parser, (lcbvrow_ROW*)datum);
        }

        if (req->include_docs && datum->docid.iov_len) {
            lcbview_DOCREQ *dreq = calloc(1, sizeof(*dreq));
            const char *orig = datum->row.iov_base;

            dreq->parent = req;
            dreq->rowbuf = malloc(datum->row.iov_len);
            memcpy(dreq->rowbuf, datum->row.iov_base, datum->row.iov_len);

            mk_docreq_iov(dreq, &datum->key, &dreq->key, orig);
            mk_docreq_iov(dreq, &datum->value, &dreq->value, orig);
            mk_docreq_iov(dreq, &datum->docid, &dreq->docid, orig);
            sllist_append(&req->pending_gets, &dreq->slnode);

        } else {
            lcb_RESPVIEWQUERY resp = { 0 };
            if (req->no_parse_rows) {
                IOV2PTRLEN(&datum->row, resp.value, resp.nvalue);
            } else {
                IOV2PTRLEN(&datum->key, resp.key, resp.nkey);
                IOV2PTRLEN(&datum->docid, resp.docid, resp.ndocid);
                IOV2PTRLEN(&datum->value, resp.value, resp.nvalue);
            }
            resp.htresp = req->cur_htresp;
            invoke_row(req, &resp);
        }
    } else if (datum->type == LCB_VRESP_ERROR) {
        invoke_last(req, LCB_PROTOCOL_ERROR);
    } else if (datum->type == LCB_VRESP_COMPLETE) {
        /* nothing */
    }
}

static void
destroy_request(lcbview_REQUEST *req)
{
    invoke_last(req, req->lasterr);
    if (req->parser != NULL) {
        lcbvrow_free(req->parser);
    }
    if (req->htreq != NULL) {
        lcb_cancel_http_request(req->instance, req->htreq);
    }
    free(req);
}

static void
unref_request(lcbview_REQUEST *req)
{
    if (!--req->refcount) {
        destroy_request(req);
    }
}

LIBCOUCHBASE_API
lcb_error_t
lcb_view_query(lcb_t instance, const void *cookie, const lcb_CMDVIEWQUERY *cmd)
{
    lcb_string path;
    lcb_CMDHTTP htcmd = { 0 };
    lcb_error_t rc;
    lcbview_REQUEST *req = NULL;
    int include_docs = 0;
    int no_parse_rows = 0;

    if (cmd->nddoc == 0 || cmd->nview == 0 || cmd->callback == NULL) {
        return LCB_EINVAL;
    }

    htcmd.method = LCB_HTTP_METHOD_GET;
    htcmd.type = LCB_HTTP_TYPE_VIEW;
    htcmd.cmdflags = LCB_CMDHTTP_F_STREAM;

    if (cmd->cmdflags & LCB_CMDVIEWQUERY_F_INCLUDE_DOCS) {
        include_docs = 1;
    }
    if (cmd->cmdflags & LCB_CMDVIEWQUERY_F_NOROWPARSE) {
        no_parse_rows = 1;
    }

    if (include_docs && no_parse_rows) {
        return LCB_OPTIONS_CONFLICT;
    }

    lcb_string_init(&path);
    if (lcb_string_appendv(&path,
        "_design/", (size_t)-1, cmd->ddoc, cmd->nddoc,
        "/_view/", (size_t)-1, cmd->view, cmd->nview, NULL) != 0) {

        lcb_string_release(&path);
        return LCB_CLIENT_ENOMEM;
    }

    if (cmd->optstr) {
        if (cmd->noptstr > MAX_GET_URI_LENGTH) {
            htcmd.body = cmd->optstr;
            htcmd.nbody = cmd->noptstr;
            htcmd.method = LCB_HTTP_METHOD_POST;
        } else {
            if (lcb_string_appendv(&path,
                "?", (size_t)-1, cmd->optstr, cmd->noptstr, NULL) != 0) {

                lcb_string_release(&path);
                return LCB_CLIENT_ENOMEM;
            }
        }
    }

    if ( (req = calloc(1, sizeof(*req))) == NULL ||
            (req->parser = lcbvrow_create()) == NULL) {
        free(req);
        lcb_string_release(&path);
        return LCB_CLIENT_ENOMEM;
    }

    req->instance = instance;
    req->cookie = cookie;
    req->include_docs = include_docs;
    req->no_parse_rows = no_parse_rows;
    req->callback = cmd->callback;
    req->parser->callback = row_callback;
    req->parser->data = req;

    LCB_CMD_SET_KEY(&htcmd, path.base, path.nused);
    htcmd.reqhandle = &req->htreq;
    rc = lcb_http3(instance, req, &htcmd);
    lcb_string_release(&path);

    if (rc == LCB_SUCCESS) {
        lcb_htreq_setcb(req->htreq, chunk_callback);
        req->refcount++;
    } else {
        req->callback = NULL;
        destroy_request(req);
    }
    return rc;
}

LIBCOUCHBASE_API
void
lcb_view_query_initcmd(lcb_CMDVIEWQUERY *vq,
    const char *design, const char *view, const char *options,
    lcb_VIEWQUERYCALLBACK callback)
{
    vq->view = view;
    vq->nview = strlen(view);
    vq->ddoc = design;
    vq->nddoc = strlen(design);
    if (options != NULL) {
        vq->optstr = options;
        vq->noptstr = strlen(options);
    }
    vq->callback = callback;
}
