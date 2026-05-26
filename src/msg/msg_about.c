#include "msg_about.h"
#include <string.h>

static MsgRouteTable_t msg_route_table[MAX_MOUDLE_NUM];
static uint8_t msg_route_valid[MAX_MOUDLE_NUM];

/**
 * @brief 根据模块 ID 查找路由表项
 * @param id 模块 ID
 * @return 路由表项指针，未找到返回 NULL
 */
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

/**
 * @brief 注册模块的消息处理函数
 * @param module  模块 ID
 * @param handler 消息处理函数指针
 * @param release 消息释放处理函数指针
 * @return 0=成功, -1=失败（模块 ID 无效）
 */
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

/**
 * @brief 注销模块的消息处理函数
 * @param module 模块 ID
 * @return 0=成功, -1=失败（模块 ID 无效）
 */
int msg_unregister_module(Module_ID_e module) 
{
    if ((Module_ID_e)module >= MODULE_ID_MAX) {
        return -1;
    }
    msg_route_valid[module] = 0U;
    memset(&msg_route_table[module], 0, sizeof(msg_route_table[module]));
    return 0;
}

/**
 * @brief 对消息执行目标模块的处理函数
 * @param msg 待处理的消息
 */
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

/**
 * @brief 对消息执行来源模块的释放处理函数
 * @param msg 待释放的消息
 */
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

/**
 * @brief 发送消息到指定目标模块
 * @param src  来源模块 ID
 * @param dst  目标模块 ID
 * @param len  数据长度
 * @param type 消息类型
 * @param data 数据指针
 * @return 0=成功, -1=失败
 */
int msg_dispatch(Module_ID_e src, Module_ID_e dst, uint32_t len, Msg_Type_e type, void *data)
{
    Common_Msg_t msg = msg_make(src, dst, len, type, data);
    return msg_send(&msg);
}

#ifdef MSG_ENABLE_PRIORITY
/**
 * @brief 发送带优先级的消息到指定目标模块
 * @param src      来源模块 ID
 * @param dst      目标模块 ID
 * @param len      数据长度
 * @param type     消息类型
 * @param priority 优先级
 * @param data     数据指针
 * @return 0=成功, -1=失败
 */
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