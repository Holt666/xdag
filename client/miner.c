/* пул и майнер, T13.744-T14.390 $DVS:time$ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "system.h"
#include "dfslib_crypt.h"
#include "address.h"
#include "block.h"
#include "global.h"
#include "miner.h"
#include "sync.h"
#include "transport.h"
#include "mining_common.h"
#include "network.h"
#include "algorithms/crc.h"
#include "utils/log.h"
#include "utils/utils.h"
#include "random_utils.h"
#include <randomx.h>
#include "rx_mine_hash.h"

#define MINERS_PWD             "minersgonnamine"
#define SECTOR0_BASE           0x1947f3acu
#define SECTOR0_OFFSET         0x82e9d1b5u
#define SEND_PERIOD            10                                  /* share period of sending shares */
#define POOL_LIST_FILE         (g_xdag_testnet ? "pools-testnet.txt" : "pools.txt")

int g_xdag_auto_swith_pool = 0;

struct miner {
	struct xdag_field id;
	uint64_t nfield_in;
	uint64_t nfield_out;
};

static struct miner g_local_miner;
static pthread_mutex_t g_miner_mutex = PTHREAD_MUTEX_INITIALIZER;
static xdag_hash_t g_fixed_miner_seed;
/* a number of mining threads */
int g_xdag_mining_threads = 0;

static int g_socket = -1, g_stop_mining = 1;

randomx_cache *rx_cache = NULL;
randomx_dataset *rx_mine_dataset = NULL;


static int can_send_share(time_t current_time, time_t task_time, time_t share_time)
{
	int can_send = current_time - share_time >= SEND_PERIOD && current_time - task_time <= 64;
	if(g_xdag_mining_threads == 0 && share_time >= task_time) {
		can_send = 0;  //we send only one share per task if mining is turned off
	}
	return can_send;
}

/* initialization of connection the miner to pool */
extern int xdag_initialize_miner(const char *pool_address)
{
	int err=0;
	pthread_t th;

	memset(&g_local_miner, 0, sizeof(struct miner));
	xdag_get_our_block(g_local_miner.id.data);

	if(g_xdag_mine_type==XDAG_RANDOMX){
		err = pthread_create(&th, 0, rx_miner_net_thread, (void*)pool_address);
	}else{
		err = pthread_create(&th, 0, miner_net_thread, (void*)pool_address);
	}

	if(err != 0) {
		printf("create miner net thread failed, error : %s\n", strerror(err));
		return -1;
	}

	err = pthread_detach(th);
	if(err != 0) {
		printf("detach miner net thread failed, error : %s\n", strerror(err));
		//return -1; //fixme: not sure why pthread_detach return 3
	}

	return 0;
}

static int send_to_pool(struct xdag_field *fld, int nfld)
{
	struct xdag_field f[XDAG_BLOCK_FIELDS];
	xdag_hash_t h;
	struct miner *m = &g_local_miner;
	int todo = nfld * sizeof(struct xdag_field), done = 0;

	if(g_socket < 0) {
		return -1;
	}

	memcpy(f, fld, todo);

	if(nfld == XDAG_BLOCK_FIELDS) {
		f[0].transport_header = 0;

		xdag_hash(f, sizeof(struct xdag_block), h);

		f[0].transport_header = BLOCK_HEADER_WORD;

		uint32_t crc = crc_of_array((uint8_t*)f, sizeof(struct xdag_block));

		f[0].transport_header |= (uint64_t)crc << 32;
	}

	for(int i = 0; i < nfld; ++i) {
		dfslib_encrypt_array(g_crypt, (uint32_t*)(f + i), DATA_SIZE, m->nfield_out++);
	}

	while(todo) {
		struct pollfd p;

		p.fd = g_socket;
		p.events = POLLOUT;

		if(!poll(&p, 1, 1000)) continue;

		if(p.revents & (POLLHUP | POLLERR)) {
			return -1;
		}

		if(!(p.revents & POLLOUT)) continue;

		int res = write(g_socket, (uint8_t*)f + done, todo);
		if(res <= 0) {
			return -1;
		}

		done += res;
		todo -= res;
	}

	if(nfld == XDAG_BLOCK_FIELDS) {
		xdag_info("Sent  : %016llx%016llx%016llx%016llx t=%llx res=%d",
			h[3], h[2], h[1], h[0], fld[0].time, 0);
	}

	return 0;
}

void *miner_net_thread(void *arg)
{
	struct xdag_block b;
	struct xdag_field data[2];
	xdag_hash_t hash;
	char pool_address[50] = {0};
	strncpy(pool_address, (const char*)arg, 49);
	const char *mess = NULL;
	int res = 0;
	int64_t pos=0;
	xtime_t t;
	struct miner *m = &g_local_miner;

	while(!g_xdag_sync_on) {
		sleep(1);
	}

begin:
	m->nfield_in = m->nfield_out = 0;

	int ndata = 0;
	int maxndata = sizeof(struct xdag_field);
	time_t share_time = 0;
	time_t task_time = 0;

	if(g_miner_address) {
		if(xdag_address2hash(g_miner_address, hash)) {
			mess = "incorrect miner address";
			goto err;
		}
	} else if(xdag_get_our_block(hash)) {
		mess = "can't create a block";
		goto err;
	}

	pos = xdag_get_block_pos(hash, &t, &b);
	if (pos == -2l) {
		;
	} else if (pos < 0) {
		mess = "can't find the block";
		goto err;
	} else {
		xdag_info("%s load block hash %016llx%016llx%016llx%016llx time %llu pos %llu ",__FUNCTION__,
							hash[3],hash[2],hash[1],hash[0],t,pos);
		struct xdag_block *blk = xdag_storage_load(hash, t, pos, &b);
		if(!blk) {
			mess = "can't load the block";
			goto err;
		}
		if(blk != &b) memcpy(&b, blk, sizeof(struct xdag_block));
	}

	pthread_mutex_lock(&g_miner_mutex);
	g_socket = xdag_connect_pool(pool_address, &mess);
	if(g_socket == INVALID_SOCKET) {
		pthread_mutex_unlock(&g_miner_mutex);
		if(g_xdag_auto_swith_pool) {
			if(!xdag_pick_pool(pool_address)) {
				mess = "no active pool available";
			}
		} else {
			mess = "connect pool failed.";
		}
		goto err;
	} else {
		xdag_mess("connected to pool %s", pool_address);
	}

	if(send_to_pool(b.field, XDAG_BLOCK_FIELDS) < 0) {
		mess = "socket is closed";
		pthread_mutex_unlock(&g_miner_mutex);
		goto err;
	}
	pthread_mutex_unlock(&g_miner_mutex);

	for(;;) {
		struct pollfd p;

		pthread_mutex_lock(&g_miner_mutex);

		if(g_socket < 0) {
			pthread_mutex_unlock(&g_miner_mutex);
			mess = "socket is closed";
			goto err;
		}

		p.fd = g_socket;
		time_t current_time = time(0);
		p.events = POLLIN | (can_send_share(current_time, task_time, share_time) ? POLLOUT : 0);

		if(!poll(&p, 1, 0)) {
			pthread_mutex_unlock(&g_miner_mutex);
			sleep(1);
			continue;
		}

		if(p.revents & POLLHUP) {
			pthread_mutex_unlock(&g_miner_mutex);
			mess = "socket hangup";
			goto err;
		}

		if(p.revents & POLLERR) {
			pthread_mutex_unlock(&g_miner_mutex);
			mess = "socket error";
			goto err;
		}

		if(p.revents & POLLIN) {
			res = read(g_socket, (uint8_t*)data + ndata, maxndata - ndata);
			if(res < 0) {
				pthread_mutex_unlock(&g_miner_mutex); mess = "read error on socket"; goto err;
			}
			ndata += res;
			if(ndata == maxndata) {
				struct xdag_field *last = data + (ndata / sizeof(struct xdag_field) - 1);

				dfslib_uncrypt_array(g_crypt, (uint32_t*)last->data, DATA_SIZE, m->nfield_in++);

				if(!memcmp(last->data, hash, sizeof(xdag_hashlow_t))) {
					xdag_set_balance(hash, last->amount);

					atomic_store_explicit_uint_least64(&g_xdag_last_received, current_time, memory_order_relaxed);

					ndata = 0;

					maxndata = sizeof(struct xdag_field);
				} else if(maxndata == 2 * sizeof(struct xdag_field)) {
					const uint64_t task_index = g_xdag_pool_task_index + 1;
					struct xdag_pool_task *task = &g_xdag_pool_task[task_index & 1];

					task->task_time = xdag_get_frame();
					xdag_hash_set_state(task->ctx, data[0].data,
						sizeof(struct xdag_block) - 2 * sizeof(struct xdag_field));
					xdag_hash_update(task->ctx, data[1].data, sizeof(struct xdag_field));
					xdag_hash_update(task->ctx, hash, sizeof(xdag_hashlow_t));

					GetRandBytes(task->nonce.data, sizeof(xdag_hash_t));

					memcpy(task->nonce.data, hash, sizeof(xdag_hashlow_t));
					memcpy(task->lastfield.data, task->nonce.data, sizeof(xdag_hash_t));

					xdag_hash_final(task->ctx, &task->nonce.amount, sizeof(uint64_t), task->minhash.data);

					g_xdag_pool_task_index = task_index;
					task_time = time(0);

					xdag_info("Task  : t=%llx N=%llu", task->task_time << 16 | 0xffff, task_index);

					ndata = 0;
					maxndata = sizeof(struct xdag_field);
				} else {
					maxndata = 2 * sizeof(struct xdag_field);
				}
			}
		}

		if(p.revents & POLLOUT) {
			const uint64_t task_index = g_xdag_pool_task_index;
			struct xdag_pool_task *task = &g_xdag_pool_task[task_index & 1];
			uint64_t *h = task->minhash.data;

			share_time = time(0);
			res = send_to_pool(&task->lastfield, 1);
			pthread_mutex_unlock(&g_miner_mutex);

			xdag_info("Share : %016llx%016llx%016llx%016llx t=%llx res=%d",
				h[0], h[1], h[2], h[3], task->task_time << 16 | 0xffff, res);

			if(res) {
				mess = "write error on socket"; goto err;
			}
		} else {
			pthread_mutex_unlock(&g_miner_mutex);
		}
	}

	return 0;

err:
	xdag_err("Miner: %s (error %d)", mess, res);

	pthread_mutex_lock(&g_miner_mutex);

	if(g_socket != INVALID_SOCKET) {
		close(g_socket);
		g_socket = INVALID_SOCKET;
	}

	pthread_mutex_unlock(&g_miner_mutex);

	sleep(10);

	goto begin;
}

void *rx_miner_net_thread(void *arg)
{
	struct xdag_block b;
	struct xdag_field data[2];
	xdag_hash_t hash;
	char pool_address[50] = {0};
	strncpy(pool_address, (const char*)arg, 49);
	const char *mess = NULL;
	int res = 0;
	int64_t pos=0;
	xtime_t t;
	struct miner *m = &g_local_miner;

	while(!g_xdag_sync_on) {
		sleep(1);
	}

	begin:
	m->nfield_in = m->nfield_out = 0;

	int ndata = 0;
	int maxndata = sizeof(struct xdag_field);
	time_t share_time = 0;
	time_t task_time = 0;

	if(g_miner_address) {
		if(xdag_address2hash(g_miner_address, hash)) {
			mess = "incorrect miner address";
			goto err;
		}
	} else if(xdag_get_our_block(hash)) {
		mess = "can't create a block";
		goto err;
	}

	pos = xdag_get_block_pos(hash, &t, &b);
	if (pos == -2l) {
		;
	} else if (pos < 0) {
		mess = "can't find the block";
		goto err;
	} else {
		xdag_info("%s load block hash %016llx%016llx%016llx%016llx time %llu pos %llu ",__FUNCTION__,
		          hash[3],hash[2],hash[1],hash[0],t,pos);
		struct xdag_block *blk = xdag_storage_load(hash, t, pos, &b);
		if(!blk) {
			mess = "can't load the block";
			goto err;
		}
		if(blk != &b) memcpy(&b, blk, sizeof(struct xdag_block));
	}

	pthread_mutex_lock(&g_miner_mutex);
	g_socket = xdag_connect_pool(pool_address, &mess);
	if(g_socket == INVALID_SOCKET) {
		pthread_mutex_unlock(&g_miner_mutex);
		if(g_xdag_auto_swith_pool) {
			if(!xdag_pick_pool(pool_address)) {
				mess = "no active pool available";
			}
		} else {
			mess = "connect pool failed.";
		}
		goto err;
	} else {
		xdag_mess("connected to pool %s", pool_address);
	}

	if(send_to_pool(b.field, XDAG_BLOCK_FIELDS) < 0) {
		mess = "socket is closed";
		pthread_mutex_unlock(&g_miner_mutex);
		goto err;
	}
	pthread_mutex_unlock(&g_miner_mutex);

	for(;;) {
		struct pollfd p;

		pthread_mutex_lock(&g_miner_mutex);

		if(g_socket < 0) {
			pthread_mutex_unlock(&g_miner_mutex);
			mess = "socket is closed";
			goto err;
		}

		p.fd = g_socket;
		time_t current_time = time(0);
		p.events = POLLIN | (can_send_share(current_time, task_time, share_time) ? POLLOUT : 0);

		if(!poll(&p, 1, 0)) {
			pthread_mutex_unlock(&g_miner_mutex);
			sleep(1);
			continue;
		}

		if(p.revents & POLLHUP) {
			pthread_mutex_unlock(&g_miner_mutex);
			mess = "socket hangup";
			goto err;
		}

		if(p.revents & POLLERR) {
			pthread_mutex_unlock(&g_miner_mutex);
			mess = "socket error";
			goto err;
		}

		if(p.revents & POLLIN) {
			res = read(g_socket, (uint8_t*)data + ndata, maxndata - ndata);
			if(res < 0) {
				pthread_mutex_unlock(&g_miner_mutex); mess = "read error on socket"; goto err;
			}
			ndata += res;
			if(ndata == maxndata) {
				struct xdag_field *last = data + (ndata / sizeof(struct xdag_field) - 1);

				dfslib_uncrypt_array(g_crypt, (uint32_t*)last->data, DATA_SIZE, m->nfield_in++);

				if(!memcmp(last->data, hash, sizeof(xdag_hashlow_t))) {
					xdag_set_balance(hash, last->amount);

					atomic_store_explicit_uint_least64(&g_xdag_last_received, current_time, memory_order_relaxed);

					ndata = 0;

					maxndata = sizeof(struct xdag_field);
				} else if(maxndata == 2 * sizeof(struct xdag_field)) {
					const uint64_t task_index = g_xdag_pool_task_index + 1;
					struct xdag_pool_task *task = &g_xdag_pool_task[task_index & 1];

					task->task_time = xdag_get_frame();
					memcpy(task->task[0].data,data[0].data, sizeof(xdag_hash_t));
					memcpy(task->task[1].data,data[1].data,sizeof(xdag_hash_t));

					//xdag_info("recv pre %016llx%016llx%016llx%016llx",data[0].data[0],data[0].data[1],data[0].data[2],data[0].data[3]);
					xdag_info("use fixed seed %016llx%016llx%016llx%016llx",
							g_fixed_miner_seed[0],g_fixed_miner_seed[1],g_fixed_miner_seed[2],g_fixed_miner_seed[3]);
					xdag_info("recv seed %016llx%016llx%016llx%016llx",data[1].data[0],data[1].data[1],data[1].data[2],data[1].data[3]);
					xdag_info("copy our block hash %016llx%016llx%016llx%016llx to lastfield data",hash[0],hash[1],hash[2],hash[3]);
					GetRandBytes(task->nonce.data, sizeof(xdag_hash_t));
					memcpy(task->nonce.data, hash, sizeof(xdag_hashlow_t));
					memcpy(task->lastfield.data, task->nonce.data, sizeof(xdag_hash_t));

					xdag_rx_mine_first_hash(g_fixed_miner_seed,sizeof(g_fixed_miner_seed),
							task->task[0].data,task->lastfield.data,task->lastfield.amount,task->minhash.data);

					xdag_info("task minhash first %016llx%016llx%016llx%016llx",
					          task->minhash.data[0],task->minhash.data[1],task->minhash.data[2],task->minhash.data[3]);
					g_xdag_pool_task_index = task_index;
					task_time = time(0);

					xdag_info("rx mine task  : t=%llx N=%llu", task->task_time << 16 | 0xffff, task_index);

					ndata = 0;
					maxndata = sizeof(struct xdag_field);
				} else {
					maxndata = 2 * sizeof(struct xdag_field);
				}
			}
		}

		if(p.revents & POLLOUT) {

				const uint64_t task_index = g_xdag_pool_task_index;
				struct xdag_pool_task *task = &g_xdag_pool_task[task_index & 1];
				uint64_t *h = task->minhash.data;

				share_time = time(0);
				res = send_to_pool(&task->lastfield, 1);
				pthread_mutex_unlock(&g_miner_mutex);

				uint64_t *d=(uint64_t *)&task->lastfield;

				xdag_info("share seed %016llx%016llx%016llx%016llx",
						g_fixed_miner_seed[0],g_fixed_miner_seed[1],g_fixed_miner_seed[2],g_fixed_miner_seed[3]);
				xdag_info("share pre %016llx%016llx%016llx%016llx",
			          task->task[0].data[0],task->task[0].data[1],task->task[0].data[2],task->task[0].data[3]);
				xdag_info("share lastfield data %016llx%016llx%016llx%016llx",d[0],d[1],d[2],d[3]);
				xdag_info("share : %016llx%016llx%016llx%016llx t=%llx res=%d",
				          h[0], h[1], h[2], h[3], task->task_time << 16 | 0xffff, res);

				if(res) {
					mess = "write error on socket"; goto err;
				}
		} else {
			pthread_mutex_unlock(&g_miner_mutex);
		}
	}

	return 0;

	err:
	xdag_err("Miner: %s (error %d)", mess, res);

	pthread_mutex_lock(&g_miner_mutex);

	if(g_socket != INVALID_SOCKET) {
		close(g_socket);
		g_socket = INVALID_SOCKET;
	}

	pthread_mutex_unlock(&g_miner_mutex);

	sleep(10);

	goto begin;
}

static void *mining_thread(void *arg)
{
	xdag_hash_t hash;
	struct xdag_field last;
	const int nthread = (int)(uintptr_t)arg;
	uint64_t oldntask = 0;
	uint64_t nonce;

	while(!g_xdag_sync_on && !g_stop_mining) {
		sleep(1);
	}

	while(!g_stop_mining) {
		const uint64_t ntask = g_xdag_pool_task_index;
		struct xdag_pool_task *task = &g_xdag_pool_task[ntask & 1];

		if(!ntask) {
			sleep(1);
			continue;
		}

		if(ntask != oldntask) {
			oldntask = ntask;
			memcpy(last.data, task->nonce.data, sizeof(xdag_hash_t));
			nonce = last.amount + nthread;
			xdag_info("mining thread %lu start nonce %016llx",pthread_self(),nonce);
		}

		last.amount = xdag_hash_final_multi(task->ctx, &nonce, 4096, g_xdag_mining_threads, hash);
		g_xdag_extstats.nhashes += 4096;
		xd_rsdb_put_extstats();
		xdag_set_min_share(task, last.data, hash);
	}

	return 0;
}

void *rx_mining_thread(void *arg)
{
	xdag_hash_t hash;
	xdag_hash_t pre_hash;
	struct xdag_field last;
	const int nthread = (int)(uintptr_t)arg;
	uint64_t oldntask = 0;
	uint64_t nonce;

	xdag_info("start rx mining thread\n");

	while(!g_xdag_sync_on && !g_stop_mining) {
		sleep(1);
	}

	while(!g_stop_mining) {
		const uint64_t ntask = g_xdag_pool_task_index;
		struct xdag_pool_task *task = &g_xdag_pool_task[ntask & 1];

		if(!ntask) {
			sleep(1);
			continue;
		}

		if(!memcmp(pre_hash,task->task[0].data, sizeof(xdag_hash_t))){
			//xdag_info("pre hash already slow hashed");
			sleep(1);
			continue;
		}else{
			memcpy(pre_hash,task->task[0].data, sizeof(xdag_hash_t));
			xdag_info("new pre hash to slow hash %016llx%016llx%016llx%016llx",pre_hash[0],pre_hash[1],pre_hash[2],pre_hash[3]);
		}

		if(ntask != oldntask) {
			oldntask = ntask;
			memcpy(last.data, task->nonce.data, sizeof(xdag_hash_t));
		}
		xdag_rx_mine_slow_hash((uint32_t)(nthread-1),task->task[0].data, last.data, last.amount, 1024,hash);

		g_xdag_extstats.nhashes += 1024;
		xd_rsdb_put_extstats();
		xdag_set_min_share(task, last.data, hash);
	}

	return NULL;
}

/* changes the number of mining threads */
int xdag_mining_start(int n_mining_threads)
{
	int err=0;
	pthread_t th;

	if(n_mining_threads == g_xdag_mining_threads) {

	} else if(!n_mining_threads) {
		g_stop_mining = 1;
		g_xdag_mining_threads = 0;
	} else if(!g_xdag_mining_threads) {
		g_stop_mining = 0;
	} else if(g_xdag_mining_threads > n_mining_threads) {
		g_stop_mining = 1;
		sleep(5);
		g_stop_mining = 0;
		g_xdag_mining_threads = 0;
	}

	if(g_xdag_mine_type == XDAG_RANDOMX){
		//TODO:use key from pool task
		xdag_info("xdag rx mining start init mine seed");
		const char* fixed_key="7f9fqlPSnmWje554eVx2yaebwAv0nVnI";
		xdag_address2hash(fixed_key,g_fixed_miner_seed);
		xdag_info("xdag init fixed seed %016llx%016llx%016llx%016llx",
		          g_fixed_miner_seed[0],g_fixed_miner_seed[1],g_fixed_miner_seed[2],g_fixed_miner_seed[3]);
		rx_mine_init_seed(g_fixed_miner_seed, sizeof(g_fixed_miner_seed),n_mining_threads);
		rx_mine_alloc_vms(n_mining_threads);
	}

	while(g_xdag_mining_threads < n_mining_threads) {
		g_xdag_mining_threads++;
		if(g_xdag_mine_type == XDAG_RAW){
			err = pthread_create(&th, 0, mining_thread, (void*)(uintptr_t)g_xdag_mining_threads);
		}else{
			err = pthread_create(&th, 0, rx_mining_thread, (void*)(uintptr_t)g_xdag_mining_threads);
		}

		if(err != 0) {
			printf("create mining_thread failed, error : %s\n", strerror(err));
			continue;
		}

		err = pthread_detach(th);
		if(err != 0) {
			printf("detach mining_thread failed, error : %s\n", strerror(err));
			continue;
		}
	}

	return 0;
}

/* send block to network via pool */
int xdag_send_block_via_pool(struct xdag_block *b)
{
	if(g_socket < 0) return -1;

	pthread_mutex_lock(&g_miner_mutex);
	int ret = send_to_pool(b->field, XDAG_BLOCK_FIELDS);
	pthread_mutex_unlock(&g_miner_mutex);
	return ret;
}

/* picks random pool from the list of pools */
int xdag_pick_pool(char *pool_address)
{
	char addresses[30][50] = {{0}, {0}};
	const char *error_message;
	srand(time(NULL));
	
	int count = 0;
	FILE *fp = xdag_open_file(POOL_LIST_FILE, "r");
	if(!fp) {
		printf("List of pools is not found\n");
		return 0;
	}
	while(fgets(addresses[count], 50, fp)) {
		// remove trailing newline character
		addresses[count][strcspn(addresses[count], "\n")] = 0;
		++count;
	}
	fclose(fp);

	int start_index = count ? rand() % count : 0;
	int index = start_index;
	do {
		int socket = xdag_connect_pool(addresses[index], &error_message);
		if(socket != INVALID_SOCKET) {
			xdag_connection_close(socket);
			strncpy(pool_address, addresses[index], 49);
			return 1;
		} else {
			++index;
			if(index >= count) {
				index = 0;
			}
		}
	} while(index != start_index);

	printf("Wallet is unable to connect to network. Check your network connection\n");
	return 0;
}
