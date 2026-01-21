// SPDX-License-Identifier: GPL-3.0-only

#include "ctype.h"

bool islower(char chr) { return chr >= 'a' && chr <= 'z'; }

char toupper(char chr) { return islower(chr) ? (chr - 'a' + 'A') : chr; }
