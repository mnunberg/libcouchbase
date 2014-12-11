#include <libcouchbase/couchbase.h>
#include <libcouchbase/q2i.h>

extern "C" {
static void cb_resp(lcb_t, int, const lcb_RESPBASE *rb)
{
    const lcb_RESPIXQUERY *resp = (const lcb_RESPIXQUERY *)rb;
    if (rb->rc == LCB_SUCCESS && (rb->rflags & LCB_RESP_F_FINAL) == 0) {
        printf("Doc ID: %.*s.", (int)resp->ndocid, resp->docid);
        printf("Key: %.*s\n", (int)resp->nkey, resp->key);
    } else {
        if (rb->rc != LCB_SUCCESS) {
            printf("Got query erorr!\n");
        } else if (rb->rc & LCB_RESP_F_FINAL) {
            printf("Got last response!\n");
        }
    }
}
}

int main(int,char**)
{
    lcb_t instance;
    lcb_create_st crst;
    memset(&crst, 0, sizeof crst);
    crst.version = 3;
    crst.v.v3.connstr = "couchbase://localhost:12000/beer-sample";
    lcb_error_t rc;
    rc = lcb_create(&instance, &crst);
    assert(rc == LCB_SUCCESS);
    rc = lcb_connect(instance);
    assert(rc == LCB_SUCCESS);
    lcb_wait(instance);
    assert(lcb_get_bootstrap_status(instance) == LCB_SUCCESS);
    lcb_install_callback3(instance, LCB_CALLBACK_Q2I, cb_resp);

    lcb_CMDIXQUERY cmd = { 0 };
    cmd.ixname = "by_name";
    cmd.nixname = strlen("by_name");

    cmd.start_key = "[\"\\u0000\"]";
    cmd.nstart_key = strlen((char*)cmd.start_key);
    cmd.end_key = "[\"\\uffff\"]";
    cmd.nend_key = strlen((char*)cmd.end_key);
    cmd.limit = 10;

    lcb_index_query(instance, NULL, &cmd);
    lcb_wait(instance);
    lcb_destroy(instance);
}
