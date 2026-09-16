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

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/sercli.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/data_parser.h"
#include "src/interfaces/serializer.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/licenses.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Characters that may not appear in an HRES layer_name. These are the
 * separators of the license strings built by license_list_to_string() and
 * parsed by _build_license_list(), plus whitespace, which that parser rejects.
 */
#define HRES_LAYER_NAME_BAD_CHARS ",;|:=()* \t\n"

list_t *cluster_license_list = NULL;
uint16_t next_lic_id = 0;
time_t last_license_update = 0;
bool preempt_for_licenses = false;
static pthread_mutex_t license_mutex = PTHREAD_MUTEX_INITIALIZER;
static void _pack_license(licenses_t *lic, buf_t *buffer,
			  uint16_t protocol_version);

typedef struct {
	uint16_t disable_hres;
	char *hres_name;
} foreach_disable_hres_args_t;

typedef struct {
	licenses_t *license_entry; /* job or reservation record */
	list_t *license_list; /* list holding the records being charged */
} foreach_hres_return_t;

typedef struct {
	licenses_t *lic; /* Pointer to record in cluster_license_list */
	bitstr_t *new_nodes_bitmap;
} hres_update_nodes_t;

typedef struct {
	bitstr_t *add_node_bitmap;
	char *err_msg;
	char *node_names;
	int rc;
	list_t *updates; /* List of hres_update_nodes_t* */
} foreach_hres_add_node_t;

typedef struct {
	uint16_t curr_hres_id;
	uint16_t depth;
	char *first_leaf;
} foreach_uniform_depth_t;

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
	char *hres_name;
	foreach_license_print_t *print_arg;
} foreach_print_hres_arg_t;

typedef struct {
	char *name;
	char *nodes;
} licenses_find_rec_by_nodes_t;

typedef struct {
	char *hres_name;
	char *layer_name;
} licenses_find_layer_t;

typedef struct {
	licenses_t *license_entry;
	job_record_t *job_ptr;
	time_t when;
} foreach_get_hres_t;

typedef struct {
	uint32_t *count;
	char *name;
} foreach_get_total_t;

typedef struct {
	job_record_t *job_ptr;
	licenses_t *license_entry;
	bitstr_t *node_mask;
	time_t when;
} foreach_hres_filter_t;

typedef struct {
	job_record_t *job_ptr;
	list_t *license_list;
	bitstr_t *node_bitmap;
	time_t when;
} hres_filter_args_t;

typedef struct {
	bf_licenses_t *bf_license_list;
	job_record_t *job_ptr;
	bitstr_t *node_bitmap;
} bf_hres_filter_args_t;

typedef struct {
	bool future;
	job_record_t *job_ptr;
	list_t *license_list;
	bool locked;
} license_return_args_t;

typedef struct {
	uint16_t idx;
	uint16_t prev_hres_id;
} foreach_hres_set_mode3_t;

typedef struct {
	licenses_t *match;
	char *name;
} fuzzy_match_remote_args_t;

typedef struct {
	char *layers;
	char *licenses;
	char *sep;
} license_list_to_string_args_t;

typedef struct {
	list_t *cluster_license_list;
} license_sync_remote_args_t;

typedef struct {
	job_record_t *job_ptr;
	licenses_t *last_entry;
	bool lic_or;
	int rc;
	bool restore;
} license_job_get_args_t;

typedef struct {
	buf_t *buffer;
	uint32_t lics_packed;
	uint16_t protocol_version;
} get_all_license_info_args_t;

typedef struct {
	slurmdb_tres_rec_t *tres_req;
	char *tres_str;
} licenses_2_tres_str_args_t;

typedef struct {
	bool locked;
	uint64_t *tres_cnt;
	slurmdb_tres_rec_t *tres_rec;
} license_set_job_tres_cnt_args_t;

typedef struct {
	list_t *bf_list;
	bool bf_running_job_reserve;
} bf_licenses_initial_args_t;

typedef struct {
	char *licenses;
	char *sep;
} bf_licenses_to_string_args_t;

typedef struct {
	bool found;
	job_record_t *job_ptr;
	bf_licenses_t *licenses;
	bool lic_or;
} slurm_bf_licenses_deduct_args_t;

typedef struct {
	bf_licenses_t *licenses;
	slurmctld_resv_t *resv_ptr;
} slurm_bf_licenses_transfer_args_t;

typedef struct {
	bool avail;
	job_record_t *job_ptr;
	bf_licenses_t *licenses;
	bitstr_t *node_bitmap;
	bitstr_t *tmp_bitmap;
} slurm_bf_licenses_avail_args_t;

typedef struct {
	list_t *new_list;
} license_update_args_t;

typedef struct {
	bool *fuzzy_match;
	bool fuzzy_match_remote;
	bool has_mode3;
	slurmdb_tres_rec_t *tres_req;
	uint64_t *tres_req_cnt;
	bool *valid;
	bool validate_configured;
	bool validate_existing;
	hres_syntax_t hres_syntax;
} license_validate_args_t;

typedef struct {
	list_t *licenses_cur;
	list_t *licenses_next;
	list_itr_t *licenses_cur_iter;
	slurmctld_resv_t *job_resv_ptr;
} find_relevant_hres_diff_args_t;

typedef struct {
	list_itr_t *licenses_iter;
	uint16_t hres_id;
	slurmctld_resv_t *job_resv_ptr;
} find_diff_hres_in_list_args_t;

static void _print_path(path_idx_t path_idx, uint16_t depth)
{
	for (int i = 0; i < depth; i++) {
		info("\t\tpath_id[%d]:%u", i, path_idx[i]);
	}
}

static int _foreach_variable_print(void *x, void *arg)
{
	hres_variable_t *variable = x;
	info("\t\tname=%s value=%u", variable->name, variable->value);
	return 0;
}

static int _foreach_license_print(void *x, void *arg)
{
	licenses_t *license_entry = x;
	foreach_license_print_t *args = arg;

	if (license_entry->id.hres_id != NO_VAL16) {
		info("licenses: %s=%s lic_id=%u hres_id=%u mode=%u layer_name=%s nodes:%s total=%u used=%u disable_hres=%s disable_layer=%s",
		     args->header, license_entry->name, license_entry->id.lic_id,
		     license_entry->id.hres_id, license_entry->mode,
		     license_entry->hres_rec.layer_name,
		     license_entry->nodes, license_entry->total,
		     license_entry->used,
		     license_entry->hres_rec.disable_hres ? "true" : "false",
		     license_entry->hres_rec.disable_layer ? "true" : "false");
		if (license_entry->mode == HRES_MODE_3) {
			licenses_t *parent = license_entry->hres_rec.parent;
			uint16_t parent_id = NO_VAL16;

			if (parent)
				parent_id = parent->id.lic_id;

			info("\tidx=%u parent_id=%u depth=%u level=%u layers_cnt=%u, leaf_cnt=%u",
			     license_entry->hres_rec.idx,
			     parent_id,
			     license_entry->hres_rec.depth,
			     license_entry->hres_rec.level,
			     license_entry->hres_rec.layers_cnt,
			     license_entry->hres_rec.leaf_cnt);
			_print_path(license_entry->hres_rec.path_idx,
				    license_entry->hres_rec.depth);
		}
		if (license_entry->hres_rec.base) {
			info("\tbase:");
			list_for_each(license_entry->hres_rec.base,
				      _foreach_variable_print, NULL);
		}
		if (license_entry->hres_rec.variables) {
			info("\tvariable:");
			list_for_each(license_entry->hres_rec.variables,
				      _foreach_variable_print, NULL);
		}
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

static int _foreach_license_print_hres(void *x, void *arg)
{
	licenses_t *license = x;
	foreach_print_hres_arg_t *args = arg;

	if (xstrcmp(license->name, args->hres_name))
		return 0;
	_foreach_license_print(license, args->print_arg);
	return 0;
}

/* Free an hres_charge_t record (for use by FREE_NULL_LIST) */
extern void hres_charge_free(void *x)
{
	hres_charge_t *charge = x;

	if (!charge)
		return;

	xfree(charge->layer_name);
	xfree(charge);
}

/* Free a license_t record (for use by FREE_NULL_LIST) */
extern void license_free_rec(void *x)
{
	licenses_t *license_entry = (licenses_t *) x;

	if (license_entry) {
		FREE_NULL_LIST(license_entry->hres_charges);
		FREE_NULL_LIST(license_entry->hres_rec.base);
		xfree(license_entry->name);
		FREE_NULL_BITMAP(license_entry->node_bitmap);
		xfree(license_entry->nodes);
		xfree(license_entry->hres_rec.layer_name);
		xfree(license_entry->hres_rec.parent_name);
		xfree(license_entry->hres_rec.topology_name);
		FREE_NULL_LIST(license_entry->hres_rec.variables);
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

static int _license_remote_fuzzy_find_rec(void *x, void *key)
{
	licenses_t *license_entry = (licenses_t *) x;
	char *name = (char *) key;

	if ((license_entry->name == NULL) || (name == NULL))
		return 0;
	if (license_entry->remote)
		return slurm_remote_license_fuzzy_match(name,
							license_entry->name);
	else if (xstrcmp(license_entry->name, name))
		return 0;
	return 1;
}

/*
 * Match query license string possibly to local or remote license name.
 * If there is a local exact match that *must* be selected, otherwise
 * an unambiguous remote license must be selected (e.g., if the same license
 * name exists for multiple server values, we must not randomly select one and
 * prefer to error out.
 */
static int _foreach_fuzzy_match_remote(void *x, void *arg)
{
	licenses_t *license_entry = x;
	fuzzy_match_remote_args_t *args = arg;
	int ismatch = _license_remote_fuzzy_find_rec(license_entry, args->name);

	xassert(ismatch < LIC_MAX_MATCH);
	if (ismatch == LIC_EXACT_MATCH) {
		args->match = license_entry;
		return -1;
	} else if (ismatch == LIC_FUZZY_MATCH) {
		if (args->match) {
			/*
			 * there is a previous match meaning that there is
			 * ambiguity about which server the user might mean,
			 * fail
			 */
			args->match = NULL;
			return -1;
		}
		args->match = license_entry;
	}

	return 0;
}

static licenses_t *_fuzzy_match_remote_licenses(char *name)
{
	fuzzy_match_remote_args_t args = {
		.name = name,
	};

	/*
	 * have to go through the entire list to ensure we pick either the
	 * exact local match or the unambiguous remote match
	 */
	list_for_each_ro(cluster_license_list, _foreach_fuzzy_match_remote,
			 &args);

	return args.match;
}

static int _license_find_root_rec(void *x, void *key)
{
	licenses_t *license_entry = x;

	if (!_license_find_rec(x, key))
		return 0;
	if ((license_entry->mode == HRES_MODE_3) &&
	    (license_entry->hres_rec.parent))
		return 0;
	return 1;
}

static int _license_find_variables_rec(void *x, void *key)
{
	licenses_t *license_entry = x;

	if (!_license_find_rec(x, key))
		return 0;

	if (!license_entry->hres_rec.variables)
		return 0;
	return 1;
}

/* Find a mode 3 HRES record that overlaps nodes and is on the same level */
static int _license_find_overlap_mode3(void *x, void *key)
{
	licenses_t *license_entry = x;
	licenses_t *license_key = key;

	if (license_entry->mode != HRES_MODE_3)
		return 0;
	/* Don't match on self */
	if (license_entry == license_key)
		return 0;
	/* Must be the same HRES ID */
	if (license_entry->id.hres_id != license_key->id.hres_id)
		return 0;
	/* Must be the same level */
	if (license_entry->hres_rec.level != license_key->hres_rec.level)
		return 0;
	if (!license_entry->node_bitmap || !license_key->node_bitmap)
		return 0;

	return bit_overlap_any(license_entry->node_bitmap,
			       license_key->node_bitmap);
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

static int _license_find_mode3(void *x, void *key)
{
	licenses_t *license_entry = x;

	if (license_entry->mode == HRES_MODE_3)
		return 1;

	return 0;
}

static int _license_find_root_mode3(void *x, void *key)
{
	licenses_t *license_entry = x;
	licenses_id_t *id = key;

	if ((license_entry->id.hres_id == id->hres_id) &&
	    (!license_entry->hres_rec.parent))
		return 1;

	return 0;
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

static int _license_find_non_hres_rec_in_list_by_id(void *x, void *key)
{
	licenses_t *license_entry = x;
	list_t *licenses = key;

	/* Ignore hres licenses */
	if (license_entry->id.hres_id != NO_VAL16)
		return 0;

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

static int _variable_find(void *x, void *key)
{
	hres_variable_t *var = x;
	char *name = key;
	if (!xstrcmp(var->name, name))
		return 1;

	return 0;
}

static int _license_find_layer(void *x, void *key)
{
	licenses_t *lic = x;
	licenses_find_layer_t *args = key;

	/*
	 * Check if it is an HRES by layer_name rather than by hres_id since
	 * this function can be used before IDs are assigned.
	 */
	if (!lic->hres_rec.layer_name)
		return 0;
	if (xstrcmp(lic->name, args->hres_name))
		return 0;
	if (!xstrcasecmp(lic->hres_rec.layer_name, args->layer_name))
		return 1;
	return 0;
}

/* Given a license string, return a list of license_t records */
static list_t *_build_license_list(char *licenses, bool *valid,
				   hres_syntax_t hres_syntax,
				   bool check_variables)
{
	int i;
	char *end_num, *tmp_str, *token;
	char *delim = ",;";
	licenses_t *license_entry;
	list_t *lic_list;

	*valid = true;
	if ((licenses == NULL) || (licenses[0] == '\0'))
		return NULL;

	if (strchr(licenses, '|')) {
		if (strchr(licenses, ',') || strchr(licenses, ';') ||
		    strchr(licenses, '(')) {
			/* Both OR and AND requested, invalid */
			*valid = false;
			return NULL;
		}
		delim = "|";
	}

	lic_list = list_create(license_free_rec);
	tmp_str = xstrdup(licenses);
	token = tmp_str;
	while (*token && *valid) {
		int32_t num = 1;
		char *hres_charge = NULL;
		char *name = token;
		if (strchr(delim, token[0])) {
			token++;
			continue;
		}

		for (i = 0; token[i]; i++) {
			if (isspace(token[i])) {
				*valid = false;
				break;
			}

			if ((token[i] == '(') && hres_syntax) {
				token[i++] = '\0';
				hres_charge = &(token[i]);
				token = strchr(hres_charge, ')');
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
				if (&token[i] == end_num && check_variables) {
					hres_variable_t *var = NULL;
					licenses_t *match = NULL;
					char *var_name_str = end_num;
					char *var_name_str_end =
						strpbrk(&token[i], delim);

					slurm_mutex_lock(&license_mutex);
					if (cluster_license_list)
						match = list_find_first(
							cluster_license_list,
							_license_find_variables_rec,
							name);

					if (var_name_str_end)
						*var_name_str_end = '\0';
					if (match &&
					    match->hres_rec.variables) {
						var = list_find_first_ro(
							match->hres_rec
								.variables,
							_variable_find,
							var_name_str);
					}
					if (var) {
						num = var->value;
					} else {
						*valid = false;
					}
					slurm_mutex_unlock(&license_mutex);
					if (var_name_str_end) {
						*var_name_str_end = delim[0];
						end_num = var_name_str_end;
					} else
						*end_num = '\0';
				}
				if ((*end_num != '\0') &&
				    !strchr(delim, *end_num))
					*valid = false;
				token = end_num;
				break;
			}

			if (strchr(delim, token[i])) {
				token[i++] = '\0';
				token = &(token[i]);
				break;
			}
		}

		if (name == token)
			token = &(token[i]);

		if (num < 0 || !(*valid)) {
			*valid = false;
			break;
		}
		if (hres_charge) {
			licenses_find_rec_by_nodes_t args = {
				.name = name,
				.nodes = hres_charge,
			};
			/*
			 * Reuse the nodes field and _license_find_rec_by_nodes
			 * to match by string and find duplicate entries.
			 */
			license_entry =
				list_find_first(lic_list,
						_license_find_rec_by_nodes,
						&args);
		} else {
			license_entry =
				list_find_first(lic_list, _license_find_rec,
						name);
		}

		if (license_entry) {
			license_entry->total += num;
		} else if (num > 0) {
			license_entry = xmalloc(sizeof(licenses_t));
			license_entry->id.lic_id = NO_VAL16;
			license_entry->id.hres_id = NO_VAL16;
			license_entry->name = xstrdup(name);
			/*
			 * Store hres charge in the nodes field, to be resolved
			 * by _resolve_hres_layers().
			 */
			license_entry->nodes = xstrdup(hres_charge);
			license_entry->total = num;
			if (delim[0] == '|')
				license_entry->op_or = true;
			/* Append to preserve the order requested by the user */
			list_append(lic_list, license_entry);
		} else {
			log_flag(LICENSE, "%s: dropping zero-count request for license %s",
				 __func__, name);
		}
	}
	xfree(tmp_str);

	if (*valid == false) {
		FREE_NULL_LIST(lic_list);
	}
	if (lic_list && !list_count(lic_list)) {
		log_flag(LICENSE, "%s: all requested licenses were zero-count; treating as no license request",
			 __func__);
		FREE_NULL_LIST(lic_list);
	}
	return lic_list;
}

static int _foreach_hres_charge_to_string(void *x, void *arg)
{
	hres_charge_t *charge = x;
	license_list_to_string_args_t *args = arg;

	xstrfmtcat(args->layers, "%s%s", (args->layers ? "," : ""),
		   charge->layer_name);
	if (charge->node_cnt > 1)
		xstrfmtcat(args->layers, "*%u", charge->node_cnt);

	return 0;
}

/*
 * Build the layer part of an HRES entry: "(layer[*node_cnt][,...])". The
 * node_cnt is only meaningful for mode 3, where a job consumes the count once
 * per node of the layer, so it is left out when it is one.
 */
static char *_hres_charges_to_string(list_t *hres_charges)
{
	license_list_to_string_args_t args = { 0 };

	list_for_each_ro(hres_charges, _foreach_hres_charge_to_string, &args);

	return args.layers;
}

static int _foreach_license_list_to_string(void *x, void *arg)
{
	licenses_t *license_entry = x;
	license_list_to_string_args_t *args = arg;
	char *layers = NULL;

	if (license_entry->hres_charges)
		layers = _hres_charges_to_string(license_entry->hres_charges);
	else if (license_entry->nodes)
		layers = xstrdup(license_entry->nodes);

	if (layers)
		xstrfmtcat(args->licenses, "%s%s(%s):%u", args->sep,
			   license_entry->name, layers, license_entry->total);
	else
		xstrfmtcat(args->licenses, "%s%s:%u", args->sep,
			   license_entry->name, license_entry->total);
	args->sep = license_entry->op_or ? "|" : ";";
	xfree(layers);

	return 0;
}

extern char *license_list_to_string(list_t *license_list)
{
	license_list_to_string_args_t args = {
		.sep = "",
	};

	if (!license_list)
		return NULL;

	list_for_each_ro(license_list, _foreach_license_list_to_string, &args);

	return args.licenses;
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

	license_entry->id.hres_id = NO_VAL16;

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
	if (match->total == INFINITE)
		return true;

	return (request->total + match->used + match->last_deficit +
		resv_licenses) <= match->total;
}

/*
 * Find another layer (not the same as the given layer) in the same HRES in
 * a list of hres_update_nodes_t.
 */
static int _hres_update_find_other_layer(void *x, void *key)
{
	hres_update_nodes_t *hres_update = x;
	licenses_find_layer_t *args = key;

	if (xstrcmp(hres_update->lic->name, args->hres_name))
		return 0; /* Not the same HRES */
	if (!xstrcasecmp(hres_update->lic->hres_rec.layer_name,
			 args->layer_name))
		return 0; /* Same layer - don't match */
	return 1; /* Different layer, same HRES */
}

static int _find_dup_layer_name(void *x, void *key)
{
	licenses_t *lic = x;
	licenses_t *lic_key = key;

	/* Don't match on self */
	if (lic == lic_key)
		return 0;

	/* Only enforce uniqueness among the same HRES. */
	if (lic->id.hres_id != lic_key->id.hres_id)
		return 0;

	if (!xstrcasecmp(lic->hres_rec.layer_name,
			 lic_key->hres_rec.layer_name))
		return 1; /* Duplicate layer_name */
	return 0;
}

static int _foreach_validate_layer_name(void *x, void *arg)
{
	licenses_t *lic = x;
	char *layer_name = lic->hres_rec.layer_name;

	if (lic->id.hres_id == NO_VAL16)
		return 0;

	/*
	 * The layer_name identifies the layer in the license strings of jobs
	 * and reservations, which reserve these characters as separators.
	 */
	if (strpbrk(layer_name, HRES_LAYER_NAME_BAD_CHARS))
		fatal("HRES layer_name '%s' contains one of the reserved characters \"%s\" or whitespace",
		      layer_name, HRES_LAYER_NAME_BAD_CHARS);

	if (list_find_first_ro(cluster_license_list, _find_dup_layer_name, lic))
		fatal("Detected duplicate HRES layer_name: %s", layer_name);
	return 0;
}

static void _parse_hierarchical_resources(list_t **license_list_ptr)
{
	int rc = EINVAL;
	bool allocated_here = false;

	xassert(license_list_ptr);

	if (!*license_list_ptr) {
		*license_list_ptr = list_create(license_free_rec);
		allocated_here = true;
	}

	if ((rc = CONF_PARSE(H_RESOURCES_AS_LICENSE_LIST, "resources.yaml",
			     *license_list_ptr))) {
		if (rc == ENOENT) {
			if (allocated_here)
				FREE_NULL_LIST(*license_list_ptr);
			return;
		}
		fatal("Something wrong with reading resources.yaml: %s",
		      slurm_strerror(rc));
	}

	if ((slurm_conf.debug_flags & DEBUG_FLAG_LICENSE)) {
		char *dump_str = NULL;
		int rc_dump = EINVAL;

		if ((rc_dump =
			     SERCLI_DUMP_STR(H_RESOURCES_AS_LICENSE_LIST, NULL,
					     *license_list_ptr, dump_str,
					     MIME_TYPE_YAML, SER_FLAGS_NO_TAG)))
			log_flag(LICENSE,
				 "%s: Hierarchical resources dump failed: %s",
				 __func__, slurm_strerror(rc_dump));
		else
			log_flag(LICENSE,
				 "%s: Dump hierarchical resources:\n %s",
				 __func__, dump_str);

		xfree(dump_str);
	}
}

extern void license_init(void)
{
	if (xstrcasestr(slurm_conf.preempt_params, "reclaim_licenses"))
		preempt_for_licenses = true;

	last_license_update = time(NULL);

	slurm_mutex_lock(&license_mutex);

	next_lic_id = 0;

	slurm_mutex_unlock(&license_mutex);
}

static int _foreach_license_set_hres(void *x, void *key)
{
	licenses_t *license = x;
	licenses_t *hres_head =
		list_find_first_ro(cluster_license_list, _license_find_rec,
				   license->name);

	if (!hres_head) {
		fatal("%s:_license_find_rec: %s not in cluster_license_list",
		      __func__,
		      license->name);
	}

	if (license->mode != HRES_MODE_OFF) {
		if (hres_head->mode != license->mode) {
			fatal("%s HRES Mode mismatch %s", __func__,
			      license->name);
		}
		if (license->hres_rec.parent_name &&
		    (license->mode != HRES_MODE_3)) {
			fatal("%s: HRES=%s layer=%s parent specified for non-mode 3 HRES",
			      __func__, license->name,
			      license->hres_rec.layer_name);
		}
		if (license != hres_head) {
			license->id.hres_id = hres_head->id.hres_id;
		} else
			license->id.hres_id = license->id.lic_id;

		if (license->nodes) {
			if (node_name2bitmap(license->nodes, false,
					     &license->node_bitmap, NULL)) {
				fatal("%s: HRES=%s layer=%s can't set node_bitmap for nodes=%s",
				      __func__, license->name,
				      license->hres_rec.layer_name,
				      license->nodes);
			}
		} else {
			license->node_bitmap = bit_alloc(node_record_count);
		}
	} else {
		if (license != hres_head) {
			fatal("%s duplicate license %s", __func__,
			      license->name);
		}
		license->id.hres_id = NO_VAL16;
	}

	return 0;
}

static int _foreach_license_set_mode3(void *x, void *arg)
{
	licenses_t *license = x;
	foreach_hres_set_mode3_t *args = arg;

	if (license->mode == HRES_MODE_3) {
		licenses_t *parent;
		licenses_find_layer_t find_layer = {
			.hres_name = license->name,
			.layer_name = license->hres_rec.parent_name,
		};

		parent = list_find_first_ro(cluster_license_list,
					    _license_find_layer, &find_layer);
		if (!parent) {
			if (find_layer.layer_name) {
				error("%s: HRES=%s layer=%s: could not find parent=%s",
				      __func__, license->name,
				      license->hres_rec.layer_name,
				      find_layer.layer_name);
				return -1;
			}
			if (args->prev_hres_id == license->id.hres_id) {
				error("%s %s isn't a rooted tree", __func__,
				      license->name);
				return -1;
			}
			args->prev_hres_id = license->id.hres_id;

			if (license->hres_rec.topology_name) {
				if (topology_g_get(TOPO_DATA_TCTX_IDX,
						   license->hres_rec
							   .topology_name,
						   &(license->hres_rec
							     .topology_idx))) {
					error("%s %s: topology %s doesn't exist ",
					      __func__, license->name,
					      license->hres_rec.topology_name);
					return -1;
				}
			} else
				license->hres_rec.topology_idx = -1;
		} else if (parent->nodes) {
			error("%s: Mode 3 HRES=%s non-leaf layer=%s defined nodes, only leaves can define nodes",
			      __func__, license->name,
			      parent->hres_rec.layer_name);
			return -1;
		} else if (parent == license) {
			error("%s: Mode 3 HRES=%s layer=%s parent specified as self",
			      __func__, license->name,
			      license->hres_rec.layer_name);
			return -1;
		} else {
			if (license->hres_rec.topology_name) {
				xassert(!parent->hres_rec.topology_name);
				parent->hres_rec.topology_name =
					license->hres_rec.topology_name;
				license->hres_rec.topology_name = NULL;
			}
			license->hres_rec.parent = parent;
		}
	}
	return 0;
}

static int _foreach_license_set_idx_level(void *x, void *arg)
{
	licenses_t *license = x;
	foreach_hres_set_mode3_t *args = arg;

	if (license->mode != HRES_MODE_3)
		return 0;

	license->hres_rec.idx = args->idx++;
	license->hres_rec.path_idx[0] = license->hres_rec.idx;

	if (!license->hres_rec.parent) {
		/* root is last, reset idx for next HRES */
		args->idx = 0;
		args->prev_hres_id = license->id.hres_id;
	} else {
		/*
		 * Leaf-first sorting allows us to simply set level of the
		 * parent as we iterate through the list. Multiple layers with
		 * the same parent will still set the parent's level to the
		 * same value.
		 */
		license->hres_rec.parent->hres_rec.level =
			license->hres_rec.level + 1;
	}
	return 0;
}

static int _foreach_license_set_path(void *x, void *key)
{
	licenses_t *license = x;
	licenses_t *parent;
	int i;

	/*
	 * Path is an array of hres_idx:
	 * [self, parent, grandparent, ..., root]
	 */
	for (i = 0, parent = license->hres_rec.parent; parent;
	     i++, parent = parent->hres_rec.parent) {
		license->hres_rec.path_idx[i + 1] = parent->hres_rec.idx;
	}

	return 0;
}

static int _foreach_license_set_depth(void *x, void *arg)
{
	licenses_t *license = x;

	if (license->mode != HRES_MODE_3)
		return 0;
	license->hres_rec.depth = 1; /* root has depth == 1 */
	for (licenses_t *parent = license->hres_rec.parent; parent;
	     parent = parent->hres_rec.parent) {
		license->hres_rec.depth++;
		/* The if check here will guard against recursive parents */
		if (license->hres_rec.depth > MAX_HIERARCHY_DEPTH) {
			fatal("%s: depth of HRES=%s is greater than %d",
			      __func__, license->name, MAX_HIERARCHY_DEPTH);
		}
	}

	return 0;
}

static int _foreach_license_set_cnt(void *x, void *key)
{
	licenses_t *license = x;
	licenses_t **root = key;

	if (license->mode != HRES_MODE_3)
		return 0;

	if (!*root || ((*root)->id.hres_id != license->id.hres_id))
		*root = license;
	else if ((*root)->hres_rec.level < license->hres_rec.level) {
		license->hres_rec.layers_cnt = (*root)->hres_rec.layers_cnt;
		license->hres_rec.leaf_cnt = (*root)->hres_rec.leaf_cnt;
		*root = license;
	}

	if (!license->hres_rec.level)
		(*root)->hres_rec.leaf_cnt++;
	(*root)->hres_rec.layers_cnt++;

	return 0;
}

static int _foreach_base_set(void *x, void *arg)
{
	hres_variable_t *var = x;
	licenses_t *license = arg;

	if ((var->value >= NO_VAL) ||
	    ((NO_VAL - var->value) <= license->hres_rec.base_usage)) {
		error("%s: HRES=%s layer=%s base overflows",
		      __func__, license->name, license->hres_rec.layer_name);
		return -1;
	}
	license->hres_rec.base_usage += var->value;

	return SLURM_SUCCESS;
}

static int _set_base(licenses_t *license)
{
	if (license->hres_rec.base)
		if (list_for_each_ro(license->hres_rec.base, _foreach_base_set,
				     license) < 0)
			return ESLURM_HRES_BASE_OVERFLOW;

	if (license->hres_rec.total < license->hres_rec.base_usage) {
		error("%s HRes %s base greater than total", __func__,
		      license->name);
		return ESLURM_INVALID_HRES_COUNT;
	}

	license->total = license->hres_rec.total;

	if (license->total != INFINITE)
		license->total -= license->hres_rec.base_usage;
	return 0;
}

static int _foreach_license_set_base(void *x, void *key)
{
	licenses_t *license = x;

	if (license->mode == HRES_MODE_OFF)
		return 0;

	if (_set_base(license))
		return -1;

	if ((license->mode == HRES_MODE_3) && (license->hres_rec.parent)) {
		licenses_t *parent = license->hres_rec.parent;

		if ((NO_VAL - license->hres_rec.base_usage) <=
		    parent->hres_rec.base_usage) {
			error("%s: HRES=%s layer=%s base_usage overflows at parent layer=%s",
			      __func__, license->name,
			      license->hres_rec.layer_name,
			      parent->hres_rec.layer_name);
			return -1;
		}
		parent->hres_rec.base_usage += license->hres_rec.base_usage;
	}

	return 0;
}

static int _sort_hres(void *void1, void *void2)
{
	licenses_t *lic1 = *(licenses_t **) void1;
	licenses_t *lic2 = *(licenses_t **) void2;

	if (lic1->id.hres_id > lic2->id.hres_id)
		return -1;
	else if (lic1->id.hres_id < lic2->id.hres_id)
		return 1;

	if (lic1->id.hres_id == NO_VAL16)
		return 0;

	if (lic1->mode == HRES_MODE_1) {
		if (lic1->total > lic2->total)
			return -1;
		else if (lic1->total < lic2->total)
			return 1;
	}

	if (lic1->mode == HRES_MODE_3) {
		/* Sort by depth: leaves (greater depth) first, root last. */
		if (lic1->hres_rec.depth > lic2->hres_rec.depth)
			return -1;
		else if (lic1->hres_rec.depth < lic2->hres_rec.depth)
			return 1;
	}

	return 0;
}

static int _foreach_license_mode3_no_overlap(void *x, void *arg)
{
	licenses_t *license = x;
	licenses_t *overlap_lic;

	if (license->mode != HRES_MODE_3)
		return 0;

	overlap_lic = list_find_first_ro(cluster_license_list,
					 _license_find_overlap_mode3, license);
	if (overlap_lic) {
		error("%s: HRES=%s: layer=%s nodes=%s overlaps with layer=%s nodes=%s. Mode 3 layers on the same level must be disjoint.",
		      __func__, license->name, license->hres_rec.layer_name,
		      license->nodes, overlap_lic->hres_rec.layer_name,
		      overlap_lic->nodes);
		return -1;
	}
	return 0;
}

static int _foreach_license_mode3_uniform_depth(void *x, void *arg)
{
	licenses_t *license = x;
	foreach_uniform_depth_t *args = arg;

	if (license->mode != HRES_MODE_3)
		return 0;
	if (license->hres_rec.level)
		return 0; /* Not a leaf */
	if (args->curr_hres_id != license->id.hres_id) {
		/* New HRES record */
		args->curr_hres_id = license->id.hres_id;
		args->depth = license->hres_rec.depth;
		args->first_leaf = license->hres_rec.layer_name;
		return 0;
	}
	if (args->depth != license->hres_rec.depth) {
		error("%s: HRES=%s has non-uniform depth: leaf=%s depth=%hu != leaf=%s depth=%hu",
		      __func__, license->name, license->hres_rec.layer_name,
		      license->hres_rec.depth, args->first_leaf,
		      args->depth);
		return -1;
	}

	return 0;
}

/*
 * Validate that all leaf layers have the same depth within each mode 3 HRES.
 * Layers of the same HRES are contiguous in the list by construction, so all
 * leaves of one HRES are found before encountering any other HRES.
 */
static bool _is_uniform_depth(void)
{
	foreach_uniform_depth_t arg = {
		.curr_hres_id = NO_VAL16,
		.depth = NO_VAL16,
	};

	if (list_for_each_ro(cluster_license_list,
			     _foreach_license_mode3_uniform_depth, &arg) < 0)
		return false;
	return true;
}

/*
 * Canonical nodes string of a layer: the ranged node list, or NULL when the
 * layer holds no node, the same as a layer configured without nodes.
 * IN node_bitmap - nodes of the layer
 * RET xmalloc'd string, or NULL
 */
static char *_layer_nodes_str(bitstr_t *node_bitmap)
{
	if (bit_ffs(node_bitmap) == -1)
		return NULL;
	return bitmap2node_name(node_bitmap);
}

static int _foreach_license_set_mode3_nodes(void *x, void *arg)
{
	licenses_t *license = x;
	licenses_t *parent;

	if (license->mode != HRES_MODE_3)
		return 0;
	/*
	 * The list is sorted leaf first. When arriving at a non-leaf, all of
	 * its children's nodes have been OR'd into its node_bitmap. We can now
	 * generate the nodes string.
	 */
	if (license->hres_rec.level) { /* non-leaf */
		xfree(license->nodes);
		license->nodes = _layer_nodes_str(license->node_bitmap);
	}
	parent = license->hres_rec.parent;
	if (parent) {
		if (!parent->node_bitmap)
			parent->node_bitmap = bit_copy(license->node_bitmap);
		else
			bit_or(parent->node_bitmap, license->node_bitmap);
	}
	return 0;
}

extern int hres_init(void)
{
	licenses_t *root = NULL;
	foreach_hres_set_mode3_t arg = {
		.idx = 0,
		.prev_hres_id = NO_VAL16,
	};

	slurm_mutex_lock(&license_mutex);
	if (!cluster_license_list) {
		slurm_mutex_unlock(&license_mutex);
		return SLURM_SUCCESS;
	}

	last_license_update = time(NULL);

	list_for_each_ro(cluster_license_list, _foreach_license_set_hres, NULL);
	/* Enforce uniqueness of hres_rec.layer_name */
	list_for_each_ro(cluster_license_list, _foreach_validate_layer_name,
			 NULL);

	if (list_for_each_ro(cluster_license_list, _foreach_license_set_mode3,
			     &arg) < 0)
		fatal("Can't set MODE3");
	list_for_each_ro(cluster_license_list, _foreach_license_set_depth,
			 NULL);

	/* Sort leaf-first before setting level and testing uniform depth */
	list_sort(cluster_license_list, _sort_hres);

	/* Reuse arg */
	arg.idx = 0;
	arg.prev_hres_id = NO_VAL16;
	list_for_each_ro(cluster_license_list, _foreach_license_set_idx_level,
			 &arg);
	if (!_is_uniform_depth())
		fatal("MODE3 HRES is not a uniform depth tree");
	list_for_each_ro(cluster_license_list, _foreach_license_set_path, NULL);

	if (list_for_each_ro(cluster_license_list,
			     _foreach_license_mode3_no_overlap, NULL) < 0)
		fatal("Invalid MODE3");
	/* Propagate leaf node_bitmaps up the tree */
	list_for_each(cluster_license_list, _foreach_license_set_mode3_nodes,
		      NULL);

	if (list_for_each_ro(cluster_license_list, _foreach_license_set_cnt,
			     &root) < 0)
		fatal("Can't set MODE3 cnt");

	if (list_for_each_ro(cluster_license_list, _foreach_license_set_base,
			     NULL) < 0)
		fatal("Can't set base");

	_licenses_print("hres_init", cluster_license_list, NULL);

	if ((slurm_conf.debug_flags & DEBUG_FLAG_LICENSE)) {
		char *dump_str = NULL;
		int rc = SLURM_SUCCESS;

		if ((rc = SERCLI_DUMP_STR(H_RESOURCES_AS_LICENSE_LIST, NULL,
					  cluster_license_list, dump_str,
					  MIME_TYPE_YAML, SER_FLAGS_NO_TAG)))
			log_flag(LICENSE, "%s: Hierarchical resources dump failed: %s",
			      __func__, slurm_strerror(rc));
		else
			log_flag(LICENSE, "%s: Dump hierarchical resources:\n %s",
				 __func__, dump_str);

		xfree(dump_str);
	}
	slurm_mutex_unlock(&license_mutex);
	return SLURM_SUCCESS;
}

static void _propagate_mode3_node_update(licenses_t *leaf, bitstr_t *added,
					 bitstr_t *removed)
{
	if (!bit_set_count(added) && !bit_set_count(removed))
		return; /* No change */
	for (licenses_t *parent = leaf->hres_rec.parent; parent;
	     parent = parent->hres_rec.parent) {
		bit_or(parent->node_bitmap, added);
		bit_and_not(parent->node_bitmap, removed);
		xfree(parent->nodes);
		parent->nodes = _layer_nodes_str(parent->node_bitmap);
	}
}

static void _update_hres_nodes(licenses_t *lic, bitstr_t *new_nodes_bitmap)
{
	bitstr_t *added;
	bitstr_t *removed;

	if (!new_nodes_bitmap)
		return;

	/*
	 * For mode 3, changes need to propagate up the tree to each parent
	 * such that the parent is still a superset of all of their children.
	 * To do this cleanly, compute the delta of the change.
	 */
	if (lic->mode == HRES_MODE_3) {
		/* Added = new & ~old */
		added = bit_copy(new_nodes_bitmap);
		bit_and_not(added, lic->node_bitmap);
		/* Removed = old & ~new */
		removed = bit_copy(lic->node_bitmap);
		bit_and_not(removed, new_nodes_bitmap);
	}
	/*
	 * DO NOT FREE node_bitmap. Perform in-place alterations only, because
	 * jobs with mode 3 HRES may have hres_select_t that alias to this
	 * pointer.
	 */
	bit_copybits(lic->node_bitmap, new_nodes_bitmap);
	xfree(lic->nodes);
	lic->nodes = _layer_nodes_str(lic->node_bitmap);

	if (lic->mode != HRES_MODE_3)
		return;

	_propagate_mode3_node_update(lic, added, removed);

	FREE_NULL_BITMAP(added);
	FREE_NULL_BITMAP(removed);
}

static int _validate_nodes(licenses_t *license, char *nodes,
			   bitstr_t *add_nodes_bitmap,
			   bitstr_t **new_nodes_bitmap, char **err_msg)
{
	char *tmp_nodes = NULL;
	bool additive = false;
	bool subtractive = false;
	int rc = SLURM_SUCCESS;
	hostlist_t *invalid_hostlist = NULL;

	if (!nodes)
		return SLURM_SUCCESS;

	if ((license->mode == HRES_MODE_3) && (license->hres_rec.level))
		return ESLURM_HRES_MODE3_NON_LEAF;

	if (nodes[0] == '+') {
		/* Add existing nodes instead of replacing existing nodes. */
		additive = true;
		tmp_nodes = nodes + 1;
	} else if (nodes[0] == '-') {
		/* Remove from existing nodes */
		subtractive = true;
		tmp_nodes = nodes + 1;
	} else {
		tmp_nodes = nodes;
	}
	if (add_nodes_bitmap) {
		*new_nodes_bitmap = bit_copy(add_nodes_bitmap);
	} else {
		rc = node_name2bitmap(tmp_nodes, false, new_nodes_bitmap,
				      &invalid_hostlist);
		if (invalid_hostlist) {
			char *str = hostlist_ranged_string_xmalloc(
				invalid_hostlist);

			*err_msg = xstrdup_printf("Invalid nodes: %s", str);
			xfree(str);
			FREE_NULL_HOSTLIST(invalid_hostlist);
			rc = ESLURM_INVALID_HRES_NODES;
			goto fini;
		} else if (rc) {
			rc = ESLURM_INVALID_HRES_NODES;
			goto fini;
		}
	}
	if (additive) {
		bit_or(*new_nodes_bitmap, license->node_bitmap);
	} else if (subtractive) {
		bit_not(*new_nodes_bitmap);
		bit_and(*new_nodes_bitmap, license->node_bitmap);
	}

	if (license->mode == HRES_MODE_3) {
		licenses_t *overlap_lic;
		bitstr_t *orig_bitmap = license->node_bitmap;

		/* Swap to new bitmap for the search */
		license->node_bitmap = *new_nodes_bitmap;
		overlap_lic = list_find_first_ro(cluster_license_list,
						 _license_find_overlap_mode3,
						 license);
		license->node_bitmap = orig_bitmap;
		if (overlap_lic) {
			char *tmp_dup_nodes =
				bitmap2node_name(*new_nodes_bitmap);

			*err_msg = xstrdup_printf(
				"Nodes=%s overlaps with layer=%s nodes=%s",
				tmp_dup_nodes, overlap_lic->hres_rec.layer_name,
				overlap_lic->nodes);
			xfree(tmp_dup_nodes);
			rc = ESLURM_HRES_MODE3_OVERLAP;
			goto fini;
		}
	}

fini:
	if (rc)
		FREE_NULL_BITMAP(*new_nodes_bitmap);
	return rc;
}

static int _validate_hres_update_nodes(char *hres_name, char *layer_name,
				       char *nodes, bitstr_t *add_nodes_bitmap,
				       licenses_t **lic_to_update,
				       bitstr_t **new_nodes_bitmap,
				       char **err_msg)
{
	licenses_find_layer_t find_layer = {
		.hres_name = hres_name,
		.layer_name = layer_name,
	};

	*lic_to_update = list_find_first_ro(cluster_license_list,
					    _license_find_layer, &find_layer);
	if (!(*lic_to_update)) {
		*err_msg = xstrdup_printf("HRES name=%s layer=%s not found",
					  hres_name, layer_name);
		return ESLURM_INVALID_HRES_NAME;
	}
	return _validate_nodes(*lic_to_update, nodes, add_nodes_bitmap,
			       new_nodes_bitmap, err_msg);
}

static void _log_hres_update_req(hres_update_msg_t *msg)
{
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_LICENSE))
		return;
	info("%s:", __func__);
	info("HRES Name=%s Layer=%s Nodes=%s Count=%u DisableHRES=%s DisableLayer=%s",
	     msg->hres_name, msg->layer_name, msg->nodes, msg->count,
	     (msg->disable_hres == NO_VAL16) ? "" :
	     (msg->disable_hres ? "true" : "false"),
	     (msg->disable_layer == NO_VAL16) ? "" :
	     (msg->disable_layer ? "true" : "false"));
	if (!msg->base)
		return;
	info("\tBase:");
	list_for_each(msg->base, _foreach_variable_print, NULL);
}

static int _foreach_base_sum(void *x, void *arg)
{
	hres_variable_t *var = x;
	uint32_t *sum = arg;

	*sum += var->value;
	return 0;
}

static int _update_hres_count_base(hres_update_msg_t *msg, licenses_t *lic,
				   char **err_msg)
{
	int rc;
	licenses_t tmp_lic = {
		.name = lic->name,
		.hres_rec.layer_name = lic->hres_rec.layer_name,
	};
	uint32_t orig_base_usage = lic->hres_rec.base_usage;
	uint32_t own_base_usage = 0;
	int64_t diff_base_usage = 0;

	/*
	 * base_usage also holds the base_usage propagated from children (mode
	 * 3). Only this layer's own base is being replaced, so keep the rest.
	 */
	if (lic->hres_rec.base)
		list_for_each_ro(lic->hres_rec.base, _foreach_base_sum,
				 &own_base_usage);
	tmp_lic.hres_rec.base_usage = orig_base_usage - own_base_usage;
	/* Use a temporary licenses_t to validate base and count */
	if (msg->base)
		tmp_lic.hres_rec.base = msg->base;
	else
		tmp_lic.hres_rec.base = lic->hres_rec.base;

	if (msg->count != NO_VAL)
		tmp_lic.hres_rec.total = msg->count;
	else
		tmp_lic.hres_rec.total = lic->hres_rec.total;

	/* Validate */
	if ((rc = _set_base(&tmp_lic)))
		return rc;
	diff_base_usage = (int64_t) tmp_lic.hres_rec.base_usage -
			  (int64_t) orig_base_usage;

	if ((lic->mode == HRES_MODE_3) && list_count(msg->base)) {
		/*
		 * base_usage propagates up the tree. Validate the new base
		 * does not exceed ancestors' total when propagated up.
		 *
		 * If msg->base != NULL but is empty (list_count() returns 0),
		 * we don't need this check because zero base_usage is always
		 * going to be <= total.
		 */
		for (licenses_t *parent = lic->hres_rec.parent; parent;
		     parent = parent->hres_rec.parent) {
			uint32_t new_base_usage;
			char *tmp_layer = parent->hres_rec.layer_name;

			if ((NO_VAL - diff_base_usage) <
			    parent->hres_rec.base_usage) {
				*err_msg = xstrdup_printf(
					"base_usage would overflow at layer=%s",
					tmp_layer);
				return ESLURM_HRES_BASE_OVERFLOW;
			}
			new_base_usage =
				parent->hres_rec.base_usage + diff_base_usage;
			if (parent->hres_rec.total < new_base_usage) {
				*err_msg = xstrdup_printf(
					"At ancestor=%s base_usage propagated up=%u exceeds total=%u",
					tmp_layer, new_base_usage,
					parent->hres_rec.total);
				return ESLURM_INVALID_HRES_COUNT;
			}
			/*
			 * Note: Because parent base usage is summed up from
			 * children, new base usage will always be >= 0, so
			 * we don't need to test for underflow.
			 */
		}
	}

	/* Update the real license record */
	if (msg->base)
		SWAP(lic->hres_rec.base, msg->base);
	lic->hres_rec.base_usage = tmp_lic.hres_rec.base_usage;
	lic->hres_rec.total = tmp_lic.hres_rec.total;
	lic->total = tmp_lic.total;

	if ((lic->mode != HRES_MODE_3) || !diff_base_usage)
		return SLURM_SUCCESS;

	/* Propagate mode 3 base_usage/count up the tree. */
	for (licenses_t *parent = lic->hres_rec.parent; parent;
	     parent = parent->hres_rec.parent) {
		parent->hres_rec.base_usage += diff_base_usage;
		if (parent->total != INFINITE)
			parent->total -= diff_base_usage;
	}

	return SLURM_SUCCESS;
}

/* A disable field is a boolean, or NO_VAL16 to leave it unchanged. */
static bool _valid_hres_disable(uint16_t disable)
{
	return ((disable == NO_VAL16) || (disable <= 1));
}

static int _foreach_update_disable_hres(void *x, void *arg)
{
	licenses_t *license = x;
	foreach_disable_hres_args_t *args = arg;

	if ((license->id.hres_id == NO_VAL16) ||
	    xstrcmp(license->name, args->hres_name))
		return 0;
	license->hres_rec.disable_hres = args->disable_hres;
	return 0;
}

extern int hres_update(hres_update_msg_t *msg, char **err_msg)
{
	int rc = SLURM_SUCCESS;
	licenses_t *lic = NULL;
	bitstr_t *new_nodes_bitmap = NULL;
	foreach_license_print_t print_arg = {
		.header = "Updated HRES",
	};
	foreach_disable_hres_args_t disable_hres_args = { 0 };

	_log_hres_update_req(msg);
	slurm_mutex_lock(&license_mutex);

	if (!cluster_license_list) {
		rc = ESLURM_INVALID_HRES_NAME;
		goto fini;
	}
	if (!_valid_hres_disable(msg->disable_hres) ||
	    !_valid_hres_disable(msg->disable_layer)) {
		rc = ESLURM_HRES_INVALID_DISABLE;
		goto fini;
	}
	if (!msg->layer_name) {
		/*
		 * Only disable_hres is a valid update without a layer name.
		 * Invalidate request if any other update is specified, or if
		 * disable_hres is missing.
		 */
		if ((msg->disable_hres == NO_VAL16) ||
		    (msg->disable_layer != NO_VAL16) || msg->base ||
		    (msg->count != NO_VAL) || msg->nodes) {
			rc = ESLURM_HRES_MISSING_LAYER_NAME;
			goto fini;
		}
	}
	if ((msg->disable_hres != NO_VAL16) && (!msg->layer_name)) {
		/*
		 * No layer specified. Validate that an HRES of hres_name
		 * exists.
		 */
		lic = list_find_first_ro(cluster_license_list,
					 _license_find_rec, msg->hres_name);
		if (!lic || (lic->id.hres_id == NO_VAL16)) {
			rc = ESLURM_INVALID_HRES_NAME;
			goto fini;
		}
		goto disable_hres_update;
	}
	if ((rc = _validate_hres_update_nodes(msg->hres_name, msg->layer_name,
					      msg->nodes, NULL, &lic,
					      &new_nodes_bitmap, err_msg)))
		goto fini;

	if ((rc = _update_hres_count_base(msg, lic, err_msg)))
		goto fini;

	_update_hres_nodes(lic, new_nodes_bitmap);

	if (msg->disable_layer != NO_VAL16)
		lic->hres_rec.disable_layer = msg->disable_layer;

disable_hres_update:
	/*
	 * disable_hres applies to all layers, whether or not a
	 * specific layer is requested.
	 */
	if (msg->disable_hres != NO_VAL16) {
		disable_hres_args.hres_name = msg->hres_name;
		disable_hres_args.disable_hres = msg->disable_hres;
		list_for_each(cluster_license_list,
			      _foreach_update_disable_hres, &disable_hres_args);
	}

	last_license_update = time(NULL);

	if (slurm_conf.debug_flags & DEBUG_FLAG_LICENSE) {
		if (!msg->layer_name) {
			foreach_print_hres_arg_t print_hres_args = {
				.hres_name = msg->hres_name,
				.print_arg = &print_arg,
			};

			list_for_each(cluster_license_list,
				      _foreach_license_print_hres,
				      &print_hres_args);
		} else {
			_foreach_license_print(lic, &print_arg);
		}
	}
fini:
	if (rc && (slurm_conf.debug_flags & DEBUG_FLAG_LICENSE)) {
		if (*err_msg) {
			log_flag(LICENSE, "%s: %s: %s",
				 __func__, slurm_strerror(rc), *err_msg);
		} else {
			log_flag(LICENSE, "%s: %s",
				 __func__, slurm_strerror(rc));
		}
	}
	FREE_NULL_BITMAP(new_nodes_bitmap);
	slurm_mutex_unlock(&license_mutex);
	return rc;
}

static void _free_hres_update(void *x)
{
	hres_update_nodes_t *hres_update = x;

	if (!x)
		return;
	/* Do not free hres_update->lic */
	FREE_NULL_BITMAP(hres_update->new_nodes_bitmap);
	xfree(hres_update);
}

static int _foreach_hres_add_nodes_validate(void *x, void *arg)
{
	node_hres_info_t *hres_info = x;
	foreach_hres_add_node_t *args = arg;
	hres_update_nodes_t *hres_update = NULL;
	hres_update_nodes_t *update_same_mode3 = NULL;
	licenses_t *lic = NULL;
	bitstr_t *new_nodes_bitmap = NULL;
	licenses_find_layer_t find_layer = {
		.hres_name = hres_info->hres_name,
		.layer_name = hres_info->layer_name,
	};

	args->rc =
		_validate_hres_update_nodes(hres_info->hres_name,
					    hres_info->layer_name,
					    args->node_names,
					    args->add_node_bitmap, &lic,
					    &new_nodes_bitmap, &args->err_msg);
	if (args->rc)
		return -1;
	/*
	 * Mode 3 - only allow an update in a single leaf layer in any given
	 * HRES. Don't allow an update to non-leaf layers, because this is
	 * inherently tied to a node, and node updates are not allowed in
	 * non-leaf layers.
	 *
	 * Modes 1/2 - layers are independent of one another, so each
	 * validation can occur independently.
	 */
	if (lic->mode == HRES_MODE_3) {
		update_same_mode3 =
			list_find_first(args->updates,
					_hres_update_find_other_layer,
					&find_layer);
		if (update_same_mode3) {
			char *other_layer_name =
				update_same_mode3->lic->hres_rec.layer_name;

			args->rc = ESLURM_HRES_MODE3_OVERLAP;
			args->err_msg = xstrdup_printf(
				"Node requests multiple layers (%s and %s) in the same mode 3 HRES=%s",
				other_layer_name, hres_info->layer_name,
				hres_info->hres_name);
			FREE_NULL_BITMAP(new_nodes_bitmap);
			return -1;
		}
	}

	hres_update = xmalloc(sizeof(*hres_update));
	hres_update->lic = lic;
	hres_update->new_nodes_bitmap = new_nodes_bitmap;
	list_append(args->updates, hres_update);
	return 0;
}

static int _foreach_hres_add_nodes(void *x, void *arg)
{
	hres_update_nodes_t *hres_update = x;
	foreach_license_print_t *print_arg = arg;

	_update_hres_nodes(hres_update->lic, hres_update->new_nodes_bitmap);
	if (slurm_conf.debug_flags & DEBUG_FLAG_LICENSE)
		_foreach_license_print(hres_update->lic, print_arg);
	return 0;
}

extern int hres_add_nodes(list_t *hres_info, char *node_names,
			  bitstr_t *node_bitmap)
{
	foreach_hres_add_node_t args = {
		.add_node_bitmap = node_bitmap,
	};
	foreach_license_print_t print_arg = { 0 };

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, READ_LOCK));

	slurm_mutex_lock(&license_mutex);
	if (!cluster_license_list) {
		args.rc = ESLURM_INVALID_HRES_NAME;
		goto fini;
	}
	args.updates = list_create(_free_hres_update);
	/*
	 * The "+" indicates this is an addition to existing nodes, rather than
	 * a replacement.
	 */
	args.node_names = xstrdup_printf("+%s", node_names);
	if (list_for_each(hres_info, _foreach_hres_add_nodes_validate, &args) <
	    0)
		goto fini;
	if (slurm_conf.debug_flags & DEBUG_FLAG_LICENSE)
		print_arg.header =
			xstrdup_printf("%s %s HRES", __func__, node_names);
	list_for_each(args.updates, _foreach_hres_add_nodes, &print_arg);
	xfree(print_arg.header);
fini:
	slurm_mutex_unlock(&license_mutex);
	if (args.rc) {
		if (args.err_msg)
			error("%s: %s: %s",
			      __func__, slurm_strerror(args.rc), args.err_msg);
		else
			error("%s: %s",
			      __func__, slurm_strerror(args.rc));
	}
	xfree(args.err_msg);
	xfree(args.node_names);
	FREE_NULL_LIST(args.updates);
	return args.rc;
}

static int _foreach_hres_rm_node(void *x, void *arg)
{
	licenses_t *license = x;
	node_record_t *node_ptr = arg;

	if (license->mode == HRES_MODE_OFF)
		return 0;
	/*
	 * Instead of doing the more complicated logic in _update_hres_nodes(),
	 * we are only removing a single node from all HRES, so we can simply
	 * do bit_clear() here. Optimization: rebuild the "nodes" string only
	 * if necessary by using bit_test(), which is much cheaper than
	 * bitmap2node_name().
	 */
	if (bit_test(license->node_bitmap, node_ptr->index)) {
		bit_clear(license->node_bitmap, node_ptr->index);
		xfree(license->nodes);
		license->nodes = _layer_nodes_str(license->node_bitmap);
	}
	return 0;
}

extern void hres_rm_node(node_record_t *node_ptr)
{
	char *header = NULL;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, READ_LOCK));

	if (slurm_conf.debug_flags & DEBUG_FLAG_LICENSE)
		header = xstrdup_printf("(%s %s)", __func__, node_ptr->name);
	slurm_mutex_lock(&license_mutex);
	if (cluster_license_list) {
		list_for_each(cluster_license_list, _foreach_hres_rm_node,
			      node_ptr);
		_licenses_print(header, cluster_license_list, NULL);
	}
	slurm_mutex_unlock(&license_mutex);
	xfree(header);
}

static int _foreach_hres_filter_mode1(void *x, void *arg)
{
	licenses_t *match = x;
	foreach_hres_filter_t *args = arg;
	int resv_licenses = 0;

	if (match->id.hres_id != args->license_entry->id.hres_id)
		return 0;
	if (match->hres_rec.disable_hres || match->hres_rec.disable_layer)
		return 0;

	resv_licenses =
		job_test_lic_resv(args->job_ptr, match->id, args->when, false);
	if (_sufficient_licenses(args->license_entry, match, resv_licenses))
		bit_or(args->node_mask, match->node_bitmap);

	return 0;
}

static int _foreach_hres_filter_mode2(void *x, void *arg)
{
	licenses_t *match = x;
	foreach_hres_filter_t *args = arg;
	int resv_licenses = 0;

	if (match->id.hres_id != args->license_entry->id.hres_id)
		return 0;
	if (match->hres_rec.disable_hres || match->hres_rec.disable_layer) {
		bit_and_not(args->node_mask, match->node_bitmap);
		return 0;
	}

	resv_licenses =
		job_test_lic_resv(args->job_ptr, match->id, args->when, false);

	if (!_sufficient_licenses(args->license_entry, match, resv_licenses))
		bit_and_not(args->node_mask, match->node_bitmap);

	return 0;
}

static int _foreach_bf_hres_filter_mode1(void *x, void *arg)
{
	bf_license_t *bf_lic = x;
	foreach_hres_filter_t *args = arg;

	if (bf_lic->id.hres_id != args->license_entry->id.hres_id)
		return 0;
	if (bf_lic->resv_ptr && (args->job_ptr->resv_ptr != bf_lic->resv_ptr))
		return 0;

	if (args->license_entry->total <= bf_lic->remaining) {
		licenses_t *match =
			list_find_first(cluster_license_list,
					_license_find_rec_by_id, &bf_lic->id);
		if (match)
			bit_or(args->node_mask, match->node_bitmap);
		else
			error("license id %d not found in cluster_license_list",
			      bf_lic->id.lic_id);
	}

	return 0;
}

static int _foreach_bf_hres_filter_mode2(void *x, void *arg)
{
	bf_license_t *bf_lic = x;
	foreach_hres_filter_t *args = arg;

	if (bf_lic->id.hres_id != args->license_entry->id.hres_id)
		return 0;
	if (bf_lic->resv_ptr && (args->job_ptr->resv_ptr != bf_lic->resv_ptr))
		return 0;

	if (args->license_entry->total > bf_lic->remaining) {
		licenses_t *match =
			list_find_first(cluster_license_list,
					_license_find_rec_by_id, &bf_lic->id);
		if (match)
			bit_and_not(args->node_mask, match->node_bitmap);
		else
			error("license id %d not found in cluster_license_list",
			      bf_lic->id.lic_id);
	}

	return 0;
}

static int _foreach_hres_filter(void *x, void *arg)
{
	licenses_t *license_entry = x;
	hres_filter_args_t *args = arg;
	bitstr_t *node_mask;
	foreach_hres_filter_t arg2 = {
		.job_ptr = args->job_ptr,
		.license_entry = license_entry,
		.when = args->when,
	};

	if ((license_entry->id.hres_id == NO_VAL16) ||
	    (license_entry->mode == HRES_MODE_3))
		return 0;

	node_mask = bit_alloc(node_record_count);
	arg2.node_mask = node_mask;

	list_for_each_ro(args->license_list, _foreach_hres_filter_mode1, &arg2);
	if (license_entry->mode == HRES_MODE_2)
		list_for_each_ro(args->license_list, _foreach_hres_filter_mode2,
				 &arg2);

	bit_and(args->node_bitmap, node_mask);
	FREE_NULL_BITMAP(node_mask);

	return 0;
}

extern int hres_filter_with_list(job_record_t *job_ptr, bitstr_t *node_bitmap,
				 list_t *license_list)
{
	hres_filter_args_t filter_args = {
		.job_ptr = job_ptr,
		.license_list = license_list,
		.node_bitmap = node_bitmap,
		.when = time(NULL),
	};

	if (!job_ptr->license_list || !license_list)
		return SLURM_SUCCESS;

	list_for_each_ro(job_ptr->license_list, _foreach_hres_filter,
			 &filter_args);
	return SLURM_SUCCESS;
}

extern int hres_filter(job_record_t *job_ptr, bitstr_t *node_bitmap)
{
	if (slurm_conf.debug_flags & DEBUG_FLAG_LICENSE) {
		char *tmp_str = bitmap2node_name(node_bitmap);
		verbose("%s: %pJ IN node_bitmap:%s",
			__func__, job_ptr, tmp_str);
		xfree(tmp_str);
	}
	slurm_mutex_lock(&license_mutex);

	hres_filter_with_list(job_ptr, node_bitmap, cluster_license_list);

	slurm_mutex_unlock(&license_mutex);
	if (slurm_conf.debug_flags & DEBUG_FLAG_LICENSE) {
		char *tmp_str = bitmap2node_name(node_bitmap);
		verbose("%s: %pJ OUT node_bitmap:%s",
			__func__, job_ptr, tmp_str);
		xfree(tmp_str);
	}
	return SLURM_SUCCESS;
}

static int _foreach_bf_hres_filter(void *x, void *arg)
{
	licenses_t *license_entry = x;
	bf_hres_filter_args_t *args = arg;
	bitstr_t *node_mask;
	foreach_hres_filter_t arg2 = {
		.job_ptr = args->job_ptr,
		.license_entry = license_entry,
	};

	if ((license_entry->id.hres_id == NO_VAL16) ||
	    (license_entry->mode == HRES_MODE_3))
		return 0;

	node_mask = bit_alloc(node_record_count);
	arg2.node_mask = node_mask;

	list_for_each_ro(args->bf_license_list, _foreach_bf_hres_filter_mode1,
			 &arg2);

	if (license_entry->mode == HRES_MODE_2)
		list_for_each_ro(args->bf_license_list,
				 _foreach_bf_hres_filter_mode2, &arg2);

	bit_and(args->node_bitmap, node_mask);
	FREE_NULL_BITMAP(node_mask);

	return 0;
}

extern void slurm_bf_hres_filter(job_record_t *job_ptr, bitstr_t *node_bitmap,
				 bf_licenses_t *bf_license_list)
{
	bf_hres_filter_args_t filter_args = {
		.bf_license_list = bf_license_list,
		.job_ptr = job_ptr,
		.node_bitmap = node_bitmap,
	};

	if (!job_ptr->license_list || !bf_license_list)
		return;

	if (slurm_conf.debug_flags & DEBUG_FLAG_LICENSE) {
		char *tmp_str = bitmap2node_name(node_bitmap);
		verbose("%s: %pJ IN node_bitmap:%s",
			__func__, job_ptr, tmp_str);
		xfree(tmp_str);
	}

	slurm_mutex_lock(&license_mutex);
	list_for_each_ro(job_ptr->license_list, _foreach_bf_hres_filter,
			 &filter_args);
	slurm_mutex_unlock(&license_mutex);

	if (slurm_conf.debug_flags & DEBUG_FLAG_LICENSE) {
		char *tmp_str = bitmap2node_name(node_bitmap);
		verbose("%s: %pJ OUT node_bitmap:%s",
			__func__, job_ptr, tmp_str);
		xfree(tmp_str);
	}
	return;
}

static int _foreach_hres_create_select(void *x, void *key)
{
	licenses_t *license = x;
	hres_select_t *hres_select = key;

	if (license->id.hres_id != hres_select->root_id.hres_id)
		return 0;

	if (license->hres_rec.level)
		return 0;

	/* Alias license->node_bitmap; do not copy */
	hres_select->leaf[hres_select->leaf_cnt].node_bitmap =
		license->node_bitmap;

	hres_select->depth = license->hres_rec.depth;

	for (int i = 0; i < hres_select->depth; i++)
		hres_select->leaf[hres_select->leaf_cnt].path_idx[i] =
			license->hres_rec.path_idx[i];

	hres_select->leaf_cnt++;

	return 0;
}

extern void hres_create_select(job_record_t *job_ptr)
{
	hres_select_t *hres_select = NULL;
	licenses_t *license_entry, *match;

	hres_select_free(job_ptr);

	if (!job_ptr->license_list)
		return;

	license_entry = list_find_first_ro(job_ptr->license_list,
					   _license_find_mode3, NULL);

	if (!license_entry)
		return;

	slurm_mutex_lock(&license_mutex);
	match = list_find_first_ro(cluster_license_list,
				   _license_find_root_mode3,
				   &(license_entry->id));

	if (!match) {
		slurm_mutex_unlock(&license_mutex);
		return;
	}

	hres_select = xmalloc(sizeof(*hres_select));
	hres_select->root_id = match->id;
	hres_select->hres_per_node = license_entry->total;
	hres_select->layers_cnt = match->hres_rec.layers_cnt;

	xassert(match->hres_rec.leaf_cnt);
	xassert(match->hres_rec.layers_cnt);

	hres_select->leaf =
		xcalloc(match->hres_rec.leaf_cnt, sizeof(hres_leaf_t));
	hres_select->avail_hres = xcalloc(match->hres_rec.layers_cnt,
					  sizeof(*hres_select->avail_hres));
	hres_select->avail_hres_orig =
		xcalloc(match->hres_rec.layers_cnt,
			sizeof(*hres_select->avail_hres_orig));
	list_for_each_ro(cluster_license_list, _foreach_hres_create_select,
			 hres_select);

	xassert(hres_select->leaf_cnt == match->hres_rec.leaf_cnt);

	hres_select->topology_idx = match->hres_rec.topology_idx;

	slurm_mutex_unlock(&license_mutex);

	job_ptr->hres_select = hres_select;
	hres_select_print(hres_select);
	return;
}

extern uint32_t hres_get_capacity(hres_select_t *hres_select, int leaf_idx)
{
	uint32_t min = INFINITE;

	if (leaf_idx < 0)
		return 0;
	for (int j = 0; j < hres_select->depth; j++) {
		uint16_t idx = hres_select->leaf[leaf_idx].path_idx[j];
		if (min > hres_select->avail_hres[idx])
			min = hres_select->avail_hres[idx];
	}

	return min;
}

static int _foreach_hres_pre_select(void *x, void *key)
{
	licenses_t *license = x;
	hres_select_t *hres_select = key;

	if (license->id.hres_id != hres_select->root_id.hres_id)
		return 0;

	if (!hres_select->test_only && (license->hres_rec.disable_hres ||
					license->hres_rec.disable_layer)) {
		hres_select->avail_hres[license->hres_rec.idx] = 0;
		hres_select->avail_hres_orig[license->hres_rec.idx] = 0;
		return 0;
	}

	hres_select->avail_hres[license->hres_rec.idx] = license->total;

	if (!hres_select->test_only && (license->total != INFINITE)) {
		if (license->total > license->used)
			hres_select->avail_hres[license->hres_rec.idx] -=
				license->used;
		else
			hres_select->avail_hres[license->hres_rec.idx] = 0;
	}

	hres_select->avail_hres_orig[license->hres_rec.idx] =
		hres_select->avail_hres[license->hres_rec.idx];

	return 0;
}

extern void hres_pre_select(job_record_t *job_ptr, bool test_only)
{
	hres_select_t *hres_select = job_ptr->hres_select;

	if (!hres_select)
		return;

	slurm_mutex_lock(&license_mutex);

	hres_select->test_only = test_only;
	list_for_each_ro(cluster_license_list, _foreach_hres_pre_select,
			 hres_select);
	slurm_mutex_unlock(&license_mutex);

	for (int i = 0; i < hres_select->leaf_cnt; i++) {
		uint32_t min = INFINITE;

		for (int j = 0; j < hres_select->depth; j++) {
			uint16_t idx = hres_select->leaf[i].path_idx[j];
			if (min > hres_select->avail_hres[idx])
				min = hres_select->avail_hres[idx];
		}
		hres_select->leaf[i].capacity = min;
	}

	hres_select_print(hres_select);

	return;
}

static int _foreach_bf_hres_pre_select(void *x, void *key)
{
	bf_license_t *bf_lic = x;
	hres_select_t *hres_select = key;
	licenses_t *license;

	if (bf_lic->id.hres_id != hres_select->root_id.hres_id)
		return 0;

	license = list_find_first_ro(cluster_license_list,
				     _license_find_rec_by_id, &bf_lic->id);
	if (!license)
		return 0;

	hres_select->avail_hres[license->hres_rec.idx] = bf_lic->remaining;

	hres_select->avail_hres_orig[license->hres_rec.idx] =
		hres_select->avail_hres[license->hres_rec.idx];

	return 0;
}

extern void slurm_bf_hres_pre_select(job_record_t *job_ptr,
				     bf_licenses_t *bf_licenses)
{
	hres_select_t *hres_select = job_ptr->hres_select;

	if (!hres_select || !bf_licenses)
		return;

	slurm_mutex_lock(&license_mutex);
	list_for_each_ro(bf_licenses, _foreach_bf_hres_pre_select, hres_select);
	slurm_mutex_unlock(&license_mutex);

	for (int i = 0; i < hres_select->leaf_cnt; i++) {
		uint32_t min = INFINITE;

		for (int j = 0; j < hres_select->depth; j++) {
			uint16_t idx = hres_select->leaf[i].path_idx[j];
			if (min > hres_select->avail_hres[idx])
				min = hres_select->avail_hres[idx];
		}
		hres_select->leaf[i].capacity = min;
	}
	return;
}

extern uint16_t hres_select_find_leaf(hres_select_t *hres_select, int node_inx)
{
	for (int i = 0; i < hres_select->leaf_cnt; i++) {
		if (bit_test(hres_select->leaf[i].node_bitmap, node_inx))
			return i;
	}
	return NO_VAL16;
}

extern void hres_select_free(job_record_t *job_ptr)
{
	hres_select_t *hres_select = job_ptr->hres_select;

	if (!hres_select)
		return;

	/* Do not free leaf[i].node_bitmap, since it is not a copy */
	xfree(hres_select->leaf);
	xfree(hres_select->avail_hres);
	xfree(hres_select->avail_hres_orig);

	xfree(job_ptr->hres_select);
}

extern void hres_select_print(hres_select_t *hres_select)
{
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_LICENSE))
		return;
	if (!hres_select)
		return;
	for (int i = 0; i < hres_select->leaf_cnt; i++) {
		verbose("%s leaf:%d capacity:%u", __func__, i,
			hres_select->leaf[i].capacity);
		for (int j = 0; j < hres_select->depth; j++) {
			uint16_t idx = hres_select->leaf[i].path_idx[j];
			verbose("\t\t %u %u", idx,
				hres_select->avail_hres[idx]);
		}
	}
}

extern bool hres_select_check(hres_select_t *hres_select,
			      uint16_t hres_leaf_idx)
{
	bool can_run = true;

	if (hres_leaf_idx == NO_VAL16)
		return false;

	for (int j = 0; j < hres_select->depth; j++) {
		uint16_t idx = hres_select->leaf[hres_leaf_idx].path_idx[j];
		if ((hres_select->avail_hres[idx] != INFINITE) &&
		    (hres_select->avail_hres[idx] <
		     hres_select->hres_per_node)) {
			can_run = false;
			break;
		}
	}

	if (can_run) {
		for (int j = 0; j < hres_select->depth; j++) {
			uint16_t idx =
				hres_select->leaf[hres_leaf_idx].path_idx[j];
			if (hres_select->avail_hres[idx] != INFINITE)
				hres_select->avail_hres[idx] -=
					hres_select->hres_per_node;
		}
	}

	return can_run;
}

extern void hres_select_return(hres_select_t *hres_select,
			       uint16_t hres_leaf_idx)
{
	if (hres_leaf_idx == NO_VAL16)
		return;

	for (int j = 0; j < hres_select->depth; j++) {
		uint16_t idx = hres_select->leaf[hres_leaf_idx].path_idx[j];
		if (hres_select->avail_hres[idx] != INFINITE)
			hres_select->avail_hres[idx] +=
				hres_select->hres_per_node;
	}
}

extern licenses_t *license_find_rec_by_id(list_t *license_list,
					  licenses_id_t id)
{
	return list_find_first_ro(license_list, _license_find_rec_by_id, &id);
}

static int _is_remote_license(void *x, void *key)
{
	licenses_t *license_entry = x;

	return license_entry->remote ? 1 : 0;
}

static int _foreach_license_update_match(void *x, void *arg)
{
	licenses_t *license_entry = x;
	license_update_args_t *args = arg;
	licenses_t *match = NULL;

	if (args->new_list && license_entry->hres_rec.layer_name) {
		licenses_find_layer_t find_args = {
			.hres_name = license_entry->name,
			.layer_name = license_entry->hres_rec.layer_name,
		};
		match = list_find_first_ro(args->new_list, _license_find_layer,
					   &find_args);
	} else if (args->new_list) {
		match = list_find_first_ro(args->new_list, _license_find_rec,
					   license_entry->name);
	}

	if (!match) {
		info("license %s removed with %u in use", license_entry->name,
		     license_entry->used);
	} else {
		match->id.lic_id = license_entry->id.lic_id;
		match->id.hres_id = license_entry->id.hres_id;
		if (license_entry->used > match->total)
			info("license %s count decreased", match->name);
	}

	return 0;
}

/* Update licenses on this system based upon slurm.conf.
 * Remove all previously allocated licenses */
extern int license_update(char *licenses)
{
	licenses_t *remote_entry;
	license_update_args_t args = { 0 };
	bool valid = true;

	last_license_update = time(NULL);
	args.new_list =
		_build_license_list(licenses, &valid, HRES_SYNTAX_NONE, false);
	if (!valid)
		fatal("Invalid configured licenses: %s", licenses);

	_parse_hierarchical_resources(&args.new_list);

	slurm_mutex_lock(&license_mutex);
	if (!cluster_license_list) { /* no licenses before now */
		goto fini;
	}

	/*
	 * Move remote licenses (handled elsewhere) from the old list to the
	 * new list with their used counters reset. args is sent to
	 * _is_remote_license() since args cannot be NULL, but it is not used
	 * there.
	 */
	while ((remote_entry = list_remove_first(cluster_license_list,
						 _is_remote_license, &args))) {
		if (!args.new_list)
			args.new_list = list_create(license_free_rec);
		remote_entry->used = 0;
		list_append(args.new_list, remote_entry);
	}

	/*
	 * Match remaining non-remote licenses against the new list to log
	 * removals and propagate ids to the matching entry. This is only
	 * relevant to multiple slurmctld failover events, where a backup
	 * slurmctld may be calling license_update() more than once (once per
	 * failover).
	 */
	list_for_each_ro(cluster_license_list, _foreach_license_update_match,
			 &args);

	FREE_NULL_LIST(cluster_license_list);
fini:
	cluster_license_list = args.new_list;
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

static int _remove_matching_remote_license(void *x, void *arg)
{
	licenses_t *license_entry = x;
	char *name = arg;

	if (!license_entry->remote)
		return 0;
	if (xstrcmp(license_entry->name, name))
		return 0;

	info("license_remove_remote: license %s removed with %u in use",
	     license_entry->name, license_entry->used);
	last_license_update = time(NULL);

	return 1;
}

extern void license_remove_remote(slurmdb_res_rec_t *rec)
{
	char *name;

	xassert(rec);
	xassert(rec->type == SLURMDB_RESOURCE_LICENSE);

	slurm_mutex_lock(&license_mutex);
	if (!cluster_license_list) {
		xassert(last_license_update);
		cluster_license_list = list_create(license_free_rec);
	}

	name = xstrdup_printf("%s@%s", rec->name, rec->server);

	if (!list_delete_first(cluster_license_list,
			       _remove_matching_remote_license, name))
		error("license_remote_remote: License '%s' not found", name);

	xfree(name);
	slurm_mutex_unlock(&license_mutex);
}

static int _find_remote_license_by_name(void *x, void *key)
{
	licenses_t *license_entry = x;
	char *name = key;

	if (!license_entry->remote)
		return 0;
	return !xstrcmp(license_entry->name, name);
}

static int _foreach_sync_remote_res(void *x, void *arg)
{
	slurmdb_res_rec_t *rec = x;
	license_sync_remote_args_t *args = arg;
	licenses_t *license_entry;
	char *name;

	if (rec->type != SLURMDB_RESOURCE_LICENSE)
		return 0;

	name = xstrdup_printf("%s@%s", rec->name, rec->server);
	license_entry = list_find_first_ro(args->cluster_license_list,
					   _find_remote_license_by_name, name);
	if (license_entry) {
		license_entry->remote = 2;
		_handle_consumed(license_entry, rec);
		if (license_entry->used > license_entry->total)
			info("license %s count decreased",
			     license_entry->name);
	} else {
		_add_res_rec_2_lic_list(rec, 1);
	}
	xfree(name);

	return 0;
}

static int _sync_remote_cleanup(void *x, void *arg)
{
	licenses_t *license_entry = x;

	if (!license_entry->remote)
		return 0;
	if (license_entry->remote == 1) {
		info("license_remove_remote: license %s removed with %u in use",
		     license_entry->name, license_entry->used);
		last_license_update = time(NULL);
		return 1;
	}
	/* remote == 2: was matched this sync, reset for next round */
	license_entry->remote = 1;

	return 0;
}

extern void license_sync_remote(list_t *res_list)
{
	slurm_mutex_lock(&license_mutex);
	if (res_list && !cluster_license_list) {
		xassert(last_license_update);
		cluster_license_list = list_create(license_free_rec);
	}

	if (res_list) {
		license_sync_remote_args_t args = {
			.cluster_license_list = cluster_license_list,
		};
		list_for_each_ro(res_list, _foreach_sync_remote_res, &args);
	}

	list_delete_all(cluster_license_list, _sync_remote_cleanup, NULL);

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
 * Record that node_cnt * license_entry->total was charged against the layer,
 * so that license_job_return() can release exactly that amount later. The
 * nodes of a layer may change while the job runs, so the amount cannot be
 * derived again from node overlap at that point.
 */
static void _add_hres_charge(licenses_t *license_entry, licenses_t *match,
			     uint16_t node_cnt)
{
	hres_charge_t *charge = xmalloc(sizeof(*charge));

	charge->id = match->id;
	charge->layer_name = xstrdup(match->hres_rec.layer_name);
	charge->node_cnt = node_cnt;

	if (!license_entry->hres_charges)
		license_entry->hres_charges = list_create(hres_charge_free);
	list_append(license_entry->hres_charges, charge);
}

/*
 * Resolve "(layer[*node_cnt][,...])" into the charge list of the entry.
 * RET the cluster record to take the id and mode from, or NULL if any layer
 *     could not be resolved, in which case the entry is left untouched.
 */
static licenses_t *_resolve_hres_layers(licenses_t *license_entry)
{
	char *tmp_str = xstrdup(license_entry->nodes);
	char *tok, *saveptr = NULL;
	licenses_t *match = NULL;
	bool valid = true;

	for (tok = strtok_r(tmp_str, ",", &saveptr); tok && valid;
	     tok = strtok_r(NULL, ",", &saveptr)) {
		licenses_find_layer_t find_layer = {
			.hres_name = license_entry->name,
		};
		long node_cnt = 1;
		char *mult = xstrchr(tok, '*');

		if (mult) {
			char *end_num = NULL;

			*mult = '\0';
			node_cnt = strtol(mult + 1, &end_num, 10);
			if ((end_num == (mult + 1)) || (*end_num != '\0') ||
			    (node_cnt < 1) || (node_cnt >= NO_VAL16)) {
				valid = false;
				break;
			}
		}

		find_layer.layer_name = tok;
		match = list_find_first_ro(cluster_license_list,
					   _license_find_layer, &find_layer);
		if (!match) {
			valid = false;
			break;
		}

		_add_hres_charge(license_entry, match, node_cnt);
	}
	xfree(tmp_str);

	if (!valid || !list_count(license_entry->hres_charges)) {
		FREE_NULL_LIST(license_entry->hres_charges);
		return NULL;
	}

	/*
	 * A single layer identifies itself, which is what a reservation names
	 * and what _license_cnt() matches on. Several layers can only come
	 * from a mode 3 job, where the root record represents the resource.
	 */
	if (list_count(license_entry->hres_charges) > 1)
		match = list_find_first_ro(cluster_license_list,
					   _license_find_root_rec,
					   license_entry->name);

	if (match)
		xfree(license_entry->nodes);

	return match;
}

static int _foreach_license_validate(void *x, void *arg)
{
	licenses_t *license_entry = x;
	license_validate_args_t *args = arg;
	licenses_t *match = NULL;
	bool is_fuzzy_match = false;

	/* Short-circuit once validation has failed. */
	if (!*args->valid)
		return 0;

	if (cluster_license_list) {
		if (license_entry->nodes) {
			match = _resolve_hres_layers(license_entry);
			/*
			 * Slurm 26.05 and older named the layer with its node
			 * list. Those strings are only accepted when state
			 * written by such a version is restored.
			 * Remove support for HRES_SYNTAX_ANY after upgrading
			 * from SLURM_26_05_PROTOCOL_VERSION is no longer
			 * supported.
			 */
			if (!match && (args->hres_syntax == HRES_SYNTAX_ANY)) {
				licenses_find_rec_by_nodes_t find_args = {
					.name = license_entry->name,
					.nodes = license_entry->nodes,
				};
				match = list_find_first_ro(
					cluster_license_list,
					_license_find_rec_by_nodes, &find_args);
				if (match) {
					xfree(license_entry->nodes);
					_add_hres_charge(license_entry, match,
							 1);
				}
			}
		} else if (xstrchr(license_entry->name, '@') ||
			   !args->fuzzy_match_remote) {
			match = list_find_first_ro(cluster_license_list,
						   _license_find_root_rec,
						   license_entry->name);
		} else {
			match = _fuzzy_match_remote_licenses(
				license_entry->name);
			is_fuzzy_match = true;
		}
	}

	if (!match) {
		debug("License name requested (%s) does not exist",
		      license_entry->name);
		if (!args->validate_existing)
			return 1; /* delete entry from job_license_list */
		*args->valid = false;
		return 0;
	}
	if (args->validate_configured &&
	    (license_entry->total > match->total)) {
		debug("Licenses count requested higher than configured (%s: %u > %u)",
		      match->name, license_entry->total, match->total);
		*args->valid = false;
		return 0;
	}

	if (is_fuzzy_match) {
		/* copy real name to maintain exact match */
		xfree(license_entry->name);
		license_entry->name = xstrdup(match->name);
		if (args->fuzzy_match)
			*args->fuzzy_match = true;
	}
	license_entry->id.lic_id = match->id.lic_id;
	license_entry->id.hres_id = match->id.hres_id;
	license_entry->mode = match->mode;

	if (license_entry->mode == HRES_MODE_3) {
		if (args->has_mode3) {
			debug("Only one HRes Mode 3 can be used per job");
			*args->valid = false;
			return 0;
		}
		args->has_mode3 = true;
	}

	if (args->tres_req_cnt) {
		int tres_pos;
		args->tres_req->name = license_entry->name;
		if ((tres_pos = assoc_mgr_find_tres_pos(args->tres_req,
							false)) != -1)
			args->tres_req_cnt[tres_pos] =
				(uint64_t) license_entry->total;
	}

	return 0;
}

extern list_t *license_validate(char *licenses, bool validate_configured,
				bool validate_existing,
				hres_syntax_t hres_syntax,
				uint64_t *tres_req_cnt, bool *valid,
				bool *fuzzy_match)
{
	list_t *job_license_list;
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_req;
	license_validate_args_t args = {
		.fuzzy_match = fuzzy_match,
		.hres_syntax = hres_syntax,
		.tres_req = &tres_req,
		.tres_req_cnt = tres_req_cnt,
		.valid = valid,
		.validate_configured = validate_configured,
		.validate_existing = validate_existing,
	};

	if (xstrcasestr(slurm_conf.license_params, "RemoteFuzzyMatch"))
		args.fuzzy_match_remote = true;
	/* initialize in case the caller didn't */
	if (fuzzy_match)
		*fuzzy_match = false;

	/* Init all the license TRES to 0 */
	if (tres_req_cnt) {
		assoc_mgr_lock_t locks = { .tres = READ_LOCK };
		assoc_mgr_lock(&locks);

		/*
		 * We can start at TRES_ARRAY_TOTAL_CNT as we know licenses are
		 * after the static TRES.
		 */
		for (int tres_pos = TRES_ARRAY_TOTAL_CNT;
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

	job_license_list =
		_build_license_list(licenses, valid, hres_syntax, true);
	if (!job_license_list)
		return job_license_list;

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_req, 0, sizeof(slurmdb_tres_rec_t));
		tres_req.type = "license";
	}

	slurm_mutex_lock(&license_mutex);
	list_delete_all(job_license_list, _foreach_license_validate, &args);
	slurm_mutex_unlock(&license_mutex);

	_licenses_print("request_license", job_license_list, NULL);

	/*
	 * The list may be empty if the license was removed and
	 * validate_existing==false. Free the license list and return NULL if
	 * that is the case to avoid crashing later.
	 */
	if (!(*valid) || !list_count(job_license_list)) {
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
	job_ptr->license_list = _build_license_list(job_ptr->licenses, &valid,
						    HRES_SYNTAX_NONE, false);
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
		resv_licenses = job_test_lic_resv(job_ptr, license_entry->id,
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

	list_for_each_ro(job_ptr->license_list, _foreach_license_job_test,
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

static int _foreach_hres_charge_copy(void *x, void *arg)
{
	hres_charge_t *charge_src = x;
	list_t *dest_list = arg;
	hres_charge_t *charge_dest = xmalloc(sizeof(*charge_dest));

	*charge_dest = *charge_src;
	charge_dest->layer_name = xstrdup(charge_src->layer_name);
	list_append(dest_list, charge_dest);

	return 0;
}

static list_t *_hres_charge_list_copy(list_t *src_list)
{
	list_t *dest_list = NULL;

	if (!src_list)
		return NULL;

	dest_list = list_create(hres_charge_free);
	list_for_each_ro(src_list, _foreach_hres_charge_copy, dest_list);

	return dest_list;
}

static int _foreach_hres_variable_copy(void *x, void *arg)
{
	hres_variable_t *var_src = x;
	list_t *dest_list = arg;
	hres_variable_t *var_dest = xmalloc(sizeof(*var_dest));

	var_dest->name = xstrdup(var_src->name);
	var_dest->value = var_src->value;
	list_append(dest_list, var_dest);

	return 0;
}

static list_t *_hres_variable_list_copy(list_t *src_list)
{
	list_t *dest_list = NULL;

	if (!src_list)
		return NULL;

	dest_list = list_create(hres_variable_free);
	list_for_each_ro(src_list, _foreach_hres_variable_copy, dest_list);

	return dest_list;
}

/* Deep copy a licenses_t record */
static int _foreach_license_copy(void *x, void *arg)
{
	licenses_t *license_entry_src = x;
	licenses_t *license_entry_dest = xmalloc(sizeof(licenses_t));
	list_t *license_list_dest = arg;

	/*
	 * memcpy, then replace pointers with malloc'd copies. Set parent
	 * pointer to NULL - it is currently unused by callers, and if it
	 * ever needs to be set then the parent pointers would need to be
	 * reconstructed from the new list.
	 */
	memcpy(license_entry_dest, license_entry_src,
	       sizeof(*license_entry_dest));
	license_entry_dest->name = xstrdup(license_entry_src->name);
	license_entry_dest->hres_charges =
		_hres_charge_list_copy(license_entry_src->hres_charges);
	license_entry_dest->nodes = xstrdup(license_entry_src->nodes);
	if (license_entry_src->node_bitmap)
		license_entry_dest->node_bitmap =
			bit_copy(license_entry_src->node_bitmap);
	license_entry_dest->hres_rec.layer_name =
		xstrdup(license_entry_src->hres_rec.layer_name);
	license_entry_dest->hres_rec.parent_name =
		xstrdup(license_entry_src->hres_rec.parent_name);
	license_entry_dest->hres_rec.parent = NULL;
	license_entry_dest->hres_rec.topology_name =
		xstrdup(license_entry_src->hres_rec.topology_name);
	license_entry_dest->hres_rec.base =
		_hres_variable_list_copy(license_entry_src->hres_rec.base);
	license_entry_dest->hres_rec.variables =
		_hres_variable_list_copy(license_entry_src->hres_rec.variables);

	list_append(license_list_dest, license_entry_dest);

	return 0;
}

static int _foreach_license_light_copy(void *x, void *arg)
{
	licenses_t *license_entry_src = x;
	licenses_t *license_entry_dest = xmalloc(sizeof(licenses_t));
	list_t *license_list_dest = arg;

	/*
	 * HRES and nodes and name intentionally not copied as they are unused
	 * by consumers of this function.
	 */
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

extern int cluster_license_count(void)
{
	int cnt;
	slurm_mutex_lock(&license_mutex);
	cnt = list_count(cluster_license_list);
	slurm_mutex_unlock(&license_mutex);
	return cnt;
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
	xfree(job_ptr->licenses_allocated);
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

	if (args->license_entry->mode == HRES_MODE_3) {
		int32_t used = bit_overlap(match->node_bitmap,
					   args->job_ptr->node_bitmap);
		if (!used)
			return 0;

		match->used += used * args->license_entry->total;
		_add_hres_charge(args->license_entry, match, used);
		return 0;
	}

	if (!bit_overlap_any(match->node_bitmap, args->job_ptr->node_bitmap))
		return 0;

	if (args->license_entry->mode == HRES_MODE_1) {
		int resv_licenses;

		/*
		 * A disabled layer must not be the one the job is charged to.
		 * _foreach_hres_filter_mode1() does the same check.
		 */
		if (match->hres_rec.disable_hres ||
		    match->hres_rec.disable_layer)
			return 0;

		resv_licenses = job_test_lic_resv(args->job_ptr, match->id,
						  args->when, false);

		if (!_sufficient_licenses(args->license_entry, match,
					  resv_licenses))
			return 0;
		match->used += args->license_entry->total;
		_add_hres_charge(args->license_entry, match, 1);
		return -1;
	} else if (args->license_entry->mode == HRES_MODE_2) {
		match->used += args->license_entry->total;
		_add_hres_charge(args->license_entry, match, 1);
		return 0;
	}

	return 0;
}

/* Charge the layers that a restored job already recorded as charged. */
static int _foreach_hres_charge_restore(void *x, void *arg)
{
	hres_charge_t *charge = x;
	foreach_get_hres_t *args = arg;
	licenses_t *match =
		license_find_rec_by_id(cluster_license_list, charge->id);

	if (!match) {
		/*
		 * This should never happen, since IDs are resolved on startup
		 * and never change.
		 */
		error("%s: Could not find HRES=%s layer=%s lic_id=%u, not restoring %u",
		      __func__, args->license_entry->name, charge->layer_name,
		      charge->id.lic_id, args->license_entry->total);
		return 0;
	}

	match->used += charge->node_cnt * args->license_entry->total;

	return 0;
}

/*
 * license_job_get - Get the licenses required for a job
 * IN job_ptr - job identification
 * RET SLURM_SUCCESS or failure code
 */
static int _foreach_license_job_get(void *x, void *arg)
{
	licenses_t *license_entry = x;
	license_job_get_args_t *args = arg;
	licenses_t *match;

	if (license_entry->id.hres_id != NO_VAL16) {
		foreach_get_hres_t hres_arg = {
			.job_ptr = args->job_ptr,
			.license_entry = license_entry,
			.when = last_license_update,
		};

		/*
		 * A restored job already knows which layers it charged. Replay
		 * that record instead of matching nodes again, which would
		 * charge the wrong layers if any layer changed nodes while
		 * slurmctld was down.
		 */
		if (args->restore && license_entry->hres_charges) {
			list_for_each_ro(license_entry->hres_charges,
					 _foreach_hres_charge_restore,
					 &hres_arg);
		} else {
			FREE_NULL_LIST(license_entry->hres_charges);
			list_for_each_ro(cluster_license_list,
					 _foreach_hres_job_get, &hres_arg);
		}

		license_entry->used += license_entry->total;
		return 0;
	}

	args->lic_or = license_entry->op_or;
	match = list_find_first_ro(cluster_license_list,
				   _license_find_rec_by_id,
				   &license_entry->id);
	if (!match) {
		error("could not find license %s for job %u",
		      license_entry->name, args->job_ptr->job_id);
		args->rc = SLURM_ERROR;
		return 0;
	}

	/*
	 * With OR, we only know that at least one of the job's requested
	 * licenses are available, so we need to test for availability again.
	 * With AND we know that all licenses are available so we don't need
	 * to check.
	 */
	if (args->lic_or) {
		int resv_blk_lic_cnt = job_test_lic_resv(
			args->job_ptr, match->id, last_license_update, false);
		if (!_sufficient_licenses(license_entry, match,
					  resv_blk_lic_cnt)) {
			/* Not enough of this license */
			return 0;
		}
	}

	match->used += license_entry->total;
	license_entry->used += license_entry->total;
	if (match->remote && args->restore) {
		if (license_entry->total > match->last_deficit)
			match->last_deficit = 0;
		else
			match->last_deficit -= license_entry->total;
	}
	if (args->lic_or) {
		args->last_entry = license_entry;
		return -1;
	}

	return 0;
}

extern int license_job_get(job_record_t *job_ptr, bool restore)
{
	license_job_get_args_t args = {
		.job_ptr = job_ptr,
		.rc = SLURM_SUCCESS,
		.restore = restore,
	};

	if (!job_ptr->license_list)	/* no licenses needed */
		return SLURM_SUCCESS;

	last_license_update = time(NULL);

	slurm_mutex_lock(&license_mutex);
	list_for_each_ro(job_ptr->license_list, _foreach_license_job_get, &args);

	/* When restoring, allocated licenses is already set */
	if (!args.rc && !restore)
		args.rc = _set_licenses_alloc(job_ptr, args.lic_or,
					      args.last_entry);

	_licenses_print("acquire_license", cluster_license_list, job_ptr);
	slurm_mutex_unlock(&license_mutex);
	return args.rc;
}

/* Release one charge that a job or reservation recorded at acquire. */
static int _foreach_hres_charge_return(void *x, void *arg)
{
	hres_charge_t *charge = x;
	foreach_hres_return_t *args = arg;
	licenses_t *match =
		license_find_rec_by_id(args->license_list, charge->id);
	uint32_t used = charge->node_cnt * args->license_entry->total;

	if (!match) {
		/*
		 * This should never happen, since IDs are resolved on startup
		 * and never change.
		 */
		error("%s: Could not find HRES=%s layer=%s lic_id=%u",
		      __func__, args->license_entry->name, charge->layer_name,
		      charge->id.lic_id);
		return 0;
	}

	if (match->used >= used) {
		match->used -= used;
	} else {
		error("%s: license use count underflow for lic_id=%u",
		      __func__, charge->id.lic_id);
		match->used = 0;
	}

	return 0;
}

static int _foreach_license_job_return(void *x, void *arg)
{
	licenses_t *license_entry = x;
	license_return_args_t *args = arg;
	licenses_t *match;

	if (license_entry->hres_charges) {
		foreach_hres_return_t arg2 = {
			.license_entry = license_entry,
			.license_list = args->license_list,
		};

		if (!args->locked)
			slurm_mutex_lock(&license_mutex);
		list_for_each_ro(license_entry->hres_charges,
				 _foreach_hres_charge_return, &arg2);
		if (!args->locked)
			slurm_mutex_unlock(&license_mutex);

		if (!args->future)
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
		if (!args->future)
			license_entry->used = 0;
	} else {
		/* This can happen after a reconfiguration */
		error("%s: job returning unknown license lic_id=%u",
		      __func__, license_entry->id.lic_id);
	}
	return 0;
}

/*
 * Return the licenses allocated to a job to the provided list
 * IN job_ptr - job identification
 * RET count of license having state changed
 */
extern int license_job_return_to_list(job_record_t *job_ptr,
				      list_t *license_list, bool locked,
				      bool future)
{
	int rc = 0;
	license_return_args_t args = {
		.future = future,
		.job_ptr = job_ptr,
		.license_list = license_list,
		.locked = locked,
	};

	if (!job_ptr->license_list)	/* no licenses needed */
		return rc;

	log_flag(TRACE_JOBS, "%s: %pJ", __func__, job_ptr);

	rc = list_for_each_ro(job_ptr->license_list,
			      _foreach_license_job_return, &args);

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
	if (license_job_return_to_list(job_ptr, cluster_license_list, true,
				       false))
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

/*
 * license_list_overlap_non_hres - test if there is any overlap in non-hres
 *	licenses names found in the two lists
 */
extern bool license_list_overlap_non_hres(list_t *list_1, list_t *list_2)
{
	if (!list_1 || !list_2)
		return false;
	return list_find_first_ro(list_1,
				  _license_find_non_hres_rec_in_list_by_id,
				  list_2);
}

/* pack_all_licenses()
 *
 * Return license counters to the library.
 */
static int _foreach_pack_license(void *x, void *arg)
{
	licenses_t *lic_entry = x;
	get_all_license_info_args_t *args = arg;

	set_reserved_license_count(lic_entry);
	/* Now encode the license data structure. */
	_pack_license(lic_entry, args->buffer, args->protocol_version);
	args->lics_packed++;

	return 0;
}

extern buf_t *get_all_license_info(uint16_t protocol_version)
{
	get_all_license_info_args_t args = {
		.protocol_version = protocol_version,
	};
	int tmp_offset;
	time_t now = time(NULL);

	debug2("%s: calling for all licenses", __func__);

	args.buffer = init_buf(BUF_SIZE);

	/* write header: version and time */
	pack32(args.lics_packed, args.buffer);
	pack_time(now, args.buffer);

	slurm_mutex_lock(&license_mutex);
	if (cluster_license_list)
		list_for_each_ro(cluster_license_list, _foreach_pack_license,
				 &args);
	slurm_mutex_unlock(&license_mutex);

	debug2("%s: processed %d licenses", __func__, args.lics_packed);

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(args.buffer);
	set_buf_offset(args.buffer, 0);
	pack32(args.lics_packed, args.buffer);
	set_buf_offset(args.buffer, tmp_offset);

	return args.buffer;
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
static int _foreach_licenses_2_tres_str(void *x, void *arg)
{
	licenses_t *license_entry = x;
	licenses_2_tres_str_args_t *args = arg;
	slurmdb_tres_rec_t *tres_rec;

	args->tres_req->name = license_entry->name;
	if (!(tres_rec = assoc_mgr_find_tres_rec(args->tres_req)))
		return 0; /* not tracked */

	if (slurmdb_find_tres_count_in_string(args->tres_str, tres_rec->id) !=
	    INFINITE64)
		return 0; /* already handled */
	/* New license */
	xstrfmtcat(args->tres_str, "%s%u=%" PRIu64, args->tres_str ? "," : "",
		   tres_rec->id, (uint64_t) license_entry->total);

	return 0;
}

extern char *licenses_2_tres_str(list_t *license_list)
{
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_req;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };
	licenses_2_tres_str_args_t args = {
		.tres_req = &tres_req,
	};

	if (!license_list)
		return NULL;

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_req, 0, sizeof(slurmdb_tres_rec_t));
		tres_req.type = "license";
	}

	assoc_mgr_lock(&locks);
	list_for_each_ro(license_list, _foreach_licenses_2_tres_str, &args);
	assoc_mgr_unlock(&locks);

	return args.tres_str;
}

static int _foreach_set_job_tres_cnt(void *x, void *arg)
{
	licenses_t *license_entry = x;
	license_set_job_tres_cnt_args_t *args = arg;
	int tres_pos;

	args->tres_rec->name = license_entry->name;
	if ((tres_pos = assoc_mgr_find_tres_pos(
		     args->tres_rec, args->locked)) != -1)
		args->tres_cnt[tres_pos] = (uint64_t) license_entry->total;

	return 0;
}

extern void license_set_job_tres_cnt(list_t *license_list,
				     uint64_t *tres_cnt,
				     bool locked)
{
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_rec;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };
	license_set_job_tres_cnt_args_t args = {
		.locked = locked,
		.tres_cnt = tres_cnt,
		.tres_rec = &tres_rec,
	};

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

	list_for_each_ro(license_list, _foreach_set_job_tres_cnt, &args);

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
	if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION) {
		packstr(lic->name, buffer);
		pack32(lic->hres_rec.base_usage, buffer);
		if (lic->mode == HRES_MODE_OFF)
			pack32(lic->total, buffer);
		else
			pack32(lic->hres_rec.total, buffer);
		pack32(lic->total, buffer);
		pack32(lic->used, buffer);
		pack32(lic->reserved, buffer);
		pack8(lic->remote, buffer);
		pack32(lic->last_consumed, buffer);
		pack32(lic->last_deficit, buffer);
		pack_time(lic->last_update, buffer);
		pack8(lic->mode, buffer);
		packbool(lic->hres_rec.disable_hres, buffer);
		packbool(lic->hres_rec.disable_layer, buffer);
		packstr(lic->nodes, buffer);
		packstr(lic->hres_rec.layer_name, buffer);
		packstr(lic->hres_rec.parent_name, buffer);
		slurm_pack_list(lic->hres_rec.base, slurm_pack_hres_variable,
				buffer, protocol_version);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

static int _foreach_bf_licenses_initial(void *x, void *arg)
{
	licenses_t *license_entry = x;
	bf_licenses_initial_args_t *args = arg;
	bf_license_t *bf_entry = xmalloc(sizeof(*bf_entry));

	if (license_entry->hres_rec.disable_hres ||
	    license_entry->hres_rec.disable_layer) {
		/* Disable backfill planning */
		bf_entry->remaining = 0;
	} else {
		bf_entry->remaining = license_entry->total;
		if (!args->bf_running_job_reserve &&
		    (bf_entry->remaining != INFINITE))
			bf_entry->remaining -= license_entry->used;
	}
	bf_entry->id = license_entry->id;

	list_append(args->bf_list, bf_entry);

	return 0;
}

extern list_t *bf_licenses_initial(bool bf_running_job_reserve)
{
	bf_licenses_initial_args_t args = {
		.bf_running_job_reserve = bf_running_job_reserve,
	};

	slurm_mutex_lock(&license_mutex);
	if (!cluster_license_list || !list_count(cluster_license_list)) {
		slurm_mutex_unlock(&license_mutex);
		return NULL;
	}

	args.bf_list = list_create(_bf_license_free_rec);

	list_for_each_ro(cluster_license_list, _foreach_bf_licenses_initial,
			 &args);

	slurm_mutex_unlock(&license_mutex);

	return args.bf_list;
}

static int _foreach_bf_licenses_to_string(void *x, void *arg)
{
	bf_license_t *entry = x;
	bf_licenses_to_string_args_t *args = arg;

	xstrfmtcat(args->licenses, "%s%s%s%slic_id=%u:%u", args->sep,
		   (entry->resv_ptr ? "resv=" : ""),
		   (entry->resv_ptr ? entry->resv_ptr->name : ""),
		   (entry->resv_ptr ? ":" : ""), entry->id.lic_id,
		   entry->remaining);
	args->sep = ",";

	return 0;
}

extern char *bf_licenses_to_string(bf_licenses_t *licenses_list)
{
	bf_licenses_to_string_args_t args = {
		.sep = "",
	};

	if (!licenses_list)
		return NULL;

	list_for_each_ro(licenses_list, _foreach_bf_licenses_to_string, &args);

	return args.licenses;
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

static int _foreach_find_hres_charge_by_id(void *x, void *key)
{
	hres_charge_t *charge = x;
	licenses_id_t *id = key;

	if ((charge->id.lic_id == id->lic_id) &&
	    (charge->id.hres_id == id->hres_id))
		return 1;
	return 0;
}

/* Find the charge that a job made against a specific layer, if any */
static hres_charge_t *_license_find_charge_by_id(list_t *hres_charges,
						 licenses_id_t *id)
{
	if (!hres_charges)
		return NULL;

	return list_find_first_ro(hres_charges, _foreach_find_hres_charge_by_id,
				  id);
}

static int _foreach_hres_deduct(void *x, void *arg)
{
	bf_license_t *bf_lic = x;
	licenses_t *match;
	foreach_get_hres_t *args = arg;
	uint32_t used = 0;

	if (bf_lic->id.hres_id != args->license_entry->id.hres_id)
		return 0;

	/*
	 * A running mode 1 job holds the one layer that it was charged on, so
	 * deduct from that layer only. The nodes of a layer may change while
	 * the job runs, so the layer that overlaps the job's nodes now is not
	 * necessarily the layer that was charged.
	 */
	if ((args->license_entry->mode == HRES_MODE_1) &&
	    IS_JOB_RUNNING(args->job_ptr) &&
	    !_license_find_charge_by_id(args->license_entry->hres_charges,
					&bf_lic->id))
		return 0;

	if (bf_lic->resv_ptr && (args->job_ptr->resv_ptr != bf_lic->resv_ptr))
		return 0;

	match = list_find_first_ro(cluster_license_list,
				   _license_find_rec_by_id, &bf_lic->id);
	if (!match)
		return 0;

	/*
	 * A disabled layer starts the plan with nothing remaining, see
	 * _foreach_bf_licenses_initial(). Jobs already running on it keep
	 * what they hold, so there is nothing to deduct and no underflow
	 * to report.
	 */
	if (match->hres_rec.disable_hres || match->hres_rec.disable_layer)
		return 0;

	if (args->license_entry->mode == HRES_MODE_3) {
		used = bit_overlap(match->node_bitmap,
				   args->job_ptr->node_bitmap);
	} else {
		used = bit_overlap_any(match->node_bitmap,
				       args->job_ptr->node_bitmap);
	}

	if (!used)
		return 0;

	used *= args->license_entry->total;

	if (bf_lic->remaining == INFINITE) {
		;
	} else if (bf_lic->remaining < used) {
		error("%s: underflow on lic_id=%u", __func__, match->id.lic_id);
		bf_lic->remaining = 0;
	} else {
		bf_lic->remaining -= used;
	}

	if (match->mode == HRES_MODE_1)
		return -1;
	else
		return 0;
}
static int _foreach_bf_licenses_deduct(void *x, void *arg)
{
	licenses_t *job_entry = x;
	slurm_bf_licenses_deduct_args_t *args = arg;
	bf_license_t *resv_entry = NULL, *bf_entry;
	int needed = job_entry->total;
	int resv_acquired = 0;

	if (job_entry->id.hres_id != NO_VAL16) {
		foreach_get_hres_t hres_arg = {
			.job_ptr = args->job_ptr,
			.license_entry = job_entry,
		};
		slurm_mutex_lock(&license_mutex);
		list_for_each_ro(args->licenses, _foreach_hres_deduct,
				 &hres_arg);
		slurm_mutex_unlock(&license_mutex);

		return 0;
	}

	args->lic_or = job_entry->op_or;
	/*
	 * Jobs with reservations may use licenses out of the reservation, as
	 * well as global ones. Deduct from reservation first, then global as
	 * needed.
	 */
	if (args->job_ptr->resv_ptr) {
		bf_licenses_find_resv_t target_record = {
			.id = job_entry->id,
			.resv_ptr = args->job_ptr->resv_ptr,
		};

		resv_entry = list_find_first_ro(args->licenses,
						_bf_licenses_find_resv,
						&target_record);
		if (resv_entry && (needed <= resv_entry->remaining)) {
			resv_entry->remaining -= needed;
			/* OR - reservation has enough, break. */
			if (args->lic_or) {
				args->found = true;
				return -1;
			}
			return 0;
		} else if (resv_entry) {
			resv_acquired = resv_entry->remaining;
			needed -= resv_acquired;
			resv_entry->remaining = 0;
		}
	}

	bf_entry = list_find_first_ro(args->licenses, _bf_licenses_find_rec,
				      &job_entry->id);

	if (!bf_entry) {
		error("%s: missing license lic_id=%u", __func__,
		      job_entry->id.lic_id);
	} else if (bf_entry->remaining < needed) {
		/*
		 * OR - Not an error; skip this one and keep going until we
		 * find the next one that is available.
		 */
		if (args->lic_or) {
			/* Return resv_acquired licenses */
			if (resv_entry) {
				resv_entry->remaining += resv_acquired;
				needed += resv_acquired;
			}
			return 0;
		}
		error("%s: underflow on lic_id=%u", __func__,
		      bf_entry->id.lic_id);
		bf_entry->remaining = 0;
	} else {
		bf_entry->remaining -= needed;
		if (args->lic_or) {
			args->found = true;
			return -1;
		}
	}

	return 0;
}

extern void slurm_bf_licenses_deduct(bf_licenses_t *licenses,
				     job_record_t *job_ptr)
{
	slurm_bf_licenses_deduct_args_t args = {
		.job_ptr = job_ptr,
		.licenses = licenses,
	};

	xassert(job_ptr);

	if (!job_ptr->license_list)
		return;

	list_for_each_ro(job_ptr->license_list, _foreach_bf_licenses_deduct,
			 &args);

	if (args.lic_or && !args.found) {
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
static int _foreach_bf_licenses_transfer(void *x, void *arg)
{
	licenses_t *resv_entry = x;
	slurm_bf_licenses_transfer_args_t *args = arg;
	bf_license_t *bf_entry, *new_entry;
	int needed = resv_entry->total;
	int reservable = resv_entry->total;

	bf_entry = list_find_first_ro(args->licenses, _bf_licenses_find_rec,
				      &(resv_entry->id));

	if (!bf_entry) {
		error("%s: missing license lic_id=%u", __func__,
		      resv_entry->id.lic_id);
	} else if (bf_entry->remaining < needed) {
		error("%s: underflow on lic_id=%u", __func__,
		      bf_entry->id.lic_id);
		reservable = bf_entry->remaining;
		bf_entry->remaining = 0;
	} else {
		bf_entry->remaining -= needed;
		reservable = needed;
	}

	new_entry = xmalloc(sizeof(*new_entry));
	new_entry->id = resv_entry->id;
	new_entry->remaining = reservable;
	new_entry->resv_ptr = args->resv_ptr;

	list_append(args->licenses, new_entry);

	return 0;
}

extern void slurm_bf_licenses_transfer(bf_licenses_t *licenses,
				       job_record_t *job_ptr)
{
	slurm_bf_licenses_transfer_args_t args = {
		.licenses = licenses,
		.resv_ptr = job_ptr->resv_ptr,
	};

	xassert(job_ptr);

	if (!job_ptr->license_list)
		return;

	list_for_each_ro(job_ptr->license_list, _foreach_bf_licenses_transfer,
			 &args);
}

static int _foreach_bf_licenses_avail(void *x, void *arg)
{
	licenses_t *need = x;
	slurm_bf_licenses_avail_args_t *args = arg;
	bf_license_t *resv_entry = NULL, *bf_entry;
	int needed = need->total;

	if (need->id.hres_id != NO_VAL16) {
		if (!args->node_bitmap)
			return 0;
		COPY_BITMAP(args->tmp_bitmap, args->node_bitmap);
		slurm_bf_hres_filter(args->job_ptr, args->tmp_bitmap,
				     args->licenses);
		if (!bit_equal(args->tmp_bitmap, args->node_bitmap)) {
			args->avail = false;
			return -1;
		}
		return 0;
	}
	/*
	 * Jobs with reservations may use licenses out of the reservation, as
	 * well as global ones. Deduct from reservation first, then global as
	 * needed.
	 */
	if (args->job_ptr->resv_ptr) {
		bf_licenses_find_resv_t target_record = {
			.id = need->id,
			.resv_ptr = args->job_ptr->resv_ptr,
		};

		resv_entry = list_find_first_ro(args->licenses,
						_bf_licenses_find_resv,
						&target_record);

		if (resv_entry && (needed <= resv_entry->remaining)) {
			/*
			 * OR - only need one, stop searching. Set avail = true
			 * in case a previous license was unavailable.
			 */
			if (need->op_or) {
				args->avail = true;
				return -1;
			}
			/* AND */
			return 0;
		} else if (resv_entry)
			needed -= resv_entry->remaining;
	}

	bf_entry = list_find_first_ro(args->licenses, _bf_licenses_find_rec,
				      &(need->id));

	if (!bf_entry || (bf_entry->remaining < needed)) {
		args->avail = false;
		/*
		 * OR - keep searching until we find one that is available or
		 * we get through the whole list.
		 */
		if (need->op_or)
			return 0;
		/* AND */
		return -1;
	}
	/* OR - only need one, stop searching. */
	if (need->op_or) {
		args->avail = true;
		return -1;
	}

	return 0;
}

extern bool slurm_bf_licenses_avail(bf_licenses_t *licenses,
				    job_record_t *job_ptr,
				    bitstr_t *node_bitmap)
{
	slurm_bf_licenses_avail_args_t args = {
		.avail = true,
		.job_ptr = job_ptr,
		.licenses = licenses,
		.node_bitmap = node_bitmap,
	};

	if (!job_ptr->license_list)
		return true;

	list_for_each_ro(job_ptr->license_list, _foreach_bf_licenses_avail,
			 &args);

	FREE_NULL_BITMAP(args.tmp_bitmap);

	return args.avail;
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
	/* The # of licenses can be different based on advanced reservations */
	if (list_count(a) != list_count(b))
		return false;
	return !(list_find_first_ro(a, _bf_licenses_find_difference, b));
}

static int _find_more_hres_in_list(void *x, void *arg)
{
	bf_license_t *lic_next = x;
	bf_license_t *lic_cur = NULL;
	find_diff_hres_in_list_args_t *args = arg;
	bf_licenses_find_resv_t target_record = {
		.id = lic_next->id,
		.resv_ptr = lic_next->resv_ptr,
	};

	if (lic_next->id.hres_id != args->hres_id)
		return 0;

	if (lic_next->resv_ptr && (args->job_resv_ptr != lic_next->resv_ptr))
		return 0;

	lic_cur = list_find(args->licenses_iter, _bf_licenses_find_resv,
			    &target_record);

	/*
	 * Break out of list_find_first if lic_next has more resources remaining
	 * the lic_cur.
	 *
	 * In context of backfill node_space table licenses, if lic_cur has more
	 * than or equal resources compared to lic_next then it would be useless
	 * to try scheduling on licenses_next's timeslot since the job would
	 * have already failed with licenses_cur's resources. In short don't set
	 * later_start if there are less or equal resources in the next slot.
	 */

	/* If lic_cur is NULL then it is like there are zero remaining */
	if (!lic_cur && !lic_next->remaining)
		return 0; /* equal remaining since both have none */
	if (!lic_cur || (lic_next->remaining > lic_cur->remaining))
		return 1; /* more resources in lic_next - break out */

	return 0;
}

static int _bf_licenses_find_relevant_hres_increase(void *x, void *key)
{
	licenses_t *job_license = x;
	find_relevant_hres_diff_args_t *args = key;
	find_diff_hres_in_list_args_t fargs = {
		.hres_id = job_license->id.hres_id,
		.job_resv_ptr = args->job_resv_ptr,
	};

	if (job_license->id.hres_id == NO_VAL16)
		return 0;

	if (!args->licenses_cur_iter)
		args->licenses_cur_iter =
			list_iterator_create(args->licenses_cur);
	else
		list_iterator_reset(args->licenses_cur_iter);

	/*
	 * Iterate through licenses_next and see if any relevant elements
	 * matching the job's hres request have more hres remaining in
	 * licenses_next than licenses_cur.
	 *
	 * bf_license lists elements are in the same order so pass an iterator
	 * of licenses_cur so both lists only needed to be iterated through
	 * once.
	 */
	fargs.licenses_iter = args->licenses_cur_iter;
	if (list_find_first_ro(args->licenses_next, _find_more_hres_in_list,
			       &fargs))
		return 1;

	/*
	 * Even if licenses_cur contains more records, they don't have the same
	 * hres id or are additional records with a reservation that are no
	 * longer available in the next time slot.
	 */

	return 0;
}

extern bool slurm_bf_licenses_relevant_hres_increase(list_t *current,
						     list_t *next,
						     job_record_t *job_ptr)
{
	bool next_has_more; /* next has more hres remaining avail to job */
	find_relevant_hres_diff_args_t args = {
		.licenses_cur = current,
		.licenses_next = next,
		.job_resv_ptr = job_ptr->resv_ptr,
	};

	if (!job_ptr->license_list)
		return false;

	next_has_more =
		list_find_first_ro(job_ptr->license_list,
				   _bf_licenses_find_relevant_hres_increase,
				   &args);
	if (args.licenses_cur_iter)
		list_iterator_destroy(args.licenses_cur_iter);

	return next_has_more;
}

/* sort appended resv bf_licenses to be in order of resv id and license id */
extern int bf_license_cmp(void *x, void *y)
{
	bf_license_t *entry_a = *(bf_license_t **) x;
	bf_license_t *entry_b = *(bf_license_t **) y;
	int resv_cmp_rc;

	if (!entry_a->resv_ptr && !entry_b->resv_ptr)
		return 0; /* keep these in cluster_license_list order */
	if (!entry_a->resv_ptr && entry_b->resv_ptr)
		return -1;
	if (entry_a->resv_ptr && !entry_b->resv_ptr)
		return 1;
	if ((resv_cmp_rc =
		     slurm_sort_uint32_list_asc(&entry_a->resv_ptr->resv_id,
						&entry_b->resv_ptr->resv_id)))
		return resv_cmp_rc;
	return slurm_sort_uint16_list_asc(&entry_a->id.lic_id,
					  &entry_b->id.lic_id);
}
