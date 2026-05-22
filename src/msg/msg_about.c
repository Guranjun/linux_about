#include "msg_about.h"

#include <string.h>

static MsgRouteTable_t msg_route_table[MAX_MOUDLE_NUM];
static uint8_t msg_route_valid[MAX_MOUDLE_NUM];

static MsgRouteTable_t *find_route(Module_ID_e id)
{
    if ((id < 0) || (id >= MODULE_ID_MAX)) {
        return NULL;
    }

    if (!msg_route_valid[id]) {
        return NULL;
    }

    return &msg_route_table[id];
}

int msg_register_module(Module_ID_e module, MsgHandler_t handler, MsgReleaseHandler_t release)
{
    if ((module < 0) || (module >= MODULE_ID_MAX)) {
        return -1;
    }

    memset(&msg_route_table[module], 0, sizeof(msg_route_table[module]));
    msg_route_table[module].mod_id = module;
    msg_route_table[module].handler = handler;
    msg_route_table[module].release_handler = release;
    msg_route_valid[module] = 1U;

    return 0;
}
int msg_unregister_module(Module_ID_e module) 
{
    if ((Module_ID_e)module >= MODULE_ID_MAX) {
        return -1;
    }
    msg_route_valid[module] = 0U; // 让路由失效
    memset(&msg_route_table[module], 0, sizeof(msg_route_table[module]));
    return 0;
}
void msg_module_handler(Common_Msg_t *msg)
{
    MsgRouteTable_t *route;

    if (msg == NULL) {
        return;
    }

    route = find_route(msg->dst_module);
    if (route != NULL && route->handler != NULL) {
        route->handler(msg);
    }
}

void msg_module_release_handler(Common_Msg_t *msg)
{
    MsgRouteTable_t *route;

    if (msg == NULL) {
        return;
    }

    route = find_route(msg->src_module);
    if (route != NULL && route->release_handler != NULL) {
        route->release_handler(msg);
    }
}

int msg_dispatch(Module_ID_e src, Module_ID_e dst, uint32_t len, Msg_Type_e type, void *data)
{
    Common_Msg_t msg = msg_make(src, dst, len, type, data);

    return msg_send(&msg);
}

#ifdef MSG_ENABLE_PRIORITY
int msg_dispatch_with_priority(Module_ID_e src,
                               Module_ID_e dst,
                               uint32_t len,
                               Msg_Type_e type,
                               Msg_Priority_e priority,
                               void *data)
{
    Common_Msg_t msg = msg_make_with_priority(src, dst, len, type, priority, data);

    return msg_send(&msg);
}
#endif
