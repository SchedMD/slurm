/*
 * $Id$
 * $Source$
 *
 * Errno support for SLURM.
 *
 * This implementation relies on "overloading" the libc errno by 
 * partitioning its domain into system (<1000) and SLURM (>=1000) values.
 * SLURM API functions should call xseterrno() to set errno to a value.
 * API users should call xstrerror() to convert all errno values to their
 * description strings.
 *
 * I think all major OS vendors that support pthreads have MT-safe errnos.
 *
 * To add a new error, #define it here and add it to the table in xerrno.c.
 */

/* set errno to the specified value - then return -1 */ 
#define xseterrno_ret(errnum) do { xseterrno(errnum); return (-1); } while (0)

/* SLURM errno values (start at 1000 to avoid conflict with system errnos) */
#define ENOSLURM		1000 /* Out of slurm */
#define EBADMAGIC_QSWLIBSTATE	1001 /* Bad magic in QsNet libstate */
#define EBADMAGIC_QSWJOBINFO	1002 /* Bad magic in QsNet jobinfo */
#define EINVAL_PRGCREATE	1003 /* Program identifier in use or number of CPUs invalid */
#define ECHILD_PRGDESTROY	1004 /* Processes belonging to this program are still running */
#define EEXIST_PRGDESTROY	1005 /* Program identifier does not exist */
#define EELAN3INIT		1006 /* Too many processes using Elan or mapping failure */
#define EELAN3CONTROL        	1007 /* Could not open elan3 control device */
#define EELAN3CREATE		1008 /* Could not create elan capability */
#define ESRCH_PRGADDCAP		1009 /* Program does not exist (addcap) */
#define EFAULT_PRGADDCAP	1010 /* Capability has invalid address (addcap) */
#define EINVAL_SETCAP		1011 /* Invalid context number (setcap) */
#define EFAULT_SETCAP		1012 /* Capability has invalid address (setcap) */
#define EGETNODEID		1013 /* Cannot determine local elan address */
#define EGETNODEID_BYHOST	1014 /* Cannot translate hostname to elan address */
#define EGETHOST_BYNODEID	1015 /* Cannot translate elan address to hostname */
#define ESRCH_PRGSIGNAL		1016 /* No such program identifier */
#define EINVAL_PRGSIGNAL	1017 /* Invalid signal number */

/* look up an errno value */
char *xstrerror(int errnum);

/* set an errno value */
void xseterrno(int errnum);

/* print message: error string for current errno value */
void xperror(char *msg);
