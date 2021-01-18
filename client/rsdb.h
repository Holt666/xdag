#ifndef XDAG_RSDB_H
#define XDAG_RSDB_H

#include <stdint.h>
#include <stdio.h>
#include <rocksdb/c.h>
#include "types.h"
#include "hash.h"
#include "system.h"
#include "block.h"
#include "sync.h"

#define RSDB_KEY_LEN sizeof(xdag_hashlow_t)
#define RSDB_MAX_COLUMN 10

typedef struct rocksdb_column_family_handle_t* xd_rsdb_column_handle;

typedef struct xd_rsdb_column_familys {
    char *colum_name[RSDB_MAX_COLUMN];
    rocksdb_options_t *column_option[RSDB_MAX_COLUMN];
    xd_rsdb_column_handle column_handle[RSDB_MAX_COLUMN];
} xd_rsdb_cf_t;

typedef struct xdag_rsdb_conf {
    char                              *db_name;
    char                              *db_path;
} xd_rsdb_conf_t;

typedef struct xdag_rsdb {
    xd_rsdb_conf_t                    *config;
    rocksdb_options_t                *options;
    rocksdb_t                             *db;
    xd_rsdb_cf_t                          *cf;
} xd_rsdb_t;

typedef enum xd_rsdb_op_result {
    XDAG_RSDB_OP_SUCCESS                  =  0,
    XDAG_RSDB_NULL                        =  1,
    XDAG_RSDB_CONF_ERROR                  =  2,
    XDAG_RSDB_OPEN_ERROR                  =  3,
    XDAG_RSDB_PUT_ERROR                   =  4,
    XDAG_RSDB_DELETE_ERROR                =  5
} xd_rsdb_op_t;

typedef enum xd_rsdb_column_type {
    SETTING                               =  0x00,
    HASH_ORP_BLOCK                        =  0x01,
    HASH_EXT_BLOCK                        =  0x02,
    HASH_BLOCK_INTERNAL                   =  0x03,
    HASH_BLOCK_OUR                        =  0x04,
    HASH_BLOCK_REMARK                     =  0x05,
    HASH_BLOCK_BACKREF                    =  0x06,
    HASH_BLOCK_CACHE                      =  0x07,
    HEIGHT_BLOCK                          =  0x08,
    COLUMN_DEFAULT                        =  0x09
} xd_rsdb_column_type_t;

typedef enum xd_rsdb_setting_type {
    SETTING_VERSION                       =  0x01,
    SETTING_CREATED                       =  0x02,
    SETTING_STATS                         =  0x03,
    SETTING_EXT_STATS                     =  0x04,
    SETTING_PRE_TOP_MAIN                  =  0x05,
    SETTING_TOP_MAIN                      =  0x06,
    SETTING_OUR_FIRST_HASH                =  0x07,
    SETTING_OUR_LAST_HASH                 =  0x08,
    SETTING_OUR_BALANCE                   =  0x09,
    SETTING_CUR_TIME                      =  0x10
} xd_rsdb_setting_type_t;

xd_rsdb_op_t xd_rsdb_pre_init(int read_only);
xd_rsdb_op_t xd_rsdb_init(xdag_time_t *time);
xd_rsdb_op_t xd_rsdb_load(xd_rsdb_t* db);
xd_rsdb_op_t xd_rsdb_conf_check(xd_rsdb_t  *db);
xd_rsdb_op_t xd_rsdb_column_conf(xd_rsdb_t  *db);
xd_rsdb_op_t xd_rsdb_conf(xd_rsdb_t* db);
xd_rsdb_op_t xd_rsdb_open(xd_rsdb_t* db, int);
xd_rsdb_op_t xd_rsdb_close(xd_rsdb_t* db);

//get
void* xd_rsdb_getkey(xd_rsdb_column_handle column_handle, const char* key, const size_t klen, size_t* vlen);
xd_rsdb_op_t xd_rsdb_get_bi(xdag_hashlow_t hash, struct block_internal*);
xd_rsdb_op_t xd_rsdb_get_ournext(xdag_hashlow_t hash, xdag_hashlow_t next);
xd_rsdb_op_t xd_rsdb_get_orpblock(xdag_hashlow_t hash, struct xdag_block*);
xd_rsdb_op_t xd_rsdb_get_extblock(xdag_hashlow_t hash, struct xdag_block*);
xd_rsdb_op_t xd_rsdb_get_cacheblock(xdag_hashlow_t hash, struct xdag_block *xb);
xd_rsdb_op_t xd_rsdb_get_stats(void);
xd_rsdb_op_t xd_rsdb_get_extstats(void);
xd_rsdb_op_t xd_rsdb_get_remark(xdag_hashlow_t hash, xdag_remark_t);
xd_rsdb_op_t xd_rsdb_get_heighthash(uint64_t height, xdag_hashlow_t hash);

//put
xd_rsdb_op_t xd_rsdb_put_bi(struct block_internal *bi);
xd_rsdb_op_t xd_rsdb_putkey(xd_rsdb_column_handle column_handle, const char* key, size_t klen, const char* value, size_t vlen);
xd_rsdb_op_t xd_rsdb_put_backref(xdag_hashlow_t backref, struct block_internal*);
xd_rsdb_op_t xd_rsdb_put_ournext(xdag_hashlow_t hash, xdag_hashlow_t next);
xd_rsdb_op_t xd_rsdb_put_setting(xd_rsdb_setting_type_t type, const char* value, size_t vlen);
xd_rsdb_op_t xd_rsdb_put_orpblock(xdag_hashlow_t hash, struct xdag_block* xb);
xd_rsdb_op_t xd_rsdb_put_extblock(xdag_hashlow_t hash, struct xdag_block* xb);
xd_rsdb_op_t xd_rsdb_put_stats(xdag_time_t time);
xd_rsdb_op_t xd_rsdb_put_extstats(void);
xd_rsdb_op_t xd_rsdb_put_remark(struct block_internal *bi, xdag_remark_t strbuf);
xd_rsdb_op_t xd_rsdb_put_cacheblock(xdag_hashlow_t hash, struct xdag_block *xb);
xd_rsdb_op_t xd_rsdb_put_heighthash(uint64_t height, xdag_hashlow_t hash);

//del
xd_rsdb_op_t xd_rsdb_delkey(xd_rsdb_column_handle column_handle, const char* key, size_t klen);
xd_rsdb_op_t xd_rsdb_del_bi(xdag_hashlow_t hash);
xd_rsdb_op_t xd_rsdb_del_orpblock(xdag_hashlow_t hash);
xd_rsdb_op_t xd_rsdb_del_extblock(xdag_hashlow_t hash);
xd_rsdb_op_t xd_rsdb_del_heighthash(uint64_t height);
xd_rsdb_op_t xd_rsdb_merge_bi(struct block_internal* bi);

#endif
