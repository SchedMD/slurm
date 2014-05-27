/*****************************************************************************\
 *  partition_mgr.c - manage the partition information of slurm
 *	Note: there is a global partition list (part_list) and
 *	time stamp (last_part_update)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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

#include "src/slurmctld/groups.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"


/* Change PART_STATE_VERSION value when changing the state save format */
#define PART_STATE_VERSION        "PROTOCOL_VERSION"
#define PART_2_6_STATE_VERSION    "VER004"	/* SLURM version 2.6 */

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
static time_t _get_group_tlm(void);
static void   _list_delete_part(void *part_entry);
static int    _open_part_state_file(char **state_file);
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
		old_bitmap = NULL;
	} else {
		old_bitmap = bit_copy(part_ptr->node_bitmap);
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

	part_ptr = (struct part_record *) xmalloc(sizeof(struct part_record));

	xassert (part_ptr->magic = PART_MAGIC);  /* set value */
	part_ptr->name              = xstrdup("DEFAULT");
	part_ptr->alternate         = xstrdup(default_part.alternate);
	part_ptr->cr_type	    = default_part.cr_type;
	part_ptr->flags             = default_part.flags;
	part_ptr->max_time          = default_part.max_time;
	part_ptr->default_time      = default_part.default_time;
	part_ptr->max_cpus_per_node = default_part.max_cpus_per_node;
	part_ptr->max_nodes         = default_part.max_nodes;
	part_ptr->max_nodes_orig    = default_part.max_nodes;
	part_ptr->min_nodes         = default_part.min_nodes;
	part_ptr->min_nodes_orig    = default_part.min_nodes;
	part_ptr->state_up          = default_part.state_up;
	part_ptr->max_share         = default_part.max_share;
	part_ptr->preempt_mode      = default_part.preempt_mode;
	part_ptr->priority          = default_part.priority;
	part_ptr->grace_time 	    = default_part.grace_time;
	if (part_max_priority)
		part_ptr->norm_priority = (double)default_part.priority
			/ (double)part_max_priority;
	part_ptr->node_bitmap       = NULL;

	if (default_part.allow_accounts) {
		part_ptr->allow_accounts = xstrdup(default_part.allow_accounts);
		accounts_list_build(part_ptr->allow_accounts,
				    &part_ptr->allow_account_array);
	} else
		part_ptr->allow_accounts = NULL;

	if (default_part.allow_groups)
		part_ptr->allow_groups = xstrdup(default_part.allow_groups);
	else
		part_ptr->allow_groups = NULL;

	if (default_part.allow_qos) {
		part_ptr->allow_qos = xstrdup(default_part.allow_qos);
		qos_list_build(part_ptr->allow_qos, &part_ptr->allow_qos_bitstr);
	} else
		part_ptr->allow_qos = NULL;

	if (default_part.deny_accounts) {
		part_ptr->deny_accounts = xstrdup(default_part.deny_accounts);
		accounts_list_build(part_ptr->deny_accounts,
				    &part_ptr->deny_account_array);
	} else
		part_ptr->deny_accounts = NULL;

	if (default_part.deny_qos) {
		part_ptr->deny_qos = xstrdup(default_part.deny_qos);
		qos_list_build(part_ptr->deny_qos, &part_ptr->deny_qos_bitstr);
	} else
		part_ptr->deny_qos = NULL;

	if (default_part.allow_alloc_nodes)
		part_ptr->allow_alloc_nodes = xstrdup(default_part.
						      allow_alloc_nodes);
	else
		part_ptr->allow_alloc_nodes = NULL;

	if (default_part.nodes)
		part_ptr->nodes = xstrdup(default_part.nodes);
	else
		part_ptr->nodes = NULL;

	(void) list_append(part_list, part_ptr);

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
	if (name == NULL) {
		i = list_delete_all(part_list, &list_find_part,
				    "universal_key");
	} else
		i = list_delete_all(part_list, &list_find_part, name);
	if ((name == NULL) || (i != 0))
		return 0;

	error("_delete_part_record: attempt to delete non-existent "
	      "partition %s", name);
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
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	/* write partition records to buffer */
	lock_slurmctld(part_read_lock);
	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		xassert (part_ptr->magic == PART_MAGIC);
		_dump_part_state(part_ptr, buffer);
	}
	list_iterator_destroy(part_iterator);

	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/part_state.old");
	reg_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(reg_file, "/part_state");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/part_state.new");
	unlock_slurmctld(part_read_lock);

	/* write the buffer to file */
	lock_state_files();
	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, error creating file %s, %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount, rc;
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

		rc = fsync_and_close(log_fd, "partition");
		if (rc && !error_code)
			error_code = rc;
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file)) {
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		}
		(void) unlink(reg_file);
		if (link(new_file, reg_file)) {
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		}
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
	xassert(part_ptr);
	if (default_part_loc == part_ptr)
		part_ptr->flags |= PART_FLAG_DEFAULT;
	else
		part_ptr->flags &= (~PART_FLAG_DEFAULT);

	packstr(part_ptr->name,          buffer);
	pack32(part_ptr->grace_time,	 buffer);
	pack32(part_ptr->max_time,       buffer);
	pack32(part_ptr->default_time,   buffer);
	pack32(part_ptr->max_cpus_per_node, buffer);
	pack32(part_ptr->max_nodes_orig, buffer);
	pack32(part_ptr->min_nodes_orig, buffer);

	pack16(part_ptr->flags,          buffer);
	pack16(part_ptr->max_share,      buffer);
	pack16(part_ptr->preempt_mode,   buffer);
	pack16(part_ptr->priority,       buffer);

	pack16(part_ptr->state_up,       buffer);
	pack16(part_ptr->cr_type,        buffer);

	packstr(part_ptr->allow_accounts, buffer);
	packstr(part_ptr->allow_groups,  buffer);
	packstr(part_ptr->allow_qos,     buffer);
	packstr(part_ptr->allow_alloc_nodes, buffer);
	packstr(part_ptr->alternate,     buffer);
	packstr(part_ptr->deny_accounts, buffer);
	packstr(part_ptr->deny_qos,      buffer);
	packstr(part_ptr->nodes,         buffer);
}

/* Open the partition state save file, or backup if necessary.
 * state_file IN - the name of the state save file used
 * RET the file description to read from or error code
 */
static int _open_part_state_file(char **state_file)
{
	int state_fd;
	struct stat stat_buf;

	*state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(*state_file, "/part_state");
	state_fd = open(*state_file, O_RDONLY);
	if (state_fd < 0) {
		error("Could not open partition state file %s: %m",
		      *state_file);
	} else if (fstat(state_fd, &stat_buf) < 0) {
		error("Could not stat partition state file %s: %m",
		      *state_file);
		(void) close(state_fd);
	} else if (stat_buf.st_size < 10) {
		error("Partition state file %s too small", *state_file);
		(void) close(state_fd);
	} else 	/* Success */
		return state_fd;

	error("NOTE: Trying backup state save file. Information may be lost!");
	xstrcat(*state_file, ".old");
	state_fd = open(*state_file, O_RDONLY);
	return state_fd;
}

/*
 * load_all_part_state - load the partition state from file, recover on
 *	slurmctld restart. execute this after loading the configuration
 *	file data.
 * NOTE: READ lock_slurmctld config before entry
 */
int load_all_part_state(void)
{
	char *part_name = NULL, *nodes = NULL;
	char *allow_accounts = NULL, *allow_groups = NULL, *allow_qos = NULL;
	char *deny_accounts = NULL, *deny_qos = NULL;
	char *state_file, *data = NULL;
	uint32_t max_time, default_time, max_nodes, min_nodes;
	uint32_t max_cpus_per_node = INFINITE, grace_time = 0;
	time_t time;
	uint16_t flags;
	uint16_t max_share, preempt_mode, priority, state_up, cr_type;
	struct part_record *part_ptr;
	uint32_t data_size = 0, name_len;
	int data_allocated, data_read = 0, error_code = 0, part_cnt = 0;
	int state_fd;
	Buf buffer;
	char *ver_str = NULL;
	char* allow_alloc_nodes = NULL;
	uint16_t protocol_version = (uint16_t)NO_VAL;
	char* alternate = NULL;

	/* read the file */
	lock_state_files();
	state_fd = _open_part_state_file(&state_file);
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
	if (ver_str) {
		if (!strcmp(ver_str, PART_STATE_VERSION)) {
			safe_unpack16(&protocol_version, buffer);
		} else if (!strcmp(ver_str, PART_2_6_STATE_VERSION)) {
			protocol_version = SLURM_2_6_PROTOCOL_VERSION;
		}
	}

	if (protocol_version == (uint16_t)NO_VAL) {
		error("**********************************************************");
		error("Can not recover partition state, data version incompatible");
		error("**********************************************************");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);
	safe_unpack_time(&time, buffer);

	while (remaining_buf(buffer) > 0) {
		if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
			safe_unpackstr_xmalloc(&part_name, &name_len, buffer);
			safe_unpack32(&grace_time, buffer);
			safe_unpack32(&max_time, buffer);
			safe_unpack32(&default_time, buffer);
			safe_unpack32(&max_cpus_per_node, buffer);
			safe_unpack32(&max_nodes, buffer);
			safe_unpack32(&min_nodes, buffer);

			safe_unpack16(&flags,        buffer);
			safe_unpack16(&max_share,    buffer);
			safe_unpack16(&preempt_mode, buffer);
			safe_unpack16(&priority,     buffer);

			if (priority > part_max_priority)
				part_max_priority = priority;

			safe_unpack16(&state_up, buffer);
			safe_unpack16(&cr_type, buffer);

			safe_unpackstr_xmalloc(&allow_accounts,
					       &name_len, buffer);
			safe_unpackstr_xmalloc(&allow_groups,
					       &name_len, buffer);
			safe_unpackstr_xmalloc(&allow_qos,
					       &name_len, buffer);
			safe_unpackstr_xmalloc(&deny_accounts,
					       &name_len, buffer);
			safe_unpackstr_xmalloc(&deny_qos,
					       &name_len, buffer);
			safe_unpackstr_xmalloc(&allow_alloc_nodes,
					       &name_len, buffer);
			safe_unpackstr_xmalloc(&alternate, &name_len, buffer);
			safe_unpackstr_xmalloc(&nodes, &name_len, buffer);
			if ((flags & PART_FLAG_DEFAULT_CLR) ||
			    (flags & PART_FLAG_HIDDEN_CLR)  ||
			    (flags & PART_FLAG_NO_ROOT_CLR) ||
			    (flags & PART_FLAG_ROOT_ONLY_CLR) ||
			    (flags & PART_FLAG_REQ_RESV_CLR) ||
			    (flags & PART_FLAG_LLN_CLR)) {
				error("Invalid data for partition %s: flags=%u",
				      part_name, flags);
				error_code = EINVAL;
			}
		} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
			safe_unpackstr_xmalloc(&part_name, &name_len, buffer);
			safe_unpack32(&grace_time, buffer);
			safe_unpack32(&max_time, buffer);
			safe_unpack32(&default_time, buffer);
			safe_unpack32(&max_cpus_per_node, buffer);
			safe_unpack32(&max_nodes, buffer);
			safe_unpack32(&min_nodes, buffer);

			safe_unpack16(&flags,        buffer);
			safe_unpack16(&max_share,    buffer);
			safe_unpack16(&preempt_mode, buffer);
			safe_unpack16(&priority,     buffer);

			if (priority > part_max_priority)
				part_max_priority = priority;

			safe_unpack16(&state_up, buffer);
			safe_unpack16(&cr_type, buffer);

			safe_unpackstr_xmalloc(&allow_groups,
					       &name_len, buffer);
			safe_unpackstr_xmalloc(&allow_alloc_nodes,
					       &name_len, buffer);
			safe_unpackstr_xmalloc(&alternate, &name_len, buffer);
			safe_unpackstr_xmalloc(&nodes, &name_len, buffer);
			if ((flags & PART_FLAG_DEFAULT_CLR) ||
			    (flags & PART_FLAG_HIDDEN_CLR)  ||
			    (flags & PART_FLAG_NO_ROOT_CLR) ||
			    (flags & PART_FLAG_ROOT_ONLY_CLR) ||
			    (flags & PART_FLAG_REQ_RESV_CLR)) {
				error("Invalid data for partition %s: flags=%u",
				      part_name, flags);
				error_code = EINVAL;
			}
		} else {
			error("load_all_part_state: protocol_version "
			      "%hu not supported", protocol_version);
			goto unpack_error;
		}
		/* validity test as possible */
		if (state_up > PARTITION_UP) {
			error("Invalid data for partition %s: state_up=%u",
			      part_name, state_up);
			error_code = EINVAL;
		}
		if (error_code) {
			error("No more partition data will be processed from "
			      "the checkpoint file");
			xfree(allow_accounts);
			xfree(allow_groups);
			xfree(allow_qos);
			xfree(allow_alloc_nodes);
			xfree(alternate);
			xfree(deny_accounts);
			xfree(deny_qos);
			xfree(part_name);
			xfree(nodes);
			error_code = EINVAL;
			break;
		}

		/* find record and perform update */
		part_ptr = list_find_first(part_list, &list_find_part,
					   part_name);
		part_cnt++;
		if (part_ptr == NULL) {
			info("load_all_part_state: partition %s missing from "
				"configuration file", part_name);
			part_ptr = create_part_record();
			xfree(part_ptr->name);
			part_ptr->name = xstrdup(part_name);
		}

		part_ptr->flags          = flags;
		if (part_ptr->flags & PART_FLAG_DEFAULT) {
			xfree(default_part_name);
			default_part_name = xstrdup(part_name);
			default_part_loc = part_ptr;
		}
		part_ptr->max_time       = max_time;
		part_ptr->default_time   = default_time;
		part_ptr->max_cpus_per_node = max_cpus_per_node;
		part_ptr->max_nodes      = max_nodes;
		part_ptr->max_nodes_orig = max_nodes;
		part_ptr->min_nodes      = min_nodes;
		part_ptr->min_nodes_orig = min_nodes;
		part_ptr->max_share      = max_share;
		part_ptr->grace_time     = grace_time;
		if (preempt_mode != (uint16_t) NO_VAL)
			part_ptr->preempt_mode   = preempt_mode;
		part_ptr->priority       = priority;
		part_ptr->state_up       = state_up;
		part_ptr->cr_type	 = cr_type;
		xfree(part_ptr->allow_accounts);
		part_ptr->allow_accounts = allow_accounts;
		xfree(part_ptr->allow_groups);
		accounts_list_build(part_ptr->allow_accounts,
				    &part_ptr->allow_account_array);
		part_ptr->allow_groups   = allow_groups;
		xfree(part_ptr->allow_qos);
		part_ptr->allow_qos      = allow_qos;
		qos_list_build(part_ptr->allow_qos,&part_ptr->allow_qos_bitstr);
		xfree(part_ptr->allow_alloc_nodes);
		part_ptr->allow_alloc_nodes   = allow_alloc_nodes;
		xfree(part_ptr->alternate);
		part_ptr->alternate      = alternate;
		xfree(part_ptr->deny_accounts);
		part_ptr->deny_accounts  = deny_accounts;
		accounts_list_build(part_ptr->deny_accounts,
				    &part_ptr->deny_account_array);
		xfree(part_ptr->deny_qos);
		part_ptr->deny_qos       = deny_qos;
		qos_list_build(part_ptr->deny_qos, &part_ptr->deny_qos_bitstr);
		xfree(part_ptr->nodes);
		part_ptr->nodes = nodes;

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
 * RET pointer to partition or NULL if not found
 */
struct part_record *find_part_record(char *name)
{
	return list_find_first(part_list, &list_find_part, name);
}

/*
 * Create a copy of a job's part_list *partition list
 * IN part_list_src - a job's part_list
 * RET copy of part_list_src, must be freed by caller
 */
extern List part_list_copy(List part_list_src)
{
	struct part_record *part_ptr;
	ListIterator iter;
	List part_list_dest = NULL;

	if (!part_list_src)
		return part_list_dest;

	part_list_dest = list_create(NULL);
	iter = list_iterator_create(part_list_src);
	while ((part_ptr = (struct part_record *) list_next(iter))) {
		list_append(part_list_dest, part_ptr);
	}
	list_iterator_destroy(iter);

	return part_list_dest;
}

/*
 * get_part_list - find record for named partition(s)
 * IN name - partition name(s) in a comma separated list
 * RET List of pointers to the partitions or NULL if not found
 * NOTE: Caller must free the returned list
 */
extern List get_part_list(char *name)
{
	struct part_record *part_ptr;
	List job_part_list = NULL;
	char *token, *last = NULL, *tmp_name;

	if (name == NULL)
		return job_part_list;

	tmp_name = xstrdup(name);
	token = strtok_r(tmp_name, ",", &last);
	while (token) {
		part_ptr = list_find_first(part_list, &list_find_part, token);
		if (part_ptr) {
			if (job_part_list == NULL) {
				job_part_list = list_create(NULL);
			}
			list_append(job_part_list, part_ptr);
		} else {
			FREE_NULL_LIST(job_part_list);
			break;
		}
		token = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_name);
	return job_part_list;
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
	default_part.flags          = 0;
	if (slurmctld_conf.disable_root_jobs)
		default_part.flags |= PART_FLAG_NO_ROOT;
	default_part.max_time       = INFINITE;
	default_part.default_time   = NO_VAL;
	default_part.max_cpus_per_node = INFINITE;
	default_part.max_nodes      = INFINITE;
	default_part.max_nodes_orig = INFINITE;
	default_part.min_nodes      = 1;
	default_part.min_nodes_orig = 1;
	default_part.state_up       = PARTITION_UP;
	default_part.max_share      = 1;
	default_part.preempt_mode   = (uint16_t) NO_VAL;
	default_part.priority       = 1;
	default_part.norm_priority  = 0;
	default_part.total_nodes    = 0;
	default_part.total_cpus     = 0;
	default_part.grace_time     = 0;
	default_part.cr_type	    = 0;
	xfree(default_part.nodes);
	xfree(default_part.allow_accounts);
	accounts_list_free(&default_part.allow_account_array);
	xfree(default_part.allow_groups);
	xfree(default_part.allow_qos);
	FREE_NULL_BITMAP(default_part.allow_qos_bitstr);
	xfree(default_part.allow_uids);
	xfree(default_part.allow_alloc_nodes);
	xfree(default_part.alternate);
	xfree(default_part.deny_accounts);
	accounts_list_free(&default_part.deny_account_array);
	xfree(default_part.deny_qos);
	FREE_NULL_BITMAP(default_part.deny_qos_bitstr);
	FREE_NULL_BITMAP(default_part.node_bitmap);

	if (part_list)		/* delete defunct partitions */
		(void) _delete_part_record(NULL);
	else
		part_list = list_create(_list_delete_part);

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

	xfree(part_ptr->allow_accounts);
	accounts_list_free(&part_ptr->allow_account_array);
	xfree(part_ptr->allow_alloc_nodes);
	xfree(part_ptr->allow_groups);
	xfree(part_ptr->allow_uids);
	xfree(part_ptr->allow_qos);
	FREE_NULL_BITMAP(part_ptr->allow_qos_bitstr);
	xfree(part_ptr->alternate);
	xfree(part_ptr->deny_accounts);
	accounts_list_free(&part_ptr->deny_account_array);
	xfree(part_ptr->deny_qos);
	FREE_NULL_BITMAP(part_ptr->deny_qos_bitstr);
	xfree(part_ptr->name);
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
		if (part_ptr->flags & PART_FLAG_HIDDEN)
			continue;
		if (validate_group (part_ptr, uid) == 0) {
			part_ptr->flags |= PART_FLAG_HIDDEN;
			part_ptr->flags |= PART_FLAG_HIDDEN_CLR;
		}
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
		if (part_ptr->flags & PART_FLAG_HIDDEN_CLR) {
			part_ptr->flags &= (~PART_FLAG_HIDDEN);
			part_ptr->flags &= (~PART_FLAG_HIDDEN_CLR);
		}
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
			  uint16_t show_flags, uid_t uid,
			  uint16_t protocol_version)
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
		    ((part_ptr->flags & PART_FLAG_HIDDEN)
		     || (validate_group (part_ptr, uid) == 0)))
			continue;
		pack_part(part_ptr, buffer, protocol_version);
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
void pack_part(struct part_record *part_ptr, Buf buffer,
	       uint16_t protocol_version)
{
	uint32_t altered;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		if (default_part_loc == part_ptr)
			part_ptr->flags |= PART_FLAG_DEFAULT;
		else
			part_ptr->flags &= (~PART_FLAG_DEFAULT);

		packstr(part_ptr->name, buffer);
		pack32(part_ptr->grace_time, buffer);
		pack32(part_ptr->max_time, buffer);
		pack32(part_ptr->default_time, buffer);
		pack32(part_ptr->max_nodes_orig, buffer);
		pack32(part_ptr->min_nodes_orig, buffer);
		altered = part_ptr->total_nodes;
		select_g_alter_node_cnt(SELECT_APPLY_NODE_MAX_OFFSET, &altered);
		pack32(altered,              buffer);
		pack32(part_ptr->total_cpus, buffer);
		pack32(part_ptr->def_mem_per_cpu, buffer);
		pack32(part_ptr->max_cpus_per_node, buffer);
		pack32(part_ptr->max_mem_per_cpu, buffer);

		pack16(part_ptr->flags,      buffer);
		pack16(part_ptr->max_share,  buffer);
		pack16(part_ptr->preempt_mode, buffer);
		pack16(part_ptr->priority,   buffer);
		pack16(part_ptr->state_up, buffer);
		pack16(part_ptr->cr_type, buffer);

		packstr(part_ptr->allow_accounts, buffer);
		packstr(part_ptr->allow_groups, buffer);
		packstr(part_ptr->allow_alloc_nodes, buffer);
		packstr(part_ptr->allow_qos, buffer);
		packstr(part_ptr->alternate, buffer);
		packstr(part_ptr->deny_accounts, buffer);
		packstr(part_ptr->deny_qos, buffer);
		packstr(part_ptr->nodes, buffer);
		pack_bit_fmt(part_ptr->node_bitmap, buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		if (default_part_loc == part_ptr)
			part_ptr->flags |= PART_FLAG_DEFAULT;
		else
			part_ptr->flags &= (~PART_FLAG_DEFAULT);

		packstr(part_ptr->name, buffer);
		pack32(part_ptr->grace_time, buffer);
		pack32(part_ptr->max_time, buffer);
		pack32(part_ptr->default_time, buffer);
		pack32(part_ptr->max_nodes_orig, buffer);
		pack32(part_ptr->min_nodes_orig, buffer);
		altered = part_ptr->total_nodes;
		select_g_alter_node_cnt(SELECT_APPLY_NODE_MAX_OFFSET, &altered);
		pack32(altered,              buffer);
		pack32(part_ptr->total_cpus, buffer);
		pack32(part_ptr->def_mem_per_cpu, buffer);
		pack32(part_ptr->max_cpus_per_node, buffer);
		pack32(part_ptr->max_mem_per_cpu, buffer);

		pack16(part_ptr->flags,      buffer);
		pack16(part_ptr->max_share,  buffer);
		pack16(part_ptr->preempt_mode, buffer);
		pack16(part_ptr->priority,   buffer);
		pack16(part_ptr->state_up, buffer);
		pack16(part_ptr->cr_type, buffer);

		packstr(part_ptr->allow_groups, buffer);
		packstr(part_ptr->allow_alloc_nodes, buffer);
		packstr(part_ptr->alternate, buffer);
		packstr(part_ptr->nodes, buffer);
		pack_bit_fmt(part_ptr->node_bitmap, buffer);
	} else {
		error("pack_part: protocol_version "
		      "%hu not supported", protocol_version);
	}
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

	if (part_desc->max_cpus_per_node != NO_VAL) {
		info("update_part: setting MaxCPUsPerNode to %u for partition %s",
		     part_desc->max_cpus_per_node, part_desc->name);
		part_ptr->max_cpus_per_node = part_desc->max_cpus_per_node;
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
		info("update_part: setting default_time to %u "
		     "for partition %s",
		     part_desc->default_time, part_desc->name);
		part_ptr->default_time = part_desc->default_time;
	}

	if (part_desc->max_nodes != NO_VAL) {
		info("update_part: setting max_nodes to %u for partition %s",
		     part_desc->max_nodes, part_desc->name);
		part_ptr->max_nodes      = part_desc->max_nodes;
		part_ptr->max_nodes_orig = part_desc->max_nodes;
		select_g_alter_node_cnt(SELECT_SET_MP_CNT,
					&part_ptr->max_nodes);
	}

	if (part_desc->min_nodes != NO_VAL) {
		info("update_part: setting min_nodes to %u for partition %s",
		     part_desc->min_nodes, part_desc->name);
		part_ptr->min_nodes      = part_desc->min_nodes;
		part_ptr->min_nodes_orig = part_desc->min_nodes;
		select_g_alter_node_cnt(SELECT_SET_MP_CNT,
					&part_ptr->min_nodes);
	}

	if (part_desc->grace_time != NO_VAL) {
		info("update_part: setting grace_time to %u for partition %s",
		     part_desc->grace_time, part_desc->name);
		part_ptr->grace_time = part_desc->grace_time;
	}

	if (part_desc->flags & PART_FLAG_HIDDEN) {
		info("update_part: setting hidden for partition %s",
		     part_desc->name);
		part_ptr->flags |= PART_FLAG_HIDDEN;
	} else if (part_desc->flags & PART_FLAG_HIDDEN_CLR) {
		info("update_part: clearing hidden for partition %s",
		     part_desc->name);
		part_ptr->flags &= (~PART_FLAG_HIDDEN);
	}

	if (part_desc->flags & PART_FLAG_REQ_RESV) {
		info("update_part: setting req_resv for partition %s",
		     part_desc->name);
		part_ptr->flags |= PART_FLAG_REQ_RESV;
	} else if (part_desc->flags & PART_FLAG_REQ_RESV_CLR) {
		info("update_part: clearing req_resv for partition %s",
		     part_desc->name);
		part_ptr->flags &= (~PART_FLAG_REQ_RESV);
	}

	if (part_desc->flags & PART_FLAG_ROOT_ONLY) {
		info("update_part: setting root_only for partition %s",
		     part_desc->name);
		part_ptr->flags |= PART_FLAG_ROOT_ONLY;
	} else if (part_desc->flags & PART_FLAG_ROOT_ONLY_CLR) {
		info("update_part: clearing root_only for partition %s",
		     part_desc->name);
		part_ptr->flags &= (~PART_FLAG_ROOT_ONLY);
	}

	if (part_desc->flags & PART_FLAG_NO_ROOT) {
		info("update_part: setting no_root for partition %s",
		     part_desc->name);
		part_ptr->flags |= PART_FLAG_NO_ROOT;
	} else if (part_desc->flags & PART_FLAG_NO_ROOT_CLR) {
		info("update_part: clearing no_root for partition %s",
		     part_desc->name);
		part_ptr->flags &= (~PART_FLAG_NO_ROOT);
	}

	if (part_desc->flags & PART_FLAG_DEFAULT) {
		if (default_part_name == NULL) {
			info("update_part: setting default partition to %s",
			     part_desc->name);
		} else if (strcmp(default_part_name, part_desc->name) != 0) {
			info("update_part: changing default "
			     "partition from %s to %s",
			     default_part_name, part_desc->name);
		}
		xfree(default_part_name);
		default_part_name = xstrdup(part_desc->name);
		default_part_loc = part_ptr;
		part_ptr->flags |= PART_FLAG_DEFAULT;
	} else if ((part_desc->flags & PART_FLAG_DEFAULT_CLR) &&
		   (default_part_loc == part_ptr)) {
		info("update_part: clearing default partition from %s",
		     part_desc->name);
		xfree(default_part_name);
		default_part_loc = NULL;
		part_ptr->flags &= (~PART_FLAG_DEFAULT);
	}

	if (part_desc->flags & PART_FLAG_LLN) {
		info("update_part: setting LLN for partition %s",
		     part_desc->name);
		part_ptr->flags |= PART_FLAG_LLN;
	} else if (part_desc->flags & PART_FLAG_LLN_CLR) {
		info("update_part: clearing LLN for partition %s",
		     part_desc->name);
		part_ptr->flags &= (~PART_FLAG_LLN);
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

	if (part_desc->preempt_mode != (uint16_t) NO_VAL) {
		uint16_t new_mode;
		new_mode = part_desc->preempt_mode & (~PREEMPT_MODE_GANG);
		if (new_mode <= PREEMPT_MODE_CANCEL) {
			info("update_part: setting preempt_mode to %s for "
			     "partition %s",
			     preempt_mode_string(new_mode), part_desc->name);
			part_ptr->preempt_mode = new_mode;
		} else {
			info("update_part: invalid preempt_mode %u", new_mode);
		}
	}

	if (part_desc->priority != (uint16_t) NO_VAL) {
		info("update_part: setting priority to %u for partition %s",
		     part_desc->priority, part_desc->name);
		part_ptr->priority = part_desc->priority;

		/* If the max_priority changes we need to change all
		 * the normalized priorities of all the other
		 * partitions. If not then just set this partition.
		 */
		if (part_ptr->priority > part_max_priority) {
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

	if (part_desc->allow_accounts != NULL) {
		xfree(part_ptr->allow_accounts);
		if ((strcasecmp(part_desc->allow_accounts, "ALL") == 0) ||
		    (part_desc->allow_accounts[0] == '\0')) {
			info ("update_part: setting AllowAccounts to ALL for "
			      "partition %s",
			      part_desc->name);
		} else {
			part_ptr->allow_accounts = part_desc->allow_accounts;
			part_desc->allow_accounts = NULL;
			info("update_part: setting AllowAccounts to %s for "
			     "partition %s",
			     part_ptr->allow_accounts, part_desc->name);
		}
		accounts_list_build(part_ptr->allow_accounts,
				    &part_ptr->allow_account_array);
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
			clear_group_cache();
		}
	}

	if (part_desc->allow_qos != NULL) {
		xfree(part_ptr->allow_qos);
		if ((strcasecmp(part_desc->allow_qos, "ALL") == 0) ||
		    (part_desc->allow_qos[0] == '\0')) {
			info("update_partition: setting AllowQOS to ALL for "
			     "partition %s",
			     part_desc->name);
		} else {
			part_ptr->allow_qos = part_desc->allow_qos;
			part_desc->allow_qos = NULL;
			info("update_part: setting AllowQOS to %s for "
			     "partition %s",
			     part_ptr->allow_qos, part_desc->name);
		}
		qos_list_build(part_ptr->allow_qos,&part_ptr->allow_qos_bitstr);
	}

	if (part_desc->allow_alloc_nodes != NULL) {
		xfree(part_ptr->allow_alloc_nodes);
		if ((part_desc->allow_alloc_nodes[0] == '\0') ||
		    (strcasecmp(part_desc->allow_alloc_nodes, "ALL") == 0)) {
			part_ptr->allow_alloc_nodes = NULL;
			info("update_part: setting allow_alloc_nodes to ALL"
			     " for partition %s",part_desc->name);
		}
		else {
			part_ptr->allow_alloc_nodes = part_desc->
						      allow_alloc_nodes;
			part_desc->allow_alloc_nodes = NULL;
			info("update_part: setting allow_alloc_nodes to %s for "
			     "partition %s",
			     part_ptr->allow_alloc_nodes, part_desc->name);
		}
	}
	if (part_desc->alternate != NULL) {
		xfree(part_ptr->alternate);
		if ((strcasecmp(part_desc->alternate, "NONE") == 0) ||
		    (part_desc->alternate[0] == '\0'))
			part_ptr->alternate = NULL;
		else
			part_ptr->alternate = xstrdup(part_desc->alternate);
		part_desc->alternate = NULL;
		info("update_part: setting alternate to %s for "
		     "partition %s",
		     part_ptr->alternate, part_desc->name);
	}

	if (part_desc->def_mem_per_cpu != NO_VAL) {
		char *key;
		uint32_t value;
		if (part_desc->def_mem_per_cpu & MEM_PER_CPU) {
			key = "DefMemPerCpu";
			value = part_desc->def_mem_per_cpu & (~MEM_PER_CPU);
		} else {
			key = "DefMemPerNode";
			value = part_desc->def_mem_per_cpu;
		}
		info("update_part: setting %s to %u for partition %s",
		     key, value, part_desc->name);
		part_ptr->def_mem_per_cpu = part_desc->def_mem_per_cpu;
	}

	if (part_desc->deny_accounts != NULL) {
		xfree(part_ptr->deny_accounts);
		if (part_desc->deny_accounts == '\0')
			xfree(part_desc->deny_accounts);
		part_ptr->deny_accounts = part_desc->deny_accounts;
		part_desc->deny_accounts = NULL;
		info("update_part: setting DenyAccounts to %s for "
		     "partition %s",
		     part_ptr->deny_accounts, part_desc->name);
		accounts_list_build(part_ptr->deny_accounts,
				    &part_ptr->deny_account_array);
	}
	if (part_desc->allow_accounts && part_desc->deny_accounts) {
		error("Both AllowAccounts and DenyAccounts are "
		      "defined, DenyAccounts will be ignored");
	}

	if (part_desc->deny_qos != NULL) {
		xfree(part_ptr->deny_qos);
		if (part_desc->deny_qos[0] == '\0')
			xfree(part_ptr->deny_qos);
		part_ptr->deny_qos = part_desc->deny_qos;
		part_desc->deny_qos = NULL;
		info("update_part: setting DenyQOS to %s for partition %s",
		     part_ptr->deny_qos, part_desc->name);
		qos_list_build(part_ptr->deny_qos, &part_ptr->deny_qos_bitstr);
	}
	if (part_desc->allow_qos && part_desc->deny_qos) {
		error("Both AllowQOS and DenyQOS are defined, "
		      "DenyQOS will be ignored");
	}

	if (part_desc->max_mem_per_cpu != NO_VAL) {
		char *key;
		uint32_t value;
		if (part_desc->max_mem_per_cpu & MEM_PER_CPU) {
			key = "MaxMemPerCpu";
			value = part_desc->max_mem_per_cpu & (~MEM_PER_CPU);
		} else {
			key = "MaxMemPerNode";
			value = part_desc->max_mem_per_cpu;
		}
		info("update_part: setting %s to %u for partition %s",
		     key, value, part_desc->name);
		part_ptr->max_mem_per_cpu = part_desc->max_mem_per_cpu;
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
			info("update_part: setting nodes to %s "
			     "for partition %s",
			     part_ptr->nodes, part_desc->name);
			xfree(backup_node_list);
		}
		update_part_nodes_in_resv(part_ptr);
	} else if (part_ptr->node_bitmap == NULL) {
		/* Newly created partition needs a bitmap, even if empty */
		part_ptr->node_bitmap = bit_alloc(node_record_count);
	}

	if (error_code == SLURM_SUCCESS) {
		slurm_sched_g_partition_change();	/* notify sched plugin */
		select_g_reconfigure();		/* notify select plugin too */
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
 * validate_alloc_node - validate that the allocating node
 * is allowed to use this partition
 * IN part_ptr - pointer to a partition
 * IN alloc_node - allocting node of the request
 * RET 1 if permitted to run, 0 otherwise
 */
extern int validate_alloc_node(struct part_record *part_ptr, char* alloc_node)
{
	int status;

 	if (part_ptr->allow_alloc_nodes == NULL)
 		return 1;	/* all allocating nodes allowed */
 	if (alloc_node == NULL)
 		return 1;	/* if no allocating node defined
				 * let it go */

 	hostlist_t hl = hostlist_create(part_ptr->allow_alloc_nodes);
 	status=hostlist_find(hl,alloc_node);
 	hostlist_destroy(hl);

 	if (status == -1)
		status = 0;
 	else
		status = 1;

 	return status;
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
	DEF_TIMERS;

	START_TIMER;
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
	clear_group_cache();
	list_iterator_destroy(part_iterator);
	END_TIMER2("load_part_uid_allow_list");
}


/*
 * _get_groups_members - identify the users in a list of group names
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
		temp_uids = get_group_members(one_group_name);
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

	if (partition_in_use(part_desc_ptr->name))
		return ESLURM_PARTITION_IN_USE;

	if (default_part_loc == part_ptr) {
		error("Deleting default partition %s", part_ptr->name);
		default_part_loc = NULL;
	}
	(void) kill_job_by_part_name(part_desc_ptr->name);
	list_delete_all(part_list, list_find_part, part_desc_ptr->name);
	last_part_update = time(NULL);

	slurm_sched_g_partition_change();	/* notify sched plugin */
	select_g_reconfigure();		/* notify select plugin too */

	return SLURM_SUCCESS;
}

/*
 * Determine of the specified job can execute right now or is currently
 * blocked by a miscellaneous limit. This does not re-validate job state,
 * but relies upon schedule() in src/slurmctld/job_scheduler.c to do so.
 */
extern bool misc_policy_job_runnable_state(struct job_record *job_ptr)
{
	if ((job_ptr->state_reason == FAIL_ACCOUNT) ||
	    (job_ptr->state_reason == FAIL_QOS) ||
	    (job_ptr->state_reason == WAIT_NODE_NOT_AVAIL)) {
		return false;
	}

	return true;
}

/*
 * Determine of the specified job can execute right now or is currently
 * blocked by a partition state or limit. These job states should match the
 * reason values returned by job_limits_check().
 */
extern bool part_policy_job_runnable_state(struct job_record *job_ptr)
{
	if ((job_ptr->state_reason == WAIT_PART_DOWN) ||
	    (job_ptr->state_reason == WAIT_PART_INACTIVE) ||
	    (job_ptr->state_reason == WAIT_PART_NODE_LIMIT) ||
	    (job_ptr->state_reason == WAIT_PART_TIME_LIMIT) ||
	    (job_ptr->state_reason == WAIT_QOS_THRES)) {
		return false;
	}

	return true;
}

/* Validate a job's account against the partition's AllowAccounts or
 * DenyAccounts parameters. */
extern int part_policy_valid_acct(
	struct part_record *part_ptr, char *acct)
{
	int i;

	if (part_ptr->allow_account_array && part_ptr->allow_account_array[0]) {
		int match = 0;
		if (!acct) {
			info("part_policy_valid_acct: job's account "
			     "not known, so it can't use this partition "
			     "(%s allows %s)",
			     part_ptr->name, part_ptr->allow_accounts);
			return ESLURM_INVALID_ACCOUNT;
		}

		for (i = 0; part_ptr->allow_account_array[i]; i++) {
			if (strcmp(part_ptr->allow_account_array[i], acct))
				continue;
			match = 1;
			break;
		}
		if (match == 0) {
			info("part_policy_valid_acct: job's account "
			     "not permitted to use this partition "
			     "(%s allows %s not %s)",
			     part_ptr->name, part_ptr->allow_accounts, acct);
			return ESLURM_INVALID_ACCOUNT;
		}
	} else if (part_ptr->deny_account_array &&
		   part_ptr->deny_account_array[0]) {
		int match = 0;
		if (!acct) {
			debug2("part_policy_valid_acct: job's account "
			       "not known, so couldn't check if it was "
			       "denied or not");
			return SLURM_SUCCESS;
		}
		for (i = 0; part_ptr->deny_account_array[i]; i++) {
			if (strcmp(part_ptr->deny_account_array[i], acct))
				continue;
			match = 1;
			break;
		}
		if (match == 1) {
			info("part_policy_valid_acct: job's account "
			     "not permitted to use this partition "
			     "(%s denies %s including %s)",
			     part_ptr->name, part_ptr->deny_accounts, acct);
			return ESLURM_INVALID_ACCOUNT;
		}
	}

	return SLURM_SUCCESS;
}

/* Validate a job's QOS against the partition's AllowQOS or
 * DenyQOS parameters. */
extern int part_policy_valid_qos(
	struct part_record *part_ptr, slurmdb_qos_rec_t *qos_ptr)
{
	if (part_ptr->allow_qos_bitstr) {
		int match = 0;
		if (!qos_ptr) {
			info("part_policy_valid_qos: job's QOS not known, "
			     "so it can't use this partition (%s allows %s)",
			     part_ptr->name, part_ptr->allow_qos);
			return ESLURM_INVALID_QOS;
		}
		if ((qos_ptr->id < bit_size(part_ptr->allow_qos_bitstr)) &&
		    bit_test(part_ptr->allow_qos_bitstr, qos_ptr->id))
			match = 1;
		if (match == 0) {
			info("part_policy_valid_qos: job's QOS not permitted to "
			     "use this partition (%s allows %s not %s)",
			     part_ptr->name, part_ptr->allow_qos,
			     qos_ptr->name);
			return ESLURM_INVALID_QOS;
		}
	} else if (part_ptr->deny_qos_bitstr) {
		int match = 0;
		if (!qos_ptr) {
			debug2("part_policy_valid_qos: job's QOS not known, "
			       "so couldn't check if it was denied or not");
			return SLURM_SUCCESS;
		}
		if ((qos_ptr->id < bit_size(part_ptr->deny_qos_bitstr)) &&
		    bit_test(part_ptr->deny_qos_bitstr, qos_ptr->id))
			match = 1;
		if (match == 1) {
			info("part_policy_valid_qos: job's QOS not permitted "
			     "to use this partition (%s denies %s "
			     "including %s)",
			     part_ptr->name, part_ptr->allow_qos,
			     qos_ptr->name);
			return ESLURM_INVALID_QOS;
		}
	}

	return SLURM_SUCCESS;
}
