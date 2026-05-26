#include "common.h"
#include "file_query.h"
#include "json_about.h"
#include "msg_about.h"
#include "my_time.h"

#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_AVI_FILES 256

static Log_Msg_t log_msg;

int scan_avi_files(const char *path, const char ***out_files, int *out_count)
{
    char pattern[256];
    glob_t gbuf;
    int ret;
    const char **files = NULL;
    int count = 0;

    if (path == NULL || out_files == NULL || out_count == NULL) return -1;

    snprintf(pattern, sizeof(pattern), "%s/*.avi", path);

    ret = glob(pattern, GLOB_NOSORT, NULL, &gbuf);
    if (ret != 0) {
        /* 没有匹配文件或目录不存在，返回空列表 */
        *out_files = NULL;
        *out_count = 0;
        return 0;
    }

    count = (int)gbuf.gl_pathc;
    if (count > MAX_AVI_FILES) {
        count = MAX_AVI_FILES;
    }

    files = (const char **)malloc((size_t)count * sizeof(char *));
    if (files == NULL) {
        globfree(&gbuf);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        const char *fullpath = gbuf.gl_pathv[i];
        const char *basename = strrchr(fullpath, '/');
        if (basename != NULL) {
            basename++;
        } else {
            basename = fullpath;
        }
        files[i] = strdup(basename);
        if (files[i] == NULL) {
            /* 个别 strdup 失败不影响整体 */
        }
    }

    globfree(&gbuf);

    *out_files = files;
    *out_count = count;
    return 0;
}

void free_file_list(const char **files, int count)
{
    if (files == NULL) return;
    for (int i = 0; i < count; i++) {
        if (files[i] != NULL) {
            free((void *)files[i]);
        }
    }
    free(files);
}

void handle_query_files(void)
{
    const char **files = NULL;
    int count = 0;
    cJSON *root;

    if (scan_avi_files("/mnt/sdcard", &files, &count) != 0) {
        log_make(&log_msg, LOG_ERROR, gettime_us(),
                 MODULE_ID_COMMAND, "scan avi failed");
        msg_dispatch(MODULE_ID_COMMAND, MODULE_ID_LOGGER,
                     sizeof(log_msg), MSG_TYPE_LOG, &log_msg);
        root = json_create_error(1, -1, "scan failed");
    } else {
        root = json_create_filelist(files, count);
        free_file_list(files, count);
    }

    if (root != NULL) {
        json_send_response(MODULE_ID_COMMAND, root);
    }
}