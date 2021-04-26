 /*****************************************************************************\
 *  test12.2.prog.c - Simple test program for Slurm regression test12.2.
 *  Usage: test12.2.prog <exit_code> <sleep_secs> <mem_kb> <file_size> <file_stem>
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int
main (int argc, char **argv)
{
	int exit_code, sleep_time, mem_kb;
	int i;
	long long int n;
	long long int file_size;
	int fd;
	char *mem = NULL;
	char *file_stem;
	char file_read_path[64];
	char file_write_path[64];
	time_t time_start = time(NULL);
	int size, rank;

	if (argc != 6) {
		fprintf(stderr,
			"Usage: %s <exit_code> <sleep_time> <mem_kb> <file_size> <file_stem>\n",
			argv[0]);
		exit(1);
	}

	exit_code  = atoi(argv[1]);
	sleep_time = atoi(argv[2]);
	mem_kb     = atoi(argv[3]);
	file_size  = atoll(argv[4]);
	file_stem  = argv[5];
	sprintf(file_read_path, "%s.read", file_stem);
	sprintf(file_write_path, "%s.write", file_stem);

	rank = atoi(getenv("SLURM_PROCID"));

	if (rank == 0) {
		mem = malloc(mem_kb * 1024);
		/* We need to do a memset on the memory or AIX will not count
		 * the memory in the job step's Resident Set Size.
		 */
		memset(mem, 0, (mem_kb * 1024));
		/* Then we touch some memory using the volatile keyword so
		 * the compiler does not optimize out the memset.
		 */
		*(volatile char*)mem = *(volatile char*)mem;
	}

	if (rank == 1) {
		/* Don't use malloc() to write() and read() a blob
		 * of memory as it will interfere with the memory
		 * test, don't use stdio for the same reason, it
		 * allocates memory.
		 */
		fd = open(file_write_path, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
		n = file_size/sizeof(int);

		for (i = 0; i < n; i++) {
			if (write(fd, &i, sizeof(int)) != sizeof(int)) {
				fprintf(stderr, "FAILURE: write error\n");
				exit(1);
			}
		}
		fsync(fd);
		close(fd);
	}

	if (rank == 2) {
		fd = open(file_read_path, O_RDONLY, S_IRUSR|S_IWUSR);
		n = file_size/sizeof(int);
		int j = 0;
		for (i = 0; i < n; i++) {
			if (read(fd, &j, sizeof(int)) != sizeof(int)) {
				fprintf(stderr, "FAILURE: read error\n");
				exit(1);
			}
		}
		close(fd);
	}

	sleep_time -= difftime(time(NULL), time_start);
	sleep(sleep_time);
	if (mem)
		free(mem);

	exit(exit_code);
}
