/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
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
 */

#ifndef LCB_METRICS_H
#define LCB_METRICS_H

#ifdef __cplusplus
extern "C" {
#endif

struct lcb_METRICS_st;

typedef struct lcb_IOMETRICS_st {
    const char *hostport;
    size_t io_close;
    size_t io_error;
    size_t bytes_sent;
    size_t bytes_received;
} lcb_IOMETRICS;

typedef struct lcb_SERVERMETRICS_st {
    /** IO Metrics for the underlying socket */
    lcb_IOMETRICS iometrics;

    /** Number of packets sent on this server */
    size_t packets_sent;

    /** Number of packets read on this server */
    size_t packets_read;

    /** Total number of packets placed in send queue */
    size_t packets_queued;

    /** Total number of bytes placed in send queue */
    size_t bytes_queued;

    /**
     * Number of packets which failed on this server (i.e. as a result
     * of a timeout/network error or similar)
     */
    size_t packets_errored;

    /** Number of packets which timed out. Subset of packets_errored */
    size_t packets_timeout;

    /** Number of packets received which were timed out or otherwise cancelled */
    size_t packets_ownerless;

    /** Number of NOT_MY_VBUCKET replies received */
    size_t packets_nmv;
} lcb_SERVERMETRICS;

typedef struct lcb_METRICS_st {
    size_t nservers;
    const lcb_SERVERMETRICS **servers;

    /** Number of times a packet entered the retry queue */
    size_t packets_retried;
} lcb_METRICS;

lcb_METRICS *
lcb_metrics_new(void);

void
lcb_metrics_destroy(lcb_METRICS *metrics);

lcb_SERVERMETRICS *
lcb_metrics_getserver(lcb_METRICS *metrics,
    const char *host, const char *port, int create);

#ifdef __cplusplus
}
#endif

#endif
