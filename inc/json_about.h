#ifndef __JSON_ABOUT_H
#define __JSON_ABOUT_H

#include "cJSON.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 从已解析的 cJSON 对象中提取上位机命令到 Command_Msg_t
 * @param root     已解析的 cJSON 根对象（不负责释放）
 * @param cmd_msg  输出：解析后的命令结构体
 * @return 0=成功, -1=失败
 */
int json_parse_command(cJSON *root, Command_Msg_t *cmd_msg);

/**
 * @brief 创建一个标准 JSON 响应根对象（空壳）
 * @param type 响应类型（0=控制, 1=查询, 2=文件）
 * @return cJSON* 失败返回 NULL
 *
 * 生成: {"ver":0, "type":type, "status":"ok"}
 */
cJSON* json_create_response(int type);

/**
 * @brief 直接创建含文件列表的响应
 * @param files  文件名数组
 * @param count  文件数量
 * @return cJSON* 失败返回 NULL
 *
 * 生成: {"ver":0,"type":1,"status":"ok","files":["a.avi","b.avi"],"count":2}
 */
cJSON* json_create_filelist(const char *files[], int count);

/**
 * @brief 创建错误响应
 * @param type     响应类型
 * @param err_code 错误码
 * @param err_msg  错误描述
 * @return cJSON*
 *
 * 生成: {"ver":0,"type":type,"status":"error","err_code":N,"err_msg":"..."}
 */
cJSON* json_create_error(int type, int err_code, const char *err_msg);

/**
 * @brief 将 cJSON 对象通过消息系统发送给 TCP 发送线程
 *        内部会 cJSON_Print + cJSON_Delete，调用方无需再管 root
 * @param src_module  来源模块 ID
 * @param root        cJSON 对象（函数接管所有权）
 * @return 0=成功, -1=失败
 */
int json_send_response(Module_ID_e src_module, cJSON *root);

/**
 * @brief 安全获取 JSON 对象的 int 字段，带默认值
 * @param root         JSON 根对象
 * @param key          键名
 * @param default_val  如果键不存在返回的默认值
 * @return int 值
 */
int json_get_int_def(cJSON *root, const char *key, int default_val);

#ifdef __cplusplus
}
#endif

#endif // __JSON_ABOUT_H