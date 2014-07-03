/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2012 Couchbase, Inc.
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
#include "mt-internal.h"
#include "mt_io_opts.h"
#include <libcouchbase/plugins/io/bsdio-inl.c>
#include <pthread.h>

typedef struct mt_EVENT mt_EVENT;
struct mt_EVENT {
    lcb_list_t list;
    lcb_socket_t sock;
    short flags;
    short eflags; /* effective flags */
    void *cb_data;
    lcb_ioE_callback handler;
    mt_EVENT *next; /* for chaining active events */
};

typedef struct mt_TIMER mt_TIMER;
struct mt_TIMER {
    lcb_list_t list;
    int active;
    hrtime_t exptime;
    void *cb_data;
    lcb_ioE_callback handler;
};

typedef struct {
    volatile int active;
    pthread_mutex_t mutex;
    lcbmt_APILOCK *apilock;
    lcb_list_t events;
    lcb_list_t timers;
    pthread_t thr;
    lcb_socket_t request_fd; /* Write to this for requests */
    lcb_socket_t react_fd; /* Read to this for responses */
} mt_LOOP;

/*
 * EPIPHANY:
 * The mutex shall always be locked while the callbacks are being invoked for
 * the current thread. This allows proper streamlined access for internal
 * event invocation and scheduling commands within callbacks. External threads
 * should only be able to access the instance once select() is already in
 * place (and thus other threads cannot modify it)
 */
#define LOOP_FROM_IOPS(iops) (iops)->v.v0.cookie
#define WITHIN_LOOP_THREAD(loop) (pthread_equal(pthread_self(), loop->thr))

static void
start_update(mt_LOOP *loop)
{
    if (WITHIN_LOOP_THREAD(loop)) {
        /* Scheduled from within the event loop. Must be within a callback */
        return;
    }

    /* Determine the state */
    pthread_mutex_lock(&loop->mutex);
}

static void
end_update(mt_LOOP *loop)
{
    char dummy = '\0';

    if (WITHIN_LOOP_THREAD(loop)) {
        return;
    }
    write(loop->request_fd, &dummy, 1);
    pthread_mutex_unlock(&loop->mutex);
}

static int
timer_cmp_asc(lcb_list_t *a, lcb_list_t *b)
{
    mt_TIMER *ta = LCB_LIST_ITEM(a, mt_TIMER, list);
    mt_TIMER *tb = LCB_LIST_ITEM(b, mt_TIMER, list);
    if (ta->exptime > tb->exptime) {
        return 1;
    } else if (ta->exptime < tb->exptime) {
        return -1;
    } else {
        return 0;
    }
}

static void *
mt_event_new(lcb_io_opt_t iops)
{
    mt_EVENT *ret = calloc(1, sizeof(mt_EVENT));
    (void)iops;
    return ret;
}

static int
mt_event_update(lcb_io_opt_t iops, lcb_socket_t sock, void *event, short flags,
    void *cb_data, lcb_ioE_callback handler)
{
    mt_EVENT *ev = event;
    mt_LOOP *io = LOOP_FROM_IOPS(iops);
    start_update(io);

    if (ev->flags && flags == 0) {
        lcb_list_delete(&ev->list);
    } else if (ev->flags == 0 && flags) {
        lcb_list_append(&io->events, &ev->list);
    }

    if ((ev->flags = flags)) {
        ev->sock = sock;
        ev->handler = handler;
        ev->cb_data = cb_data;
    }
    end_update(io);
    return 0;
}

static void
mt_event_cancel(lcb_io_opt_t iops, lcb_socket_t sock, void *event)
{
    mt_event_update(iops, sock, event, 0, NULL, NULL);
}

static void
mt_event_free(lcb_io_opt_t iops, void *event)
{
    mt_EVENT *ev = event;
    mt_LOOP *io = LOOP_FROM_IOPS(iops);
    mt_event_cancel(iops, 0, event);
    free(ev);
    (void)io;
}


static void *
mt_timer_new(lcb_io_opt_t iops)
{
    mt_TIMER *ret = calloc(1, sizeof(mt_TIMER));
    (void)iops;
    return ret;
}

static void
mt_timer_cancel(lcb_io_opt_t iops, void *timer)
{
    mt_TIMER *tm = timer;
    mt_LOOP *loop = LOOP_FROM_IOPS(iops);

    start_update(loop);
    if (tm->active) {
        tm->active = 0;
        lcb_list_delete(&tm->list);
    }
    end_update(loop);
    (void)iops;
}


static void
mt_timer_free(lcb_io_opt_t iops, void *timer)
{
    mt_timer_cancel(iops, timer);
    free(timer);
    (void)iops;
}

static int
mt_timer_schedule(lcb_io_opt_t iops, void *timer, lcb_U32 usec, void *cb_data,
    lcb_ioE_callback handler)
{
    mt_TIMER *tm = timer;
    mt_LOOP *cookie = iops->v.v0.cookie;

    start_update(cookie);
    lcb_assert(!tm->active);
    tm->exptime = gethrtime() + (usec * (hrtime_t)1000);
    tm->cb_data = cb_data;
    tm->handler = handler;
    tm->active = 1;
    lcb_list_add_sorted(&cookie->timers, &tm->list, timer_cmp_asc);
    end_update(cookie);

    return 0;
}

static mt_TIMER *
pop_next_timer(mt_LOOP *cookie, hrtime_t now)
{
    mt_TIMER *ret;

    if (LCB_LIST_IS_EMPTY(&cookie->timers)) {
        return NULL;
    }

    ret = LCB_LIST_ITEM(cookie->timers.next, mt_TIMER, list);
    if (ret->exptime > now) {
        return NULL;
    }
    lcb_list_shift(&cookie->timers);
    ret->active = 0;
    return ret;
}

static int
get_next_timeout(mt_LOOP *cookie, struct timeval *tmo, hrtime_t now)
{
    mt_TIMER *first;
    hrtime_t delta;

    if (LCB_LIST_IS_EMPTY(&cookie->timers)) {
        tmo->tv_sec = 0;
        tmo->tv_usec = 0;
        return 0;
    }

    first = LCB_LIST_ITEM(cookie->timers.next, mt_TIMER, list);
    if (now < first->exptime) {
        delta = first->exptime - now;
    } else {
        delta = 0;
    }


    if (delta) {
        delta /= 1000;
        tmo->tv_sec = (long)(delta / 1000000);
        tmo->tv_usec = delta % 1000000;
    } else {
        tmo->tv_sec = 0;
        tmo->tv_usec = 0;
    }
    return 1;
}

static void
process_events(struct lcb_io_opt_st *iops, fd_set *rfd, fd_set *wfd, fd_set *efd)
{
    mt_EVENT *active = NULL;
    lcb_list_t *ii;
    mt_LOOP *io = LOOP_FROM_IOPS(iops);
    mt_EVENT *ev = NULL;

    LCB_LIST_FOR(ii, &io->events) {
        ev = LCB_LIST_ITEM(ii, mt_EVENT, list);
        if (ev->flags != 0) {
            ev->eflags = 0;
            if (FD_ISSET(ev->sock, rfd)) {
                ev->eflags |= LCB_READ_EVENT;
            }
            if (FD_ISSET(ev->sock, wfd)) {
                ev->eflags |= LCB_WRITE_EVENT;
            }
            if (FD_ISSET(ev->sock, efd)) {
                /** It should error */
                ev->eflags = LCB_ERROR_EVENT | LCB_RW_EVENT;
            }
            if (ev->eflags != 0) {
                ev->next = active;
                active = ev;
            }
        }
    }
    ev = active;
    while (ev) {
        mt_EVENT *p = ev->next;
        ev->handler(ev->sock, ev->eflags, ev->cb_data);
        ev = p;
    }
}

static void
loop_once(struct lcb_io_opt_st *iops)
{
    mt_LOOP *io = iops->v.v0.cookie;
    mt_EVENT *ev;
    lcb_list_t *ii;
    fd_set readfds, writefds, exceptfds;
    struct timeval tmo, *t;
    int ret;
    int nevents = 0;
    int has_timers;
    lcb_socket_t fdmax = 0;
    hrtime_t now;

    t = NULL;
    now = gethrtime();


    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    FD_SET(io->react_fd, &readfds);
    fdmax = io->react_fd;

    pthread_mutex_lock(&io->mutex); {
        LCB_LIST_FOR(ii, &io->events) {
            ev = LCB_LIST_ITEM(ii, mt_EVENT, list);
            if (ev->flags != 0) {
                if (ev->flags & LCB_READ_EVENT) {
                    FD_SET(ev->sock, &readfds);
                }

                if (ev->flags & LCB_WRITE_EVENT) {
                    FD_SET(ev->sock, &writefds);
                }

                FD_SET(ev->sock, &exceptfds);
                if (ev->sock > fdmax) {
                    fdmax = ev->sock;
                }
                ++nevents;
            }
        }

        has_timers = get_next_timeout(io, &tmo, now);
        if (has_timers) {
            t = &tmo;
        }
    }
    pthread_mutex_unlock(&io->mutex);

    GT_SELECT:
    /* XXX: Other threads are now able to issue requests here! */
    ret = select(fdmax + 1, &readfds, &writefds, &exceptfds, t);
    if (ret == -1) {
        assert(errno == EINTR);
        goto GT_SELECT;
    }

    if (FD_ISSET(io->react_fd, &readfds)) {
        /* Swallow them all */
        char dummy[4096];
        while (read(io->react_fd, &dummy, sizeof dummy) > 0) {

        }
    }

    now = gethrtime();
    /* Note that the API mutex covers the I/O mutex, so it is not required
     * to explicitly lock the I/O mutex as well.*/
    LCBMT_APILOCK_ACQUIRE(io->apilock);
    {
        if (has_timers) {
            mt_TIMER *tm;
            while ((tm = pop_next_timer(io, now))) {
                tm->handler(-1, 0, tm->cb_data);
            }
        }

        if (ret) {
            process_events(iops, &readfds, &writefds, &exceptfds);
        }
    }
    LCBMT_APILOCK_RELEASE(io->apilock);
}

static void *
pthr_runloop(void *arg)
{
    struct lcb_io_opt_st *iops = arg;
    mt_LOOP *io = iops->v.v0.cookie;

    do {
        loop_once(iops);
    } while (io->active);
    return NULL;
}

static void mt_run_loop(struct lcb_io_opt_st *iops) { (void)iops; }
static void mt_stop_loop(struct lcb_io_opt_st *iops) { (void)iops; }


static void
mt_destroy_iops(struct lcb_io_opt_st *iops)
{
    mt_LOOP *io = iops->v.v0.cookie;
    lcb_list_t *nn, *ii;
    mt_EVENT *ev;
    mt_TIMER *tm;
    void *retval = NULL;

    io->active = 0;
    write(io->request_fd, &retval, sizeof retval);
    pthread_join(io->thr, &retval);

    assert(io->active == 0);
    LCB_LIST_SAFE_FOR(ii, nn, &io->events) {
        ev = LCB_LIST_ITEM(ii, mt_EVENT, list);
        iops->v.v0.destroy_event(iops, ev);
    }
    assert(LCB_LIST_IS_EMPTY(&io->events));
    LCB_LIST_SAFE_FOR(ii, nn, &io->timers) {
        tm = LCB_LIST_ITEM(ii, mt_TIMER, list);
        iops->v.v0.destroy_timer(iops, tm);
    }
    assert(LCB_LIST_IS_EMPTY(&io->timers));
    pthread_mutex_destroy(&io->mutex);

    close(io->react_fd);
    close(io->request_fd);
    free(io);
    free(iops);
}

static lcb_SSIZE
sendv_wrap(lcb_io_opt_t iops, lcb_socket_t sock, struct lcb_iovec_st *iov,
    lcb_SIZE niov)
{
    mt_LOOP *loop = iops->v.v0.cookie;
    lcb_SSIZE rv;

    LCBMT_APILOCK_RELEASE(loop->apilock);
    sched_yield();
    rv = sendv_impl(iops, sock, iov, niov);
    LCBMT_APILOCK_ACQUIRE(loop->apilock);
    return rv;
}

static
lcb_SSIZE
recvv_wrap(lcb_io_opt_t iops, lcb_socket_t sock, struct lcb_iovec_st *iov,
    lcb_SIZE niov)
{
    mt_LOOP *loop = iops->v.v0.cookie;
    lcb_SSIZE rv;

    LCBMT_APILOCK_RELEASE(loop->apilock);
    sched_yield();
    rv = recvv_impl(iops, sock, iov, niov);
    LCBMT_APILOCK_ACQUIRE(loop->apilock);
    return rv;
}

LIBCOUCHBASE_API
lcb_error_t
lcb_create_mtio_io_opts(int version, lcb_io_opt_t *io, void *arg)
{
    lcb_io_opt_t ret;
    mt_LOOP *cookie;
    int pipes[2];
    int rv;

    if (version != 0) {
        return LCB_PLUGIN_VERSION_MISMATCH;
    }

    if (!arg) {
        return LCB_EINVAL; /* Need user mutex */
    }

    ret = calloc(1, sizeof(*ret));
    cookie = calloc(1, sizeof(*cookie));
    if (ret == NULL || cookie == NULL) {
        free(ret); free(cookie); return LCB_CLIENT_ENOMEM;
    }

    if (0 != pthread_mutex_init(&cookie->mutex, NULL)) {
        free(ret); free(cookie); return LCB_EINTERNAL;
    }

    /* Initialize the socket watcher */
    rv = pipe(pipes);
    if (rv != 0) {
        pthread_mutex_destroy(&cookie->mutex);
        free(ret); free(cookie); return LCB_EINTERNAL;
    }

    cookie->request_fd = pipes[1];
    cookie->react_fd = pipes[0];
    cookie->active = 1;
    cookie->apilock = (lcbmt_APILOCK *)arg;

    /* The FD as used within the event loop should not block */
    fcntl(cookie->react_fd, F_SETFL, fcntl(cookie->react_fd, F_GETFL)|O_NONBLOCK);

    lcb_list_init(&cookie->events);
    lcb_list_init(&cookie->timers);

    /* setup io iops! */
    ret->version = 0;
    ret->dlhandle = NULL;
    ret->destructor = mt_destroy_iops;
    /* consider that struct isn't allocated by the library,
     * `need_cleanup' flag might be set in lcb_create() */
    ret->v.v0.need_cleanup = 0;
    ret->v.v0.delete_event = mt_event_cancel;
    ret->v.v0.destroy_event = mt_event_free;
    ret->v.v0.create_event = mt_event_new;
    ret->v.v0.update_event = mt_event_update;

    ret->v.v0.delete_timer = mt_timer_cancel;
    ret->v.v0.destroy_timer = mt_timer_free;
    ret->v.v0.create_timer = mt_timer_new;
    ret->v.v0.update_timer = mt_timer_schedule;

    ret->v.v0.run_event_loop = mt_run_loop;
    ret->v.v0.stop_event_loop = mt_stop_loop;
    ret->v.v0.cookie = cookie;

    wire_lcb_bsd_impl(ret);

    /* Wrap sendv and recvv */
    ret->v.v0.sendv = sendv_wrap;
    ret->v.v0.recvv = recvv_wrap;

    *io = ret;

    pthread_create(&cookie->thr, NULL, pthr_runloop, ret);

    (void)arg;
    return LCB_SUCCESS;
}
