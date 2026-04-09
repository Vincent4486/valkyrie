// SPDX-License-Identifier: GPL-3.0-only

#include "crypto.h"
#include <std/stdio.h>
#include <sys/sys.h>

void Crypto_SelfTest(void)
{
   if (MD5_SelfTest() == CRYPTO_OK)
   {
      logfmt(LOG_INFO, "[CRYPTO] MD5 self-test=PASS\n");
   }
   else
   {
      logfmt(LOG_ERROR, "[CRYPTO] MD5 self-test=FAIL\n");
   }

   if (SHA1_SelfTest() == CRYPTO_OK)
   {
      logfmt(LOG_INFO, "[CRYPTO] SHA1 self-test=PASS\n");
   }
   else
   {
      logfmt(LOG_ERROR, "[CRYPTO] SHA1 self-test=FAIL\n");
   }
}
