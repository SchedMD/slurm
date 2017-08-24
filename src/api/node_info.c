/*****************************************************************************\
 *  node_info.c - get/print the node state information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * slurm_print_node_info_msg - output information about all Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_info_msg_ptr - node information message pointer
 * IN one_liner - print as a single line if true
 */
void
slurm_print_node_info_msg ( FILE * out, node_info_msg_t * node_info_msg_ptr,
			    int one_liner )
{
	int i;
	node_info_t * node_ptr = node_info_msg_ptr -> node_array ;
	char time_str[32];

	slurm_make_time_str ((time_t *)&node_info_msg_ptr->last_update,
			     time_str, sizeof(time_str));
	fprintf( out, "Node data as of %s, record count %d\n",
		 time_str, node_info_msg_ptr->record_count);

	for (i = 0; i < node_info_msg_ptr-> record_count; i++) {
		slurm_print_node_table ( out, & node_ptr[i],
					 node_info_msg_ptr->node_scaling,
					 one_liner ) ;
	}
}


/*
 * slurm_print_node_table - output information about a specific Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_ptr - an individual node information record pointer
 * IN node_scaling - number of nodes each node represents
 * IN one_liner - print as a single line if true
 */
void
slurm_print_node_table ( FILE * out, node_info_t * node_ptr,
			 int node_scaling, int one_liner )
{
	char *print_this = slurm_sprint_node_table(node_ptr, node_scaling,
						   one_liner);
	fprintf ( out, "%s", print_this);
	xfree(print_this);
}

/* Given data structures containing information about nodes and partitions,
 * populate the node's "partitions" field */
void
slurm_populate_node_partitions(node_info_msg_t *node_buffer_ptr,
			       partition_info_msg_t *part_buffer_ptr)
{
	int i, j, n, p;
	node_info_t *node_ptr;
	partition_info_t *part_ptr;

	if (!node_buffer_ptr || (node_buffer_ptr->record_count == 0) ||
	    !part_buffer_ptr || (part_buffer_ptr->record_count == 0))
		return;

	for (n = 0, node_ptr = node_buffer_ptr->node_array;
	     n < node_buffer_ptr->record_count; n++, node_ptr++) {
		xfree(node_ptr->partitions);
	}

	/*
	 * Iterate through the partitions in the slurm.conf using "p".  The
	 * partition has an array of node index pairs to specify the range.
	 * Using "i", iterate by two's through the node list to get the
	 * begin-end node range.  Using "j", interate through the node range
	 * and add the partition name to the node's partition list.  If the
	 * node on the partition is a singleton (i.e. Nodes=node1), the
	 * begin-end range are both the same node index value.
	 */
	for (p = 0, part_ptr = part_buffer_ptr->partition_array;
	     p < part_buffer_ptr->record_count; p++, part_ptr++) {
		for (i = 0; ; i += 2) {
			if (part_ptr->node_inx[i] == -1)
				break;
			for (j = part_ptr->node_inx[i];
			     j <= part_ptr->node_inx[i+1]; j++) {
				char *sep = "";
				if ((j < 0) ||
				    (j >= node_buffer_ptr->record_count))
					continue;
				node_ptr = node_buffer_ptr->node_array + j;
				if (node_ptr->partitions)
					sep = ",";
				xstrfmtcat(node_ptr->partitions, "%s%s", sep,
					   part_ptr->name);
			}		
		}
	}
}

/*
 * slurm_sprint_node_table - output information about a specific Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN node_ptr - an individual node information record pointer
 * IN node_scaling - number of nodes each node represents
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
char *
slurm_sprint_node_table (node_info_t * node_ptr,
			 int node_scaling, int one_liner )
{
	uint32_t my_state = node_ptr->node_state;
	char *cloud_str = "", *comp_str = "", *drain_str = "", *power_str = "";
	char time_str[32];
	char *out = NULL, *reason_str = NULL, *select_reason_str = NULL;
	uint16_t err_cpus = 0, alloc_cpus = 0;
	int cpus_per_node = 1;
	int idle_cpus;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	uint64_t alloc_memory;
	char *node_alloc_tres = NULL;
	char *line_end = (one_liner) ? " " : "\n   ";

	if (node_scaling)
		cpus_per_node = node_ptr->cpus / node_scaling;

	if (my_state & NODE_STATE_CLOUD) {
		my_state &= (~NODE_STATE_CLOUD);
		cloud_str = "+CLOUD";
	}
	if (my_state & NODE_STATE_COMPLETING) {
		my_state &= (~NODE_STATE_COMPLETING);
		comp_str = "+COMPLETING";
	}
	if (my_state & NODE_STATE_DRAIN) {
		my_state &= (~NODE_STATE_DRAIN);
		drain_str = "+DRAIN";
	}
	if (my_state & NODE_STATE_FAIL) {
		my_state &= (~NODE_STATE_FAIL);
		drain_str = "+FAIL";
	}
	if (my_state & NODE_STATE_POWER_SAVE) {
		my_state &= (~NODE_STATE_POWER_SAVE);
		power_str = "+POWER";
	}
	slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
				  SELECT_NODEDATA_SUBCNT,
				  NODE_STATE_ALLOCATED,
				  &alloc_cpus);
	if (cluster_flags & CLUSTER_FLAG_BG) {
		if (!alloc_cpus &&
		    (IS_NODE_ALLOCATED(node_ptr) ||
		     IS_NODE_COMPLETING(node_ptr)))
			alloc_cpus = node_ptr->cpus;
		else
			alloc_cpus *= cpus_per_node;
	}
	idle_cpus = node_ptr->cpus - alloc_cpus;

	slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
				  SELECT_NODEDATA_SUBCNT,
				  NODE_STATE_ERROR,
				  &err_cpus);
	if (cluster_flags & CLUSTER_FLAG_BG)
		err_cpus *= cpus_per_node;
	idle_cpus -= err_cpus;

	if ((alloc_cpus && err_cpus) ||
	    (idle_cpus  && (idle_cpus != node_ptr->cpus))) {
		my_state &= NODE_STATE_FLAGS;
		my_state |= NODE_STATE_MIXED;
	}

	/****** Line 1 ******/
	xstrfmtcat(out, "NodeName=%s ", node_ptr->name);
	if (cluster_flags & CLUSTER_FLAG_BG) {
		slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
					  SELECT_NODEDATA_RACK_MP,
					  0, &select_reason_str);
		if (select_reason_str) {
			xstrfmtcat(out, "RackMidplane=%s ", select_reason_str);
			xfree(select_reason_str);
		}
	}

	if (node_ptr->arch)
		xstrfmtcat(out, "Arch=%s ", node_ptr->arch);

	xstrfmtcat(out, "CoresPerSocket=%u", node_ptr->cores);

	xstrcat(out, line_end);

	/****** Line ******/
	xstrfmtcat(out, "CPUAlloc=%u CPUErr=%u CPUTot=%u ",
		   alloc_cpus, err_cpus, node_ptr->cpus);

	if (node_ptr->cpu_load == NO_VAL)
		xstrcat(out, "CPULoad=N/A");
	else
		xstrfmtcat(out, "CPULoad=%.2f", (node_ptr->cpu_load / 100.0));

	xstrcat(out, line_end);

	/****** Line ******/
	xstrfmtcat(out, "AvailableFeatures=%s", node_ptr->features);
	xstrcat(out, line_end);

	/****** Line ******/
	xstrfmtcat(out, "ActiveFeatures=%s", node_ptr->features_act);
	xstrcat(out, line_end);

	/****** Line ******/
	xstrfmtcat(out, "Gres=%s", node_ptr->gres);
	xstrcat(out, line_end);

	/****** Line (optional) ******/
	if (node_ptr->gres_drain) {
		xstrfmtcat(out, "GresDrain=%s", node_ptr->gres_drain);
		xstrcat(out, line_end);
	}

	/****** Line (optional) ******/
	if (node_ptr->gres_used) {
		xstrfmtcat(out, "GresUsed=%s", node_ptr->gres_used);
		xstrcat(out, line_end);
	}

	/****** Line (optional) ******/
	{
		bool line_used = false;

		if (node_ptr->node_addr) {
			xstrfmtcat(out, "NodeAddr=%s ", node_ptr->node_addr);
			line_used = true;
		}

		if (node_ptr->node_hostname) {
			xstrfmtcat(out, "NodeHostName=%s ",
				   node_ptr->node_hostname);
			line_used = true;
		}

		if (node_ptr->port != slurm_get_slurmd_port()) {
			xstrfmtcat(out, "Port=%u ", node_ptr->port);
			line_used = true;
		}

		if (node_ptr->version && xstrcmp(node_ptr->version, slurmctld_conf.version)) {
			xstrfmtcat(out, "Version=%s", node_ptr->version);
			line_used = true;
		}

		if (line_used)
			xstrcat(out, line_end);
	}

	/****** Line ******/
	if (node_ptr->os)
		xstrfmtcat(out, "OS=%s ", node_ptr->os);

	slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
				  SELECT_NODEDATA_MEM_ALLOC,
				  NODE_STATE_ALLOCATED,
				  &alloc_memory);
	xstrfmtcat(out, "RealMemory=%"PRIu64" AllocMem=%"PRIu64" ",
		   node_ptr->real_memory, alloc_memory);

	if (node_ptr->free_mem == NO_VAL64)
		xstrcat(out, "FreeMem=N/A ");
	else
		xstrfmtcat(out, "FreeMem=%"PRIu64" ", node_ptr->free_mem);

	xstrfmtcat(out, "Sockets=%u Boards=%u",
		   node_ptr->sockets, node_ptr->boards);

	xstrcat(out, line_end);

	/****** core & memory specialization Line (optional) ******/
	if (node_ptr->core_spec_cnt || node_ptr->cpu_spec_list ||
	    node_ptr->mem_spec_limit) {
		if (node_ptr->core_spec_cnt) {
			xstrfmtcat(out, "CoreSpecCount=%u ",
				   node_ptr->core_spec_cnt);
		}
		if (node_ptr->cpu_spec_list) {
			xstrfmtcat(out, "CPUSpecList=%s ",
				   node_ptr->cpu_spec_list);
		}
		if (node_ptr->mem_spec_limit) {
			xstrfmtcat(out, "MemSpecLimit=%"PRIu64"",
				   node_ptr->mem_spec_limit);
		}
		xstrcat(out, line_end);
	}

	/****** Line ******/
	xstrfmtcat(out, "State=%s%s%s%s%s ThreadsPerCore=%u TmpDisk=%u Weight=%u ",
		   node_state_string(my_state),
		   cloud_str, comp_str, drain_str, power_str,
		   node_ptr->threads, node_ptr->tmp_disk, node_ptr->weight);

	if (node_ptr->owner == NO_VAL) {
		xstrcat(out, "Owner=N/A ");
	} else {
		char *user_name = uid_to_string((uid_t) node_ptr->owner);
		xstrfmtcat(out, "Owner=%s(%u) ", user_name, node_ptr->owner);
		xfree(user_name);
	}

	xstrfmtcat(out, "MCS_label=%s",
		   (node_ptr->mcs_label == NULL) ? "N/A" : node_ptr->mcs_label);

	xstrcat(out, line_end);

	/****** Line ******/
	if (node_ptr->partitions) {
		xstrfmtcat(out, "Partitions=%s ", node_ptr->partitions);
		xstrcat(out, line_end);
	}

	/****** Line ******/
	if (node_ptr->boot_time) {
		slurm_make_time_str((time_t *)&node_ptr->boot_time,
				    time_str, sizeof(time_str));
		xstrfmtcat(out, "BootTime=%s ", time_str);
	} else {
		xstrcat(out, "BootTime=None ");
	}

	if (node_ptr->slurmd_start_time) {
		slurm_make_time_str ((time_t *)&node_ptr->slurmd_start_time,
				     time_str, sizeof(time_str));
		xstrfmtcat(out, "SlurmdStartTime=%s", time_str);
	} else {
		xstrcat(out, "SlurmdStartTime=None");
	}
	xstrcat(out, line_end);

	/****** TRES Line ******/
	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_TRES_ALLOC_FMT_STR,
				     NODE_STATE_ALLOCATED, &node_alloc_tres);
	xstrfmtcat(out, "CfgTRES=%s", node_ptr->tres_fmt_str);
	xstrcat(out, line_end);
	xstrfmtcat(out, "AllocTRES=%s",
		   (node_alloc_tres) ?  node_alloc_tres : "");
	xfree(node_alloc_tres);
	xstrcat(out, line_end);

	/****** Power Management Line ******/
	if (!node_ptr->power || (node_ptr->power->cap_watts == NO_VAL))
		xstrcat(out, "CapWatts=n/a");
	else
		xstrfmtcat(out, "CapWatts=%u", node_ptr->power->cap_watts);

	xstrcat(out, line_end);

	/****** Power Consumption Line ******/
	if (!node_ptr->energy || node_ptr->energy->current_watts == NO_VAL)
		xstrcat(out, "CurrentWatts=n/s LowestJoules=n/s ConsumedJoules=n/s");
	else
		xstrfmtcat(out, "CurrentWatts=%u "
				"LowestJoules=%"PRIu64" "
				"ConsumedJoules=%"PRIu64"",
				node_ptr->energy->current_watts,
				node_ptr->energy->base_consumed_energy,
				node_ptr->energy->consumed_energy);

	xstrcat(out, line_end);

	/****** external sensors Line ******/
	if (!node_ptr->ext_sensors
	    || node_ptr->ext_sensors->consumed_energy == NO_VAL)
		xstrcat(out, "ExtSensorsJoules=n/s ");
	else
		xstrfmtcat(out, "ExtSensorsJoules=%"PRIu64" ",
			   node_ptr->ext_sensors->consumed_energy);

	if (!node_ptr->ext_sensors
	    || node_ptr->ext_sensors->current_watts == NO_VAL)
		xstrcat(out, "ExtSensorsWatts=n/s ");
	else
		xstrfmtcat(out, "ExtSensorsWatts=%u ",
			   node_ptr->ext_sensors->current_watts);

	if (!node_ptr->ext_sensors
	    || node_ptr->ext_sensors->temperature == NO_VAL)
		xstrcat(out, "ExtSensorsTemp=n/s");
	else
		xstrfmtcat(out, "ExtSensorsTemp=%u",
			   node_ptr->ext_sensors->temperature);

	xstrcat(out, line_end);

	/****** Line ******/
	if (node_ptr->reason && node_ptr->reason[0])
		xstrcat(reason_str, node_ptr->reason);
	slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
				  SELECT_NODEDATA_EXTRA_INFO,
				  0, &select_reason_str);
	if (select_reason_str && select_reason_str[0]) {
		if (reason_str)
			xstrcat(reason_str, "\n");
		xstrcat(reason_str, select_reason_str);
	}
	xfree(select_reason_str);
	if (reason_str) {
		int inx = 1;
		char *save_ptr = NULL, *tok, *user_name;
		tok = strtok_r(reason_str, "\n", &save_ptr);
		while (tok) {
			if (inx == 1) {
				xstrcat(out, "Reason=");
			} else {
				xstrcat(out, line_end);
				xstrcat(out, "       ");
			}
			xstrfmtcat(out, "%s", tok);
			if ((inx++ == 1) && node_ptr->reason_time) {
				user_name = uid_to_string(node_ptr->reason_uid);
				slurm_make_time_str((time_t *)&node_ptr->reason_time,
						    time_str, sizeof(time_str));
				xstrfmtcat(out, " [%s@%s]", user_name, time_str);
				xfree(user_name);
			}
			tok = strtok_r(NULL, "\n", &save_ptr);
		}
		xfree(reason_str);
	}
	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");

	return out;
}

static void _set_node_mixed(node_info_msg_t *resp)
{
	node_info_t *node_ptr = NULL;
	int i;

	if (!resp)
		return;

	for (i = 0, node_ptr = resp->node_array;
	     i < resp->record_count; i++, node_ptr++) {
		uint16_t used_cpus = 0;
		select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
					     SELECT_NODEDATA_SUBCNT,
					     NODE_STATE_ALLOCATED, &used_cpus);
		if ((used_cpus != 0) && (used_cpus != node_ptr->cpus)) {
			node_ptr->node_state &= NODE_STATE_FLAGS;
			node_ptr->node_state |= NODE_STATE_MIXED;
		}
	}
}

/*
 * slurm_load_node - issue RPC to get slurm all node configuration information
 *	if changed since update_time
 * IN update_time - time of current configuration data
 * OUT resp - place to store a node configuration pointer
 * IN show_flags - node filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_node (time_t update_time,
			    node_info_msg_t **resp, uint16_t show_flags)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	node_info_request_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req.last_update  = update_time;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_NODE_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_NODE_INFO:
		*resp = (node_info_msg_t *) resp_msg.data;
		if (show_flags & SHOW_MIXED)
			_set_node_mixed(*resp);
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		*resp = NULL;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_load_node_single - issue RPC to get slurm configuration information
 *	for a specific node
 * OUT resp - place to store a node configuration pointer
 * IN node_name - name of the node for which information is requested
 * IN show_flags - node filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_node_single (node_info_msg_t **resp,
				   char *node_name, uint16_t show_flags)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	node_info_single_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req.node_name    = node_name;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_NODE_INFO_SINGLE;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_NODE_INFO:
		*resp = (node_info_msg_t *) resp_msg.data;
		if (show_flags & SHOW_MIXED)
			_set_node_mixed(*resp);
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		*resp = NULL;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_get_node_energy_n - issue RPC to get the energy data of all
 * configured sensors on the target machine
 * IN  host  - name of node to query, NULL if localhost
 * IN  delta - Use cache if data is newer than this in seconds
 * OUT sensors_cnt - number of sensors
 * OUT energy - array of acct_gather_energy_t structures on success or
 *                NULL other wise
 * RET 0 on success or a slurm error code
 * NOTE: free the response using xfree
 */
extern int slurm_get_node_energy(char *host, uint16_t delta,
				 uint16_t *sensor_cnt,
				 acct_gather_energy_t **energy)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	acct_gather_energy_req_msg_t req;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	char *this_addr;

	xassert(sensor_cnt);
	xassert(energy);

	*sensor_cnt = 0;
	*energy = NULL;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	if (host)
		slurm_conf_get_addr(host, &req_msg.address);
	else if (cluster_flags & CLUSTER_FLAG_MULTSD) {
		if ((this_addr = getenv("SLURMD_NODENAME"))) {
			slurm_conf_get_addr(this_addr, &req_msg.address);
		} else {
			this_addr = "localhost";
			slurm_set_addr(&req_msg.address,
				       (uint16_t)slurm_get_slurmd_port(),
				       this_addr);
		}
	} else {
		char this_host[256];
		/*
		 *  Set request message address to slurmd on localhost
		 */
		gethostname_short(this_host, sizeof(this_host));
		this_addr = slurm_conf_get_nodeaddr(this_host);
		if (this_addr == NULL)
			this_addr = xstrdup("localhost");
		slurm_set_addr(&req_msg.address,
			       (uint16_t)slurm_get_slurmd_port(),
			       this_addr);
		xfree(this_addr);
	}

	req.delta        = delta;
	req_msg.msg_type = REQUEST_ACCT_GATHER_ENERGY;
	req_msg.data     = &req;

	rc = slurm_send_recv_node_msg(&req_msg, &resp_msg, 0);

	if (rc != 0 || !resp_msg.auth_cred) {
		error("slurm_get_node_energy: %m");
		if (resp_msg.auth_cred)
			g_slurm_auth_destroy(resp_msg.auth_cred);
		return SLURM_ERROR;
	}
	if (resp_msg.auth_cred)
		g_slurm_auth_destroy(resp_msg.auth_cred);
	switch (resp_msg.msg_type) {
	case RESPONSE_ACCT_GATHER_ENERGY:
		*sensor_cnt = ((acct_gather_node_resp_msg_t *)
			       resp_msg.data)->sensor_cnt;
		*energy = ((acct_gather_node_resp_msg_t *)
			   resp_msg.data)->energy;
		((acct_gather_node_resp_msg_t *) resp_msg.data)->energy = NULL;
		slurm_free_acct_gather_node_resp_msg(resp_msg.data);
		break;
	case RESPONSE_SLURM_RC:
	        rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}
