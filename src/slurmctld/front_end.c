/*****************************************************************************\
 *  front_end.c - Define front end node functions.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"

/* No need to change we always pack SLURM_PROTOCOL_VERSION */
#define FRONT_END_STATE_VERSION        "PROTOCOL_VERSION"

front_end_record_t *front_end_nodes = NULL;
uint16_t front_end_node_cnt = 0;
time_t last_front_end_update = (time_t) 0;

#ifdef HAVE_FRONT_END
/*
 * _dump_front_end_state - dump state of a specific front_end node to a buffer
 * IN front_end_ptr - pointer to node for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
static void _dump_front_end_state(front_end_record_t *front_end_ptr,
				  Buf buffer)
{
	packstr  (front_end_ptr->name, buffer);
	pack32   (front_end_ptr->node_state, buffer);
	packstr  (front_end_ptr->reason, buffer);
	pack_time(front_end_ptr->reason_time, buffer);
	pack32   (front_end_ptr->reason_uid, buffer);
	pack16   (front_end_ptr->protocol_version, buffer);
}


/*
 * Open the front_end node state save file, or backup if necessary.
 * state_file IN - the name of the state save file used
 * RET the file description to read from or error code
 */
static Buf _open_front_end_state_file(char **state_file)
{
	Buf buf;

	*state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(*state_file, "/front_end_state");

	if (!(buf = create_mmap_buf(*state_file)))
		error("Could not open front_end state file %s: %m",
		      *state_file);
	else
		return buf;

	error("NOTE: Trying backup front_end_state save file. Information may "
	      "be lost!");
	xstrcat(*state_file, ".old");
	return create_mmap_buf(*state_file);
}

/*
 * _pack_front_end - dump all configuration information about a specific
 *	front_end node in machine independent form (for network transmission)
 * IN dump_front_end_ptr - pointer to front_end node for which information is
 *	requested
 * IN/OUT buffer - buffer where data is placed, pointers automatically updated
 * IN protocol_version - slurm protocol version of client
 * NOTE: if you make any changes here be sure to make the corresponding
 *	changes to load_front_end_config in api/node_info.c
 */
static void _pack_front_end(struct front_end_record *dump_front_end_ptr,
			    Buf buffer, uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(dump_front_end_ptr->allow_groups, buffer);
		packstr(dump_front_end_ptr->allow_users, buffer);
		pack_time(dump_front_end_ptr->boot_time, buffer);
		packstr(dump_front_end_ptr->deny_groups, buffer);
		packstr(dump_front_end_ptr->deny_users, buffer);
		packstr(dump_front_end_ptr->name, buffer);
		pack32(dump_front_end_ptr->node_state, buffer);
		packstr(dump_front_end_ptr->version, buffer);

		packstr(dump_front_end_ptr->reason, buffer);
		pack_time(dump_front_end_ptr->reason_time, buffer);
		pack32(dump_front_end_ptr->reason_uid, buffer);

		pack_time(dump_front_end_ptr->slurmd_start_time, buffer);
	} else {
		error("_pack_front_end: Unsupported slurm version %u",
		      protocol_version);
	}
}
#endif

#ifdef HAVE_FRONT_END
/* Validate job's access to a specific front-end node */
static bool _front_end_access(front_end_record_t *front_end_ptr,
			      struct job_record *job_ptr)
{
	int i;

	if (!job_ptr)
		return true;

	if (front_end_ptr->deny_gids) {
		for (i = 0; front_end_ptr->deny_gids[i]; i++) {
			if (job_ptr->group_id == front_end_ptr->deny_gids[i])
				return false;
		}
	}
	if (front_end_ptr->deny_uids) {
		for (i = 0; front_end_ptr->deny_uids[i]; i++) {
			if (job_ptr->user_id == front_end_ptr->deny_uids[i])
				return false;
		}
	}
	if (front_end_ptr->allow_gids || front_end_ptr->allow_uids) {
		if (front_end_ptr->allow_gids) {
			for (i = 0; front_end_ptr->allow_gids[i]; i++) {
				if (job_ptr->group_id ==
				    front_end_ptr->allow_gids[i])
					return true;
			}
		}
		if (front_end_ptr->allow_uids) {
			for (i = 0; front_end_ptr->allow_uids[i]; i++) {
				if (job_ptr->user_id ==
				    front_end_ptr->allow_uids[i])
					return true;
			}
		}
		return false;
	}
	return true;
}
#endif

/*
 * assign_front_end - assign a front end node for starting a job
 * job_ptr IN - job to assign a front end node (tests access control lists)
 * RET pointer to the front end node to use or NULL if none found
 */
extern front_end_record_t *assign_front_end(struct job_record *job_ptr)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr, *best_front_end = NULL;
	uint32_t state_flags;
	int i;

	if (!job_ptr->batch_host && (job_ptr->batch_flag == 0) &&
	    (front_end_ptr = find_front_end_record(job_ptr->alloc_node))) {
		/* Use submit host for interactive job */
		if (!IS_NODE_DOWN(front_end_ptr)  &&
		    !IS_NODE_DRAIN(front_end_ptr) &&
		    !IS_NODE_NO_RESPOND(front_end_ptr) &&
		    _front_end_access(front_end_ptr, job_ptr)) {
			best_front_end = front_end_ptr;
		} else {
			info("%s: front-end node %s not available for %pJ",
			     __func__, job_ptr->alloc_node, job_ptr);
			return NULL;
		}
	} else {
		for (i = 0, front_end_ptr = front_end_nodes;
		     i < front_end_node_cnt; i++, front_end_ptr++) {
			if (job_ptr->batch_host) { /* Find specific front-end */
				if (xstrcmp(job_ptr->batch_host,
					   front_end_ptr->name))
					continue;
				if (!_front_end_access(front_end_ptr, job_ptr))
					break;
			} else {	      /* Find a usable front-end node */
				if (IS_NODE_DOWN(front_end_ptr) ||
				    IS_NODE_DRAIN(front_end_ptr) ||
				    IS_NODE_NO_RESPOND(front_end_ptr))
					continue;
				if (!_front_end_access(front_end_ptr, job_ptr))
					continue;
			}
			if ((best_front_end == NULL) ||
			    (front_end_ptr->job_cnt_run <
			     best_front_end->job_cnt_run))
				best_front_end = front_end_ptr;
		}
	}

	if (best_front_end) {
		state_flags = best_front_end->node_state & NODE_STATE_FLAGS;
		best_front_end->node_state = NODE_STATE_ALLOCATED | state_flags;
		best_front_end->job_cnt_run++;
		return best_front_end;
	} else if (job_ptr->batch_host) {    /* Find specific front-end node */
		error("assign_front_end: front end node %s not found",
		      job_ptr->batch_host);
	} else {		/* Find some usable front-end node */
		error("assign_front_end: no available front end nodes found");
	}
#endif
	return NULL;
}

/*
 * avail_front_end - test if any front end nodes are available for starting job
 * job_ptr IN - job to consider for starting (tests access control lists) or
 *              NULL to test if any job can start (no test of ACL)
 */
extern bool avail_front_end(struct job_record *job_ptr)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
	int i;

	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		if (IS_NODE_DOWN(front_end_ptr)  ||
		    IS_NODE_DRAIN(front_end_ptr) ||
		    IS_NODE_NO_RESPOND(front_end_ptr))
			continue;
		if (!_front_end_access(front_end_ptr, job_ptr))
			continue;
		return true;
	}
	return false;
#else
	return true;
#endif
}

/*
 * Update front end node state
 * update_front_end_msg_ptr IN change specification
 * RET SLURM_SUCCESS or error code
 */
extern int update_front_end(update_front_end_msg_t *msg_ptr)
{
#ifdef HAVE_FRONT_END
	char  *this_node_name = NULL;
	hostlist_t host_list;
	front_end_record_t *front_end_ptr;
	int i, rc = SLURM_SUCCESS;
	time_t now = time(NULL);

	if ((host_list = hostlist_create(msg_ptr->name)) == NULL) {
		error("hostlist_create error on %s: %m", msg_ptr->name);
		return ESLURM_INVALID_NODE_NAME;
	}

	last_front_end_update = now;
	while ((this_node_name = hostlist_shift(host_list))) {
		for (i = 0, front_end_ptr = front_end_nodes;
		     i < front_end_node_cnt; i++, front_end_ptr++) {
			xassert(front_end_ptr->magic == FRONT_END_MAGIC);
			if (xstrcmp(this_node_name, front_end_ptr->name))
				continue;
			if (msg_ptr->node_state == NO_VAL) {
				;	/* No change in node state */
			} else if (msg_ptr->node_state == NODE_RESUME) {
				front_end_ptr->node_state = NODE_STATE_IDLE;
				xfree(front_end_ptr->reason);
				front_end_ptr->reason_time = 0;
				front_end_ptr->reason_uid = 0;
			} else if (msg_ptr->node_state == NODE_STATE_DRAIN) {
				front_end_ptr->node_state |= NODE_STATE_DRAIN;
				if (msg_ptr->reason) {
					xfree(front_end_ptr->reason);
					front_end_ptr->reason =
						xstrdup(msg_ptr->reason);
					front_end_ptr->reason_time = now;
					front_end_ptr->reason_uid =
						msg_ptr->reason_uid;
				}
			} else if (msg_ptr->node_state == NODE_STATE_DOWN) {
				set_front_end_down(front_end_ptr,
						   msg_ptr->reason);
			}
			if (msg_ptr->node_state != NO_VAL) {
				info("update_front_end: set state of %s to %s",
				     this_node_name,
				     node_state_string(front_end_ptr->
						       node_state));
			}
			break;
		}
		if (i >= front_end_node_cnt) {
			info("update_front_end: could not find front end: %s",
			     this_node_name);
			rc = ESLURM_INVALID_NODE_NAME;
		}
		free(this_node_name);
	}
	hostlist_destroy(host_list);

	return rc;
#else
	return ESLURM_INVALID_NODE_NAME;
#endif
}

/*
 * find_front_end_record - find a record for front_endnode with specified name
 * input: name - name of the desired front_end node
 * output: return pointer to front_end node record or NULL if not found
 */
extern front_end_record_t *find_front_end_record(char *name)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
	int i;

	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		xassert(front_end_ptr->magic == FRONT_END_MAGIC);
		if (xstrcmp(front_end_ptr->name, name) == 0)
			return front_end_ptr;
	}
#endif
	return NULL;
}

/*
 * log_front_end_state - log all front end node state
 */
extern void log_front_end_state(void)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
	int i;

	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		xassert(front_end_ptr->magic == FRONT_END_MAGIC);
		info("FrontendName=%s FrontendAddr=%s Port=%u State=%s "
		     "Reason=%s JobCntRun=%u JobCntComp=%u "
		     "AllowGroups=%s AllowUsers=%s "
		     "DenyGroups=%s DenyUsers=%s ",
		     front_end_ptr->name, front_end_ptr->comm_name,
		     front_end_ptr->port,
		     node_state_string(front_end_ptr->node_state),
		     front_end_ptr->reason, front_end_ptr->job_cnt_run,
		     front_end_ptr->job_cnt_comp,
		     front_end_ptr->allow_groups, front_end_ptr->allow_users,
		     front_end_ptr->deny_groups, front_end_ptr->deny_users);
	}
#endif
}

/*
 * purge_front_end_state - purge all front end node state
 */
extern void purge_front_end_state(void)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
	int i;

	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		xassert(front_end_ptr->magic == FRONT_END_MAGIC);
		xfree(front_end_ptr->allow_gids);
		xfree(front_end_ptr->allow_groups);
		xfree(front_end_ptr->allow_uids);
		xfree(front_end_ptr->allow_users);
		xfree(front_end_ptr->comm_name);
		xfree(front_end_ptr->deny_gids);
		xfree(front_end_ptr->deny_groups);
		xfree(front_end_ptr->deny_users);
		xfree(front_end_ptr->name);
		xfree(front_end_ptr->reason);
		xfree(front_end_ptr->version);
	}
	xfree(front_end_nodes);
	front_end_node_cnt = 0;
#endif
}

/* Translate comma delimited string of GIDs/group names into a zero terminated
 * array of GIDs */
gid_t *_xlate_groups(char *group_str, char *key)
{
	char *tmp_str, *token, *save_ptr = NULL;
	gid_t *gids_array = NULL;
	int array_size = 0;
	gid_t gid;

	if (!group_str || !group_str[0])
		return gids_array;

	tmp_str = xstrdup(group_str);
	token = strtok_r(tmp_str, ",", &save_ptr);
	while (token) {
		if (gid_from_string(token, &gid) || (gid == (gid_t) 0)) {
			error("Invalid %s value (%s), ignored", key, token);
		} else {
			xrealloc(gids_array, sizeof(gid_t) * (array_size+2));
			gids_array[array_size++] = gid;
		}
		token = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_str);
	return gids_array;
}

/* Translate comma delimited string of UIDs/user names into a zero terminated
 * array of UIDs */
uid_t *_xlate_users(char *user_str, char *key)
{
	char *tmp_str, *token, *save_ptr = NULL;
	uid_t *uids_array = NULL;
	int array_size = 0;
	uid_t uid;

	if (!user_str || !user_str[0])
		return uids_array;

	tmp_str = xstrdup(user_str);
	token = strtok_r(tmp_str, ",", &save_ptr);
	while (token) {
		if (uid_from_string(token, &uid) || (uid == (uid_t) 0)) {
			error("Invalid %s value (%s), ignored", key, token);
		} else {
			xrealloc(uids_array, sizeof(uid_t) * (array_size+2));
			uids_array[array_size++] = uid;
		}
		token = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_str);
	return uids_array;
}

/*
 * restore_front_end_state - restore frontend node state
 * IN recover - replace job, node and/or partition data with latest
 *              available information depending upon value
 *              0 = use no saved state information, rebuild everything from
 *		    slurm.conf contents
 *              1 = recover saved job and trigger state,
 *                  node DOWN/DRAIN/FAIL state and reason information
 *              2 = recover all saved state
 */
extern void restore_front_end_state(int recover)
{
#ifdef HAVE_FRONT_END
	slurm_conf_frontend_t *slurm_conf_fe_ptr;
	ListIterator iter;
	uint32_t state_base, state_flags, tree_width;
	int i;

	last_front_end_update = time(NULL);
	if (recover == 0)
		purge_front_end_state();
	if (front_end_list == NULL)
		return;		/* No front ends in slurm.conf */

	iter = list_iterator_create(front_end_list);
	while ((slurm_conf_fe_ptr = (slurm_conf_frontend_t *)
				    list_next(iter))) {
		if (slurm_conf_fe_ptr->frontends == NULL) {
			fatal("FrontendName is NULL");
			return;	/* Prevent CLANG false positive */
		}
		for (i = 0; i < front_end_node_cnt; i++) {
			if (xstrcmp(front_end_nodes[i].name,
				    slurm_conf_fe_ptr->frontends) == 0)
				break;
		}
		if (i >= front_end_node_cnt) {
			front_end_node_cnt++;
			xrealloc(front_end_nodes,
				 sizeof(front_end_record_t) *
				 front_end_node_cnt);
			front_end_nodes[i].name =
				xstrdup(slurm_conf_fe_ptr->frontends);
			front_end_nodes[i].magic = FRONT_END_MAGIC;
		}

		xfree(front_end_nodes[i].allow_gids);
		xfree(front_end_nodes[i].allow_groups);
		if (slurm_conf_fe_ptr->allow_groups) {
			front_end_nodes[i].allow_groups =
				xstrdup(slurm_conf_fe_ptr->allow_groups);
			front_end_nodes[i].allow_gids =
				_xlate_groups(slurm_conf_fe_ptr->allow_groups,
					      "AllowGroups");
		}
		xfree(front_end_nodes[i].allow_uids);
		xfree(front_end_nodes[i].allow_users);
		if (slurm_conf_fe_ptr->allow_users) {
			front_end_nodes[i].allow_users =
				xstrdup(slurm_conf_fe_ptr->allow_users);
			front_end_nodes[i].allow_uids =
				_xlate_users(slurm_conf_fe_ptr->allow_users,
					     "AllowUsers");
		}
		xfree(front_end_nodes[i].deny_gids);
		xfree(front_end_nodes[i].deny_groups);
		if (slurm_conf_fe_ptr->deny_groups) {
			front_end_nodes[i].deny_groups =
				xstrdup(slurm_conf_fe_ptr->deny_groups);
			front_end_nodes[i].deny_gids =
				_xlate_groups(slurm_conf_fe_ptr->deny_groups,
					      "DenyGroups");
		}
		xfree(front_end_nodes[i].deny_uids);
		xfree(front_end_nodes[i].deny_users);
		if (slurm_conf_fe_ptr->deny_users) {
			front_end_nodes[i].deny_users =
				xstrdup(slurm_conf_fe_ptr->deny_users);
			front_end_nodes[i].deny_uids =
				_xlate_users(slurm_conf_fe_ptr->deny_users,
					     "DenyUsers");
		}

		xfree(front_end_nodes[i].comm_name);
		if (slurm_conf_fe_ptr->addresses) {
			front_end_nodes[i].comm_name =
				xstrdup(slurm_conf_fe_ptr->addresses);
		} else {
			front_end_nodes[i].comm_name =
				xstrdup(front_end_nodes[i].name);
		}
		state_base  = front_end_nodes[i].node_state & NODE_STATE_BASE;
		state_flags = front_end_nodes[i].node_state & NODE_STATE_FLAGS;
		if ((state_base == 0) || (state_base == NODE_STATE_UNKNOWN)) {
			front_end_nodes[i].node_state =
				slurm_conf_fe_ptr->node_state | state_flags;
		}
		if ((front_end_nodes[i].reason == NULL) &&
		    (slurm_conf_fe_ptr->reason != NULL)) {
			front_end_nodes[i].reason =
				xstrdup(slurm_conf_fe_ptr->reason);
		}
		if (slurm_conf_fe_ptr->port)
			front_end_nodes[i].port = slurm_conf_fe_ptr->port;
		else
			front_end_nodes[i].port = slurmctld_conf.slurmd_port;
		slurm_set_addr(&front_end_nodes[i].slurm_addr,
			       front_end_nodes[i].port,
			       front_end_nodes[i].comm_name);
	}
	list_iterator_destroy(iter);
	if (front_end_node_cnt == 0)
		fatal("No front end nodes defined");
	tree_width = slurm_get_tree_width();
	if (front_end_node_cnt > tree_width) {
		fatal("front_end_node_cnt > tree_width (%u > %u)",
		      front_end_node_cnt, tree_width);
	}
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FRONT_END)
		log_front_end_state();
#endif
}

/*
 * pack_all_front_end - dump all front_end node information for all nodes
 *	in machine independent form (for network transmission)
 * OUT buffer_ptr - pointer to the stored data
 * OUT buffer_size - set to size of the buffer in bytes
 * IN protocol_version - slurm protocol version of client
 * NOTE: the caller must xfree the buffer at *buffer_ptr
 * NOTE: READ lock_slurmctld config before entry
 */
extern void pack_all_front_end(char **buffer_ptr, int *buffer_size, uid_t uid,
			       uint16_t protocol_version)
{
	time_t now = time(NULL);
	uint32_t nodes_packed = 0;
	Buf buffer;
#ifdef HAVE_FRONT_END
	uint32_t tmp_offset;
	front_end_record_t *front_end_ptr;
	int i;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf(BUF_SIZE * 2);
	nodes_packed = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* write header: count and time */
		pack32(nodes_packed, buffer);
		pack_time(now, buffer);

		/* write records */
		for (i = 0, front_end_ptr = front_end_nodes;
		     i < front_end_node_cnt; i++, front_end_ptr++) {
			xassert(front_end_ptr->magic == FRONT_END_MAGIC);
			_pack_front_end(front_end_ptr, buffer,
					protocol_version);
			nodes_packed++;
		}
	} else {
		error("pack_all_front_end: Unsupported slurm version %u",
		      protocol_version);
	}

	tmp_offset = get_buf_offset (buffer);
	set_buf_offset(buffer, 0);
	pack32(nodes_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
#else
	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	buffer = init_buf(64);
	pack32(nodes_packed, buffer);
	pack_time(now, buffer);
	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
#endif
}

/* dump_all_front_end_state - save the state of all front_end nodes to file */
extern int dump_all_front_end_state(void)
{
#ifdef HAVE_FRONT_END
	/* Save high-water mark to avoid buffer growth with copies */
	static int high_buffer_size = (1024 * 1024);
	int error_code = 0, i, log_fd;
	char *old_file, *new_file, *reg_file;
	front_end_record_t *front_end_ptr;
	/* Locks: Read config and node */
	slurmctld_lock_t node_read_lock = { READ_LOCK, NO_LOCK, READ_LOCK,
					    NO_LOCK, NO_LOCK };
	Buf buffer = init_buf(high_buffer_size);
	DEF_TIMERS;

	START_TIMER;
	/* write header: version, time */
	packstr(FRONT_END_STATE_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	/* write node records to buffer */
	lock_slurmctld (node_read_lock);

	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		xassert(front_end_ptr->magic == FRONT_END_MAGIC);
		_dump_front_end_state(front_end_ptr, buffer);
	}

	old_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (old_file, "/front_end_state.old");
	reg_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (reg_file, "/front_end_state");
	new_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (new_file, "/front_end_state.new");
	unlock_slurmctld (node_read_lock);

	/* write the buffer to file */
	lock_state_files();
	log_fd = creat (new_file, 0600);
	if (log_fd < 0) {
		error ("Can't save state, error creating file %s %m", new_file);
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

		rc = fsync_and_close(log_fd, "front_end");
		if (rc && !error_code)
			error_code = rc;
	}
	if (error_code)
		(void) unlink (new_file);
	else {	/* file shuffle */
		(void) unlink (old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink (reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink (new_file);
	}
	xfree (old_file);
	xfree (reg_file);
	xfree (new_file);
	unlock_state_files ();

	free_buf (buffer);
	END_TIMER2("dump_all_front_end_state");
	return error_code;
#else
	return SLURM_SUCCESS;
#endif
}

/*
 * load_all_front_end_state - Load the front_end node state from file, recover
 *	on slurmctld restart. Execute this after loading the configuration
 *	file data. Data goes into common storage.
 * IN state_only - if true, overwrite only front_end node state and reason
 *	Use this to overwrite the "UNKNOWN state typically used in slurm.conf
 * RET 0 or error code
 * NOTE: READ lock_slurmctld config before entry
 */
extern int load_all_front_end_state(bool state_only)
{
#ifdef HAVE_FRONT_END
	char *node_name = NULL, *reason = NULL, *state_file;
	int error_code = 0, node_cnt = 0;
	uint32_t node_state;
	uint32_t name_len;
	uint32_t reason_uid = NO_VAL;
	time_t reason_time = 0;
	front_end_record_t *front_end_ptr;
	time_t time_stamp;
	Buf buffer;
	char *ver_str = NULL;
	uint16_t protocol_version = NO_VAL16;

	/* read the file */
	lock_state_files();
	if (!(buffer = _open_front_end_state_file(&state_file))) {
		info("No node state file (%s) to recover", state_file);
		xfree(state_file);
		unlock_state_files();
		return ENOENT;
	}
	xfree(state_file);
	unlock_state_files();

	safe_unpackstr_xmalloc( &ver_str, &name_len, buffer);
	debug3("Version string in front_end_state header is %s", ver_str);
	if (ver_str && !xstrcmp(ver_str, FRONT_END_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);

	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can not recover front_end state, version incompatible, start with '-i' to ignore this");
		error("*****************************************************");
		error("Can not recover front_end state, version incompatible");
		error("*****************************************************");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);

	safe_unpack_time(&time_stamp, buffer);

	while (remaining_buf (buffer) > 0) {
		uint32_t base_state = NO_VAL;
		uint16_t obj_protocol_version = NO_VAL16;

		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpackstr_xmalloc (&node_name, &name_len, buffer);
			safe_unpack32 (&node_state,  buffer);
			safe_unpackstr_xmalloc (&reason,    &name_len, buffer);
			safe_unpack_time (&reason_time, buffer);
			safe_unpack32 (&reason_uid,  buffer);
			safe_unpack16 (&obj_protocol_version, buffer);
			base_state = node_state & NODE_STATE_BASE;
		} else
			goto unpack_error;

		/* validity test as possible */

		/* find record and perform update */
		front_end_ptr = find_front_end_record(node_name);
		if (front_end_ptr == NULL) {
			error("Front_end node %s has vanished from "
			      "configuration", node_name);
		} else if (state_only) {
			uint32_t orig_flags;
			orig_flags = front_end_ptr->node_state &
				     NODE_STATE_FLAGS;
			if (IS_NODE_UNKNOWN(front_end_ptr)) {
				if (base_state == NODE_STATE_DOWN) {
					orig_flags &= (~NODE_STATE_COMPLETING);
					front_end_ptr->node_state =
						NODE_STATE_DOWN | orig_flags;
				}
				if (node_state & NODE_STATE_DRAIN) {
					 front_end_ptr->node_state |=
						 NODE_STATE_DRAIN;
				}
				if (node_state & NODE_STATE_FAIL) {
					front_end_ptr->node_state |=
						NODE_STATE_FAIL;
				}
			}
			if (front_end_ptr->reason == NULL) {
				front_end_ptr->reason = reason;
				reason = NULL;	/* Nothing to free */
				front_end_ptr->reason_time = reason_time;
				front_end_ptr->reason_uid = reason_uid;
			}
		} else {
			front_end_ptr->node_state = node_state;
			xfree(front_end_ptr->reason);
			front_end_ptr->reason	= reason;
			reason			= NULL;	/* Nothing to free */
			front_end_ptr->reason_time	= reason_time;
			front_end_ptr->reason_uid	= reason_uid;
			front_end_ptr->last_response	= (time_t) 0;
		}

		if (front_end_ptr) {
			node_cnt++;
			if (obj_protocol_version != NO_VAL16)
				front_end_ptr->protocol_version =
					obj_protocol_version;
			else
				front_end_ptr->protocol_version =
					protocol_version;

			/* Sanity check to make sure we can take a version we
			 * actually understand.
			 */
			if (front_end_ptr->protocol_version <
			    SLURM_MIN_PROTOCOL_VERSION)
				front_end_ptr->protocol_version =
					SLURM_MIN_PROTOCOL_VERSION;
		}

		xfree(node_name);
		xfree(reason);
	}

fini:	info("Recovered state of %d front_end nodes", node_cnt);
	free_buf (buffer);
	return error_code;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete front_end node data checkpoint file, start with '-i' to ignore this");
	error("Incomplete front_end node data checkpoint file");
	error_code = EFAULT;
	xfree (node_name);
	xfree(reason);
	goto fini;
#else
	return 0;
#endif
}

/*
 * set_front_end_down - make the specified front end node's state DOWN and
 *	kill jobs as needed
 * IN front_end_pt - pointer to the front end node
 * IN reason - why the node is DOWN
 */
extern void set_front_end_down (front_end_record_t *front_end_ptr,
				char *reason)
{
#ifdef HAVE_FRONT_END
	time_t now = time(NULL);
	uint16_t state_flags = front_end_ptr->node_state & NODE_STATE_FLAGS;

	state_flags &= (~NODE_STATE_COMPLETING);
	front_end_ptr->node_state = NODE_STATE_DOWN | state_flags;
	trigger_front_end_down(front_end_ptr);
	(void) kill_job_by_front_end_name(front_end_ptr->name);
	if ((front_end_ptr->reason == NULL) ||
	    (xstrncmp(front_end_ptr->reason, "Not responding", 14) == 0)) {
		xfree(front_end_ptr->reason);
		front_end_ptr->reason = xstrdup(reason);
		front_end_ptr->reason_time = now;
		front_end_ptr->reason_uid = slurmctld_conf.slurm_user_id;
	}
	last_front_end_update = now;
#endif
}

/*
 * sync_front_end_state - synchronize job pointers and front-end node state
 */
extern void sync_front_end_state(void)
{
#ifdef HAVE_FRONT_END
	ListIterator job_iterator;
	struct job_record *job_ptr;
	front_end_record_t *front_end_ptr;
	uint32_t state_flags;
	int i;

	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		front_end_ptr->job_cnt_comp = 0;
		front_end_ptr->job_cnt_run  = 0;
	}

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->batch_host) {
			job_ptr->front_end_ptr =
				find_front_end_record(job_ptr->batch_host);
			if ((job_ptr->front_end_ptr == NULL) &&
			    IS_JOB_RUNNING(job_ptr)) {
				error("front end node %s has vanished, killing %pJ",
				      job_ptr->batch_host, job_ptr);
				job_ptr->job_state = JOB_NODE_FAIL |
						     JOB_COMPLETING;
			} else if (job_ptr->front_end_ptr == NULL) {
				info("front end node %s has vanished",
				     job_ptr->batch_host);
			} else if (IS_JOB_COMPLETING(job_ptr)) {
				job_ptr->front_end_ptr->job_cnt_comp++;
			} else if (IS_JOB_RUNNING(job_ptr) ||
				   IS_JOB_SUSPENDED(job_ptr)) {
				job_ptr->front_end_ptr->job_cnt_run++;
			}
		} else {
			job_ptr->front_end_ptr = NULL;
		}
	}
	list_iterator_destroy(job_iterator);

	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		if ((IS_NODE_IDLE(front_end_ptr) ||
		     IS_NODE_UNKNOWN(front_end_ptr)) &&
		    (front_end_ptr->job_cnt_run != 0)) {
			state_flags = front_end_ptr->node_state &
				      NODE_STATE_FLAGS;
			front_end_ptr->node_state = NODE_STATE_ALLOCATED |
						    state_flags;
		}
		if (IS_NODE_ALLOCATED(front_end_ptr) &&
		    (front_end_ptr->job_cnt_run == 0)) {
			state_flags = front_end_ptr->node_state &
				      NODE_STATE_FLAGS;
			front_end_ptr->node_state = NODE_STATE_IDLE |
						    state_flags;
		}
		if (IS_NODE_COMPLETING(front_end_ptr) &&
		    (front_end_ptr->job_cnt_comp == 0)) {
			front_end_ptr->node_state &= (~NODE_STATE_COMPLETING);
		}
		if (!IS_NODE_COMPLETING(front_end_ptr) &&
		    (front_end_ptr->job_cnt_comp != 0)) {
			front_end_ptr->node_state |= NODE_STATE_COMPLETING;
		}
	}

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_FRONT_END)
		log_front_end_state();
#endif
}
