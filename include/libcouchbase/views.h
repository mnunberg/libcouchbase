/**
 *     Copyright 2014 Couchbase, Inc.
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
 **/

#ifndef LCB_VIEWS_API_H
#define LCB_VIEWS_API_H
#include <libcouchbase/couchbase.h>
#include <libcouchbase/api3.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lcb_RESPVIEW_st lcb_RESPVIEWQUERY;

/**
 * Callback function invoked for each row returned from the view
 * @param instance the library handle
 * @param cbtype the callback type. Note that this is currently unset, but is
 * provided for compatibility with the @ref lcb_RESPCALLBACK ABI.
 * @param row Information about the current row
 *
 * Note that this callback's `row->rflags` will contain the @ref LCB_RESP_F_FINAL
 * flag set after all rows have been returned. Applications should check for
 * the presence of this flag. If this flag is present, the row itself will
 * contain the raw response metadata in its lcb_RESPVIEWQUERY::value field.
 */
typedef void (*lcb_VIEWQUERYCALLBACK)(lcb_t instance,
        int cbtype, const lcb_RESPVIEWQUERY *row);

/** Set this flag to execute an actual get with each response */
#define LCB_CMDVIEWQUERY_F_INCLUDE_DOCS 1 << 16

/**Set this flag to only parse the top level row, and not its constituent
 * parts. Note this is incompatible with `F_INCLUDE_DOCS`*/
#define LCB_CMDVIEWQUERY_F_NOROWPARSE 1 << 17

/** Command structure for querying a view */
typedef struct {
    /** Common command flags; e.g. @ref LCB_CMDVIEWQUERY_F_INCLUDE_DOCS */
    lcb_U32 cmdflags;

    /** The design document as a string; e.g. `"beer"` */
    const char *ddoc;
    /** Length of design document name */
    size_t nddoc;

    /** The name of the view as a string; e.g. `"brewery_beers"` */
    const char *view;
    /** Length of the view name */
    size_t nview;

    /**Any URL parameters to be passed to the view should be specified here.
     * The library will internally insert a `?` character before the options
     * (if specified), so do not place one yourself.
     *
     * The format of the options follows the standard for passing parameters
     * via HTTP requests; thus e.g. `key1=value1&key2=value2`. This string
     * is itself not parsed by the library but simply appended to the URL. */
    const char *optstr;

    /** Length of the option string */
    size_t noptstr;

    /**Callback to invoke for each row. If not provided, @ref LCB_EINVAL will
     * be returned from lcb_view_query() */
    lcb_VIEWQUERYCALLBACK callback;
} lcb_CMDVIEWQUERY;

/**
 * Response structure. This is provided for each invocation of the
 * lcb_CMDVIEWQUERY::callback invocation. The #key and #nkey fields here
 * refer to the first argument passed to the `emit` function by the
 * `map` function.
 *
 * This response structure may be used as-is, in case the values are simple,
 * or may be relayed over to a more advanced JSON parser to decode the
 * individual key and value properties.
 *
 * @note
 * The #key and #value fields are JSON encoded. This means that if they are
 * bare strings, they will be surrounded by quotes. Please take note of this
 * if doing any form of string comparison/processing.
 *
 * @note
 * If the @ref LCB_CMDVIEWQUERY_F_NOROWPARSE flag has been set, the #value
 * field will contain the raw row contents, rather than the constituent
 * elements.
 */
struct lcb_RESPVIEW_st {
    LCB_RESP_BASE
    const char *docid; /**< Document ID (i.e. memcached key) associated with this row */
    size_t ndocid; /**< Length of document ID */

    /**Emitted value. If `rflags & LCB_RESP_F_FINAL` is true then this will
     * contain the _metadata_ of the view response itself. This includes the
     * `total_rows` field among other things, and should be parsed as JSON */
    const char *value;

    size_t nvalue; /**< Length of emitted value */

    /**If the request failed, this will contain the raw underlying request.
     * You may inspect this request and perform some other processing on
     * the underlying HTTP data. Note that this may not necessarily contain
     * the entire response body; just the chunk at which processing failed.*/
    const lcb_RESPHTTP *htresp;

    /**If @ref LCB_CMDVIEWQUERY_F_INCLUDE_DOCS was specified in the request,
     * this will contain the response for the _GET_ command. This is the same
     * response as would be received in the `LCB_CALLBACK_GET` for
     * lcb_get3().
     *
     * Note that this field should be checked for various errors as well, as it
     * is remotely possible the get request did not succeed.
     *
     * If the @ref LCB_CMDVIEWQUERY_F_INCLUDE_DOCS flag was not specified, this
     * field will be `NULL`.
     */
    const lcb_RESPGET *docresp;
};

/**
 * Initiate a view query. This will execute a view (MapReduce) request against
 * a view endpoint within the cluster. For each row emitted by the view
 * functions (i.e. the `map`, and possibly `reduce` functions) the
 * callback specified in the lcb_CMDVIEWQUERY::callback field will be invoked.
 *
 * Here is a functional example:
 *
 * @code{.c}
 * static void rowCallback(lcb_t instance, int ignoreme, const lcb_RESPVIEWQUERY *response) {
 *   if (response->rflags & LCB_RESP_F_FINAL) {
 *     printf("View is done!\n");
 *     if (response->nvalue) {
 *       printf("Raw JSON metadata: %.*s\n", (int)response->nvalue, response->value);
 *     }
 *     if (response->rc != LCB_SUCCESS) {
 *       printf("Query failed: %s\n", lcb_sterror(instance, response->rc));
 *     }
 *     return;
 *   }
 *
 *   // Key and value always exist:
 *   printf("Emitted key: %.*s\n", (int)resp->nkey, resp-key);
 *   printf("Emitted value: %s.*s\n", (int)resp->nvalue, resp->value);
 *
 *   // Document IDs are only present for non-reduce queries
 *   if (resp->docid) {
 *     printf("Document ID: %.*s\n", (int)resp->docid, resp->ndocid);
 *     // If LCB_CMDVIEWQUERY_F_INCLUDE_DOCS was specified, this might
 *     // contain the actual document value
 *     if (resp->docresp) {
 *       const lcb_RESPGET *rg = resp->docresp;
 *       if (rg->rc == LCB_SUCCESS) {
 *         printf("Document contents: %.*s\n", (int)rg->value, rg->nvalue);
 *         printf("CAS: 0x%lx\n", rg->cas);
 *       } else {
 *         printf("Couldn't fetch document: %s\n", lcb_strerror(instance, rg->rc));
 *       }
 *     }
 *   }
 * }
 *
 * static void doViewRequest(lcb_t instance) {
 *   lcb_CMDVIEWQUERY cmd = { 0 };
 *   vq.ddoc = "beer";
 *   vq.nddoc = strlen(vq.ddoc);
 *   vq.view = "brewery_beers";
 *   vq.nview = strlen(vq.view);
 *   vq.optstr = "limit=10&descending=true";
 *   vq.noptstr = strlen(vq.optstr);
 *   vq.callback = rowCallback;
 *
 *   // If you want to perform an implicit lcb_get3() on each row's `docid`
 *   if (includeDocs) {
 *     vq.cmdflags |= LCB_CMDVIEWQUERY_F_INCLUDE_DOCS;
 *   }
 *
 *   lcb_error_t rc = lcb_view_query(instance, NULL, &cmd);
 *   // handle error
 *   lcb_wait(instance);
 * }
 * @endcode
 *
 * @param instance The library handle
 * @param cookie user pointer included in every response
 * @param cmd the command detailing the type of query to perform
 * @return LCB_SUCCESS on scheduling success, or an error code on scheduling
 * failure.
 */
LIBCOUCHBASE_API
lcb_error_t
lcb_view_query(lcb_t instance, const void *cookie, const lcb_CMDVIEWQUERY *cmd);

/**
 * Initialize a command object. This is a convenience function.
 * @param vq the command to initialize
 * @param design the name of the design. Required and should be NUL-terminated
 * @param view the name of the view. Required and should be NUL-terminated
 * @param options a string of options. Optional. If provided, should be
 * NUL-terminated
 * @param callback the callback to invoke. Required
 */
LIBCOUCHBASE_API
void
lcb_view_query_initcmd(lcb_CMDVIEWQUERY *vq,
    const char *design, const char *view, const char *options,
    lcb_VIEWQUERYCALLBACK callback);


#ifdef __cplusplus
}
#endif

#endif
