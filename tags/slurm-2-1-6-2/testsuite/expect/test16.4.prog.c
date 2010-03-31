 /*****************************************************************************\
 *  test1.18.proc.c - Simple I/O test program for SLURM regression test1.18.
 *  Print "waiting\n" to stdout and wait for "exit" as stdin.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

main (int argc, char **argv)
{
	char in_line[10];
	int i;

	fprintf(stdout, "WAITING\n");
	fflush(stdout);

	for (i=0; i<sizeof(in_line); ) {
		in_line[i] = getc(stdin);
		if ((in_line[i] < 'a') ||
		    (in_line[i] > 'z'))
			i = 0;
		else if (strncmp(in_line, "exit", 4) == 0)
			exit(0);
		else
			i++;
	}

	fprintf(stderr, "Invalid input\n");
	exit(1);
}
