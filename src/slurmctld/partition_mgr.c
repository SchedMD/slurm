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
	bitstr_t *node_bitmap;	/* bitmap of nodes in partition */
	char update_spec[] =
		"MaxTime=34 MaxNodes=56 Key=NO State=DOWN Shared=FORCE";
	log_options_t opts = LOG_OPTS_STDERR_ONLY;

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
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
		part_ptr->node_bitmap = (bitstr_t *) bit_alloc(20);
		bit_nset(part_ptr->node_bitmap, 2, 5);
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
	error_code = dump_all_part (&dump, &dump_size, &update_time);
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
 * build_part_bitmap - update the total_cpus, total_nodes, and node_bitmap for the specified 
 *	partition, also reset the partition pointers in the node back to this partition.
 * input: part_record_point - pointer to the partition
 * output: returns 0 if no error, errno otherwise
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: this does not report nodes defined in more than one partition. this is checked only  
 *	upon reading the configuration file, not on an update
 */
int build_part_bitmap (struct part_record *part_record_point) {
	int i, error_code, node_count;
	char *node_list;
	bitstr_t *old_bitmap;
	struct node_record *node_record_point;	/* pointer to node_record */

	part_record_point->total_cpus = 0;
	part_record_point->total_nodes = 0;

	if (part_record_point->node_bitmap == NULL) {
		part_record_point->node_bitmap = (bitstr_t *) bit_alloc (node_record_count);
		if (part_record_point->node_bitmap == NULL)
			fatal("bit_alloc memory allocation failure");
		old_bitmap = NULL;
	}
	else {
		bit_nclear (part_record_point->node_bitmap, 0, node_record_count-1);
		old_bitmap = bit_copy (part_record_point->node_bitmap);
	}

	if (part_record_point->nodes == NULL) {		/* no nodes in partition */
		if (old_bitmap)				/* leave with empty bitmap */
			bit_free (old_bitmap);
		return 0;
	}

	error_code = node_name2list(part_record_point->nodes, &node_list, &node_count);
	if (error_code) {
		if (old_bitmap)
			bit_free (old_bitmap);
		return error_code;
	}

	for (i = 0; i < node_count; i++) {
		node_record_point = find_node_record (&node_list[i*MAX_NAME_LEN]);
		if (node_record_point == NULL) {
			error ("build_part_bitmap: invalid node specified %s",
				&node_list[i*MAX_NAME_LEN]);
			if (old_bitmap)
				bit_free (old_bitmap);
			xfree(node_list);
			return EINVAL;
		}	
		part_record_point->total_nodes++;
		part_record_point->total_cpus += node_record_point->cpus;
		node_record_point->partition_ptr = part_record_point;
		bit_clear (old_bitmap,
			      (int) (node_record_point - node_record_table_ptr));
		bit_set (part_record_point->node_bitmap,
			    (int) (node_record_point - node_record_table_ptr));
	}
	xfree(node_list);

	/* unlink nodes removed from the partition */
	for (i = 0; i < node_record_count; i++) {
		if (bit_test (old_bitmap, i) == 0)
			continue;
		node_record_table_ptr[i].partition_ptr = NULL;
	}			

	if (old_bitmap)
		xfree (old_bitmap);
	return 0;
}


/* 
 * create_part_record - create a partition record
 * input: error_code - location to store error value in
 * output: error_code - set to zero if no error, errno otherwise
 *         returns a pointer to the record or NULL if error
 * global: default_part - default partition parameters
 *         part_list - global partition list
 * NOTE: the record's values are initialized to those of default_part
 * NOTE: allocates memory that should be xfreed with delete_part_record
 */
struct part_record * create_part_record (int *error_code) {
	struct part_record *part_record_point;

	*error_code = 0;
	last_part_update = time (NULL);

	part_record_point =
		(struct part_record *) xmalloc (sizeof (struct part_record));

	strcpy (part_record_point->name, "DEFAULT");
	part_record_point->max_time = default_part.max_time;
	part_record_point->max_nodes = default_part.max_nodes;
	part_record_point->key = default_part.key;
	part_record_point->state_up = default_part.state_up;
	part_record_point->shared = default_part.shared;
	part_record_point->total_nodes = default_part.total_nodes;
	part_record_point->total_cpus = default_part.total_cpus;
	part_record_point->node_bitmap = NULL;
	part_record_point->magic = PART_MAGIC;

	if (default_part.allow_groups) {
		part_record_point->allow_groups =
			(char *) xmalloc (strlen (default_part.allow_groups) + 1);
		strcpy (part_record_point->allow_groups,
			default_part.allow_groups);
	}
	else
		part_record_point->allow_groups = NULL;

	if (default_part.nodes) {
		part_record_point->nodes =
			(char *) xmalloc (strlen (default_part.nodes) + 1);	
		strcpy (part_record_point->nodes, default_part.nodes);
	}
	else
		part_record_point->nodes = NULL;

	if (list_append (part_list, part_record_point) == NULL)
		fatal ("create_part_record: unable to allocate memory");

	return part_record_point;
}


/* 
 * delete_part_record - delete record for partition with specified name
 * input: name - name of the desired node, delete all partitions if pointer is NULL 
 * output: return 0 on success, errno otherwise
 * global: part_list - global partition list
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

	error ("delete_part_record: attempt to delete non-existent partition %s",
		name);
	return ENOENT;
}


/* 
 * dump_all_part - dump all partition information to a buffer
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the data buffer is actually allocated by dump_part and the 
 *                     calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 *         returns 0 if no error, errno otherwise
 * global: part_list - global list of partition records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 */
int 
dump_all_part (char **buffer_ptr, int *buffer_size, time_t * update_time) {
	ListIterator part_record_iterator;	/* for iterating through part_record_list */
	struct part_record *part_record_point;	/* pointer to part_record */
	char *buffer;
	int buffer_offset, buffer_allocated, error_code, i, record_size;
	char out_line[BUF_SIZE];

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	buffer = NULL;
	buffer_offset = 0;
	buffer_allocated = 0;
	if (*update_time == last_part_update)
		return 0;

	part_record_iterator = list_iterator_create (part_list);		

	/* write haeader, version and time */
	sprintf (out_line, HEAD_FORMAT, (unsigned long) last_part_update,
		 PART_STRUCT_VERSION);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	/* write partition records */
	while (part_record_point =
	       (struct part_record *) list_next (part_record_iterator)) {
		if (part_record_point->magic != PART_MAGIC)
			fatal ("dump_part: data integrity is bad");

		error_code = dump_part(part_record_point, out_line, BUF_SIZE);
		if (error_code != 0) continue;

		if (write_buffer
		    (&buffer, &buffer_offset, &buffer_allocated, out_line))
			goto cleanup;
	}			

	list_iterator_destroy (part_record_iterator);
	xrealloc (buffer, buffer_offset);

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	*update_time = last_part_update;
	return 0;

      cleanup:
	list_iterator_destroy (part_record_iterator);
	if (buffer)
		xfree (buffer);
	return EINVAL;
}


/* 
 * dump_part - dump all configuration information about a specific partition to a buffer
 * input:  dump_part_ptr - pointer to partition for which information is requested
 *         out_line - buffer for partition information 
 *         out_line_size - byte size of out_line
 * output: out_line - set to partition information values
 *         return 0 if no error, 1 if out_line buffer too small
 * NOTE: if you make any changes here be sure to increment the value of PART_STRUCT_VERSION
 *       and make the corresponding changes to load_part_config in api/partition_info.c
 */
int 
dump_part (struct part_record *part_record_point, char *out_line, int out_line_size) {
	char *nodes, *default_flag, *key, *state, *allow_groups, *shared;

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

	if ((strlen(PART_STRUCT_FORMAT) + strlen(part_record_point->name) + 20 +
	     strlen(nodes) + strlen(allow_groups)) > out_line_size) {
		error ("dump_node: buffer too small for node %s", part_record_point->name);
		return 1;
	}

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

	return 0;
}


/* 
 * init_part_conf - initialize the default partition configuration values and create 
 *	a (global) partition list. 
 * this should be called before creating any partition entries.
 * output: return value - 0 if no error, otherwise an error code
 * global: default_part - default partition values
 *         part_list - global partition list
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
		xfree (default_part.nodes);
	default_part.nodes = (char *) NULL;
	if (default_part.allow_groups)
		xfree (default_part.allow_groups);
	default_part.allow_groups = (char *) NULL;
	if (default_part.node_bitmap)
		bit_free (default_part.node_bitmap);
	default_part.node_bitmap = (bitstr_t *) NULL;

	if (part_list)		/* delete defunct partitions */
		(void) delete_part_record (NULL);
	else
		part_list = list_create (&list_delete_part);

	if (part_list == NULL) 
		fatal ("init_part_conf: list_create can not allocate memory");
		

	strcpy (default_part_name, "");
	default_part_loc = (struct part_record *) NULL;

	return 0;
}

/*
 * list_delete_part - delete an entry from the global partition list, see 
 *	common/list.h for documentation
 * global: node_record_count - count of nodes in the system
 *         node_record_table_ptr - pointer to global node table
 */
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
		xfree (part_record_point->allow_groups);
	if (part_record_point->nodes)
		xfree (part_record_point->nodes);
	if (part_record_point->node_bitmap)
		bit_free (part_record_point->node_bitmap);
	xfree (part_entry);
}


/* list_find_part - find an entry in the partition list, see list.h for documentation 
 *	key is partition name or "universal_key" for all partitions 
 * global- part_list - the global partition list
 */
int list_find_part (void *part_entry, void *key) {
	struct part_record *part_record_point;	/* pointer to part_record */

	if (strcmp (key, "universal_key") == 0)
		return 1;
	part_record_point = (struct part_record *) part_entry;
	if (strncmp (part_record_point->name, (char *) key, MAX_NAME_LEN) == 0)
		return 1;
	return 0;
}


/* part_lock - lock the partition information 
 * global: part_mutex - semaphore for the partition table
 */
void part_lock () {
	int error_code;
	error_code = pthread_mutex_lock (&part_mutex);
	if (error_code)
		fatal ("part_lock: pthread_mutex_lock error %d", error_code);
	
}


/* part_unlock - unlock the partition information 
 * global: part_mutex - semaphore for the partition table
 */
void part_unlock () {
	int error_code;
	error_code = pthread_mutex_unlock (&part_mutex);
	if (error_code)
		fatal ("part_unlock: pthread_mutex_unlock error %d", error_code);
}


/* 
 * update_part - update a partition's configuration data
 * input: partition_name - partition's name
 *        spec - the updates to the partition's specification 
 * output: return - 0 if no error, otherwise an error code
 * global: part_list - list of partition entries
 * NOTE: the contents of spec are overwritten by white space
 */
int update_part (char *partition_name, char *spec) {
	int error_code;
	struct part_record *part_ptr;
	int max_time_val, max_nodes_val, key_val, state_val, shared_val, default_val;
	char *allow_groups, *nodes;
	int bad_index, i;

	if ((strcmp (partition_name, "DEFAULT") == 0) ||
	    (strlen (partition_name) >= MAX_NAME_LEN)) {
		error ("update_part: invalid partition name  %s", partition_name);
		return EINVAL;
	}			

	allow_groups = nodes = NULL;
	part_ptr =
		list_find_first (part_list, &list_find_part, partition_name);
	if (part_ptr == 0) {
		error ("update_part: partition %s does not exist, being created.",
			partition_name);
		part_ptr = create_part_record (&error_code);
		if (error_code)
			return error_code;
		strcpy(part_ptr->name, partition_name);
	}			

	max_time_val = NO_VAL;
	error_code = load_integer (&max_time_val, "MaxTime=", spec);
	if (error_code)
		return error_code;

	max_nodes_val = NO_VAL;
	error_code = load_integer (&max_nodes_val, "MaxNodes=", spec);
	if (error_code)
		return error_code;

	key_val = NO_VAL;
	error_code = load_integer (&key_val, "Key=NO", spec);
	if (error_code)
		return error_code;
	if (key_val == 1)
		key_val = 0;
	error_code = load_integer (&key_val, "Key=YES", spec);
	if (error_code)
		return error_code;

	state_val = NO_VAL;
	error_code = load_integer (&state_val, "State=DOWN", spec);
	if (error_code)
		return error_code;
	if (state_val == 1)
		state_val = 0;
	error_code = load_integer (&state_val, "State=UP", spec);
	if (error_code)
		return error_code;

	shared_val = NO_VAL;
	error_code = load_integer (&shared_val, "Shared=NO", spec);
	if (error_code)
		return error_code;
	if (shared_val == 1)
		shared_val = 0;
	error_code = load_integer (&shared_val, "Shared=FORCE", spec);
	if (error_code)
		return error_code;
	if (shared_val == 1)
		shared_val = 2;
	error_code = load_integer (&shared_val, "Shared=YES", spec);
	if (error_code)
		return error_code;

	default_val = NO_VAL;
	error_code = load_integer (&default_val, "Default=YES", spec);
	if (error_code)
		return error_code;

	allow_groups = NULL;
	error_code = load_string (&allow_groups, "AllowGroups=", spec);
	if (error_code)
		return error_code;

	nodes = NULL;
	error_code = load_string (&nodes, "Nodes=", spec);
	if (error_code) {
		if (allow_groups) xfree(allow_groups);
		return error_code;
	}

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
		error ("update_part: ignored partition %s update specification: %s",
			partition_name, &spec[bad_index]);
		if (allow_groups) xfree(allow_groups);
		if (nodes) xfree(nodes);
		return EINVAL;
	}			

	last_part_update = time (NULL);
	if (max_time_val != NO_VAL) {
		info ("update_part: setting max_time to %d for partition %s",
			max_time_val, partition_name);
		part_ptr->max_time = max_time_val;
	}			

	if (max_nodes_val != NO_VAL) {
		info ("update_part: setting max_nodes to %d for partition %s",
			max_nodes_val, partition_name);
		part_ptr->max_nodes = max_nodes_val;
	}			

	if (key_val != NO_VAL) {
		info ("update_part: setting key to %d for partition %s",
			key_val, partition_name);
		part_ptr->key = key_val;
	}			

	if (state_val != NO_VAL) {
		info ("update_part: setting state_up to %d for partition %s",
			state_val, partition_name);
		part_ptr->state_up = state_val;
	}			

	if (shared_val != NO_VAL) {
		info ("update_part: setting shared to %d for partition %s",
			shared_val, partition_name);
		part_ptr->shared = shared_val;
	}			

	if (default_val == 1) {
		info ("update_part: changing default partition from %s to %s",
			default_part_name, partition_name);
		strcpy (default_part_name, partition_name);
		default_part_loc = part_ptr;
	}			

	if (allow_groups != NULL) {
		if (part_ptr->allow_groups)
			xfree (part_ptr->allow_groups);
		part_ptr->allow_groups = allow_groups;
		info ("update_part: setting allow_groups to %s for partition %s",
			allow_groups, partition_name);
	}			

	if (nodes != NULL) {
		if (part_ptr->nodes)
			xfree (part_ptr->nodes);
		part_ptr->nodes = nodes;
		info ("update_part: setting nodes to %s for partition %s",
			nodes, partition_name);
		/* now we need to update total_cpus, total_nodes, and node_bitmap */
		error_code = build_part_bitmap (part_ptr);
		if (error_code)
			return error_code;
	}			

	return error_code;
}
