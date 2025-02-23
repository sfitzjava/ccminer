/**
* Equihash solver interface for ccminer (compatible with linux and windows)
* Solver taken from nheqminer, by djeZo (and NiceHash)
* tpruvot - 2017 (GPL v3)
*/
//#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#define VERUS_KEY_SIZE 8832
#define VERUS_KEY_SIZE128 552
#include <stdexcept>
#include <vector>

#include "verus_clhash.h"
#include "uint256.h"
//#include "hash.h"
#include <miner.h>
//#include "primitives/block.h"

#include "SSE2NEON.h"
extern "C" 
{
//#include "haraka.h"
#include "haraka_portable.h"
}
#include "verus_clhash_portable.h"

enum
{
	// primary actions
	SER_NETWORK = (1 << 0),
	SER_DISK = (1 << 1),
	SER_GETHASH = (1 << 2),
};
// input here is 140 for the header and 1344 for the solution (equi.cpp)
static const int PROTOCOL_VERSION = 170002;

//#include <cuda_helper.h>

#define EQNONCE_OFFSET 30 /* 27:34 */
#define NONCE_OFT EQNONCE_OFFSET

static bool init[MAX_GPUS] = { 0 };

static __thread uint32_t throughput = 0;



#ifndef htobe32
#define htobe32(x) swab32(x)
#endif

extern "C" inline void GenNewCLKey(unsigned char *seedBytes32, _m128i *keyback)
{
	// generate a new key by chain hashing with Haraka256 from the last curbuf
	int n256blks = VERUS_KEY_SIZE >> 5;  //8832 >> 5
	int nbytesExtra = VERUS_KEY_SIZE & 0x1f;  //8832 & 0x1f
	unsigned char *pkey = (unsigned char*)keyback;
	unsigned char *psrc = seedBytes32;
	for (int i = 0; i < n256blks; i++)
	{
		haraka256_port(pkey, psrc);

		psrc = pkey;
		pkey += 32;
	}
	if (nbytesExtra)
	{
		unsigned char buf[32];
		haraka256_port(buf, psrc);
		memcpy(pkey, buf, nbytesExtra);
	}
}

extern "C" inline void FixKey(uint32_t *fixrand, uint32_t *fixrandex, _m128i *keyback,
	_m128i * g_prand, _m128i *g_prandex)
{
	_m128i buf1, buf2;
	for (int i = 31; i > -1; i--)
	{
		keyback[fixrandex[i]] = g_prandex[i];
		keyback[fixrand[i]] = g_prand[i];
	}

}


extern "C" void VerusHashHalf(void *result2, unsigned char *data, size_t len)
{
	alignas(32) unsigned char buf1[64] = { 0 }, buf2[64];
	unsigned char *curBuf = buf1, *result = buf2;
	size_t curPos = 0;
	//unsigned char result[64];
	curBuf = buf1;
	result = buf2;
	curPos = 0;
	std::fill(buf1, buf1 + sizeof(buf1), 0);

	unsigned char *tmp;

	load_constants_port();

	// digest up to 32 bytes at a time
	for (int pos = 0; pos < len; )
	{
		int room = 32 - curPos;

		if (len - pos >= room)
		{
			memcpy(curBuf + 32 + curPos, data + pos, room);
			haraka512_port(result, curBuf);
			tmp = curBuf;
			curBuf = result;
			result = tmp;
			pos += room;
			curPos = 0;
		}
		else
		{
			memcpy(curBuf + 32 + curPos, data + pos, len - pos);
			curPos += len - pos;
			pos = len;
		}
	}

	memcpy(curBuf + 47, curBuf, 16);
	memcpy(curBuf + 63, curBuf, 1);
	//	FillExtra((_m128i *)curBuf);
	memcpy(result2, curBuf, 64);
};




extern "C" void Verus2hash(unsigned char *hash, unsigned char *curBuf, uint32_t nonce,
	_m128i * __restrict data_key, uint8_t *gpu_init, uint32_t * __restrict fixrand, uint32_t * __restrict fixrandex, _m128i *g_prand, _m128i *g_prandex)
{
//	uint64_t mask = VERUS_KEY_SIZE128; //552
	static const __m128i shuf1 = _mm_setr_epi8_emu(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0);
	const __m128i fill1 = _mm_shuffle_epi8_emu(_mm_load_si128((u128 *)curBuf), shuf1);
	static const __m128i shuf2 = _mm_setr_epi8_emu(1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7, 0);
	unsigned char ch = curBuf[0];
	_mm_store_si128((u128 *)(&curBuf[32 + 16]), fill1);
	curBuf[32 + 15] = ch;
	//	FillExtra((_m128i *)curBuf);
	uint64_t intermediate;

	((uint32_t*)&curBuf[0])[8] = nonce;

	intermediate = verusclhashv2_2(data_key, curBuf, 511, fixrand, fixrandex, g_prand, g_prandex);
		//FillExtra
	_m128i fill2 = _mm_shuffle_epi8_emu(_mm_loadl_epi64((_m128i *)&intermediate), shuf2);
	_mm_store_si128((_m128i *)(&curBuf[32 + 16]), fill2);
	curBuf[32 + 15] = *((unsigned char *)&intermediate);
	//memcpy(curBuf + 47, &intermediate, 8);
	//memcpy(curBuf + 55, &intermediate, 8);
	//memcpy(curBuf + 63, &intermediate, 1);
	intermediate &= 511;
	haraka512_port_keyed(hash, curBuf, data_key + intermediate);
	FixKey(fixrand, fixrandex, data_key, g_prand, g_prandex);
}
#ifdef _WIN32

#define posix_memalign(p, a, s) (((*(p)) = (_m128i*) _aligned_malloc((s), (a))), *(p) ?0 :errno)
#endif

extern "C" int scanhash_verus(int thr_id, struct work *work, uint32_t max_nonce, unsigned long *hashes_done)
{
	//	unsigned char data[] = {
	//	};
	//uint32_t _ALIGN(64) endiandata[35];
	uint32_t *pdata = work->data;
	uint32_t *ptarget = work->target;

	uint8_t _ALIGN(4)  blockhash_half[64] = { 0 };
	uint8_t gpuinit = 0;
	struct timeval tv_start, tv_end, diff;
	//double secs, solps;
	_m128i *data_key =  (_m128i*)malloc(VERUS_KEY_SIZE + 1024);

	//posix_memalign((void**)&data_key,32,VERUS_KEY_SIZE);

	//_m128i *data_key_master = (_m128i *)malloc(VERUS_KEY_SIZE);
	_m128i *data_key_prand = data_key + VERUS_KEY_SIZE128 ;
	_m128i *data_key_prandex = data_key + VERUS_KEY_SIZE128 + 32;
	uint32_t nonce_buf = 0;
	uint32_t _ALIGN(4)  fixrand[32];
	uint32_t _ALIGN(4)  fixrandex[32];

	unsigned char block_41970[3] = { 0xfd, 0x40, 0x05};
	uint8_t _ALIGN(4) full_data[140 + 3 + 1344] = { 0 };
	uint8_t* sol_data = &full_data[140];

	memcpy(full_data, pdata, 140);
	memcpy(sol_data, block_41970, 3);
	memcpy(sol_data + 3, &work->hash_ver, 1344);
        applog(LOG_DEBUG, "stratum work hash_ver(%x)",work->hash_ver);
//TBD log
	//	memcpy(full_data, data, 1487);

	uint32_t _ALIGN(64) vhash[8] = { 0 };

	VerusHashHalf(blockhash_half, (unsigned char*)full_data, 1487);

	GenNewCLKey((unsigned char*)blockhash_half, data_key);  //data_key a global static 2D array data_key[16][8832];


	gettimeofday(&tv_start, NULL);

	throughput = 1;
	const uint32_t Htarg = ptarget[7];
	do {

		*hashes_done = nonce_buf + throughput;
		Verus2hash((unsigned char *)vhash, (unsigned char *)blockhash_half, nonce_buf, data_key, 
				&gpuinit, fixrand, fixrandex , data_key_prand, data_key_prandex);

		if (vhash[7] <= Htarg )
		{
			*((uint32_t *)full_data + 368) = nonce_buf;
			work->valid_nonces++;

			memcpy(work->data, full_data, 140);
			int nonce = work->valid_nonces - 1;
			memcpy(work->extra, sol_data, 1347);
			bn_store_hash_target_ratio(vhash, work->target, work, nonce);

			work->nonces[work->valid_nonces - 1] = ((uint32_t*)full_data)[NONCE_OFT];
			//pdata[NONCE_OFT] = endiandata[NONCE_OFT] + 1;
			goto out;
		}

		//}
		if ((uint64_t)throughput + (uint64_t)nonce_buf >= (uint64_t)max_nonce) {

			break;
		}
		nonce_buf += throughput;

	} while (!work_restart[thr_id].restart);


out:
	gettimeofday(&tv_end, NULL);
	// timeval_subtract(&diff, &tv_end, &tv_start);
	// secs = (1.0 * diff.tv_sec) + (0.000001 * diff.tv_usec);
	// solps = (double)nonce_buf / secs;

	pdata[NONCE_OFT] = ((uint32_t*)full_data)[NONCE_OFT] + 1;
	free(data_key);
	//free(data_key_master);
	return work->valid_nonces;
}

// cleanup
void free_verushash(int thr_id)
{
	if (!init[thr_id])
		return;



	init[thr_id] = false;
}
