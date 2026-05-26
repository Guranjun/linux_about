#include "sqlite_about.h"
#include "log.h"
#include <fcntl.h>
#include <unistd.h>
#define DB_PATH "/mnt/flash/syslogs.db"
#define TMP_PATH "/tmp/log_cpy_syslogs.db"

sqlite3* DB_Init(void)
{
    sqlite3* db = NULL;
    if(sqlite3_open(DB_PATH, &db) != SQLITE_OK){
        fprintf(stderr, "Can't open the database:%s", sqlite3_errmsg(db));
        return;
    }
    const char* sql = "CREATE TABLE IF NOT EXISTS logs("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "level INTEGER,"
                      "timestamp INTEGER,"
                      "module INTEGER,"
                      "content TEXT);";
    sqlite3_exec(db, sql, NULL, NULL, NULL);
    return db;
}
uint8_t get_db_count(sqlite3* db, uint16_t* out_count)
{  
    sqlite3_stmt* stmt = NULL;
    int count = 0;
    const char* sql = "SELECT COUNT(*) FROM logs;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if(rc == SQLITE_OK){
        if(sqlite3_step(stmt) == SQLITE_ROW){
            count = sqlite3_column_int(stmt, 0);
        }
        else{
            fprintf(stderr, "SQL step error: %s\n", sqlite3_errmsg(db));
            return -1;
        }
    }
    else{
        fprintf(stderr, "SQL prepare error: %s (code: %d)\n", sqlite3_errmsg(db), rc);
        return -1;
    }
    sqlite3_finalize(stmt);
    *out_count = count;
    return 0;
}
//还需健全返回值，判断是什么情况
uint8_t delete_db_msg(sqlite3* db, uint16_t delete_num)
{
    /*写一个除level = error外删除delete_num行的语句，删除普通的调试、程序信息
    不过最好分表先，建两个表，error一个表，其它的一个表
    */
    sqlite3_stmt* stmt = NULL;
    const char* sql = "DELETE FROM logs WHERE level < 3 AND id IN (SELECT id FROM logs ORDER BY id ASC LIMIT ?);";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if(rc == SQLITE_OK){
        sqlite3_bind_int(stmt, 1, delete_num);
        if(sqlite3_step(stmt) != SQLITE_DONE){
            fprintf(stderr, "Delete failed: %s\n", sqlite3_errmsg(db));
            return -1;
        }
    }
    else{
        fprintf(stderr, "SQL prepare error: %s (code: %d)\n", sqlite3_errmsg(db), rc);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

void db_save_batch(sqlite3* db, Log_Buffer_t* buffer)
{
    if(buffer->count <=0){
        return;
    }
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL,NULL);
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO logs (level, timestamp, module, content) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("Prepare error: %s\n", sqlite3_errmsg(db));
        return;
    }

    for(int i = 0; i < buffer->count; i++){
        sqlite3_bind_int(stmt, 1, buffer->items[i].level);
        sqlite3_bind_int64(stmt, 2, buffer->items[i].timestamp);
        sqlite3_bind_int(stmt, 3, buffer->items[i].module);
        sqlite3_bind_text(stmt, 4, buffer->items[i].content, -1, SQLITE_STATIC);
        if(sqlite3_step(stmt) != SQLITE_DONE){
            printf("SQLite Error: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_reset(stmt);
    }
    //printf("sql make !!!\n");
    sqlite3_finalize(stmt);
    int rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        printf("Commit Failed: %s\n", sqlite3_errmsg(db));   
    }
    buffer->count = 0;
}

void upload_db_to_server(sqlite3* src_db)
{
    sqlite3* dest_db = NULL;
    unlink(TMP_PATH);
    /* 打开目标数据库（tmp路径），如果不存在则创建 */
    if(sqlite3_open(TMP_PATH, &dest_db) != SQLITE_OK){
        fprintf(stderr, "Can't open tmp db: %s\n", sqlite3_errmsg(dest_db));
        if (dest_db){
            sqlite3_close(dest_db);
        }
        
        return;
    }

    /* 使用SQLite在线备份API，安全地从src_db拷贝到dest_db */
    sqlite3_backup* backup = sqlite3_backup_init(dest_db, "main", src_db, "main");
    if(backup == NULL){
        fprintf(stderr, "Backup init failed: %s\n", sqlite3_errmsg(dest_db));
        sqlite3_close(dest_db);
        return;
    }

    int rc;
    do {
        rc = sqlite3_backup_step(backup, 64); /* 每次拷贝64页 */
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            usleep(1000 * 10); // 10ms
            rc = SQLITE_OK;    // 强行修正状态让其继续循环
        }
    } while(rc == SQLITE_OK);

    sqlite3_backup_finish(backup);
    sqlite3_close(dest_db);

    if(rc != SQLITE_DONE){
        fprintf(stderr, "Database backup failed\n");
        unlink(TMP_PATH);
        return;
    }

    /* 打开备份文件，通过export_logs_on_demand上传 */
    int fd = open(TMP_PATH, O_RDONLY);
    if(fd < 0){
        perror("open tmp db");
        return;
    }

    export_logs_on_demand(fd);
}
