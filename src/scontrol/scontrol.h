/*****************************************************************************\
 *  scontrol.h - definitions for all scontrol modules
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef __SCONTROL_H__
#define __SCONTROL_H__

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if HAVE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

#include "slurm/slurm.h"

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurmdb_defs.h"

#define CKPT_WAIT	10
#define	MAX_INPUT_FIELDS 128

extern char *command_name;
extern List clusters;
extern int all_flag;	/* display even hidden partitions */
extern int detail_flag;	/* display additional details */
extern int future_flag;	/* display future nodes */
extern int exit_code;	/* scontrol's exit code, =1 on any error at any time */
extern int exit_flag;	/* program to terminate if =1 */
extern int federation_flag; /* show federated jobs */
extern int input_words;	/* number of words of input permitted */
extern int local_flag;	/* show only local jobs -- not remote remote sib jobs */
extern int one_liner;	/* one record per line if =1 */
extern int quiet_flag;	/* quiet=1, verbose=-1, normal=0 */
extern int sibling_flag; /* show sibling jobs (if any fed job). */
extern uint32_t cluster_flags; /* what type of cluster are we talking to */
extern uint32_t euid; /* send request to the slurmctld in behave of this user */

extern front_end_info_msg_t *old_front_end_info_ptr;
extern job_info_msg_t *old_job_info_ptr;
extern node_info_msg_t *old_node_info_ptr;
extern partition_info_msg_t *old_part_info_ptr;
extern reserve_info_msg_t *old_res_info_ptr;
extern slurm_ctl_conf_info_msg_t *old_slurm_ctl_conf_ptr;

extern int	parse_requeue_flags(char *s, uint32_t *flags);
extern int	scontrol_batch_script(int argc, char **argv);
extern int	scontrol_callerid(int argc, char **argv);
extern int	scontrol_checkpoint(char *op, char *job_step_id_str, int argc,
				    char **argv);
extern int	scontrol_create_part(int argc, char **argv);
extern int	scontrol_create_res(int argc, char **argv);
extern int	scontrol_encode_hostlist(char *hostlist, bool sorted);
extern uint16_t	scontrol_get_job_state(uint32_t job_id);
extern int	scontrol_hold(char *op, char *job_id_str);
extern int	scontrol_job_notify(int argc, char **argv);
extern int	scontrol_job_ready(char *job_id_str);
extern void	scontrol_list_pids(const char *jobid_str,
				   const char *node_name);
extern void	scontrol_getent(const char *node_name);
extern int	scontrol_load_front_end(front_end_info_msg_t **
					front_end_buffer_pptr);
extern int	scontrol_load_job(job_info_msg_t ** job_buffer_pptr,
				  uint32_t job_id);
extern int 	scontrol_load_jobs (job_info_msg_t ** job_buffer_pptr);
extern int 	scontrol_load_nodes (node_info_msg_t ** node_buffer_pptr,
				     uint16_t show_flags);
extern int 	scontrol_load_partitions (partition_info_msg_t **
					  part_info_pptr);
extern void	scontrol_pid_info(pid_t job_pid);
extern void	scontrol_print_assoc_mgr_info(int argc, char **argv);
extern void	scontrol_print_bbstat(int argc, char **argv);
extern void	scontrol_print_burst_buffer(void);
extern void	scontrol_print_completing (void);
extern void	scontrol_print_completing_job(job_info_t *job_ptr,
					      node_info_msg_t *node_info_msg);
extern void	scontrol_print_federation(void);
extern void	scontrol_print_front_end_list(char *node_list);
extern void	scontrol_print_front_end(char *node_name,
					 front_end_info_msg_t  *
					 front_end_buffer_ptr);
extern void	scontrol_print_job (char * job_id_str);
extern void	scontrol_print_hosts (char * node_list);
extern void	scontrol_print_licenses(const char *feature);
extern void	scontrol_print_node (char *node_name,
				     node_info_msg_t *node_info_ptr);
extern void	scontrol_print_node_list (char *node_list);
extern void	scontrol_print_part (char *partition_name);
extern void	scontrol_print_res (char *reservation_name);
extern void	scontrol_print_step (char *job_step_id_str);
extern void	scontrol_print_topo (char *node_list);
extern void	scontrol_print_layout (int argc, char **argv);
extern void	scontrol_print_powercap (char *node_list);
extern void	scontrol_requeue(uint32_t flags, char *job_str);
extern void	scontrol_requeue_hold(uint32_t flags, char *job_str);
extern void	scontrol_suspend(char *op, char *job_id_str);
extern void	scontrol_top_job(char *job_str);
extern int	scontrol_update_front_end (int argc, char **argv);
extern int	scontrol_update_job (int argc, char **argv);
extern int	scontrol_update_layout (int argc, char **argv);
extern int	scontrol_update_node (int argc, char **argv);
extern int	scontrol_update_part (int argc, char **argv);
extern int	scontrol_update_res (int argc, char **argv);
extern int	scontrol_update_step (int argc, char **argv);
extern int	scontrol_update_powercap (int argc, char **argv);

/* reboot_node.c */
extern int      scontrol_cancel_reboot(char *nodes);
extern int      scontrol_reboot_nodes(char *node_list, bool asap,
				      uint32_t next_state, char *reason);

#endif
