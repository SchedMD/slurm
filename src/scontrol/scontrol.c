/*****************************************************************************\
 * scontrol - administration tool for slurm. 
 * provides interface to read, write, update, and configurations.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else  /* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif  /* HAVE_INTTYPES_H */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <src/api/slurm.h>
#include <src/common/hostlist.h>
#include <src/common/log.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/xmalloc.h>

#define	BUF_SIZE 1024
#define	MAX_INPUT_FIELDS 128

static char *command_name;
static int exit_flag;			/* program to terminate if =1 */
static int quiet_flag;			/* quiet=1, verbose=-1, normal=0 */
static int input_words;			/* number of words of input permitted */

void dump_command (int argc, char *argv[]);
int get_command (int *argc, char *argv[]);
void print_config (char *config_param);
void print_job (char * job_id_str);
void print_node (char *node_name, node_info_msg_t *node_info_ptr);
void print_node_list (char *node_list);
void print_part (char *partition_name);
int process_command (int argc, char *argv[]);
int strcmp_i (const char *s1, const char *s2);
int strncmp_i (const char *s1, const char *s2, int len);
int update_it (int argc, char *argv[]);
int update_job (int argc, char *argv[]);
int update_node (int argc, char *argv[]);
int update_part (int argc, char *argv[]);
void usage ();

int 
main (int argc, char *argv[]) 
{
	int error_code, i, input_field_count;
	char **input_fields;
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	command_name = argv[0];
	exit_flag = 0;
	input_field_count = 0;
	quiet_flag = 0;
	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);

	if (argc > MAX_INPUT_FIELDS)	/* bogus input, but let's continue anyway */
		input_words = argc;
	else
		input_words = 128;
	input_fields = (char **) xmalloc (sizeof (char *) * input_words);
	for (i = 1; i < argc; i++) {
		if (strncmp (argv[i], "-help", 2) == 0)
			usage ();
		else if (strcmp (argv[i], "-q") == 0)
			quiet_flag = 1;
		else if (strcmp (argv[i], "quiet") == 0)
			quiet_flag = 1;
		else if (strcmp (argv[i], "-v") == 0)
			quiet_flag = -1;
		else if (strcmp (argv[i], "verbose") == 0)
			quiet_flag = -1;
		else
			input_fields[input_field_count++] = argv[i];
	}			

	if (input_field_count)
		exit_flag = 1;
	else
		error_code = get_command (&input_field_count, input_fields);

	while (1) {
#if DEBUG_MODULE
		dump_command (input_field_count, input_fields);
#endif
		error_code =
			process_command (input_field_count, input_fields);
		if (error_code != 0)
			break;
		if (exit_flag == 1)
			break;
		error_code = get_command (&input_field_count, input_fields);
		if (error_code != 0)
			break;
	}			

	exit (error_code);
}


/*
 * dump_command - dump the user's command
 * input: argc - count of arguments
 *        argv - the arguments
 */
void
dump_command (int argc, char *argv[]) 
{
	int i;

	for (i = 0; i < argc; i++) {
		printf ("arg %d:%s:\n", i, argv[i]);
	}			
}


/*
 * get_command - get a command from the user
 * input: argc - location to store count of arguments
 *        argv - location to store the argument list
 * output: returns error code, 0 if no problems
 */
int 
get_command (int *argc, char **argv) 
{
	static char *in_line;
	static int in_line_size = 0;
	int in_line_pos = 0;
	int temp_char, i;

	if (in_line_size == 0) {
		in_line_size += BUF_SIZE;
		in_line = (char *) xmalloc (in_line_size);
		if (in_line == NULL) {
			fprintf (stderr, "%s: error %d allocating memory\n",
				 command_name, errno);
			in_line_size = 0;
			return ENOMEM;
		}		
	}			

	printf ("scontrol: ");
	*argc = 0;
	in_line_pos = 0;

	while (1) {
		temp_char = getchar();
		if (temp_char == EOF)
			break;
		if (temp_char == (int) '\n')
			break;
		if ((in_line_pos + 2) >= in_line_size) {
			in_line_size += BUF_SIZE;
			xrealloc (in_line, in_line_size);
			if (in_line == NULL) {
				fprintf (stderr,
					 "%s: error %d allocating memory\n",
					 command_name, errno);
				in_line_size = 0;
				return ENOMEM;
			}	
		}		
		in_line[in_line_pos++] = (char) temp_char;
	}			
	in_line[in_line_pos] = (char) NULL;

	for (i = 0; i < in_line_pos; i++) {
		if (isspace ((int) in_line[i]))
			continue;
		if (((*argc) + 1) > MAX_INPUT_FIELDS) {	/* really bogus input line */
			fprintf (stderr, "%s: can not process over %d words as configured\n",
				 command_name, input_words);
			return E2BIG;
		}		
		argv[(*argc)++] = &in_line[i];
		for (i++; i < in_line_pos; i++) {
			if (!isspace ((int) in_line[i]))
				continue;
			in_line[i] = (char) NULL;
			break;
		}		
	}			
	return 0;
}


/* 
 * print_config - print the specified configuration parameter and value 
 * input: config_param - NULL to print all parameters and values
 */
void 
print_config (char *config_param)
{
	int error_code;
	static slurm_ctl_conf_info_msg_t *old_slurm_ctl_conf_ptr = NULL;
	slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;

	if (old_slurm_ctl_conf_ptr) {
		error_code = slurm_load_ctl_conf (old_slurm_ctl_conf_ptr->last_update,
			 &slurm_ctl_conf_ptr);
		if (error_code == 0)
			slurm_free_ctl_conf(old_slurm_ctl_conf_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			slurm_ctl_conf_ptr = old_slurm_ctl_conf_ptr;
			error_code = 0;
			if (quiet_flag == -1)
				printf ("slurm_load_ctl_conf no change in data\n");
		}
	}
	else
		error_code = slurm_load_ctl_conf ((time_t) NULL, &slurm_ctl_conf_ptr);

	if (error_code) {
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_ctl_conf error");
		return;
	}
	old_slurm_ctl_conf_ptr = slurm_ctl_conf_ptr;

	slurm_print_ctl_conf (stdout, slurm_ctl_conf_ptr) ;
}


/*
 * print_job - print the specified job's information
 * input: job_id - job's id or NULL to print information about all jobs
 */
void 
print_job (char * job_id_str) 
{
	int error_code, i, print_cnt = 0;
	uint32_t job_id = 0;
	static job_info_msg_t *old_job_buffer_ptr = NULL;
	job_info_msg_t * job_buffer_ptr = NULL;
	job_table_t *job_ptr = NULL;

	if (old_job_buffer_ptr) {
		error_code = slurm_load_jobs (old_job_buffer_ptr->last_update, 
					&job_buffer_ptr);
		if (error_code == 0)
			slurm_free_job_info (old_job_buffer_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			job_buffer_ptr = old_job_buffer_ptr;
			error_code = 0;
			if (quiet_flag == -1)
 				printf ("slurm_free_job_info no change in data\n");
		}
	}
	else
		error_code = slurm_load_jobs ((time_t) NULL, &job_buffer_ptr);
	if (error_code) {
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_jobs error");
		return;
	}
	else if (error_code == 0)
		old_job_buffer_ptr = job_buffer_ptr;
	
	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", (long) job_buffer_ptr->last_update);

	if (job_id_str)
		job_id = (uint32_t) strtol (job_id_str, (char **)NULL, 10);

	job_ptr = job_buffer_ptr->job_array ;
	for (i = 0; i < job_buffer_ptr->record_count; i++) {
		if (job_id_str && job_id != job_ptr[i].job_id) 
			continue;
		print_cnt++;
		slurm_print_job_table (stdout, & job_ptr[i] ) ;
		if (job_id_str)
			break;
	}

	if ((print_cnt == 0) && (quiet_flag != 1)) {
		if (job_buffer_ptr->record_count)
			printf ("Job %u not found\n", job_id);
		else
			printf ("No jobs in the system\n");
	}
}


/*
 * print_node - print the specified node's information
 * input: node_name - NULL to print all node information
 *	  node_ptr - pointer to node table of information
 * NOTE: call this only after executing load_node, called from print_node_list
 * NOTE: To avoid linear searches, we remember the location of the last name match
 */
void
print_node (char *node_name, node_info_msg_t  * node_buffer_ptr) 
{
	int i, j, print_cnt = 0;
	static int last_inx = 0;

	for (j = 0; j < node_buffer_ptr->record_count; j++) {
		if (node_name) {
			i = (j + last_inx) % node_buffer_ptr->record_count;
			if (strcmp (node_name, node_buffer_ptr->node_array[i].name) != 0)
				continue;
		}
		else
			i = j;
		print_cnt++;
		slurm_print_node_table (stdout, & node_buffer_ptr->node_array[i]);

		if (node_name) {
			last_inx = i;
			break;
		}
	}

	if ((print_cnt == 0) && (quiet_flag != 1)) {
		if (node_buffer_ptr->record_count)
			printf ("Node %s not found\n", node_name);
		else
			printf ("No nodes in the system\n");
	}
}


/*
 * print_node_list - print information about the supplied node list (or regular expression)
 * input: node_list - print information about the supplied node list (or regular expression)
 */
void
print_node_list (char *node_list) 
{
	static node_info_msg_t *old_node_info_ptr = NULL;
	node_info_msg_t *node_info_ptr = NULL;
	hostlist_t host_list;
	int error_code;
	char *this_node_name;

	if (old_node_info_ptr) {
		error_code = slurm_load_node (old_node_info_ptr->last_update, 
			&node_info_ptr);
		if (error_code == 0)
			slurm_free_node_info (old_node_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			node_info_ptr = old_node_info_ptr;
			error_code = 0;
			if (quiet_flag == -1)
				printf ("slurm_free_job_info no change in data\n");
		}

	}
	else
		error_code = slurm_load_node ((time_t) NULL, &node_info_ptr);
	if (error_code) {
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_node error");
		return;
	}
	else if (error_code == 0)
		old_node_info_ptr = node_info_ptr;

	if (quiet_flag == -1)
		printf ("last_update_time=%ld, records=%d\n", 
			(long) node_info_ptr->last_update, node_info_ptr->record_count);

	if (node_list == NULL) {
		print_node (NULL, node_info_ptr);
	}
	else {
		if ( (host_list = hostlist_create (node_list)) ) {
			while ( (this_node_name = hostlist_shift (host_list)) )
				print_node (this_node_name, node_info_ptr);

			hostlist_destroy (host_list);
		}
		else if (quiet_flag != 1) {
			if (errno == EINVAL)
				fprintf (stderr, "unable to parse node list %s\n", node_list);
			else if (errno == ERANGE)
				fprintf (stderr, "too many nodes in supplied range %s\n", node_list);
			else
				perror ("error parsing node list");
		}
	}			
	return;
}


/*
 * print_part - print the specified partition's information
 * input: partition_name - NULL to print information about all partition 
 */
void 
print_part (char *partition_name) 
{
	int error_code, i, print_cnt = 0;
	static partition_info_msg_t *old_part_info_ptr = NULL;
	partition_info_msg_t *part_info_ptr = NULL;
	partition_table_t *part_ptr = NULL;

	if (old_part_info_ptr) {
		error_code = slurm_load_partitions (old_part_info_ptr->last_update, 
					&part_info_ptr);
		if (error_code == 0) {
			slurm_free_partition_info (old_part_info_ptr);
		}
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			part_info_ptr = old_part_info_ptr;
			error_code = 0;
			if (quiet_flag == -1)
				printf ("slurm_load_part no change in data\n");
		}
	}
	else
		error_code = slurm_load_partitions ((time_t) NULL, &part_info_ptr);
	if (error_code > 0) {
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_partitions error");
		return;
	}
	else
		old_part_info_ptr = part_info_ptr;

	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", (long) part_info_ptr->last_update);

	part_ptr = part_info_ptr->partition_array;
	for (i = 0; i < part_info_ptr->record_count; i++) {
		if (partition_name && 
		    strcmp (partition_name, part_ptr[i].name) != 0)
			continue;
		print_cnt++;
		slurm_print_partition_table (stdout, & part_ptr[i] ) ;
		if (partition_name)
			break;
	}

	if ((print_cnt == 0) && (quiet_flag != 1)) {
		if (part_info_ptr->record_count)
			printf ("Partition %s not found\n", partition_name);
		else
			printf ("No partitions in the system\n");
	}
}


/*
 * process_command - process the user's command
 * input: argc - count of arguments
 *        argv - the arguments
 * ourput: return code is 0 or errno (only for errors fatal to scontrol)
 */
int
process_command (int argc, char *argv[]) 
{
	int error_code;

	if ((strcmp_i (argv[0], "exit") == 0) ||
	    (strcmp_i (argv[0], "quit") == 0)) {
		if (argc > 1)
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		exit_flag = 1;

	}
	else if (strcmp_i (argv[0], "help") == 0) {
		if (argc > 1)
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		usage ();

	}
	else if (strcmp_i (argv[0], "quiet") == 0) {
		if (argc > 1)
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		quiet_flag = 1;

	}
	else if (strncmp_i (argv[0], "reconfigure", 7) == 0) {
		if (argc > 2)
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		error_code = slurm_reconfigure ();
		if ((error_code != 0) && (quiet_flag != 1))
			fprintf (stderr, "error %d from reconfigure\n",
				 error_code);

	}
	else if (strcmp_i (argv[0], "show") == 0) {
		if (argc > 3) {
			if (quiet_flag != 1)
				fprintf (stderr,
					 "too many arguments for keyword:%s\n",
					 argv[0]);
		}
		else if (argc < 2) {
			if (quiet_flag != 1)
				fprintf (stderr,
					 "too few arguments for keyword:%s\n",
					 argv[0]);
		}
		else if (strncmp_i (argv[1], "config", 3) == 0) {
			if (argc > 2)
				print_config (argv[2]);
			else
				print_config (NULL);
		}
		else if (strncmp_i (argv[1], "jobs", 3) == 0) {
			if (argc > 2)
				print_job (argv[2]);
			else
				print_job (NULL);
		}
		else if (strncmp_i (argv[1], "nodes", 3) == 0) {
			if (argc > 2)
				print_node_list (argv[2]);
			else
				print_node_list (NULL);
		}
		else if (strncmp_i (argv[1], "partitions", 3) == 0) {
			if (argc > 2)
				print_part (argv[2]);
			else
				print_part (NULL);
		}
		else if (quiet_flag != 1) {
			fprintf (stderr,
				 "invalid entity:%s for keyword:%s \n",
				 argv[1], argv[0]);
		}		

	}
	else if (strcmp_i (argv[0], "update") == 0) {
		if (argc < 2) {
			fprintf (stderr, "too few arguments for %s keyword\n",
				 argv[0]);
			return 0;
		}		
		update_it ((argc - 1), &argv[1]);

	}
	else if (strcmp_i (argv[0], "verbose") == 0) {
		if (argc > 1) {
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		quiet_flag = -1;

	}
	else if (strcmp_i (argv[0], "version") == 0) {
		if (argc > 1) {
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		printf ("%s Version %s\n", command_name, VERSION);

	}
	else
		fprintf (stderr, "invalid keyword: %s\n", argv[0]);

	return 0;
}


/* 
 * update_it - update the slurm configuration per the supplied arguments 
 * input: argc - count of arguments
 *        argv - list of arguments
 * output: returns 0 if no error, errno otherwise
 */
int
update_it (int argc, char *argv[]) 
{
	int error_code, i;

	error_code = 0;
	/* First identify the entity to update */
	for (i=0; i<argc; i++) {
		if (strncmp_i (argv[i], "NodeName=", 9) == 0) {
			error_code = update_node (argc, argv);
			break;
		}
		else if (strncmp_i (argv[i], "PartitionName=", 14) == 0) {
			error_code = update_part (argc, argv);
			break;
		}
		else if (strncmp_i (argv[i], "JobId=", 6) == 0) {
			error_code = update_job (argc, argv);
			break;
		}
	}
	
	if (i >= argc) {	
		printf("No valid entity in update command\n");
		printf("Input line must include \"NodeName\", \"PartitionName\", or \"JobId\"\n");
		error_code = EINVAL;
	}
	else if (error_code) {
		printf ("slurm_update error %d %s\n", 
			errno, slurm_strerror(errno));
	}
	return error_code;
}

/* 
 * update_job - update the slurm job configuration per the supplied arguments 
 * input: argc - count of arguments
 *        argv - list of arguments
 * output: returns 0 if no error, errno otherwise
 */
int
update_job (int argc, char *argv[]) 
{
	int error_code, i;
	job_desc_msg_t job_msg;

	error_code = 0;
	slurm_init_job_desc_msg (&job_msg);	

	for (i=0; i<argc; i++) {
		if (strncmp_i(argv[i], "JobId=", 6) == 0)
			job_msg.job_id = (uint32_t) strtol(&argv[i][6], (char **) NULL, 10);
		else if (strncmp_i(argv[i], "TimeLimit=", 10) == 0)
			job_msg.time_limit = (uint32_t) strtol(&argv[i][10], (char **) NULL, 10);
		else if (strncmp_i(argv[i], "Priority=", 9) == 0)
			job_msg.priority = (uint32_t) strtol(&argv[i][9], (char **) NULL, 10);
		else if (strncmp_i(argv[i], "ReqProcs=", 9) == 0)
			job_msg.num_procs = (uint32_t) strtol(&argv[i][9], (char **) NULL, 10);
		else if (strncmp_i(argv[i], "ReqNodes=", 9) == 0)
			job_msg.num_nodes = (uint32_t) strtol(&argv[i][9], (char **) NULL, 10);
		else if (strncmp_i(argv[i], "MinProcs=", 9) == 0)
			job_msg.min_procs = (uint32_t) strtol(&argv[i][9], (char **) NULL, 10);
		else if (strncmp_i(argv[i], "MinMemory=", 10) == 0)
			job_msg.min_memory = (uint32_t) strtol(&argv[i][10], (char **) NULL, 10);
		else if (strncmp_i(argv[i], "MinTmpDisk=", 11) == 0)
			job_msg.min_tmp_disk = (uint32_t) strtol(&argv[i][11], (char **) NULL, 10);
		else if (strncmp_i(argv[i], "Partition=", 10) == 0)
			job_msg.partition = &argv[i][10];
		else if (strncmp_i(argv[i], "Name=", 5) == 0)
			job_msg.name = &argv[i][5];
		else if (strncmp_i(argv[i], "Shared=", 7) == 0) {
			if (strcmp_i(&argv[i][7], "YES") == 0)
				job_msg.shared = 1;
			else if (strcmp_i(&argv[i][7], "NO") == 0)
				job_msg.shared = 0;
			else
				job_msg.shared = (uint16_t) strtol(&argv[i][7], (char **) NULL, 10);
		}
		else if (strncmp_i(argv[i], "Contiguous=", 11) == 0) {
			if (strcmp_i(&argv[i][11], "YES") == 0)
				job_msg.contiguous = 1;
			else if (strcmp_i(&argv[i][11], "NO") == 0)
				job_msg.contiguous = 0;
			else
				job_msg.contiguous = (uint16_t) strtol(&argv[i][11], (char **) NULL, 10);
		}
		else if (strncmp_i(argv[i], "ReqNodeList=", 12) == 0)
			job_msg.req_nodes = &argv[i][12];
		else if (strncmp_i(argv[i], "Features=", 9) == 0)
			job_msg.features = &argv[i][9];
		else {
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return EINVAL;
		}
	}

	if (slurm_update_job(&job_msg))
		return slurm_get_errno ();
	else
		return 0;
}

/* 
 * update_node - update the slurm node configuration per the supplied arguments 
 * input: argc - count of arguments
 *        argv - list of arguments
 * output: returns 0 if no error, errno otherwise
 */
int
update_node (int argc, char *argv[]) 
{
	int error_code, i, j, k;
	uint16_t state_val;
	update_node_msg_t node_msg;

	error_code = 0;
	node_msg.node_names = NULL;
	node_msg.node_state = (uint16_t) NO_VAL;
	for (i=0; i<argc; i++) {
		if (strncmp_i(argv[i], "NodeName=", 9) == 0)
			node_msg.node_names = &argv[i][9];
		else if (strncmp_i(argv[i], "State=", 6) == 0) {
			state_val = (uint16_t) NO_VAL;
			for (j = 0; j <= NODE_STATE_END; j++) {
				if (strcmp (node_state_string(j), "END") == 0) {
					fprintf (stderr, "Invalid input: %s\n", argv[i]);
					fprintf (stderr, "Request aborted\n Valid states are:");
					for (k = 0; k <= NODE_STATE_END; k++) {
						fprintf (stderr, "%s ", node_state_string(k));
					}
					fprintf (stderr, "\n");
					return EINVAL;
				}
				if (strcmp_i (node_state_string(j), &argv[i][6]) == 0) {
					state_val = (uint16_t) j;
					break;
				}
			}	
			node_msg.node_state = state_val;
		}
		else {
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return EINVAL;
		}
	}

	if (slurm_update_node(&node_msg))
		return slurm_get_errno ();
	else
		return 0;
}

/* 
 * update_part - update the slurm partition configuration per the supplied arguments 
 * input: argc - count of arguments
 *        argv - list of arguments
 * output: returns 0 if no error, errno otherwise
 */
int
update_part (int argc, char *argv[]) 
{
	int error_code, i;
	update_part_msg_t part_msg;

	error_code = 0;
	slurm_init_part_desc_msg ( &part_msg );
	for (i=0; i<argc; i++) {
		if (strncmp_i(argv[i], "PartitionName=", 14) == 0)
			part_msg.name = &argv[i][14];
		else if (strncmp_i(argv[i], "MaxTime=", 8) == 0) {
			if (strcmp_i(&argv[i][8],"INFINITE") == 0)
				part_msg.max_time = INFINITE;
			else
				part_msg.max_time = (uint32_t) strtol(&argv[i][8], (char **) NULL, 10);
		}
		else if (strncmp_i(argv[i], "MaxNodes=", 9) == 0)
			if (strcmp_i(&argv[i][9],"INFINITE") == 0)
				part_msg.max_nodes = INFINITE;
			else
				part_msg.max_nodes = (uint32_t) strtol(&argv[i][9], (char **) NULL, 10);
		else if (strncmp_i(argv[i], "Default=", 8) == 0) {
			if (strcmp_i(&argv[i][8], "NO") == 0)
				part_msg.default_part = 0;
			else if (strcmp_i(&argv[i][8], "YES") == 0)
				part_msg.default_part = 1;
			else {
				fprintf (stderr, "Invalid input: %s\n", argv[i]);
				fprintf (stderr, "Acceptable Default values are YES and NO\n");
				return EINVAL;
			}
		}
		else if (strncmp_i(argv[i], "Key=", 4) == 0) {
			if (strcmp_i(&argv[i][4], "NO") == 0)
				part_msg.key = 0;
			else if (strcmp_i(&argv[i][4], "YES") == 0)
				part_msg.key = 1;
			else {
				fprintf (stderr, "Invalid input: %s\n", argv[i]);
				fprintf (stderr, "Acceptable Key values are YES and NO\n");
				return EINVAL;
			}
		}
		else if (strncmp_i(argv[i], "Shared=", 7) == 0) {
			if (strcmp_i(&argv[i][7], "NO") == 0)
				part_msg.shared = SHARED_NO;
			else if (strcmp_i(&argv[i][7], "YES") == 0)
				part_msg.shared = SHARED_YES;
			else if (strcmp_i(&argv[i][7], "FORCE") == 0)
				part_msg.shared = SHARED_FORCE;
			else {
				fprintf (stderr, "Invalid input: %s\n", argv[i]);
				fprintf (stderr, "Acceptable Shared values are YES, NO and FORCE\n");
				return EINVAL;
			}
		}
		else if (strncmp_i(argv[i], "State=", 6) == 0) {
			if (strcmp_i(&argv[i][6], "DOWN") == 0)
				part_msg.state_up = 0;
			else if (strcmp_i(&argv[i][6], "UP") == 0)
				part_msg.state_up = 1;
			else {
				fprintf (stderr, "Invalid input: %s\n", argv[i]);
				fprintf (stderr, "Acceptable State values are UP and DOWN\n");
				return EINVAL;
			}
		}
		else if (strncmp_i(argv[i], "Nodes=", 6) == 0)
			part_msg.nodes = &argv[i][6];
		else if (strncmp_i(argv[i], "AllowGroups=", 12) == 0)
			part_msg.allow_groups = &argv[i][12];
		else {
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return EINVAL;
		}
	}

	if (slurm_update_partition(&part_msg))
		return slurm_get_errno ();
	else
		return 0;
}

/* usage - show the valid scontrol commands */
void
usage () {
	printf ("scontrol [-q | -v] [<COMMAND>]\n");
	printf ("  -q is equivalent to the keyword \"quiet\" described below.\n");
	printf ("  -v is equivalent to the keyword \"verbose\" described below.\n");
	printf ("  <keyword> may be omitted from the execute line and scontrol will execute in interactive\n");
	printf ("    mode. It will process commands as entered until explicitly terminated.\n");
	printf ("    Valid <COMMAND> values are:\n");
	printf ("     exit                     terminate this command.\n");
	printf ("     help                     print this description of use.\n");
	printf ("     quiet                    print no messages other than error messages.\n");
	printf ("     quit                     terminate this command.\n");
	printf ("     reconfigure              re-read configuration files.\n");
	printf ("     show <ENTITY> [<ID>]     display state of identified entity, default is all records.\n");
	printf ("     update <SPECIFICATIONS>   update job, node, or partition configuration.\n");
	printf ("     verbose                  enable detailed logging.\n");
	printf ("     version                  display tool version number.\n");
	printf ("  <ENTITY> may be \"config\", \"job\", \"node\", or \"partition\".\n");
	printf ("  <ID> may be a configuration parametername , job id, node name or partition name.\n");
	printf ("     Node names mayspecified using simple regular expressions, (e.g. \"lx[10-20]\").\n");
	printf ("  <SPECIFICATIONS> are specified in the same format as the configuration file. You may\n");
	printf ("     wish to use the \"show\" keyword then use its output as input for the update keyword,\n");
	printf ("     editing as needed.\n");
	printf ("  All commands and options are case-insensitive, although node names and partition\n");
	printf ("     names tests are case-sensitive (node names \"LX\" and \"lx\" are distinct).\n");
}

/* strcmp_i - case insensitive version of strcmp */
int 
strcmp_i (const char *s1, const char *s2)
{
	int i;
	int c1, c2;

	for (i=0; ; i++) {
		if (isupper(s1[i]))
			c1 = tolower(s1[i]);
		else
			c1 = s1[i];

		if (isupper(s2[i]))
			c2 = tolower(s2[i]);
		else
			c2 = s2[i];

		if (c1 == c2) {
			if (c1 == '\0')
				return 0;
			continue;
		}
		else
			return 1;
	}
}

/* strncmp_i - case insensitive version of strncmp */
int 
strncmp_i (const char *s1, const char *s2, int len)
{
	int i;
	int c1, c2;

	for (i=0; i<len; i++) {
		if (isupper(s1[i]))
			c1 = tolower(s1[i]);
		else
			c1 = s1[i];

		if (isupper(s2[i]))
			c2 = tolower(s2[i]);
		else
			c2 = s2[i];

		if (c1 == c2) {
			if (c1 == '\0')
				return 0;
			continue;
		}
		else
			return 1;
	}
	return 0;
}
