/*****************************************************************************\
 *  test1.34.prog.c - Sleep for a second and terminate with SIGSEGV
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

int main (int argc, char *argv[])
{
	char *buffer;

	/* Print the pid so the test1.34 script
	 * can rm the core file core.getpid()
	 * fflush() otherwise stdout loss in
	 * the core dump.
	 */
	printf("%d\n", getpid());
	fflush(stdout);
	buffer = (char *) 0;
	buffer[1] = 'a';
	return;
}
