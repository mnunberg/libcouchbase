#ifndef LIBCOUCHBASE_COUCHBASE_H
#error "include <libcouchbase/couchbase.h first>"
#endif

#ifndef LCB_DCP_H
#define LCB_DCP_H
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Structure representing the state of a single vbucket.
 */
typedef struct {
    lcb_U16 vbid; /**< vBucket number */
    lcb_U64 uuid; /**< vBucket UUID */
    lcb_U64 seqno; /**< vBucket sequence */
} lcb_DCBVBSTATE;

typedef struct {
    int mtype; /**< Mutation type which took place */
    const protocol_binary_request_header *header;
};


LIBCOUCHBASE_API
lcb_error_t lcb_start_dcp(lcb_t instance);


#ifdef __cplusplus
}
#endif
#endif
