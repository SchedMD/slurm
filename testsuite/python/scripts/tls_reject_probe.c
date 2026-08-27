/*****************************************************************************\
 *  tls_reject_probe.c - Probe that a non-TLS RPC connection to a Slurm RPC
 *  listener is rejected when TLS is required.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
\*****************************************************************************/

/*
 * Opens a plaintext RPC connection to a listener and reports whether it was
 * rejected. Run with SLURM_CONF pointing at a TLSType=tls/none config so this
 * process speaks plaintext while the target listener (real config) requires
 * TLS.
 *
 * Target selection:
 *   * "tls_reject_probe <host> <port>" - probe an explicit listener (e.g. a
 *     slurmctld at <SlurmctldHost>:SlurmctldPort or a slurmd at
 *     <node>:SlurmdPort), which is sent REQUEST_PING; or
 *   * "tls_reject_probe" run as a task under srun - probe srun's step-launch
 *     listener via SLURM_SRUN_COMM_HOST / SLURM_SRUN_COMM_PORT, which is sent
 *     SRUN_PING.
 *
 * Prints one of the following and exits 0:
 *   PROBE_RESULT=TLS_REQUIRED  - listener rejected with ESLURM_TLS_REQUIRED
 *   PROBE_RESULT=ACCEPTED      - connection accepted, ping answered SUCCESS
 *   PROBE_RESULT=ERROR         - RPC refused for some other reason
 *   PROBE_RESULT=NO_RESPONSE   - could not send/recv (refused/reset)
 * Exits 2 only if no target could be determined.
 *
 * What a dispatched ping answers depends on the target, and for slurmd also on
 * who runs the probe:
 *   * slurmctld and srun answer SLURM_SUCCESS -> ACCEPTED.
 *   * slurmd marks REQUEST_PING from_slurmctld. An unprivileged sender is
 *     refused ESLURM_USER_ID_MISSING at slurmd's authorization check ->
 *     ERROR rc=2010. root or SlurmUser instead reaches _rpc_ping(), which
 *     answers RESPONSE_PING_SLURMD; that is not a bare rc, but
 *     slurm_get_return_code() maps it to SLURM_SUCCESS -> ACCEPTED.
 * Either slurmd answer proves the listener accepted the connection and
 * dispatched the RPC, so either distinguishes a live listener from
 * TLS_REQUIRED. Callers testing slurmd must accept both unless they pin the
 * uid the probe runs as.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/interfaces/auth.h"

int main(int argc, char **argv)
{
	char *host = NULL;
	char *port_str = NULL;
	char *endptr = NULL;
	unsigned long parsed_port;
	uint16_t port = 0;
	uint16_t msg_type;
	slurm_msg_t req;
	int rc;

	slurm_init(NULL);

	if (argc >= 3) {
		host = argv[1];
		port_str = argv[2];
		/* slurmctld replies to REQUEST_PING with a rc; slurmd's reply
		 * depends on the sender's uid (see above). */
		msg_type = REQUEST_PING;
	} else {
		host = getenv("SLURM_SRUN_COMM_HOST");
		port_str = getenv("SLURM_SRUN_COMM_PORT");
		/* srun replies to SRUN_PING with a rc. */
		msg_type = SRUN_PING;
	}

	if (!host || !port_str) {
		fprintf(stderr,
			"ERROR: no target (pass <host> <port> or run under srun)\n");
		return 2;
	}
	parsed_port = strtoul(port_str, &endptr, 10);
	if (*endptr || !parsed_port || (parsed_port > UINT16_MAX)) {
		fprintf(stderr, "ERROR: invalid port '%s'\n", port_str);
		return 2;
	}
	port = parsed_port;

	slurm_msg_t_init(&req);
	slurm_set_addr(&req.address, port, host);
	/* Both ping types carry no body and are answered with a bare rc. */
	req.msg_type = msg_type;
	/*
	 * r_uid must be set before the message can be packed/sent. It is the
	 * uid allowed to *decode* the credential, not the sender's uid, so it
	 * must not be restricted to our own uid: slurmctld decodes as
	 * SlurmUser. Use SLURM_AUTH_UID_ANY like slurm_send_recv_controller_msg().
	 */
	slurm_msg_set_r_uid(&req, SLURM_AUTH_UID_ANY);
	/*
	 * When run as a task under srun, srun exports its (pinned,
	 * per-allocation) certificate via SLURM_SRUN_TLS_CERT; use it so a
	 * valid-TLS connection to srun's step-launch listener can be
	 * established. Harmless when TLS is disabled (TLSType=tls/none) since
	 * no TLS is attempted.
	 *
	 * NOTE: This is not owned memory. Never call slurm_free_msg_members()
	 * on req as that would free() the environment string.
	 *
	 * NOTE: SLURM_SRUN_TLS_CERT is an internal interface between srun and
	 * its tasks and is not documented in srun.1, unlike the
	 * SLURM_SRUN_COMM_HOST/PORT read above. If it is ever renamed, the
	 * control leg breaks for a reason unrelated to TLS enforcement.
	 */
	req.tls_cert = getenv("SLURM_SRUN_TLS_CERT");

	/*
	 * The reject fires before the RPC is parsed/authenticated, so the
	 * specific msg_type here is not important; any RPC on a non-TLS
	 * connection should come back as ESLURM_TLS_REQUIRED.
	 */
	if (slurm_send_recv_rc_msg_only_one(&req, &rc, 0) < 0) {
		printf("PROBE_RESULT=NO_RESPONSE %s:%u errno=%s\n", host, port,
		       slurm_strerror(errno));
		return 0;
	}

	if (rc == ESLURM_TLS_REQUIRED) {
		printf("PROBE_RESULT=TLS_REQUIRED\n");
		return 0;
	}

	/*
	 * A slurmctld or srun listener that accepted and dispatched the RPC
	 * answers SLURM_SUCCESS, so anything else means the RPC was refused for
	 * some other reason (bad auth, unsupported type, ...) and must not be
	 * reported as ACCEPTED. Report the rc so the caller can tell those
	 * reasons apart.
	 */
	if (rc != SLURM_SUCCESS) {
		printf("PROBE_RESULT=ERROR rc=%d (%s)\n", rc,
		       slurm_strerror(rc));
		return 0;
	}

	printf("PROBE_RESULT=ACCEPTED\n");
	return 0;
}
