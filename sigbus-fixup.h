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

