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

#include "slurm.h"
#include "list.h"

#define BUF_SIZE 1024
#define NO_VAL (-99)

int parse_node_spec (char *in_line);
int parse_part_spec (char *in_line);

char *backup_controller = NULL;
char *control_machine = NULL;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main (int argc, char *argv[]) {
	int error_code, start_inx, end_inx, count_inx;
	char out_line[BUF_SIZE];
	char *format, *node_name, *bitmap;
	char *partitions[] = { "login", "debug", "batch", "class", "end" };
	struct part_record *part_ptr;
	int cycles, i, found;

	if (argc > 3) {
		printf ("usage: %s <in_file> <cnt>\n", argv[0]);
		exit (1);
	}			

	error_code = init_slurm_conf ();
	if (error_code != 0)
		exit (error_code);

	if (argc >= 2)
		error_code = read_slurm_conf (argv[1]);
	else
		error_code = read_slurm_conf (SLURM_CONF);

	if (error_code) {
		printf ("error %d from read_slurm_conf\n", error_code);
		exit (1);
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
	bitmap = bitmap_print (up_node_bitmap);
	printf ("\nup_node_bitmap  =%s\n", bitmap);
	free (bitmap);
	bitmap = bitmap_print (idle_node_bitmap);
	printf ("idle_node_bitmap=%s\n\n", bitmap);
	free (bitmap);

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
		}		/* else */
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
		bitmap = bitmap_print (part_ptr->node_bitmap);
		printf ("node_bitmap=%s\n", bitmap);
		if (bitmap)
			free (bitmap);
	}			
	if (argc < 3)
		exit (0);

	cycles = atoi (argv[2]);
	printf ("let's reinitialize the database %d times. run /bin/ps to get memory size.\n", cycles);
	sleep (5);
	for (i = 0; i < cycles; i++) {
		error_code = init_slurm_conf ();
		if (error_code) {
			printf ("error %d from init_slurm_conf\n",
				error_code);
			exit (error_code);
		}		

		error_code = read_slurm_conf (argv[1]);
		if (error_code) {
			printf ("error %d from read_slurm_conf\n",
				error_code);
			exit (error_code);
		}		
	}			
	printf ("all done. run /bin/ps again look for increase in memory size (leakage).\n");
	sleep (10);

	exit (0);
}
#endif


/*
 * build_bitmaps - build node bitmaps to define which nodes are in which 
 *    1) partition  2) configuration record  3) up state  4) idle state
 *    also sets values of total_nodes and total_cpus for every partition.
 * output: returns 0 if no error, errno otherwise
 */
int build_bitmaps () {
	int i, j, size, error_code;
	ListIterator config_record_iterator;	/* for iterating through config_record */
	ListIterator part_record_iterator;	/* for iterating through part_record_list */
	struct config_record *config_record_point;	/* pointer to config_record */
	struct part_record *part_record_point;	/* pointer to part_record */
	struct node_record *node_record_point;	/* pointer to node_record */
	unsigned *all_part_node_bitmap;
	char *format, this_node_name[BUF_SIZE];
	int start_inx, end_inx, count_inx;
	char *my_node_list, *str_ptr1, *str_ptr2;

	error_code = 0;
	last_node_update = time (NULL);
	last_part_update = time (NULL);
	size = (node_record_count + (sizeof (unsigned) * 8) - 1) / (sizeof (unsigned) * 8);	
				/* unsigned int records in bitmap */
	size *= 8;		/* bytes in bitmap */

	/* initialize the idle and up bitmaps */
	if (idle_node_bitmap)
		free (idle_node_bitmap);
	if (up_node_bitmap)
		free (up_node_bitmap);
	idle_node_bitmap = (unsigned *) malloc (size);
	up_node_bitmap = (unsigned *) malloc (size);
	if ((idle_node_bitmap == NULL) || (up_node_bitmap == NULL)) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "build_bitmaps: unable to allocate memory\n");
#else
		syslog (log_alert,
			"build_bitmaps: unable to allocate memory\n");
#endif
		abort ();
	}			
	memset (idle_node_bitmap, 0, size);
	memset (up_node_bitmap, 0, size);

	/* initialize the configuration bitmaps */
	config_record_iterator = list_iterator_create (config_list);
	if (config_record_iterator == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "build_bitmaps: list_iterator_create unable to allocate memory\n");
#else
		syslog (log_alert,
			"build_bitmaps: list_iterator_create unable to allocate memory\n");
#endif
		abort ();
	}			
	while (config_record_point =
	       (struct config_record *) list_next (config_record_iterator)) {
		if (config_record_point->node_bitmap)
			free (config_record_point->node_bitmap);
		config_record_point->node_bitmap = (unsigned *) malloc (size);
		if (config_record_point->node_bitmap == NULL) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "build_bitmaps: unable to allocate memory\n");
#else
			syslog (log_alert,
				"build_bitmaps: unable to allocate memory\n");
#endif
			abort ();
		}		
		memset (config_record_point->node_bitmap, 0, size);
	}			
	list_iterator_destroy (config_record_iterator);

	/* scan all nodes and identify which are up and idle and their configuration */
	for (i = 0; i < node_record_count; i++) {
		if (strlen (node_record_table_ptr[i].name) == 0)
			continue;	/* defunct */
		if (node_record_table_ptr[i].node_state == STATE_IDLE)
			bitmap_set (idle_node_bitmap, i);
		if (node_record_table_ptr[i].node_state > STATE_DOWN)
			bitmap_set (up_node_bitmap, i);
		if (node_record_table_ptr[i].config_ptr)
			bitmap_set (node_record_table_ptr[i].config_ptr->
				    node_bitmap, i);
	}			

	/* scan partition table and identify nodes in each */
	all_part_node_bitmap = (unsigned *) malloc (size);
	if (all_part_node_bitmap == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "build_bitmaps: unable to allocate memory\n");
#else
		syslog (log_alert,
			"build_bitmaps: unable to allocate memory\n");
#endif
		abort ();
	}			
	memset (all_part_node_bitmap, 0, size);
	part_record_iterator = list_iterator_create (part_list);
	if (part_record_iterator == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "build_bitmaps: list_iterator_create unable to allocate memory\n");
#else
		syslog (log_alert,
			"build_bitmaps: list_iterator_create unable to allocate memory\n");
#endif
		abort ();
	}			
	while (part_record_point =
	       (struct part_record *) list_next (part_record_iterator)) {
		if (part_record_point->node_bitmap)
			free (part_record_point->node_bitmap);
		part_record_point->node_bitmap = (unsigned *) malloc (size);
		if (part_record_point->node_bitmap == NULL) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "build_bitmaps: unable to allocate memory\n");
#else
			syslog (log_alert,
				"build_bitmaps: unable to allocate memory\n");
#endif
			abort ();
		}		
		memset (part_record_point->node_bitmap, 0, size);

		/* check for each node in the partition */
		if ((part_record_point->nodes == NULL) ||
		    (strlen (part_record_point->nodes) == 0))
			continue;
		my_node_list =
			(char *) malloc (strlen (part_record_point->nodes) +
					 1);
		if (my_node_list == NULL) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "build_bitmaps: unable to allocate memory\n");
#else
			syslog (log_alert,
				"build_bitmaps: unable to allocate memory\n");
#endif
			abort ();
		}		
		strcpy (my_node_list, part_record_point->nodes);
		str_ptr2 = (char *) strtok_r (my_node_list, ",", &str_ptr1);
		while (str_ptr2) {	/* break apart by comma separators */
			error_code =
				parse_node_name (str_ptr2, &format,
						 &start_inx, &end_inx,
						 &count_inx);
			if (error_code)
				continue;
			if (strlen (format) >= sizeof (this_node_name)) {
#if DEBUG_SYSTEM
				fprintf (stderr,
					 "build_bitmaps: node name specification too long: %s\n",
					 format);
#else
				syslog (log_err,
					"build_bitmaps: node name specification too long: %s\n",
					format);
#endif
				free (format);
				continue;
			}	
			for (i = start_inx; i <= end_inx; i++) {
				if (count_inx == 0)
					strncpy (this_node_name, format,
						 sizeof (this_node_name));
				else
					sprintf (this_node_name, format, i);
				node_record_point =
					find_node_record (this_node_name);
				if (node_record_point == NULL) {
#if DEBUG_SYSTEM
					fprintf (stderr,
						 "build_bitmaps: invalid node specified %s\n",
						 this_node_name);
#else
					syslog (log_err,
						"build_bitmaps: invalid node specified %s\n",
						this_node_name);
#endif
					continue;
				}	
				j = node_record_point - node_record_table_ptr;
				if (bitmap_value (all_part_node_bitmap, j) ==
				    1) {
#if DEBUG_SYSTEM
					fprintf (stderr,
						 "build_bitmaps: node %s defined in more than one partition\n",
						 this_node_name);
					fprintf (stderr,
						 "build_bitmaps: only the first partition's specification is honored\n");
#else
					syslog (log_err,
						"build_bitmaps: node %s defined in more than one partition\n",
						this_node_name);
					syslog (log_err,
						"build_bitmaps: only the first partition's specification is honored\n");
#endif
				}
				else {
					bitmap_set (part_record_point->
						    node_bitmap, j);
					bitmap_set (all_part_node_bitmap, j);
					part_record_point->total_nodes++;
					part_record_point->total_cpus +=
						node_record_point->cpus;
					node_record_point->partition_ptr =
						part_record_point;
				}	/* else */
			}	
			free (format);
			str_ptr2 = (char *) strtok_r (NULL, ",", &str_ptr1);
		}		/* while (str_ptr2 */
		free (my_node_list);
	}			/* while (part_record_point */
	list_iterator_destroy (part_record_iterator);
	free (all_part_node_bitmap);
	return error_code;
}


/* 
 * init_slurm_conf - initialize or re-initialize the slurm configuration  
 *	values. this should be called before calling read_slurm_conf.  
 * output: return value - 0 if no error, otherwise an error code
 */
int init_slurm_conf () {
	int error_code;

	if (control_machine) {
		free (control_machine);
		control_machine = NULL;
	}
	if (backup_controller) {
		free (backup_controller);
		backup_controller = NULL;
	}

	error_code = init_node_conf ();
	if (error_code)
		return error_code;

	error_code = init_part_conf ();
	if (error_code)
		return error_code;

	return 0;
}


/* 
 * parse_node_spec - parse the node specification, build table and set values
 * input:  in_line line from the configuration file
 * output: in_line parsed keywords and values replaced by blanks
 *         return value 0 if no error, error code otherwise
 */
int parse_node_spec (char *in_line) {
	char *node_name, *state, *feature, *format, this_node_name[BUF_SIZE];
	int start_inx, end_inx, count_inx;
	int error_code, i;
	int state_val, cpus_val, real_memory_val, tmp_disk_val, weight_val;
	struct node_record *node_record_point;
	struct config_record *config_point;
	char *str_ptr1, *str_ptr2;

	node_name = state = feature = (char *) NULL;
	cpus_val = real_memory_val = state_val = NO_VAL;
	tmp_disk_val = weight_val = NO_VAL;
	if (error_code = load_string (&node_name, "NodeName=", in_line))
		return error_code;
	if (node_name == NULL)
		return 0;	/* no node info */

	if (error_code == 0)
		error_code = load_integer (&cpus_val, "CPUs=", in_line);
	if (error_code == 0)
		error_code =
			load_integer (&real_memory_val, "RealMemory=",
				      in_line);
	if (error_code == 0)
		error_code =
			load_integer (&tmp_disk_val, "TmpDisk=", in_line);
	if (error_code == 0)
		error_code = load_integer (&weight_val, "Weight=", in_line);
	if (error_code != 0) {
		free (node_name);
		return error_code;
	}			

	if (error_code = load_string (&state, "State=", in_line))
		return error_code;
	if (state != NULL) {
		for (i = 0; i <= STATE_END; i++) {
			if (strcmp (node_state_string[i], "END") == 0)
				break;
			if (strcmp (node_state_string[i], state) == 0) {
				state_val = i;
				break;
			}	
		}		
		if (state_val == NO_VAL) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "parse_node_spec: invalid state %s for node_name %s\n",
				 state, node_name);
#else
			syslog (log_err,
				"parse_node_spec: invalid state %s for node_name %s\n",
				state, node_name);
#endif
			free (node_name);
			free (state);
			return EINVAL;
		}		
		free (state);
	}			

	error_code = load_string (&feature, "Feature=", in_line);
	if (error_code != 0) {
		free (node_name);
		return error_code;
	}			

	error_code =
		parse_node_name (node_name, &format, &start_inx, &end_inx,
				 &count_inx);
	if (error_code != 0) {
		free (node_name);
		if (feature)
			free (feature);
		return error_code;
	}			
	if (count_inx == 0) {	/* execute below loop once */
		start_inx = 0;
		end_inx = 0;
	}			

	for (i = start_inx; i <= end_inx; i++) {
		if (count_inx == 0) {	/* deal with comma separated node names here */
			if (i == start_inx)
				str_ptr2 = strtok_r (format, ",", &str_ptr1);
			else
				str_ptr2 = strtok_r (NULL, ",", &str_ptr1);
			if (str_ptr2 == NULL)
				break;
			end_inx++;
			strncpy (this_node_name, str_ptr2,
				 sizeof (this_node_name));
		}
		else
			sprintf (this_node_name, format, i);
		if (strlen (this_node_name) >= MAX_NAME_LEN) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "parse_node_spec: node name %s too long\n",
				 this_node_name);
#else
			syslog (log_err,
				"parse_node_spec: node name %s too long\n",
				this_node_name);
#endif
			if (i == start_inx)
				free (node_name);
			if (feature)
				free (feature);	/* can't use feature */
			error_code = EINVAL;
			break;
		}		
		if (strcmp (node_name, "DEFAULT") == 0) {
			if (i == start_inx)
				free (node_name);
			if (cpus_val != NO_VAL)
				default_config_record.cpus = cpus_val;
			if (real_memory_val != NO_VAL)
				default_config_record.real_memory =
					real_memory_val;
			if (tmp_disk_val != NO_VAL)
				default_config_record.tmp_disk = tmp_disk_val;
			if (weight_val != NO_VAL)
				default_config_record.weight = weight_val;
			if (state_val != NO_VAL)
				default_node_record.node_state = state_val;
			if (feature) {
				if (default_config_record.feature)
					free (default_config_record.feature);
				default_config_record.feature = feature;
			}	
		}
		else {
			if (i == start_inx) {
				config_point =
					create_config_record (&error_code);
				if (error_code != 0) {
					if (feature)
						free (feature);	/* can't use feature */
					break;
				}	
				config_point->nodes = node_name;
				if (cpus_val != NO_VAL)
					config_point->cpus = cpus_val;
				if (real_memory_val != NO_VAL)
					config_point->real_memory =
						real_memory_val;
				if (tmp_disk_val != NO_VAL)
					config_point->tmp_disk = tmp_disk_val;
				if (weight_val != NO_VAL)
					config_point->weight = weight_val;
				if (feature) {
					if (config_point->feature)
						free (config_point->feature);
					config_point->feature = feature;
				}	
			}	

			node_record_point = find_node_record (this_node_name);
			if (node_record_point == NULL) {
				node_record_point =
					create_node_record (&error_code,
							    config_point,
							    this_node_name);
				if (error_code != 0)
					break;
				if (state_val != NO_VAL)
					node_record_point->node_state =
						state_val;
			}
			else {
#if DEBUG_SYSTEM
				fprintf (stderr,
					 "parse_node_spec: reconfiguration for node %s ignored.\n",
					 this_node_name);
#else
				syslog (log_err,
					"parse_node_spec: reconfiguration for node %s ignored.\n",
					this_node_name);
#endif
			}	/* else */
		}		/* else */
	}			/* for (i */

	/* free allocated storage */
	if (format)
		free (format);
	return error_code;
}


/*
 * parse_part_spec - parse the partition specification, build table and set values
 * output: 0 if no error, error code otherwise
 */
int parse_part_spec (char *in_line) {
	int line_num;		/* line number in input file */
	char *allow_groups, *nodes, *partition_name;
	int max_time_val, max_nodes_val, key_val, default_val, state_up_val,
		shared_val;
	int error_code, i;
	struct part_record *part_record_point;

	allow_groups = nodes = partition_name = (char *) NULL;
	max_time_val = max_nodes_val = key_val = default_val = state_up_val =
		shared_val = NO_VAL;

	if (error_code =
	    load_string (&partition_name, "PartitionName=", in_line))
		return error_code;
	if (partition_name == NULL)
		return 0;	/* no partition info */
	if (strlen (partition_name) >= MAX_NAME_LEN) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "parse_part_spec: partition name %s too long\n",
			 partition_name);
#else
		syslog (log_err,
			"parse_part_spec: partition name %s too long\n",
			partition_name);
#endif
		free (partition_name);
		return EINVAL;
	}			

	if (error_code == 0)
		error_code =
			load_integer (&max_time_val, "MaxTime=", in_line);
	if (error_code == 0)
		error_code =
			load_integer (&max_nodes_val, "MaxNodes=", in_line);
	if (error_code == 0)
		error_code =
			load_integer (&default_val, "Default=NO", in_line);
	if (default_val == 1)
		default_val = 0;
	if (error_code == 0)
		error_code =
			load_integer (&default_val, "Default=YES", in_line);
	if (error_code == 0)
		error_code = load_integer (&shared_val, "Shared=NO", in_line);
	if (state_up_val == 1)
		shared_val = 0;
	if (error_code == 0)
		error_code =
			load_integer (&shared_val, "Shared=FORCE", in_line);
	if (state_up_val == 1)
		shared_val = 2;
	if (error_code == 0)
		error_code =
			load_integer (&shared_val, "Shared=YES", in_line);
	if (error_code == 0)
		error_code =
			load_integer (&state_up_val, "State=DOWN", in_line);
	if (state_up_val == 1)
		state_up_val = 0;
	if (error_code == 0)
		error_code =
			load_integer (&state_up_val, "State=UP", in_line);
	if (error_code == 0)
		error_code = load_integer (&key_val, "Key=NO", in_line);
	if (key_val == 1)
		key_val = 0;
	if (error_code == 0)
		error_code = load_integer (&key_val, "Key=YES", in_line);
	if (error_code != 0) {
		free (partition_name);
		return error_code;
	}			

	error_code = load_string (&nodes, "Nodes=", in_line);
	if (error_code) {
		free (partition_name);
		return error_code;
	}			

	error_code = load_string (&allow_groups, "AllowGroups=", in_line);
	if (error_code) {
		free (partition_name);
		if (nodes)
			free (nodes);
		return error_code;
	}			

	if (strcmp (partition_name, "DEFAULT") == 0) {
		free (partition_name);
		if (max_time_val != NO_VAL)
			default_part.max_time = max_time_val;
		if (max_nodes_val != NO_VAL)
			default_part.max_nodes = max_nodes_val;
		if (key_val != NO_VAL)
			default_part.key = key_val;
		if (state_up_val != NO_VAL)
			default_part.state_up = state_up_val;
		if (shared_val != NO_VAL)
			default_part.shared = shared_val;
		if (allow_groups) {
			if (default_part.allow_groups)
				free (default_part.allow_groups);
			default_part.allow_groups = allow_groups;
		}		
		if (nodes) {
			if (default_part.nodes)
				free (default_part.nodes);
			default_part.nodes = nodes;
		}		
		return 0;
	}			

	part_record_point =
		list_find_first (part_list, &list_find_part, partition_name);
	if (part_record_point == NULL) {
		part_record_point = create_part_record (&error_code);
		if (error_code) {
			free (partition_name);
			if (nodes)
				free (nodes);
			if (allow_groups)
				free (allow_groups);
			return error_code;
		}		
		strcpy (part_record_point->name, partition_name);
	}
	else {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "parse_part_spec: duplicate entry for partition %s\n",
			 partition_name);
#else
		syslog (log_notice,
			"parse_node_spec: duplicate entry for partition %s\n",
			partition_name);
#endif
	}			/* else */
	if (default_val == 1) {
		if (strlen (default_part_name) > 0) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "parse_part_spec: changing default partition from %s to %s\n",
				 default_part_name, partition_name);
#else
			syslog (log_notice,
				"parse_node_spec: changing default partition from %s to %s\n",
				default_part_name, partition_name);
#endif
		}		
		strcpy (default_part_name, partition_name);
		default_part_loc = part_record_point;
	}			
	if (max_time_val != NO_VAL)
		part_record_point->max_time = max_time_val;
	if (max_nodes_val != NO_VAL)
		part_record_point->max_nodes = max_nodes_val;
	if (key_val != NO_VAL)
		part_record_point->key = key_val;
	if (state_up_val != NO_VAL)
		part_record_point->state_up = state_up_val;
	if (shared_val != NO_VAL)
		part_record_point->shared = shared_val;
	if (allow_groups) {
		if (part_record_point->allow_groups)
			free (part_record_point->allow_groups);
		part_record_point->allow_groups = allow_groups;
	}			
	if (nodes) {
		if (part_record_point->nodes)
			free (part_record_point->nodes);
		part_record_point->nodes = nodes;
	}			
	free (partition_name);
	return 0;
}


/*
 * read_slurm_conf - load the slurm configuration from the specified file. 
 * call init_slurm_conf before ever calling read_slurm_conf.  
 * read_slurm_conf can be called more than once if so desired.
 * input: file_name - name of the file containing overall slurm configuration information
 * output: return - 0 if no error, otherwise an error code
 */
int read_slurm_conf (char *file_name) {
	FILE *slurm_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUF_SIZE];	/* input line */
	char scratch[BUF_SIZE];	/* scratch area for parsing the input line */
	char *str_ptr1, *str_ptr2, *str_ptr3;
	int i, j, error_code;

	/* initialization */
	slurm_spec_file = fopen (file_name, "r");
	if (slurm_spec_file == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr, "read_slurm_conf error %d opening file %s\n",
			 errno, file_name);
#else
		syslog (log_alert,
			"read_slurm_conf error %d opening file %s\n", errno,
			file_name);
#endif
		return errno;
	}			

#if DEBUG_SYSTEM
	fprintf (stderr, "read_slurm_conf: loading configuration from %s\n",
		 file_name);
#else
	syslog (log_notice,
		"read_slurm_conf: loading configuration from %s\n",
		file_name);
#endif

	/* process the data file */
	line_num = 0;
	while (fgets (in_line, BUF_SIZE, slurm_spec_file) != NULL) {
		line_num++;
		if (strlen (in_line) >= (BUF_SIZE - 1)) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "read_slurm_conf line %d, of input file %s too long\n",
				 line_num, file_name);
#else
			syslog (log_alert,
				"read_slurm_conf line %d, of input file %s too long\n",
				line_num, file_name);
#endif
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
		if (error_code =
		    load_string (&control_machine, "ControlMachine=",
				 in_line)) {
			fclose (slurm_spec_file);
			return error_code;
		}		
		if (error_code =
		    load_string (&backup_controller, "BackupController=",
				 in_line)) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		/* node configuration parameters */
		if (error_code = parse_node_spec (in_line)) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		/* partition configuration parameters */
		if (error_code = parse_part_spec (in_line)) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		/* report any leftover strings on input line */
		report_leftover (in_line, line_num);
	}			
	fclose (slurm_spec_file);

	/* if values not set in configuration file, set defaults */
	if (backup_controller == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "read_slurm_conf: backup_controller value not specified.\n");
#else
		syslog (log_warning,
			"read_slurm_conf: backup_controller value not specified.\n");
#endif
	}			

	if (control_machine == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "read_slurm_conf: control_machine value not specified.\n");
#else
		syslog (log_alert,
			"read_slurm_conf: control_machine value not specified.\n");
#endif
		return EINVAL;
	}			

	if (default_part_loc == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "read_slurm_conf: default partition not set.\n");
#else
		syslog (log_alert,
			"read_slurm_conf: default partition not set.\n");
#endif
		return EINVAL;
	}			
	rehash ();
	if (error_code = build_bitmaps ())
		return error_code;
	list_sort (config_list, &list_compare_config);

#if DEBUG_SYSTEM
	fprintf (stderr, "read_slurm_conf: finished loading configuration\n");
#else
	syslog (log_notice,
		"read_slurm_conf: finished loading configuration\n");
#endif

	return 0;
}
