/*****************************************************************************\
 *  node_mgr.c - manage the node records of slurm
 *	Note: there is a global node table (node_record_table_ptr), its 
 *	hash table (hash_table), time stamp (last_node_update) and 
 *	configuration list (config_list)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov> et. al.
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
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <src/common/hostlist.h>
#include <src/common/xstring.h>
#include <src/slurmctld/locks.h>
#include <src/slurmctld/slurmctld.h>

#define BUF_SIZE 	4096

List config_list = NULL;		/* list of config_record entries */
struct node_record *node_record_table_ptr = NULL;	/* location of the node records */
int *hash_table = NULL;			/* table of hashed indicies into node_record */
struct config_record default_config_record;
struct node_record default_node_record;
time_t last_bitmap_update = (time_t) NULL;	/* time of last node creation or deletion */
time_t last_node_update = (time_t) NULL;	/* time of last update to node records */

bitstr_t *up_node_bitmap = NULL;	/* bitmap of nodes are up */
bitstr_t *idle_node_bitmap = NULL;	/* bitmap of nodes are idle */

int	delete_config_record ();
void	dump_hash ();
void 	dump_node_state (struct node_record *dump_node_ptr, void **buf_ptr, int *buf_len);
int	hash_index (char *name);
void 	pack_node (struct node_record *dump_node_ptr, void **buf_ptr, int *buf_len);
void	split_node_name (char *name, char *prefix, char *suffix, int *index, int *digits);


/*
 * bitmap2node_name - given a bitmap, build a list of comma separated node names.
 *	names may include regular expressions (e.g. "lx[01-10]")
 * input: bitmap - bitmap pointer
 * output: returns pointer to node list or NULL on error 
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
char *  
bitmap2node_name (bitstr_t *bitmap) 
{
	char *node_list_ptr;
	int node_list_size, i;
	char prefix[MAX_NAME_LEN], suffix[MAX_NAME_LEN];
	char format[MAX_NAME_LEN], temp[MAX_NAME_LEN];
	char last_prefix[MAX_NAME_LEN], last_suffix[MAX_NAME_LEN];
	int first_index = 0, last_index = 0, index, first_digits, last_digits;

	if (bitmap == NULL) {
		node_list_ptr = xmalloc (1);	/* returns ptr to "\0" */
		return node_list_ptr;
	}

	node_list_size = 0;
	node_list_ptr = xmalloc (BUF_SIZE);
	strcpy (node_list_ptr, "");

	strcpy (last_prefix, "");
	strcpy (last_suffix, "");
	for (i = 0; i < node_record_count; i++) {
		if (node_list_size <
		    (strlen (node_list_ptr) + MAX_NAME_LEN * 3)) {
			node_list_size += BUF_SIZE;
			xrealloc (node_list_ptr, node_list_size);
		}
		if (bit_test (bitmap, i) == 0)
			continue;
		split_node_name (node_record_table_ptr[i].name, prefix,
				 suffix, &index, &last_digits);
		if ((index == (last_index + 1)) &&	/* next in sequence */
		    (strcmp (last_prefix, prefix) == 0) &&
		    (strcmp (last_suffix, suffix) == 0)) {
			last_index = index;
			continue;
		}
		if ((strlen (last_prefix) != 0) ||	/* end of a sequence */
		    (strlen (last_suffix) != 0)) {
			if (strlen (node_list_ptr) > 0)
				strcat (node_list_ptr, ",");
			strcat (node_list_ptr, last_prefix);
			if (first_index != last_index)
				strcat (node_list_ptr, "[");
			strcpy (format, "%0");
			sprintf (&format[2], "%dd", first_digits);
			sprintf (temp, format, first_index);
			strcat (node_list_ptr, temp);
			if (first_index != last_index) {
				strcat (node_list_ptr, "-");
				strcpy (format, "%0");
				sprintf (&format[2], "%dd]", first_digits);
				sprintf (temp, format, last_index);
				strcat (node_list_ptr, temp);
			}	
			strcat (node_list_ptr, last_suffix);
			strcpy (last_prefix, "");
			strcpy (last_suffix, "");
		}
		if (index == NO_VAL) {
			if (strlen (node_list_ptr) > 0)
				strcat (node_list_ptr, ",");
			strcat (node_list_ptr, node_record_table_ptr[i].name);
		}
		else {
			first_digits = last_digits;
			strcpy (last_prefix, prefix);
			strcpy (last_suffix, suffix);
			first_index = last_index = index;
		}
	}

	if ((strlen (last_prefix) != 0) ||	/* end of a sequence */
	    (strlen (last_suffix) != 0)) {
		if (strlen (node_list_ptr) > 0)
			strcat (node_list_ptr, ",");
		strcat (node_list_ptr, last_prefix);
		if (first_index != last_index)
			strcat (node_list_ptr, "[");
		strcpy (format, "%0");
		sprintf (&format[2], "%dd", first_digits);
		sprintf (temp, format, first_index);
		strcat (node_list_ptr, temp);
		if (first_index != last_index) {
			strcat (node_list_ptr, "-");
			strcpy (format, "%0");
			sprintf (&format[2], "%dd]", first_digits);
			sprintf (temp, format, last_index);
			strcat (node_list_ptr, temp);
		}
		strcat (node_list_ptr, last_suffix);
	}
	xrealloc (node_list_ptr, strlen (node_list_ptr) + 1);
	return node_list_ptr;
}


/*
 * create_config_record - create a config_record entry and set is values to the defaults.
 *	each config record corresponds to a line in the slurm.conf file and typically 
 *	describes the configuration of a large number of nodes
 * output: returns pointer to the config_record
 * global: default_config_record - default configuration values
 * NOTE: memory allocated will remain in existence until delete_config_record() is called 
 *	to deletet all configuration records
 */
struct config_record * 
create_config_record (void) 
{
	struct config_record *config_point;

	last_node_update = time (NULL);
	config_point =
		(struct config_record *)
		xmalloc (sizeof (struct config_record));

	/* set default values */
	config_point->cpus = default_config_record.cpus;
	config_point->real_memory = default_config_record.real_memory;
	config_point->tmp_disk = default_config_record.tmp_disk;
	config_point->weight = default_config_record.weight;
	config_point->nodes = NULL;
	config_point->node_bitmap = NULL;
	config_point->magic = CONFIG_MAGIC;
	if (default_config_record.feature) {
		config_point->feature =
			(char *)
			xmalloc (strlen (default_config_record.feature) + 1);
		strcpy (config_point->feature, default_config_record.feature);
	}
	else
		config_point->feature = (char *) NULL;

	if (list_append(config_list, config_point) == NULL)
		fatal ("create_config_record: unable to allocate memory\n");

	return config_point;
}


/* 
 * create_node_record - create a node record and set its values to defaults
 * input: config_point - pointer to node's configuration information
 *        node_name - name of the node
 * output: returns a pointer to the record or NULL if error
 * global: default_node_record - default node values
 * NOTE: the record's values are initialized to those of default_node_record, node_name and 
 *	config_point's cpus, real_memory, and tmp_disk values
 * NOTE: allocates memory at node_record_table_ptr that must be xfreed when the 
 *	global node table is no longer required
 */
struct node_record * 
create_node_record (struct config_record *config_point, char *node_name) 
{
	struct node_record *node_record_point;
	int old_buffer_size, new_buffer_size;

	last_node_update = time (NULL);
	if (config_point == NULL)
		fatal ("create_node_record: invalid config_point");
	if (node_name == NULL) 
		fatal ("create_node_record: node_name is NULL");
	if (strlen (node_name) >= MAX_NAME_LEN)
		fatal ("create_node_record: node_name too long: %s", node_name);

	/* round up the buffer size to reduce overhead of xrealloc */
	old_buffer_size = (node_record_count) * sizeof (struct node_record);
	old_buffer_size = ((int) ((old_buffer_size / BUF_SIZE) + 1)) * BUF_SIZE;
	new_buffer_size = (node_record_count + 1) * sizeof (struct node_record);
	new_buffer_size = ((int) ((new_buffer_size / BUF_SIZE) + 1)) * BUF_SIZE;
	if (node_record_count == 0)
		node_record_table_ptr = (struct node_record *) xmalloc (new_buffer_size);
	else if (old_buffer_size != new_buffer_size)
		xrealloc (node_record_table_ptr, new_buffer_size);

	node_record_point = node_record_table_ptr + (node_record_count++);
	strcpy (node_record_point->name, node_name);
	node_record_point->node_state = default_node_record.node_state;
	node_record_point->last_response = default_node_record.last_response;
	node_record_point->config_ptr = config_point;
	node_record_point->partition_ptr = NULL;
	/* these values will be overwritten when the node actually registers */
	node_record_point->cpus = config_point->cpus;
	node_record_point->real_memory = config_point->real_memory;
	node_record_point->tmp_disk = config_point->tmp_disk;
	node_record_point->magic = NODE_MAGIC;
	last_bitmap_update = time (NULL);
	return node_record_point;
}


/*
 * delete_config_record - delete all configuration records
 * output: returns 0 if no error, errno otherwise
 * global: config_list - list of all configuration records
 */
int 
delete_config_record () 
{
	last_node_update = time (NULL);
	(void) list_delete_all (config_list, &list_find_config,
				"universal_key");
	return SLURM_SUCCESS;
}


/* 
 * delete_node_record - delete the node record for a node with specified name
 *   to avoid invalidating the bitmaps and hash table, we just clear the name 
 *   set its state to NODE_STATE_DOWN
 * input: name - name of the desired node
 * output: return SLURM_SUCCESS on success, errno otherwise
 * global: node_record_table_ptr - pointer to global node table
 */
int 
delete_node_record (char *name) 
{
	struct node_record *node_record_point;	/* pointer to node_record */

	last_node_update = time (NULL);
	node_record_point = find_node_record (name);
	if (node_record_point == (struct node_record *) NULL) {
		error ("delete_node_record: attempt to delete non-existent node %s", name);
		return ENOENT;
	}  

	if (node_record_point->partition_ptr) {
		(node_record_point->partition_ptr->total_nodes)--;
		(node_record_point->partition_ptr->total_cpus) -=
			node_record_point->cpus;
	}
	strcpy (node_record_point->name, "");
	node_record_point->node_state = NODE_STATE_DOWN;
	last_bitmap_update = time (NULL);
	return SLURM_SUCCESS;
}


/* 
 * dump_hash - print the hash_table contents, used for debugging or analysis of hash technique 
 * global: node_record_table_ptr - pointer to global node table
 *         hash_table - table of hash indecies
 */
void 
dump_hash () 
{
	int i, inx;

	if (hash_table == NULL)
		return;
	for (i = 0; i < node_record_count; i++) {
		inx = hash_table[i];
		if ((inx >= node_record_count) ||
		    (strlen (node_record_table_ptr[inx].name) == 0))
			continue;
		debug ("hash:%d:%s", i, node_record_table_ptr[inx].name);
	}
}


/* dump_all_node_state - save the state of all nodes to file */
int
dump_all_node_state ( void )
{
	int buf_len, buffer_allocated, buffer_offset = 0, error_code = 0, inx, log_fd;
	char *buffer;
	void *buf_ptr;
	char *old_file, *new_file, *reg_file;
	/* Locks: Read config and node */
	slurmctld_lock_t node_read_lock = { READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	buffer_allocated = (BUF_SIZE*16);
	buffer = xmalloc(buffer_allocated);
	buf_ptr = buffer;
	buf_len = buffer_allocated;

	/* write header: time */
	pack32  ((uint32_t) time (NULL), &buf_ptr, &buf_len);

	/* write node records to buffer */
	lock_slurmctld (node_read_lock);
	for (inx = 0; inx < node_record_count; inx++) {
		if ((node_record_table_ptr[inx].magic != NODE_MAGIC) ||
		    (node_record_table_ptr[inx].config_ptr->magic != CONFIG_MAGIC))
			fatal ("dump_all_node_state: data integrity is bad");

		dump_node_state(&node_record_table_ptr[inx], &buf_ptr, &buf_len);
		if (buf_len > BUF_SIZE) 
			continue;
		buffer_allocated += (BUF_SIZE*16);
		buf_len += (BUF_SIZE*16);
		buffer_offset = (char *)buf_ptr - buffer;
		xrealloc(buffer, buffer_allocated);
		buf_ptr = buffer + buffer_offset;
	}
	unlock_slurmctld (node_read_lock);

	/* write the buffer to file */
	old_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (old_file, "/node_state.old");
	reg_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (reg_file, "/node_state");
	new_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (new_file, "/node_state.new");
	lock_state_files ();
	log_fd = creat (new_file, 0600);
	if (log_fd == 0) {
		error ("Can't save state, error creating file %s %m", new_file);
		error_code = errno;
	}
	else {
		buf_len = buffer_allocated - buf_len;
		if (write (log_fd, buffer, buf_len) != buf_len) {
			error ("Can't save state, error writing file %s %m", new_file);
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
	return error_code;
}

/*
 * dump_node_state - dump the state of a specific node to a buffer
 * input:  dump_node_ptr - pointer to node for which information is requested
 *	buf_ptr - buffer for node information 
 *	buf_len - byte size of buffer
 * output: buf_ptr - advanced to end of data written
 *	buf_len - byte size remaining in buffer
 */
void 
dump_node_state (struct node_record *dump_node_ptr, void **buf_ptr, int *buf_len) 
{
	packstr (dump_node_ptr->name, buf_ptr, buf_len);
	pack16  (dump_node_ptr->node_state, buf_ptr, buf_len);
	pack32  (dump_node_ptr->config_ptr->cpus, buf_ptr, buf_len);
	pack32  (dump_node_ptr->config_ptr->real_memory, buf_ptr, buf_len);
	pack32  (dump_node_ptr->config_ptr->tmp_disk, buf_ptr, buf_len);
}

/*
 * load_node_state - load the node state from file, recover from slurmctld restart.
 *	execute this after loading the configuration file data.
 */
int
load_node_state ( void )
{
	char *node_name, *state_file;
	int buffer_allocated, buffer_used = 0, error_code = 0;
	uint16_t node_state, name_len;
	uint32_t time, cpus, real_memory, tmp_disk;
	struct node_record *node_ptr;
	uint32_t buffer_size = 0;
	int state_fd;
	char *buffer;
	void *buf_ptr;

	/* read the file */
	state_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (state_file, "/node_state");
	lock_state_files ();
	state_fd = open (state_file, O_RDONLY);
	if (state_fd < 0) {
		info ("No node state file (%s) to recover", state_file);
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
			error ("Read error on %s, %m", state_file);
	}
	xfree (state_file);
	unlock_state_files ();

	if (buffer_size > sizeof (uint32_t))
		unpack32 (&time, &buf_ptr, &buffer_size);

	while (buffer_size > 0) {
		safe_unpackstr_xmalloc (&node_name, &name_len, &buf_ptr, &buffer_size);
		safe_unpack16 (&node_state, &buf_ptr, &buffer_size);
		safe_unpack32 (&cpus, &buf_ptr, &buffer_size);
		safe_unpack32 (&real_memory, &buf_ptr, &buffer_size);
		safe_unpack32 (&tmp_disk, &buf_ptr, &buffer_size);

		/* find record and perform update */
		node_ptr = find_node_record (node_name);
		if (node_ptr) {
			node_ptr->node_state = node_state;
			node_ptr->cpus = cpus;
			node_ptr->real_memory = real_memory;
			node_ptr->tmp_disk = tmp_disk;
		}
		if (node_name)
			xfree (node_name);
	}
	return error_code;
}

/* 
 * find_node_record - find a record for node with specified name
 * input: name - name of the desired node 
 * output: return pointer to node record or NULL if not found
 * global: node_record_table_ptr - pointer to global node table
 *         hash_table - table of hash indecies
 */
struct node_record * 
find_node_record (char *name) 
{
	int i, inx;

	/* try to find in hash table first */
	if (hash_table) {
		i = hash_index (name);
		if ( (i <= node_record_count) &&
		     ((inx = hash_table[i]) <= node_record_count ) &&
		     (strncmp ((node_record_table_ptr[inx]).name, name, MAX_NAME_LEN) == 0) )
			return (node_record_table_ptr + inx);
		debug ("find_node_record: hash table lookup failure for %s", name);
#if DEBUG_SYSTEM
		dump_hash ();
#endif
	}

	/* revert to sequential search */
	for (i = 0; i < node_record_count; i++) {
		if (strcmp (name, node_record_table_ptr[i].name) != 0)
			continue;
		return (node_record_table_ptr + i);
	}

	if (hash_table) 
		error ("find_node_record: lookup failure for %s", name);
	return (struct node_record *) NULL;
}


/* 
 * hash_index - return a hash table index for the given node name 
 * this code is optimized for names containing a base-ten suffix (e.g. "lx04")
 * input: the node's name
 * output: return code is the hash table index
 * global: hash_table - table of hash indexes
 *	slurmctld_conf.hash_base - numbering base for sequence numbers
 */
int 
hash_index (char *name) 
{
	int i, inx, tmp;

	if (node_record_count == 0)
		return SLURM_SUCCESS;	/* degenerate case */
	inx = 0;

	if ( slurmctld_conf.hash_base == 10 ) {
		for (i = 0;; i++) {
			tmp = (int) name[i];
			if (tmp == 0)
				break;	/* end if string */
			if ((tmp >= (int) '0') && (tmp <= (int) '9'))
				inx = (inx * slurmctld_conf.hash_base) + (tmp - (int) '0');
		}
	}

	else if ( slurmctld_conf.hash_base == 8 ) {
		for (i = 0;; i++) {
			tmp = (int) name[i];
			if (tmp == 0)
				break;	/* end if string */
			if ((tmp >= (int) '0') && (tmp <= (int) '7'))
				inx = (inx * slurmctld_conf.hash_base) + (tmp - (int) '0');
		}
	}

	else {
		for (i = 0; i < 5; i++) {
			tmp = (int) name[i];
			if (tmp == 0)
				break;	/* end if string */
			if ((tmp >= (int) '0') && (tmp <= (int) '9')) {	/* value 0-9 */
				tmp -= (int) '0';
				}
			else if ((tmp >= (int) 'a') && (tmp <= (int) 'z')) {	/* value 10-35 */
				tmp -= (int) 'a';
				tmp += 10;
			}
			else if ((tmp >= (int) 'a') && (tmp <= (int) 'z')) {	/* value 10-35 */
				tmp -= (int) 'a';
				tmp += 10;
			}
			else {
				tmp = 36;
			}
			inx = (inx * 37) + tmp;
		}
	}

	inx = inx % node_record_count;
	return inx;
}


/* 
 * init_node_conf - initialize the node configuration tables and values. 
 *	this should be called before creating any node or configuration entries.
 * output: return value - 0 if no error, otherwise an error code
 * global: node_record_table_ptr - pointer to global node table
 *         default_node_record - default values for node records
 *         default_config_record - default values for configuration records
 *         hash_table - table of hash indecies
 *         last_node_update - time of last node table update
 */
int 
init_node_conf () 
{
	last_node_update = time (NULL);

	node_record_count = 0;
	if (node_record_table_ptr) {
		xfree (node_record_table_ptr);
		node_record_table_ptr = NULL;
	}
	if (hash_table) {
		xfree (hash_table);
		hash_table = NULL;
	}

	strcpy (default_node_record.name, "DEFAULT");
	default_node_record.node_state = NODE_STATE_UNKNOWN;
	default_node_record.last_response = (time_t) 0;
	default_node_record.cpus = 1;
	default_node_record.real_memory = 1;
	default_node_record.tmp_disk = 1;
	default_node_record.config_ptr = NULL;
	default_node_record.partition_ptr = NULL;
	default_config_record.cpus = 1;
	default_config_record.real_memory = 1;
	default_config_record.tmp_disk = 1;
	default_config_record.weight = 1;
	if (default_config_record.feature)
		xfree (default_config_record.feature);
	default_config_record.feature = (char *) NULL;
	if (default_config_record.nodes)
		xfree (default_config_record.nodes);
	default_config_record.nodes = (char *) NULL;
	if (default_config_record.node_bitmap)
		bit_free (default_config_record.node_bitmap);
	default_config_record.node_bitmap = (bitstr_t *) NULL;

	if (config_list)	/* delete defunct configuration entries */
		(void) delete_config_record ();
	else
		config_list = list_create (&list_delete_config);

	if (config_list == NULL)
		fatal ("init_node_conf: list_create can not allocate memory");
	return SLURM_SUCCESS;
}


/* list_compare_config - compare two entry from the config list based upon weight, 
 * see list.h for documentation */
int 
list_compare_config (void *config_entry1, void *config_entry2) 
{
	int weight1, weight2;
	weight1 = ((struct config_record *) config_entry1)->weight;
	weight2 = ((struct config_record *) config_entry2)->weight;
	return (weight1 - weight2);
}


/* list_delete_config - delete an entry from the config list, see list.h for documentation */
void 
list_delete_config (void *config_entry) 
{
	struct config_record *config_record_point;	/* pointer to config_record */
	config_record_point = (struct config_record *) config_entry;
	if (config_record_point->feature)
		xfree (config_record_point->feature);
	if (config_record_point->nodes)
		xfree (config_record_point->nodes);
	if (config_record_point->node_bitmap)
		bit_free (config_record_point->node_bitmap);
	xfree (config_record_point);
}


/* list_find_config - find an entry in the config list, see list.h for documentation 
 * key is partition name or "universal_key" for all config */
int 
list_find_config (void *config_entry, void *key) 
{
	if (strcmp (key, "universal_key") == 0)
		return 1;
	return SLURM_SUCCESS;
}


/*
 * node_name2bitmap - given a node name regular expression, build a bitmap representation
 * input: node_names - list of nodes
 *        bitmap - place to put bitmap pointer
 * output: bitmap - set to bitmap or NULL on error 
 *         returns 0 if no error, otherwise EINVAL or enomem
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree memory at bitmap when no longer required
 */
int 
node_name2bitmap (char *node_names, bitstr_t **bitmap) 
{
	struct node_record *node_record_point;
	char *this_node_name;
	bitstr_t *my_bitmap;
	hostlist_t host_list;

	bitmap[0] = NULL;
	if (node_names == NULL) {
		error ("node_name2bitmap: node_names is NULL");
		return EINVAL;
	}
	if (node_record_count == 0) {
		error ("node_name2bitmap: system has no nodes");
		return EINVAL;
	}

	if ( (host_list = hostlist_create (node_names)) == NULL) {
		error ("hostlist_create on %s error:", node_names);
		return EINVAL;
	}

	my_bitmap = (bitstr_t *) bit_alloc (node_record_count);
	if (my_bitmap == NULL)
		fatal("bit_alloc memory allocation failure");

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		node_record_point = find_node_record (this_node_name);
		if (node_record_point == NULL) {
			error ("node_name2bitmap: invalid node specified %s",this_node_name);
			hostlist_destroy (host_list);
			bit_free (my_bitmap);
			free (this_node_name);
			return EINVAL;
		}
		bit_set (my_bitmap,
			    (bitoff_t) (node_record_point - node_record_table_ptr));
		free (this_node_name);
	}

	hostlist_destroy (host_list);
	bitmap[0] = my_bitmap;
	return SLURM_SUCCESS;
}


/* 
 * pack_all_node - dump all configuration and node information for all nodes in 
 *	machine independent form (for network transmission)
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree the buffer at *buffer_ptr when no longer required
 * NOTE: change slurm_load_node() in api/node_info.c whenever the data format changes
 */
void 
pack_all_node (char **buffer_ptr, int *buffer_size, time_t * update_time) 
{
	int buf_len, buffer_allocated, buffer_offset = 0, inx;
	char *buffer;
	void *buf_ptr;
	int nodes_packed;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	if (*update_time == last_node_update)
		return;

	buffer_allocated = (BUF_SIZE*16);
	buffer = xmalloc(buffer_allocated);
	buf_ptr = buffer;
	buf_len = buffer_allocated;

	/* write header: version and time */
	nodes_packed = 0 ;
	pack32  ((uint32_t) nodes_packed, &buf_ptr, &buf_len);
	pack32  ((uint32_t) last_node_update, &buf_ptr, &buf_len);

	/* write node records */
	for (inx = 0; inx < node_record_count; inx++) {
		if ((node_record_table_ptr[inx].magic != NODE_MAGIC) ||
		    (node_record_table_ptr[inx].config_ptr->magic != CONFIG_MAGIC))
			fatal ("pack_all_node: data integrity is bad");

		pack_node(&node_record_table_ptr[inx], &buf_ptr, &buf_len);
		nodes_packed ++ ;
		if (buf_len > BUF_SIZE) 
			continue;
		buffer_allocated += (BUF_SIZE*16);
		buf_len += (BUF_SIZE*16);
		buffer_offset = (char *)buf_ptr - buffer;
		xrealloc(buffer, buffer_allocated);
		buf_ptr = buffer + buffer_offset;
	}

	buffer_offset = (char *)buf_ptr - buffer;
	xrealloc (buffer, buffer_offset);

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	*update_time = last_node_update;

        /* put in the real record count in the message body header */
        buf_ptr = buffer;
        buf_len = buffer_allocated;
        pack32  ((uint32_t) nodes_packed, &buf_ptr, &buf_len);
}


/* 
 * pack_node - dump all configuration information about a specific node in 
 *	machine independent form (for network transmission)
 * input:  dump_node_ptr - pointer to node for which information is requested
 *	buf_ptr - buffer for node information 
 *	buf_len - byte size of buffer
 * output: buf_ptr - advanced to end of data written
 *	buf_len - byte size remaining in buffer
 * NOTE: if you make any changes here be sure to make the corresponding changes to 
 *	load_node_config in api/node_info.c
 */
void 
pack_node (struct node_record *dump_node_ptr, void **buf_ptr, int *buf_len) 
{
	packstr (dump_node_ptr->name, buf_ptr, buf_len);
	pack16  (dump_node_ptr->node_state, buf_ptr, buf_len);
	if (slurmctld_conf.fast_schedule) {	/* Only data from config_record used for scheduling */
		pack32  (dump_node_ptr->config_ptr->cpus, buf_ptr, buf_len);
		pack32  (dump_node_ptr->config_ptr->real_memory, buf_ptr, buf_len);
		pack32  (dump_node_ptr->config_ptr->tmp_disk, buf_ptr, buf_len);
	}
	else {			/* Individual node data used for scheduling */
		pack32  (dump_node_ptr->cpus, buf_ptr, buf_len);
		pack32  (dump_node_ptr->real_memory, buf_ptr, buf_len);
		pack32  (dump_node_ptr->tmp_disk, buf_ptr, buf_len);
	}
	pack32  (dump_node_ptr->config_ptr->weight, buf_ptr, buf_len);
	packstr (dump_node_ptr->config_ptr->feature, buf_ptr, buf_len);
	if (dump_node_ptr->partition_ptr)
		packstr (dump_node_ptr->partition_ptr->name, buf_ptr, buf_len);
	else
		packstr (NULL, buf_ptr, buf_len);
}


/* 
 * rehash - build a hash table of the node_record entries. this is a large hash table 
 * to permit the immediate finding of a record based only upon its name without regards 
 * to the number. there should be no need for a search. the algorithm is optimized for 
 * node names with a base-ten sequence number suffix. if you have a large cluster and 
 * use a different naming convention, this function and/or the hash_index function 
 * should be re-written.
 * global: node_record_table_ptr - pointer to global node table
 *         hash_table - table of hash indecies
 * NOTE: allocates memory for hash_table
 */
void 
rehash () 
{
	int i, inx;

	xrealloc (hash_table, (sizeof (int) * node_record_count));
	memset (hash_table, 0, (sizeof (int) * node_record_count));

	for (i = 0; i < node_record_count; i++) {
		if (strlen (node_record_table_ptr[i].name) == 0)
			continue;
		inx = hash_index (node_record_table_ptr[i].name);
		hash_table[inx] = i;
	}

	return;
}


/* set_slurmd_addr - establish the slurm_addr for the slurmd on each node */
void 
set_slurmd_addr (void) 
{
	int i;

	for (i = 0; i < node_record_count; i++) {
		if (strlen (node_record_table_ptr[i].name) == 0)
			continue;
		slurm_set_addr (& node_record_table_ptr[i].slurm_addr, 
				slurmctld_conf.slurmd_port, node_record_table_ptr[i].name);
	}

	return;
}


/* 
 * split_node_name - split a node name into prefix, suffix, index value, and digit count
 * input: name - the node name to parse
 *        prefix, suffix, index, digits - location into which to store node name's constituents
 * output: prefix, suffix, index - the node name's constituents 
 *         index - index, defaults to NO_VAL
 *         digits - number of digits in the index, defaults to NO_VAL
 */
void 
split_node_name (char *name, char *prefix, char *suffix, int *index,
		 int *digits) 
{
	int i;
	char tmp[2];

	strcpy (prefix, "");
	strcpy (suffix, "");
	*index = NO_VAL;
	*digits = NO_VAL;
	tmp[1] = (char) NULL;
	for (i = 0;; i++) {
		if (name[i] == (char) NULL)
			break;
		if ((name[i] >= '0') && (name[i] <= '9')) {
			if (*index == NO_VAL) {
				*index = *digits = 0;
			}	
			(*digits)++;
			*index = (*index * 10) + (name[i] - '0');
		}
		else {
			tmp[0] = name[i];
			if (*index == NO_VAL)
				strcat (prefix, tmp);
			else
				strcat (suffix, tmp);
		}
	}
	return;
}


/* 
 * update_node - update the configuration data for one or more nodes
 * global: node_record_table_ptr - pointer to global node table
 */
int 
update_node ( update_node_msg_t * update_node_msg ) 
{
	int error_code = 0, state_val;
	char  *this_node_name ;
	struct node_record *node_record_point;
	hostlist_t host_list;

	if (update_node_msg -> node_names == NULL ) {
		error ("update_node: invalid node name  %s\n", update_node_msg -> node_names );
		return ESLURM_INVALID_NODE_NAME;
	}

	state_val = update_node_msg -> node_state ; 
	
	if ( (host_list = hostlist_create (update_node_msg -> node_names)) == NULL) {
		error ("hostlist_create error on %s: %m", update_node_msg -> node_names);
		return ESLURM_INVALID_NODE_NAME;
	}

	last_node_update = time (NULL);
	while ( (this_node_name = hostlist_shift (host_list)) ) {
		node_record_point = find_node_record (this_node_name);
		if (node_record_point == NULL) {
			error ("update_node: node name %s does not exist, can not be updated", 
				this_node_name);
			error_code = ESLURM_INVALID_NODE_NAME;
			free (this_node_name);
			break;
		}

		if (state_val != NO_VAL) {
			if (state_val == NODE_STATE_DOWN) {
				bit_clear (up_node_bitmap,
					      (int) (node_record_point - node_record_table_ptr));
				bit_clear (idle_node_bitmap,
					      (int) (node_record_point - node_record_table_ptr));
			}
			else if (state_val != NODE_STATE_IDLE)
				bit_clear (idle_node_bitmap,
					      (int) (node_record_point - node_record_table_ptr));
			else	/* (state_val == NODE_STATE_IDLE) */
				bit_set (idle_node_bitmap,
					      (int) (node_record_point - node_record_table_ptr));

			node_record_point->node_state = state_val;
			info ("update_node: node %s state set to %s",
				this_node_name, node_state_string(state_val));
		}
		free (this_node_name);
	}

	hostlist_destroy (host_list);
	return error_code;
}


/*
 * validate_node_specs - validate the node's specifications as valid, 
 *   if not set state to down, in any case update last_response
 * input: node_name - name of the node
 *        cpus - number of cpus measured
 *        real_memory - mega_bytes of real_memory measured
 *        tmp_disk - mega_bytes of tmp_disk measured
 * output: returns 0 if no error, ENOENT if no such node, EINVAL if values too low
 * global: node_record_table_ptr - pointer to global node table
 */
int 
validate_node_specs (char *node_name, uint32_t cpus, 
			uint32_t real_memory, uint32_t tmp_disk) {
	int error_code;
	struct config_record *config_ptr;
	struct node_record *node_ptr;

	node_ptr = find_node_record (node_name);
	if (node_ptr == NULL)
		return ENOENT;
	node_ptr->last_response = last_node_update = time (NULL);

	config_ptr = node_ptr->config_ptr;
	error_code = 0;

	if (cpus < config_ptr->cpus) {
		error ("validate_node_specs: node %s has low cpu count %u", node_name, cpus);
		error_code = EINVAL;
	}
	node_ptr->cpus = cpus;
	if ((config_ptr->cpus != cpus) && (node_ptr->partition_ptr))		
		node_ptr->partition_ptr->total_cpus += (cpus - config_ptr->cpus);

	if (real_memory < config_ptr->real_memory) {
		error ("validate_node_specs: node %s has low real_memory size %u", node_name, real_memory);
		error_code = EINVAL;
	}
	node_ptr->real_memory = real_memory;

	if (tmp_disk < config_ptr->tmp_disk) {
		error ("validate_node_specs: node %s has low tmp_disk size %u", node_name, tmp_disk);
		error_code = EINVAL;
	}
	node_ptr->tmp_disk = tmp_disk;

	if (error_code) {
		error ("validate_node_specs: setting node %s state to DOWN",
			node_name);
		node_ptr->node_state = NODE_STATE_DOWN;
		bit_clear (up_node_bitmap, (node_ptr - node_record_table_ptr));

	}
	else {
		info ("validate_node_specs: node %s has registered", node_name);
		node_ptr->node_state &= (uint16_t) (~NODE_STATE_NO_RESPOND);
		if (node_ptr->node_state == NODE_STATE_UNKNOWN) {
			node_ptr->node_state = NODE_STATE_IDLE;
			bit_set (idle_node_bitmap, (node_ptr - node_record_table_ptr));
		}
		if (node_ptr->node_state != NODE_STATE_DOWN)
			bit_set (up_node_bitmap, (node_ptr - node_record_table_ptr));
	}

	return error_code;
}
