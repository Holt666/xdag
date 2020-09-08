#include "global.h"

int g_xdag_state = XDAG_STATE_INIT;
int g_xdag_testnet = 0;
int g_xdag_run = 0;
enum xdag_field_type g_block_header_type = XDAG_FIELD_HEAD;
struct xdag_stats g_xdag_stats;
struct xdag_ext_stats g_xdag_extstats;
int g_disable_mining = 0;
enum xdag_type g_xdag_type = XDAG_POOL;
enum xdag_mine_type g_xdag_mine_type = XDAG_RAW;
enum randomx_mode g_xdag_rx_mode = RANDOMX_LIGHT;
char *g_coinname, *g_progname;
xd_rsdb_t  *g_xdag_rsdb = NULL;
xdag_hashlow_t g_ourfirst_hash = {0};
xdag_hashlow_t g_ourlast_hash = {0};
xdag_amount_t g_balance = 0;
struct block_internal top_main_chain;
struct block_internal pretop_main_chain;

inline int is_pool(void) { return g_xdag_type == XDAG_POOL; }
inline int is_wallet(void) { return g_xdag_type == XDAG_WALLET; }
