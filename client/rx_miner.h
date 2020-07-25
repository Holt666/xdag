//
// Created by mac on 2020/6/16.
//

#ifndef XDAG_RX_MINER_H
#define XDAG_RX_MINER_H

#include <stdio.h>
#include "block.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tag_rx_mine_state {
	rx_state_uinit = 0,
	rx_state_initing = 1,
	rx_state_inited = 2,
	rx_state_switching = 3
} rx_mine_state;

extern int g_rx_auto_swith_pool;

/* a number of mining threads */
extern int g_rx_mining_threads;

/* changes the number of mining threads */
extern int rx_mining_start(int n_mining_threads);

/* initialization of connection the miner to pool */
extern int rx_initialize_miner(const char *pool_address);

/* miner net thread to receive rx task*/
extern void *rx_miner_net_thread(void *arg);

/* send block to network via pool */
extern int rx_send_block_via_pool(struct xdag_block *block);

/* picks random pool from the list of pools */
extern int rx_pick_pool(char *pool_address);

#ifdef __cplusplus
};
#endif

#endif //XDAG_RX_MINER_H