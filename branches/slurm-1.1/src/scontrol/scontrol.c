/*****************************************************************************\
 *  scontrol - administration tool for slurm. 
 *	provides interface to read, write, update, and configurations.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
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
#define OPT_LONG_HIDE	0x102
#define	MAX_INPUT_FIELDS 128

static char *command_name;
static int all_flag;	/* display even hidden partitions */
static int exit_code;	/* scontrol's exit code, =1 on any error at any time */
static int exit_flag;	/* program to terminate if =1 */
static int input_words;	/* number of words of input permitted */
static int one_liner;	/* one record per line if =1 */
static int quiet_flag;	/* quiet=1, verbose=-1, normal=0 */

static int	_checkpoint(char *op, char *job_step_id_str);
static void	_delete_it (int argc, char *argv[]);
static int	_get_command (int *argc, char *argv[]);
static bool	_in_node_bit_list(int inx, int *node_list_array);
static int 	_load_jobs (job_info_msg_t ** job_buffer_pptr);
static int 	_load_nodes (node_info_msg_t ** node_buffer_pptr, 
			uint16_t show_flags);
static int 	_load_partitions (partition_info_msg_t **part_info_pptr);
static void	_pid_info(pid_t job_pid);
static void     _ping_slurmctld(char *control_machine, char *backup_controller);
static void	_print_completing (void);
static void	_print_completing_job(job_info_t *job_ptr, 
				node_info_msg_t *node_info_msg);
static void	_print_config (char *config_param);
static void     _print_daemons (void);
static void	_print_job (char * job_id_str);
static void	_print_node (char *node_name, node_info_msg_t *node_info_ptr);
static void	_print_node_list (char *node_list);
static void	_print_part (char *partition_name);
static void	_print_ping (void);
static void	_print_step (char *job_step_id_str);
static void     _print_version( void );
static int	_process_command (int argc, char *argv[]);
static int	_suspend(char *op, char *job_id_str);
static void	_update_it (int argc, char *argv[]);
static int	_update_job (int argc, char *argv[]);
static int	_update_node (int argc, char *argv[]);
static int	_update_part (int argc, char *argv[]);
static int	_update_bluegene_block (int argc, char *argv[]);
static void	_usage ();

int 
main (int argc, char *argv[]) 
{
	int error_code = SLURM_SUCCESS, i, opt_char, input_field_count;
	char **input_fields;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	int option_index;
	static struct option long_options[] = {
		{"all",      0, 0, 'a'},
		{"help",     0, 0, 'h'},
		{"hide",     0, 0, OPT_LONG_HIDE},
		{"oneliner", 0, 0, 'o'},
		{"quiet",    0, 0, 'q'},
		{"usage",    0, 0, 'h'},
		{"verbose",  0, 0, 'v'},
		{"version",  0, 0, 'V'},
		{NULL,       0, 0, 0}
	};

	command_name      = argv[0];
	all_flag          = 0;
	exit_code         = 0;
	exit_flag         = 0;
	input_field_count = 0;
	quiet_flag        = 0;
	log_init("scontrol", opts, SYSLOG_FACILITY_DAEMON, NULL);

	if (getenv ("SCONTROL_ALL"))
		all_flag= 1;

	while((opt_char = getopt_long(argc, argv, "ahoqvV",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, "Try \"scontrol --help\" for more information\n");
			exit(1);
			break;
		case (int)'a':
			all_flag = 1;
			break;
		case (int)'h':
			_usage ();
			exit(exit_code);
			break;
		case OPT_LONG_HIDE:
			all_flag = 0;
			break;
		case (int)'o':
			one_liner = 1;
			break;
		case (int)'q':
			quiet_flag = 1;
			break;
		case (int)'v':
			quiet_flag = -1;
			break;
		case (int)'V':
			_print_version();
			exit(exit_code);
			break;
		default:
			exit_code = 1;
			fprintf(stderr, "getopt error, returned %c\n", opt_char);
			exit(exit_code);
		}
	}

	if (argc > MAX_INPUT_FIELDS)	/* bogus input, but continue anyway */
		input_words = argc;
	else
		input_words = 128;
	input_fields = (char **) xmalloc (sizeof (char *) * input_words);
	if (optind < argc) {
		for (i = optind; i < argc; i++) {
			input_fields[input_field_count++] = argv[i];
		}	
	}

	if (input_field_count)
		exit_flag = 1;
	else
		error_code = _get_command (&input_field_count, input_fields);

	while (error_code == SLURM_SUCCESS) {
		error_code = _process_command (input_field_count, 
					       input_fields);
		if (error_code || exit_flag)
			break;
		error_code = _get_command (&input_field_count, input_fields);
	}			

	exit(exit_code);
}

static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
	if (quiet_flag == -1) {
		long version = slurm_api_version();
		printf("slurm_api_version: %ld, %ld.%ld.%ld\n", version,
			SLURM_VERSION_MAJOR(version), 
			SLURM_VERSION_MINOR(version),
			SLURM_VERSION_MICRO(version));
	}
}


#if !HAVE_READLINE
/*
 * Alternative to readline if readline is not available
 */
static char *
getline(const char *prompt)
{
	char buf[4096];
	char *line;
	int len;

	printf("%s", prompt);

	fgets(buf, 4096, stdin);
	len = strlen(buf);
	if ((len > 0) && (buf[len-1] == '\n'))
		buf[len-1] = '\0';
	else
		len++;
	line = malloc (len * sizeof(char));
	return strncpy(line, buf, len);
}
#endif

/*
 * _get_command - get a command from the user
 * OUT argc - location to store count of arguments
 * OUT argv - location to store the argument list
 */
static int 
_get_command (int *argc, char **argv) 
{
	char *in_line;
	static char *last_in_line = NULL;
	int i, in_line_size;
	static int last_in_line_size = 0;

	*argc = 0;

#if HAVE_READLINE
	in_line = readline ("scontrol: ");
#else
	in_line = getline("scontrol: ");
#endif
	if (in_line == NULL)
		return 0;
	else if (strcmp (in_line, "!!") == 0) {
		free (in_line);
		in_line = last_in_line;
		in_line_size = last_in_line_size;
	} else {
		if (last_in_line)
			free (last_in_line);
		last_in_line = in_line;
		last_in_line_size = in_line_size = strlen (in_line);
	}

#if HAVE_READLINE
	add_history(in_line);
#endif

	/* break in_line into tokens */
	for (i = 0; i < in_line_size; i++) {
		bool double_quote = false, single_quote = false;
		if (in_line[i] == '\0')
			break;
		if (isspace ((int) in_line[i]))
			continue;
		if (((*argc) + 1) > MAX_INPUT_FIELDS) {	/* bogus input line */
			exit_code = 1;
			fprintf (stderr, 
				 "%s: can not process over %d words\n",
				 command_name, input_words);
			return E2BIG;
		}		
		argv[(*argc)++] = &in_line[i];
		for (i++; i < in_line_size; i++) {
			if (in_line[i] == '\042') {
				double_quote = !double_quote;
				continue;
			}
			if (in_line[i] == '\047') {
				single_quote = !single_quote;
				continue;
			}
			if (in_line[i] == '\0')
				break;
			if (double_quote || single_quote)
				continue;
			if (isspace ((int) in_line[i])) {
				in_line[i] = '\0';
				break;
			}
		}		
	}
	return 0;		
}


/* Load current job table information into *job_buffer_pptr */
static int 
_load_jobs (job_info_msg_t ** job_buffer_pptr) 
{
	int error_code;
	static job_info_msg_t *old_job_info_ptr = NULL;
	static uint16_t last_show_flags = 0xffff;
	uint16_t show_flags = 0;
	job_info_msg_t * job_info_ptr = NULL;

	if (all_flag)
		show_flags |= SHOW_ALL;

	if (old_job_info_ptr) {
		if (last_show_flags != show_flags)
			old_job_info_ptr->last_update = (time_t) 0;
		error_code = slurm_load_jobs (old_job_info_ptr->last_update, 
					&job_info_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg (old_job_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			job_info_ptr = old_job_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
 				printf ("slurm_load_jobs no change in data\n");
		}
	}
	else
		error_code = slurm_load_jobs ((time_t) NULL, &job_info_ptr,
				show_flags);

	if (error_code == SLURM_SUCCESS) {
		old_job_info_ptr = job_info_ptr;
		last_show_flags  = show_flags;
		*job_buffer_pptr = job_info_ptr;
	}

	return error_code;
}
/* Load current node table information into *node_buffer_pptr */
static int 
_load_nodes (node_info_msg_t ** node_buffer_pptr, uint16_t show_flags) 
{
	int error_code;
	static node_info_msg_t *old_node_info_ptr = NULL;
	static int last_show_flags = 0xffff;
	node_info_msg_t *node_info_ptr = NULL;

	if (old_node_info_ptr) {
		if (last_show_flags != show_flags)
			old_node_info_ptr->last_update = (time_t) 0;
		error_code = slurm_load_node (old_node_info_ptr->last_update, 
			&node_info_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg (old_node_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			node_info_ptr = old_node_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
				printf ("slurm_load_node no change in data\n");
		}
	}
	else
		error_code = slurm_load_node ((time_t) NULL, &node_info_ptr,
				show_flags);

	if (error_code == SLURM_SUCCESS) {
		old_node_info_ptr = node_info_ptr;
		last_show_flags = show_flags;
		*node_buffer_pptr = node_info_ptr;
	}

	return error_code;
}

/* Load current partiton table information into *part_buffer_pptr */
static int 
_load_partitions (partition_info_msg_t **part_buffer_pptr)
{
	int error_code;
	static partition_info_msg_t *old_part_info_ptr = NULL;
	static uint16_t last_show_flags = 0xffff;
	uint16_t show_flags = 0;
	partition_info_msg_t *part_info_ptr = NULL;

	if (all_flag)
		show_flags |= SHOW_ALL;

	if (old_part_info_ptr) {
		if (last_show_flags != show_flags)
			old_part_info_ptr->last_update = (time_t) 0;
		error_code = slurm_load_partitions (
						old_part_info_ptr->last_update,
						&part_info_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg (old_part_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			part_info_ptr = old_part_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
				printf ("slurm_load_part no change in data\n");
		}
	}
	else
		error_code = slurm_load_partitions((time_t) NULL, 
						   &part_info_ptr, show_flags);

	if (error_code == SLURM_SUCCESS) {
		old_part_info_ptr = part_info_ptr;
		last_show_flags = show_flags;
		*part_buffer_pptr = part_info_ptr;
	}

	return error_code;
}

/* 
 * _pid_info - given a local process id, print the corresponding slurm job id
 *	and its expected end time
 * IN job_pid - the local process id of interest
 */
static void
_pid_info(pid_t job_pid)
{
	int error_code, i;
	uint32_t job_id;
	time_t end_time, start_time = time(NULL);
	long rem_time;

	error_code = slurm_pid2jobid (job_pid, &job_id);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_pid2jobid error");
		return;
	}

	error_code = slurm_get_end_time(job_id, &end_time);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_get_end_time error");
		return;
	}
	for (i=0; i<10000; i++)
		rem_time = slurm_get_rem_time(job_id);

	printf("Slurm job id %u ends at %s\n", job_id, ctime(&end_time));
	printf("slurm_get_rem_time is %ld\n", rem_time);
	printf("10000 slurm_get_rem_time calls in %d seconds\n",
		(int) difftime(time(NULL), start_time));
	return;
}


/*
 * print_completing - print jobs in completing state and associated nodes 
 * in COMPLETING or DOWN state
 */
static void	
_print_completing (void)
{
	int error_code, i;
	job_info_msg_t  *job_info_msg;
	job_info_t      *job_info;
	node_info_msg_t *node_info_msg;
	uint16_t         show_flags = 0;

	error_code = _load_jobs  (&job_info_msg);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_jobs error");
		return;
	}
	/* Must load all nodes including hidden for cross-index 
	 * from job's node_inx to node table to work */
	/*if (all_flag)		Always set this flag */
		show_flags |= SHOW_ALL;
	error_code = _load_nodes (&node_info_msg, show_flags);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_nodes error");
		return;
	}

	/* Scan the jobs for completing state */
	job_info = job_info_msg->job_array;
	for (i=0; i<job_info_msg->record_count; i++) {
		if (job_info[i].job_state & JOB_COMPLETING)
			_print_completing_job(&job_info[i], node_info_msg);
	}
}

static void
_print_completing_job(job_info_t *job_ptr, node_info_msg_t *node_info_msg)
{
	int i;
	node_info_t *node_info;
	uint16_t node_state, base_state;
	hostlist_t all_nodes, comp_nodes, down_nodes;
	char node_buf[1024];

	all_nodes  = hostlist_create(job_ptr->nodes);
	comp_nodes = hostlist_create("");
	down_nodes = hostlist_create("");

	node_info = node_info_msg->node_array;
	for (i=0; i<node_info_msg->record_count; i++) {
		node_state = node_info[i].node_state;
		base_state = node_info[i].node_state & NODE_STATE_BASE;
		if ((node_state & NODE_STATE_COMPLETING) && 
		    (_in_node_bit_list(i, job_ptr->node_inx)))
			hostlist_push_host(comp_nodes, node_info[i].name);
		else if ((base_state == NODE_STATE_DOWN) &&
			 (hostlist_find(all_nodes, node_info[i].name) != -1))
			hostlist_push_host(down_nodes, node_info[i].name);
	}

	fprintf(stdout, "JobId=%u ", job_ptr->job_id);
	i = hostlist_ranged_string(comp_nodes, sizeof(node_buf), node_buf);
	if (i > 0)
		fprintf(stdout, "Nodes(COMPLETING)=%s ", node_buf);
	i = hostlist_ranged_string(down_nodes, sizeof(node_buf), node_buf);
	if (i > 0)
		fprintf(stdout, "Nodes(DOWN)=%s ", node_buf);
	fprintf(stdout, "\n");

	hostlist_destroy(all_nodes);
	hostlist_destroy(comp_nodes);
	hostlist_destroy(down_nodes);
}

/*
 * Determine if a node index is in a node list pair array. 
 * RET -  true if specified index is in the array
 */
static bool
_in_node_bit_list(int inx, int *node_list_array)
{
	int i;
	bool rc = false;

	for (i=0; ; i+=2) {
		if (node_list_array[i] == -1)
			break;
		if ((inx >= node_list_array[i]) &&
		    (inx <= node_list_array[i+1])) {
			rc = true;
			break;
		}
	}

	return rc;
}

/* 
 * _print_config - print the specified configuration parameter and value 
 * IN config_param - NULL to print all parameters and values
 */
static void 
_print_config (char *config_param)
{
	int error_code;
	static slurm_ctl_conf_info_msg_t *old_slurm_ctl_conf_ptr = NULL;
	slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;

	if (old_slurm_ctl_conf_ptr) {
		error_code = slurm_load_ctl_conf (
				old_slurm_ctl_conf_ptr->last_update,
			 &slurm_ctl_conf_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_ctl_conf(old_slurm_ctl_conf_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			slurm_ctl_conf_ptr = old_slurm_ctl_conf_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
				printf ("slurm_load_ctl_conf no change in data\n");
		}
	}
	else
		error_code = slurm_load_ctl_conf ((time_t) NULL, 
						  &slurm_ctl_conf_ptr);

	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_ctl_conf error");
	}
	else
		old_slurm_ctl_conf_ptr = slurm_ctl_conf_ptr;


	if (error_code == SLURM_SUCCESS) {
		slurm_print_ctl_conf (stdout, slurm_ctl_conf_ptr) ;
		fprintf(stdout, "\n"); 
	}
	if (slurm_ctl_conf_ptr)
		_ping_slurmctld (slurm_ctl_conf_ptr->control_machine,
				 slurm_ctl_conf_ptr->backup_controller);
}

/* Print state of controllers only */
static void
_print_ping (void)
{
	slurm_ctl_conf_info_msg_t *conf;
	char *primary, *secondary;

	slurm_conf_init(NULL);

	conf = slurm_conf_lock();
	primary = xstrdup(conf->control_machine);
	secondary = xstrdup(conf->backup_controller);
	slurm_conf_unlock();

	_ping_slurmctld (primary, secondary);

	xfree(primary);
	xfree(secondary);
}

/* Report if slurmctld daemons are responding */
static void 
_ping_slurmctld(char *control_machine, char *backup_controller)
{
	static char *state[2] = { "UP", "DOWN" };
	int primary = 1, secondary = 1;

	if (slurm_ping(1) == SLURM_SUCCESS)
		primary = 0;
	if (slurm_ping(2) == SLURM_SUCCESS)
		secondary = 0;
	fprintf(stdout, "Slurmctld(primary/backup) ");
	if (control_machine || backup_controller) {
		fprintf(stdout, "at ");
		if (control_machine)
			fprintf(stdout, "%s/", control_machine);
		else
			fprintf(stdout, "(NULL)/");
		if (backup_controller)
			fprintf(stdout, "%s ", backup_controller);
		else
			fprintf(stdout, "(NULL) ");
	}
	fprintf(stdout, "are %s/%s\n", 
		state[primary], state[secondary]);
}

/*
 * _print_daemons - report what daemons should be running on this node
 */
static void
_print_daemons (void)
{
	slurm_ctl_conf_info_msg_t *conf;
	char me[MAX_SLURM_NAME], *b, *c, *n;
	int actld = 0, ctld = 0, d = 0;
	char daemon_list[] = "slurmctld slurmd";

	slurm_conf_init(NULL);
	conf = slurm_conf_lock();

	getnodename(me, MAX_SLURM_NAME);
	if ((b = conf->backup_controller)) {
		if ((strcmp(b, me) == 0) ||
		    (strcasecmp(b, "localhost") == 0))
			ctld = 1;
	}
	if ((c = conf->control_machine)) {
		actld = 1;
		if ((strcmp(c, me) == 0) ||
		    (strcasecmp(c, "localhost") == 0))
			ctld = 1;
	}
	slurm_conf_unlock();

	if ((n = slurm_conf_get_nodename(me))) {
		d = 1;
		xfree(n);
	} else if ((n = slurm_conf_get_nodename("localhost"))) {
		d = 1;
		xfree(n);
	}

	strcpy(daemon_list, "");
	if (actld && ctld)
		strcat(daemon_list, "slurmctld ");
	if (actld && d)
		strcat(daemon_list, "slurmd");
	fprintf (stdout, "%s\n", daemon_list) ;
}

/*
 * _print_job - print the specified job's information
 * IN job_id - job's id or NULL to print information about all jobs
 */
static void 
_print_job (char * job_id_str) 
{
	int error_code = SLURM_SUCCESS, i, print_cnt = 0;
	uint32_t job_id = 0;
	job_info_msg_t * job_buffer_ptr = NULL;
	job_info_t *job_ptr = NULL;

	error_code = _load_jobs(&job_buffer_ptr);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_jobs error");
		return;
	}
	
	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str ((time_t *)&job_buffer_ptr->last_update, 
				     time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n", 
			time_str, job_buffer_ptr->record_count);
	}

	if (job_id_str)
		job_id = (uint32_t) strtol (job_id_str, (char **)NULL, 10);

	job_ptr = job_buffer_ptr->job_array ;
	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		if (job_id_str && job_id != job_ptr[i].job_id) 
			continue;
		print_cnt++;
		slurm_print_job_info (stdout, & job_ptr[i], one_liner ) ;
		if (job_id_str)
			break;
	}

	if (print_cnt == 0) {
		if (job_id_str) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Job %u not found\n", job_id);
		} else if (quiet_flag != 1)
			printf ("No jobs in the system\n");
	}
}


/*
 * _print_node - print the specified node's information
 * IN node_name - NULL to print all node information
 * IN node_ptr - pointer to node table of information
 * NOTE: call this only after executing load_node, called from 
 *	_print_node_list
 * NOTE: To avoid linear searches, we remember the location of the 
 *	last name match
 */
static void
_print_node (char *node_name, node_info_msg_t  * node_buffer_ptr) 
{
	int i, j, print_cnt = 0;
	static int last_inx = 0;

	for (j = 0; j < node_buffer_ptr->record_count; j++) {
		if (node_name) {
			i = (j + last_inx) % node_buffer_ptr->record_count;
			if (strcmp (node_name, 
				    node_buffer_ptr->node_array[i].name))
				continue;
		}
		else
			i = j;
		print_cnt++;
		slurm_print_node_table (stdout, 
					& node_buffer_ptr->node_array[i], one_liner);

		if (node_name) {
			last_inx = i;
			break;
		}
	}

	if (print_cnt == 0) {
		if (node_name) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Node %s not found\n", node_name);
		} else if (quiet_flag != 1)
				printf ("No nodes in the system\n");
	}
}


/*
 * _print_node_list - print information about the supplied node list (or 
 *	regular expression)
 * IN node_list - print information about the supplied node list 
 *	(or regular expression)
 */
static void
_print_node_list (char *node_list) 
{
	node_info_msg_t *node_info_ptr = NULL;
	hostlist_t host_list;
	int error_code;
	uint16_t show_flags = 0;
	char *this_node_name;

	if (all_flag)
		show_flags |= SHOW_ALL;

	error_code = _load_nodes(&node_info_ptr, show_flags);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_node error");
		return;
	}

	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str ((time_t *)&node_info_ptr->last_update, 
			             time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n", 
			time_str, node_info_ptr->record_count);
	}

	if (node_list == NULL) {
		_print_node (NULL, node_info_ptr);
	}
	else {
		if ( (host_list = hostlist_create (node_list)) ) {
			while ((this_node_name = hostlist_shift (host_list))) {
				_print_node (this_node_name, node_info_ptr);
				free (this_node_name);
			}

			hostlist_destroy (host_list);
		}
		else {
			exit_code = 1;
			if (quiet_flag != 1) {
				if (errno == EINVAL)
					fprintf (stderr, 
					         "unable to parse node list %s\n", 
					         node_list);
				else if (errno == ERANGE)
					fprintf (stderr, 
					         "too many nodes in supplied range %s\n", 
					         node_list);
				else
					perror ("error parsing node list");
			}
		}
	}			
	return;
}


/*
 * _print_part - print the specified partition's information
 * IN partition_name - NULL to print information about all partition 
 */
static void 
_print_part (char *partition_name) 
{
	int error_code, i, print_cnt = 0;
	partition_info_msg_t *part_info_ptr = NULL;
	partition_info_t *part_ptr = NULL;

	error_code = _load_partitions(&part_info_ptr);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_partitions error");
		return;
	}

	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str ((time_t *)&part_info_ptr->last_update, 
			       time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n", 
			time_str, part_info_ptr->record_count);
	}

	part_ptr = part_info_ptr->partition_array;
	for (i = 0; i < part_info_ptr->record_count; i++) {
		if (partition_name && 
		    strcmp (partition_name, part_ptr[i].name) != 0)
			continue;
		print_cnt++;
		slurm_print_partition_info (stdout, & part_ptr[i], 
		                            one_liner ) ;
		if (partition_name)
			break;
	}

	if (print_cnt == 0) {
		if (partition_name) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Partition %s not found\n", 
				        partition_name);
		} else if (quiet_flag != 1)
			printf ("No partitions in the system\n");
	}
}


/*
 * _print_step - print the specified job step's information
 * IN job_step_id_str - job step's id or NULL to print information
 *	about all job steps
 */
static void 
_print_step (char *job_step_id_str)
{
	int error_code, i;
	uint32_t job_id = 0, step_id = 0, step_id_set = 0;
	char *next_str;
	job_step_info_response_msg_t *job_step_info_ptr;
	job_step_info_t * job_step_ptr;
	static uint32_t last_job_id = 0, last_step_id = 0;
	static job_step_info_response_msg_t *old_job_step_info_ptr = NULL;
	static uint16_t last_show_flags = 0xffff;
	uint16_t show_flags = 0;

	if (job_step_id_str) {
		job_id = (uint32_t) strtol (job_step_id_str, &next_str, 10);
		if (next_str[0] == '.') {
			step_id = (uint32_t) strtol (&next_str[1], NULL, 10);
			step_id_set = 1;
		}
	}

	if (all_flag)
		show_flags |= SHOW_ALL;

	if ((old_job_step_info_ptr) &&
	    (last_job_id == job_id) && (last_step_id == step_id)) {
		if (last_show_flags != show_flags)
			old_job_step_info_ptr->last_update = (time_t) 0;
		error_code = slurm_get_job_steps ( 
					old_job_step_info_ptr->last_update,
					job_id, step_id, &job_step_info_ptr,
					show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_step_info_response_msg (
					old_job_step_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			job_step_info_ptr = old_job_step_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
				printf ("slurm_get_job_steps no change in data\n");
		}
	}
	else {
		if (old_job_step_info_ptr) {
			slurm_free_job_step_info_response_msg (
					old_job_step_info_ptr);
			old_job_step_info_ptr = NULL;
		}
		error_code = slurm_get_job_steps ( (time_t) 0, job_id, step_id, 
				&job_step_info_ptr, show_flags);
	}

	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_get_job_steps error");
		return;
	}

	old_job_step_info_ptr = job_step_info_ptr;
	last_show_flags = show_flags;
	last_job_id = job_id;
	last_step_id = step_id;

	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str ((time_t *)&job_step_info_ptr->last_update, 
			             time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n", 
			time_str, job_step_info_ptr->job_step_count);
	}

	job_step_ptr = job_step_info_ptr->job_steps ;
	for (i = 0; i < job_step_info_ptr->job_step_count; i++) {
		if (step_id_set && (step_id == 0) && 
		    (job_step_ptr[i].step_id != 0)) 
			continue;
		slurm_print_job_step_info (stdout, & job_step_ptr[i], 
		                           one_liner ) ;
	}

	if (job_step_info_ptr->job_step_count == 0) {
		if (job_step_id_str) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Job step %u.%u not found\n", 
				        job_id, step_id);
		} else if (quiet_flag != 1)
			printf ("No job steps in the system\n");
	}
}


/*
 * _process_command - process the user's command
 * IN argc - count of arguments
 * IN argv - the arguments
 * RET 0 or errno (only for errors fatal to scontrol)
 */
static int
_process_command (int argc, char *argv[]) 
{
	int error_code;

	if (argc < 1) {
		exit_code = 1;
		if (quiet_flag == -1)
			fprintf(stderr, "no input");
	}
	else if (strncasecmp (argv[0], "abort", 5) == 0) {
		/* require full command name */
		if (argc > 2) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n", 
				 argv[0]);
		}
		error_code = slurm_shutdown (1);
		if (error_code) {
			exit_code = 1;
			if (quiet_flag != 1)
				slurm_perror ("slurm_shutdown error");
		}
	}
	else if (strncasecmp (argv[0], "all", 3) == 0)
		all_flag = 1;
	else if (strncasecmp (argv[0], "completing", 3) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n", 
				 argv[0]);
		}
		_print_completing();
	}
	else if (strncasecmp (argv[0], "exit", 1) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n", 
				 argv[0]);
		}
		exit_flag = 1;
	}
	else if (strncasecmp (argv[0], "help", 2) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		_usage ();
	}
	else if (strncasecmp (argv[0], "hide", 2) == 0)
		all_flag = 0;
	else if (strncasecmp (argv[0], "oneliner", 1) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		one_liner = 1;
	}
	else if (strncasecmp (argv[0], "pidinfo", 3) == 0) {
		if (argc > 2) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n", 
				 argv[0]);
		} else if (argc < 2) {
			exit_code = 1;
			fprintf (stderr, 
				 "missing argument for keyword:%s\n", 
				 argv[0]);
		} else
			_pid_info ((pid_t) atol (argv[1]) );

	}
	else if (strncasecmp (argv[0], "ping", 3) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		}
		_print_ping ();
	}
	else if (strncasecmp (argv[0], "quiet", 4) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, "too many arguments for keyword:%s\n", 
				 argv[0]);
		}
		quiet_flag = 1;
	}
	else if (strncasecmp (argv[0], "quit", 4) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr, 
				 "too many arguments for keyword:%s\n", 
				 argv[0]);
		}
		exit_flag = 1;
	}
	else if (strncasecmp (argv[0], "reconfigure", 3) == 0) {
		if (argc > 2) {
			exit_code = 1;
			fprintf (stderr, "too many arguments for keyword:%s\n",
			         argv[0]);
		}
		error_code = slurm_reconfigure ();
		if (error_code) {
			exit_code = 1;
			if (quiet_flag != 1)
				slurm_perror ("slurm_reconfigure error");
		}
	}
	else if (strncasecmp (argv[0], "checkpoint", 5) == 0) {
		if (argc > 3) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too many arguments for keyword:%s\n", 
				        argv[0]);
		}
		else if (argc < 3) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		}
		else {
			error_code =_checkpoint(argv[1], argv[2]);
			if (error_code) {
				exit_code = 1;
				if (quiet_flag != 1)
					slurm_perror ("slurm_checkpoint error");
			}
		}
	}
	else if ((strncasecmp (argv[0], "suspend", 3) == 0)
	||       (strncasecmp (argv[0], "resume", 3) == 0)) {
		if (argc > 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too many arguments for keyword:%s\n",
					argv[0]);
		}
		else if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr,
					"too few arguments for keyword:%s\n",
					argv[0]);
		} else {
			error_code =_suspend(argv[0], argv[1]);
			if (error_code) {
				exit_code = 1;
				if (quiet_flag != 1)
					slurm_perror ("slurm_suspend error");
			}
		}
	}
	else if (strncasecmp (argv[0], "show", 3) == 0) {
		if (argc > 3) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too many arguments for keyword:%s\n", 
				        argv[0]);
		}
		else if (argc < 2) {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf(stderr, 
				        "too few arguments for keyword:%s\n", 
				        argv[0]);
		}
		else if (strncasecmp (argv[1], "config", 3) == 0) {
			if (argc > 2)
				_print_config (argv[2]);
			else
				_print_config (NULL);
		}
		else if (strncasecmp (argv[1], "daemons", 3) == 0) {
			if (argc > 2) {
				exit_code = 1;
				if (quiet_flag != 1)
					fprintf(stderr,
					        "too many arguments for keyword:%s\n", 
					        argv[0]);
			}
			_print_daemons ();
		}
		else if (strncasecmp (argv[1], "jobs", 3) == 0) {
			if (argc > 2)
				_print_job (argv[2]);
			else
				_print_job (NULL);
		}
		else if (strncasecmp (argv[1], "nodes", 3) == 0) {
			if (argc > 2)
				_print_node_list (argv[2]);
			else
				_print_node_list (NULL);
		}
		else if (strncasecmp (argv[1], "partitions", 3) == 0) {
			if (argc > 2)
				_print_part (argv[2]);
			else
				_print_part (NULL);
		}
		else if (strncasecmp (argv[1], "steps", 3) == 0) {
			if (argc > 2)
				_print_step (argv[2]);
			else
				_print_step (NULL);
		}
		else {
			exit_code = 1;
			if (quiet_flag != 1)
				fprintf (stderr,
					 "invalid entity:%s for keyword:%s \n",
					 argv[1], argv[0]);
		}		

	}
	else if (strncasecmp (argv[0], "shutdown", 8) == 0) {
		/* require full command name */
		if (argc > 2) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for keyword:%s\n", 
				 argv[0]);
		}
		error_code = slurm_shutdown (0);
		if (error_code) {
			exit_code = 1;
			if (quiet_flag != 1)
				slurm_perror ("slurm_shutdown error");
		}
	}
	else if (strncasecmp (argv[0], "update", 1) == 0) {
		if (argc < 2) {
			exit_code = 1;
			fprintf (stderr, "too few arguments for %s keyword\n",
				 argv[0]);
			return 0;
		}		
		_update_it ((argc - 1), &argv[1]);
	}
	else if (strncasecmp (argv[0], "delete", 3) == 0) {
		if (argc < 2) {
			exit_code = 1;
			fprintf (stderr, "too few arguments for %s keyword\n",
				 argv[0]);
			return 0;
		}
		_delete_it ((argc - 1), &argv[1]);
	}
	else if (strncasecmp (argv[0], "verbose", 4) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		quiet_flag = -1;
	}
	else if (strncasecmp (argv[0], "version", 4) == 0) {
		if (argc > 1) {
			exit_code = 1;
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		_print_version();
	}
	else {
		exit_code = 1;
		fprintf (stderr, "invalid keyword: %s\n", argv[0]);
	}

	return 0;
}

/* 
 * _delete_it - delete the slurm the specified slurm entity 
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void
_delete_it (int argc, char *argv[]) 
{
	delete_part_msg_t part_msg;

	/* First identify the entity type to delete */
	if (strncasecmp (argv[0], "PartitionName=", 14) == 0) {
		part_msg.name = argv[0] + 14;
		if (slurm_delete_partition(&part_msg)) {
			char errmsg[64];
			snprintf(errmsg, 64, "delete_partition %s", argv[0]);
			slurm_perror(errmsg);
		}
	} else {
		exit_code = 1;
		fprintf(stderr, "Invalid deletion entity: %s\n", argv[1]);
	}
}


/* 
 * _update_it - update the slurm configuration per the supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 */
static void
_update_it (int argc, char *argv[]) 
{
	int i, error_code = SLURM_SUCCESS;

	/* First identify the entity to update */
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "NodeName=", 9) == 0) {
			error_code = _update_node (argc, argv);
			break;
		} else if (strncasecmp (argv[i], "PartitionName=", 14) == 0) {
			error_code = _update_part (argc, argv);
			break;
		} else if (strncasecmp (argv[i], "JobId=", 6) == 0) {
			error_code = _update_job (argc, argv);
			break;
		} else if (strncasecmp (argv[i], "BlockName=", 10) == 0) {
			error_code = _update_bluegene_block (argc, argv);
			break;
		}
		
	}
	
	if (i >= argc) {
		exit_code = 1;
		fprintf(stderr, "No valid entity in update command\n");
		fprintf(stderr, "Input line must include \"NodeName\", ");
#ifdef HAVE_BG
		fprintf(stderr, "\"BlockName\", ");
#endif
		fprintf(stderr, "\"PartitionName\", or \"JobId\"\n");
	}
	else if (error_code) {
		exit_code = 1;
		slurm_perror ("slurm_update error");
	}
}

/* 
 * _update_job - update the slurm job configuration per the supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints 
 *			error message and returns 0
 */
static int
_update_job (int argc, char *argv[]) 
{
	int i, update_cnt = 0;
	job_desc_msg_t job_msg;

	slurm_init_job_desc_msg (&job_msg);	

	for (i=0; i<argc; i++) {
		if (strncasecmp(argv[i], "JobId=", 6) == 0)
			job_msg.job_id = 
				(uint32_t) strtol(&argv[i][6], 
						 (char **) NULL, 10);
		else if (strncasecmp(argv[i], "TimeLimit=", 10) == 0) {
			if ((strcasecmp(&argv[i][10], "UNLIMITED") == 0) ||
			    (strcasecmp(&argv[i][10], "INFINITE") == 0))
				job_msg.time_limit = INFINITE;
			else
				job_msg.time_limit = 
					(uint32_t) strtol(&argv[i][10], 
							  (char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Priority=", 9) == 0) {
			job_msg.priority = 
				(uint32_t) strtoll(&argv[i][9], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Nice=", 5) == 0) {
			int nice;
			nice = strtoll(&argv[i][5], (char **) NULL, 10);
			if (abs(nice) > NICE_OFFSET) {
				error("Invalid nice value, must be between "
					"-%d and %d", NICE_OFFSET, NICE_OFFSET);
				exit_code = 1;
				return 0;
			}
			job_msg.nice = NICE_OFFSET + nice;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Nice", 4) == 0) {
			job_msg.nice = NICE_OFFSET + 100;
			update_cnt++;
		}		
		else if (strncasecmp(argv[i], "ReqProcs=", 9) == 0) {
			job_msg.num_procs = 
				(uint32_t) strtol(&argv[i][9], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinNodes=", 9) == 0) {
			job_msg.min_nodes = 
				(uint32_t) strtol(&argv[i][9],
						 (char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinProcs=", 9) == 0) {
			job_msg.min_procs = 
				(uint32_t) strtol(&argv[i][9], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinMemory=", 10) == 0) {
			job_msg.min_memory = 
				(uint32_t) strtol(&argv[i][10], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinTmpDisk=", 11) == 0) {
			job_msg.min_tmp_disk = 
				(uint32_t) strtol(&argv[i][11], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Partition=", 10) == 0) {
			job_msg.partition = &argv[i][10];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Name=", 5) == 0) {
			job_msg.name = &argv[i][5];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Shared=", 7) == 0) {
			if (strcasecmp(&argv[i][7], "YES") == 0)
				job_msg.shared = 1;
			else if (strcasecmp(&argv[i][7], "NO") == 0)
				job_msg.shared = 0;
			else
				job_msg.shared = 
					(uint16_t) strtol(&argv[i][7], 
							(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Contiguous=", 11) == 0) {
			if (strcasecmp(&argv[i][11], "YES") == 0)
				job_msg.contiguous = 1;
			else if (strcasecmp(&argv[i][11], "NO") == 0)
				job_msg.contiguous = 0;
			else
				job_msg.contiguous = 
					(uint16_t) strtol(&argv[i][11], 
							(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "ReqNodeList=", 12) == 0) {
			job_msg.req_nodes = &argv[i][12];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Features=", 9) == 0) {
			job_msg.features = &argv[i][9];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Account=", 8) == 0) {
			job_msg.account = &argv[i][8];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Dependency=", 11) == 0) {
			job_msg.dependency =
				(uint32_t) strtol(&argv[i][11],
					(char **) NULL, 10);
			update_cnt++;
		}
#ifdef HAVE_BG
		else if (strncasecmp(argv[i], "Geometry=", 9) == 0) {
			char* token, *delimiter = ",x", *next_ptr;
			int j, rc = 0;
			uint16_t geo[SYSTEM_DIMENSIONS];
			char* geometry_tmp = xstrdup(&argv[i][9]);
			char* original_ptr = geometry_tmp;
			token = strtok_r(geometry_tmp, delimiter, &next_ptr);
			for (j=0; j<SYSTEM_DIMENSIONS; j++) {
				if (token == NULL) {
					error("insufficient dimensions in "
						"Geometry");
					rc = -1;
					break;
				}
				geo[j] = (uint16_t) atoi(token);
				if (geo[j] <= 0) {
					error("invalid --geometry argument");
					rc = -1;
					break;
				}
				geometry_tmp = next_ptr;
				token = strtok_r(geometry_tmp, delimiter, 
					&next_ptr);
			}
			if (token != NULL) {
				error("too many dimensions in Geometry");
				rc = -1;
			}

			if (original_ptr)
				xfree(original_ptr);
			if (rc != 0) {
				for (j=0; j<SYSTEM_DIMENSIONS; j++)
					geo[j] = (uint16_t) NO_VAL;
				exit_code = 1;
			} else
				update_cnt++;
			select_g_set_jobinfo(job_msg.select_jobinfo,
					     SELECT_DATA_GEOMETRY,
					     (void *) &geo);			
		}

		else if (strncasecmp(argv[i], "Rotate=", 7) == 0) {
			uint16_t rotate;
			if (strcasecmp(&argv[i][7], "yes") == 0)
				rotate = 1;
			else if (strcasecmp(&argv[i][7], "no") == 0)
				rotate = 0;
			else
				rotate = (uint16_t) strtol(&argv[i][7], 
							   (char **) NULL, 10);
			select_g_set_jobinfo(job_msg.select_jobinfo,
					     SELECT_DATA_ROTATE,
					     (void *) &rotate);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Connection=", 11) == 0) {
			uint16_t conn_type;
			if (strcasecmp(&argv[i][11], "torus") == 0)
				conn_type = SELECT_TORUS;
			else if (strcasecmp(&argv[i][11], "mesh") == 0)
				conn_type = SELECT_MESH;
			else if (strcasecmp(&argv[i][11], "nav") == 0)
				conn_type = SELECT_NAV;
			else
				conn_type = 
					(uint16_t) strtol(&argv[i][11], 
							(char **) NULL, 10);
			select_g_set_jobinfo(job_msg.select_jobinfo,
					     SELECT_DATA_CONN_TYPE,
					     (void *) &conn_type);
			update_cnt++;
		}
#endif
		else if (strncasecmp(argv[i], "StartTime=", 10) == 0) {
			job_msg.begin_time = parse_time(&argv[i][10]);
			update_cnt++;
		}
		else {
			exit_code = 1;
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return 0;
		}
	}

	if (update_cnt == 0) {
		exit_code = 1;
		fprintf (stderr, "No changes specified\n");
		return 0;
	}

	if (slurm_update_job(&job_msg))
		return slurm_get_errno ();
	else
		return 0;
}

/* 
 * _update_node - update the slurm node configuration per the supplied
 *	arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints 
 *			error message and returns 0
 */
static int
_update_node (int argc, char *argv[]) 
{
	int i, j, k, rc = 0, update_cnt = 0;
	uint16_t state_val;
	update_node_msg_t node_msg;
	char *reason_str = NULL;
	char *user_name;

	node_msg.node_names = NULL;
	node_msg.reason = NULL;
	node_msg.node_state = (uint16_t) NO_VAL;
	for (i=0; i<argc; i++) {
		if (strncasecmp(argv[i], "NodeName=", 9) == 0)
			node_msg.node_names = &argv[i][9];
		else if (strncasecmp(argv[i], "Reason=", 7) == 0) {
			char time_buf[64], time_str[32];
			time_t now;
			int len = strlen(&argv[i][7]);
			reason_str = xmalloc(len+1);
			if (argv[i][7] == '"')
				strcpy(reason_str, &argv[i][8]);
			else
				strcpy(reason_str, &argv[i][7]);

			len = strlen(reason_str) - 1;
			if ((len >= 0) && (reason_str[len] == '"'))
				reason_str[len] = '\0';

			/* Append user, date and time */
			xstrcat(reason_str, " [");
			user_name = getlogin();
			if (user_name)
				xstrcat(reason_str, user_name);
			else {
				sprintf(time_buf, "%d", getuid());
				xstrcat(reason_str, time_buf);
			}
			now = time(NULL);
			slurm_make_time_str(&now, time_str, sizeof(time_str));
			snprintf(time_buf, sizeof(time_buf), "@%s]", time_str); 
			xstrcat(reason_str, time_buf);
				
			node_msg.reason = reason_str;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=NoResp", 12) == 0) {
			node_msg.node_state = NODE_STATE_NO_RESPOND;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=DRAIN", 11) == 0) {
			node_msg.node_state = NODE_STATE_DRAIN;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=RES", 9) == 0) {
			node_msg.node_state = NODE_RESUME;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=", 6) == 0) {
			state_val = (uint16_t) NO_VAL;
			for (j = 0; j <= NODE_STATE_END; j++) {
				if (strcasecmp (node_state_string(j), 
				                &argv[i][6]) == 0) {
					state_val = (uint16_t) j;
					break;
				}
				if (j == NODE_STATE_END) {
					exit_code = 1;
					fprintf(stderr, "Invalid input: %s\n", 
						argv[i]);
					fprintf (stderr, "Request aborted\n");
					fprintf (stderr, "Valid states are: ");
					fprintf (stderr, "NoResp DRAIN RES ");
					for (k = 0; k < NODE_STATE_END; k++) {
						fprintf (stderr, "%s ", 
						         node_state_string(k));
					}
					fprintf (stderr, "\n");
					goto done;
				}
			}
			node_msg.node_state = state_val;
			update_cnt++;
		}
		else {
			exit_code = 1;
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			goto done;
		}
	}

	if ((node_msg.node_state == NODE_STATE_DRAIN)  &&
	    (node_msg.reason == NULL)) {
		fprintf (stderr, "You must specify a reason when DRAINING a "
			"node\nRequest aborted\n");
		goto done;
	}

	if (update_cnt == 0) {
		exit_code = 1;
		fprintf (stderr, "No changes specified\n");
		return 0;
	}

	rc = slurm_update_node(&node_msg);

done:	xfree(reason_str);
	if (rc) {
		exit_code = 1;
		return slurm_get_errno ();
	} else
		return 0;
}

/* 
 * _update_part - update the slurm partition configuration per the 
 *	supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints 
 *			error message and returns 0
 */
static int
_update_part (int argc, char *argv[]) 
{
	int i, update_cnt = 0;
	update_part_msg_t part_msg;

	slurm_init_part_desc_msg ( &part_msg );
	for (i=0; i<argc; i++) {
		if (strncasecmp(argv[i], "PartitionName=", 14) == 0)
			part_msg.name = &argv[i][14];
		else if (strncasecmp(argv[i], "MaxTime=", 8) == 0) {
			if ((strcasecmp(&argv[i][8],"UNLIMITED") == 0) ||
			    (strcasecmp(&argv[i][8],"INFINITE") == 0))
				part_msg.max_time = INFINITE;
			else
				part_msg.max_time = 
					(uint32_t) strtol(&argv[i][8], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MaxNodes=", 9) == 0) {
			if ((strcasecmp(&argv[i][9],"UNLIMITED") == 0) ||
			    (strcasecmp(&argv[i][8],"INFINITE") == 0))
				part_msg.max_nodes = INFINITE;
			else
				part_msg.max_nodes = 
					(uint32_t) strtol(&argv[i][9], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinNodes=", 9) == 0) {
			if ((strcasecmp(&argv[i][9],"UNLIMITED") == 0) ||
			    (strcasecmp(&argv[i][8],"INFINITE") == 0))
				part_msg.min_nodes = INFINITE;
			else
				part_msg.min_nodes = 
					(uint32_t) strtol(&argv[i][9], 
						(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Default=", 8) == 0) {
			if (strcasecmp(&argv[i][8], "NO") == 0)
				part_msg.default_part = 0;
			else if (strcasecmp(&argv[i][8], "YES") == 0)
				part_msg.default_part = 1;
			else {
				exit_code = 1;
				fprintf (stderr, "Invalid input: %s\n", 
					 argv[i]);
				fprintf (stderr, "Acceptable Default values "
					"are YES and NO\n");
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Hidden=", 4) == 0) {
			if (strcasecmp(&argv[i][7], "NO") == 0)
				part_msg.hidden = 0;
			else if (strcasecmp(&argv[i][7], "YES") == 0)
				part_msg.hidden = 1;
			else {
				exit_code = 1;
				fprintf (stderr, "Invalid input: %s\n", 
					 argv[i]);
				fprintf (stderr, "Acceptable Hidden values "
					"are YES and NO\n");
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "RootOnly=", 4) == 0) {
			if (strcasecmp(&argv[i][9], "NO") == 0)
				part_msg.root_only = 0;
			else if (strcasecmp(&argv[i][9], "YES") == 0)
				part_msg.root_only = 1;
			else {
				exit_code = 1;
				fprintf (stderr, "Invalid input: %s\n", 
					 argv[i]);
				fprintf (stderr, "Acceptable RootOnly values "
					"are YES and NO\n");
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Shared=", 7) == 0) {
			if (strcasecmp(&argv[i][7], "NO") == 0)
				part_msg.shared = SHARED_NO;
			else if (strcasecmp(&argv[i][7], "YES") == 0)
				part_msg.shared = SHARED_YES;
			else if (strcasecmp(&argv[i][7], "FORCE") == 0)
				part_msg.shared = SHARED_FORCE;
			else {
				exit_code = 1;
				fprintf (stderr, "Invalid input: %s\n", 
					 argv[i]);
				fprintf (stderr, "Acceptable Shared values "
					"are YES, NO and FORCE\n");
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=", 6) == 0) {
			if (strcasecmp(&argv[i][6], "DOWN") == 0)
				part_msg.state_up = 0;
			else if (strcasecmp(&argv[i][6], "UP") == 0)
				part_msg.state_up = 1;
			else {
				exit_code = 1;
				fprintf (stderr, "Invalid input: %s\n", 
					 argv[i]);
				fprintf (stderr, "Acceptable State values "
					"are UP and DOWN\n");
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Nodes=", 6) == 0) {
			part_msg.nodes = &argv[i][6];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "AllowGroups=", 12) == 0) {
			part_msg.allow_groups = &argv[i][12];
			update_cnt++;
		}
		else {
			exit_code = 1;
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return 0;
		}
	}

	if (update_cnt == 0) {
		exit_code = 1;
		fprintf (stderr, "No changes specified\n");
		return 0;
	}

	if (slurm_update_partition(&part_msg)) {
		exit_code = 1;
		return slurm_get_errno ();
	} else
		return 0;
}

/* 
 * _update_bluegene_block - update the bluegene block per the 
 *	supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints 
 *			error message and returns 0
 */
static int
_update_bluegene_block (int argc, char *argv[]) 
{
#ifdef HAVE_BG
	int i, update_cnt = 0;
	update_part_msg_t part_msg;

	slurm_init_part_desc_msg ( &part_msg );
	/* means this is for bluegene */
	part_msg.hidden = (uint16_t)INFINITE;

	for (i=0; i<argc; i++) {
		if (strncasecmp(argv[i], "BlockName=", 10) == 0)
			part_msg.name = &argv[i][10];
		else if (strncasecmp(argv[i], "State=", 6) == 0) {
			if (strcasecmp(&argv[i][6], "ERROR") == 0)
				part_msg.state_up = 0;
			else if (strcasecmp(&argv[i][6], "FREE") == 0)
				part_msg.state_up = 1;
			else {
				exit_code = 1;
				fprintf (stderr, "Invalid input: %s\n", 
					 argv[i]);
				fprintf (stderr, "Acceptable State values "
					"are FREE and ERROR\n");
				return 0;
			}
			update_cnt++;
		}
	}
	if (slurm_update_partition(&part_msg)) {
		exit_code = 1;
		return slurm_get_errno ();
	} else
		return 0;
#else
	printf("This only works on a bluegene system.\n");
	return 0;
#endif
}

/* _usage - show the valid scontrol commands */
void
_usage () {
	printf ("\
scontrol [<OPTION>] [<COMMAND>]                                            \n\
    Valid <OPTION> values are:                                             \n\
     -a or --all: equivalent to \"all\" command                            \n\
     -h or --help: equivalent to \"help\" command                          \n\
     --hide: equivalent to \"hide\" command                                \n\
     -o or --oneliner: equivalent to \"oneliner\" command                  \n\
     -q or --quiet: equivalent to \"quite\" command                        \n\
     -v or --verbose: equivalent to \"verbose\" command                    \n\
     -V or --version: equivalent to \"version\" command                    \n\
                                                                           \n\
  <keyword> may be omitted from the execute line and scontrol will execute \n\
  in interactive mode. It will process commands as entered until explicitly\n\
  terminated.                                                              \n\
                                                                           \n\
    Valid <COMMAND> values are:                                            \n\
     abort                    shutdown slurm controller immediately        \n\
                              generating a core file.                      \n\
     all                      display information about all partitions,    \n\
                              including hidden partitions.                 \n\
     checkpoint <CH_OP><step> perform a checkpoint operation on identified \n\
                              job step \n\
     completing               display jobs in completing state along with  \n\
                              their completing or down nodes               \n\
     delete <SPECIFICATIONS>  delete the specified partition, kill its jobs\n\
     exit                     terminate scontrol                           \n\
     help                     print this description of use.               \n\
     hide                     do not display information about hidden partitions.\n\
     oneliner                 report output one record per line.           \n\
     pidinfo <pid>            return slurm job information for given pid.  \n\
     ping                     print status of slurmctld daemons.           \n\
     quiet                    print no messages other than error messages. \n\
     quit                     terminate this command.                      \n\
     reconfigure              re-read configuration files.                 \n\
     show <ENTITY> [<ID>]     display state of identified entity, default  \n\
                              is all records.                              \n\
     shutdown                 shutdown slurm controller.                   \n\
     suspend <job_id>         susend specified job                         \n\
     resume <job_id>          resume previously suspended job              \n\
     update <SPECIFICATIONS>  update job, node, partition, or bluegene     \n\
                              block configuration                          \n\
     verbose                  enable detailed logging.                     \n\
     version                  display tool version number.                 \n\
     !!                       Repeat the last command entered.             \n\
                                                                           \n\
  <ENTITY> may be \"config\", \"daemons\", \"job\", \"node\", \"partition\"\n\
           \"block\" or \"step\".                                          \n\
                                                                           \n\
  <ID> may be a configuration parameter name , job id, node name, partition\n\
       name or job step id.                                                \n\
                                                                           \n\
  Node names may be specified using simple range expressions,              \n\
  (e.g. \"lx[10-20]\" corresponsds to lx10, lx11, lx12, ...)               \n\
  The job step id is the job id followed by a period and the step id.      \n\
                                                                           \n\
  <SPECIFICATIONS> are specified in the same format as the configuration   \n\
  file. You may wish to use the \"show\" keyword then use its output as    \n\
  input for the update keyword, editing as needed.  Bluegene blocks are    \n\
  only able to be set to an error or free state. (Bluegene systems only)   \n\
                                                                           \n\
  <CH_OP> identify checkpoint operations and may be \"able\", \"disable\", \n\
  \"enable\", \"create\", \"vacate\", \"restart\", or \"error\".           \n\
                                                                           \n\
  All commands and options are case-insensitive, although node names and   \n\
  partition names tests are case-sensitive (node names \"LX\" and \"lx\"   \n\
  are distinct).                                                       \n\n");

}

/* 
 * _checkpoint - perform some checkpoint/resume operation
 * IN op - checkpoint operation
 * IN job_step_id_str - either a job name (for all steps of the given job) or 
 *			a step name: "<jid>.<step_id>"
 * RET 0 if no slurm error, errno otherwise. parsing error prints 
 *			error message and returns 0
 */
static int _checkpoint(char *op, char *job_step_id_str)
{
	int rc = SLURM_SUCCESS;
	uint32_t job_id = 0, step_id = 0, step_id_set = 0;
	char *next_str;
	uint32_t ckpt_errno;
	char *ckpt_strerror = NULL;

	if (job_step_id_str) {
		job_id = (uint32_t) strtol (job_step_id_str, &next_str, 10);
		if (next_str[0] == '.') {
			step_id = (uint32_t) strtol (&next_str[1], &next_str, 10);
			step_id_set = 1;
		} else
			step_id = NO_VAL;
		if (next_str[0] != '\0') {
			fprintf(stderr, "Invalid job step name\n");
			return 0;
		}
	} else {
		fprintf(stderr, "Invalid job step name\n");
		return 0;
	}

	if (strncasecmp(op, "able", 2) == 0) {
		time_t start_time;
		rc = slurm_checkpoint_able (job_id, step_id, &start_time);
		if (rc == SLURM_SUCCESS) {
			if (start_time) {
				char buf[128], time_str[32];
				slurm_make_time_str(&start_time, time_str,
					sizeof(time_str));
				snprintf(buf, sizeof(buf), 
					"Began at %s\n", time_str); 
				printf(time_str);
			} else
				printf("Yes\n");
		} else if (slurm_get_errno() == ESLURM_DISABLED) {
			printf("No\n");
			rc = SLURM_SUCCESS;	/* not real error */
		}
	}
	else if (strncasecmp(op, "complete", 3) == 0) {
		/* Undocumented option used for testing purposes */
		static uint32_t error_code = 1;
		char error_msg[64];
		sprintf(error_msg, "test error message %d", error_code);
		rc = slurm_checkpoint_complete(job_id, step_id, (time_t) 0,
			error_code++, error_msg);
	}
	else if (strncasecmp(op, "disable", 3) == 0)
		rc = slurm_checkpoint_disable (job_id, step_id);
	else if (strncasecmp(op, "enable", 2) == 0)
		rc = slurm_checkpoint_enable (job_id, step_id);
	else if (strncasecmp(op, "create", 2) == 0)
		rc = slurm_checkpoint_create (job_id, step_id, CKPT_WAIT);
	else if (strncasecmp(op, "vacate", 2) == 0)
		rc = slurm_checkpoint_vacate (job_id, step_id, CKPT_WAIT);
	else if (strncasecmp(op, "restart", 2) == 0)
		rc = slurm_checkpoint_restart (job_id, step_id);
	else if (strncasecmp(op, "error", 2) == 0) {
		rc = slurm_checkpoint_error (job_id, step_id, 
			&ckpt_errno, &ckpt_strerror);
		if (rc == SLURM_SUCCESS) {
			printf("error(%u): %s\n", ckpt_errno, ckpt_strerror);
			free(ckpt_strerror);
		}
	}

	else {
		fprintf (stderr, "Invalid checkpoint operation: %s\n", op);
		return 0;
	}

	return rc;
}

/*
 * _suspend - perform some suspend/resume operation
 * IN op - suspend/resume operation
 * IN job_id_str - a job id
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *		error message and returns 0
 */
static int _suspend(char *op, char *job_id_str)
{
	int rc = SLURM_SUCCESS;
	uint32_t job_id = 0;
	char *next_str;

	if (job_id_str) {
		job_id = (uint32_t) strtol (job_id_str, &next_str, 10);
		if (next_str[0] != '\0') {
			fprintf(stderr, "Invalid job id specified\n");
			exit_code = 1;
			return 0;
		}
	} else {
		fprintf(stderr, "Invalid job id specified\n");
		exit_code = 1;
		return 0;
	}

	if (strncasecmp(op, "suspend", 3) == 0)
		rc = slurm_suspend (job_id);
	else
		rc = slurm_resume (job_id);

	return rc;
}
