/*****************************************************************************\
 *  test7.2.prog.c - Test of basic PMI library functionality
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <slurm/pmi.h>

#define OFFSET_1  1234
#define OFFSET_2  5678

main (int argc, char **argv)
{
	int i, j;
	int nprocs, procid;
	char *nprocs_ptr, *procid_ptr;
	int pmi_rank, pmi_size;
	PMI_BOOL initialized;
	char attr[20], val[20];

	/* Get process count and our id from environment variables */
	nprocs_ptr = getenv("SLURM_NPROCS");
	procid_ptr = getenv("SLURM_PROCID");
	if ((nprocs_ptr == NULL) ||
	    (procid_ptr == NULL)) {
		printf("FAILURE: Environment variables not set\n");
		exit(1);
	}
	nprocs = atoi(nprocs_ptr);
	procid = atoi(procid_ptr);

	/* Validate process count and our id */
	if ((nprocs < 1) || (nprocs > 9999)) {
		printf("FAILURE: Invalid nprocs %s\n", nprocs_ptr);
		exit(1);
	}
	if ((procid < 0) || (procid > 9999)) {
		printf("FAILURE: Invalid procid %s\n", procid_ptr);
		exit(1);
	}

	/* Get process count and size from PMI and validate */
	if (PMI_Init(&i) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Init: %m\n");
		exit(1);
	}
	initialized = PMI_FALSE;
	if (PMI_Initialized(&initialized) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Initialized: %m\n");
		exit(1);
	}
	if (initialized != PMI_TRUE) {
		printf("FAILURE: PMI_Initialized returned false\n");
		exit(1);
	}
	if (PMI_Get_rank(&pmi_rank) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Get_rank: %m\n");
		exit(1);
	}
	if (PMI_Get_size(&pmi_size) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Get_size: %m\n");
		exit(1);
	}
	if (pmi_rank != procid) {
		printf("FAILURE: Rank(%d) != PROCID(%d)\n",
			pmi_rank, procid);
		exit(1);
	}
	if (pmi_size != nprocs) {
		printf("FAILURE: Size(%d) != NPROCS(%d)\n",
			pmi_size, nprocs);
		exit(1);
	}

#if 0
	/*  Build and set some attr=val pairs */
	snprintf(attr, sizeof(attr), "ATTR_1_%d", procid);
	snprintf(val,  sizeof(val),  "A%d", procid+OFFSET_1);
	if (BNR_Put(bnr_gid, attr, val) != BNR_SUCCESS)
		exit(1);
	snprintf(attr, sizeof(attr), "attr_2_%d", procid);
	snprintf(val,  sizeof(val),  "B%d", procid+OFFSET_2);
	if (BNR_Put(bnr_gid, attr, val) != BNR_SUCCESS)
		exit(1);

	/* Fence to sync with other tasks */
	if (BNR_Fence(bnr_gid) != BNR_SUCCESS)
		exit(1);

	/* Now lets get all keypairs and validate */
	for (i=0; i<bnr_cnt; i++) {
		snprintf(attr, sizeof(attr), "ATTR_1_%d", i);
		if (BNR_Get(bnr_gid, attr, val) != BNR_SUCCESS) {
			printf("FAILURE: BNR_Get(%s): %m\n", attr);
			exit(1);
		}
		if ((val[0] != 'A') || ((atoi(&val[1])-OFFSET_1) != i)) {
			printf("FAILURE: Bad keypair %s=%s\n", attr, val);
			exit(1);
		}
		printf("Read keypair %s=%s\n", attr, val);

		snprintf(attr, sizeof(attr), "attr_2_%d", i);
		if (BNR_Get(bnr_gid, attr, val) != BNR_SUCCESS)
			exit(1);
		if ((val[0] != 'B') || ((atoi(&val[1])-OFFSET_2) != i)) {
			printf("FAILURE: Bad keypair %s=%s\n", attr, val);
			exit(1);
		}
		printf("Read keypair %s=%s\n", attr, val);
	}
#endif

	if (PMI_Finalize() != PMI_SUCCESS) {
		printf("FAILURE: PMI_Finalize: %m\n");
		exit(1);
	}

	printf("PMI test ran successfully\n");
	exit(0);
}

