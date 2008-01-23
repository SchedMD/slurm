/*****************************************************************************\
 *  nodeacct_storage_none.c - NO-OP slurm job completion logging plugin.
 *****************************************************************************
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-226842.
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

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <stdio.h>
#include <slurm/slurm_errno.h>

#include "src/slurmctld/slurmctld.h"
#include "src/common/slurm_nodeacct_storage.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a 
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job accounting API 
 * matures.
 */
const char plugin_name[] = "Node accounting storage NOT_INVOKED plugin";
const char plugin_type[] = "nodeacct_storage/none";
const uint32_t plugin_version = 100;

#define DEFAULT_NODEACCT_LOC "localhost"

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

extern int nodeacct_storage_p_node_down(struct node_record *node_ptr,
					time_t event_time,
					char *reason)
{
	uint16_t cpus;
	int rc = SLURM_ERROR;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];

	if (slurmctld_conf.fast_schedule)
		cpus = node_ptr->config_ptr->cpus;
	else
		cpus = node_ptr->cpus;

#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("Node_acct_down: %s at %s with %u cpus due to %s", 
	     node_ptr->name, tmp_buff, cpus, node_ptr->reason);
#endif
	/* If the node was already down end that record since the
	 * reason will most likely be different
	 */

	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) 
		return rc;
	
	gold_request_add_condition(gold_request, "Machine", cluster_name,
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "Name", node_ptr->name,
				   GOLD_OPERATOR_NONE);

	snprintf(tmp_buff, sizeof(tmp_buff), "%d", ((int)event_time - 1));
	gold_request_add_assignment(gold_request, "EndTime", tmp_buff);		
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("jobacct_p_cluster_procs: no response received");
		return rc;
	}

	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		destroy_gold_response(gold_response);
		return rc;
	}
	destroy_gold_response(gold_response);

	/* now add the new one */
	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_CREATE);
	if(!gold_request) 
		return rc;
	
	gold_request_add_assignment(gold_request, "Machine", cluster_name);
	snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)event_time);
	gold_request_add_assignment(gold_request, "StartTime", tmp_buff);
	gold_request_add_assignment(gold_request, "Name", node_ptr->name);
	snprintf(tmp_buff, sizeof(tmp_buff), "%u", node_ptr->cpus);
	gold_request_add_assignment(gold_request, "CPUCount", tmp_buff);
	if(reason)
		gold_request_add_assignment(gold_request, "Reason", reason);
	else	
		gold_request_add_assignment(gold_request, "Reason", 
					    node_ptr->reason);
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("jobacct_p_cluster_procs: no response received");
		return rc;
	}

	if(!gold_response->rc) 
		rc = SLURM_SUCCESS;
	else {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
	}
	destroy_gold_response(gold_response);

	return rc;
}

extern int nodeacct_storage_p_node_up(struct node_record *node_ptr,
				      time_t event_time)
{
	int rc = SLURM_ERROR;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];

#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("Node_acct_up: %s at %s", node_ptr->name, tmp_buff);
#endif
	/* FIXME: WRITE TO DATABASE HERE */

	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) 
		return rc;
	
	gold_request_add_condition(gold_request, "Machine", cluster_name,
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "Name", node_ptr->name,
				   GOLD_OPERATOR_NONE);

	snprintf(tmp_buff, sizeof(tmp_buff), "%d", ((int)event_time - 1));
	gold_request_add_assignment(gold_request, "EndTime", tmp_buff);		
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("jobacct_p_node_up: no response received");
		return rc;
	}

	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		destroy_gold_response(gold_response);
		return rc;
	}
	destroy_gold_response(gold_response);


	return rc;
}

extern int nodeacct_storage_p_cluster_procs(uint32_t procs, time_t event_time)
{
	static uint32_t last_procs = -1;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int rc = SLURM_ERROR;

	if (procs == last_procs) {
		debug3("we have the same procs as before no need to "
		       "query the database.");
		return SLURM_SUCCESS;
	}
	last_procs = procs;

	/* Record the processor count */
#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("Node_acct_procs: %s has %u total CPUs at %s", 
	     cluster_name, procs, tmp_buff);
#endif
	
	/* get the last known one */
	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return rc;
	gold_request_add_condition(gold_request, "Machine", cluster_name,
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "Name", "NULL",
				   GOLD_OPERATOR_NONE);

	gold_request_add_selection(gold_request, "CPUCount");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("jobacct_p_cluster_procs: no response received");
		return rc;
	}

	if(gold_response->entry_cnt > 0) {
		gold_response_entry_t *resp_entry = 
			list_pop(gold_response->entries);
		gold_name_value_t *name_val = list_pop(resp_entry->name_val);

		if(procs == atoi(name_val->value)) {
			debug("System hasn't changed since last entry");
			destroy_gold_name_value(name_val);
			destroy_gold_response_entry(resp_entry);
			destroy_gold_response(gold_response);
			return SLURM_SUCCESS;
		} else {
			debug("System has changed from %s cpus to %d",
			      name_val->value, procs);   
		}

		destroy_gold_name_value(name_val);
		destroy_gold_response_entry(resp_entry);
	} else {
		debug("We don't have an entry for this machine "
		      "most likely a first time running.");
	}

	destroy_gold_response(gold_response);
	


	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) 
		return rc;
	
	gold_request_add_condition(gold_request, "Machine", cluster_name,
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "Name", "NULL",
				   GOLD_OPERATOR_NONE);

	snprintf(tmp_buff, sizeof(tmp_buff), "%d", ((int)event_time - 1));
	gold_request_add_assignment(gold_request, "EndTime", tmp_buff);		
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("jobacct_p_cluster_procs: no response received");
		return rc;
	}

	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		destroy_gold_response(gold_response);
		return rc;
	}
	destroy_gold_response(gold_response);

	/* now add the new one */
	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_CREATE);
	if(!gold_request) 
		return rc;
	
	gold_request_add_assignment(gold_request, "Machine", cluster_name);
	snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)event_time);
	gold_request_add_assignment(gold_request, "StartTime", tmp_buff);
	snprintf(tmp_buff, sizeof(tmp_buff), "%u", procs);
	gold_request_add_assignment(gold_request, "CPUCount", tmp_buff);
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("jobacct_p_cluster_procs: no response received");
		return rc;
	}

	if(!gold_response->rc) 
		rc = SLURM_SUCCESS;
	else {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
	}
	destroy_gold_response(gold_response);

	return rc;
}

