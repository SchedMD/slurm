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
#include "src/common/node_conf.h"
#include "src/common/read_config.h"
#include "src/common/run_command.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

const char plugin_name[] = "node_features helpers plugin";
const char plugin_type[] = "node_features/helpers";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static uid_t *allowed_uid = NULL;
static int allowed_uid_cnt = 0;
static List helper_features = NULL;
static List helper_exclusives = NULL;
static uint32_t boot_time = (5 * 60);
static uint32_t exec_time = 10;
static uint32_t node_reboot_weight = (INFINITE - 1);

typedef struct {
	const char *name;
	const char *helper;
} plugin_feature_t;

static int _cmp_str(void *x, void *key)
{
	return !strcmp(x, key);
}

static int _cmp_features(void *x, void *key)
{
	plugin_feature_t *feature = x;
	return !strcmp(feature->name, key);
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
			error("helpers.conf: Invalid AllowUserBoot: %s", tok);
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

	if (!feature->helper)
		return SLURM_ERROR;

	argv = xcalloc(3, sizeof(char *));	/* NULL terminated */
	argv[0] = xstrdup(feature->helper);
	argv[1] = xstrdup(feature->name);
	output = run_command("set_state", feature->helper,
			     argv, NULL, (exec_time * 1000), 0, &rc);

	if (rc != SLURM_SUCCESS) {
		error("failed to set new value for feature: %s", feature->name);
	}

	free_command_argv(argv);
	xfree(output);
	return rc;
}

static List _feature_get_state(const plugin_feature_t *feature)
{
	char *tmp, *kv;
	char *output = NULL;
	int rc = 0;
	List result = list_create(xfree_ptr);

	output = run_command("get_state", feature->helper,
			     NULL, NULL, (exec_time * 1000), 0, &rc);

	if (rc != SLURM_SUCCESS) {
		goto cleanup;
	}

	tmp = output;
	while ((kv = strsep(&tmp, "\n"))) {
		if (kv[0] == '\0')
			break;

		list_append(result, xstrdup(kv));
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
	if (existing != NULL) {
		error("feature \"%s\" previously registered with helper \"%s\"",
		      name, existing->helper);
		return SLURM_ERROR;
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
	char *tmp = input;
	char *entry;

	while ((entry = strsep(&tmp, ","))) {
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

static int parse_feature(void **data, slurm_parser_enum_t type,
			 const char *key, const char *name,
			 const char *line, char **leftover)
{
	static s_p_options_t feature_options[] = {
		 {"Helper", S_P_STRING},
		 {NULL},
	};
	s_p_hashtbl_t *tbl = NULL;
	char *path = NULL;
	int rc = -1;

	if (!_is_feature_valid(name)) {
		slurm_seterrno(ESLURM_INVALID_FEATURE);
		goto fail;
	}

	tbl = s_p_hashtbl_create(feature_options);
	if (!s_p_parse_line(tbl, *leftover, leftover))
		goto fail;

	s_p_get_string(&path, "Helper", tbl);

	/* In slurmctld context, we can have path == NULL */
	*data = _feature_create(name, path);
	xfree(path);

	rc = 1;

fail:
	s_p_hashtbl_destroy(tbl);
	return rc;
}

static int _parse_exclusives(void **data, slurm_parser_enum_t type,
			     const char *key, const char *name,
			     const char *line, char **leftover)
{
	*data = xstrdup(name);

	return 1;
}

static s_p_options_t conf_options[] = {
	{"Feature", S_P_ARRAY, parse_feature, (ListDelF) _feature_destroy},
	{"BootTime", S_P_UINT32},
	{"ExecTime", S_P_UINT32},
	{"MutuallyExclusive", S_P_ARRAY, _parse_exclusives, xfree_ptr},
	{"NodeRebootWeight", S_P_UINT32},
	{"AllowUserBoot", S_P_STRING},
	{NULL},
};

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
	if (s_p_parse_file(tbl, NULL, confpath, false) == SLURM_ERROR) {
		error("could not parse configuration file: %s", confpath);
		goto fail;
	}
	xfree(confpath);

	if (!s_p_get_array(&features, &count, "Feature", tbl)) {
		error("no \"Feature\" entry in configuration file %s",
		      confpath);
		goto fail;
	}

	if (s_p_get_string(&tmp_str, "AllowUserBoot", tbl)) {
		_make_uid_array(tmp_str);
		xfree(tmp_str);
	}

	for (int i = 0; i < count; ++i) {
		const plugin_feature_t *feature = features[i];
		if (_feature_register(feature->name, feature->helper))
			goto fail;
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

	if (!s_p_get_uint32(&node_reboot_weight, "NodeRebootWeight", tbl))
		info("NodeRebootWeight not specified, using default value: %u",
		     node_reboot_weight);

	rc = SLURM_SUCCESS;

fail:
	s_p_hashtbl_destroy(tbl);

	return rc;
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
	char *ptr = strstr(job_features, feature);
	unsigned int len = strlen(feature);

	/* check for every matching pattern */
	while (ptr) {
		/* check word+1 to verify exact match */
		if (isalnum(ptr[len]) || ptr[len] == '-' || ptr[len] == '.' ||
			ptr[len] == '_' || ptr[len] == '=') {
			ptr = strstr(&ptr[len], feature);
			continue;
		}

		/* check word-1 to verify exact match */
		if ((ptr != job_features) && isalnum(ptr[-1])) {
			ptr = strstr(&ptr[len], feature);
			continue;
		}

		((excl_count_t *) y)->count++;
		ptr = strstr(&ptr[len], feature);
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

	if (strstr(job_features, feature->name) != NULL) {
		return -1;
	}

	return 0;
}

extern int node_features_p_job_valid(char *job_features)
{
	if (!job_features)
		return SLURM_SUCCESS;

	/* Check the mutually exclusive lists */
	if (list_for_each(helper_exclusives, _count_exclusivity,
			  job_features) < 0) {
		error("job requests mutually exclusive features");
		return ESLURM_INVALID_FEATURE;
	}

	/* Check for unsupported constraint operators in constraint expression */
	if (!strpbrk(job_features, "[]()|*"))
		return SLURM_SUCCESS;

	/* If an unsupported operator was used, the constraint is valid only if
	 * the expression doesn't contain a feature handled by this plugin. */
	if (list_for_each(helper_features, _foreach_feature, job_features) < 0) {
		error("operator(s) \"[]()|*\" not allowed in constraint \"%s\" when using changeable features",
		      job_features);
		return ESLURM_INVALID_FEATURE;
	}

	return SLURM_SUCCESS;
}

extern int node_features_p_node_set(char *active_features)
{
	char *kv, *tmp;
	char *input = NULL;
	const plugin_feature_t *feature = NULL;
	int rc = SLURM_ERROR;

	input = xstrdup(active_features);
	tmp = input;
	while ((kv = strsep(&tmp, ","))) {

		feature = list_find_first(helper_features, _cmp_features, kv);
		if (!feature) {
			info("skipping unregistered feature \"%s\"", kv);
			continue;
		}

		if (_feature_set_state(feature) != SLURM_SUCCESS)
			goto fail;
	}

	rc = SLURM_SUCCESS;

fail:
	xfree(input);
	active_features[0] = '\0';
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

	if (!current || list_is_empty(current))
		return 0;

	/* filter out duplicates */
	list_for_each(current, _foreach_check_duplicates, all_current);

	list_destroy(current);

	return 0;
}

extern void node_features_p_node_state(char **avail_modes, char **current_mode)
{
	List all_current = NULL;
	List filtered_modes = NULL;
	_foreach_modes_t args;

	if (!avail_modes || !current_mode)
		return;

	verbose("original: avail=%s current=%s",
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

	list_destroy(all_current);
	list_destroy(filtered_modes);

	verbose("new: avail=%s current=%s", *avail_modes, *current_mode);
}

extern char *node_features_p_node_xlate(char *new_features, char *orig_features,
					char *avail_features, int node_inx)
{
	List features = NULL;
	char *feature = NULL;
	char *input = NULL;
	char *tmp = NULL;
	char *merged = NULL;

	verbose("new_features: %s", new_features);
	verbose("orig_features: %s", orig_features);
	verbose("avail_features: %s", avail_features);

	if (!new_features || new_features[0] == '\0')
		return xstrdup(orig_features);

	if (!orig_features || orig_features[0] == '\0')
		return xstrdup(new_features);

	/* Compute: merged = new_features U (orig_features - changeable_features) */
	features = list_create(xfree_ptr);

	/* Add all features in "new_features" */
	input = xstrdup(new_features);
	tmp = input;
	while ((feature = strsep(&tmp, ",")))
		list_append(features, xstrdup(feature));
	xfree(input);

	input = xstrdup(orig_features);
	tmp = input;
	while ((feature = strsep(&tmp, ","))) {
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

	list_destroy(features);
	verbose("merged features: %s", merged);

	return merged;
}

extern char *node_features_p_job_xlate(char *job_features)
{
	char *node_features = NULL;

	if (!job_features || (job_features[0] == '\0'))
		return NULL;

	if (strpbrk(job_features, "[]()|*") != NULL) {
		info("an unsupported constraint operator was used in \"%s\", clearing job constraint",
		     job_features);
		return NULL;
	}

	/*
	 * The only special character allowed in this plugin is the
	 * ampersand '&' character. Substitute all '&' for commas.
	 * If we allow other special characters then more parsing may be
	 * needed, similar to the knl_cray or knl_generic node_features plugins.
	 */
	node_features = xstrdup(job_features);
	xstrsubstituteall(node_features, "&", ",");

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
	key_pair->name = xstrdup("NodeRebootWeight");
	key_pair->value = xstrdup_printf("%u", node_reboot_weight);
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
	bitstr_t *bitmap;
	bitmap = bit_alloc(node_record_count);
	bit_set_all(bitmap);
	return bitmap;
}

extern char *node_features_p_node_xlate2(char *new_features)
{
	return xstrdup(new_features);
}

extern uint32_t node_features_p_boot_time(void)
{
	return boot_time;
}

extern uint32_t node_features_p_reboot_weight(void)
{
	return node_reboot_weight;
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
