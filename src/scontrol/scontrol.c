/*
 * scontrol - administration tool for slurm. 
 * provides interface to read, write, update, and configurations.
 */

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
#include <src/common/log.h>
#include <src/common/nodelist.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/xmalloc.h>

#define	BUF_SIZE 1024
#define	max_input_fields 128

static char *command_name;
static int exit_flag;			/* program to terminate if =1 */
static int quiet_flag;			/* quiet=1, verbose=-1, normal=0 */
static int input_words;			/* number of words of input permitted */

void dump_command (int argc, char *argv[]);
int get_command (int *argc, char *argv[]);
void print_build (char *build_param);
void print_job (char * job_id_str);
void print_node (char *node_name, node_info_msg_t *node_info_ptr);
void print_node_list (char *node_list);
void print_part (char *partition_name);
int process_command (int argc, char *argv[]);
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

	if (argc > max_input_fields)	/* bogus input, but let's continue anyway */
		input_words = argc;
	else
		input_words = 128;
	input_fields = (char **) xmalloc (sizeof (char *) * input_words);
	for (i = 1; i < argc; i++) {
		if (strcmp (argv[i], "-q") == 0) {
			quiet_flag = 1;
		}
		else if (strcmp (argv[i], "quiet") == 0) {
			quiet_flag = 1;
		}
		else if (strcmp (argv[i], "-v") == 0) {
			quiet_flag = -1;
		}
		else if (strcmp (argv[i], "verbose") == 0) {
			quiet_flag = -1;
		}
		else {
			input_fields[input_field_count++] = argv[i];
		}		
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
		if (((*argc) + 1) > max_input_fields) {	/* really bogus input line */
			fprintf (stderr, "%s: over %d fields in line: %s\n",
				 command_name, input_words, in_line);
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
 * print_build - print the specified build parameter and value 
 * input: build_param - NULL to print all parameters and values
 */
void 
print_build (char *build_param)
{
	int error_code;
	static slurm_ctl_conf_info_msg_t *old_slurm_ctl_conf_ptr = NULL;
	slurm_ctl_conf_info_msg_t  *slurm_ctl_conf_ptr = NULL;

	if (old_slurm_ctl_conf_ptr) {
		error_code = slurm_load_ctl_conf (old_slurm_ctl_conf_ptr->last_update,
			 &slurm_ctl_conf_ptr);
		if (error_code == 0)
			slurm_free_ctl_conf(old_slurm_ctl_conf_ptr);
		else if (error_code == SLURM_NO_CHANGE_IN_DATA) {
			slurm_ctl_conf_ptr = old_slurm_ctl_conf_ptr;
			error_code = 0;
			if (quiet_flag == -1)
				printf ("slurm_load_build no change in data\n");
		}
	}
	else
		error_code = slurm_load_ctl_conf ((time_t) NULL, &slurm_ctl_conf_ptr);

	if (error_code) {
		if (quiet_flag != 1)
			printf ("slurm_load_build error %d\n", error_code);
		return;
	}
	old_slurm_ctl_conf_ptr = slurm_ctl_conf_ptr;

	if (build_param == NULL ||
	    strcmp (build_param, "BACKUP_INTERVAL") == 0)
		printf ("BACKUP_INTERVAL	= %u\n", 
			slurm_ctl_conf_ptr->backup_interval);
	if (build_param == NULL ||
	    strcmp (build_param, "BACKUP_LOCATION") == 0)
		printf ("BACKUP_LOCATION	= %s\n", 
			slurm_ctl_conf_ptr->backup_location);
	if (build_param == NULL ||
	    strcmp (build_param, "BACKUP_MACHINE") == 0)
		printf ("BACKUP_MACHINE	= %s\n", 
			slurm_ctl_conf_ptr->backup_machine);
	if (build_param == NULL ||
	    strcmp (build_param, "CONTROL_DAEMON") == 0)
		printf ("CONTROL_DAEMON	= %s\n", 
			slurm_ctl_conf_ptr->control_daemon);
	if (build_param == NULL ||
	    strcmp (build_param, "CONTROL_MACHINE") == 0)
		printf ("CONTROL_MACHINE	= %s\n", 
			slurm_ctl_conf_ptr->control_machine);
	if (build_param == NULL ||
	    strcmp (build_param, "CONTROLLER_TIMEOUT") == 0)
		printf ("CONTROLLER_TIMEOUT	= %u\n", 
			slurm_ctl_conf_ptr->controller_timeout);
	if (build_param == NULL ||
	    strcmp (build_param, "EPILOG") == 0)
		printf ("EPILOG  	= %s\n", slurm_ctl_conf_ptr->epilog);
	if (build_param == NULL ||
	    strcmp (build_param, "FAST_SCHEDULE") == 0)
		printf ("FAST_SCHEDULE	= %u\n", 
			slurm_ctl_conf_ptr->fast_schedule);
	if (build_param == NULL ||
	    strcmp (build_param, "HASH_BASE") == 0)
		printf ("HASH_BASE	= %u\n", 
			slurm_ctl_conf_ptr->hash_base);
	if (build_param == NULL ||
	    strcmp (build_param, "HEARTBEAT_INTERVAL") == 0)
		printf ("HEARTBEAT_INTERVAL	= %u\n", 
			slurm_ctl_conf_ptr->heartbeat_interval);
	if (build_param == NULL ||
	    strcmp (build_param, "INIT_PROGRAM") == 0)
		printf ("INIT_PROGRAM	= %s\n", slurm_ctl_conf_ptr->init_program);
	if (build_param == NULL ||
	    strcmp (build_param, "KILL_WAIT") == 0)
		printf ("KILL_WAIT	= %u\n", slurm_ctl_conf_ptr->kill_wait);
	if (build_param == NULL ||
	    strcmp (build_param, "PRIORITIZE") == 0)
		printf ("PRIORITIZE	= %s\n", slurm_ctl_conf_ptr->prioritize);
	if (build_param == NULL ||
	    strcmp (build_param, "PROLOG") == 0)
		printf ("PROLOG  	= %s\n", slurm_ctl_conf_ptr->prolog);
	if (build_param == NULL ||
	    strcmp (build_param, "SERVER_DAEMON") == 0)
		printf ("SERVER_DAEMON	= %s\n", 
			slurm_ctl_conf_ptr->server_daemon);
	if (build_param == NULL ||
	    strcmp (build_param, "SERVER_TIMEOUT") == 0)
		printf ("SERVER_TIMEOUT	= %u\n", 
			slurm_ctl_conf_ptr->server_timeout);
	if (build_param == NULL ||
	    strcmp (build_param, "SLURM_CONF") == 0)
		printf ("SLURM_CONF	= %s\n", slurm_ctl_conf_ptr->slurm_conf);
	if (build_param == NULL ||
	    strcmp (build_param, "TMP_FS") == 0)
		printf ("TMP_FS  	= %s\n", slurm_ctl_conf_ptr->tmp_fs);
}


/*
 * print_job - print the specified job's information
 * input: job_id - job's id or NULL to print information about all jobs
 */
void 
print_job (char * job_id_str) 
{
	int error_code, i;
	uint32_t job_id = 0;
	static job_info_msg_t *old_job_buffer_ptr = NULL;
	job_info_msg_t * job_buffer_ptr = NULL;
	job_table_t *job_ptr = NULL;

	if (old_job_buffer_ptr) {
		error_code = slurm_load_jobs (old_job_buffer_ptr->last_update, 
					&job_buffer_ptr);
		if (error_code == 0)
			slurm_free_job_info (old_job_buffer_ptr);
		else if (error_code == SLURM_NO_CHANGE_IN_DATA) {
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
			printf ("slurm_load_job error %d\n", error_code);
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
		slurm_print_job_table (stdout, & job_ptr[i] ) ;
		if (job_id_str)
			break;
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
	int i, j;
	static int last_inx = 0;

	for (j = 0; j < node_buffer_ptr->record_count; j++) {
		if (node_name) {
			i = (j + last_inx) % node_buffer_ptr->record_count;
			if (strcmp (node_name, node_buffer_ptr->node_array[i].name) != 0)
				continue;
		}
		else
			i = j;
		slurm_print_node_table (stdout, & node_buffer_ptr->node_array[i]);

		if (node_name) {
			last_inx = i;
			break;
		}
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

	int start_inx, end_inx, count_inx, error_code, i;
	char *str_ptr1, *str_ptr2, *format, *my_node_list;
	char this_node_name[BUF_SIZE];

	if (old_node_info_ptr) {
		error_code = slurm_load_node (old_node_info_ptr->last_update, 
			&node_info_ptr);
		if (error_code == 0)
			slurm_free_node_info (old_node_info_ptr);
		else if (error_code == SLURM_NO_CHANGE_IN_DATA) {
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
			printf ("load_node error %d\n", error_code);
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
		format = NULL;
		my_node_list = xmalloc (strlen (node_list) + 1);
		if (my_node_list == NULL) {
			if (quiet_flag != 1)
				fprintf (stderr,
					 "unable to allocate memory\n");
			abort ();
		}		

		strcpy (my_node_list, node_list);
		str_ptr2 = (char *) strtok_r (my_node_list, ",", &str_ptr1);
		while (str_ptr2) {	/* break apart by comma separators */
			error_code =
				parse_node_names (str_ptr2, &format,
						 &start_inx, &end_inx,
						 &count_inx);
			if (error_code) {
				if (quiet_flag != 1)
					fprintf (stderr,
						 "invalid node name specification: %s\n",
						 str_ptr2);
				break;
			}	
			if (strlen (format) >= sizeof (this_node_name)) {
				if (quiet_flag != 1)
					fprintf (stderr,
						 "invalid node name specification: %s\n",
						 format);
				xfree (format);
				break;
			}	
			for (i = start_inx; i <= end_inx; i++) {
				if (count_inx == 0)
					strncpy (this_node_name, format,
						 sizeof (this_node_name));
				else
					sprintf (this_node_name, format, i);
				print_node (this_node_name, node_info_ptr);
			}
			if (format)
				xfree (format);
			str_ptr2 = (char *) strtok_r (NULL, ",", &str_ptr1);
		}		
		xfree (my_node_list);
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
	int error_code, i;
	static partition_info_msg_t *old_part_info_ptr = NULL;
	partition_info_msg_t *part_info_ptr = NULL;
	partition_table_msg_t *part_ptr = NULL;

	if (old_part_info_ptr) {
		error_code = slurm_load_partitions (old_part_info_ptr->last_update, 
					&part_info_ptr);
		if (error_code == 0) {
			slurm_free_partition_info (old_part_info_ptr);
		}
		else if (error_code == SLURM_NO_CHANGE_IN_DATA) {
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
			printf ("slurm_load_part error %d\n", error_code);
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
		slurm_print_partition_table (stdout, & part_ptr[i] ) ;
		if (partition_name)
			break;
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

	if ((strcmp (argv[0], "exit") == 0) ||
	    (strcmp (argv[0], "quit") == 0)) {
		if (argc > 1)
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		exit_flag = 1;

	}
	else if (strcmp (argv[0], "help") == 0) {
		if (argc > 1)
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		usage ();

	}
	else if (strcmp (argv[0], "quiet") == 0) {
		if (argc > 1)
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		quiet_flag = 1;

	}
	else if (strncmp (argv[0], "reconfigure", 7) == 0) {
		if (argc > 2)
			fprintf (stderr,
				 "too many arguments for keyword:%s\n",
				 argv[0]);
		error_code = slurm_reconfigure ();
		if ((error_code != 0) && (quiet_flag != 1))
			fprintf (stderr, "error %d from reconfigure\n",
				 error_code);

	}
	else if (strcmp (argv[0], "show") == 0) {
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
		else if (strncmp (argv[1], "build", 3) == 0) {
			if (argc > 2)
				print_build (argv[2]);
			else
				print_build (NULL);
		}
		else if (strncmp (argv[1], "jobs", 3) == 0) {
			if (argc > 2)
				print_job (argv[2]);
			else
				print_job (NULL);
		}
		else if (strncmp (argv[1], "nodes", 3) == 0) {
			if (argc > 2)
				print_node_list (argv[2]);
			else
				print_node_list (NULL);
		}
		else if (strncmp (argv[1], "partitions", 3) == 0) {
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
	else if (strcmp (argv[0], "update") == 0) {
		if (argc < 2) {
			fprintf (stderr, "too few arguments for %s keyword\n",
				 argv[0]);
			return 0;
		}		
		update_it ((argc - 1), &argv[1]);

	}
	else if (strcmp (argv[0], "verbose") == 0) {
		if (argc > 1) {
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		quiet_flag = -1;

	}
	else if (strcmp (argv[0], "version") == 0) {
		if (argc > 1) {
			fprintf (stderr,
				 "too many arguments for %s keyword\n",
				 argv[0]);
		}		
		printf ("%s version 0.1\n", command_name);

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
		if (strncmp (argv[i], "NodeName=", 9) == 0) {
			error_code = update_node (argc, argv);
			break;
		}
		else if (strncmp (argv[i], "PartitionName=", 14) == 0) {
			error_code = update_part (argc, argv);
			break;
		}
		else if (strncmp (argv[i], "JobId=", 6) == 0) {
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
		printf("errorno=%d\n",error_code);
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
	printf("Not yet implemented\n");
	return EINVAL;
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
		if (strncmp(argv[i], "NodeName=", 9) == 0)
			node_msg.node_names = &argv[i][9];
		else if (strncmp(argv[i], "State=", 6) == 0) {
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
				if (strcmp (node_state_string(j), &argv[i][6]) == 0) {
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

	error_code = slurm_update_node(&node_msg);
	return error_code;
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
	part_msg.name		= NULL;
	part_msg.max_time	= (uint32_t) NO_VAL;
	part_msg.max_nodes	= (uint32_t) NO_VAL;
	part_msg.default_part	= (uint16_t) NO_VAL;
	part_msg.key		= (uint16_t) NO_VAL;
	part_msg.shared		= (uint16_t) NO_VAL;
	part_msg.state_up	= (uint16_t) NO_VAL;
	part_msg.nodes		= NULL;
	part_msg.allow_groups	= NULL;
	for (i=0; i<argc; i++) {
		if (strncmp(argv[i], "PartitionName=", 14) == 0)
			part_msg.name = &argv[i][14];
		else if (strncmp(argv[i], "MaxTime=", 8) == 0) {
			if (strcmp(&argv[i][8],"INFINITE") == 0)
				part_msg.max_time = INFINITE;
			else
				part_msg.max_time = (uint32_t) strtol(&argv[i][8], (char **) NULL, 10);
		}
		else if (strncmp(argv[i], "MaxNodes=", 9) == 0)
			if (strcmp(&argv[i][9],"INFINITE") == 0)
				part_msg.max_nodes = INFINITE;
			else
				part_msg.max_nodes = (uint32_t) strtol(&argv[i][9], (char **) NULL, 10);
		else if (strncmp(argv[i], "Default=", 8) == 0) {
			if (strcmp(&argv[i][8], "NO") == 0)
				part_msg.default_part = 0;
			else if (strcmp(&argv[i][8], "YES") == 0)
				part_msg.default_part = 1;
			else {
				fprintf (stderr, "Invalid input: %s\n", argv[i]);
				fprintf (stderr, "Acceptable Default values are YES and NO\n");
				return EINVAL;
			}
		}
		else if (strncmp(argv[i], "Key=", 4) == 0) {
			if (strcmp(&argv[i][4], "NO") == 0)
				part_msg.key = 0;
			else if (strcmp(&argv[i][4], "YES") == 0)
				part_msg.key = 1;
			else {
				fprintf (stderr, "Invalid input: %s\n", argv[i]);
				fprintf (stderr, "Acceptable Key values are YES and NO\n");
				return EINVAL;
			}
		}
		else if (strncmp(argv[i], "Shared=", 7) == 0) {
			if (strcmp(&argv[i][7], "NO") == 0)
				part_msg.shared = SHARED_NO;
			else if (strcmp(&argv[i][7], "YES") == 0)
				part_msg.shared = SHARED_YES;
			else if (strcmp(&argv[i][7], "FORCE") == 0)
				part_msg.shared = SHARED_FORCE;
			else {
				fprintf (stderr, "Invalid input: %s\n", argv[i]);
				fprintf (stderr, "Acceptable Shared values are YES, NO and FORCE\n");
				return EINVAL;
			}
		}
		else if (strncmp(argv[i], "State=", 6) == 0) {
			if (strcmp(&argv[i][6], "DOWN") == 0)
				part_msg.state_up = 0;
			else if (strcmp(&argv[i][6], "UP") == 0)
				part_msg.state_up = 1;
			else {
				fprintf (stderr, "Invalid input: %s\n", argv[i]);
				fprintf (stderr, "Acceptable State values are UP and DOWN\n");
				return EINVAL;
			}
		}
		else if (strncmp(argv[i], "Nodes=", 6) == 0)
			part_msg.nodes = &argv[i][6];
		else if (strncmp(argv[i], "AllowGroups=", 12) == 0)
			part_msg.allow_groups = &argv[i][12];
		else {
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			return EINVAL;
		}
	}

	error_code = slurm_update_partition(&part_msg);
	return error_code;
}

/* usage - show the valid scontrol commands */
void
usage () {
	printf ("%s [-q | -v] [<keyword>]\n", command_name);
	printf ("  -q is equivalent to the keyword \"quiet\" described below.\n");
	printf ("  -v is equivalent to the keyword \"verbose\" described below.\n");
	printf ("  <keyword> may be omitted from the execute line and %s will execute in interactive\n",
		command_name);
	printf ("    mode to process multiple keywords (i.e. commands). valid <entity> values are:\n");
	printf ("    build, job, node, and partition. node names may be sepcified using regular simple \n");
	printf ("    expressions. valid <keyword> values are:\n");
	printf ("     exit                     terminate this command.\n");
	printf ("     help                     print this description of use.\n");
	printf ("     quiet                    print no messages other than error messages.\n");
	printf ("     quit                     terminate this command.\n");
	printf ("     reconfigure              re-read configuration files.\n");
	printf ("     show <entity> [<id>]     display state of identified entity, default is all records.\n");
	printf ("     update <options>         update configuration per configuration file format.\n");
	printf ("     verbose                  enable detailed logging.\n");
	printf ("     version                  display tool version number.\n");
}
