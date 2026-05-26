#ifndef __SQLITE_ABOUT_H
#define __SQLITE_ABOUT_H
#include "common.h"
#include "log.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
sqlite3* DB_Init(void);
uint8_t get_db_count(sqlite3* db, uint16_t* out_count);
uint8_t delete_db_msg(sqlite3* db, uint16_t delete_num);
void db_save_batch(sqlite3* db, Log_Buffer_t* buffer);
void upload_db_to_server(sqlite3* src_db);
#endif //__SQLITE_ABOUT_H