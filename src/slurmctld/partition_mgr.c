/* 
 * partition_mgr.c - manage the partition information of slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "slurmctld.h"

#define BUF_SIZE 1024
#define SEPCHARS " \n\t"

struct part_record default_part;	/* default configuration values */
List part_list = NULL;			/* partition list */
char default_part_name[MAX_NAME_LEN];	/* name of default partition */
struct part_record *default_part_loc = NULL;	/* location of default partition */
time_t last_part_update;		/* time of last update to partition records */
static pthread_mutex_t part_mutex = PTHREAD_MUTEX_INITIALIZER;	/* lock for partition info */

int build_part_bitmap (struct part_record *part_record_point);
void list_delete_part (void *part_entry);
int list_find_part (void *part_entry, void *key);

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int 
main (int argc, char *argv[]) 
{
	int error_code, error_count;
	time_t update_time;
	struct part_record *part_ptr;
	char *dump;
	int dump_size;
	char update_spec[] =
		"MaxTime=34 MaxNodes=56 Key=NO State=DOWN Shared=FORCE";
	log_options_t opts = LOG_OPTS_STDERR_ONLY;

	error_count = 0;
	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	error_code = init_node_conf ();
	if (error_code) {
		printf ("init_node_conf error %d\n", error_code);
		error_count++;
	}
	error_code = init_part_conf ();
	if (error_code) {
		printf ("init_part_conf error %d\n", error_code);
		error_count++;
	}
	default_part.max_time = 223344;
	default_part.max_nodes = 556677;
	default_part.total_nodes = 4;
	default_part.total_cpus = 16;
	default_part.key = 1;
	node_record_count = 8;

	printf ("create some partitions and test defaults\n");
	part_ptr = create_part_record ();
	if (part_ptr->max_time != 223344) {
		printf ("ERROR: partition default max_time not set\n");
		error_count++;
	}
	if (part_ptr->max_nodes != 556677) {
		printf ("ERROR: partition default max_nodes not set\n");
		error_count++;
	}
	if (part_ptr->total_nodes != 4) {
		printf ("ERROR: partition default total_nodes not set\n");
		error_count++;
	}
	if (part_ptr->total_cpus != 16) {
		printf ("ERROR: partition default max_nodes not set\n");
		error_count++;
	}
	if (part_ptr->key != 1) {
		printf ("ERROR: partition default key not set\n");
		error_count++;
	}
	if (part_ptr->state_up != 1) {
		printf ("ERROR: partition default state_up not set\n");
		error_count++;
	}
	if (part_ptr->shared != 0) {
		printf ("ERROR: partition default shared not set\n");
		error_count++;
	}
	strcpy (part_ptr->name, "interactive");
	part_ptr->nodes = "lx[01-04]";
	part_ptr->allow_groups = "students";
	part_ptr->node_bitmap = (bitstr_t *) bit_alloc(20);
	bit_nset(part_ptr->node_bitmap, 2, 5);

	part_ptr = create_part_record ();
	strcpy (part_ptr->name, "batch");
	part_ptr = create_part_record ();
	strcpy (part_ptr->name, "class");

	update_time = (time_t) 0;
	error_code = pack_all_part (&dump, &dump_size, &update_time);
	if (error_code) {
		printf ("ERROR: pack_all_part error %d\n", error_code);
		error_count++;
	}
	xfree (dump);

	error_code = update_part ("batch", update_spec);
	if (error_code) {
		printf ("ERROR: update_part error %d\n", error_code);
		error_count++;
	}

	part_ptr = find_part_record ("batch");
	if (part_ptr == NULL) {
		printf ("ERROR: list_find failure\n");
		error_count++;
	}
	if (part_ptr->max_time != 34) {
		printf ("ERROR: update_part max_time not reset\n");
		error_count++;
	}
	if (part_ptr->max_nodes != 56) {
		printf ("ERROR: update_part max_nodes not reset\n");
		error_count++;
	}
	if (part_ptr->key != 0) {
		printf ("ERROR: update_part key not reset\n");
		error_count++;
	}
	if (part_ptr->state_up != 0) {
		printf ("ERROR: update_part state_up not set\n");
		error_count++;
	}
	if (part_ptr->shared != 2) {
		printf ("ERROR: update_part shared not set\n");
		error_count++;
	}

	node_record_count = 0;	/* delete_part_record dies if node count is bad */
	error_code = delete_part_record ("batch");
	if (error_code != 0) {
		printf ("delete_part_record error1 %d\n", error_code);
		error_count++;
	}
	printf ("NOTE: we expect delete_part_record to report not finding a record for batch\n");
	error_code = delete_part_record ("batch");
	if (error_code != ENOENT) {
		printf ("ERROR: delete_part_record error2 %d\n", error_code);
		error_count++;
	}

	exit (error_count);
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
int build_part_bitmap (struct part_record *part_record_point) 
{
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
 * output: returns a pointer to the record or NULL if error
 * global: default_part - default partition parameters
 *         part_list - global partition list
 * NOTE: the record's values are initialized to those of default_part
 * NOTE: allocates memory that should be xfreed with delete_part_record
 */
struct part_record * create_part_record (void) 
{
	struct part_record *part_record_point;

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
int delete_part_record (char *name) 
{
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
 * find_part_record - find a record for partition with specified name,
 * input: name - name of the desired partition 
 * output: return pointer to node partition or null if not found
 * global: part_list - global partition list
 */
struct part_record *
find_part_record (char *name){
	return list_find_first (part_list, &list_find_part, name);
}


/* 
 * init_part_conf - initialize the default partition configuration values and create 
 *	a (global) partition list. 
 * this should be called before creating any partition entries.
 * output: return value - 0 if no error, otherwise an error code
 * global: default_part - default partition values
 *         part_list - global partition list
 */
int init_part_conf () 
{
	last_part_update = time (NULL);

	strcpy (default_part.name, "DEFAULT");
	default_part.allow_groups = (char *) NULL;
	default_part.max_time = INFINITE;
	default_part.max_nodes = INFINITE;
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
 * list_delete_part - delete an entry from the global partition list, 
 *	see common/list.h for documentation
 * global: node_record_count - count of nodes in the system
 *         node_record_table_ptr - pointer to global node table
 */
void 
list_delete_part (void *part_entry) 
{
	struct part_record *part_record_point;	/* pointer to part_record */
	int i;

	part_record_point = (struct part_record *) part_entry;
	for (i = 0; i < node_record_count; i++) {
		if (node_record_table_ptr[i].partition_ptr != part_record_point)
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


/*
 * list_find_part - find an entry in the partition list, see common/list.h for documentation,
 *	key is partition name or "universal_key" for all partitions 
 * global- part_list - the global partition list
 */
int 
list_find_part (void *part_entry, void *key) 
{
	if (strcmp (key, "universal_key") == 0)
		return 1;

	if (strncmp (((struct part_record *) part_entry)->name, 
	    (char *) key, MAX_NAME_LEN) == 0)
		return 1;

	return 0;
}


/* 
 * pack_all_part - dump all partition information for all partitions in 
 *	machine independent form (for network transmission)
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 *         returns 0 if no error, errno otherwise
 * global: part_list - global list of partition records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change PART_STRUCT_VERSION in common/slurmlib.h whenever the format changes
 * NOTE: change slurm_load_part() in api/part_info.c whenever the data format changes
 */
int 
pack_all_part (char **buffer_ptr, int *buffer_size, time_t * update_time) 
{
	ListIterator part_record_iterator;
	struct part_record *part_record_point;
	int buf_len, buffer_allocated, buffer_offset = 0, error_code;
	char *buffer;
	void *buf_ptr;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	if (*update_time == last_part_update)
		return 0;

	buffer_allocated = (BUF_SIZE*16);
	buffer = xmalloc(buffer_allocated);
	buf_ptr = buffer;
	buf_len = buffer_allocated;

	part_record_iterator = list_iterator_create (part_list);		

	/* write haeader: version and time */
	pack32  ((uint32_t) PART_STRUCT_VERSION, &buf_ptr, &buf_len);
	pack32  ((uint32_t) last_part_update, &buf_ptr, &buf_len);

	/* write individual partition records */
	while ((part_record_point = 
		(struct part_record *) list_next (part_record_iterator))) {
		if (part_record_point->magic != PART_MAGIC)
			fatal ("pack_all_part: data integrity is bad");

		error_code = pack_part(part_record_point, &buf_ptr, &buf_len);
		if (error_code != 0) continue;
		if (buf_len > BUF_SIZE) 
			continue;
		buffer_allocated += (BUF_SIZE*16);
		buf_len += (BUF_SIZE*16);
		buffer_offset = (char *)buf_ptr - buffer;
		xrealloc(buffer, buffer_allocated);
		buf_ptr = buffer + buffer_offset;
	}			

	list_iterator_destroy (part_record_iterator);
	buffer_offset = (char *)buf_ptr - buffer;
	xrealloc (buffer, buffer_offset);

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	*update_time = last_part_update;
	return 0;
}


/* 
 * pack_part - dump all configuration information about a specific partition in 
 *	machine independent form (for network transmission)
 * input:  dump_part_ptr - pointer to partition for which information is requested
 *	buf_ptr - buffer for node information 
 *	buf_len - byte size of buffer
 * output: buf_ptr - advanced to end of data written
 *	buf_len - byte size remaining in buffer
 *	return 0 if no error, 1 if buffer too small
 * global: default_part_loc - pointer to the default partition
 * NOTE: if you make any changes here be sure to increment the value of PART_STRUCT_VERSION
 *	and make the corresponding changes to load_part_config in api/partition_info.c
 */
int 
pack_part (struct part_record *part_record_point, void **buf_ptr, int *buf_len) 
{
	uint16_t default_part_flag;
	char node_inx_ptr[BUF_SIZE];

	if (default_part_loc == part_record_point)
		default_part_flag = 1;
	else
		default_part_flag = 0;

	packstr (part_record_point->name, buf_ptr, buf_len);
	pack32  (part_record_point->max_time, buf_ptr, buf_len);
	pack32  (part_record_point->max_nodes, buf_ptr, buf_len);
	pack32  (part_record_point->total_nodes, buf_ptr, buf_len);

	pack32  (part_record_point->total_cpus, buf_ptr, buf_len);
	pack16  (default_part_flag, buf_ptr, buf_len);
	pack16  ((uint16_t)part_record_point->key, buf_ptr, buf_len);
	pack16  ((uint16_t)part_record_point->shared, buf_ptr, buf_len);

	pack16  ((uint16_t)part_record_point->state_up, buf_ptr, buf_len);
	packstr (part_record_point->allow_groups, buf_ptr, buf_len);
	packstr (part_record_point->nodes, buf_ptr, buf_len);
	if (part_record_point->node_bitmap) {
		bit_fmt (node_inx_ptr, BUF_SIZE, part_record_point->node_bitmap);
		packstr (node_inx_ptr, buf_ptr, buf_len);
	}
	else
		packstr ("", buf_ptr, buf_len);


	return 0;
}


/* part_lock - lock the partition information 
 * global: part_mutex - semaphore for the partition table
 */
void 
part_lock () 
{
	int error_code;
	error_code = pthread_mutex_lock (&part_mutex);
	if (error_code)
		fatal ("part_lock: pthread_mutex_lock error %d", error_code);
	
}


/* part_unlock - unlock the partition information 
 * global: part_mutex - semaphore for the partition table
 */
void 
part_unlock () 
{
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
int 
update_part (char *partition_name, char *spec) 
{
	int error_code;
	struct part_record *part_ptr;
	int max_time_val, max_nodes_val, key_val, state_val, shared_val, default_val;
	char *allow_groups, *default_str, *key_str, *nodes, *shared_str, *state_str;
	int bad_index, i;

	if ((strcmp (partition_name, "DEFAULT") == 0) ||
	    (strlen (partition_name) >= MAX_NAME_LEN)) {
		error ("update_part: invalid partition name  %s", partition_name);
		return EINVAL;
	}			

	allow_groups = nodes = NULL;
	part_ptr = list_find_first (part_list, &list_find_part, partition_name);
	if (part_ptr == NULL) {
		error ("update_part: partition %s does not exist, being created.",
			partition_name);
		part_ptr = create_part_record ();
		strcpy(part_ptr->name, partition_name);
	}			

	default_val = key_val = max_time_val = max_nodes_val = NO_VAL;
	shared_val = state_val = NO_VAL;
	allow_groups = default_str = key_str = nodes = shared_str = NULL;
	state_str = NULL;
	error_code = slurm_parser(spec,
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
		if (max_time_val == -1)
			part_ptr->max_time = INFINITE;
		else
			part_ptr->max_time = max_time_val;
	}			

	if (max_nodes_val != NO_VAL) {
		info ("update_part: setting max_nodes to %d for partition %s",
			max_nodes_val, partition_name);
		if (max_nodes_val == -1)
			part_ptr->max_nodes = INFINITE;
		else
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

      cleanup:
	if (allow_groups)
		xfree(allow_groups);
	if (default_str)
		xfree(default_str);
	if (key_str)
		xfree(key_str);
	if (nodes)
		xfree(nodes);
	if (shared_str)
		xfree(shared_str);
	if (state_str)
		xfree(state_str);
	return error_code;
}
