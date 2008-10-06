/*****************************************************************************\
 *  node_mgr.c - manage the node records of slurm
 *	Note: there is a global node table (node_record_table_ptr), its 
 *	hash table (node_hash_table), time stamp (last_node_update) and 
 *	configuration list (config_list)
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
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
#include "src/common/parse_time.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/trigger_mgr.h"
#include "src/plugins/select/bluegene/plugin/bg_boot_time.h"

#define _DEBUG		0
#define MAX_RETRIES	10

/* Change NODE_STATE_VERSION value when changing the state save format */
#define NODE_STATE_VERSION      "VER002"

/* Global variables */
List config_list = NULL;		/* list of config_record entries */
struct node_record *node_record_table_ptr = NULL;	/* node records */
struct node_record **node_hash_table = NULL;	/* node_record hash table */ 
time_t last_bitmap_update = (time_t) NULL;	/* time of last node creation 
						 * or deletion */
time_t last_node_update = (time_t) NULL;	/* time of last update to 
						 * node records */
bitstr_t *avail_node_bitmap = NULL;	/* bitmap of available nodes */
bitstr_t *idle_node_bitmap  = NULL;	/* bitmap of idle nodes */
bitstr_t *share_node_bitmap = NULL;  	/* bitmap of sharable nodes */
bitstr_t *up_node_bitmap    = NULL;  	/* bitmap of non-down nodes */

static int	_delete_config_record (void);
static void 	_dump_node_state (struct node_record *dump_node_ptr, 
				  Buf buffer);
static struct node_record * _find_alias_node_record (char *name);
static int	_hash_index (char *name);
static void 	_list_delete_config (void *config_entry);
static int	_list_find_config (void *config_entry, void *key);
static void 	_make_node_down(struct node_record *node_ptr,
				time_t event_time);
static void	_node_did_resp(struct node_record *node_ptr);
static bool	_node_is_hidden(struct node_record *node_ptr);
static void 	_pack_node (struct node_record *dump_node_ptr, bool cr_flag,
				Buf buffer);
static void	_sync_bitmaps(struct node_record *node_ptr, int job_count);
static void	_update_config_ptr(bitstr_t *bitmap,
				struct config_record *config_ptr);
static int	_update_node_features(char *node_names, char *features);
static bool 	_valid_node_state_change(uint16_t old, uint16_t new); 
#ifndef HAVE_FRONT_END
static void	_node_not_resp (struct node_record *node_ptr, time_t msg_time);
#endif
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
 * NOTE: memory allocated will remain in existence until 
 *	_delete_config_record() is called to delete all configuration records
 */
struct config_record * create_config_record (void) 
{
	struct config_record *config_ptr;

	last_node_update = time (NULL);
	config_ptr = (struct config_record *)
		     xmalloc (sizeof (struct config_record));

	config_ptr->nodes = NULL;
	config_ptr->node_bitmap = NULL;
	xassert (config_ptr->magic = CONFIG_MAGIC);  /* set value */

	if (list_append(config_list, config_ptr) == NULL)
		fatal ("create_config_record: unable to allocate memory");

	return config_ptr;
}


/* 
 * create_node_record - create a node record and set its values to defaults
 * IN config_ptr - pointer to node's configuration information
 * IN node_name - name of the node
 * RET pointer to the record or NULL if error
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
	node_ptr->name = xstrdup(node_name);
	node_ptr->last_response = (time_t)0;
	node_ptr->config_ptr = config_ptr;
	node_ptr->part_cnt = 0;
	node_ptr->part_pptr = NULL;
	/* these values will be overwritten when the node actually registers */
	node_ptr->cpus = config_ptr->cpus;
	node_ptr->sockets = config_ptr->sockets;
	node_ptr->cores = config_ptr->cores;
	node_ptr->threads = config_ptr->threads;
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
	/* write header: version, time */
	packstr(NODE_STATE_VERSION, buffer);
	pack_time(time (NULL), buffer);

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
	END_TIMER2("dump_all_node_state");
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
	packstr (dump_node_ptr->config_ptr->feature, buffer);
	pack16  (dump_node_ptr->node_state, buffer);
	pack16  (dump_node_ptr->cpus, buffer);
	pack16  (dump_node_ptr->sockets, buffer);
	pack16  (dump_node_ptr->cores, buffer);
	pack16  (dump_node_ptr->threads, buffer);
	pack32  (dump_node_ptr->real_memory, buffer);
	pack32  (dump_node_ptr->tmp_disk, buffer);
}

/* 
 * _find_alias_node_record - find a record for node with the alias of
 * the specified name supplied
 * input: name - name to be aliased of the desired node 
 * output: return pointer to node record or NULL if not found
 * global: node_record_table_ptr - pointer to global node table
 *         node_hash_table - table of hash indecies
 */
static struct node_record * 
_find_alias_node_record (char *name) 
{
	int i;
	char *alias = NULL;
	
	if ((name == NULL)
	||  (name[0] == '\0')) {
		info("_find_alias_node_record: passed NULL name");
		return NULL;
	}
	/* Get the alias we have just to make sure the user isn't
	 * trying to use the real hostname to run on something that has
	 * been aliased.  
	 */
	alias = slurm_conf_get_nodename(name);
	
	if(!alias)
		return NULL;
	
	/* try to find via hash table, if it exists */
	if (node_hash_table) {
		struct node_record *node_ptr;
			
		i = _hash_index (alias);
		node_ptr = node_hash_table[i];
		while (node_ptr) {
			xassert(node_ptr->magic == NODE_MAGIC);
			if (!strcmp(node_ptr->name, alias)) {
				xfree(alias);
				return node_ptr;
			}
			node_ptr = node_ptr->node_next;
		}
		error ("_find_alias_node_record: lookup failure for %s", name);
	} 

	/* revert to sequential search */
	else {
		for (i = 0; i < node_record_count; i++) {
			if (!strcmp (alias, node_record_table_ptr[i].name)) {
				xfree(alias);
				return (&node_record_table_ptr[i]);
			} 
		}
	}

	xfree(alias);
	return (struct node_record *) NULL;
}

/*
 * load_all_node_state - Load the node state from file, recover on slurmctld 
 *	restart. Execute this after loading the configuration file data.
 *	Data goes into common storage.
 * IN state_only - if true, overwrite only node state, features and reason
 *	Use this to overwrite the "UNKNOWN state typically used in slurm.conf
 * RET 0 or error code
 * NOTE: READ lock_slurmctld config before entry
 */
extern int load_all_node_state ( bool state_only )
{
	char *node_name, *reason = NULL, *data = NULL, *state_file, *features;
	int data_allocated, data_read = 0, error_code = 0, node_cnt = 0;
	uint16_t node_state;
	uint16_t cpus = 1, sockets = 1, cores = 1, threads = 1;
	uint32_t real_memory, tmp_disk, data_size = 0, name_len;
	struct node_record *node_ptr;
	int state_fd;
	time_t time_stamp, now = time(NULL);
	Buf buffer;
	char *ver_str = NULL;

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
			data_read = read(state_fd, &data[data_size], BUF_SIZE);
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

	safe_unpackstr_xmalloc( &ver_str, &name_len, buffer);
	debug3("Version string in node_state header is %s", ver_str);
	if ((!ver_str) || (strcmp(ver_str, NODE_STATE_VERSION) != 0)) {
		error("*****************************************************");
		error("Can not recover node state, data version incompatable");
		error("*****************************************************");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);

	safe_unpack_time (&time_stamp, buffer);

	while (remaining_buf (buffer) > 0) {
		uint16_t base_state;
		safe_unpackstr_xmalloc (&node_name, &name_len, buffer);
		safe_unpackstr_xmalloc (&reason,    &name_len, buffer);
		safe_unpackstr_xmalloc (&features,  &name_len, buffer);
		safe_unpack16 (&node_state,  buffer);
		safe_unpack16 (&cpus,        buffer);
		safe_unpack16 (&sockets,     buffer);
		safe_unpack16 (&cores,       buffer);
		safe_unpack16 (&threads,     buffer);
		safe_unpack32 (&real_memory, buffer);
		safe_unpack32 (&tmp_disk,    buffer);
		base_state = node_state & NODE_STATE_BASE;

		/* validity test as possible */
		if ((cpus == 0) || 
		    (sockets == 0) || 
		    (cores == 0) || 
		    (threads == 0) || 
		    (base_state  >= NODE_STATE_END)) {
			error ("Invalid data for node %s: procs=%u, "
				"sockets=%u, cores=%u, threads=%u, state=%u",
				node_name, cpus, 
				sockets, cores, threads, node_state);
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
			xfree(features);
			xfree(reason);
		} else if (state_only) {
			uint16_t orig_base, orig_flags;
			orig_base  = node_ptr->node_state & NODE_STATE_BASE;
			orig_flags = node_ptr->node_state & NODE_STATE_FLAGS;
			node_cnt++;
			if (orig_base == NODE_STATE_UNKNOWN) {
				if (base_state == NODE_STATE_DOWN) {
					node_ptr->node_state = NODE_STATE_DOWN
						| orig_flags;
				}
				if (node_state & NODE_STATE_DRAIN)
					 node_ptr->node_state |=
						 NODE_STATE_DRAIN;
				if (node_state & NODE_STATE_FAIL)
					node_ptr->node_state |=
						NODE_STATE_FAIL;
			}
			if (node_ptr->reason == NULL)
				node_ptr->reason = reason;
			else
				xfree(reason);
			xfree(node_ptr->features);
			node_ptr->features = features;
		} else {
			node_cnt++;
			node_ptr->node_state    = node_state;
			xfree(node_ptr->reason);
			node_ptr->reason        = reason;
			xfree(node_ptr->features);
			node_ptr->features = features;
			node_ptr->part_cnt      = 0;
			xfree(node_ptr->part_pptr);
			node_ptr->cpus          = cpus;
			node_ptr->sockets       = sockets;
			node_ptr->cores         = cores;
			node_ptr->threads       = threads;
			node_ptr->real_memory   = real_memory;
			node_ptr->tmp_disk      = tmp_disk;
			node_ptr->last_response = (time_t) 0;
			node_ptr->last_idle	= now;
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
	
	if ((name == NULL)
	||  (name[0] == '\0')) {
		info("find_node_record passed NULL name");
		return NULL;
	}
	
	/* try to find via hash table, if it exists */
	if (node_hash_table) {
		struct node_record *node_ptr;
			
		i = _hash_index (name);
		node_ptr = node_hash_table[i];
		while (node_ptr) {
			xassert(node_ptr->magic == NODE_MAGIC);
			if (!strcmp(node_ptr->name, name)) {
				return node_ptr;
			}
			node_ptr = node_ptr->node_next;
		}

		if ((node_record_count == 1)
		&&  (strcmp(node_record_table_ptr[0].name, "localhost") == 0))
			return (&node_record_table_ptr[0]);
	       
		error ("find_node_record: lookup failure for %s", name);
	} 

	/* revert to sequential search */
	else {
		for (i = 0; i < node_record_count; i++) {
			if (!strcmp (name, node_record_table_ptr[i].name)) {
				return (&node_record_table_ptr[i]);
			} 
		}
	}
	
	/* look for the alias node record if the user put this in
	   instead of what slurm sees the node name as */
	return _find_alias_node_record (name);
}

/* 
 * _hash_index - return a hash table index for the given node name 
 * IN name = the node's name
 * RET the hash table index
 */
static int _hash_index (char *name) 
{
	int index = 0;
	int j;

	if ((node_record_count == 0)
	||  (name == NULL))
		return 0;	/* degenerate case */

	/* Multiply each character by its numerical position in the
	 * name string to add a bit of entropy, because host names such
	 * as cluster[0001-1000] can cause excessive index collisions.
	 */
	for (j = 1; *name; name++, j++)
		index += (int)*name * j;
	index %= node_record_count;
	
	return index;
}


/* 
 * init_node_conf - initialize the node configuration tables and values. 
 *	this should be called before creating any node or configuration 
 *	entries.
 * RET 0 if no error, otherwise an error code
 * global: node_record_table_ptr - pointer to global node table
 *         node_hash_table - table of hash indecies
 *         last_node_update - time of last node table update
 */
int init_node_conf (void) 
{
	last_node_update = time (NULL);
	int i;

	for (i=0; i<node_record_count; i++) {
		xfree(node_record_table_ptr[i].arch);
		xfree(node_record_table_ptr[i].comm_name);
		xfree(node_record_table_ptr[i].features);
		xfree(node_record_table_ptr[i].name);
		xfree(node_record_table_ptr[i].os);
		xfree(node_record_table_ptr[i].part_pptr);
		xfree(node_record_table_ptr[i].reason);
	}

	node_record_count = 0;
	xfree(node_record_table_ptr);
	xfree(node_hash_table);

	if (config_list)	/* delete defunct configuration entries */
		(void) _delete_config_record ();
	else {
		config_list = list_create (_list_delete_config);
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
	build_config_feature_array(config_ptr);
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
	return 0;
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

static bool _node_is_hidden(struct node_record *node_ptr)
{
	int i;
	bool shown = false;

	for (i=0; i<node_ptr->part_cnt; i++) {
		if (node_ptr->part_pptr[i]->hidden == 0) {
			shown = true;
			break;
		}
	}

	if (shown || (node_ptr->part_cnt == 0))
		return false;
	return true;
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
	bool cr_flag = false;
	time_t now = time(NULL);
	struct node_record *node_ptr = node_record_table_ptr;
	char *select_type;

	/*
	 * If Consumable Resources enabled, get allocated_cpus.
	 * Otherwise, report either all cpus or zero cpus are in 
	 * use dependeing upon node state (entire node is either 
	 * allocated or not).
	 */
	select_type = slurm_get_select_type();
	if (strcmp(select_type, "select/cons_res") == 0)
		cr_flag = true;
	xfree(select_type);

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

		if (((show_flags & SHOW_ALL) == 0) && (uid != 0) &&
		    (_node_is_hidden(node_ptr)))
			continue;
		if ((node_ptr->name == NULL) ||
		    (node_ptr->name[0] == '\0'))
			continue;

		_pack_node(node_ptr, cr_flag, buffer);
		nodes_packed ++ ;
	}
	part_filter_clear();

	tmp_offset = get_buf_offset (buffer);
	set_buf_offset (buffer, 0);
	pack32  (nodes_packed, buffer);
	set_buf_offset (buffer, tmp_offset);

	*buffer_size = get_buf_offset (buffer);
	buffer_ptr[0] = xfer_buf_data (buffer);
}


/* 
 * _pack_node - dump all configuration information about a specific node in 
 *	machine independent form (for network transmission)
 * IN dump_node_ptr - pointer to node for which information is requested
 * IN cr_flag - set if running with select/cons_res, which keeps track of the
 *		CPUs actually allocated on each node (other plugins do not)
 * IN/OUT buffer - buffer where data is placed, pointers automatically updated
 * NOTE: if you make any changes here be sure to make the corresponding 
 *	changes to load_node_config in api/node_info.c
 * NOTE: READ lock_slurmctld config before entry
 */
static void _pack_node (struct node_record *dump_node_ptr, bool cr_flag,
		Buf buffer) 
{
	packstr (dump_node_ptr->name, buffer);
	pack16  (dump_node_ptr->node_state, buffer);
	if (slurmctld_conf.fast_schedule) {	
		/* Only data from config_record used for scheduling */
		pack16  (dump_node_ptr->config_ptr->cpus, buffer);
		pack16  (dump_node_ptr->config_ptr->sockets, buffer);
		pack16  (dump_node_ptr->config_ptr->cores, buffer);
		pack16  (dump_node_ptr->config_ptr->threads, buffer);
		pack32  (dump_node_ptr->config_ptr->real_memory, buffer);
		pack32  (dump_node_ptr->config_ptr->tmp_disk, buffer);
	} else {	
		/* Individual node data used for scheduling */
		pack16  (dump_node_ptr->cpus, buffer);
		pack16  (dump_node_ptr->sockets, buffer);
		pack16  (dump_node_ptr->cores, buffer);
		pack16  (dump_node_ptr->threads, buffer);
		pack32  (dump_node_ptr->real_memory, buffer);
		pack32  (dump_node_ptr->tmp_disk, buffer);
	}
	pack32  (dump_node_ptr->config_ptr->weight, buffer);

	if (cr_flag) {
		uint16_t allocated_cpus;
		int error_code;
		error_code = select_g_get_select_nodeinfo(dump_node_ptr,
				SELECT_ALLOC_CPUS, &allocated_cpus);
		if (error_code != SLURM_SUCCESS) {
			error ("_pack_node: error from "
				"select_g_get_select_nodeinfo: %m");
			allocated_cpus = 0;
		}
		pack16(allocated_cpus, buffer);
	} else if ((dump_node_ptr->node_state & NODE_STATE_COMPLETING) ||
		   (dump_node_ptr->node_state == NODE_STATE_ALLOCATED)) {
		if (slurmctld_conf.fast_schedule)
			pack16(dump_node_ptr->config_ptr->cpus, buffer);
		else
			pack16(dump_node_ptr->cpus, buffer);
	} else {
		pack16((uint16_t) 0, buffer);
	}

	packstr (dump_node_ptr->arch, buffer);
	packstr (dump_node_ptr->config_ptr->feature, buffer);
	packstr (dump_node_ptr->os, buffer);
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
	struct node_record *node_ptr = node_record_table_ptr;

	xfree (node_hash_table);
	node_hash_table = xmalloc (sizeof (struct node_record *) * 
				node_record_count);

	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if ((node_ptr->name == NULL) ||
		    (node_ptr->name[0] == '\0'))
			continue;	/* vestigial record */
		inx = _hash_index (node_ptr->name);
		node_ptr->node_next = node_hash_table[inx];
		node_hash_table[inx] = node_ptr;
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
	struct node_record *node_ptr = node_record_table_ptr;
	DEF_TIMERS;

	START_TIMER;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if ((node_ptr->name == NULL) ||
		    (node_ptr->name[0] == '\0'))
			continue;
		if (node_ptr->port == 0)
			node_ptr->port = slurmctld_conf.slurmd_port;
		slurm_set_addr (&node_ptr->slurm_addr,
				node_ptr->port,
				node_ptr->comm_name);
		if (node_ptr->slurm_addr.sin_port)
			continue;
		fatal ("slurm_set_addr failure on %s", 
		       node_ptr->comm_name);
	}

	END_TIMER2("set_slurmd_addr");
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
	int error_code = 0, node_inx;
	struct node_record *node_ptr = NULL;
	char  *this_node_name = NULL;
	hostlist_t host_list;
	uint16_t base_state = 0, node_flags = 0, state_val;
	time_t now = time(NULL);

	if (update_node_msg -> node_names == NULL ) {
		error ("update_node: invalid node name  %s", 
		       update_node_msg -> node_names );
		return ESLURM_INVALID_NODE_NAME;
	}

	if ( (host_list = hostlist_create (update_node_msg -> node_names))
								 == NULL) {
		error ("hostlist_create error on %s: %m", 
		       update_node_msg -> node_names);
		return ESLURM_INVALID_NODE_NAME;
	}

	last_node_update = now;
	while ( (this_node_name = hostlist_shift (host_list)) ) {
		int err_code = 0;
		state_val = update_node_msg->node_state;
		node_ptr = find_node_record (this_node_name);
		node_inx = node_ptr - node_record_table_ptr;
		if (node_ptr == NULL) {
			error ("update_node: node %s does not exist", 
				this_node_name);
			error_code = ESLURM_INVALID_NODE_NAME;
			free (this_node_name);
			break;
		}
		
		if ((update_node_msg -> reason) && 
		    (update_node_msg -> reason[0])) {
			xfree(node_ptr->reason);
			node_ptr->reason = xstrdup(update_node_msg->reason);
			info ("update_node: node %s reason set to: %s",
				this_node_name, node_ptr->reason);
		}

		if (state_val != (uint16_t) NO_VAL) {
			base_state = node_ptr->node_state; 
			if (!_valid_node_state_change(base_state, state_val)) {
				info("Invalid node state transition requested "
				     "for node %s from=%s to=%s",
				     this_node_name, 
				     node_state_string(base_state),
				     node_state_string(state_val));
				state_val = (uint16_t) NO_VAL;
				error_code = ESLURM_INVALID_NODE_STATE;
			}
		}
		if (state_val != (uint16_t) NO_VAL) {
			if (state_val == NODE_RESUME) {
				base_state &= NODE_STATE_BASE;
				if ((base_state == NODE_STATE_IDLE) &&
				    ((node_ptr->node_state & NODE_STATE_DRAIN) 
				     || (node_ptr->node_state &
					 NODE_STATE_FAIL))) {
					clusteracct_storage_g_node_up(
						acct_db_conn, 
						slurmctld_cluster_name,
						node_ptr,
						now);
				}
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
				base_state &= NODE_STATE_BASE;
				if (base_state == NODE_STATE_DOWN) {
					state_val = NODE_STATE_IDLE;
					node_ptr->node_state |= 
							NODE_STATE_NO_RESPOND;
					node_ptr->last_response = now;
					ping_nodes_now = true;
				} else
					state_val = base_state;
			}
			if (state_val == NODE_STATE_DOWN) {
				/* We must set node DOWN before killing 
				 * its jobs */
				_make_node_down(node_ptr, now);
				kill_running_job_by_node_name (this_node_name,
							       false);
			}
			else if (state_val == NODE_STATE_IDLE) {
				/* assume they want to clear DRAIN and
				 * FAIL flags too */
				base_state &= NODE_STATE_BASE;
				if (base_state == NODE_STATE_DOWN) {
					trigger_node_up(node_ptr);
					clusteracct_storage_g_node_up(
						acct_db_conn, 
						slurmctld_cluster_name,
						node_ptr,
						now);
				} else if ((base_state == NODE_STATE_IDLE) &&
					   ((node_ptr->node_state &
					     NODE_STATE_DRAIN) ||
					    (node_ptr->node_state &
					     NODE_STATE_FAIL))) {
					clusteracct_storage_g_node_up(
						acct_db_conn, 
						slurmctld_cluster_name,
						node_ptr,
						now);
				}
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
				bit_set (avail_node_bitmap, node_inx);
				bit_set (idle_node_bitmap, node_inx);
				bit_set (up_node_bitmap, node_inx);
				node_ptr->last_idle = now;
				reset_job_priority();
			}
			else if (state_val == NODE_STATE_ALLOCATED) {
				if (!(node_ptr->node_state & (NODE_STATE_DRAIN
						| NODE_STATE_FAIL)))
					bit_set (up_node_bitmap, node_inx);
				bit_set   (avail_node_bitmap, node_inx);
				bit_clear (idle_node_bitmap, node_inx);
			}
			else if (state_val == NODE_STATE_DRAIN) {
				bit_clear (avail_node_bitmap, node_inx);
				state_val = node_ptr->node_state |
					NODE_STATE_DRAIN;
				if ((node_ptr->run_job_cnt  == 0) &&
				    (node_ptr->comp_job_cnt == 0)) {
					trigger_node_drained(node_ptr);
					clusteracct_storage_g_node_down(
						acct_db_conn, 
						slurmctld_cluster_name,
						node_ptr, now, NULL);
				}
			}
			else if (state_val == NODE_STATE_FAIL) {
				bit_clear (avail_node_bitmap, node_inx);
				state_val = node_ptr->node_state |
					NODE_STATE_FAIL;
				trigger_node_failing(node_ptr);
				if ((node_ptr->run_job_cnt  == 0) &&
				    (node_ptr->comp_job_cnt == 0))
					clusteracct_storage_g_node_down(
						acct_db_conn, 
						slurmctld_cluster_name,
						node_ptr, now, NULL);
			}
			else {
				info ("Invalid node state specified %d", 
					state_val);
				err_code = 1;
				error_code = ESLURM_INVALID_NODE_STATE;
			}

			if (err_code == 0) {
				node_flags = node_ptr->node_state & 
						NODE_STATE_FLAGS;
				node_ptr->node_state = state_val | node_flags;

				select_g_update_node_state(node_inx, 
							   state_val);

				info ("update_node: node %s state set to %s",
					this_node_name, 
					node_state_string(state_val));
			}
		}

		base_state = node_ptr->node_state & NODE_STATE_BASE;
		if ((base_state != NODE_STATE_DOWN)
		&&  ((node_ptr->node_state & (NODE_STATE_DRAIN |
				NODE_STATE_FAIL)) == 0))
			xfree(node_ptr->reason);

		free (this_node_name);
	}
	hostlist_destroy (host_list);

	if ((error_code == 0) && (update_node_msg->features)) {
		error_code = _update_node_features(
			update_node_msg->node_names, 
			update_node_msg->features);
	}

	return error_code;
}

/*
 * restore_node_features - Restore node features based upon state 
 *	saved (preserves interactive updates)
 */
extern void restore_node_features(void)
{
	int i, j, update_cnt = 0;
	char *node_list;

	/* Identify all nodes that have features field
	 * preserved and not explicitly set in slurm.conf 
	 * to a different value */
	for (i=0; i<node_record_count; i++) {
		if (!node_record_table_ptr[i].features)
			continue;
		if (node_record_table_ptr[i].config_ptr->feature) {
			/* use Features explicitly set in slurm.conf */
			continue;
		}
		update_cnt++;
	}
	if (update_cnt == 0)
		return;

	for (i=0; i<node_record_count; i++) {
		if (!node_record_table_ptr[i].features)
			continue;
		node_list = xstrdup(node_record_table_ptr[i].name);

		for (j=(i+1); j<node_record_count; j++) {
			if (!node_record_table_ptr[j].features ||
			    strcmp(node_record_table_ptr[i].features,
			    node_record_table_ptr[j].features))
				continue;
			xstrcat(node_list, ",");
			xstrcat(node_list, node_record_table_ptr[j].name);
			xfree(node_record_table_ptr[j].features);
		}
		_update_node_features(node_list, 
			node_record_table_ptr[i].features);
		xfree(node_record_table_ptr[i].features);
		xfree(node_list);
	}
}

/*
 * _update_node_features - Update features associated with nodes
 *	build new config list records as needed
 * IN node_names - List of nodes to update
 * IN features - New features value
 * RET: SLURM_SUCCESS or error code
 */
static int _update_node_features(char *node_names, char *features)
{
	bitstr_t *node_bitmap = NULL, *tmp_bitmap;
	ListIterator config_iterator;
	struct config_record *config_ptr, *new_config_ptr;
	struct config_record *first_new = NULL;
	int rc, config_cnt, tmp_cnt;

	rc = node_name2bitmap(node_names, false, &node_bitmap);
	if (rc) {
		info("_update_node_features: invalid node_name");
		return rc;
	}

	/* For each config_record with one of these nodes, 
	 * update it (if all nodes updated) or split it into 
	 * a new entry */
	config_iterator = list_iterator_create(config_list);
	if (config_iterator == NULL)
		fatal("list_iterator_create malloc failure");
	while ((config_ptr = (struct config_record *)
			list_next(config_iterator))) {
		if (config_ptr == first_new)
			break;	/* done with all original records */

		tmp_bitmap = bit_copy(node_bitmap);
		bit_and(tmp_bitmap, config_ptr->node_bitmap);
		config_cnt = bit_set_count(config_ptr->node_bitmap);
		tmp_cnt = bit_set_count(tmp_bitmap);
		if (tmp_cnt == 0) {
			/* no overlap, leave alone */
		} else if (tmp_cnt == config_cnt) {
			/* all nodes changed, update in situ */
			xfree(config_ptr->feature);
			if (features[0])
				config_ptr->feature = xstrdup(features);
			else
				config_ptr->feature = NULL;
			build_config_feature_array(config_ptr);
		} else {
			/* partial update, split config_record */
			new_config_ptr = create_config_record();
			if (first_new == NULL);
				first_new = new_config_ptr;
			new_config_ptr->magic       = config_ptr->magic;
			new_config_ptr->cpus        = config_ptr->cpus;
			new_config_ptr->sockets     = config_ptr->sockets;
			new_config_ptr->cores       = config_ptr->cores;
			new_config_ptr->threads     = config_ptr->threads;
			new_config_ptr->real_memory = config_ptr->real_memory;
			new_config_ptr->tmp_disk    = config_ptr->tmp_disk;
			new_config_ptr->weight      = config_ptr->weight;
			if (features[0])
				new_config_ptr->feature = xstrdup(features);
			build_config_feature_array(new_config_ptr);
			new_config_ptr->node_bitmap = bit_copy(tmp_bitmap);
			new_config_ptr->nodes = 
				bitmap2node_name(tmp_bitmap);
			_update_config_ptr(tmp_bitmap, new_config_ptr);

			/* Update remaining records */ 
			bit_not(tmp_bitmap);
			bit_and(config_ptr->node_bitmap, tmp_bitmap);
			xfree(config_ptr->nodes);
			config_ptr->nodes = bitmap2node_name(
				config_ptr->node_bitmap);
		}
		bit_free(tmp_bitmap);
	}
	list_iterator_destroy(config_iterator);
	bit_free(node_bitmap);
 
	info("_update_node_features: nodes %s reason set to: %s",
		node_names, features);
	return SLURM_SUCCESS;
}

/* Reset the config pointer for updated jobs */
static void _update_config_ptr(bitstr_t *bitmap, 
		struct config_record *config_ptr)
{
	int i;

	for (i=0; i<node_record_count; i++) {
		if (bit_test(bitmap, i) == 0)
			continue;
		node_record_table_ptr[i].config_ptr = config_ptr;
	}
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
	time_t now = time(NULL);

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

		if (node_ptr->node_state & NODE_STATE_DRAIN) {
			/* state already changed, nothing to do */
			free (this_node_name);
			continue;
		}

		node_ptr->node_state |= NODE_STATE_DRAIN;
		bit_clear (avail_node_bitmap, node_inx);
		info ("drain_nodes: node %s state set to DRAIN",
			this_node_name);

		xfree(node_ptr->reason);
		node_ptr->reason = xstrdup(reason);
		if ((node_ptr->run_job_cnt  == 0) &&
		    (node_ptr->comp_job_cnt == 0)) {
			/* no jobs, node is drained */
			trigger_node_drained(node_ptr);
			clusteracct_storage_g_node_down(acct_db_conn, 
							slurmctld_cluster_name,
							node_ptr, now, NULL);
		}

		select_g_update_node_state(node_inx, node_ptr->node_state);

		free (this_node_name);
	}

	hostlist_destroy (host_list);
	return error_code;
}
/* Return true if admin request to change node state from old to new is valid */
static bool _valid_node_state_change(uint16_t old, uint16_t new)
{
	uint16_t base_state, node_flags;
	if (old == new)
		return true;

	base_state = (old) & NODE_STATE_BASE;
	node_flags = (old) & NODE_STATE_FLAGS;
	switch (new) {
		case NODE_STATE_DOWN:
		case NODE_STATE_DRAIN:
		case NODE_STATE_FAIL:
			return true;
			break;

		case NODE_RESUME:
			if (base_state == NODE_STATE_UNKNOWN)
				return false;
			if ((base_state == NODE_STATE_DOWN)
			||  (node_flags & NODE_STATE_DRAIN)
			||  (node_flags & NODE_STATE_FAIL))
				return true;
			break;

		case NODE_STATE_IDLE:
			if ((base_state == NODE_STATE_DOWN)
			||  (base_state == NODE_STATE_IDLE))
				return true;
			break;

		case NODE_STATE_ALLOCATED:
			if (base_state == NODE_STATE_ALLOCATED)
				return true;
			break;

		default:	/* All others invalid */
			break;
	}

	return false;
}

/*
 * validate_node_specs - validate the node's specifications as valid, 
 *	if not set state to down, in any case update last_response
 * IN reg_msg - node registration message
 * RET 0 if no error, ENOENT if no such node, EINVAL if values too low
 * NOTE: READ lock_slurmctld config before entry
 */
extern int validate_node_specs(slurm_node_registration_status_msg_t *reg_msg)
{
	int error_code, i;
	struct config_record *config_ptr;
	struct node_record *node_ptr;
	char *reason_down = NULL;
	uint16_t base_state, node_flags;
	time_t now = time(NULL);

	node_ptr = find_node_record (reg_msg->node_name);
	if (node_ptr == NULL)
		return ENOENT;
	node_ptr->last_response = now;

	config_ptr = node_ptr->config_ptr;
	error_code = 0;

#if 0
	/* Testing the socket, core, and thread count here can produce 
	 * an error if the user did not specify the values on slurm.conf
	 * for a multi-core system */
	if ((slurmctld_conf.fast_schedule != 2)
	&&  (reg_msg->sockets < config_ptr->sockets)) {
		error("Node %s has low socket count %u", 
			reg_msg->node_name, reg_msg->sockets);
		error_code  = EINVAL;
		reason_down = "Low socket count";
	}
	node_ptr->sockets = reg_msg->sockets;

	if ((slurmctld_conf.fast_schedule != 2)
	&&  (reg_msg->cores < config_ptr->cores)) {
		error("Node %s has low core count %u", 
			reg_msg->node_name, reg_msg->cores);
		error_code  = EINVAL;
		reason_down = "Low core count";
	}
	node_ptr->cores = reg_msg->cores;

	if ((slurmctld_conf.fast_schedule != 2)
	&&  (reg_msg->threads < config_ptr->threads)) {
		error("Node %s has low thread count %u", 
			reg_msg->node_name, reg_msg->threads);
		error_code  = EINVAL;
		reason_down = "Low thread count";
	}
	node_ptr->threads = reg_msg->threads;
#else
	if (slurmctld_conf.fast_schedule != 2) {
		int tot1, tot2;
		tot1 = reg_msg->sockets * reg_msg->cores * reg_msg->threads;
		tot2 = config_ptr->sockets * config_ptr->cores *
			config_ptr->threads;
		if (tot1 < tot2) {
			error("Node %s has low socket*core*thread count %u",
				reg_msg->node_name, tot1);
			error_code = EINVAL;
			reason_down = "Low socket*core*thread count";
		}
	}
	node_ptr->sockets = reg_msg->sockets;
	node_ptr->cores   = reg_msg->cores;
	node_ptr->threads = reg_msg->threads;
#endif

	if ((slurmctld_conf.fast_schedule != 2)
	&&  (reg_msg->cpus < config_ptr->cpus)) {
		error ("Node %s has low cpu count %u", 
			reg_msg->node_name, reg_msg->cpus);
		error_code  = EINVAL;
		reason_down = "Low CPUs";
	}
	if ((node_ptr->cpus != reg_msg->cpus)
	&&  (slurmctld_conf.fast_schedule == 0)) {
		for (i=0; i<node_ptr->part_cnt; i++) {
			node_ptr->part_pptr[i]->total_cpus += 
				(reg_msg->cpus - node_ptr->cpus);
		}
	}
	node_ptr->cpus = reg_msg->cpus;

	if ((slurmctld_conf.fast_schedule != 2) 
	&&  (reg_msg->real_memory < config_ptr->real_memory)) {
		error ("Node %s has low real_memory size %u", 
		       reg_msg->node_name, reg_msg->real_memory);
		error_code  = EINVAL;
		reason_down = "Low RealMemory";
	}
	node_ptr->real_memory = reg_msg->real_memory;

	if ((slurmctld_conf.fast_schedule != 2)
	&&  (reg_msg->tmp_disk < config_ptr->tmp_disk)) {
		error ("Node %s has low tmp_disk size %u",
		       reg_msg->node_name, reg_msg->tmp_disk);
		error_code = EINVAL;
		reason_down = "Low TmpDisk";
	}
	node_ptr->tmp_disk = reg_msg->tmp_disk;

	xfree(node_ptr->arch);
	node_ptr->arch = reg_msg->arch;
	reg_msg->arch = NULL;	/* Nothing left to free */

	xfree(node_ptr->os);
	node_ptr->os = reg_msg->os;
	reg_msg->os = NULL;	/* Nothing left to free */

	if (node_ptr->node_state & NODE_STATE_NO_RESPOND) {
		last_node_update = time (NULL);
		reset_job_priority();
		node_ptr->node_state &= (uint16_t) (~NODE_STATE_NO_RESPOND);
	}
	base_state = node_ptr->node_state & NODE_STATE_BASE;
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	if (error_code) {
		if (base_state != NODE_STATE_DOWN) {
			error ("Setting node %s state to DOWN", 
				reg_msg->node_name);
		}
		last_node_update = time (NULL);
		set_node_down(reg_msg->node_name, reason_down);
		_sync_bitmaps(node_ptr, reg_msg->job_count);
	} else if (reg_msg->status == ESLURMD_PROLOG_FAILED) {
		if ((node_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL)) == 0) {
#ifdef HAVE_BG
			info("Prolog failure on node %s", reg_msg->node_name);
#else
			last_node_update = time (NULL);
			error("Prolog failure on node %s, state to DOWN",
				reg_msg->node_name);
			set_node_down(reg_msg->node_name, "Prolog failed");
#endif
		}
	} else {
		if (base_state == NODE_STATE_UNKNOWN) {
			last_node_update = time (NULL);
			reset_job_priority();
			debug("validate_node_specs: node %s has registered", 
				reg_msg->node_name);
			if (reg_msg->job_count) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
					node_flags;
			} else {
				node_ptr->node_state = NODE_STATE_IDLE |
					node_flags;
				node_ptr->last_idle = now;
			}
			if ((node_flags & NODE_STATE_DRAIN) == 0)
				xfree(node_ptr->reason);
			clusteracct_storage_g_node_up(acct_db_conn, 
						      slurmctld_cluster_name,
						      node_ptr, now);
		} else if ((base_state == NODE_STATE_DOWN) &&
			   ((slurmctld_conf.ret2service == 2) ||
		            ((slurmctld_conf.ret2service == 1) &&
			     (node_ptr->reason != NULL) && 
			     (strncmp(node_ptr->reason, "Not responding", 14) 
					== 0)))) {
			last_node_update = time (NULL);
			if (reg_msg->job_count) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
					node_flags;
			} else {
				node_ptr->node_state = NODE_STATE_IDLE |
					node_flags;
				node_ptr->last_idle = now;
			}
			info ("node %s returned to service", reg_msg->node_name);
			xfree(node_ptr->reason);
			reset_job_priority();
			trigger_node_up(node_ptr);
			clusteracct_storage_g_node_up(acct_db_conn, 
						      slurmctld_cluster_name,
						      node_ptr, now);
		} else if ((base_state == NODE_STATE_ALLOCATED) &&
			   (reg_msg->job_count == 0)) {	/* job vanished */
			last_node_update = now;
			node_ptr->node_state = NODE_STATE_IDLE | node_flags;
			node_ptr->last_idle = now;
		} else if ((node_flags & NODE_STATE_COMPLETING) &&
			   (reg_msg->job_count == 0)) {	/* job already done */
			last_node_update = now;
			node_ptr->node_state &= (~NODE_STATE_COMPLETING);
		}
		select_g_update_node_state((node_ptr - node_record_table_ptr),
					   node_ptr->node_state);
		_sync_bitmaps(node_ptr, reg_msg->job_count);
	}

	return error_code;
}

/*
 * validate_nodes_via_front_end - validate all nodes on a cluster as having
 *	a valid configuration as soon as the front-end registers. Individual
 *	nodes will not register with this configuration
 * IN reg_msg - node registration message
 * RET 0 if no error, SLURM error code otherwise
 * NOTE: READ lock_slurmctld config before entry
 */
extern int validate_nodes_via_front_end(
		slurm_node_registration_status_msg_t *reg_msg)
{
	int error_code = 0, i, jobs_on_node;
	bool updated_job = false;
#ifdef HAVE_BG
	bool failure_logged = false;
#endif
	struct job_record *job_ptr;
	struct config_record *config_ptr;
	struct node_record *node_ptr;
	time_t now = time(NULL);
	ListIterator job_iterator;
	hostlist_t return_hostlist = NULL, reg_hostlist = NULL;
	hostlist_t prolog_hostlist = NULL;
	char host_str[64];
	uint16_t base_state, node_flags;

	/* First validate the job info */
	node_ptr = &node_record_table_ptr[0];	/* All msg send to node zero,
				 * the front-end for the wholel cluster */
	for (i = 0; i < reg_msg->job_count; i++) {
		if ( (reg_msg->job_id[i] >= MIN_NOALLOC_JOBID) && 
		     (reg_msg->job_id[i] <= MAX_NOALLOC_JOBID) ) {
			info("NoAllocate job %u.%u reported",
				reg_msg->job_id[i], reg_msg->step_id[i]);
			continue;
		}

		job_ptr = find_job_record(reg_msg->job_id[i]);
		if (job_ptr == NULL) {
			error("Orphan job %u.%u reported",
			      reg_msg->job_id[i], reg_msg->step_id[i]);
			abort_job_on_node(reg_msg->job_id[i], job_ptr, node_ptr);
		}

		else if ((job_ptr->job_state == JOB_RUNNING) ||
		         (job_ptr->job_state == JOB_SUSPENDED)) {
			debug3("Registered job %u.%u",
			       reg_msg->job_id[i], reg_msg->step_id[i]);
			if (job_ptr->batch_flag) {
				/* NOTE: Used for purging defunct batch jobs */
				job_ptr->time_last_active = now;
			}
		}

		else if (job_ptr->job_state & JOB_COMPLETING) {
			/* Re-send kill request as needed, 
			 * not necessarily an error */
			kill_job_on_node(reg_msg->job_id[i], job_ptr, node_ptr);
		}


		else if (job_ptr->job_state == JOB_PENDING) {
			/* Typically indicates a job requeue and the hung
			 * slurmd that went DOWN is now responding */
			error("Registered PENDING job %u.%u",
				reg_msg->job_id[i], reg_msg->step_id[i]);
			abort_job_on_node(reg_msg->job_id[i], job_ptr, node_ptr);
		}

		else {		/* else job is supposed to be done */
			error("Registered job %u.%u in state %s",
				reg_msg->job_id[i], reg_msg->step_id[i], 
				job_state_string(job_ptr->job_state));
			kill_job_on_node(reg_msg->job_id[i], job_ptr, node_ptr);
		}
	}

	/* purge orphan batch jobs */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((job_ptr->job_state != JOB_RUNNING) ||
		    (job_ptr->batch_flag == 0))
			continue;
#ifdef HAVE_BG
		    /* slurmd does not report job presence until after prolog 
		     * completes which waits for bgblock boot to complete.  
		     * This can take several minutes on BlueGene. */
		if (difftime(now, job_ptr->time_last_active) <= 

		    (BG_FREE_PREVIOUS_BLOCK + BG_MIN_BLOCK_BOOT +
		     BG_INCR_BLOCK_BOOT * job_ptr->node_cnt))
			continue;
#else
		if (difftime(now, job_ptr->time_last_active) <= 5)
			continue;
#endif

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

		if (reg_msg->status == ESLURMD_PROLOG_FAILED) {
			if (!(node_ptr->node_state & (NODE_STATE_DRAIN | 
						      NODE_STATE_FAIL))) {
#ifdef HAVE_BG
				if (!failure_logged) {
					error("Prolog failure");
					failure_logged = true;
				}
#else
				updated_job = true;
				if (prolog_hostlist)
					(void) hostlist_push_host(
						prolog_hostlist, 
						node_ptr->name);
				else
					prolog_hostlist = hostlist_create(
						node_ptr->name);
				set_node_down(node_ptr->name, "Prolog failed");
#endif
			}
		} else {
			base_state = node_ptr->node_state & NODE_STATE_BASE;
			node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
			if (base_state == NODE_STATE_UNKNOWN) {
				updated_job = true;
				if (reg_hostlist)
					(void) hostlist_push_host(
						reg_hostlist, node_ptr->name);
				else
					reg_hostlist = hostlist_create(
						node_ptr->name);
				if (jobs_on_node) {
					node_ptr->node_state = 
						NODE_STATE_ALLOCATED | 
						node_flags;
				} else {
					node_ptr->node_state = 
						NODE_STATE_IDLE |
						node_flags;
					node_ptr->last_idle = now;
				}
				xfree(node_ptr->reason);
				if ((node_flags & 
				     (NODE_STATE_DRAIN | NODE_STATE_FAIL)) == 0)
					clusteracct_storage_g_node_up(
						acct_db_conn, 
						slurmctld_cluster_name,
						node_ptr,
						now);
			} else if ((base_state == NODE_STATE_DOWN) &&
			           (slurmctld_conf.ret2service == 1)) {
				updated_job = true;
				if (jobs_on_node) {
					node_ptr->node_state = 
						NODE_STATE_ALLOCATED |
						node_flags;
				} else {
					node_ptr->node_state = 
						NODE_STATE_IDLE |
						node_flags;
					node_ptr->last_idle = now;
				}
				if (return_hostlist)
					(void) hostlist_push_host(
						return_hostlist,
						node_ptr->name);
				else
					return_hostlist = hostlist_create(
						node_ptr->name);
				xfree(node_ptr->reason);
				trigger_node_up(node_ptr);
				clusteracct_storage_g_node_up(
					acct_db_conn, 
					slurmctld_cluster_name,
					node_ptr, now);
			} else if ((base_state == NODE_STATE_ALLOCATED) &&
				   (jobs_on_node == 0)) {
				/* job vanished */
				updated_job = true;
				node_ptr->node_state = NODE_STATE_IDLE |
					node_flags;
				node_ptr->last_idle = now;
			} else if ((node_flags & NODE_STATE_COMPLETING) &&
			           (jobs_on_node == 0)) {  
				/* job already done */
				updated_job = true;
				node_ptr->node_state &=
					(~NODE_STATE_COMPLETING);
			}
			select_g_update_node_state(
				(node_ptr - node_record_table_ptr),
				node_ptr->node_state);
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
		debug("Nodes %s have registered", host_str);
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
	return error_code;
}

/* Sync idle, share, and avail_node_bitmaps for a given node */
static void _sync_bitmaps(struct node_record *node_ptr, int job_count)
{
	uint16_t base_state;
	int node_inx = node_ptr - node_record_table_ptr;

	if (job_count == 0) {
		bit_set (idle_node_bitmap, node_inx);
		bit_set (share_node_bitmap, node_inx);
	}
	base_state = node_ptr->node_state & NODE_STATE_BASE;
	if ((base_state == NODE_STATE_DOWN)
	||  (node_ptr->node_state & (NODE_STATE_DRAIN | NODE_STATE_FAIL)))
		bit_clear (avail_node_bitmap, node_inx);
	else
		bit_set   (avail_node_bitmap, node_inx);
	if (base_state == NODE_STATE_DOWN)
		bit_clear (up_node_bitmap, node_inx);
	else
		bit_set   (up_node_bitmap, node_inx);
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
	debug2("node_did_resp %s",name);
#else
	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("node_did_resp unable to find node %s", name);
		return;
	}
	_node_did_resp(node_ptr);
	debug2("node_did_resp %s",name);
#endif
}

static void _node_did_resp(struct node_record *node_ptr)
{
	int node_inx;
	uint16_t resp_state, base_state, node_flags;
	time_t now = time(NULL);

	node_inx = node_ptr - node_record_table_ptr;
	node_ptr->last_response = now;
	resp_state = node_ptr->node_state & NODE_STATE_NO_RESPOND;
	if (resp_state) {
		info("Node %s now responding", node_ptr->name);
		last_node_update = now;
		reset_job_priority();
		node_ptr->node_state &= (uint16_t) (~NODE_STATE_NO_RESPOND);
	}
	base_state = node_ptr->node_state & NODE_STATE_BASE;
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	if (base_state == NODE_STATE_UNKNOWN) {
		last_node_update = now;
		node_ptr->last_idle = now;
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		if ((node_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL)) == 0)
			clusteracct_storage_g_node_up(acct_db_conn, 
						      slurmctld_cluster_name,
						      node_ptr, now);
	}
	if ((base_state == NODE_STATE_DOWN) &&
	    (slurmctld_conf.ret2service == 1) &&
	    (node_ptr->reason != NULL) && 
	    (strncmp(node_ptr->reason, "Not responding", 14) == 0)) {
		last_node_update = now;
		node_ptr->last_idle = now;
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		info("node_did_resp: node %s returned to service", 
			node_ptr->name);
		xfree(node_ptr->reason);
		trigger_node_up(node_ptr);
		if ((node_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL)) == 0)
			clusteracct_storage_g_node_up(acct_db_conn, 
						      slurmctld_cluster_name,
						      node_ptr, now);
	}
	base_state = node_ptr->node_state & NODE_STATE_BASE;
	if ((base_state == NODE_STATE_IDLE) 
	&&  ((node_flags & NODE_STATE_COMPLETING) == 0)) {
		bit_set (idle_node_bitmap, node_inx);
		bit_set (share_node_bitmap, node_inx);
	}
	if ((base_state == NODE_STATE_DOWN)
	||  (node_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL)))
		bit_clear (avail_node_bitmap, node_inx);
	else
		bit_set   (avail_node_bitmap, node_inx);
	if (base_state == NODE_STATE_DOWN)
		bit_clear (up_node_bitmap, node_inx);
	else
		bit_set   (up_node_bitmap, node_inx);
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

	for (i=0; i<node_record_count; i++) {
		node_ptr = node_record_table_ptr + i;
		node_ptr->not_responding = true;
	}
#else
	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("node_not_resp unable to find node %s", name);
		return;
	}
	if ((node_ptr->node_state & NODE_STATE_BASE) != NODE_STATE_DOWN) {
		/* Logged by node_no_resp_msg() on periodic basis */
		node_ptr->not_responding = true;
	}
	_node_not_resp(node_ptr, msg_time);
#endif
}

/* For every node with the "not_responding" flag set, clear the flag
 * and log that the node is not responding using a hostlist expression */
extern void node_no_resp_msg(void)
{
	int i;
	struct node_record *node_ptr;
	char host_str[1024];
	hostlist_t no_resp_hostlist = NULL;

	for (i=0; i<node_record_count; i++) {
		node_ptr = &node_record_table_ptr[i];
		if (!node_ptr->not_responding)
			continue;
		if (no_resp_hostlist) {
			(void) hostlist_push_host(no_resp_hostlist, 
						  node_ptr->name);
		} else
			no_resp_hostlist = hostlist_create(node_ptr->name);
		node_ptr->not_responding = false;
 	}
	if (no_resp_hostlist) {
		hostlist_uniq(no_resp_hostlist);
		hostlist_ranged_string(no_resp_hostlist, 
				       sizeof(host_str), host_str);
		error("Nodes %s not responding", host_str);
		hostlist_destroy(no_resp_hostlist);
	}
}

#ifndef HAVE_FRONT_END
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
#endif

/*
 * set_node_down - make the specified node's state DOWN and
 *	kill jobs as needed 
 * IN name - name of the node 
 * IN reason - why the node is DOWN
 */
void set_node_down (char *name, char *reason)
{
	struct node_record *node_ptr;
	time_t now = time(NULL);

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("node_not_resp unable to find node %s", name);
		return;
	}

	if ((node_ptr->reason == NULL)
	||  (strncmp(node_ptr->reason, "Not responding", 14) == 0)) {
		time_t now;
		char time_buf[64], time_str[32];

		now = time (NULL);
		slurm_make_time_str(&now, time_str, sizeof(time_str));
		snprintf(time_buf, sizeof(time_buf), " [slurm@%s]",
			time_str);
		xfree(node_ptr->reason);
		node_ptr->reason = xstrdup(reason);
		xstrcat(node_ptr->reason, time_buf);
	}
	_make_node_down(node_ptr, now);
	(void) kill_running_job_by_node_name(name, false);

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

	base_state = node_ptr->node_state & NODE_STATE_BASE;
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

/* msg_to_slurmd - send given msg_type (REQUEST_RECONFIGURE or
 * REQUEST_SHUTDOWN) to every slurmd, no args */
void msg_to_slurmd (slurm_msg_type_t msg_type)
{
	int i;
	shutdown_msg_t *shutdown_req;
	agent_arg_t *kill_agent_args;
	
	kill_agent_args = xmalloc (sizeof (agent_arg_t));
	kill_agent_args->msg_type = msg_type;
	kill_agent_args->retry = 0;
	kill_agent_args->hostlist = hostlist_create("");
	if (msg_type == REQUEST_SHUTDOWN) {
 		shutdown_req = xmalloc(sizeof(shutdown_msg_t));
		shutdown_req->options = 0;
		kill_agent_args->msg_args = shutdown_req;
	}

	for (i = 0; i < node_record_count; i++) {
		hostlist_push(kill_agent_args->hostlist, 
			      node_record_table_ptr[i].name);
		kill_agent_args->node_count++;
#ifdef HAVE_FRONT_END		/* Operate only on front-end */
		break;
#endif
	}

	if (kill_agent_args->node_count == 0) {
		hostlist_destroy(kill_agent_args->hostlist);
		xfree (kill_agent_args);
	} else {
		debug ("Spawning agent msg_type=%d", msg_type);
		agent_queue_request(kill_agent_args);
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
	uint16_t node_flags;

	last_node_update = time (NULL);

	(node_ptr->run_job_cnt)++;
	bit_clear(idle_node_bitmap, inx);
	if (job_ptr->details && (job_ptr->details->shared == 0)) {
		bit_clear(share_node_bitmap, inx);
		(node_ptr->no_share_job_cnt)++;
	}

	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	xfree(node_ptr->reason);
}

/* make_node_comp - flag specified node as completing a job
 * IN node_ptr - pointer to node marked for completion of job
 * IN job_ptr - pointer to job that is completing
 * IN suspended - true if job was previously suspended
 */
extern void make_node_comp(struct node_record *node_ptr,
			   struct job_record *job_ptr, bool suspended)
{
	int inx = node_ptr - node_record_table_ptr;
	uint16_t node_flags, base_state;
	time_t now = time(NULL);

	xassert(node_ptr);
	last_node_update = now;
	if (!suspended) {
		if (node_ptr->run_job_cnt)
			(node_ptr->run_job_cnt)--;
		else
			error("Node %s run_job_cnt underflow in "
				"make_node_comp", node_ptr->name);

		if (job_ptr->details && (job_ptr->details->shared == 0)) {
			if (node_ptr->no_share_job_cnt)
				(node_ptr->no_share_job_cnt)--;
			else
				error("Node %s no_share_job_cnt underflow in "
					"make_node_comp", node_ptr->name);
			if (node_ptr->no_share_job_cnt == 0)
				bit_set(share_node_bitmap, inx);
		}
	}

	base_state = node_ptr->node_state & NODE_STATE_BASE;
	if (base_state != NODE_STATE_DOWN)  {
		/* Don't verify  RPC if DOWN */
		(node_ptr->comp_job_cnt)++;
		node_ptr->node_state |= NODE_STATE_COMPLETING;
	} 
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;

	if ((node_ptr->run_job_cnt  == 0)
	&&  (node_ptr->comp_job_cnt == 0)) {
		bit_set(idle_node_bitmap, inx);
		if ((node_ptr->node_state & NODE_STATE_DRAIN) ||
		    (node_ptr->node_state & NODE_STATE_FAIL)) {
			trigger_node_drained(node_ptr);
			clusteracct_storage_g_node_down(acct_db_conn, 
							slurmctld_cluster_name,
							node_ptr, now, NULL);
		}
	}

	if (base_state == NODE_STATE_DOWN) {
		debug3("make_node_comp: Node %s being left DOWN", 
		       node_ptr->name);
	} else if (node_ptr->run_job_cnt)
		node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	else {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		node_ptr->last_idle = now;
	}
}

/* _make_node_down - flag specified node as down */
static void _make_node_down(struct node_record *node_ptr, time_t event_time)
{
	int inx = node_ptr - node_record_table_ptr;
	uint16_t node_flags;
	
	xassert(node_ptr);
	last_node_update = time (NULL);
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	node_flags &= (~NODE_STATE_COMPLETING);
	node_ptr->node_state = NODE_STATE_DOWN | node_flags;
	bit_clear (avail_node_bitmap, inx);
	bit_set   (idle_node_bitmap,  inx);
	bit_set   (share_node_bitmap, inx);
	bit_clear (up_node_bitmap,    inx);
	select_g_update_node_state(inx, node_ptr->node_state);	
	trigger_node_down(node_ptr);
	clusteracct_storage_g_node_down(acct_db_conn, 
					slurmctld_cluster_name,
					node_ptr, event_time, NULL);
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
	uint16_t node_flags, base_state;
	time_t now = time(NULL);
	
	xassert(node_ptr);
	if (job_ptr			/* Specific job completed */
	&&  (job_ptr->job_state & JOB_COMPLETING)	/* Not a replay */
	&&  (bit_test(job_ptr->node_bitmap, inx))) {	/* Not a replay */
		last_job_update = now;
		bit_clear(job_ptr->node_bitmap, inx);
		if (job_ptr->node_cnt) {
			if ((--job_ptr->node_cnt) == 0) {
				time_t delay;
				delay = last_job_update - job_ptr->end_time;
				if (delay > 60)
					info("Job %u completion process took "
						"%ld seconds", job_ptr->job_id,
						(long) delay);
				job_ptr->job_state &= (~JOB_COMPLETING);
				delete_step_records(job_ptr, 0);
				slurm_sched_schedule();
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
				error("Node %s run_job_cnt underflow in "
					"make_node_idle", node_ptr->name);
		} else {
			if (node_ptr->comp_job_cnt)
				(node_ptr->comp_job_cnt)--;
			else
				error("Node %s comp_job_cnt underflow in "
					"make_node_idle, job_id %u", 
					node_ptr->name, job_ptr->job_id);
			if (node_ptr->comp_job_cnt > 0) 
				return;		/* More jobs completing */
		}
	}

	last_node_update = now;
	if (node_ptr->comp_job_cnt == 0)
		node_ptr->node_state &= (~NODE_STATE_COMPLETING);
	base_state = node_ptr->node_state & NODE_STATE_BASE;
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	if (base_state == NODE_STATE_DOWN) {
		debug3("make_node_idle: Node %s being left DOWN",
			node_ptr->name);
	} else if ((node_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL)) &&
	           (node_ptr->run_job_cnt == 0) &&
	           (node_ptr->comp_job_cnt == 0)) {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		bit_set(idle_node_bitmap, inx);
		bit_clear(avail_node_bitmap, inx);
		debug3("make_node_idle: Node %s is DRAINED", 
		       node_ptr->name);
		node_ptr->last_idle = now;
		trigger_node_drained(node_ptr);
		clusteracct_storage_g_node_down(acct_db_conn, 
						slurmctld_cluster_name,
						node_ptr, now, NULL);
	} else if (node_ptr->run_job_cnt) {
		node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	} else {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		if (((node_flags & NODE_STATE_NO_RESPOND) == 0)
		&&  ((node_flags & NODE_STATE_COMPLETING) == 0))
			bit_set(idle_node_bitmap, inx);
		node_ptr->last_idle = now;
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

	for (i=0; i< node_record_count; i++) {
		xfree(node_record_table_ptr[i].arch);
		xfree(node_record_table_ptr[i].comm_name);
		xfree(node_record_table_ptr[i].features);
		xfree(node_record_table_ptr[i].name);
		xfree(node_record_table_ptr[i].os);
		xfree(node_record_table_ptr[i].part_pptr);
		xfree(node_record_table_ptr[i].reason);
	}

	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(avail_node_bitmap);
	FREE_NULL_BITMAP(share_node_bitmap);
	FREE_NULL_BITMAP(up_node_bitmap);

	xfree(node_record_table_ptr);
	xfree(node_hash_table);
	node_record_count = 0;
}

extern int send_nodes_to_accounting(time_t event_time)
{
	int rc = SLURM_SUCCESS, i = 0;
	struct node_record *node_ptr;
	/* send nodes not in not 'up' state */
	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (node_ptr->name == '\0'
		    || (!(node_ptr->node_state & NODE_STATE_DRAIN)
			&& !(node_ptr->node_state & NODE_STATE_FAIL) 
			&& (node_ptr->node_state & NODE_STATE_BASE) 
			!= NODE_STATE_DOWN))
			continue;

		if((rc = clusteracct_storage_g_node_down(acct_db_conn,
							 slurmctld_cluster_name,
							 node_ptr, event_time,
							 NULL))
		   == SLURM_ERROR) 
			break;
	}
	return rc;
}

/* Given a config_record, clear any existing feature_array and
 * if feature is set, then rebuild feature_array */
extern void  build_config_feature_array(struct config_record *config_ptr)
{
	int i;
	char *tmp_str, *token, *last;

	/* clear any old feature_array */
	if (config_ptr->feature_array) {
		for (i=0; config_ptr->feature_array[i]; i++)
			xfree(config_ptr->feature_array[i]);
		xfree(config_ptr->feature_array);
	}

	if (config_ptr->feature) {
		i = strlen(config_ptr->feature) + 1;	/* oversized */
		config_ptr->feature_array = xmalloc(i * sizeof(char *));
		tmp_str = xstrdup(config_ptr->feature);
		i = 0;
		token = strtok_r(tmp_str, ",", &last);
		while (token) {
			config_ptr->feature_array[i++] = xstrdup(token);
			token = strtok_r(NULL, ",", &last);
		}
		xfree(tmp_str);
	}
}
