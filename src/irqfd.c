#include <lcbio/lcbio.h>
#include <lcbio/iotable.h>
#include <libcouchbase/irq.h>
#include "internal.h"


typedef struct lcb_IRQFD_s {
    lcb_socket_t fd_req; /* IRQ Requester */
    lcbio_pCTX ctx_lsn; /* IRQ Listener */
    lcb_t instance; /* Pointer to instance */
    const void *cookie;
    lcb_IRQCALLBACK callback; /* Callback to invoke when signalled */
} lcb_IRQFD;

#ifdef _POSIX_VERSION
static void
cb_err(lcbio_pCTX ctx, lcb_error_t err)
{
    lcb_IRQFD *irq = lcbio_ctx_data(ctx);
    if (irq->callback) {
        irq->callback(irq->instance, err, irq->cookie);
    }
    if (ctx->sock != NULL) {
        lcbio_shutdown(ctx->sock);
    }
}
static void
cb_read(lcbio_pCTX ctx, unsigned nr)
{
    lcb_IRQFD *irq = lcbio_ctx_data(ctx);
    lcbio_CTXRDITER iter;
    LCBIO_CTX_ITERFOR(ctx, &iter, nr) {
        nr -= lcbio_ctx_risize(&iter);
    }

    /* Now invoke the callback */
    if (irq->callback) {
        irq->callback(irq->instance, LCB_SUCCESS, irq->cookie);
    }
    lcbio_ctx_rwant(ctx, 1);
}

LIBCOUCHBASE_API
lcb_error_t
lcb_irq_init(lcb_t instance, lcb_IRQCALLBACK callback, const void *cookie)
{
    lcb_IRQFD *irq = NULL;
    lcbio_SOCKET *sockobj = NULL;
    int socks[2];
    int rv;
    int ii;
    lcbio_CTXPROCS procs = { NULL };

    if (instance->iotable->model != LCB_IOMODEL_EVENT) {
        return LCB_CLIENT_FEATURE_UNAVAILABLE;
    }

    rv = socketpair(AF_UNIX, SOCK_STREAM, 0, socks);
    if (rv != 0) {
        /* Determine what the error was */
        return lcbio_mklcberr(errno, instance->settings);
    }

    /* Make the socket pair non-blocking */
    for (ii = 0; ii < 2; ii++) {
        int flags = fcntl(socks[ii], F_GETFL);
        if (flags == -1 || fcntl(socks[ii], F_SETFL, flags|O_NONBLOCK) == -1) {
            goto GT_EARLY_ERROR;
        }
    }

    irq = calloc(1, sizeof(*irq));
    if (!irq) {
        GT_EARLY_ERROR:
        free(irq);
        close(socks[0]); close(socks[1]);
        return LCB_CLIENT_ENOMEM;
    }

    instance->irqfd = irq;
    irq->callback = callback;
    irq->cookie = cookie;
    irq->fd_req = socks[0];
    irq->instance = instance;

    sockobj = lcbio_wrap_fd(instance->iotable, instance->settings, socks[1]);
    if (sockobj == NULL) {
        lcb_irq_cleanup(instance);
        return LCB_CLIENT_ENOMEM;
    }

    procs.cb_err = cb_err;
    procs.cb_read = cb_read;
    irq->ctx_lsn = lcbio_ctx_new(sockobj, irq, &procs);
    lcbio_unref(sockobj);
    if (!irq->ctx_lsn) {
        lcb_irq_cleanup(instance);
        return LCB_CLIENT_ENOMEM;
    }
    return LCB_SUCCESS;
}

LIBCOUCHBASE_API
lcb_error_t
lcb_irq_signal(lcb_t instance)
{
    volatile lcb_IRQFD *irq = instance->irqfd;
    int rv;
    static char dummy = '!';

    if (!irq) {
        return LCB_CLIENT_ETMPFAIL;
    }

    if (irq->fd_req == INVALID_SOCKET) {
        return LCB_NETWORK_ERROR;
    }

    GT_AGAIN:
    rv = send(irq->fd_req, &dummy, 1, 0);
    if (rv == 1) {
        return LCB_SUCCESS;
    } else if (rv == 0) {
        /* Is this possible? */
        return LCB_NETWORK_ERROR;
    } else {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return LCB_BUSY;
        } else if (errno == EINTR) {
            goto GT_AGAIN;
        } else {
            return lcbio_mklcberr(errno, instance->settings);
        }
    }
}

LIBCOUCHBASE_API
void
lcb_irq_cleanup(lcb_t instance)
{
    lcb_IRQFD *irq = instance->irqfd;
    if (!irq) {
        return;
    }
    if (irq->ctx_lsn) {
        lcbio_shutdown(irq->ctx_lsn->sock);
        lcbio_ctx_close(irq->ctx_lsn, NULL, NULL);
    }
    close(irq->fd_req);
    free(irq);
    instance->irqfd = NULL;
}

#else
LIBCOUCHBASE_API lcb_error_t lcb_irq_init(lcb_t instance, lcb_IRQCALLBACK cb, void *cookie) {
    (void)instance; (void)cookie; (void)cb;
    return LCB_CLIENT_FEATURE_UNAVAILABLE;
}
LIBCOUCHBASE_API lcb_error_t lcb_irq_signal(lcb_t instance) {
    (void)instance;
    return LCB_CLIENT_FEATURE_UNAVAILABLE;
}
LIBCOUCHBASE_API void lcb_irq_cleanup(lcb_t instance)
#endif
