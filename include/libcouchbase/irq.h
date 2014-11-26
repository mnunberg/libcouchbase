#ifndef LCB_IRQ_H
#define LCB_IRQ_H

#ifndef LIBCOUCHBASE_COUCHBASE_H
#error "include <libcouchbase/couchbase.h> first!"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**@ingroup lcb-public-api
 * @defgroup lcb-irq Thread safe event loop interrupts (Unix Only)
 *
 * Sometimes you might be running a threaded application in which you might
 * want to break out of a loop before an operation has completed, essentially
 * pre-empting lcb_wait(). The following set of functions will allow you
 * to asynchronously get called from within lcb_wait(), from which you can
 * then perform other operations on the instance within a safe context.
 *
 * This functionality is available on Unix-Like systems only, and only when
 * using an event-style I/O loop (the default loop
 * implementations are event-style).
 *
 * To use this feature, initialize the IRQ system via lcb_irq_init(), in which
 * you can pass a callback and a custom pointer to be invoked whenever
 * lcb_wait() is asynchronously interrupted.
 *
 * To send an interrupt, call lcb_irq_signal(). Special about this function
 * is that it is thread safe and thus need not have an instance-level mutex.
 *
 * Internally the IRQ system is implemented as a pair of connected sockets
 * (using socketpair(2)). When the IRQ system is initialized, the library
 * will watch for events on one end of the socket, and lcb_irq_signal() will
 * write to the other end of the socket. When new data is available on the
 * signalled socket, the library will invoke the callback defined by the
 * application.
 *
 * @addtogroup lcb-irq
 * @{
 */

/** Callback to be invoked on behalf of lcb_irq_signal()
 * @param instance
 * @param err the error for which this callback was received. This will usually
 * be LCB_SUCCESS if called on behalf of lcb_irq_signal(), but may also be
 * an error code if something went wrong internally.
 * @param arg the cookie pointer passed to lcb_irq_init()
 */
typedef void (*lcb_IRQCALLBACK)(lcb_t instance, lcb_error_t err, const void *arg);

/**
 * Initialize the interrupt subsystem
 * @param instance
 * @param callback the callback to invoke whenever the interrupt is triggered
 * @param cookie a custom pointer to pass to the callback
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_irq_init(lcb_t instance, lcb_IRQCALLBACK callback, const void *cookie);

/**
 * Send an interrupt to the IRQ system. This will result in a call to the
 * callback passed to lcb_irq_init in a "timely" manner. Note that there is
 * absolutely no guarantee as to _when_ the callback will be invoked; only
 * that the callback will be invoked from within the context of lcb_wait()
 * and thus provide thread-safe access to the library.
 *
 * Additionally, calling this function multiple times may or may not result
 * in multiple calls to the callback. In all cases the context (i.e. the
 * `cookie`) installed via lcb_irq_init() should track any required state
 * to ensure consistency.
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_irq_signal(lcb_t instance);

/**Call this before destroying the instance. This is here so we make as few
 * modifications to the core as possible to accomodate this patch */
LIBCOUCHBASE_API
void
lcb_irq_cleanup(lcb_t instance);

/**@}*/

#ifdef __cplusplus
}
#endif
#endif
