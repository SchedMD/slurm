/*****************************************************************************\
 *  front_end.c - Define front end node functions.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <stdlib.h>

#include <slurm/slurm.h>
#include <src/common/list.h>
#include <src/common/log.h>
#include <src/common/node_conf.h>
#include <src/common/read_config.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/xstring.h>
#include <src/slurmctld/slurmctld.h>

front_end_record_t *front_end_nodes = NULL;
uint16_t front_end_node_cnt = 0;
time_t last_front_end_update = (time_t) 0;

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
	uint16_t state_base;
	time_t now = time(NULL);

	if ((host_list = hostlist_create(msg_ptr->name)) == NULL) {
		error("hostlist_create error on %s: %m", msg_ptr->name);
		return ESLURM_INVALID_NODE_NAME;
	}

	last_front_end_update = now;
	while ((this_node_name = hostlist_shift(host_list))) {
		for (i = 0, front_end_ptr = front_end_nodes;
		     i < front_end_node_cnt; i++, front_end_ptr++) {
			if (strcmp(this_node_name, front_end_ptr->name))
				continue;
			if (msg_ptr->node_state == (uint16_t) NO_VAL)
				;	/* No change in node state */
			else if (msg_ptr->node_state == NODE_RESUME)
				front_end_ptr->node_state = NODE_STATE_IDLE;
			else if (msg_ptr->node_state == NODE_STATE_DRAIN)
				front_end_ptr->node_state |= NODE_STATE_DRAIN;
			else if (msg_ptr->node_state == NODE_STATE_DOWN) {
				front_end_ptr->node_state &= NODE_STATE_FLAGS;
				front_end_ptr->node_state |= NODE_STATE_DOWN;
			}
			state_base = front_end_ptr->node_state &
				     NODE_STATE_BASE;
			if ((front_end_ptr->node_state & NODE_STATE_DRAIN) ||
			    (state_base == NODE_STATE_DOWN)) {
				if (msg_ptr->reason) {
					xfree(front_end_ptr->reason);
					front_end_ptr->reason =
						xstrdup(msg_ptr->reason);
					front_end_ptr->reason_time = now;
					front_end_ptr->reason_uid =
						msg_ptr->reason_uid;
				}
			} else if (front_end_ptr->reason) {
				/* Should not be any reason set */
				xfree(front_end_ptr->reason);
				front_end_ptr->reason_time = 0;
				front_end_ptr->reason_uid = 0;
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
#endif
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
		info("FrontendName=%s FrontendAddr=%s Port=%u State=%s "
		     "Reason=%s",
		     front_end_ptr->name, front_end_ptr->comm_name,
		     front_end_ptr->port,
		     node_state_string(front_end_ptr->node_state),
		     front_end_ptr->reason);
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
		xfree(front_end_ptr->comm_name);
		xfree(front_end_ptr->name);
		xfree(front_end_ptr->reason);
	}
	xfree(front_end_nodes);
	front_end_node_cnt = 0;
#endif
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
	uint16_t state_base, state_flags;
	int i;

	if (recover == 2)
		return;
	last_front_end_update = time(NULL);
	if (recover == 0)
		purge_front_end_state();
	if (front_end_list == NULL)
		return;		/* No front ends in slurm.conf */

	iter = list_iterator_create(front_end_list);
	if (iter == NULL)
		fatal("list_iterator_create: malloc failure");
	while ((slurm_conf_fe_ptr = (slurm_conf_frontend_t *)
				    list_next(iter))) {
		if (slurm_conf_fe_ptr->frontends == NULL)
			fatal("FrontendName is NULL");
		for (i = 0; i < front_end_node_cnt; i++) {
			if (strcmp(front_end_nodes[i].name,
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
	if (slurm_get_debug_flags() & DEBUG_FLAG_FRONT_END)
		log_front_end_state();
#endif
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
#ifdef HAVE_FRONT_END
	if (protocol_version >= SLURM_2_2_PROTOCOL_VERSION) {
		pack_time(dump_front_end_ptr->boot_time, buffer);
		packstr(dump_front_end_ptr->name, buffer);
		pack16(dump_front_end_ptr->node_state, buffer);

		packstr(dump_front_end_ptr->reason, buffer);
		pack_time(dump_front_end_ptr->reason_time, buffer);
		pack32(dump_front_end_ptr->reason_uid, buffer);

		pack_time(dump_front_end_ptr->slurmd_start_time, buffer);
	} else {
		error("_pack_front_end: Unsupported slurm version %u",
		      protocol_version);
	}
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
	uint32_t nodes_packed, tmp_offset;
	front_end_record_t *front_end_ptr;
	Buf buffer;
	int i;
	time_t now = time(NULL);

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf(BUF_SIZE * 2);
	nodes_packed = 0;

	if (protocol_version >= SLURM_2_2_PROTOCOL_VERSION) {
		/* write header: count and time */
		pack32(nodes_packed, buffer);
		pack_time(now, buffer);

		/* write records */
		for (i = 0, front_end_ptr = front_end_nodes;
		     i < front_end_node_cnt; i++, front_end_ptr++) {
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
}
