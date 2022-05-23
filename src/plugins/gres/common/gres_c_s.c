/*****************************************************************************\
 *  gres_c_s.c - common functions for shared gres plugins
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <sys/stat.h>

#include "gres_c_s.h"

List shared_info = NULL;

static gres_slurmd_conf_t *_create_shared_rec(
	gres_slurmd_conf_t *sharing_record, char *shared_name,
	gres_slurmd_conf_t *shared_record_in)
{
	gres_slurmd_conf_t *shared_record = xmalloc(sizeof(gres_slurmd_conf_t));
	shared_record->config_flags = sharing_record->config_flags;

	if (shared_record_in &&
	    gres_id_shared(shared_record_in->config_flags)) {
		shared_record->config_flags |= shared_record_in->config_flags;
	} else {
		shared_record->config_flags |= GRES_CONF_SHARED;
		/* The default for MPS is to have only one gpu sharing */
		if (!xstrcasecmp(shared_name, "mps"))
			shared_record->config_flags |= GRES_CONF_ONE_SHARING;
	}

	shared_record->cpu_cnt = sharing_record->cpu_cnt;
	shared_record->cpus = xstrdup(sharing_record->cpus);
	if (sharing_record->cpus_bitmap) {
		shared_record->cpus_bitmap =
			bit_copy(sharing_record->cpus_bitmap);
	}
	shared_record->file = xstrdup(sharing_record->file);
	shared_record->name = xstrdup(shared_name);
	shared_record->plugin_id = gres_build_id(shared_name);
	shared_record->type_name = xstrdup(sharing_record->type_name);
	return shared_record;
}

/* Distribute MPS Count to records on original list */
static void _distribute_count(List gres_conf_list, List sharing_conf_list,
			      uint64_t count,
			      gres_slurmd_conf_t *shared_record_in)
{
	gres_slurmd_conf_t *sharing_record, *shared_record;
	int rem_sharings = list_count(sharing_conf_list);
	while ((sharing_record = list_pop(sharing_conf_list))) {
		shared_record = _create_shared_rec(sharing_record,
						   shared_record_in->name,
						   shared_record_in);
		shared_record->count = count / rem_sharings;
		count -= shared_record->count;
		rem_sharings--;
		list_append(gres_conf_list, shared_record);

		list_append(gres_conf_list, sharing_record);
	}
}

static int _find_matching_file_gres(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf1 = x;
	gres_slurmd_conf_t *gres_slurmd_conf2 = arg;

	if (!xstrcmp(gres_slurmd_conf1->file, gres_slurmd_conf2->file))
		return 1;
	return 0;
}

static int _delete_leftovers(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;

	error("Discarding gres/'shared' configuration (File=%s) without matching gres/'sharing' record",
	      gres_slurmd_conf->file);
	return 1;
}

/* Merge SHARED records back to original list, updating and reordering as needed */
static int _merge_lists(List gres_conf_list, List sharing_conf_list,
			List shared_conf_list, char *shared_name)
{
	gres_slurmd_conf_t *sharing_record, *shared_record;

	if (!list_count(sharing_conf_list) && list_count(shared_conf_list)) {
		error("SHARED specified without any SHARING found");
		return SLURM_ERROR;
	}

	/*
	 * If gres/shared has Count, but no File specification, then evenly
	 * distribute gres/shared Count over all gres/sharing file records
	 */
	if (list_count(shared_conf_list) == 1) {
		shared_record = list_peek(shared_conf_list);
		if (!shared_record->file) {
			_distribute_count(gres_conf_list, sharing_conf_list,
					  shared_record->count, shared_record);
			list_flush(shared_conf_list);
			return SLURM_SUCCESS;
		}
	}

	/*
	 * Add SHARED records, matching File ordering to that of SHARING
	 * records
	 */
	while ((sharing_record = list_pop(sharing_conf_list))) {
		shared_record = list_remove_first(shared_conf_list,
						  _find_matching_file_gres,
						  sharing_record);
		if (shared_record) {
			/*
			 * Copy gres/sharing Type & CPU info to
			 * gres/shared
			 */
			if (sharing_record->type_name) {
				shared_record->config_flags |=
					GRES_CONF_HAS_TYPE;
			}
			if (sharing_record->cpus) {
				xfree(shared_record->cpus);
				shared_record->cpus =
					xstrdup(sharing_record->cpus);
			}
			if (sharing_record->cpus_bitmap) {
				shared_record->cpu_cnt =
					sharing_record->cpu_cnt;
				FREE_NULL_BITMAP(
					shared_record->cpus_bitmap);
				shared_record->cpus_bitmap =
					bit_copy(sharing_record->
						 cpus_bitmap);
			}
			xfree(shared_record->type_name);
			shared_record->type_name =
				xstrdup(sharing_record->type_name);
			xfree(shared_record->unique_id);
			shared_record->unique_id =
				xstrdup(sharing_record->unique_id);
			list_append(gres_conf_list, shared_record);
		} else {
			/* Add gres/shared record to match gres/gps record */
			shared_record = _create_shared_rec(
				sharing_record, shared_name, NULL);
			shared_record->count = 0;
			list_append(gres_conf_list, shared_record);
		}
		list_append(gres_conf_list, sharing_record);
	}

	/* Remove any remaining SHARED records (no matching File) */
	(void) list_delete_all(shared_conf_list, _delete_leftovers, NULL);

	return SLURM_SUCCESS;
}

/*
 * Return true if fake_sharings.conf does exist. Used for testing
 */
static bool _test_gpu_list_fake(void)
{
	struct stat config_stat;
	char *fake_gpus_file = NULL;
	bool have_fake_gpus = false;

	fake_gpus_file = get_extra_conf_path("fake_gpus.conf");
	if (stat(fake_gpus_file, &config_stat) >= 0) {
		have_fake_gpus = true;
	}
	xfree(fake_gpus_file);
	return have_fake_gpus;
}

/* Translate device file name to numeric index "/dev/nvidia2" -> 2 */
static int _compute_local_id(char *dev_file_name)
{
	int i, local_id = -1, mult = 1;

	if (!dev_file_name)
		return -1;

	for (i = strlen(dev_file_name) - 1; i >= 0; i--) {
		if ((dev_file_name[i] < '0') || (dev_file_name[i] > '9'))
			break;
		if (local_id == -1)
			local_id = 0;
		local_id += (dev_file_name[i] - '0') * mult;
		mult *= 10;
	}

	return local_id;
}

static uint64_t _build_shared_dev_info(List gres_conf_list)
{
	uint64_t shared_count = 0;
	gres_slurmd_conf_t *gres_slurmd_conf;
	shared_dev_info_t *shared_conf;
	ListIterator iter;

	FREE_NULL_LIST(shared_info);
	shared_info = list_create(xfree_ptr);
	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(iter))) {
		if (!gres_id_shared(gres_slurmd_conf->config_flags))
			continue;
		shared_conf = xmalloc(sizeof(shared_dev_info_t));
		shared_conf->count = gres_slurmd_conf->count;
		shared_conf->id = _compute_local_id(gres_slurmd_conf->file);
		list_append(shared_info, shared_conf);
		shared_count += gres_slurmd_conf->count;
	}
	list_iterator_destroy(iter);
	return shared_count;
}

/*
 * Count of gres/shared records is zero, remove them from GRES list sent to
 * slurmctld daemon.
 */
static int _remove_shared_recs(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	char *shared_name = arg;

	if (!xstrcmp(gres_slurmd_conf->name, shared_name))
		return 1;
	return 0;
}

/*
 * Convert all sharing records to a new entries in a list where each File is a
 * unique device (i.e. convert a record with "File=nvidia[0-3]" into 4 separate
 * records).
 */
static List _build_sharing_list(List gres_list, char *sharing_name)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_slurmd_conf, *sharing_record;
	List sharing_list;
	hostlist_t hl;
	char *f_name;
	bool log_fname = true;

	if (gres_list == NULL)
		return NULL;

	sharing_list = list_create(destroy_gres_slurmd_conf);
	itr = list_iterator_create(gres_list);
	while ((gres_slurmd_conf = list_next(itr))) {
		if (xstrcmp(gres_slurmd_conf->name, sharing_name))
			continue;
		if (!gres_slurmd_conf->file) {
			if (log_fname) {
				error("SHARING configuration lacks \"File\" specification");
				log_fname = false;
			}
			continue;
		}
		hl = hostlist_create(gres_slurmd_conf->file);
		while ((f_name = hostlist_shift(hl))) {
			sharing_record = xmalloc(sizeof(gres_slurmd_conf_t));
			sharing_record->config_flags =
				gres_slurmd_conf->config_flags;
			if (gres_slurmd_conf->type_name) {
				sharing_record->config_flags |=
					GRES_CONF_HAS_TYPE;
			}
			sharing_record->count = 1;
			sharing_record->cpu_cnt = gres_slurmd_conf->cpu_cnt;
			sharing_record->cpus = xstrdup(gres_slurmd_conf->cpus);
			if (gres_slurmd_conf->cpus_bitmap) {
				sharing_record->cpus_bitmap =
					bit_copy(gres_slurmd_conf->cpus_bitmap);
			}
			sharing_record->file = xstrdup(f_name);
			sharing_record->links =
				xstrdup(gres_slurmd_conf->links);
			sharing_record->name = xstrdup(gres_slurmd_conf->name);
			sharing_record->plugin_id = gres_slurmd_conf->plugin_id;
			sharing_record->type_name =
				xstrdup(gres_slurmd_conf->type_name);
			sharing_record->unique_id =
				xstrdup(gres_slurmd_conf->unique_id);
			list_append(sharing_list, sharing_record);
			free(f_name);
		}
		hostlist_destroy(hl);
		(void) list_delete_item(itr);
	}
	list_iterator_destroy(itr);

	return sharing_list;
}

/*
 * Convert all shared_name records to a new entries in a list where each File is
 * a unique device (i.e. convert a record with "File=nvidia[0-3]" into 4
 * separate records). Similar to _build_gpu_list(), but we copy more fields,
 * divide the "Count" across all shared_name records and remove from the
 * original list.
 */
static List _build_shared_list(List gres_list, char *shared_name)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_slurmd_conf, *shared_record;
	List shared_list;
	hostlist_t hl;
	char *f_name;
	uint64_t count_per_file;
	int shared_no_file_recs = 0, shared_file_recs = 0;

	if (gres_list == NULL)
		return NULL;

	shared_list = list_create(destroy_gres_slurmd_conf);
	itr = list_iterator_create(gres_list);
	while ((gres_slurmd_conf = list_next(itr))) {
		if (xstrcmp(gres_slurmd_conf->name, shared_name))
			continue;
		if (!gres_slurmd_conf->file) {
			if (shared_no_file_recs)
				fatal("%s: bad configuration, multiple configurations without \"File\"",
				      __func__);
			if (shared_file_recs)
				fatal("%s: multiple configurations with and without \"File\"",
				      __func__);
			shared_no_file_recs++;
			shared_record = xmalloc(sizeof(gres_slurmd_conf_t));
			shared_record->config_flags =
				gres_slurmd_conf->config_flags;
			if (gres_slurmd_conf->type_name)
				shared_record->config_flags |= GRES_CONF_HAS_TYPE;
			shared_record->count = gres_slurmd_conf->count;
			shared_record->cpu_cnt = gres_slurmd_conf->cpu_cnt;
			shared_record->cpus = xstrdup(gres_slurmd_conf->cpus);
			if (gres_slurmd_conf->cpus_bitmap) {
				shared_record->cpus_bitmap =
					bit_copy(gres_slurmd_conf->cpus_bitmap);
			}
			shared_record->name = xstrdup(gres_slurmd_conf->name);
			shared_record->plugin_id = gres_slurmd_conf->plugin_id;
			shared_record->type_name =
				xstrdup(gres_slurmd_conf->type_name);
			shared_record->unique_id =
				xstrdup(gres_slurmd_conf->unique_id);
			list_append(shared_list, shared_record);
		} else {
			shared_file_recs++;
			if (shared_no_file_recs)
				fatal("gres/shared: multiple configurations with and without \"File\"");
			hl = hostlist_create(gres_slurmd_conf->file);
			count_per_file =
				gres_slurmd_conf->count / hostlist_count(hl);
			while ((f_name = hostlist_shift(hl))) {
				shared_record =
					xmalloc(sizeof(gres_slurmd_conf_t));
				shared_record->config_flags =
					gres_slurmd_conf->config_flags;
				if (gres_slurmd_conf->type_name) {
					shared_record->config_flags |=
						GRES_CONF_HAS_TYPE;
				}
				shared_record->count = count_per_file;
				shared_record->cpu_cnt =
					gres_slurmd_conf->cpu_cnt;
				shared_record->cpus = xstrdup(
					gres_slurmd_conf->cpus);
				if (gres_slurmd_conf->cpus_bitmap) {
					shared_record->cpus_bitmap =
					     bit_copy(gres_slurmd_conf->
						      cpus_bitmap);
				}
				shared_record->file = xstrdup(f_name);
				shared_record->name = xstrdup(
					gres_slurmd_conf->name);
				shared_record->plugin_id =
					gres_slurmd_conf->plugin_id;
				shared_record->type_name =
					xstrdup(gres_slurmd_conf->type_name);
				shared_record->unique_id =
					xstrdup(gres_slurmd_conf->unique_id);
				list_append(shared_list, shared_record);
				free(f_name);
			}
			hostlist_destroy(hl);
		}
		(void) list_delete_item(itr);
	}
	list_iterator_destroy(itr);

	return shared_list;
}

extern void gres_c_s_fini(void)
{
	FREE_NULL_LIST(shared_info);
}

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
 * In the general case, no code would need to be changed.
 */
extern int gres_c_s_init_share_devices(List gres_conf_list,
				       List *share_devices,
				       node_config_load_t *config,
				       char *sharing_name,
				       char *shared_name)
{
	int rc = SLURM_SUCCESS;
	List sharing_conf_list, shared_conf_list;
	log_level_t log_lvl;

	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES)
		log_lvl = LOG_LEVEL_VERBOSE;
	else
		log_lvl = LOG_LEVEL_DEBUG;

	/* Assume this state is caused by an scontrol reconfigure */
	if (share_devices && *share_devices) {
		debug("Resetting share_devices");
		FREE_NULL_LIST(*share_devices);
	}

	log_flag(GRES, "Initalized gres.conf list");
	print_gres_list(gres_conf_list, log_lvl);

	/*
	 * Ensure that every SHARING device file is listed as a SHARED file.
	 * Any SHARED entry that we need to add will have a "Count" of zero.
	 * Every SHARED "Type" will be made to match the SHARING "Type". The
	 * order of SHARED records (by "File") must match the order in which
	 * SHARINGs are defined for the GRES bitmaps in slurmctld to line up.
	 *
	 * First, convert all SHARING records to a new entries in a list where
	 * each File is a unique device (i.e. convert a record with
	 * "File=nvidia[0-3]" into 4 separate records).
	 */
	sharing_conf_list = _build_sharing_list(
		gres_conf_list, sharing_name);

	/* Now move SHARED records to new List, each with unique device file */
	shared_conf_list = _build_shared_list(
		gres_conf_list, shared_name);

	/*
	 * Merge SHARED records back to original list, updating and reordering
	 * as needed.
	 */
	rc = _merge_lists(gres_conf_list, sharing_conf_list, shared_conf_list,
			  shared_name);
	FREE_NULL_LIST(sharing_conf_list);
	FREE_NULL_LIST(shared_conf_list);
	if (rc != SLURM_SUCCESS)
		fatal("failed to merge SHARED and SHARING configuration");

	rc = common_node_config_load(gres_conf_list, shared_name, config,
				     share_devices);
	if (rc != SLURM_SUCCESS)
		fatal("failed to load configuration");
	if (!_build_shared_dev_info(gres_conf_list) && gres_conf_list)
		(void) list_delete_all(gres_conf_list, _remove_shared_recs,
				       shared_name);

	log_var(log_lvl, "Final gres.conf list:");
	print_gres_list(gres_conf_list, log_lvl);

	// Print in parsable format for tests if fake system is in use
	if (_test_gpu_list_fake()) {
		info("Final normalized gres.conf list (parsable):");
		print_gres_list_parsable(gres_conf_list);
	}

	return rc;
}

extern void gres_c_s_send_stepd(buf_t *buffer)
{
	uint32_t shared_cnt;
	shared_dev_info_t *shared_ptr;
	ListIterator itr;

	if (!shared_info) {
		shared_cnt = 0;
		pack32(shared_cnt, buffer);
	} else {
		shared_cnt = list_count(shared_info);
		pack32(shared_cnt, buffer);
		itr = list_iterator_create(shared_info);
		while ((shared_ptr = list_next(itr))) {
			pack64(shared_ptr->count, buffer);
			pack64(shared_ptr->id, buffer);
		}
		list_iterator_destroy(itr);
	}
	return;
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void gres_c_s_recv_stepd(buf_t *buffer)
{
	shared_dev_info_t *shared_ptr = NULL;
	uint64_t uint64_tmp;
	uint32_t shared_cnt;

	safe_unpack32(&shared_cnt, buffer);
	if (!shared_cnt)
		return;

	FREE_NULL_LIST(shared_info);
	shared_info = list_create(xfree_ptr);
	for (uint32_t i = 0; i < shared_cnt; i++) {
		shared_ptr = xmalloc(sizeof(shared_dev_info_t));
		safe_unpack64(&uint64_tmp, buffer);
		shared_ptr->count = uint64_tmp;
		safe_unpack64(&uint64_tmp, buffer);
		shared_ptr->id = uint64_tmp;
		list_append(shared_info, shared_ptr);
	}
	return;

unpack_error:
	error("failed");
	xfree(shared_ptr);
	return;
}
