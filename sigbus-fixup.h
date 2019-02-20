/*
 * Copyright (C) 2019 SUSE LLC
 *
 * This file is part of pgc.
 *
 * pgc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * pgc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pgc.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _SIGBUS_FIXUP
#define _SIGBUS_FIXUP

#include <stdbool.h>
#include <setjmp.h>

struct sigbus_fixup
{
	bool active;
	jmp_buf jmp_buf;
};

extern __thread struct sigbus_fixup sigbus_fixup;

int sigbus_fixup_init(void);
void sigbus_fixup_cleanup(void);

#endif

