/*****************************************************************************\
 *  scontrol.h - definitions for all scontrol modules
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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

#ifndef __SCONTROL_H__
#define __SCONTROL_H__

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
#  include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif
#include <time.h>
#include <unistd.h>

#if HAVE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else  /* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif  /* HAVE_INTTYPES_H */

#include <slurm/slurm.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/parse_spec.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define CKPT_WAIT	10
#define	MAX_INPUT_FIELDS 128

extern char *command_name;
extern int all_flag;	/* display even hidden partitions */
extern int exit_code;	/* scontrol's exit code, =1 on any error at any time */
extern int exit_flag;	/* program to terminate if =1 */
extern int input_words;	/* number of words of input permitted */
extern int one_liner;	/* one record per line if =1 */
extern int quiet_flag;	/* quiet=1, verbose=-1, normal=0 */

extern int	scontrol_checkpoint(char *op, char *job_step_id_str, int argc, 
				    char **argv);
extern int	scontrol_encode_hostlist(char *hostlist);
extern int	scontrol_job_notify(int argc, char *argv[]);
extern int 	scontrol_load_jobs (job_info_msg_t ** job_buffer_pptr);
extern int 	scontrol_load_nodes (node_info_msg_t ** node_buffer_pptr, 
				     uint16_t show_flags);
extern int 	scontrol_load_partitions (
	partition_info_msg_t **part_info_pptr);
extern int 	scontrol_load_node_select(
	node_select_info_msg_t **node_select_info_pptr);
extern void	scontrol_pid_info(pid_t job_pid);
extern void	scontrol_print_completing (void);
extern void	scontrol_print_completing_job(job_info_t *job_ptr, 
					      node_info_msg_t *node_info_msg);
extern void	scontrol_print_job (char * job_id_str);
extern void	scontrol_print_hosts (char * node_list);
extern void	scontrol_print_node (char *node_name, 
				     node_info_msg_t *node_info_ptr);
extern void	scontrol_print_node_list (char *node_list);
extern void	scontrol_print_part (char *partition_name);
extern void	scontrol_print_node_select (char *block_name);
extern void	scontrol_print_res (char *reservation_name);
extern void	scontrol_print_step (char *job_step_id_str);
extern void	scontrol_print_topo (char *node_list);
extern int	scontrol_requeue(char *job_step_id_str);
extern int	scontrol_suspend(char *op, char *job_id_str);
extern int	scontrol_update_job (int argc, char *argv[]);
extern int	scontrol_update_node (int argc, char *argv[]);
extern int	scontrol_update_part (int argc, char *argv[]);
extern int	scontrol_update_res (int argc, char *argv[]);
extern void     scontrol_list_pids(const char *jobid_str,
				   const char *node_name);
extern int	scontrol_create_part(int argc, char *argv[]);
extern int	scontrol_create_res(int argc, char *argv[]);

#endif
