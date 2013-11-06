/*****************************************************************************\
 *  test1.95.prog.upc - Basic UPC (Unified Parallel C) test via srun.
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
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

#include <stdio.h>
#include <unistd.h>
#include <upc.h>

shared int inx[THREADS];

int main(int argc, char * argv[])
{
	printf("Hello from %d of %d\n", MYTHREAD, THREADS);
	inx[MYTHREAD] = MYTHREAD;
	upc_barrier;
	if (MYTHREAD == 0) {
		int i, total = 0;
		for (i = 0; i < THREADS; i++)
			total += inx[i];
		/* Make sure "Total" message is last for Expect parsing*/
		sleep(1);
		printf("Total is %d\n", total);
	}
	return 0;
}
