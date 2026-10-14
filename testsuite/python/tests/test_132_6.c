/*****************************************************************************\
 *  test_132_6.c - Send one REQUEST_COMPLETE_PROLOG to slurmctld.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
\*****************************************************************************/

/*
 * Usage: test_132_6 <job_id> <node_name> <prolog_rc>
 *
 * Reports that the Prolog of <job_id> finished on <node_name> with
 * <prolog_rc>, the same RPC slurmd sends when a Prolog launched at allocation
 * exits. slurmctld only accepts it from root or SlurmUser.
 *
 * Prints "RC=<n> <strerror>" with slurmctld's reply and exits 0, or exits 1 if
 * the RPC could not be sent at all.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"

int main(int argc, char **argv)
{
	prolog_complete_msg_t req = { 0 };
	slurm_msg_t msg;
	char *endptr = NULL;
	int rc = SLURM_SUCCESS;

	if (argc != 4) {
		fprintf(stderr, "Usage: %s <job_id> <node_name> <prolog_rc>\n",
			argv[0]);
		return 1;
	}

	slurm_init(NULL);

	req.step_id.job_id = strtoul(argv[1], &endptr, 10);
	if (*endptr || !req.step_id.job_id) {
		fprintf(stderr, "Invalid job id '%s'\n", argv[1]);
		return 1;
	}
	req.step_id.step_id = NO_VAL;
	req.step_id.step_het_comp = NO_VAL;
	req.node_name = argv[2];
	req.prolog_rc = strtoul(argv[3], &endptr, 10);
	if (*endptr) {
		fprintf(stderr, "Invalid prolog rc '%s'\n", argv[3]);
		return 1;
	}

	slurm_msg_t_init(&msg);
	msg.msg_type = REQUEST_COMPLETE_PROLOG;
	msg.data = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc, NULL)) {
		fprintf(stderr, "Unable to send REQUEST_COMPLETE_PROLOG: %s\n",
			slurm_strerror(errno));
		return 1;
	}

	printf("RC=%d %s\n", rc, slurm_strerror(rc));

	slurm_fini();
	return 0;
}
