/* 
 * partition_mgr.c - manage the partition information of slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "list.h"
#include "slurm.h"

#define BUF_SIZE 1024
#define NO_VAL   -99
#define SEPCHARS " \n\t"

struct part_record default_part;	/* default configuration values */
List part_list = NULL;			/* partition list */
char default_part_name[MAX_NAME_LEN];	/* name of default partition */
struct part_record *default_part_loc = NULL;	/* location of default partition */
time_t last_part_update;		/* time of last update to part records */
static pthread_mutex_t part_mutex = PTHREAD_MUTEX_INITIALIZER;	/* lock for partition info */

int build_part_bitmap (struct part_record *part_record_point);
void list_delete_part (void *part_entry);
int list_find_part (void *part_entry, void *key);

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
main (int argc, char *argv[]) {
	int error_code;
	time_t update_time;
	struct part_record *part_ptr;
	char *dump;
	int dump_size;
	char req_name[MAX_NAME_LEN];	/* name of the partition */
	char next_name[MAX_NAME_LEN];	/* name of the next partition */
	int max_time;		/* -1 if unlimited */
	int max_nodes;		/* -1 if unlimited */
	int total_nodes;	/* total number of nodes in the partition */
	int total_cpus;		/* total number of cpus in the partition */
	char *nodes;		/* names of nodes in partition */
	char *allow_groups;	/* NULL indicates all */
	int key;		/* 1 if slurm distributed key is required for use of partition */
	int state_up;		/* 1 if state is up */
	int shared;		/* 1 if partition can be shared */
	unsigned *node_bitmap;	/* bitmap of nodes in partition */
	int bitmap_size;	/* bytes in node_bitmap */
	char update_spec[] =
		"MaxTime=34 MaxNodes=56 Key=NO State=DOWN Shared=FORCE";

	error_code = init_node_conf ();
	if (error_code)
		printf ("init_node_conf error %d\n", error_code);
	error_code = init_part_conf ();
	if (error_code)
		printf ("init_part_conf error %d\n", error_code);
	default_part.max_time = 223344;
	default_part.max_nodes = 556677;
	default_part.total_nodes = 4;
	default_part.total_cpus = 16;
	default_part.key = 1;
	node_record_count = 8;

	printf ("create some partitions and test defaults\n");
	part_ptr = create_part_record (&error_code);
	if (error_code)
		printf ("create_part_record error %d\n", error_code);
	else {
		int tmp_bitmap;
		if (part_ptr->max_time != 223344)
			printf ("ERROR: partition default max_time not set\n");
		if (part_ptr->max_nodes != 556677)
			printf ("ERROR: partition default max_nodes not set\n");
		if (part_ptr->total_nodes != 4)
			printf ("ERROR: partition default total_nodes not set\n");
		if (part_ptr->total_cpus != 16)
			printf ("ERROR: partition default max_nodes not set\n");
		if (part_ptr->key != 1)
			printf ("ERROR: partition default key not set\n");
		if (part_ptr->state_up != 1)
			printf ("ERROR: partition default state_up not set\n");
		if (part_ptr->shared != 0)
			printf ("ERROR: partition default shared not set\n");
		strcpy (part_ptr->name, "interactive");
		part_ptr->nodes = "lx[01-04]";
		part_ptr->allow_groups = "students";
		tmp_bitmap = 0x3c << (sizeof (unsigned) * 8 - 8);
		part_ptr->node_bitmap = &tmp_bitmap;
	}			
	part_ptr = create_part_record (&error_code);
	if (error_code)
		printf ("create_part_record error %d\n", error_code);
	else
		strcpy (part_ptr->name, "batch");
	part_ptr = create_part_record (&error_code);
	if (error_code)
		printf ("ERROR: create_part_record error %d\n", error_code);
	else
		strcpy (part_ptr->name, "class");

	update_time = (time_t) 0;
	error_code = dump_part (&dump, &dump_size, &update_time);
	if (error_code)
		printf ("ERROR: dump_part error %d\n", error_code);

	error_code = update_part ("batch", update_spec);
	if (error_code)
		printf ("ERROR: update_part error %d\n", error_code);

	part_ptr = list_find_first (part_list, &list_find_part, "batch");
	if (part_ptr == NULL)
		printf ("ERROR: list_find failure\n");
	if (part_ptr->max_time != 34)
		printf ("ERROR: update_part max_time not reset\n");
	if (part_ptr->max_nodes != 56)
		printf ("ERROR: update_part max_nodes not reset\n");
	if (part_ptr->key != 0)
		printf ("ERROR: update_part key not reset\n");
	if (part_ptr->state_up != 0)
		printf ("ERROR: update_part state_up not set\n");
	if (part_ptr->shared != 2)
		printf ("ERROR: update_part shared not set\n");

	node_record_count = 0;	/* delete_part_record dies if node count is bad */
	error_code = delete_part_record ("batch");
	if (error_code != 0)
		printf ("delete_part_record error1 %d\n", error_code);
	printf ("NOTE: we expect delete_part_record to report not finding a record for batch\n");
	error_code = delete_part_record ("batch");
	if (error_code != ENOENT)
		printf ("ERROR: delete_part_record error2 %d\n", error_code);

	exit (0);
}
#endif


/*
 * build_part_bitmap - update the total_cpus, total_nodes, and node_bitmap for the specified partition
 *	also reset the partition pointers in the node back to this partition.
 * input: part_record_point - pointer to the partition
 * output: returns 0 if no error, errno otherwise
 * NOTE: this does not report nodes defined in more than one partition. this is checked only  
 *	upon reading the configuration file, not on an update
 */
int build_part_bitmap (struct part_record *part_record_point) {
	int start_inx, end_inx, count_inx;
	int i, j, error_code, size;
	char *str_ptr1, *str_ptr2, *format, *my_node_list,
		this_node_name[BUF_SIZE];
	unsigned *old_bitmap;
	struct node_record *node_record_point;	/* pointer to node_record */

	format = my_node_list = NULL;
	part_record_point->total_cpus = 0;
	part_record_point->total_nodes = 0;

	size = (node_record_count + (sizeof (unsigned) * 8) - 1) / (sizeof (unsigned) * 8);	/* unsigned int records in bitmap */
	size *= 8;		/* bytes in bitmap */
	if (part_record_point->node_bitmap == NULL) {
		part_record_point->node_bitmap = malloc (size);
		if (part_record_point->node_bitmap == NULL) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "build_part_bitmap: unable to allocate memory\n");
#else
			syslog (LOG_ALERT,
				"build_part_bitmap: unable to allocate memory\n");
#endif
			abort ();
		}		
		old_bitmap = NULL;
	}
	else
		old_bitmap = bitmap_copy (part_record_point->node_bitmap);
	memset (part_record_point->node_bitmap, 0, size);

	if (part_record_point->nodes == NULL) {
		if (old_bitmap)
			free (old_bitmap);
		return 0;
	}			
	my_node_list = malloc (strlen (part_record_point->nodes) + 1);
	if (my_node_list == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "build_part_bitmap: unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"build_part_bitmap: unable to allocate memory\n");
#endif
		if (old_bitmap)
			free (old_bitmap);
		abort ();
	}			
	strcpy (my_node_list, part_record_point->nodes);

	str_ptr2 = (char *) strtok_r (my_node_list, ",", &str_ptr1);
	while (str_ptr2) {	/* break apart by comma separators */
		error_code =
			parse_node_name (str_ptr2, &format, &start_inx,
					 &end_inx, &count_inx);
		if (error_code) {
			free (my_node_list);
			if (old_bitmap)
				free (old_bitmap);
			return EINVAL;
		}		
		if (strlen (format) >= sizeof (this_node_name)) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "build_part_bitmap: node name specification too long: %s\n",
				 format);
#else
			syslog (LOG_ERR,
				"build_part_bitmap: node name specification too long: %s\n",
				format);
#endif
			free (my_node_list);
			free (format);
			if (old_bitmap)
				free (old_bitmap);
			return EINVAL;
		}		
		for (i = start_inx; i <= end_inx; i++) {
			if (count_inx == 0)
				strncpy (this_node_name, format,
					 sizeof (this_node_name));
			else
				sprintf (this_node_name, format, i);
			node_record_point = find_node_record (this_node_name);
			if (node_record_point == NULL) {
#if DEBUG_SYSTEM
				fprintf (stderr,
					 "build_part_bitmap: invalid node specified %s\n",
					 this_node_name);
#else
				syslog (LOG_ERR,
					"build_part_bitmap: invalid node specified %s\n",
					this_node_name);
#endif
				free (my_node_list);
				free (format);
				if (old_bitmap)
					free (old_bitmap);
				return EINVAL;
			}	
			bitmap_set (part_record_point->node_bitmap,
				    (int) (node_record_point -
					   node_record_table_ptr));
			part_record_point->total_nodes++;
			part_record_point->total_cpus +=
				node_record_point->cpus;
			node_record_point->partition_ptr = part_record_point;
			bitmap_clear (old_bitmap,
				      (int) (node_record_point -
					     node_record_table_ptr));
		}		
		str_ptr2 = (char *) strtok_r (NULL, ",", &str_ptr1);
	}			

	/* unlink nodes removed from the partition */
	for (i = 0; i < node_record_count; i++) {
		if (bitmap_value (old_bitmap, i) == 0)
			continue;
		node_record_table_ptr[i].partition_ptr = NULL;
	}			

	if (my_node_list)
		free (my_node_list);
	if (format)
		free (format);
	if (old_bitmap)
		free (old_bitmap);
	return 0;
}


/* 
 * create_part_record - create a partition record
 * input: error_code - location to store error value in
 * output: error_code - set to zero if no error, errno otherwise
 *         returns a pointer to the record or NULL if error
 * NOTE: the record's values are initialized to those of default_part
 * NOTE: allocates memory that should be freed with delete_part_record
 */
struct part_record * create_part_record (int *error_code) {
	struct part_record *part_record_point;

	*error_code = 0;
	last_part_update = time (NULL);

	part_record_point =
		(struct part_record *) malloc (sizeof (struct part_record));
	if (part_record_point == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "create_part_record: unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"create_part_record: unable to allocate memory\n");
#endif
		abort ();
	}			

	strcpy (part_record_point->name, "default");
	part_record_point->max_time = default_part.max_time;
	part_record_point->max_nodes = default_part.max_nodes;
	part_record_point->key = default_part.key;
	part_record_point->state_up = default_part.state_up;
	part_record_point->shared = default_part.shared;
	part_record_point->total_nodes = default_part.total_nodes;
	part_record_point->total_cpus = default_part.total_cpus;
	part_record_point->node_bitmap = NULL;
#if DEBUG_SYSTEM
	part_record_point->magic = PART_MAGIC;
#endif

	if (default_part.allow_groups) {
		part_record_point->allow_groups =
			(char *) malloc (strlen (default_part.allow_groups) +
					 1);
		if (part_record_point->allow_groups == NULL) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "create_part_record: unable to allocate memory\n");
#else
			syslog (LOG_ALERT,
				"create_part_record: unable to allocate memory\n");
#endif
			abort ();
		}		
		strcpy (part_record_point->allow_groups,
			default_part.allow_groups);
	}
	else
		part_record_point->allow_groups = NULL;

	if (default_part.nodes) {
		part_record_point->nodes =
			(char *) malloc (strlen (default_part.nodes) + 1);
		if (part_record_point->nodes == NULL) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "create_part_record: unable to allocate memory\n");
#else
			syslog (LOG_ALERT,
				"create_part_record: unable to allocate memory\n");
#endif
			abort ();
		}		
		strcpy (part_record_point->nodes, default_part.nodes);
	}
	else
		part_record_point->nodes = NULL;

	if (list_append (part_list, part_record_point) == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "create_part_record: unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"create_part_record: unable to allocate memory\n");
#endif
		abort ();
	}			

	return part_record_point;
}


/* 
 * delete_part_record - delete record for partition with specified name
 * input: name - name of the desired node, delete all partitions if pointer is NULL 
 * output: return 0 on success, errno otherwise
 */
int delete_part_record (char *name) {
	int i;

	last_part_update = time (NULL);
	if (name == NULL)
		i = list_delete_all (part_list, &list_find_part,
				     "universal_key");
	else
		i = list_delete_all (part_list, &list_find_part, name);
	if ((name == NULL) || (i != 0))
		return 0;

#if DEBUG_SYSTEM
	fprintf (stderr,
		 "delete_part_record: attempt to delete non-existent partition %s\n",
		 name);
#else
	syslog (LOG_ERR,
		"delete_part_record: attempt to delete non-existent partition %s\n",
		name);
#endif
	return ENOENT;
}


/* 
 * dump_part - dump all partition information to a buffer
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the data buffer is actually allocated by dump_part and the 
 *                     calling function must free the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 *         returns 0 if no error, errno otherwise
 * NOTE: the buffer at *buffer_ptr must be freed by the caller
 * NOTE: if you make any changes here be sure to increment the value of PART_STRUCT_VERSION
 *       and make the corresponding changes to load_part_name in api/partition_info.c
 */
int dump_part (char **buffer_ptr, int *buffer_size, time_t * update_time) {
	ListIterator part_record_iterator;	/* for iterating through part_record_list */
	struct part_record *part_record_point;	/* pointer to part_record */
	char *buffer;
	int buffer_offset, buffer_allocated, i, record_size;
	char out_line[BUF_SIZE * 2], *nodes, *key, *default_flag, *allow_groups,
		*shared, *state;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	buffer = NULL;
	buffer_offset = 0;
	buffer_allocated = 0;
	if (*update_time == last_part_update)
		return 0;

	part_record_iterator = list_iterator_create (part_list);
	if (part_record_iterator == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "dump_part: list_iterator_create unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"dump_part: list_iterator_create unable to allocate memory\n");
#endif
		abort ();
	}			

	/* write haeader, version and time */
	sprintf (out_line, HEAD_FORMAT, (unsigned long) last_part_update,
		 PART_STRUCT_VERSION);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	/* write partition records */
	while (part_record_point =
	       (struct part_record *) list_next (part_record_iterator)) {
#if DEBUG_SYSTEM
		if (part_record_point->magic != PART_MAGIC) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "dump_part: data integrity is bad\n");
#else
			syslog (LOG_ALERT,
				"dump_part: data integrity is bad\n");
#endif
			abort ();
		}		
#endif

		if (part_record_point->nodes)
			nodes = part_record_point->nodes;
		else
			nodes = "NONE";
		if (part_record_point == default_part_loc)
			default_flag = "YES";
		else
			default_flag = "NO";
		if (part_record_point->key)
			key = "YES";
		else
			key = "NO";
		if (part_record_point->state_up)
			state = "UP";
		else
			state = "DONW";
		if (part_record_point->shared)
			shared = "YES";
		else
			shared = "NO";
		if (part_record_point->allow_groups)
			allow_groups = part_record_point->allow_groups;
		else
			allow_groups = "ALL";
		sprintf (out_line, PART_STRUCT_FORMAT,
			 part_record_point->name,
			 part_record_point->max_nodes,
			 part_record_point->max_time,
			 nodes,
			 key,
			 default_flag,
			 allow_groups,
			 shared,
			 state,
			 part_record_point->total_nodes,
			 part_record_point->total_cpus);

		if (strlen (out_line) > BUF_SIZE) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "dump_part: buffer overflow for partition %s\n",
				 part_record_point->name);
#else
			syslog (LOG_ALERT,
				"dump_part: buffer overflow for partition %s\n",
				part_record_point->name);
#endif
			if (strlen (out_line) > (2 * BUF_SIZE))
				abort ();
		}		

		if (write_buffer
		    (&buffer, &buffer_offset, &buffer_allocated, out_line))
			goto cleanup;
	}			

	list_iterator_destroy (part_record_iterator);
	buffer = realloc (buffer, buffer_offset);
	if (buffer == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr, "dump_part: unable to allocate memory\n");
#else
		syslog (LOG_ALERT, "dump_part: unable to allocate memory\n");
#endif
		abort ();
	}			

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	*update_time = last_part_update;
	return 0;

      cleanup:
	list_iterator_destroy (part_record_iterator);
	if (buffer)
		free (buffer);
	return EINVAL;
}


/* 
 * init_part_conf - initialize the partition configuration values. 
 * this should be called before creating any partition entries.
 * output: return value - 0 if no error, otherwise an error code
 */
int init_part_conf () {
	last_part_update = time (NULL);

	strcpy (default_part.name, "DEFAULT");
	default_part.allow_groups = (char *) NULL;
	default_part.max_time = -1;
	default_part.max_nodes = -1;
	default_part.key = 0;
	default_part.state_up = 1;
	default_part.shared = 0;
	default_part.total_nodes = 0;
	default_part.total_cpus = 0;
	if (default_part.nodes)
		free (default_part.nodes);
	default_part.nodes = (char *) NULL;
	if (default_part.allow_groups)
		free (default_part.allow_groups);
	default_part.allow_groups = (char *) NULL;
	if (default_part.node_bitmap)
		free (default_part.node_bitmap);
	default_part.node_bitmap = (unsigned *) NULL;

	if (part_list)		/* delete defunct partitions */
		(void) delete_part_record (NULL);
	else
		part_list = list_create (&list_delete_part);

	if (part_list == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "init_part_conf: list_create can not allocate memory\n");
#else
		syslog (LOG_ALERT,
			"init_part_conf: list_create can not allocate memory\n");
#endif
		abort ();
	}			

	strcpy (default_part_name, "");
	default_part_loc = (struct part_record *) NULL;

	return 0;
}

/* list_delete_part - delete an entry from the partition list, see list.h for documentation */
void list_delete_part (void *part_entry) {
	struct part_record *part_record_point;	/* pointer to part_record */
	int i;

	part_record_point = (struct part_record *) part_entry;
	for (i = 0; i < node_record_count; i++) {
		if (node_record_table_ptr[i].partition_ptr !=
		    part_record_point)
			continue;
		node_record_table_ptr[i].partition_ptr = NULL;
	}			
	if (part_record_point->allow_groups)
		free (part_record_point->allow_groups);
	if (part_record_point->nodes)
		free (part_record_point->nodes);
	if (part_record_point->node_bitmap)
		free (part_record_point->node_bitmap);
	free (part_entry);
}


/* list_find_part - find an entry in the partition list, see list.h for documentation 
 * key is partition name or "universal_key" for all partitions */
int list_find_part (void *part_entry, void *key) {
	struct part_record *part_record_point;	/* pointer to part_record */
	if (strcmp (key, "universal_key") == 0)
		return 1;
	part_record_point = (struct part_record *) part_entry;
	if (strcmp (part_record_point->name, (char *) key) == 0)
		return 1;
	return 0;
}


/* part_lock - lock the partition information */
void part_lock () {
	int error_code;
	error_code = pthread_mutex_lock (&part_mutex);
	if (error_code) {
#if DEBUG_SYSTEM
		fprintf (stderr, "part_lock: pthread_mutex_lock error %d\n",
			 error_code);
#else
		syslog (LOG_ALERT, "part_lock: pthread_mutex_lock error %d\n",
			error_code);
#endif
		abort ();
	}			
}


/* part_unlock - unlock the partition information */
void part_unlock () {
	int error_code;
	error_code = pthread_mutex_unlock (&part_mutex);
	if (error_code) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "part_unlock: pthread_mutex_unlock error %d\n",
			 error_code);
#else
		syslog (LOG_ALERT,
			"part_unlock: pthread_mutex_unlock error %d\n",
			error_code);
#endif
		abort ();
	}			
}


/* 
 * update_part - update a partition's configuration data
 * input: partition_name - partition's name
 *        spec - the updates to the partition's specification 
 * output:  return - 0 if no error, otherwise an error code
 * NOTE: the contents of spec are overwritten by white space
 */
int update_part (char *partition_name, char *spec) {
	int error_code;
	struct part_record *part_ptr;
	int max_time_val, max_nodes_val, key_val, state_val, shared_val,
		default_val;
	char *allow_groups, *nodes;
	int bad_index, i;

	if (strcmp (partition_name, "DEFAULT") == 0) {
#if DEBUG_SYSTEM
		fprintf (stderr, "update_part: invalid partition name %s\n",
			 partition_name);
#else
		syslog (LOG_ALERT,
			"update_part: invalid partition name  %s\n",
			partition_name);
#endif
		return EINVAL;
	}			

	part_ptr =
		list_find_first (part_list, &list_find_part, partition_name);
	if (part_ptr == 0) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "update_part: partition %s does not exist, being created.\n",
			 partition_name);
#else
		syslog (LOG_ALERT,
			"update_part: partition %s does not exist, being created.\n",
			partition_name);
#endif
		part_ptr = create_part_record (&error_code);
		if (error_code)
			goto cleanup;
	}			

	max_time_val = NO_VAL;
	error_code = load_integer (&max_time_val, "MaxTime=", spec);
	if (error_code)
		goto cleanup;

	max_nodes_val = NO_VAL;
	error_code = load_integer (&max_nodes_val, "MaxNodes=", spec);
	if (error_code)
		goto cleanup;

	key_val = NO_VAL;
	error_code = load_integer (&key_val, "Key=NO", spec);
	if (error_code)
		goto cleanup;
	if (key_val == 1)
		key_val = 0;
	error_code = load_integer (&key_val, "Key=YES", spec);
	if (error_code)
		goto cleanup;

	state_val = NO_VAL;
	error_code = load_integer (&state_val, "State=DOWN", spec);
	if (error_code)
		goto cleanup;
	if (state_val == 1)
		state_val = 0;
	error_code = load_integer (&state_val, "State=UP", spec);
	if (error_code)
		goto cleanup;

	shared_val = NO_VAL;
	error_code = load_integer (&shared_val, "Shared=NO", spec);
	if (error_code)
		goto cleanup;
	if (shared_val == 1)
		shared_val = 0;
	error_code = load_integer (&shared_val, "Shared=FORCE", spec);
	if (error_code)
		goto cleanup;
	if (shared_val == 1)
		shared_val = 2;
	error_code = load_integer (&shared_val, "Shared=YES", spec);
	if (error_code)
		goto cleanup;

	default_val = NO_VAL;
	error_code = load_integer (&default_val, "Default=YES", spec);
	if (error_code)
		goto cleanup;

	allow_groups = NULL;
	error_code = load_string (&allow_groups, "AllowGroups=", spec);
	if (error_code)
		goto cleanup;

	nodes = NULL;
	error_code = load_string (&nodes, "Nodes=", spec);
	if (error_code)
		goto cleanup;

	bad_index = -1;
	for (i = 0; i < strlen (spec); i++) {
		if (spec[i] == '\n')
			spec[i] = ' ';
		if (isspace ((int) spec[i]))
			continue;
		bad_index = i;
		break;
	}			

	if (bad_index != -1) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "update_part: ignored partition %s update specification: %s\n",
			 partition_name, &spec[bad_index]);
#else
		syslog (LOG_ERR,
			"update_part: ignored partition %s update specification: %s\n",
			partition_name, &spec[bad_index]);
#endif
		error_code = EINVAL;
		goto cleanup;
	}			

	last_part_update = time (NULL);
	if (max_time_val != NO_VAL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "update_part: setting max_time to %d for partition %s\n",
			 max_time_val, partition_name);
#else
		syslog (LOG_NOTICE,
			"update_part: setting max_time to %d for partition %s\n",
			max_time_val, partition_name);
#endif
		part_ptr->max_time = max_time_val;
	}			

	if (max_nodes_val != NO_VAL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "update_part: setting max_nodes to %d for partition %s\n",
			 max_nodes_val, partition_name);
#else
		syslog (LOG_NOTICE,
			"update_part: setting max_nodes to %d for partition %s\n",
			max_nodes_val, partition_name);
#endif
		part_ptr->max_nodes = max_nodes_val;
	}			

	if (key_val != NO_VAL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "update_part: setting key to %d for partition %s\n",
			 key_val, partition_name);
#else
		syslog (LOG_NOTICE,
			"update_part: setting key to %d for partition %s\n",
			key_val, partition_name);
#endif
		part_ptr->key = key_val;
	}			

	if (state_val != NO_VAL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "update_part: setting state_up to %d for partition %s\n",
			 state_val, partition_name);
#else
		syslog (LOG_NOTICE,
			"update_part: setting state_up to %d for partition %s\n",
			state_val, partition_name);
#endif
		part_ptr->state_up = state_val;
	}			

	if (shared_val != NO_VAL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "update_part: setting shared to %d for partition %s\n",
			 shared_val, partition_name);
#else
		syslog (LOG_NOTICE,
			"update_part: setting shared to %d for partition %s\n",
			shared_val, partition_name);
#endif
		part_ptr->shared = shared_val;
	}			

	if (default_val == 1) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "update_part: changing default partition from %s to %s\n",
			 default_part_name, partition_name);
#else
		syslog (LOG_NOTICE,
			"update_part: changing default partition from %s to %s\n",
			default_part_name, partition_name);
#endif
		strcpy (default_part_name, partition_name);
		default_part_loc = part_ptr;
	}			

	if (allow_groups != NULL) {
		if (part_ptr->allow_groups)
			free (part_ptr->allow_groups);
		part_ptr->allow_groups = allow_groups;
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "update_part: setting allow_groups to %s for partition %s\n",
			 allow_groups, partition_name);
#else
		syslog (LOG_NOTICE,
			"update_part: setting allow_groups to %s for partition %s\n",
			allow_groups, partition_name);
#endif
	}			

	if (nodes != NULL) {
		if (part_ptr->nodes)
			free (part_ptr->nodes);
		part_ptr->nodes = nodes;
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "update_part: setting nodes to %s for partition %s\n",
			 nodes, partition_name);
#else
		syslog (LOG_NOTICE,
			"update_part: setting nodes to %s for partition %s\n",
			nodes, partition_name);
#endif
		/* now we need to update total_cpus, total_nodes, and node_bitmap */
		error_code = build_part_bitmap (part_ptr);
		if (error_code)
			goto cleanup;
	}			

      cleanup:
	return error_code;
}
