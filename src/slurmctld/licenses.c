/*****************************************************************************\
 *  licenses.c - Functions for handling cluster-wide consumable resources
 *****************************************************************************
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/data_parser.h"
#include "src/interfaces/serializer.h"

#include "src/slurmctld/licenses.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"

list_t *cluster_license_list = NULL;
uint16_t next_lic_id = 0;
time_t last_license_update = 0;
bool preempt_for_licenses = false;
static pthread_mutex_t license_mutex = PTHREAD_MUTEX_INITIALIZER;
static void _pack_license(licenses_t *lic, buf_t *buffer,
			  uint16_t protocol_version);

typedef struct {
	licenses_id_t id;
	slurmctld_resv_t *resv_ptr;
} bf_licenses_find_resv_t;

typedef struct {
	job_record_t *job_ptr;
	list_t *license_list;
	int rc;
	bool reboot;
	time_t when;
} license_test_args_t;

typedef struct {
	char *header;
	job_record_t *job_ptr;
} foreach_license_print_t;

typedef struct {
	char *name;
	char *nodes;
} licenses_find_rec_by_nodes_t;

typedef struct {
	licenses_t *license_entry;
	job_record_t *job_ptr;
} foreach_get_hres_t;

typedef struct {
	uint32_t *count;
	char *name;
} foreach_get_total_t;

typedef struct {
	licenses_t *license_entry;
	bitstr_t *node_mask;
} foreach_hres_filter_t;

typedef struct {
	list_t *license_list;
	bitstr_t *node_bitmap;
} hres_filter_args_t;

typedef struct {
	bf_licenses_t *bf_license_list;
	bitstr_t *node_bitmap;
} bf_hres_filter_args_t;

typedef struct {
	job_record_t *job_ptr;
	list_t *license_list;
	bool locked;
} license_return_args_t;

static int _foreach_license_print(void *x, void *arg)
{
	licenses_t *license_entry = x;
	foreach_license_print_t *args = arg;

	if (license_entry->id.hres_id != NO_VAL16) {
		info("licenses: %s=%s lic_id=%u hres_id=%u mode=%u nodes:%s total=%u used=%u",
		     args->header, license_entry->name, license_entry->id.lic_id,
		     license_entry->id.hres_id, license_entry->mode,
		     license_entry->nodes, license_entry->total,
		     license_entry->used);
	} else if (!args->job_ptr) {
		info("licenses: %s=%s lic_id=%u total=%u used=%u",
		     args->header, license_entry->name, license_entry->id.lic_id,
		     license_entry->total, license_entry->used);
	} else {
		info("licenses: %s=%s lic_id=%u %pJ available=%u used=%u",
		     args->header, license_entry->name, license_entry->id.lic_id,
		     args->job_ptr, license_entry->total, license_entry->used);
	}

	return 0;
}

/* Print all licenses on a list */
static void _licenses_print(char *header, list_t *licenses,
			    job_record_t *job_ptr)
{
	foreach_license_print_t args = { .header = header, .job_ptr = job_ptr };
	if (licenses == NULL)
		return;
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_LICENSE))
		return;
	list_for_each(licenses, _foreach_license_print, &args);
}

/* Free a license_t record (for use by FREE_NULL_LIST) */
extern void license_free_rec(void *x)
{
	licenses_t *license_entry = (licenses_t *) x;

	if (license_entry) {
		xfree(license_entry->name);
		FREE_NULL_BITMAP(license_entry->node_bitmap);
		xfree(license_entry->nodes);
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
	if (xstrcmp(license_entry->name, name))
		return 0;
	return 1;
}

/* Find a license_t record by license name (for use by list_find_first) */
static int _license_find_rec_by_nodes(void *x, void *key)
{
	licenses_t *license_entry = (licenses_t *) x;
	licenses_find_rec_by_nodes_t *target = key;

	if ((license_entry->name == NULL) || (target->name == NULL))
		return 0;
	if (xstrcmp(license_entry->name, target->name))
		return 0;
	if (xstrcmp(license_entry->nodes, target->nodes))
		return 0;
	return 1;
}

/*
 * Find a license_t record by license id (for use by list_find_first)
 */
static int _license_find_rec_by_id(void *x, void *key)
{
	licenses_t *license_entry = x;
	licenses_id_t *id = key;

	xassert(id->lic_id != NO_VAL16);

	if (license_entry->id.lic_id == id->lic_id)
		return 1;
	return 0;
}

/*
 * Find a license_t record that does NOT match license id. This is the inverse
 * of _license_find_rec_by_id
 */
static int _license_find_rec_by_id_not(void *x, void *key)
{
	return !_license_find_rec_by_id(x, key);
}

static int _license_find_rec_in_list_by_id(void *x, void *key)
{
	licenses_t *license_entry = x;
	list_t *licenses = key;
	if (list_find_first_ro(licenses, _license_find_rec_by_id,
			       &(license_entry->id))) {
		return 1;
	}
	return 0;
}

/* Find a license_t record by license name (for use by list_find_first) */
static int _license_find_remote_rec(void *x, void *key)
{
	licenses_t *license_entry = (licenses_t *) x;

	if (!license_entry->remote)
		return 0;
	return _license_find_rec(x, key);
}

/* Given a license string, return a list of license_t records */
static list_t *_build_license_list(char *licenses, bool *valid, bool hres)
{
	int i;
	char *end_num, *tmp_str, *token, *last;
	char *delim = hres ? ";" : ",;";
	licenses_t *license_entry;
	list_t *lic_list;

	*valid = true;
	if ((licenses == NULL) || (licenses[0] == '\0'))
		return NULL;

	if (strchr(licenses, '|')) {
		if (strchr(licenses, ',') || strchr(licenses, ';')) {
			/* Both OR and AND requested, invalid */
			*valid = false;
			return NULL;
		}
		delim = "|";
	}

	lic_list = list_create(license_free_rec);
	tmp_str = xstrdup(licenses);
	token = strtok_r(tmp_str, delim, &last);
	while (token && *valid) {
		int32_t num = 1;
		char *nodes = NULL;
		char *name = token;
		for (i = 0; token[i]; i++) {
			if (isspace(token[i])) {
				*valid = false;
				break;
			}

			if ((token[i] == '(') && hres) {
				token[i++] = '\0';
				nodes = &(token[i]);
				token = strchr(nodes, ')');
				if (!token) {
					*valid = false;
					break;
				}
				i = 0;
				token[i++] = '\0';
			}

			if ((token[i] == ':') ||
			    (token[i] == '=')) {
				token[i++] = '\0';
				num = (int32_t)strtol(&token[i], &end_num, 10);
				if (*end_num != '\0')
					 *valid = false;
				break;
			}
		}
		if (num < 0 || !(*valid)) {
			*valid = false;
			break;
		}

		license_entry =
			list_find_first(lic_list, _license_find_rec, name);
		if (license_entry && !nodes) {
			license_entry->total += num;
		} else {
			license_entry = xmalloc(sizeof(licenses_t));
			license_entry->id.lic_id = NO_VAL16;
			license_entry->id.hres_id = NO_VAL16;
			license_entry->name = xstrdup(name);
			license_entry->nodes = xstrdup(nodes);
			license_entry->total = num;
			if (delim[0] == '|')
				license_entry->op_or = true;
			/* Append to preserve the order requested by the user */
			list_append(lic_list, license_entry);
		}
		token = strtok_r(NULL, delim, &last);
	}
	xfree(tmp_str);

	if (*valid == false) {
		FREE_NULL_LIST(lic_list);
	}
	return lic_list;
}

/*
 * Given a list of license_t records, return a license string.
 *
 * This can be combined with _build_license_list() to eliminate duplicates
 *
 * IN license_list - list of license_t records
 *
 * RET string representation of licenses. Must be destroyed by caller.
 */
extern char *license_list_to_string(list_t *license_list)
{
	char *sep = "";
	char *licenses = NULL;
	list_itr_t *iter;
	licenses_t *license_entry;

	if (!license_list)
		return licenses;

	iter = list_iterator_create(license_list);
	while ((license_entry = list_next(iter))) {
		if (license_entry->nodes)
			xstrfmtcat(licenses, "%s%s(%s):%u", sep,
				   license_entry->name, license_entry->nodes,
				   license_entry->total);
		else
			xstrfmtcat(licenses, "%s%s:%u", sep,
				   license_entry->name, license_entry->total);
		sep = license_entry->op_or ? "|" : ";";
	}
	list_iterator_destroy(iter);

	return licenses;
}

static void _handle_consumed(licenses_t *license_entry, slurmdb_res_rec_t *rec)
{
	uint32_t external = 0;

	if (rec->flags & SLURMDB_RES_FLAG_ABSOLUTE) {
		license_entry->total = rec->clus_res_rec->allowed;
	} else {
		license_entry->total = ((rec->count *
					 rec->clus_res_rec->allowed) / 100);
	}

	if (license_entry->total > rec->count) {
		debug("allocated more licenses than exist total (%u > %u). this should not happen.",
		      license_entry->total, rec->count);
	} else
		external = rec->count - license_entry->total;

	license_entry->last_consumed = rec->last_consumed;
	if (license_entry->last_consumed <= (external + license_entry->used)) {
		/*
		 * "Normal" operation - license consumption is below what the
		 * local cluster, plus possible use from other clusters,
		 * have assigned out. No deficit in this case.
		 */
		license_entry->last_deficit = 0;
	} else {
		/*
		 * "Deficit" operation. Someone is using licenses that aren't
		 * included in our local tracking, and exceed that available
		 * to other clusters. So... we need to adjust our scheduling
		 * behavior here to avoid over-allocating licenses.
		 */
		license_entry->last_deficit = license_entry->last_consumed;
		license_entry->last_deficit -= external;
		license_entry->last_deficit -= license_entry->used;
	}
	license_entry->last_update = rec->last_update;
}

/* license_mutex should be locked before calling this. */
static void _add_res_rec_2_lic_list(slurmdb_res_rec_t *rec, bool sync)
{
	licenses_t *license_entry = xmalloc(sizeof(licenses_t));

	license_entry->name = xstrdup_printf("%s@%s", rec->name, rec->server);
	license_entry->remote = sync ? 2 : 1;
	_handle_consumed(license_entry, rec);

	license_entry->id.lic_id = next_lic_id++;
	xassert(license_entry->id.lic_id != NO_VAL16);

	list_append(cluster_license_list, license_entry);
	last_license_update = time(NULL);
}

static int _foreach_license_set_id(void *x, void *key)
{
	licenses_t *license = x;

	if (license->id.lic_id == NO_VAL16) {
		license->id.lic_id = next_lic_id++;
	}

	if (license->id.lic_id == NO_VAL16)
		return 1;

	return 0;
}

static void _set_license_ids(void)
{
	if (!cluster_license_list) {
		/* No licenses, nothing to do */
		return;
	}

	if (list_for_each(cluster_license_list, _foreach_license_set_id, NULL) <
	    0)
		fatal("Can't set lic_id");
}

static bool _sufficient_licenses(licenses_t *request, licenses_t *match,
				 int resv_licenses)
{
	return (request->total + match->used + match->last_deficit +
		resv_licenses) <= match->total;
}

static void _parse_hierarchical_resources(list_t **license_list_ptr)
{
	char *resources_conf = get_extra_conf_path("resources.yaml");
	struct stat stat_buf;

	xassert(license_list_ptr);

	/* Parse hierarchical resources from resources.yaml if config exists */
	if (!stat(resources_conf, &stat_buf)) {
		int rc;
		buf_t *conf_buf = NULL;

		if (!*license_list_ptr)
			*license_list_ptr = list_create(license_free_rec);

		if (!(conf_buf = create_mmap_buf(resources_conf))) {
			fatal("Hierarchical resources could not be loaded from %s",
			      resources_conf);
		}
		DATA_PARSE_FROM_STR(H_RESOURCES_AS_LICENSE_LIST, conf_buf->head,
				    conf_buf->size, *license_list_ptr, NULL,
				    MIME_TYPE_YAML, rc);
		if (rc)
			fatal("Something wrong with reading %s: %s",
			      resources_conf, slurm_strerror(rc));

		if ((slurm_conf.debug_flags & DEBUG_FLAG_LICENSE)) {
			char *dump_str = NULL;
			DATA_DUMP_TO_STR(H_RESOURCES_AS_LICENSE_LIST,
					 *license_list_ptr, dump_str, NULL,
					 MIME_TYPE_YAML, SER_FLAGS_NO_TAG, rc);
			if (rc)
				error("Hierarchical resources dump failed");
			verbose("Dump hierarchical resources:\n %s", dump_str);
			xfree(dump_str);
		}
		FREE_NULL_BUFFER(conf_buf);
	}

	xfree(resources_conf);
}

/* Initialize licenses on this system based upon slurm.conf */
extern int license_init(char *licenses)
{
	bool valid = true;

	if (xstrcasestr(slurm_conf.preempt_params, "reclaim_licenses"))
		preempt_for_licenses = true;

	last_license_update = time(NULL);

	slurm_mutex_lock(&license_mutex);
	if (cluster_license_list)
		fatal("cluster_license_list already defined");

	cluster_license_list = _build_license_list(licenses, &valid, false);
	if (!valid)
		fatal("Invalid configured licenses: %s", licenses);

	_parse_hierarchical_resources(&cluster_license_list);

	next_lic_id = 0;
	_set_license_ids();

	_licenses_print("init_license", cluster_license_list, NULL);
	slurm_mutex_unlock(&license_mutex);
	return SLURM_SUCCESS;
}

static int _foreach_license_set_hres(void *x, void *key)
{
	licenses_t *license = x;
	licenses_t *hres_head =
		list_find_first_ro(cluster_license_list, _license_find_rec,
				   license->name);
	if (license->nodes) {
		if (hres_head->mode != license->mode) {
			error("%s HRES Mode mismatch %s", __func__,
			      license->name);
			return -1;
		}

		if (license != hres_head)
			license->id.hres_id = hres_head->id.hres_id;
		else
			license->id.hres_id = license->id.lic_id;

		if (node_name2bitmap(license->nodes, false,
				     &license->node_bitmap, NULL))
			return -1;
	} else {
		xassert(license->mode == HRES_MODE_OFF);
		if (license != hres_head) {
			error("%s duplicate license %s", __func__,
			      license->name);
			return -1;
		}
		license->id.hres_id = NO_VAL16;
	}

	return 0;
}

static int _sort_hres(void *void1, void *void2)
{
	licenses_t *lic1 = *(licenses_t **) void1;
	licenses_t *lic2 = *(licenses_t **) void2;

	if (lic1->id.hres_id != lic2->id.hres_id)
		return 0;

	if (lic1->id.hres_id == NO_VAL16)
		return 0;

	if (lic1->mode != HRES_MODE_1)
		return 0;

	if (lic1->total > lic2->total)
		return -1;
	else if (lic1->total < lic2->total)
		return 1;

	return 0;
}

extern int hres_init()
{
	slurm_mutex_lock(&license_mutex);

	if (!cluster_license_list) {
		slurm_mutex_unlock(&license_mutex);
		return SLURM_SUCCESS;
	}

	last_license_update = time(NULL);

	if (list_for_each_ro(cluster_license_list, _foreach_license_set_hres,
			     NULL) < 0)
		fatal("Can't set hres_id or bitmap");
	list_sort(cluster_license_list, _sort_hres);
	_licenses_print("hres_init", cluster_license_list, NULL);
	slurm_mutex_unlock(&license_mutex);
	return SLURM_SUCCESS;
}

static int _foreach_hres_filter_mode1(void *x, void *arg)
{
	licenses_t *match = x;
	foreach_hres_filter_t *args = arg;

	if (match->id.hres_id != args->license_entry->id.hres_id)
		return 0;

	if (_sufficient_licenses(args->license_entry, match, 0))
		bit_or(args->node_mask, match->node_bitmap);

	return 0;
}

static int _foreach_hres_filter_mode2(void *x, void *arg)
{
	licenses_t *match = x;
	foreach_hres_filter_t *args = arg;

	if (match->id.hres_id != args->license_entry->id.hres_id)
		return 0;

	if (!_sufficient_licenses(args->license_entry, match, 0))
		bit_and_not(args->node_mask, match->node_bitmap);

	return 0;
}

static int _foreach_bf_hres_filter_mode1(void *x, void *arg)
{
	bf_license_t *bf_lic = x;
	foreach_hres_filter_t *args = arg;

	if (bf_lic->id.hres_id != args->license_entry->id.hres_id)
		return 0;

	if (args->license_entry->total <= bf_lic->remaining) {
		licenses_t *match =
			list_find_first(cluster_license_list,
					_license_find_rec_by_id, &bf_lic->id);
		bit_or(args->node_mask, match->node_bitmap);
	}

	return 0;
}

static int _foreach_bf_hres_filter_mode2(void *x, void *arg)
{
	bf_license_t *bf_lic = x;
	foreach_hres_filter_t *args = arg;

	if (bf_lic->id.hres_id != args->license_entry->id.hres_id)
		return 0;

	if (args->license_entry->total > bf_lic->remaining) {
		licenses_t *match =
			list_find_first(cluster_license_list,
					_license_find_rec_by_id, &bf_lic->id);
		bit_and_not(args->node_mask, match->node_bitmap);
	}

	return 0;
}

static int _foreach_hres_filter(void *x, void *arg)
{
	licenses_t *license_entry = x;
	hres_filter_args_t *args = arg;

	if (license_entry->id.hres_id != NO_VAL16) {
		bitstr_t *node_mask = bit_alloc(node_record_count);
		foreach_hres_filter_t arg2 = {
			.node_mask = node_mask,
			.license_entry = license_entry,
		};

		list_for_each_ro(args->license_list, _foreach_hres_filter_mode1,
				 &arg2);
		if (license_entry->mode == HRES_MODE_2)
			list_for_each_ro(args->license_list,
					 _foreach_hres_filter_mode2, &arg2);

		bit_and(args->node_bitmap, node_mask);
		FREE_NULL_BITMAP(node_mask);
		return 0;
	}

	return 0;
}

extern int hres_filter_with_list(job_record_t *job_ptr, bitstr_t *node_bitmap,
				 list_t *license_list)
{
	hres_filter_args_t filter_args = {
		.license_list = license_list,
		.node_bitmap = node_bitmap,
	};

	if (!job_ptr->license_list || !license_list)
		return SLURM_SUCCESS;

	list_for_each(job_ptr->license_list, _foreach_hres_filter,
		      &filter_args);
	return SLURM_SUCCESS;
}

extern int hres_filter(job_record_t *job_ptr, bitstr_t *node_bitmap)
{
	slurm_mutex_lock(&license_mutex);

	hres_filter_with_list(job_ptr, node_bitmap, cluster_license_list);

	slurm_mutex_unlock(&license_mutex);
	return SLURM_SUCCESS;
}

static int _foreach_bf_hres_filter(void *x, void *arg)
{
	licenses_t *license_entry = x;
	bf_hres_filter_args_t *args = arg;

	if (license_entry->id.hres_id != NO_VAL16) {
		bitstr_t *node_mask = bit_alloc(node_record_count);
		foreach_hres_filter_t arg2 = {
			.node_mask = node_mask,
			.license_entry = license_entry,
		};

		list_for_each_ro(args->bf_license_list,
				 _foreach_bf_hres_filter_mode1, &arg2);

		if (license_entry->mode == HRES_MODE_2)
			list_for_each_ro(args->bf_license_list,
					 _foreach_bf_hres_filter_mode2, &arg2);

		bit_and(args->node_bitmap, node_mask);
		FREE_NULL_BITMAP(node_mask);
		return 0;
	}

	return 0;
}

extern void slurm_bf_hres_filter(job_record_t *job_ptr, bitstr_t *node_bitmap,
				 bf_licenses_t *bf_license_list)
{
	bf_hres_filter_args_t filter_args = {
		.bf_license_list = bf_license_list,
		.node_bitmap = node_bitmap,
	};

	if (!job_ptr->license_list || !bf_license_list)
		return;

	slurm_mutex_lock(&license_mutex);
	list_for_each(job_ptr->license_list, _foreach_bf_hres_filter,
		      &filter_args);
	slurm_mutex_unlock(&license_mutex);

	return;
}

/* Update licenses on this system based upon slurm.conf.
 * Remove all previously allocated licenses */
extern int license_update(char *licenses)
{
	list_itr_t *iter;
	licenses_t *license_entry, *match;
	list_t *new_list;
	bool valid = true;

	new_list = _build_license_list(licenses, &valid, false);
	if (!valid)
		fatal("Invalid configured licenses: %s", licenses);

	_parse_hierarchical_resources(&new_list);

	slurm_mutex_lock(&license_mutex);
	if (!cluster_license_list) { /* no licenses before now */
		cluster_license_list = new_list;
		slurm_mutex_unlock(&license_mutex);
		return SLURM_SUCCESS;
	}

	iter = list_iterator_create(cluster_license_list);
	while ((license_entry = list_next(iter))) {
		/* Always add the remote ones, since we handle those
		   else where. */
		if (license_entry->remote) {
			list_remove(iter);
			if (!new_list)
				new_list = list_create(license_free_rec);
			license_entry->used = 0;
			list_append(new_list, license_entry);
			continue;
		}
		if (new_list) {
			licenses_find_rec_by_nodes_t args = {
				.name = license_entry->name,
				.nodes = license_entry->nodes,
			};
			match = list_find_first(new_list,
						_license_find_rec_by_nodes,
						&args);
		} else
			match = NULL;

		if (!match) {
			info("license %s removed with %u in use",
			     license_entry->name, license_entry->used);
		} else {
			match->id.lic_id = license_entry->id.lic_id;
			match->id.hres_id = license_entry->id.hres_id;
			if (license_entry->used > match->total) {
				info("license %s count decreased",
				     match->name);
			}
		}
	}
	list_iterator_destroy(iter);

	FREE_NULL_LIST(cluster_license_list);
	cluster_license_list = new_list;
	_set_license_ids();

	_licenses_print("update_license", cluster_license_list, NULL);
	slurm_mutex_unlock(&license_mutex);
	return SLURM_SUCCESS;
}

extern void license_add_remote(slurmdb_res_rec_t *rec)
{
	licenses_t *license_entry;
	char *name;


	xassert(rec);
	xassert(rec->type == SLURMDB_RESOURCE_LICENSE);

	name = xstrdup_printf("%s@%s", rec->name, rec->server);

	slurm_mutex_lock(&license_mutex);
	if (!cluster_license_list) {
		/* If last_license_update then init already ran and we
		 * don't have any licenses defined in the slurm.conf
		 * so make the cluster_license_list.
		 */
		xassert(last_license_update);
		cluster_license_list = list_create(license_free_rec);
	}

	license_entry = list_find_first(
		cluster_license_list, _license_find_remote_rec, name);

	if (license_entry)
		error("license_add_remote: license %s already exists!", name);
	else
		_add_res_rec_2_lic_list(rec, 0);

	xfree(name);

	slurm_mutex_unlock(&license_mutex);
}

extern void license_update_remote(slurmdb_res_rec_t *rec)
{
	licenses_t *license_entry;
	char *name;

	xassert(rec);
	xassert(rec->clus_res_rec);
	xassert(rec->type == SLURMDB_RESOURCE_LICENSE);

	name = xstrdup_printf("%s@%s", rec->name, rec->server);

	slurm_mutex_lock(&license_mutex);
	if (!cluster_license_list) {
		/* If last_license_update then init already ran and we
		 * don't have any licenses defined in the slurm.conf
		 * so make the cluster_license_list.
		 */
		xassert(last_license_update);
		cluster_license_list = list_create(license_free_rec);
	}

	license_entry = list_find_first(
		cluster_license_list, _license_find_remote_rec, name);

	if (!license_entry) {
		debug("license_update_remote: License '%s' not found, adding",
		      name);
		_add_res_rec_2_lic_list(rec, 0);
	} else {
		_handle_consumed(license_entry, rec);
	}
	last_license_update = time(NULL);

	xfree(name);

	slurm_mutex_unlock(&license_mutex);
}

extern void license_remove_remote(slurmdb_res_rec_t *rec)
{
	licenses_t *license_entry;
	list_itr_t *iter;
	char *name;

	xassert(rec);
	xassert(rec->type == SLURMDB_RESOURCE_LICENSE);

	slurm_mutex_lock(&license_mutex);
	if (!cluster_license_list) {
		xassert(last_license_update);
		cluster_license_list = list_create(license_free_rec);
	}

	name = xstrdup_printf("%s@%s", rec->name, rec->server);

	iter = list_iterator_create(cluster_license_list);
	while ((license_entry = list_next(iter))) {
		if (!license_entry->remote)
			continue;
		if (!xstrcmp(license_entry->name, name)) {
			info("license_remove_remote: license %s "
			     "removed with %u in use",
			     license_entry->name, license_entry->used);
			list_delete_item(iter);
			last_license_update = time(NULL);
			break;
		}
	}
	list_iterator_destroy(iter);

	if (!license_entry)
		error("license_remote_remote: License '%s' not found", name);

	xfree(name);
	slurm_mutex_unlock(&license_mutex);
}

extern void license_sync_remote(list_t *res_list)
{
	slurmdb_res_rec_t *rec = NULL;
	licenses_t *license_entry;
	list_itr_t *iter;

	slurm_mutex_lock(&license_mutex);
	if (res_list && !cluster_license_list) {
		xassert(last_license_update);
		cluster_license_list = list_create(license_free_rec);
	}

	iter = list_iterator_create(cluster_license_list);
	if (res_list) {
		list_itr_t *iter2 = list_iterator_create(res_list);
		while ((rec = list_next(iter2))) {
			char *name;
			if (rec->type != SLURMDB_RESOURCE_LICENSE)
				continue;
			name = xstrdup_printf("%s@%s", rec->name, rec->server);
			while ((license_entry = list_next(iter))) {
				if (!license_entry->remote)
					continue;
				if (!xstrcmp(license_entry->name, name)) {
					license_entry->remote = 2;
					_handle_consumed(license_entry, rec);
					if (license_entry->used >
					    license_entry->total) {
						info("license %s count "
						     "decreased",
						     license_entry->name);
					}
					break;
				}
			}
			xfree(name);
			if (!license_entry)
				_add_res_rec_2_lic_list(rec, 1);
			list_iterator_reset(iter);
		}
		list_iterator_destroy(iter2);
	}

	while ((license_entry = list_next(iter))) {
		if (!license_entry->remote)
			continue;
		else if (license_entry->remote == 1) {
			info("license_remove_remote: license %s "
			     "removed with %u in use",
			     license_entry->name, license_entry->used);
			list_delete_item(iter);
			last_license_update = time(NULL);
		} else if (license_entry->remote == 2)
			license_entry->remote = 1;
	}
	list_iterator_destroy(iter);

	slurm_mutex_unlock(&license_mutex);
}

/* Free memory associated with licenses on this system */
extern void license_free(void)
{
	slurm_mutex_lock(&license_mutex);
	FREE_NULL_LIST(cluster_license_list);
	slurm_mutex_unlock(&license_mutex);
}

/*
 * license_validate - Test if the required licenses are valid
 * IN licenses - required licenses
 * IN validate_configured - if true, validate that there are enough configured
 *                          licenses for the requested amount.
 * IN validate_existing - if true, validate that licenses exist, otherwise don't
 *                        return them in the final list.
 * OUT tres_req_cnt - appropriate counts for each requested gres,
 *                    since this only matters on pending jobs you can
 *                    send in NULL otherwise
 * OUT valid - true if required licenses are valid and a sufficient number
 *             are configured (though not necessarily available now)
 * RET license_list, must be destroyed by caller
 */
extern list_t *license_validate(char *licenses, bool validate_configured,
				bool validate_existing, bool hres,
				uint64_t *tres_req_cnt, bool *valid)
{
	list_itr_t *iter;
	licenses_t *license_entry, *match;
	list_t *job_license_list;
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_req;
	int tres_pos;

	/* Init all the license TRES to 0 */
	if (tres_req_cnt) {
		assoc_mgr_lock_t locks = { .tres = READ_LOCK };
		assoc_mgr_lock(&locks);

		/*
		 * We can start at TRES_ARRAY_TOTAL_CNT as we know licenses are
		 * after the static TRES.
		 */
		for (tres_pos = TRES_ARRAY_TOTAL_CNT;
		     tres_pos < slurmctld_tres_cnt;
		     tres_pos++) {
			if (tres_req_cnt[tres_pos] &&
			    !xstrcasecmp(assoc_mgr_tres_array[tres_pos]->type,
					 "license")) {
				tres_req_cnt[tres_pos] = 0;
			}
		}
		assoc_mgr_unlock(&locks);
	}

	job_license_list = _build_license_list(licenses, valid, hres);
	if (!job_license_list)
		return job_license_list;

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_req, 0, sizeof(slurmdb_tres_rec_t));
		tres_req.type = "license";
	}

	slurm_mutex_lock(&license_mutex);
	iter = list_iterator_create(job_license_list);
	while ((license_entry = list_next(iter))) {
		if (cluster_license_list) {
			if (license_entry->nodes) {
				licenses_find_rec_by_nodes_t args = {
					.name = license_entry->name,
					.nodes = license_entry->nodes,
				};
				match = list_find_first(
					cluster_license_list,
					_license_find_rec_by_nodes, &args);
			} else
				match = list_find_first(cluster_license_list,
							_license_find_rec,
							license_entry->name);
		} else
			match = NULL;
		if (!match) {
			debug("License name requested (%s) does not exist",
			      license_entry->name);
			if (!validate_existing) {
				list_delete_item(iter);
				continue;
			}
			*valid = false;
			break;
		} else if (validate_configured &&
			   (license_entry->total > match->total)) {
			debug("Licenses count requested higher than configured "
			      "(%s: %u > %u)",
			      match->name, license_entry->total, match->total);
			*valid = false;
			break;
		}

		license_entry->id.lic_id = match->id.lic_id;
		license_entry->id.hres_id = match->id.hres_id;
		license_entry->mode = match->mode;

		if (tres_req_cnt) {
			tres_req.name = license_entry->name;
			if ((tres_pos = assoc_mgr_find_tres_pos(
				     &tres_req, false)) != -1)
				tres_req_cnt[tres_pos] =
					(uint64_t)license_entry->total;
		}
	}
	list_iterator_destroy(iter);
	slurm_mutex_unlock(&license_mutex);

	_licenses_print("request_license", job_license_list, NULL);

	if (!(*valid)) {
		FREE_NULL_LIST(job_license_list);
	}
	return job_license_list;
}

/*
 * license_job_merge - The licenses from one job have just been merged into
 *	another job by appending one job's licenses to another, possibly
 *	including duplicate names. Reconstruct this job's licenses and
 *	license_list fields to eliminate duplicates.
 */
extern void license_job_merge(job_record_t *job_ptr)
{
	bool valid = true;

	FREE_NULL_LIST(job_ptr->license_list);
	job_ptr->license_list =
		_build_license_list(job_ptr->licenses, &valid, false);
	xfree(job_ptr->licenses);
	job_ptr->licenses = license_list_to_string(job_ptr->license_list);
}

static void _add_license(list_t *license_list, licenses_t *license_entry)
{
	if (!list_find_first(license_list, _license_find_rec_by_id,
			     &license_entry->id)) {
		list_append(license_list, license_entry);
	}
}

static int _foreach_license_job_test(void *x, void *arg)
{
	licenses_t *license_entry = x;
	licenses_t *match;
	license_test_args_t *test_args = arg;
	job_record_t *job_ptr = test_args->job_ptr;
	list_t *license_list = test_args->license_list;
	bool reboot = test_args->reboot;
	time_t when = test_args->when;
	int resv_licenses;

	if (license_entry->id.hres_id != NO_VAL16)
		return 0;

	match = list_find_first(license_list, _license_find_rec_by_id,
				&(license_entry->id));
	if (!match) {
		error("could not find license %s for job %u",
		      license_entry->name, job_ptr->job_id);
		/*
		 * Preempting jobs for licenses won't be effective, so don't
		 * preempt for any.
		 */
		if (job_ptr->licenses_to_preempt)
			FREE_NULL_LIST(job_ptr->licenses_to_preempt);
		test_args->rc = SLURM_ERROR;
		return -1;
	} else if (license_entry->total > match->total) {
		info("job %u wants more %s(lic_id=%u) licenses than configured",
		     job_ptr->job_id, license_entry->name, match->id.lic_id);
		/*
		 * Preempting jobs for licenses won't be effective so don't
		 * preempt for any.
		 */
		if (job_ptr->licenses_to_preempt)
			FREE_NULL_LIST(job_ptr->licenses_to_preempt);
		test_args->rc = SLURM_ERROR;
		return -1;
	} else if (!_sufficient_licenses(license_entry, match, 0)) {
		if (job_ptr->licenses_to_preempt)
			_add_license(job_ptr->licenses_to_preempt,
				     license_entry);
		test_args->rc = EAGAIN;
	} else {
		/* Assume node reboot required since we have not
		 * selected the compute nodes yet */
		resv_licenses = job_test_lic_resv(job_ptr,
						  license_entry->name,
						  when, reboot);
		if (!_sufficient_licenses(license_entry, match,
					  resv_licenses)) {
			if (job_ptr->licenses_to_preempt)
				_add_license(job_ptr->licenses_to_preempt,
					     license_entry);
			test_args->rc = EAGAIN;
		} else if (license_entry->op_or) {
			test_args->rc = SLURM_SUCCESS;
			FREE_NULL_LIST(job_ptr->licenses_to_preempt);
			/* Stop list_for_each */
			return -1;
		}
	}
	return 0;
}

/*
 * license_job_test_with_list - Test if the licenses required for a job are
 *	available in provided list
 * IN job_ptr - job identification
 * IN when    - time to check
 * IN reboot    - true if node reboot required to start job
 * RET: SLURM_SUCCESS, EAGAIN (not available now), SLURM_ERROR (never runnable)
 */
extern int license_job_test_with_list(job_record_t *job_ptr, time_t when,
				      bool reboot, list_t *license_list,
				      bool check_preempt_licenses)
{
	license_test_args_t test_args = {
		.job_ptr = job_ptr,
		.license_list = license_list,
		.rc = SLURM_SUCCESS,
		.reboot = reboot,
		.when = when,
	};
	licenses_t *license_entry;
	bool use_licenses_to_preempt;

	if (!job_ptr->license_list)	/* no licenses needed */
		return SLURM_SUCCESS;

	/* reclaim_licenses is disabled with OR'd licenses */
	license_entry = list_peek(job_ptr->license_list);
	use_licenses_to_preempt = preempt_for_licenses &&
				  check_preempt_licenses &&
				  !license_entry->op_or;
	if (!job_ptr->licenses_to_preempt && use_licenses_to_preempt)
		job_ptr->licenses_to_preempt = list_create(NULL);

	list_for_each(job_ptr->license_list, _foreach_license_job_test,
		      &test_args);
	if (use_licenses_to_preempt)
		_licenses_print("licenses_to_preempt",
				job_ptr->licenses_to_preempt, job_ptr);

	return test_args.rc;
}

/*
 * license_job_test - Test if the licenses required for a job are available
 * IN job_ptr - job identification
 * IN when    - time to check
 * IN reboot    - true if node reboot required to start job
 * RET: SLURM_SUCCESS, EAGAIN (not available now), SLURM_ERROR (never runnable)
 */
extern int license_job_test(job_record_t *job_ptr, time_t when, bool reboot)
{
	int rc;

	slurm_mutex_lock(&license_mutex);
	rc = license_job_test_with_list(job_ptr, when, reboot,
					cluster_license_list, false);
	slurm_mutex_unlock(&license_mutex);

	return rc;
}

static int _foreach_license_copy(void *x, void *arg)
{
	licenses_t *license_entry_src = x;
	licenses_t *license_entry_dest = xmalloc(sizeof(licenses_t));
	list_t *license_list_dest = arg;

	license_entry_dest->name = xstrdup(license_entry_src->name);
	license_entry_dest->total = license_entry_src->total;
	license_entry_dest->used = license_entry_src->used;
	license_entry_dest->last_deficit = license_entry_src->last_deficit;
	license_entry_dest->id = license_entry_src->id;
	license_entry_dest->mode = license_entry_src->mode;
	license_entry_dest->op_or = license_entry_src->op_or;
	list_append(license_list_dest, license_entry_dest);

	return 0;
}

static int _foreach_license_light_copy(void *x, void *arg)
{
	licenses_t *license_entry_src = x;
	licenses_t *license_entry_dest = xmalloc(sizeof(licenses_t));
	list_t *license_list_dest = arg;

	license_entry_dest->total = license_entry_src->total;
	license_entry_dest->used = license_entry_src->used;
	license_entry_dest->last_deficit = license_entry_src->last_deficit;
	license_entry_dest->id = license_entry_src->id;
	license_entry_dest->mode = license_entry_src->mode;
	license_entry_dest->op_or = license_entry_src->op_or;
	list_append(license_list_dest, license_entry_dest);

	return 0;
}
/*
 * license_copy - create a copy of a license list
 * IN license_list_src - job license list to be copied
 * RET a copy of the license list
 */
extern list_t *license_copy(list_t *license_list_src)
{
	list_t *license_list_dest = NULL;

	if (!license_list_src)
		return license_list_dest;

	license_list_dest = list_create(license_free_rec);

	list_for_each(license_list_src, _foreach_license_copy,
		      license_list_dest);

	return license_list_dest;
}

/*
 * cluster_license_copy - create a copy of the cluster_license_list
 * RET a copy of the license list
 */
extern list_t *cluster_license_copy(void)
{
	list_t *license_list_dest = NULL;

	slurm_mutex_lock(&license_mutex);
	if (cluster_license_list) {
		license_list_dest = list_create(license_free_rec);
		list_for_each(cluster_license_list, _foreach_license_light_copy,
			      license_list_dest);
	}
	slurm_mutex_unlock(&license_mutex);

	return license_list_dest;
}

/*
 * We need to track the allocated licenses separately, so that:
 *
 * - when the job is state saved and then restored, or
 * - when the job completes,
 *
 * we update the license counts in cluster_license_list using only the licenses
 * that were allocated.
 */
static int _set_licenses_alloc(job_record_t *job_ptr, bool lic_or,
			       licenses_t *license_entry)
{
	if (lic_or) {
		if (!license_entry) {
			/*
			 * We tested that there were enough licenses available
			 * but then there weren't enough when we tried to
			 * allocate. This indicates faulty logic.
			 */
			error("Could not allocate licenses %s for %pJ",
			      job_ptr->licenses, job_ptr);
			return SLURM_ERROR;
		}

		/* Remove all other licenses besides the one we allocated. */
		list_delete_all(job_ptr->license_list,
				_license_find_rec_by_id_not,
				&license_entry->id);
		xassert(list_count(job_ptr->license_list) == 1);
		xassert(license_entry ==
			(licenses_t *) list_peek(job_ptr->license_list));
	}
	job_ptr->licenses_allocated =
		license_list_to_string(job_ptr->license_list);

	return SLURM_SUCCESS;
}

static int _foreach_hres_job_get(void *x, void *arg)
{
	licenses_t *match = x;
	foreach_get_hres_t *args = arg;

	if (match->id.hres_id != args->license_entry->id.hres_id)
		return 0;

	if (!bit_overlap_any(match->node_bitmap, args->job_ptr->node_bitmap))
		return 0;

	if (args->license_entry->mode == HRES_MODE_1 &&
	    _sufficient_licenses(args->license_entry, match, 0)) {
		match->used += args->license_entry->total;
		args->license_entry->id.lic_id = match->id.lic_id;
		xfree(args->license_entry->nodes);
		args->license_entry->nodes = xstrdup(match->nodes);
		return -1;
	} else if (args->license_entry->mode == HRES_MODE_2) {
		match->used += args->license_entry->total;
		return 0;
	}

	return 0;
}

/*
 * license_job_get - Get the licenses required for a job
 * IN job_ptr - job identification
 * RET SLURM_SUCCESS or failure code
 */
extern int license_job_get(job_record_t *job_ptr, bool restore)
{
	list_itr_t *iter;
	licenses_t *license_entry, *match;
	int rc = SLURM_SUCCESS;
	bool lic_or = false;

	if (!job_ptr->license_list)	/* no licenses needed */
		return rc;

	last_license_update = time(NULL);

	slurm_mutex_lock(&license_mutex);
	iter = list_iterator_create(job_ptr->license_list);
	while ((license_entry = list_next(iter))) {
		if (license_entry->id.hres_id != NO_VAL16) {
			foreach_get_hres_t arg = {
				.job_ptr = job_ptr,
				.license_entry = license_entry,
			};
			list_for_each_ro(cluster_license_list,
					 _foreach_hres_job_get, &arg);

			license_entry->used += license_entry->total;

			continue;
		}

		lic_or = license_entry->op_or;
		match = list_find_first(cluster_license_list,
					_license_find_rec_by_id,
					&license_entry->id);
		if (match) {
			/*
			 * With OR, we only know that at least one of the job's
			 * requested licenses are available, so we need to test
			 * for availability again. With AND we know that all
			 * licenses are available so we don't need to check.
			 */
			if (lic_or) {
				int resv_blk_lic_cnt =
					job_test_lic_resv(job_ptr, match->name,
							  last_license_update,
							  false);

				if (!_sufficient_licenses(license_entry, match,
							  resv_blk_lic_cnt)) {
					/* Not enough of this license */
					continue;
				}
			}

			match->used += license_entry->total;
			license_entry->used += license_entry->total;
			if (match->remote && restore) {
				if (license_entry->total > match->last_deficit)
					match->last_deficit = 0;
				else
					match->last_deficit -=
						license_entry->total;
			}
			if (lic_or) {
				break;
			}
		} else {
			error("could not find license %s for job %u",
			      license_entry->name, job_ptr->job_id);
			rc = SLURM_ERROR;
		}
	}
	list_iterator_destroy(iter);

	/* When restoring, allocated licenses is already set */
	if (!rc && !restore)
		rc = _set_licenses_alloc(job_ptr, lic_or, license_entry);

	_licenses_print("acquire_license", cluster_license_list, job_ptr);
	slurm_mutex_unlock(&license_mutex);
	return rc;
}

static int _foreach_hres_job_return_mode2(void *x, void *arg)
{
	licenses_t *lic = x;
	foreach_get_hres_t *args = arg;
	licenses_t *match;

	if (lic->id.hres_id != args->license_entry->id.hres_id)
		return 0;
	if (lic->node_bitmap)
		match = lic;
	else
		match = list_find_first_ro(cluster_license_list,
					   _license_find_rec_by_id, &lic->id);
	if (!match ||
	    !bit_overlap_any(match->node_bitmap, args->job_ptr->node_bitmap))
		return 0;
	if (lic->used >= args->license_entry->total)
		lic->used -= args->license_entry->total;
	else {
		error("%s: license use count underflow for lic_id=%u",
		      __func__, lic->id.lic_id);
		lic->used = 0;
	}

	return 0;
}

static int _foreach_license_job_return(void *x, void *arg)
{
	licenses_t *license_entry = x;
	license_return_args_t *args = arg;
	licenses_t *match;

	if (license_entry->mode == HRES_MODE_2) {
		foreach_get_hres_t arg2 = {
			.job_ptr = args->job_ptr,
			.license_entry = license_entry,
		};
		if (!args->locked)
			slurm_mutex_lock(&license_mutex);
		list_for_each_ro(args->license_list,
				 _foreach_hres_job_return_mode2, &arg2);
		if (!args->locked)
			slurm_mutex_unlock(&license_mutex);

		license_entry->used = 0;
		return 0;
	}

	match = list_find_first(args->license_list, _license_find_rec_by_id,
				&license_entry->id);
	if (match) {
		if (match->used >= license_entry->total)
			match->used -= license_entry->total;
		else {
			error("%s: license use count underflow for lic_id=%u",
			      __func__, match->id.lic_id);
			match->used = 0;
		}
		license_entry->used = 0;
		if (license_entry->mode == HRES_MODE_1) {
			license_entry->id.lic_id = license_entry->id.hres_id;
		}
	} else {
		/* This can happen after a reconfiguration */
		error("%s: job returning unknown license lic_id=%u",
		      __func__, license_entry->id.lic_id);
	}
	return 0;
}

/*
 * license_job_return_to_list - Return the licenses allocated to a job to the
 *	`provided list
 * IN job_ptr - job identification
 * RET count of license having state changed
 */
extern int license_job_return_to_list(job_record_t *job_ptr,
				      list_t *license_list, bool locked)
{
	int rc = 0;
	license_return_args_t args = {
		.job_ptr = job_ptr,
		.license_list = license_list,
		.locked = locked,
	};

	if (!job_ptr->license_list)	/* no licenses needed */
		return rc;

	log_flag(TRACE_JOBS, "%s: %pJ", __func__, job_ptr);

	rc = list_for_each(job_ptr->license_list, _foreach_license_job_return,
			   &args);

	return rc;
}

/*
 * license_job_return - Return the licenses allocated to a job
 * IN job_ptr - job identification
 * RET SLURM_SUCCESS or failure code
 */
extern int license_job_return(job_record_t *job_ptr)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&license_mutex);
	if (license_job_return_to_list(job_ptr, cluster_license_list, true))
		last_license_update = time(NULL);
	_licenses_print("return_license", cluster_license_list, job_ptr);
	slurm_mutex_unlock(&license_mutex);

	return rc;
}

/*
 * license_list_overlap - test if there is any overlap in licenses
 *	names found in the two lists
 */
extern bool license_list_overlap(list_t *list_1, list_t *list_2)
{
	if (!list_1 || !list_2)
		return false;
	return list_find_first_ro(list_1, _license_find_rec_in_list_by_id,
				  list_2);
}

/* pack_all_licenses()
 *
 * Return license counters to the library.
 */
extern buf_t *get_all_license_info(uint16_t protocol_version)
{
	list_itr_t *iter;
	licenses_t *lic_entry;
	uint32_t lics_packed;
	int tmp_offset;
	buf_t *buffer;
	time_t now = time(NULL);

	debug2("%s: calling for all licenses", __func__);

	buffer = init_buf(BUF_SIZE);

	/* write header: version and time
	 */
	lics_packed = 0;
	pack32(lics_packed, buffer);
	pack_time(now, buffer);

	slurm_mutex_lock(&license_mutex);
	if (cluster_license_list) {
		iter = list_iterator_create(cluster_license_list);
		while ((lic_entry = list_next(iter))) {
			set_reserved_license_count(lic_entry);
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

	return buffer;
}

static int _foreach_get_total_license_cnt(void *x, void *arg)
{
	licenses_t *license_entry = (licenses_t *) x;
	foreach_get_total_t *args = arg;

	if ((license_entry->name == NULL) || (args->name == NULL))
		return 0;

	if (xstrcmp(license_entry->name, args->name))
		return 0;

	*(args->count) = +license_entry->total;
	return 0;
}

extern uint32_t get_total_license_cnt(char *name)
{
	uint32_t count = 0;

	slurm_mutex_lock(&license_mutex);
	if (cluster_license_list) {
		foreach_get_total_t arg = {
			.count = &count,
			.name = name,
		};

		list_for_each_ro(cluster_license_list,
				 _foreach_get_total_license_cnt, &arg);
	}
	slurm_mutex_unlock(&license_mutex);

	return count;
}

/* node_read should be locked before coming in here
 * returns 1 if change happened.
 */
extern char *licenses_2_tres_str(list_t *license_list)
{
	list_itr_t *itr;
	slurmdb_tres_rec_t *tres_rec;
	licenses_t *license_entry;
	char *tres_str = NULL;
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_req;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (!license_list)
		return NULL;

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_req, 0, sizeof(slurmdb_tres_rec_t));
		tres_req.type = "license";
	}

	assoc_mgr_lock(&locks);
	itr = list_iterator_create(license_list);
	while ((license_entry = list_next(itr))) {
		tres_req.name = license_entry->name;
		if (!(tres_rec = assoc_mgr_find_tres_rec(&tres_req)))
			continue; /* not tracked */

		if (slurmdb_find_tres_count_in_string(
			    tres_str, tres_rec->id) != INFINITE64)
			continue; /* already handled */
		/* New license */
		xstrfmtcat(tres_str, "%s%u=%"PRIu64,
			   tres_str ? "," : "",
			   tres_rec->id, (uint64_t)license_entry->total);
	}
	list_iterator_destroy(itr);
	assoc_mgr_unlock(&locks);

	return tres_str;
}

extern void license_set_job_tres_cnt(list_t *license_list,
				     uint64_t *tres_cnt,
				     bool locked)
{
	list_itr_t *itr;
	licenses_t *license_entry;
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_rec;
	int tres_pos;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
		tres_rec.type = "license";
	}

	if (!license_list || !tres_cnt)
		return;

	if (!locked)
		assoc_mgr_lock(&locks);

	itr = list_iterator_create(license_list);
	while ((license_entry = list_next(itr))) {
		tres_rec.name = license_entry->name;
		if ((tres_pos = assoc_mgr_find_tres_pos(
			     &tres_rec, locked)) != -1)
			tres_cnt[tres_pos] = (uint64_t)license_entry->total;
	}
	list_iterator_destroy(itr);

	if (!locked)
		assoc_mgr_unlock(&locks);
}

/*
 * Please update src/common/slurm_protocol_pack.c _unpack_license_info_msg() if
 * this changes.
 */
static void _pack_license(licenses_t *lic, buf_t *buffer,
			  uint16_t protocol_version)
{
	if (protocol_version >= SLURM_25_05_PROTOCOL_VERSION) {
		packstr(lic->name, buffer);
		pack32(lic->total, buffer);
		pack32(lic->used, buffer);
		pack32(lic->reserved, buffer);
		pack8(lic->remote, buffer);
		pack32(lic->last_consumed, buffer);
		pack32(lic->last_deficit, buffer);
		pack_time(lic->last_update, buffer);
		pack8(lic->mode, buffer);
		packstr(lic->nodes, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(lic->name, buffer);
		pack32(lic->total, buffer);
		pack32(lic->used, buffer);
		pack32(lic->reserved, buffer);
		pack8(lic->remote, buffer);
		pack32(lic->last_consumed, buffer);
		pack32(lic->last_deficit, buffer);
		pack_time(lic->last_update, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

static void _bf_license_free_rec(void *x)
{
	bf_license_t *entry = x;

	if (!entry)
		return;

	xfree(entry);
}

/*
 * Will never match on a reserved license.
 */
static int _bf_licenses_find_rec(void *x, void *key)
{
	bf_license_t *license_entry = x;
	licenses_id_t *id = key;

	xassert(license_entry->id.lic_id != NO_VAL16);
	xassert(id->lic_id != NO_VAL16);

	if (license_entry->resv_ptr)
		return 0;

	if (license_entry->id.lic_id == id->lic_id)
		return 1;

	return 0;
}

static int _bf_licenses_find_resv(void *x, void *key)
{
	bf_license_t *license_entry = x;
	bf_licenses_find_resv_t *target = key;

	if (license_entry->resv_ptr != target->resv_ptr)
		return 0;

	if (license_entry->id.lic_id != target->id.lic_id)
		return 0;

	return 1;
}

extern list_t *bf_licenses_initial(bool bf_running_job_reserve)
{
	list_t *bf_list;
	list_itr_t *iter;
	licenses_t *license_entry;
	bf_license_t *bf_entry;

	slurm_mutex_lock(&license_mutex);
	if (!cluster_license_list || !list_count(cluster_license_list)) {
		slurm_mutex_unlock(&license_mutex);
		return NULL;
	}

	bf_list = list_create(_bf_license_free_rec);

	iter = list_iterator_create(cluster_license_list);
	while ((license_entry = list_next(iter))) {
		bf_entry = xmalloc(sizeof(*bf_entry));
		bf_entry->remaining = license_entry->total;
		bf_entry->id = license_entry->id;

		if (!bf_running_job_reserve)
			bf_entry->remaining -= license_entry->used;

		list_append(bf_list, bf_entry);
	}
	list_iterator_destroy(iter);

	slurm_mutex_unlock(&license_mutex);

	return bf_list;
}

extern char *bf_licenses_to_string(bf_licenses_t *licenses_list)
{
	char *sep = "";
	char *licenses = NULL;
	list_itr_t *iter;
	bf_license_t *entry;

	if (!licenses_list)
		return NULL;

	iter = list_iterator_create(licenses_list);
	while ((entry = list_next(iter))) {
		xstrfmtcat(licenses, "%s%s%s%slic_id=%u:%u", sep,
			   (entry->resv_ptr ? "resv=" : ""),
			   (entry->resv_ptr ? entry->resv_ptr->name : ""),
			   (entry->resv_ptr ? ":" : ""), entry->id.lic_id,
			   entry->remaining);
		sep = ",";
	}
	list_iterator_destroy(iter);

	return licenses;
}

static int _foreach_bf_license_copy(void *x, void *arg)
{
	bf_license_t *entry_src = x;
	bf_licenses_t *licenses_dest = arg;
	bf_license_t *entry_dest;

	entry_dest = xmalloc(sizeof(*entry_dest));
	entry_dest->remaining = entry_src->remaining;
	entry_dest->resv_ptr = entry_src->resv_ptr;
	entry_dest->id = entry_src->id;
	list_append(licenses_dest, entry_dest);

	return 0;
}

extern bf_licenses_t *slurm_bf_licenses_copy(bf_licenses_t *licenses_src)
{
	bf_licenses_t *licenses_dest = NULL;

	if (!licenses_src)
		return NULL;

	licenses_dest = list_create(_bf_license_free_rec);

	list_for_each(licenses_src, _foreach_bf_license_copy, licenses_dest);

	return licenses_dest;
}

static int _foreach_hres_deduct(void *x, void *arg)
{
	bf_license_t *bf_lic = x;
	licenses_t *match;
	foreach_get_hres_t *args = arg;

	if (bf_lic->id.hres_id != args->license_entry->id.hres_id)
		return 0;

	match = list_find_first(cluster_license_list, _license_find_rec_by_id,
				&bf_lic->id);
	if (!match ||
	    !bit_overlap_any(match->node_bitmap, args->job_ptr->node_bitmap))

		return 0;

	if (bf_lic->remaining < args->license_entry->total) {
		error("%s: underflow on lic_id=%u", __func__, match->id.lic_id);
		bf_lic->remaining = 0;
	} else {
		bf_lic->remaining -= args->license_entry->total;
	}

	if (match->mode == HRES_MODE_1)
		return -1;
	else
		return 0;
}
extern void slurm_bf_licenses_deduct(bf_licenses_t *licenses,
				     job_record_t *job_ptr)
{
	licenses_t *job_entry;
	list_itr_t *iter;
	bool found = false, lic_or = false;

	xassert(job_ptr);

	if (!job_ptr->license_list)
		return;

	iter = list_iterator_create(job_ptr->license_list);
	while ((job_entry = list_next(iter))) {
		bf_license_t *resv_entry = NULL, *bf_entry;
		int needed = job_entry->total;
		int resv_acquired = 0;

		if (job_entry->id.hres_id != NO_VAL16) {
			foreach_get_hres_t arg = {
				.job_ptr = job_ptr,
				.license_entry = job_entry,
			};
			slurm_mutex_lock(&license_mutex);
			list_for_each_ro(licenses, _foreach_hres_deduct, &arg);
			slurm_mutex_unlock(&license_mutex);

			continue;
		}

		lic_or = job_entry->op_or;
		/*
		 * Jobs with reservations may use licenses out of the
		 * reservation, as well as global ones. Deduct from
		 * reservation first, then global as needed.
		 */
		if (job_ptr->resv_ptr) {
			bf_licenses_find_resv_t target_record = {
				.id = job_entry->id,
				.resv_ptr = job_ptr->resv_ptr,
			};

			resv_entry = list_find_first(licenses,
						     _bf_licenses_find_resv,
						     &target_record);
			if (resv_entry && (needed <= resv_entry->remaining)) {
				resv_entry->remaining -= needed;
				/* OR - reservation has enough, break. */
				if (lic_or) {
					found = true;
					break;
				}
				continue;
			} else if (resv_entry) {
				resv_acquired = resv_entry->remaining;
				needed -= resv_acquired;
				resv_entry->remaining = 0;
			}
		}

		bf_entry = list_find_first(licenses, _bf_licenses_find_rec,
					   &job_entry->id);

		if (!bf_entry) {
			error("%s: missing license lic_id=%u",
			      __func__, job_entry->id.lic_id);
		} else if (bf_entry->remaining < needed) {
			/*
			 * OR - Not an error;  skip this one and keep going
			 * until we find the next one that is available.
			 */
			if (lic_or) {
				/* Return resv_acquired licenses */
				if (resv_entry) {
					resv_entry->remaining += resv_acquired;
					needed += resv_acquired;
				}
				continue;
			}
			error("%s: underflow on lic_id=%u",
			      __func__, bf_entry->id.lic_id);
			bf_entry->remaining = 0;
		} else {
			bf_entry->remaining -= needed;
			if (lic_or) {
				found = true;
				break;
			}
		}
	}
	list_iterator_destroy(iter);

	if (lic_or && !found) {
		/*
		 * If we get to this function, we should always have found an
		 * available license. If we did not, this indicates an error
		 * in testing if one is available in slurm_bf_licenses_avail().
		 */
		error("%s: %pJ No OR'd licenses available for bf plan",
		      __func__, job_ptr);
	}
}

/*
 * Transfer licenses into the control of a reservation.
 * Finds the global license, deducts the required number, then assigns those
 * to a new record locked to that reservation.
 */
extern void slurm_bf_licenses_transfer(bf_licenses_t *licenses,
				       job_record_t *job_ptr)
{
	licenses_t *resv_entry;
	list_itr_t *iter;

	xassert(job_ptr);

	if (!job_ptr->license_list)
		return;

	iter = list_iterator_create(job_ptr->license_list);
	while ((resv_entry = list_next(iter))) {
		bf_license_t *bf_entry, *new_entry;
		int needed = resv_entry->total;
		int reservable = resv_entry->total;

		bf_entry = list_find_first(licenses, _bf_licenses_find_rec,
					   &(resv_entry->id));

		if (!bf_entry) {
			error("%s: missing license lic_id=%u",
			      __func__, resv_entry->id.lic_id);
		} else if (bf_entry->remaining < needed) {
			error("%s: underflow on lic_id=%u",
			      __func__,bf_entry->id.lic_id);
			reservable = bf_entry->remaining;
			bf_entry->remaining = 0;
		} else {
			bf_entry->remaining -= needed;
			reservable = needed;
		}

		new_entry = xmalloc(sizeof(*new_entry));
		new_entry->id = resv_entry->id;
		new_entry->remaining = reservable;
		new_entry->resv_ptr = job_ptr->resv_ptr;

		list_append(licenses, new_entry);
	}
	list_iterator_destroy(iter);
}

extern bool slurm_bf_licenses_avail(bf_licenses_t *licenses,
				    job_record_t *job_ptr,
				    bitstr_t *node_bitmap)
{
	list_itr_t *iter;
	licenses_t *need;
	bool avail = true;
	bitstr_t *tmp_bitmap = NULL;

	if (!job_ptr->license_list)
		return true;

	iter = list_iterator_create(job_ptr->license_list);
	while ((need = list_next(iter))) {
		bf_license_t *resv_entry = NULL, *bf_entry;
		int needed = need->total;

		if (need->id.hres_id != NO_VAL16) {
			if (!node_bitmap)
				continue;
			COPY_BITMAP(tmp_bitmap, node_bitmap);
			slurm_bf_hres_filter(job_ptr, tmp_bitmap, licenses);
			if (!bit_equal(tmp_bitmap, node_bitmap)) {
				avail = false;
				break;
			}
		}
		/*
		 * Jobs with reservations may use licenses out of the
		 * reservation, as well as global ones. Deduct from
		 * reservation first, then global as needed.
		 */
		if (job_ptr->resv_ptr) {
			bf_licenses_find_resv_t target_record = {
				.id = need->id,
				.resv_ptr = job_ptr->resv_ptr,
			};

			resv_entry = list_find_first(licenses,
						     _bf_licenses_find_resv,
						     &target_record);

			if (resv_entry && (needed <= resv_entry->remaining)) {
				/*
				 * OR - only need one, stop searching.
				 * Set avail = true in case a previous license
				 * was unavailable.
				 */
				if (need->op_or) {
					avail = true;
					break;
				}
				/* AND */
				continue;
			} else if (resv_entry)
				needed -= resv_entry->remaining;
		}

		bf_entry = list_find_first(licenses, _bf_licenses_find_rec,
					   &(need->id));

		if (!bf_entry || (bf_entry->remaining < needed)) {
			avail = false;
			/*
			 * OR - keep searching until we find one that is
			 * available or we get through the whole list.
			 */
			if (need->op_or)
				continue;
			/* AND */
			break;
		}
		/* OR - only need one, stop searching. */
		if (need->op_or) {
			avail = true;
			break;
		}
	}
	list_iterator_destroy(iter);
	FREE_NULL_BITMAP(tmp_bitmap);

	return avail;
}

static int _bf_licenses_find_difference(void *x, void *key)
{
	bf_license_t *entry_a = x;
	bf_licenses_t *b = key;
	bf_license_t *entry_b;
	bf_licenses_find_resv_t target_record = {
		.id = entry_a->id,
		.resv_ptr = entry_a->resv_ptr,
	};

	entry_b = list_find_first_ro(b, _bf_licenses_find_resv, &target_record);

	if (!entry_b || (entry_a->remaining != entry_b->remaining)) {
		return 1;
	}
	return 0;
}

extern bool slurm_bf_licenses_equal(bf_licenses_t *a, bf_licenses_t *b)
{
	return !(list_find_first_ro(a, _bf_licenses_find_difference, b));
}
