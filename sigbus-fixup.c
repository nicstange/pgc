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

#include <signal.h>
#include <stddef.h>
#include "sigbus-fixup.h"

__thread struct sigbus_fixup sigbus_fixup;

static void sigbus_handler(int sig)
{
	if (sigbus_fixup.active) {
		sigbus_fixup.active = false;
		sigbus_fixup_init();
		longjmp(sigbus_fixup.jmp_buf, 1);
	}
}

int sigbus_fixup_init(void)
{
	struct sigaction sa = {
		.sa_handler = sigbus_handler,
		.sa_flags = SA_RESETHAND | SA_NODEFER,
	};

	if (sigaction(SIGBUS, &sa, NULL))
		return -1;

	return 0;
}

void sigbus_fixup_cleanup(void)
{
	struct sigaction sa = {
		.sa_handler = SIG_DFL,
	};
	sigaction(SIGBUS, &sa, NULL);
}
