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

static List _get_record_list_from_response(gold_response_t *gold_response);
static int _get_account_accounting_list_from_response(
	gold_response_t *gold_response,
	account_record_rec_t *account_rec);
static List _get_user_list_from_response(gold_response_t *gold_response);
static List _get_account_list_from_response(gold_response_t *gold_response);
static List _get_cluster_list_from_response(gold_response_t *gold_response);


static List _get_record_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List record_list = NULL;
	account_record_rec_t *account_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	if(gold_response->entry_cnt <= 0) {
		debug2("_get_record_list_from_response: No entries given");
		return NULL;
	}
	record_list = list_create(destroy_account_record_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		account_rec = xmalloc(sizeof(account_record_rec_t));

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
		list_append(record_list, account_rec);
	}
	list_iterator_destroy(itr);

	return record_list;
}

static int _get_account_accounting_list_from_response(
	gold_response_t *gold_response,
	account_record_rec_t *account_rec)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	account_accounting_rec_t *accounting_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	if(gold_response->entry_cnt <= 0) {
		debug2("_get_accounting_list_from_response: No entries given");
		return SLURM_ERROR;
	}
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
	
	if(gold_response->entry_cnt <= 0) {
		debug2("_get_user_list_from_response: No entries given");
		return NULL;
	}
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
					atoi(name_val->value);
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
	account_acct_rec_t *account_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	if(gold_response->entry_cnt <= 0) {
		debug2("_get_account_list_from_response: No entries given");
		return NULL;
	}
	account_list = list_create(destroy_account_acct_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		account_rec = xmalloc(sizeof(account_acct_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, "Expedite")) {
				account_rec->expedite = 
					atoi(name_val->value);
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
	
	if(gold_response->entry_cnt <= 0) {
		debug2("_get_cluster_list_from_response: No entries given");
		return NULL;
	}
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
	return SLURM_SUCCESS;
}

extern int account_storage_p_add_coord(char *account, List user_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_add_accounts(List account_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_add_clusters(List cluster_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_add_records(List record_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_modify_users(List user_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_modify_user_admin_level(
	account_admin_level_t level, List user_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_modify_accounts(List account_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_modify_clusters(List cluster_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_modify_records(List record_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_remove_users(List user_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_remove_coord(char *account, List user_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_remove_accounts(List account_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_remove_clusters(List cluster_list)
{
	return SLURM_SUCCESS;
}

extern int account_storage_p_remove_records(List record_list)
{
	return SLURM_SUCCESS;
}

extern List account_storage_p_get_users(List selected_users,
					void *params)
{
	gold_request_t *gold_request = create_gold_request(GOLD_OBJECT_USER,
							   GOLD_ACTION_QUERY);
	gold_response_t *gold_response = NULL;
	char *temp_char = NULL;
	List user_list = NULL;
	ListIterator itr = NULL;
	int set = 0;

	if(!gold_request) 
		return NULL;

	if(selected_users && list_count(selected_users)) {
		itr = list_iterator_create(selected_users);
		if(list_count(selected_users) > 1)
			set = 2;
		else
			set = 0;
		while((temp_char = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   temp_char,
						   GOLD_OPERATOR_NONE,
						   set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	gold_request_add_selection(gold_request, "Name");
	gold_request_add_selection(gold_request, "DefaultProject");
	gold_request_add_selection(gold_request, "Expedite");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_cluster_procs: no response received");
		return NULL;
	}

	if(gold_response->entry_cnt > 0) {
		user_list = _get_user_list_from_response(gold_response);
	} else {
		debug("We don't have an entry for this machine for this time");
	}
	destroy_gold_response(gold_response);

	return user_list;
}

extern List account_storage_p_get_accounts(List selected_accounts,
					   void *params)
{
	gold_request_t *gold_request = create_gold_request(GOLD_OBJECT_PROJECT,
							   GOLD_ACTION_QUERY);
	gold_response_t *gold_response = NULL;
	char *temp_char = NULL;
	List account_list = NULL;
	ListIterator itr = NULL;
	int set = 0;

	if(!gold_request) 
		return NULL;

	if(selected_accounts && list_count(selected_accounts)) {
		itr = list_iterator_create(selected_accounts);
		if(list_count(selected_accounts) > 1)
			set = 2;
		else
			set = 0;
		while((temp_char = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   temp_char,
						   GOLD_OPERATOR_NONE,
						   set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	gold_request_add_selection(gold_request, "Name");
	gold_request_add_selection(gold_request, "Organization");
	gold_request_add_selection(gold_request, "Description");
	gold_request_add_selection(gold_request, "Expedite");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_cluster_procs: no response received");
		return NULL;
	}

	if(gold_response->entry_cnt > 0) {
		account_list = _get_account_list_from_response(gold_response);
	} else {
		debug("We don't have an entry for this machine for this time");
	}
	destroy_gold_response(gold_response);

	return account_list;
}

extern List account_storage_p_get_clusters(List selected_clusters,
					   void *params)
{
	gold_request_t *gold_request = create_gold_request(GOLD_OBJECT_MACHINE,
							   GOLD_ACTION_QUERY);
	gold_response_t *gold_response = NULL;
	char *temp_char = NULL;
	List cluster_list = NULL;
	ListIterator itr = NULL;
	int set = 0;

	if(!gold_request) 
		return NULL;

	if(selected_clusters && list_count(selected_clusters)) {
		itr = list_iterator_create(selected_clusters);
		if(list_count(selected_clusters) > 1)
			set = 2;
		else
			set = 0;
		while((temp_char = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   temp_char,
						   GOLD_OPERATOR_NONE,
						   set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	gold_request_add_selection(gold_request, "Name");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_cluster_procs: no response received");
		return NULL;
	}

	if(gold_response->entry_cnt > 0) {
		cluster_list = _get_cluster_list_from_response(gold_response);
	} else {
		debug("We don't have an entry for this machine for this time");
	}
	destroy_gold_response(gold_response);

	return cluster_list;
}

extern List account_storage_p_get_records(List selected_users,
					  List selected_accounts,
					  List selected_parts,
					  char *cluster,
					  void *params)
{

	gold_request_t *gold_request = create_gold_request(GOLD_OBJECT_ACCOUNT,
							   GOLD_ACTION_QUERY);
	gold_response_t *gold_response = NULL;
	char *temp_char = NULL;
	List record_list = NULL;
	ListIterator itr = NULL;
	int set = 0;

	if(!gold_request) 
		return NULL;

	if(selected_users && list_count(selected_users)) {
		itr = list_iterator_create(selected_users);
		if(list_count(selected_users) > 1)
			set = 2;
		else
			set = 0;
		while((temp_char = list_next(itr))) {
			gold_request_add_condition(gold_request, "User",
						   temp_char,
						   GOLD_OPERATOR_NONE,
						   set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}
	if(selected_accounts && list_count(selected_accounts)) {
		itr = list_iterator_create(selected_accounts);
		if(list_count(selected_accounts) > 1)
			set = 2;
		else
			set = 0;
		while((temp_char = list_next(itr))) {
			gold_request_add_condition(gold_request, "Project",
						   temp_char,
						   GOLD_OPERATOR_NONE,
						   set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

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
		error("account_storage_p_get_records: no response received");
		return NULL;
	}

	if(gold_response->entry_cnt > 0) {
		record_list = _get_record_list_from_response(gold_response);
	} else {
		debug("We don't have an entry for this machine for this time");
	}
	destroy_gold_response(gold_response);

	return record_list;
}

extern int account_storage_p_get_hourly_usage(account_record_rec_t *acct_rec,
					      time_t start, 
					      time_t end,
					      void *params)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_ERROR;
	char tmp_buff[50];

	if(!acct_rec || acct_rec->id) {
		error("account_storage_p_get_hourly_usage: "
		      "We need an id to go off to query off of");
		return rc;
	}

	gold_request = create_gold_request(
		GOLD_OBJECT_ACCOUNT_HOUR_USAGE, GOLD_ACTION_QUERY);

	if(!gold_request) 
		return rc;

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_rec->id);
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

	if(gold_response->entry_cnt > 0) {
		rc = _get_account_accounting_list_from_response(
			gold_response, acct_rec);
	} else {
		debug("We don't have an entry for this machine for this time");
	}
	destroy_gold_response(gold_response);

	return rc;
}

extern int account_storage_p_get_daily_usage(account_record_rec_t *acct_rec,
					     time_t start, 
					     time_t end,
					     void *params)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_ERROR;
	char tmp_buff[50];

	if(!acct_rec || acct_rec->id) {
		error("account_storage_p_get_daily_usage: "
		      "We need an id to go off to query off of");
		return rc;
	}

	gold_request = create_gold_request(
		GOLD_OBJECT_ACCOUNT_DAY_USAGE, GOLD_ACTION_QUERY);

	if(!gold_request) 
		return rc;

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_rec->id);
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

	if(gold_response->entry_cnt > 0) {
		rc = _get_account_accounting_list_from_response(
			gold_response, acct_rec);
	} else {
		debug("We don't have an entry for this machine for this time");
	}
	destroy_gold_response(gold_response);

	return rc;
}

extern int account_storage_p_get_monthly_usage(account_record_rec_t *acct_rec,
					       time_t start, 
					       time_t end,
					       void *params)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_ERROR;
	char tmp_buff[50];

	if(!acct_rec || acct_rec->id) {
		error("account_storage_p_get_monthly_usage: "
		      "We need an id to go off to query off of");
		return rc;
	}

	gold_request = create_gold_request(
		GOLD_OBJECT_ACCOUNT_MONTH_USAGE, GOLD_ACTION_QUERY);

	if(!gold_request) 
		return rc;

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_rec->id);
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

	if(gold_response->entry_cnt > 0) {
		rc = _get_account_accounting_list_from_response(
			gold_response, acct_rec);
	} else {
		debug("We don't have an entry for this machine for this time");
	}
	destroy_gold_response(gold_response);

	return rc;
}
