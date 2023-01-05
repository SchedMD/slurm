/*****************************************************************************\
 *  node_features_helpers.c - Plugin for supporting arbitrary node features
 *  using external helper binaries
 *****************************************************************************
 *  Copyright (C) 2021 NVIDIA CORPORATION. All rights reserved.
 *  Written by NVIDIA CORPORATION.
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

#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>

#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/job_features.h"
#include "src/common/node_conf.h"
#include "src/common/read_config.h"
#include "src/common/run_command.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmd/slurmd/slurmd.h"

/*
 * These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern slurmd_conf_t *conf __attribute__((weak_import));
#else
slurmd_conf_t *conf = NULL;
#endif

const char plugin_name[] = "node_features helpers plugin";
const char plugin_type[] = "node_features/helpers";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static uid_t *allowed_uid = NULL;
static int allowed_uid_cnt = 0;
static List helper_features = NULL;
static List helper_exclusives = NULL;
static uint32_t boot_time = (5 * 60);
static uint32_t exec_time = 10;

typedef struct {
	list_t *final_list;
	bitstr_t *job_node_bitmap;
} build_valid_feature_set_args_t;

typedef struct {
	char *final_feature_str;
	bitstr_t *job_node_bitmap;
} valid_feature_args_t;

typedef struct {
	const char *name;
	const char *helper;
} plugin_feature_t;

static s_p_options_t feature_options[] = {
	 {"Feature", S_P_STRING},
	 {"Helper", S_P_STRING},
	 {NULL},
};

static int _cmp_str(void *x, void *key)
{
	return !xstrcmp(x, key);
}

static int _cmp_features(void *x, void *key)
{
	plugin_feature_t *feature = x;
	return !xstrcmp(feature->name, key);
}

static bool _is_feature_valid(const char *k)
{
	if (!k || k[0] == '\0')
		return false;

	if (!isalpha(k[0]) && k[0] != '_' && k[0] != '=')
		return false;

	for (int i = 1; k[i] != '\0'; ++i) {
		if (!isalnum(k[i]) && k[i] != '_' && k[i] != '.' && k[i] != '=')
			return false;
	}

	return true;
}

static void _make_uid_array(char *uid_str)
{
	char *save_ptr = NULL, *tmp_str, *tok;
	int uid_cnt = 0;

	if (!uid_str)
		return;

	/* Count the number of users */
	for (int i = 0; uid_str[i]; i++) {
		if (uid_str[i] == ',')
			uid_cnt++;
	}
	uid_cnt++;

	allowed_uid = xcalloc(uid_cnt, sizeof(uid_t));
	allowed_uid_cnt = 0;
	tmp_str = xstrdup(uid_str);
	tok = strtok_r(tmp_str, ",", &save_ptr);
	while (tok) {
		if (uid_from_string(tok, &allowed_uid[allowed_uid_cnt++]) < 0)
			fatal("helpers.conf: Invalid AllowUserBoot: %s", tok);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_str);
}

static int _list_make_str(void *x, void *y)
{
	char *feature = (char *) x;
	char **string = (char **) y;

	xstrfmtcat(*string, "%s%s", (*string ? "," : ""), feature);

	return 0;
}

static plugin_feature_t *_feature_create(const char *name, const char *helper)
{
	plugin_feature_t *feature = xmalloc(sizeof(*feature));

	feature->name = xstrdup(name);
	feature->helper = xstrdup(helper);

	return feature;
}

static void _feature_destroy(plugin_feature_t *feature)
{
	if (!feature)
		return;

	xfree(feature->name);
	xfree(feature->helper);
	xfree(feature);
}

static int _feature_set_state(const plugin_feature_t *feature)
{
	char *output, **argv = NULL;
	int rc = 0;
	run_command_args_t run_command_args = {
		.max_wait = (exec_time * 1000),
		.status = &rc };

	if (!feature->helper)
		return SLURM_ERROR;

	argv = xcalloc(3, sizeof(char *));	/* NULL terminated */
	argv[0] = xstrdup(feature->helper);
	argv[1] = xstrdup(feature->name);
	run_command_args.script_argv = argv;
	run_command_args.script_path = feature->helper;
	run_command_args.script_type = "set_state";
	output = run_command(&run_command_args);

	if (rc != SLURM_SUCCESS) {
		error("failed to set new value for feature: %s", feature->name);
	}

	xfree_array(argv);
	xfree(output);
	return rc;
}

static List _feature_get_state(const plugin_feature_t *feature)
{
	char *tmp, *saveptr;
	char *output = NULL;
	int rc = 0;
	List result = list_create(xfree_ptr);
	run_command_args_t run_command_args = {
		.max_wait = (exec_time * 1000),
		.script_path = feature->helper,
		.script_type = "get_state",
		.status = &rc };

	output = run_command(&run_command_args);

	if (rc != SLURM_SUCCESS) {
		goto cleanup;
	}

	for (tmp = strtok_r(output, "\n", &saveptr); tmp;
	     tmp = strtok_r(NULL, "\n", &saveptr)) {
		list_append(result, xstrdup(tmp));
	}

cleanup:
	xfree(output);
	return result;
}

static int _feature_register(const char *name, const char *helper)
{
	const plugin_feature_t *existing;
	plugin_feature_t *feature = NULL;

	existing = list_find_first(helper_features, _cmp_features,
				   (char *) name);
	if (existing) {
		if (running_in_slurmctld()) {
			/* The controller just needs the feature names */
			return SLURM_SUCCESS;
		} else if (xstrcmp(existing->helper, helper)) {
			error("feature \"%s\" previously registered with different helper \"%s\"",
			      name, existing->helper);
			return SLURM_ERROR;
		} else {
			debug("feature \"%s\" previously registered same helper \"%s\"",
			      name, existing->helper);
			return SLURM_SUCCESS;
		}
	}

	feature = _feature_create(name, helper);

	info("Adding new feature \"%s\"", feature->name);
	list_append(helper_features, feature);

	return SLURM_SUCCESS;
}

static int _exclusive_register(const char *listp)
{
	List data_list = list_create(xfree_ptr);
	char *input = xstrdup(listp);
	char *entry, *saveptr;

	for (entry = strtok_r(input, ",", &saveptr); entry;
	     entry = strtok_r(NULL, ",", &saveptr)) {
		if (list_find_first(data_list, _cmp_str, entry)) {
			error("Feature \"%s\" already in exclusive list",
			      entry);
			continue;
		}

		list_append(data_list, xstrdup(entry));
	}

	xfree(input);

	list_append(helper_exclusives, data_list);

	return SLURM_SUCCESS;
}

static int _parse_feature(void **data, slurm_parser_enum_t type,
			  const char *key, const char *name,
			  const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl = NULL;
	char *path = NULL;
	int rc = -1;
	char *tmp_name;

	tbl = s_p_hashtbl_create(feature_options);
	if (!s_p_parse_line(tbl, *leftover, leftover))
		goto fail;

	if (name) {
		tmp_name = xstrdup(name);
	} else if (!s_p_get_string(&tmp_name, "Feature", tbl)) {
			error("Invalid FEATURE data, no type Feature (%s)", line);
			goto fail;
	}

	s_p_get_string(&path, "Helper", tbl);

	/* In slurmctld context, we can have path == NULL */
	*data = _feature_create(tmp_name, path);
	xfree(path);

	rc = 1;

fail:
	xfree(tmp_name);
	s_p_hashtbl_destroy(tbl);
	return rc;
}

static int _parse_feature_node(void **data, slurm_parser_enum_t type, const char *key,
			       const char *name, const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl = NULL;

	if (!running_in_slurmctld() && conf->node_name && name) {
		bool match = false;
		hostlist_t hl;
		hl = hostlist_create(name);
		if (hl) {
			match = (hostlist_find(hl, conf->node_name) >= 0);
			hostlist_destroy(hl);
		}
		if (!match) {
			debug("skipping Feature for NodeName=%s %s", name, line);
			tbl = s_p_hashtbl_create(feature_options);
			s_p_parse_line(tbl, *leftover, leftover);
			s_p_hashtbl_destroy(tbl);
			return 0;
		}
	}
	return _parse_feature(data, type, key, NULL, line, leftover);
}

static int _parse_exclusives(void **data, slurm_parser_enum_t type,
			     const char *key, const char *name,
			     const char *line, char **leftover)
{
	*data = xstrdup(name);

	return 1;
}

static s_p_options_t conf_options[] = {
	{"AllowUserBoot", S_P_STRING},
	{"BootTime", S_P_UINT32},
	{"ExecTime", S_P_UINT32},
	{"Feature", S_P_ARRAY, _parse_feature, (ListDelF) _feature_destroy},
	{"MutuallyExclusive", S_P_ARRAY, _parse_exclusives, xfree_ptr},
	{"NodeName", S_P_ARRAY, _parse_feature_node,
		(ListDelF) _feature_destroy},
	{NULL},
};

static int _handle_config_features(plugin_feature_t **features, int count)
{
	for (int i = 0; i < count; ++i) {
		const plugin_feature_t *feature = features[i];
		char *tmp_name, *tok, *saveptr;

		tmp_name = xstrdup(feature->name);
		for (tok = strtok_r(tmp_name, ",", &saveptr); tok;
		     tok = strtok_r(NULL, ",", &saveptr)) {

			if (!_is_feature_valid(tok)) {
				slurm_seterrno(ESLURM_INVALID_FEATURE);
				xfree(tmp_name);
				return SLURM_ERROR;
			}

			/* In slurmctld context, we can have path == NULL */
			if (_feature_register(tok, feature->helper)) {
				xfree(tmp_name);
				return SLURM_ERROR;
			}
		}

		xfree(tmp_name);
	}

	return SLURM_SUCCESS;
}

static int _read_config_file(void)
{
	s_p_hashtbl_t *tbl = NULL;
	char *confpath = NULL;
	char *tmp_str = NULL;
	void **features = NULL;
	void **exclusives = NULL;
	int count = 0;
	int rc = SLURM_ERROR;

	xfree(allowed_uid);
	allowed_uid_cnt = 0;

	FREE_NULL_LIST(helper_features);
	helper_features = list_create((ListDelF) _feature_destroy);

	FREE_NULL_LIST(helper_exclusives);
	helper_exclusives = list_create((ListDelF) list_destroy);

	tbl = s_p_hashtbl_create(conf_options);

	confpath = get_extra_conf_path("helpers.conf");
	if (s_p_parse_file(tbl, NULL, confpath, false, NULL) == SLURM_ERROR) {
		error("could not parse configuration file: %s", confpath);
		goto fail;
	}
	xfree(confpath);

	if (s_p_get_array(&features, &count, "Feature", tbl) &&
	    _handle_config_features((plugin_feature_t **)features, count))
		goto fail;

	if (s_p_get_array(&features, &count, "NodeName", tbl) &&
	    _handle_config_features((plugin_feature_t **)features, count))
		goto fail;

	if (s_p_get_string(&tmp_str, "AllowUserBoot", tbl)) {
		_make_uid_array(tmp_str);
		xfree(tmp_str);
	}

	if (s_p_get_array(&exclusives, &count, "MutuallyExclusive", tbl) != 0) {
		for (int i = 0; i < count; ++i) {
			if (_exclusive_register(exclusives[i]))
				goto fail;
		}
	}

	if (!s_p_get_uint32(&boot_time, "BootTime", tbl))
		info("BootTime not specified, using default value: %u",
		     boot_time);

	if (!s_p_get_uint32(&exec_time, "ExecTime", tbl))
		info("ExecTime not specified, using default value: %u",
		     exec_time);

	rc = SLURM_SUCCESS;

fail:
	s_p_hashtbl_destroy(tbl);

	return rc;
}

static int _build_valid_feature_set(void *x, void *arg)
{
	job_feature_t *job_feat_ptr = x;
	build_valid_feature_set_args_t *args = arg;

	if (bit_super_set(args->job_node_bitmap,
			  job_feat_ptr->node_bitmap_avail)) {
		/* Valid - only include changeable features */
		if (!job_feat_ptr->changeable)
			return 0;

		/* The list should be unique already */
		list_append(args->final_list, xstrdup(job_feat_ptr->name));
	} else {
		/* Invalid */
		log_flag(NODE_FEATURES, "Feature %s is invalid",
			 job_feat_ptr->name);
		return -1; /* Exit list_for_each */
	}

	return 0;
}

static int _reconcile_job_features(void *x, void *arg)
{
	int rc = 0;
	list_t *final_list = NULL;
	list_t *features_list = x;
	valid_feature_args_t *valid_arg = arg;
	build_valid_feature_set_args_t build_arg = {
		.job_node_bitmap = valid_arg->job_node_bitmap,
	};

	final_list = list_create(xfree_ptr);
	build_arg.final_list = final_list;

	if (slurm_conf.debug_flags & DEBUG_FLAG_NODE_FEATURES) {
		char *list_str = NULL;
		char *nodes_str = bitmap2node_name(valid_arg->job_node_bitmap);

		job_features_set2str(features_list, &list_str);
		log_flag(NODE_FEATURES, "Check if the features %s are valid on nodes %s",
			 list_str, nodes_str);
		xfree(list_str);
		xfree(nodes_str);
	}
	if (list_for_each(features_list, _build_valid_feature_set,
			  &build_arg) < 0) {
		rc = 0; /* Continue to next list */
	} else {
		list_for_each(final_list, _list_make_str,
			      &valid_arg->final_feature_str);
		rc = -1; /* Got a valid feature list; stop iterating */
	}

	FREE_NULL_LIST(final_list);
	return rc;
}

static char *_xlate_job_features(char *job_features,
				 list_t *job_feature_list,
				 bitstr_t *job_node_bitmap)
{
	list_t *feature_sets;
	valid_feature_args_t valid_arg = {
		.final_feature_str = NULL,
		.job_node_bitmap = job_node_bitmap
	};

	if (slurm_conf.debug_flags & DEBUG_FLAG_NODE_FEATURES) {
		char *tmp_str = bitmap2node_name(job_node_bitmap);
		log_flag(NODE_FEATURES, "Find a valid feature combination for %s on nodes %s",
			 job_features, tmp_str);
		xfree(tmp_str);
	}

	feature_sets = job_features_list2feature_sets(job_features,
						      job_feature_list,
						      true);

	/*
	 * Find the first feature set that works for this job and turn it into a
	 * comma-separated list of char* of only the changeable features.
	 */
	list_for_each(feature_sets,
		      _reconcile_job_features,
		      &valid_arg);
	log_flag(NODE_FEATURES, "final_feature_str=%s",
		 valid_arg.final_feature_str);

	FREE_NULL_LIST(feature_sets);

	return valid_arg.final_feature_str;
}

extern int init(void)
{
	return _read_config_file();
}

extern int fini(void)
{
	FREE_NULL_LIST(helper_features);
	FREE_NULL_LIST(helper_exclusives);
	xfree(allowed_uid);
	allowed_uid_cnt = 0;

	return SLURM_SUCCESS;
}

extern bool node_features_p_changeable_feature(char *input)
{
	plugin_feature_t *feature = NULL;

	feature = list_find_first(helper_features, _cmp_features, input);
	if (!feature)
		return false;

	return true;
}

typedef struct {
	char *job_features;
	unsigned int count;
} excl_count_t;

static int _get_list_excl_count(void *x, void *y)
{
	char *feature = (char *) x;
	char *job_features = ((excl_count_t *) y)->job_features;
	char *ptr = xstrstr(job_features, feature);
	unsigned int len = strlen(feature);

	/* check for every matching pattern */
	while (ptr) {
		/* check word+1 to verify exact match */
		if (isalnum(ptr[len]) || ptr[len] == '-' || ptr[len] == '.' ||
			ptr[len] == '_' || ptr[len] == '=') {
			ptr = xstrstr(&ptr[len], feature);
			continue;
		}

		/* check word-1 to verify exact match */
		if ((ptr != job_features) && isalnum(ptr[-1])) {
			ptr = xstrstr(&ptr[len], feature);
			continue;
		}

		((excl_count_t *) y)->count++;
		ptr = xstrstr(&ptr[len], feature);
	}

	return 0;
}

static int _count_exclusivity(void *x, void *y)
{
	List exclusive_list = (List) x;

	excl_count_t args = {
		.job_features = (char *)y,
		.count = 0
	};

	list_for_each(exclusive_list, _get_list_excl_count, &args);

	if (args.count > 1)
		return -1;
	else
		return 0;
}

static int _foreach_feature(void *x, void *y)
{
	char *job_features = (char *)y;
	plugin_feature_t *feature = (plugin_feature_t *)x;

	if (xstrstr(job_features, feature->name) != NULL) {
		return -1;
	}

	return 0;
}

static int _has_exclusive_features(void *x, void *arg)
{
	List feature_list = x;
	char *str = NULL;
	int rc = 0;

	job_features_set2str(feature_list, &str);
	log_flag(NODE_FEATURES, "Testing if feature list %s has exclusive features",
		 str);

	if (list_count(feature_list) > 1)
		rc = list_for_each(helper_exclusives, _count_exclusivity, str);
	xfree(str);

	return rc;
}

extern int node_features_p_job_valid(char *job_features, list_t *feature_list)
{
	List feature_sets;
	int rc;

	if (!job_features)
		return SLURM_SUCCESS;

	if (list_for_each(helper_features, _foreach_feature,
			  job_features) >= 0) {
		/* No helpers features requested */
		return SLURM_SUCCESS;
	}

	/* Check the mutually exclusive lists */
	feature_sets = job_features_list2feature_sets(job_features,
						      feature_list, true);
	rc = list_for_each(feature_sets, _has_exclusive_features, NULL);
	FREE_NULL_LIST(feature_sets);
	if (rc < 0) {
		error("job requests mutually exclusive features");
		return ESLURM_INVALID_FEATURE;
	}

	/* Check for unsupported constraint operators in constraint expression */
	if (!strpbrk(job_features, "[]*"))
		return SLURM_SUCCESS;

	/* If an unsupported operator was used, the constraint is valid only if
	 * the expression doesn't contain a feature handled by this plugin. */
	if (list_for_each(helper_features, _foreach_feature, job_features) < 0) {
		error("operator(s) \"[]*\" not allowed in constraint \"%s\" when using changeable features",
		      job_features);
		return ESLURM_INVALID_FEATURE;
	}

	return SLURM_SUCCESS;
}

extern int node_features_p_node_set(char *active_features)
{
	char *tmp, *saveptr;
	char *input = NULL;
	const plugin_feature_t *feature = NULL;
	int rc = SLURM_ERROR;

	input = xstrdup(active_features);
	for (tmp = strtok_r(input, ",", &saveptr); tmp;
	     tmp = strtok_r(NULL, ",", &saveptr)) {
		feature = list_find_first(helper_features, _cmp_features, tmp);
		if (!feature) {
			info("skipping unregistered feature \"%s\"", tmp);
			continue;
		}

		if (_feature_set_state(feature) != SLURM_SUCCESS) {
			active_features[0] = '\0';
			goto fini;
		}
	}

	rc = SLURM_SUCCESS;

fini:
	xfree(input);
	return rc;
}

static int _foreach_filter_modes(void *x, void *y)
{
	char *feature = (char *)x;
	List filtered = (List)y;

	/* Verify this mode is legitimate to filter out garbage */
	if (list_find_first(helper_features, _cmp_features, feature))
		list_append(filtered, xstrdup(feature));

	return 0;
}

static int _foreach_check_duplicates(void *x, void *y)
{
	char *feature = (char *)x;
	List filtered = (List)y;

	if (!list_find_first(filtered, _cmp_str, feature))
		list_append(filtered, xstrdup(feature));

	return 0;
}

typedef struct {
	char **avail_modes;
	List all_current;
} _foreach_modes_t;

static int _foreach_helper_get_modes(void *x, void *y)
{
	char **avail_modes = ((_foreach_modes_t *)y)->avail_modes;
	List all_current = ((_foreach_modes_t *)y)->all_current;
	plugin_feature_t *feature = (plugin_feature_t *)x;

	List current = _feature_get_state(feature);

	xstrfmtcat(*avail_modes, "%s%s", (*avail_modes ? "," : ""), feature->name);

	if (!current || list_is_empty(current)) {
		FREE_NULL_LIST(current);
		return 0;
	}

	/* filter out duplicates */
	list_for_each(current, _foreach_check_duplicates, all_current);

	FREE_NULL_LIST(current);

	return 0;
}

extern void node_features_p_node_state(char **avail_modes, char **current_mode)
{
	List all_current = NULL;
	List filtered_modes = NULL;
	_foreach_modes_t args;

	if (!avail_modes || !current_mode)
		return;

	log_flag(NODE_FEATURES, "original: avail=%s current=%s",
		 *avail_modes, *current_mode);

	all_current = list_create(xfree_ptr);

	args.all_current = all_current;
	args.avail_modes = avail_modes;

	/*
	 * Call every helper with no args to get list of active features
	 */
	list_for_each(helper_features, _foreach_helper_get_modes, &args);

	filtered_modes = list_create(xfree_ptr);

	/* Filter out garbage */
	list_for_each(all_current, _foreach_filter_modes, filtered_modes);

	list_for_each(filtered_modes, _list_make_str, current_mode);

	FREE_NULL_LIST(all_current);
	FREE_NULL_LIST(filtered_modes);

	log_flag(NODE_FEATURES, "new: avail=%s current=%s",
		 *avail_modes, *current_mode);
}

extern char *node_features_p_node_xlate(char *new_features, char *orig_features,
					char *avail_features, int node_inx)
{
	List features = NULL;
	char *feature = NULL;
	char *input = NULL;
	char *merged = NULL;
	char *saveptr = NULL;

	log_flag(NODE_FEATURES, "new_features: %s", new_features);
	log_flag(NODE_FEATURES, "orig_features: %s", orig_features);
	log_flag(NODE_FEATURES, "avail_features: %s", avail_features);

	if (!new_features || new_features[0] == '\0')
		return xstrdup(orig_features);

	if (!orig_features || orig_features[0] == '\0')
		return xstrdup(new_features);

	/* Compute: merged = new_features U (orig_features - changeable_features) */
	features = list_create(xfree_ptr);

	/* Add all features in "new_features" */
	input = xstrdup(new_features);
	for (feature = strtok_r(input, ",", &saveptr); feature;
	     feature = strtok_r(NULL, ",", &saveptr)) {
		list_append(features, xstrdup(feature));
	}
	xfree(input);

	input = xstrdup(orig_features);
	for (feature = strtok_r(input, ",", &saveptr); feature;
	     feature = strtok_r(NULL, ",", &saveptr)) {
		/* orig_features - plugin_changeable_features */
		if (node_features_p_changeable_feature(feature))
			continue;
		/* new_features U (orig_features - plugin_changeable_features) */
		if (list_find_first(features, _cmp_str, feature) != NULL)
			continue;
		list_append(features, xstrdup(feature));
	}
	xfree(input);

	list_for_each(features, _list_make_str, &merged);

	FREE_NULL_LIST(features);
	log_flag(NODE_FEATURES, "merged features: %s", merged);

	return merged;
}

extern char *node_features_p_job_xlate(char *job_features,
				       list_t *feature_list,
				       bitstr_t *job_node_bitmap)
{
	char *node_features = NULL;

	if (!job_features)
		return NULL;

	if (strpbrk(job_features, "[]*")) {
		info("an unsupported constraint operator was used in \"%s\", clearing job constraint",
		     job_features);
		return NULL;
	}

	node_features = _xlate_job_features(job_features, feature_list,
					    job_node_bitmap);
	if (!node_features) {
		char *job_nodes = bitmap2node_name(job_node_bitmap);
		/*
		 * This should not happen and means there is a mismatch in
		 * handling features in this plugin and in the scheduler.
		 */
		error("Failed to translate feature request '%s' into features that match with the job's nodes '%s'",
		      job_features, job_nodes);
		xfree(job_nodes);
	}

	return node_features;
}

/* Return true if the plugin requires PowerSave mode for booting nodes */
extern bool node_features_p_node_power(void)
{
	return false;
}

static char *_make_helper_str(const plugin_feature_t *feature)
{
	char *str = NULL;
	/* Format: "Name Helper=<path>" */

	xstrfmtcat(str, "%s Helper=%s", feature->name, feature->helper);

	return str;
}

static char *_make_exclusive_str(List exclusive)
{
	char *str = NULL;

	list_for_each(exclusive, _list_make_str, &str);

	return str;
}

static char *_make_uid_str(uid_t *uid_array, int uid_cnt)
{
	char *sep = "", *tmp_str = NULL, *uid_str = NULL;
	int i;

	if (allowed_uid_cnt == 0) {
		uid_str = xstrdup("ALL");
		return uid_str;
	}

	for (i = 0; i < uid_cnt; i++) {
		tmp_str = uid_to_string(uid_array[i]);
		xstrfmtcat(uid_str, "%s%s(%d)", sep, tmp_str, uid_array[i]);
		xfree(tmp_str);
		sep = ",";
	}

	return uid_str;
}

static int _make_features_config(void *x, void *y)
{
	plugin_feature_t *feature = (plugin_feature_t *)x;
	List data = (List)y;
	config_key_pair_t *key_pair;

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("Feature");
	key_pair->value = _make_helper_str(feature);
	list_append(data, key_pair);

	return 0;
}

static int _make_exclusive_config(void *x, void *y)
{
	List exclusive = (List) x;
	List data = (List) y;
	config_key_pair_t *key_pair;

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("MutuallyExclusive");
	key_pair->value = _make_exclusive_str(exclusive);
	list_append(data, key_pair);

	return 0;
}

/* Get node features plugin configuration */
extern void node_features_p_get_config(config_plugin_params_t *p)
{
	config_key_pair_t *key_pair;
	List data;

	xassert(p);
	xstrcat(p->name, plugin_type);
	data = p->key_pairs;

	list_for_each(helper_features, _make_features_config, data);

	list_for_each(helper_exclusives, _make_exclusive_config, data);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("AllowUserBoot");
	key_pair->value = _make_uid_str(allowed_uid, allowed_uid_cnt);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("BootTime");
	key_pair->value = xstrdup_printf("%u", boot_time);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(*key_pair));
	key_pair->name = xstrdup("ExecTime");
	key_pair->value = xstrdup_printf("%u", exec_time);
	list_append(data, key_pair);
}

extern bitstr_t *node_features_p_get_node_bitmap(void)
{
	return node_conf_get_active_bitmap();
}

extern char *node_features_p_node_xlate2(char *new_features)
{
	return xstrdup(new_features);
}

extern uint32_t node_features_p_boot_time(void)
{
	return boot_time;
}

extern int node_features_p_reconfig(void)
{
	return _read_config_file();
}

extern bool node_features_p_user_update(uid_t uid)
{
	/* Default is ALL users allowed to update */
	if (allowed_uid_cnt == 0)
		return true;

	for (int i = 0; i < allowed_uid_cnt; i++) {
		if (allowed_uid[i] == uid)
			return true;
	}
	log_flag(NODE_FEATURES, "UID %u is not allowed to update node features",
		 uid);

	return false;
}

extern void node_features_p_step_config(bool mem_sort, bitstr_t *numa_bitmap)
{
	return;
}

extern int node_features_p_overlap(bitstr_t *active_bitmap)
{
	/* Executed on slurmctld and not used by this plugin */
	return bit_set_count(active_bitmap);
}

extern int node_features_p_get_node(char *node_list)
{
	/* Executed on slurmctld and not used by this plugin */
	return SLURM_SUCCESS;
}

extern int node_features_p_node_update(char *active_features,
				       bitstr_t *node_bitmap)
{
	/* Executed on slurmctld and not used by this plugin */
	return SLURM_SUCCESS;
}

extern bool node_features_p_node_update_valid(void *node_ptr,
					      update_node_msg_t *update_node_msg)
{
	/* Executed on slurmctld and not used by this plugin */
	return true;
}
