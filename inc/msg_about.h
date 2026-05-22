#ifndef __MSG_ABOUT_H
#define __MSG_ABOUT_H

#include "common.h"
#include "msg_deliver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MOUDLE_NUM MODULE_ID_MAX

int msg_register_module(Module_ID_e module, MsgHandler_t handler, MsgReleaseHandler_t release);
int msg_unregister_module(Module_ID_e module);
void msg_module_handler(Common_Msg_t *msg);
void msg_module_release_handler(Common_Msg_t *msg);
int msg_dispatch(Module_ID_e src, Module_ID_e dst, uint32_t len, Msg_Type_e type, void *data);

#ifdef MSG_ENABLE_PRIORITY
int msg_dispatch_with_priority(Module_ID_e src,
                               Module_ID_e dst,
                               uint32_t len,
                               Msg_Type_e type,
                               Msg_Priority_e priority,
                               void *data);
#endif

#ifdef __cplusplus
}
#endif

#endif
