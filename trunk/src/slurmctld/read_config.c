/*
 * read_config.c - read the overall slurm configuration file
 * see slurm.h for documentation on external functions and data structures
 *
 * NOTE: DEBUG_MODULE mode test with execution line
 *	read_config ../../etc/slurm.conf1
 *	read_config ../../etc/slurm.conf1 1000
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "slurmctld.h"
#include "list.h"

#define BUF_SIZE 1024

int parse_node_spec (char *in_line);
int parse_part_spec (char *in_line);

char *backup_controller = NULL;
char *control_machine = NULL;
int node_record_count = 0;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
#include <sys/resource.h>
int 
main (int argc, char *argv[]) {
	int error_code;
	char bitmap[128];
	char *partitions[] = { "login", "debug", "batch", "class", "end" };
	struct part_record *part_ptr;
	int cycles, i, found;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	struct rusage begin_usage, end_usage;

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	if (argc > 3) {
		printf ("usage: %s <in_file> <cnt>\n", argv[0]);
		exit (1);
	}			

	error_code = init_slurm_conf ();
	if (error_code != 0) {
		printf ("ERROR %d from init_slurm_conf\n", error_code);
		exit (error_code);
	}

	if (argc >= 2)
		error_code = read_slurm_conf (argv[1]);
	else
		error_code = read_slurm_conf (SLURM_CONF);

	if (error_code) {
		printf ("ERROR %d from read_slurm_conf\n", error_code);
		exit (error_code);
	}			

	printf ("ControlMachine=%s\n", control_machine);
	printf ("BackupController=%s\n", backup_controller);
	printf ("\n");

	for (i = 0; i < node_record_count; i++) {
		if (strlen (node_record_table_ptr[i].name) == 0)
			continue;
		printf ("NodeName=%s ", node_record_table_ptr[i].name);
		printf ("NodeState=%s ",
			node_state_string[node_record_table_ptr[i].
					  node_state]);
		printf ("LastResponse=%ld ",
			(long) node_record_table_ptr[i].last_response);

		printf ("CPUs=%d ", node_record_table_ptr[i].cpus);
		printf ("RealMemory=%d ",
			node_record_table_ptr[i].real_memory);
		printf ("TmpDisk=%d ", node_record_table_ptr[i].tmp_disk);
		printf ("Weight=%d ",
			node_record_table_ptr[i].config_ptr->weight);
		printf ("Feature=%s\n",
			node_record_table_ptr[i].config_ptr->feature);
	}			
	(void) bit_fmt (bitmap, sizeof(bitmap), up_node_bitmap);
	printf ("\nup_node_bitmap  = %s\n", bitmap);
	(void) bit_fmt (bitmap, sizeof(bitmap), idle_node_bitmap);
	printf ("idle_node_bitmap= %s\n\n", bitmap);

	printf ("default_part_name=%s\n", default_part_name);
	found = 0;
	for (i = 0;; i++) {
		if (strcmp (partitions[i], "end") == 0) {
			if (found)
				break;
			part_ptr = default_part_loc;
		}
		else {
			part_ptr =
				list_find_first (part_list, &list_find_part,
						 partitions[i]);
			if (part_ptr == default_part_loc)
				found = 1;
		}		
		if (part_ptr == NULL)
			continue;
		printf ("PartitionName=%s ", part_ptr->name);
		printf ("MaxTime=%d ", part_ptr->max_time);
		printf ("MaxNodes=%d ", part_ptr->max_nodes);
		printf ("Key=%d ", part_ptr->key);
		printf ("State=%d ", part_ptr->state_up);
		printf ("Shared=%d ", part_ptr->shared);
		printf ("Nodes=%s ", part_ptr->nodes);
		printf ("AllowGroups=%s  ", part_ptr->allow_groups);
		printf ("total_nodes=%d ", part_ptr->total_nodes);
		printf ("total_cpus=%d ", part_ptr->total_cpus);
		(void)  bit_fmt (bitmap, sizeof(bitmap), part_ptr->node_bitmap);
		printf ("node_bitmap=%s\n", bitmap);
	}			
	if (argc < 3)
		exit (0);

	cycles = atoi (argv[2]);
	printf ("let's reinitialize the database %d times.\n", cycles);
	if (getrusage (RUSAGE_SELF, &begin_usage))
		printf ("ERROR %d from getrusage\n", errno);
	for (i = 0; i < cycles; i++) {
		error_code = init_slurm_conf ();
		if (error_code) {
			printf ("ERROR %d from init_slurm_conf\n",
				error_code);
			exit (error_code);
		}		

		error_code = read_slurm_conf (argv[1]);
		if (error_code) {
			printf ("ERROR %d from read_slurm_conf\n",
				error_code);
			exit (error_code);
		}		
	}			
	if (getrusage (RUSAGE_SELF, &end_usage))
		printf ("error %d from getrusage\n", errno);
	i = (int) (end_usage.ru_maxrss - begin_usage.ru_maxrss);
	if (i > 0) {
	    printf ("ERROR: Change in maximum RSS is %d.\n", i);
	    error_code = ENOMEM;
	}

	exit (error_code);
}
#endif


/* 
 * report_leftover - report any un-parsed (non-whitespace) characters on the
 * configuration input line.
 * input: in_line - what is left of the configuration input line.
 *        line_num - line number of the configuration file.
 * output: none
 */
static void
report_leftover (char *in_line, int line_num)
{
	int bad_index, i;

	bad_index = -1;
	for (i = 0; i < strlen (in_line); i++) {
		if (isspace ((int) in_line[i]) || (in_line[i] == '\n'))
			continue;
		bad_index = i;
		break;
	}

	if (bad_index == -1)
		return;
	error ("report_leftover: ignored input on line %d of configuration: %s",
			line_num, &in_line[bad_index]);
	return;
}


/*
 * build_bitmaps - build node bitmaps to define which nodes are in which 
 *    1) partition  2) configuration record  3) up state  4) idle state
 *    also sets values of total_nodes and total_cpus for every partition.
 * output: return - 0 if no error, errno otherwise
 * global: idle_node_bitmap - bitmap record of idle nodes
 *	up_node_bitmap - bitmap records of up nodes
 *	node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 *	part_list - pointer to global partition list
 */
int 
build_bitmaps () {
	int i, j, error_code, node_count;
	char  *node_list;
	ListIterator config_record_iterator;	/* for iterating through config_record */
	ListIterator part_record_iterator;	/* for iterating through part_record_list */
	struct config_record *config_record_point;	/* pointer to config_record */
	struct part_record *part_record_point;	/* pointer to part_record */
	struct node_record *node_record_point;	/* pointer to node_record */
	bitstr_t *all_part_node_bitmap;

	error_code = 0;
	last_node_update = time (NULL);
	last_part_update = time (NULL);

	/* initialize the idle and up bitmaps */
	if (idle_node_bitmap)
		bit_free (idle_node_bitmap);
	if (up_node_bitmap)
		bit_free (up_node_bitmap);
	idle_node_bitmap = (bitstr_t *) bit_alloc (node_record_count);
	up_node_bitmap   = (bitstr_t *) bit_alloc (node_record_count);
	if ((idle_node_bitmap == NULL) || (up_node_bitmap == NULL))
		fatal ("bit_alloc memory allocation failure");

	/* initialize the configuration bitmaps */
	config_record_iterator = list_iterator_create (config_list);
	if (config_record_iterator == NULL)
		fatal ("build_bitmaps: list_iterator_create unable to allocate memory");
	
	while ((config_record_point =
	       (struct config_record *) list_next (config_record_iterator))) {
		if (config_record_point->node_bitmap)
			bit_free (config_record_point->node_bitmap);

		config_record_point->node_bitmap = (bitstr_t *) bit_alloc (node_record_count);	
		if (config_record_point->node_bitmap == NULL)
			fatal ("bit_alloc memory allocation failure");
	}			
	list_iterator_destroy (config_record_iterator);

	/* scan all nodes and identify which are up and idle and their configuration */
	for (i = 0; i < node_record_count; i++) {
		if (strlen (node_record_table_ptr[i].name) == 0)
			continue;	/* defunct */
		if (node_record_table_ptr[i].node_state == STATE_IDLE)
			bit_set (idle_node_bitmap, i);
		if ((node_record_table_ptr[i].node_state != STATE_DOWN) &&
		    ((node_record_table_ptr[i].node_state & STATE_NO_RESPOND) == 0))
			bit_set (up_node_bitmap, i);
		if (node_record_table_ptr[i].config_ptr)
			bit_set (node_record_table_ptr[i].config_ptr->node_bitmap, i);
	}			

	/* scan partition table and identify nodes in each */
	all_part_node_bitmap = (bitstr_t *) bit_alloc (node_record_count);	
	if (all_part_node_bitmap == NULL)
		fatal ("bit_alloc memory allocation failure");
	part_record_iterator = list_iterator_create (part_list);
	if (part_record_iterator == NULL)
		fatal ("build_bitmaps: list_iterator_create unable to allocate memory");

	while ((part_record_point = 
		(struct part_record *) list_next (part_record_iterator))) {
		if (part_record_point->node_bitmap)
			bit_free (part_record_point->node_bitmap);
		part_record_point->node_bitmap = (bitstr_t *) bit_alloc (node_record_count);	
		if (part_record_point->node_bitmap == NULL)
			fatal ("bit_alloc memory allocation failure");

		/* check for each node in the partition */
		if ((part_record_point->nodes == NULL) ||
		    (strlen (part_record_point->nodes) == 0))
			continue;

		error_code = node_name2list(part_record_point->nodes, 
						&node_list, &node_count);
		if (error_code)
			continue;

		for (i = 0; i < node_count; i++) {
			node_record_point = find_node_record (&node_list[i*MAX_NAME_LEN]);
			if (node_record_point == NULL) {
				error ("build_bitmaps: invalid node name specified %s",
					&node_list[i*MAX_NAME_LEN]);
				continue;
			}	
			j = node_record_point - node_record_table_ptr;
			if (bit_test (all_part_node_bitmap, j) == 1) {
				error ("build_bitmaps: node %s defined in more than one partition",
					 &node_list[i*MAX_NAME_LEN]);
				error ("build_bitmaps: only the first specification is honored");
			}
			else {
				bit_set (part_record_point->node_bitmap, j);
				bit_set (all_part_node_bitmap, j);
				part_record_point->total_nodes++;
				part_record_point->total_cpus +=
					node_record_point->cpus;
				node_record_point->partition_ptr = part_record_point;
			}
		}	
		xfree (node_list);
	}
	list_iterator_destroy (part_record_iterator);
	bit_free (all_part_node_bitmap);
	return error_code;
}


/* 
 * init_slurm_conf - initialize or re-initialize the slurm configuration  
 *	values. this should be called before calling read_slurm_conf.  
 * output: return value - 0 if no error, otherwise an error code
 * globals: control_machine - name of primary slurmctld machine
 *	backup_controller - name of backup slurmctld machine
 */
int
init_slurm_conf () {
	int error_code;

	if (control_machine) {
		xfree (control_machine);
		control_machine = NULL;
	}
	if (backup_controller) {
		xfree (backup_controller);
		backup_controller = NULL;
	}

	if ((error_code = init_node_conf ()))
		return error_code;

	if ((error_code = init_part_conf ()))
		return error_code;

	if ((error_code = init_job_conf ()))
		return error_code;

	if ((error_code = init_step_conf ()))
		return error_code;

	return 0;
}


/* 
 * parse_node_spec - parse the node specification (per the configuration file format), 
 *	build table and set values
 * input:  in_line line from the configuration file
 * output: in_line parsed keywords and values replaced by blanks
 *	return - 0 if no error, error code otherwise
 * global: default_config_record - default configuration values for group of nodes
 *	default_node_record - default node configuration values
 */
int
parse_node_spec (char *in_line) {
	char *node_name, *state, *feature, *node_list;
	int error_code, i, node_count;
	int state_val, cpus_val, real_memory_val, tmp_disk_val, weight_val;
	struct node_record *node_record_point;
	struct config_record *config_point = NULL;

	node_name = state = feature = node_list = (char *) NULL;
	cpus_val = real_memory_val = state_val = NO_VAL;
	tmp_disk_val = weight_val = NO_VAL;
	if ((error_code = load_string (&node_name, "NodeName=", in_line)))
		return error_code;
	if (node_name == NULL)
		return 0;	/* no node info */

	error_code = slurm_parser(in_line,
		"Procs=", 'd', &cpus_val, 
		"Feature=", 's', &feature, 
		"RealMemory=", 'd', &real_memory_val, 
		"State=", 's', &state, 
		"TmpDisk=", 'd', &tmp_disk_val, 
		"Weight=", 'd', &weight_val, 
		"END");

	if (error_code)
		goto cleanup;

	if (state != NULL) {
		state_val = NO_VAL;
		for (i = 0; i <= STATE_END; i++) {
			if (strcmp (node_state_string[i], "END") == 0)
				break;
			if (strcmp (node_state_string[i], state) == 0) {
				state_val = i;
				break;
			}	
		}		
		if (state_val == NO_VAL) {
			error ("parse_node_spec: invalid state %s for node_name %s",
				 state, node_name);
			error_code = EINVAL;
			goto cleanup;
		}	
	}			

	error_code = node_name2list(node_name, &node_list, &node_count);
	if (error_code)
		goto cleanup;

	for (i = 0; i < node_count; i++) {
		if (strcmp (&node_list[i*MAX_NAME_LEN], "DEFAULT") == 0) {
			xfree(node_name);
			node_name = NULL;
			if (cpus_val != NO_VAL)
				default_config_record.cpus = cpus_val;
			if (real_memory_val != NO_VAL)
				default_config_record.real_memory = real_memory_val;
			if (tmp_disk_val != NO_VAL)
				default_config_record.tmp_disk = tmp_disk_val;
			if (weight_val != NO_VAL)
				default_config_record.weight = weight_val;
			if (state_val != NO_VAL)
				default_node_record.node_state = state_val;
			if (feature) {
				if (default_config_record.feature)
					xfree (default_config_record.feature);
				default_config_record.feature = feature;
			}
			break;
		}

		if (i == 0) {
			config_point = create_config_record ();
			if (config_point->nodes)
				free(config_point->nodes);	
			config_point->nodes = node_name;
			if (cpus_val != NO_VAL)
				config_point->cpus = cpus_val;
			if (real_memory_val != NO_VAL)
				config_point->real_memory = real_memory_val;
			if (tmp_disk_val != NO_VAL)
				config_point->tmp_disk = tmp_disk_val;
			if (weight_val != NO_VAL)
				config_point->weight = weight_val;
			if (feature) {
				if (config_point->feature)
					xfree (config_point->feature);
				config_point->feature = feature;
			}	
		}	

		node_record_point = find_node_record (&node_list[i*MAX_NAME_LEN]);
		if (node_record_point == NULL) {
			node_record_point =
				create_node_record (config_point, 
							&node_list[i*MAX_NAME_LEN]);
			if (state_val != NO_VAL)
				node_record_point->node_state = state_val;
		}
		else {
			error ("parse_node_spec: reconfiguration for node %s ignored.",
				&node_list[i*MAX_NAME_LEN]);
		}		
	}

	/* xfree allocated storage */
	if (state)
		xfree(state);
	if (node_list)
		xfree (node_list);
	return error_code;

      cleanup:
	if (node_name)
		xfree(node_name);
	if (feature)
		xfree(feature);
	if (state)
		xfree(state);
	return error_code;
}


/*
 * parse_part_spec - parse the partition specification, build table and set values
 * output: 0 if no error, error code otherwise
 * global: part_list - global partition list pointer
 *	default_part - default parameters for a partition
 */
int 
parse_part_spec (char *in_line) {
	char *allow_groups, *nodes, *partition_name;
	char *default_str, *key_str, *shared_str, *state_str;
	int max_time_val, max_nodes_val, key_val, default_val;
	int state_val, shared_val;
	int error_code;
	struct part_record *part_record_point;

	partition_name = (char *) NULL;
	default_str = shared_str = state_str = (char *) NULL;
	max_time_val = max_nodes_val = key_val = default_val = state_val =
		shared_val = NO_VAL;

	if ((error_code =
	    load_string (&partition_name, "PartitionName=", in_line)))
		return error_code;
	if (partition_name == NULL)
		return 0;	/* no partition info */

	if (strlen (partition_name) >= MAX_NAME_LEN) {
		error ("parse_part_spec: partition name %s too long\n", partition_name);
		xfree (partition_name);
		return EINVAL;
	}			

	allow_groups = default_str = key_str = nodes = NULL;
	shared_str = state_str = NULL;
	error_code = slurm_parser(in_line,
		"AllowGroups=", 's', &allow_groups, 
		"Default=", 's', &default_str, 
		"Key=", 's', &key_str, 
		"MaxTime=", 'd', &max_time_val, 
		"MaxNodes=", 'd', &max_nodes_val, 
		"Nodes=", 's', &nodes, 
		"Shared=", 's', &shared_str, 
		"State=", 's', &state_str, 
		"END");

	if (error_code) 
		goto cleanup;

	if (default_str) {
		if (strcmp(default_str, "YES") == 0)
			default_val = 1;
		else if (strcmp(default_str, "NO") == 0)
			default_val = 0;
		else {
			error ("update_part: ignored partition %s update, bad state %s",
			    partition_name, default_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree (default_str);
		default_str = NULL;
	}

	if (key_str) {
		if (strcmp(key_str, "YES") == 0)
			key_val = 1;
		else if (strcmp(key_str, "NO") == 0)
			key_val = 0;
		else {
			error ("update_part: ignored partition %s update, bad key %s",
			    partition_name, key_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree (key_str);
		key_str = NULL;
	}

	if (shared_str) {
		if (strcmp(shared_str, "YES") == 0)
			shared_val = 1;
		else if (strcmp(shared_str, "NO") == 0)
			shared_val = 0;
		else if (strcmp(shared_str, "FORCE") == 0)
			shared_val = 2;
		else {
			error ("update_part: ignored partition %s update, bad shared %s",
			    partition_name, shared_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree (shared_str);
		shared_str = NULL;
	}

	if (state_str) {
		if (strcmp(state_str, "UP") == 0)
			state_val = 1;
		else if (strcmp(state_str, "DOWN") == 0)
			state_val = 0;
		else {
			error ("update_part: ignored partition %s update, bad state %s",
			    partition_name, state_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree (state_str);
		state_str = NULL;
	}

	if (strcmp (partition_name, "DEFAULT") == 0) {
		xfree (partition_name);
		if (max_time_val != NO_VAL)
			default_part.max_time = max_time_val;
		if (max_nodes_val != NO_VAL)
			default_part.max_nodes = max_nodes_val;
		if (key_val != NO_VAL)
			default_part.key = key_val;
		if (state_val != NO_VAL)
			default_part.state_up = state_val;
		if (shared_val != NO_VAL)
			default_part.shared = shared_val;
		if (allow_groups) {
			if (default_part.allow_groups)
				xfree (default_part.allow_groups);
			default_part.allow_groups = allow_groups;
		}		
		if (nodes) {
			if (default_part.nodes)
				xfree (default_part.nodes);
			default_part.nodes = nodes;
		}		
		return 0;
	}			

	part_record_point = list_find_first (part_list, &list_find_part, partition_name);
	if (part_record_point == NULL) {
		part_record_point = create_part_record ();
		strcpy (part_record_point->name, partition_name);
	}
	else {
		info ("parse_node_spec: duplicate entry for partition %s", partition_name);
	}			
	if (default_val == 1) {
		if (strlen (default_part_name) > 0)
			info ("parse_part_spec: changing default partition from %s to %s",
				 default_part_name, partition_name);
		strcpy (default_part_name, partition_name);
		default_part_loc = part_record_point;
	}			
	if (max_time_val != NO_VAL)
		part_record_point->max_time = max_time_val;
	if (max_nodes_val != NO_VAL)
		part_record_point->max_nodes = max_nodes_val;
	if (key_val != NO_VAL)
		part_record_point->key = key_val;
	if (state_val != NO_VAL)
		part_record_point->state_up = state_val;
	if (shared_val != NO_VAL)
		part_record_point->shared = shared_val;
	if (allow_groups) {
		if (part_record_point->allow_groups)
			xfree (part_record_point->allow_groups);
		part_record_point->allow_groups = allow_groups;
	}			
	if (nodes) {
		if (part_record_point->nodes)
			xfree (part_record_point->nodes);
		part_record_point->nodes = nodes;
	}			
	xfree (partition_name);
	return 0;

      cleanup:
	if (allow_groups)
		xfree(allow_groups);
	if (default_str)
		xfree(default_str);
	if (key_str)
		xfree(key_str);
	if (nodes)
		xfree(nodes);
	if (partition_name)
		xfree(partition_name);
	if (shared_str)
		xfree(shared_str);
	if (state_str)
		xfree(state_str);
	return error_code;
}


/*
 * read_slurm_conf - load the slurm configuration from the specified file. 
 * read_slurm_conf can be called more than once if so desired.
 * input: file_name - name of the file containing overall slurm configuration information
 * output: return - 0 if no error, otherwise an error code
 * global: control_machine - primary machine on which slurmctld runs
 * 	backup_controller - backup machine on which slurmctld runs
 *	default_part_loc - pointer to default partition
 * NOTE: call init_slurm_conf before ever calling read_slurm_conf.  
 */
int 
read_slurm_conf (char *file_name) {
	FILE *slurm_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUF_SIZE];	/* input line */
	int i, j, error_code;

	/* initialization */
	slurm_spec_file = fopen (file_name, "r");
	if (slurm_spec_file == NULL) {
		fatal ("read_slurm_conf error %d opening file %s", errno, file_name);
		return errno;
	}			

	info ("read_slurm_conf: loading configuration from %s", file_name);

	/* process the data file */
	line_num = 0;
	while (fgets (in_line, BUF_SIZE, slurm_spec_file) != NULL) {
		line_num++;
		if (strlen (in_line) >= (BUF_SIZE - 1)) {
			error ("read_slurm_conf line %d, of input file %s too long\n",
				 line_num, file_name);
			fclose (slurm_spec_file);
			return E2BIG;
			break;
		}		

		/* everything after a non-escaped "#" is a comment */
		/* replace comment flag "#" with an end of string (NULL) */
		for (i = 0; i < BUF_SIZE; i++) {
			if (in_line[i] == (char) NULL)
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {	/* escaped "#" */
				for (j = i; j < BUF_SIZE; j++) {
					in_line[j - 1] = in_line[j];
				}	
				continue;
			}	
			in_line[i] = (char) NULL;
			break;
		}		

		/* parse what is left */
		/* overall slurm configuration parameters */
		error_code = slurm_parser(in_line,
			"ControlMachine=", 's', &control_machine, 
			"BackupController=", 's', &backup_controller, 
			"END");
		if (error_code) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		/* node configuration parameters */
		if ((error_code = parse_node_spec (in_line))) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		/* partition configuration parameters */
		if ((error_code = parse_part_spec (in_line))) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		/* report any leftover strings on input line */
		report_leftover (in_line, line_num);
	}			
	fclose (slurm_spec_file);

	/* if values not set in configuration file, set defaults */
	if (backup_controller == NULL)
		info ("read_slurm_conf: backup_controller value not specified.");		

	if (control_machine == NULL) {
		error ("read_slurm_conf: control_machine value not specified.");
		return EINVAL;
	}			

	if (default_part_loc == NULL) {
		error ("read_slurm_conf: default partition not set.");
		return EINVAL;
	}			
	rehash ();
	if ((error_code = build_bitmaps ()))
		return error_code;
	list_sort (config_list, &list_compare_config);

	info ("read_slurm_conf: finished loading configuration");

	return 0;
}
