static lcbmt__schedfunc_v2
find_wrap_target2(const char *name) {
if (!strcmp(name, "lcb_get")) { return (lcbmt__schedfunc_v2)lcb_get; }
if (!strcmp(name, "lcb_store")) { return (lcbmt__schedfunc_v2)lcb_store; }
if (!strcmp(name, "lcb_remove")) { return (lcbmt__schedfunc_v2)lcb_remove; }
if (!strcmp(name, "lcb_unlock")) { return (lcbmt__schedfunc_v2)lcb_unlock; }
if (!strcmp(name, "lcb_touch")) { return (lcbmt__schedfunc_v2)lcb_touch; }
if (!strcmp(name, "lcb_arithmetic")) { return (lcbmt__schedfunc_v2)lcb_arithmetic; }
if (!strcmp(name, "lcb_server_stats")) { return (lcbmt__schedfunc_v2)lcb_server_stats; }
abort();return NULL; }
static lcbmt__schedfunc_v3
find_wrap_target3(const char *name) {
if (!strcmp(name, "lcb_get3")) { return (lcbmt__schedfunc_v3)lcb_get3; }
if (!strcmp(name, "lcb_store3")) { return (lcbmt__schedfunc_v3)lcb_store3; }
if (!strcmp(name, "lcb_remove3")) { return (lcbmt__schedfunc_v3)lcb_remove3; }
if (!strcmp(name, "lcb_unlock3")) { return (lcbmt__schedfunc_v3)lcb_unlock3; }
if (!strcmp(name, "lcb_touch3")) { return (lcbmt__schedfunc_v3)lcb_touch3; }
if (!strcmp(name, "lcb_counter3")) { return (lcbmt__schedfunc_v3)lcb_counter3; }
if (!strcmp(name, "lcb_stats3")) { return (lcbmt__schedfunc_v3)lcb_stats3; }
abort();return NULL; }
