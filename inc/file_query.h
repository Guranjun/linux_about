#ifndef __FILE_QUERY_H
#define __FILE_QUERY_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 扫描指定目录下的所有 .avi 文件
 * @param path      扫描路径，如 "/mnt/sdcard"
 * @param out_files 输出：文件名数组（不含路径），需调用 free_file_list 释放
 * @param out_count 输出：文件数量
 * @return 0=成功, -1=失败
 */
int scan_avi_files(const char *path, const char ***out_files, int *out_count);

/**
 * @brief 释放 scan_avi_files 返回的文件列表
 */
void free_file_list(const char **files, int count);

/**
 * @brief 处理查询文件列表命令 (CMD_STORAGE_QUERY_FILES)
 *        内部会调用 json_send_response 发送结果
 */
void handle_query_files(void);

#ifdef __cplusplus
}
#endif

#endif // __FILE_QUERY_H