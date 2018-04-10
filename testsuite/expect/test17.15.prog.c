/*****************************************************************************\
 *  proc1.29.proc.c - Simple user limit set program for Slurm regression
 *  test1.29. Get the core, fsize, nofile, and nproc limits and print their
 *  values in the same format as Slurm environment variables.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

static void print_limit(const char *str, const rlim_t lim)
{
	if (lim == RLIM_INFINITY)
		printf("%s=-1\n", str);
	else
		printf("%s=%lu\n", str, (unsigned long) lim);
}

int main (int argc, char **argv)
{
	struct rlimit u_limit;
	int exit_code = 0;

	(void) getrlimit(RLIMIT_CORE, &u_limit);
	print_limit("USER_CORE", u_limit.rlim_cur);
	(void) getrlimit(RLIMIT_FSIZE, &u_limit);
	print_limit("USER_FSIZE", u_limit.rlim_cur);
	(void) getrlimit(RLIMIT_NOFILE, &u_limit);
	print_limit("USER_NOFILE", u_limit.rlim_cur);
#ifdef RLIMIT_NPROC
	(void) getrlimit(RLIMIT_NPROC, &u_limit);
	print_limit("USER_NPROC", u_limit.rlim_cur);
#else
	printf("USER_NPROC unsupported\n");
#endif
#ifdef RLIMIT_STACK
        (void) getrlimit(RLIMIT_STACK, &u_limit);
        print_limit("USER_STACK", u_limit.rlim_cur);
#else
        printf("USER_STACK unsupported\n");
#endif

	exit(exit_code);
}
