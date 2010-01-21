/*****************************************************************************\
 *  test7.2.prog.c - Test of basic PMI library functionality
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
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
#include <slurm/pmi.h>
#include <sys/time.h>

#if 0
/* Typical MPICH2 use */
#  define BARRIER_CNT           0
#  define PUTS_PER_BARRIER      0
#else
/* Typical MVAPICH2 use
 *
 * Adjust job time limit and timeout in test7.2 as needed
 * for large values.
 */
#  define BARRIER_CNT           4
#  define PUTS_PER_BARRIER      0
#endif

#define _DEBUG    0
#define OFFSET_1  1234
#define OFFSET_2  5678

main (int argc, char **argv)
{
	int i, j, rc;
	int nprocs, procid;
	int clique_size, *clique_ranks = NULL;
	char *jobid_ptr, *nprocs_ptr, *procid_ptr;
	int pmi_rank, pmi_size, kvs_name_len, key_len, val_len;
	PMI_BOOL initialized;
	char *key, *val, *kvs_name;
	struct timeval tv1, tv2;
	long delta_t;
	char tv_str[20];

	gettimeofday(&tv1, NULL);

	/* Get process count and our id from environment variables */
	jobid_ptr  = getenv("SLURM_JOBID");
	nprocs_ptr = getenv("SLURM_NPROCS");
	procid_ptr = getenv("SLURM_PROCID");
	if (jobid_ptr == NULL) {
		printf("WARNING: PMI test not run under SLURM\n");
		nprocs = 1;
		procid = 0;
	} else if ((nprocs_ptr == NULL) || (procid_ptr == NULL)) {
		printf("FAILURE: SLURM environment variables not set\n");
		exit(1);
	} else {
		nprocs = atoi(nprocs_ptr);
		procid = atoi(procid_ptr);
	}

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
	if ((rc = PMI_Init(&i)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Init: %d\n", rc);
		exit(1);
	}
	initialized = PMI_FALSE;
	if ((rc = PMI_Initialized(&initialized)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Initialized: %d\n", rc);
		exit(1);
	}
	if (initialized != PMI_TRUE) {
		printf("FAILURE: PMI_Initialized returned false\n");
		exit(1);
	}
	if ((rc = PMI_Get_rank(&pmi_rank)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Get_rank: %d\n", rc);
		exit(1);
	}
#if _DEBUG
	printf("PMI_Get_rank = %d\n", pmi_rank);
#endif
	if ((rc = PMI_Get_size(&pmi_size)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Get_size: %d, task %d\n", rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_Get_size = %d\n", pmi_size);
#endif
	if (pmi_rank != procid) {
		printf("FAILURE: Rank(%d) != PROCID(%d)\n",
			pmi_rank, procid);
		exit(1);
	}
	if (pmi_size != nprocs) {
		printf("FAILURE: Size(%d) != NPROCS(%d), task %d\n",
			pmi_size, nprocs, pmi_rank);
		exit(1);
	}

	if ((rc = PMI_Get_clique_size(&clique_size)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Get_clique_size: %d, task %d\n",
			rc, pmi_rank);
		exit(1);
	}
	clique_ranks = malloc(sizeof(int) * clique_size);
	if ((rc = PMI_Get_clique_ranks(clique_ranks, clique_size)) !=
	     PMI_SUCCESS) {
		printf("FAILURE: PMI_Get_clique_ranks: %d, task %d\n",
			rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	for (i=0; i<clique_size; i++)
		printf("PMI_Get_clique_ranks[%d]=%d\n", i, clique_ranks[i]);
#endif
	free(clique_ranks);

	if ((rc = PMI_KVS_Get_name_length_max(&kvs_name_len)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Get_name_length_max: %d, task %d\n",
			rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Get_name_length_max = %d\n", kvs_name_len);
#endif
	kvs_name = malloc(kvs_name_len);
	if ((rc = PMI_KVS_Get_my_name(kvs_name, kvs_name_len)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Get_my_name: %d, task %d\n", rc,
			pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Get_my_name = %s\n", kvs_name);
#endif
	if ((rc = PMI_KVS_Get_key_length_max(&key_len)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Get_key_length_max: %d, task %d\n",
			rc, pmi_rank);
		exit(1);
	}
	key = malloc(key_len);
	if ((rc = PMI_KVS_Get_value_length_max(&val_len)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Get_value_length_max: %d, task %d\n",
			rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Get_value_length_max = %d\n", val_len);
#endif
	val = malloc(val_len);

	/*  Build and set some key=val pairs */
	snprintf(key, key_len, "ATTR_1_%d", procid);
	snprintf(val, val_len, "A%d", procid+OFFSET_1);
	if ((rc = PMI_KVS_Put(kvs_name, key, val)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Put(%s,%s,%s): %d, task %d\n",
			kvs_name, key, val, rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Put(%s,%s,%s)\n", kvs_name, key, val);
#endif
	snprintf(key, key_len, "attr_2_%d", procid);
	snprintf(val, val_len, "B%d", procid+OFFSET_2);
	if ((rc = PMI_KVS_Put(kvs_name, key, val)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Put(%s,%s,%s): %d, task %d\n",
			kvs_name, key, val, rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Put(%s,%s,%s)\n", kvs_name, key, val);
#endif
	/* Sync KVS across all tasks */
	if ((rc = PMI_KVS_Commit(kvs_name)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Commit: %d, task %d\n", rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Commit completed\n");
#endif
	if ((rc = PMI_Barrier()) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Barrier: %d, task %d\n", rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_Barrier completed\n");
#endif
	/* Now lets get all keypairs and validate */
	for (i=0; i<pmi_size; i++) {
		snprintf(key, key_len, "ATTR_1_%d", i);
		if ((rc = PMI_KVS_Get(kvs_name, key, val, val_len))
				!= PMI_SUCCESS) {
			printf("FAILURE: PMI_KVS_Get(%s): %d, task %d\n",
				key, rc, pmi_rank);
			exit(1);
		}
		if ((val[0] != 'A')
		||  ((atoi(&val[1])-OFFSET_1) != i)) {
			printf("FAILURE: Bad keypair %s=%s, task %d\n",
				key, val, pmi_rank);
			exit(1);
		}
#if _DEBUG
		if ((pmi_size <= 8) && (pmi_rank == 0))	/* limit output */
			printf("PMI_KVS_Get(%s,%s) %s\n", kvs_name, key, val);
#endif
		snprintf(key, key_len, "attr_2_%d", i);
		if ((rc = PMI_KVS_Get(kvs_name, key, val, val_len))
				!= PMI_SUCCESS) {
			printf("FAILURE: PMI_KVS_Get(%s): %d, task %d\n",
				key, rc, pmi_rank);
			exit(1);
		}
		if ((val[0] != 'B')
		||  ((atoi(&val[1])-OFFSET_2) != i)) {
			printf("FAILURE: Bad keypair %s=%s, task %d\n",
				key,val, pmi_rank);
			exit(1);
		}
#if _DEBUG
		if ((pmi_size <= 8) && (pmi_rank == 1))	/* limit output */
			printf("PMI_KVS_Get(%s,%s) %s\n", kvs_name, key, val);
#endif
	}

	/* use iterator */
	if ((rc = PMI_KVS_Iter_first(kvs_name, key, key_len, val,
			val_len)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_iter_first: %d, task %d\n", rc,
			pmi_rank);
		exit(1);
	}
	for (i=0; ; i++) {
		if (key[0] == '\0') {
			if (i != (pmi_size * 2)) {
				printf("FAILURE: PMI_KVS_iter_next "
					"cycle count(%d, %d), task %d\n",
					i, pmi_size, pmi_rank);
			}
			break;
		}
#if _DEBUG
		if ((pmi_size <= 8) && (pmi_rank == 1)) {	/* limit output */
			printf("PMI_KVS_Iter_next(%s,%d): %s=%s\n", kvs_name,
				i, key, val);
		}
#endif
		if ((rc = PMI_KVS_Iter_next(kvs_name, key, key_len,
				val, val_len)) != PMI_SUCCESS) {
			printf("FAILURE: PMI_KVS_iter_next: %d, task %d\n",
				rc, pmi_rank);
			exit(1);
		}
	}

	/* Build some more key=val pairs */
	snprintf(key, key_len, "ATTR_3_%d", procid);
	snprintf(val, val_len, "C%d", procid+OFFSET_1);
	if ((rc = PMI_KVS_Put(kvs_name, key, val)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Put(%s,%s,%s): %d, task %d\n",
			kvs_name, key, val, rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Put(%s,%s,%s)\n", kvs_name, key, val);
#endif
	snprintf(key, key_len, "attr_4_%d", procid);
	snprintf(val, val_len, "D%d", procid+OFFSET_2);
	if ((rc = PMI_KVS_Put(kvs_name, key, val)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Put(%s,%s,%s): %d, task %d\n",
			kvs_name, key, val, rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Put(%s,%s,%s)\n", kvs_name, key, val);
#endif
	/* Sync KVS across all tasks */
	if ((rc = PMI_KVS_Commit(kvs_name)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Commit: %d, task %d\n", rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Commit completed\n");
#endif
	if ((rc = PMI_Barrier()) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Barrier: %d, task %d\n", rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_Barrier completed\n");
#endif
	/* Now lets get some keypairs and validate */
	for (i=0; i<pmi_size; i++) {
		snprintf(key, key_len, "ATTR_1_%d", i);
		if ((rc = PMI_KVS_Get(kvs_name, key, val, val_len))
				!= PMI_SUCCESS) {
			printf("FAILURE: PMI_KVS_Get(%s): %d, task %d\n",
				key, rc, pmi_rank);
			exit(1);
		}
		if ((val[0] != 'A')
		||  ((atoi(&val[1])-OFFSET_1) != i)) {
			printf("FAILURE: Bad keypair %s=%s, task %d\n",
				key, val, pmi_rank);
			exit(1);
		}
#if _DEBUG
		if ((pmi_size <= 8) && (pmi_rank == 1))	/* limit output */
			printf("PMI_KVS_Get(%s,%s) %s\n", kvs_name, key, val);
#endif
		snprintf(key, key_len, "attr_4_%d", i);
		if ((rc = PMI_KVS_Get(kvs_name, key, val, val_len))
				!= PMI_SUCCESS) {
			printf("FAILURE: PMI_KVS_Get(%s): %d, task %d\n",
				key, rc, pmi_rank);
			exit(1);
		}
		if ((val[0] != 'D')
		||  ((atoi(&val[1])-OFFSET_2) != i)) {
			printf("FAILURE: Bad keypair %s=%s, task %d\n",
				key,val, pmi_rank);
			exit(1);
		}
#if _DEBUG
		if ((pmi_size <= 8) && (pmi_rank == 1))	/* limit output */
			printf("PMI_KVS_Get(%s,%s) %s\n", kvs_name, key, val);
#endif
	}

	/* Replicate the very heavy load that MVAPICH2 puts on PMI
	 * This load exceeds that of MPICH2 by a very wide margin */
#if _DEBUG
	printf("Starting %d interations each with %d PMI_KVS_Put and \n"
		"  one each PMI_KVS_Commit and KVS_Barrier\n",
		BARRIER_CNT, PUTS_PER_BARRIER);
	fflush(stdout);
#endif
	for (i=0; i<BARRIER_CNT; i++) {
		for (j=0; j<PUTS_PER_BARRIER; j++) {
			snprintf(key, key_len, "ATTR_%d_%d_%d", i, j, procid);
			snprintf(val, val_len, "C%d", procid+OFFSET_1);
			if ((rc = PMI_KVS_Put(kvs_name, key, val)) !=
					PMI_SUCCESS) {
				printf("FAILURE: PMI_KVS_Put(%s,%s,%s): "
					"%d, task %d\n",
					kvs_name, key, val, rc, pmi_rank);
				exit(1);
			}
		}
		if ((rc= PMI_KVS_Commit(kvs_name)) != PMI_SUCCESS) {
			printf("FAILURE: PMI_KVS_Commit: %d, task %d\n",
				rc, pmi_rank);
			exit(1);
		}
		if ((rc = PMI_Barrier()) != PMI_SUCCESS) {
			printf("FAILURE: PMI_Barrier: %d, task %d\n",
				rc, pmi_rank);
			exit(1);
		}
		/* Don't bother with PMI_KVS_Get as those are all local
		 * and do not put a real load on srun or the network */
	}
#if _DEBUG
	printf("Interative PMI calls successful\n");
#endif

	/* create new keyspace and test it */
	if ((rc = PMI_KVS_Create(kvs_name, kvs_name_len)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Create: %d, task %d\n", rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Create %s\n", kvs_name);
#endif
	if ((rc = PMI_KVS_Put(kvs_name, "KVS_KEY", "KVS_VAL")) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Put: %d, task %d\n", rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Put(%s,KVS_KEY,KVS_VAL)\n", kvs_name);
#endif
	if ((rc =  PMI_KVS_Get(kvs_name, "KVS_KEY", val, val_len)) !=
			PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Get(%s, KVS_KEY): %d, task %d\n",
			kvs_name, rc, pmi_rank);
		exit(1);
	}
#if _DEBUG
	printf("PMI_KVS_Get(%s,%s) %s\n", kvs_name, "KVS_KEY", val);
#endif
	if ((rc = PMI_KVS_Destroy(kvs_name)) != PMI_SUCCESS) {
		printf("FAILURE: PMI_KVS_Destroy(%s): %d, task %d\n",
			kvs_name, rc, pmi_rank);
		exit(1);
	}
	if ((rc = PMI_KVS_Get(kvs_name, "KVS_KEY", val, val_len)) !=
			PMI_ERR_INVALID_KVS) {
		printf("FAILURE: PMI_KVS_Get(%s, KVS_KEY): %d, task %d\n",
			kvs_name, rc, pmi_rank);
		exit(1);
	}
	if ((rc = PMI_Finalize()) != PMI_SUCCESS) {
		printf("FAILURE: PMI_Finalize: %d, task %d\n", rc, pmi_rank);
		exit(1);
	}

	if (_DEBUG || (pmi_rank < 4)) {
		gettimeofday(&tv2, NULL);
		delta_t  = (tv2.tv_sec  - tv1.tv_sec) * 1000000;
		delta_t +=  tv2.tv_usec - tv1.tv_usec;
		snprintf(tv_str, sizeof(tv_str), "usec=%ld", delta_t);
		printf("PMI test ran successfully, for task %d, %s\n",
			pmi_rank, tv_str);
	}
	if (pmi_rank == 0) {
		printf("NOTE: All failures reported, ");
		printf("but only first four successes reported\n");
	}
	exit(0);
}

