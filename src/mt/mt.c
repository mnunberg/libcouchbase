#include "mt-internal.h"
#include "sllist-inl.h"
#include <unistd.h>

static void dispatch_generic(lcb_t instance, int cbtype, const lcb_RESPBASE *resp);
static lcbmt_PENDING * alloc_pending_res(lcbmt_CTX *, lcbmt_WAITHANDLE);
static void release_pending_res(lcbmt_PENDING *req);

LIBCOUCHBASE_API
lcb_error_t
lcbmt_create(lcb_t *instance, const struct lcb_create_st *options)
{
    lcb_error_t err;
    struct lcb_create_io_ops_st io_cropts = { 0 };
    struct lcb_create_st myopts = { 0 };
    lcb_io_opt_t io;
    lcb_t obj;

    lcbmt_CTX *ctx;

    if (options) {
        myopts.version = options->version;
        if (options->version == 0) {
            myopts.v.v0 = options->v.v0;
        } else if (options->version == 1) {
            myopts.v.v1 = options->v.v1;
        } else if (options->version == 2) {
            myopts.v.v2 = options->v.v2;
        } else if (options->version == 3) {
            myopts.v.v3 = options->v.v3;
        } else {
            return LCB_EINVAL;
        }
    }

    if (myopts.v.v0.io) {
        return LCB_EINVAL;
    }

    ctx = calloc(1, sizeof(*ctx));

    pthread_mutex_init(&ctx->pendcache.lock, NULL);
    LCBMT_APILOCK_INIT(&ctx->apilock);
    LCBMT_APILOCK_ACQUIRE(&ctx->apilock);
    pthread_key_create(&ctx->thrkey, lcbmt_implicit_dtor);

    lcb_clist_init(&ctx->pendcache.avail);
    lcb_list_init(&ctx->pendcache.released);

    io_cropts.version = 0;
    io_cropts.v.v0.type = LCB_IO_OPS_MT;
    io_cropts.v.v0.cookie = &ctx->apilock;
    err = lcb_create_io_ops(&io, &io_cropts);

    if (err != LCB_SUCCESS) {
        LCBMT_APILOCK_DESTROY(&ctx->apilock);
        free(ctx);
        return err;
    }

    myopts.v.v0.io = io;
    err = lcb_create(instance, &myopts);
    if (err != LCB_SUCCESS) {
        LCBMT_APILOCK_DESTROY(&ctx->apilock);
        free(ctx);
        lcb_destroy_io_ops(io);
        return err;
    }

    obj = *instance;
    ctx->instance = obj;
    ctx->callbacks = obj->callbacks;
    obj->callbacks.v3callbacks[LCB_CALLBACK_DEFAULT] = dispatch_generic;
    obj->thrctx = ctx;

    LCBMT_APILOCK_RELEASE(&ctx->apilock);

    return LCB_SUCCESS;
}

/* Real bootstrap callback. We invoke the user defined events and signal
 * to the connect wait handle here */
static void
bs_callback(lcb_t instance, lcb_error_t err)
{
    lcbmt_CTX *ctx = instance->thrctx;
    pthread_mutex_lock(&ctx->bs_ctx->mutex);
    ctx->bs_ctx->remaining = 0;

    /* Invoke the various connect callbacks.. */
    ctx->callbacks.bootstrap(instance, err);
    if (err == LCB_SUCCESS){
        ctx->callbacks.configuration(instance, LCB_CONFIGURATION_CHANGED);
    } else {
        ctx->callbacks.error(instance, err, "");
    }

    pthread_cond_signal(&ctx->bs_ctx->cond);
    pthread_mutex_unlock(&ctx->bs_ctx->mutex);
    (void)err;
}

lcb_error_t
lcbmt_init_connect(lcb_t instance)
{
    lcbmt_CTX *ctx = LCBMT_CTX_FROM_INSTANCE(instance);

    /* Noop the configuration callback. This will be called in any event by
     * the bootstrap callback which has proper wait semantics */
    ctx->callbacks.configuration = instance->callbacks.configuration;
    ctx->callbacks.bootstrap = instance->callbacks.bootstrap;
    ctx->callbacks.error = instance->callbacks.error;

    lcb_set_bootstrap_callback(instance, bs_callback);

    ctx->bs_ctx = lcbmt_wh_create(instance);
    lcbmt_wh_prepare(ctx->bs_ctx, 0);
    pthread_mutex_lock(&ctx->bs_ctx->mutex);
    return lcb_connect(instance);
}

LIBCOUCHBASE_API
lcb_error_t
lcbmt_connect(lcb_t instance)
{
    lcbmt_CTX *ctx = LCBMT_CTX_FROM_INSTANCE(instance);
    lcb_error_t err = lcbmt_init_connect(instance);

    pthread_mutex_unlock(&ctx->bs_ctx->mutex);
    if (err == LCB_SUCCESS) {
        lcbmt_wh_incr(ctx->bs_ctx);
        lcbmt_wh_wait(ctx->bs_ctx);
    } else {
        lcbmt_wh_cancel(ctx->bs_ctx);
    }
    return err;
}

/* This function is called in the context of the I/O thread. Its job is
 * to dispatch the callbacks to the user */
static void
dispatch_generic(lcb_t instance, int cbtype, const lcb_RESPBASE *resp)
{
    lcbmt_WAITHANDLE wh = (lcbmt_WAITHANDLE )resp->cookie;
    lcbmt_CTX *ctx = wh->parent;
    lcbmt_PENDING *qres;
    unsigned decrcount = 1;

    LCBMT_APILOCK_RELEASE(&ctx->apilock);
    sched_yield();

    /* Find the response */
    qres = alloc_pending_res(ctx, wh);
    qres->type = cbtype;

    #define COPY_RES(fld, type) qres->u.fld = *(const type*)resp
    #define MAYBE_COPY_KEY(fld) \
        if ((wh->flags & LCBMT_PREPARE_F_COPYKEY) && qres->u.fld.nkey) { \
            memcpy(qres->reskey, qres->u.fld.key, qres->u.fld.nkey); \
            qres->u.fld.key = qres->reskey; \
        }

    #define DO_BASIC_COPY(fld, type) \
        COPY_RES(fld, type); \
        MAYBE_COPY_KEY(fld)

    switch (cbtype) {
    case LCB_CALLBACK_GET:
    case LCB_CALLBACK_GETREPLICA:
        DO_BASIC_COPY(get, lcb_RESPGET);
        if (qres->u.get.bufh) {
            lcb_backbuf_ref(qres->u.get.bufh);
        }
        break;
    case LCB_CALLBACK_STORE:
        DO_BASIC_COPY(store, lcb_RESPSTORE);
        break;
    case LCB_CALLBACK_REMOVE:
        DO_BASIC_COPY(remove, lcb_RESPREMOVE);
        break;
    case LCB_CALLBACK_COUNTER:
        DO_BASIC_COPY(counter, lcb_RESPCOUNTER);
        break;
    case LCB_CALLBACK_TOUCH:
        DO_BASIC_COPY(touch, lcb_RESPTOUCH);
        break;
    case LCB_CALLBACK_UNLOCK:
        DO_BASIC_COPY(unlock, lcb_RESPUNLOCK);
        break;
    case LCB_CALLBACK_ENDURE:
        DO_BASIC_COPY(endure, lcb_RESPENDURE);
        break;
    case LCB_CALLBACK_OBSERVE:
        DO_BASIC_COPY(observe, lcb_RESPOBSERVE);
        if ((resp->rflags & LCB_RESP_F_FINAL) == 0) {
            decrcount = 0;
        }
        break;

    case LCB_CALLBACK_STATS:
    case LCB_CALLBACK_VERBOSITY:
    case LCB_CALLBACK_VERSIONS:
    case LCB_CALLBACK_FLUSH: {
        const lcb_RESPSERVERBASE *sb = (void *)resp;
        if (cbtype == LCB_CALLBACK_STATS) {
            COPY_RES(stats, lcb_RESPSTATS);
            const lcb_RESPSTATS *rs = (void *)resp;
            if (rs->nkey) {
                memcpy(qres->reskey, rs->key, rs->nkey);
                qres->u.stats.key = qres->reskey;
            }
        } else if (cbtype == LCB_CALLBACK_VERBOSITY) {
            COPY_RES(verbosity, lcb_RESPVERBOSITY);
        } else if (cbtype == LCB_CALLBACK_FLUSH) {
            COPY_RES(flush, lcb_RESPFLUSH);
        } else if (cbtype == LCB_CALLBACK_VERSIONS) {
            COPY_RES(version, lcb_RESPMCVERSION);
            if (qres->u.version.nversion) {
                memcpy(qres->reskey, qres->u.version.mcversion,
                    qres->u.version.nversion);
                qres->u.version.mcversion = qres->reskey;
            }
        }

        if (sb->server) {
            qres->u.sbase.server = strdup(sb->server);
        }
        if (!(resp->rflags & LCB_RESP_F_FINAL)) {
            decrcount = 0;
        }
    }
    break;
    default:
        abort();
        break;
    }

    pthread_mutex_lock(&wh->mutex);
    wh->remaining -= decrcount;
    sllist_append(&wh->pending, &qres->slnode);
    pthread_cond_signal(&wh->cond);
    pthread_mutex_unlock(&wh->mutex);

    LCBMT_APILOCK_ACQUIRE(&ctx->apilock);
    (void)instance;
}

static void
clear_pending_res(lcbmt_PENDING *res)
{
    if (res->type == LCB_CALLBACK_GET || res->type == LCB_CALLBACK_GETREPLICA) {
        if (res->u.get.bufh) {
            lcb_backbuf_unref(res->u.get.bufh);
        }

    } else if (res->type == LCB_CALLBACK_STATS) {
        free((void*)res->u.stats.value);
        free((void*)res->u.stats.server);

    } else if (res->type == LCB_CALLBACK_VERSIONS ||
            res->type == LCB_CALLBACK_VERBOSITY ||
            res->type == LCB_CALLBACK_FLUSH) {
        free((void*)res->u.sbase.server);
    }
}

static lcbmt_PENDING *
alloc_pending_res(lcbmt_CTX *ctx, lcbmt_WAITHANDLE wh)
{
    lcbmt_PENDCACHE *pc = &ctx->pendcache;
    lcb_list_t *llcur, *llnext;
    lcbmt_PENDING *res;

    pthread_mutex_lock(&pc->lock);

    LCB_LIST_SAFE_FOR(llcur, llnext, &pc->released) {
        res = LCB_LIST_ITEM(llcur, lcbmt_PENDING, llnode);
        clear_pending_res(res);
        lcb_list_delete(llcur);
        if (LCB_CLIST_SIZE(&pc->avail) < 100) {
            lcb_clist_append(&pc->avail, llcur);
        } else {
            free(res->reskey);
            free(res);
        }
    }

    llcur = lcb_clist_pop(&pc->avail);
    pthread_mutex_unlock(&pc->lock);

    if (llcur) {
        res = LCB_LIST_ITEM(llcur, lcbmt_PENDING, llnode);
    } else {
        res = malloc(sizeof(*res));
        res->reskey = malloc(LCBMT_RESKEY_SIZE);
    }
    res->parent = ctx;
    res->used = 1;
    (void)wh;
    return res;
}

static void
release_pending_res(lcbmt_PENDING *res)
{
    lcbmt_PENDCACHE *pc = &res->parent->pendcache;
    res->used = 0;
    pthread_mutex_lock(&pc->lock);
    lcb_list_append(&pc->released, &res->llnode);
    pthread_mutex_unlock(&pc->lock);
}

LIBCOUCHBASE_API
lcbmt_WAITHANDLE
lcbmt_wh_create(lcb_t instance)
{
    lcbmt_WAITHANDLE wh = calloc(1, sizeof(*wh));

    if (!wh) {
        return NULL;
    }
    wh->parent = (lcbmt_CTX *)instance->thrctx;
    pthread_mutex_init(&wh->mutex, NULL);
    pthread_cond_init(&wh->cond, NULL);
    return wh;
}

LIBCOUCHBASE_API
void
lcbmt_wh_destroy(lcbmt_WAITHANDLE wh)
{
    pthread_mutex_destroy(&wh->mutex);
    pthread_cond_destroy(&wh->cond);
    free(wh);
}

LIBCOUCHBASE_API
void
lcbmt_wh_prepare(lcbmt_WAITHANDLE wh, int flags)
{
    lcbmt_CTX *ctx = wh->parent;

    wh->remaining = 0;
    wh->flags = flags;
    if ((flags & LCBMT_PREPARE_F_NOLOCK) == 0) {
        LCBMT_APILOCK_ACQUIRE(&ctx->apilock);
    }
}

static int
invoke_pending_callbacks(lcbmt_WAITHANDLE wh)
{
    sllist_node *sl;
    lcbmt_PENDING *res;

    sl = SLLIST_FIRST(&wh->pending);
    if (!sl) {
        return 0;
    }

    sllist_remove_head(&wh->pending);
    res = SLLIST_ITEM(sl, lcbmt_PENDING, slnode);

    res->u.base.cookie = wh->cookie;
    /* Invoke the callback? */
    lcb_RESPCALLBACK cb = lcb_find_callback2(&wh->parent->callbacks, res->type);
    if (cb) {
        cb(wh->parent->instance, res->type, &res->u.base);
    }
    release_pending_res(res);
    return 1;
}

LIBCOUCHBASE_API
void
lcbmt_wh_wait(lcbmt_WAITHANDLE wh)
{
    if ((wh->flags & LCBMT_PREPARE_F_NOLOCK) == 0) {
        LCBMT_APILOCK_RELEASE(&wh->parent->apilock);
    }

    pthread_mutex_lock(&wh->mutex);
    while (wh->remaining) {
        pthread_cond_wait(&wh->cond, &wh->mutex);
        while (invoke_pending_callbacks(wh));
    }
    pthread_mutex_unlock(&wh->mutex);
    while (invoke_pending_callbacks(wh));
}

LIBCOUCHBASE_API
void
lcbmt_wh_incr(lcbmt_WAITHANDLE wh)
{
    wh->remaining++;
}

LIBCOUCHBASE_API
void
lcbmt_wh_setudata(lcbmt_WAITHANDLE wh, void *data)
{
    wh->cookie = data;
}


LIBCOUCHBASE_API
void
lcbmt_wh_cancel(lcbmt_WAITHANDLE wh)
{
    LCBMT_APILOCK_RELEASE(&wh->parent->apilock);
}

LIBCOUCHBASE_API
lcb_RESPCALLBACK
lcbmt_install_callback(lcb_t instance, int type, lcb_RESPCALLBACK cb)
{
    lcbmt_CTX *ctx = instance->thrctx;
    lcb_RESPCALLBACK ret = ctx->callbacks.v3callbacks[type];
    if (cb) {
        ctx->callbacks.v3callbacks[type] = cb;
    }
    return ret;
}

LIBCOUCHBASE_API
void
lcbmt_lock(lcb_t instance)
{
    LCBMT_APILOCK_ACQUIRE(&LCBMT_CTX_FROM_INSTANCE(instance)->apilock);
}

LIBCOUCHBASE_API
void
lcbmt_unlock(lcb_t instance)
{
    LCBMT_APILOCK_RELEASE(&LCBMT_CTX_FROM_INSTANCE(instance)->apilock);
}
