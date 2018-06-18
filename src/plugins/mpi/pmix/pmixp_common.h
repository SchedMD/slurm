/*****************************************************************************\
 **  pmix_common.h - PMIx common declarations and includes
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
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

#ifndef PMIXP_COMMON_H
#define PMIXP_COMMON_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>

/* Common includes for all source files
 * Define Slurm translator header first to override
 * all translated functions
 */
#include "src/common/slurm_xlator.h"

/* Other useful includes */
#include "slurm/slurm_errno.h"
#include "src/common/slurm_mpi.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/eio.h"
#include "src/common/mapping.h"
#include "src/common/fd.h"
#include "src/common/net.h"

/* ----------------------------------------------------------
 * Slurm environment that influence us:
 * Job/step resource description
 * ---------------------------------------------------------- */
#define PMIXP_STEP_NODES_ENV "SLURM_STEP_NODELIST"
/* srun does not propagates SLURM_JOB_NODELIST
 * we need to check both of the variables:
 * - SLURM_JOB_NODELIST - a new one
 * - SLURM_NODELIST - a deprecated one */
#define PMIXP_JOB_NODES_ENV "SLURM_JOB_NODELIST"
#define PMIXP_JOB_NODES_ENV_DEP "SLURM_NODELIST"
#define PMIXP_CPUS_PER_NODE_ENV "SLURM_JOB_CPUS_PER_NODE"
#define PMIXP_CPUS_PER_TASK "SLURM_CPUS_PER_TASK"
#define PMIXP_SLURM_MAPPING_ENV "SLURM_PMIX_MAPPING_SERV"

/* ----------------------------------------------------------
 * This variables can be used to adjust the plugin's behavior
 * TODO: put their description to documentation
 * ---------------------------------------------------------- */

/* Setup communication timeout */
#define PMIXP_TIMEOUT "SLURM_PMIX_TIMEOUT"
#define PMIXP_TIMEOUT_DEFAULT 300

/* setup path to the temp directory for usock files for:
 * - inter-stepd comunication;
 * - libpmix - client communication
 */
#define PMIXP_TMPDIR_SRV "SLURM_PMIX_SRV_TMPDIR"
#define PMIXP_TMPDIR_CLI "SLURM_PMIX_TMPDIR"
#define PMIXP_DIRECT_SAMEARCH "SLURM_PMIX_SAMEARCH"
#define PMIXP_DIRECT_CONN "SLURM_PMIX_DIRECT_CONN"
#define PMIXP_DIRECT_CONN_UCX "SLURM_PMIX_DIRECT_CONN_UCX"
#define PMIXP_TMPDIR_DEFAULT "/tmp/"
#define PMIXP_OS_TMPDIR_ENV "TMPDIR"
/* This variable will be propagated to server-side
 * part of libPMIx */
#define PMIXP_DEBUG_LIB "SLURM_PMIX_SRV_DEBUG"
#define PMIXP_DIRECT_CONN_EARLY "SLURM_PMIX_DIRECT_CONN_EARLY"

/* ----------------------------------------------------------
 * This is libPMIx variable that we need to control it
 * ---------------------------------------------------------- */
#define PMIXP_PMIXLIB_TMPDIR "PMIX_SERVER_TMPDIR"
#define PMIXP_PMIXLIB_DEBUG "PMIX_DEBUG"
#define PMIXP_PMIXLIB_DEBUG_REDIR "PMIX_OUTPUT_REDIRECT"

/* ----------------------------------------------------------
 * This variables can be used to setup a ping-pong test to test
 * the communication latency (for debug purpose only)
 * ---------------------------------------------------------- */

/* Request a point-to-point test to be executed
 * before running a job
 */
#define PMIXP_PP_ON "SLURM_PMIX_WANT_PP"
/* Smallest message size (power of 2) */
#define PMIXP_PP_LOW "SLURM_PMIX_PP_LOW_PWR2"
/* Largest message size (power of 2) */
#define PMIXP_PP_UP "SLURM_PMIX_PP_UP_PWR2"
/* Number of repetitions for the small messages */
#define PMIXP_PP_SITER "SLURM_PMIX_PP_ITER_SMALL"
/* Number of repetitions for the large messages */
#define PMIXP_PP_LITER "SLURM_PMIX_PP_ITER_LARGE"
/* The bound after which message is considered large */
#define PMIXP_PP_BOUND "SLURM_PMIX_PP_LARGE_PWR2"
/* Send/receive from the same thread (like regular MPI p2p benchmarks) */
#define PMIXP_PP_SAMETHR "SLURM_PMIX_PP_SAME_THR"

/* Request a collective test to be executed
 * before running a job
 */
#define PMIXP_CPERF_ON "SLURM_PMIX_WANT_COLL_PERF"
/* Smallest message size (power of 2) */
#define PMIXP_CPERF_LOW "SLURM_PMIX_COLL_PERF_LOW_PWR2"
/* Largest message size (power of 2) */
#define PMIXP_CPERF_UP "SLURM_PMIX_COLL_PERF_UP_PWR2"
/* Number of repetitions for the small messages */
#define PMIXP_CPERF_SITER "SLURM_PMIX_COLL_PERF_ITER_SMALL"
/* Number of repetitions for the large messages */
#define PMIXP_CPERF_LITER "SLURM_PMIX_COLL_PERF_ITER_LARGE"
/* The bound after which message is considered large */
#define PMIXP_CPERF_BOUND "SLURM_PMIX_COLL_PERF_LARGE_PWR2"
/* The prefered fence type, values:[auto|tree|ring] */
#define PMIXP_COLL_FENCE "SLURM_PMIX_FENCE"
#define SLURM_PMIXP_FENCE_BARRIER "SLURM_PMIX_FENCE_BARRIER"

typedef enum {
	PMIXP_P2P_INLINE,
	PMIXP_P2P_REGULAR
} pmixp_p2p_ctx_t;

/* Message access callbacks */
typedef int (*pmixp_p2p_hdr_unpack_cb_t)(void *hdr_net, void *hdr_host);
typedef void *(*pmixp_p2p_buf_ptr_cb_t)(void *msg);

typedef uint32_t (*pmixp_2p2_payload_size_cb_t)(void *hdr);
typedef size_t (*pmixp_p2p_buf_size_cb_t)(void *msg);
typedef void (*pmixp_p2p_send_complete_cb_t)(void *msg,
					     pmixp_p2p_ctx_t ctx, int rc);
typedef void (*pmixp_p2p_msg_return_cb_t)(void *hdr, Buf buf);

typedef struct {
	/* receiver-related fields */
	bool recv_on;
	uint32_t rhdr_host_size;
	uint32_t rhdr_net_size;
	pmixp_2p2_payload_size_cb_t payload_size_cb;
	pmixp_p2p_hdr_unpack_cb_t hdr_unpack_cb;
	pmixp_p2p_msg_return_cb_t new_msg;
	uint32_t recv_padding;
	/* transmitter-related fields */
	bool send_on;
	pmixp_p2p_buf_ptr_cb_t  buf_ptr;
	pmixp_p2p_buf_size_cb_t buf_size;
	pmixp_p2p_send_complete_cb_t send_complete;
} pmixp_p2p_data_t;

#define PMIXP_MAX_NSLEN     255
#define PMIXP_MAX_KEYLEN    511


#define PMIXP_ERR_TIMEOUT                      -24
#define PMIXP_ERR_BAD_PARAM                    -27
#define PMIXP_ERR_INVALID_NAMESPACE            -44

typedef struct {
    char nspace[PMIXP_MAX_NSLEN+1];
    uint32_t rank;
} pmixp_proc_t;

#endif /* PMIXP_COMMON_H */
