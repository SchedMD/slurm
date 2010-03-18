/*****************************************************************************\
 *  proc_args.h - helper functions for command argument processing
 *  $Id: opt.h 11996 2007-08-10 20:36:26Z jette $
 *****************************************************************************
 *  Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 *  Written by Christopher Holmes <cholmes@hp.com>, who borrowed heavily
 *  from existing SLURM source code, particularly src/srun/opt.c
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

#ifndef _PROC_ARGS_H
#define _PROC_ARGS_H


#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h" /* true and false */
#include "src/common/env.h"


#define format_task_dist_states(t) (t == SLURM_DIST_BLOCK) ? "block" :   \
		                 (t == SLURM_DIST_CYCLIC) ? "cyclic" : \
		                 (t == SLURM_DIST_PLANE) ? "plane" : \
		                 (t == SLURM_DIST_CYCLIC_CYCLIC) ? "cyclic:cyclic" : \
		                 (t == SLURM_DIST_CYCLIC_BLOCK) ? "cyclic:block" : \
		                 (t == SLURM_DIST_BLOCK_CYCLIC) ? "block:cyclic" : \
		                 (t == SLURM_DIST_BLOCK_BLOCK) ? "block:block" : \
			         (t == SLURM_DIST_ARBITRARY) ? "arbitrary" : \
			         "unknown"


/* print this version of SLURM */
void print_slurm_version(void);

/* verify the requested distribution type */
task_dist_states_t verify_dist_type(const char *arg, uint32_t *plane_size);

/* verify the requested connection type */
uint16_t verify_conn_type(const char *arg);

/* verify the requested geometry arguments */
int verify_geometry(const char *arg, uint16_t *geometry);

/* return command name from its full path name */
char * base_name(char* command);

/* confirm and convert a str to it's presented numeric value */
long str_to_bytes(const char *arg);

/* verify that a node count in arg is of a known form (count or min-max) */
bool verify_node_count(const char *arg, int *min_nodes, int *max_nodes);

/* verify a node list is valid based on the dist and task count given */
bool verify_node_list(char **node_list_pptr, enum task_dist_states dist,
		      int task_count);

/*
 * get either 1 or 2 integers for a resource count in the form of either
 * (count, min-max, or '*')
 * A partial error message is passed in via the 'what' param.
 * IN arg - argument
 * IN what - variable name (for errors)
 * OUT min - first number
 * OUT max - maximum value if specified, NULL if don't care
 * IN isFatal - if set, exit on error
 * RET true if valid
 */
bool get_resource_arg_range(const char *arg, const char *what, int* min,
			    int *max, bool isFatal);

/* verify resource counts from a complex form of: X, X:X, X:X:X or X:X:X:X */
bool verify_socket_core_thread_count(const char *arg, int *min_sockets,
				     int *min_cores, int *min_threads,
				     cpu_bind_type_t *cpu_bind_type);

/* verify a hint and convert it into the implied settings */
bool verify_hint(const char *arg, int *min_sockets, int *min_cores,
		 int *min_threads, int *ntasks_per_core,
		 cpu_bind_type_t *cpu_bind_type);

/* parse the mail type */
uint16_t parse_mail_type(const char *arg);

/* print the mail type */
char *print_mail_type(const uint16_t type);

/* search PATH to confirm the access of the given command */
char *search_path(char *cwd, char *cmd, bool check_current_dir,
		  int access_mode);

/* helper function for printing options */
char *print_commandline(const int script_argc, char **script_argv);

/* helper function for printing geometry option */
char *print_geometry(const uint16_t *geometry);

/* Translate a signal option string "--signal=<int>[@<time>]" into
 * it's warn_signal and warn_time components.
 * RET 0 on success, -1 on failure */
int get_signal_opts(char *optarg, uint16_t *warn_signal, uint16_t *warn_time);

#endif /* !_PROC_ARGS_H */
