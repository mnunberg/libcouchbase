/**
 *     Copyright 2015 Couchbase, Inc.
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

#ifndef LCB_Q2I_API_H
#define LCB_Q2I_API_H
#include <libcouchbase/couchbase.h>
#include <libcouchbase/api3.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lcb_RESPIXQUERY_st lcb_RESPIXQUERY;
typedef struct lcbixq_REQUEST_st *lcb_IXQUERYHANDLE;


#define LCB_CMDIXQUERY_F_INCLUDE_DOCS 1 << 16
#define LCB_CMDIXQUERY_F_INCLUSIVE_START 1 << 17
#define LCB_CMDIXQUERY_F_INCLUSIVE_END 1 << 18
#define LCB_CMDIXQUERY_F_DISTINCT 1 << 19
#define LCB_CMDIXQUERY_F_LOOKUP 1 << 20

typedef struct {
    lcb_U32 cmdflags;

    /** Name of the index to query */
    const char *ixname;
    size_t nixname;

    /** Key from which to start delivering results */
    const char *start_key;
    size_t nstart_key;

    /** Key at which to end delivering results */
    const char *end_key;
    size_t nend_key;

    /** Output at most this number of rows */
    size_t limit;

    /** Hint to indicate how many responses at a time should be batched */
    size_t pagesize;
} lcb_CMDIXQUERY;

struct lcb_RESPIXQUERY_st {
    LCB_RESP_BASE
    const char *docid;
    size_t ndocid;
    const char *ixkey;
    size_t nixkey;
    const lcb_RESPGET *docresp;
};

typedef void (*lcb_IXQUERYCALLBACK)(lcb_t instance, int cbtype,
        const lcb_RESPIXQUERY *row);

LIBCOUCHBASE_API
lcb_error_t
lcb_index_query(lcb_t instance, const void *cookie, const lcb_CMDIXQUERY *cmd);

#ifdef __cplusplus
}
#endif
#endif
