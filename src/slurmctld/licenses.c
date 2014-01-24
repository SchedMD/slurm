/*****************************************************************************\
 *  licenses.c - Functions for handling cluster-wide consumable resources
 *****************************************************************************
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
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

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/slurm_accounting_storage.h"

List license_list = (List) NULL;
List clus_license_list = (List) NULL;
time_t last_license_update;
static pthread_mutex_t license_mutex = PTHREAD_MUTEX_INITIALIZER;
static void _pack_license(struct licenses *lic, Buf buffer, uint16_t protocol_version);

/* Print all licenses on a list */
static inline void _licenses_print(char *header, List licenses, int job_id)
{
	ListIterator iter;
	licenses_t *license_entry;

	if (licenses == NULL)
		return;

	iter = list_iterator_create(licenses);
  	while ((license_entry = (licenses_t *) list_next(iter))) {
		if (job_id == 0) {
			info("licenses: %s=%s total=%u used=%u",
			     header, license_entry->name,
			     license_entry->total, license_entry->used);
		} else {
			info("licenses: %s=%s job_id=%u available=%u used=%u",
			     header, license_entry->name, job_id,
			     license_entry->total, license_entry->used);
		}
	}
	list_iterator_destroy(iter);
}

/* Free a license_t record (for use by list_destroy) */
extern void license_free_rec(void *x)
{
	licenses_t *license_entry = (licenses_t *) x;

	if (license_entry) {
		xfree(license_entry->name);
		xfree(license_entry);
	}
}

/* Find a license_t record by license name (for use by list_find_first) */
static int _license_find_rec(void *x, void *key)
{
	licenses_t *license_entry = (licenses_t *) x;
	char *name = (char *) key;

	if ((license_entry->name == NULL) || (name == NULL))
		return 0;
	if (strcmp(license_entry->name, name))
		return 0;
	return 1;
}

/* Find a slurmdb_ser_res_rec_t record by license name
 *(for use by list_find_first) */
static int _license_find_sys_rec(void *x, void *key)
{
	slurmdb_ser_res_rec_t *license_entry = (slurmdb_ser_res_rec_t *) x;
	char *name = (char *) key;

	if ((license_entry->name == NULL) || (name == NULL))
		return 0;
	if (strcmp(license_entry->name, name))
		return 0;
	return 1;
}

/* Find a slurmdb_clus_res_rec_t record by license name
 *(for use by list_find_first) */
static int _license_find_clus_rec(void *x, void *key)
{
	slurmdb_clus_res_rec_t *license_entry = (slurmdb_clus_res_rec_t *) x;
	char *name = (char *) key;

	if ((license_entry->res_ptr->name == NULL) || (name == NULL))
		return 0;
	if (strcmp(license_entry->res_ptr->name, name))
		return 0;
	return 1;
}

/* Given a license string, return a list of license_t records */
static List _build_license_list(char *licenses, bool *valid)
{
	int i;
	char *end_num, *tmp_str, *token, *last;
	licenses_t *license_entry;
	List lic_list;

	*valid = true;
	if ((licenses == NULL) || (licenses[0] == '\0'))
		return NULL;

	lic_list = list_create(license_free_rec);
	tmp_str = xstrdup(licenses);
	token = strtok_r(tmp_str, ",;", &last);
	while (token && *valid) {
		uint32_t num = 1;
		for (i = 0; token[i]; i++) {
			if (isspace(token[i])) {
				*valid = false;
				break;
			}
			/* ':' is used as a separator in version 2.5 or later
			 * '*' is used as a separator in version 2.4 or earlier
			 */
			if ((token[i] == ':') || (token[i] == '*')) {
				token[i++] = '\0';
				num = (uint32_t)strtol(&token[i], &end_num,10);
			}
		}
		if (num <= 0) {
			*valid = false;
			break;
		}

		license_entry = list_find_first(lic_list, _license_find_rec,
						token);
		if (license_entry) {
			license_entry->total += num;
		} else {
			license_entry = xmalloc(sizeof(licenses_t));
			license_entry->name = xstrdup(token);
			license_entry->total = num;
			list_push(lic_list, license_entry);
		}
		token = strtok_r(NULL, ",;", &last);
	}
	xfree(tmp_str);

	if (*valid == false) {
		list_destroy(lic_list);
		lic_list = NULL;
	}
	return lic_list;
}

/* Given a list of license_t records, return a license string.
 * This can be combined with _build_license_list() to eliminate duplicates
 * (e.g. "tux*2,tux*3" gets changed to "tux*5"). */
static char * _build_license_string(List license_list)
{
	char buf[128], *sep;
	char *licenses = NULL;
	ListIterator iter;
	licenses_t *license_entry;

	if (!license_list)
		return licenses;

	iter = list_iterator_create(license_list);
	while ((license_entry = (licenses_t *) list_next(iter))) {
		if (licenses)
			sep = ",";
		else
			sep = "";
		snprintf(buf, sizeof(buf), "%s%s*%u", sep, license_entry->name,
			 license_entry->total);
		xstrcat(licenses, buf);
	}
	list_iterator_destroy(iter);

	return licenses;
}

/* Get string of used license information. Caller must xfree return value */
extern char *get_licenses_used(void)
{
	char *licenses_used = NULL;
	licenses_t *license_entry;
	ListIterator iter;

	slurm_mutex_lock(&license_mutex);
	if (license_list) {
		iter = list_iterator_create(license_list);
		while ((license_entry = (licenses_t *) list_next(iter))) {
			if (licenses_used)
				xstrcat(licenses_used, ",");
			xstrfmtcat(licenses_used, "%s:%u/%u",
			           license_entry->name, license_entry->used,
			           license_entry->total);
		}
		list_iterator_destroy(iter);
	}
	slurm_mutex_unlock(&license_mutex);

	return licenses_used;
}

/* Merge a cluster license list (built using accounting database information
 * into a license_list, preserving all previously allocated licenses.
*/
static List merge_license_lists(List lic_list, List clus_license_list)
{
	licenses_t *license, *cluster_license, *new_license;
	ListIterator itr = NULL;
	licenses_t *match_lic = NULL;
	uint16_t temp;

	if (!clus_license_list)
		return lic_list;
	if (!license_list)
		return clus_license_list;
	/* deal with new & potentially modified cluster licenses */
	itr = list_iterator_create(clus_license_list);
	while ((cluster_license = list_next(itr))){
		match_lic = list_find_first(license_list,
			_license_find_rec, cluster_license->name);

		if (match_lic == NULL) {		/* new license */
			new_license = xmalloc(sizeof(licenses_t));
			new_license->name = xstrdup(cluster_license->name);
			new_license->total = cluster_license->total;
			new_license->used = 0;
			new_license->cluster = 1;
			list_push(lic_list, new_license);
		} else if ((match_lic != NULL)
			   && cluster_license->cluster == 1) {
			match_lic->total = cluster_license->total;
		}
	}
	list_iterator_destroy(itr);

	/* now deal with deleted cluster licenses */
	itr = list_iterator_create(license_list);
	while ((license = (licenses_t *) list_next(itr))) {
		match_lic = list_find_first(clus_license_list,
			_license_find_rec, license->name);

		if ((match_lic == NULL) && (license->cluster == 1)) {
			temp = list_delete_item(itr);
			if (!temp)
				return NULL;
		}
	}
	list_iterator_destroy(itr);
	return lic_list;
}

/* update clus_license_list & license_list for a modified system license */
static int _modify_ser_lic(List clus_license_list, List clus_rec_list,
			    slurmdb_update_object_t *update_obj)
{
	ListIterator itr = NULL;
	slurmdb_ser_res_rec_t *match_sys_rec = NULL;
	slurmdb_clus_res_rec_t *match_clus_rec = NULL;
	licenses_t *license;
	uint16_t rc = SLURM_SUCCESS;

	itr = list_iterator_create(clus_license_list);
	while ((license = list_next(itr))) {
		match_sys_rec = list_find_first(update_obj->objects,
				_license_find_sys_rec, license->name);
		if (match_sys_rec) {
			match_clus_rec = list_find_first(clus_rec_list,
			_license_find_clus_rec, license->name);
			if (match_clus_rec) {
				license->total =
				 ((match_sys_rec->count *
				   match_clus_rec->percent_allowed) / 100);
			}
		}
	}
	list_iterator_destroy(itr);
	return rc;
}

/* update clus_license_list to add a cluster license */
static int _add_clus_lic(List clus_license_list,
			  slurmdb_update_object_t *update_obj)
{
	ListIterator itr = NULL;
	slurmdb_clus_res_rec_t *clus_rec = NULL;
	licenses_t *match_lic = NULL;
	licenses_t *new_license;
	uint16_t rc = SLURM_SUCCESS;
	char *cluster_name = NULL;

	cluster_name = slurm_get_cluster_name();
	itr = list_iterator_create(update_obj->objects);
	while ((clus_rec = list_next(itr))) {
		if (!strstr(clus_rec->cluster, cluster_name) ||
		    clus_rec->res_ptr->type != SLURMDB_RESOURCE_LICENSE)
			continue;
		match_lic = list_find_first(clus_license_list,
			_license_find_rec, clus_rec->res_ptr->name);

		if (match_lic == NULL) {
			new_license = xmalloc(sizeof(licenses_t));
			new_license->name = xstrdup(clus_rec->res_ptr->name);
			new_license->total =
			((clus_rec->res_ptr->count *
			  clus_rec->percent_allowed) / 100);
			new_license->used = 0;
			new_license->cluster = 1;
			list_push(clus_license_list, new_license);
			slurmdb_clus_res_rec_t *new_rec =
					xmalloc(sizeof(slurmdb_clus_res_rec_t));
			slurmdb_init_clus_res_rec(new_rec, 0);
			new_rec->res_ptr = xmalloc(sizeof(
					   slurmdb_ser_res_rec_t));
			slurmdb_init_ser_res_rec(new_rec->res_ptr, 0);
			new_rec->res_ptr->description =
			   xstrdup(clus_rec->res_ptr->description);
			new_rec->res_ptr->id = clus_rec->res_ptr->id;
			new_rec->res_ptr->name =
			   xstrdup(clus_rec->res_ptr->name);
			new_rec->res_ptr->count = clus_rec->res_ptr->count;
			new_rec->res_ptr->type = clus_rec->res_ptr->type;
			new_rec->res_ptr->manager =
			   xstrdup(clus_rec->res_ptr->manager);
			new_rec->res_ptr->server =
			   xstrdup(clus_rec->res_ptr->server);
			new_rec->cluster = xstrdup(clus_rec->cluster);
			new_rec->percent_allowed = clus_rec->percent_allowed;
			list_append(assoc_mgr_clus_res_list, new_rec);
		} else {
			error("cluster_resource_list already contains"
			       " this cluster license %s",
			       clus_rec->res_ptr->name);
			rc = SLURM_ERROR;
			break;
		}
	}
	xfree(cluster_name);
	list_iterator_destroy(itr);
	return rc;
}

/* update clus_license_list for a modified cluster license */
static int _modify_clus_lic(List clus_license_list,
			     slurmdb_update_object_t *update_obj)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_clus_res_rec_t *clus_rec = NULL;
	slurmdb_clus_res_rec_t *assoc_mgr_rec = NULL;
	licenses_t *match_lic = NULL;
	uint16_t rc = SLURM_SUCCESS;
	char *cluster_name = NULL;

	cluster_name = slurm_get_cluster_name();
	itr = list_iterator_create(update_obj->objects);
	itr2 = list_iterator_create(assoc_mgr_clus_res_list);
	while ((clus_rec = list_next(itr))) {
		if (!strstr(clus_rec->cluster, cluster_name) ||
		    clus_rec->res_ptr->type != SLURMDB_RESOURCE_LICENSE)
			continue;
		match_lic = list_find_first(clus_license_list,
			    _license_find_rec, clus_rec->res_ptr->name);
		if (match_lic) {
			match_lic->total =
			((clus_rec->res_ptr->count *
			  clus_rec->percent_allowed) / 100);
			while ((assoc_mgr_rec = list_next(itr2))) {
				if (strstr(clus_rec->res_ptr->name,
					assoc_mgr_rec->res_ptr->name)) {
						assoc_mgr_rec->percent_allowed =
						   clus_rec->percent_allowed;
					}
			}
			list_iterator_reset(itr2);
		} else {
			error("cluster_license_list doesn't contain %s",
			       clus_rec->res_ptr->name);
			rc = SLURM_ERROR;
			break;
		}
	}
	xfree(cluster_name);
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	return rc;
}

/* update clus_license_list for a removed cluster license */
static int _remove_clus_lic(List clus_license_list,
			     slurmdb_update_object_t *update_obj)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator itr3 = NULL;
	slurmdb_clus_res_rec_t *clus_rec = NULL;
	slurmdb_clus_res_rec_t *assoc_mgr_rec = NULL;
	licenses_t *license;
	uint16_t temp;
	uint16_t rc = SLURM_SUCCESS;
	char *cluster_name = NULL;

	cluster_name = slurm_get_cluster_name();
	itr = list_iterator_create(update_obj->objects);
	itr2 = list_iterator_create(clus_license_list);
	itr3 = list_iterator_create(assoc_mgr_clus_res_list);
	while ((clus_rec = list_next(itr))) {
		if (!strstr(clus_rec->cluster, cluster_name) ||
		    clus_rec->res_ptr->type != SLURMDB_RESOURCE_LICENSE)
			continue;
		while ((license = list_next(itr2))) {
			if (strstr(clus_rec->res_ptr->name, license->name)) {
				temp = list_delete_item(itr2);
				if (!temp) {
					error("clus_license_list removal"
					      "problem for %s", license->name);
					rc = SLURM_ERROR;
					break;
				}
				while ((assoc_mgr_rec = list_next(itr3))) {
					if (strstr(clus_rec->res_ptr->name,
						assoc_mgr_rec->res_ptr->name)) {
						temp = list_delete_item(itr3);
						if (!temp) {
							error("assoc_mgr_clus_"
							"res_list removal"
							"problem for %s",
							license->name);
							rc = SLURM_ERROR;
							break;
						}
					}
				}
				list_iterator_reset(itr3);
			}
		}
		list_iterator_reset(itr2);
	}
	xfree(cluster_name);
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr3);
	return rc;
}

/* create a list of license_t records from a list of clus_res_rec_t
 * records
 */
static List _clus_license_init(List clus_rec_list)
{
	List clus_license_list = NULL;
	ListIterator itr = NULL;
	slurmdb_clus_res_rec_t *clus_rec = NULL;
	licenses_t *license;

	if (!clus_rec_list) {
		return NULL;
	}
	clus_license_list = list_create(license_free_rec);
	itr = list_iterator_create(clus_rec_list );
	while ((clus_rec = list_next(itr))) {
		license = xmalloc(sizeof(licenses_t));
		license->name = xstrdup(clus_rec->res_ptr->name);
		license->total =
		 ((clus_rec->res_ptr->count * clus_rec->percent_allowed) / 100);
		license->used = 0;
		license->cluster = 1;
		list_push(clus_license_list, license);
	}
	list_iterator_destroy(itr);
	return clus_license_list;
}

/* Initialize licenses on this system based upon slurm.conf
 * and information in the accounting database/
 */
extern int license_init(char *licenses)
{
	bool valid;

	last_license_update = time(NULL);

	slurm_mutex_lock(&license_mutex);
	if (license_list)
		fatal("license_list already defined");

	license_list = _build_license_list(licenses, &valid);
	if (!valid)
		fatal("Invalid configured licenses: %s", licenses);

	if (assoc_mgr_clus_res_list) {
		clus_license_list = _clus_license_init(assoc_mgr_clus_res_list);
		license_list = merge_license_lists(license_list,
			       clus_license_list);
	}

	_licenses_print("init_license", license_list, 0);
	slurm_mutex_unlock(&license_mutex);

	return SLURM_SUCCESS;
}

/* Update the cluster license list for this system.
 * Preserve all previously allocated licenses */
static List _clus_license_list_update(slurmdb_update_object_t *update_obj)
{
	uint16_t type;
	uint16_t rc = SLURM_SUCCESS;

	if (!clus_license_list) {
		if (assoc_mgr_clus_res_list)
			clus_license_list =
			   _clus_license_init(assoc_mgr_clus_res_list);
	}
	type = update_obj->type;
	switch ( type )
	{
		case SLURMDB_MODIFY_SER_RES :
			rc = _modify_ser_lic(clus_license_list,
					     assoc_mgr_clus_res_list,
					     update_obj);
			break;
		case SLURMDB_ADD_SER_RES :
		case SLURMDB_REMOVE_SER_RES :
			break;
		case SLURMDB_ADD_CLUS_RES :
			rc = _add_clus_lic(clus_license_list, update_obj);
			break;
		case SLURMDB_MODIFY_CLUS_RES :
			rc = _modify_clus_lic(clus_license_list, update_obj);
			break;
		case SLURMDB_REMOVE_CLUS_RES :
			rc = _remove_clus_lic(clus_license_list, update_obj);
			break;
	}
	if (rc != SLURM_SUCCESS) {
		error("problem updating clus_license_list");
		return NULL;
	}

	return clus_license_list;
}

/* Update licenses on this system based upon slurm.conf.
 * Preserve all previously allocated licenses */
extern int license_update(char *licenses)
{
	ListIterator iter;
	licenses_t *license_entry, *match;
	List new_list;
	bool valid;

	new_list = _build_license_list(licenses, &valid);
	if (!valid)
		fatal("Invalid configured licenses: %s", licenses);

	slurm_mutex_lock(&license_mutex);
	if (!license_list) {	/* no licenses before now */
		license_list = new_list;
		slurm_mutex_unlock(&license_mutex);
		return SLURM_SUCCESS;
	}

	iter = list_iterator_create(license_list);
	while ((license_entry = (licenses_t *) list_next(iter))) {
		match = list_find_first(new_list, _license_find_rec,
			license_entry->name);
		if (!match) {
			info("license %s removed with %u in use",
			     license_entry->name, license_entry->used);
		} else {
			match->used = license_entry->used;
			match->cluster = 0;
			if (match->used > match->total) {
				info("license %s count decreased",
				     match->name);
			}
		}
	}
	list_iterator_destroy(iter);

	list_destroy(license_list);
	license_list = new_list;
	_licenses_print("update_license", license_list, 0);
	slurm_mutex_unlock(&license_mutex);
	return SLURM_SUCCESS;
}

/* Update cluster licenses on this system based upon accounting database.
 * Preserve all previously allocated licenses */
extern int cluster_license_update(slurmdb_update_object_t *update_obj)
{
	List clus_license_list;

	slurm_mutex_lock(&license_mutex);
	clus_license_list = _clus_license_list_update(update_obj);

	if (!license_list) {	/* no licenses before now */
		license_list = clus_license_list;
	} else {
		if (clus_license_list)
			license_list =
			  merge_license_lists(license_list, clus_license_list);
	}
	slurm_mutex_unlock(&license_mutex);
	return SLURM_SUCCESS;
}

/* Free memory associated with licenses on this system */
extern void license_free(void)
{
	slurm_mutex_lock(&license_mutex);
	FREE_NULL_LIST(license_list);
	slurm_mutex_unlock(&license_mutex);
}

/*
 * license_validate - Test if the required licenses are valid
 * IN licenses - required licenses
 * OUT valid - true if required licenses are valid and a sufficient number
 *             are configured (though not necessarily available now)
 * RET license_list, must be destroyed by caller
 */
extern List license_validate(char *licenses, bool *valid)
{
	ListIterator iter;
	licenses_t *license_entry, *match;
	List job_license_list;

	job_license_list = _build_license_list(licenses, valid);
	if (!job_license_list)
		return job_license_list;

	slurm_mutex_lock(&license_mutex);
	_licenses_print("request_license", job_license_list, 0);
	iter = list_iterator_create(job_license_list);
	while ((license_entry = (licenses_t *) list_next(iter))) {
		if (license_list) {
			match = list_find_first(license_list,
						_license_find_rec,
						license_entry->name);
		} else
			match = NULL;
		if (!match) {
			debug("License name requested (%s) does not exist",
			      license_entry->name);
			*valid = false;
			break;
		} else if (license_entry->total > match->total) {
			debug("Licenses count requested higher than configured "
			      "(%s: %u > %u)",
			      match->name, license_entry->total, match->total);
			*valid = false;
			break;
		}
	}
	list_iterator_destroy(iter);
	slurm_mutex_unlock(&license_mutex);

	if (!(*valid)) {
		list_destroy(job_license_list);
		job_license_list = NULL;
	}
	return job_license_list;
}

/*
 * license_job_merge - The licenses from one job have just been merged into
 *	another job by appending one job's licenses to another, possibly
 *	including duplicate names. Reconstruct this job's licenses and
 *	license_list fields to eliminate duplicates.
 */
extern void license_job_merge(struct job_record *job_ptr)
{
	bool valid;

	FREE_NULL_LIST(job_ptr->license_list);
	job_ptr->license_list = _build_license_list(job_ptr->licenses, &valid);
	xfree(job_ptr->licenses);
	job_ptr->licenses = _build_license_string(job_ptr->license_list);
}

/*
 * license_job_test - Test if the licenses required for a job are available
 * IN job_ptr - job identification
 * IN when    - time to check
 * RET: SLURM_SUCCESS, EAGAIN (not available now), SLURM_ERROR (never runnable)
 */
extern int license_job_test(struct job_record *job_ptr, time_t when)
{
	ListIterator iter;
	licenses_t *license_entry, *match;
	int rc = SLURM_SUCCESS, resv_licenses;

	if (!job_ptr->license_list)	/* no licenses needed */
		return rc;

	slurm_mutex_lock(&license_mutex);
	iter = list_iterator_create(job_ptr->license_list);
	while ((license_entry = (licenses_t *) list_next(iter))) {
		match = list_find_first(license_list, _license_find_rec,
			license_entry->name);
		if (!match) {
			error("could not find license %s for job %u",
			      license_entry->name, job_ptr->job_id);
			rc = SLURM_ERROR;
			break;
		} else if (license_entry->total > match->total) {
			info("job %u wants more %s licenses than configured",
			     job_ptr->job_id, match->name);
			rc = SLURM_ERROR;
			break;
		} else if ((license_entry->total + match->used) >
			   match->total) {
			rc = EAGAIN;
			break;
		} else {
			resv_licenses = job_test_lic_resv(job_ptr,
							  license_entry->name,
							  when);
			if ((license_entry->total + match->used +
			     resv_licenses) > match->total) {
				rc = EAGAIN;
				break;
			}
		}
	}
	list_iterator_destroy(iter);
	slurm_mutex_unlock(&license_mutex);
	return rc;
}

/*
 * license_job_copy - create a copy of a job's license list
 * IN license_list_src - job license list to be copied
 * RET a copy of the original job license list
 */
extern List license_job_copy(List license_list_src)
{
	licenses_t *license_entry_src, *license_entry_dest;
	ListIterator iter;
	List license_list_dest = NULL;

	if (!license_list_src)
		return license_list_dest;

	license_list_dest = list_create(license_free_rec);
	iter = list_iterator_create(license_list_src);
	while ((license_entry_src = (licenses_t *) list_next(iter))) {
		license_entry_dest = xmalloc(sizeof(licenses_t));
		license_entry_dest->name = xstrdup(license_entry_src->name);
		license_entry_dest->total = license_entry_src->total;
		list_push(license_list_dest, license_entry_dest);
	}
	list_iterator_destroy(iter);
	return license_list_dest;
}

/*
 * license_job_get - Get the licenses required for a job
 * IN job_ptr - job identification
 * RET SLURM_SUCCESS or failure code
 */
extern int license_job_get(struct job_record *job_ptr)
{
	ListIterator iter;
	licenses_t *license_entry, *match;
	int rc = SLURM_SUCCESS;

	if (!job_ptr->license_list)	/* no licenses needed */
		return rc;

	last_license_update = time(NULL);

	slurm_mutex_lock(&license_mutex);
	iter = list_iterator_create(job_ptr->license_list);
	while ((license_entry = (licenses_t *) list_next(iter))) {
		match = list_find_first(license_list, _license_find_rec,
			license_entry->name);
		if (match) {
			match->used += license_entry->total;
			license_entry->used += license_entry->total;
		} else {
			error("could not find license %s for job %u",
			      license_entry->name, job_ptr->job_id);
			rc = SLURM_ERROR;
		}
	}
	list_iterator_destroy(iter);
	_licenses_print("acquire_license", license_list, job_ptr->job_id);
	slurm_mutex_unlock(&license_mutex);
	return rc;
}

/*
 * license_job_return - Return the licenses allocated to a job
 * IN job_ptr - job identification
 * RET SLURM_SUCCESS or failure code
 */
extern int license_job_return(struct job_record *job_ptr)
{
	ListIterator iter;
	licenses_t *license_entry, *match;
	int rc = SLURM_SUCCESS;

	if (!job_ptr->license_list)	/* no licenses needed */
		return rc;

	last_license_update = time(NULL);

	slurm_mutex_lock(&license_mutex);
	iter = list_iterator_create(job_ptr->license_list);
	while ((license_entry = (licenses_t *) list_next(iter))) {
		match = list_find_first(license_list, _license_find_rec,
			license_entry->name);
		if (match) {
			if (match->used >= license_entry->total)
				match->used -= license_entry->total;
			else {
				error("license use count underflow for %s",
				      match->name);
				match->used = 0;
				rc = SLURM_ERROR;
			}
			license_entry->used = 0;
		} else {
			/* This can happen after a reconfiguration */
			error("job returning unknown license name %s",
			      license_entry->name);
		}
	}
	list_iterator_destroy(iter);
	_licenses_print("return_license", license_list, job_ptr->job_id);
	slurm_mutex_unlock(&license_mutex);
	return rc;
}

/*
 * license_list_overlap - test if there is any overlap in licenses
 *	names found in the two lists
 */
extern bool license_list_overlap(List list_1, List list_2)
{
	ListIterator iter;
	licenses_t *license_entry;
	bool match = false;

	if (!list_1 || !list_2)
		return false;

	iter = list_iterator_create(list_1);
	while ((license_entry = (licenses_t *) list_next(iter))) {
		if (list_find_first(list_2, _license_find_rec,
				    license_entry->name)) {
			match = true;
			break;
		}
	}
	list_iterator_destroy(iter);

	return match;
}

/* pack_all_licenses()
 *
 * Return license counters to the library.
 */
extern void
get_all_license_info(char **buffer_ptr,
                     int *buffer_size,
                     uid_t uid,
                     uint16_t protocol_version)
{
	ListIterator iter;
	licenses_t *lic_entry;
	uint32_t lics_packed;
	int tmp_offset;
	Buf buffer;
	time_t now = time(NULL);

	debug2("%s: calling for all licenses", __func__);

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf(BUF_SIZE);

	/* write header: version and time
	 */
	lics_packed = 0;
	pack32(lics_packed, buffer);
	pack_time(now, buffer);

	slurm_mutex_lock(&license_mutex);

	if (license_list) {

		iter = list_iterator_create(license_list);
		while ((lic_entry = list_next(iter))) {
			/* Now encode the license data structure.
			 */
			_pack_license(lic_entry, buffer, protocol_version);
			++lics_packed;
		}
		list_iterator_destroy(iter);
	}

	slurm_mutex_unlock(&license_mutex);
	debug2("%s: processed %d licenses", __func__, lics_packed);

	/* put the real record count in the message body header
	 */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(lics_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
}

/* pack_license()
 *
 * Encode the licenses data structure.
 *
 *	char *		name;
 *	uint32_t	total;
 *	uint32_t	used;
 *	uint32_t	cluster;
 *
 */
static void
_pack_license(struct licenses *lic, Buf buffer, uint16_t protocol_version)
{
	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		packstr(lic->name, buffer);
		pack32(lic->total, buffer);
		pack32(lic->used, buffer);
		pack32(lic->cluster, buffer);
	} else {
		error("\
%s: protocol_version %hu not supported", __func__, protocol_version);
	}
}
