/* hash-function, T13.654-T13.864 $DVS:time$ */

#include <string.h>
#ifdef SHA256_OPENSSL_MBLOCK
#include <arpa/inet.h>
#include <log.h>
#include <pthread.h>

#endif
#include "algorithms/sha256.h"
#include "hash.h"
#include "rx_mine_hash.h"
#include "system.h"
#include "block.h"

pthread_mutex_t g_rx_first_hash_mutext = PTHREAD_MUTEX_INITIALIZER;

void xdag_hash(void *data, size_t size, xdag_hash_t hash)
{
	SHA256REF_CTX ctx;

	sha256_init(&ctx);
	sha256_update(&ctx,(const uint8_t *)data, size);
	sha256_final(&ctx, (uint8_t*)hash);
	sha256_init(&ctx);
	sha256_update(&ctx, (uint8_t*)hash, sizeof(xdag_hash_t));
	sha256_final(&ctx, (uint8_t*)hash);
}

unsigned xdag_hash_ctx_size(void)
{
	return sizeof(SHA256REF_CTX);
}

void xdag_hash_init(void *ctxv)
{
	SHA256REF_CTX *ctx = (SHA256REF_CTX*)ctxv;

	sha256_init(ctx);
}

void xdag_hash_update(void *ctxv, void *data, size_t size)
{
	SHA256REF_CTX *ctx = (SHA256REF_CTX*)ctxv;

	sha256_update(ctx, (const uint8_t *)data, size);
}

void xdag_hash_final(void *ctxv, void *data, size_t size, xdag_hash_t hash)
{
	SHA256REF_CTX ctx;

	memcpy(&ctx, ctxv, sizeof(ctx));
	sha256_update(&ctx, (uint8_t*)data, size);
	sha256_final(&ctx, (uint8_t*)hash);
	sha256_init(&ctx);
	sha256_update(&ctx, (uint8_t*)hash, sizeof(xdag_hash_t));
	sha256_final(&ctx, (uint8_t*)hash);
}

#ifndef SHA256_OPENSSL_MBLOCK

uint64_t xdag_hash_final_multi(void *ctxv, uint64_t *nonce, int attempts, int step, xdag_hash_t hash)
{
	SHA256REF_CTX ctx;
	xdag_hash_t hash0;
	uint64_t min_nonce = 0;
	int i;

	for (i = 0; i < attempts; ++i) {
		memcpy(&ctx, ctxv, sizeof(ctx));
		sha256_update(&ctx, (uint8_t*)nonce, sizeof(uint64_t));
		sha256_final(&ctx, (uint8_t*)hash0);
		sha256_init(&ctx);
		sha256_update(&ctx, (uint8_t*)hash0, sizeof(xdag_hash_t));
		sha256_final(&ctx, (uint8_t*)hash0);

		if (!i || xdag_cmphash(hash0, hash) < 0) {
			memcpy(hash, hash0, sizeof(xdag_hash_t));
			min_nonce = *nonce;
		}

		*nonce += step;
	}

	return min_nonce;
}

#else

#define N 8

typedef struct {
	unsigned int A[8], B[8], C[8], D[8], E[8], F[8], G[8], H[8];
} SHA256_MB_CTX;
typedef struct {
	const unsigned char *ptr;
	int blocks;
} HASH_DESC;

extern void xsha256_multi_block(SHA256_MB_CTX *, const HASH_DESC *, int);

uint64_t xdag_hash_final_multi(void *ctxv, uint64_t *nonce, int attempts, int step, xdag_hash_t hash)
{
	SHA256_MB_CTX mctx1, mctx2, mctx;
	SHA256REF_CTX *ctx1 = (SHA256REF_CTX*)ctxv, ctx2[1];
	HASH_DESC desc1[N], desc2[N];
	uint64_t arr1[N * 16], arr2[N * 8];
	uint8_t *array1 = (uint8_t*)arr1, *array2 = (uint8_t*)arr2;
	xdag_hash_t hash0;
	uint64_t min_nonce = 0, nonce0;
	uint32_t *hash032 = (uint32_t*)(uint64_t*)hash0;
	int i, j;

	memset(array1, 0, 128);
	memcpy(array1, ctx1->data, 56);
	array1[64] = 0x80;
	array1[126] = 0x10;

	for (i = 1; i < N; ++i) {
		memcpy(array1 + i * 128, array1, 128);
	}

	for (i = 0; i < N; ++i) {
		desc1[i].ptr = array1 + i * 128, desc1[i].blocks = 2;
	}

	memset(array2, 0, 64);
	array2[32] = 0x80;
	array2[62] = 1;

	for (i = 1; i < N; ++i) {
		memcpy(array2 + i * 64, array2, 64);
	}

	for (i = 0; i < N; ++i) {
		desc2[i].ptr = array2 + i * 64, desc2[i].blocks = 1;
	}

	sha256_init(ctx2);

	for (i = 0; i < N; ++i) {
		mctx1.A[i] = ctx1->state[0]; mctx2.A[i] = ctx2->state[0];
		mctx1.B[i] = ctx1->state[1]; mctx2.B[i] = ctx2->state[1];
		mctx1.C[i] = ctx1->state[2]; mctx2.C[i] = ctx2->state[2];
		mctx1.D[i] = ctx1->state[3]; mctx2.D[i] = ctx2->state[3];
		mctx1.E[i] = ctx1->state[4]; mctx2.E[i] = ctx2->state[4];
		mctx1.F[i] = ctx1->state[5]; mctx2.F[i] = ctx2->state[5];
		mctx1.G[i] = ctx1->state[6]; mctx2.G[i] = ctx2->state[6];
		mctx1.H[i] = ctx1->state[7]; mctx2.H[i] = ctx2->state[7];
	}

	for (j = 0; j < attempts; j += N) {
		memcpy(&mctx, &mctx1, 8 * 8 * 4);
		nonce0 = *nonce;

		for (i = 0; i < N; ++i) {
			memcpy(array1 + i * 128 + 56, nonce, 8); *nonce += step;
		}
		xsha256_multi_block(&mctx, desc1, N / 4);

		for (i = 0; i < N; ++i) {
			((uint32_t*)array2)[i * 16 + 0] = htonl(mctx.A[i]);
			((uint32_t*)array2)[i * 16 + 1] = htonl(mctx.B[i]);
			((uint32_t*)array2)[i * 16 + 2] = htonl(mctx.C[i]);
			((uint32_t*)array2)[i * 16 + 3] = htonl(mctx.D[i]);
			((uint32_t*)array2)[i * 16 + 4] = htonl(mctx.E[i]);
			((uint32_t*)array2)[i * 16 + 5] = htonl(mctx.F[i]);
			((uint32_t*)array2)[i * 16 + 6] = htonl(mctx.G[i]);
			((uint32_t*)array2)[i * 16 + 7] = htonl(mctx.H[i]);
		}
		memcpy(&mctx, &mctx2, 8 * 8 * 4);
		xsha256_multi_block(&mctx, desc2, N / 4);

		for (i = 0; i < N; ++i, nonce0 += step) {
			hash032[0] = htonl(mctx.A[i]);
			hash032[1] = htonl(mctx.B[i]);
			hash032[2] = htonl(mctx.C[i]);
			hash032[3] = htonl(mctx.D[i]);
			hash032[4] = htonl(mctx.E[i]);
			hash032[5] = htonl(mctx.F[i]);
			hash032[6] = htonl(mctx.G[i]);
			hash032[7] = htonl(mctx.H[i]);
			if ((!i && !j) || xdag_cmphash(hash0, hash) < 0) {
				memcpy(hash, hash0, sizeof(xdag_hash_t));
				min_nonce = nonce0;
			}
		}
	}
	return min_nonce;
}

#undef N

#endif

void xdag_hash_get_state(void *ctxv, xdag_hash_t state)
{
	SHA256REF_CTX *ctx = (SHA256REF_CTX*)ctxv;

	memcpy(state, ctx->state, sizeof(xdag_hash_t));
}

void xdag_hash_set_state(void *ctxv, xdag_hash_t state, size_t size)
{
	SHA256REF_CTX *ctx = (SHA256REF_CTX*)ctxv;

	memcpy(ctx->state, state, sizeof(xdag_hash_t));
	ctx->datalen = 0;
	ctx->bitlen = size << 3;
	ctx->bitlenH = 0;
	ctx->md_len = SHA256_BLOCK_SIZE;
}

//calculate sha256 hash for rx mining
void xdag_rx_pre_hash(void *data, size_t size, xdag_hash_t hash)
{
	SHA256REF_CTX ctx;
	sha256_init(&ctx);
	sha256_update(&ctx, (uint8_t*)data, size);
	sha256_final(&ctx, (uint8_t*)hash);
}

uint64_t xdag_rx_mine_first_hash(void* seed,size_t seed_size,const xdag_hash_t pre_hash,xdag_hash_t last_data , uint64_t nonce,xdag_hash_t output_hash){

	uint64_t nonce_pos=0;
	uint8_t data2hash[sizeof(xdag_hash_t)*2];
	memcpy(data2hash,pre_hash,sizeof(xdag_hash_t));
	memcpy(data2hash+sizeof(xdag_hash_t),last_data,sizeof(xdag_hashlow_t));
	nonce_pos=sizeof(xdag_hash_t)+sizeof(xdag_hashlow_t);
	memcpy(data2hash+nonce_pos,&nonce,sizeof(uint64_t));
	rx_mine_calc_first_hash(seed,seed_size,data2hash, sizeof(data2hash), output_hash);

	return nonce;
}

uint64_t xdag_rx_mine_slow_hash(uint32_t thread_index,const xdag_hash_t pre_hash,xdag_hash_t last_data, uint64_t nonce,
                                       uint64_t attemps,xdag_hash_t output_hash){
	int nonce_pos=0;
	xdag_hash_t hash0;
	uint64_t nonce0=nonce;
	uint64_t min_nonce;

	uint8_t data2hash[sizeof(xdag_hash_t)*2];
	memcpy(data2hash,pre_hash,sizeof(xdag_hash_t));
	memcpy(data2hash+sizeof(xdag_hash_t),last_data,sizeof(xdag_hashlow_t));

	nonce0 += thread_index * attemps;
	min_nonce=nonce0;
	nonce_pos = sizeof(xdag_hash_t) + sizeof(xdag_hashlow_t);

	//calculate first hash to output_hash
	memcpy(data2hash+nonce_pos,&nonce0,sizeof(uint64_t));
	rx_mine_hash(thread_index,data2hash, sizeof(data2hash), output_hash);

	//try attemps-1 times to find min hash
	for(int i=0;i < attemps-1;i++)
	{
		memcpy(data2hash+nonce_pos,&nonce0,sizeof(uint64_t));
		rx_mine_hash(thread_index,data2hash, sizeof(data2hash), hash0);
		if(xdag_cmphash(hash0,output_hash) < 0)
		{
			memcpy(output_hash, hash0, sizeof(xdag_hash_t));
			min_nonce = nonce0;
		}
		nonce0+=1;
	}

	last_data[3]=min_nonce;
	memcpy(data2hash,pre_hash,sizeof(xdag_hash_t));
	memcpy(data2hash+sizeof(xdag_hash_t),last_data,sizeof(xdag_hash_t));

	uint64_t *td=(uint64_t*)data2hash;
	rx_mine_hash(thread_index,data2hash, sizeof(data2hash), output_hash);

	xdag_info("rx pre %016llx%016llx%016llx%016llx",pre_hash[0],pre_hash[1],pre_hash[2],pre_hash[3]);
	xdag_info("rx last data %016llx%016llx%016llx%016llx",last_data[0],last_data[1],last_data[2],last_data[3]);
	xdag_info("rx final %016llx%016llx%016llx%016llx",output_hash[0],output_hash[1],output_hash[2],output_hash[3]);
	xdag_info("rx data2 %016llx%016llx%016llx%016llx%016llx%016llx%016llx%016llx",
	          td[0],td[1],td[2],td[3],td[4],td[5],td[6],td[7]);
	xdag_info("rx final nonce %016llx",min_nonce);

	return min_nonce;
}

