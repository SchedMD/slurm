/*****************************************************************************\
 *  proc_args.h - helper functions for command argument processing
 *  $Id: opt.h 11996 2007-08-10 20:36:26Z jette $
 *****************************************************************************
 *  Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 *  Written by Christopher Holmes <cholmes@hp.com>, who borrowed heavily
 *  from existing SLURM source code, particularly src/srun/opt.c 
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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
int verify_conn_type(const char *arg);

/* verify the requested geometry arguments */
int verify_geometry(const char *arg, uint16_t *geometry);

/* return command name from its full path name */
char * base_name(char* command);

/* confirm and convert a str to it's presented numeric value */
long str_to_bytes(const char *arg);

/* verify that a node count in arg is of a known form (count or min-max) */
bool verify_node_count(const char *arg, int *min_nodes, int *max_nodes);

/* parse a possible range of values from the form: count, min-max, or '*' */
bool get_resource_arg_range(const char *arg, const char *what,
				   int* min, int *max, bool isFatal);

/* verify resource counts from a complex form of: X, X:X, X:X:X or X:X:X:X */
bool verify_socket_core_thread_count(const char *arg, 
				     int *min_sockets, int *max_sockets,
				     int *min_cores, int *max_cores,
				     int *min_threads, int  *max_threads,
				     cpu_bind_type_t *cpu_bind_type);

/* verify a hint and convert it into the implied settings */
bool verify_hint(const char *arg, int *min_sockets, int *max_sockets,
		 int *min_cores, int *max_cores, int *min_threads,
		 int  *max_threads, cpu_bind_type_t *cpu_bind_type);

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

#endif /* !_PROC_ARGS_H */
