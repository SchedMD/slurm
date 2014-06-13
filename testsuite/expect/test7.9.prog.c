/*****************************************************************************\
 *  test7.9.prog Report any open files (other than stdin, stdout, and stderr).
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* exit() prototype is here */
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define _DEBUG         0
#define _EXTREME_DEBUG 0

main (int argc, char **argv)
{
	int i;
	struct stat buf;

	/* sleep for 0 to 100 msec to induce some randomness
	 * and better detect any synchronization issues */
	usleep(time(NULL) % 100000);

	/* start at fd=3
	 * skip stdin, stdout, and stderr */
	for (i=3; i<256; i++) {
		if (fstat(i, &buf))
			continue;
		printf("FAILED: File descriptor %d is open\n", i);
#if _DEBUG
{
		printf("  st_mode:    0%o\n",(int) buf.st_mode);
		printf("  st_uid:     %d\n", (int) buf.st_uid);
		printf("  st_gid:     %d\n", (int) buf.st_gid);
		printf("  st_size:    %d\n", (int) buf.st_size);
		printf("  st_ino:     %d\n", (int) buf.st_ino);
		printf("  st_dev:     %d\n", (int) buf.st_dev);
#if _EXTREME_DEBUG
	{
		char data[64];
		int j;
		size_t data_size;

		lseek(i, 0, SEEK_SET);
		data_size = read(i, data, 64);
		if (data_size < 0)
			printf("  read error: %s", strerror(errno));
		else {
			printf("  bytes read: %d\n", (int) data_size);
			for (j=0; j<data_size; j++)
				printf("  data[%d]:0x%x\n", j, data[j]);
		}
	}
#endif
}
#endif
	}
	exit(0);
}
