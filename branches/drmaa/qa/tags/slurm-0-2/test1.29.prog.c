/**********************************************************************\
 * Simple user limit set program for SLURM regression test1.29
 * Get the core, fsize, nofile, and nproc limits and print their values 
 * in the same format as SLURM environment variables
\**********************************************************************/
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

main (int argc, char **argv) 
{
	struct rlimit u_limit;
	int exit_code = 0;

	(void) getrlimit(RLIMIT_CORE, &u_limit);
	printf("USER_CORE=%d\n", u_limit.rlim_cur);
	(void) getrlimit(RLIMIT_FSIZE, &u_limit);
	printf("USER_FSIZE=%d\n", u_limit.rlim_cur);
	(void) getrlimit(RLIMIT_NOFILE, &u_limit);
	printf("USER_NOFILE=%d\n", u_limit.rlim_cur);
	(void) getrlimit(RLIMIT_NPROC, &u_limit);
	printf("USER_NPROC=%d\n", u_limit.rlim_cur);
	exit(exit_code);
}
