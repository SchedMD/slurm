/*
 * scontrol - administration tool for slurm. 
 * provides interface to read, write, update, and configurations.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "slurmlib.h"

#define	BUF_SIZE 1024
#define	max_input_fields 128

static char *command_name;
static int exit_flag;			/* program to terminate if =1 */
static int quiet_flag;			/* quiet=1, verbose=-1, normal=0 */
static int input_words;		/* number of words of input permitted */

void dump_command (int argc, char *argv[]);
int get_command (int *argc, char *argv[]);
void print_build (char *build_param);
void print_node (char *node_name);
void print_node_list (char *node_list);
void print_part (char *partition_name);
int process_command (int argc, char *argv[]);
int update_it (int argc, char *argv[]);
void usage ();

main (int argc, char *argv[]) {
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
		}		/* else */
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
dump_command (int argc, char *argv[]) {
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
int get_command (int *argc, char **argv) {
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
void print_build (char *build_param) {
	char req_name[BUILD_SIZE], next_name[BUILD_SIZE], value[BUILD_SIZE];
	int error_code;

	error_code = slurm_load_build ();
	if (error_code) {
		if (quiet_flag != 1)
			printf ("slurm_load_build error %d\n", error_code);
		return;
	}			


	if (build_param)
		strncpy (req_name, build_param, BUILD_SIZE);
	else
		strcpy (req_name, "");	/* start at beginning of node list */

	while (1) {
		error_code = slurm_load_build_name (req_name, next_name, value);
		if (error_code != 0) {
			if (quiet_flag != 1) {
				if (error_code == ENOENT)
					printf ("no parameter %s found\n",
						req_name);
				else
					printf ("error %d finding value for parameter %s\n", 
						error_code, req_name);
			}	
			break;
		}		
		printf ("%s=%s\n", req_name, value);

		if (build_param || (strlen (next_name) == 0))
			break;
		strcpy (req_name, next_name);
	}			
/*  slurm_free_build_info();		keep data for reuse, cleaned on exit */
}

/*
 * print_node - print the specified node's information
 * input: node_name - NULL to print all node information
 * NOTE: call this only after executing load_node, called from print_node_list
 */
void
print_node (char *node_name) {
	int error_code, size, i;
	char partition[MAX_NAME_LEN], node_state[MAX_NAME_LEN],
		features[FEATURE_SIZE];
	char req_name[MAX_NAME_LEN];	/* name of the partition */
	char next_name[MAX_NAME_LEN];	/* name of the next partition */
	int cpus, real_memory, tmp_disk, weight;
	char *dump;
	int dump_size;
	time_t update_time;
	unsigned *node_bitmap;	/* bitmap of nodes in partition */
	int bitmap_size;	/* bytes in node_bitmap */

	if (node_name)
		strncpy (req_name, node_name, MAX_NAME_LEN);
	else
		strcpy (req_name, "");	/* start at beginning of node list */

	while (1) {
		error_code =
			load_node_config (req_name, next_name, &cpus,
					  &real_memory, &tmp_disk, &weight,
					  features, partition, node_state);
		if (error_code != 0) {
			if (quiet_flag != 1) {
				if (error_code == ENOENT)
					printf ("no node %s found\n",
						req_name);
				else
					printf ("error %d finding information for node %s\n", error_code, req_name);
			}	
			break;
		}		
		printf ("NodeName=%s CPUs=%d RealMemory=%d TmpDisk=%d ",
			req_name, cpus, real_memory, tmp_disk);
		printf ("State=%s Weight=%d Features=%s Partition=%s\n",
			node_state, weight, features, partition);

		if (node_name || (strlen (next_name) == 0))
			break;
		strcpy (req_name, next_name);
	}			
}


/*
 * print_node_list - print information about the supplied node list (or regular expression)
 * input: node_list - print information about the supplied node list (or regular expression)
 */
void
print_node_list (char *node_list) {
	static time_t last_update_time = (time_t) NULL;
	int start_inx, end_inx, count_inx, error_code, i;
	char *str_ptr1, *str_ptr2, *format, *my_node_list,
		this_node_name[BUF_SIZE];;

	error_code = load_node (&last_update_time);
	if (error_code) {
		if (quiet_flag != 1)
			printf ("load_node error %d\n", error_code);
		return;
	}			
	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", (long) last_update_time);

	if (node_list == NULL) {
		print_node (NULL);
	}
	else {
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
				parse_node_name (str_ptr2, &format,
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
				print_node (this_node_name);
			}	
			free (format);
			str_ptr2 = (char *) strtok_r (NULL, ",", &str_ptr1);
		}		
		free (my_node_list);
	}			/* else */

/*  free_node_info();		keep data for reuse, cleaned on exit */
	return;
}


/*
 * print_part - print the specified partition's information
 * input: partition_name - NULL to print all partition information
 */
void print_part (char *partition_name) {
	static time_t last_update_time = (time_t) NULL;	/* time desired for data */
	static char *yes_no[0] = { "NO", "YES" };
	static char *up_down[0] = { "DOWN", "UP" };
	char req_name[MAX_NAME_LEN];	/* name of the partition */
	char next_name[MAX_NAME_LEN];	/* name of the next partition */
	int max_time;			/* -1 if unlimited */
	int max_nodes;			/* -1 if unlimited */
	int total_nodes;		/* total number of nodes in the partition */
	int total_cpus;			/* total number of cpus in the partition */
	char nodes[FEATURE_SIZE];	/* names of nodes in partition */
	char allow_groups[FEATURE_SIZE];/* NULL indicates all */
	int key;			/* 1 if slurm distributed key is required */
	int state_up;			/* 1 if state is up */
	int shared;			/* 1 if partition can be shared */
	int default_flag;		/* 1 if default partition */
	int error_code;

	error_code = load_part (&last_update_time);
	if (error_code) {
		if (quiet_flag != 1)
			printf ("load_part error %d\n", error_code);
		return;
	}			
	if (quiet_flag == -1)
		printf ("last_update_time=%ld\n", (long) last_update_time);

	if (partition_name)
		strncpy (req_name, partition_name, MAX_NAME_LEN);
	else
		strcpy (req_name, "");	/* start at beginning of partition list */

	while (1) {
		error_code =
			load_part_name (req_name, next_name, &max_time,
					&max_nodes, &total_nodes, &total_cpus,
					&key, &state_up, &shared, &default_flag,
					nodes, allow_groups);
		if (error_code != 0) {
			if (quiet_flag != 1) {
				if (error_code == ENOENT)
					printf ("no partition %s found\n",
						req_name);
				else
					printf ("error %d finding information for partition %s\n", error_code, req_name);
			}	
			break;
		}		

		printf ("PartitionName=%s Nodes=%s  MaxTime=%d  MaxNodes=%d Default=%s ", 
			req_name, nodes, max_time, max_nodes, yes_no[default_flag]);
		printf ("Key=%s State=%s Shared=%s AllowGroups=%s ", 
			yes_no[key], up_down[state_up], yes_no[shared], allow_groups);
		printf ("TotalNodes=%d total_cpus=%d \n", total_nodes, total_cpus);

		if (partition_name || (strlen (next_name) == 0))
			break;
		strcpy (req_name, next_name);
	}			
/*  free_part_info(); 	keep data for reuse, cleaned on exit */
}


/*
 * process_command - process the user's command
 * input: argc - count of arguments
 *        argv - the arguments
 * ourput: return code is 0 or errno (only for errors fatal to scontrol)
 */
int
process_command (int argc, char *argv[]) {
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
			if (quiet_flag != 1)
				printf ("keyword:%s entity:%s command not yet implemented\n", argv[0], argv[1]);
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
update_it (int argc, char *argv[]) {
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
	printf ("  <keyword> may be omitted from the execute line and %s will execute in interactive\n");
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
}				/* usage */
