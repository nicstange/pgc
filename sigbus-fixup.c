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
