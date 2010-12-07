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
