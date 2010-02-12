/*****************************************************************************\
 *  test7.3.io.c - Test of "user managed" IO with the slurm_step_launch()
 *                 API function (required for "poe" launch on IBM
 *                 AIX systems).
 *
 *  Writes short message to stdout, another from stderr, reads message from
 *  stdin and writes it back to stdout with header.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv)
{
	char buf1[128], buf2[128], *tmp;
	int size, j, procid = -1, rc = 0;

	tmp = getenv("SLURM_PROCID");
	if (tmp)
		procid = atoi(tmp);
	sprintf(buf1, "task %d write to stdout:", procid);
	rc = write(STDOUT_FILENO, buf1, strlen(buf1));
	sprintf(buf1, "task %d write to stderr:", procid);
	rc = write(STDOUT_FILENO, buf1, strlen(buf1));
	while  ((size = read(STDIN_FILENO, buf1, sizeof(buf1))) != 0) {
		if (size > 0) {
			int offset;
			sprintf(buf2, "task %d read from stdin:", procid);
			offset = strlen(buf2);
			for (j=0; j<size; j++)	/* may lack null terminator */
				buf2[offset+j] = buf1[j];
			buf2[offset+j] = ':';
			buf2[offset+j+1] = '\0';
			rc = write(STDOUT_FILENO, buf2, strlen(buf2));
			break;
		} else {
			if ((errno == EINTR) || (errno == EAGAIN)) {
				sleep(1);
				continue;
			}
			sprintf(buf1, "io read errno:%d:", errno);
			rc = write(STDOUT_FILENO, buf1, strlen(buf1));
			break;
		}
	}
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	return (0);
}
