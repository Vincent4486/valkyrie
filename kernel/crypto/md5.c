// SPDX-License-Identifier: GPL-3.0-only

#include "crypto.h"
#include <mem/mm_kernel.h>
#include <std/string.h>

#define MD5_F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))
#define MD5_ROTL(x, s) (((x) << (s)) | ((x) >> (32 - (s))))

static void MD5_Encode32(uint8_t *out, const uint32_t *in, size_t len)
{
   size_t i = 0;
   size_t j = 0;

   while (j < len)
   {
      out[j] = (uint8_t)(in[i] & 0xff);
      out[j + 1] = (uint8_t)((in[i] >> 8) & 0xff);
      out[j + 2] = (uint8_t)((in[i] >> 16) & 0xff);
      out[j + 3] = (uint8_t)((in[i] >> 24) & 0xff);
      i++;
      j += 4;
   }
}

static void MD5_Decode32(uint32_t *out, const uint8_t *in, size_t len)
{
   size_t i = 0;
   size_t j = 0;

   while (j < len)
   {
      out[i] = (uint32_t)in[j] | ((uint32_t)in[j + 1] << 8) |
               ((uint32_t)in[j + 2] << 16) | ((uint32_t)in[j + 3] << 24);
      i++;
      j += 4;
   }
}

static void MD5_Transform(uint32_t state[4], const uint8_t block[64])
{
   uint32_t a = state[0];
   uint32_t b = state[1];
   uint32_t c = state[2];
   uint32_t d = state[3];
   uint32_t x[16];

   MD5_Decode32(x, block, 64);

#define MD5_STEP(f, a_, b_, c_, d_, xk, s, ti)                                 \
   do                                                                          \
   {                                                                           \
      (a_) += f((b_), (c_), (d_)) + (xk) + (ti);                               \
      (a_) = MD5_ROTL((a_), (s));                                              \
      (a_) += (b_);                                                            \
   } while (0)

   MD5_STEP(MD5_F, a, b, c, d, x[0], 7, 0xd76aa478);
   MD5_STEP(MD5_F, d, a, b, c, x[1], 12, 0xe8c7b756);
   MD5_STEP(MD5_F, c, d, a, b, x[2], 17, 0x242070db);
   MD5_STEP(MD5_F, b, c, d, a, x[3], 22, 0xc1bdceee);
   MD5_STEP(MD5_F, a, b, c, d, x[4], 7, 0xf57c0faf);
   MD5_STEP(MD5_F, d, a, b, c, x[5], 12, 0x4787c62a);
   MD5_STEP(MD5_F, c, d, a, b, x[6], 17, 0xa8304613);
   MD5_STEP(MD5_F, b, c, d, a, x[7], 22, 0xfd469501);
   MD5_STEP(MD5_F, a, b, c, d, x[8], 7, 0x698098d8);
   MD5_STEP(MD5_F, d, a, b, c, x[9], 12, 0x8b44f7af);
   MD5_STEP(MD5_F, c, d, a, b, x[10], 17, 0xffff5bb1);
   MD5_STEP(MD5_F, b, c, d, a, x[11], 22, 0x895cd7be);
   MD5_STEP(MD5_F, a, b, c, d, x[12], 7, 0x6b901122);
   MD5_STEP(MD5_F, d, a, b, c, x[13], 12, 0xfd987193);
   MD5_STEP(MD5_F, c, d, a, b, x[14], 17, 0xa679438e);
   MD5_STEP(MD5_F, b, c, d, a, x[15], 22, 0x49b40821);

   MD5_STEP(MD5_G, a, b, c, d, x[1], 5, 0xf61e2562);
   MD5_STEP(MD5_G, d, a, b, c, x[6], 9, 0xc040b340);
   MD5_STEP(MD5_G, c, d, a, b, x[11], 14, 0x265e5a51);
   MD5_STEP(MD5_G, b, c, d, a, x[0], 20, 0xe9b6c7aa);
   MD5_STEP(MD5_G, a, b, c, d, x[5], 5, 0xd62f105d);
   MD5_STEP(MD5_G, d, a, b, c, x[10], 9, 0x02441453);
   MD5_STEP(MD5_G, c, d, a, b, x[15], 14, 0xd8a1e681);
   MD5_STEP(MD5_G, b, c, d, a, x[4], 20, 0xe7d3fbc8);
   MD5_STEP(MD5_G, a, b, c, d, x[9], 5, 0x21e1cde6);
   MD5_STEP(MD5_G, d, a, b, c, x[14], 9, 0xc33707d6);
   MD5_STEP(MD5_G, c, d, a, b, x[3], 14, 0xf4d50d87);
   MD5_STEP(MD5_G, b, c, d, a, x[8], 20, 0x455a14ed);
   MD5_STEP(MD5_G, a, b, c, d, x[13], 5, 0xa9e3e905);
   MD5_STEP(MD5_G, d, a, b, c, x[2], 9, 0xfcefa3f8);
   MD5_STEP(MD5_G, c, d, a, b, x[7], 14, 0x676f02d9);
   MD5_STEP(MD5_G, b, c, d, a, x[12], 20, 0x8d2a4c8a);

   MD5_STEP(MD5_H, a, b, c, d, x[5], 4, 0xfffa3942);
   MD5_STEP(MD5_H, d, a, b, c, x[8], 11, 0x8771f681);
   MD5_STEP(MD5_H, c, d, a, b, x[11], 16, 0x6d9d6122);
   MD5_STEP(MD5_H, b, c, d, a, x[14], 23, 0xfde5380c);
   MD5_STEP(MD5_H, a, b, c, d, x[1], 4, 0xa4beea44);
   MD5_STEP(MD5_H, d, a, b, c, x[4], 11, 0x4bdecfa9);
   MD5_STEP(MD5_H, c, d, a, b, x[7], 16, 0xf6bb4b60);
   MD5_STEP(MD5_H, b, c, d, a, x[10], 23, 0xbebfbc70);
   MD5_STEP(MD5_H, a, b, c, d, x[13], 4, 0x289b7ec6);
   MD5_STEP(MD5_H, d, a, b, c, x[0], 11, 0xeaa127fa);
   MD5_STEP(MD5_H, c, d, a, b, x[3], 16, 0xd4ef3085);
   MD5_STEP(MD5_H, b, c, d, a, x[6], 23, 0x04881d05);
   MD5_STEP(MD5_H, a, b, c, d, x[9], 4, 0xd9d4d039);
   MD5_STEP(MD5_H, d, a, b, c, x[12], 11, 0xe6db99e5);
   MD5_STEP(MD5_H, c, d, a, b, x[15], 16, 0x1fa27cf8);
   MD5_STEP(MD5_H, b, c, d, a, x[2], 23, 0xc4ac5665);

   MD5_STEP(MD5_I, a, b, c, d, x[0], 6, 0xf4292244);
   MD5_STEP(MD5_I, d, a, b, c, x[7], 10, 0x432aff97);
   MD5_STEP(MD5_I, c, d, a, b, x[14], 15, 0xab9423a7);
   MD5_STEP(MD5_I, b, c, d, a, x[5], 21, 0xfc93a039);
   MD5_STEP(MD5_I, a, b, c, d, x[12], 6, 0x655b59c3);
   MD5_STEP(MD5_I, d, a, b, c, x[3], 10, 0x8f0ccc92);
   MD5_STEP(MD5_I, c, d, a, b, x[10], 15, 0xffeff47d);
   MD5_STEP(MD5_I, b, c, d, a, x[1], 21, 0x85845dd1);
   MD5_STEP(MD5_I, a, b, c, d, x[8], 6, 0x6fa87e4f);
   MD5_STEP(MD5_I, d, a, b, c, x[15], 10, 0xfe2ce6e0);
   MD5_STEP(MD5_I, c, d, a, b, x[6], 15, 0xa3014314);
   MD5_STEP(MD5_I, b, c, d, a, x[13], 21, 0x4e0811a1);
   MD5_STEP(MD5_I, a, b, c, d, x[4], 6, 0xf7537e82);
   MD5_STEP(MD5_I, d, a, b, c, x[11], 10, 0xbd3af235);
   MD5_STEP(MD5_I, c, d, a, b, x[2], 15, 0x2ad7d2bb);
   MD5_STEP(MD5_I, b, c, d, a, x[9], 21, 0xeb86d391);

#undef MD5_STEP

   state[0] += a;
   state[1] += b;
   state[2] += c;
   state[3] += d;

   memset(x, 0, sizeof(x));
}

void MD5_Init(MD5_Context *ctx)
{
   if (!ctx) return;

   ctx->BitCount = 0;
   ctx->State[0] = 0x67452301;
   ctx->State[1] = 0xefcdab89;
   ctx->State[2] = 0x98badcfe;
   ctx->State[3] = 0x10325476;
   memset(ctx->Buffer, 0, sizeof(ctx->Buffer));
}

void MD5_Update(MD5_Context *ctx, const void *data, size_t len)
{
   if (!ctx || (!data && len > 0)) return;

   const uint8_t *input = (const uint8_t *)data;
   size_t index = (size_t)((ctx->BitCount >> 3) & 0x3fu);
   size_t part_len = MD5_BLOCK_SIZE - index;
   size_t i = 0;

   ctx->BitCount += ((uint64_t)len << 3);

   if (len >= part_len)
   {
      memcpy(&ctx->Buffer[index], input, part_len);
      MD5_Transform(ctx->State, ctx->Buffer);

      for (i = part_len; i + MD5_BLOCK_SIZE - 1 < len; i += MD5_BLOCK_SIZE)
      {
         MD5_Transform(ctx->State, &input[i]);
      }

      index = 0;
   }

   memcpy(&ctx->Buffer[index], &input[i], len - i);
}

void MD5_Final(MD5_Context *ctx, uint8_t digest[MD5_DIGEST_SIZE])
{
   if (!ctx || !digest) return;

   static const uint8_t padding[64] = {0x80};
   uint8_t bits[8];
   uint32_t bit_count_words[2];

   bit_count_words[0] = (uint32_t)(ctx->BitCount & 0xffffffffu);
   bit_count_words[1] = (uint32_t)((ctx->BitCount >> 32) & 0xffffffffu);
   MD5_Encode32(bits, bit_count_words, sizeof(bits));

   size_t index = (size_t)((ctx->BitCount >> 3) & 0x3fu);
   size_t pad_len = (index < 56) ? (56 - index) : (120 - index);

   MD5_Update(ctx, padding, pad_len);
   MD5_Update(ctx, bits, sizeof(bits));

   MD5_Encode32(digest, ctx->State, MD5_DIGEST_SIZE);

   memset(ctx, 0, sizeof(*ctx));
}

void MD5_Calculate(const void *data, size_t len,
                   uint8_t digest[MD5_DIGEST_SIZE])
{
   MD5_Context ctx;
   MD5_Init(&ctx);
   MD5_Update(&ctx, data, len);
   MD5_Final(&ctx, digest);
}

void MD5_ToHex(const uint8_t digest[MD5_DIGEST_SIZE],
               char out_hex[MD5_HEX_SIZE])
{
   static const char hex_chars[] = "0123456789abcdef";

   if (!digest || !out_hex) return;

   for (size_t i = 0; i < MD5_DIGEST_SIZE; i++)
   {
      out_hex[i * 2] = hex_chars[(digest[i] >> 4) & 0x0f];
      out_hex[i * 2 + 1] = hex_chars[digest[i] & 0x0f];
   }
   out_hex[32] = '\0';
}

bool MD5_SelfTest(void)
{
   static const char *vectors[] = {"", "abc"};
   static const char *expected[] = {
       "d41d8cd98f00b204e9800998ecf8427e",
       "900150983cd24fb0d6963f7d28e17f72",
   };

   uint8_t digest[MD5_DIGEST_SIZE];
   char hex[MD5_HEX_SIZE];

   for (size_t i = 0; i < 2; i++)
   {
      MD5_Calculate(vectors[i], strlen(vectors[i]), digest);
      MD5_ToHex(digest, hex);

      if (strcmp(hex, expected[i]) != 0)
      {
         return false;
      }
   }

   return true;
}
