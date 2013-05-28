/*****************************************************************************\
 *  defined_block.c - functions for creating blocks in a static environment.
 *
 *  $Id: defined_block.c 12954 2008-01-04 20:37:49Z da $
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "bg_defined_block.h"

/*
 * create_defined_blocks - create the static blocks that will be used
 * for scheduling, all partitions must be able to be created and booted
 * at once.
 * IN - int overlapped, 1 if partitions are to be overlapped, 0 if they are
 * static.
 * RET - success of fitting all configurations
 */
extern int create_defined_blocks(bg_layout_t overlapped,
				 List bg_found_block_list)
{
	int rc = SLURM_SUCCESS;

	ListIterator itr;
	bg_record_t *bg_record = NULL;
	int i;
	char temp[256];
	struct part_record *part_ptr = NULL;
	bitstr_t *usable_mp_bitmap = bit_alloc(node_record_count);

	/* Locks are already in place to protect part_list here */
	itr = list_iterator_create(part_list);
	while ((part_ptr = list_next(itr))) {
		/* we only want to use mps that are in
		 * partitions
		 */
		if (!part_ptr->node_bitmap) {
			debug4("Partition %s doesn't have any nodes in it.",
			       part_ptr->name);
			continue;
		}
		bit_or(usable_mp_bitmap, part_ptr->node_bitmap);
	}
	list_iterator_destroy(itr);

	if (bit_ffs(usable_mp_bitmap) == -1) {
		fatal("We don't have any nodes in any partitions.  "
		      "Can't create blocks.  "
		      "Please check your slurm.conf.");
	}

	slurm_mutex_lock(&block_state_mutex);
	reset_ba_system(false);
	ba_set_removable_mps(usable_mp_bitmap, 1);
	if (bg_lists->main) {
		itr = list_iterator_create(bg_lists->main);
		while ((bg_record = list_next(itr))) {
			/* If we are deleting old blocks they will
			   have been added to the main list, so we
			   want to skip over them.
			*/
			if (bg_record->free_cnt)
				continue;
			if (bg_record->mp_count > 0
			    && !bg_record->full_block
			    && bg_record->cpu_cnt >= bg_conf->cpus_per_mp) {
				char *name = NULL;
				char start_char[SYSTEM_DIMENSIONS+1];
				char geo_char[SYSTEM_DIMENSIONS+1];

				if (overlapped == LAYOUT_OVERLAP) {
					reset_ba_system(false);
					ba_set_removable_mps(usable_mp_bitmap,
							     1);
				}

				/* we want the mps that aren't
				 * in this record to mark them as used
				 */
				if (ba_set_removable_mps(
					    bg_record->mp_bitmap, 1)
				    != SLURM_SUCCESS)
					fatal("It doesn't seem we have a "
					      "bitmap for %s",
					      bg_record->bg_block_id);

				for (i=0; i<SYSTEM_DIMENSIONS; i++) {
					start_char[i] = alpha_num[
						bg_record->start[i]];
					geo_char[i] = alpha_num[
						bg_record->geo[i]];
				}
				start_char[i] = '\0';
				geo_char[i] = '\0';

				debug2("adding %s %s %s",
				       bg_record->mp_str,
				       start_char, geo_char);
				if (bg_record->ba_mp_list
				    && list_count(bg_record->ba_mp_list)) {
					if ((rc = check_and_set_mp_list(
						     bg_record->ba_mp_list))
					    != SLURM_SUCCESS) {
						error("Something happened in "
						      "the load of %s.  "
						      "Did you use smap to "
						      "make the "
						      "bluegene.conf file?",
						       bg_record->bg_block_id);
						break;
					}
					ba_reset_all_removed_mps();
				} else {
#ifdef HAVE_BGQ
					List results =
						list_create(destroy_ba_mp);
#else
					List results = list_create(NULL);
#endif
					select_ba_request_t ba_request;
					memset(&ba_request, 0,
					       sizeof(ba_request));
					memcpy(ba_request.geometry,
					       bg_record->geo,
					       sizeof(bg_record->geo));
					memcpy(ba_request.conn_type,
					       bg_record->conn_type,
					       sizeof(bg_record->conn_type));
					name = set_bg_block(results,
							    &ba_request);
					ba_reset_all_removed_mps();
					if (!name) {
						error("I was unable to "
						      "make the "
						      "requested block.");
						list_destroy(results);
						rc = SLURM_ERROR;
						break;
					}

					snprintf(temp, sizeof(temp), "%s%s",
						 bg_conf->slurm_node_prefix,
						 name);

					xfree(name);
					if (strcmp(temp, bg_record->mp_str)) {
						fatal("given list of %s "
						      "but allocated %s, "
						      "your order might be "
						      "wrong in bluegene.conf",
						      bg_record->mp_str,
						      temp);
					}
					if (bg_record->ba_mp_list)
						list_destroy(
							bg_record->ba_mp_list);
#ifdef HAVE_BGQ
					bg_record->ba_mp_list = results;
					results = NULL;
					memcpy(bg_record->start,
					       ba_request.start,
					       sizeof(bg_record->start));
#else
					bg_record->ba_mp_list =
						list_create(destroy_ba_mp);
					copy_node_path(results,
						       &bg_record->ba_mp_list);
					list_destroy(results);
#endif
				}
			}
			if (!block_exist_in_list(
				    bg_found_block_list, bg_record)) {
				if (bg_record->full_block) {
					/* if this is defined we need
					   to remove it since we are
					   going to try to create it
					   later on overlap systems
					   this doesn't matter, but
					   since we don't clear the
					   table on static mode we
					   can't do it here or it just
					   won't work since other
					   wires will be or are
					   already set
					*/
					list_remove(itr);
					continue;
				}
				if ((rc = bridge_block_create(bg_record))
				    != SLURM_SUCCESS)
					break;
				setup_subblock_structs(bg_record);
				print_bg_record(bg_record);
			}
		}
		list_iterator_destroy(itr);
		if (rc != SLURM_SUCCESS)
			goto end_it;
	} else {
		error("create_defined_blocks: no bg_lists->main 2");
		rc = SLURM_ERROR;
		goto end_it;
	}
	slurm_mutex_unlock(&block_state_mutex);
	create_full_system_block(bg_found_block_list);

	slurm_mutex_lock(&block_state_mutex);
	sort_bg_record_inc_size(bg_lists->main);

end_it:
	ba_reset_all_removed_mps();
	FREE_NULL_BITMAP(usable_mp_bitmap);
	slurm_mutex_unlock(&block_state_mutex);

#ifdef _PRINT_BLOCKS_AND_EXIT
	if (bg_lists->main) {
		itr = list_iterator_create(bg_lists->main);
		debug("\n\n");
		while ((found_record = (bg_record_t *) list_next(itr))
		       != NULL) {
			/* If we are deleting old blocks they will
			   have been added to the main list, so we
			   want to skip over them.
			*/
			if (found_record->free_cnt)
				continue;
			print_bg_record(found_record);
		}
		list_iterator_destroy(itr);
	} else {
		error("create_defined_blocks: no bg_lists->main 5");
	}
 	exit(0);
#endif	/* _PRINT_BLOCKS_AND_EXIT */
	//exit(0);
	return rc;
}

extern int create_full_system_block(List bg_found_block_list)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	bg_record_t *bg_record = NULL;
	char *name = NULL;
	List records = NULL;
	uint16_t geo[SYSTEM_DIMENSIONS];
	int i;
	select_ba_request_t blockreq;
	List results = NULL;
	struct part_record *part_ptr = NULL;
	bitstr_t *bitmap = bit_alloc(node_record_count);
	static int *dims = NULL;
	bool larger = 0;
	char start_char[SYSTEM_DIMENSIONS+1];
	char geo_char[SYSTEM_DIMENSIONS+1];
	select_ba_request_t ba_request;

	if (!dims) {
		dims = select_g_ba_get_dims();
		memset(start_char, 0, sizeof(start_char));
		memset(geo_char, 0, sizeof(geo_char));
	}

	/* Locks are already in place to protect part_list here */
	itr = list_iterator_create(part_list);
	while ((part_ptr = list_next(itr))) {
		/* we only want to use mps that are in
		 * partitions
		 */
		if (!part_ptr->node_bitmap) {
			debug4("Partition %s doesn't have any nodes in it.",
			       part_ptr->name);
			continue;
		}
		bit_or(bitmap, part_ptr->node_bitmap);
	}
	list_iterator_destroy(itr);

	bit_not(bitmap);
	if (bit_ffs(bitmap) != -1) {
		error("We don't have the entire system covered by partitions, "
		      "can't create full system block");
		FREE_NULL_BITMAP(bitmap);
		return SLURM_ERROR;
	}
	FREE_NULL_BITMAP(bitmap);

	/* Here we are adding a block that in for the entire machine
	   just in case it isn't in the bluegene.conf file.
	*/
	slurm_mutex_lock(&block_state_mutex);

	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		geo[i] = dims[i] - 1;
		if (geo[i] > 0)
			larger = 1;
		geo_char[i] = alpha_num[geo[i]];
		start_char[i] = alpha_num[0];
	}

	if (!larger)
		name = xstrdup_printf("%s%s",
				      bg_conf->slurm_node_prefix, start_char);
	else
		name = xstrdup_printf("%s[%sx%s]",
				      bg_conf->slurm_node_prefix,
				      start_char, geo_char);

	if (bg_found_block_list) {
		itr = list_iterator_create(bg_found_block_list);
		while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
			/* Skip all small blocks since they are not
			   the full system block.
			*/
			if (bg_record->cnode_cnt < bg_conf->mp_cnode_cnt)
				continue;

			if (!strcmp(name, bg_record->mp_str)) {
				xfree(name);
				list_iterator_destroy(itr);
				/* don't create total already there */
				goto no_total;
			}
		}
		list_iterator_destroy(itr);
	} else {
		error("create_full_system_block: no bg_found_block_list 2");
	}

	if (bg_lists->main) {
		itr = list_iterator_create(bg_lists->main);
		while ((bg_record = (bg_record_t *) list_next(itr))
		       != NULL) {
			/* If we are deleting old blocks they will
			   have been added to the main list, so we
			   want to skip over them.
			*/
			if (bg_record->free_cnt)
				continue;

			/* Skip all small blocks since they are not
			   the full system block.
			*/
			if (bg_record->cnode_cnt < bg_conf->mp_cnode_cnt)
				continue;

			if (!strcmp(name, bg_record->mp_str)) {
				debug2("create_full_system_block: not "
				       "implicitly adding full system block -"
				       " block already defined");
				xfree(name);
				list_iterator_destroy(itr);
				/* don't create total already there */
				goto no_total;
			}
		}
		list_iterator_destroy(itr);
	} else {
		xfree(name);
		error("create_overlapped_blocks: no bg_lists->main 3");
		rc = SLURM_ERROR;
		goto no_total;
	}

	records = list_create(destroy_bg_record);

	memset(&blockreq, 0, sizeof(select_ba_request_t));
	blockreq.save_name = name;
	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		blockreq.conn_type[i] = SELECT_TORUS;

	add_bg_record(records, NULL, &blockreq, 0 , 0);
	xfree(name);

	bg_record = (bg_record_t *) list_pop(records);
	if (!bg_record) {
		error("Nothing was returned from full system create");
		rc = SLURM_ERROR;
		goto no_total;
	}
	reset_ba_system(false);
	for(i=0; i<SYSTEM_DIMENSIONS; i++) {
		geo_char[i] = alpha_num[bg_record->geo[i]];
		start_char[i] = alpha_num[bg_record->start[i]];
	}
	debug2("adding %s %s %s",  bg_record->mp_str, start_char, geo_char);
	if (bg_record->ba_mp_list)
		list_flush(bg_record->ba_mp_list);
	else
		bg_record->ba_mp_list = list_create(destroy_ba_mp);
#ifdef HAVE_BGQ
	results = list_create(destroy_ba_mp);
#else
	results = list_create(NULL);
#endif
	memset(&ba_request, 0, sizeof(ba_request));
	memcpy(ba_request.start, bg_record->start, sizeof(bg_record->start));
	memcpy(ba_request.geometry, bg_record->geo, sizeof(bg_record->geo));
	memcpy(ba_request.conn_type, bg_record->conn_type,
	       sizeof(bg_record->conn_type));
	ba_request.start_req = 1;
	name = set_bg_block(results, &ba_request);

	if (!name) {
		error("I was unable to make the full system block.");
		list_destroy(results);
		list_iterator_destroy(itr);
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
	xfree(name);
	if (bg_record->ba_mp_list)
		list_destroy(bg_record->ba_mp_list);
#ifdef HAVE_BGQ
	bg_record->ba_mp_list = results;
	results = NULL;
#else
	bg_record->ba_mp_list = list_create(destroy_ba_mp);
	copy_node_path(results, &bg_record->ba_mp_list);
	list_destroy(results);
#endif
	if ((rc = bridge_block_create(bg_record)) == SLURM_ERROR) {
		error("create_full_system_block: "
		      "unable to configure block in api");
		destroy_bg_record(bg_record);
		goto no_total;
	}

	setup_subblock_structs(bg_record);
	print_bg_record(bg_record);
	list_append(bg_lists->main, bg_record);

no_total:
	if (records)
		list_destroy(records);
	slurm_mutex_unlock(&block_state_mutex);
	return rc;
}
