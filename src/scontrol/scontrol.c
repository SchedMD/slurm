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

#define	BUF_SIZE 1024
#define	max_input_fields 128

static char *command_name;
static int exit_flag;			/* program to terminate if =1 */
static int quiet_flag;			/* quiet=1, verbose=-1, normal=0 */
static int input_words;		/* number of words of input permitted */

void dump_command (int argc, char *argv[]);
int get_command (int *argc, char *argv[]);
void print_build (char *build_param);
void print_job (char * job_id_str);
void print_node (char *node_name, struct node_buffer *node_buffer_ptr);
void print_node_list (char *node_list);
void print_part (char *partition_name);
int process_command (int argc, char *argv[]);
int update_it (int argc, char *argv[]);
void usage ();

int 
main (int argc, char *argv[]) 
{
	int error_code, i, input_field_count;
	char **input_fields;

	command_name = argv[0];
	exit_flag = 0;
	input_field_count = 0;
	quiet_flag = 0;
	if (argc > max_input_fields)	/* bogus input, but let's continue anyway */
		input_words = argc;
	else
		input_words = 128;
	input_fields = (char **) malloc (sizeof (char *) * input_words);
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
		in_line = (char *) malloc (in_line_size);
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
		temp_char = getc (stdin);
		if (temp_char == EOF)
			break;
		if (temp_char == (int) '\n')
			break;
		if ((in_line_pos + 2) >= in_line_size) {
			in_line_size += BUF_SIZE;
			in_line = (char *) realloc (in_line, in_line_size);
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
	static struct build_buffer *old_build_buffer_ptr = NULL;
	struct build_buffer *build_buffer_ptr = NULL;
	struct build_table  *build_table_ptr = NULL;

	if (old_build_buffer_ptr) {
		error_code = slurm_load_build (old_build_buffer_ptr->last_update,
			 &build_buffer_ptr);
		if (error_code == 0)
			slurm_free_build_info(old_build_buffer_ptr);
		else if (error_code == -1)
			build_buffer_ptr = old_build_buffer_ptr;
	}
	else
		error_code = slurm_load_build ((time_t) NULL, &build_buffer_ptr);
	if (error_code > 0) {
		if (quiet_flag != 1)
			printf ("slurm_load_build error %d\n", error_code);
		return;
	}
	old_build_buffer_ptr = build_buffer_ptr;

	build_table_ptr = build_buffer_ptr->build_table_ptr;
	if (build_param == NULL ||
	    strcmp (build_param, "BACKUP_INTERVAL") == 0)
		printf ("BACKUP_INTERVAL	= %u\n", 
			build_table_ptr->backup_interval);
	if (build_param == NULL ||
	    strcmp (build_param, "BACKUP_LOCATION") == 0)
		printf ("BACKUP_LOCATION	= %s\n", 
			build_table_ptr->backup_location);
	if (build_param == NULL ||
	    strcmp (build_param, "BACKUP_MACHINE") == 0)
		printf ("BACKUP_MACHINE	= %s\n", 
			build_table_ptr->backup_machine);
	if (build_param == NULL ||
	    strcmp (build_param, "CONTROL_DAEMON") == 0)
		printf ("CONTROL_DAEMON	= %s\n", 
			build_table_ptr->control_daemon);
	if (build_param == NULL ||
	    strcmp (build_param, "CONTROL_MACHINE") == 0)
		printf ("CONTROL_MACHINE	= %s\n", 
			build_table_ptr->control_machine);
	if (build_param == NULL ||
	    strcmp (build_param, "EPILOG") == 0)
		printf ("EPILOG  	= %s\n", build_table_ptr->epilog);
	if (build_param == NULL ||
	    strcmp (build_param, "FAST_SCHEDULE") == 0)
		printf ("FAST_SCHEDULE	= %u\n", 
			build_table_ptr->fast_schedule);
	if (build_param == NULL ||
	    strcmp (build_param, "HASH_BASE") == 0)
		printf ("HASH_BASE	= %u\n", 
			build_table_ptr->hash_base);
	if (build_param == NULL ||
	    strcmp (build_param, "HEARTBEAT_INTERVAL") == 0)
		printf ("HEARTBEAT_INTERVAL	= %u\n", 
			build_table_ptr->heartbeat_interval);
	if (build_param == NULL ||
	    strcmp (build_param, "INIT_PROGRAM") == 0)
		printf ("INIT_PROGRAM	= %s\n", build_table_ptr->init_program);
	if (build_param == NULL ||
	    strcmp (build_param, "KILL_WAIT") == 0)
		printf ("KILL_WAIT	= %u\n", build_table_ptr->kill_wait);
	if (build_param == NULL ||
	    strcmp (build_param, "PRIORITIZE") == 0)
		printf ("PRIORITIZE	= %s\n", build_table_ptr->prioritize);
	if (build_param == NULL ||
	    strcmp (build_param, "PROLOG") == 0)
		printf ("PROLOG  	= %s\n", build_table_ptr->prolog);
	if (build_param == NULL ||
	    strcmp (build_param, "SERVER_DAEMON") == 0)
		printf ("SERVER_DAEMON	= %s\n", 
			build_table_ptr->server_daemon);
	if (build_param == NULL ||
	    strcmp (build_param, "SERVER_TIMEOUT") == 0)
		printf ("SERVER_TIMEOUT	= %u\n", 
			build_table_ptr->server_timeout);
	if (build_param == NULL ||
	    strcmp (build_param, "SLURM_CONF") == 0)
		printf ("SLURM_CONF	= %s\n", build_table_ptr->slurm_conf);
	if (build_param == NULL ||
	    strcmp (build_param, "TMP_FS") == 0)
		printf ("TMP_FS  	= %s\n", build_table_ptr->tmp_fs);
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
	static struct job_buffer *old_job_buffer_ptr = NULL;
	struct job_buffer *job_buffer_ptr = NULL;
	struct job_table *job_ptr = NULL;

	if (old_job_buffer_ptr) {
		error_code = slurm_load_job (old_job_buffer_ptr->last_update, 
					&job_buffer_ptr);
		if (error_code == 0)
			slurm_free_job_info (old_job_buffer_ptr);
		else if (error_code == -1)
			job_buffer_ptr = old_job_buffer_ptr;
	}
	else
		error_code = slurm_load_job ((time_t) NULL, &job_buffer_ptr);
	if (error_code > 0) {
		if (quiet_flag != 1)
			printf ("slurm_load_job error %d\n", error_code);
		return;
	}
	else if (error_code == 0)
		old_job_buffer_ptr = job_buffer_ptr;
printf("time=%lu\n",(long)old_job_buffer_ptr->last_update);

	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", (long) job_buffer_ptr->last_update);

	if (job_id_str)
		job_id = (uint32_t) strtol (job_id_str, (char **)NULL, 10);

	job_ptr = job_buffer_ptr->job_table_ptr;
	for (i = 0; i < job_buffer_ptr->job_count; i++) {
		if (job_id_str && 
		    job_id != job_ptr[i].job_id) 
			continue;

		printf ("JobId=%u UserId=%u ", 
			job_ptr[i].job_id, job_ptr[i].user_id);
		printf ("JobState=%u TimeLimit=%u ", 
			job_ptr[i].job_state, job_ptr[i].time_limit);
		printf ("Priority=%u Partition=%s\n", 
			job_ptr[i].priority, job_ptr[i].partition);

		printf ("   Name=%s NodeList=%s ", 
			job_ptr[i].name, job_ptr[i].nodes);
		printf ("StartTime=%x EndTime=%x\n", 
			(uint32_t) job_ptr[i].start_time, 
			(uint32_t) job_ptr[i].end_time);

		printf ("   ReqProcs=%u ReqNodes=%u ",
			job_ptr[i].num_procs, job_ptr[i].num_nodes);
		printf ("Shared=%u Contiguous=%u ",
			job_ptr[i].shared, job_ptr[i].contiguous);
		printf ("MinProcs=%u MinMemory=%u ",
			job_ptr[i].min_procs, job_ptr[i].min_memory);
		printf ("MinTmpDisk=%u\n",
			job_ptr[i].min_tmp_disk);

		printf ("   ReqNodeList=%s Features=%s ",
			job_ptr[i].req_nodes, job_ptr[i].features);
		printf ("JobScript=%s\n\n",
			job_ptr[i].job_script);

		if (job_id_str)
			break;
	}
}


/*
 * print_node - print the specified node's information
 * input: node_name - NULL to print all node information
 *	  node_ptr - pointer to node table of information
 * NOTE: call this only after executing load_node, called from print_node_list
 */
void
print_node (char *node_name, struct node_buffer *node_buffer_ptr) 
{
	int i, j;
	static int last_inx = 0;
	struct node_table *node_ptr;

	node_ptr = node_buffer_ptr->node_table_ptr;
	for (j = 0; j < node_buffer_ptr->node_count; j++) {
		if (node_name) {
			i = (j + last_inx) % node_buffer_ptr->node_count;
			if (strcmp (node_name, node_ptr[i].name) != 0)
				continue;
		}
		else
			i = j;
		printf ("NodeName=%s CPUs=%u ", 
			node_ptr[i].name, node_ptr[i].cpus);
		printf ("RealMemory=%u TmpDisk=%u ", 
			node_ptr[i].real_memory, node_ptr[i].tmp_disk);
		printf ("State=%u Weight=%u ", 
			node_ptr[i].node_state, node_ptr[i].weight);
		printf ("Features=%s Partition=%s\n", 
			node_ptr[i].features, node_ptr[i].partition);
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
	static struct node_buffer *old_node_buffer_ptr = NULL;
	struct node_buffer *node_buffer_ptr = NULL;

	int start_inx, end_inx, count_inx, error_code, i;
	char *str_ptr1, *str_ptr2, *format, *my_node_list;
	char this_node_name[BUF_SIZE];

	if (old_node_buffer_ptr) {
		error_code = slurm_load_node (old_node_buffer_ptr->last_update, 
			&node_buffer_ptr);
		if (error_code == 0)
			slurm_free_node_info (old_node_buffer_ptr);
		else if (error_code == -1)
			node_buffer_ptr = old_node_buffer_ptr;

	}
	else
		error_code = slurm_load_node ((time_t) NULL, &node_buffer_ptr);
	if (error_code > 0) {
		if (quiet_flag != 1)
			printf ("load_node error %d\n", error_code);
		return;
	}
	else if (error_code == 0)
		old_node_buffer_ptr = node_buffer_ptr;

	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", (long) node_buffer_ptr->last_update);

	if (node_list == NULL) {
		print_node (NULL, node_buffer_ptr);
	}
	else {
		format = NULL;
		my_node_list = malloc (strlen (node_list) + 1);
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
				free (format);
				break;
			}	
			for (i = start_inx; i <= end_inx; i++) {
				if (count_inx == 0)
					strncpy (this_node_name, format,
						 sizeof (this_node_name));
				else
					sprintf (this_node_name, format, i);
				print_node (this_node_name, node_buffer_ptr);
			}
			if (format)
				free (format);
			str_ptr2 = (char *) strtok_r (NULL, ",", &str_ptr1);
		}		
		free (my_node_list);
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
	static struct part_buffer *old_part_buffer_ptr = NULL;
	struct part_buffer *part_buffer_ptr = NULL;
	struct part_table *part_ptr = NULL;

	if (old_part_buffer_ptr) {
		error_code = slurm_load_part (old_part_buffer_ptr->last_update, 
					&part_buffer_ptr);
		if (error_code == 0)
			slurm_free_part_info (old_part_buffer_ptr);
		else if (error_code == -1)
			part_buffer_ptr = old_part_buffer_ptr;
	}
	else
		error_code = slurm_load_part ((time_t) NULL, &part_buffer_ptr);
	if (error_code > 0) {
		if (quiet_flag != 1)
			printf ("slurm_load_part error %d\n", error_code);
		return;
	}
	else if (error_code == 0)
		old_part_buffer_ptr = part_buffer_ptr;

	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", (long) part_buffer_ptr->last_update);

	part_ptr = part_buffer_ptr->part_table_ptr;
	for (i = 0; i < part_buffer_ptr->part_count; i++) {
		if (partition_name && 
		    strcmp (partition_name, part_ptr[i].name) != 0)
			continue;
		printf ("PartitionName=%s MaxTime=%u ", 
			part_ptr[i].name, part_ptr[i].max_time);
		printf ("MaxNodes=%u TotalNodes=%u ", 
			part_ptr[i].max_nodes, part_ptr[i].total_nodes);
		printf ("TotalCPUs=%u Key=%u\n", 
			part_ptr[i].total_cpus, part_ptr[i].key);
		printf ("   Default=%u ", 
			part_ptr[i].default_part);
		printf ("Shared=%u StateUp=%u ", 
			part_ptr[i].shared, part_ptr[i].state_up);
		printf ("Nodes=%s AllowGroups=%s\n\n", 
			part_ptr[i].nodes, part_ptr[i].allow_groups);
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
		error_code = reconfigure ();
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
	char *in_line;
	int error_code, i, in_line_size;

	in_line_size = BUF_SIZE;
	in_line = (char *) malloc (in_line_size);
	if (in_line == NULL) {
		fprintf (stderr, "%s: error %d allocating memory\n",
			 command_name, errno);
		return ENOMEM;
	}			
	strcpy (in_line, "");

	for (i = 0; i < argc; i++) {
		if ((strlen (in_line) + strlen (argv[i]) + 2) > in_line_size) {
			in_line_size += BUF_SIZE;
			in_line = (char *) realloc (in_line, in_line_size);
			if (in_line == NULL) {
				fprintf (stderr,
					 "%s: error %d allocating memory\n",
					 command_name, errno);
				return ENOMEM;
			}	
		}		

		strcat (in_line, argv[i]);
		strcat (in_line, " ");
	}			

	error_code = update_config (in_line);
	free (in_line);
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
