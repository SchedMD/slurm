/*****************************************************************************\
 *  node_mgr.c - manage the node records of slurm
 *	Note: there is a global node table (node_record_table_ptr), its 
 *	hash table (node_hash_table), time stamp (last_node_update) and 
 *	configuration list (config_list)
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/common/hostlist.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/slurmctld.h"

#define _DEBUG		0
#define BUF_SIZE 	4096
#define MAX_RETRIES	10

/* Global variables */
List config_list = NULL;		/* list of config_record entries */
struct node_record *node_record_table_ptr = NULL;	/* node records */
struct node_record **node_hash_table = NULL;	/* node_record hash table */ 
struct config_record default_config_record;
struct node_record default_node_record;
time_t last_bitmap_update = (time_t) NULL;	/* time of last node creation 
						 * or deletion */
time_t last_node_update = (time_t) NULL;	/* time of last update to 
						 * node records */
bitstr_t *avail_node_bitmap = NULL;	/* bitmap of available nodes */
bitstr_t *idle_node_bitmap  = NULL;	/* bitmap of idle nodes */
bitstr_t *share_node_bitmap = NULL;  	/* bitmap of sharable nodes */

static int	_delete_config_record (void);
static void 	_dump_node_state (struct node_record *dump_node_ptr, 
				  Buf buffer);
static int	_hash_index (char *name);
static void 	_list_delete_config (void *config_entry);
static int	_list_find_config (void *config_entry, void *key);
static void 	_make_node_down(struct node_record *node_ptr);
static void	_node_did_resp(struct node_record *node_ptr);
static void	_node_not_resp (struct node_record *node_ptr, time_t msg_time);
static void 	_pack_node (struct node_record *dump_node_ptr, Buf buffer);
static void	_sync_bitmaps(struct node_record *node_ptr, int job_count);
static bool 	_valid_node_state_change(enum node_states old, 
					enum node_states new);
#if _DEBUG
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
	int i;
	hostlist_t hl;
	char buf[8192];

	if (bitmap == NULL)
		return xstrdup("");

	hl = hostlist_create("");
	for (i = 0; i < node_record_count; i++) {
		if (bit_test (bitmap, i) == 0)
			continue;
		hostlist_push(hl, node_record_table_ptr[i].name);
	}
	hostlist_uniq(hl);
	hostlist_ranged_string(hl, sizeof(buf), buf);
	hostlist_destroy(hl);
	return xstrdup(buf);
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
	if (default_config_record.feature)
		config_ptr->feature = xstrdup(default_config_record.feature);
	else
		config_ptr->feature = NULL;

	if (list_append(config_list, config_ptr) == NULL)
		fatal ("create_config_record: unable to allocate memory");

	return config_ptr;
}


/* 
 * create_node_record - create a node record and set its values to defaults
 * IN config_ptr - pointer to node's configuration information
 * IN node_name - name of the node
 * RET pointer to the record or NULL if error
 * global: default_node_record - default node values
 * NOTE: the record's values are initialized to those of default_node_record, 
 *	node_name and config_ptr's cpus, real_memory, and tmp_disk values
 * NOTE: allocates memory at node_record_table_ptr that must be xfreed when  
 *	the global node table is no longer required
 */
struct node_record * 
create_node_record (struct config_record *config_ptr, char *node_name) 
{
	struct node_record *node_ptr;
	int old_buffer_size, new_buffer_size;

	last_node_update = time (NULL);
	xassert(config_ptr);
	xassert(node_name); 
	xassert(strlen (node_name) < MAX_NAME_LEN);

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

	node_ptr = node_record_table_ptr + (node_record_count++);
	strcpy (node_ptr->name, node_name);
	node_ptr->node_state = default_node_record.node_state;
	node_ptr->last_response = default_node_record.last_response;
	node_ptr->config_ptr = config_ptr;
	node_ptr->partition_ptr = NULL;
	/* these values will be overwritten when the node actually registers */
	node_ptr->cpus = config_ptr->cpus;
	node_ptr->real_memory = config_ptr->real_memory;
	node_ptr->tmp_disk = config_ptr->tmp_disk;
	xassert (node_ptr->magic = NODE_MAGIC)  /* set value */;
	last_bitmap_update = time (NULL);
	return node_ptr;
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
	DEF_TIMERS;

	START_TIMER;
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
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);

		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(log_fd);
		close(log_fd);
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
	END_TIMER;
	debug3("dump_all_node_state %s", TIME_STR);
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
	packstr (dump_node_ptr->reason, buffer);
	pack16  (dump_node_ptr->node_state, buffer);
	pack32  (dump_node_ptr->cpus, buffer);
	pack32  (dump_node_ptr->real_memory, buffer);
	pack32  (dump_node_ptr->tmp_disk, buffer);
}

/*
 * load_all_node_state - Load the node state from file, recover on slurmctld 
 *	restart. Execute this after loading the configuration file data.
 *	Data goes into common storage.
 * IN state_only - if true over-write only node state and reason fields
 * RET 0 or error code
 * NOTE: READ lock_slurmctld config before entry
 */
extern int load_all_node_state ( bool state_only )
{
	char *node_name, *reason = NULL, *data = NULL, *state_file;
	int data_allocated, data_read = 0, error_code = 0, node_cnt = 0;
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
		while (1) {
			data_read = read (state_fd, &data[data_size], BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error ("Read error on %s: %m", 
						state_file);
					break;
				}
			} else if (data_read == 0)     /* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close (state_fd);
	}
	xfree (state_file);
	unlock_state_files ();

	buffer = create_buf (data, data_size);
	safe_unpack_time (&time_stamp, buffer);

	while (remaining_buf (buffer) > 0) {
		safe_unpackstr_xmalloc (&node_name, &name_len, buffer);
		safe_unpackstr_xmalloc (&reason, &name_len, buffer);
		safe_unpack16 (&node_state,  buffer);
		safe_unpack32 (&cpus,        buffer);
		safe_unpack32 (&real_memory, buffer);
		safe_unpack32 (&tmp_disk,    buffer);
		node_state &= (~NODE_STATE_NO_RESPOND);

		/* validity test as possible */
		if ((cpus == 0) || (node_state  >= NODE_STATE_END)) {
			error ("Invalid data for node %s: cpus=%u, state=%u",
				node_name, cpus, node_state);
			error ("No more node data will be processed from the "
				"checkpoint file");
			xfree (node_name);
			error_code = EINVAL;
			break;
			
		}

		/* find record and perform update */
		node_ptr = find_node_record (node_name);
		if (node_ptr == NULL) {
			error ("Node %s has vanished from configuration", 
			       node_name);
			xfree(reason);
		} else if (state_only) {
			node_cnt++;
			if ((node_ptr->node_state == NODE_STATE_UNKNOWN) &&
			    ((node_state == NODE_STATE_DOWN) ||
			     (node_state == NODE_STATE_DRAINED) ||
			     (node_state == NODE_STATE_DRAINING)))
				node_ptr->node_state    = node_state;
			if (node_ptr->reason == NULL)
				node_ptr->reason = reason;
			else
				xfree(reason);
		} else {
			node_cnt++;
			node_ptr->node_state    = node_state;
			xfree(node_ptr->reason);
			node_ptr->reason        = reason;
			node_ptr->cpus          = cpus;
			node_ptr->real_memory   = real_memory;
			node_ptr->tmp_disk      = tmp_disk;
			node_ptr->last_response = (time_t) 0;
		}
		xfree (node_name);
	}

	info ("Recovered state of %d nodes", node_cnt);
	free_buf (buffer);
	return error_code;

unpack_error:
	error ("Incomplete node data checkpoint file");
	info("Recovered state of %d nodes", node_cnt);
	free_buf (buffer);
	return EFAULT;
}

/* 
 * find_node_record - find a record for node with specified name
 * input: name - name of the desired node 
 * output: return pointer to node record or NULL if not found
 * global: node_record_table_ptr - pointer to global node table
 *         node_hash_table - table of hash indecies
 */
struct node_record * 
find_node_record (char *name) 
{
	int i;

	/* try to find via hash table, if it exists */
	if (node_hash_table) {
		struct node_record *node_ptr;

		i = _hash_index (name);
		node_ptr = node_hash_table[i];
		while (node_ptr) {
			xassert(node_ptr->magic == NODE_MAGIC);
			if (strncmp(node_ptr->name, name, MAX_NAME_LEN) == 0)
				return node_ptr;
			node_ptr = node_ptr->node_next;
		}
		error ("find_node_record: lookup failure for %s", name);
	} 

	/* revert to sequential search */
	else {
		for (i = 0; i < node_record_count; i++) {
			if (strcmp (name, node_record_table_ptr[i].name) == 0)
				return (&node_record_table_ptr[i]);
		}
	}

	return (struct node_record *) NULL;
}


/* 
 * _hash_index - return a hash table index for the given node name 
 * IN name = the node's name
 * RET the hash table index
 */
static int _hash_index (char *name) 
{
	int i = 0;

	if (node_record_count == 0)
		return 0;	/* degenerate case */

	while (*name)
		i += (int) *name++;
	i %= node_record_count;
	return i;
}


/* 
 * init_node_conf - initialize the node configuration tables and values. 
 *	this should be called before creating any node or configuration 
 *	entries.
 * RET 0 if no error, otherwise an error code
 * global: node_record_table_ptr - pointer to global node table
 *         default_node_record - default values for node records
 *         default_config_record - default values for configuration records
 *         node_hash_table - table of hash indecies
 *         last_node_update - time of last node table update
 */
int init_node_conf (void) 
{
	last_node_update = time (NULL);

	node_record_count = 0;
	xfree(node_record_table_ptr);
	xfree(node_hash_table);

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
	else {
		config_list = list_create (&_list_delete_config);
		if (config_list == NULL)
			fatal("list_create malloc failure");
	}

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

	xassert(config_ptr);
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
 * IN node_names  - list of nodes
 * IN best_effort - if set don't return an error on invalid node name entries 
 * OUT bitmap     - set to bitmap or NULL on error 
 * RET 0 if no error, otherwise EINVAL
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must bit_free() memory at bitmap when no longer required
 */
extern int node_name2bitmap (char *node_names, bool best_effort, 
		bitstr_t **bitmap) 
{
	int rc = SLURM_SUCCESS;
	char *this_node_name;
	bitstr_t *my_bitmap;
	hostlist_t host_list;

	my_bitmap = (bitstr_t *) bit_alloc (node_record_count);
	if (my_bitmap == NULL)
		fatal("bit_alloc malloc failure");
	*bitmap = my_bitmap;
	
	if (node_names == NULL) {
		error ("node_name2bitmap: node_names is NULL");
		return rc;
	}

	if ( (host_list = hostlist_create (node_names)) == NULL) {
		/* likely a badly formatted hostlist */
		error ("hostlist_create on %s error:", node_names);
		if (!best_effort)
			rc = EINVAL;
		return rc;
	}

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		struct node_record *node_ptr;
		node_ptr = find_node_record (this_node_name);
		if (node_ptr) {
			bit_set (my_bitmap, (bitoff_t) (node_ptr - 
						node_record_table_ptr));
		} else {
			error ("node_name2bitmap: invalid node specified %s",
			       this_node_name);
			if (!best_effort) {
				free (this_node_name);
				rc = EINVAL;
				break;
			}
		}
		free (this_node_name);
	}
	hostlist_destroy (host_list);

	return rc;
}


/* 
 * pack_all_node - dump all configuration and node information for all nodes  
 *	in machine independent form (for network transmission)
 * OUT buffer_ptr - pointer to the stored data
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - node filtering options
 * IN uid - uid of user making request (for partition filtering)
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree the buffer at *buffer_ptr
 * NOTE: change slurm_load_node() in api/node_info.c when data format changes
 * NOTE: READ lock_slurmctld config before entry
 */
extern void pack_all_node (char **buffer_ptr, int *buffer_size,
		uint16_t show_flags, uid_t uid)
{
	int inx;
	uint32_t nodes_packed, tmp_offset;
	Buf buffer;
	time_t now = time(NULL);
	struct node_record *node_ptr = node_record_table_ptr;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf (BUF_SIZE*16);

	/* write header: version and time */
	nodes_packed = 0 ;
	pack32  (nodes_packed, buffer);
	pack_time  (now, buffer);

	/* write node records */
	part_filter_set(uid);
	for (inx = 0; inx < node_record_count; inx++, node_ptr++) {
		xassert (node_ptr->magic == NODE_MAGIC);
		xassert (node_ptr->config_ptr->magic ==  
			 CONFIG_MAGIC);

		if (((show_flags & SHOW_ALL) == 0) && 
		    (node_ptr->partition_ptr) && 
		    (node_ptr->partition_ptr->hidden))
			continue;

		_pack_node(node_ptr, buffer);
		nodes_packed ++ ;
	}
	part_filter_clear();

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
 * NOTE: READ lock_slurmctld config before entry
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
	packstr (dump_node_ptr->reason, buffer);
}


/* 
 * rehash_node - build a hash table of the node_record entries. 
 * global: node_record_table_ptr - pointer to global node table
 *         node_hash_table - table of hash indecies
 * NOTE: manages memory for node_hash_table
 */
void rehash_node (void) 
{
	int i, inx;

	xfree (node_hash_table);
	node_hash_table = xmalloc (sizeof (struct node_record *) * 
				node_record_count);

	for (i = 0; i < node_record_count; i++) {
		if (strlen (node_record_table_ptr[i].name) == 0)
			continue;	/* vestigial record */
		inx = _hash_index (node_record_table_ptr[i].name);
		node_record_table_ptr[i].node_next = node_hash_table[inx];
		node_hash_table[inx] = &node_record_table_ptr[i];
	}

#if _DEBUG
	_dump_hash();
#endif
	return;
}


/*
 * set_slurmd_addr - establish the slurm_addr for the slurmd on each node
 *	Uses common data structures.
 * NOTE: READ lock_slurmctld config before entry
 */
void set_slurmd_addr (void) 
{
	int i;

	for (i = 0; i < node_record_count; i++) {
		if (node_record_table_ptr[i].name[0] == '\0')
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
 * update_node - update the configuration data for one or more nodes
 * IN update_node_msg - update node request
 * RET SLURM_SUCCESS or error code
 * global: node_record_table_ptr - pointer to global node table
 */
int update_node ( update_node_msg_t * update_node_msg ) 
{
	int error_code = 0, base_state, node_inx;
	struct node_record *node_ptr;
	char  *this_node_name ;
	hostlist_t host_list;
	uint16_t no_resp_flag = 0, state_val;

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
		int err_code = 0;
		node_ptr = find_node_record (this_node_name);
		node_inx = node_ptr - node_record_table_ptr;
		if (node_ptr == NULL) {
			error ("update_node: node %s does not exist", 
				this_node_name);
			error_code = ESLURM_INVALID_NODE_NAME;
			free (this_node_name);
			break;
		}

		if (state_val != (uint16_t) NO_VAL) {
			base_state = node_ptr->node_state & 
			             (~NODE_STATE_NO_RESPOND);
			if (!_valid_node_state_change(base_state, state_val)) {
				info ("Invalid node state transition requested "
					"for node %s from=%s to=%s",
					this_node_name, 
					node_state_string(base_state),
					node_state_string(state_val));
				state_val = (uint16_t) NO_VAL;
				error_code = ESLURM_INVALID_NODE_STATE;
			}
		}
		if (state_val != (uint16_t) NO_VAL) {
			if (state_val == NODE_STATE_DOWN) {
				/* We must set node down before killing its jobs */
				_make_node_down(node_ptr);
				kill_running_job_by_node_name (this_node_name,
							       false);
			}
			else if (state_val == NODE_STATE_IDLE) {
				bit_set (avail_node_bitmap, node_inx);
				bit_set (idle_node_bitmap, node_inx);
				reset_job_priority();
			}
			else if (state_val == NODE_STATE_ALLOCATED) {
				bit_set   (avail_node_bitmap, node_inx);
				bit_clear (idle_node_bitmap, node_inx);
			}
			else if ((state_val == NODE_STATE_DRAINED) ||
			         (state_val == NODE_STATE_DRAINING)) {
				if ((node_ptr->run_job_cnt + node_ptr->comp_job_cnt) == 0)
					state_val = NODE_STATE_DRAINED;
				else
					state_val = NODE_STATE_DRAINING;
				bit_clear (avail_node_bitmap, node_inx);
			}
			else {
				info ("Invalid node state specified %d", 
					state_val);
				err_code = 1;
				error_code = ESLURM_INVALID_NODE_STATE;
			}

			if (err_code == 0) {
				no_resp_flag = node_ptr->node_state & 
				               NODE_STATE_NO_RESPOND;
				node_ptr->node_state = state_val | no_resp_flag;
				info ("update_node: node %s state set to %s",
					this_node_name, 
					node_state_string(state_val));
			}
		}

		if ((update_node_msg -> reason) && 
		    (update_node_msg -> reason[0])) {
			xfree(node_ptr->reason);
			node_ptr->reason = xstrdup(update_node_msg->reason);
			info ("update_node: node %s reason set to: %s",
				this_node_name, node_ptr->reason);
		}

		base_state = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
		if ((base_state != NODE_STATE_DRAINED)  && 
		    (base_state != NODE_STATE_DRAINING) &&
		    (base_state != NODE_STATE_DOWN))
			xfree(node_ptr->reason);

		free (this_node_name);
	}

	hostlist_destroy (host_list);
	return error_code;
}

/* 
 * drain_nodes - drain one or more nodes, 
 *  no-op for nodes already drained or draining
 * IN nodes - nodes to drain
 * IN reason - reason to drain the nodes
 * RET SLURM_SUCCESS or error code
 * global: node_record_table_ptr - pointer to global node table
 */
extern int drain_nodes ( char *nodes, char *reason )
{
	int error_code = 0, node_inx;
	struct node_record *node_ptr;
	char  *this_node_name ;
	hostlist_t host_list;
	uint16_t base_state, no_resp_flag, state_val;

	if ((nodes == NULL) || (nodes[0] == '\0')) {
		error ("drain_nodes: invalid node name  %s", nodes);
		return ESLURM_INVALID_NODE_NAME;
	}
	
	if ( (host_list = hostlist_create (nodes)) == NULL) {
		error ("hostlist_create error on %s: %m", nodes);
		return ESLURM_INVALID_NODE_NAME;
	}

	last_node_update = time (NULL);
	while ( (this_node_name = hostlist_shift (host_list)) ) {
		node_ptr = find_node_record (this_node_name);
		node_inx = node_ptr - node_record_table_ptr;
		if (node_ptr == NULL) {
			error ("drain_nodes: node %s does not exist", 
				this_node_name);
			error_code = ESLURM_INVALID_NODE_NAME;
			free (this_node_name);
			break;
		}

		base_state = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
		no_resp_flag = node_ptr->node_state &  NODE_STATE_NO_RESPOND;
		if ((base_state == NODE_STATE_DRAINED)
		||  (base_state == NODE_STATE_DRAINING)) {
			/* state already changed, nothing to do */
			free (this_node_name);
			continue;
		}

		if ((node_ptr->run_job_cnt + node_ptr->comp_job_cnt) == 0)
			state_val = NODE_STATE_DRAINED;
		else
			state_val = NODE_STATE_DRAINING;
		node_ptr->node_state = state_val | no_resp_flag;
		bit_clear (avail_node_bitmap, node_inx);
		info ("drain_nodes: node %s state set to %s",
			this_node_name, node_state_string(state_val));

		xfree(node_ptr->reason);
		node_ptr->reason = xstrdup(reason);

		free (this_node_name);
	}

	hostlist_destroy (host_list);
	return error_code;
}
/* Return true if admin request to change node state from old to new is valid */
static bool _valid_node_state_change(enum node_states old, enum node_states new)
{
	if (old == new)
		return true;

	switch (new) {
		case NODE_STATE_DOWN:
		case NODE_STATE_DRAINED:
		case NODE_STATE_DRAINING:
			return true;
			break;

		case NODE_STATE_IDLE:
			if ((old == NODE_STATE_DRAINED) ||
			    (old == NODE_STATE_DOWN))
				return true;
			break;

		case NODE_STATE_ALLOCATED:
			if (old == NODE_STATE_DRAINING)
				return true;
			break;

		default:	/* All others invalid */
			break;
	}

	return false;
}

/*
 * validate_node_specs - validate the node's specifications as valid, 
 *   if not set state to down, in any case update last_response
 * IN node_name - name of the node
 * IN cpus - number of cpus measured
 * IN real_memory - mega_bytes of real_memory measured
 * IN tmp_disk - mega_bytes of tmp_disk measured
 * IN job_count - number of jobs allocated to this node
 * IN status - node status code
 * RET 0 if no error, ENOENT if no such node, EINVAL if values too low
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: READ lock_slurmctld config before entry
 */
extern int 
validate_node_specs (char *node_name, uint32_t cpus, 
			uint32_t real_memory, uint32_t tmp_disk, 
			uint32_t job_count, uint32_t status)
{
	int error_code;
	struct config_record *config_ptr;
	struct node_record *node_ptr;
	char *reason_down = NULL;

	node_ptr = find_node_record (node_name);
	if (node_ptr == NULL)
		return ENOENT;
	node_ptr->last_response = time (NULL);

	config_ptr = node_ptr->config_ptr;
	error_code = 0;

	if (cpus < config_ptr->cpus) {
		error ("Node %s has low cpu count %u", node_name, cpus);
		error_code  = EINVAL;
		reason_down = "Low CPUs";
	}
	if ((node_ptr->cpus != cpus) && (node_ptr->partition_ptr) &&
	    (slurmctld_conf.fast_schedule == 0))
		node_ptr->partition_ptr->total_cpus += (cpus - node_ptr->cpus);
	node_ptr->cpus = cpus;

	if (real_memory < config_ptr->real_memory) {
		error ("Node %s has low real_memory size %u", 
		       node_name, real_memory);
		error_code  = EINVAL;
		reason_down = "Low RealMemory";
	}
	node_ptr->real_memory = real_memory;

	if (tmp_disk < config_ptr->tmp_disk) {
		error ("Node %s has low tmp_disk size %u",
		       node_name, tmp_disk);
		error_code = EINVAL;
		reason_down = "Low TmpDisk";
	}
	node_ptr->tmp_disk = tmp_disk;

	/* Every node in a given partition must have the same
	 * processor count with elan switch at present */
	if ((slurmctld_conf.fast_schedule == 0)  &&
	    (node_ptr->config_ptr->cpus != cpus) &&
	    (strcmp(slurmctld_conf.switch_type, "switch/elan") == 0)) {
		error ("Node %s processor count inconsistent with rest "
			"of partition", node_name);
		error_code = EINVAL;
		reason_down = "Inconsistent CPU count in partition";
	}

	if (node_ptr->node_state & NODE_STATE_NO_RESPOND) {
		last_node_update = time (NULL);
		reset_job_priority();
		node_ptr->node_state &= (uint16_t) (~NODE_STATE_NO_RESPOND);
	}
	if (error_code) {
		if ((node_ptr->node_state != NODE_STATE_DRAINING) &&
		    (node_ptr->node_state != NODE_STATE_DRAINED)  &&
		    (node_ptr->node_state != NODE_STATE_DOWN)) {
			last_node_update = time (NULL);
			error ("Setting node %s state to DOWN", node_name);
			set_node_down(node_name, reason_down);
		}
		_sync_bitmaps(node_ptr, job_count);
	} else if (status == ESLURMD_PROLOG_FAILED) {
		if ((node_ptr->node_state != NODE_STATE_DRAINING) &&
		    (node_ptr->node_state != NODE_STATE_DRAINED)) {
			last_node_update = time (NULL);
			error ("Prolog failure on node %s, state to DOWN",
				node_name);
			set_node_down(node_name, "Prolog failed");
		}
	} else {
		if (node_ptr->node_state == NODE_STATE_UNKNOWN) {
			last_node_update = time (NULL);
			reset_job_priority();
			debug("validate_node_specs: node %s has registered", 
				node_name);
			if (job_count)
				node_ptr->node_state = NODE_STATE_ALLOCATED;
			else
				node_ptr->node_state = NODE_STATE_IDLE;
			xfree(node_ptr->reason);
		} else if (node_ptr->node_state == NODE_STATE_DRAINING) {
			if (job_count == 0) {
				last_node_update = time (NULL);
				node_ptr->node_state = NODE_STATE_DRAINED;
			}
		} else if (node_ptr->node_state == NODE_STATE_DRAINED) {
			if (job_count != 0) {
				last_node_update = time (NULL);
				node_ptr->node_state = NODE_STATE_DRAINING;
			}
		} else if ((node_ptr->node_state == NODE_STATE_DOWN) &&
		           (slurmctld_conf.ret2service == 1)) {
			last_node_update = time (NULL);
			if (job_count)
				node_ptr->node_state = NODE_STATE_ALLOCATED;
			else
				node_ptr->node_state = NODE_STATE_IDLE;
			info ("validate_node_specs: node %s returned to service", 
			      node_name);
			xfree(node_ptr->reason);
			reset_job_priority();
		} else if ((node_ptr->node_state == NODE_STATE_ALLOCATED) &&
			   (job_count == 0)) {	/* job vanished */
			last_node_update = time (NULL);
			node_ptr->node_state = NODE_STATE_IDLE;
		} else if ((node_ptr->node_state == NODE_STATE_COMPLETING) &&
			   (job_count == 0)) {	/* job already done */
			last_node_update = time (NULL);
			node_ptr->node_state = NODE_STATE_IDLE;
		}
		_sync_bitmaps(node_ptr, job_count);
	}

	return error_code;
}

/*
 * validate_nodes_via_front_end - validate all nodes on a cluster as having
 *	a valid configuration as soon as the front-end registers. Individual
 *	nodes will not register with this configuration
 * IN job_count - number of jobs which should be running on cluster
 * IN job_id_ptr - pointer to array of job_ids that should be on cluster
 * IN step_id_ptr - pointer to array of job step ids that should be on cluster
 * IN status - cluster status code
 * RET 0 if no error, SLURM error code otherwise
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: READ lock_slurmctld config before entry
 */
extern int validate_nodes_via_front_end(uint32_t job_count, 
			uint32_t *job_id_ptr, uint16_t *step_id_ptr,
			uint32_t status)
{
	int error_code = 0, i, jobs_on_node;
	bool updated_job = false;
	struct job_record *job_ptr;
	struct config_record *config_ptr;
	struct node_record *node_ptr;
	time_t now = time(NULL);
	ListIterator job_iterator;
	hostlist_t return_hostlist = NULL, reg_hostlist = NULL;
	hostlist_t prolog_hostlist = NULL;
	char host_str[64];

	/* First validate the job info */
	node_ptr = &node_record_table_ptr[0];	/* All msg send to node zero,
				 * the front-end for the wholel cluster */
	for (i = 0; i < job_count; i++) {
		if ( (job_id_ptr[i] >= MIN_NOALLOC_JOBID) && 
		     (job_id_ptr[i] <= MAX_NOALLOC_JOBID) ) {
			info("NoAllocate job %u.%u reported",
				job_id_ptr[i], step_id_ptr[i]);
			continue;
		}

		job_ptr = find_job_record(job_id_ptr[i]);
		if (job_ptr == NULL) {
			error("Orphan job %u.%u reported",
			      job_id_ptr[i], step_id_ptr[i]);
			kill_job_on_node(job_id_ptr[i], job_ptr, node_ptr);
		}

		else if (job_ptr->job_state == JOB_RUNNING) {
			debug3("Registered job %u.%u",
			       job_id_ptr[i], step_id_ptr[i]);
			if (job_ptr->batch_flag) {
				/* NOTE: Used for purging defunct batch jobs */
				job_ptr->time_last_active = now;
			}
		}

		else if (job_ptr->job_state & JOB_COMPLETING) {
			/* Re-send kill request as needed, 
			 * not necessarily an error */
			kill_job_on_node(job_id_ptr[i], job_ptr, node_ptr);
		}


		else if (job_ptr->job_state == JOB_PENDING) {
			error("Registered PENDING job %u.%u",
				job_id_ptr[i], step_id_ptr[i]);
			/* FIXME: Could possibly recover the job */
			job_ptr->job_state = JOB_FAILED;
			last_job_update    = now;
			job_ptr->start_time = job_ptr->end_time  = now;
			delete_job_details(job_ptr);
			kill_job_on_node(job_id_ptr[i], job_ptr, node_ptr);
			job_completion_logger(job_ptr);
		}

		else {		/* else job is supposed to be done */
			error("Registered job %u.%u in state %s",
				job_id_ptr[i], step_id_ptr[i], 
				job_state_string(job_ptr->job_state));
			kill_job_on_node(job_id_ptr[i], job_ptr, node_ptr);
		}
	}

	/* purge orphan batch jobs */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((job_ptr->job_state != JOB_RUNNING) ||
		    (job_ptr->batch_flag == 0)          ||
#ifdef HAVE_BGL
		    /* slurmd does not report job presence until after prolog 
		     * completes which waits for bglblock boot to complete.  
		     * This can take several minutes on BlueGene. */
		    (difftime(now, job_ptr->time_last_active) <= 
				(300 + 20 * job_ptr->node_cnt)))
#else
		    (difftime(now, job_ptr->time_last_active) <= 5))
#endif
			continue;

		info("Killing orphan batch job %u", job_ptr->job_id);
		job_complete(job_ptr->job_id, 0, false, 0);
	}
	list_iterator_destroy(job_iterator);

	/* Now validate the node info */
	for (i=0; i<node_record_count; i++) {
		node_ptr = &node_record_table_ptr[i];
		config_ptr = node_ptr->config_ptr;
		jobs_on_node = node_ptr->run_job_cnt + node_ptr->comp_job_cnt;
		node_ptr->last_response = time (NULL);

		if (node_ptr->node_state & NODE_STATE_NO_RESPOND) {
			updated_job = true;
			node_ptr->node_state &= (uint16_t) 
					(~NODE_STATE_NO_RESPOND);
		}

		if (status == ESLURMD_PROLOG_FAILED) {
			if ((node_ptr->node_state != NODE_STATE_DRAINING) &&
			    (node_ptr->node_state != NODE_STATE_DRAINED)) {
				updated_job = true;
				if (prolog_hostlist)
					(void) hostlist_push_host(
						prolog_hostlist, 
						node_ptr->name);
				else
					prolog_hostlist = hostlist_create(
						node_ptr->name);
				set_node_down(node_ptr->name, "Prolog failed");
			}
		} else {
			if (node_ptr->node_state == NODE_STATE_UNKNOWN) {
				updated_job = true;
				if (reg_hostlist)
					(void) hostlist_push_host(
						reg_hostlist, node_ptr->name);
				else
					reg_hostlist = hostlist_create(
						node_ptr->name);
				if (jobs_on_node)
					node_ptr->node_state = NODE_STATE_ALLOCATED;
				else
					node_ptr->node_state = NODE_STATE_IDLE;
				xfree(node_ptr->reason);
			} else if (node_ptr->node_state == NODE_STATE_DRAINING) {
				if (jobs_on_node== 0) {
					updated_job = true;
					node_ptr->node_state = NODE_STATE_DRAINED;
				}
			} else if (node_ptr->node_state == NODE_STATE_DRAINED) {
				if (jobs_on_node != 0) {
					updated_job = true;
					node_ptr->node_state = NODE_STATE_DRAINING;
				}
			} else if ((node_ptr->node_state == NODE_STATE_DOWN) &&
			           (slurmctld_conf.ret2service == 1)) {
				updated_job = true;
				if (jobs_on_node)
					node_ptr->node_state = NODE_STATE_ALLOCATED;
				else
					node_ptr->node_state = NODE_STATE_IDLE;
				if (return_hostlist)
					(void) hostlist_push_host(
						return_hostlist, node_ptr->name);
				else
					return_hostlist = hostlist_create(
						node_ptr->name);
				xfree(node_ptr->reason);
			} else if ((node_ptr->node_state == NODE_STATE_ALLOCATED) &&
				   (jobs_on_node == 0)) {	/* job vanished */
				updated_job = true;
				node_ptr->node_state = NODE_STATE_IDLE;
			} else if ((node_ptr->node_state == NODE_STATE_COMPLETING) &&
				   (jobs_on_node == 0)) {	/* job already done */
				updated_job = true;
				node_ptr->node_state = NODE_STATE_IDLE;
			}
			_sync_bitmaps(node_ptr, jobs_on_node);
		}
	}

	if (prolog_hostlist) {
		hostlist_uniq(prolog_hostlist);
		hostlist_ranged_string(prolog_hostlist, sizeof(host_str),
			host_str);
		error("Prolog failure on nodes %s, set to DOWN", host_str);
		hostlist_destroy(prolog_hostlist);
	}
	if (reg_hostlist) {
		hostlist_uniq(reg_hostlist);
		hostlist_ranged_string(reg_hostlist, sizeof(host_str),
			host_str);
		debug("Nodes %s have registerd", host_str);
		hostlist_destroy(reg_hostlist);
	}
	if (return_hostlist) {
		hostlist_uniq(return_hostlist);
		hostlist_ranged_string(return_hostlist, sizeof(host_str),
			host_str);
		info("Nodes %s returned to service", host_str);
		hostlist_destroy(return_hostlist);
	}

	if (updated_job) {
		last_node_update = time (NULL);
		reset_job_priority();
	}
	return error_code;;
}

/* Sync idle, share, and avail_node_bitmaps for a given node */
static void _sync_bitmaps(struct node_record *node_ptr, int job_count)
{
	int node_inx = node_ptr - node_record_table_ptr;

	if (job_count == 0) {
		bit_set (idle_node_bitmap, node_inx);
		bit_set (share_node_bitmap, node_inx);
		if (node_ptr->node_state == NODE_STATE_DRAINING)
			node_ptr->node_state = NODE_STATE_DRAINED;
	} else {
		if (node_ptr->node_state == NODE_STATE_DRAINED)
			node_ptr->node_state = NODE_STATE_DRAINING;
	}

	if ((node_ptr->node_state == NODE_STATE_DOWN)     ||
	    (node_ptr->node_state == NODE_STATE_DRAINING) ||
	    (node_ptr->node_state == NODE_STATE_DRAINED))
		bit_clear (avail_node_bitmap, node_inx);
	else
		bit_set   (avail_node_bitmap, node_inx);
}

/*
 * node_did_resp - record that the specified node is responding
 * IN name - name of the node
 * NOTE: READ lock_slurmctld config before entry
 */ 
void node_did_resp (char *name)
{
	struct node_record *node_ptr;
#ifdef HAVE_FRONT_END		/* Fake all other nodes */
	int i;

	for (i=0; i<node_record_count; i++) {
		node_ptr = &node_record_table_ptr[i];
		_node_did_resp(node_ptr);
	}
#else
	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("node_did_resp unable to find node %s", name);
		return;
	}
	_node_did_resp(node_ptr);
#endif
}

static void _node_did_resp(struct node_record *node_ptr)
{
	int node_inx;
	uint16_t resp_state;

	node_inx = node_ptr - node_record_table_ptr;
	node_ptr->last_response = time (NULL);
	resp_state = node_ptr->node_state & NODE_STATE_NO_RESPOND;
	if (resp_state) {
		info("Node %s now responding", node_ptr->name);
		last_node_update = time (NULL);
		reset_job_priority();
		node_ptr->node_state &= (uint16_t) (~NODE_STATE_NO_RESPOND);
	}
	if (node_ptr->node_state == NODE_STATE_UNKNOWN) {
		last_node_update = time (NULL);
		node_ptr->node_state = NODE_STATE_IDLE;
	}
	if ((node_ptr->node_state == NODE_STATE_DOWN) &&
	    (slurmctld_conf.ret2service == 1) &&
	    (node_ptr->reason != NULL) && 
	    (strcmp(node_ptr->reason, "Not responding") == 0)) {
		last_node_update = time (NULL);
		node_ptr->node_state = NODE_STATE_IDLE;
		info("node_did_resp: node %s returned to service", 
			node_ptr->name);
		xfree(node_ptr->reason);
	}
	if (node_ptr->node_state == NODE_STATE_IDLE) {
		bit_set (idle_node_bitmap, node_inx);
		bit_set (share_node_bitmap, node_inx);
	}
	if ((node_ptr->node_state == NODE_STATE_DOWN)     ||
	    (node_ptr->node_state == NODE_STATE_DRAINING) ||
	    (node_ptr->node_state == NODE_STATE_DRAINED))
		bit_clear (avail_node_bitmap, node_inx);
	else
		bit_set   (avail_node_bitmap, node_inx);
	return;
}

/*
 * node_not_resp - record that the specified node is not responding 
 * IN name - name of the node
 * IN msg_time - time message was sent 
 */
void node_not_resp (char *name, time_t msg_time)
{
	struct node_record *node_ptr;
#ifdef HAVE_FRONT_END		/* Fake all other nodes */
	int i;
	char host_str[64];
	hostlist_t no_resp_hostlist = hostlist_create("");

	for (i=0; i<node_record_count; i++) {
		node_ptr = &node_record_table_ptr[i];
		(void) hostlist_push_host(no_resp_hostlist, node_ptr->name);
		_node_not_resp(node_ptr, msg_time);
	}
	hostlist_uniq(no_resp_hostlist);
	hostlist_ranged_string(no_resp_hostlist, sizeof(host_str), host_str);
	error("Nodes %s not responding", host_str);
	hostlist_destroy(no_resp_hostlist);
#else
	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("node_not_resp unable to find node %s", name);
		return;
	}
	error("Node %s not responding", node_ptr->name);
	_node_not_resp(node_ptr, msg_time);
#endif
}

static void _node_not_resp (struct node_record *node_ptr, time_t msg_time)
{
	int i;

	i = node_ptr - node_record_table_ptr;
	if (node_ptr->node_state & NODE_STATE_NO_RESPOND)
		return;		/* Already known to be not responding */

	if (node_ptr->last_response >= msg_time) {
		debug("node_not_resp: node %s responded since msg sent", 
			node_ptr->name);
		return;
	}
	last_node_update = time (NULL);
	bit_clear (avail_node_bitmap, i);
	node_ptr->node_state |= NODE_STATE_NO_RESPOND;
	return;
}

/*
 * set_node_down - make the specified node's state DOWN if possible
 *	(not in a DRAIN state), kill jobs as needed 
 * IN name - name of the node 
 * IN reason - why the node is DOWN
 */
void set_node_down (char *name, char *reason)
{
	struct node_record *node_ptr;
	uint16_t base_state;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("node_not_resp unable to find node %s", name);
		return;
	}

	base_state = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
	if ((base_state != NODE_STATE_DRAINING) &&
	    (base_state != NODE_STATE_DRAINED))
		_make_node_down(node_ptr);
	(void) kill_running_job_by_node_name(name, false);
	if (node_ptr->reason == NULL)
		node_ptr->reason = xstrdup(reason);

	return;
}

/*
 * is_node_down - determine if the specified node's state is DOWN
 * IN name - name of the node
 * RET true if node exists and is down, otherwise false 
 */
bool is_node_down (char *name)
{
	struct node_record *node_ptr;
	uint16_t base_state;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("is_node_down unable to find node %s", name);
		return false;
	}

	base_state = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
	if (base_state == NODE_STATE_DOWN)
		return true;
	return false;
}

/*
 * is_node_resp - determine if the specified node's state is responding
 * IN name - name of the node
 * RET true if node exists and is responding, otherwise false 
 */
bool is_node_resp (char *name)
{
	struct node_record *node_ptr;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("is_node_resp unable to find node %s", name);
		return false;
	}

	if (node_ptr->node_state & NODE_STATE_NO_RESPOND)
		return false;
	return true;
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

#if _DEBUG
/* 
 * _dump_hash - print the node_hash_table contents, used for debugging
 *	or analysis of hash technique 
 * global: node_record_table_ptr - pointer to global node table
 *         node_hash_table - table of hash indecies
 */
static void _dump_hash (void) 
{
	int i, inx;
	struct node_record *node_ptr;

	if (node_hash_table == NULL)
		return;
	for (i = 0; i < node_record_count; i++) {
		node_ptr = node_hash_table[i];
		while (node_ptr) {
			inx = node_ptr -  node_record_table_ptr;
			debug3("node_hash[%d]:%d", i, inx);
			node_ptr = node_ptr->node_next;
		}
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
	int retries = 0;

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
#ifdef HAVE_FRONT_END		/* Operate only on front-end */
		break;
#endif
	}

	if (kill_agent_args->node_count == 0)
		xfree (kill_agent_args);
	else {
		debug ("Spawning agent msg_type=%d", msg_type);
		slurm_attr_init (&kill_attr_agent);
		if (pthread_attr_setdetachstate (&kill_attr_agent, 
						PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate error %m");
		while (pthread_create (&kill_thread_agent, &kill_attr_agent, 
					agent, (void *)kill_agent_args)) {
			error ("pthread_create error %m");
			if (++retries > MAX_RETRIES)
				fatal("Can't create pthread");
			sleep(1);	/* sleep and try again */
		}
	}
}


/* make_node_alloc - flag specified node as allocated to a job
 * IN node_ptr - pointer to node being allocated
 * IN job_ptr  - pointer to job that is starting
 */
extern void make_node_alloc(struct node_record *node_ptr,
		            struct job_record *job_ptr)
{
	int inx = node_ptr - node_record_table_ptr;
	uint16_t no_resp_flag, base_state;

	last_node_update = time (NULL);

	(node_ptr->run_job_cnt)++;
	bit_clear(idle_node_bitmap, inx);
	if (job_ptr->details && (job_ptr->details->shared == 0)) {
		bit_clear(share_node_bitmap, inx);
		(node_ptr->no_share_job_cnt)++;
	}

	base_state   = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
	no_resp_flag = node_ptr->node_state &   NODE_STATE_NO_RESPOND ;
	if (base_state != NODE_STATE_COMPLETING)
		node_ptr->node_state = NODE_STATE_ALLOCATED | no_resp_flag;
	xfree(node_ptr->reason);
}

/* make_node_comp - flag specified node as completing a job
 * IN node_ptr - pointer to node marked for completion of job
 * IN job_ptr - pointer to job that is completing
 */
extern void make_node_comp(struct node_record *node_ptr,
			   struct job_record *job_ptr)
{
	int inx = node_ptr - node_record_table_ptr;
	uint16_t no_resp_flag, base_state;

	xassert(node_ptr);
	last_node_update = time (NULL);
	if (node_ptr->run_job_cnt)
		(node_ptr->run_job_cnt)--;
	else
		error("Node %s run_job_cnt underflow", node_ptr->name);

	if (job_ptr->details && (job_ptr->details->shared == 0)) {
		if (node_ptr->no_share_job_cnt)
			(node_ptr->no_share_job_cnt)--;
		else
			error("Node %s no_share_job_cnt underflow", 
				node_ptr->name);
		if (node_ptr->no_share_job_cnt == 0)
			bit_set(share_node_bitmap, inx);
	}

	base_state   = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
	no_resp_flag = node_ptr->node_state &   NODE_STATE_NO_RESPOND;
	if (base_state != NODE_STATE_DOWN)
		(node_ptr->comp_job_cnt)++;	/* Don't verify  RPC */

	if ((base_state == NODE_STATE_DRAINING) && 
	    (node_ptr->run_job_cnt  == 0) &&
	    (node_ptr->comp_job_cnt == 0)) {
		bit_set(idle_node_bitmap, inx);
		node_ptr->node_state = NODE_STATE_DRAINED | no_resp_flag;
	}

	if ((base_state == NODE_STATE_DOWN) ||
	    (base_state == NODE_STATE_DRAINED) ||
	    (base_state == NODE_STATE_DRAINING)) {
		debug3("make_node_comp: Node %s being left in state %s", 
		       node_ptr->name, 
		       node_state_string((enum node_states)
					 node_ptr->node_state));
	} else {
		node_ptr->node_state = NODE_STATE_COMPLETING | no_resp_flag;
		xfree(node_ptr->reason);
	}
}

/* _make_node_down - flag specified node as down */
static void _make_node_down(struct node_record *node_ptr)
{
	int inx = node_ptr - node_record_table_ptr;
	uint16_t no_resp_flag;

	xassert(node_ptr);
	last_node_update = time (NULL);
	no_resp_flag = node_ptr->node_state & NODE_STATE_NO_RESPOND;
	node_ptr->node_state = NODE_STATE_DOWN | no_resp_flag;
	bit_clear (avail_node_bitmap, inx);
	bit_set   (idle_node_bitmap,  inx);
	bit_set   (share_node_bitmap, inx);
}

/*
 * make_node_idle - flag specified node as having finished with a job
 * IN node_ptr - pointer to node reporting job completion
 * IN job_ptr - pointer to job that just completed
 */
void make_node_idle(struct node_record *node_ptr, 
		    struct job_record *job_ptr)
{
	int inx = node_ptr - node_record_table_ptr;
	uint16_t no_resp_flag, base_state;

	xassert(node_ptr);

	if (job_ptr			/* Specific job completed */
	&&  (job_ptr->job_state & JOB_COMPLETING)	/* Not a replay */
	&&  (bit_test(job_ptr->node_bitmap, inx))) {	/* Not a replay */
		last_job_update = time (NULL);
		bit_clear(job_ptr->node_bitmap, inx);
		if (job_ptr->node_cnt) {
			if ((--job_ptr->node_cnt) == 0) {
				time_t delay;
				delay = last_job_update - job_ptr->end_time;
				if (delay > 60)
					info("Job %u completion process took "
						"%ld seconds", job_ptr->job_id,
						(long) delay);
				delete_all_step_records(job_ptr);
				job_ptr->job_state &= (~JOB_COMPLETING);
			}
		} else {
			error("node_cnt underflow on job_id %u", 
			      job_ptr->job_id);
		}

		if (job_ptr->job_state == JOB_RUNNING) {
			/* Remove node from running job */
			if (node_ptr->run_job_cnt)
				(node_ptr->run_job_cnt)--;
			else
				error("Node %s run_job_cnt underflow", 
				      node_ptr->name);
		} else {
			if (node_ptr->comp_job_cnt)
				(node_ptr->comp_job_cnt)--;
			else
				error("Node %s comp_job_cnt underflow, job_id %u", 
				      node_ptr->name, job_ptr->job_id);
			if (node_ptr->comp_job_cnt > 0) 
				return;		/* More jobs completing */
		}
	}

	last_node_update = time (NULL);
	base_state   = node_ptr->node_state & (~NODE_STATE_NO_RESPOND);
	no_resp_flag = node_ptr->node_state & NODE_STATE_NO_RESPOND;
	if ((base_state == NODE_STATE_DRAINING) && 
	    (node_ptr->run_job_cnt == 0) && 
	    (node_ptr->comp_job_cnt == 0)) {
		node_ptr->node_state = NODE_STATE_DRAINED;
		bit_set(idle_node_bitmap, inx);
		bit_clear(avail_node_bitmap, inx);
		debug3("make_node_idle: Node %s is %s", 
		       node_ptr->name, 
		       node_state_string((enum node_states)base_state));
	} else if ((base_state == NODE_STATE_DOWN)     ||
	           (base_state == NODE_STATE_DRAINING) ||
	           (base_state == NODE_STATE_DRAINED)) {
		debug3("make_node_idle: Node %s being left in state %s", 
		       node_ptr->name, 
		       node_state_string((enum node_states)base_state));
	} else if (node_ptr->comp_job_cnt) {
		node_ptr->node_state = NODE_STATE_COMPLETING | no_resp_flag;
	} else if (node_ptr->run_job_cnt) {
		node_ptr->node_state = NODE_STATE_ALLOCATED | no_resp_flag;
	} else {
		node_ptr->node_state = NODE_STATE_IDLE | no_resp_flag;
		if (no_resp_flag == 0)
			bit_set(idle_node_bitmap, inx);
	}
}

/* node_fini - free all memory associated with node records */
void node_fini(void)
{
	int i;

	if (config_list) {
		list_destroy(config_list);
		config_list = NULL;
	}

	for (i=0; i< node_record_count; i++)
		xfree(node_record_table_ptr[i].reason);

	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(avail_node_bitmap);
	FREE_NULL_BITMAP(share_node_bitmap);

	xfree(node_record_table_ptr);
	xfree(node_hash_table);
}
