/*****************************************************************************\
 *  partition_mgr.c - manage the partition information of slurm
 *	Note: there is a global partition list (part_list) and
 *	time stamp (last_part_update)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov> et. al.
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <src/common/hostlist.h>
#include <src/common/list.h>
#include <src/common/xstring.h>
#include <src/slurmctld/locks.h>
#include <src/slurmctld/slurmctld.h>

#define BUF_SIZE 1024

struct part_record default_part;	/* default configuration values */
List part_list = NULL;			/* partition list */
char default_part_name[MAX_NAME_LEN];	/* name of default partition */
struct part_record *default_part_loc = NULL;	/* location of default partition */
time_t last_part_update;		/* time of last update to partition records */

int	build_part_bitmap (struct part_record *part_record_point);
void	dump_part_state (struct part_record *part_record_point, void **buf_ptr, int *buf_len);
void	list_delete_part (void *part_entry);
int	list_find_part (void *part_entry, void *key);

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
	if (part_ptr->shared != SHARED_NO) {
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
int 
build_part_bitmap (struct part_record *part_record_point) 
{
	int i, update_nodes;
	char  *this_node_name ;
	bitstr_t *old_bitmap;
	struct node_record *node_record_point;	/* pointer to node_record */
	hostlist_t host_list;

	part_record_point->total_cpus = 0;
	part_record_point->total_nodes = 0;

	if (part_record_point->node_bitmap == NULL) {
		part_record_point->node_bitmap = (bitstr_t *) bit_alloc (node_record_count);
		if (part_record_point->node_bitmap == NULL)
			fatal("bit_alloc memory allocation failure");
		old_bitmap = NULL;
	}
	else {
		old_bitmap = bit_copy (part_record_point->node_bitmap);
		bit_nclear (part_record_point->node_bitmap, 0, node_record_count-1);
	}

	if (part_record_point->nodes == NULL) {		/* no nodes in partition */
		if (old_bitmap)				/* leave with empty bitmap */
			bit_free (old_bitmap);
		return 0;
	}

	if ( (host_list = hostlist_create (part_record_point->nodes)) == NULL) {
		if (old_bitmap)
			bit_free (old_bitmap);
		error ("hostlist_create errno %d on %s", errno, part_record_point->nodes);
		return ESLURM_INVALID_NODE_NAME;
	}

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		node_record_point = find_node_record (this_node_name);
		if (node_record_point == NULL) {
			error ("build_part_bitmap: invalid node specified %s", this_node_name);
			free (this_node_name);
			if (old_bitmap)
				bit_free (old_bitmap);
			hostlist_destroy (host_list);
			return ESLURM_INVALID_NODE_NAME;
		}	
		part_record_point->total_nodes++;
		part_record_point->total_cpus += node_record_point->cpus;
		node_record_point->partition_ptr = part_record_point;
		if (old_bitmap) 
			bit_clear (old_bitmap,
			      (int) (node_record_point - node_record_table_ptr));
		bit_set (part_record_point->node_bitmap,
			    (int) (node_record_point - node_record_table_ptr));
		free (this_node_name);
	}
	hostlist_destroy (host_list);

	/* unlink nodes removed from the partition */
	if (old_bitmap) {
		update_nodes = 0;
		for (i = 0; i < node_record_count; i++) {
			if (bit_test (old_bitmap, i) == 0)
				continue;
			node_record_table_ptr[i].partition_ptr = NULL;
			update_nodes = 1;
		}
		bit_free (old_bitmap);
		if (update_nodes)
			last_node_update = time (NULL);
	}			

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
struct part_record * 
create_part_record (void) 
{
	struct part_record *part_record_point;

	last_part_update = time (NULL);

	part_record_point =
		(struct part_record *) xmalloc (sizeof (struct part_record));

	strcpy (part_record_point->name, "DEFAULT");
	part_record_point->max_time = default_part.max_time;
	part_record_point->max_nodes = default_part.max_nodes;
	part_record_point->root_only = default_part.root_only;
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
int 
delete_part_record (char *name) 
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

	error ("delete_part_record: attempt to delete non-existent partition %s", name);
	return ENOENT;
}


/* dump_all_part_state - save the state of all partitions to file */
int
dump_all_part_state ( void )
{
	ListIterator part_record_iterator;
	struct part_record *part_record_point;
	int buf_len, buffer_allocated, buffer_offset = 0, error_code = 0, log_fd;
	char *buffer;
	void *buf_ptr;
	char *old_file, *new_file, *reg_file;
	/* Locks: Read partition */
	slurmctld_lock_t part_read_lock = { READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	buffer_allocated = (BUF_SIZE*16);
	buffer = xmalloc(buffer_allocated);
	buf_ptr = buffer;
	buf_len = buffer_allocated;

	/* write header: time */
	pack32  ((uint32_t) time (NULL), &buf_ptr, &buf_len);

	/* write partition records to buffer */
	lock_slurmctld (part_read_lock);
	part_record_iterator = list_iterator_create (part_list);		
	while ((part_record_point = (struct part_record *) list_next (part_record_iterator))) {
		if (part_record_point->magic != PART_MAGIC)
			fatal ("pack_all_part: data integrity is bad");

		dump_part_state (part_record_point, &buf_ptr, &buf_len);
		if (buf_len > BUF_SIZE) 
			continue;
		buffer_allocated += (BUF_SIZE*16);
		buf_len += (BUF_SIZE*16);
		buffer_offset = (char *)buf_ptr - buffer;
		xrealloc(buffer, buffer_allocated);
		buf_ptr = buffer + buffer_offset;
	}			
	list_iterator_destroy (part_record_iterator);
	unlock_slurmctld (part_read_lock);

	/* write the buffer to file */
	old_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (old_file, "/part_state.old");
	reg_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (reg_file, "/part_state");
	new_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (new_file, "/part_state.new");
	lock_state_files ();
	log_fd = creat (new_file, 0600);
	if (log_fd == 0) {
		error ("Create error %d on file %s, can't save state", errno, new_file);
		error_code = errno;
	}
	else {
		buf_len = buffer_allocated - buf_len;
		if (write (log_fd, buffer, buf_len) != buf_len) {
			error ("Write error %d on file %s, can't save state", errno, new_file);
			error_code = errno;
		}
		close (log_fd);
	}
	if (error_code) 
		(void) unlink (new_file);
	else {	/* file shuffle */
		(void) unlink (old_file);
		(void) link (reg_file, old_file);
		(void) unlink (reg_file);
		(void) link (new_file, reg_file);
		(void) unlink (new_file);
	}
	xfree (old_file);
	xfree (reg_file);
	xfree (new_file);
	unlock_state_files ();

	xfree (buffer);
	return 0;
}

/*
 * dump_part_state - dump the state of a specific partition to a buffer
 * input:  part_record_point - pointer to partition for which information is requested
 *	buf_ptr - buffer for node information 
 *	buf_len - byte size of buffer
 * output: buf_ptr - advanced to end of data written
 *	buf_len - byte size remaining in buffer
 */
void 
dump_part_state (struct part_record *part_record_point, void **buf_ptr, int *buf_len) 
{
	uint16_t default_part_flag;

	if (default_part_loc == part_record_point)
		default_part_flag = 1;
	else
		default_part_flag = 0;

	packstr (part_record_point->name, buf_ptr, buf_len);
	pack32  (part_record_point->max_time, buf_ptr, buf_len);
	pack32  (part_record_point->max_nodes, buf_ptr, buf_len);

	pack16  (default_part_flag, buf_ptr, buf_len);
	pack16  ((uint16_t)part_record_point->root_only, buf_ptr, buf_len);
	pack16  ((uint16_t)part_record_point->shared, buf_ptr, buf_len);

	pack16  ((uint16_t)part_record_point->state_up, buf_ptr, buf_len);
	packstr (part_record_point->allow_groups, buf_ptr, buf_len);
	packstr (part_record_point->nodes, buf_ptr, buf_len);
}

/*
 * load_part_state - load the partition state from file, recover from slurmctld restart.
 *	execute this after loading the configuration file data.
 */
int
load_part_state ( void )
{
	char *part_name, *allow_groups, *nodes, *state_file;
	uint32_t time, max_time, max_nodes;
	uint16_t name_len, def_part_flag, root_only, shared, state_up;
	struct part_record *part_ptr;
	uint32_t buffer_size;
	int buffer_allocated, buffer_used = 0, error_code = 0;
	int state_fd;
	char *buffer;
	void *buf_ptr;

	/* read the file */
	state_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (state_file, "/part_state");
	lock_state_files ();
	state_fd = open (state_file, O_RDONLY);
	if (state_fd < 0) {
		info ("No partition state file (%s) to recover", state_file);
		error_code = ENOENT;
	}
	else {
		buffer_allocated = BUF_SIZE;
		buffer = xmalloc(buffer_allocated);
		buf_ptr = buffer;
		while ((buffer_used = read (state_fd, buf_ptr, BUF_SIZE)) == BUF_SIZE) {
			buffer_size += buffer_used;
			buffer_allocated += BUF_SIZE;
			xrealloc(buffer, buffer_allocated);
			buf_ptr = (void *) (buffer + buffer_size);
		}
		buf_ptr = (void *) buffer;
		buffer_size += buffer_used;
		close (state_fd);
		if (buffer_used < 0) 
			error ("Read error %d on %s", errno, state_file);
	}
	xfree (state_file);
	unlock_state_files ();

	if (buffer_size > sizeof (uint32_t))
		unpack32 (&time, &buf_ptr, &buffer_size);

	while (buffer_size > 0) {
		safe_unpackstr_xmalloc (&part_name, &name_len, &buf_ptr, &buffer_size);
		safe_unpack32 (&max_time, &buf_ptr, &buffer_size);
		safe_unpack32 (&max_nodes, &buf_ptr, &buffer_size);
		safe_unpack16 (&def_part_flag, &buf_ptr, &buffer_size);
		safe_unpack16 (&root_only, &buf_ptr, &buffer_size);
		safe_unpack16 (&shared, &buf_ptr, &buffer_size);
		safe_unpack16 (&state_up, &buf_ptr, &buffer_size);
		safe_unpackstr_xmalloc (&allow_groups, &name_len, &buf_ptr, &buffer_size);
		safe_unpackstr_xmalloc (&nodes, &name_len, &buf_ptr, &buffer_size);

		/* find record and perform update */
		part_ptr = list_find_first (part_list, &list_find_part, part_name);

		if (part_ptr == NULL) {
			info ("load_part_state: partition %s removed from configuration file.", part_name);
		}
		else {
			part_ptr->max_time = max_time;
			part_ptr->max_nodes = max_nodes;
			if (def_part_flag) {
				strcpy (default_part_name, part_name);
				default_part_loc = part_ptr;	
			}
			part_ptr->root_only = root_only;
			part_ptr->shared = shared;
			part_ptr->state_up = state_up;
			if (part_ptr->allow_groups)
				xfree (part_ptr->allow_groups);
			part_ptr->allow_groups = allow_groups;
			if (part_ptr->nodes)
				xfree (part_ptr->nodes);
			part_ptr->nodes = nodes;
		}		

		if (part_name)
			xfree (part_name);
	}
	return error_code;
}

/* 
 * find_part_record - find a record for partition with specified name
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
int 
init_part_conf () 
{
	last_part_update = time (NULL);

	strcpy (default_part.name, "DEFAULT");
	default_part.allow_groups = (char *) NULL;
	default_part.max_time = INFINITE;
	default_part.max_nodes = INFINITE;
	default_part.root_only = 0;
	default_part.state_up = 1;
	default_part.shared = SHARED_NO;
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
 * global: part_list - global list of partition records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change PART_STRUCT_VERSION in common/slurmlib.h whenever the format changes
 * NOTE: change slurm_load_part() in api/part_info.c whenever the data format changes
 */
void 
pack_all_part (char **buffer_ptr, int *buffer_size, time_t * update_time) 
{
	ListIterator part_record_iterator;
	struct part_record *part_record_point;
	int buf_len, buffer_allocated, buffer_offset = 0;
	char *buffer;
	void *buf_ptr;
	int parts_packed;
	/* Locks: Read partition */
	slurmctld_lock_t part_read_lock = { NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	if (*update_time == last_part_update)
		return;

	lock_slurmctld (part_read_lock);
	buffer_allocated = (BUF_SIZE*16);
	buffer = xmalloc(buffer_allocated);
	buf_ptr = buffer;
	buf_len = buffer_allocated;

	part_record_iterator = list_iterator_create (part_list);		

	/* write haeader: version and time */
	parts_packed = 0 ;
	pack32  ((uint32_t) parts_packed, &buf_ptr, &buf_len);
	pack32  ((uint32_t) last_part_update, &buf_ptr, &buf_len);

	/* write individual partition records */
	while ((part_record_point = 
		(struct part_record *) list_next (part_record_iterator))) {
		if (part_record_point->magic != PART_MAGIC)
			fatal ("pack_all_part: data integrity is bad");

		pack_part(part_record_point, &buf_ptr, &buf_len);
		parts_packed ++ ;
		if (buf_len > BUF_SIZE) 
			continue;
		buffer_allocated += (BUF_SIZE*16);
		buf_len += (BUF_SIZE*16);
		buffer_offset = (char *)buf_ptr - buffer;
		xrealloc(buffer, buffer_allocated);
		buf_ptr = buffer + buffer_offset;
	}			

	list_iterator_destroy (part_record_iterator);
	unlock_slurmctld (part_read_lock);
	buffer_offset = (char *)buf_ptr - buffer;
	xrealloc (buffer, buffer_offset);

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	*update_time = last_part_update;

	/* put in the real record count in the message body header */
        buf_ptr = buffer;
        buf_len = buffer_allocated;
        pack32  ((uint32_t) parts_packed, &buf_ptr, &buf_len);
}


/* 
 * pack_part - dump all configuration information about a specific partition in 
 *	machine independent form (for network transmission)
 * input:  dump_part_ptr - pointer to partition for which information is requested
 *	buf_ptr - buffer for node information 
 *	buf_len - byte size of buffer
 * output: buf_ptr - advanced to end of data written
 *	buf_len - byte size remaining in buffer
 * global: default_part_loc - pointer to the default partition
 * NOTE: if you make any changes here be sure to increment the value of PART_STRUCT_VERSION
 *	and make the corresponding changes to load_part_config in api/partition_info.c
 */
void 
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
	pack16  ((uint16_t)part_record_point->root_only, buf_ptr, buf_len);
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
}


/* 
 * update_part - update a partition's configuration data
 * global: part_list - list of partition entries
 *	last_part_update - update time of partition records
 */
int 
update_part (update_part_msg_t * part_desc ) 
{
	int error_code, i;
	struct part_record *part_ptr;
	/* Locks: Read node, write partition */
	slurmctld_lock_t part_write_lock = { NO_LOCK, NO_LOCK, READ_LOCK, WRITE_LOCK };

	if ((part_desc -> name == NULL ) ||
			(strlen (part_desc->name ) >= MAX_NAME_LEN)) {
		error ("update_part: invalid partition name  %s", part_desc->name);
		return ESLURM_INVALID_PARTITION_NAME ;
	}			

	error_code = 0;
	lock_slurmctld (part_write_lock);
	part_ptr = list_find_first (part_list, &list_find_part, part_desc->name);

	if (part_ptr == NULL) {
		error ("update_part: partition %s does not exist, being created.",
				part_desc->name);
		part_ptr = create_part_record ();
		strcpy(part_ptr->name, part_desc->name );
	}			

	last_part_update = time (NULL);
	if (part_desc->max_time != NO_VAL) {
		info ("update_part: setting max_time to %d for partition %s",
				part_desc->max_time, part_desc->name);
		part_ptr->max_time = part_desc->max_time;
	}			

	if (part_desc->max_nodes != NO_VAL) {
		info ("update_part: setting max_nodes to %d for partition %s",
				part_desc->max_nodes, part_desc->name);
		part_ptr->max_nodes = part_desc->max_nodes;
	}			

	if (part_desc->root_only != (uint16_t) NO_VAL) {
		info ("update_part: setting root_only to %d for partition %s",
				part_desc->root_only, part_desc->name);
		part_ptr->root_only = part_desc->root_only;
	}			

	if (part_desc->state_up != (uint16_t) NO_VAL) {
		info ("update_part: setting state_up to %d for partition %s",
				part_desc->state_up, part_desc->name);
		part_ptr->state_up = part_desc->state_up;
	}			

	if (part_desc->shared != (uint16_t) NO_VAL) {
		info ("update_part: setting shared to %d for partition %s",
				part_desc->shared, part_desc->name);
		part_ptr->shared = part_desc->shared;
	}			

	if ((part_desc->default_part == 1) && 
	     (strcmp(default_part_name, part_desc->name) != 0)) {
		info ("update_part: changing default partition from %s to %s",
				default_part_name, part_desc->name);
		strcpy (default_part_name, part_desc->name);
		default_part_loc = part_ptr;
	}			

	if (part_desc->allow_groups != NULL) {
		if (part_ptr->allow_groups)
			xfree (part_ptr->allow_groups);
		i = strlen(part_desc->allow_groups) + 1;
		part_ptr->allow_groups = xmalloc(i);
		strcpy ( part_ptr->allow_groups , part_desc->allow_groups ) ;
		info ("update_part: setting allow_groups to %s for partition %s",
				part_desc->allow_groups, part_desc->name);
	}			

	if (part_desc->nodes != NULL) {
		char *backup_node_list;
		backup_node_list = part_ptr->nodes;
		i = strlen(part_desc->nodes) + 1;
		part_ptr->nodes = xmalloc(i);
		strcpy ( part_ptr->nodes , part_desc->nodes ) ;

		error_code = build_part_bitmap (part_ptr);
		if (error_code) {
			if (part_ptr->nodes)
				xfree (part_ptr->nodes);
			part_ptr->nodes = backup_node_list;
		}
		else {
			info ("update_part: setting nodes to %s for partition %s",
				part_desc->nodes, part_desc->name);
			if (backup_node_list)
				xfree(backup_node_list);
		}
	}
			
	unlock_slurmctld (part_write_lock);
	return error_code;
}
