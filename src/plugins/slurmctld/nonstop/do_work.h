/*****************************************************************************\
 *  do_work.h - Define functions that do most of the operations.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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

#ifndef _HAVE_NONSTOP_DO_WORK_H
#define _HAVE_NONSTOP_DO_WORK_H

/*
 * Drain nodes which a user believes are bad
 * cmd_ptr IN - Input format "DRAIN:NODES:name:REASON:string"
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *drain_nodes_user(char *cmd_ptr, uid_t cmd_uid,
			      uint32_t protocol_version);

/*
 * Remove a job's failed or failing node from its allocation
 * cmd_ptr IN - Input format "DROP_NODE:JOBID:#:NODE:name"
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *drop_node(char *cmd_ptr, uid_t cmd_uid,
		       uint32_t protocol_version);

/*
 * Identify a job's failed and failing nodes
 * cmd_ptr IN - Input format "GET_FAIL_NODES:JOBID:#:STATE_FLAGS:#"
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *fail_nodes(char *cmd_ptr, uid_t cmd_uid,
			uint32_t protocol_version);

extern void job_begin_callback(struct job_record *job_ptr);
extern void job_fini_callback(struct job_record *job_ptr);
extern void node_fail_callback(struct job_record *job_ptr,
			       struct node_record *node_ptr);

/*
 * Register a callback port for job events, set port to zero to clear
 * cmd_ptr IN - Input format "CALLBACK:JOBID:#:PORT:#"
 * cmd_uid IN - User issuing the RPC
 * cli_addr IN - Client communication address (host for response)
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *register_callback(char *cmd_ptr, uid_t cmd_uid,
			       slurm_addr_t cli_addr,
			       uint32_t protocol_version);

/*
 * Replace a job's failed or failing node
 * cmd_ptr IN - Input format "REPLACE_NODE:JOBID:#:NODE:name"
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *replace_node(char *cmd_ptr, uid_t cmd_uid,
			  uint32_t protocol_version);

/*
 * Restore all nonstop plugin state information
 */
extern int restore_nonstop_state(void);

/*
 * Save all nonstop plugin state information
 */
extern int save_nonstop_state(void);

/*
 * Report nonstop plugin global state/configuration information
 * cmd_ptr IN - Input format "SHOW_CONFIG
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *show_config(char *cmd_ptr, uid_t cmd_uid,
			 uint32_t protocol_version);

/*
 * Report nonstop plugin state information for a particular job
 * cmd_ptr IN - Input format "SHOW_JOB:JOBID:#
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *show_job(char *cmd_ptr, uid_t cmd_uid, uint32_t protocol_version);

/* Spawn thread to periodically save nonstop plugin state to disk */
extern int spawn_state_thread(void);

/* Terminate thread used to periodically save nonstop plugin state to disk */
extern void term_state_thread(void);

/*
 * Reset a job's time limit
 * cmd_ptr IN - Input format "TIME_INCR:JOBID:#:MINUTES:#
 * cmd_uid IN - User issuing the RPC
 * protocol_version IN - Communication protocol version number
 * RET - Response string, must be freed by the user
 */
extern char *time_incr(char *cmd_ptr, uid_t cmd_uid, uint32_t protocol_version);

#endif	/* _HAVE_NONSTOP_DO_WORK_H */
