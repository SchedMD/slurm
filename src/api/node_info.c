/*****************************************************************************\
 *  node_info.c - get/print the node state information of slurm
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#ifdef HAVE_SYS_SYSLOG_H
#  include <sys/syslog.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
	char load_str[32], tmp_line[512], time_str[32];
	char *out = NULL, *reason_str = NULL, *select_reason_str = NULL;
	uint16_t err_cpus = 0, alloc_cpus = 0;
	int cpus_per_node = 1;
	int total_used = node_ptr->cpus;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	uint32_t alloc_memory;

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
	total_used -= alloc_cpus;

	slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
				  SELECT_NODEDATA_SUBCNT,
				  NODE_STATE_ERROR,
				  &err_cpus);
	if (cluster_flags & CLUSTER_FLAG_BG)
		err_cpus *= cpus_per_node;
	total_used -= err_cpus;

	if ((alloc_cpus && err_cpus) ||
	    (total_used  && (total_used != node_ptr->cpus))) {
		my_state &= NODE_STATE_FLAGS;
		my_state |= NODE_STATE_MIXED;
	}

	/****** Line 1 ******/
	snprintf(tmp_line, sizeof(tmp_line), "NodeName=%s ", node_ptr->name);
	xstrcat(out, tmp_line);
	if (cluster_flags & CLUSTER_FLAG_BG) {
		slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
					  SELECT_NODEDATA_RACK_MP,
					  0, &select_reason_str);
		if (select_reason_str) {
			xstrfmtcat(out, "RackMidplane=%s ", select_reason_str);
			xfree(select_reason_str);
		}
	}

	if (node_ptr->arch) {
		snprintf(tmp_line, sizeof(tmp_line), "Arch=%s ",
			 node_ptr->arch);
		xstrcat(out, tmp_line);
	}
	snprintf(tmp_line, sizeof(tmp_line), "CoresPerSocket=%u",
		 node_ptr->cores);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 2 ******/
	if (node_ptr->cpu_load == NO_VAL)
		strcpy(load_str, "N/A");
	else {
		snprintf(load_str, sizeof(load_str), "%.2f",
			 (node_ptr->cpu_load / 100.0));
	}
	snprintf(tmp_line, sizeof(tmp_line),
		 "CPUAlloc=%u CPUErr=%u CPUTot=%u CPULoad=%s Features=%s",
		 alloc_cpus, err_cpus, node_ptr->cpus, load_str,
		 node_ptr->features);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 3 ******/
	snprintf(tmp_line, sizeof(tmp_line), "Gres=%s", node_ptr->gres);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 4 (optional) ******/
	if (node_ptr->gres_drain) {
		snprintf(tmp_line, sizeof(tmp_line), "GresDrain=%s",
			 node_ptr->gres_drain);
		xstrcat(out, tmp_line);
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
	}

	/****** Line 5 (optional) ******/
	if (node_ptr->gres_used) {
		snprintf(tmp_line, sizeof(tmp_line), "GresUsed=%s",
			 node_ptr->gres_used);
		xstrcat(out, tmp_line);
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
	}

	/****** Line 6 (optional) ******/
	if (node_ptr->node_hostname || node_ptr->node_addr) {
		snprintf(tmp_line, sizeof(tmp_line),
			 "NodeAddr=%s NodeHostName=%s Version=%s",
			 node_ptr->node_addr, node_ptr->node_hostname,
			 node_ptr->version);
		xstrcat(out, tmp_line);
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
	}

	/****** Line 7 ******/
	if (node_ptr->os) {
		snprintf(tmp_line, sizeof(tmp_line), "OS=%s ", node_ptr->os);
		xstrcat(out, tmp_line);
	}
	slurm_get_select_nodeinfo(node_ptr->select_nodeinfo,
				  SELECT_NODEDATA_MEM_ALLOC,
				  NODE_STATE_ALLOCATED,
				  &alloc_memory);
	snprintf(tmp_line, sizeof(tmp_line),
		 "RealMemory=%u AllocMem=%u Sockets=%u Boards=%u",
		 node_ptr->real_memory, alloc_memory,
		 node_ptr->sockets, node_ptr->boards);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** core & memory specialization Line (optional) ******/
	if (node_ptr->core_spec_cnt || node_ptr->cpu_spec_list ||
	    node_ptr->mem_spec_limit) {
		if (node_ptr->core_spec_cnt) {
			snprintf(tmp_line, sizeof(tmp_line),
				 "CoreSpecCount=%u ", node_ptr->core_spec_cnt);
			xstrcat(out, tmp_line);
		}
		if (node_ptr->cpu_spec_list) {
			snprintf(tmp_line, sizeof(tmp_line), "CPUSpecList=%s ",
				 node_ptr->cpu_spec_list);
			xstrcat(out, tmp_line);
		}
		if (node_ptr->mem_spec_limit) {
			snprintf(tmp_line, sizeof(tmp_line), "MemSpecLimit=%u",
				 node_ptr->mem_spec_limit);
			xstrcat(out, tmp_line);
		}
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
	}

	/****** Line 8 ******/

	snprintf(tmp_line, sizeof(tmp_line),
		 "State=%s%s%s%s%s ThreadsPerCore=%u TmpDisk=%u Weight=%u",
		 node_state_string(my_state),
		 cloud_str, comp_str, drain_str, power_str,
		 node_ptr->threads, node_ptr->tmp_disk, node_ptr->weight);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 9 ******/
	if (node_ptr->boot_time) {
		slurm_make_time_str ((time_t *)&node_ptr->boot_time,
				     time_str, sizeof(time_str));
	} else {
		strncpy(time_str, "None", sizeof(time_str));
	}
	snprintf(tmp_line, sizeof(tmp_line), "BootTime=%s ", time_str);
	xstrcat(out, tmp_line);

	if (node_ptr->slurmd_start_time) {
		slurm_make_time_str ((time_t *)&node_ptr->slurmd_start_time,
				     time_str, sizeof(time_str));
	} else {
		strncpy(time_str, "None", sizeof(time_str));
	}
	snprintf(tmp_line, sizeof(tmp_line), "SlurmdStartTime=%s", time_str);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** power Line ******/
	if (!node_ptr->energy || node_ptr->energy->current_watts == NO_VAL)
		snprintf(tmp_line, sizeof(tmp_line), "CurrentWatts=n/s "
				"LowestJoules=n/s ConsumedJoules=n/s");
	else
		snprintf(tmp_line, sizeof(tmp_line), "CurrentWatts=%u "
				"LowestJoules=%u ConsumedJoules=%u",
				node_ptr->energy->current_watts,
				node_ptr->energy->base_watts,
				node_ptr->energy->consumed_energy);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** external sensors Line ******/
	if (!node_ptr->ext_sensors
	    || node_ptr->ext_sensors->consumed_energy == NO_VAL)
		snprintf(tmp_line, sizeof(tmp_line), "ExtSensorsJoules=n/s ");
	else
		snprintf(tmp_line, sizeof(tmp_line), "ExtSensorsJoules=%u ",
			 node_ptr->ext_sensors->consumed_energy);
	xstrcat(out, tmp_line);
	if (!node_ptr->ext_sensors
	    || node_ptr->ext_sensors->current_watts == NO_VAL)
		snprintf(tmp_line, sizeof(tmp_line), "ExtSensorsWatts=n/s ");
	else
		snprintf(tmp_line, sizeof(tmp_line), "ExtSensorsWatts=%u ",
			 node_ptr->ext_sensors->current_watts);
	xstrcat(out, tmp_line);
	if (!node_ptr->ext_sensors
	    || node_ptr->ext_sensors->temperature == NO_VAL)
		snprintf(tmp_line, sizeof(tmp_line), "ExtSensorsTemp=n/s");
	else
		snprintf(tmp_line, sizeof(tmp_line), "ExtSensorsTemp=%u",
			 node_ptr->ext_sensors->temperature);
	xstrcat(out, tmp_line);

	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 12 ******/
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
				if (one_liner)
					xstrcat(out, " ");
				else
					xstrcat(out, "\n   ");
				xstrcat(out, "       ");
			}
			snprintf(tmp_line, sizeof(tmp_line), "%s", tok);
			xstrcat(out, tmp_line);
			if ((inx++ == 1) && node_ptr->reason_time) {
				user_name = uid_to_string(node_ptr->reason_uid);
				slurm_make_time_str((time_t *)&node_ptr->reason_time,
						    time_str,sizeof(time_str));
				snprintf(tmp_line, sizeof(tmp_line),
					 " [%s@%s]", user_name, time_str);
				xstrcat(out, tmp_line);
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
	uint16_t used_cpus = 0;
	int i;

	if (!resp)
		return;

	for (i = 0, node_ptr = resp->node_array;
	     i < resp->record_count; i++, node_ptr++) {
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
 * slurm_node_energy - issue RPC to get the energy data on this machine
 * IN  host  - name of node to query, NULL if localhost
 * IN  delta - Use cache if data is newer than this in seconds
 * OUT acct_gather_energy_t structure on success or NULL other wise
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_acct_gather_energy_destroy
 */
extern int slurm_get_node_energy(char *host, uint16_t delta,
				 acct_gather_energy_t **acct_gather_energy)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	acct_gather_energy_req_msg_t req;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	char *this_addr;

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
		*acct_gather_energy = ((acct_gather_node_resp_msg_t *)
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
