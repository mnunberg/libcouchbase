#include <libcouchbase/couchbase.h>
#include <libcouchbase/views.h>
#include <libcouchbase/pktfwd.h>

#include "parser.h"
#include "sllist.h"

struct lcbview_REQUEST_st;

typedef struct {
    /** Special pointer for LCB_CMD_F_INTERNAL_CALLBACK */
    lcb_RESPCALLBACK callback;
    sllist_node slnode;
    struct lcbview_REQUEST_st *parent;
    lcb_RESPGET docresp;
    /** Buffer for the row data */
    char *rowbuf;

    /** Whether this request is ready to be sent to the user */
    unsigned ready;
    lcb_IOV key;
    lcb_IOV value;
    lcb_IOV docid;
} lcbview_DOCREQ;

typedef struct lcbview_REQUEST_st {
    /** Current HTTP response to provide in callbacks */
    const lcb_RESPHTTP *cur_htresp;
    /** HTTP request object, in case we need to cancel prematurely */
    struct lcb_http_request_st *htreq;
    lcbvrow_PARSER *parser;
    const void *cookie;
    lcb_VIEWQUERYCALLBACK callback;
    lcb_t instance;

    /**This queue holds requests which were not yet issued to the library
     * via lcb_get3(). This list is aggregated after each chunk callback and
     * sent as a batch*/
    sllist_root pending_gets;

    /**This queue holds the requests which were already passed to lcb_get3().
     * It is popped when the callback arrives (and is popped in order!) */
    sllist_root cb_queue;

    /**Count of total pending rows which are waiting on documents */
    unsigned ndocs_pending;

    unsigned refcount;
    unsigned include_docs;
    unsigned no_parse_rows;
    lcb_error_t lasterr;
} lcbview_REQUEST;
