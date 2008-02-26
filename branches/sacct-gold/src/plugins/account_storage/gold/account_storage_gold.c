/*****************************************************************************\
 *  account_storage_gold.c - account interface to gold.
 *
 *  $Id: account_gold.c 13061 2008-01-22 21:23:56Z da $
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
#include "src/common/slurm_account_storage.h"

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
const char plugin_name[] = "Account storage GOLD plugin";
const char plugin_type[] = "account_storage/gold";
const uint32_t plugin_version = 100;

static char *cluster_name = NULL;


static void _destroy_char(void *object);
static List _get_association_list_from_response(gold_response_t *gold_response);
static int _get_account_accounting_list_from_response(
	gold_response_t *gold_response,
	account_association_rec_t *account_rec);
static List _get_user_list_from_response(gold_response_t *gold_response);
static List _get_account_list_from_response(gold_response_t *gold_response);
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
	account_association_rec_t *account_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	association_list = list_create(destroy_account_association_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		account_rec = xmalloc(sizeof(account_association_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, "Id")) {
				account_rec->id = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Parent")) {
				account_rec->parent = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "FairShare")) {
				account_rec->fairshare = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxJobs")) {
				account_rec->max_jobs = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxNodesPerJob")) {
				account_rec->max_nodes_per_job = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxWallDurationPerJob")) {
				account_rec->max_wall_duration_per_job = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxProcSecondsPerJob")) {
				account_rec->max_cpu_seconds_per_job = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "User")) {
				account_rec->user = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Project")) {
				account_rec->account = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Machine")) {
				account_rec->cluster = 
					xstrdup(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(association_list, account_rec);
	}
	list_iterator_destroy(itr);

	return association_list;
}

static int _get_account_accounting_list_from_response(
	gold_response_t *gold_response,
	account_association_rec_t *account_rec)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	account_accounting_rec_t *accounting_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	if(!account_rec->accounting_list)
		account_rec->accounting_list =
			list_create(destroy_account_accounting_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		accounting_rec = xmalloc(sizeof(account_accounting_rec_t));

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
		list_append(account_rec->accounting_list, accounting_rec);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
	
}

static List _get_user_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List user_list = NULL;
	account_user_rec_t *user_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	user_list = list_create(destroy_account_user_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		user_rec = xmalloc(sizeof(account_user_rec_t));

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
				user_rec->default_account = 
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

static List _get_account_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List account_list = NULL;
	account_account_rec_t *account_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	account_list = list_create(destroy_account_account_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		account_rec = xmalloc(sizeof(account_account_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, "Expedite")) {
				account_rec->expedite = 
					atoi(name_val->value)+1;
			} else if(!strcmp(name_val->name, 
					  "Name")) {
				account_rec->name = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Organization")) {
				account_rec->organization = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Description")) {
				account_rec->description = 
					xstrdup(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(account_list, account_rec);
	}
	list_iterator_destroy(itr);

	return account_list;
}

static List _get_cluster_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List cluster_list = NULL;
	account_cluster_rec_t *cluster_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	cluster_list = list_create(destroy_account_cluster_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		cluster_rec = xmalloc(sizeof(account_cluster_rec_t));

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

	gold_request = create_gold_request(GOLD_OBJECT_ACCOUNT_HOUR_USAGE,
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
			gold_request_add_condition(gold_request, "Account",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}
			       
	gold_response = get_gold_response(gold_request);	

	if(!gold_response) {
		error("account_storage_p_modify_associations: "
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

	gold_request->object = GOLD_OBJECT_ACCOUNT_DAY_USAGE;	
	gold_response = get_gold_response(gold_request);	
	
	if(!gold_response) {
		error("account_storage_p_modify_associations: "
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
	
	gold_request->object = GOLD_OBJECT_ACCOUNT_MONTH_USAGE;	
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
		
	if(!gold_response) {
		error("account_storage_p_modify_associations: "
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
		fatal("To run account_storage/gold you have to specify "
		      "ClusterName in your slurm.conf");

	if(!(keyfile = slurm_get_account_storage_pass()) 
	   || strlen(keyfile) < 1) {
		keyfile = xstrdup("/etc/gold/auth_key");
		debug2("No keyfile specified with AccountStoragePass, "
		       "gold using default %s", keyfile);
	}
	

	if(stat(keyfile, &statbuf)) {
		fatal("Can't stat key file %s. "
		      "To run account_storage/gold you have to set "
		      "your gold keyfile as "
		      "AccountStoragePass in your slurm.conf", keyfile);
	}


	if(!(host = slurm_get_account_storage_host())) {
		host = xstrdup("localhost");
		debug2("No host specified with AccountStorageHost, "
		       "gold using default %s", host);
	}

	if(!(port = slurm_get_account_storage_port())) {
		port = 7112;
		debug2("No port specified with AccountStoragePort, "
		       "gold using default %u", port);
	}

	debug2("connecting from %s to gold with keyfile='%s' for %s(%d)",
	       cluster_name, keyfile, host, port);

	init_gold(cluster_name, keyfile, host, port);

//	if(!gold_account_list) 
//		gold_account_list = list_create(_destroy_gold_account);
		
	xfree(keyfile);
	xfree(host);

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	xfree(cluster_name);
//	if(gold_account_list) 
//		list_destroy(gold_account_list);
	fini_gold();
	return SLURM_SUCCESS;
}

extern int account_storage_p_add_users(List user_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	account_user_rec_t *object = NULL;
	char tmp_buff[50];

	itr = list_iterator_create(user_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->default_account) {
			error("We need a user name and "
			      "default account to add.");
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
					    object->default_account);		

		if(object->expedite != ACCOUNT_EXPEDITE_NOTSET) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->expedite-1);
			gold_request_add_assignment(gold_request, "Expedite",
						    tmp_buff);
		}		
		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);

		if(!gold_response) {
			error("account_storage_p_add_users: "
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

extern int account_storage_p_add_coord(char *account,
				       account_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_add_accounts(List account_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	account_account_rec_t *object = NULL;
	char tmp_buff[50];

	itr = list_iterator_create(account_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->description
		   || !object->organization) {
			error("We need a account name, description, and "
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
		if(object->expedite != ACCOUNT_EXPEDITE_NOTSET) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->expedite-1);
			gold_request_add_assignment(gold_request, "Expedite",
						    tmp_buff);
		}		
		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);

		if(!gold_response) {
			error("account_storage_p_add_accounts: "
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

extern int account_storage_p_add_clusters(List cluster_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	account_cluster_rec_t *object = NULL;

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
			error("account_storage_p_add_clusters: "
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

extern int account_storage_p_add_associations(List association_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	account_association_rec_t *object = NULL;
	char tmp_buff[50];

	itr = list_iterator_create(association_list);
	while((object = list_next(itr))) {
		if(!object->cluster || !object->account) {
			error("We need a association cluster and "
			      "account to add one.");
			rc = SLURM_ERROR;
			continue;
		}
		gold_request = create_gold_request(GOLD_OBJECT_ACCOUNT,
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
				 object->account,
				 object->cluster,
				 object->user);
		} else 
			snprintf(tmp_buff, sizeof(tmp_buff), 
				 "%s of %s on %s",
				 object->account,
				 object->parent_account,
				 object->cluster);
			
		gold_request_add_assignment(gold_request, "Name", tmp_buff);

		gold_request_add_assignment(gold_request, "Project",
					    object->account);		
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
			error("account_storage_p_add_associations: "
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

extern int account_storage_p_modify_users(account_user_cond_t *user_q,
					  account_user_rec_t *user)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	char tmp_buff[50];
	int set = 0;

	if(!user_q) {
		error("account_storage_p_modify_users: "
		      "we need conditions to modify");
		return SLURM_ERROR;
	}

	if(!user) {
		error("account_storage_p_modify_users: "
		      "we need something to change");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_USER,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) { 
		error("account_storage_p_modify_users: "
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

	if(user_q->def_account_list && list_count(user_q->def_account_list)) {
		itr = list_iterator_create(user_q->def_account_list);
		if(list_count(user_q->def_account_list) > 1)
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

	if(user->default_account) 
		gold_request_add_assignment(gold_request,
					    "DefaultProject",
					    user->default_account);
	
	if(user->expedite != ACCOUNT_EXPEDITE_NOTSET) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 user->expedite-1);
		gold_request_add_assignment(gold_request, "Expedite",
					    tmp_buff);		
	}

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("account_storage_p_modify_users: "
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

extern int account_storage_p_modify_user_admin_level(
	account_user_cond_t *user_q)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!user_q || user_q->admin_level == ACCOUNT_ADMIN_NOTSET) {
		error("account_storage_p_modify_users: "
		      "we need conditions to modify");
		return SLURM_ERROR;
	}

	if(user_q->admin_level == ACCOUNT_ADMIN_NONE) 
		gold_request = create_gold_request(GOLD_OBJECT_ROLEUSER,
						   GOLD_ACTION_DELETE);
	else 
		gold_request = create_gold_request(GOLD_OBJECT_ROLEUSER,
						   GOLD_ACTION_CREATE);
	
	if(!gold_request) { 
		error("couldn't create gold_request");
		return SLURM_ERROR;
	}

	if(user_q->admin_level == ACCOUNT_ADMIN_NONE) {
		gold_request_add_condition(gold_request,
					   "Role",
					   "SystemAdmin",
					   GOLD_OPERATOR_NONE, 2);
		
		gold_request_add_condition(gold_request,
					   "Role",
					   "Operator",
					   GOLD_OPERATOR_NONE, 1);
	} else if(user_q->admin_level == ACCOUNT_ADMIN_SUPER_USER)
		gold_request_add_assignment(gold_request,
					    "Role",
					    "SystemAdmin");
	else if(user_q->admin_level == ACCOUNT_ADMIN_OPERATOR)
		gold_request_add_assignment(gold_request,
					    "Role",
					    "Operator");
	else {
		error("account_storage_p_modify_user_admin_level: "
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

	if(user_q->def_account_list && list_count(user_q->def_account_list)) {
		itr = list_iterator_create(user_q->def_account_list);
		if(list_count(user_q->def_account_list) > 1)
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
		error("account_storage_p_modify_users: "
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

extern int account_storage_p_modify_accounts(account_account_cond_t *account_q,
					     account_account_rec_t *account)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int set = 0;
	char *object = NULL;

	if(!account_q) {
		error("account_storage_p_modify_accounts: "
		      "we need conditions to modify");
		return SLURM_ERROR;
	}

	if(!account) {
		error("account_storage_p_modify_accounts: "
		      "we need something to change");
		return SLURM_ERROR;
	}
	
	gold_request = create_gold_request(GOLD_OBJECT_ACCOUNT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) { 
		error("couldn't create gold_request");
		return SLURM_ERROR;
	}

	if(account_q->account_list && list_count(account_q->account_list)) {
		itr = list_iterator_create(account_q->account_list);
		if(list_count(account_q->account_list) > 1)
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

	if(account_q->description_list 
	   && list_count(account_q->description_list)) {
		itr = list_iterator_create(account_q->description_list);
		if(list_count(account_q->description_list) > 1)
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

	if(account_q->organization_list 
	   && list_count(account_q->organization_list)) {
		itr = list_iterator_create(account_q->organization_list);
		if(list_count(account_q->organization_list) > 1)
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

	if(account->description) 
		gold_request_add_assignment(gold_request,
					    "Description",
					    account->description);
	if(account->organization) 
		gold_request_add_assignment(gold_request,
					    "Organization",
					    account->organization);
	
	if(account->expedite != ACCOUNT_EXPEDITE_NOTSET) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 account->expedite-1);
		gold_request_add_assignment(gold_request, "Expedite",
					    tmp_buff);		
	}
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("account_storage_p_modify_accounts: "
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

extern int account_storage_p_modify_clusters(account_cluster_cond_t *cluster_q,
					     account_cluster_rec_t *cluster)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_modify_associations(
	account_association_cond_t *assoc_q, account_association_rec_t *assoc)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	char *object = NULL;
	int set = 0;

	if(!assoc_q) {
		error("account_storage_p_modify_associations: "
		      "we need conditions to modify");
		return SLURM_ERROR;
	}

	if(!assoc) {
		error("account_storage_p_modify_associations: "
		      "we need something to change");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_ACCOUNT,
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

	if(assoc_q->account_list && list_count(assoc_q->account_list)) {
		itr = list_iterator_create(assoc_q->account_list);
		if(list_count(assoc_q->account_list) > 1)
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
		error("account_storage_p_modify_associations: "
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

extern int account_storage_p_remove_users(account_user_cond_t *user_q)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!user_q) {
		error("account_storage_p_remove_users: "
		      "we need conditions to remove");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_USER,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("account_storage_p_remove_users: "
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

	if(user_q->def_account_list && list_count(user_q->def_account_list)) {
		itr = list_iterator_create(user_q->def_account_list);
		if(list_count(user_q->def_account_list) > 1)
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
		error("account_storage_p_remove_users: "
		      "no response received");
		return SLURM_ERROR;
	}
		
	if(gold_response->rc) {
		error("account_storage_p_remove_users: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}
	destroy_gold_response(gold_response);		
		
	return rc;
}

extern int account_storage_p_remove_coord(char *account,
					  account_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_remove_accounts(account_account_cond_t *account_q)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!account_q) {
		error("account_storage_p_remove_accounts: "
		      "we need conditions to remove");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_PROJECT,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("account_storage_p_remove_accounts: "
		      "couldn't create gold_request");
		return SLURM_ERROR;
	}
	
	if(account_q->account_list && list_count(account_q->account_list)) {
		itr = list_iterator_create(account_q->account_list);
		if(list_count(account_q->account_list) > 1)
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

	if(account_q->description_list 
	   && list_count(account_q->description_list)) {
		itr = list_iterator_create(account_q->description_list);
		if(list_count(account_q->description_list) > 1)
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

	if(account_q->organization_list 
	   && list_count(account_q->organization_list)) {
		itr = list_iterator_create(account_q->organization_list);
		if(list_count(account_q->organization_list) > 1)
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
		error("account_storage_p_remove_accounts: "
		      "no response received");
		return SLURM_ERROR;
	}
	
	if(gold_response->rc) {
		error("account_storage_p_remove_accounts: "
		      "gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}
	destroy_gold_response(gold_response);		
		
	return rc;
}

extern int account_storage_p_remove_clusters(account_cluster_cond_t *cluster_q)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!cluster_q) {
		error("account_storage_p_modify_clusters: "
		      "we need conditions to modify");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_MACHINE,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("account_storage_p_remove_clusters: "
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
		error("account_storage_p_remove_clusters: "
		      "no response received");
		return SLURM_ERROR;
	}
	
	if(gold_response->rc) {
		error("account_storage_p_remove_clusters: "
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
		error("account_storage_p_remove_clusters: "
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
		error("account_storage_p_remove_clusters: "
		      "no response received");
		destroy_gold_request(gold_request);
		return SLURM_ERROR;
	}
		
	if(gold_response->rc) {
		error("account_storage_p_remove_clusters: "
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
		error("account_storage_p_remove_clusters: "
		      "no response received");
		destroy_gold_request(gold_request);
		return SLURM_ERROR;
	}
		
	if(gold_response->rc) {
		error("account_storage_p_remove_clusters: "
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
		error("account_storage_p_remove_clusters: "
		      "no response received");
		destroy_gold_request(gold_request);
		return SLURM_ERROR;
	}
		
	if(gold_response->rc) {
		error("account_storage_p_remove_clusters: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		rc = SLURM_ERROR;
	}
	
	destroy_gold_request(gold_request);
	destroy_gold_response(gold_response);
	
	return rc;
}

extern int account_storage_p_remove_associations(
	account_association_cond_t *assoc_q)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	char *object = NULL;
	int set = 0;

	if(!assoc_q) {
		error("account_storage_p_remove_associations: "
		      "we need conditions to remove");
		return SLURM_ERROR;
	}

	gold_request = create_gold_request(GOLD_OBJECT_ACCOUNT,
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

	if(assoc_q->account_list && list_count(assoc_q->account_list)) {
		itr = list_iterator_create(assoc_q->account_list);
		if(list_count(assoc_q->account_list) > 1)
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
		error("account_storage_p_modify_associations: "
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

extern List account_storage_p_get_users(account_user_cond_t *user_q)
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

	if(user_q->def_account_list && list_count(user_q->def_account_list)) {
		itr = list_iterator_create(user_q->def_account_list);
		if(list_count(user_q->def_account_list) > 1)
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
	
	if(user_q->expedite != ACCOUNT_EXPEDITE_NOTSET) {
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
		error("account_storage_p_get_users: no response received");
		return NULL;
	}

	user_list = _get_user_list_from_response(gold_response);
	
	destroy_gold_response(gold_response);

	return user_list;
}

extern List account_storage_p_get_accounts(account_account_cond_t *account_q)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	List account_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *object = NULL;
	char tmp_buff[50];


	gold_request = create_gold_request(GOLD_OBJECT_PROJECT,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return NULL;

	if(!account_q) 
		goto empty;

	if(account_q->account_list && list_count(account_q->account_list)) {
		itr = list_iterator_create(account_q->account_list);
		if(list_count(account_q->account_list) > 1)
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

	if(account_q->description_list 
	   && list_count(account_q->description_list)) {
		itr = list_iterator_create(account_q->description_list);
		if(list_count(account_q->description_list) > 1)
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

	if(account_q->organization_list 
	   && list_count(account_q->organization_list)) {
		itr = list_iterator_create(account_q->organization_list);
		if(list_count(account_q->organization_list) > 1)
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

	if(account_q->expedite != ACCOUNT_EXPEDITE_NOTSET) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 account_q->expedite-1);
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
		error("account_storage_p_get_accounts: no response received");
		return NULL;
	}

	account_list = _get_account_list_from_response(gold_response);
	
	destroy_gold_response(gold_response);

	return account_list;
}

extern List account_storage_p_get_clusters(account_cluster_cond_t *cluster_q)
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
		error("account_storage_p_get_clusters: no response received");
		return NULL;
	}

	cluster_list = _get_cluster_list_from_response(gold_response);
	
	destroy_gold_response(gold_response);

	return cluster_list;
}

extern List account_storage_p_get_associations(
	account_association_cond_t *assoc_q)
{

	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	List association_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *object = NULL;
	char tmp_buff[50];

	gold_request = create_gold_request(GOLD_OBJECT_ACCOUNT,
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

	if(assoc_q->account_list && list_count(assoc_q->account_list)) {
		itr = list_iterator_create(assoc_q->account_list);
		if(list_count(assoc_q->account_list) > 1)
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
		error("account_storage_p_get_associations: "
		      "no response received");
		return NULL;
	}

	association_list = _get_association_list_from_response(gold_response);

	destroy_gold_response(gold_response);

	return association_list;
}

extern int account_storage_p_get_hourly_usage(
	account_association_rec_t *acct_assoc,
	time_t start, time_t end)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_ERROR;
	char tmp_buff[50];

	if(!acct_assoc || acct_assoc->id) {
		error("account_storage_p_get_hourly_usage: "
		      "We need an id to go off to query off of");
		return rc;
	}

	gold_request = create_gold_request(
		GOLD_OBJECT_ACCOUNT_HOUR_USAGE, GOLD_ACTION_QUERY);

	if(!gold_request) 
		return rc;

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_assoc->id);
	gold_request_add_condition(gold_request, "Account", tmp_buff,
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
		error("account_storage_p_get_hourly_usage: "
		      "no response received");
		return rc;
	}

	rc = _get_account_accounting_list_from_response(
		gold_response, acct_assoc);

	destroy_gold_response(gold_response);

	return rc;
}

extern int account_storage_p_get_daily_usage(
	account_association_rec_t *acct_assoc,
	time_t start, time_t end)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_ERROR;
	char tmp_buff[50];

	if(!acct_assoc || acct_assoc->id) {
		error("account_storage_p_get_daily_usage: "
		      "We need an id to go off to query off of");
		return rc;
	}

	gold_request = create_gold_request(
		GOLD_OBJECT_ACCOUNT_DAY_USAGE, GOLD_ACTION_QUERY);

	if(!gold_request) 
		return rc;

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_assoc->id);
	gold_request_add_condition(gold_request, "Account", tmp_buff,
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
		error("account_storage_p_get_daily_usage: "
		      "no response received");
		return rc;
	}

	rc = _get_account_accounting_list_from_response(
		gold_response, acct_assoc);

	destroy_gold_response(gold_response);

	return rc;
}

extern int account_storage_p_get_monthly_usage(
	account_association_rec_t *acct_assoc,
	time_t start, time_t end)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_ERROR;
	char tmp_buff[50];

	if(!acct_assoc || acct_assoc->id) {
		error("account_storage_p_get_monthly_usage: "
		      "We need an id to go off to query off of");
		return rc;
	}

	gold_request = create_gold_request(
		GOLD_OBJECT_ACCOUNT_MONTH_USAGE, GOLD_ACTION_QUERY);

	if(!gold_request) 
		return rc;

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_assoc->id);
	gold_request_add_condition(gold_request, "Account", tmp_buff,
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
		error("account_storage_p_get_monthly_usage: "
		      "no response received");
		return rc;
	}

	rc = _get_account_accounting_list_from_response(
		gold_response, acct_assoc);

	destroy_gold_response(gold_response);

	return rc;
}
