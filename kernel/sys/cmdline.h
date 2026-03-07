// SPDX-License-Identifier: GPL-3.0-only

#ifndef CMDLINE_H
#define CMDLINE_H

#include <stdint.h>
#include <sys/sys.h>

/* Parse the command line from g_SysInfo->boot.commandLine into s_params_table
 */
void CmdLine_Initialize(void);

#endif
