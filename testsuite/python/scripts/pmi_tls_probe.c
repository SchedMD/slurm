/*****************************************************************************\
 *  pmi_tls_probe.c - Minimal libpmi client used to check that a PMI task
 *  cannot talk to srun's step launch listener without TLS.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
\*****************************************************************************/

/*
 * Run as a task under srun. PMI_Init() and PMI_Barrier() both talk back to
 * srun's step launch listener, so pointing this task at a TLSType=tls/none
 * config via SLURM_CONF makes those connections plaintext while srun still
 * requires TLS. This is the reproducer from issue 51066, reduced to the two
 * calls that cross the socket.
 *
 * Prints one of the following:
 *   PMI_RESULT=OK              - the PMI exchange completed
 *   PMI_RESULT=INIT_FAILED     - PMI_Init() failed
 *   PMI_RESULT=BARRIER_FAILED  - PMI_Barrier() failed
 */

#include <stdio.h>

#include "slurm/pmi.h"

int main(void)
{
	int rc, spawned = 0;

	if ((rc = PMI_Init(&spawned)) != PMI_SUCCESS) {
		printf("PMI_RESULT=INIT_FAILED rc=%d\n", rc);
		return 1;
	}

	if ((rc = PMI_Barrier()) != PMI_SUCCESS) {
		printf("PMI_RESULT=BARRIER_FAILED rc=%d\n", rc);
		return 1;
	}

	PMI_Finalize();

	printf("PMI_RESULT=OK\n");
	return 0;
}
