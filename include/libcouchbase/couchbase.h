/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2010-2012 Couchbase, Inc.
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

#ifndef LIBCOUCHBASE_COUCHBASE_H
#define LIBCOUCHBASE_COUCHBASE_H 1

#define LCB_CONFIG_MCD_PORT 11210
#define LCB_CONFIG_MCD_SSL_PORT 11207
#define LCB_CONFIG_HTTP_PORT 8091
#define LCB_CONFIG_HTTP_SSL_PORT 18091
#define LCB_CONFIG_MCCOMPAT_PORT 11211

struct lcb_st;
typedef struct lcb_st *lcb_t;
struct lcb_http_request_st;
typedef struct lcb_http_request_st *lcb_http_request_t;

#include <stddef.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <libcouchbase/sysdefs.h>
#include <libcouchbase/assert.h>
#include <libcouchbase/visibility.h>
#include <libcouchbase/error.h>
#include <libcouchbase/iops.h>
#include <libcouchbase/http.h>
#include <libcouchbase/configuration.h>
#include <libcouchbase/_cxxwrap.h>
#include <libcouchbase/kvbuf.h>


#ifdef __cplusplus
extern "C"{
#endif

typedef lcb_U8 lcb_datatype_t;
typedef lcb_U32 lcb_USECS;

/**
 * @file
 * Main header file for Couchbase
 */

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ** INITIALIZATION                                                           **
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

/**
 * @ingroup lcb-public-api
 * @defgroup lcb-init Basic Library Routines
 *
 * @details
 *
 * To communicate with a Couchbase cluster, a new library handle instance is
 * created in the form of an lcb_t. To create such an object, the lcb_create()
 * function is called, passing it a structure of type lcb_create_st. The structure
 * acts as a container for a union of other structures which are extended
 * as more features are added. This container is forwards and backwards
 * compatible, meaning that if the structure is extended, you code and application
 * will still function if using an older version of the structure. The current
 * sub-field of the lcb_create_st structure is the `v3` field.
 *
 * Connecting to the cluster involes the client knowing the necessary
 * information needed to actually locate its services and connect to it.
 *
 * A connection specification consists of:
 *
 * 1. One or more hosts which comprise the cluster
 * 2. The name of the bucket to access and perform operations on
 * 3. The credentials of the bucket
 *
 * All these options are specified within the form of a URI in the form of
 *
 * `couchbase://$HOSTS/$BUCKET?$OPTIONS`
 *
 * @note
 * If any of the fields (hosts, bucket, options) contain the `/` character then
 * it _must_ be url-encoded; thus a bucket named `foo/bar` would be specified
 * as `couchbase:///foo%2Fbar`
 *
 * ### Hosts
 *
 * In the most typical use case, you would specify a list of several hostnames
 * delimited by a comma (`,`); each host specified should be a member of the
 * cluster. The library will use this list to initially connect to the cluster.
 *
 * Note that it is not necessary to specify _all_ the nodes of the cluster as in
 * a normal situation the library will only initially connect to one of the nodes.
 * Passing multiple nodes increases the chance of a connection succeeding even
 * if some of the nodes are currently down. Once connected to the cluster, the
 * client will update itself with the other nodes actually found within the
 * cluster and discard the list passed to it
 *
 * You can specify multiple hosts like so:
 *
 * `couchbase://foo.com,bar.com,baz.com`
 *
 * Or a single host:
 *
 * `couchbase://localhost`
 *
 * #### Specifying Ports and Protocol Options
 *
 * The default `couchbase://` scheme will assume all hosts and/or ports
 * specify the _memcached_ port. If no port is specified, it is assumed
 * that the port is _11210). For more extended options there are additional
 * schemes available:
 *
 * * `couchbases://` - Will assume all ports refer to the SSL-enabled memcached
 *   ports. This setting implicitly enables SSL on the instance as well. If no
 *   ports are provided for the hosts, the implicit port for each host will be
 *   _11207_.
 *
 * * `http://` - Will assume all ports refer to the HTTP REST API ports used
 *   by Couchbase 2.2 and lower. These are also used when connecting to a
 *   memcached bucket. If no port is specified it will be assumed the port is
 *   _8091_.
 *
 * ### Bucket
 *
 * A bucket may be specified by using the optional _path_ component of the URI
 * For protected buckets a password will still need to be supplied out of band.
 *
 * * `couchbase://1.1.1.1,2.2.2.2,3.3.3.3/users` - Connect to the `users`
 *   bucket.
 *
 * ### Options
 *
 * @warning The key-value options here are considered to be a volatile interface
 * as their names may change.
 *
 * Options can be specified as the _query_ part of the connection string,
 * for example:
 *
 * `couchbase://cbnode.net/beer?operation_timeout=10000000`.
 *
 * Options may either be appropriate _key_ parameters for lcb_cntl_string()
 * or one of the following:
 *
 * * `boostrap_on` - specify bootstrap protocols. Values can be `http` to force
 *   old-style bootstrap mode for legacy clusters, `cccp` to force bootstrap
 *   over the memcached port (For clusters 2.5 and above), or `all` to try with
 *   _cccp_ and revert to _http_
 *
 * * `certpath` - Specify the path (on the local filesystem) to the server's
 *   SSL certificate. Only applicable if SSL is being used (i.e. the scheme is
 *   `couchbases`)
 *
 * ### Bucket Identification and Credentials
 *
 * The most common settings you will wish to modify are the bucket name
 *  and the credentials field (`user` and `passwd`). If a
 * `bucket` is not specified it will revert to the `default` bucket (i.e. the
 * bucket which is created when Couchbase Server is installed).
 *
 * The `user` and `passwd` fields authenticate for the bucket. This is only
 * needed if you have configured your bucket to employ SASL auth. You can tell
 * if the bucket has been configured with SASL auth by
 *
 * 1. Logging into the Couchbase Administration Console
 * 2. Going to the _Data Buckets_ tab
 * 3. Locate the row for your bucket
 * 4. Expand the row into the detailed view (by clicking on the arrow at the
 *    left of the row)
 * 5. Click on _Edit_
 * 6. Inspect the _Access Control_ section in the pop-up
 *
 * The bucket name is specified as the _path_ portion of the URI.
 *
 * For security purposes, the _user_ and _passwd_ cannot be specified within
 * the URI
 *
 *
 * @note
 * You may not change the bucket or credentials after initializing the handle.
 *
 * #### Bootstrap Options
 *
 * The default configuration process will attempt to bootstrap first from
 * the new memcached configuration protocol (CCCP) and if that fails, use
 * the "HTTP" protocol via the REST API.
 *
 * The CCCP configuration will by default attempt to connect to one of
 * the nodes specified on the port 11201. While normally the memcached port
 * is determined by the configuration itself, this is not possible when
 * the configuration has not been attained. You may specify a list of
 * alternate memcached servers by using the 'mchosts' field.
 *
 * If you wish to modify the default bootstrap protocol selection, you
 * can use the `bootstrap_on` option to specify the desired bootstrap
 * specification
 * to use for configuration (note that the ordering of this array is
 * ignored). Using this mechanism, you can disable CCCP or HTTP.
 *
 * To force only "new-style" bootstrap, you may use `bootstrap_on=cccp`.
 * To force only "old-style" bootstrap, use `bootstrap_on=http`. To force the
 * default behavior, use `bootstrap_on=all`
 *
 *
 * @addtogroup lcb-init
 * @{
 */


/**@name Creating A Library Handle
 *
 * These structures contain the various options passed to the lcb_create()
 * function.
 * @{
 */

/** @brief Handle types @see lcb_create_st3::type */
typedef enum {
    LCB_TYPE_BUCKET = 0x00, /**< Handle for data access (default) */
    LCB_TYPE_CLUSTER = 0x01 /**< Handle for administrative access */
} lcb_type_t;

#ifndef __LCB_DOXYGEN__
/* These are definitions for some of the older fields of the `lcb_create_st`
 * structure. They are here for backwards compatibility and should not be
 * used by new code */
typedef enum { LCB_CONFIG_TRANSPORT_LIST_END = 0, LCB_CONFIG_TRANSPORT_HTTP = 1, LCB_CONFIG_TRANSPORT_CCCP, LCB_CONFIG_TRANSPORT_MAX } lcb_config_transport_t;
#define LCB_CREATE_V0_FIELDS const char *host; const char *user; const char *passwd; const char *bucket; struct lcb_io_opt_st *io;
#define LCB_CREATE_V1_FIELDS LCB_CREATE_V0_FIELDS lcb_type_t type;
#define LCB_CREATE_V2_FIELDS LCB_CREATE_V1_FIELDS const char *mchosts; const lcb_config_transport_t* transports;
struct lcb_create_st0 { LCB_CREATE_V0_FIELDS };
struct lcb_create_st1 { LCB_CREATE_V1_FIELDS };
struct lcb_create_st2 { LCB_CREATE_V2_FIELDS };
#endif

/**
 * @brief Structure for lcb_create().
 * @see lcb-init
 */
struct lcb_create_st3 {
    const char *connstr; /**< Connection string */
    const char *username; /**< Username for bucket. Unused as of Server 2.5 */
    const char *passwd; /**< Password for bucket */
    void *_pad_bucket; /* Padding. Unused */
    struct lcb_io_opt_st *io; /**< IO Options */
    lcb_type_t type;
};

/**@brief Wrapper structure for lcb_create()
 * @see lcb_create_st3 */
struct lcb_create_st {
    /** Indicates which field in the @ref lcb_CRST_u union should be used. Set this to `3` */
    int version;

    /**This union contains the set of current and historical options. The
     * The #v3 field should be used. */
    union lcb_CRST_u {
        struct lcb_create_st0 v0;
        struct lcb_create_st1 v1;
        struct lcb_create_st2 v2;
        struct lcb_create_st3 v3; /**< Use this field */
    } v;
    LCB_DEPR_CTORS_CRST
};

/**
 * @brief Create an instance of lcb.
 * @param instance Where the instance should be returned
 * @param options How to create the libcouchbase instance
 * @return LCB_SUCCESS on success
 *
 *
 * ### Examples
 * Create an instance using the default values:
 *
 * @code{.c}
 * lcb_t instance;
 * lcb_error_t err = lcb_create(&instance, NULL);
 * if (err != LCB_SUCCESS) {
 *    fprintf(stderr, "Failed to create instance: %s\n", lcb_strerror(NULL, err));
 *    exit(EXIT_FAILURE);
 * }
 * @endcode
 *
 * Specify server list
 *
 * @code{.c}
 * struct lcb_create_st options;
 * memset(&options, 0, sizeof(options));
 * options.version = 3;
 * options.v.v3.connstr = "couchbase://host1,host2,host3";
 * err = lcb_create(&instance, &options);
 * @endcode
 *
 *
 * Create a handle for data requests to protected bucket
 *
 * @code{.c}
 * struct lcb_create_st options;
 * memset(&options, 0, sizeof(options));
 * options.version = 3;
 * options.v.v3.host = "couchbase://example.com,example.org/protected"
 * options.v.v3.passwd = "secret";
 * err = lcb_create(&instance, &options);
 * @endcode
 * @committed
 * @see lcb_create_st3
 */
LIBCOUCHBASE_API
lcb_error_t lcb_create(lcb_t *instance, const struct lcb_create_st *options);

/**
 * @brief Schedule the initial connection
 * This function will schedule the initial connection for the handle. This
 * function _must_ be called before any operations can be performed.
 *
 * lcb_set_bootstrap_callback() or lcb_get_bootstrap_status() can be used to
 * determine if the scheduled connection completed successfully.
 *
 * lcb_wait() should be called after this function.
 * @committed
 */
LIBCOUCHBASE_API
lcb_error_t lcb_connect(lcb_t instance);

/**@}*/

/**
 * Associate a cookie with an instance of lcb. The _cookie_ is a user defined
 * pointer which will remain attached to the specified `lcb_t` for its duration.
 * This is the way to associate user data with the `lcb_t`.
 *
 * @param instance the instance to associate the cookie to
 * @param cookie the cookie to associate with this instance.
 *
 * @attention
 * There is no destructor for the specified `cookie` stored with the instance;
 * thus you must ensure to manually free resources to the pointer (if it was
 * dynamically allocated) when it is no longer required.
 * @committed
 *
 * @code{.c}
 * typedef struct {
 *   const char *status;
 *   // ....
 * } instance_info;
 *
 * static void bootstrap_callback(lcb_t instance, lcb_error_t err) {
 *   instance_info *info = (instance_info *)lcb_get_cookie(instance);
 *   if (err == LCB_SUCCESS) {
 *     info->status = "Connected";
 *   } else {
 *     info->status = "Error";
 *   }
 * }
 *
 * static void do_create(void) {
 *   instance_info *info = calloc(1, sizeof(*info));
 *   // info->status is currently NULL
 *   // .. create the instance here
 *   lcb_set_cookie(instance, info);
 *   lcb_set_bootstrap_callback(instance, bootstrap_callback);
 *   lcb_connect(instance);
 *   lcb_wait(instance);
 *   printf("Status of instance is %s\n", info->status);
 * }
 * @endcode
 */
LIBCOUCHBASE_API
void lcb_set_cookie(lcb_t instance, const void *cookie);

/**
 * Retrieve the cookie associated with this instance
 * @param instance the instance of lcb
 * @return The cookie associated with this instance or NULL
 * @see lcb_set_cookie()
 * @committed
 */
LIBCOUCHBASE_API
const void *lcb_get_cookie(lcb_t instance);

/**
 * @brief Wait for the execution of all batched requests
 *
 * A batched request is any request which requires network I/O.
 * This includes most of the APIs. You should _not_ use this API if you are
 * integrating with an asynchronous event loop (i.e. one where your application
 * code is invoked asynchronously via event loops).
 *
 * This function will block the calling thread until either
 *
 * * All operations have been completed
 * * lcb_breakout() is explicitly called
 *
 * @param instance the instance containing the requests
 * @return whether the wait operation failed, or LCB_SUCCESS
 * @committed
 */
LIBCOUCHBASE_API
lcb_error_t lcb_wait(lcb_t instance);

/**@brief Flags for lcb_wait3()*/
typedef enum {
    /**Behave like the old lcb_wait()*/
    LCB_WAIT_DEFAULT = 0x00,

    /**Do not check pending operations before running the event loop. By default
     * lcb_wait() will traverse the server list to check if any operations are
     * pending, and if nothing is pending the function will return without
     * running the event loop. This is usually not necessary for applications
     * which already _only_ call lcb_wait() when they know they have scheduled
     * at least one command.
     */
    LCB_WAIT_NOCHECK = 0x01
} lcb_WAITFLAGS;

/**
 * @committed
 * @brief Wait for completion of scheduled operations.
 * @param instance the instance
 * @param flags flags to modify the behavior of lcb_wait(). Pass 0 to obtain
 * behavior identical to lcb_wait().
 */
LIBCOUCHBASE_API
void lcb_wait3(lcb_t instance, lcb_WAITFLAGS flags);

/**
 * @brief Forcefully break from the event loop.
 *
 * You may call this function from within any callback to signal to the library
 * that it return control to the function calling lcb_wait() as soon as possible.
 * Note that if there are pending functions which have not been processed, you
 * are responsible for calling lcb_wait() a second time.
 *
 * @param instance the instance to run the event loop for.
 * @committed
 */
LIBCOUCHBASE_API
void lcb_breakout(lcb_t instance);


/**
 * Bootstrap callback. Invoked once the instance is ready to perform operations
 * @param instance The instance which was bootstrapped
 * @param err The error code received. If this is not LCB_SUCCESS then the
 * instance is not bootstrapped and must be recreated
 *
 * @attention This callback only receives information during instantiation.
 * @committed
 */
typedef void (*lcb_bootstrap_callback)(lcb_t instance, lcb_error_t err);

/**
 * @brief Set the callback for notification of success or failure of
 * initial connection
 * @param instance the instance
 * @param callback the callback to set. If `NULL`, return the existing callback
 * @return The existing (and previous) callback.
 * @see lcb_connect()
 * @see lcb_get_bootstrap_status()
 */
LIBCOUCHBASE_API
lcb_bootstrap_callback
lcb_set_bootstrap_callback(lcb_t instance, lcb_bootstrap_callback callback);

/**
 * @brief Gets the initial bootstrap status
 *
 * This is an alternative to using the lcb_bootstrap_callback() and may be used
 * after the initial lcb_connect() and lcb_wait() sequence.
 * @param instance
 * @return LCB_SUCCESS if properly bootstrapped or an error code otherwise.
 *
 * @attention
 * Calling this function only makes sense during instantiation.
 * @committed
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_get_bootstrap_status(lcb_t instance);

/**
 * @uncommitted
 *
 * @brief Force the library to refetch the cluster configuration
 *
 * The library by default employs various heuristics to determine if a new
 * configuration is needed from the cluster. However there are some situations
 * in which an application may wish to force a refresh of the configuration:
 *
 * * If a specific node has been failed
 *   over and the library has received a configuration in which there is no
 *   master node for a given key, the library will immediately return the error
 *   `LCB_NO_MATCHING_SERVER` for the given item and will not request a new
 *   configuration. In this state, the client will not perform any network I/O
 *   until a request has been made to it using a key that is mapped to a known
 *   active node.
 *
 * * The library's heuristics may have failed to detect an error warranting
 *   a configuration change, but the application either through its own
 *   heuristics, or through an out-of-band channel knows that the configuration
 *   has changed.
 *
 *
 * This function is provided as an aid to assist in such situations
 *
 * If you wish for your application to block until a new configuration is
 * received, you _must_ call lcb_wait3() with the LCB_WAIT_NO_CHECK flag as
 * this function call is not bound to a specific operation. Additionally there
 * is no status notification as to whether this operation succeeded or failed
 * (the configuration callback via lcb_set_configuration_callback() may
 * provide hints as to whether a configuration was received or not, but by no
 * means should be considered to be part of this function's control flow).
 *
 * In general the use pattern of this function is like so:
 *
 * @code{.c}
 * unsigned retries = 5;
 * lcb_error_t err;
 * do {
 *   retries--;
 *   err = lcb_get(instance, cookie, ncmds, cmds);
 *   if (err == LCB_NO_MATCHING_SERVER) {
 *     lcb_refresh_config(instance);
 *     usleep(100000);
 *     lcb_wait3(instance, LCB_WAIT_NO_CHECK);
 *   } else {
 *     break;
 *   }
 * } while (retries);
 * if (err == LCB_SUCCESS) {
 *   lcb_wait3(instance, 0); // equivalent to lcb_wait(instance);
 * } else {
 *   printf("Tried multiple times to fetch the key, but its node is down\n");
 * }
 * @endcode
 */
LIBCOUCHBASE_API
void
lcb_refresh_config(lcb_t instance);

/**
 * Destroy (and release all allocated resources) an instance of lcb.
 * Using instance after calling destroy will most likely cause your
 * application to crash.
 *
 * Note that any pending operations will not have their callbacks invoked.
 *
 * @param instance the instance to destroy.
 * @committed
 */
LIBCOUCHBASE_API
void lcb_destroy(lcb_t instance);

/**
 * @brief Callback received when instance is about to be destroyed
 * @param cookie cookie passed to lcb_destroy_async()
 */
typedef void (*lcb_destroy_callback)(const void *cookie);

/**
 * @brief Set the callback to be invoked when the instance is destroyed
 * asynchronously.
 * @return the previous callback.
 */
LIBCOUCHBASE_API
lcb_destroy_callback
lcb_set_destroy_callback(lcb_t, lcb_destroy_callback);
/**
 * @brief Asynchronously schedule the destruction of an instance.
 *
 * This function provides a safe way for asynchronous environments to destroy
 * the lcb_t handle without worrying about reentrancy issues.
 *
 * @param instance
 * @param arg a pointer passed to the callback.
 *
 * While the callback and cookie are optional, they are very much recommended
 * for testing scenarios where you wish to ensure that all resources allocated
 * by the instance have been closed. Specifically when the callback is invoked,
 * all timers (save for the one actually triggering the destruction) and sockets
 * will have been closed.
 *
 * As with lcb_destroy() you may call this function only once. You may not
 * call this function together with lcb_destroy as the two are mutually
 * exclusive.
 *
 * If for whatever reason this function is being called in a synchronous
 * flow, lcb_wait() must be invoked in order for the destruction to take effect
 *
 * @see lcb_set_destroy_callback
 *
 * @committed
 */
LIBCOUCHBASE_API
void lcb_destroy_async(lcb_t instance, const void *arg);

/******************************************************************************
 ******************************************************************************
 ** IO CREATION                                                              **
 ******************************************************************************
 ******************************************************************************/

/**
 * @brief Built-in I/O plugins
 * @committed
 */
typedef enum {
    LCB_IO_OPS_INVALID = 0x00, /**< @private */
    LCB_IO_OPS_DEFAULT = 0x01, /**< @private */

    /** Integrate with the libevent loop. See lcb_create_libevent_io_opts() */
    LCB_IO_OPS_LIBEVENT = 0x02,
    LCB_IO_OPS_WINSOCK = 0x03, /**< @private */
    LCB_IO_OPS_LIBEV = 0x04,
    LCB_IO_OPS_SELECT = 0x05,
    LCB_IO_OPS_WINIOCP = 0x06,
    LCB_IO_OPS_LIBUV = 0x07
} lcb_io_ops_type_t;

/** @brief IO Creation for builtin plugins */
typedef struct {
    lcb_io_ops_type_t type; /**< The predefined type you want to create */
    void *cookie; /**< Plugin-specific argument */
} lcb_IOCREATEOPTS_BUILTIN;

#ifndef __LCB_DOXYGEN__
/* These are mostly internal structures which may be in use by older applications.*/
typedef struct { const char *sofile; const char *symbol; void *cookie; } lcb_IOCREATEOPTS_DSO;
typedef struct { lcb_io_create_fn create; void *cookie; } lcb_IOCREATEOPS_FUNCTIONPOINTER;
#endif

/** @uncommited */
struct lcb_create_io_ops_st {
    int version;
    union {
        lcb_IOCREATEOPTS_BUILTIN v0;
        lcb_IOCREATEOPTS_DSO v1;
        lcb_IOCREATEOPS_FUNCTIONPOINTER v2;
    } v;
};

/**
 * Create a new instance of one of the library-supplied io ops types.
 *
 * This function should only be used if you wish to override/customize the
 * default I/O plugin behavior; for example to select a specific implementation
 * (e.g. always for the _select_ plugin) and/or to integrate
 * a builtin plugin with your own application (e.g. pass an existing `event_base`
 * structure to the _libevent_ plugin).
 *
 * If you _do_ use this function, then you must call lcb_destroy_io_ops() on
 * the plugin handle once it is no longer required (and no instance is using
 * it).
 *
 * Whether a single `lcb_io_opt_t` may be used by multiple instances at once
 * is dependent on the specific implementation, but as a general rule it should
 * be assumed to be unsafe.
 *
 * @param[out] op The newly created io ops structure
 * @param options How to create the io ops structure
 * @return @ref LCB_SUCCESS on success
 * @uncommitted
 */
LIBCOUCHBASE_API
lcb_error_t lcb_create_io_ops(lcb_io_opt_t *op, const struct lcb_create_io_ops_st *options);

/**
 * Destroy the plugin handle created by lcb_create_io_ops()
 * @param op ops structure
 * @return LCB_SUCCESS on success
 * @uncommitted
 */
LIBCOUCHBASE_API
lcb_error_t lcb_destroy_io_ops(lcb_io_opt_t op);
/**@}*/


/**
 * @ingroup lcb-public-api
 * @defgroup lcb-kv-api Key-Value API
 * @brief Operate on one or more key values
 * @details
 *
 * Basic command and structure definitions for public API. This represents the
 * new API of the library (Starting from version 2.4.0).
 *
 * In a nutshell:
 *
 * ### Storing an item:
 *
 * @code{.c}
 * static void globalStoreCallback(lcb_t, int, const lcb_RESPSTORE *resp) {
 *   if (resp->rc == LCB_SUCCESS) {
 *     printf("Stored OK!\n");
 *   } else {
 *     printf("Couldn't store!. %s\n", lcb_strerror(NULL, resp->rc));
 *   }
 * }
 *
 * //----------- Create the command
 * lcb_CMDSTORE scmd = { 0 };
 * LCB_CMD_SET_KEY(&scmd, "keyToStore", strlen("keyToStore"));
 * LCB_CMD_SET_VALUE(&scmd, "valueToStore", strlen("valueToStore"));
 * scmd.operation = LCB_SET;
 * //----------- Spool it
 * lcb_sched_enter(instance);
 * lcb_store3(instance, NULL, &scmd);
 * lcb_sched_leave(instance);
 * //----------- Set the callback
 * lcb_install_callback3(instance, LCB_CALLBACK_STORE, globalStoreCallback);
 * //-----------Wait for completion
 * lcb_wait(instance);
 * @endcode
 *
 * ### Retrieving an item:
 * @code{.c}
 * static void globalGetCallback(lcb_t, int, const lcb_RESPGET *resp) {
 *   if (resp->rc == LCB_SUCCESS) {
 *     printf("Value of item is %.*s\n", (int)resp->nvalue, resp->value);
 *   } else if (resp->rc == LCB_KEY_ENOENT) {
 *     printf("Key not stored in server!\n");
 *   } else {
 *     printf("Got other error: %s\n", lcb_strerror(resp->rc));
 *   }
 * }
 *
 * //---------- Create the command
 * lcb_CMDGET gcmd = { 0 };
 * LCB_CMD_SET_KEY(&gcmd, "keyToFetch", strlen("keyToFetch"));
 * //---------- Spool it:
 * lcb_sched_enter(instance);
 * lcb_get3(instance, NULL, &gcmd);
 * lcb_sched_leave(instance);
 * //---------- Set the callback
 * lcb_install_callback3(instance, LCB_CALLBACK_GET, globalGetCallback);
 * //---------- Wait for completion
 * lcb_wait(instance);
 * @endcode
 *
 * @addtogroup lcb-kv-api
 * @{
 */

/**
 * @name Creating Commands
 * @details
 *
 * Issuing a command to the Cluster involves selecting the correct command
 * structure, populating it with the data relevant for the command, optionally
 * associating the command with your own application data, issuing the command
 * to a spooling function, and finally receiving the response.
 *
 * Command structures all derive from the common lcb_CMDBASE structure. This
 * structure defines the common fields for all commands.
 *
 * Almost all commands need to contain a key, which should be assigned using
 * the LCB_CMD_SET_KEY() macro.
 *
 * @{*/

#define LCB_CMD_BASE \
    /**Common flags for the command. These modify the command itself. Currently
     the lower 16 bits of this field are reserved, and the higher 16 bits are
     used for individual commands.*/ \
    lcb_U32 cmdflags; \
    \
    /**Specify the expiration time. This is either an absolute Unix time stamp
     or a relative offset from now, in seconds. If the value of this number
     is greater than the value of thirty days in seconds, then it is a Unix
     timestamp.

     This field is used in mutation operations (lcb_store3()) to indicate
     the lifetime of the item. It is used in lcb_get3() with the lcb_RESPGET::lock
     option to indicate the lock expiration itself. */ \
    lcb_U32 exptime; \
    \
    /**The known CAS of the item. This is passed to mutation to commands to
     ensure the item is only changed if the server-side CAS value matches the
     one specified here. For other operations (such as lcb_CMDENDURE) this
     is used to ensure that the item has been persisted/replicated to a number
     of servers with the value specified here. */ \
    lcb_U64 cas; \
    \
    /**Note that hashkey/groupid is not a supported feature of Couchbase Server
     and this client.  It should be considered volatile and experimental.
     Using this could lead to an unbalanced cluster, inability to interoperate
     with the data from other languages, not being able to use the
     Couchbase Server UI to look up documents and other possible future
     upgrade/migration concerns. */ \
    lcb_KEYBUF key; \
    \
    /**@private
     * @volatile
     * This exists purely to support the hashkey fields of the v2 API. This field
     * will be _removed_ in future versions. */ \
    lcb_KEYBUF _hashkey

/**@brief Common ABI header for all commands. _Any_ command may be safely
 * casted to this type.*/
typedef struct lcb_CMDBASE {
    LCB_CMD_BASE;
} lcb_CMDBASE;

/**
 * Set the key for the command.
 * @param cmd A command derived from lcb_CMDBASE
 * @param keybuf the buffer for the key
 * @param keylen the length of the key.
 *
 * The storage for `keybuf` may be released or modified after the command has
 * been spooled.
 */
#define LCB_CMD_SET_KEY(cmd, keybuf, keylen) \
        LCB_KREQ_SIMPLE(&(cmd)->key, keybuf, keylen)
/**@}*/


/**
 * @name Receiving Responses
 * @details
 * This section describes the structures used for receiving responses to
 * commands.
 *
 * Each command will have a callback invoked (typically once, for some commands
 * this may be more than once) with a response structure. The response structure
 * will be of a type that extends lcb_RESPBASE. The response structure should
 * not be modified and any of its fields should be considered to point to memory
 * which will be released after the callback exits.
 *
 * The common response header contains the lcb_RESPBASE::cookie field which
 * is the pointer to your application context (passed as the second argument
 * to the spooling function) and allows you to associate a specific command
 * with a specific response.
 *
 * The header will also contain the key (lcb_RESPBASE::key) field which can
 * also help identify the specific command. This is useful if you maintain a
 * single _cookie_ for multiple commands, and have per-item specific data
 * you wish to associate within the _cookie_ itself.
 *
 * Success or failure of the operation is signalled through the lcb_RESPBASE::rc
 * field. Note that even in the case of failure, the lcb_RESPBASE::cookie and
 * lcb_RESPBASE::key fields will _always_ be populated.
 *
 * Most commands also return the CAS of the item (as it exists on the server)
 * and this is placed inside the lcb_RESPBASE::cas field, however it is
 * only valid in the case where lcb_RESPBASE::rc is LCB_SUCCESS.
 *
 * @{
 */

#define LCB_RESP_BASE \
    void *cookie; /**< User data associated with request */ \
    const void *key; /**< Key for request */ \
    lcb_SIZE nkey; /**< Size of key */ \
    lcb_cas_t cas; /**< CAS for response (if applicable) */ \
    lcb_error_t rc; /**< Status code */ \
    lcb_U16 version; /**< ABI version for response */ \
    lcb_U16 rflags; /**< Response specific flags. see lcb_RESPFLAGS */


/**@brief Base response structure for callbacks.
 * All responses structures derive from this ABI.*/
typedef struct {
    LCB_RESP_BASE
} lcb_RESPBASE;

#define LCB_RESP_SERVER_FIELDS \
    /** String containing the `host:port` of the server which sent this response */ \
    const char *server;

/**@brief Base structure for informational commands from servers
 * This contains an additional lcb_RESPSERVERBASE::server field containing the
 * server which emitted this response.
 */
typedef struct {
    LCB_RESP_BASE
    LCB_RESP_SERVER_FIELDS
} lcb_RESPSERVERBASE;

/**@brief Response flags.
 * These provide additional 'meta' information about the response*/
typedef enum {
    /** No more responses are to be received for this request */
    LCB_RESP_F_FINAL = 0x01,

    /**The response was artificially generated inside the client.
     * This does not contain reply data from the server for the command, but
     * rather contains the basic fields to indicate success or failure and is
     * otherwise empty.
     */
    LCB_RESP_F_CLIENTGEN = 0x02,

    /**The response was a result of a not-my-vbucket error */
    LCB_RESP_F_NMVGEN = 0x04
} lcb_RESPFLAGS;

/**
 * The type of response passed to the callback. This is used to install callbacks
 * for the library and to distinguish between responses if a single callback
 * is used for multiple response types.
 *
 * @note These callbacks may conflict with the older version 2 callbacks. The
 * rules are as follows:
 * * If a callback has been installed using lcb_install_callback3(), then
 * the older version 2 callback will not be invoked for that operation. The order
 * of installation does not matter.
 * * If the LCB_CALLBACK_DEFAULT callback is installed, _none_ of the version 2
 * callbacks are invoked.
 */
typedef enum {
    LCB_CALLBACK_DEFAULT = 0, /**< Default callback invoked as a fallback */
    LCB_CALLBACK_GET, /**< lcb_get3() */
    LCB_CALLBACK_STORE, /**< lcb_store3() */
    LCB_CALLBACK_COUNTER, /**< lcb_counter3() */
    LCB_CALLBACK_TOUCH, /**< lcb_touch3() */
    LCB_CALLBACK_REMOVE, /**< lcb_remove3() */
    LCB_CALLBACK_UNLOCK, /**< lcb_unlock3() */
    LCB_CALLBACK_STATS, /**< lcb_stats3() */
    LCB_CALLBACK_VERSIONS, /**< lcb_server_versions3() */
    LCB_CALLBACK_VERBOSITY, /**< lcb_server_verbosity3() */
    LCB_CALLBACK_FLUSH, /**< lcb_flush3() */
    LCB_CALLBACK_OBSERVE, /**< lcb_observe3_ctxnew() */
    LCB_CALLBACK_GETREPLICA, /**< lcb_rget3() */
    LCB_CALLBACK_ENDURE, /**< lcb_endure3_ctxnew() */
    LCB_CALLBACK_HTTP, /**< lcb_http3() */
    LCB_CALLBACK__MAX /* Number of callbacks */
} lcb_CALLBACKTYPE;

/**
 * Callback invoked for responses.
 * @param instance The handle
 * @param cbtype The type of callback - or in other words, the type of operation
 * this callback has been invoked for.
 * @param resp The response for the operation. Depending on the operation this
 * response structure should be casted into a more specialized type.
 */
typedef void (*lcb_RESPCALLBACK)
        (lcb_t instance, int cbtype, const lcb_RESPBASE* resp);

/**
 * @comitted
 *
 * Install a new-style callback for an operation. The callback will be invoked
 * with the relevant response structure.
 *
 * @param instance the handle
 * @param cbtype the type of operation for which this callback should be installed.
 *        The value should be one of the lcb_CALLBACKTYPE constants
 * @param cb the callback to install
 * @return the old callback
 *
 * @note LCB_CALLBACK_DEFAULT is initialized to the default handler which proxies
 * back to the older 2.x callbacks. If you set `cbtype` to LCB_CALLBACK_DEFAULT
 * then your `2.x` callbacks _will not work_.
 *
 * @note The old callback may be `NULL`. It is usually not an error to have a
 * `NULL` callback installed. If the callback is `NULL`, then the default callback
 * invocation pattern will take place (as desribed above). However it is an error
 * to set the default callback to `NULL`.
 */
LIBCOUCHBASE_API
lcb_RESPCALLBACK
lcb_install_callback3(lcb_t instance, int cbtype, lcb_RESPCALLBACK cb);

/**
 * @comitted
 *
 * Get the current callback installed as `cbtype`. Note that this does not
 * perform any kind of resolution (as described in lcb_install_callback3) and
 * will only return a non-`NULL` value if a callback had specifically been
 * installed via lcb_install_callback3() with the given `cbtype`.
 *
 * @param instance the handle
 * @param cbtype the type of callback to retrieve
 * @return the installed callback for the type.
 */
LIBCOUCHBASE_API
lcb_RESPCALLBACK
lcb_get_callback3(lcb_t instance, int cbtype);

/**@}*/

/**@name General Spooling API
 *
 * @details
 * The following operation APIs are low level entry points which create a
 * single operation. To use these operation APIs you should call the
 * lcb_sched_enter() which creates a virtual scope in which to create operations.
 *
 * For each of these operation APIs, the actual API call will insert the
 * created packet into a "Scheduling Queue" (this is done through
 * mcreq_sched_add() which is in mcreq.h). You may add as many items to this
 * scheduling queue as you would like.
 *
 * Note that an operation is only added to the queue if it was able to be
 * scheduled properly. If a scheduling failure occurred (for example, if a
 * configuration is missing, the command had invalid input, or memory allocation
 * failed) then the command will not be placed into the queue.
 *
 * Once all operations have been scheduled you can call
 * lcb_sched_leave() which will place all commands scheduled into the I/O
 * queue.
 *
 * If you wish to _discard_ all scheduled operations (for example, if one of
 * them errored, and your application cannot handle partial scheduling failures)
 * then you may call lcb_sched_fail() which will release all the resources
 * of the packets placed into the temporary queue.
 *
 * @{*/

/**
 * @comitted
 * @brief Enter a scheduling context.
 *
 * A scheduling context is an ephemeral list of
 * commands issued to various servers. Operations (like lcb_get3(), lcb_store3())
 * place packets into the current context.
 *
 * The context mechanism allows you to efficiently pipeline and schedule multiple
 * operations of different types and quantities. The network is not touched
 * and nothing is scheduled until the context is exited.
 *
 * @param instance the instance
 *
 * @code{.c}
 * lcb_sched_enter(instance);
 * lcb_get3(...);
 * lcb_store3(...);
 * lcb_counter3(...);
 * lcb_sched_leave(instance);
 * lcb_wait3(instance, LCB_WAIT_NOCHECK);
 * @endcode
 */
LIBCOUCHBASE_API
void lcb_sched_enter(lcb_t instance);

/**
 * @comitted
 *
 * @brief Leave the current scheduling context, scheduling the commands within the
 * context to be flushed to the network.
 *
 * @param instance the instance
 */
LIBCOUCHBASE_API
void lcb_sched_leave(lcb_t instance);


/**
 * @comitted
 * @brief Fail all commands in the current scheduling context.
 *
 * The commands placed within the current
 * scheduling context are released and are never flushed to the network.
 * @param instance
 */
LIBCOUCHBASE_API
void lcb_sched_fail(lcb_t instance);

/**
 * @comitted
 * @brief Request commands to be flushed to the network
 *
 * By default, the library will implicitly request a flush to the network upon
 * a call to lcb_sched_leave() [ Note, this does not mean the items are flushed
 * and I/O is performed, but it means the relevant event loop watchers are
 * activated to perform the operations on the next iteration ]. If
 * @ref LCB_CNTL_SCHED_NOFLUSH is set then this behavior is disabled and the
 * application must explicitly call lcb_sched_flush(). This may be considered
 * more performant in the cases where multiple discreet operations are scheduled
 * in an lcb_sched_enter()/lcb_sched_leave() pair. With implicit flush enabled,
 * each call to lcb_sched_leave() will possibly invoke system repeatedly.
 */
LIBCOUCHBASE_API
void lcb_sched_flush(lcb_t instance);

/**@}*/

/**@name Simple Retrievals
 * @brief Request and response structure for retrieving items
 * @{
 */

/**@brief Command for retrieving a single item
 *
 * @see lcb_get3()
 * @see lcb_RESPGET
 * @note The #cas member should be set to 0 for this operation.
 */
typedef struct {
    LCB_CMD_BASE;

    /**
     * If this parameter is set then the server will in addition to retrieving
     * the item also lock the item, making it so that subsequent attempts to
     * lock and/or modify the same item will fail with an error
     * (either @ref LCB_KEY_EEXISTS or @ref LCB_ETMPFAIL).
     *
     * The lock will be released when one of the following happens:
     *
     * 1. The item is explicitly unlocked (see lcb_unlock())
     * 2. The lock expires (See the #exptime parameter)
     * 3. The item is modified using lcb_store(), and being provided with the
     *    correct _CAS_.
     */
    int lock;
} lcb_CMDGET;

/** @brief Response structure when retrieving a single item */
typedef struct {
    LCB_RESP_BASE
    const void *value; /**< Value buffer for the item */
    lcb_SIZE nvalue; /**< Length of value */
    void* bufh; /**< This is actually an lcb_BACKBUF structure. @see lcb_backbuf_ref() */
    lcb_U8 datatype; /**< Currently ignored */
    lcb_U32 itmflags; /**< User-defined flags for the item */
} lcb_RESPGET;

/**
 * @brief Spool a single get operation
 * @param instance the handle
 * @param cookie a pointer to be associated with the command
 * @param cmd the command structure
 * @return LCB_SUCCESS if successful, an error code otherwise
 * @see lcb_sched_enter(), lcb_sched_leave()
 *
 * @code{.c}
 * lcb_sched_enter(instance);
 * lcb_CMDGET cmd = { 0 };
 * LCB_CMD_SET_KEY(&cmd, "Hello", 5);
 * lcb_install_callback3(instance, LCB_CALLBACK_GET, a_callback);
 * lcb_get3(instance, cookie, &cmd);
 * lcb_sched_leave(instance);
 * lcb_wait3(instance, LCB_WAIT_NOCHECK);
 * @endcode
 *
 * @commited
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_get3(lcb_t instance, const void *cookie, const lcb_CMDGET *cmd);

/**@brief Command for lcb_unlock3()
 * @attention lcb_CMDBASE::cas must be specified, or the operation will fail on
 * the server*/
typedef lcb_CMDBASE lcb_CMDUNLOCK;

/**@brief Response structure for an unlock command.
 * @note the lcb_RESPBASE::cas field does not contain the CAS of the item*/
typedef lcb_RESPBASE lcb_RESPUNLOCK;

/**@comitted
 * @brief
 * Unlock a previously locked item using lcb_CMDGET::lock
 *
 * @param instance the instance
 * @param cookie the context pointer to associate with the command
 * @param cmd the command containing the information about the locked key
 * @return LCB_SUCCESS if successful, an error code otherwise
 * @see lcb_get3(), lcb_sched_enter(), lcb_sched_leave()
 *
 * @code{.c}
 * static void locked_callback(lcb_t, lcb_CALLBACKTYPE, const lcb_RESPBASE *resp) {
 *   lcb_CMDUNLOCK cmd = { 0 };
 *   LCB_CMD_SET_KEY(&cmd, resp->key, resp->nkey);
 *   cmd.cas = resp->cas;
 *   lcb_sched_enter(instance);
 *   lcb_unlock3(instance, cookie, &cmd);
 *   lcb_sched_leave(instance);
 * }
 * @endcode
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_unlock3(lcb_t instance, const void *cookie, const lcb_CMDUNLOCK *cmd);
/**@}*/

/**
 * @name Counter Operations
 * @{
 */

/**@brief Command for counter operations.
 * @see lcb_counter3(), lcb_RESPCOUNTER.
 *
 * @warning You may only set the #exptime member if the #create member is set
 * to a true value. Setting `exptime` otherwise will cause the operation to
 * fail with @ref LCB_OPTIONS_CONFLICT
 *
 * @warning The #cas member should be set to 0 for this operation. As this
 * operation itself is atomic, specifying a CAS is not necessary.
 */
typedef struct {
    LCB_CMD_BASE;
    /**Delta value. If this number is negative the item on the server is
     * decremented. If this number is positive then the item on the server
     * is incremented */
    lcb_int64_t delta;
    /**If the item does not exist on the server (and `create` is true) then
     * this will be the initial value for the item. */
    lcb_U64 initial;
    /**Boolean value. Create the item and set it to `initial` if it does not
     * already exist */
    int create;
} lcb_CMDCOUNTER;

/**@brief Response structure for counter operations
 * @see lcb_counter3()
 */
typedef struct {
    LCB_RESP_BASE
    /** Contains the _current_ value after the operation was performed */
    lcb_U64 value;
} lcb_RESPCOUNTER;

/**
 * @brief Spool a single counter operation
 * @param instance the instance
 * @param cookie the pointer to associate with the request
 * @param cmd the command to use
 * @return LCB_SUCCESS on success, other error on failure
 *
 * @code{.c}
 * lcb_CMDCOUNTER cmd = { 0 };
 * LCB_CMD_SET_KEY(&cmd, "counter", strlen("counter"));
 * cmd.delta = 1; // Increment by one
 * cmd.initial = 42; // Default value is 42 if it does not exist
 * cmd.exptime = 300; // Expire in 5 minutes
 * lcb_sched_enter(instance);
 * lcb_counter3(instance, NULL, &cmd);
 * lcb_sched_leave(instance);
 * lcb_install_callback3(instance, LCB_CALLBACKTYPE_COUNTER, cb);
 * lcb_wait3(instance, LCB_WAIT_NOCHECK);
 * @endcode
 *
 * @comitted
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_counter3(lcb_t instance, const void *cookie, const lcb_CMDCOUNTER *cmd);
/**@}*/


/**@name Replica Reads
 *
 * @details
 * Get items from replica. This is like lcb_get3() but is useful when
 * an item from the master cannot be retrieved - usually because the node is
 * unresponsive. See @ref lcb_rget3() for an example.
 *
 * @warning
 * The read-from-replica functionality allows the application to sacrifice data
 * integrity for availability for certain use cases. Developers should take
 * into consideration that they may be retrieving stale data. The actual
 * likelihood of the data being stale depends on how and why the master (or
 * active) node for the given item is unavailable.
 *
 * @{*/

/**@brief Select get-replica mode
 * @see lcb_CMDGETREPLICA */
typedef enum {
    /**Query all the replicas sequentially, retrieving the first successful
     * response. This is the default and recommended mode */
    LCB_REPLICA_FIRST = 0x00,

    /**Query all the replicas concurrently, retrieving all the responses*/
    LCB_REPLICA_ALL = 0x01,

    /**Query the specific replica specified by the
     * lcb_CMDGETREPLICA#index field */
    LCB_REPLICA_SELECT = 0x02
} lcb_replica_t;

/**
 * @brief Command for requesting an item from a replica
 * @note The `options.exptime` and `options.cas` fields are ignored for this
 * command.
 *
 * @see lcb_rget3(), lcb_RESPGET
 */
typedef struct {
    LCB_CMD_BASE;
    lcb_replica_t strategy; /**< Strategy to use for selecting a replica */
    int index;
} lcb_CMDGETREPLICA;

/**
 * @brief Spool a single get-with-replica request
 * @param instance
 * @param cookie
 * @param cmd
 * @return LCB_SUCCESS on success, error code otherwise
 *
 * The example below shows the normal use case of lcb_rget3(). It will first
 * issue a normal GET command, and will attempt to retrieve from the replica
 * if there is a clear indicator that the first server is offline. Note that
 * there are multiple ways to determine if a specific node is online or offline;
 * but the @ref LCB_NO_MATCHING_SERVER is typically set when a server has been
 * failed over.
 *
 * @code{.c}
 * static void get_callback(lcb_t instance, int cbtype, const lcb_RESPGET *resp)
 * {
 *   if (resp->rc == LCB_NO_MATCHING_SERVER) {
 *     // This is an indication that the node is temporarily unavailable,
 *     // for example:
 *     if (cbtype == LCB_CALLBACK_GETREPLICA) {
 *       printf("Wow. We couldn't read from the replica!");
 *       return;
 *     }
 *     lcb_sched_enter(instance);
 *     lcb_CMDGETREPLICA rcmd = { 0 };
 *     LCB_CMD_SET_KEY(&rcmd, resp->key, resp->nkey);
 *     lcb_rget3(instance, NULL, &rcmd);
 *     lcb_sched_leave(instance);
 *   }
 * }
 * void doGetKey() {
 *   // Setup code..
 *   lcb_install_callback3(instance, LCB_CALLBACK_GET, get_callback);
 *   lcb_install_callback3(instance, LCB_CALLBACK_GETREPLICA, get_callback);
 *   lcb_CMDGET gcmd = { 0 };
 *   LCB_CMD_SET_KEY(&gcmd, "aKey", 4);
 *   lcb_sched_enter(instance);
 *   lcb_get3(instance, &gcmd);
 *   lcb_sched_leave(instance);
 *   lcb_wait(instance);
 * }
 * @endcode
 *
 * @comitted
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_rget3(lcb_t instance, const void *cookie, const lcb_CMDGETREPLICA *cmd);
/**@}*/

/**@name Storing and Mutating Items
 * @{
 */

/**
 * @brief Value for lcb_CMDSTORE::operation
 *
 * Storing an item in couchbase is only one operation with a different
 * set of attributes / constraints.
 */
typedef enum {
    /** Add the item to the cache, but fail if the object exists alread */
    LCB_ADD = 0x01,
    /** Replace the existing object in the cache */
    LCB_REPLACE = 0x02,
    /** Unconditionally set the object in the cache */
    LCB_SET = 0x03,
    /** Append this object to the existing object */
    LCB_APPEND = 0x04,
    /** Prepend this  object to the existing object */
    LCB_PREPEND = 0x05
} lcb_storage_t;

/**@brief
 * Command for storing an item to the server. This command must contain the
 * key to mutate, the value which should be set (or appended/prepended) in the
 * lcb_CMDSTORE::value field (see LCB_CMD_SET_VALUE()) and the operation indicating
 * the mutation type (lcb_CMDSTORE::operation).
 *
 * @warning #exptime *cannot* be used with #operation set to @ref LCB_APPEND
 * or @ref LCB_PREPEND.
 */
typedef struct {
    LCB_CMD_BASE;

    /** Value to store on the server */
    lcb_VALBUF value;
    /**These flags are stored alongside the item on the server. They are
     * typically used by higher level clients to store format/type information*/
    lcb_U32 flags;
    /**Ignored for now */
    lcb_datatype_t datatype;
    /**Must be assigned*/
    lcb_storage_t operation;
} lcb_CMDSTORE;

typedef struct {
    LCB_RESP_BASE
    lcb_storage_t op;
} lcb_RESPSTORE;

/**
 * @brief Set the value buffer for the command
 * @param scmd an lcb_CMDSTORE pointer
 * @param valbuf the buffer for the value
 * @param vallen the length of the buffer
 * The buffer needs to remain valid only until the command is passed to the
 * lcb_store3() function.
 */
#define LCB_CMD_SET_VALUE(scmd, valbuf, vallen) do { \
    (scmd)->value.vtype = LCB_KV_COPY; \
    (scmd)->value.u_buf.contig.bytes = valbuf; \
    (scmd)->value.u_buf.contig.nbytes = vallen; \
} while (0);

/**
 * @comitted
 * @brief Spool a single storage request
 * @param instance the handle
 * @param cookie pointer to associate with the command
 * @param cmd the command structure
 * @return LCB_SUCCESS on success, error code on failure
 *
 * @code{.c}
 * lcb_CMDSTORE cmd = { 0 };
 * LCB_CMD_SET_KEY(&cmd, "Key", 3);
 * LCB_CMD_SET_VALUE(&cmd, "value", 5);
 * cmd.operation = LCB_ADD; // Only create if it does not exist
 * cmd.exptime = 60; // expire in a minute
 * lcb_sched_enter(instance);
 * lcb_store3(instance, cookie, &cmd);
 * lcb_sched_leave(instance);
 * lcb_wait3(instance, LCB_WAIT_NOCHECK);
 * @endcode
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_store3(lcb_t instance, const void *cookie, const lcb_CMDSTORE *cmd);
/**@}*/

/**@name Removing Items
 * @{*/

/**@brief
 * Command for removing an item from the server
 * @note The lcb_CMDREMOVE::exptime field here does nothing.
 *
 * The lcb_CMDREMOVE::cas field may be
 * set to the last CAS received from a previous operation if you wish to
 * ensure the item is removed only if it has not been mutated since the last
 * retrieval
 */
typedef lcb_CMDBASE lcb_CMDREMOVE;

/**@brief
 * Response structure for removal operation. The lcb_RESPREMOVE::cas field
 * contains the CAS of the item which may be used to check that it no longer
 * exists on any node's storage using the lcb_endure3_ctxnew() function.
 *
 * The lcb_RESPREMOVE::rc field may be set to LCB_KEY_ENOENT if the item does
 * not exist, or LCB_KEY_EEXISTS if a CAS was specified and the item has since
 * been mutated.
 */
typedef lcb_RESPBASE lcb_RESPREMOVE;

/**@comitted
 * @brief Spool a removal of an item
 * @param instance the handle
 * @param cookie pointer to associate with the request
 * @param cmd the command
 * @return LCB_SUCCESS on success, other code on failure
 *
 * @code{.c}
 * lcb_CMDREMOVE cmd = { 0 };
 * LCB_CMD_SET_KEY(&cmd, "deleteme", strlen("deleteme"));
 * lcb_sched_enter(instance);
 * lcb_remove3(instance, cookie, &cmd);
 * lcb_sched_leave(instance);
 * lcb_wait(instance);
 * @endcode
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_remove3(lcb_t instance, const void *cookie, const lcb_CMDREMOVE * cmd);
/**@}*/

/**@name Modify an item's expiration time
 *
 * @details
 * The lcb_touch3() command may be used to modify an items operation time, either
 * to make it expire at a given time, or to clear its pending expiration. This
 * command may be used in case you wish to only ensure the item is not deleted
 * but no actually modify (lcb_store3()) or retrieve (lcb_get3()) the item.
 *
 * @{
 */

/**@brief Command structure for a touch request
 * @note The lcb_CMDTOUCH::cas field is ignored. The item's modification time
 * is always updated regardless if the CAS on the server differs*/
typedef lcb_CMDBASE lcb_CMDTOUCH;

/**@brief Response structure for a touch request
 * @note the lcb_RESPTOUCH::cas field contains the current CAS of the item*/
typedef lcb_RESPBASE lcb_RESPTOUCH;

/**@comitted
 * @brief Spool a touch request
 * @param instance the handle
 * @param cookie the pointer to associate with the request
 * @param cmd the command
 * @return LCB_SUCCESS on success, other error code on failure
 *
 * @code{.c}
 * lcb_CMDTOUCH cmd = { 0 };
 * LCB_CMD_SET_KEY(&cmd, "keep_me", strlen("keep_me"));
 * cmd.exptime = 0; // Clear the expiration
 * lcb_sched_enter(instance);
 * lcb_touch3(instance, cookie, &cmd);
 * lcb_sched_leave(instance);
 * @endcode
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_touch3(lcb_t instance, const void *cookie, const lcb_CMDTOUCH *cmd);
/**@}*/


/**@name Retrieve statistics from the cluster
 * @{
 */

/**@brief Command structure for stats request
 * The lcb_CMDSTATS::key field should contain the statistics key, or be empty
 * if the default statistics are desired. */
typedef lcb_CMDBASE lcb_CMDSTATS;

/** The key is a stored item for which statistics should be retrieved. This
 * invokes the 'keystats' semantics. Note that when using such semantics, a key
 * must be present, and must not have any spaces in it. */
#define LCB_CMDSTATS_F_KV (1 << 16)

/**@brief Response structure for cluster statistics.
 * The lcb_RESPSTATS::key field contains the statistic name (_not_ the same
 * as was passed in lcb_CMDSTATS::key which is the name of the statistical
 * _group_).*/
typedef struct {
    LCB_RESP_BASE
    LCB_RESP_SERVER_FIELDS
    const char *value; /**< The value, if any, for the given statistic */
    lcb_SIZE nvalue; /**< Length of value */
} lcb_RESPSTATS;


/**@comitted
 * @brief Spool a request for statistics from the cluster.
 * @param instance the instance
 * @param cookie pointer to associate with the request
 * @param cmd the command
 * @return LCB_SUCCESS on success, other error code on failure.
 *
 * Note that the callback for this command is invoked an indeterminate amount
 * of times. The callback is invoked once for each statistic for each server.
 * When all the servers have responded with their statistics, a final callback
 * is delivered to the application with the LCB_RESP_F_FINAL flag set in the
 * lcb_RESPSTATS::rflags field. When this response is received no more callbacks
 * for this command shall be invoked.
 *
 * @code{.c}
 * void stats_callback(lcb_t, lcb_CALLBACKTYPE, const lcb_RESPSTATS *resp)
 * {
 *   if (resp->key) {
 *     FILE *fp = (FILE *)resp->cookie;
 *     fprintf(fp, "Server %s: %.*s = %.*s\n", resp->server,
 *            (int)resp->nkey, resp->key,
 *            (int)resp->nvalue, resp->value);
 *   }
 *   if (resp->rflags & LCB_RESP_F_FINAL) {
 *     fclose(cookie);
 *   }
 * }
 *
 * void printStatsToFile(const char *path) {
 *   FILE *fp = fopen(path, "w");
 *   // .. initialize your instance
 *   lcb_install_callback3(instance, LCB_CALLBACK_STATS, (lcb_RESP_cb)stats_callback);
 *   lcb_CMDSTATS cmd = { 0 };
 *   // Using default stats, no further initialization
 *   lcb_sched_enter(instance);
 *   lcb_stats3(instance, fp, &cmd);
 *   lcb_sched_leave(instance);
 *   lcb_wait3(instance, LCB_WAIT_NOCHECK);
 * }
 * @endcode
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_stats3(lcb_t instance, const void *cookie, const lcb_CMDSTATS * cmd);
/**@}*/

/**
 * Multi Command Context API
 * Some commands (notably, OBSERVE and its higher level equivalent, endue)
 * are handled more efficiently at the cluster side by stuffing multiple
 * items into a single packet.
 *
 * This structure defines three function pointers to invoke. The #addcmd()
 * function will add a new command to the current packet, the #done()
 * function will schedule the packet(s) into the current scheduling context
 * and the #fail() function will destroy the context without progressing
 * further.
 *
 * Some commands will return an lcb_MULTICMD_CTX object to be used for this
 * purpose:
 *
 * @code{.c}
 * lcb_MUTLICMD_CTX *ctx = lcb_observe3_ctxnew(instance);
 *
 * lcb_CMDOBSERVE cmd = { 0 };
 * LCB_CMD_SET_KEY(&cmd, "key1", strlen("key1"));
 * ctx->addcmd(ctx, &cmd);
 * LCB_CMD_SET_KEY(&cmd.key, "key2", strlen("key2"));
 * ctx->addcmd(ctx, &cmd);
 * LCB_CMD_SET_KEY(&cmd.key, "key3", strlen("key3"));
 * ctx->addcmd(ctx, &cmd);
 *
 * lcb_sched_enter(instance);
 * ctx->done(ctx);
 * lcb_sched_leave(instance);
 * lcb_wait(instance);
 * @endcode
 */
typedef struct lcb_MULTICMD_CTX_st {
    /**
     * Add a command to the current context
     * @param ctx the context
     * @param cmd the command to add. Note that `cmd` may be a subclass of lcb_CMDBASE
     * @return LCB_SUCCESS, or failure if a command could not be added.
     */
    lcb_error_t (*addcmd)(struct lcb_MULTICMD_CTX_st *ctx, const lcb_CMDBASE *cmd);

    /**
     * Indicate that no more commands are added to this context, and that the
     * context should assemble the packets and place them in the current
     * scheduling context
     * @param ctx The multi context
     * @param cookie The cookie for all commands
     * @return LCB_SUCCESS if scheduled successfully, or an error code if there
     * was a problem constructing the packet(s).
     */
    lcb_error_t (*done)(struct lcb_MULTICMD_CTX_st *ctx, const void *cookie);

    /**
     * Indicate that no more commands should be added to this context, and that
     * the context should not add its contents to the packet queues, but rather
     * release its resources. Called if you don't want to actually perform
     * the operations.
     * @param ctx
     */
    void (*fail)(struct lcb_MULTICMD_CTX_st *ctx);
} lcb_MULTICMD_CTX;

/**@name Retrieve replication and persistence status about an item
 * @{
 */

/**Set this bit in the cmdflags field to indicate that only the master node
 * should be contacted*/
#define LCB_CMDOBSERVE_F_MASTER_ONLY 1<<16

/**@brief Structure for an observe request.
 * To request the status from _only_ the master node of the key, set the
 * LCB_CMDOBSERVE_F_MASTERONLY bit inside the lcb_CMDOBSERVE::cmdflags field
 */
typedef lcb_CMDBASE lcb_CMDOBSERVE;

/**@brief Response structure for an observe command.
 * Note that the lcb_RESPOBSERVE::cas contains the CAS of the item as it is
 * stored within that specific server. The CAS may be incorrect or stale
 * unless lcb_RESPOBSERVE::ismaster is true.
 */
typedef struct {
    LCB_RESP_BASE
    lcb_U8 status; /**<Bit set of flags */
    lcb_U8 ismaster; /**< Set to true if this response came from the master node */
    lcb_U32 ttp; /**<Unused */
    lcb_U32 ttr; /**<Unused */
} lcb_RESPOBSERVE;

/**@comitted
 * @brief Create a new multi context for an observe operation
 * @param instance the instance
 * @return a new multi command context, or NULL on allocation failure.
 *
 * Note that the callback for this command will be invoked multiple times,
 * one for each node. To determine when no more callbacks will be invoked,
 * check for the LCB_RESP_F_FINAL flag inside the lcb_RESPOBSERVE::rflags
 * field.
 *
 * @code{.c}
 * void callback(lcb_t, lcb_CALLBACKTYPE, const lcb_RESPOBSERVE *resp)
 * {
 *   if (resp->rflags & LCB_RESP_F_FINAL) {
 *     return;
 *   }
 *
 *   printf("Got status for key %.*s\n", (int)resp->nkey, resp->key);
 *   printf("  Node Type: %s\n", resp->ismaster ? "MASTER" : "REPLICA");
 *   printf("  Status: 0x%x\n", resp->status);
 *   printf("  Current CAS: 0x%"PRIx64"\n", resp->cas);
 * }
 *
 * lcb_MULTICMD_CTX *mctx = lcb_observe3_ctxnew(instance);
 * lcb_CMDOBSERVE cmd = { 0 };
 * LCB_CMD_SET_KEY(&cmd, "key", 3);
 * mctx->addcmd(mctx, (lcb_CMDBASE *)&cmd);
 * lcb_sched_enter(instance);
 * mctx->done(mctx, cookie);
 * lcb_sched_leave(instance);
 * lcb_install_callback3(instance, LCB_CALLBACK_OBSERVE, (lcb_RESPCALLBACK)callback);
 * @endcode
 */
LIBCOUCHBASE_API
lcb_MULTICMD_CTX *
lcb_observe3_ctxnew(lcb_t instance);
/**@}*/

/**@name Wait for items to be persisted or replicated to nodes.
 * @details
 * This feature allows the client to query the server for the status of an
 * item until either the item is persisted/replicated, or the specified timeout
 * is reached.
 *
 * @{
 */

/**@brief Command structure for endure.
 * If the lcb_CMDENDURE::cas field is specified, the operation will check and
 * verify that the CAS found on each of the nodes matches the CAS specified
 * and only then consider the item to be replicated and/or persisted to the
 * nodes. If the item exists on the master's cache with a different CAS then
 * the operation will fail
 */
typedef lcb_CMDBASE lcb_CMDENDURE;

/**@brief Response structure for endure */
typedef struct {
    LCB_RESP_BASE
    lcb_U16 nresponses; /**< Total number of polls needed for this item */
    lcb_U8 exists_master; /**< True if the item exists in master's cache */
    lcb_U8 persisted_master; /**< True if item exists in master's disk */
    lcb_U8 npersisted; /**< How many nodes was this item persisted to */
    lcb_U8 nreplicated; /**< How many nodes was this item replicated to */
} lcb_RESPENDURE;

/** @brief Options for lcb_durability_poll() */
typedef struct {
    /**
     * Upper limit in microseconds from the scheduling of the command. When
     * this timeout occurs, all remaining non-verified keys will have their
     * callbacks invoked with @c LCB_ETIMEDOUT
     */
    lcb_U32 timeout;

    /**
     * The durability check may involve more than a single call to observe - or
     * more than a single packet sent to a server to check the key status. This
     * value determines the time to wait (in microseconds)
     * between multiple probes for the same server.
     * If left at 0, the @ref LCB_CNTL_DURABILITY_INTERVAL will be used
     * instead.
     */
    lcb_U32 interval;

    /** how many nodes the key should be persisted to (including master) */
    lcb_U16 persist_to;

    /** how many nodes the key should be replicated to (excluding master) */
    lcb_U16 replicate_to;

    /**
     * this flag inverts the sense of the durability check and ensures that
     * the key does *not* exist
     */
    lcb_U8 check_delete;

    /**
     * If replication/persistence requirements are excessive, cap to
     * the maximum available
     */
    lcb_U8 cap_max;
} lcb_DURABILITYOPTSv0;

/**@brief Options for lcb_endure3_ctxnew() (wrapper)
 * @see lcb_DURABILITYOPTSv0 */
typedef struct lcb_durability_opts_st {
    int version;
    union {
        lcb_DURABILITYOPTSv0 v0;
    } v;
} lcb_durability_opts_t;

/**
 * @comitted
 * @brief Return a new command context for scheduling endure operations
 * @param instance the instance
 * @param options a structure containing the various criteria needed for
 * durability requirements
 * @param[out] err Error code if a new context could not be created. This
 * may be a @c LCB_DURABILITY_ETOOMANY which indicates that
 * the number of servers specified by the user exceeds the possible number
 * of servers that the key may be replicated and/or persisted to.
 *
 * @return the new context, or NULL on error. Note that in addition to memory
 * allocation failure, this function might also return NULL because the `options`
 * structure contained bad values. Always check the `err` result.
 *
 * An example:
 * @code{.c}
 * // This is probably best executed within a storage callback:
 * static void store_cb(lcb_t instance, int cbtype, lcb_RESPSTORE *resp) {
 *   if (resp->rc != LCB_SUCCESS) {
 *     // oops!
 *   }
 *   lcb_durability_opts_t options = { 0 };
 *   lcb_error_t err;
 *
 *   options.v.v0.persist_to = -1;
 *   options.v.v0.replicate_to = -1;
 *   options.v.v0.cap_max = 1;
 *
 *   lcb_sched_enter(instance);
 *   lcb_MULTICMD_CTX *ctx = lcb_endure3_ctxnew(instance, &options, &err);
 *   // Create the command
 *   lcb_CMDENDURE dcmd = { 0 };
 *   LCB_CMD_SET_KEY(&dcmd, resp->key, resp->nkey);
 *   dcmd.cas = resp->cas;
 *   err = ctx->addcmd(ctx, (lcb_RESPBASE*)&dcmd);
 *   // We're only adding a single command this time, so just call ->done()
 *   ctx->done(ctx);
 *   lcb_sched_leave(instance);
 * }
 * @endcode
 */
LIBCOUCHBASE_API
lcb_MULTICMD_CTX *
lcb_endure3_ctxnew(lcb_t instance,
    const lcb_durability_opts_t *options, lcb_error_t *err);
/**@}*/

/**@name Check the memcached server versions
 * @brief Return the memcached version (not Couchbase version) for all servers.
 * May also be used as a simple way to check that all nodes are responding.
 *
 * @{
 */

/**@brief Response structure for the version command */
typedef struct {
    LCB_RESP_BASE
    LCB_RESP_SERVER_FIELDS
    const char *mcversion; /**< The version string */
    lcb_SIZE nversion; /**< Length of the version string */
} lcb_RESPMCVERSION;

/**@comitted*/
LIBCOUCHBASE_API
lcb_error_t
lcb_server_versions3(lcb_t instance, const void *cookie, const lcb_CMDBASE * cmd);
/**@}*/

/**@name Set the memcached Logging Level
 * @{
 */

/** @brief `level` field for lcb_set_verbosity() */
typedef enum {
    /**This is the most verbose level and generates a lot of output on the
     * server. Using this level will impact the cluster's performance */
    LCB_VERBOSITY_DETAIL = 0x00,

    /**This level generates a lot of output. Using this level will impact the
     * cluster's performance */
    LCB_VERBOSITY_DEBUG = 0x01,

    /**This level traces all commands and generates a fair amount of output.
     * Depend on the workload it may slow down the system a little bit */
    LCB_VERBOSITY_INFO = 0x02,

    /**This is the default level and only errors and warnings will be logged*/
    LCB_VERBOSITY_WARNING = 0x03
} lcb_verbosity_level_t;

typedef struct {
    /* unused */
    LCB_CMD_BASE;
    const char *server;
    lcb_verbosity_level_t level;
} lcb_CMDVERBOSITY;
typedef lcb_RESPSERVERBASE lcb_RESPVERBOSITY;
/**@comitted*/
LIBCOUCHBASE_API
lcb_error_t
lcb_server_verbosity3(lcb_t instance, const void *cookie, const lcb_CMDVERBOSITY *cmd);
/**@}*/

/**@name Flush a memcached Bucket
 * @details Clear a memcached bucket of all items. Note that this will not
 * work on a Couchbase bucket. To flush a couchbase bucket, use the REST API
 * @{
 */
typedef lcb_CMDBASE lcb_CMDFLUSH;
typedef lcb_RESPSERVERBASE lcb_RESPFLUSH;
/**@comitted*/
LIBCOUCHBASE_API
lcb_error_t
lcb_flush3(lcb_t instance, const void *cookie, const lcb_CMDFLUSH *cmd);
/**@}*/


/** Command flag for HTTP to indicate that the callback is to be invoked
 * multiple times for each new chunk of incoming data. Once all the chunks
 * have been received, the callback will be invoked once more with the
 * LCB_RESP_F_FINAL flag and an empty content. */

/**
 * @name Perform an HTTP operation
 * @{
 */

#define LCB_CMDHTTP_F_STREAM 1<<16

/**
 * @brief The type of HTTP request to execute
 */
typedef enum {
    /**
     * Execute a request against the bucket. The handle must be of
     * @ref LCB_TYPE_BUCKET and must be connected.
     */
    LCB_HTTP_TYPE_VIEW = 0,

    /**
     * Execute a management API request. The credentials used will match
     * those passed during the instance creation time. Thus is the instance
     * type is @ref LCB_TYPE_BUCKET then only bucket-level credentials will
     * be used.
     */
    LCB_HTTP_TYPE_MANAGEMENT = 1,

    /**
     * Execute an arbitrary request against a host and port
     */
    LCB_HTTP_TYPE_RAW = 2,
    LCB_HTTP_TYPE_MAX = 3
} lcb_http_type_t;

/**
 * @brief HTTP Request method enumeration
 * These just enumerate the various types of HTTP request methods supported.
 * Refer to the specific cluster or view API to see which method is appropriate
 * for your request
 */
typedef enum {
    LCB_HTTP_METHOD_GET = 0,
    LCB_HTTP_METHOD_POST = 1,
    LCB_HTTP_METHOD_PUT = 2,
    LCB_HTTP_METHOD_DELETE = 3,
    LCB_HTTP_METHOD_MAX = 4
} lcb_http_method_t;

/**
 * Structure for performing an HTTP request.
 * Note that the key and nkey fields indicate the _path_ for the API
 */
typedef struct {
    LCB_CMD_BASE;
    /**Type of request to issue. LCB_HTTP_TYPE_VIEW will issue a request
     * against a random node's view API. LCB_HTTP_TYPE_MANAGEMENT will issue
     * a request against a random node's administrative API, and
     * LCB_HTTP_TYPE_RAW will issue a request against an arbitrary host. */
    lcb_http_type_t type;
    lcb_http_method_t method; /**< HTTP Method to use */

    /** If the request requires a body (e.g. `PUT` or `POST`) then it will
     * go here. Be sure to indicate the length of the body too. */
    const char *body;

    /** Length of the body for the request */
    lcb_SIZE nbody;

    /** If non-NULL, will be assigned a handle which may be used to
     * subsequently cancel the request */
    lcb_http_request_t *reqhandle;

    /** For views, set this to `application/json` */
    const char *content_type;

    /** Username to authenticate with, if left empty, will use the credentials
     * passed to lcb_create() */
    const char *username;

    /** Password to authenticate with, if left empty, will use the credentials
     * passed to lcb_create() */
    const char *password;

    /** If set, this must be a string in the form of `http://host:port`. Should
     * only be used for raw requests. */
    const char *host;
} lcb_CMDHTTP;

typedef struct {
    LCB_RESP_BASE
    short htstatus; /** HTTP status code */
    const char * const * headers;
    const void *body;
    lcb_SIZE nbody;
    lcb_http_request_t _htreq; /* Private */
} lcb_RESPHTTP;

/**@comitted*/
LIBCOUCHBASE_API
lcb_error_t
lcb_http3(lcb_t instance, const void *cookie, const lcb_CMDHTTP *cmd);
/**@}*/
/**@}*/


/**
 * @ingroup lcb-public-api
 * @defgroup lcb-instance-status Retrieve status information from an lcb_t
 * @brief These functions return status information about the handle, the current
 * connection, and the number of nodes found within the cluster.
 *
 * @see lcb_cntl() for more functions to retrieve status info
 *
 * @addtogroup lcb-instance-status
 * @{
 */

/**@name Information about Nodes
 * @{*/

/**@brief
 * Type of node to retrieve for the lcb_get_node() function
 */
typedef enum {
    /** Get an HTTP configuration (Rest API) node */
    LCB_NODE_HTCONFIG = 0x01,
    /** Get a data (memcached) node */
    LCB_NODE_DATA = 0x02,
    /** Get a view (CAPI) node */
    LCB_NODE_VIEWS = 0x04,
    /** Only return a node which is connected, or a node which is known to be up */
    LCB_NODE_CONNECTED = 0x08,

    /** Specifying this flag adds additional semantics which instruct the library
     * to search additional resources to return a host, and finally,
     * if no host can be found, return the string
     * constant @ref LCB_GETNODE_UNAVAILABLE. */
    LCB_NODE_NEVERNULL = 0x10,

    /** Equivalent to `LCB_NODE_HTCONFIG|LCB_NODE_CONNECTED` */
    LCB_NODE_HTCONFIG_CONNECTED = 0x09,

    /**Equivalent to `LCB_NODE_HTCONFIG|LCB_NODE_NEVERNULL`.
     * When this is passed, some additional attempts may be made by the library
     * to return any kind of host, including searching the initial list of hosts
     * passed to the lcb_create() function. */
    LCB_NODE_HTCONFIG_ANY = 0x11
} lcb_GETNODETYPE;

/** String constant returned by lcb_get_node() when the @ref LCB_NODE_NEVERNULL
 * flag is specified, and no node can be returned */
#define LCB_GETNODE_UNAVAILABLE "invalid_host:0"

/**
 * @brief Return a string of `host:port` for a node of the given type.
 *
 * @param instance the instance from which to retrieve the node
 * @param type the type of node to return
 * @param index the node number if index is out of bounds it will be wrapped
 * around, thus there is never an invalid value for this parameter
 *
 * @return a string in the form of `host:port`. If LCB_NODE_NEVERNULL was specified
 * as an option in `type` then the string constant LCB_GETNODE_UNAVAILABLE is
 * returned. Otherwise `NULL` is returned if the type is unrecognized or the
 * LCB_NODE_CONNECTED option was specified and no connected node could be found
 * or a memory allocation failed.
 *
 * @note The index parameter is _ignored_ if `type` is
 * LCB_NODE_HTCONFIG|LCB_NODE_CONNECTED as there will always be only a single
 * HTTP bootstrap node.
 *
 * @code{.c}
 * const char *viewnode = lcb_get_node(instance, LCB_NODE_VIEWS, 0);
 * // Get the connected REST endpoint:
 * const char *restnode = lcb_get_node(instance, LCB_NODE_HTCONFIG|LCB_NODE_CONNECTED, 0);
 * if (!restnode) {
 *   printf("Instance not connected via HTTP!\n");
 * }
 * @endcode
 *
 * Iterate over all the data nodes:
 * @code{.c}
 * unsigned ii;
 * for (ii = 0; ii < lcb_get_num_servers(instance); ii++) {
 *   const char *kvnode = lcb_get_node(instance, LCB_NODE_DATA, ii);
 *   if (kvnode) {
 *     printf("KV node %s exists at index %u\n", kvnode, ii);
 *   } else {
 *     printf("No node for index %u\n", ii);
 *   }
 * }
 * @endcode
 *
 * @committed
 */
LIBCOUCHBASE_API
const char *
lcb_get_node(lcb_t instance, lcb_GETNODETYPE type, unsigned index);

/**
 * @brief Get the number of the replicas in the cluster
 *
 * @param instance The handle to lcb
 * @return -1 if the cluster wasn't configured yet, and number of replicas
 * otherwise. This may be `0` if there are no replicas.
 * @committed
 */
LIBCOUCHBASE_API
lcb_S32 lcb_get_num_replicas(lcb_t instance);

/**
 * @brief Get the number of the nodes in the cluster
 * @param instance The handle to lcb
 * @return -1 if the cluster wasn't configured yet, and number of nodes otherwise.
 * @committed
 */
LIBCOUCHBASE_API
lcb_S32 lcb_get_num_nodes(lcb_t instance);


/**
 * @brief Get a list of nodes in the cluster
 *
 * @return a NULL-terminated list of 0-terminated strings consisting of
 * node hostnames:admin_ports for the entire cluster.
 * The storage duration of this list is only valid until the
 * next call to a libcouchbase function and/or when returning control to
 * libcouchbase' event loop.
 *
 * @code{.c}
 * const char * const * curp = lcb_get_server_list(instance);
 * for (; *curp; curp++) {
 *   printf("Have node %s\n", *curp);
 * }
 * @endcode
 * @committed
 */
LIBCOUCHBASE_API
const char *const *lcb_get_server_list(lcb_t instance);

/**@}*/

/**
 * @brief Check if instance is blocked in the event loop
 * @param instance the instance to run the event loop for.
 * @return non-zero if nobody is waiting for IO interaction
 * @uncomitted
 */
LIBCOUCHBASE_API
int lcb_is_waiting(lcb_t instance);


/**@name Modifying Settings
 * The lcb_cntl() function and its various helpers are the means by which to
 * modify settings within the library
 * @{
 */

/**
 * This function exposes an ioctl/fcntl-like interface to read and write
 * various configuration properties to and from an lcb_t handle.
 *
 * @param instance The instance to modify
 *
 * @param mode One of LCB_CNTL_GET (to retrieve a setting) or LCB_CNTL_SET
 *      (to modify a setting). Note that not all configuration properties
 *      support SET.
 *
 * @param cmd The specific command/property to modify. This is one of the
 *      LCB_CNTL_* constants defined in this file. Note that it is safe
 *      (and even recommanded) to use the raw numeric value (i.e.
 *      to be backwards and forwards compatible with libcouchbase
 *      versions), as they are not subject to change.
 *
 *      Using the actual value may be useful in ensuring your application
 *      will still compile with an older libcouchbase version (though
 *      you may get a runtime error (see return) if the command is not
 *      supported
 *
 * @param arg The argument passed to the configuration handler.
 *      The actual type of this pointer is dependent on the
 *      command in question.  Typically for GET operations, the
 *      value of 'arg' is set to the current configuration value;
 *      and for SET operations, the current configuration is
 *      updated with the contents of *arg.
 *
 * @return LCB_NOT_SUPPORTED if the code is unrecognized
 *      LCB_EINVAL if there was a problem with the argument
 *      (typically for SET) other error codes depending on the
 *      command.
 *
 * @committed
 *
 * @see lcb_cntl_setu32()
 * @see lcb_cntl_string()
 */
LIBCOUCHBASE_API
lcb_error_t lcb_cntl(lcb_t instance, int mode, int cmd, void *arg);

/**
 * Alternate way to set configuration settings by passing a string key
 * and value. This may be used to provide a simple interface from a command
 * line or higher level language to allow the setting of specific key-value
 * pairs.
 *
 * The format for the value is dependent on the option passed, the following
 * value types exist:
 *
 * - **Timeout**. A _timeout_ value can either be specified as fractional
 *   seconds (`"1.5"` for 1.5 seconds), or in microseconds (`"1500000"`).
 * - **Number**. This is any valid numerical value. This may be signed or
 *   unsigned depending on the setting.
 * - **Boolean**. This specifies a boolean. A true value is either a positive
 *   numeric value (i.e. `"1"`) or the string `"true"`. A false value
 *   is a zero (i.e. `"0"`) or the string `"false"`.
 * - **Float**. This is like a _Number_, but also allows fractional specification,
 *   e.g. `"2.4"`.
 *
 * | Code | Name | Type
 * |------|------|-----
 * |@ref LCB_CNTL_OP_TIMEOUT                | `"operation_timeout"` | Timeout |
 * |@ref LCB_CNTL_VIEW_TIMEOUT              | `"view_timeout"`      | Timeout |
 * |@ref LCB_CNTL_DURABILITY_TIMEOUT        | `"durability_timeout"` | Timeout |
 * |@ref LCB_CNTL_DURABILITY_INTERVAL       | `"durability_interval"`| Timeout |
 * |@ref LCB_CNTL_HTTP_TIMEOUT              | `"http_timeout"`      | Timeout |
 * |@ref LCB_CNTL_RANDOMIZE_BOOTSTRAP_HOSTS | `"randomize_nodes"`   | Boolean|
 * |@ref LCB_CNTL_CONFERRTHRESH             | `"error_thresh_count"`| Number (Positive)|
 * |@ref LCB_CNTL_CONFDELAY_THRESH          |`"error_thresh_delay"` | Timeout |
 * |@ref LCB_CNTL_CONFIGURATION_TIMEOUT     | `"config_total_timeout"`|Timeout|
 * |@ref LCB_CNTL_CONFIG_NODE_TIMEOUT       | `"config_node_timeout"` | Timeout |
 * |@ref LCB_CNTL_CONFIGCACHE               | `"config_cache"`      | Path |
 * |@ref LCB_CNTL_DETAILED_ERRCODES         | `"detailed_errcodes"` | Boolean |
 * |@ref LCB_CNTL_HTCONFIG_URLTYPE          | `"http_urlmode"`      | Number (values are the constant values) |
 * |@ref LCB_CNTL_RETRY_BACKOFF             | `"retry_backoff"`     | Float |
 * |@ref LCB_CNTL_HTTP_POOLSIZE             | `"http_poolsize"`     | Number |
 * |@ref LCB_CNTL_VBGUESS_PERSIST           | `"vbguess_persist"`   | Boolean |
 *
 *
 * @committed - Note, the actual API call is considered committed and will
 * not disappear, however the existence of the various string settings are
 * dependendent on the actual settings they map to. It is recommended that
 * applications use the numerical lcb_cntl() as the string names are
 * subject to change.
 *
 * @see lcb_cntl()
 * @see lcb-cntl-settings
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_cntl_string(lcb_t instance, const char *key, const char *value);

/**
* @brief Convenience function to set a value as an lcb_U32
* @param instance
* @param cmd setting to modify
* @param arg the new value
* @return see lcb_cntl() for details
* @committed
*/
LIBCOUCHBASE_API
lcb_error_t lcb_cntl_setu32(lcb_t instance, int cmd, lcb_U32 arg);

/**
* @brief Retrieve an lcb_U32 setting
* @param instance
* @param cmd setting to retrieve
* @return the value.
* @warning This function does not return an error code. Ensure that the cntl is
* correct for this version, or use lcb_cntl() directly.
* @committed
*/
LIBCOUCHBASE_API
lcb_U32 lcb_cntl_getu32(lcb_t instance, int cmd);

/**
 * Determine if a specific control code exists
 * @param ctl the code to check for
 * @return 0 if it does not exist, nonzero if it exists.
 */
LIBCOUCHBASE_API
int
lcb_cntl_exists(int ctl);
/**@}*/ /* settings */
/**@}*/ /* lcbt_info */

/**
 * @ingroup lcb-public-api
 * @defgroup lcb-timings Instrument and inspect times for operations
 * @brief Determine how long operations are taking to be completed
 *
 * libcouchbase provides a simple form of per-command timings you may use
 * to figure out the current lantency for the request-response cycle as
 * generated by your application. Please note that these numbers are not
 * necessarily accurate as you may affect the timing recorded by doing
 * work in the event loop.
 *
 * The time recorded with this library is the time elapsed from the
 * command being called, and the response packet being received from the
 * server.  Everything the application does before driving the event loop
 * will affect the timers.
 *
 * The function lcb_enable_timings() is used to enable the timings for
 * the given instance, and lcb_disable_timings is used to disable the
 * timings. The overhead of using the timers should be negligible.
 *
 * The function lcb_get_timings is used to retrieve the current timing.
 * values from the given instance. The cookie is passed transparently to
 * the callback function.
 *
 * Here is an example of the usage of this module:
 *
 * @code{.c}
 * #include <libcouchbase/couchbase.h>
 *
 * static void callback(
 *  lcb_t instance, const void *cookie, lcb_timeunit_t timeunit, lcb_U32 min,
 *  lcb_U32 max, lcb_U32 total, lcb_U32 maxtotal)
 * {
 *   FILE* out = (void*)cookie;
 *   int num = (float)10.0 * (float)total / ((float)maxtotal);
 *   fprintf(out, "[%3u - %3u]", min, max);
 *   switch (timeunit) {
 *   case LCB_TIMEUNIT_NSEC:
 *      fprintf(out, "ns");
 *      break;
 *   case LCB_TIMEUNIT_USEC:
 *      fprintf(out, "us");
 *      break;
 *   case LCB_TIMEUNIT_MSEC:
 *      fsprintf(out, "ms");
 *      break;
 *   case LCB_TIMEUNIT_SEC:
 *      fprintf(out, "s ");
 *      break;
 *   default:
 *      ;
 *   }
 *
 *   fprintf(out, " |");
 *   for (int ii = 0; ii < num; ++ii) {
 *      fprintf(out, "#");
 *   }
 *   fprintf(out, " - %u\n", total);
 * }
 *
 *
 * lcb_enable_timings(instance);
 * ... do a lot of operations ...
 * fprintf(stderr, "              +---------+\n"
 * lcb_get_timings(instance, stderr, callback);
 * fprintf(stderr, "              +---------+\n"
 * lcb_disable_timings(instance);
 * @endcode
 *
 * @addtogroup lcb-timings
 * @{
 */

/**
 * @brief Time units reported by lcb_get_timings()
 */
enum lcb_timeunit_t {
    LCB_TIMEUNIT_NSEC = 0, /**< @brief Time is in nanoseconds */
    LCB_TIMEUNIT_USEC = 1, /**< @brief Time is in microseconds */
    LCB_TIMEUNIT_MSEC = 2, /**< @brief Time is in milliseconds */
    LCB_TIMEUNIT_SEC = 3 /**< @brief Time is in seconds */
};
typedef enum lcb_timeunit_t lcb_timeunit_t;

/**
 * Start recording timing metrics for the different operations.
 * The timer is started when the command is called (and the data
 * spooled to the server), and the execution time is the time until
 * we parse the response packets. This means that you can affect
 * the timers by doing a lot of other stuff before checking if
 * there is any results available..
 *
 * @param instance the handle to lcb
 * @return Status of the operation.
 * @committed
 */
LIBCOUCHBASE_API
lcb_error_t lcb_enable_timings(lcb_t instance);


/**
 * Stop recording (and release all resources from previous measurements)
 * timing metrics.
 *
 * @param instance the handle to lcb
 * @return Status of the operation.
 * @committed
 */
LIBCOUCHBASE_API
lcb_error_t lcb_disable_timings(lcb_t instance);

/**
 * The following function is called for each bucket in the timings
 * histogram when you call lcb_get_timings.
 * You are guaranteed that the callback will be called with the
 * lowest [min,max] range first.
 *
 * @param instance the handle to lcb
 * @param cookie the cookie you provided that allows you to pass
 *               arbitrary user data to the callback
 * @param timeunit the "scale" for the values
 * @param min The lower bound for this histogram bucket
 * @param max The upper bound for this histogram bucket
 * @param total The number of hits in this histogram bucket
 * @param maxtotal The highest value in all of the buckets
 */
typedef void (*lcb_timings_callback)(lcb_t instance,
                                     const void *cookie,
                                     lcb_timeunit_t timeunit,
                                     lcb_U32 min,
                                     lcb_U32 max,
                                     lcb_U32 total,
                                     lcb_U32 maxtotal);

/**
 * Get the timings histogram
 *
 * @param instance the handle to lcb
 * @param cookie a cookie that will be present in all of the callbacks
 * @param callback Callback to invoke which will handle the timings
 * @return Status of the operation.
 * @committed
 */
LIBCOUCHBASE_API
lcb_error_t lcb_get_timings(lcb_t instance,
                            const void *cookie,
                            lcb_timings_callback callback);
/**@}*/

/**
* @ingroup lcb-public-api
* @defgroup lcb-build-info Build and version information for the library
* These functions and macros may be used to conditionally compile features
* depending on the version of the library being used. They may also be used
* to employ various features at runtime and to retrieve the version for
* informational purposes.
* @addtogroup lcb-build-info
* @{
*/

#if !defined(LCB_VERSION_STRING) || defined(__LCB_DOXYGEN__)
/** @brief libcouchbase version string */
#define LCB_VERSION_STRING "unknown"
#endif

#if !defined(LCB_VERSION) || defined(__LCB_DOXYGEN__)
/**@brief libcouchbase hex version
 *
 * This number contains the hexadecimal representation of the library version.
 * It is in a format of `0xXXYYZZ` where `XX` is the two digit major version
 * (e.g. `02`), `YY` is the minor version (e.g. `05`) and `ZZ` is the patch
 * version (e.g. `24`).
 *
 * For example:
 *
 * String   |Hex
 * ---------|---------
 * 2.0.0    | 0x020000
 * 2.1.3    | 0x020103
 * 3.0.15   | 0x030015
 */
#define LCB_VERSION 0x000000
#endif

#if !defined(LCB_VERSION_CHANGESET) || defined(__LCB_DOXYGEN__)
/**@brief The SCM revision ID. @see LCB_CNTL_CHANGESET */
#define LCB_VERSION_CHANGESET "0xdeadbeef"
#endif

/**
 * Get the version of the library.
 *
 * @param version where to store the numeric representation of the
 *         version (or NULL if you don't care)
 *
 * @return the textual description of the version ('\0'
 *          terminated). Do <b>not</b> try to release this string.
 *
 */
LIBCOUCHBASE_API
const char *lcb_get_version(lcb_U32 *version);

/**@brief Whether the library has SSL support*/
#define LCB_SUPPORTS_SSL 1
/**@brief Whether the library has experimental compression support */
#define LCB_SUPPORTS_SNAPPY 2

/**
 * @committed
 * Determine if this version has support for a particularl feature
 * @param n the feature ID to check for
 * @return 0 if not supported, nonzero if supported.
 */
LIBCOUCHBASE_API
int
lcb_supports_feature(int n);

/**@}*/

/**
 * This may be used in conjunction with the errmap callback if it wishes
 * to fallback for default behavior for the given code.
 * @uncomitted
 */
LIBCOUCHBASE_API
lcb_error_t lcb_errmap_default(lcb_t instance, lcb_U16 code);

/**
 * Callback for error mappings. This will be invoked when requesting whether
 * the user has a possible mapping for this error code.
 *
 * This will be called for response codes which may be ambiguous in most
 * use cases, or in cases where detailed response codes may be mapped to
 * more generic ones.
 */
typedef lcb_error_t (*lcb_errmap_callback)(lcb_t instance, lcb_U16 bincode);

/**@uncommitted*/
LIBCOUCHBASE_API
lcb_errmap_callback lcb_set_errmap_callback(lcb_t, lcb_errmap_callback);

/**
 * Functions to allocate and free memory related to libcouchbase. This is
 * mainly for use on Windows where it is possible that the DLL and EXE
 * are using two different CRTs
 */
LIBCOUCHBASE_API
void *lcb_mem_alloc(lcb_SIZE size);

/** Use this to free memory allocated with lcb_mem_alloc */
LIBCOUCHBASE_API
void lcb_mem_free(void *ptr);

/**
 * These two functions unconditionally start and stop the event loop. These
 * should be used _only_ when necessary. Use lcb_wait and lcb_breakout
 * for safer variants.
 *
 * Internally these proxy to the run_event_loop/stop_event_loop calls
 */
LCB_INTERNAL_API
void lcb_run_loop(lcb_t instance);

LCB_INTERNAL_API
void lcb_stop_loop(lcb_t instance);

/* This returns the library's idea of time */
LCB_INTERNAL_API
lcb_U64 lcb_nstime(void);

typedef enum {
    /** Dump the raw vbucket configuration */
    LCB_DUMP_VBCONFIG =  0x01,
    /** Dump information about each packet */
    LCB_DUMP_PKTINFO = 0x02,
    /** Dump memory usage/reservation information about buffers */
    LCB_DUMP_BUFINFO = 0x04,
    /** Dump everything */
    LCB_DUMP_ALL = 0xff
} lcb_DUMPFLAGS;

/**
 * @volatile
 * @brief Write a textual dump to a file.
 *
 * This function will inspect the various internal structures of the current
 * client handle (indicated by `instance`) and write the state information
 * to the file indicated by `fp`.
 * @param instance the handle to dump
 * @param fp the file to which the dump should be written
 * @param flags a set of modifiers (of @ref lcb_DUMPFLAGS) indicating what
 * information to dump. Note that a standard set of information is always
 * dumped, but by default more verbose information is hidden, and may be
 * enabled with these flags.
 */
LIBCOUCHBASE_API
void
lcb_dump(lcb_t instance, FILE *fp, lcb_U32 flags);

/* Post-include some other headers */
#ifdef __cplusplus
}
#endif /* __cplusplus */

#include <libcouchbase/api2.h>
#include <libcouchbase/cntl.h>
#include <libcouchbase/deprecated.h>
#endif /* LIBCOUCHBASE_COUCHBASE_H */
