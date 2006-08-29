/*****************************************************************************\
 *  get_nodes.c - Process Wiki get node info request
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
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

#include "./msg.h"
#include "src/slurmctld/slurmctld.h"

static void	_dump_node(struct node_record *node_ptr);
static char *	_get_node_state(uint16_t state);

/*
 * get_nodes - get information on specific node(s) changed since some time
 * cmd_ptr IN - CMD=GETNODES ARG=[<UPDATETIME>:<NODEID>[:<NODEID>]...]
 *                               [<UPDATETIME>:ALL]
 * fd IN - file in which to write response
 * RET 0 on success, -1 on failure
 *
 * Response format
 * ARG=<cnt>#<NODEID>;STATE=<state>;CMEMORY=<mb>;CDISK=<mb>;CPROC=<cpus>;
 *                    FEATURE=<feature:feature>;PARTITION=<part>[#<NODEID;...];
 */
extern int	get_nodes(char *cmd_ptr, slurm_fd fd)
{
/* FIXME */
int err;
int *err_code = &err;
char **err_msg = &cmd_ptr;

	char *arg_ptr, *tmp_char;
	time_t update_time;

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = 300;
		*err_msg = "GETNODES lacks ARG";
		error("wiki: GETNODES lacks ARG");
		return -1;
	}
	update_time = (uint32_t) strtol(arg_ptr+4, &tmp_char, 10);
	if (tmp_char[0] != ':') {
		*err_code = 300;
		*err_msg = "Invalid ARG value";
		error("wiki: GETNODES has invalid ARG value");
		return -1;
	}
	tmp_char++;
	if (strncmp(tmp_char, "ALL", 3) == 0) {
		/* report all nodes */
	} else {
		*err_code = 300;
		*err_msg = "Support for individual node data not available";
		error("wiki: GETNODES list individual nodes");
		return -1;
	}
	return 0;
}

static void	_dump_node(struct node_record *node_ptr)
{
	char tmp[512];
	int i;
	uint32_t cpu_cnt;

	if (!node_ptr)
		return;

	snprintf(tmp, sizeof(tmp), "%s;STATE=%s;",
		node_ptr->name, 
		_get_node_state(node_ptr->node_state));

	if (slurmctld_conf.fast_schedule) {
		cpu_cnt = node_ptr->config_ptr->cpus;
		snprintf(tmp, sizeof(tmp),
			"CMEMORY=%u;CDISK=%u;CPROC=%u;",
			node_ptr->config_ptr->real_memory,
			node_ptr->config_ptr->tmp_disk,
			node_ptr->config_ptr->cpus);
	} else {
		cpu_cnt = node_ptr->cpus;
		snprintf(tmp, sizeof(tmp),
			"CMEMORY=%u;CDISK=%u;CPROC=%u;",
			node_ptr->real_memory,
			node_ptr->tmp_disk,
			node_ptr->cpus);
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
	}
		
	for (i=0; i<node_ptr->part_cnt; i++) {
		char *header;
		uint32_t cpu_avail;
		if (i == 0)
			header = "CCLASS=";
		else
			header = ",";
		snprintf(tmp, sizeof(tmp), "%s%s:%u", 
			header,
			node_ptr->part_pptr[i]->name,
			cpu_cnt);
/* FIXME: Modify to support shared nodes and consumable resources */
		if (node_ptr->node_state == NODE_STATE_IDLE)
			cpu_avail = cpu_cnt;
		else
			cpu_avail = 0;
		if (i == 0)
			header = "ACLASS=";
		else
			header = ",";
		snprintf(tmp, sizeof(tmp), "%s%s:%u",
			header,
			node_ptr->part_pptr[i]->name,
			cpu_cnt);
	}
}

static char *	_get_node_state(uint16_t state)
{
	uint16_t base_state = state & NODE_STATE_FLAGS;

	if (state & NODE_STATE_DRAIN)
		return "Draining";
	if (state & NODE_STATE_COMPLETING)
		return "Busy";

	if (base_state == NODE_STATE_DOWN)
		return "Down";
	if (base_state == NODE_STATE_ALLOCATED)
		return "Running";
	if (base_state == NODE_STATE_IDLE)
		return "Idle";
	
	return "Unknown";
}
