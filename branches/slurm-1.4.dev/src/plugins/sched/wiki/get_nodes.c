/*****************************************************************************\
 *  get_nodes.c - Process Wiki get node info request
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "./msg.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

static char *	_dump_all_nodes(int *node_cnt, time_t update_time);
static char *	_dump_node(struct node_record *node_ptr, time_t update_time);
static char *	_get_node_state(struct node_record *node_ptr);

/*
 * get_nodes - get information on specific node(s) changed since some time
 * cmd_ptr IN - CMD=GETNODES ARG=[<UPDATETIME>:<NODEID>[:<NODEID>]...]
 *                               [<UPDATETIME>:ALL]
 * RET 0 on success, -1 on failure
 *
 * Response format
 * Response format
 * ARG=<cnt>#<NODEID>:
 *	STATE=<state>;		 Moab equivalent node state
 *	[ARCH=<architecture>;]	 Computer architecture
 *	[OS=<operating_system>;] Operating system
 *	CMEMORY=<MB>;		 MB of memory on node
 *	CDISK=<MB>;		 MB of disk space on node
 *	CPROC=<cpus>;		 CPU count on node
 *	[FEATURE=<feature>;]	 Features associated with node, if any
 *  [#<NODEID>:...];
 */
extern int	get_nodes(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *tmp_char, *tmp_buf, *buf = NULL;
	time_t update_time;
	/* Locks: read node, read partition */
	slurmctld_lock_t node_read_lock = {
		NO_LOCK, NO_LOCK, READ_LOCK, READ_LOCK };
	int node_rec_cnt = 0, buf_size = 0;

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "GETNODES lacks ARG";
		error("wiki: GETNODES lacks ARG");
		return -1;
	}
	update_time = (time_t) strtoul(arg_ptr+4, &tmp_char, 10);
	if (tmp_char[0] != ':') {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: GETNODES has invalid ARG value");
		return -1;
	}
	tmp_char++;
	lock_slurmctld(node_read_lock);
	if (strncmp(tmp_char, "ALL", 3) == 0) {
		/* report all nodes */
		buf = _dump_all_nodes(&node_rec_cnt, update_time);
	} else {
		struct node_record *node_ptr = NULL;
		char *node_name = NULL, *tmp2_char = NULL;

		node_name = strtok_r(tmp_char, ":", &tmp2_char);
		while (node_name) {
			node_ptr = find_node_record(node_name);
			if (node_ptr == NULL) {
				error("sched/wiki2: bad hostname %s",
				      node_name);
				continue;
			}
			tmp_buf = _dump_node(node_ptr, update_time);
			if (node_rec_cnt > 0)
				xstrcat(buf, "#");
			xstrcat(buf, tmp_buf);
			xfree(tmp_buf);
			node_rec_cnt++;
			node_name = strtok_r(NULL, ":", &tmp2_char);
		}
	}
	unlock_slurmctld(node_read_lock);

	/* Prepend ("ARG=%d", node_rec_cnt) to reply message */
	if (buf)
		buf_size = strlen(buf);
	tmp_buf = xmalloc(buf_size + 32);
	if (node_rec_cnt)
		sprintf(tmp_buf, "SC=0 ARG=%d#%s", node_rec_cnt, buf);
	else
		sprintf(tmp_buf, "SC=0 ARG=0#");
	xfree(buf);
	*err_code = 0;
	*err_msg = tmp_buf;
	return 0;
}

static char *	_dump_all_nodes(int *node_cnt, time_t update_time)
{
	int i, cnt = 0;
	struct node_record *node_ptr = node_record_table_ptr;
	char *tmp_buf, *buf = NULL;
	uint16_t base_state;

	for (i=0; i<node_record_count; i++, node_ptr++) {
		if (node_ptr->name == NULL)
			continue;
		base_state = node_ptr->node_state & NODE_STATE_BASE;
		if (base_state == NODE_STATE_FUTURE)
			continue;
		tmp_buf = _dump_node(node_ptr, update_time);
		if (cnt > 0)
			xstrcat(buf, "#");
		xstrcat(buf, tmp_buf);
		xfree(tmp_buf);
		cnt++;
	}
	*node_cnt = cnt;
	return buf;
}

static char *	_dump_node(struct node_record *node_ptr, time_t update_time)
{
	char tmp[512], *buf = NULL;
	int i;

	if (!node_ptr)
		return NULL;

	snprintf(tmp, sizeof(tmp), "%s:STATE=%s;",
		node_ptr->name, 
		_get_node_state(node_ptr));
	xstrcat(buf, tmp);

	if (node_ptr->arch) {
		snprintf(tmp, sizeof(tmp), "ARCH=%s;", node_ptr->arch);
		xstrcat(buf, tmp);
	}

	if (node_ptr->os) {
		snprintf(tmp, sizeof(tmp), "OS=%s;", node_ptr->os);
		xstrcat(buf, tmp);
	}

	if (node_ptr->config_ptr
	&&  node_ptr->config_ptr->feature) {
		snprintf(tmp, sizeof(tmp), "FEATURES=%s;",
			node_ptr->config_ptr->feature);
		/* comma separated to colon */
		for (i=0; (tmp[i] != '\0'); i++) {
			if ((tmp[i] == ',')
			||  (tmp[i] == '|'))
				tmp[i] = ':';
		}
		xstrcat(buf, tmp);
	}

	if (update_time > 0)
		return buf;

	if (slurmctld_conf.fast_schedule) {
		/* config from slurm.conf */
		snprintf(tmp, sizeof(tmp),
			"CMEMORY=%u;CDISK=%u;CPROC=%u;",
			node_ptr->config_ptr->real_memory,
			node_ptr->config_ptr->tmp_disk,
			node_ptr->config_ptr->cpus);
	} else {
		/* config as reported by slurmd */
		snprintf(tmp, sizeof(tmp),
			"CMEMORY=%u;CDISK=%u;CPROC=%u;",
			node_ptr->real_memory,
			node_ptr->tmp_disk,
			node_ptr->cpus);
	}
	xstrcat(buf, tmp);

	return buf;
}

static char *	_get_node_state(struct node_record *node_ptr)
{
	static bool got_select_type = false;
	static bool node_allocations;
	uint16_t state = node_ptr->node_state;
	uint16_t base_state = state & NODE_STATE_BASE;

	if (!got_select_type) {
		char * select_type = slurm_get_select_type();
		if (select_type && 
		    (strcasecmp(select_type, "select/linear") == 0))
			node_allocations = true;
		else
			node_allocations = false;
		xfree(select_type);
		got_select_type = true;
	}

	if ((state & NODE_STATE_DRAIN)
	||  (state & NODE_STATE_FAIL))
		return "Draining";
	if (state & NODE_STATE_COMPLETING)
		return "Busy";

	if (base_state == NODE_STATE_DOWN)
		return "Down";
	if (base_state == NODE_STATE_ALLOCATED) {
		if (node_allocations)
			return "Busy";
		else
			return "Running";
	}
	if (base_state == NODE_STATE_IDLE)
		return "Idle";
	
	return "Unknown";
}
