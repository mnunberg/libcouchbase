#ifndef LCB_DCP_INTERNAL_H
#define LCB_DCP_INTERANL_H

#include "mcserver.h"

typedef struct dcp_VBSTREAM_st {
    lcb_U16 vbid;
    lcb_U64 uuid;
    lcb_U64 seqno;
    lcb_U32 opaque;
} dcp_VBSTREAM;

typedef struct dcp_CONNECTION_st {
    mc_SERVER *parent; // Parent server
    dcp_VBSTREAM **streams;
} dcp_CONNECTION;

dcp_CONNECTION *
dcp_connection_alloc(mc_SERVER *);

#endif /* LCB_DCPXX_H */
