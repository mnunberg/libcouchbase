#ifndef LCB_MT_INTERNAL_H
#define LCB_MT_INTERNAL_H

#include <libcouchbase/mt.h>
#include <libcouchbase/pktfwd.h>
#include <pthread.h>
#include <sched.h>
#include "internal.h"
#include "sllist.h"
#ifdef __APPLE__
#include <libkern/OSAtomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


#if 0
#define LCBMT_APILOCK_SPINLOCK
#endif

#ifdef LCBMT_APILOCK_SPINLOCK
#ifndef __APPLE__
typedef pthread_spinlock_t lcbmt_APILOCK;
#define LCBMT_APILOCK_INIT(v) pthread_spin_init(v, 0)
#define LCBMT_APILOCK_DESTROY(v) pthread_spin_destroy(v)
#define LCBMT_APILOCK_ACQUIRE(v) pthread_spin_lock(v)
#define LCBMT_APILOCK_RELEASE(v) pthread_spin_unlock(v)
#else
typedef OSSpinLock lcbmt_APILOCK;
#define LCBMT_APILOCK_INIT(v)
#define LCBMT_APILOCK_DESTROY(v)
#define LCBMT_APILOCK_ACQUIRE(v) OSSpinLockLock(v)
#define LCBMT_APILOCK_RELEASE(v) OSSpinLockUnlock(v)
#endif
#else
typedef pthread_mutex_t lcbmt_APILOCK;
#define LCBMT_APILOCK_INIT(v) pthread_mutex_init(v, NULL)
#define LCBMT_APILOCK_DESTROY(v) pthread_mutex_destroy(v)
#define LCBMT_APILOCK_ACQUIRE(v) pthread_mutex_lock(v)
#define LCBMT_APILOCK_RELEASE(v) pthread_mutex_unlock(v)
#endif

typedef struct {
    lcb_clist_t avail;
    lcb_list_t released;
    pthread_mutex_t lock;
} lcbmt_PENDCACHE;

typedef struct {
    /** The underlying instance */
    lcb_t instance;

    /**The API lock of the instance. This should be locked whenever
     * an API call is being made, or whenever an API call from a different
     * thread is prohibited */
    lcbmt_APILOCK apilock;

    /**This is used for wrapping the existing API. In this case the key
     * contains an implicit per-instance/per-thread wait context*/
    pthread_key_t thrkey;

    lcbmt_PENDCACHE pendcache;

    /**Callback table which contains the user-defined handlers for operations*/
    struct lcb_callback_st callbacks;

    /**Bootstrap wait context. This is only used during lcbmt_connect()*/
    lcbmt_WAITHANDLE bs_ctx;
} lcbmt_CTX;

/**This structure is queued back into the relevant wait context when a response
 * has arrived.
 *
 * Currently we copy out the key and the base structure; the value remains
 * reserved via the rdb buffer system - so we don't have to copy large payloads
 * accross threads; increasing efficiency.
 */
typedef struct {
    lcb_list_t llnode;
    sllist_node slnode;

    lcb_CALLBACKTYPE type;
    lcbmt_CTX *parent;
    int used;

    union {
        lcb_RESPBASE base;
        lcb_RESPGET get;
        lcb_RESPSTORE store;
        lcb_RESPTOUCH touch;
        lcb_RESPREMOVE remove;
        lcb_RESPUNLOCK unlock;
        lcb_RESPENDURE endure;
        lcb_RESPSERVERBASE sbase;
        lcb_RESPCOUNTER counter;
        lcb_RESPOBSERVE observe;
        lcb_RESPSTATS stats;
        lcb_RESPVERBOSITY verbosity;
        lcb_RESPMCVERSION version;
        lcb_RESPFLUSH flush;
    } u;
    char *reskey;
} lcbmt_PENDING;

#define LCBMT_RESKEY_SIZE 150

typedef struct lcbmt_WSCOPE {
    /** The parent context */
    lcbmt_CTX *parent;
    unsigned flags;
    pthread_cond_t cond;
    pthread_mutex_t mutex;

    /**How many commands does this context have remaining? When this hits
     * 0 the requesting thread is unblocked */
    unsigned remaining;

    /** List of current PENDING structures that have a response */
    sllist_root pending;

    /** Linked list of pending available PENDING structures */
    lcb_clist_t pavail;


    /**Associated cookie structure (passed to operation callback) */
    void *cookie;

    /** Only used for implicit contexts, node in the list of current contexts */
    lcb_list_t llnode;
} lcbmt_WSCOPE;

/** Per-thread implicit structure */
typedef struct {
    pthread_t id; /**<The thread ID to which this object belongs to */
    lcb_list_t avail; /**< List of available wait handles */
    lcb_list_t active; /**<List of wait handles that have remaining items */
    lcbmt_WAITHANDLE cur_wh; /**<Current wait handle being process in I/O thread */
    lcb_t instance;
} lcbmt_IMPLICIT;

lcb_error_t
lcbmt_init_connect(lcb_t instance);

void
lcbmt_implicit_dtor(void *arg);

#define LCBMT_WCTX_FROM_LL(ll) LCB_LIST_ITEM(ll, lcbmt_WSCOPE, llnode)
#define LCBMT_CTX_FROM_INSTANCE(instance) ((lcbmt_CTX*)(instance)->thrctx)

#ifdef __cplusplus
}
#endif
#endif
