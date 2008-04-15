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
	GOLD_MSG_INIT = 1400,	/* Connection initialization		*/
	GOLD_MSG_CLUSTER_PROCS,	/* Record tota processors on cluster	*/
	GOLD_MSG_JOB_COMPLETE,	/* Record job completion 		*/
	GOLD_MSG_JOB_START,	/* Record job starting			*/
	GOLD_MSG_NODE_DOWN,	/* Record node state going DOWN		*/
	GOLD_MSG_NODE_UP,	/* Record node state coming UP		*/
	GOLD_MSG_STEP_START	/* Record step starting			*/
} slurm_gold_msg_type_t;

/*****************************************************************************\
 * Slurm DBD protocol data structures
\*****************************************************************************/

typedef struct gold_agent_msg {
	uint16_t msg_type;	/* see gold_agent_msg_type_t above */
	void * data;		/* pointer to a message type below */
} gold_agent_msg_t;

typedef struct gold_cluster_procs_msg {
	uint32_t proc_count;	/* total processor count */
	time_t event_time;	/* time of transition */
} gold_cluster_procs_msg_t;

typedef struct gold_job_info_msg {
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
} gold_job_info_msg_t;

typedef struct gold_node_down_msg {
	uint16_t cpus;		/* processors on the node */
	time_t event_time;	/* time of transition */
	char *hostlist;		/* name of hosts */
	char *reason;		/* explanation for the node's state */
} gold_node_down_msg_t;


typedef struct gold_node_up_msg {
	time_t event_time;	/* time of transition */
	char *hostlist;		/* name of hosts */
} gold_node_up_msg_t;

/*****************************************************************************\
 * Slurm DBD message processing functions
\*****************************************************************************/

/* Initiated a Gold message agent. Recover any saved RPCs. */
extern int gold_agent_init(void);

/* Terminate a Gold message agent. Save any pending RPCs. */
extern int gold_agent_fini(void);

/* Send an RPC to the Gold. Do not wait for the reply. The RPC
 * will be queued and processed later if Gold is not responding.
 * Returns SLURM_SUCCESS or an error code */
extern int gold_agent_xmit(gold_agent_msg_t *req);

/*****************************************************************************\
 * Functions for processing the Gold requests, located in jobacct_gold.c
\*****************************************************************************/
/* For all functions below
 * RET SLURM_SUCCESS on success 
 *     SLURM_ERROR on non-recoverable error (e.g. invalid account ID)
 *     EAGAIN on recoverable error (e.g. Gold not responding) */
extern int agent_cluster_procs(Buf buffer);
extern int agent_job_start(Buf buffer);
extern int agent_job_complete(Buf buffer);
extern int agent_step_start(Buf buffer);
extern int agent_node_down(Buf buffer);
extern int agent_node_up(Buf buffer);

/*****************************************************************************\
 * Free various Gold message structures
\*****************************************************************************/
void inline gold_agent_free_cluster_procs_msg(gold_cluster_procs_msg_t *msg);
void inline gold_agent_free_job_info_msg(gold_job_info_msg_t *msg);
void inline gold_agent_free_node_down_msg(gold_node_down_msg_t *msg);
void inline gold_agent_free_node_up_msg(gold_node_up_msg_t *msg);

/*****************************************************************************\
 * Pack various Gold message structures into a buffer
\*****************************************************************************/
void inline gold_agent_pack_cluster_procs_msg(gold_cluster_procs_msg_t *msg,
								     Buf buffer);
void inline gold_agent_pack_job_info_msg(gold_job_info_msg_t *msg,   Buf buffer);
void inline gold_agent_pack_node_down_msg(gold_node_down_msg_t *msg, Buf buffer);
void inline gold_agent_pack_node_up_msg(gold_node_up_msg_t *msg,     Buf buffer);

/*****************************************************************************\
 * Unpack various Gold message structures from a buffer
\*****************************************************************************/
int inline gold_agent_unpack_cluster_procs_msg(gold_cluster_procs_msg_t **msg,
								     Buf buffer);
int inline gold_agent_unpack_job_info_msg(gold_job_info_msg_t **msg, Buf buffer);
int inline gold_agent_unpack_node_down_msg(gold_node_down_msg_t **msg, 
								     Buf buffer);
int inline gold_agent_unpack_node_up_msg(gold_node_up_msg_t **msg,   Buf buffer);

#endif	/* !_GOLD_AGENT_H */
