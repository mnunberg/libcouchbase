#ifndef LCB_MT_H
#define LCB_MT_H

#include <libcouchbase/couchbase.h>
#include <libcouchbase/api3.h>
/**
 * @file
 * _EXPERIMENTAL_ Multi-Thread extensions for libcouchbase
 *
 * These functions provide equivalents to the standard functionality within
 * the library, except that they allow multiple requests to be scheduled
 * concurrently from within the library - allowing far greater network
 * utilization without creating many instances.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup lcb_mt_api Multi Thread API
 *
 * @brief Efficient access from multiple threads
 * @details
 * This API provides a way to utilize a single library handle from multiple
 * threads. The benefits of this API as opposed to simply locking an
 * instance while performing and waiting for an operation is that other
 * operations are able to be scheduled in sequence, allowing pipelining of
 * pending operations and responses to the handle.
 *
 * This API is still very experimental and currently only works on Unix-like
 * systems (i.e. _pthreads_).
 */

/**
 * @addtogroup lcb_mt_api
 * @{
 */

/**
 * @name Creating an Instance
 *
 * The APIs here are similar to the normal APIs used for creation. First you
 * create the instance itself via lcbmt_create() and then wait for it to
 * connect via lcbmt_connect().
 *
 * @{
 */

/**
 * @volatile
 * Create a new MT-enabled instance of libcouchbase. This functions much like
 * the lcb_create() function.
 *
 * Note that the lcb_create_st3::io _must_ be NULL. Multi threading works by
 * employing a customized event loop.
 *
 * @param instance the instance to create
 * @param options the options for creation
 * @return LCB_SUCCESS on success, error code otherwise
 * @see lcb_create()
 */
LIBCOUCHBASE_API
lcb_error_t
lcbmt_create(lcb_t *instance, const struct lcb_create_st *options);

/**
 * @volatile
 * Connect an MT-enabled instance of the library. This function will block
 * until the instance has been connected.
 * @param instance the instance to connect.
 */
LIBCOUCHBASE_API
lcb_error_t
lcbmt_connect(lcb_t instance);
/**@}*/

/**
 * @name Scheduling Operations
 *
 * @details
 * Operation scheduling in the MT-enabled mode requires several additional steps.
 * This is because you must associate your current thread with the operations
 * being scheduled in order that the response can be received properly.
 *
 * Operation callbacks may be installed using the lcbmt_install_callback()
 * function (do **not** use lcb_install_callback3()). The callback will be
 * invoked in a _different_ thread than the one actually performing the
 * scheduling.
 *
 * If your operation threads (i.e. the ones scheduling the operations) need to
 * be blocked until the scheduled operations have been completed you will need
 * a way to signal from the callback thread over to the operation thread
 * that all the operations have been completed. Functions like lcb_wait() and
 * lcb_breakout() will **not** work as they assume that the event loop is being
 * invoked from within the operation thread.
 *
 * The lcbmt_pWAITCTX handle provides a convenient means of scheduling operations
 * and then waiting for them. This is done through:
 *
 * 1. Allocating the handle (lcbmt_wh_create())
 * 2. Preparing the handle (lcbmt_wh_prepare()) - which will also lock the
 *    associated instance
 * 3. Associating the _cookie_ with the handle (lcbmt_wh_setudata()).
 * 4. Scheduling operations (e.g. lcb_get3(), lcb_store3()). You must pass
 *    the lcbmt_pWAITCTX handle as the cookie argument. The callback will
 *    receive the cookie set with the lcbmt_wh_setudata() call.
 * 5. Incrementing the handle count for each operation scheduled
 *    (lcbmt_wh_incr()).
 * 6. Waiting for all the operations to complete (lcbmt_wh_wait()).
 *
 * @{
 */

typedef struct lcbmt_WSCOPE* lcbmt_WAITHANDLE;

/**@volatile
 * Create a new wait context. A wait context allows your thread to block until
 * all operations associated with the context have completed.
 *
 * There needs to be only a single wait context per thread.
 * @param instance the instance which will be used to perform operations for
 * this context.
 */
LIBCOUCHBASE_API
lcbmt_WAITHANDLE
lcbmt_wh_create(lcb_t instance);

#define LCBMT_PREPARE_F_NOLOCK 0x01 /* don't lock the instance */
#define LCBMT_PREPARE_F_COPYKEY 0x02 /* Return key in response */

/**@volatile
 * Prepare the wait handle and the associated instance for scheduling. This will
 * clear the remaining counters and lock the underlying instance - so ensure
 * not to call this function when the instance is _already_ locked.
 * @param wh the wait handle to prepare
 * @param flags
 */
LIBCOUCHBASE_API
void
lcbmt_wh_prepare(lcbmt_WAITHANDLE wh, int flags);

/**@volatile
 * Indicate that a new operation has been scheduled to the lcb_t object and
 * increment the number of completed operations the lcbmt_wh_wait() function
 * should wait for.
 * @param wh the wait handle
 *
 * @note An operation consists of _one_ scheduling call, regardless of how many
 * callback invocations are to be received for this command. The library will
 * ensure that the counter is decremented only once all the callbacks for a
 * specific operation (in the case where a single operation may receive multiple
 * callbacks) have been invoked.
 */
LIBCOUCHBASE_API
void
lcbmt_wh_incr(lcbmt_WAITHANDLE wh);

/**@volatile
 * Reset the wait handle. This should only be used in case a scheduling API
 * fails _and_ lcb_sched_leave() is not called (i.e. lcb_sched_fail() is called).
 *
 * @param wh the wait handle
 */
LIBCOUCHBASE_API
void
lcbmt_wh_cancel(lcbmt_WAITHANDLE wh);

/**@volatile
 * Set the cookie pointer to be passed in the callback
 * @param wh the wait handle
 * @param data the pointer to be passed as the lcb_RESPBASE::cookie field
 */
LIBCOUCHBASE_API
void
lcbmt_wh_setudata(lcbmt_WAITHANDLE wh, void *data);

/**@volatile
 * Block the current thread until the number of operations associated with the
 * wait handle have been completed.
 * @param wh the handle on which to wait. The handle should have at least
 * one operation scheduled with it, otherwise the calling thread may block
 * indefinitely.
 */
LIBCOUCHBASE_API
void
lcbmt_wh_wait(lcbmt_WAITHANDLE wh);

LIBCOUCHBASE_API
void
lcbmt_wh_destroy(lcbmt_WAITHANDLE ctx);

/**@volatile
 * Install an operation callback to be invoked when an operation is complete.
 * The callback will be invoked from the I/O thread, so be sure to keep any
 * processing to a minimum and copy over the relevant data back to the
 * calling thread.
 * @param instance the instance
 * @param type the type of callback (operation) to handle
 * @param cb the callback to install (specify `NULL` to only retrurn the current
 * callback)
 * @return the old (and/or current) callback
 */
LIBCOUCHBASE_API
lcb_RESPCALLBACK
lcbmt_install_callback(lcb_t instance, int type, lcb_RESPCALLBACK cb);

/**@}*/

LIBCOUCHBASE_API
void
lcbmt_sched_leave(lcb_t instance);

LIBCOUCHBASE_API
void
lcbmt_lock(lcb_t instance);

LIBCOUCHBASE_API
void
lcbmt_unlock(lcb_t instance);
/**@}*/

LIBCOUCHBASE_API
lcb_error_t
lcbmt__Wconnect(lcb_t instance);

LIBCOUCHBASE_API
lcb_error_t
lcbmt__Wwait(lcb_t instance);

LIBCOUCHBASE_API
void
lcbmt__Wbail3(lcb_t instance);

LIBCOUCHBASE_API
void
lcbmt__Wbreakout(lcb_t instance);

LIBCOUCHBASE_API
lcb_error_t
lcbmt__Wwrap2(lcb_t instance, const void *cookie,
    lcb_SIZE n, const void * const * cmds,
    const char *tgtname);

LIBCOUCHBASE_API
lcb_error_t
lcbmt__Wwrap3(lcb_t instance, const void *cookie, const void *cmd,
    const char *tgtname);

LIBCOUCHBASE_API
void
lcbmt__Wenter3(lcb_t instance);

LIBCOUCHBASE_API
void
lcbmt__Wleave3(lcb_t instance);

#ifdef __cplusplus
}
#endif
#endif
