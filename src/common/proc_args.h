/*****************************************************************************\
 *  proc_args.h - helper functions for command argument processing
 *****************************************************************************
 *  Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 *  Written by Christopher Holmes <cholmes@hp.com>, who borrowed heavily
 *  from existing SLURM source code, particularly src/srun/opt.c
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h" /* true and false */
#include "src/common/env.h"

/* convert task state ID to equivalent string */
extern char *format_task_dist_states(task_dist_states_t t);

/* print this version of SLURM */
void print_slurm_version(void);

/* print the available gres options */
void print_gres_help(void);

/* set distribution type strings from distribution type const */
void set_distribution(task_dist_states_t distribution,
		      char **dist, char **lllp_dist);

/* verify the requested distribution type */
task_dist_states_t verify_dist_type(const char *arg, uint32_t *plane_size);

/*
 * verify comma separated list of connection types to array of uint16_t
 * connection_types or NO_VAL if not recognized
 */
extern void verify_conn_type(const char *arg, uint16_t *conn_type);

/* verify the requested geometry arguments */
int verify_geometry(const char *arg, uint16_t *geometry);

/* return command name from its full path name */
char * base_name(char* command);

/*
 * str_to_mbytes(): verify that arg is numeric with optional "K", "M", "G"
 * or "T" at end and return the number in mega-bytes. Default units are MB.
 */
long str_to_mbytes(const char *arg);

/*
 * str_to_mbytes2(): verify that arg is numeric with optional "K", "M", "G"
 * or "T" at end and return the number in mega-bytes. Default units are GB
 * if ???, otherwise MB.
 */
long str_to_mbytes2(const char *arg);

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

/*
 * search PATH to confirm the location and access mode of the given command
 * IN cwd - current working directory
 * IN cmd - command to execute
 * IN check_current_dir - if true, search cwd for the command
 * IN access_mode - required access rights of cmd
 * IN test_exec - if false, do not confirm access mode of cmd if full path
 * RET full path of cmd or NULL if not found
 */
char *search_path(char *cwd, char *cmd, bool check_current_dir, int access_mode,
		  bool test_exec);

/* helper function for printing options */
char *print_commandline(const int script_argc, char **script_argv);

/* helper function for printing geometry option */
char *print_geometry(const uint16_t *geometry);

/* Translate a signal option string "--signal=<int>[@<time>]" into
 * it's warn_signal and warn_time components.
 * RET 0 on success, -1 on failure */
int get_signal_opts(char *optarg, uint16_t *warn_signal, uint16_t *warn_time,
		    uint16_t *warn_flags);

/* Convert a signal name to it's numeric equivalent.
 * Return 0 on failure */
int sig_name2num(char *signal_name);

/*
 * parse_uint16/32/64 - Convert ascii string to a 16/32/64 bit unsigned int.
 * IN      aval - ascii string.
 * IN/OUT  ival - 16/32/64 bit pointer.
 * RET     0 if no error, 1 otherwise.
 */
extern int parse_uint16(char *aval, uint16_t *ival);
extern int parse_uint32(char *aval, uint32_t *ival);
extern int parse_uint64(char *aval, uint64_t *ival);

/* Get a decimal integer from arg
 * IN      name - command line name
 * IN      val - command line argument value
 * IN      positive - true if number needs to be greater than 0
 * RET     Returns the integer on success, exits program on failure.
 */
extern int parse_int(const char *name, const char *val, bool positive);


/* print_db_notok() - Print an error message about slurmdbd
 *                    is unreachable or wrong cluster name.
 * IN  cname - char * cluster name
 * IN  isenv - bool   cluster name from env or from command line option.
 */
extern void print_db_notok(const char *cname, bool isenv);



extern void bg_figure_nodes_tasks(int *min_nodes, int *max_nodes,
				  int *ntasks_per_node, bool *ntasks_set,
				  int *ntasks, bool nodes_set,
				  bool nodes_set_opt, bool overcommit,
				  bool set_tasks);

/*
 *  _parse_flags  is used to parse the Flags= option.  It handles
 *  daily, weekly, static_alloc, part_nodes, and maint, optionally
 *  preceded by + or -, separated by a comma but no spaces.
 */

extern uint32_t parse_resv_flags(const char *flagstr, const char *msg);

extern uint16_t parse_compress_type(const char *arg);

extern int validate_acctg_freq(char *acctg_freq);

#endif /* !_PROC_ARGS_H */
