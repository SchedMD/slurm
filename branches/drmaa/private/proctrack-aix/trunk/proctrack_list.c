/*****************************************************************************\
 *  proctrack_version.c - Process tracking kernel extension test for AIX. 
 *  Keep track of process ancestry with respect to SLURM jobs.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Pub0lic License as published by the Free
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include "proctrack.h"

int mycmp(const void *a, const void *b)
{
	uint32_t x = *(uint32_t *)a;
	uint32_t y = *(uint32_t *)b;

	if (x < y)
		return -1;
	else if (x > y)
		return 1;
	else
		return 0;
}

int uniq(uint32_t *uniq_jids, int len)
{
	int i, j;

	qsort((void *)uniq_jids, len, sizeof(uint32_t), mycmp);

	j = 0;
	for (i = 1; i < len; i++) {
		if (uniq_jids[i] != uniq_jids[j]) {
			j++;
			uniq_jids[j] = uniq_jids[i];
		}
	}

	return j+1;
}

main (int argc, char **argv) {
	int len = 2048;
	int32_t *pids;
	uint32_t *jids;
	uint32_t *uniq_jids;
	int num_uniq_jids;
	int rc;
	int i, j;

	pids = (int32_t *)malloc(len * sizeof(int32_t));
	jids = (uint32_t *)malloc(len * sizeof(uint32_t));
	memset(pids, 0, len * sizeof(int32_t));
	memset(jids, 0, len * sizeof(uint32_t));

	rc = proctrack_get_all_pids(len, pids, jids);
	if (rc == -1) {
		perror("proctrack_get_all_pids failed");
		free(pids);
		free(jids);
		exit(1);
	}

	if (rc > 0) {
		uniq_jids = (uint32_t *)malloc(rc * sizeof(uint32_t));
		memcpy(uniq_jids, jids, rc * sizeof(uint32_t));


		num_uniq_jids = uniq(uniq_jids, rc);
		for (i = 0; i < num_uniq_jids; i++) {
			printf("Job ID %u has pids:\n", uniq_jids[i]);
			for (j = 0; j < rc && j < len; j++) { 
				if (jids[j] == uniq_jids[i])
					printf("\t%d\n", pids[j]);
			}
		}

		free(uniq_jids);
	}

	free(pids);
	free(jids);
}
