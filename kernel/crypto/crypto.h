// SPDX-License-Identifier: GPL-3.0-only

#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MD5_DIGEST_SIZE 16u
#define MD5_BLOCK_SIZE 64u
#define MD5_HEX_SIZE 33u

#define SHA1_DIGEST_SIZE 20u
#define SHA1_BLOCK_SIZE 64u
#define SHA1_HEX_SIZE 41u

typedef struct
{
   uint32_t State[4];
   uint64_t BitCount;
   uint8_t Buffer[MD5_BLOCK_SIZE];
} MD5_Context;

typedef struct
{
   uint32_t State[5];
   uint64_t BitCount;
   uint8_t Buffer[SHA1_BLOCK_SIZE];
} SHA1_Context;

void MD5_Init(MD5_Context *ctx);
void MD5_Update(MD5_Context *ctx, const void *data, size_t len);
void MD5_Final(MD5_Context *ctx, uint8_t digest[MD5_DIGEST_SIZE]);
void MD5_Calculate(const void *data, size_t len,
                   uint8_t digest[MD5_DIGEST_SIZE]);
void MD5_ToHex(const uint8_t digest[MD5_DIGEST_SIZE],
               char out_hex[MD5_HEX_SIZE]);
bool MD5_SelfTest(void);

void SHA1_Init(SHA1_Context *ctx);
void SHA1_Update(SHA1_Context *ctx, const void *data, size_t len);
void SHA1_Final(SHA1_Context *ctx, uint8_t digest[SHA1_DIGEST_SIZE]);
void SHA1_Calculate(const void *data, size_t len,
                    uint8_t digest[SHA1_DIGEST_SIZE]);
void SHA1_ToHex(const uint8_t digest[SHA1_DIGEST_SIZE],
                char out_hex[SHA1_HEX_SIZE]);
bool SHA1_SelfTest(void);

void Crypto_SelfTest(void);

#endif