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
#  include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/common/hostlist.h"
#include "src/common/pack.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#define BUF_SIZE 	4096

/* Global variables */
List config_list = NULL;		/* list of config_record entries */
struct node_record *node_record_table_ptr = NULL;	/* node records */
int *hash_table = NULL;			/* table of hashed indexes into 
					 * node_record */
struct config_record default_config_record;
struct node_record default_node_record;
time_t last_bitmap_update = (time_t) NULL;	/* time of last node creation 
						 * or deletion */
time_t last_node_update = (time_t) NULL;	/* time of last update to 
						 * node records */
bitstr_t *up_node_bitmap = NULL;	/* bitmap of nodes are up */
bitstr_t *idle_node_bitmap = NULL;	/* bitmap of nodes are idle */


static int	_delete_config_record (void);
static void 	_dump_node_state (struct node_record *dump_node_ptr, 
				  Buf buffer);
static int	_hash_index (char *name);
static void 	_list_delete_config (void *config_entry);
static int	_list_find_config (void *config_entry, void *key);
static void 	_make_node_down(struct node_record *node_ptr);
static void 	_pack_node (struct node_record *dump_node_ptr, Buf buffer);
static void	_split_node_name (char *name, char *prefix, char *suffix, 
					int *index, int *digits);

#if DEBUG_SYSTEM
static void	_dump_hash (void);
#endif


/*
 * bitmap2node_name - given a bitmap, build a list of comma separated node 
 *	names. names may include regular expressions (e.g. "lx[01-10]")
 * IN bitmap - bitmap pointer
 * RET pointer to node list or NULL on error 
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
char * bitmap2node_name (bitstr_t *bitmap) 
{
	char *node_list_ptr;
	int node_list_size, i;
	char prefix[MAX_NAME_LEN], suffix[MAX_NAME_LEN];
	char format[MAX_NAME_LEN], temp[MAX_NAME_LEN];
	char last_prefix[MAX_NAME_LEN], last_suffix[MAX_NAME_LEN];
	int first_index = 0, last_index = 0, index;
	int first_digits = 0, last_digits = 0;

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
		_split_node_name (node_record_table_ptr[i].name, prefix,
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
 * create_config_record - create a config_record entry and set is values to 
 *	the defaults. each config record corresponds to a line in the  
 *	slurm.conf file and typically describes the configuration of a 
 *	large number of nodes
 * RET pointer to the config_record
 * global: default_config_record - default configuration values
 * NOTE: memory allocated will remain in existence until 
 *	_delete_config_record() is called to delete all configuration records
 */
struct config_record * create_config_record (void) 
{
	struct config_record *config_ptr;

	last_node_update = time (NULL);
	config_ptr = (struct config_record *)
		     xmalloc (sizeof (struct config_record));

	/* set default values */
	config_ptr->cpus = default_config_record.cpus;
	config_ptr->real_memory = default_config_record.real_memory;
	config_ptr->tmp_disk = default_config_record.tmp_disk;
	config_ptr->weight = default_config_record.weight;
	config_ptr->nodes = NULL;
	config_ptr->node_bitmap = NULL;
	xassert (config_ptr->magic = CONFIG_MAGIC);  /* set value */
	if (default_config_record.feature) {
		config_ptr->feature =
			(char *)
			xmalloc (strlen (default_config_record.feature) + 1);
		strcpy (config_ptr->feature, default_config_record.feature);
	}
	else
		config_ptr->feature = (char *) NULL;

	if (list_append(config_list, config_ptr) == NULL)
		fatal ("create_config_record: unable to allocate memory");

	return config_ptr;
}


/* 
 * create_node_record - create a node record and set its values to defaults
 * IN config_point - pointer to node's configuration information
 * IN node_name - name of the node
 * RET pointer to the record or NULL if error
 * global: default_node_record - default node values
 * NOTE: the record's values are initialized to those of default_node_record, 
 *	node_name and config_point's cpus, real_memory, and tmp_disk values
 * NOTE: allocates memory at node_record_table_ptr that must be xfreed when  
 *	the global node table is no longer required
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
		fatal ("create_node_record: node_name too long: %s", 
		       node_name);

	/* round up the buffer size to reduce overhead of xrealloc */
	old_buffer_size = (node_record_count) * sizeof (struct node_record);
	old_buffer_size = 
		((int) ((old_buffer_size / BUF_SIZE) + 1)) * BUF_SIZE;
	new_buffer_size = 
		(node_record_count + 1) * sizeof (struct node_record);
	new_buffer_size = 
		((int) ((new_buffer_size / BUF_SIZE) + 1)) * BUF_SIZE;
	if (node_record_count == 0)
		node_record_table_ptr = 
			(struct node_record *) xmalloc (new_buffer_size);
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
	xassert (node_record_point->magic = NODE_MAGIC)  /* set value */;
	last_bitmap_update = time (NULL);
	return node_record_point;
}


/*
 * _delete_config_record - delete all configuration records
 * RET 0 if no error, errno otherwise
 * global: config_list - list of all configuration records
 */
static int _delete_config_record (void) 
{
	last_node_update = time (NULL);
	(void) list_delete_all (config_list, &_list_find_config,
				"universal_key");
	return SLURM_SUCCESS;
}


/* dump_all_node_state - save the state of all nodes to file */
int dump_all_node_state ( void )
{
	int error_code = 0, inx, log_fd;
	char *old_file, *new_file, *reg_file;
	/* Locks: Read config and node */
	slurmctld_lock_t node_read_lock = { READ_LOCK, NO_LOCK, READ_LOCK, 
						NO_LOCK };
	Buf buffer = init_buf(BUF_SIZE*16);

	/* write header: time */
	pack_time  (time (NULL), buffer);

	/* write node records to buffer */
	lock_slurmctld (node_read_lock);
	for (inx = 0; inx < node_record_count; inx++) {
		xassert (node_record_table_ptr[inx].magic == NODE_MAGIC);
		xassert (node_record_table_ptr[inx].config_ptr->magic == 
			 CONFIG_MAGIC);

		_dump_node_state (&node_record_table_ptr[inx], buffer);
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
		error ("Can't save state, error creating file %s %m", 
		       new_file);
		error_code = errno;
	}
	else {
		if (write (log_fd, get_buf_data(buffer), 
		           get_buf_offset(buffer)) != 
					get_buf_offset(buffer)) {
			error ("Can't save state, error writing file %s %m", 
			       new_file);
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

	free_buf (buffer);
	return error_code;
}

/*
 * _dump_node_state - dump the state of a specific node to a buffer
 * IN dump_node_ptr - pointer to node for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
static void 
_dump_node_state (struct node_record *dump_node_ptr, Buf buffer) 
{
	packstr (dump_node_ptr->name, buffer);
	pack16  (dump_node_ptr->node_state, buffer);
	pack32  (dump_node_ptr->config_ptr->cpus, buffer);
	pack32  (dump_node_ptr->config_ptr->real_memory, buffer);
	pack32  (dump_node_ptr->config_ptr->tmp_disk, buffer);
}

/*
 * load_all_node_state - load the node state from file, recover on slurmctld 
 *	restart. execute this after loading the configuration file data.
 *	data goes into common storage
 */
int load_all_node_state ( void )
{
	char *node_name, *data = NULL, *state_file;
	int data_allocated, data_read = 0, error_code = 0;
	uint16_t node_state, name_len;
	uint32_t cpus, real_memory, tmp_disk, data_size = 0;
	struct node_record *node_ptr;
	int state_fd;
	time_t time_stamp;
	Buf buffer;

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
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while ((data_read = 
			   read (state_fd, &data[data_size], BUF_SIZE)) == 
					BUF_SIZE) {
			data_size += data_read;
			data_allocated += BUF_SIZE;
			xrealloc(data, data_allocated);
		}
		data_size += data_read;
		close (state_fd);
		if (data_read < 0) 
			error ("Read error on %s, %m", state_file);
	}
	xfree (state_file);
	unlock_state_files ();

	buffer = create_buf (data, data_size);
	safe_unpack_time (&time_stamp, buffer);

	while (remaining_buf (buffer) > 0) {
		safe_unpackstr_xmalloc (&node_name, &name_len, buffer);
		safe_unpack16 (&node_state,  buffer);
		safe_unpack32 (&cpus,        buffer);
		safe_unpack32 (&real_memory, buffer);
		safe_unpack32 (&tmp_disk,    buffer);

		/* validity test as possible */
		if ((cpus == 0) || 
		    ((node_state & (~NODE_STATE_NO_RESPOND)) >= 
							NODE_STATE_END)) {
			error ("Invalid data for node %s: cpus=%u, state=%u",
				node_name, cpus, node_state);
			error ("No more node data will be processed from the checkpoint file");
			xfree (node_name);
			error_code = EINVAL;
			break;
			
		}

		/* find record and perform update */
		node_ptr = find_node_record (node_name);
		if (node_ptr) {
			node_ptr->node_state    = node_state;
			node_ptr->cpus          = cpus;
			node_ptr->real_memory   = real_memory;
			node_ptr->tmp_disk      = tmp_disk;
			node_ptr->last_response = (time_t) 0;
		} else {
			error ("Node %s has vanished from configuration", 
			       node_name);
		}
		xfree (node_name);
	}

	free_buf (buffer);
	return error_code;

unpack_error:
	error ("Incomplete node data checkpoint file. Incomplete restore.");
	free_buf (buffer);
	return EFAULT;
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
		i = _hash_index (name);
		if ( (i <= node_record_count) &&
		     ((inx = hash_table[i]) <= node_record_count ) &&
		     (strncmp ((node_record_table_ptr[inx]).name, name, 
		                MAX_NAME_LEN) == 0) )
			return (node_record_table_ptr + inx);
		debug ("find_node_record: hash table lookup failure for %s", 
		       name);
#if DEBUG_SYSTEM
		_dump_hash ();
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
 * _hash_index - return a hash table index for the given node name 
 * IN name = the node's name
 * RET the hash table index
 * global: hash_table - table of hash indexes
 *	slurmctld_conf.hash_base - numbering base for sequence numbers
 */
static int _hash_index (char *name) 
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
				inx = (inx * slurmctld_conf.hash_base) + 
				      (tmp - (int) '0');
		}
	}

	else if ( slurmctld_conf.hash_base == 8 ) {
		for (i = 0;; i++) {
			tmp = (int) name[i];
			if (tmp == 0)
				break;	/* end if string */
			if ((tmp >= (int) '0') && (tmp <= (int) '7'))
				inx = (inx * slurmctld_conf.hash_base) + 
				      (tmp - (int) '0');
		}
	}

	else {
		for (i = 0; i < 5; i++) {
			tmp = (int) name[i];
			if (tmp == 0)
				break;	/* end if string */
			if ((tmp >= (int) '0') && (tmp <= (int) '9')) {	
				/* value 0-9 */
				tmp -= (int) '0';
				}
			else if ((tmp >= (int) 'a') && (tmp <= (int) 'z')) {
				/* value 10-35 */
				tmp -= (int) 'a';
				tmp += 10;
			}
			else if ((tmp >= (int) 'a') && (tmp <= (int) 'z')) {
				/* value 10-35 */
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
 *	this should be called before creating any node or configuration 
 *	entries.
 * RET 0 if no error, otherwise an error code
 * global: node_record_table_ptr - pointer to global node table
 *         default_node_record - default values for node records
 *         default_config_record - default values for configuration records
 *         hash_table - table of hash indecies
 *         last_node_update - time of last node table update
 */
int init_node_conf (void) 
{
	last_node_update = time (NULL);

	node_record_count = 0;
	xfree(node_record_table_ptr);
	xfree(hash_table);

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
	xfree(default_config_record.feature);
	xfree(default_config_record.nodes);
	FREE_NULL_BITMAP (default_config_record.node_bitmap);

	if (config_list)	/* delete defunct configuration entries */
		(void) _delete_config_record ();
	else
		config_list = list_create (&_list_delete_config);

	if (config_list == NULL)
		fatal ("memory allocation failure");
	return SLURM_SUCCESS;
}


/* list_compare_config - compare two entry from the config list based upon 
 *	weight, see common/list.h for documentation */
int list_compare_config (void *config_entry1, void *config_entry2) 
{
	int weight1, weight2;
	weight1 = ((struct config_record *) config_entry1)->weight;
	weight2 = ((struct config_record *) config_entry2)->weight;
	return (weight1 - weight2);
}


/* _list_delete_config - delete an entry from the config list, 
 *	see list.h for documentation */
static void _list_delete_config (void *config_entry) 
{
	struct config_record *config_ptr = (struct config_record *) 
					   config_entry;

	if (config_ptr == NULL)
		fatal ("_list_delete_config: config_ptr == NULL");
	xassert(config_ptr->magic == CONFIG_MAGIC);
	xfree (config_ptr->feature);
	xfree (config_ptr->nodes);
	FREE_NULL_BITMAP (config_ptr->node_bitmap);
	xfree (config_ptr);
}


/* 
 * _list_find_config - find an entry in the config list, see list.h for   
 *	documentation 
 * IN key - is "universal_key" for all config
 * RET 1 if key == "universal_key", 0 otherwise
 */
static int _list_find_config (void *config_entry, void *key) 
{
	if (strcmp (key, "universal_key") == 0)
		return 1;
	return SLURM_SUCCESS;
}


/*
 * node_name2bitmap - given a node name regular expression, build a bitmap 
 *	representation
 * IN node_names - list of nodes
 * OUT bitmap - set to bitmap or NULL on error 
 * RET 0 if no error, otherwise EINVAL or enomem
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree memory at bitmap when no longer required
 */
int node_name2bitmap (char *node_names, bitstr_t **bitmap) 
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
	if (my_bitmap == 0)
		fatal ("memory allocation failure");

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		node_record_point = find_node_record (this_node_name);
		if (node_record_point == NULL) {
			error ("node_name2bitmap: invalid node specified %s",
			       this_node_name);
			hostlist_destroy (host_list);
			bit_free (my_bitmap);
			free (this_node_name);
			return EINVAL;
		}
		bit_set (my_bitmap,
			 (bitoff_t) (node_record_point - 
			             node_record_table_ptr));
		free (this_node_name);
	}

	hostlist_destroy (host_list);
	bitmap[0] = my_bitmap;
	return SLURM_SUCCESS;
}


/* 
 * pack_all_node - dump all configuration and node information for all nodes  
 *	in machine independent form (for network transmission)
 * OUT buffer_ptr - pointer to the stored data
 * OUT buffer_size - set to size of the buffer in bytes
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree the buffer at *buffer_ptr
 * NOTE: change slurm_load_node() in api/node_info.c when data format changes
 */
void pack_all_node (char **buffer_ptr, int *buffer_size) 
{
	int inx;
	uint32_t nodes_packed, tmp_offset;
	Buf buffer;
	time_t now = time(NULL);

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf (BUF_SIZE*16);

	/* write header: version and time */
	nodes_packed = 0 ;
	pack32  (nodes_packed, buffer);
	pack_time  (now, buffer);

	/* write node records */
	for (inx = 0; inx < node_record_count; inx++) {
		xassert (node_record_table_ptr[inx].magic == NODE_MAGIC);
		xassert (node_record_table_ptr[inx].config_ptr->magic ==  
			 CONFIG_MAGIC);

		_pack_node(&node_record_table_ptr[inx], buffer);
		nodes_packed ++ ;
	}

	tmp_offset = get_buf_offset (buffer);
	set_buf_offset (buffer, 0);
	pack32  ((uint32_t) nodes_packed, buffer);
	set_buf_offset (buffer, tmp_offset);

	*buffer_size = get_buf_offset (buffer);
	buffer_ptr[0] = xfer_buf_data (buffer);
}


/* 
 * _pack_node - dump all configuration information about a specific node in 
 *	machine independent form (for network transmission)
 * IN dump_node_ptr - pointer to node for which information is requested
 * IN/OUT buffer - buffer where data is placed, pointers automatically updated
 * NOTE: if you make any changes here be sure to make the corresponding 
 *	changes to load_node_config in api/node_info.c
 */
static void _pack_node (struct node_record *dump_node_ptr, Buf buffer) 
{
	packstr (dump_node_ptr->name, buffer);
	pack16  (dump_node_ptr->node_state, buffer);
	if (slurmctld_conf.fast_schedule) {	
		/* Only data from config_record used for scheduling */
		pack32  (dump_node_ptr->config_ptr->cpus, buffer);
		pack32  (dump_node_ptr->config_ptr->real_memory, buffer);
		pack32  (dump_node_ptr->config_ptr->tmp_disk, buffer);
	} else {	
		/* Individual node data used for scheduling */
		pack32  (dump_node_ptr->cpus, buffer);
		pack32  (dump_node_ptr->real_memory, buffer);
		pack32  (dump_node_ptr->tmp_disk, buffer);
	}
	pack32  (dump_node_ptr->config_ptr->weight, buffer);
	packstr (dump_node_ptr->config_ptr->feature, buffer);
	if (dump_node_ptr->partition_ptr)
		packstr (dump_node_ptr->partition_ptr->name, buffer);
	else
		packstr (NULL, buffer);
}


/* 
 * rehash - build a hash table of the node_record entries. this is a large 
 *	hash table to permit the immediate finding of a record based only 
 *	upon its name without regards to their number. there should be no 
 *	need for a search. 
 * global: node_record_table_ptr - pointer to global node table
 *         hash_table - table of hash indecies
 * NOTE: manages memory for hash_table
 */
void rehash (void) 
{
	int i, inx;

	xrealloc (hash_table, (sizeof (int) * node_record_count));
	memset (hash_table, 0, (sizeof (int) * node_record_count));

	for (i = 0; i < node_record_count; i++) {
		if (strlen (node_record_table_ptr[i].name) == 0)
			continue;
		inx = _hash_index (node_record_table_ptr[i].name);
		hash_table[inx] = i;
	}

	return;
}


/* set_slurmd_addr - establish the slurm_addr for the slurmd on each node
 *	Uses common data structures. */
void set_slurmd_addr (void) 
{
	int i;

	for (i = 0; i < node_record_count; i++) {
		if (strlen (node_record_table_ptr[i].name) == 0)
			continue;
		slurm_set_addr (& node_record_table_ptr[i].slurm_addr, 
				slurmctld_conf.slurmd_port, 
				node_record_table_ptr[i].comm_name);
		if (node_record_table_ptr[i].slurm_addr.sin_port)
			continue;
		error ("slurm_set_addr failure on %s", 
		       node_record_table_ptr[i].comm_name);
		strncpy (node_record_table_ptr[i].name,
			 node_record_table_ptr[i].comm_name, 
			 MAX_NAME_LEN);
		slurm_set_addr (& node_record_table_ptr[i].slurm_addr, 
				slurmctld_conf.slurmd_port, 
				node_record_table_ptr[i].comm_name);
		if (node_record_table_ptr[i].slurm_addr.sin_port)
			continue;
		fatal ("slurm_set_addr failure on %s", 
		       node_record_table_ptr[i].comm_name);
	}

	return;
}


/* 
 * _split_node_name - split a node name into prefix, suffix, index value, 
 *	and digit count
 * IN name - the node name to parse
 * OUT prefix, suffix, index - the node name's constituents 
 * OUT index - index, defaults to NO_VAL
 * OUT digits - number of digits in the index, defaults to NO_VAL
 */
static void _split_node_name (char *name, char *prefix, char *suffix, 
				int *index, int *digits) 
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
 * IN update_node_msg - update node request
 * RET 0 or error code
 * global: node_record_table_ptr - pointer to global node table
 */
int update_node ( update_node_msg_t * update_node_msg ) 
{
	int error_code = 0, state_val, node_inx;
	char  *this_node_name ;
	struct node_record *node_record_point;
	hostlist_t host_list;

	if (update_node_msg -> node_names == NULL ) {
		error ("update_node: invalid node name  %s", 
		       update_node_msg -> node_names );
		return ESLURM_INVALID_NODE_NAME;
	}

	state_val = update_node_msg -> node_state ; 
	
	if ( (host_list = hostlist_create (update_node_msg -> node_names))
								 == NULL) {
		error ("hostlist_create error on %s: %m", 
		       update_node_msg -> node_names);
		return ESLURM_INVALID_NODE_NAME;
	}

	last_node_update = time (NULL);
	while ( (this_node_name = hostlist_shift (host_list)) ) {
		node_record_point = find_node_record (this_node_name);
		node_inx = node_record_point - node_record_table_ptr;
		if (node_record_point == NULL) {
			error ("update_node: node %s does not exist", 
				this_node_name);
			error_code = ESLURM_INVALID_NODE_NAME;
			free (this_node_name);
			break;
		}

		if (state_val != NO_VAL) {
			if (state_val == NODE_STATE_DOWN) {
				bit_clear (up_node_bitmap, node_inx);
				bit_clear (idle_node_bitmap, node_inx);
				kill_running_job_by_node_name (this_node_name,
							       false);
			}
			else if (state_val == NODE_STATE_UNKNOWN) {
				bit_clear (up_node_bitmap, node_inx);
				bit_clear (idle_node_bitmap, node_inx);
			}
			else if (state_val == NODE_STATE_IDLE) {
				bit_set (up_node_bitmap, node_inx);
				bit_set (idle_node_bitmap, node_inx);
			}
			else if (state_val == NODE_STATE_ALLOCATED) {
				bit_set (up_node_bitmap, node_inx);
				bit_clear (idle_node_bitmap, node_inx);
			}
			else if (state_val == NODE_STATE_DRAINED) {
				if (bit_test (idle_node_bitmap, node_inx) == 
									false)
					state_val = NODE_STATE_DRAINING;
				bit_clear (up_node_bitmap, node_inx);
			}
			else if (state_val == NODE_STATE_DRAINING) {
				if (bit_test (idle_node_bitmap, node_inx)) {
					state_val = NODE_STATE_DRAINED;
					bit_clear (idle_node_bitmap, node_inx);
				}
				bit_clear (up_node_bitmap, node_inx);
			}
			else if (state_val == NODE_STATE_NO_RESPOND) {
				bit_clear (up_node_bitmap,   node_inx);
				bit_clear (idle_node_bitmap, node_inx);
				node_record_point->node_state |=
						NODE_STATE_NO_RESPOND;
				info ("update_node: node %s state set to %s",
				      this_node_name, "NoResp");
				continue;
			}
			else {
				error ("Invalid node state specified %d", 
				       state_val);
				continue;
			}

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
 * IN node_name - name of the node
 * IN cpus - number of cpus measured
 * IN real_memory - mega_bytes of real_memory measured
 * IN tmp_disk - mega_bytes of tmp_disk measured
 * IN status - node status code
 * RET 0 if no error, ENOENT if no such node, EINVAL if values too low
 * global: node_record_table_ptr - pointer to global node table
 */
int 
validate_node_specs (char *node_name, uint32_t cpus, 
			uint32_t real_memory, uint32_t tmp_disk, 
			uint32_t job_count, uint32_t status) {
	int error_code;
	struct config_record *config_ptr;
	struct node_record *node_ptr;
	uint16_t resp_state;

	node_ptr = find_node_record (node_name);
	if (node_ptr == NULL)
		return ENOENT;
	node_ptr->last_response = last_node_update = time (NULL);

	config_ptr = node_ptr->config_ptr;
	error_code = 0;

	if (cpus < config_ptr->cpus) {
		error ("Node %s has low cpu count %u", node_name, cpus);
		error_code = EINVAL;
	}
	node_ptr->cpus = cpus;
	if ((config_ptr->cpus != cpus) && (node_ptr->partition_ptr))		
		node_ptr->partition_ptr->total_cpus += 
						(cpus - config_ptr->cpus);

	if (real_memory < config_ptr->real_memory) {
		error ("Node %s has low real_memory size %u", 
		       node_name, real_memory);
		error_code = EINVAL;
	}
	node_ptr->real_memory = real_memory;

	if (tmp_disk < config_ptr->tmp_disk) {
		error ("Node %s has low tmp_disk size %u",
		       node_name, tmp_disk);
		error_code = EINVAL;
	}
	node_ptr->tmp_disk = tmp_disk;

	if (error_code) {
		error ("Setting node %s state to DOWN", node_name);
		set_node_down(node_name);
	} else if (status == ESLURMD_PROLOG_FAILED) {
		error ("Prolog failure on node %s, state to DOWN",
			node_name);
		set_node_down(node_name);
	} else {
		info ("validate_node_specs: node %s has registered", 
		      node_name);
		node_ptr->cpus = cpus;
		node_ptr->real_memory = real_memory;
		node_ptr->tmp_disk = tmp_disk;
#ifdef 		HAVE_LIBELAN3
		/* Every node in a given partition must have the same 
		 * processor count at present */
		if ((slurmctld_conf.fast_schedule == 0) &&
		    (node_ptr->config_ptr->cpus != cpus)) {
			error ("Node %s processor count inconsistent with rest of partition",
				node_name);
			return EINVAL;		/* leave node down */
		}
#endif
		resp_state = node_ptr->node_state & NODE_STATE_NO_RESPOND;
		node_ptr->node_state &= (uint16_t) (~NODE_STATE_NO_RESPOND);
		if (node_ptr->node_state == NODE_STATE_UNKNOWN) {
			if (job_count)
				node_ptr->node_state = NODE_STATE_ALLOCATED;
			else
				node_ptr->node_state = NODE_STATE_IDLE;
		} else if (node_ptr->node_state == NODE_STATE_DRAINING) {
			if (job_count == 0)
				node_ptr->node_state = NODE_STATE_DRAINED;
		} else if (node_ptr->node_state == NODE_STATE_DRAINED) {
			if (job_count != 0)
				node_ptr->node_state = NODE_STATE_DRAINING;
		} else if ((node_ptr->node_state == NODE_STATE_DOWN) &&
		           (slurmctld_conf.ret2service == 1)) {
			if (job_count)
				node_ptr->node_state = NODE_STATE_ALLOCATED;
			else
				node_ptr->node_state = NODE_STATE_IDLE;
			info ("validate_node_specs: node %s returned to service", 
			      node_name);
			resp_state = 1;	/* just started responding */
		} else if ((node_ptr->node_state == NODE_STATE_ALLOCATED) &&
			   (job_count == 0)) {	/* job vanished */
			node_ptr->node_state = NODE_STATE_IDLE;
		} else if ((node_ptr->node_state == NODE_STATE_COMPLETING) &&
			   (job_count == 0)) {	/* job already done */
			node_ptr->node_state = NODE_STATE_IDLE;
		}

		if (node_ptr->node_state == NODE_STATE_IDLE) {
			bit_set (idle_node_bitmap, 
			         (node_ptr - node_record_table_ptr));
			if (resp_state)	{
				/* Node just started responding, 
				 * do all pending RPCs now */
				retry_pending (node_name);
			}
		}
		if (node_ptr->node_state != NODE_STATE_DOWN)
			bit_set (up_node_bitmap, 
			         (node_ptr - node_record_table_ptr));
	}

	return error_code;
}

/* node_did_resp - record that the specified node is responding
 * IN name - name of the node */
void node_did_resp (char *name)
{
	struct node_record *node_ptr;
	int node_inx;
	uint16_t resp_state;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("node_not_resp unable to find node %s", name);
		return;
	}

	node_inx = node_ptr - node_record_table_ptr;
	last_node_update = time (NULL);
	node_record_table_ptr[node_inx].last_response = time (NULL);
	resp_state = node_ptr->node_state & NODE_STATE_NO_RESPOND;
	node_ptr->node_state &= (uint16_t) (~NODE_STATE_NO_RESPOND);
	if (node_ptr->node_state == NODE_STATE_UNKNOWN)
		node_ptr->node_state = NODE_STATE_IDLE;
	if (node_ptr->node_state == NODE_STATE_IDLE) {
		bit_set (idle_node_bitmap, node_inx);
		if (resp_state)	{
			/* Node just started responding, 
			 * do all its pending RPCs now */
			retry_pending (name);
		}
	}
	if (node_ptr->node_state != NODE_STATE_DOWN)
		bit_set (up_node_bitmap, node_inx);
	return;
}

/* node_not_resp - record that the specified node is not responding 
 * IN name - name of the node */
void node_not_resp (char *name)
{
	struct node_record *node_ptr;
	int i;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("node_not_resp unable to find node %s", name);
		return;
	}

	i = node_ptr - node_record_table_ptr;
	if (node_record_table_ptr[i].node_state & NODE_STATE_NO_RESPOND)
		return;		/* Already known to be not responding */

	last_node_update = time (NULL);
	error ("Node %s not responding", name);
	bit_clear (up_node_bitmap, i);
	bit_clear (idle_node_bitmap, i);
	node_record_table_ptr[i].node_state |= NODE_STATE_NO_RESPOND;
	return;
}

/* set_node_down - make the specified node's state DOWN, kill jobs as needed 
 * IN name - name of the node */
void set_node_down (char *name)
{
	struct node_record *node_ptr;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("node_not_resp unable to find node %s", name);
		return;
	}

	(void) kill_running_job_by_node_name(name, false);
	_make_node_down(node_ptr);

	return;
}

/* ping_nodes - check that all nodes and daemons are alive,  
 *	get nodes in UNKNOWN state to register */
void ping_nodes (void)
{
	int i, pos, age;
	time_t now;
	uint16_t base_state;

	int ping_buf_rec_size = 0;
	agent_arg_t *ping_agent_args;
	pthread_attr_t ping_attr_agent;
	pthread_t ping_thread_agent;

	int reg_buf_rec_size = 0;
	agent_arg_t *reg_agent_args;
	pthread_attr_t reg_attr_agent;
	pthread_t reg_thread_agent;

	ping_agent_args = xmalloc (sizeof (agent_arg_t));
	ping_agent_args->msg_type = REQUEST_PING;
	ping_agent_args->retry = 0;
	reg_agent_args = xmalloc (sizeof (agent_arg_t));
	reg_agent_args->msg_type = REQUEST_NODE_REGISTRATION_STATUS;
	reg_agent_args->retry = 0;
	now = time (NULL);

	for (i = 0; i < node_record_count; i++) {
		base_state = node_record_table_ptr[i].node_state & 
				(~NODE_STATE_NO_RESPOND);
		if (base_state == NODE_STATE_DOWN)
			continue;

		age = difftime (now, node_record_table_ptr[i].last_response);
		if (age < slurmctld_conf.heartbeat_interval)
			continue;

		if ((node_record_table_ptr[i].last_response != (time_t)0) &&
		    (age >= slurmctld_conf.slurmd_timeout) &&
		    (base_state != NODE_STATE_DOWN)) {
			error ("Node %s not responding, setting DOWN", 
			       node_record_table_ptr[i].name);
			kill_running_job_by_node_name (
					node_record_table_ptr[i].name, false);
			_make_node_down(&node_record_table_ptr[i]);
			continue;
		}

		if (node_record_table_ptr[i].last_response == (time_t)0)
			node_record_table_ptr[i].last_response = 
						slurmctld_conf.last_update;

		if (base_state == NODE_STATE_UNKNOWN) {
			debug3 ("attempt to register %s now", 
			        node_record_table_ptr[i].name);
			if ((reg_agent_args->node_count+1) > 
						reg_buf_rec_size) {
				reg_buf_rec_size += 32;
				xrealloc ((reg_agent_args->slurm_addr), 
				          (sizeof (struct sockaddr_in) * 
					  reg_buf_rec_size));
				xrealloc ((reg_agent_args->node_names), 
				          (MAX_NAME_LEN * reg_buf_rec_size));
			}
			reg_agent_args->slurm_addr[
					reg_agent_args->node_count] = 
					node_record_table_ptr[i].slurm_addr;
			pos = MAX_NAME_LEN * reg_agent_args->node_count;
			strncpy (&reg_agent_args->node_names[pos],
			         node_record_table_ptr[i].name, MAX_NAME_LEN);
			reg_agent_args->node_count++;
			continue;
		}

		debug3 ("ping %s now", node_record_table_ptr[i].name);

		if ((ping_agent_args->node_count+1) > ping_buf_rec_size) {
			ping_buf_rec_size += 32;
			xrealloc ((ping_agent_args->slurm_addr), 
			          (sizeof (struct sockaddr_in) * 
				  ping_buf_rec_size));
			xrealloc ((ping_agent_args->node_names), 
			          (MAX_NAME_LEN * ping_buf_rec_size));
		}
		ping_agent_args->slurm_addr[ping_agent_args->node_count] = 
					node_record_table_ptr[i].slurm_addr;
		pos = MAX_NAME_LEN * ping_agent_args->node_count;
		strncpy (&ping_agent_args->node_names[pos],
		         node_record_table_ptr[i].name, MAX_NAME_LEN);
		ping_agent_args->node_count++;

	}

	if (ping_agent_args->node_count == 0)
		xfree (ping_agent_args);
	else {
		debug ("Spawning ping agent");
		if (pthread_attr_init (&ping_attr_agent))
			fatal ("pthread_attr_init error %m");
		if (pthread_attr_setdetachstate (&ping_attr_agent, 
						PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		if (pthread_attr_setscope (&ping_attr_agent, 
						PTHREAD_SCOPE_SYSTEM))
			error ("pthread_attr_setscope error %m");
#endif
		if (pthread_create (&ping_thread_agent, &ping_attr_agent, 
					agent, (void *)ping_agent_args)) {
			error ("pthread_create error %m");
			sleep (1); /* sleep and try once more */
			if (pthread_create (&ping_thread_agent, 
					    &ping_attr_agent, 
					    agent, (void *)ping_agent_args))
				fatal ("pthread_create error %m");
		}
	}

	if (reg_agent_args->node_count == 0)
		xfree (reg_agent_args);
	else {
		debug ("Spawning node registration agent");
		if (pthread_attr_init (&reg_attr_agent))
			fatal ("pthread_attr_init error %m");
		if (pthread_attr_setdetachstate (&reg_attr_agent, 
						 PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		if (pthread_attr_setscope (&reg_attr_agent, 
					   PTHREAD_SCOPE_SYSTEM))
			error ("pthread_attr_setscope error %m");
#endif
		if (pthread_create (&reg_thread_agent, &reg_attr_agent, 
					agent, (void *)reg_agent_args)) {
			error ("pthread_create error %m");
			sleep (1); /* sleep and try once more */
			if (pthread_create (&reg_thread_agent, 
					    &reg_attr_agent,
					    agent, (void *)reg_agent_args))
				fatal ("pthread_create error %m");
		}
	}
}

/*
 * find_first_node_record - find a record for first node in the bitmap
 * IN node_bitmap
 */
struct node_record *
find_first_node_record (bitstr_t *node_bitmap)
{
	int inx;

	if (node_bitmap == NULL) {
		error ("find_first_node_record passed null bitstring");
		return NULL;
	}

	inx = bit_ffs (node_bitmap);
	if (inx < 0)
		return NULL;
	else
		return &node_record_table_ptr[inx];
}

#if DEBUG_SYSTEM
/* 
 * _dump_hash - print the hash_table contents, used for debugging or
 *	analysis of hash technique 
 * global: node_record_table_ptr - pointer to global node table
 *         hash_table - table of hash indecies
 */
static void _dump_hash (void) 
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
#endif

/* msg_to_slurmd - send given msg_type every slurmd, no args */
void msg_to_slurmd (slurm_msg_type_t msg_type)
{
	int i, pos;
	shutdown_msg_t *shutdown_req;

	int kill_buf_rec_size = 0;
	agent_arg_t *kill_agent_args;
	pthread_attr_t kill_attr_agent;
	pthread_t kill_thread_agent;

	kill_agent_args = xmalloc (sizeof (agent_arg_t));
	kill_agent_args->msg_type = msg_type;
	kill_agent_args->retry = 0;
	if (msg_type == REQUEST_SHUTDOWN) {
 		shutdown_req = xmalloc(sizeof(shutdown_msg_t));
		shutdown_req->core = 0;
		kill_agent_args->msg_args = shutdown_req;
	}

	for (i = 0; i < node_record_count; i++) {
		if ((kill_agent_args->node_count+1) > kill_buf_rec_size) {
			kill_buf_rec_size += 64;
			xrealloc ((kill_agent_args->slurm_addr), 
			          (sizeof (struct sockaddr_in) * 
				  kill_buf_rec_size));
			xrealloc ((kill_agent_args->node_names), 
			          (MAX_NAME_LEN * kill_buf_rec_size));
		}
		kill_agent_args->slurm_addr[kill_agent_args->node_count] = 
					node_record_table_ptr[i].slurm_addr;
		pos = MAX_NAME_LEN * kill_agent_args->node_count;
		strncpy (&kill_agent_args->node_names[pos],
		         node_record_table_ptr[i].name, MAX_NAME_LEN);
		kill_agent_args->node_count++;

	}

	if (kill_agent_args->node_count == 0)
		xfree (kill_agent_args);
	else {
		debug ("Spawning slurmd msg(%d) agent", msg_type);
		if (pthread_attr_init (&kill_attr_agent))
			fatal ("pthread_attr_init error %m");
		if (pthread_attr_setdetachstate (&kill_attr_agent, 
						PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		if (pthread_attr_setscope (&kill_attr_agent, 
						PTHREAD_SCOPE_SYSTEM))
			error ("pthread_attr_setscope error %m");
#endif
		if (pthread_create (&kill_thread_agent, &kill_attr_agent, 
					agent, (void *)kill_agent_args)) {
			error ("pthread_create error %m");
			agent((void *)kill_agent_args);	/* do inline */
		}
	}
}


/* make_node_alloc - flag specified node as allocated to a job */
void make_node_alloc(struct node_record *node_ptr)
{
	int inx = node_ptr - node_record_table_ptr;
	uint16_t no_resp_flag, base_state;

	last_node_update = time (NULL);
	(node_ptr->run_job_cnt)++;
	bit_clear(idle_node_bitmap, inx);
	base_state   = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
	no_resp_flag = node_ptr->node_state &   NODE_STATE_NO_RESPOND ;
	if (base_state != NODE_STATE_COMPLETING)
		node_ptr->node_state = NODE_STATE_ALLOCATED | no_resp_flag;
}

/* make_node_comp - flag specified node as completing a job */
void make_node_comp(struct node_record *node_ptr)
{
	uint16_t no_resp_flag, base_state;

	last_node_update = time (NULL);
	base_state   = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
	no_resp_flag = node_ptr->node_state &   NODE_STATE_NO_RESPOND;
	if ((base_state == NODE_STATE_DOWN) ||
	    (base_state == NODE_STATE_DRAINED) ||
	    (base_state == NODE_STATE_DRAINING)) {
		debug3("Node %s being left in state %s", node_ptr->name, 
		       node_state_string((enum node_states)
					 node_ptr->node_state));
	} else {
		node_ptr->node_state = NODE_STATE_COMPLETING | no_resp_flag;
	}

	if (node_ptr->run_job_cnt)
		(node_ptr->run_job_cnt)--;
	else
		error("Node %s run_job_cnt underflow", node_ptr->name);
	(node_ptr->comp_job_cnt)++;
}

/* _make_node_down - flag specified node as down */
static void _make_node_down(struct node_record *node_ptr)
{
	int inx = node_ptr - node_record_table_ptr;
	uint16_t no_resp_flag;

	last_node_update = time (NULL);
	no_resp_flag = node_ptr->node_state & NODE_STATE_NO_RESPOND;
	node_ptr->node_state = NODE_STATE_DOWN | no_resp_flag;
	bit_clear (up_node_bitmap, inx);
	bit_clear (idle_node_bitmap, inx);
}

/*
 * make_node_idle - flag specified node as having completed a job
 * IN node_ptr - pointer to node reporting job completion
 * IN job_ptr - pointer to job that just completed
 */
void make_node_idle(struct node_record *node_ptr, 
		    struct job_record *job_ptr)
{
	int inx = node_ptr - node_record_table_ptr;
	uint16_t no_resp_flag, base_state;

	if ((job_ptr) &&			/* Specific job completed */
	    (bit_test(job_ptr->node_bitmap, inx))) {	/* Not a replay */
		last_job_update = time (NULL);
		bit_clear(job_ptr->node_bitmap, inx);
		if (job_ptr->node_cnt) {
			if ((--job_ptr->node_cnt) == 0)
				job_ptr->job_state &= (~JOB_COMPLETING);
		} else {
			error("node_cnt underflow on job_id %u", 
			      job_ptr->job_id);
		}

		if (node_ptr->comp_job_cnt)
			(node_ptr->comp_job_cnt)--;
		else
			error("Node %s comp_job_cnt underflow, job_id %u", 
			      node_ptr->name, job_ptr->job_id);
		if (node_ptr->comp_job_cnt > 0) 
			return;		/* More jobs completing */
	}

	last_node_update = time (NULL);
	base_state   = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
	no_resp_flag = node_ptr->node_state & NODE_STATE_NO_RESPOND;
	if ((base_state == NODE_STATE_DOWN) ||
	    (base_state == NODE_STATE_DRAINED)) {
		debug3("Node %s being left in state %s", node_ptr->name, 
		       node_state_string((enum node_states)base_state));
	} else if (base_state == NODE_STATE_DRAINING) {
		node_ptr->node_state = NODE_STATE_DRAINED;
		bit_clear(idle_node_bitmap, inx);
		bit_clear(up_node_bitmap, inx);
	} else if (node_ptr->run_job_cnt) {
		node_ptr->node_state = NODE_STATE_ALLOCATED | no_resp_flag;
	} else {
		node_ptr->node_state = NODE_STATE_IDLE | no_resp_flag;
		if (no_resp_flag == 0)
			bit_set(idle_node_bitmap, inx);
	}
}

