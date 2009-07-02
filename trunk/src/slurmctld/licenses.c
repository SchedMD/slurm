/*****************************************************************************\
 *  licenses.c - Functions for handling cluster-wide consumable resources
 *****************************************************************************
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
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

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <slurm/slurm_errno.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"

#define _DEBUG 0

List license_list = (List) NULL;
static pthread_mutex_t license_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Print all licenses on a list */
static inline void _licenses_print(char *header, List licenses)
{
#if _DEBUG
	ListIterator iter;
	licenses_t *license_entry;

	info("licenses: %s", header);
	if (licenses == NULL)
		return;

	iter = list_iterator_create(licenses);
	if (iter == NULL)
		fatal("malloc failure from list_iterator_create");
	while ((license_entry = (licenses_t *) list_next(iter))) {
		info("name:%s total:%u used:%u", license_entry->name, 
		     license_entry->total, license_entry->used);
	}
	list_iterator_destroy(iter);
#endif
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
		uint16_t num = 1;
		for (i=0; token[i]; i++) {
			if (isspace(token[i])) {
				*valid = false;
				break;
			}
			if (token[i] == '*') {
				token[i++] = '\0';
				num = (uint16_t)strtol(&token[i], &end_num,10);
			}
		}
		if (num <= 0) {
			*valid = false;
			break;
		}
		license_entry = xmalloc(sizeof(licenses_t));
		license_entry->name = xstrdup(token);
		license_entry->total = num;
		list_push(lic_list, license_entry);
		token = strtok_r(NULL, ",;", &last);
	}
	xfree(tmp_str);

	if (*valid == false) {
		list_destroy(lic_list);
		lic_list = NULL;
	}
	return lic_list;
}

/* Initialize licenses on this system based upon slurm.conf */
extern int license_init(char *licenses)
{
	bool valid;

	slurm_mutex_lock(&license_mutex);
	if (license_list)
		fatal("license_list already defined");

	license_list = _build_license_list(licenses, &valid);
	if (!valid)
		fatal("Invalid configured licenses: %s", licenses);

	_licenses_print("licences_init", license_list);
	slurm_mutex_unlock(&license_mutex);
	return SLURM_SUCCESS;
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
	if (iter == NULL)
		fatal("malloc failure from list_iterator_create");
	while ((license_entry = (licenses_t *) list_next(iter))) {
		match = list_find_first(new_list, _license_find_rec, 
			license_entry->name);
		if (!match) {
			info("license %s removed with %u in use",
			     license_entry->name, license_entry->used);
		} else {
			match->used = license_entry->used;
			if (match->used > match->total) {
				info("license %s count decreased", 
				     match->name);
			}
		}
	}
	list_iterator_destroy(iter);

	list_destroy(license_list);
	license_list = new_list;
	_licenses_print("licences_update", license_list);
	slurm_mutex_unlock(&license_mutex);
	return SLURM_SUCCESS;
}

/* Free memory associated with licenses on this system */
extern void license_free(void)
{
	slurm_mutex_lock(&license_mutex);
	if (license_list) {
		list_destroy(license_list);
		license_list = (List) NULL;
	}
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
	_licenses_print("job_validate", job_license_list);
	if (!job_license_list)
		return job_license_list;

	slurm_mutex_lock(&license_mutex);
	iter = list_iterator_create(job_license_list);
	if (iter == NULL)
		fatal("malloc failure from list_iterator_create");
	while ((license_entry = (licenses_t *) list_next(iter))) {
		if (license_list) {
			match = list_find_first(license_list,
				_license_find_rec, license_entry->name);
		} else
			match = NULL;
		if (!match) {
			debug("could not find license %s for job",
			      license_entry->name);
			*valid = false;
			break;
		} else if (license_entry->total > match->total) {
			debug("job wants more %s licenses than configured",
			     match->name);
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
	if (iter == NULL)
		fatal("malloc failure from list_iterator_create");
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

	slurm_mutex_lock(&license_mutex);
	iter = list_iterator_create(job_ptr->license_list);
	if (iter == NULL)
		fatal("malloc failure from list_iterator_create");
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
	_licenses_print("licences_job_get", license_list);
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

	slurm_mutex_lock(&license_mutex);
	iter = list_iterator_create(job_ptr->license_list);
	if (iter == NULL)
		fatal("malloc failure from list_iterator_create");
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
			error("job returning unknown license %s", 
			      license_entry->name);
		}
	}
	list_iterator_destroy(iter);
	_licenses_print("licences_job_return", license_list);
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
	if (iter == NULL)
		fatal("malloc failure from list_iterator_create");
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
