#include "json_about.h"
#include "msg_about.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int json_parse_command(cJSON *root, Command_Msg_t *cmd_msg)
{
    cJSON *ver_node, *mod_node, *cmd_node, *type_node, *param_node;

    if (root == NULL || cmd_msg == NULL) {
        return -1;
    }

    ver_node  = cJSON_GetObjectItemCaseSensitive(root, "ver");
    mod_node  = cJSON_GetObjectItemCaseSensitive(root, "mod");
    cmd_node  = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    type_node = cJSON_GetObjectItemCaseSensitive(root, "type");
    param_node = cJSON_GetObjectItemCaseSensitive(root, "param");

    if (!cJSON_IsNumber(ver_node) || !cJSON_IsNumber(mod_node) ||
        !cJSON_IsNumber(cmd_node) || !cJSON_IsNumber(type_node)) {
        return -1;
    }

    if (ver_node->valueint != 0) {
        return -1;
    }

    cmd_msg->cmd  = (CMD_ID)cmd_node->valueint;
    cmd_msg->src  = MODULE_ID_TCP_RECV;
    cmd_msg->type = type_node->valueint;

    /* 如果 param 存在且不是 null，携带 cJSON 节点引用到目标模块；
     * 如果 param 为 null 或不存在，置为 NULL */
    if (param_node != NULL && !cJSON_IsNull(param_node)) {
        cmd_msg->param = param_node;
    } else {
        cmd_msg->param = NULL;
    }

    return 0;
}

cJSON* json_create_response(int type)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddNumberToObject(root, "ver",  0);
    cJSON_AddNumberToObject(root, "type", type);
    cJSON_AddStringToObject(root, "status", "ok");

    return root;
}

cJSON* json_create_filelist(const char *files[], int count)
{
    cJSON *root, *arr;
    int i;

    root = json_create_response(1); /* type=1 (query) */
    if (root == NULL) return NULL;

    arr = cJSON_AddArrayToObject(root, "files");
    if (arr == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    for (i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(files[i]));
    }

    cJSON_AddNumberToObject(root, "count", count);

    return root;
}

cJSON* json_create_error(int type, int err_code, const char *err_msg)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddNumberToObject(root, "ver",      0);
    cJSON_AddNumberToObject(root, "type",     type);
    cJSON_AddStringToObject(root, "status",   "error");
    cJSON_AddNumberToObject(root, "err_code", err_code);
    if (err_msg != NULL) {
        cJSON_AddStringToObject(root, "err_msg", err_msg);
    }

    return root;
}

int json_send_response(Module_ID_e src_module, cJSON *root)
{
    char *json_str;
    size_t len;
    BigData_Msg_t *big_msg;

    if (root == NULL) return -1;

    json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root); /* root 不再需要 */

    if (json_str == NULL) return -1;

    len = strlen(json_str);

    big_msg = (BigData_Msg_t *)malloc(sizeof(BigData_Msg_t));
    if (big_msg == NULL) {
        cJSON_free(json_str);
        return -1;
    }

    big_msg->data_ptr  = json_str;
    big_msg->total_len = (uint32_t)len;
    big_msg->type      = JSON;  /* 类型标记为命令响应 */
    big_msg->status    = SEND;
    big_msg->fd        = -1;

    if (msg_dispatch(src_module, MODULE_ID_TCP_SEND,
                     (uint32_t)len, MSG_TYPE_BIGDATA, big_msg) != 0) {
        /* 消息队列满，释放资源 */
        cJSON_free(json_str);
        free(big_msg);
        return -1;
    }

    return 0;
}

int json_get_int_def(cJSON *root, const char *key, int default_val)
{
    cJSON *item;

    if (root == NULL || key == NULL) return default_val;

    item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }

    return default_val;
}