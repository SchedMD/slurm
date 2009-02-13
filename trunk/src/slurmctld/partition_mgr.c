/*****************************************************************************\
 *  partition_mgr.c - manage the partition information of slurm
 *	Note: there is a global partition list (part_list) and
 *	time stamp (last_part_update)
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov> et. al.
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
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"

/* Change PART_STATE_VERSION value when changing the state save format */
#define PART_STATE_VERSION      "VER002"

/* Global variables */
struct part_record default_part;	/* default configuration values */
List part_list = NULL;			/* partition list */
char *default_part_name = NULL;		/* name of default partition */
struct part_record *default_part_loc = NULL; /* default partition location */
time_t last_part_update;	/* time of last update to partition records */
uint16_t part_max_priority = 0;         /* max priority in all partitions */

static int    _build_part_bitmap(struct part_record *part_ptr);
static int    _delete_part_record(char *name);
static void   _dump_part_state(struct part_record *part_ptr,
			       Buf buffer);
static uid_t *_get_groups_members(char *group_names);
static uid_t *_get_group_members(char *group_name);
static time_t _get_group_tlm(void);
static void   _list_delete_part(void *part_entry);
static int    _uid_list_size(uid_t * uid_list_ptr);
static void   _unlink_free_nodes(bitstr_t *old_bitmap, 
			struct part_record *part_ptr);

/*
 * _build_part_bitmap - update the total_cpus, total_nodes, and node_bitmap 
 *	for the specified partition, also reset the partition pointers in 
 *	the node back to this partition.
 * IN part_ptr - pointer to the partition
 * RET 0 if no error, errno otherwise
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: this does not report nodes defined in more than one partition. this   
 *	is checked only upon reading the configuration file, not on an update
 */
static int _build_part_bitmap(struct part_record *part_ptr)
{
	char *this_node_name;
	bitstr_t *old_bitmap;
	struct node_record *node_ptr;	/* pointer to node_record */
	hostlist_t host_list;

	part_ptr->total_cpus = 0;
	part_ptr->total_nodes = 0;

	if (part_ptr->node_bitmap == NULL) {
		part_ptr->node_bitmap = bit_alloc(node_record_count);
		if (part_ptr->node_bitmap == NULL)
			fatal("bit_alloc malloc failure");
		old_bitmap = NULL;
	} else {
		old_bitmap = bit_copy(part_ptr->node_bitmap);
		if (old_bitmap == NULL)
			fatal("bit_copy malloc failure");
		bit_nclear(part_ptr->node_bitmap, 0,
			   node_record_count - 1);
	}

	if (part_ptr->nodes == NULL) {	/* no nodes in partition */
		_unlink_free_nodes(old_bitmap, part_ptr);
		FREE_NULL_BITMAP(old_bitmap);
		return 0;
	}

	if ((host_list = hostlist_create(part_ptr->nodes)) == NULL) {
		FREE_NULL_BITMAP(old_bitmap);
		error("hostlist_create error on %s, %m",
		      part_ptr->nodes);
		return ESLURM_INVALID_NODE_NAME;
	}

	while ((this_node_name = hostlist_shift(host_list))) {
		node_ptr = find_node_record(this_node_name);
		if (node_ptr == NULL) {
			error("_build_part_bitmap: invalid node name %s",
				this_node_name);
			free(this_node_name);
			FREE_NULL_BITMAP(old_bitmap);
			hostlist_destroy(host_list);
			return ESLURM_INVALID_NODE_NAME;
		}
		part_ptr->total_nodes++;
		if (slurmctld_conf.fast_schedule)
			part_ptr->total_cpus += node_ptr->config_ptr->cpus;
		else
			part_ptr->total_cpus += node_ptr->cpus;
		node_ptr->part_cnt++;
		xrealloc(node_ptr->part_pptr, (node_ptr->part_cnt *
			sizeof(struct part_record *)));
		node_ptr->part_pptr[node_ptr->part_cnt-1] = part_ptr;
		if (old_bitmap)
			bit_clear(old_bitmap,
				  (int) (node_ptr -
					 node_record_table_ptr));
		bit_set(part_ptr->node_bitmap,
			(int) (node_ptr - node_record_table_ptr));
		free(this_node_name);
	}
	hostlist_destroy(host_list);

	_unlink_free_nodes(old_bitmap, part_ptr);
	last_node_update = time(NULL);
	FREE_NULL_BITMAP(old_bitmap);
	return 0;
}

/* unlink nodes removed from a partition */
static void _unlink_free_nodes(bitstr_t *old_bitmap, 
		struct part_record *part_ptr)
{
	int i, j, k, update_nodes = 0;
	struct node_record *node_ptr;

	if (old_bitmap == NULL)
		return;

	node_ptr = &node_record_table_ptr[0];
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (bit_test(old_bitmap, i) == 0)
			continue;
		for (j=0; j<node_ptr->part_cnt; j++) {
			if (node_ptr->part_pptr[j] != part_ptr)
				continue;
			node_ptr->part_cnt--;
			for (k=j; k<node_ptr->part_cnt; k++) {
				node_ptr->part_pptr[k] = 
					node_ptr->part_pptr[k+1];
			}
			break;
		}
		update_nodes = 1;
	}

	if (update_nodes)
		last_node_update = time(NULL);
}


/* 
 * create_part_record - create a partition record
 * RET a pointer to the record or NULL if error
 * global: part_list - global partition list
 * NOTE: allocates memory that should be xfreed with _delete_part_record
 */
struct part_record *create_part_record(void)
{
	struct part_record *part_ptr;

	last_part_update = time(NULL);

	part_ptr =
	    (struct part_record *) xmalloc(sizeof(struct part_record));

	xassert (part_ptr->magic = PART_MAGIC);  /* set value */
	part_ptr->name              = xstrdup("DEFAULT");
	part_ptr->disable_root_jobs = default_part.disable_root_jobs;
	part_ptr->hidden            = default_part.hidden;
	part_ptr->max_time          = default_part.max_time;
	part_ptr->default_time      = default_part.default_time;
	part_ptr->max_nodes         = default_part.max_nodes;
	part_ptr->max_nodes_orig    = default_part.max_nodes;
	part_ptr->min_nodes         = default_part.min_nodes;
	part_ptr->min_nodes_orig    = default_part.min_nodes;
	part_ptr->root_only         = default_part.root_only;
	part_ptr->state_up          = default_part.state_up;
	part_ptr->max_share         = default_part.max_share;
	part_ptr->priority          = default_part.priority;
	if(part_max_priority)
		part_ptr->norm_priority = (double)default_part.priority 
			/ (double)part_max_priority;
	part_ptr->node_bitmap       = NULL;

	if (default_part.allow_groups)
		part_ptr->allow_groups = xstrdup(default_part.allow_groups);
	else
		part_ptr->allow_groups = NULL;

	if (default_part.nodes)
		part_ptr->nodes = xstrdup(default_part.nodes);
	else
		part_ptr->nodes = NULL;

	if (list_append(part_list, part_ptr) == NULL)
		fatal("create_part_record: unable to allocate memory");

	return part_ptr;
}


/* 
 * _delete_part_record - delete record for partition with specified name
 * IN name - name of the desired node, delete all partitions if NULL 
 * RET 0 on success, errno otherwise
 * global: part_list - global partition list
 */
static int _delete_part_record(char *name)
{
	int i;

	last_part_update = time(NULL);
	if (name == NULL)
		i = list_delete_all(part_list, &list_find_part,
				    "universal_key");
	else
		i = list_delete_all(part_list, &list_find_part, name);
	if ((name == NULL) || (i != 0))
		return 0;

	error
	    ("_delete_part_record: attempt to delete non-existent partition %s",
	     name);
	return ENOENT;
}


/* dump_all_part_state - save the state of all partitions to file */
int dump_all_part_state(void)
{
	/* Save high-water mark to avoid buffer growth with copies */
	static int high_buffer_size = BUF_SIZE;
	ListIterator part_iterator;
	struct part_record *part_ptr;
	int error_code = 0, log_fd;
	char *old_file, *new_file, *reg_file;
	/* Locks: Read partition */
	slurmctld_lock_t part_read_lock =
	    { READ_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	Buf buffer = init_buf(high_buffer_size);
	DEF_TIMERS;

	START_TIMER;
	/* write header: time */
	packstr(PART_STATE_VERSION, buffer);
	pack_time(time(NULL), buffer);

	/* write partition records to buffer */
	lock_slurmctld(part_read_lock);
	part_iterator = list_iterator_create(part_list);
	if (!part_iterator)
		fatal("list_iterator_create malloc");
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		xassert (part_ptr->magic == PART_MAGIC);
		_dump_part_state(part_ptr, buffer);
	}
	list_iterator_destroy(part_iterator);
	/* Maintain config read lock until we copy state_save_location *\
	\* unlock_slurmctld(part_read_lock);          - see below      */

	/* write the buffer to file */
	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/part_state.old");
	reg_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(reg_file, "/part_state");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/part_state.new");
	unlock_slurmctld(part_read_lock);
	lock_state_files();
	log_fd = creat(new_file, 0600);
	if (log_fd == 0) {
		error("Can't save state, error creating file %s, %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
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
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		(void) link(reg_file, old_file);
		(void) unlink(reg_file);
		(void) link(new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	unlock_state_files();

	free_buf(buffer);
	END_TIMER2("dump_all_part_state");
	return 0;
}

/*
 * _dump_part_state - dump the state of a specific partition to a buffer
 * IN part_ptr - pointer to partition for which information 
 *	is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
static void _dump_part_state(struct part_record *part_ptr, Buf buffer)
{
	uint16_t default_part_flag;

	xassert(part_ptr);
	if (default_part_loc == part_ptr)
		default_part_flag = 1;
	else
		default_part_flag = 0;

	packstr(part_ptr->name,          buffer);
	pack32(part_ptr->max_time,       buffer);
	pack32(part_ptr->default_time,   buffer);
	pack32(part_ptr->max_nodes_orig, buffer);
	pack32(part_ptr->min_nodes_orig, buffer);

	pack16(default_part_flag,        buffer);
	pack16(part_ptr->hidden,         buffer);
	pack16(part_ptr->root_only,      buffer);
	pack16(part_ptr->max_share,      buffer);
	pack16(part_ptr->priority,       buffer);

	pack16(part_ptr->state_up,       buffer);
	packstr(part_ptr->allow_groups,  buffer);
	packstr(part_ptr->nodes,         buffer);
}

/*
 * load_all_part_state - load the partition state from file, recover on 
 *	slurmctld restart. execute this after loading the configuration 
 *	file data.
 * NOTE: READ lock_slurmctld config before entry
 */
int load_all_part_state(void)
{
	char *part_name, *allow_groups, *nodes, *state_file, *data = NULL;
	uint32_t max_time, default_time, max_nodes, min_nodes;
	time_t time;
	uint16_t def_part_flag, hidden, root_only;
	uint16_t max_share, priority, state_up;
	struct part_record *part_ptr;
	uint32_t data_size = 0, name_len;
	int data_allocated, data_read = 0, error_code = 0, part_cnt = 0;
	int state_fd;
	Buf buffer;
	char *ver_str = NULL;

	/* read the file */
	state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(state_file, "/part_state");
	lock_state_files();
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		info("No partition state file (%s) to recover",
		     state_file);
		error_code = ENOENT;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size], 
					BUF_SIZE);
			if (data_read < 0) {
				if  (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m", 
						state_file);
					break;
				}
			} else if (data_read == 0)     /* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);
	unlock_state_files();

	buffer = create_buf(data, data_size);

	safe_unpackstr_xmalloc( &ver_str, &name_len, buffer);
	debug3("Version string in part_state header is %s", ver_str);
	if ((!ver_str) || (strcmp(ver_str, PART_STATE_VERSION) != 0)) {
		error("**********************************************************");
		error("Can not recover partition state, data version incompatable");
		error("**********************************************************");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);
	safe_unpack_time(&time, buffer);

	while (remaining_buf(buffer) > 0) {
		safe_unpackstr_xmalloc(&part_name, &name_len, buffer);
		safe_unpack32(&max_time, buffer);
		safe_unpack32(&default_time, buffer);
		safe_unpack32(&max_nodes, buffer);
		safe_unpack32(&min_nodes, buffer);

		safe_unpack16(&def_part_flag, buffer);
		safe_unpack16(&hidden,    buffer);
		safe_unpack16(&root_only, buffer);
		safe_unpack16(&max_share, buffer);
		safe_unpack16(&priority,  buffer);

		if(priority > part_max_priority) 
			part_max_priority = priority;

		safe_unpack16(&state_up, buffer);
		safe_unpackstr_xmalloc(&allow_groups, &name_len, buffer);
		safe_unpackstr_xmalloc(&nodes, &name_len, buffer);

		/* validity test as possible */
		if ((def_part_flag > 1) ||
		    (root_only > 1) || (hidden > 1) ||
		    (state_up > 1)) {
			error("Invalid data for partition %s: def_part_flag=%u, "
				"hidden=%u root_only=%u, state_up=%u",
				part_name, def_part_flag, hidden, root_only,
				state_up);
			error("No more partition data will be processed from "
				"the checkpoint file");
			xfree(part_name);
			error_code = EINVAL;
			break;
		}

		/* find record and perform update */
		part_ptr = list_find_first(part_list, &list_find_part, 
					   part_name);

		if (part_ptr) {
			part_cnt++;
			part_ptr->hidden         = hidden;
			part_ptr->max_time       = max_time;
			part_ptr->default_time   = default_time;
			part_ptr->max_nodes      = max_nodes;
			part_ptr->max_nodes_orig = max_nodes;
			part_ptr->min_nodes      = min_nodes;
			part_ptr->min_nodes_orig = min_nodes;
			if (def_part_flag) {
				xfree(default_part_name);
				default_part_name = xstrdup(part_name);
				default_part_loc = part_ptr;
			}
			part_ptr->root_only      = root_only;
			part_ptr->max_share      = max_share;
			part_ptr->priority       = priority;

			if(part_max_priority) 
				part_ptr->norm_priority = 
					(double)part_ptr->priority 
					/ (double)part_max_priority;

			part_ptr->state_up       = state_up;
			xfree(part_ptr->allow_groups);
			part_ptr->allow_groups   = allow_groups;
			xfree(part_ptr->nodes);
			part_ptr->nodes = nodes;
		} else {
			info("load_all_part_state: partition %s removed from "
				"configuration file", part_name);
		}

		xfree(part_name);
	}

	info("Recovered state of %d partitions", part_cnt);
	free_buf(buffer);
	return error_code;

      unpack_error:
	error("Incomplete partition data checkpoint file");
	info("Recovered state of %d partitions", part_cnt);
	free_buf(buffer);
	return EFAULT;
}

/* 
 * find_part_record - find a record for partition with specified name
 * IN name - name of the desired partition 
 * RET pointer to node partition or NULL if not found
 * global: part_list - global partition list
 */
struct part_record *find_part_record(char *name)
{
	return list_find_first(part_list, &list_find_part, name);
}


/* 
 * init_part_conf - initialize the default partition configuration values 
 *	and create a (global) partition list. 
 * this should be called before creating any partition entries.
 * RET 0 if no error, otherwise an error code
 * global: default_part - default partition values
 *         part_list - global partition list
 */
int init_part_conf(void)
{
	last_part_update = time(NULL);

	xfree(default_part.name);	/* needed for reconfig */
	default_part.name           = xstrdup("DEFAULT");
	default_part.disable_root_jobs = slurmctld_conf.disable_root_jobs;
	default_part.hidden         = 0;
	default_part.max_time       = INFINITE;
	default_part.default_time   = NO_VAL;
	default_part.max_nodes      = INFINITE;
	default_part.max_nodes_orig = INFINITE;
	default_part.min_nodes      = 1;
	default_part.min_nodes_orig = 1;
	default_part.root_only      = 0;
	default_part.state_up       = 1;
	default_part.max_share      = 1;
	default_part.priority       = 1;
	default_part.norm_priority  = 0;
	default_part.total_nodes    = 0;
	default_part.total_cpus     = 0;
	xfree(default_part.nodes);
	xfree(default_part.allow_groups);
	xfree(default_part.allow_uids);
	FREE_NULL_BITMAP(default_part.node_bitmap);

	if (part_list)		/* delete defunct partitions */
		(void) _delete_part_record(NULL);
	else
		part_list = list_create(_list_delete_part);

	if (part_list == NULL)
		fatal ("memory allocation failure");

	xfree(default_part_name);
	default_part_loc = (struct part_record *) NULL;

	return 0;
}

/*
 * _list_delete_part - delete an entry from the global partition list, 
 *	see common/list.h for documentation
 * global: node_record_count - count of nodes in the system
 *         node_record_table_ptr - pointer to global node table
 */
static void _list_delete_part(void *part_entry)
{
	struct part_record *part_ptr;
	struct node_record *node_ptr;
	int i, j, k;

	part_ptr = (struct part_record *) part_entry;
	node_ptr = &node_record_table_ptr[0];
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		for (j=0; j<node_ptr->part_cnt; j++) {
			if (node_ptr->part_pptr[j] != part_ptr)
				continue;
			node_ptr->part_cnt--;
			for (k=j; k<node_ptr->part_cnt; k++) {
				node_ptr->part_pptr[k] = 
					node_ptr->part_pptr[k+1];
			}
			break;
		}
	}
	xfree(part_ptr->name);
	xfree(part_ptr->allow_groups);
	xfree(part_ptr->allow_uids);
	xfree(part_ptr->nodes);
	FREE_NULL_BITMAP(part_ptr->node_bitmap);
	xfree(part_entry);
}


/*
 * list_find_part - find an entry in the partition list, see common/list.h 
 *	for documentation
 * IN key - partition name or "universal_key" for all partitions 
 * RET 1 if matches key, 0 otherwise 
 * global- part_list - the global partition list
 */
int list_find_part(void *part_entry, void *key)
{
	if (key == NULL)
		return 0;

	if (strcmp(key, "universal_key") == 0)
		return 1;

	if (strcmp(((struct part_record *)part_entry)->name, (char *) key) == 0)
		return 1;

	return 0;
}

/* part_filter_set - Set the partition's hidden flag based upon a user's 
 * group access. This must be followed by a call to part_filter_clear() */
extern void part_filter_set(uid_t uid)
{
	struct part_record *part_ptr;
	ListIterator part_iterator;

	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		if (part_ptr->hidden)
			continue;
		if (validate_group (part_ptr, uid) == 0)
			part_ptr->hidden |= 0x8000;
	}
	list_iterator_destroy(part_iterator);
}

/* part_filter_clear - Clear the partition's hidden flag based upon a user's
 * group access. This must follow a call to part_filter_set() */
extern void part_filter_clear(void)
{
	struct part_record *part_ptr;
	ListIterator part_iterator;

	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		part_ptr->hidden &= 0x7fff;
	}
	list_iterator_destroy(part_iterator);
}

/* 
 * pack_all_part - dump all partition information for all partitions in 
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - partition filtering options
 * IN uid - uid of user making request (for partition filtering)
 * global: part_list - global list of partition records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change slurm_load_part() in api/part_info.c if data format changes
 */
extern void pack_all_part(char **buffer_ptr, int *buffer_size, 
		uint16_t show_flags, uid_t uid)
{
	ListIterator part_iterator;
	struct part_record *part_ptr;
	uint32_t parts_packed;
	int tmp_offset;
	Buf buffer;
	time_t now = time(NULL);

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf(BUF_SIZE);

	/* write header: version and time */
	parts_packed = 0;
	pack32(parts_packed, buffer);
	pack_time(now, buffer);

	/* write individual partition records */
	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		xassert (part_ptr->magic == PART_MAGIC);
		if (((show_flags & SHOW_ALL) == 0) && (uid != 0) &&
		    ((part_ptr->hidden) 
		     || (validate_group (part_ptr, uid) == 0)))
			continue;
		pack_part(part_ptr, buffer);
		parts_packed++;
	}
	list_iterator_destroy(part_iterator);

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(parts_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}


/* 
 * pack_part - dump all configuration information about a specific partition 
 *	in machine independent form (for network transmission)
 * IN part_ptr - pointer to partition for which information is requested
 * IN/OUT buffer - buffer in which data is placed, pointers automatically 
 *	updated
 * global: default_part_loc - pointer to the default partition
 * NOTE: if you make any changes here be sure to make the corresponding changes
 *	to _unpack_partition_info_members() in common/slurm_protocol_pack.c
 */
void pack_part(struct part_record *part_ptr, Buf buffer)
{
	uint16_t default_part_flag;
	char node_inx_ptr[BUF_SIZE];
	uint32_t altered, node_scaling;

	if (default_part_loc == part_ptr)
		default_part_flag = 1;
	else
		default_part_flag = 0;

	packstr(part_ptr->name, buffer);
	pack32(part_ptr->max_time, buffer);
	pack32(part_ptr->default_time, buffer);
	pack32(part_ptr->max_nodes_orig, buffer);
	pack32(part_ptr->min_nodes_orig, buffer);
	altered = part_ptr->total_nodes;
	select_g_alter_node_cnt(SELECT_APPLY_NODE_MAX_OFFSET, 
				&altered);
	pack32(altered, buffer);
	select_g_alter_node_cnt(SELECT_GET_NODE_SCALING, 
				&node_scaling);
	pack16(node_scaling, buffer);
	pack32(part_ptr->total_cpus, buffer);
	pack16(default_part_flag,    buffer);
	pack16(part_ptr->disable_root_jobs, buffer);
	pack16(part_ptr->hidden,     buffer);
	pack16(part_ptr->root_only,  buffer);
	pack16(part_ptr->max_share,  buffer);
	pack16(part_ptr->priority,   buffer);

	pack16(part_ptr->state_up, buffer);
	packstr(part_ptr->allow_groups, buffer);
	packstr(part_ptr->nodes, buffer);
	if (part_ptr->node_bitmap) {
		bit_fmt(node_inx_ptr, BUF_SIZE,
			part_ptr->node_bitmap);
		packstr((char *)node_inx_ptr, buffer);
	} else
		packstr("", buffer);
}


/* 
 * update_part - create or update a partition's configuration data
 * IN part_desc - description of partition changes
 * IN create_flag - create a new partition
 * RET 0 or an error code
 * global: part_list - list of partition entries
 *	last_part_update - update time of partition records
 */
extern int update_part (update_part_msg_t * part_desc, bool create_flag)
{
	int error_code;
	struct part_record *part_ptr;

	if (part_desc->name == NULL) {
		info("update_part: invalid partition name, NULL");
		return ESLURM_INVALID_PARTITION_NAME;
	}

	error_code = SLURM_SUCCESS;
	part_ptr = list_find_first(part_list, &list_find_part, 
				   part_desc->name);

	if (create_flag) {
		if (part_ptr) {
			verbose("Duplicate partition name for create (%s)",
				part_desc->name);
			return ESLURM_INVALID_PARTITION_NAME;
		}
		info("update_part: partition %s being created",
		     part_desc->name);
		part_ptr = create_part_record();
		xfree(part_ptr->name);
		part_ptr->name = xstrdup(part_desc->name);
	} else {
		if (!part_ptr) {
			verbose("Update for partition not found (%s)",
				part_desc->name);
			return ESLURM_INVALID_PARTITION_NAME;
		}
	}

	last_part_update = time(NULL);

	if (part_desc->hidden != (uint16_t) NO_VAL) {
		info("update_part: setting hidden to %u for partition %s",
			part_desc->hidden, part_desc->name);
		part_ptr->hidden = part_desc->hidden;
	}

	if (part_desc->max_time != NO_VAL) {
		info("update_part: setting max_time to %u for partition %s", 
		     part_desc->max_time, part_desc->name);
		part_ptr->max_time = part_desc->max_time;
	}

	if ((part_desc->default_time != NO_VAL) && 
	    (part_desc->default_time > part_ptr->max_time)) {
		info("update_part: DefaultTime would exceed MaxTime for "
		     "partition %s", part_desc->name);
	} else if (part_desc->default_time != NO_VAL) {
		info("update_part: setting default_time to %u for partition %s", 
		     part_desc->default_time, part_desc->name);
		part_ptr->default_time = part_desc->default_time;
	}

	if (part_desc->max_nodes != NO_VAL) {
		info("update_part: setting max_nodes to %u for partition %s", 
		     part_desc->max_nodes, part_desc->name);
		part_ptr->max_nodes      = part_desc->max_nodes;
		part_ptr->max_nodes_orig = part_desc->max_nodes;
		select_g_alter_node_cnt(SELECT_SET_BP_CNT,
					&part_ptr->max_nodes);
	}

	if (part_desc->min_nodes != NO_VAL) {
		info("update_part: setting min_nodes to %u for partition %s", 
		     part_desc->min_nodes, part_desc->name);
		part_ptr->min_nodes      = part_desc->min_nodes;
		part_ptr->min_nodes_orig = part_desc->min_nodes;
		select_g_alter_node_cnt(SELECT_SET_BP_CNT,
					&part_ptr->min_nodes);
	}

	if (part_desc->root_only != (uint16_t) NO_VAL) {
		info("update_part: setting root_only to %u for partition %s", 
		     part_desc->root_only, part_desc->name);
		part_ptr->root_only = part_desc->root_only;
	}

	if (part_desc->state_up != (uint16_t) NO_VAL) {
		info("update_part: setting state_up to %u for partition %s", 
		     part_desc->state_up, part_desc->name);
		part_ptr->state_up = part_desc->state_up;
	}

	if (part_desc->max_share != (uint16_t) NO_VAL) {
		uint16_t force = part_desc->max_share & SHARED_FORCE;
		uint16_t val = part_desc->max_share & (~SHARED_FORCE);
		char tmp_str[24];
		if (val == 0)
			snprintf(tmp_str, sizeof(tmp_str), "EXCLUSIVE");
		else if (force)
			snprintf(tmp_str, sizeof(tmp_str), "FORCE:%u", val);
		else if (val == 1)
			snprintf(tmp_str, sizeof(tmp_str), "NO");
		else
			snprintf(tmp_str, sizeof(tmp_str), "YES:%u", val);
		info("update_part: setting share to %s for partition %s",
		     tmp_str, part_desc->name);
		part_ptr->max_share = part_desc->max_share;
	}

	if (part_desc->priority != (uint16_t) NO_VAL) {
		info("update_part: setting priority to %u for partition %s",
		     part_desc->priority, part_desc->name);
		part_ptr->priority = part_desc->priority;
		
		/* If the max_priority changes we need to change all
		   the normalized priorities of all the other
		   partitions.  If not then just set this partitions.
		*/
		if(part_ptr->priority > part_max_priority) {
			ListIterator itr = list_iterator_create(part_list);
			struct part_record *part2 = NULL;

			part_max_priority = part_ptr->priority;

			while((part2 = list_next(itr))) {
				part2->norm_priority = (double)part2->priority 
					/ (double)part_max_priority;
			}
			list_iterator_destroy(itr);
		} else {
			part_ptr->norm_priority = (double)part_ptr->priority 
				/ (double)part_max_priority;
		}
	}

	if (part_desc->default_part == 1) {
		if (default_part_name == NULL) {
			info("update_part: setting default partition to %s", 
			     part_desc->name);
		} else if (strcmp(default_part_name, part_desc->name) != 0) {
			info("update_part: changing default partition from %s to %s", 
			     default_part_name, part_desc->name);
		}
		xfree(default_part_name);
		default_part_name = xstrdup(part_desc->name);
		default_part_loc = part_ptr;
	}

	if (part_desc->allow_groups != NULL) {
		xfree(part_ptr->allow_groups);
		xfree(part_ptr->allow_uids);
		if ((strcasecmp(part_desc->allow_groups, "ALL") == 0) ||
		    (part_desc->allow_groups[0] == '\0')) {
			info("update_part: setting allow_groups to ALL for "
				"partition %s", 
				part_desc->name);
		} else {
			part_ptr->allow_groups = part_desc->allow_groups;
			part_desc->allow_groups = NULL;
			info("update_part: setting allow_groups to %s for "
				"partition %s", 
				part_ptr->allow_groups, part_desc->name);
			part_ptr->allow_uids =
				_get_groups_members(part_ptr->allow_groups);
		}
	}

	if (part_desc->nodes != NULL) {
		char *backup_node_list = part_ptr->nodes;

		if (part_desc->nodes[0] == '\0')
			part_ptr->nodes = NULL;	/* avoid empty string */
		else {
			int i;
			part_ptr->nodes = xstrdup(part_desc->nodes);
			for (i=0; part_ptr->nodes[i]; i++) {
				if (isspace(part_ptr->nodes[i]))
					part_ptr->nodes[i] = ',';
			}
		}

		error_code = _build_part_bitmap(part_ptr);
		if (error_code) {
			xfree(part_ptr->nodes);
			part_ptr->nodes = backup_node_list;
		} else {
			info("update_part: setting nodes to %s for partition %s", 
			     part_ptr->nodes, part_desc->name);
			xfree(backup_node_list);
		}
	} else if (part_ptr->node_bitmap == NULL) {
		/* Newly created partition needs a bitmap, even if empty */
		part_ptr->node_bitmap = bit_alloc(node_record_count);
	}

	if (error_code == SLURM_SUCCESS) {
		slurm_sched_partition_change();	/* notify sched plugin */
		select_g_reconfigure();		/* notify select plugin too */
		reset_job_priority();		/* free jobs */
		
		/* I am not sure why this was ever there (da) */
/* 		if (select_g_block_init(part_list) != SLURM_SUCCESS ) */
/* 			error("failed to update node selection plugin state"); */
	}

	return error_code;
}


/*
 * validate_group - validate that the submit uid is authorized to run in 
 *	this partition
 * IN part_ptr - pointer to a partition
 * IN run_uid - user to run the job as
 * RET 1 if permitted to run, 0 otherwise
 */
extern int validate_group(struct part_record *part_ptr, uid_t run_uid)
{
	int i = 0;

	if (part_ptr->allow_groups == NULL)
		return 1;	/* all users allowed */
	if ((run_uid == 0) || (run_uid == getuid()))
		return 1;	/* super-user can run anywhere */
	if (part_ptr->allow_uids == NULL)
		return 0;	/* no non-super-users in the list */
	
	for (i = 0; part_ptr->allow_uids[i]; i++) {
		if (part_ptr->allow_uids[i] == run_uid)
			return 1;
	}
	return 0;		/* not in this group's list */

}

/*
 * load_part_uid_allow_list - reload the allow_uid list of partitions
 *	if required (updated group file or force set)
 * IN force - if set then always reload the allow_uid list
 */
void load_part_uid_allow_list(int force)
{
	static time_t last_update_time;
	time_t temp_time;
	ListIterator part_iterator;
	struct part_record *part_ptr;

	temp_time = _get_group_tlm();
	if ((force == 0) && (temp_time == last_update_time))
		return;
	debug("Updating partition uid access list");
	last_update_time = temp_time;
	last_part_update = time(NULL);

	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		xfree(part_ptr->allow_uids);
		part_ptr->allow_uids =
			_get_groups_members(part_ptr->allow_groups);
	}
	list_iterator_destroy(part_iterator);
}


/* 
 * _get_groups_members - indentify the users in a list of group names
 * IN group_names - a comma delimited list of group names
 * RET a zero terminated list of its UIDs or NULL on error 
 * NOTE: User root has implicitly access to every group
 * NOTE: The caller must xfree non-NULL return values
 */
uid_t *_get_groups_members(char *group_names)
{
	uid_t *group_uids = NULL;
	uid_t *temp_uids  = NULL;
	int i, j, k;
	char *tmp_names = NULL, *name_ptr = NULL, *one_group_name = NULL;

	if (group_names == NULL)
		return NULL;

	tmp_names = xstrdup(group_names);
	one_group_name = strtok_r(tmp_names, ",", &name_ptr);
	while (one_group_name) {
		temp_uids = _get_group_members(one_group_name);
		if (temp_uids == NULL)
			;
		else if (group_uids == NULL) {
			group_uids = temp_uids;
		} else {
			/* concatenate the uid_lists and free the new one */
			i = _uid_list_size(group_uids);
			j = _uid_list_size(temp_uids);
			xrealloc(group_uids, sizeof(uid_t) * (i + j + 1));
			for (k = 0; k <= j; k++)
				group_uids[i + k] = temp_uids[k];
			xfree(temp_uids);
		}
		one_group_name = strtok_r(NULL, ",", &name_ptr);
	}
	xfree(tmp_names);

	return group_uids;
}

/* 
 * _get_group_members - indentify the users in a given group name
 * IN group_name - a single group name
 * RET a zero terminated list of its UIDs or NULL on error 
 * NOTE: User root has implicitly access to every group
 * NOTE: The caller must xfree non-NULL return values
 */
uid_t *_get_group_members(char *group_name)
{
	char grp_buffer[PW_BUF_SIZE];
	char pw_buffer[PW_BUF_SIZE];
  	struct group grp,  *grp_result = NULL;
	struct passwd pw, *pwd_result = NULL;
	uid_t *group_uids, my_uid;
	gid_t my_gid;
	int i, j, uid_cnt;
#ifdef HAVE_AIX
	FILE *fp = NULL;
#endif

	/* We need to check for !grp_result, since it appears some 
	 * versions of this function do not return an error on failure.
	 */
	if (getgrnam_r(group_name, &grp, grp_buffer, PW_BUF_SIZE, 
		       &grp_result) || (grp_result == NULL)) {
		error("Could not find configured group %s", group_name);
		return NULL;
	}

	my_gid = grp_result->gr_gid;

	for (uid_cnt=0; ; uid_cnt++) {
		if (grp_result->gr_mem[uid_cnt] == NULL)
			break;
	}

	group_uids = (uid_t *) xmalloc(sizeof(uid_t) * (uid_cnt + 1));

	j = 0;
	for (i=0; i<uid_cnt; i++) {
		my_uid = uid_from_string(grp_result->gr_mem[i]);
		if (my_uid == (uid_t) -1) {
			error("Could not find user %s in configured group %s",
			      grp_result->gr_mem[i], group_name);
		} else if (my_uid) {
			group_uids[j++] = my_uid;
		}
	}

#ifdef HAVE_AIX
	setpwent_r(&fp);
	while (!getpwent_r(&pw, pw_buffer, PW_BUF_SIZE, &fp)) {
		pwd_result = &pw;
#else
	setpwent();
	while (!getpwent_r(&pw, pw_buffer, PW_BUF_SIZE, &pwd_result)) {
#endif
 		if (pwd_result->pw_gid != my_gid)
			continue;
		j++;
 		xrealloc(group_uids, ((j+1) * sizeof(uid_t)));
		group_uids[j-1] = pwd_result->pw_uid;
	}
#ifdef HAVE_AIX
	endpwent_r(&fp);
#else
	endpwent();
#endif

	return group_uids;
}

/* _get_group_tlm - return the time of last modification for the GROUP_FILE */
time_t _get_group_tlm(void)
{
	struct stat stat_buf;

	if (stat(GROUP_FILE, &stat_buf)) {
		error("Can't stat file %s %m", GROUP_FILE);
		return (time_t) 0;
	}
	return stat_buf.st_mtime;
}

#if EXTREME_LOGGING
/* _print_group_members - print the members of a uid list */
static void _print_group_members(uid_t * uid_list)
{
	int i;

	if (uid_list) {
		for (i = 0; uid_list[i]; i++) {
			debug3("%u", (unsigned int) uid_list[i]);
		}
	}
	printf("\n\n");
}
#endif

/* _uid_list_size - return the count of uid's in a zero terminated list */
static int _uid_list_size(uid_t * uid_list_ptr)
{
	int i;

	if (uid_list_ptr == NULL)
		return 0;

	for (i = 0;; i++) {
		if (uid_list_ptr[i] == 0)
			break;
	}

	return i;
}

/* part_fini - free all memory associated with partition records */
void part_fini (void) 
{
	if (part_list) {
		list_destroy(part_list);
		part_list = NULL;
	}
	xfree(default_part_name);
	xfree(default_part.name);
	default_part_loc = (struct part_record *) NULL;
}

/*
 * delete_partition - delete the specified partition (actually leave 
 *	the entry, just flag it as defunct)
 * IN job_specs - job specification from RPC
 */
extern int delete_partition(delete_part_msg_t *part_desc_ptr)
{
	struct part_record *part_ptr;

	part_ptr = find_part_record (part_desc_ptr->name);
	if (part_ptr == NULL)	/* No such partition */
		return ESLURM_INVALID_PARTITION_NAME;

	if (default_part_loc == part_ptr) {
		error("Deleting default partition %s", part_ptr->name);
		default_part_loc = NULL;
	}
	(void) kill_job_by_part_name(part_desc_ptr->name);
	list_delete_all(part_list, list_find_part, part_desc_ptr->name);
	last_part_update = time(NULL);

	return SLURM_SUCCESS;
}
