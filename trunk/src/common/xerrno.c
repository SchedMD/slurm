#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "xerrno.h"


/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} xerrtab_t;

/* Add new error values to xerrno.h, and their descriptions to this table */
static xerrtab_t xerrtab[] = {
	{ ENOSLURM,		"Out of slurm" }, /* oh no! */ 
	/* qsw.c */
	{ EBADMAGIC_QSWLIBSTATE,"Bad magic in QSW libstate" },
	{ EBADMAGIC_QSWJOBINFO,	"Bad magic in QSW jobinfo" },
	{ EINVAL_PRGCREATE, 	"Program identifier in use or number of CPUs invalid" },
	{ ECHILD_PRGDESTROY, 	"Processes belonging to this program are still running" },
	{ EEXIST_PRGDESTROY,	"Program identifier does not exist" },
	{ EELAN3INIT, 		"Too many processes using Elan or mapping failure" },
	{ EELAN3CONTROL, 	"Could not open elan3 control device" },
	{ EELAN3CREATE, 	"Could not create elan capability" },
	{ ESRCH_PRGADDCAP,	"Program does not exist (addcap)" },
	{ EFAULT_PRGADDCAP,	"Capability has invalid address (addcap)" },
	{ EINVAL_SETCAP,	"Invalid context number (setcap)" },
	{ EFAULT_SETCAP,	"Capability has invalid address (setcap)" },
	{ EGETNODEID,		"Cannot determine local elan address" },
	{ EGETNODEID_BYHOST,	"Cannot translate hostname to elan address" },
	{ EGETHOST_BYNODEID,	"Cannot translate elan address to hostname" },
	{ ESRCH_PRGSIGNAL,	"No such program identifier" },
	{ EINVAL_PRGSIGNAL,	"Invalid signal number" },
};

/* 
 * Linear search through table of errno values and strings, 
 * returns NULL on error, string on success.
 */
static char *
_lookup_xerrtab(int errnum)
{
	char *res = NULL;
	int i;

	for (i = 0; i < sizeof(xerrtab) / sizeof(xerrtab_t); i++) {
		if (xerrtab[i].xe_number == errnum) {
			res = xerrtab[i].xe_message;
			break;
		}
	}
	return res;
}

/*
 * Return string associated with error (SLURM or system).
 * Always returns a valid string (because strerror always does).
 */
char *
xstrerror(int errnum)
{
	char *res = _lookup_xerrtab(errnum);

	return (res ? res : strerror(errnum));
}

/*
 * Set errno to the specified value.
 */
void
xseterrno(int errnum)
{
#ifdef __set_errno
	__set_errno(errnum);
#else
	errno = errnum;
#endif
}

/*
 * Print "message: error description" on stderr for current errno value.
 */
void
xperror(char *msg)
{
	fprintf(stderr, "%s: %s\n", msg, xstrerror(errno));
}

#if 0
int
main(int argc, char *argv[])
{
	int i;

	for (i = 0; i < 1010; i++) {
		char tmpstr[64];

		xseterrno(i);

		sprintf(tmpstr, "%d", errno);
		xperror(tmpstr);
	}
	exit(0);
}
#endif
