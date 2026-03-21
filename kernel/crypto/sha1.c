// SPDX-License-Identifier: GPL-3.0-only

#include "crypto.h"
#include <mem/mm_kernel.h>
#include <std/string.h>

#define SHA1_ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void SHA1_Encode32BE(uint8_t *out, const uint32_t *in, size_t len)
{
   size_t i = 0;
   size_t j = 0;

   while (j < len)
   {
      out[j] = (uint8_t)((in[i] >> 24) & 0xff);
      out[j + 1] = (uint8_t)((in[i] >> 16) & 0xff);
      out[j + 2] = (uint8_t)((in[i] >> 8) & 0xff);
      out[j + 3] = (uint8_t)(in[i] & 0xff);
      i++;
      j += 4;
   }
}

static uint32_t SHA1_Load32BE(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
          ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void SHA1_Transform(uint32_t state[5], const uint8_t block[64])
{
   uint32_t w[80];

   for (size_t t = 0; t < 16; t++)
   {
      w[t] = SHA1_Load32BE(&block[t * 4]);
   }
   for (size_t t = 16; t < 80; t++)
   {
      w[t] = SHA1_ROTL32(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
   }

   uint32_t a = state[0];
   uint32_t b = state[1];
   uint32_t c = state[2];
   uint32_t d = state[3];
   uint32_t e = state[4];

   for (size_t t = 0; t < 80; t++)
   {
      uint32_t f;
      uint32_t k;

      if (t < 20)
      {
         f = (b & c) | ((~b) & d);
         k = 0x5A827999;
      }
      else if (t < 40)
      {
         f = b ^ c ^ d;
         k = 0x6ED9EBA1;
      }
      else if (t < 60)
      {
         f = (b & c) | (b & d) | (c & d);
         k = 0x8F1BBCDC;
      }
      else
      {
         f = b ^ c ^ d;
         k = 0xCA62C1D6;
      }

      uint32_t temp = SHA1_ROTL32(a, 5) + f + e + k + w[t];
      e = d;
      d = c;
      c = SHA1_ROTL32(b, 30);
      b = a;
      a = temp;
   }

   state[0] += a;
   state[1] += b;
   state[2] += c;
   state[3] += d;
   state[4] += e;

   memset(w, 0, sizeof(w));
}

void SHA1_Init(SHA1_Context *ctx)
{
   if (!ctx) return;

   ctx->State[0] = 0x67452301;
   ctx->State[1] = 0xEFCDAB89;
   ctx->State[2] = 0x98BADCFE;
   ctx->State[3] = 0x10325476;
   ctx->State[4] = 0xC3D2E1F0;
   ctx->BitCount = 0;
   memset(ctx->Buffer, 0, sizeof(ctx->Buffer));
}

void SHA1_Update(SHA1_Context *ctx, const void *data, size_t len)
{
   if (!ctx || (!data && len > 0)) return;

   const uint8_t *input = (const uint8_t *)data;
   size_t index = (size_t)((ctx->BitCount >> 3) & 0x3fu);
   size_t part_len = SHA1_BLOCK_SIZE - index;
   size_t i = 0;

   ctx->BitCount += ((uint64_t)len << 3);

   if (len >= part_len)
   {
      memcpy(&ctx->Buffer[index], input, part_len);
      SHA1_Transform(ctx->State, ctx->Buffer);

      for (i = part_len; i + SHA1_BLOCK_SIZE - 1 < len; i += SHA1_BLOCK_SIZE)
      {
         SHA1_Transform(ctx->State, &input[i]);
      }

      index = 0;
   }

   memcpy(&ctx->Buffer[index], &input[i], len - i);
}

void SHA1_Final(SHA1_Context *ctx, uint8_t digest[SHA1_DIGEST_SIZE])
{
   if (!ctx || !digest) return;

   static const uint8_t padding[64] = {0x80};
   uint8_t bit_count_be[8];
   uint32_t high_low[2];

   high_low[0] = (uint32_t)((ctx->BitCount >> 32) & 0xffffffffu);
   high_low[1] = (uint32_t)(ctx->BitCount & 0xffffffffu);
   SHA1_Encode32BE(bit_count_be, high_low, sizeof(bit_count_be));

   size_t index = (size_t)((ctx->BitCount >> 3) & 0x3fu);
   size_t pad_len = (index < 56) ? (56 - index) : (120 - index);

   SHA1_Update(ctx, padding, pad_len);
   SHA1_Update(ctx, bit_count_be, sizeof(bit_count_be));

   SHA1_Encode32BE(digest, ctx->State, SHA1_DIGEST_SIZE);
   memset(ctx, 0, sizeof(*ctx));
}

void SHA1_Calculate(const void *data, size_t len,
                    uint8_t digest[SHA1_DIGEST_SIZE])
{
   SHA1_Context ctx;
   SHA1_Init(&ctx);
   SHA1_Update(&ctx, data, len);
   SHA1_Final(&ctx, digest);
}

void SHA1_ToHex(const uint8_t digest[SHA1_DIGEST_SIZE],
                char out_hex[SHA1_HEX_SIZE])
{
   static const char hex_chars[] = "0123456789abcdef";

   if (!digest || !out_hex) return;

   for (size_t i = 0; i < SHA1_DIGEST_SIZE; i++)
   {
      out_hex[i * 2] = hex_chars[(digest[i] >> 4) & 0x0f];
      out_hex[i * 2 + 1] = hex_chars[digest[i] & 0x0f];
   }
   out_hex[40] = '\0';
}

bool SHA1_SelfTest(void)
{
   static const char *vectors[] = {"", "abc"};
   static const char *expected[] = {
       "da39a3ee5e6b4b0d3255bfef95601890afd80709",
       "a9993e364706816aba3e25717850c26c9cd0d89d",
   };

   uint8_t digest[SHA1_DIGEST_SIZE];
   char hex[SHA1_HEX_SIZE];

   for (size_t i = 0; i < 2; i++)
   {
      SHA1_Calculate(vectors[i], strlen(vectors[i]), digest);
      SHA1_ToHex(digest, hex);
      if (strcmp(hex, expected[i]) != 0)
      {
         return false;
      }
   }

   return true;
}
