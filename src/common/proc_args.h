/*****************************************************************************\
 *  proc_args.h - helper functions for command argument processing
 *****************************************************************************
 *  Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 *  Written by Christopher Holmes <cholmes@hp.com>, who borrowed heavily
 *  from existing Slurm source code, particularly src/srun/opt.c
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

#ifndef _PROC_ARGS_H
#define _PROC_ARGS_H

#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h" /* true and false */
#include "src/common/env.h"

/* convert task state ID to equivalent string */
extern char *format_task_dist_states(task_dist_states_t t);

/* print this version of Slurm */
void print_slurm_version(void);

/* print the available gres options */
void print_gres_help(void);

/*
 * Set distribution type strings from distribution type const.
 * The value is xmalloc'd and returned in *dist; caller must free.
 */
void set_distribution(task_dist_states_t distribution, char **dist);

/* check if path2 is subpath of path2 */
extern bool subpath(char *path1, char *path2);

/* verify the requested distribution type */
task_dist_states_t verify_dist_type(const char *arg, uint32_t *plane_size);

/* return command name from its full path name */
char *base_name(const char *command);

/*
 * str_to_mbytes(): verify that arg is numeric with optional "K", "M", "G"
 * or "T" at end and return the number in mega-bytes. Default units are MB.
 */
uint64_t str_to_mbytes(const char *arg);

/*
 * Reverse the above conversion. Returns an xmalloc()'d string.
 */
extern char *mbytes_to_str(uint64_t mbytes);

/* verify that a node count in arg is of a known form (count or min-max[:step]) */
bool verify_node_count(const char *arg, int *min_nodes, int *max_nodes,
		       char **job_size_str);

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
 * IN check_cwd_last - if true, search cwd after PATH is checked
 *                   - if false, search cwd for the command first
 * IN access_mode - required access rights of cmd
 * IN test_exec - if false, do not confirm access mode of cmd if full path
 * RET full path of cmd or NULL if not found
 */
char *search_path(char *cwd, char *cmd, bool check_cwd_last, int access_mode,
		  bool test_exec);

/* helper function for printing options */
char *print_commandline(const int script_argc, char **script_argv);

/* Translate a signal option string "--signal=<int>[@<time>]" into
 * it's warn_signal and warn_time components.
 * RET 0 on success, -1 on failure */
int get_signal_opts(char *optarg, uint16_t *warn_signal, uint16_t *warn_time,
		    uint16_t *warn_flags);
/* Return an xmalloc()'d string representing the original cmdline args */
extern char *signal_opts_to_cmdline(uint16_t warn_signal, uint16_t warn_time,
				    uint16_t warn_flags);

/* Convert a signal name to it's numeric equivalent.
 * Return 0 on failure */
int sig_name2num(const char *signal_name);
/* Return an xmalloc()'d string reversing the above conversion */
extern char *sig_num2name(int signal);

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

/*
 * parse_resv_flags() used to parse the Flags= option.  It handles
 * daily, weekly, static_alloc, part_nodes, and maint, optionally
 * preceded by + or -, separated by a comma but no spaces.
 *
 * flagstr IN - reservation flag string
 * msg IN - string to append to error message (e.g. function name)
 * resv_msg_ptr IN/OUT - sets flags and times in ptr.
 * RET equivalent reservation flag bits
 */
extern uint64_t parse_resv_flags(const char *flagstr, const char *msg,
				 resv_desc_msg_t  *resv_msg_ptr);

extern uint16_t parse_compress_type(const char *arg);

extern int parse_send_libs(const char *arg);

extern int validate_acctg_freq(char *acctg_freq);

/*
 * Format a tres_per_* argument
 * dest OUT - resulting string
 * prefix IN - TRES type (e.g. "gpu")
 * src IN - user input, can include multiple comma-separated specifications
 */
extern void xfmt_tres(char **dest, char *prefix, char *src);

/*
 * Format a tres_freq argument
 * dest OUT - resulting string
 * prefix IN - TRES type (e.g. "gpu")
 * src IN - user input
 */
extern void xfmt_tres_freq(char **dest, char *prefix, char *src);

#endif /* !_PROC_ARGS_H */
