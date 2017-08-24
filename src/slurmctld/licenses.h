/*****************************************************************************\
 *  licenses.h - Definitions for handling cluster-wide consumable resources
 *****************************************************************************
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
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

#ifndef _LICENSES_H
#define _LICENSES_H

#include "src/common/list.h"
#include "src/slurmctld/slurmctld.h"

typedef struct licenses {
	char *		name;		/* name associated with a license */
	uint32_t	total;		/* total license configued */
	uint32_t	used;		/* used licenses */
	uint8_t         remote;	        /* non-zero if remote (from database) */
} licenses_t;

extern List license_list;
extern List clus_license_list;
extern time_t last_license_update;

/* Get string of used license information. Caller must xfree return value */
extern char *get_licenses_used(void);

/* Initialize licenses on this system based upon slurm.conf */
extern int license_init(char *licenses);

/* Update licenses on this system based upon slurm.conf.
 * Remove all previously allocated licenses */
extern int license_update(char *licenses);

extern void license_add_remote(slurmdb_res_rec_t *rec);
extern void license_update_remote(slurmdb_res_rec_t *rec);
extern void license_remove_remote(slurmdb_res_rec_t *rec);
extern void license_sync_remote(List res_list);

/* Free memory associated with licenses on this system */
extern void license_free(void);

/* Free a license_t record (for use by list_destroy) */
extern void license_free_rec(void *x);

/*
 * license_job_copy - create a copy of a job's license list
 * IN license_list_src - job license list to be copied
 * RET a copy of the original job license list
 */
extern List license_job_copy(List license_list_src);

/*
 * license_job_get - Get the licenses required for a job
 * IN job_ptr - job identification
 * RET SLURM_SUCCESS or failure code
 */
extern int license_job_get(struct job_record *job_ptr);

/*
 * license_job_merge - The licenses from one job have just been merged into
 *	another job by appending one job's licenses to another, possibly
 *	including duplicate names. Reconstruct this job's licenses and
 *	license_list fields to eliminate duplicates.
 */
extern void license_job_merge(struct job_record *job_ptr);

/*
 * license_job_return - Return the licenses allocated to a job
 * IN job_ptr - job identification
 * RET SLURM_SUCCESS or failure code
 */
extern int license_job_return(struct job_record *job_ptr);

/*
 * license_job_test - Test if the licenses required for a job are available
 * IN job_ptr - job identification
 * IN when    - time to check
 * RET: SLURM_SUCCESS, EAGAIN (not available now), SLURM_ERROR (never runnable)
 */
extern int license_job_test(struct job_record *job_ptr, time_t when);

/*
 * license_validate - Test if the required licenses are valid
 * IN licenses - required licenses
 * OUT tres_req_cnt - appropriate counts for each requested gres
 * OUT valid - true if required licenses are valid and a sufficient number
 *             are configured (though not necessarily available now)
 * RET license_list, must be destroyed by caller
 */
extern List license_validate(char *licenses,
			     uint64_t *tres_req_cnt, bool *valid);

/*
 * license_list_overlap - test if there is any overlap in licenses
 *	names found in the two lists
 */
extern bool license_list_overlap(List list_1, List list_2);

/* pack_all_licenses()
 *
 * Get the licenses and the usage counters in the io buffer
 * to be sent out to the library
 */
extern void
get_all_license_info(char **buffer_ptr,
                     int *buffer_size,
                     uid_t uid,
                     uint16_t protocol_version);

/*
 * get_total_license_cnt - give me the total count of a given license name.
 *
 */
extern uint32_t get_total_license_cnt(char *name);

/*
 * lic_get_value_by_type - Return count of named licenses used by job
 * IN licenses - list containing licenses_t records
 * IN name - name of the license
 * RET number of licenses of the particular type used
 */
extern uint32_t license_get_total_cnt_from_list(List license_list, char *name);

/* node_read should be locked before coming in here
 * returns tres_str of the license_list.
 */
extern char *licenses_2_tres_str(List license_list);


/* node_read should be locked before coming in here
 * fills in tres_cnt of the license_list.
 * locked if assoc_mgr tres read lock is locked or not.
 */
extern void license_set_job_tres_cnt(List license_list,
				     uint64_t *tres_cnt,
				     bool locked);

#endif /* !_LICENSES_H */
