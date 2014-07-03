#include "common/options.h"
#include <libcouchbase/api3.h>
#include <libcouchbase/mt.h>
#include <signal.h>
#include <pthread.h>
#include <iostream>

using namespace cbc;
using namespace cliopts;
using std::vector;
using std::string;

struct WorkerThread {
    pthread_t thr;
    lcb_t instance;
    int id;
};

extern "C" {
static void on_get(lcb_t, int, const lcb_RESPBASE *)
{
    ; // nothing
}

static void on_store(lcb_t, int, const lcb_RESPBASE *)
{
    ; // nothing
}


static void *pthrfunc(void *arg)
{
    WorkerThread *w = (WorkerThread *)arg;
    lcb_t instance = w->instance;
    lcbmt_WAITHANDLE wh = lcbmt_wh_create(instance);
    lcb_CMDGET gcmd = { 0 };

    char key[4096] = { 0 };
    sprintf(key, "MTBENCH_%d", w->id);
    size_t nkey = strlen(key);

    lcb_CMDSTORE scmd = { 0 };
    scmd.operation = LCB_SET;
    LCB_CMD_SET_KEY(&scmd, key, nkey);
    LCB_CMD_SET_VALUE(&scmd, key, nkey);

    lcbmt_wh_prepare(wh, 0);
    lcb_store3(instance, wh, &scmd);
    lcbmt_wh_incr(wh);
    lcbmt_wh_wait(wh);

    LCB_CMD_SET_KEY(&gcmd, key, nkey);

    while (1) {
        lcbmt_wh_prepare(wh, 0);
        lcb_get3(instance, wh, &gcmd);
        lcbmt_wh_incr(wh);
        lcbmt_wh_wait(wh);
    }
    return NULL;
}
}

static void exit_on_sig(int signum){
    exit(0);
}

int main(int argc, char **argv)
{
    ConnParams params;
    Parser parser;
    IntOption nThreads("threads");
    lcb_create_st cropts;
    unsigned ii;
    lcb_t instance;
    lcb_error_t err;
    vector<WorkerThread> threads;

    nThreads.description("Number of threads to spawn");
    nThreads.setDefault(10);
    parser.addOption(nThreads);

    params.addToParser(parser);
    parser.parse(argc, argv, false);
    params.fillCropts(cropts);

    signal(SIGINT, exit_on_sig);
    err = lcbmt_create(&instance, &cropts);
    assert(err == LCB_SUCCESS);
    params.doCtls(instance);

    lcbmt_connect(instance);
    lcbmt_install_callback(instance, LCB_CALLBACK_GET, on_get);
    lcbmt_install_callback(instance, LCB_CALLBACK_STORE, on_store);

    for (ii = 0; ii < nThreads.result(); ii++) {
        threads.push_back(WorkerThread());
        WorkerThread& cur = threads.back();
        cur.instance = instance;
        cur.id = ii;
    }

    for (ii = 0; ii < threads.size(); ii++) {
        pthread_create(&threads[ii].thr, NULL, pthrfunc, &threads[ii]);
    }
    for (ii = 0; ii < threads.size(); ii++) {
        void *res = NULL;
        pthread_join(threads[ii].thr, &res);
    }
    return 0;
}