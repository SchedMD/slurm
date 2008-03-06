/****************************************************************************\
 *  agent.h - Definitions used to queue and process pending Gold requests
 *  Largely copied from src/common/slurmdbd_defs.h in Slurm v1.3
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _GOLD_AGENT_H
#define _GOLD_AGENT_H

#if HAVE_CONFIG_H 
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include "src/common/pack.h"

/* Increment SLURM_DBD_VERSION if any of the RPCs change */
#define SLURM_DBD_VERSION 01

/* SLURM DBD message types */
typedef enum {
	DBD_INIT = 1400,	/* Connection initialization		*/
	DBD_CLUSTER_PROCS,	/* Record tota processors on cluster	*/
	DBD_JOB_COMPLETE,	/* Record job completion 		*/
	DBD_JOB_START,		/* Record job starting			*/
	DBD_NODE_DOWN,		/* Record node state going DOWN		*/
	DBD_NODE_UP,		/* Record node state coming UP		*/
	DBD_STEP_START		/* Record step starting			*/
} slurmdbd_msg_type_t;

/*****************************************************************************\
 * Slurm DBD protocol data structures
\*****************************************************************************/

typedef struct slurmdbd_msg {
	uint16_t msg_type;	/* see slurmdbd_msg_type_t above */
	void * data;		/* pointer to a message type below */
} slurmdbd_msg_t;

typedef struct dbd_cluster_procs_msg {
	uint32_t proc_count;	/* total processor count */
	time_t event_time;	/* time of transition */
} dbd_cluster_procs_msg_t;

typedef struct dbd_job_info_msg {
	char *   account;	/* bank account for job */
	time_t   begin_time;	/* time job becomes eligible to run */
	time_t   end_time;	/* job termintation time */
	uint32_t exit_code;	/* job exit code or signal */
	uint32_t job_id;	/* job ID */
	uint16_t job_state;	/* job state */
	char *   name;		/* job name */
	char *   nodes;		/* hosts allocated to the job */
	char *   partition;	/* job's partition */
	time_t   start_time;	/* job start time */
	time_t   submit_time;	/* job submit time */
	uint32_t total_procs;	/* count of allocated processors */
	uint32_t user_id;	/* owner's UID */
} dbd_job_info_msg_t;

typedef struct dbd_node_down_msg {
	uint16_t cpus;		/* processors on the node */
	time_t event_time;	/* time of transition */
	char *hostlist;		/* name of hosts */
	char *reason;		/* explanation for the node's state */
} dbd_node_down_msg_t;


typedef struct dbd_node_up_msg {
	time_t event_time;	/* time of transition */
	char *hostlist;		/* name of hosts */
} dbd_node_up_msg_t;

/*****************************************************************************\
 * Slurm DBD message processing functions
\*****************************************************************************/

/* Open a socket connection to SlurmDbd using SlurmdbdAuthInfo specified */
extern int slurm_open_slurmdbd_conn(char *auth_info);

/* Close the SlurmDBD socket connection */
extern int slurm_close_slurmdbd_conn(void);

/* Send an RPC to the SlurmDBD. Do not wait for the reply. The RPC
 * will be queued and processed later if the SlurmDBD is not responding.
 * Returns SLURM_SUCCESS or an error code */
extern int slurm_send_slurmdbd_msg(slurmdbd_msg_t *req);

/* Send an RPC to the SlurmDBD and wait for an arbitrary reply message.
 * The RPC will not be queued if an error occurs.
 * The "resp" message must be freed by the caller.
 * Returns SLURM_SUCCESS or an error code */
extern int slurm_send_recv_slurmdbd_msg(slurmdbd_msg_t *req, 
					slurmdbd_msg_t *resp);

/* Send an RPC to the SlurmDBD and wait for the return code reply.
 * The RPC will not be queued if an error occurs.
 * Returns SLURM_SUCCESS or an error code */
extern int slurm_send_slurmdbd_recv_rc_msg(slurmdbd_msg_t *req, int *rc);

/*****************************************************************************\
 * Free various SlurmDBD message structures
\*****************************************************************************/
void inline slurm_dbd_free_cluster_procs_msg(dbd_cluster_procs_msg_t *msg);
void inline slurm_dbd_free_job_info_msg(dbd_job_info_msg_t *msg);
void inline slurm_dbd_free_node_down_msg(dbd_node_down_msg_t *msg);
void inline slurm_dbd_free_node_up_msg(dbd_node_up_msg_t *msg);

/*****************************************************************************\
 * Pack various SlurmDBD message structures into a buffer
\*****************************************************************************/
void inline slurm_dbd_pack_cluster_procs_msg(dbd_cluster_procs_msg_t *msg,
								   Buf buffer);
void inline slurm_dbd_pack_job_info_msg(dbd_job_info_msg_t *msg,   Buf buffer);
void inline slurm_dbd_pack_node_down_msg(dbd_node_down_msg_t *msg, Buf buffer);
void inline slurm_dbd_pack_node_up_msg(dbd_node_up_msg_t *msg,     Buf buffer);

/*****************************************************************************\
 * Unpack various SlurmDBD message structures from a buffer
\*****************************************************************************/
int inline slurm_dbd_unpack_cluster_procs_msg(dbd_cluster_procs_msg_t **msg,
								     Buf buffer);
int inline slurm_dbd_unpack_job_info_msg(dbd_job_info_msg_t **msg,   Buf buffer);
int inline slurm_dbd_unpack_node_down_msg(dbd_node_down_msg_t **msg, Buf buffer);
int inline slurm_dbd_unpack_node_up_msg(dbd_node_up_msg_t **msg,     Buf buffer);

#endif	/* !_GOLD_AGENT_H */
