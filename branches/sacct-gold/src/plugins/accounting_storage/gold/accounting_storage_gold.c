/*****************************************************************************\
 *  accounting_storage_gold.c - accounting interface to gold.
 *
 *  $Id: accounting_gold.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <pwd.h>


#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/xstring.h"
#include "src/common/uid.h"
#include <src/common/parse_time.h>

#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_accounting_storage.h"

#include "src/database/gold_interface.h"

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
const char plugin_name[] = "Accounting storage GOLD plugin";
const char plugin_type[] = "accounting_storage/gold";
const uint32_t plugin_version = 100;

static char *cluster_name = NULL;


static void _destroy_char(void *object);
static List _get_association_list_from_response(gold_response_t *gold_response);
static int _get_cluster_accounting_list_from_response(
	gold_response_t *gold_response, 
	acct_cluster_rec_t *cluster_rec);
static int _get_acct_accounting_list_from_response(
	gold_response_t *gold_response,
	acct_association_rec_t *account_rec);
static List _get_user_list_from_response(gold_response_t *gold_response);
static List _get_acct_list_from_response(gold_response_t *gold_response);
static List _get_cluster_list_from_response(gold_response_t *gold_response);
static int _remove_association_accounting(List id_list);


static void _destroy_char(void *object)
{
	char *tmp = (char *)object;
	xfree(tmp);
}

static List _get_association_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List association_list = NULL;
	acct_association_rec_t *acct_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	association_list = list_create(destroy_acct_association_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		acct_rec = xmalloc(sizeof(acct_association_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, "Id")) {
				acct_rec->id = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Parent")) {
				acct_rec->parent = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "FairShare")) {
				acct_rec->fairshare = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxJobs")) {
				acct_rec->max_jobs = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxNodesPerJob")) {
				acct_rec->max_nodes_per_job = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxWallDurationPerJob")) {
				acct_rec->max_wall_duration_per_job = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxProcSecondsPerJob")) {
				acct_rec->max_cpu_seconds_per_job = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "User")) {
				acct_rec->user = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Project")) {
				acct_rec->acct = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Machine")) {
				acct_rec->cluster = 
					xstrdup(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(association_list, acct_rec);
	}
	list_iterator_destroy(itr);

	return association_list;
}

static int _get_cluster_accounting_list_from_response(
	gold_response_t *gold_response,
	acct_cluster_rec_t *cluster_rec)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	cluster_accounting_rec_t *clusteracct_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	if(gold_response->entry_cnt <= 0) {
		debug2("_get_list_from_response: No entries given");
		return SLURM_ERROR;
	}
	if(!cluster_rec->accounting_list)
		cluster_rec->accounting_list = 
			list_create(destroy_cluster_accounting_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		clusteracct_rec = xmalloc(sizeof(cluster_accounting_rec_t));
		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, "CPUCount")) {
				clusteracct_rec->cpu_count = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "PeriodStart")) {
				clusteracct_rec->period_start = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "IdleCPUSeconds")) {
				clusteracct_rec->idle_secs = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "DownCPUSeconds")) {
				clusteracct_rec->down_secs = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "AllocatedCPUSeconds")) {
				clusteracct_rec->alloc_secs = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "ReservedCPUSeconds")) {
				clusteracct_rec->resv_secs = 
					atoi(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(cluster_rec->accounting_list, clusteracct_rec);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static int _get_acct_accounting_list_from_response(
	gold_response_t *gold_response,
	acct_association_rec_t *acct_rec)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	acct_accounting_rec_t *accounting_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	if(!acct_rec->accounting_list)
		acct_rec->accounting_list =
			list_create(destroy_acct_accounting_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		accounting_rec = xmalloc(sizeof(acct_accounting_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, "PeriodStart")) {
				accounting_rec->period_start = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, "AllocatedCPUSecs")) {
				accounting_rec->alloc_secs = 
					atoi(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(acct_rec->accounting_list, accounting_rec);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
	
}

static List _get_user_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List user_list = NULL;
	acct_user_rec_t *user_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	user_list = list_create(destroy_acct_user_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		user_rec = xmalloc(sizeof(acct_user_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, "Name")) {
				struct passwd *passwd_ptr = NULL;
				user_rec->name = 
					xstrdup(name_val->value);
				passwd_ptr = getpwnam(user_rec->name);
				if(passwd_ptr) {
					user_rec->uid = passwd_ptr->pw_uid;
					user_rec->gid = passwd_ptr->pw_gid;
				}
				
			} else if(!strcmp(name_val->name, "Expedite")) {
				user_rec->expedite = 
					atoi(name_val->value)+1;
			} else if(!strcmp(name_val->name, "DefaultProject")) {
				user_rec->default_acct = 
					xstrdup(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(user_list, user_rec);
	}
	list_iterator_destroy(itr);

	return user_list;
}

static List _get_acct_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List acct_list = NULL;
	acct_account_rec_t *acct_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	acct_list = list_create(destroy_acct_account_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		acct_rec = xmalloc(sizeof(acct_account_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, "Expedite")) {
				acct_rec->expedite = 
					atoi(name_val->value)+1;
			} else if(!strcmp(name_val->name, 
					  "Name")) {
				acct_rec->name = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Organization")) {
				acct_rec->organization = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Description")) {
				acct_rec->description = 
					xstrdup(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(acct_list, acct_rec);
	}
	list_iterator_destroy(itr);

	return acct_list;
}

static List _get_cluster_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List cluster_list = NULL;
	acct_cluster_rec_t *cluster_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	cluster_list = list_create(destroy_acct_cluster_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		cluster_rec = xmalloc(sizeof(acct_cluster_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, 
					  "Name")) {
				cluster_rec->name = 
					xstrdup(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(cluster_list, cluster_rec);
	}
	list_iterator_destroy(itr);

	return cluster_list;
}

static int _remove_association_accounting(List id_list)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	int set = 0;
	ListIterator itr = NULL;

	gold_request = create_gold_request(GOLD_OBJECT_ACCT_HOUR_USAGE,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("couldn't create gold_request");
		rc = SLURM_ERROR;
		return rc;
	}
	
	if(id_list && list_count(id_list)) {
		itr = list_iterator_create(id_list);
		if(list_count(id_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Acct",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}
			       
	gold_response = get_gold_response(gold_request);	

	if(!gold_response) {
		error("acct_storage_p_modify_associations: "
		      "no response received");
		destroy_gold_request(gold_request);
		rc = SLURM_ERROR;
		return rc;
	}
		
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		destroy_gold_request(gold_request);
		destroy_gold_response(gold_response);
		rc = SLURM_ERROR;
		return rc;
	}

	destroy_gold_response(gold_response);

	gold_request->object = GOLD_OBJECT_ACCT_DAY_USAGE;	
	gold_response = get_gold_response(gold_request);	
	
	if(!gold_response) {
		error("acct_storage_p_modify_associations: "
		      "no response received");
		destroy_gold_request(gold_request);
		rc = SLURM_ERROR;
		return rc;
	}
		
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		destroy_gold_request(gold_request);
		destroy_gold_response(gold_response);
		rc = SLURM_ERROR;
		return rc;
	}
	destroy_gold_response(gold_response);
	
	gold_request->object = GOLD_OBJECT_ACCT_MONTH_USAGE;	
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
		
	if(!gold_response) {
		error("acct_storage_p_modify_associations: "
		      "no response received");
		destroy_gold_request(gold_request);
		rc = SLURM_ERROR;
		return rc;
	}
		
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}

	destroy_gold_response(gold_response);		


	return rc; 
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	char *keyfile = NULL;
	char *host = NULL;
	uint32_t port = 0;
	struct	stat statbuf;

	if(!(cluster_name = slurm_get_cluster_name())) 
		fatal("To run acct_storage/gold you have to specify "
		      "ClusterName in your slurm.conf");

	if(!(keyfile = slurm_get_accounting_storage_pass()) 
	   || strlen(keyfile) < 1) {
		keyfile = xstrdup("/etc/gold/auth_key");
		debug2("No keyfile specified with AcctStoragePass, "
		       "gold using default %s", keyfile);
	}
	

	if(stat(keyfile, &statbuf)) {
		fatal("Can't stat key file %s. "
		      "To run acct_storage/gold you have to set "
		      "your gold keyfile as "
		      "AcctStoragePass in your slurm.conf", keyfile);
	}


	if(!(host = slurm_get_accounting_storage_host())) {
		host = xstrdup("localhost");
		debug2("No host specified with AcctStorageHost, "
		       "gold using default %s", host);
	}

	if(!(port = slurm_get_accounting_storage_port())) {
		port = 7112;
		debug2("No port specified with AcctStoragePort, "
		       "gold using default %u", port);
	}

	debug2("connecting from %s to gold with keyfile='%s' for %s(%d)",
	       cluster_name, keyfile, host, port);

	init_gold(cluster_name, keyfile, host, port);

//	if(!gold_acct_list) 
//		gold_acct_list = list_create(_destroy_gold_acct);
		
	xfree(keyfile);
	xfree(host);

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	xfree(cluster_name);
//	if(gold_acct_list) 
//		list_destroy(gold_acct_list);
	fini_gold();
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_users(List user_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	acct_user_rec_t *object = NULL;
	char tmp_buff[50];

	itr = list_iterator_create(user_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->default_acct) {
			error("We need a user name and "
			      "default acct to add.");
			rc = SLURM_ERROR;
			continue;
		}
		gold_request = create_gold_request(GOLD_OBJECT_USER,
						   GOLD_ACTION_CREATE);
		if(!gold_request) { 
			error("couldn't create gold_request");
			rc = SLURM_ERROR;
			break;
		}
		gold_request_add_assignment(gold_request, "Name",
					    object->name);		
		gold_request_add_assignment(gold_request, "DefaultProject",
					    object->default_acct);		

		if(object->expedite != ACCT_EXPEDITE_NOTSET) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->expedite-1);
			gold_request_add_assignment(gold_request, "Expedite",
						    tmp_buff);
		}		
		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);

		if(!gold_response) {
			error("acct_storage_p_add_users: "
			      "no response received");
			rc = SLURM_ERROR;
			break;
		}
		
		if(gold_response->rc) {
			error("gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
			      gold_response->message);
			destroy_gold_response(gold_response);
			rc = SLURM_ERROR;
			break;
		}
		destroy_gold_response(gold_response);		
	}
	list_iterator_destroy(itr);
	
	return rc;
}

extern int acct_storage_p_add_coord(char *acct,
				       acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_accts(List acct_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	acct_account_rec_t *object = NULL;
	char tmp_buff[50];

	itr = list_iterator_create(acct_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->description
		   || !object->organization) {
			error("We need a acct name, description, and "
			      "organization to add one.");
			rc = SLURM_ERROR;
			continue;
		}
		gold_request = create_gold_request(GOLD_OBJECT_PROJECT,
						   GOLD_ACTION_CREATE);
		if(!gold_request) { 
			error("couldn't create gold_request");
			rc = SLURM_ERROR;
			break;
		}
		gold_request_add_assignment(gold_request, "Name",
					    object->name);		
		gold_request_add_assignment(gold_request, "Description",
					    object->description);		
		gold_request_add_assignment(gold_request, "Organization",
					    object->organization);		
		if(object->expedite != ACCT_EXPEDITE_NOTSET) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->expedite-1);
			gold_request_add_assignment(gold_request, "Expedite",
						    tmp_buff);
		}		
		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);

		if(!gold_response) {
			error("acct_storage_p_add_accts: "
			      "no response received");
			rc = SLURM_ERROR;
			break;
		}
		
		if(gold_response->rc) {
			error("gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
			      gold_response->message);
			destroy_gold_response(gold_response);
			rc = SLURM_ERROR;
			break;
		}
		destroy_gold_response(gold_response);		
	}
	list_iterator_destroy(itr);
	
	return rc;
}

extern int acct_storage_p_add_clusters(List cluster_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	acct_cluster_rec_t *object = NULL;

	itr = list_iterator_create(cluster_list);
	while((object = list_next(itr))) {
		if(!object->name) {
			error("We need a cluster name to add.");
			rc = SLURM_ERROR;
			continue;
		}
		gold_request = create_gold_request(GOLD_OBJECT_MACHINE,
						   GOLD_ACTION_CREATE);
		if(!gold_request) { 
			error("couldn't create gold_request");
			rc = SLURM_ERROR;
			break;
		}
		gold_request_add_assignment(gold_request, "Name",
					    object->name);		

		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);

		if(!gold_response) {
			error("acct_storage_p_add_clusters: "
			      "no response received");
			rc = SLURM_ERROR;
			break;
		}
		
		if(gold_response->rc) {
			error("gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
			      gold_response->message);
			destroy_gold_response(gold_response);
			rc = SLURM_ERROR;
			break;
		}
		destroy_gold_response(gold_response);		
	}
	list_iterator_destroy(itr);
	
	return rc;
}

extern int acct_storage_p_add_associations(List association_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	acct_association_rec_t *object = NULL;
	char tmp_buff[50];

	itr = list_iterator_create(association_list);
	while((object = list_next(itr))) {
		if(!object->cluster || !object->acct) {
			error("We need a association cluster and "
			      "acct to add one.");
			rc = SLURM_ERROR;
			continue;
		}
		gold_request = create_gold_request(GOLD_OBJECT_ACCT,
						   GOLD_ACTION_CREATE);
		if(!gold_request) { 
			error("couldn't create gold_request");
			rc = SLURM_ERROR;
			break;
		}
		if(object->user) {
			gold_request_add_assignment(gold_request, "User",
						    object->user);		
			snprintf(tmp_buff, sizeof(tmp_buff), 
				 "%s on %s for %s",
				 object->acct,
				 object->cluster,
				 object->user);
		} else 
			snprintf(tmp_buff, sizeof(tmp_buff), 
				 "%s of %s on %s",
				 object->acct,
				 object->parent_acct,
				 object->cluster);
			
		gold_request_add_assignment(gold_request, "Name", tmp_buff);

		gold_request_add_assignment(gold_request, "Project",
					    object->acct);		
		gold_request_add_assignment(gold_request, "Machine",
					    object->cluster);	
			
		if(object->parent) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->parent);
			gold_request_add_assignment(gold_request, "Parent",
						    tmp_buff);		
		}

		if(object->fairshare) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->fairshare);
			gold_request_add_assignment(gold_request, "FairShare",
						    tmp_buff);		
		}

		if(object->max_jobs) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u", 
				 object->max_jobs);
			gold_request_add_assignment(gold_request, "MaxJobs",
						    tmp_buff);
		}
		
		if(object->max_nodes_per_job) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->max_nodes_per_job);
			gold_request_add_assignment(gold_request,
						    "MaxNodesPerJob",
						    tmp_buff);
		}

		if(object->max_wall_duration_per_job) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->max_wall_duration_per_job);
			gold_request_add_assignment(gold_request,
						    "MaxWallDurationPerJob",
						    tmp_buff);		
		}

		if(object->max_cpu_seconds_per_job) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->max_cpu_seconds_per_job);
			gold_request_add_assignment(gold_request,
						    "MaxProcSecondsPerJob",
						    tmp_buff);		
		}

		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);

		if(!gold_response) {
			error("acct_storage_p_add_associations: "
			      "no response received");
			rc = SLURM_ERROR;
			break;
		}
		
		if(gold_response->rc) {
			error("gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
			      gold_response->message);
			destroy_gold_response(gold_response);
			rc = SLURM_ERROR;
			break;
		}
		destroy_gold_response(gold_response);		
	}
	list_iterator_destroy(itr);
	
	return rc;
}

extern int acct_storage_p_modify_users(acct_user_cond_t *user_q,
					  acct_user_rec_t *user)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	char tmp_buff[50];
	int set = 0;

	if(!user_q) {
		error("acct_storage_p_modify_users: "
		      "we need conditions to modify");
		return SLURM_ERROR;
	}

	if(!user) {
		error("acct_storage_p_modify_users: "
		      "we need something to change");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_USER,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) { 
		error("acct_storage_p_modify_users: "
		      "couldn't create gold_request");
		return SLURM_ERROR;
	}

	if(user_q->user_list && list_count(user_q->user_list)) {
		itr = list_iterator_create(user_q->user_list);
		if(list_count(user_q->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		itr = list_iterator_create(user_q->def_acct_list);
		if(list_count(user_q->def_acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request,
						   "DefaultProject",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(user->default_acct) 
		gold_request_add_assignment(gold_request,
					    "DefaultProject",
					    user->default_acct);
	
	if(user->expedite != ACCT_EXPEDITE_NOTSET) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 user->expedite-1);
		gold_request_add_assignment(gold_request, "Expedite",
					    tmp_buff);		
	}

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_modify_users: "
		      "no response received");
		return SLURM_ERROR;
	}
	
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}

	destroy_gold_response(gold_response);		
	
	return rc;
}

extern int acct_storage_p_modify_user_admin_level(
	acct_user_cond_t *user_q)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!user_q || user_q->admin_level == ACCT_ADMIN_NOTSET) {
		error("acct_storage_p_modify_users: "
		      "we need conditions to modify");
		return SLURM_ERROR;
	}

	if(user_q->admin_level == ACCT_ADMIN_NONE) 
		gold_request = create_gold_request(GOLD_OBJECT_ROLEUSER,
						   GOLD_ACTION_DELETE);
	else 
		gold_request = create_gold_request(GOLD_OBJECT_ROLEUSER,
						   GOLD_ACTION_CREATE);
	
	if(!gold_request) { 
		error("couldn't create gold_request");
		return SLURM_ERROR;
	}

	if(user_q->admin_level == ACCT_ADMIN_NONE) {
		gold_request_add_condition(gold_request,
					   "Role",
					   "SystemAdmin",
					   GOLD_OPERATOR_NONE, 2);
		
		gold_request_add_condition(gold_request,
					   "Role",
					   "Operator",
					   GOLD_OPERATOR_NONE, 1);
	} else if(user_q->admin_level == ACCT_ADMIN_SUPER_USER)
		gold_request_add_assignment(gold_request,
					    "Role",
					    "SystemAdmin");
	else if(user_q->admin_level == ACCT_ADMIN_OPERATOR)
		gold_request_add_assignment(gold_request,
					    "Role",
					    "Operator");
	else {
		error("acct_storage_p_modify_user_admin_level: "
		      "unknown admin level %d", user_q->admin_level);
		return SLURM_ERROR;
	}

	if(user_q->user_list && list_count(user_q->user_list)) {
		itr = list_iterator_create(user_q->user_list);
		if(list_count(user_q->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		itr = list_iterator_create(user_q->def_acct_list);
		if(list_count(user_q->def_acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request,
						   "DefaultProject",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_modify_users: "
		      "no response received");
		return SLURM_ERROR;
	}
	
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}
	destroy_gold_response(gold_response);	
	
	return rc;
}

extern int acct_storage_p_modify_accts(acct_account_cond_t *acct_q,
					     acct_account_rec_t *acct)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int set = 0;
	char *object = NULL;

	if(!acct_q) {
		error("acct_storage_p_modify_accts: "
		      "we need conditions to modify");
		return SLURM_ERROR;
	}

	if(!acct) {
		error("acct_storage_p_modify_accts: "
		      "we need something to change");
		return SLURM_ERROR;
	}
	
	gold_request = create_gold_request(GOLD_OBJECT_ACCT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) { 
		error("couldn't create gold_request");
		return SLURM_ERROR;
	}

	if(acct_q->acct_list && list_count(acct_q->acct_list)) {
		itr = list_iterator_create(acct_q->acct_list);
		if(list_count(acct_q->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->description_list 
	   && list_count(acct_q->description_list)) {
		itr = list_iterator_create(acct_q->description_list);
		if(list_count(acct_q->description_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Description",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->organization_list 
	   && list_count(acct_q->organization_list)) {
		itr = list_iterator_create(acct_q->organization_list);
		if(list_count(acct_q->organization_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Organization",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct->description) 
		gold_request_add_assignment(gold_request,
					    "Description",
					    acct->description);
	if(acct->organization) 
		gold_request_add_assignment(gold_request,
					    "Organization",
					    acct->organization);
	
	if(acct->expedite != ACCT_EXPEDITE_NOTSET) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 acct->expedite-1);
		gold_request_add_assignment(gold_request, "Expedite",
					    tmp_buff);		
	}
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_modify_accts: "
		      "no response received");
		return SLURM_ERROR;
	}
	
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}

	destroy_gold_response(gold_response);		
	
	return rc;
}

extern int acct_storage_p_modify_clusters(acct_cluster_cond_t *cluster_q,
					     acct_cluster_rec_t *cluster)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_associations(
	acct_association_cond_t *assoc_q, acct_association_rec_t *assoc)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	char *object = NULL;
	int set = 0;

	if(!assoc_q) {
		error("acct_storage_p_modify_associations: "
		      "we need conditions to modify");
		return SLURM_ERROR;
	}

	if(!assoc) {
		error("acct_storage_p_modify_associations: "
		      "we need something to change");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_ACCT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) { 
		error("couldn't create gold_request");
		return SLURM_ERROR;
	}

	if(assoc_q->id_list && list_count(assoc_q->id_list)) {
		itr = list_iterator_create(assoc_q->id_list);
		if(list_count(assoc_q->id_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Id",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->user_list && list_count(assoc_q->user_list)) {
		itr = list_iterator_create(assoc_q->user_list);
		if(list_count(assoc_q->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "User",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->acct_list && list_count(assoc_q->acct_list)) {
		itr = list_iterator_create(assoc_q->acct_list);
		if(list_count(assoc_q->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Project",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->cluster_list && list_count(assoc_q->cluster_list)) {
		itr = list_iterator_create(assoc_q->cluster_list);
		if(list_count(assoc_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Machine",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->parent) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc_q->parent);
		gold_request_add_condition(gold_request, "Parent",
					   object,
					   GOLD_OPERATOR_NONE, 0);
	}

	if(assoc_q->lft && assoc_q->rgt) {
		error("lft && rgt don't work with gold.");
	}
		
	if(assoc->fairshare) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc->fairshare);
		gold_request_add_assignment(gold_request, "Fairshare",
					    tmp_buff);		
	}

	if(assoc->max_jobs) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u", 
			 assoc->max_jobs);
		gold_request_add_assignment(gold_request, "MaxJobs",
					    tmp_buff);
	}
		
	if(assoc->max_nodes_per_job) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc->max_nodes_per_job);
		gold_request_add_assignment(gold_request,
					    "MaxNodesPerJob",
					    tmp_buff);
	}

	if(assoc->max_wall_duration_per_job) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc->max_wall_duration_per_job);
		gold_request_add_assignment(gold_request,
					    "MaxWallDurationPerJob",
					    tmp_buff);		
	}

	if(assoc->max_cpu_seconds_per_job) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc->max_cpu_seconds_per_job);
		gold_request_add_assignment(gold_request,
					    "MaxProcSecondsPerJob",
					    tmp_buff);		
	}

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_modify_associations: "
		      "no response received");
		return SLURM_ERROR;
	}
		
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}
	destroy_gold_response(gold_response);		
	
	return rc;
}

extern int acct_storage_p_remove_users(acct_user_cond_t *user_q)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!user_q) {
		error("acct_storage_p_remove_users: "
		      "we need conditions to remove");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_USER,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("acct_storage_p_remove_users: "
		      "couldn't create gold_request");
		return SLURM_ERROR;
	}
	
	if(user_q->user_list && list_count(user_q->user_list)) {
		itr = list_iterator_create(user_q->user_list);
		if(list_count(user_q->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		itr = list_iterator_create(user_q->def_acct_list);
		if(list_count(user_q->def_acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request,
						   "DefaultProject",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}
	
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_remove_users: "
		      "no response received");
		return SLURM_ERROR;
	}
		
	if(gold_response->rc) {
		error("acct_storage_p_remove_users: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}
	destroy_gold_response(gold_response);		
		
	return rc;
}

extern int acct_storage_p_remove_coord(char *acct,
					  acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_remove_accts(acct_account_cond_t *acct_q)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!acct_q) {
		error("acct_storage_p_remove_accts: "
		      "we need conditions to remove");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_PROJECT,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("acct_storage_p_remove_accts: "
		      "couldn't create gold_request");
		return SLURM_ERROR;
	}
	
	if(acct_q->acct_list && list_count(acct_q->acct_list)) {
		itr = list_iterator_create(acct_q->acct_list);
		if(list_count(acct_q->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->description_list 
	   && list_count(acct_q->description_list)) {
		itr = list_iterator_create(acct_q->description_list);
		if(list_count(acct_q->description_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Description",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->organization_list 
	   && list_count(acct_q->organization_list)) {
		itr = list_iterator_create(acct_q->organization_list);
		if(list_count(acct_q->organization_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Organization",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_remove_accts: "
		      "no response received");
		return SLURM_ERROR;
	}
	
	if(gold_response->rc) {
		error("acct_storage_p_remove_accts: "
		      "gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}
	destroy_gold_response(gold_response);		
		
	return rc;
}

extern int acct_storage_p_remove_clusters(acct_cluster_cond_t *cluster_q)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!cluster_q) {
		error("acct_storage_p_modify_clusters: "
		      "we need conditions to modify");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_MACHINE,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("acct_storage_p_remove_clusters: "
		      "couldn't create gold_request");
		return SLURM_ERROR;
	}
	
	if(cluster_q->cluster_list && list_count(cluster_q->cluster_list)) {
		itr = list_iterator_create(cluster_q->cluster_list);
		if(list_count(cluster_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}


	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
		
	if(!gold_response) {
		error("acct_storage_p_remove_clusters: "
		      "no response received");
		return SLURM_ERROR;
	}
	
	if(gold_response->rc) {
		error("acct_storage_p_remove_clusters: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		destroy_gold_response(gold_response);
		return SLURM_ERROR;
	}
	destroy_gold_response(gold_response);

	gold_request = create_gold_request(GOLD_OBJECT_MACHINE_HOUR_USAGE,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("acct_storage_p_remove_clusters: "
		      "couldn't create gold_request");
		return SLURM_ERROR;
	}
	
	if(cluster_q->cluster_list && list_count(cluster_q->cluster_list)) {
		itr = list_iterator_create(cluster_q->cluster_list);
		if(list_count(cluster_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Machine",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_remove_clusters: "
		      "no response received");
		destroy_gold_request(gold_request);
		return SLURM_ERROR;
	}
		
	if(gold_response->rc) {
		error("acct_storage_p_remove_clusters: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		destroy_gold_request(gold_request);
		destroy_gold_response(gold_response);
		return SLURM_ERROR;
	}
	destroy_gold_response(gold_response);

	gold_request->object = GOLD_OBJECT_MACHINE_DAY_USAGE;
	gold_response = get_gold_response(gold_request);	
	if(!gold_response) {
		error("acct_storage_p_remove_clusters: "
		      "no response received");
		destroy_gold_request(gold_request);
		return SLURM_ERROR;
	}
		
	if(gold_response->rc) {
		error("acct_storage_p_remove_clusters: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		destroy_gold_request(gold_request);
		destroy_gold_response(gold_response);
		return SLURM_ERROR;
	}
	
	destroy_gold_response(gold_response);

	gold_request->object = GOLD_OBJECT_MACHINE_MONTH_USAGE;
	gold_response = get_gold_response(gold_request);	
	if(!gold_response) {
		error("acct_storage_p_remove_clusters: "
		      "no response received");
		destroy_gold_request(gold_request);
		return SLURM_ERROR;
	}
		
	if(gold_response->rc) {
		error("acct_storage_p_remove_clusters: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}
	
	destroy_gold_request(gold_request);
	destroy_gold_response(gold_response);
	
	return rc;
}

extern int acct_storage_p_remove_associations(
	acct_association_cond_t *assoc_q)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	char *object = NULL;
	int set = 0;

	if(!assoc_q) {
		error("acct_storage_p_remove_associations: "
		      "we need conditions to remove");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_ACCT,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("couldn't create gold_request");
		return SLURM_ERROR;
	}

	if(assoc_q->id_list && list_count(assoc_q->id_list)) {
		itr = list_iterator_create(assoc_q->id_list);
		if(list_count(assoc_q->id_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Id",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->user_list && list_count(assoc_q->user_list)) {
		itr = list_iterator_create(assoc_q->user_list);
		if(list_count(assoc_q->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "User",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->acct_list && list_count(assoc_q->acct_list)) {
		itr = list_iterator_create(assoc_q->acct_list);
		if(list_count(assoc_q->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Project",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->cluster_list && list_count(assoc_q->cluster_list)) {
		itr = list_iterator_create(assoc_q->cluster_list);
		if(list_count(assoc_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Machine",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->parent) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc_q->parent);
		gold_request_add_condition(gold_request, "Parent",
					   object,
					   GOLD_OPERATOR_NONE, 0);
	}

	if(assoc_q->lft && assoc_q->rgt) {
		error("lft && rgt don't work with gold.");
	}
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_modify_associations: "
		      "no response received");
		return SLURM_ERROR;
	}
		
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}

	if(gold_response->entry_cnt > 0) {
		ListIterator itr = NULL;
		ListIterator itr2 = NULL;
		gold_response_entry_t *resp_entry = NULL;
		gold_name_value_t *name_val = NULL;
		List id_list = list_create(_destroy_char);

		itr = list_iterator_create(gold_response->entries);
		while((resp_entry = list_next(itr))) {
			itr2 = list_iterator_create(
				resp_entry->name_val);
			while((name_val = list_next(itr2))) {
				if(!strcmp(name_val->name, "Id")) {
					list_push(id_list, name_val->value);
					break;
				}
			}
			list_iterator_destroy(itr2);			
		}
		list_iterator_destroy(itr);
		_remove_association_accounting(id_list);
		list_destroy(id_list);
	} else {
		debug3("no associations found");
	}
	destroy_gold_response(gold_response);		

	return rc;
}

extern List acct_storage_p_get_users(acct_user_cond_t *user_q)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	List user_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	char tmp_buff[50];

	gold_request = create_gold_request(GOLD_OBJECT_USER,
					   GOLD_ACTION_QUERY);

	if(!gold_request) 
		return NULL;

	if(!user_q) 
		goto empty;

	if(user_q->user_list && list_count(user_q->user_list)) {
		itr = list_iterator_create(user_q->user_list);
		if(list_count(user_q->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		itr = list_iterator_create(user_q->def_acct_list);
		if(list_count(user_q->def_acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request,
						   "DefaultProject",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}
	
	if(user_q->expedite != ACCT_EXPEDITE_NOTSET) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 user_q->expedite-1);
		gold_request_add_condition(gold_request, "Expedite",
					   tmp_buff,
					   GOLD_OPERATOR_NONE, 0);		
	}

empty:
	gold_request_add_condition(gold_request, "Active",
				   "True",
				   GOLD_OPERATOR_NONE,
				   0);

	gold_request_add_condition(gold_request, "Special",
				   "False",
				   GOLD_OPERATOR_NONE,
				   0);

	gold_request_add_selection(gold_request, "Name");
	gold_request_add_selection(gold_request, "DefaultProject");
	gold_request_add_selection(gold_request, "Expedite");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_users: no response received");
		return NULL;
	}

	user_list = _get_user_list_from_response(gold_response);
	
	destroy_gold_response(gold_response);

	return user_list;
}

extern List acct_storage_p_get_accts(acct_account_cond_t *acct_q)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	List acct_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *object = NULL;
	char tmp_buff[50];


	gold_request = create_gold_request(GOLD_OBJECT_PROJECT,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return NULL;

	if(!acct_q) 
		goto empty;

	if(acct_q->acct_list && list_count(acct_q->acct_list)) {
		itr = list_iterator_create(acct_q->acct_list);
		if(list_count(acct_q->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->description_list 
	   && list_count(acct_q->description_list)) {
		itr = list_iterator_create(acct_q->description_list);
		if(list_count(acct_q->description_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Description",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->organization_list 
	   && list_count(acct_q->organization_list)) {
		itr = list_iterator_create(acct_q->organization_list);
		if(list_count(acct_q->organization_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Organization",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->expedite != ACCT_EXPEDITE_NOTSET) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 acct_q->expedite-1);
		gold_request_add_condition(gold_request, "Expedite",
					   tmp_buff,
					   GOLD_OPERATOR_NONE, 0);		
	}
empty:
	gold_request_add_condition(gold_request, "Active",
				   "True",
				   GOLD_OPERATOR_NONE,
				   0);

	gold_request_add_condition(gold_request, "Special",
				   "False",
				   GOLD_OPERATOR_NONE,
				   0);

	gold_request_add_selection(gold_request, "Name");
	gold_request_add_selection(gold_request, "Organization");
	gold_request_add_selection(gold_request, "Description");
	gold_request_add_selection(gold_request, "Expedite");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_accts: no response received");
		return NULL;
	}

	acct_list = _get_acct_list_from_response(gold_response);
	
	destroy_gold_response(gold_response);

	return acct_list;
}

extern List acct_storage_p_get_clusters(acct_cluster_cond_t *cluster_q)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	List cluster_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *object = NULL;


	gold_request = create_gold_request(GOLD_OBJECT_MACHINE,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return NULL;

	if(!cluster_q) 
		goto empty;

	if(cluster_q->cluster_list && list_count(cluster_q->cluster_list)) {
		itr = list_iterator_create(cluster_q->cluster_list);
		if(list_count(cluster_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

empty:
	gold_request_add_condition(gold_request, "Active",
				   "True",
				   GOLD_OPERATOR_NONE,
				   0);

	gold_request_add_condition(gold_request, "Special",
				   "False",
				   GOLD_OPERATOR_NONE,
				   0);
	
	gold_request_add_selection(gold_request, "Name");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_clusters: no response received");
		return NULL;
	}

	cluster_list = _get_cluster_list_from_response(gold_response);
	
	destroy_gold_response(gold_response);

	return cluster_list;
}

extern List acct_storage_p_get_associations(
	acct_association_cond_t *assoc_q)
{

	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	List association_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *object = NULL;
	char tmp_buff[50];

	gold_request = create_gold_request(GOLD_OBJECT_ACCT,
					   GOLD_ACTION_QUERY);
	
	if(!gold_request) 
		return NULL;

	if(!assoc_q) 
		goto empty;
	
	if(assoc_q->id_list && list_count(assoc_q->id_list)) {
		itr = list_iterator_create(assoc_q->id_list);
		if(list_count(assoc_q->id_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Id",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->user_list && list_count(assoc_q->user_list)) {
		itr = list_iterator_create(assoc_q->user_list);
		if(list_count(assoc_q->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "User",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->acct_list && list_count(assoc_q->acct_list)) {
		itr = list_iterator_create(assoc_q->acct_list);
		if(list_count(assoc_q->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Project",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->cluster_list && list_count(assoc_q->cluster_list)) {
		itr = list_iterator_create(assoc_q->cluster_list);
		if(list_count(assoc_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Machine",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->parent) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc_q->parent);
		gold_request_add_condition(gold_request, "Parent",
					   object,
					   GOLD_OPERATOR_NONE, 0);
	}

	if(assoc_q->lft && assoc_q->rgt) {
		error("lft && rgt don't work with gold.");
	}

empty:
	gold_request_add_selection(gold_request, "Id");
	gold_request_add_selection(gold_request, "User");
	gold_request_add_selection(gold_request, "Project");
	gold_request_add_selection(gold_request, "Machine");
	gold_request_add_selection(gold_request, "Parent");
	gold_request_add_selection(gold_request, "FairShare");
	gold_request_add_selection(gold_request, "MaxJobs");
	gold_request_add_selection(gold_request, "MaxNodesPerJob");
	gold_request_add_selection(gold_request, "MaxWallDurationPerJob");
	gold_request_add_selection(gold_request, "MaxProcSecondsPerJob");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_associations: "
		      "no response received");
		return NULL;
	}

	association_list = _get_association_list_from_response(gold_response);

	destroy_gold_response(gold_response);

	return association_list;
}

extern int acct_storage_p_get_hourly_usage(
	acct_association_rec_t *acct_assoc,
	time_t start, time_t end)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_ERROR;
	char tmp_buff[50];

	if(!acct_assoc || acct_assoc->id) {
		error("acct_storage_p_get_hourly_usage: "
		      "We need an id to go off to query off of");
		return rc;
	}

	gold_request = create_gold_request(
		GOLD_OBJECT_ACCT_HOUR_USAGE, GOLD_ACTION_QUERY);

	if(!gold_request) 
		return rc;

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_assoc->id);
	gold_request_add_condition(gold_request, "Acct", tmp_buff,
				   GOLD_OPERATOR_NONE, 0);

	if(start) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)start);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_GE, 0);
	}
	if(end) {	
		snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)end);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_L, 0);
	}

	gold_request_add_selection(gold_request, "PeriodStart");
	gold_request_add_selection(gold_request, "AllocatedCPUSecs");

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_hourly_usage: "
		      "no response received");
		return rc;
	}

	rc = _get_acct_accounting_list_from_response(
		gold_response, acct_assoc);

	destroy_gold_response(gold_response);

	return rc;
}

extern int acct_storage_p_get_daily_usage(
	acct_association_rec_t *acct_assoc,
	time_t start, time_t end)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_ERROR;
	char tmp_buff[50];

	if(!acct_assoc || acct_assoc->id) {
		error("acct_storage_p_get_daily_usage: "
		      "We need an id to go off to query off of");
		return rc;
	}

	gold_request = create_gold_request(
		GOLD_OBJECT_ACCT_DAY_USAGE, GOLD_ACTION_QUERY);

	if(!gold_request) 
		return rc;

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_assoc->id);
	gold_request_add_condition(gold_request, "Acct", tmp_buff,
				   GOLD_OPERATOR_NONE, 0);

	if(start) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)start);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_GE, 0);
	}
	if(end) {	
		snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)end);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_L, 0);
	}

	gold_request_add_selection(gold_request, "PeriodStart");
	gold_request_add_selection(gold_request, "AllocatedCPUSecs");

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_daily_usage: "
		      "no response received");
		return rc;
	}

	rc = _get_acct_accounting_list_from_response(
		gold_response, acct_assoc);

	destroy_gold_response(gold_response);

	return rc;
}

extern int acct_storage_p_get_monthly_usage(
	acct_association_rec_t *acct_assoc,
	time_t start, time_t end)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_ERROR;
	char tmp_buff[50];

	if(!acct_assoc || acct_assoc->id) {
		error("acct_storage_p_get_monthly_usage: "
		      "We need an id to go off to query off of");
		return rc;
	}

	gold_request = create_gold_request(
		GOLD_OBJECT_ACCT_MONTH_USAGE, GOLD_ACTION_QUERY);

	if(!gold_request) 
		return rc;

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_assoc->id);
	gold_request_add_condition(gold_request, "Acct", tmp_buff,
				   GOLD_OPERATOR_NONE, 0);

	if(start) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)start);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_GE, 0);
	}
	if(end) {	
		snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)end);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_L, 0);
	}

	gold_request_add_selection(gold_request, "PeriodStart");
	gold_request_add_selection(gold_request, "AllocatedCPUSecs");

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_monthly_usage: "
		      "no response received");
		return rc;
	}

	rc = _get_acct_accounting_list_from_response(
		gold_response, acct_assoc);

	destroy_gold_response(gold_response);

	return rc;
}

extern int clusteracct_storage_p_node_down(struct node_record *node_ptr,
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
	info("cluster_acct_down: %s at %s with %u cpus due to %s", 
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
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "Name", node_ptr->name,
				   GOLD_OPERATOR_NONE, 0);

	snprintf(tmp_buff, sizeof(tmp_buff), "%d", ((int)event_time - 1));
	gold_request_add_assignment(gold_request, "EndTime", tmp_buff);		
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_storage_p_node_down: no response received");
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
		error("clusteracct_p_node_down: no response received");
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

extern int clusteracct_storage_p_node_up(struct node_record *node_ptr,
					 time_t event_time)
{
	int rc = SLURM_ERROR;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];

#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("cluster_acct_up: %s at %s", node_ptr->name, tmp_buff);
#endif

	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) 
		return rc;
	
	gold_request_add_condition(gold_request, "Machine", cluster_name,
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "Name", node_ptr->name,
				   GOLD_OPERATOR_NONE, 0);

	snprintf(tmp_buff, sizeof(tmp_buff), "%d", ((int)event_time - 1));
	gold_request_add_assignment(gold_request, "EndTime", tmp_buff);		
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_node_up: no response received");
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

extern int clusteracct_storage_p_cluster_procs(uint32_t procs,
					       time_t event_time)
{
	static uint32_t last_procs = -1;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int rc = SLURM_ERROR;
	bool no_modify = 0;

	if (procs == last_procs) {
		debug3("we have the same procs as before no need to "
		       "query the database.");
		return SLURM_SUCCESS;
	}
	last_procs = procs;

	/* Record the processor count */
#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("cluster_acct_procs: %s has %u total CPUs at %s", 
	     cluster_name, procs, tmp_buff);
#endif
	
	/* get the last known one */
	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return rc;
	gold_request_add_condition(gold_request, "Machine", cluster_name,
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "Name", "NULL",
				   GOLD_OPERATOR_NONE, 0);

	gold_request_add_selection(gold_request, "CPUCount");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_cluster_procs: no response received");
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
		no_modify = 1;
	}

	destroy_gold_response(gold_response);
	
	if(no_modify) {
		gold_request = create_gold_request(GOLD_OBJECT_EVENT,
						   GOLD_ACTION_MODIFY);
		if(!gold_request) 
			return rc;
		
		gold_request_add_condition(gold_request, "Machine",
					   cluster_name,
					   GOLD_OPERATOR_NONE, 0);
		gold_request_add_condition(gold_request, "EndTime", "0",
					   GOLD_OPERATOR_NONE, 0);
		gold_request_add_condition(gold_request, "Name", "NULL",
					   GOLD_OPERATOR_NONE, 0);
		
		snprintf(tmp_buff, sizeof(tmp_buff), "%d", 
			 ((int)event_time - 1));
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
	}

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
		error("clusteracct_p_cluster_procs: no response received");
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

extern int clusteracct_storage_p_get_hourly_usage(
	acct_cluster_rec_t *cluster_rec, time_t start, 
	time_t end, void *params)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int rc = SLURM_ERROR;

	if(!cluster_rec || !cluster_rec->name) {
		error("clusteracct_storage_p_get_hourly_usage:"
		      "no cluster name given to query.");
		return rc;
	}
	/* get the last known one */
	gold_request = create_gold_request(GOLD_OBJECT_MACHINE_HOUR_USAGE,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return rc;

	gold_request_add_condition(gold_request, "Machine", cluster_rec->name,
				   GOLD_OPERATOR_NONE, 0);
	if(start) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)start);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_GE, 0);
	}
	if(end) {	
		snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)end);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_L, 0);
	}

	gold_request_add_selection(gold_request, "CPUCount");
	gold_request_add_selection(gold_request, "PeriodStart");
	gold_request_add_selection(gold_request, "IdleCPUSeconds");
	gold_request_add_selection(gold_request, "DownCPUSeconds");
	gold_request_add_selection(gold_request, "AllocatedCPUSeconds");
	gold_request_add_selection(gold_request, "ReservedCPUSeconds");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_get_hourly_usage: no response received");
		return rc;
	}

	if(gold_response->entry_cnt > 0) {
		rc = _get_cluster_accounting_list_from_response(
			gold_response, cluster_rec);
	} else {
		debug("We don't have an entry for this machine for this time");
	}
	destroy_gold_response(gold_response);

	return rc;
}

extern int clusteracct_storage_p_get_daily_usage(
	acct_cluster_rec_t *cluster_rec, time_t start, 
	time_t end, void *params)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int rc = SLURM_ERROR;

	if(!cluster_rec || !cluster_rec->name) {
		error("clusteracct_storage_p_get_daily_usage:"
		      "no cluster name given to query.");
		return rc;
	}
	/* get the last known one */
	gold_request = create_gold_request(GOLD_OBJECT_MACHINE_DAY_USAGE,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return rc;

	gold_request_add_condition(gold_request, "Machine", cluster_rec->name,
				   GOLD_OPERATOR_NONE, 0);
	if(start) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)start);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_GE, 0);
	}
	if(end) {	
		snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)end);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_L, 0);
	}

	gold_request_add_selection(gold_request, "CPUCount");
	gold_request_add_selection(gold_request, "PeriodStart");
	gold_request_add_selection(gold_request, "IdleCPUSeconds");
	gold_request_add_selection(gold_request, "DownCPUSeconds");
	gold_request_add_selection(gold_request, "AllocatedCPUSeconds");
	gold_request_add_selection(gold_request, "ReservedCPUSeconds");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_get_daily_usage: no response received");
		return rc;
	}

	if(gold_response->entry_cnt > 0) {
		rc = _get_cluster_accounting_list_from_response(
			gold_response, cluster_rec);
	} else {
		debug("We don't have an entry for this machine for this time");
	}
	destroy_gold_response(gold_response);

	return rc;
}

extern int clusteracct_storage_p_get_monthly_usage(
	acct_cluster_rec_t *cluster_rec, time_t start, 
	time_t end, void *params)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int rc = SLURM_ERROR;

	if(!cluster_rec || !cluster_rec->name) {
		error("clusteracct_storage_p_get_monthly_usage:"
		      "no cluster name given to query.");
		return rc;
	}
	/* get the last known one */
	gold_request = create_gold_request(GOLD_OBJECT_MACHINE_MONTH_USAGE,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return rc;

	gold_request_add_condition(gold_request, "Machine", cluster_rec->name,
				   GOLD_OPERATOR_NONE, 0);
	if(start) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)start);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_GE, 0);
	}
	if(end) {	
		snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)end);
		gold_request_add_condition(gold_request, "PeriodStart",
					   tmp_buff,
					   GOLD_OPERATOR_L, 0);
	}

	gold_request_add_selection(gold_request, "CPUCount");
	gold_request_add_selection(gold_request, "PeriodStart");
	gold_request_add_selection(gold_request, "IdleCPUSeconds");
	gold_request_add_selection(gold_request, "DownCPUSeconds");
	gold_request_add_selection(gold_request, "AllocatedCPUSeconds");
	gold_request_add_selection(gold_request, "ReservedCPUSeconds");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_storage_p_get_monthly_usage: "
		      "no response received");
		return rc;
	}

	if(gold_response->entry_cnt > 0) {
		rc = _get_cluster_accounting_list_from_response(
			gold_response, cluster_rec);
	} else {
		debug("We don't have an entry for this machine for this time");
	}
	destroy_gold_response(gold_response);

	return rc;
}
