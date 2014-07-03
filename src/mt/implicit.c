#include "mt-internal.h"

/* This file contains routines used by the wrapping API */

static lcbmt_IMPLICIT * find_implicit(lcb_t instance, pthread_t thr);

/* pthread_setspecific()'s dtor */
void
lcbmt_implicit_dtor(void *arg)
{
    lcb_list_t *llcur, *llnext;
    lcbmt_IMPLICIT *ict = arg;

    LCB_LIST_SAFE_FOR(llcur, llnext, &ict->avail) {
        lcbmt_WAITHANDLE wh = LCBMT_WCTX_FROM_LL(llcur);
        lcb_list_delete(llcur);
        lcbmt_wh_destroy(wh);
    }

    LCB_LIST_SAFE_FOR(llcur, llnext, &ict->active) {
        abort();
    }
}

static lcbmt_IMPLICIT *
find_implicit(lcb_t instance, pthread_t thrid)
{
    lcbmt_CTX *ctx = LCBMT_CTX_FROM_INSTANCE(instance);
    lcbmt_IMPLICIT *ret = pthread_getspecific(ctx->thrkey);

    if (ret) {
        return ret;
    }

    ret = calloc(1, sizeof(*ret));
    ret->id = thrid;
    ret->instance = instance;
    lcb_list_init(&ret->avail);
    lcb_list_init(&ret->active);
    pthread_setspecific(ctx->thrkey, ret);
    return ret;
}

#define GET_CUR_ICT(obj) find_implicit(obj, pthread_self())

static lcbmt_WAITHANDLE
make_implicit_wh(lcbmt_IMPLICIT *ict, const void *cookie)
{
    lcb_list_t *ll, *ll_next;
    lcbmt_WAITHANDLE wh;

    if (LCB_LIST_IS_EMPTY(&ict->avail) && LCB_LIST_IS_EMPTY(&ict->active)) {
        wh = lcbmt_wh_create(ict->instance);
        wh->flags = LCBMT_PREPARE_F_COPYKEY|LCBMT_PREPARE_F_NOLOCK;
        lcb_list_append(&ict->avail, &wh->llnode);
        goto GT_CHECKAVAIL;
    }

    LCB_LIST_SAFE_FOR(ll, ll_next, &ict->active) {
        wh = LCBMT_WCTX_FROM_LL(ll);
        if (wh->cookie == cookie) {
            lcb_list_delete(&wh->llnode);
            /* No locking required. Already active */
            return wh;
        }
    }

    GT_CHECKAVAIL:
    ll = lcb_list_pop(&ict->avail);
    wh = LCBMT_WCTX_FROM_LL(ll);
    lcb_list_append(&ict->active, &wh->llnode);
    pthread_mutex_lock(&wh->mutex);
    return wh;
}

typedef struct {
    lcbmt_WAITHANDLE wh;
    unsigned last_remaining;
    lcbmt_IMPLICIT *ict;
} WRAPVARS;


static void
Wwrap_init_common(lcb_t instance, const void *cookie, lcb_SIZE n,
    WRAPVARS *wv)
{
    wv->ict = GET_CUR_ICT(instance);
    wv->wh = make_implicit_wh(wv->ict, cookie);
    wv->last_remaining = wv->wh->remaining;
    wv->wh->remaining += n;
    wv->wh->cookie = (void *)cookie;
}

static void
Wwrap_cleanup_common(WRAPVARS *wv, lcb_error_t err)
{
    if (err != LCB_SUCCESS && wv->last_remaining == 0) {
        /* release back to pool */
        lcb_list_append(&wv->ict->avail, &wv->wh->llnode);
    }
}

typedef lcb_error_t
        (*lcbmt__schedfunc_v2) (lcb_t, const void*, lcb_SIZE, const void * const *);
typedef lcb_error_t
        (*lcbmt__schedfunc_v3) (lcb_t, const void *, const void *);

#include "wrap-targets.h"

LIBCOUCHBASE_API
lcb_error_t
lcbmt__Wwrap2(lcb_t instance, const void *cookie, lcb_SIZE n,
    const void * const * cmds, const char *name)
{
    lcb_error_t err;
    WRAPVARS wv = { NULL };
    lcbmt__schedfunc_v2 schedfunc = find_wrap_target2(name);

    Wwrap_init_common(instance, cookie, n, &wv);

    lcbmt_lock(instance);
    err = schedfunc(instance, wv.wh, n, cmds);
    lcbmt_unlock(instance);

    Wwrap_cleanup_common(&wv, err);
    return err;
}

LIBCOUCHBASE_API
lcb_error_t
lcbmt__Wwrap3(lcb_t instance, const void *cookie, const void *cmd, const char *name)
{
    lcb_error_t err;
    WRAPVARS wv = { NULL };
    lcbmt__schedfunc_v3 schedfn = find_wrap_target3(name);

    Wwrap_init_common(instance, cookie, 1, &wv);
    err = schedfn(instance, wv.wh, cmd);
    Wwrap_cleanup_common(&wv, err);
    return err;
}

LIBCOUCHBASE_API
void
lcbmt__Wbail3(lcb_t instance)
{
    abort(); /* TODO: */
    (void)instance;
}

LIBCOUCHBASE_API
lcb_error_t
lcbmt__Wconnect(lcb_t instance)
{
    lcbmt_CTX *ctx;
    lcbmt_IMPLICIT *ict;
    lcb_error_t err;

    ctx = LCBMT_CTX_FROM_INSTANCE(instance);
    ict = GET_CUR_ICT(instance);
    err = lcbmt_init_connect(instance);

    if (err == LCB_SUCCESS) {
        lcbmt_wh_incr(ctx->bs_ctx);
        lcb_list_append(&ict->active, &ctx->bs_ctx->llnode);
    } else {
        pthread_mutex_unlock(&ctx->bs_ctx->mutex);
    }
    return err;
}

LIBCOUCHBASE_API
lcb_error_t
lcbmt__Wwait(lcb_t instance)
{
    lcb_list_t *ll, *llnext;
    lcbmt_CTX *ctx;
    lcbmt_IMPLICIT *ict;

    ctx = LCBMT_CTX_FROM_INSTANCE(instance);
    ict = GET_CUR_ICT(instance);

    LCB_LIST_FOR(ll, &ict->active) {
        lcbmt_WSCOPE *wh = LCBMT_WCTX_FROM_LL(ll);
        pthread_mutex_unlock(&wh->mutex);
    }

    LCB_LIST_SAFE_FOR(ll, llnext, &ict->active) {
        lcbmt_WAITHANDLE wh = LCBMT_WCTX_FROM_LL(ll);

        lcbmt_wh_wait(wh);

        /* Check that the wh doesn't have anything remaining. The mutex
         * could be prematurely unlocked if lcb_breakout() is called */
        if (wh->remaining == 0) {
            lcb_list_delete(ll);
            if (wh == ctx->bs_ctx) {
                lcbmt_wh_destroy(wh);
            } else {
                lcb_list_append(&ict->avail, ll);
            }
        }
    }
    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
void
lcbmt__Wenter3(lcb_t instance)
{
    lcbmt_lock(instance);
    lcb_sched_enter(instance);
}
LIBCOUCHBASE_API
void
lcbmt__Wleave3(lcb_t instance)
{
    lcb_sched_leave(instance);
    lcbmt_unlock(instance);
}
