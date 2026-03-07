// SPDX-License-Identifier: GPL-3.0-only

/*
 * kernel/sys/cmdline.c — Kernel command-line parsing for ValkyrieOS.
 *
 * Parses the raw kernel command line from the bootloader into a global
 * key/value table accessible to all kernel subsystems.
 *
 * Supported command-line format (space-separated key=value pairs):
 *   root=LABEL=BOOT quiet loglevel=3 root=PARTUUID=ABCD1234
 *
 * A token without '=' is treated as a bare flag stored with an empty value.
 */

#include "cmdline.h"
#include <mem/mm_kernel.h>
#include <std/string.h>
#include <sys/sys.h>
#include <std/stdio.h>

/* -------------------------------------------------------------------------
 * Module-internal state
 * ---------------------------------------------------------------------- */

#define CMDLINE_MAX_ARGS 16
#define CMDLINE_MAX_KEYLEN 64
#define CMDLINE_MAX_VALLEN 192 /* large enough for LABEL=, PARTUUID= values */

BOOT_Params s_params_table;
static int s_parsed = 0; /* 0 = not yet parsed */

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/*
 * next_token — extract the next key (and optional value) from the command line.
 *
 * Parses one "word" from `src` (delimited by spaces).  The word is split on
 * the *first* '=' to produce a key and a value; tokens without '=' get an
 * empty value string.  Example:
 *   "root=LABEL=BOOT" → key="root", value="LABEL=BOOT"
 *   "quiet"           → key="quiet", value=""
 *
 * Returns a pointer to the character immediately after the consumed token,
 * or NULL when the end of `src` is reached.
 */
static const char *next_token(const char *src, char *keyOut, size_t keyMax,
                              char *valOut, size_t valMax)
{
   /* Skip leading whitespace */
   while (*src == ' ') src++;
   if (!*src) return NULL;

   /* -- Copy key: stop at '=' or space ---------------------------------- */
   size_t ki = 0;
   while (*src && *src != ' ' && *src != '=' && ki + 1 < keyMax)
      keyOut[ki++] = *src++;
   keyOut[ki] = '\0';

   /* -- Copy value (everything up to the next space) -------------------- */
   valOut[0] = '\0';
   if (*src == '=')
   {
      src++; /* consume '=' */
      size_t vi = 0;
      while (*src && *src != ' ' && vi + 1 < valMax) valOut[vi++] = *src++;
      valOut[vi] = '\0';
   }

   /* Skip trailing whitespace so the next call starts cleanly */
   while (*src == ' ') src++;

   return src; /* may point to '\0' — caller checks *src */
}

/* -------------------------------------------------------------------------
 * Public functions
 * ---------------------------------------------------------------------- */

/*
 * CmdLine_Initialize
 *
 * Splits the raw kernel command line stored in g_SysInfo->boot.commandLine
 * into the global s_params_table key/value table.  Each string is
 * heap-allocated so the original buffer is never mutated.
 *
 * Safe to call multiple times; subsequent calls are no-ops.
 */
void CmdLine_Initialize(void)
{
   if (s_parsed) return;

   const char *raw = g_SysInfo->boot.commandLine;
   if (!raw)
   {
      s_parsed = 1;
      return;
   }

   memset(&s_params_table, 0, sizeof(s_params_table));
   s_params_table.count = 0;

   /* Static scratch buffers — only used during the parse pass */
   static char keyBuf[CMDLINE_MAX_KEYLEN];
   static char valBuf[CMDLINE_MAX_VALLEN];

   const char *p = raw;
   while (p && *p && s_params_table.count < CMDLINE_MAX_ARGS)
   {
      keyBuf[0] = '\0';
      valBuf[0] = '\0';

      p = next_token(p, keyBuf, CMDLINE_MAX_KEYLEN, valBuf, CMDLINE_MAX_VALLEN);

      /* Skip empty keys produced by leading/trailing spaces */
      if (strlen(keyBuf) == 0) continue;

      /* Heap-allocate persistent copies */
      size_t klen = strlen(keyBuf) + 1;
      size_t vlen = strlen(valBuf) + 1;
      char *kheap = (char *)kmalloc(klen);
      char *vheap = (char *)kmalloc(vlen);

      if (!kheap || !vheap)
      {
         if (kheap) free(kheap);
         if (vheap) free(vheap);
         break; /* Out of memory — stop parsing */
      }

      memcpy(kheap, keyBuf, klen);
      memcpy(vheap, valBuf, vlen);

      s_params_table.args[s_params_table.count].key = kheap;
      s_params_table.args[s_params_table.count].value = vheap;
      s_params_table.count++;
   }

   g_SysInfo->boot_params = s_params_table;

   logfmt(LOG_INFO, "[CMDLINE] Parsed %u arguments from bootloader\n",
          s_params_table.count);
   for (uint32_t i = 0; i < s_params_table.count; i++)
   {
      if (s_params_table.args[i].value && s_params_table.args[i].value[0] != '\0')
      {
         logfmt(LOG_INFO, "[CMDLINE]   %s=%s\n",
                s_params_table.args[i].key, s_params_table.args[i].value);
      }
      else
      {
         logfmt(LOG_INFO, "[CMDLINE]   %s\n", s_params_table.args[i].key);
      }
   }

   s_parsed = 1;
}
