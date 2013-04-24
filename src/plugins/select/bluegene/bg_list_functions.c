/*****************************************************************************\
 *  bg_list_functions.c - header for dealing with the lists that
 *                        contain bg_records.
 *
 *  $Id: bg_list_functions.c 12954 2008-01-04 20:37:49Z da $
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "bg_core.h"

/* see if a record already of like bitmaps exists in a list */
extern bg_record_t *block_exist_in_list(List my_list, bg_record_t *bg_record)
{
	ListIterator itr;
	bg_record_t *found_record = NULL;

	if (!my_list || !bg_record)
		return NULL;

	itr = list_iterator_create(my_list);
	while ((found_record = list_next(itr))) {
		if (found_record->magic != BLOCK_MAGIC)
			continue;
		/* check for full node bitmap compare */
		if (bit_equal(bg_record->mp_bitmap, found_record->mp_bitmap)
		    && bit_equal(bg_record->ionode_bitmap,
				 found_record->ionode_bitmap)) {
			/* now make sure the conn_type is the same for
			   regular sized blocks */
			if (bg_record->cnode_cnt >= bg_conf->mp_cnode_cnt) {
				int dim;
				for (dim=0; dim<SYSTEM_DIMENSIONS; dim++) {
					if (bg_record->conn_type[dim]
					    != found_record->conn_type[dim])
						break;
				}
				if (dim != SYSTEM_DIMENSIONS)
					continue;
			}

			if (bg_record->ionode_str)
				debug("This block %s[%s] "
				      "is already in the list %s",
				      bg_record->mp_str,
				      bg_record->ionode_str,
				      found_record->bg_block_id);
			else
				debug("This block %s "
				      "is already in the list %s",
				      bg_record->mp_str,
				      found_record->bg_block_id);
			break;
		}
	}
	list_iterator_destroy(itr);
	return found_record;
}

/* see if the exact record already exists in a list */
extern int block_ptr_exist_in_list(List my_list, bg_record_t *bg_record)
{
	ListIterator itr = NULL;
	bg_record_t *found_record = NULL;
	int rc = 0;

	if (!my_list || !bg_record)
		return rc;

	itr = list_iterator_create(my_list);
	while ((found_record = list_next(itr))) {
		if (bg_record == found_record) {
			rc = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	return rc;
}

/* if looking at the main list this should have some nice
 * block_state_mutex locks around it.
 */
extern bg_record_t *find_bg_record_in_list(List my_list,
					   const char *bg_block_id)
{
	ListIterator itr;
	bg_record_t *bg_record = NULL;

	xassert(my_list);

	if (!bg_block_id)
		return NULL;

	itr = list_iterator_create(my_list);
	while ((bg_record = list_next(itr))) {
		if (bg_record->bg_block_id && (bg_record->magic == BLOCK_MAGIC))
			if (!strcasecmp(bg_record->bg_block_id, bg_block_id))
				break;
	}
	list_iterator_destroy(itr);

	if (bg_record)
		return bg_record;
	else
		return NULL;
}

/* must set the protecting mutex if any before this function is called */

extern int remove_from_bg_list(List my_list, bg_record_t *bg_record)
{
	bg_record_t *found_record = NULL;
	ListIterator itr;
	int rc = SLURM_ERROR;

	if (!bg_record)
		return rc;

	//slurm_mutex_lock(&block_state_mutex);
	itr = list_iterator_create(my_list);
	while ((found_record = list_next(itr))) {
		if (found_record->magic == BLOCK_MAGIC)
			if (bg_record == found_record) {
				list_remove(itr);
				rc = SLURM_SUCCESS;
				break;
			}
	}
	list_iterator_destroy(itr);
	//slurm_mutex_unlock(&block_state_mutex);

	return rc;
}

/* This is here to remove from the orignal list when dealing with
 * copies like above all locks need to be set.  This function does not
 * free anything you must free it when you are done */
extern bg_record_t *find_and_remove_org_from_bg_list(List my_list,
						     bg_record_t *bg_record)
{
	ListIterator itr = list_iterator_create(my_list);
	bg_record_t *found_record = NULL;

	while ((found_record = list_next(itr))) {
		if (found_record->magic != BLOCK_MAGIC)
			continue;

		/* check for full node bitmap compare */
		if (bit_equal(bg_record->mp_bitmap, found_record->mp_bitmap)
		    && bit_equal(bg_record->ionode_bitmap,
				 found_record->ionode_bitmap)) {
			if (!strcmp(bg_record->bg_block_id,
				    found_record->bg_block_id)) {
				list_remove(itr);
				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_SELECT_TYPE)
					info("got the block");
				break;
			}
		}
	}
	list_iterator_destroy(itr);
	return found_record;
}

/* This is here to remove from the orignal list when dealing with
 * copies like above all locks need to be set */
extern bg_record_t *find_org_in_bg_list(List my_list, bg_record_t *bg_record)
{
	ListIterator itr = list_iterator_create(my_list);
	bg_record_t *found_record = NULL;

	while ((found_record = list_next(itr))) {
		if (found_record->magic != BLOCK_MAGIC)
			continue;
		/* check for full node bitmap compare */
		if (bit_equal(bg_record->mp_bitmap, found_record->mp_bitmap)
		    && bit_equal(bg_record->ionode_bitmap,
				 found_record->ionode_bitmap)) {

			if (!strcmp(bg_record->bg_block_id,
				    found_record->bg_block_id)) {
				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_SELECT_TYPE)
					info("got the block");
				break;
			}
		}
	}
	list_iterator_destroy(itr);
	return found_record;
}

/* Check to see if a job has been added to the bg_record NO_VAL
 * returns the first one on the list. */
extern struct job_record *find_job_in_bg_record(bg_record_t *bg_record,
						uint32_t job_id)
{
	ListIterator itr;
	struct job_record *job_ptr;

	xassert(bg_record);

	if (!bg_record->job_list)
		return NULL;
	itr = list_iterator_create(bg_record->job_list);
	while ((job_ptr = list_next(itr))) {
		if (job_ptr->magic != JOB_MAGIC) {
			error("find_job_in_bg_record: "
			      "bad magic found when "
			      "looking at block %s",
			      bg_record->bg_block_id);
			list_delete_item(itr);
			continue;
		}

		if ((job_ptr->job_id == job_id) || (job_id == NO_VAL))
			break;
	}
	list_iterator_destroy(itr);
	return job_ptr;
}
