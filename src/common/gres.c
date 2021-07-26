/*****************************************************************************\
 *  gres.c - driver for gres plugin
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2014-2019 SchedMD LLC
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "config.h"

#define _GNU_SOURCE

#ifdef __FreeBSD__
#  include <sys/param.h>
#  include <sys/cpuset.h>
typedef cpuset_t cpu_set_t;
#endif

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef MAJOR_IN_MKDEV
#  include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#  include <sys/sysmacros.h>
#endif

#include <math.h>

#ifdef __NetBSD__
#define CPU_ZERO(c) cpuset_zero(*(c))
#define CPU_ISSET(i,c) cpuset_isset((i),*(c))
#define sched_getaffinity sched_getaffinity_np
#endif

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/gres.h"
#include "src/common/job_resources.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_conf.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define MAX_GRES_BITMAP 1024

strong_alias(gres_gresid_to_gresname, slurm_gres_gresid_to_gresname);
strong_alias(gres_get_node_used, slurm_gres_get_node_used);
strong_alias(gres_get_system_cnt, slurm_gres_get_system_cnt);
strong_alias(gres_get_value_by_type, slurm_gres_get_value_by_type);
strong_alias(gres_get_job_info, slurm_gres_get_job_info);
strong_alias(gres_build_job_details, slurm_gres_build_job_details);
strong_alias(gres_get_step_info, slurm_gres_get_step_info);
strong_alias(gres_get_step_state, slurm_gres_get_step_state);
strong_alias(gres_get_job_state, slurm_gres_get_job_state);
strong_alias(gres_2_tres_str, slurm_gres_2_tres_str);
strong_alias(gres_set_job_tres_cnt, slurm_gres_set_job_tres_cnt);
strong_alias(gres_set_node_tres_cnt, slurm_gres_set_node_tres_cnt);
strong_alias(gres_device_major, slurm_gres_device_major);
strong_alias(destroy_gres_device, slurm_destroy_gres_device);
strong_alias(destroy_gres_slurmd_conf, slurm_destroy_gres_slurmd_conf);

/* Gres symbols provided by the plugin */
typedef struct slurm_gres_ops {
	int		(*node_config_load)	( List gres_conf_list,
						  node_config_load_t *node_conf);
	void		(*job_set_env)		( char ***job_env_ptr,
						  void *gres_ptr, int node_inx,
						  gres_internal_flags_t flags);
	void		(*step_set_env)		( char ***job_env_ptr,
						  void *gres_ptr,
						  gres_internal_flags_t flags);
	void		(*step_reset_env)	( char ***job_env_ptr,
						  void *gres_ptr,
						  bitstr_t *usable_gres,
						  gres_internal_flags_t flags);
	void		(*send_stepd)		( Buf buffer );
	void		(*recv_stepd)		( Buf buffer );
	int		(*job_info)		( gres_job_state_t *job_gres_data,
						  uint32_t node_inx,
						  enum gres_job_data_type data_type,
						  void *data);
	int		(*step_info)		( gres_step_state_t *step_gres_data,
						  uint32_t node_inx,
						  enum gres_step_data_type data_type,
						  void *data);
	List            (*get_devices)		( void );
	void            (*step_hardware_init)	( bitstr_t *, char * );
	void            (*step_hardware_fini)	( void );
	gres_epilog_info_t *(*epilog_build_env)(gres_job_state_t *gres_job_ptr);
	void            (*epilog_set_env)	( char ***epilog_env_ptr,
						gres_epilog_info_t *epilog_info,
						int node_inx );
} slurm_gres_ops_t;

/*
 * Gres plugin context, one for each gres type.
 * Add to gres_context through _add_gres_context().
 */
typedef struct slurm_gres_context {
	plugin_handle_t	cur_plugin;
	uint8_t		config_flags;		/* See GRES_CONF_* in gres.h */
	char *		gres_name;		/* name (e.g. "gpu") */
	char *		gres_name_colon;	/* name + colon (e.g. "gpu:") */
	int		gres_name_colon_len;	/* size of gres_name_colon */
	char *		gres_type;		/* plugin name (e.g. "gres/gpu") */
	slurm_gres_ops_t ops;			/* pointers to plugin symbols */
	uint32_t	plugin_id;		/* key for searches */
	plugrack_t	*plugin_list;		/* plugrack info */
	uint64_t        total_cnt;		/* Total GRES across all nodes */
} slurm_gres_context_t;

/* Generic gres data structure for adding to a list. Depending upon the
 * context, gres_data points to gres_node_state_t, gres_job_state_t or
 * gres_step_state_t */
typedef struct gres_state {
	uint32_t	plugin_id;
	void		*gres_data;
} gres_state_t;

typedef struct gres_search_key {
	int node_offset;
	uint32_t plugin_id;
	uint32_t type_id;
} gres_key_t;

typedef struct {
	slurm_gres_context_t *context_ptr;
	int new_has_file;
	int new_has_type;
	int rec_count;
} foreach_gres_conf_t;

/* Pointers to functions in src/slurmd/common/xcpuinfo.h that we may use */
typedef struct xcpuinfo_funcs {
	int (*xcpuinfo_abs_to_mac) (char *abs, char **mac);
} xcpuinfo_funcs_t;
xcpuinfo_funcs_t xcpuinfo_ops;

/* Local variables */
static int gres_context_cnt = -1;
static uint32_t gres_cpu_cnt = 0;
static slurm_gres_context_t *gres_context = NULL;
static char *gres_node_name = NULL;
static char *gres_plugin_list = NULL;
static pthread_mutex_t gres_context_lock = PTHREAD_MUTEX_INITIALIZER;
static List gres_conf_list = NULL;
static bool init_run = false;
static bool have_gpu = false, have_mps = false;
static uint32_t gpu_plugin_id = NO_VAL, mps_plugin_id = NO_VAL;
static volatile uint32_t autodetect_flags = GRES_AUTODETECT_UNSET;
static uint32_t select_plugin_type = NO_VAL;
static Buf gres_context_buf = NULL;
static Buf gres_conf_buf = NULL;

/* Local functions */
static void _add_gres_context(char *gres_name);
static gres_node_state_t *
		_build_gres_node_state(void);
static void	_build_node_gres_str(List *gres_list, char **gres_str,
				     int cores_per_sock, int sock_per_node);
static uint32_t **_build_tasks_per_node_sock(struct job_resources *job_res,
					    uint8_t overcommit,
					    gres_mc_data_t *tres_mc_ptr,
					    node_record_t *node_table_ptr);
static bitstr_t *_core_bitmap_rebuild(bitstr_t *old_core_bitmap, int new_size);
static void	_epilog_list_del(void *x);
static int	_find_job_by_sock_gres(void *x, void *key);
static int	_find_sock_by_job_gres(void *x, void *key);
static void	_free_tasks_per_node_sock(uint32_t **tasks_per_node_socket,
					  int node_cnt);
static void	_get_gres_cnt(gres_node_state_t *gres_data, char *orig_config,
			      char *gres_name, char *gres_name_colon,
			      int gres_name_colon_len);
static uint64_t _get_job_gres_list_cnt(List gres_list, char *gres_name,
				       char *gres_type);
static uint32_t	_get_task_cnt_node(uint32_t **tasks_per_node_socket,
				   int node_inx, int sock_cnt);
static uint64_t	_get_tot_gres_cnt(uint32_t plugin_id, uint64_t *topo_cnt,
				  int *config_type_cnt);
static int	_gres_find_id(void *x, void *key);
static int	_gres_find_job_by_key(void *x, void *key);
static int	_gres_find_step_by_key(void *x, void *key);
static void	_gres_job_list_delete(void *list_element);
static int	_job_alloc(void *job_gres_data, void *node_gres_data,
			   int node_cnt, int node_index, int node_offset,
			   char *gres_name, uint32_t job_id, char *node_name,
			   bitstr_t *core_bitmap, uint32_t plugin_id,
			   uint32_t user_id);
static void	_job_core_filter(void *job_gres_data, void *node_gres_data,
				 bool use_total_gres, bitstr_t *core_bitmap,
				 int core_start_bit, int core_end_bit,
				 char *gres_name, char *node_name,
				 uint32_t plugin_id);
static int	_job_dealloc(void *job_gres_data, void *node_gres_data,
			     int node_offset, char *gres_name, uint32_t job_id,
			     char *node_name, bool old_job, uint32_t plugin_id,
			     uint32_t user_id, bool job_fini);
static void	_job_state_delete(void *gres_data);
static void *	_job_state_dup(void *gres_data);
static void *	_job_state_dup2(void *gres_data, int node_index);
static void	_job_state_log(void *gres_data, uint32_t job_id,
			       uint32_t plugin_id);
static uint32_t _job_test(void *job_gres_data, void *node_gres_data,
			  bool use_total_gres, bitstr_t *core_bitmap,
			  int core_start_bit, int core_end_bit, bool *topo_set,
			  uint32_t job_id, char *node_name, char *gres_name,
			  uint32_t plugin_id, bool disable_binding);
static int	_load_gres_plugin(slurm_gres_context_t *plugin_context);
static int	_log_gres_slurmd_conf(void *x, void *arg);
static void	_my_stat(char *file_name);
static int	_node_config_init(char *node_name, char *orig_config,
				  slurm_gres_context_t *context_ptr,
				  gres_state_t *gres_ptr);
static char *	_node_gres_used(void *gres_data, char *gres_name);
static int	_node_reconfig(char *node_name, char *new_gres, char **gres_str,
			       gres_state_t *gres_ptr, bool config_overrides,
			       slurm_gres_context_t *context_ptr,
			       bool *updated_gpu_cnt);
static int	_node_reconfig_test(char *node_name, char *new_gres,
				    gres_state_t *gres_ptr,
				    slurm_gres_context_t *context_ptr);
static void	_node_state_dealloc(gres_state_t *gres_ptr);
static void *	_node_state_dup(void *gres_data);
static void	_node_state_log(void *gres_data, char *node_name,
				char *gres_name);
static int	_parse_gres_config(void **dest, slurm_parser_enum_t type,
				   const char *key, const char *value,
				   const char *line, char **leftover);
static int	_parse_gres_config2(void **dest, slurm_parser_enum_t type,
				    const char *key, const char *value,
				    const char *line, char **leftover);
static bool	_shared_gres(uint32_t plugin_id);
static bool	_sharing_gres(uint32_t plugin_id);
static void	_sock_gres_del(void *x);
static int	_step_alloc(void *step_gres_data, void *job_gres_data,
			    uint32_t plugin_id, int node_offset,
			    bool first_step_node,
			    slurm_step_id_t *step_id,
			    uint16_t tasks_on_node, uint32_t rem_nodes);
static int      _step_dealloc(gres_state_t *step_gres_ptr, List job_gres_list,
			      slurm_step_id_t *step_id);
static void *	_step_state_dup(void *gres_data);
static void *	_step_state_dup2(void *gres_data, int node_index);
static void	_step_state_log(void *gres_data, slurm_step_id_t *step_id,
				char *gres_name);
static uint64_t _step_test(void *step_gres_data, void *job_gres_data,
			   int node_offset, bool first_step_node,
			   uint16_t cpus_per_task, int max_rem_nodes,
			   bool ignore_alloc, slurm_step_id_t *step_id,
			   uint32_t plugin_id);
static void	_sync_node_mps_to_gpu(gres_state_t *mps_gres_ptr,
				      gres_state_t *gpu_gres_ptr);
static int	_unload_gres_plugin(slurm_gres_context_t *plugin_context);
static void	_validate_slurm_conf(List slurm_conf_list,
				     slurm_gres_context_t *context_ptr);
static void	_validate_gres_conf(List gres_conf_list,
				    slurm_gres_context_t *context_ptr);
static int	_validate_file(char *path_name, char *gres_name);
static void	_validate_links(gres_slurmd_conf_t *p);
static void	_validate_gres_node_cores(gres_node_state_t *node_gres_ptr,
					  int cpus_ctld, char *node_name);
static int	_valid_gres_type(char *gres_name, gres_node_state_t *gres_data,
				 bool config_overrides, char **reason_down);

extern uint32_t gres_plugin_build_id(char *name)
{
	int i, j;
	uint32_t id = 0;

	if (!name)
		return id;

	for (i = 0, j = 0; name[i]; i++) {
		id += (name[i] << j);
		j = (j + 8) % 32;
	}

	return id;
}

static int _gres_find_id(void *x, void *key)
{
	uint32_t *plugin_id = (uint32_t *)key;
	gres_state_t *state_ptr = (gres_state_t *) x;
	if (state_ptr->plugin_id == *plugin_id)
		return 1;
	return 0;
}

/* Find job record with matching name and type */
static int _gres_find_job_by_key(void *x, void *key)
{
	gres_state_t *state_ptr = (gres_state_t *) x;
	gres_key_t *job_key = (gres_key_t *) key;
	gres_job_state_t *gres_data_ptr;
	gres_data_ptr = (gres_job_state_t *)state_ptr->gres_data;

	if ((state_ptr->plugin_id == job_key->plugin_id) &&
	    ((job_key->type_id == NO_VAL) ||
	     (gres_data_ptr->type_id == job_key->type_id)))
		return 1;
	return 0;
}

/* Find job record with matching name and type */
static int _gres_find_job_by_key_with_cnt(void *x, void *key)
{
	gres_state_t *state_ptr = (gres_state_t *) x;
	gres_key_t *job_key = (gres_key_t *) key;
	gres_job_state_t *gres_data_ptr;
	gres_data_ptr = (gres_job_state_t *)state_ptr->gres_data;

	if (!_gres_find_job_by_key(x, key))
		return 0;
	/* ignore count on no_consume gres */
	if (!gres_data_ptr->node_cnt ||
	    gres_data_ptr->gres_cnt_node_alloc[job_key->node_offset])
		return 1;
	return 0;
}

static int _gres_find_step_by_key(void *x, void *key)
{
	gres_state_t *state_ptr = (gres_state_t *) x;
	gres_key_t *step_key = (gres_key_t *) key;
	gres_step_state_t *gres_data_ptr;
	gres_data_ptr = (gres_step_state_t *)state_ptr->gres_data;

	if ((state_ptr->plugin_id == step_key->plugin_id) &&
	    (gres_data_ptr->type_id == step_key->type_id))
		return 1;
	return 0;
}

static int _gres_find_name_internal(char *name, char *key, uint32_t plugin_id)
{
	if (!name) {
		int i;
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_context[i].plugin_id == plugin_id) {
				name = gres_context[i].gres_name;
				break;
			}
		}

		if (!name) {
			debug("%s: couldn't find name", __func__);
			return 0;
		}
	}

	if (!xstrcmp(name, key))
		return 1;
	return 0;
}

static int _gres_job_find_name(void *x, void *key)
{
	gres_state_t *state_ptr = (gres_state_t *) x;
	gres_job_state_t *gres_data_ptr =
		(gres_job_state_t *)state_ptr->gres_data;

	return _gres_find_name_internal(gres_data_ptr->type_name, (char *)key,
					state_ptr->plugin_id);
}

static int _gres_step_find_name(void *x, void *key)
{
	gres_state_t *state_ptr = (gres_state_t *) x;
	gres_step_state_t *gres_data_ptr =
		(gres_step_state_t *)state_ptr->gres_data;
	return _gres_find_name_internal(gres_data_ptr->type_name, (char *)key,
					state_ptr->plugin_id);
}

static int _load_gres_plugin(slurm_gres_context_t *plugin_context)
{
	/*
	 * Must be synchronized with slurm_gres_ops_t above.
	 */
	static const char *syms[] = {
		"node_config_load",
		"job_set_env",
		"step_set_env",
		"step_reset_env",
		"send_stepd",
		"recv_stepd",
		"job_info",
		"step_info",
		"get_devices",
		"step_hardware_init",
		"step_hardware_fini",
		"epilog_build_env",
		"epilog_set_env"
	};
	int n_syms = sizeof(syms) / sizeof(char *);

	/* Find the correct plugin */
	if (plugin_context->config_flags & GRES_CONF_COUNT_ONLY) {
		debug("Plugin of type %s only tracks gres counts",
		      plugin_context->gres_type);
		return SLURM_SUCCESS;
	}

	plugin_context->cur_plugin = plugin_load_and_link(
					plugin_context->gres_type,
					n_syms, syms,
					(void **) &plugin_context->ops);
	if (plugin_context->cur_plugin != PLUGIN_INVALID_HANDLE)
		return SLURM_SUCCESS;

	if (errno != EPLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      plugin_context->gres_type, plugin_strerror(errno));
		return SLURM_ERROR;
	}

	debug("gres: Couldn't find the specified plugin name for %s looking "
	      "at all files", plugin_context->gres_type);

	/* Get plugin list */
	if (plugin_context->plugin_list == NULL) {
		plugin_context->plugin_list = plugrack_create("gres");
		plugrack_read_dir(plugin_context->plugin_list,
		                  slurm_conf.plugindir);
	}

	plugin_context->cur_plugin = plugrack_use_by_type(
					plugin_context->plugin_list,
					plugin_context->gres_type );
	if (plugin_context->cur_plugin == PLUGIN_INVALID_HANDLE) {
		debug("Cannot find plugin of type %s, just track gres counts",
		      plugin_context->gres_type);
		plugin_context->config_flags |= GRES_CONF_COUNT_ONLY;
		return SLURM_ERROR;
	}

	/* Dereference the API. */
	if (plugin_get_syms(plugin_context->cur_plugin,
			    n_syms, syms,
			    (void **) &plugin_context->ops ) < n_syms ) {
		error("Incomplete %s plugin detected",
		      plugin_context->gres_type);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _unload_gres_plugin(slurm_gres_context_t *plugin_context)
{
	int rc;

	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if (plugin_context->plugin_list)
		rc = plugrack_destroy(plugin_context->plugin_list);
	else {
		rc = SLURM_SUCCESS;
		plugin_unload(plugin_context->cur_plugin);
	}
	xfree(plugin_context->gres_name);
	xfree(plugin_context->gres_name_colon);
	xfree(plugin_context->gres_type);

	return rc;
}

/*
 * Add new gres context to gres_context array and load the plugin.
 * Must hold gres_context_lock before calling.
 */
static void _add_gres_context(char *gres_name)
{
	slurm_gres_context_t *plugin_context;

	if (!gres_name || !gres_name[0])
		fatal("%s: invalid empty gres_name", __func__);

	xrecalloc(gres_context, (gres_context_cnt + 1),
		  sizeof(slurm_gres_context_t));

	plugin_context = &gres_context[gres_context_cnt];
	plugin_context->gres_name = xstrdup(gres_name);
	plugin_context->plugin_id = gres_plugin_build_id(gres_name);
	plugin_context->gres_type = xstrdup_printf("gres/%s", gres_name);
	plugin_context->plugin_list = NULL;
	plugin_context->cur_plugin = PLUGIN_INVALID_HANDLE;

	gres_context_cnt++;
}

/*
 * Initialize the GRES plugins.
 *
 * Returns a Slurm errno.
 */
extern int gres_plugin_init(void)
{
	int i, j, rc = SLURM_SUCCESS;
	char *last = NULL, *names, *one_name, *full_name;
	char *sorted_names = NULL, *sep = "";
	bool append_mps = false;

	if (init_run && (gres_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&gres_context_lock);

	if (gres_context_cnt >= 0)
		goto fini;

	gres_plugin_list = xstrdup(slurm_conf.gres_plugins);
	gres_context_cnt = 0;
	if ((gres_plugin_list == NULL) || (gres_plugin_list[0] == '\0'))
		goto fini;

	/* Ensure that "gres/mps" follows "gres/gpu" */
	have_gpu = false;
	have_mps = false;
	names = xstrdup(gres_plugin_list);
	one_name = strtok_r(names, ",", &last);
	while (one_name) {
		bool skip_name = false;
		if (!xstrcmp(one_name, "mps")) {
			have_mps = true;
			if (!have_gpu) {
				append_mps = true; /* "mps" must follow "gpu" */
				skip_name = true;
			}
			mps_plugin_id = gres_plugin_build_id("mps");
		} else if (!xstrcmp(one_name, "gpu")) {
			have_gpu = true;
			gpu_plugin_id = gres_plugin_build_id("gpu");
		}
		if (!skip_name) {
			xstrfmtcat(sorted_names, "%s%s", sep, one_name);
			sep = ",";
		}
		one_name = strtok_r(NULL, ",", &last);
	}
	if (append_mps) {
		if (!have_gpu)
			fatal("GresTypes: gres/mps requires that gres/gpu also be configured");
		xstrfmtcat(sorted_names, "%s%s", sep, "mps");
	}
	xfree(names);

	gres_context_cnt = 0;
	one_name = strtok_r(sorted_names, ",", &last);
	while (one_name) {
		full_name = xstrdup("gres/");
		xstrcat(full_name, one_name);
		for (i = 0; i < gres_context_cnt; i++) {
			if (!xstrcmp(full_name, gres_context[i].gres_type))
				break;
		}
		xfree(full_name);
		if (i < gres_context_cnt) {
			error("Duplicate plugin %s ignored",
			      gres_context[i].gres_type);
		} else {
			_add_gres_context(one_name);
		}
		one_name = strtok_r(NULL, ",", &last);
	}
	xfree(sorted_names);

	/* Ensure that plugin_id is valid and unique */
	for (i = 0; i < gres_context_cnt; i++) {
		for (j = i + 1; j < gres_context_cnt; j++) {
			if (gres_context[i].plugin_id !=
			    gres_context[j].plugin_id)
				continue;
			fatal("Gres: Duplicate plugin_id %u for %s and %s, "
			      "change gres name for one of them",
			      gres_context[i].plugin_id,
			      gres_context[i].gres_type,
			      gres_context[j].gres_type);
		}
		xassert(gres_context[i].gres_name);

		gres_context[i].gres_name_colon =
			xstrdup_printf("%s:", gres_context[i].gres_name);
		gres_context[i].gres_name_colon_len =
			strlen(gres_context[i].gres_name_colon);
	}

fini:
	if ((select_plugin_type == NO_VAL) &&
	    (select_g_get_info_from_plugin(SELECT_CR_PLUGIN, NULL,
				&select_plugin_type) != SLURM_SUCCESS)) {
		select_plugin_type = NO_VAL;	/* error */
	}
	if (have_mps && running_in_slurmctld() &&
	    (select_plugin_type != SELECT_TYPE_CONS_TRES)) {
		fatal("Use of gres/mps requires the use of select/cons_tres");
	}

	init_run = true;
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

extern int gres_plugin_get_gres_cnt(void)
{
	static int cnt = -1;

	if (cnt != -1)
		return cnt;

	gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	cnt = gres_context_cnt;
	slurm_mutex_unlock(&gres_context_lock);

	return cnt;
}

/*
 * Add a GRES record. This is used by the node_features plugin after the
 * slurm.conf file is read and the initial GRES records are built by
 * gres_plugin_init().
 */
extern void gres_plugin_add(char *gres_name)
{
	int i;

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (!xstrcmp(gres_context[i].gres_name, gres_name))
			goto fini;
	}

	_add_gres_context(gres_name);
fini:	slurm_mutex_unlock(&gres_context_lock);
}

/* Given a gres_name, return its context index or -1 if not found */
static int _gres_name_context(char *gres_name)
{
	int i;

	for (i = 0; i < gres_context_cnt; i++) {
		if (!xstrcmp(gres_context[i].gres_name, gres_name))
			return i;
	}

	return -1;
}

/*
 * Takes a GRES config line (typically from slurm.conf) and remove any
 * records for GRES which are not defined in GresTypes.
 * RET string of valid GRES, Release memory using xfree()
 */
extern char *gres_plugin_name_filter(char *orig_gres, char *nodes)
{
	char *new_gres = NULL, *save_ptr = NULL;
	char *colon, *sep = "", *tmp, *tok, *name;

	slurm_mutex_lock(&gres_context_lock);
	if (!orig_gres || !orig_gres[0] || !gres_context_cnt) {
		slurm_mutex_unlock(&gres_context_lock);
		return new_gres;
	}

	tmp = xstrdup(orig_gres);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		name = xstrdup(tok);
		if ((colon = strchr(name, ':')))
			colon[0] = '\0';
		if (_gres_name_context(name) != -1) {
			xstrfmtcat(new_gres, "%s%s", sep, tok);
			sep = ",";
		} else {
			/* Logging may not be initialized at this point */
			error("Invalid GRES configured on node %s: %s", nodes,
			      tok);
		}
		xfree(name);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	slurm_mutex_unlock(&gres_context_lock);
	xfree(tmp);

	return new_gres;
}

/*
 * Terminate the gres plugin. Free memory.
 *
 * Returns a Slurm errno.
 */
extern int gres_plugin_fini(void)
{
	int i, j, rc = SLURM_SUCCESS;

	slurm_mutex_lock(&gres_context_lock);
	xfree(gres_node_name);
	if (gres_context_cnt < 0)
		goto fini;

	init_run = false;
	for (i = 0; i < gres_context_cnt; i++) {
		j = _unload_gres_plugin(gres_context + i);
		if (j != SLURM_SUCCESS)
			rc = j;
	}
	xfree(gres_context);
	xfree(gres_plugin_list);
	FREE_NULL_LIST(gres_conf_list);
	FREE_NULL_BUFFER(gres_context_buf);
	FREE_NULL_BUFFER(gres_conf_buf);
	gres_context_cnt = -1;

fini:	slurm_mutex_unlock(&gres_context_lock);
	return rc;
}

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Return a plugin-specific help message for salloc, sbatch and srun
 * Result must be xfree()'d.
 *
 * NOTE: GRES "type" (e.g. model) information is only available from slurmctld
 * after slurmd registers. It is not readily available from srun (as used here).
 */
extern char *gres_plugin_help_msg(void)
{
	int i;
	char *msg = xstrdup("Valid gres options are:\n");

	gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		xstrcat(msg, gres_context[i].gres_name);
		xstrcat(msg, "[[:type]:count]\n");
	}
	slurm_mutex_unlock(&gres_context_lock);

	return msg;
}

/*
 * Perform reconfig, re-read any configuration files
 * OUT did_change - set if gres configuration changed
 */
extern int gres_plugin_reconfig(void)
{
	int rc = SLURM_SUCCESS;
	bool plugin_change;

	slurm_mutex_lock(&gres_context_lock);

	if (xstrcmp(slurm_conf.gres_plugins, gres_plugin_list))
		plugin_change = true;
	else
		plugin_change = false;
	slurm_mutex_unlock(&gres_context_lock);

	if (plugin_change) {
		error("GresPlugins changed from %s to %s ignored",
		     gres_plugin_list, slurm_conf.gres_plugins);
		error("Restart the slurmctld daemon to change GresPlugins");
#if 0
		/* This logic would load new plugins, but we need the old
		 * plugins to persist in order to process old state
		 * information. */
		rc = gres_plugin_fini();
		if (rc == SLURM_SUCCESS)
			rc = gres_plugin_init();
#endif
	}

	return rc;
}

/* Return 1 if a gres_conf record is the correct plugin_id and has no file */
static int _find_fileless_gres(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_conf = (gres_slurmd_conf_t *)x;
	uint32_t plugin_id = *(uint32_t *)arg;

	if ((gres_conf->plugin_id == plugin_id) && !gres_conf->file) {
		debug("Removing file-less GPU %s:%s from final GRES list",
		      gres_conf->name, gres_conf->type_name);
		return 1;
	}
	return 0;

}

/*
 * Log the contents of a gres_slurmd_conf_t record
 */
static int _log_gres_slurmd_conf(void *x, void *arg)
{
	gres_slurmd_conf_t *p;
	char *links = NULL;
	int index = -1, offset, mult = 1;

	p = (gres_slurmd_conf_t *) x;
	xassert(p);

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES)) {
		verbose("Gres Name=%s Type=%s Count=%"PRIu64,
			p->name, p->type_name, p->count);
		return 0;
	}

	if (p->file) {
		index = 0;
		offset = strlen(p->file);
		while (offset > 0) {
			offset--;
			if ((p->file[offset] < '0') || (p->file[offset] > '9'))
				break;
			index += (p->file[offset] - '0') * mult;
			mult *= 10;
		}
	}

	if (p->links)
		xstrfmtcat(links, "Links=%s", p->links);
	if (p->cpus && (index != -1)) {
		info("Gres Name=%s Type=%s Count=%"PRIu64" Index=%d ID=%u "
		     "File=%s Cores=%s CoreCnt=%u %s",
		     p->name, p->type_name, p->count, index, p->plugin_id,
		     p->file, p->cpus, p->cpu_cnt, links);
	} else if (index != -1) {
		info("Gres Name=%s Type=%s Count=%"PRIu64" Index=%d ID=%u File=%s %s",
		     p->name, p->type_name, p->count, index, p->plugin_id,
		     p->file, links);
	} else if (p->file) {
		info("Gres Name=%s Type=%s Count=%"PRIu64" ID=%u File=%s %s",
		     p->name, p->type_name, p->count, p->plugin_id, p->file,
		    links);
	} else {
		info("Gres Name=%s Type=%s Count=%"PRIu64" ID=%u %s", p->name,
		     p->type_name, p->count, p->plugin_id, links);
	}
	xfree(links);

	return 0;
}

/* Make sure that specified file name exists, wait up to 20 seconds or generate
 * fatal error and exit. */
static void _my_stat(char *file_name)
{
	struct stat config_stat;
	bool sent_msg = false;
	int i;

	if (!running_in_slurmd_stepd())
		return;

	for (i = 0; i < 20; i++) {
		if (i)
			sleep(1);
		if (stat(file_name, &config_stat) == 0) {
			if (sent_msg)
				info("gres.conf file %s now exists", file_name);
			return;
		}

		if (errno != ENOENT)
			break;

		if (!sent_msg) {
			error("Waiting for gres.conf file %s", file_name);
			sent_msg = true;
		}
	}
	fatal("can't stat gres.conf file %s: %m", file_name);
	return;
}

static int _validate_file(char *filenames, char *gres_name)
{
	char *one_name;
	hostlist_t hl;
	int file_count = 0;

	if (!(hl = hostlist_create(filenames)))
		fatal("can't parse File=%s", filenames);

	while ((one_name = hostlist_shift(hl))) {
		_my_stat(one_name);
		file_count++;
		free(one_name);
	}

	hostlist_destroy(hl);

	return file_count;
}

/*
 * Check that we have a comma-delimited list of numbers
 */
static void _validate_links(gres_slurmd_conf_t *p)
{
	char *tmp, *tok, *save_ptr = NULL, *end_ptr = NULL;
	long int val;

	if (!p->links)
		return;
	if (p->links[0] == '\0') {
		xfree(p->links);
		return;
	}

	tmp = xstrdup(p->links);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		val = strtol(tok, &end_ptr, 10);
		if ((val < -2) || (val > GRES_MAX_LINK) || (val == LONG_MIN) ||
		    (end_ptr[0] != '\0')) {
			error("gres.conf: Ignoring invalid Link (%s) for Name=%s",
			      tok, p->name);
			xfree(p->links);
			break;
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);
}

/*
 * Return true if count can be greater than 1 for a given file.
 * For example, each GPU can have arbitrary count of MPS elements.
 */
static bool _multi_count_per_file(char *name)
{
	if (!xstrcmp(name, "mps"))
		return true;
	return false;
}

static char *_get_autodetect_flags_str(void)
{
	char *flags = NULL;

	if (!(autodetect_flags & GRES_AUTODETECT_GPU_FLAGS))
		xstrfmtcat(flags, "%sunset", flags ? "," : "");
	else {
		if (autodetect_flags & GRES_AUTODETECT_GPU_NVML)
			xstrfmtcat(flags, "%snvml", flags ? "," : "");
		else if (autodetect_flags & GRES_AUTODETECT_GPU_RSMI)
			xstrfmtcat(flags, "%srsmi", flags ? "," : "");
		else if (autodetect_flags & GRES_AUTODETECT_GPU_OFF)
			xstrfmtcat(flags, "%soff", flags ? "," : "");
	}

	return flags;
}

static uint32_t _handle_autodetect_flags(char *str)
{
	uint32_t flags = 0;

	/* Set the node-local gpus value of autodetect_flags */
	if (xstrcasestr(str, "nvml"))
		flags |= GRES_AUTODETECT_GPU_NVML;
	else if (xstrcasestr(str, "rsmi"))
		flags |= GRES_AUTODETECT_GPU_RSMI;
	else if (!xstrcmp(str, "off"))
		flags |= GRES_AUTODETECT_GPU_OFF;

	return flags;
}

static void _handle_local_autodetect(char *str)
{
	uint32_t autodetect_flags_local = _handle_autodetect_flags(str);

	/* Only set autodetect_flags once locally, unless it's the same val */
	if ((autodetect_flags != GRES_AUTODETECT_UNSET) &&
	    (autodetect_flags != autodetect_flags_local)) {
		fatal("gres.conf: duplicate node-local AutoDetect specification does not match the first");
		return;
	}

	/* Set the node-local gpus value of autodetect_flags */
	autodetect_flags |= autodetect_flags_local;

	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES) {
		char *flags = _get_autodetect_flags_str();
		log_flag(GRES, "Using node-local AutoDetect=%s(%d)",
			 flags, autodetect_flags);
		xfree(flags);
	}
}

static void _handle_global_autodetect(char *str)
{
	/* If GPU flags exist, node-local value was already specified */
	if (autodetect_flags & GRES_AUTODETECT_GPU_FLAGS)
		debug2("gres.conf: AutoDetect GPU flags were locally set, so ignoring global flags");
	else
		autodetect_flags |= _handle_autodetect_flags(str);

	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES) {
		char *flags = _get_autodetect_flags_str();
		log_flag(GRES, "Global AutoDetect=%s(%d)",
			 flags, autodetect_flags);
		xfree(flags);
	}
}

/*
 * Build gres_slurmd_conf_t record based upon a line from the gres.conf file
 */
static int _parse_gres_config(void **dest, slurm_parser_enum_t type,
			      const char *key, const char *value,
			      const char *line, char **leftover)
{
	static s_p_options_t _gres_options[] = {
		{"AutoDetect", S_P_STRING},
		{"Count", S_P_STRING},	/* Number of Gres available */
		{"CPUs" , S_P_STRING},	/* CPUs to bind to Gres resource
					 * (deprecated, use Cores) */
		{"Cores", S_P_STRING},	/* Cores to bind to Gres resource */
		{"File",  S_P_STRING},	/* Path to Gres device */
		{"Files", S_P_STRING},	/* Path to Gres device */
		{"Flags", S_P_STRING},	/* GRES Flags */
		{"Link",  S_P_STRING},	/* Communication link IDs */
		{"Links", S_P_STRING},	/* Communication link IDs */
		{"MultipleFiles", S_P_STRING}, /* list of GRES device files */
		{"Name",  S_P_STRING},	/* Gres name */
		{"Type",  S_P_STRING},	/* Gres type (e.g. model name) */
		{NULL}
	};
	int i;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t *p;
	uint64_t tmp_uint64, mult;
	char *tmp_str, *last;
	bool cores_flag = false, cpus_flag = false;
	char *type_str = NULL;
	char *autodetect_string = NULL;
	bool autodetect = false;

	tbl = s_p_hashtbl_create(_gres_options);
	s_p_parse_line(tbl, *leftover, leftover);

	p = xmalloc(sizeof(gres_slurmd_conf_t));

	/*
	 * Detect and set the node-local AutoDetect option only if
	 * NodeName is specified.
	 */
	if (s_p_get_string(&autodetect_string, "AutoDetect", tbl)) {
		if (value)
			error("gres.conf: In-line AutoDetect requires NodeName to take effect");
		else {
			_handle_local_autodetect(autodetect_string);
			/* AutoDetect was specified w/ NodeName */
			autodetect = true;
		}
		xfree(autodetect_string);
	}

	if (!value) {
		if (!s_p_get_string(&p->name, "Name", tbl)) {
			if (!autodetect)
				error("Invalid GRES data, no type name (%s)",
				      line);
			xfree(p);
			s_p_hashtbl_destroy(tbl);
			return 0;
		}
	} else {
		p->name = xstrdup(value);
	}

	p->cpu_cnt = gres_cpu_cnt;
	if (s_p_get_string(&p->cpus, "Cores", tbl)) {
		cores_flag = true;
		type_str = "Cores";
	} else if (s_p_get_string(&p->cpus, "CPUs", tbl)) {
		cpus_flag = true;
		type_str = "CPUs";
	}
	if (cores_flag || cpus_flag) {
		char *local_cpus = NULL;
		if (xcpuinfo_ops.xcpuinfo_abs_to_mac) {
			i = (xcpuinfo_ops.xcpuinfo_abs_to_mac)
				(p->cpus, &local_cpus);
			if (i != SLURM_SUCCESS) {
				error("Invalid GRES data for %s, %s=%s",
				      p->name, type_str, p->cpus);
			}
		} else {
			/*
			 * Not converting Cores into machine format is only for
			 * testing or if we don't care about cpus_bitmap. The
			 * slurmd should always convert to machine format.
			 */
			debug("%s: %s=%s is not being converted to machine-local format",
			      __func__, type_str, p->cpus);
			local_cpus = xstrdup(p->cpus);
			i = SLURM_SUCCESS;
		}
		if (i == SLURM_SUCCESS) {
			p->cpus_bitmap = bit_alloc(gres_cpu_cnt);
			if (!bit_size(p->cpus_bitmap) ||
			    bit_unfmt(p->cpus_bitmap, local_cpus)) {
				fatal("Invalid GRES data for %s, %s=%s (only %u CPUs are available)",
				      p->name, type_str, p->cpus, gres_cpu_cnt);
			}
		}
		xfree(local_cpus);
	}

	if (s_p_get_string(&p->file, "File", tbl) ||
	    s_p_get_string(&p->file, "Files", tbl)) {
		p->count = _validate_file(p->file, p->name);
		p->config_flags |= GRES_CONF_HAS_FILE;
	}

	if (s_p_get_string(&p->file, "MultipleFiles", tbl)) {
		if (p->config_flags & GRES_CONF_HAS_FILE)
			fatal("File and MultipleFiles options are mutually exclusive");
		p->count = 1;
		_validate_file(p->file, p->name);
		p->config_flags |= GRES_CONF_HAS_FILE;
	}

	if (s_p_get_string(&tmp_str, "Flags", tbl)) {
		if (xstrcasestr(tmp_str, "CountOnly"))
			p->config_flags |= GRES_CONF_COUNT_ONLY;
		xfree(tmp_str);
	}

	if (s_p_get_string(&p->links, "Link",  tbl) ||
	    s_p_get_string(&p->links, "Links", tbl)) {
		_validate_links(p);
	}

	if (s_p_get_string(&p->type_name, "Type", tbl)) {
		p->config_flags |= GRES_CONF_HAS_TYPE;
	}

	if (s_p_get_string(&tmp_str, "Count", tbl)) {
		tmp_uint64 = strtoll(tmp_str, &last, 10);
		if ((tmp_uint64 == LONG_MIN) || (tmp_uint64 == LONG_MAX)) {
			fatal("Invalid GRES record for %s, invalid count %s",
			      p->name, tmp_str);
		}
		if ((mult = suffix_mult(last)) != NO_VAL64) {
			tmp_uint64 *= mult;
		} else {
			fatal("Invalid GRES record for %s, invalid count %s",
			      p->name, tmp_str);
		}
		/*
		 * Some GRES can have count > 1 for a given file. For example,
		 * each GPU can have arbitrary count of MPS elements.
		 */
		if (p->count && (p->count != tmp_uint64) &&
		    !_multi_count_per_file(p->name)) {
			fatal("Invalid GRES record for %s, count does not match File value",
			      p->name);
		}
		if (tmp_uint64 >= NO_VAL64) {
			fatal("GRES %s has invalid count value %"PRIu64,
			      p->name, tmp_uint64);
		}
		p->count = tmp_uint64;
		xfree(tmp_str);
	} else if (p->count == 0)
		p->count = 1;

	s_p_hashtbl_destroy(tbl);

	for (i = 0; i < gres_context_cnt; i++) {
		if (xstrcasecmp(p->name, gres_context[i].gres_name) == 0)
			break;
	}
	if (i >= gres_context_cnt) {
		error("Ignoring gres.conf record, invalid name: %s", p->name);
		destroy_gres_slurmd_conf(p);
		return 0;
	}
	p->plugin_id = gres_context[i].plugin_id;
	*dest = (void *)p;
	return 1;
}
static int _parse_gres_config2(void **dest, slurm_parser_enum_t type,
			       const char *key, const char *value,
			       const char *line, char **leftover)
{
	static s_p_options_t _gres_options[] = {
		{"AutoDetect", S_P_STRING},
		{"Count", S_P_STRING},	/* Number of Gres available */
		{"CPUs" , S_P_STRING},	/* CPUs to bind to Gres resource */
		{"Cores", S_P_STRING},	/* Cores to bind to Gres resource */
		{"File",  S_P_STRING},	/* Path to Gres device */
		{"Files",  S_P_STRING},	/* Path to Gres device */
		{"Flags", S_P_STRING},	/* GRES Flags */
		{"Link",  S_P_STRING},	/* Communication link IDs */
		{"Links", S_P_STRING},	/* Communication link IDs */
		{"MultipleFiles", S_P_STRING}, /* list of GRES device files */
		{"Name",  S_P_STRING},	/* Gres name */
		{"Type",  S_P_STRING},	/* Gres type (e.g. model name) */
		{NULL}
	};
	s_p_hashtbl_t *tbl;

	if (gres_node_name && value) {
		bool match = false;
		hostlist_t hl;
		hl = hostlist_create(value);
		if (hl) {
			match = (hostlist_find(hl, gres_node_name) >= 0);
			hostlist_destroy(hl);
		}
		if (!match) {
			debug("skipping GRES for NodeName=%s %s", value, line);
			tbl = s_p_hashtbl_create(_gres_options);
			s_p_parse_line(tbl, *leftover, leftover);
			s_p_hashtbl_destroy(tbl);
			return 0;
		}
	}
	return _parse_gres_config(dest, type, key, NULL, line, leftover);
}

static int _foreach_slurm_conf(void *x, void *arg)
{
	gres_state_t *gres_ptr = (gres_state_t *)x;
	slurm_gres_context_t *context_ptr = (slurm_gres_context_t *)arg;
	gres_node_state_t *slurm_gres;
	uint64_t tmp_count = 0;

	/* Only look at the GRES under the current plugin (same name) */
	if (gres_ptr->plugin_id != context_ptr->plugin_id)
		return 0;

	slurm_gres = (gres_node_state_t *)gres_ptr->gres_data;

	/*
	 * gres_cnt_config should equal the combined count from
	 * type_cnt_avail if there are no untyped GRES
	 */
	for (uint16_t i = 0; i < slurm_gres->type_cnt; i++)
		tmp_count += slurm_gres->type_cnt_avail[i];

	/* Forbid mixing typed and untyped GRES under the same name */
	if (slurm_gres->type_cnt &&
	    slurm_gres->gres_cnt_config > tmp_count)
		fatal("%s: Some %s GRES in slurm.conf have a type while others do not (slurm_gres->gres_cnt_config (%"PRIu64") > tmp_count (%"PRIu64"))",
		      __func__, context_ptr->gres_name,
		      slurm_gres->gres_cnt_config, tmp_count);
	return 1;
}

static void _validate_slurm_conf(List slurm_conf_list,
				 slurm_gres_context_t *context_ptr)
{
	if (!slurm_conf_list)
		return;

	(void)list_for_each_nobreak(slurm_conf_list, _foreach_slurm_conf,
				    context_ptr);
}

static int _foreach_gres_conf(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = (gres_slurmd_conf_t *)x;
	foreach_gres_conf_t *foreach_gres_conf = (foreach_gres_conf_t *)arg;
	slurm_gres_context_t *context_ptr = foreach_gres_conf->context_ptr;
	bool orig_has_file, orig_has_type;

	/* Only look at the GRES under the current plugin (same name) */
	if (gres_slurmd_conf->plugin_id != context_ptr->plugin_id)
		return 0;

	/*
	 * If any plugin of this type has this set it will virally set
	 * any other to be the same as we use the context_ptr from here
	 * on out.
	 */
	if (gres_slurmd_conf->config_flags & GRES_CONF_COUNT_ONLY)
		context_ptr->config_flags |= GRES_CONF_COUNT_ONLY;

	/*
	 * Since there could be multiple types of the same plugin we
	 * need to only make sure we load it once.
	 */
	if (!(context_ptr->config_flags & GRES_CONF_LOADED)) {
		/*
		 * Ignore return code, as we will still support the gres
		 * with or without the plugin.
		 */
		if (_load_gres_plugin(context_ptr) == SLURM_SUCCESS)
			context_ptr->config_flags |= GRES_CONF_LOADED;
	}

	foreach_gres_conf->rec_count++;
	orig_has_file = gres_slurmd_conf->config_flags & GRES_CONF_HAS_FILE;
	if (foreach_gres_conf->new_has_file == -1) {
		if (gres_slurmd_conf->config_flags & GRES_CONF_HAS_FILE)
			foreach_gres_conf->new_has_file = 1;
		else
			foreach_gres_conf->new_has_file = 0;
	} else if ((foreach_gres_conf->new_has_file && !orig_has_file) ||
		   (!foreach_gres_conf->new_has_file && orig_has_file)) {
		fatal("gres.conf for %s, some records have \"File\" specification while others do not",
		      context_ptr->gres_name);
	}
	orig_has_type = gres_slurmd_conf->config_flags &
		GRES_CONF_HAS_TYPE;
	if (foreach_gres_conf->new_has_type == -1) {
		if (gres_slurmd_conf->config_flags &
		    GRES_CONF_HAS_TYPE) {
			foreach_gres_conf->new_has_type = 1;
		} else
			foreach_gres_conf->new_has_type = 0;
	} else if ((foreach_gres_conf->new_has_type && !orig_has_type) ||
		   (!foreach_gres_conf->new_has_type && orig_has_type)) {
		fatal("gres.conf for %s, some records have \"Type=\" specification while others do not",
		      context_ptr->gres_name);
	}

	if (!foreach_gres_conf->new_has_file &&
	    !foreach_gres_conf->new_has_type &&
	    (foreach_gres_conf->rec_count > 1)) {
		fatal("gres.conf duplicate records for %s",
		      context_ptr->gres_name);
	}

	if (foreach_gres_conf->new_has_file)
		context_ptr->config_flags |= GRES_CONF_HAS_FILE;

	return 0;
}

static void _validate_gres_conf(List gres_conf_list,
				slurm_gres_context_t *context_ptr)
{
	foreach_gres_conf_t gres_conf = {
		.context_ptr = context_ptr,
		.new_has_file = -1,
		.new_has_type = -1,
		.rec_count = 0,
	};

	(void)list_for_each_nobreak(gres_conf_list, _foreach_gres_conf,
				    &gres_conf);

	if (!(context_ptr->config_flags & GRES_CONF_LOADED)) {
		/*
		 * This means there was no gre.conf line for this gres found.
		 * We still need to try to load it for AutoDetect's sake.
		 * If we fail loading we will treat it as a count
		 * only GRES since the stepd will try to load it elsewise.
		 */
		if (_load_gres_plugin(context_ptr) != SLURM_SUCCESS)
			context_ptr->config_flags |= GRES_CONF_COUNT_ONLY;
	} else
		/* Remove as this is only really used locally */
		context_ptr->config_flags &= (~GRES_CONF_LOADED);
}

/*
 * Keep track of which gres.conf lines have a count greater than expected
 * according to the current slurm.conf GRES. Modify the count of throw-away
 * records in gres_conf_list_tmp to keep track of this. Any gres.conf records
 * with a count > 0 means that slurm.conf did not account for it completely.
 *
 * gres_conf_list_tmp - (in/out) The temporary gres.conf list.
 * count              - (in) The count of the current slurm.conf GRES record.
 * type_name          - (in) The type of the current slurm.conf GRES record.
 */
static void _compare_conf_counts(List gres_conf_list_tmp, uint64_t count,
				 char *type_name)
{
	gres_slurmd_conf_t *gres_conf;
	ListIterator iter = list_iterator_create(gres_conf_list_tmp);
	while ((gres_conf = list_next(iter))) {
		/* Note: plugin type filter already applied */
		/* Check that type is the same */
		if (xstrcasecmp(gres_conf->type_name, type_name))
			continue;
		/* Keep track of counts */
		if (gres_conf->count > count) {
			gres_conf->count -= count;
			/* This slurm.conf GRES specification is now used up */
			list_iterator_destroy(iter);
			return;
		} else {
			count -= gres_conf->count;
			gres_conf->count = 0;
		}
	}
	list_iterator_destroy(iter);
}

/*
 * Loop through each entry in gres.conf and see if there is a corresponding
 * entry in slurm.conf. If so, see if the counts line up. If there are more
 * devices specified in gres.conf than in slurm.conf, emit errors.
 *
 * slurm_conf_list - (in) The slurm.conf GRES list.
 * gres_conf_list  - (in) The gres.conf GRES list.
 * context_ptr     - (in) Which GRES plugin we are currently working in.
 */
static void _check_conf_mismatch(List slurm_conf_list, List gres_conf_list,
				 slurm_gres_context_t *context_ptr)
{
	ListIterator iter;
	gres_slurmd_conf_t *gres_conf;
	gres_state_t *slurm_conf;
	List gres_conf_list_tmp;

	/* E.g. slurm_conf_list will be NULL in the case of --gpu-bind */
	if (!slurm_conf_list || !gres_conf_list)
		return;

	/*
	 * Duplicate the gres.conf list with records relevant to this GRES plugin
	 * only so we can mangle records. Only add records under the current plugin.
	 */
	gres_conf_list_tmp = list_create(destroy_gres_slurmd_conf);
	iter = list_iterator_create(gres_conf_list);
	while ((gres_conf = list_next(iter))) {
		gres_slurmd_conf_t *gres_conf_tmp;
		if (gres_conf->plugin_id != context_ptr->plugin_id)
			continue;

		gres_conf_tmp = xmalloc(sizeof(*gres_conf_tmp));
		gres_conf_tmp->name = xstrdup(gres_conf->name);
		gres_conf_tmp->type_name = xstrdup(gres_conf->type_name);
		gres_conf_tmp->count = gres_conf->count;
		list_append(gres_conf_list_tmp, gres_conf_tmp);
	}
	list_iterator_destroy(iter);

	/*
	 * Loop through the slurm.conf list and see if there are more gres.conf
	 * GRES than expected.
	 */
	iter = list_iterator_create(slurm_conf_list);
	while ((slurm_conf = list_next(iter))) {
		gres_node_state_t *slurm_gres;

		if (slurm_conf->plugin_id != context_ptr->plugin_id)
			continue;

		/* Determine if typed or untyped, and act accordingly */
		slurm_gres = (gres_node_state_t *)slurm_conf->gres_data;
		if (!slurm_gres->type_name) {
			_compare_conf_counts(gres_conf_list_tmp,
					     slurm_gres->gres_cnt_config, NULL);
			continue;
		}

		for (int i = 0; i < slurm_gres->type_cnt; ++i) {
			_compare_conf_counts(gres_conf_list_tmp,
					     slurm_gres->type_cnt_avail[i],
					     slurm_gres->type_name[i]);
		}
	}
	list_iterator_destroy(iter);

	/*
	 * Loop through gres_conf_list_tmp to print errors for gres.conf
	 * records that were not completely accounted for in slurm.conf.
	 */
	iter = list_iterator_create(gres_conf_list_tmp);
	while ((gres_conf = list_next(iter)))
		if (gres_conf->count > 0)
			info("WARNING: A line in gres.conf for GRES %s%s%s has %"PRIu64" more configured than expected in slurm.conf. Ignoring extra GRES.",
			     gres_conf->name,
			     (gres_conf->type_name) ? ":" : "",
			     (gres_conf->type_name) ? gres_conf->type_name : "",
			     gres_conf->count);
	list_iterator_destroy(iter);

	FREE_NULL_LIST(gres_conf_list_tmp);
}

/*
 * Match the type of a GRES from slurm.conf to a GRES in the gres.conf list. If
 * a match is found, pop it off the gres.conf list and return it.
 *
 * gres_conf_list - (in) The gres.conf list to search through.
 * gres_context   - (in) Which GRES plugin we are currently working in.
 * type_name      - (in) The type of the slurm.conf GRES record. If null, then
 * 			 it's an untyped GRES.
 *
 * Returns the first gres.conf record from gres_conf_list with the same type
 * name as the slurm.conf record.
 */
static gres_slurmd_conf_t *_match_type(List gres_conf_list,
				       slurm_gres_context_t *gres_context,
				       char *type_name)
{
	ListIterator gres_conf_itr;
	gres_slurmd_conf_t *gres_conf = NULL;

	gres_conf_itr = list_iterator_create(gres_conf_list);
	while ((gres_conf = list_next(gres_conf_itr))) {
		if (gres_conf->plugin_id != gres_context->plugin_id)
			continue;

		/*
		 * If type_name is NULL we will take the first matching
		 * gres_conf that we find.  This means we also will remove the
		 * type from the gres_conf to match 18.08 stylings.
		 */
		if (!type_name)
			xfree(gres_conf->type_name);
		else if (xstrcasecmp(gres_conf->type_name, type_name))
			continue;

		/* We found a match, so remove from gres_conf_list and break */
		list_remove(gres_conf_itr);
		break;
	}
	list_iterator_destroy(gres_conf_itr);

	return gres_conf;
}

/*
 * Add a GRES conf record with count == 0 to gres_list.
 *
 * gres_list    - (in/out) The gres list to add to.
 * gres_context - (in) The GRES plugin to add a GRES record for.
 * cpu_cnt      - (in) The cpu count configured for the node.
 */
static void _add_gres_config_empty(List gres_list,
				   slurm_gres_context_t *gres_context,
				   uint32_t cpu_cnt)
{
	gres_slurmd_conf_t *gres_conf = xmalloc(sizeof(*gres_conf));
	gres_conf->cpu_cnt = cpu_cnt;
	gres_conf->name = xstrdup(gres_context->gres_name);
	gres_conf->plugin_id = gres_context->plugin_id;
	list_append(gres_list, gres_conf);
}

/*
 * Truncate the File hostrange string of a GRES record to be to be at most
 * new_count entries. The extra entries will be removed.
 *
 * gres_conf - (in/out) The GRES record to modify.
 * count     - (in) The new number of entries in File
 */
static void _set_file_subset(gres_slurmd_conf_t *gres_conf, uint64_t new_count)
{
	/* Convert file to hostrange */
	hostlist_t hl = hostlist_create(gres_conf->file);
	unsigned long old_count = hostlist_count(hl);

	if (new_count >= old_count) {
		hostlist_destroy(hl);
		/* Nothing to do */
		return;
	}

	/* Remove all but the first entries */
	for (int i = old_count; i > new_count; --i) {
		free(hostlist_pop(hl));
	}

	debug3("%s: Truncating %s:%s File from (%ld) %s", __func__,
	       gres_conf->name, gres_conf->type_name, old_count,
	       gres_conf->file);

	/* Set file to the new subset */
	xfree(gres_conf->file);
	gres_conf->file = hostlist_ranged_string_xmalloc(hl);

	debug3("%s: to (%"PRIu64") %s", __func__, new_count, gres_conf->file);
	hostlist_destroy(hl);
}

/*
 * A continuation of _merge_gres() depending on if the slurm.conf GRES is typed
 * or not.
 *
 * gres_conf_list - (in) The gres.conf list.
 * new_list       - (out) The new merged [slurm|gres].conf list.
 * count          - (in) The count of the slurm.conf GRES record.
 * type_name      - (in) The type of the slurm.conf GRES record, if it exists.
 * gres_context   - (in) Which GRES plugin we are working in.
 * cpu_cnt        - (in) A count of CPUs on the node.
 */
static void _merge_gres2(List gres_conf_list, List new_list, uint64_t count,
			 char *type_name, slurm_gres_context_t *gres_context,
			 uint32_t cpu_count)
{
	gres_slurmd_conf_t *gres_conf, *match;

	/* If slurm.conf count is initially 0, don't waste time on it */
	if (count == 0)
		return;

	/*
	 * There can be multiple gres.conf GRES lines contained within a
	 * single slurm.conf GRES line, due to different values of Cores
	 * and Links. Append them to the list where possible.
	 */
	while ((match = _match_type(gres_conf_list, gres_context, type_name))) {
		list_append(new_list, match);

		debug3("%s: From gres.conf, using %s:%s:%"PRIu64":%s", __func__,
		       match->name, match->type_name, match->count,
		       match->file);

		/* See if we need to merge with any more gres.conf records. */
		if (match->count > count) {
			/*
			 * Truncate excess count of gres.conf to match total
			 * count of slurm.conf.
			 */
			match->count = count;
			/*
			 * Truncate excess file of gres.conf to match total
			 * count of slurm.conf.
			 */
			if (match->file)
				_set_file_subset(match, count);
			/* Floor to 0 to break out of loop. */
			count = 0;
		} else
			/*
			 * Subtract this gres.conf line count from the
			 * slurm.conf total.
			 */
			count -= match->count;

		/*
		 * All devices outlined by this slurm.conf record have now been
		 * merged with gres.conf records and added to new_list, so exit.
		 */
		if (count == 0)
			break;
	}

	if (count == 0)
		return;

	/*
	 * There are leftover GRES specified in this slurm.conf record that are
	 * not accounted for in gres.conf that still need to be added.
	 */
	gres_conf = xmalloc(sizeof(*gres_conf));
	gres_conf->count = count;
	gres_conf->cpu_cnt = cpu_count;
	gres_conf->name = xstrdup(gres_context->gres_name);
	gres_conf->plugin_id = gres_context->plugin_id;
	if (type_name) {
		gres_conf->config_flags = GRES_CONF_HAS_TYPE;
		gres_conf->type_name = xstrdup(type_name);
	}

	if (gres_context->config_flags & GRES_CONF_COUNT_ONLY)
		gres_conf->config_flags |= GRES_CONF_COUNT_ONLY;

	list_append(new_list, gres_conf);
}

/*
 * Merge a single slurm.conf GRES specification with any relevant gres.conf
 * records and append the result to new_list.
 *
 * gres_conf_list - (in) The gres.conf list.
 * new_list       - (out) The new merged [slurm|gres].conf list.
 * ptr            - (in) A slurm.conf GRES record.
 * gres_context   - (in) Which GRES plugin we are working in.
 * cpu_cnt        - (in) A count of CPUs on the node.
 */
static void _merge_gres(List gres_conf_list, List new_list, gres_state_t *ptr,
			slurm_gres_context_t *gres_context, uint32_t cpu_cnt)
{
	gres_node_state_t *slurm_gres = (gres_node_state_t *)ptr->gres_data;

	/* If this GRES has no types, merge in the single untyped GRES */
	if (slurm_gres->type_cnt == 0) {
		_merge_gres2(gres_conf_list, new_list,
			     slurm_gres->gres_cnt_config, NULL, gres_context,
			     cpu_cnt);
		return;
	}

	/* If this GRES has types, merge in each typed GRES */
	for (int i = 0; i < slurm_gres->type_cnt; i++) {
		_merge_gres2(gres_conf_list, new_list,
			     slurm_gres->type_cnt_avail[i],
			     slurm_gres->type_name[i], gres_context, cpu_cnt);
	}
}

/*
 * Merge slurm.conf and gres.conf GRES configuration.
 * gres.conf can only work within what is outlined in slurm.conf. Every
 * gres.conf device that does not match up to a device in slurm.conf is
 * discarded with an error. If no gres conf found for what is specified in
 * slurm.conf, create a zero-count conf record.
 *
 * node_conf       - (in) node configuration info (cpu count).
 * gres_conf_list  - (in/out) GRES data from gres.conf. This becomes the new
 * 		     merged slurm.conf/gres.conf list.
 * slurm_conf_list - (in) GRES data from slurm.conf.
 */
static void _merge_config(node_config_load_t *node_conf, List gres_conf_list,
			  List slurm_conf_list)
{
	int i;
	gres_state_t *gres_ptr;
	ListIterator iter;
	bool found;

	List new_gres_list = list_create(destroy_gres_slurmd_conf);

	for (i = 0; i < gres_context_cnt; i++) {
		/* Copy GRES configuration from slurm.conf */
		if (slurm_conf_list) {
			found = false;
			iter = list_iterator_create(slurm_conf_list);
			while ((gres_ptr = (gres_state_t *) list_next(iter))) {
				if (gres_ptr->plugin_id !=
				    gres_context[i].plugin_id)
					continue;
				found = true;
				_merge_gres(gres_conf_list, new_gres_list,
					    gres_ptr, &gres_context[i],
					    node_conf->cpu_cnt);
			}
			list_iterator_destroy(iter);
			if (found)
				continue;
		}

		/* Add GRES record with zero count */
		_add_gres_config_empty(new_gres_list, &gres_context[i],
				       node_conf->cpu_cnt);
	}
	/* Set gres_conf_list to be the new merged list */
	list_flush(gres_conf_list);
	list_transfer(gres_conf_list, new_gres_list);
	FREE_NULL_LIST(new_gres_list);
}

extern void _pack_gres_context(slurm_gres_context_t *ctx, Buf buffer)
{
	/* ctx->cur_plugin: DON'T PACK will be filled in on the other side */
	pack8(ctx->config_flags, buffer);
	packstr(ctx->gres_name, buffer);
	packstr(ctx->gres_name_colon, buffer);
	pack32((uint32_t)ctx->gres_name_colon_len, buffer);
	packstr(ctx->gres_type, buffer);
	/* ctx->ops: DON'T PACK will be filled in on the other side */
	pack32(ctx->plugin_id, buffer);
	/* ctx->plugin_list: DON'T PACK will be filled in on the other side */
	pack64(ctx->total_cnt, buffer);
}

extern int _unpack_gres_context(slurm_gres_context_t* ctx, Buf buffer)
{
	uint32_t uint32_tmp;

	/* ctx->cur_plugin: filled in later with _load_gres_plugin() */
	safe_unpack8(&ctx->config_flags, buffer);
	safe_unpackstr_xmalloc(&ctx->gres_name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&ctx->gres_name_colon, &uint32_tmp, buffer);
	safe_unpack32(&uint32_tmp, buffer);
	ctx->gres_name_colon_len = (int)uint32_tmp;
	safe_unpackstr_xmalloc(&ctx->gres_type, &uint32_tmp, buffer);
	/* ctx->ops: filled in later with _load_gres_plugin() */
	safe_unpack32(&ctx->plugin_id, buffer);
	/* ctx->plugin_list: filled in later with _load_gres_plugin() */
	safe_unpack64(&ctx->total_cnt, buffer);
	return SLURM_SUCCESS;

unpack_error:
	error("%s: unpack_error", __func__);
	return SLURM_ERROR;
}

static void _pack_gres_slurmd_conf(void *in, uint16_t protocol_version,
				   Buf buffer)
{
	gres_slurmd_conf_t *gres_conf = (gres_slurmd_conf_t *)in;

	/* Pack gres_slurmd_conf_t */
	pack8(gres_conf->config_flags, buffer);
	pack64(gres_conf->count, buffer);
	pack32(gres_conf->cpu_cnt, buffer);
	packstr(gres_conf->cpus, buffer);
	pack_bit_str_hex(gres_conf->cpus_bitmap, buffer);
	packstr(gres_conf->file, buffer);
	packstr(gres_conf->links, buffer);
	packstr(gres_conf->name, buffer);
	packstr(gres_conf->type_name, buffer);
	pack32(gres_conf->plugin_id, buffer);
}

static int _unpack_gres_slurmd_conf(void **object, uint16_t protocol_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	gres_slurmd_conf_t *gres_conf = xmalloc(sizeof(*gres_conf));

	/* Unpack gres_slurmd_conf_t */
	safe_unpack8(&gres_conf->config_flags, buffer);
	safe_unpack64(&gres_conf->count, buffer);
	safe_unpack32(&gres_conf->cpu_cnt, buffer);
	safe_unpackstr_xmalloc(&gres_conf->cpus, &uint32_tmp, buffer);
	unpack_bit_str_hex(&gres_conf->cpus_bitmap, buffer);
	safe_unpackstr_xmalloc(&gres_conf->file, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&gres_conf->links, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&gres_conf->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&gres_conf->type_name, &uint32_tmp, buffer);
	safe_unpack32(&gres_conf->plugin_id, buffer);

	*object = gres_conf;
	return SLURM_SUCCESS;

unpack_error:
	destroy_gres_slurmd_conf(gres_conf);
	*object = NULL;
	return SLURM_ERROR;
}

/* gres_context_lock should be locked before this */
static void _pack_context_buf(void)
{
	FREE_NULL_BUFFER(gres_context_buf);

	gres_context_buf = init_buf(0);
	pack32(gres_context_cnt, gres_context_buf);
	if (gres_context_cnt <= 0) {
		debug3("%s: No GRES context count sent to stepd", __func__);
		return;
	}

	for (int i = 0; i < gres_context_cnt; i++) {
		slurm_gres_context_t *ctx = &gres_context[i];
		_pack_gres_context(ctx, gres_context_buf);
		if (ctx->ops.send_stepd)
			(*(ctx->ops.send_stepd))(gres_context_buf);
	}
}

static int _unpack_context_buf(Buf buffer)
{
	uint32_t cnt;
	safe_unpack32(&cnt, buffer);

	gres_context_cnt = cnt;

	if (!gres_context_cnt)
		return SLURM_SUCCESS;

	xrecalloc(gres_context, gres_context_cnt, sizeof(slurm_gres_context_t));
	for (int i = 0; i < gres_context_cnt; i++) {
		slurm_gres_context_t *ctx = &gres_context[i];
		if (_unpack_gres_context(ctx, buffer) != SLURM_SUCCESS)
			goto unpack_error;
		(void)_load_gres_plugin(ctx);
		if (ctx->ops.recv_stepd)
			(*(ctx->ops.recv_stepd))(buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	error("%s: failed", __func__);
	return SLURM_ERROR;
}

/* gres_context_lock should be locked before this */
static void _pack_gres_conf(void)
{
	int len = 0;
	FREE_NULL_BUFFER(gres_conf_buf);

	gres_conf_buf = init_buf(0);
	pack32(autodetect_flags, gres_conf_buf);

	/* If there is no list to send, let the stepd know */
	if (!gres_conf_list || !(len = list_count(gres_conf_list))) {
		pack32(len, gres_conf_buf);
		return;
	}
	pack32(len, gres_conf_buf);

	if (slurm_pack_list(gres_conf_list, _pack_gres_slurmd_conf,
			    gres_conf_buf, SLURM_PROTOCOL_VERSION)
	    != SLURM_SUCCESS) {
		error("%s: Failed to pack gres_conf_list", __func__);
		return;
	}
}

static int _unpack_gres_conf(Buf buffer)
{
	uint32_t cnt;
	safe_unpack32(&cnt, buffer);
	autodetect_flags = cnt;
	safe_unpack32(&cnt, buffer);

	if (!cnt)
		return SLURM_SUCCESS;

	if (slurm_unpack_list(&gres_conf_list, _unpack_gres_slurmd_conf,
			      destroy_gres_slurmd_conf, buffer,
			      SLURM_PROTOCOL_VERSION) != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	error("%s: failed", __func__);
	return SLURM_ERROR;
}

/*
 * Load this node's configuration (how many resources it has, topology, etc.)
 * IN cpu_cnt - Number of CPUs configured on this node
 * IN node_name - Name of this node
 * IN gres_list - Node's GRES information as loaded from slurm.conf by slurmd
 * IN xcpuinfo_abs_to_mac - Pointer to xcpuinfo_abs_to_mac() funct. If
 * 	specified, Slurm will convert gres_slurmd_conf->cpus_bitmap (a bitmap
 * 	derived from gres.conf's "Cores" range string) into machine format
 * 	(normal slrumd/stepd operation). If not, it will remain unconverted (for
 * 	testing purposes or when unused).
 * IN xcpuinfo_mac_to_abs - Pointer to xcpuinfo_mac_to_abs() funct. Used to
 * 	convert CPU affinities from machine format (as collected from NVML and
 * 	others) into abstract format, for sanity checking purposes.
 * NOTE: Called from slurmd and slurmstepd
 */
extern int gres_plugin_node_config_load(uint32_t cpu_cnt, char *node_name,
					List gres_list,
					void *xcpuinfo_abs_to_mac,
					void *xcpuinfo_mac_to_abs)
{
	static s_p_options_t _gres_options[] = {
		{"AutoDetect", S_P_STRING},
		{"Name",     S_P_ARRAY, _parse_gres_config,  NULL},
		{"NodeName", S_P_ARRAY, _parse_gres_config2, NULL},
		{NULL}
	};

	int count = 0, i, rc, rc2;
	struct stat config_stat;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t **gres_array;
	char *gres_conf_file;
	char *autodetect_string = NULL;

	node_config_load_t node_conf = {
		.cpu_cnt = cpu_cnt,
		.xcpuinfo_mac_to_abs = xcpuinfo_mac_to_abs
	};

	if (cpu_cnt == 0) {
		error("%s: Invalid cpu_cnt of 0 for node %s",
		      __func__, node_name);
		return ESLURM_INVALID_CPU_COUNT;
	}

	if (xcpuinfo_abs_to_mac)
		xcpuinfo_ops.xcpuinfo_abs_to_mac = xcpuinfo_abs_to_mac;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);

	if (gres_context_cnt == 0) {
		rc = SLURM_SUCCESS;
		goto fini;
	}

	FREE_NULL_LIST(gres_conf_list);
	gres_conf_list = list_create(destroy_gres_slurmd_conf);
	gres_conf_file = get_extra_conf_path("gres.conf");
	if (stat(gres_conf_file, &config_stat) < 0) {
		info("Can not stat gres.conf file (%s), using slurm.conf data",
		      gres_conf_file);
	} else {
		if (xstrcmp(gres_node_name, node_name)) {
			xfree(gres_node_name);
			gres_node_name = xstrdup(node_name);
		}

		gres_cpu_cnt = cpu_cnt;
		tbl = s_p_hashtbl_create(_gres_options);
		if (s_p_parse_file(tbl, NULL, gres_conf_file, false) == SLURM_ERROR)
			fatal("error opening/reading %s", gres_conf_file);

		/* Overwrite unspecified local AutoDetect with global default */
		if (s_p_get_string(&autodetect_string, "Autodetect", tbl)) {
			_handle_global_autodetect(autodetect_string);
			xfree(autodetect_string);
		}

		if (s_p_get_array((void ***) &gres_array, &count, "Name", tbl)) {
			for (i = 0; i < count; i++) {
				list_append(gres_conf_list, gres_array[i]);
				gres_array[i] = NULL;
			}
		}
		if (s_p_get_array((void ***) &gres_array, &count, "NodeName", tbl)) {
			for (i = 0; i < count; i++) {
				list_append(gres_conf_list, gres_array[i]);
				gres_array[i] = NULL;
			}
		}
		s_p_hashtbl_destroy(tbl);
	}
	xfree(gres_conf_file);

	/* Validate gres.conf and slurm.conf somewhat before merging */
	for (i = 0; i < gres_context_cnt; i++) {
		_validate_slurm_conf(gres_list, &gres_context[i]);
		_validate_gres_conf(gres_conf_list, &gres_context[i]);
		_check_conf_mismatch(gres_list, gres_conf_list,
				     &gres_context[i]);
	}

	/* Merge slurm.conf and gres.conf together into gres_conf_list */
	_merge_config(&node_conf, gres_conf_list, gres_list);

	for (i = 0; i < gres_context_cnt; i++) {
		if (gres_context[i].ops.node_config_load == NULL)
			continue;	/* No plugin */
		rc2 = (*(gres_context[i].ops.node_config_load))(gres_conf_list,
								&node_conf);
		if (rc == SLURM_SUCCESS)
			rc = rc2;

	}

	/* Postprocess gres_conf_list after all plugins' node_config_load */

	/* Remove every GPU with an empty File */
	(void) list_delete_all(gres_conf_list, _find_fileless_gres,
			       &gpu_plugin_id);

	list_for_each(gres_conf_list, _log_gres_slurmd_conf, NULL);

fini:
	_pack_context_buf();
	_pack_gres_conf();
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Pack this node's gres configuration into a buffer
 * IN/OUT buffer - message buffer to pack
 */
extern int gres_plugin_node_config_pack(Buf buffer)
{
	int rc;
	uint32_t magic = GRES_MAGIC;
	uint16_t rec_cnt = 0, version = SLURM_PROTOCOL_VERSION;
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	pack16(version, buffer);
	if (gres_conf_list)
		rec_cnt = list_count(gres_conf_list);
	pack16(rec_cnt, buffer);
	if (rec_cnt) {
		iter = list_iterator_create(gres_conf_list);
		while ((gres_slurmd_conf =
			(gres_slurmd_conf_t *) list_next(iter))) {
			pack32(magic, buffer);
			pack64(gres_slurmd_conf->count, buffer);
			pack32(gres_slurmd_conf->cpu_cnt, buffer);
			pack8(gres_slurmd_conf->config_flags, buffer);
			pack32(gres_slurmd_conf->plugin_id, buffer);
			packstr(gres_slurmd_conf->cpus, buffer);
			packstr(gres_slurmd_conf->links, buffer);
			packstr(gres_slurmd_conf->name, buffer);
			packstr(gres_slurmd_conf->type_name, buffer);
		}
		list_iterator_destroy(iter);
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Unpack this node's configuration from a buffer (built/packed by slurmd)
 * IN/OUT buffer - message buffer to unpack
 * IN node_name - name of node whose data is being unpacked
 */
extern int gres_plugin_node_config_unpack(Buf buffer, char *node_name)
{
	int i, j, rc;
	uint32_t cpu_cnt = 0, magic = 0, plugin_id = 0, utmp32 = 0;
	uint64_t count64 = 0;
	uint16_t rec_cnt = 0, protocol_version = 0;
	uint8_t config_flags = 0;
	char *tmp_cpus = NULL, *tmp_links = NULL, *tmp_name = NULL;
	char *tmp_type = NULL;
	gres_slurmd_conf_t *p;

	rc = gres_plugin_init();

	FREE_NULL_LIST(gres_conf_list);
	gres_conf_list = list_create(destroy_gres_slurmd_conf);

	safe_unpack16(&protocol_version, buffer);

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;
	if (rec_cnt > NO_VAL16)
		goto unpack_error;

	slurm_mutex_lock(&gres_context_lock);
	if (protocol_version < SLURM_MIN_PROTOCOL_VERSION) {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}
	for (i = 0; i < rec_cnt; i++) {
		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;

			safe_unpack64(&count64, buffer);
			safe_unpack32(&cpu_cnt, buffer);
			safe_unpack8(&config_flags, buffer);
			safe_unpack32(&plugin_id, buffer);
			safe_unpackstr_xmalloc(&tmp_cpus, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_links, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_name, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_type, &utmp32, buffer);
		}

		log_flag(GRES, "Node:%s Gres:%s Type:%s Flags:%s CPU_IDs:%s CPU#:%u Count:%"PRIu64" Links:%s",
			 node_name, tmp_name, tmp_type,
			 gres_flags2str(config_flags), tmp_cpus, cpu_cnt,
			 count64, tmp_links);

	 	for (j = 0; j < gres_context_cnt; j++) {
			bool new_has_file,  new_has_type;
			bool orig_has_file, orig_has_type;
	 		if (gres_context[j].plugin_id != plugin_id)
				continue;
			if (xstrcmp(gres_context[j].gres_name, tmp_name)) {
				/*
				 * Should have been caught in
				 * gres_plugin_init()
				 */
				error("%s: gres/%s duplicate plugin ID with %s, unable to process",
				      __func__, tmp_name,
				      gres_context[j].gres_name);
				continue;
			}
			new_has_file  = config_flags & GRES_CONF_HAS_FILE;
			orig_has_file = gres_context[j].config_flags &
					GRES_CONF_HAS_FILE;
			if (orig_has_file && !new_has_file && count64) {
				error("%s: gres/%s lacks \"File=\" parameter for node %s",
				      __func__, tmp_name, node_name);
				config_flags |= GRES_CONF_HAS_FILE;
			}
			if (new_has_file && (count64 > MAX_GRES_BITMAP)) {
				/*
				 * Avoid over-subscribing memory with
				 * huge bitmaps
				 */
				error("%s: gres/%s has \"File=\" plus very large "
				      "\"Count\" (%"PRIu64") for node %s, "
				      "resetting value to %d",
				      __func__, tmp_name, count64,
				      node_name, MAX_GRES_BITMAP);
				count64 = MAX_GRES_BITMAP;
			}
			new_has_type  = config_flags & GRES_CONF_HAS_TYPE;
			orig_has_type = gres_context[j].config_flags &
					GRES_CONF_HAS_TYPE;
			if (orig_has_type && !new_has_type && count64) {
				error("%s: gres/%s lacks \"Type\" parameter for node %s",
				      __func__, tmp_name, node_name);
				config_flags |= GRES_CONF_HAS_TYPE;
			}
			gres_context[j].config_flags |= config_flags;

			/*
			 * On the slurmctld we need to load the plugins to
			 * correctly set env vars.  We want to call this only
			 * after we have the config_flags so we can tell if we
			 * are CountOnly or not.
			 */
			if (!(gres_context[j].config_flags &
			      GRES_CONF_LOADED)) {
				(void)_load_gres_plugin(&gres_context[j]);
				gres_context[j].config_flags |=
					GRES_CONF_LOADED;
			}

			break;
	 	}
		if (j >= gres_context_cnt) {
			/*
			 * GresPlugins is inconsistently configured.
			 * Not a fatal error, but skip this data.
			 */
			error("%s: No plugin configured to process GRES data from node %s (Name:%s Type:%s PluginID:%u Count:%"PRIu64")",
			      __func__, node_name, tmp_name, tmp_type,
			      plugin_id, count64);
			xfree(tmp_cpus);
			xfree(tmp_links);
			xfree(tmp_name);
			xfree(tmp_type);
			continue;
		}
		p = xmalloc(sizeof(gres_slurmd_conf_t));
		p->config_flags = config_flags;
		p->count = count64;
		p->cpu_cnt = cpu_cnt;
		p->cpus = tmp_cpus;
		tmp_cpus = NULL;	/* Nothing left to xfree */
		p->links = tmp_links;
		tmp_links = NULL;	/* Nothing left to xfree */
		p->name = tmp_name;     /* Preserve for accounting! */
		p->type_name = tmp_type;
		tmp_type = NULL;	/* Nothing left to xfree */
		p->plugin_id = plugin_id;
		_validate_links(p);
		list_append(gres_conf_list, p);
	}

	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error from node %s", __func__, node_name);
	xfree(tmp_cpus);
	xfree(tmp_links);
	xfree(tmp_name);
	xfree(tmp_type);
	slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

static void _gres_node_state_delete_topo(gres_node_state_t *gres_node_ptr)
{
	int i;

	for (i = 0; i < gres_node_ptr->topo_cnt; i++) {
		if (gres_node_ptr->topo_gres_bitmap)
			FREE_NULL_BITMAP(gres_node_ptr->topo_gres_bitmap[i]);
		if (gres_node_ptr->topo_core_bitmap)
			FREE_NULL_BITMAP(gres_node_ptr->topo_core_bitmap[i]);
		xfree(gres_node_ptr->topo_type_name[i]);
	}
	xfree(gres_node_ptr->topo_gres_bitmap);
	xfree(gres_node_ptr->topo_core_bitmap);
	xfree(gres_node_ptr->topo_gres_cnt_alloc);
	xfree(gres_node_ptr->topo_gres_cnt_avail);
	xfree(gres_node_ptr->topo_type_id);
	xfree(gres_node_ptr->topo_type_name);
}

static void _gres_node_state_delete(gres_node_state_t *gres_node_ptr)
{
	int i;

	FREE_NULL_BITMAP(gres_node_ptr->gres_bit_alloc);
	xfree(gres_node_ptr->gres_used);
	if (gres_node_ptr->links_cnt) {
		for (i = 0; i < gres_node_ptr->link_len; i++)
			xfree(gres_node_ptr->links_cnt[i]);
		xfree(gres_node_ptr->links_cnt);
	}

	_gres_node_state_delete_topo(gres_node_ptr);

	for (i = 0; i < gres_node_ptr->type_cnt; i++) {
		xfree(gres_node_ptr->type_name[i]);
	}
	xfree(gres_node_ptr->type_cnt_alloc);
	xfree(gres_node_ptr->type_cnt_avail);
	xfree(gres_node_ptr->type_id);
	xfree(gres_node_ptr->type_name);
	xfree(gres_node_ptr);
}

/*
 * Delete an element placed on gres_list by _node_config_validate()
 * free associated memory
 */
static void _gres_node_list_delete(void *list_element)
{
	gres_state_t *gres_ptr;
	gres_node_state_t *gres_node_ptr;

	gres_ptr = (gres_state_t *) list_element;
	gres_node_ptr = (gres_node_state_t *) gres_ptr->gres_data;
	_gres_node_state_delete(gres_node_ptr);
	xfree(gres_ptr);
}

static void _add_gres_type(char *type, gres_node_state_t *gres_data,
			   uint64_t tmp_gres_cnt)
{
	int i;
	uint32_t type_id;

	if (!xstrcasecmp(type, "no_consume")) {
		gres_data->no_consume = true;
		return;
	}

	type_id = gres_plugin_build_id(type);
	for (i = 0; i < gres_data->type_cnt; i++) {
		if (gres_data->type_id[i] != type_id)
			continue;
		gres_data->type_cnt_avail[i] += tmp_gres_cnt;
		break;
	}

	if (i >= gres_data->type_cnt) {
		gres_data->type_cnt++;
		gres_data->type_cnt_alloc =
			xrealloc(gres_data->type_cnt_alloc,
				 sizeof(uint64_t) * gres_data->type_cnt);
		gres_data->type_cnt_avail =
			xrealloc(gres_data->type_cnt_avail,
				 sizeof(uint64_t) * gres_data->type_cnt);
		gres_data->type_id =
			xrealloc(gres_data->type_id,
				 sizeof(uint32_t) * gres_data->type_cnt);
		gres_data->type_name =
			xrealloc(gres_data->type_name,
				 sizeof(char *) * gres_data->type_cnt);
		gres_data->type_cnt_avail[i] += tmp_gres_cnt;
		gres_data->type_id[i] = type_id;
		gres_data->type_name[i] = xstrdup(type);
	}
}

/*
 * Compute the total GRES count for a particular gres_name.
 * Note that a given gres_name can appear multiple times in the orig_config
 * string for multiple types (e.g. "gres=gpu:kepler:1,gpu:tesla:2").
 * IN/OUT gres_data - set gres_cnt_config field in this structure
 * IN orig_config - gres configuration from slurm.conf
 * IN gres_name - name of the gres type (e.g. "gpu")
 * IN gres_name_colon - gres name with appended colon
 * IN gres_name_colon_len - size of gres_name_colon
 * RET - Total configured count for this GRES type
 */
static void _get_gres_cnt(gres_node_state_t *gres_data, char *orig_config,
			  char *gres_name, char *gres_name_colon,
			  int gres_name_colon_len)
{
	char *node_gres_config, *tok, *last_tok = NULL;
	char *sub_tok, *last_sub_tok = NULL;
	char *num, *paren, *last_num = NULL;
	uint64_t gres_config_cnt = 0, tmp_gres_cnt = 0, mult;
	int i;

	xassert(gres_data);
	if (orig_config == NULL) {
		gres_data->gres_cnt_config = 0;
		return;
	}

	for (i = 0; i < gres_data->type_cnt; i++) {
		gres_data->type_cnt_avail[i] = 0;
	}

	node_gres_config = xstrdup(orig_config);
	tok = strtok_r(node_gres_config, ",", &last_tok);
	while (tok) {
		if (!xstrcmp(tok, gres_name)) {
			gres_config_cnt = 1;
			break;
		}
		if (!xstrncmp(tok, gres_name_colon, gres_name_colon_len)) {
			paren = strrchr(tok, '(');
			if (paren)	/* Ignore socket binding info */
				paren[0] = '\0';
			num = strrchr(tok, ':');
			if (!num) {
				error("Bad GRES configuration: %s", tok);
				break;
			}
			tmp_gres_cnt = strtoll(num + 1, &last_num, 10);
			if ((num[1] < '0') || (num[1] > '9')) {
				/*
				 * Type name, no count (e.g. "gpu:tesla").
				 * assume count of 1.
				 */
				tmp_gres_cnt = 1;
			} else if ((mult = suffix_mult(last_num)) != NO_VAL64) {
				tmp_gres_cnt *= mult;
				num[0] = '\0';
			} else {
				error("Bad GRES configuration: %s", tok);
				break;
			}

			gres_config_cnt += tmp_gres_cnt;

			sub_tok = strtok_r(tok, ":", &last_sub_tok);
			if (sub_tok)	/* Skip GRES name */
				sub_tok = strtok_r(NULL, ":", &last_sub_tok);
			while (sub_tok) {
				_add_gres_type(sub_tok, gres_data,
					       tmp_gres_cnt);
				sub_tok = strtok_r(NULL, ":", &last_sub_tok);
			}
		}
		tok = strtok_r(NULL, ",", &last_tok);
	}
	xfree(node_gres_config);

	gres_data->gres_cnt_config = gres_config_cnt;
}

static int _valid_gres_type(char *gres_name, gres_node_state_t *gres_data,
			    bool config_overrides, char **reason_down)
{
	int i, j;
	uint64_t model_cnt;

	if (gres_data->type_cnt == 0)
		return 0;

	for (i = 0; i < gres_data->type_cnt; i++) {
		model_cnt = 0;
		if (gres_data->type_cnt) {
			for (j = 0; j < gres_data->type_cnt; j++) {
				if (gres_data->type_id[i] ==
				    gres_data->type_id[j])
					model_cnt +=
						gres_data->type_cnt_avail[j];
			}
		} else {
			for (j = 0; j < gres_data->topo_cnt; j++) {
				if (gres_data->type_id[i] ==
				    gres_data->topo_type_id[j])
					model_cnt +=
					      gres_data->topo_gres_cnt_avail[j];
			}
		}
		if (config_overrides) {
			gres_data->type_cnt_avail[i] = model_cnt;
		} else if (model_cnt < gres_data->type_cnt_avail[i]) {
			if (reason_down) {
				xstrfmtcat(*reason_down,
					   "%s:%s count too low "
					   "(%"PRIu64" < %"PRIu64")",
					   gres_name, gres_data->type_name[i],
					   model_cnt,
					   gres_data->type_cnt_avail[i]);
			}
			return -1;
		}
	}
	return 0;
}

static gres_node_state_t *_build_gres_node_state(void)
{
	gres_node_state_t *gres_data;

	gres_data = xmalloc(sizeof(gres_node_state_t));
	gres_data->gres_cnt_config = NO_VAL64;
	gres_data->gres_cnt_found  = NO_VAL64;

	return gres_data;
}

/*
 * Build a node's gres record based only upon the slurm.conf contents
 */
static int _node_config_init(char *node_name, char *orig_config,
			     slurm_gres_context_t *context_ptr,
			     gres_state_t *gres_ptr)
{
	int rc = SLURM_SUCCESS;
	gres_node_state_t *gres_data;

	if (!gres_ptr->gres_data)
		gres_ptr->gres_data = _build_gres_node_state();
	gres_data = (gres_node_state_t *) gres_ptr->gres_data;

	/* If the resource isn't configured for use with this node */
	if ((orig_config == NULL) || (orig_config[0] == '\0')) {
		gres_data->gres_cnt_config = 0;
		return rc;
	}

	_get_gres_cnt(gres_data, orig_config,
		      context_ptr->gres_name,
		      context_ptr->gres_name_colon,
		      context_ptr->gres_name_colon_len);

	context_ptr->total_cnt += gres_data->gres_cnt_config;

	/* Use count from recovered state, if higher */
	gres_data->gres_cnt_avail = MAX(gres_data->gres_cnt_avail,
					gres_data->gres_cnt_config);
	if ((gres_data->gres_bit_alloc != NULL) &&
	    (gres_data->gres_cnt_avail >
	     bit_size(gres_data->gres_bit_alloc)) &&
	    !_shared_gres(context_ptr->plugin_id)) {
		gres_data->gres_bit_alloc =
			bit_realloc(gres_data->gres_bit_alloc,
				    gres_data->gres_cnt_avail);
	}

	return rc;
}

/*
 * Build a node's gres record based only upon the slurm.conf contents
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT gres_list - List of Gres records for this node to track usage
 */
extern int gres_plugin_init_node_config(char *node_name, char *orig_config,
					List *gres_list)
{
	int i, rc, rc2;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_node_list_delete);
	}
	for (i = 0; i < gres_context_cnt; i++) {
		/* Find or create gres_state entry on the list */
		gres_iter = list_iterator_create(*gres_list);
		while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
			if (gres_ptr->plugin_id == gres_context[i].plugin_id)
				break;
		}
		list_iterator_destroy(gres_iter);
		if (gres_ptr == NULL) {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = gres_context[i].plugin_id;
			list_append(*gres_list, gres_ptr);
		}

		rc2 = _node_config_init(node_name, orig_config,
					&gres_context[i], gres_ptr);
		if (rc == SLURM_SUCCESS)
			rc = rc2;
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Determine GRES availability on some node
 * plugin_id IN - plugin number to search for
 * topo_cnt OUT - count of gres.conf records of this ID found by slurmd
 *		  (each can have different topology)
 * config_type_cnt OUT - Count of records for this GRES found in configuration,
 *		  each of this represesents a different Type of of GRES with
 *		  with this name (e.g. GPU model)
 * RET - total number of GRES available of this ID on this node in (sum
 *	 across all records of this ID)
 */
static uint64_t _get_tot_gres_cnt(uint32_t plugin_id, uint64_t *topo_cnt,
				  int *config_type_cnt)
{
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	uint32_t cpu_set_cnt = 0, rec_cnt = 0;
	uint64_t gres_cnt = 0;

	xassert(config_type_cnt);
	xassert(topo_cnt);
	*config_type_cnt = 0;
	*topo_cnt = 0;
	if (gres_conf_list == NULL)
		return gres_cnt;

	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = (gres_slurmd_conf_t *) list_next(iter))) {
		if (gres_slurmd_conf->plugin_id != plugin_id)
			continue;
		gres_cnt += gres_slurmd_conf->count;
		rec_cnt++;
		if (gres_slurmd_conf->cpus || gres_slurmd_conf->type_name)
			cpu_set_cnt++;
	}
	list_iterator_destroy(iter);
	*config_type_cnt = rec_cnt;
	if (cpu_set_cnt)
		*topo_cnt = rec_cnt;
	return gres_cnt;
}

/*
 * Map a given GRES type ID back to a GRES type name.
 * gres_id IN - GRES type ID to search for.
 * gres_name IN - Pre-allocated string in which to store the GRES type name.
 * gres_name_len - Size of gres_name in bytes
 * RET - error code (currently not used--always return SLURM_SUCCESS)
 */
extern int gres_gresid_to_gresname(uint32_t gres_id, char* gres_name,
				   int gres_name_len)
{
	int rc = SLURM_SUCCESS;
	int      found = 0;
	int i;

	/*
	 * Check GresTypes from slurm.conf (gres_context) for GRES type name
	 */
	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; ++i) {
		if (gres_id == gres_context[i].plugin_id) {
			strlcpy(gres_name, gres_context[i].gres_name,
				gres_name_len);
			found = 1;
			break;
		}
	}
	slurm_mutex_unlock(&gres_context_lock);

	/*
	 * If can't find GRES type name, emit error and default to GRES type ID
	 */
	if (!found) {
		error("Could not find GRES type name in slurm.conf that corresponds to GRES type ID `%d`.  Using ID as GRES type name instead.",
		      gres_id);
		snprintf(gres_name, gres_name_len, "%u", gres_id);
	}

	return rc;
}

/* Convert comma-delimited array of link counts to an integer array */
static void _links_str2array(char *links, char *node_name,
			     gres_node_state_t *gres_data,
			     int gres_inx, int gres_cnt)
{
	char *start_ptr, *end_ptr = NULL;
	int i = 0;

	if (!links)	/* No "Links=" data */
		return;
	if (gres_inx >= gres_data->link_len) {
		error("%s: Invalid GRES index (%d >= %d)", __func__, gres_inx,
		      gres_cnt);
		return;
	}

	start_ptr = links;
	while (1) {
		gres_data->links_cnt[gres_inx][i] =
			strtol(start_ptr, &end_ptr, 10);
		if (gres_data->links_cnt[gres_inx][i] < -2) {
			error("%s: Invalid GRES Links value (%s) on node %s:"
			      "Link value '%d' < -2", __func__, links,
			      node_name, gres_data->links_cnt[gres_inx][i]);
			gres_data->links_cnt[gres_inx][i] = 0;
			return;
		}
		if (end_ptr[0] == '\0')
			return;
		if (end_ptr[0] != ',') {
			error("%s: Invalid GRES Links value (%s) on node %s:"
			      "end_ptr[0]='%c' != ','", __func__, links,
			      node_name, end_ptr[0]);
			return;
		}
		if (++i >= gres_data->link_len) {
			error("%s: Invalid GRES Links value (%s) on node %s:"
			      "i=%d >= link_len=%d", __func__, links, node_name,
			      i, gres_data->link_len);
			return;
		}
		start_ptr = end_ptr + 1;
	}
}

static bool _valid_gres_types(char *gres_name, gres_node_state_t *gres_data,
			      char **reason_down)
{
	bool rc = true;
	uint64_t gres_cnt_found = 0, gres_sum;
	int topo_inx, type_inx;

	if ((gres_data->type_cnt == 0) || (gres_data->topo_cnt == 0))
		return rc;

	for (type_inx = 0; type_inx < gres_data->type_cnt; type_inx++) {
		gres_cnt_found = 0;
		for (topo_inx = 0; topo_inx < gres_data->topo_cnt; topo_inx++) {
			if (gres_data->topo_type_id[topo_inx] !=
			    gres_data->type_id[type_inx])
				continue;
			gres_sum = gres_cnt_found +
				   gres_data->topo_gres_cnt_avail[topo_inx];
			if (gres_sum > gres_data->type_cnt_avail[type_inx]) {
				gres_data->topo_gres_cnt_avail[topo_inx] -=
					(gres_sum -
					 gres_data->type_cnt_avail[type_inx]);
			}
			gres_cnt_found +=
				gres_data->topo_gres_cnt_avail[topo_inx];
		}
		if (gres_cnt_found < gres_data->type_cnt_avail[type_inx]) {
			rc = false;
			break;
		}
	}
	if (!rc && reason_down && (*reason_down == NULL)) {
		xstrfmtcat(*reason_down,
			   "%s:%s count too low (%"PRIu64" < %"PRIu64")",
			   gres_name, gres_data->type_name[type_inx],
			   gres_cnt_found, gres_data->type_cnt_avail[type_inx]);
	}

	return rc;
}

static void _gres_bit_alloc_resize(gres_node_state_t *gres_data,
				   uint64_t gres_bits)
{
	if (!gres_bits) {
		FREE_NULL_BITMAP(gres_data->gres_bit_alloc);
		return;
	}

	if (!gres_data->gres_bit_alloc)
		gres_data->gres_bit_alloc = bit_alloc(gres_bits);
	else if (gres_bits != bit_size(gres_data->gres_bit_alloc))
		gres_data->gres_bit_alloc =
			bit_realloc(gres_data->gres_bit_alloc, gres_bits);
}

static int _node_config_validate(char *node_name, char *orig_config,
				 gres_state_t *gres_ptr,
				 int cpu_cnt, int core_cnt, int sock_cnt,
				 bool config_overrides, char **reason_down,
				 slurm_gres_context_t *context_ptr)
{
	int cpus_config = 0, i, j, gres_inx, rc = SLURM_SUCCESS;
	int config_type_cnt = 0;
	uint64_t dev_cnt, gres_cnt, topo_cnt = 0;
	bool cpu_config_err = false, updated_config = false;
	gres_node_state_t *gres_data;
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	bool has_file, has_type, rebuild_topo = false;
	uint32_t type_id;

	xassert(core_cnt);
	if (gres_ptr->gres_data == NULL)
		gres_ptr->gres_data = _build_gres_node_state();
	gres_data = (gres_node_state_t *) gres_ptr->gres_data;
	if (gres_data->node_feature)
		return rc;

	gres_cnt = _get_tot_gres_cnt(context_ptr->plugin_id, &topo_cnt,
				     &config_type_cnt);
	if ((gres_data->gres_cnt_config > gres_cnt) && !config_overrides) {
		if (reason_down && (*reason_down == NULL)) {
			xstrfmtcat(*reason_down,
				   "%s count reported lower than configured "
				   "(%"PRIu64" < %"PRIu64")",
				   context_ptr->gres_type,
				   gres_cnt, gres_data->gres_cnt_config);
		}
		rc = EINVAL;
	}
	if ((gres_cnt > gres_data->gres_cnt_config)) {
		debug("%s: %s: Ignoring excess count on node %s (%"
		      PRIu64" > %"PRIu64")",
		      __func__, context_ptr->gres_type, node_name, gres_cnt,
		      gres_data->gres_cnt_config);
		gres_cnt = gres_data->gres_cnt_config;
	}
	if (gres_data->gres_cnt_found != gres_cnt) {
		if (gres_data->gres_cnt_found != NO_VAL64) {
			info("%s: %s: Count changed on node %s (%"PRIu64" != %"PRIu64")",
			     __func__, context_ptr->gres_type, node_name,
			     gres_data->gres_cnt_found, gres_cnt);
		}
		if ((gres_data->gres_cnt_found != NO_VAL64) &&
		    (gres_data->gres_cnt_alloc != 0)) {
			if (reason_down && (*reason_down == NULL)) {
				xstrfmtcat(*reason_down,
					   "%s count changed and jobs are using them "
					   "(%"PRIu64" != %"PRIu64")",
					   context_ptr->gres_type,
					   gres_data->gres_cnt_found, gres_cnt);
			}
			rc = EINVAL;
		} else {
			gres_data->gres_cnt_found = gres_cnt;
			updated_config = true;
		}
	}
	if (!updated_config && gres_data->type_cnt) {
		/*
		 * This is needed to address the GRES specification in
		 * gres.conf having a Type option, while the GRES specification
		 * in slurm.conf does not.
		 */
		for (i = 0; i < gres_data->type_cnt; i++) {
			if (gres_data->type_cnt_avail[i])
				continue;
			updated_config = true;
			break;
		}
	}
	if (!updated_config)
		return rc;
	if ((gres_cnt > gres_data->gres_cnt_config) && config_overrides) {
		info("%s: %s: count on node %s inconsistent with slurmctld count (%"PRIu64" != %"PRIu64")",
		     __func__, context_ptr->gres_type, node_name,
		     gres_cnt, gres_data->gres_cnt_config);
		gres_cnt = gres_data->gres_cnt_config;	/* Ignore excess GRES */
	}
	if ((topo_cnt == 0) && (topo_cnt != gres_data->topo_cnt)) {
		/* Need to clear topology info */
		_gres_node_state_delete_topo(gres_data);

		gres_data->topo_cnt = topo_cnt;
	}

	has_file = context_ptr->config_flags & GRES_CONF_HAS_FILE;
	has_type = context_ptr->config_flags & GRES_CONF_HAS_TYPE;
	if (_shared_gres(context_ptr->plugin_id))
		dev_cnt = topo_cnt;
	else
		dev_cnt = gres_cnt;
	if (has_file && (topo_cnt != gres_data->topo_cnt) && (dev_cnt == 0)) {
		/*
		 * Clear any vestigial GRES node state info.
		 */
		_gres_node_state_delete_topo(gres_data);

		xfree(gres_data->gres_bit_alloc);

		gres_data->topo_cnt = 0;
	} else if (has_file && (topo_cnt != gres_data->topo_cnt)) {
		/*
		 * Need to rebuild topology info.
		 * Resize the data structures here.
		 */
		rebuild_topo = true;
		gres_data->topo_gres_cnt_alloc =
			xrealloc(gres_data->topo_gres_cnt_alloc,
				 topo_cnt * sizeof(uint64_t));
		gres_data->topo_gres_cnt_avail =
			xrealloc(gres_data->topo_gres_cnt_avail,
				 topo_cnt * sizeof(uint64_t));
		for (i = 0; i < gres_data->topo_cnt; i++) {
			if (gres_data->topo_gres_bitmap) {
				FREE_NULL_BITMAP(gres_data->
						 topo_gres_bitmap[i]);
			}
			if (gres_data->topo_core_bitmap) {
				FREE_NULL_BITMAP(gres_data->
						 topo_core_bitmap[i]);
			}
			xfree(gres_data->topo_type_name[i]);
		}
		gres_data->topo_gres_bitmap =
			xrealloc(gres_data->topo_gres_bitmap,
				 topo_cnt * sizeof(bitstr_t *));
		gres_data->topo_core_bitmap =
			xrealloc(gres_data->topo_core_bitmap,
				 topo_cnt * sizeof(bitstr_t *));
		gres_data->topo_type_id = xrealloc(gres_data->topo_type_id,
						   topo_cnt * sizeof(uint32_t));
		gres_data->topo_type_name = xrealloc(gres_data->topo_type_name,
						     topo_cnt * sizeof(char *));
		if (gres_data->gres_bit_alloc)
			gres_data->gres_bit_alloc = bit_realloc(
				gres_data->gres_bit_alloc, dev_cnt);
		gres_data->topo_cnt = topo_cnt;
	} else if (_shared_gres(context_ptr->plugin_id) && gres_data->topo_cnt){
		/*
		 * Need to rebuild topology info to recover state after
		 * slurmctld restart with running jobs.
		 */
		rebuild_topo = true;
	}

	if (rebuild_topo) {
		iter = list_iterator_create(gres_conf_list);
		gres_inx = i = 0;
		while ((gres_slurmd_conf = (gres_slurmd_conf_t *)
			list_next(iter))) {
			if (gres_slurmd_conf->plugin_id !=
			    context_ptr->plugin_id)
				continue;
			if ((gres_data->gres_bit_alloc) &&
			    !_shared_gres(context_ptr->plugin_id))
				gres_data->topo_gres_cnt_alloc[i] = 0;
			gres_data->topo_gres_cnt_avail[i] =
					gres_slurmd_conf->count;
			if (gres_slurmd_conf->cpus) {
				/* NOTE: gres_slurmd_conf->cpus is cores */
				bitstr_t *tmp_bitmap = bit_alloc(core_cnt);
				int ret = bit_unfmt(tmp_bitmap,
						    gres_slurmd_conf->cpus);
				if (ret != SLURM_SUCCESS) {
					error("%s: %s: invalid GRES core specification (%s) on node %s",
					      __func__, context_ptr->gres_type,
					      gres_slurmd_conf->cpus,
					      node_name);
					FREE_NULL_BITMAP(tmp_bitmap);
				} else
					gres_data->topo_core_bitmap[i] =
						tmp_bitmap;
				cpus_config = core_cnt;
			} else if (cpus_config && !cpu_config_err) {
				cpu_config_err = true;
				error("%s: %s: has CPUs configured for only some of the records on node %s",
				      __func__, context_ptr->gres_type,
				      node_name);
			}

			if (gres_slurmd_conf->links) {
				if (gres_data->links_cnt &&
				    (gres_data->link_len != gres_cnt)) {
					/* Size changed, need to rebuild */
					for (j = 0; j < gres_data->link_len;j++)
						xfree(gres_data->links_cnt[j]);
					xfree(gres_data->links_cnt);
				}
				if (!gres_data->links_cnt) {
					gres_data->link_len = gres_cnt;
					gres_data->links_cnt =
						xcalloc(gres_cnt,
							sizeof(int *));
					for (j = 0; j < gres_cnt; j++) {
						gres_data->links_cnt[j] =
							xcalloc(gres_cnt,
								sizeof(int));
					}
				}
			}
			if (_shared_gres(gres_slurmd_conf->plugin_id)) {
				/* If running jobs recovered then already set */
				if (!gres_data->topo_gres_bitmap[i]) {
					gres_data->topo_gres_bitmap[i] =
						bit_alloc(dev_cnt);
					bit_set(gres_data->topo_gres_bitmap[i],
						gres_inx);
				}
				gres_inx++;
			} else if (dev_cnt == 0) {
				/*
				 * Slurmd found GRES, but slurmctld can't use
				 * them. Avoid creating zero-size bitmaps.
				 */
				has_file = false;
			} else {
				gres_data->topo_gres_bitmap[i] =
					bit_alloc(dev_cnt);
				for (j = 0; j < gres_slurmd_conf->count; j++) {
					if (gres_inx >= dev_cnt) {
						/* Ignore excess GRES on node */
						break;
					}
					bit_set(gres_data->topo_gres_bitmap[i],
						gres_inx);
					if (gres_data->gres_bit_alloc &&
					    bit_test(gres_data->gres_bit_alloc,
						     gres_inx)) {
					    /* Set by recovered job */
					    gres_data->topo_gres_cnt_alloc[i]++;
					}
					_links_str2array(
							gres_slurmd_conf->links,
							node_name, gres_data,
							gres_inx, gres_cnt);
					gres_inx++;
				}
			}
			gres_data->topo_type_id[i] =
				gres_plugin_build_id(gres_slurmd_conf->
						     type_name);
			gres_data->topo_type_name[i] =
				xstrdup(gres_slurmd_conf->type_name);
			i++;
			if (i >= gres_data->topo_cnt)
				break;
		}
		list_iterator_destroy(iter);
		if (cpu_config_err) {
			/*
			 * Some GRES of this type have "CPUs" configured. Set
			 * topo_core_bitmap for all others with all bits set.
			 */
			iter = list_iterator_create(gres_conf_list);
			while ((gres_slurmd_conf = (gres_slurmd_conf_t *)
				list_next(iter))) {
				if (gres_slurmd_conf->plugin_id !=
				    context_ptr->plugin_id)
					continue;
				for (j = 0; j < i; j++) {
					if (gres_data->topo_core_bitmap[j])
						continue;
					gres_data->topo_core_bitmap[j] =
						bit_alloc(core_cnt);
					bit_set_all(gres_data->
						    topo_core_bitmap[j]);
				}
			}
			list_iterator_destroy(iter);
		}
	} else if (!has_file && has_type) {
		/* Add GRES Type information as needed */
		iter = list_iterator_create(gres_conf_list);
		while ((gres_slurmd_conf = (gres_slurmd_conf_t *)
			list_next(iter))) {
			if (gres_slurmd_conf->plugin_id !=
			    context_ptr->plugin_id)
				continue;
			type_id = gres_plugin_build_id(
					gres_slurmd_conf->type_name);
			for (i = 0; i < gres_data->type_cnt; i++) {
				if (type_id == gres_data->type_id[i])
					break;
			}
			if (i < gres_data->type_cnt) {
				/* Update count as needed */
				gres_data->type_cnt_avail[i] =
					gres_slurmd_conf->count;
			} else {
				_add_gres_type(gres_slurmd_conf->type_name,
					       gres_data,
					       gres_slurmd_conf->count);
			}

		}
		list_iterator_destroy(iter);
	}

	if ((orig_config == NULL) || (orig_config[0] == '\0'))
		gres_data->gres_cnt_config = 0;
	else if (gres_data->gres_cnt_config == NO_VAL64) {
		/* This should have been filled in by _node_config_init() */
		_get_gres_cnt(gres_data, orig_config,
			      context_ptr->gres_name,
			      context_ptr->gres_name_colon,
			      context_ptr->gres_name_colon_len);
	}

	gres_data->gres_cnt_avail = gres_data->gres_cnt_config;

	if (has_file) {
		uint64_t gres_bits;
		if (_shared_gres(context_ptr->plugin_id)) {
			gres_bits = topo_cnt;
		} else {
			if (gres_data->gres_cnt_avail > MAX_GRES_BITMAP) {
				error("%s: %s has \"File\" plus very large \"Count\" "
				      "(%"PRIu64") for node %s, resetting value to %u",
				      __func__, context_ptr->gres_type,
				      gres_data->gres_cnt_avail, node_name,
				      MAX_GRES_BITMAP);
				gres_data->gres_cnt_avail = MAX_GRES_BITMAP;
				gres_data->gres_cnt_found = MAX_GRES_BITMAP;
			}
			gres_bits = gres_data->gres_cnt_avail;
		}

		_gres_bit_alloc_resize(gres_data, gres_bits);
	}

	if ((config_type_cnt > 1) &&
	    !_valid_gres_types(context_ptr->gres_type, gres_data, reason_down)){
		rc = EINVAL;
	} else if (!config_overrides &&
		   (gres_data->gres_cnt_found < gres_data->gres_cnt_config)) {
		if (reason_down && (*reason_down == NULL)) {
			xstrfmtcat(*reason_down,
				   "%s count too low (%"PRIu64" < %"PRIu64")",
				   context_ptr->gres_type,
				   gres_data->gres_cnt_found,
				   gres_data->gres_cnt_config);
		}
		rc = EINVAL;
	} else if (_valid_gres_type(context_ptr->gres_type, gres_data,
				    config_overrides, reason_down)) {
		rc = EINVAL;
	} else if (config_overrides && gres_data->topo_cnt &&
		   (gres_data->gres_cnt_found != gres_data->gres_cnt_config)) {
		error("%s on node %s configured for %"PRIu64" resources but "
		      "%"PRIu64" found, ignoring topology support",
		      context_ptr->gres_type, node_name,
		      gres_data->gres_cnt_config, gres_data->gres_cnt_found);
		if (gres_data->topo_core_bitmap) {
			for (i = 0; i < gres_data->topo_cnt; i++) {
				if (gres_data->topo_core_bitmap) {
					FREE_NULL_BITMAP(gres_data->
							 topo_core_bitmap[i]);
				}
				if (gres_data->topo_gres_bitmap) {
					FREE_NULL_BITMAP(gres_data->
							 topo_gres_bitmap[i]);
				}
				xfree(gres_data->topo_type_name[i]);
			}
			xfree(gres_data->topo_core_bitmap);
			xfree(gres_data->topo_gres_bitmap);
			xfree(gres_data->topo_gres_cnt_alloc);
			xfree(gres_data->topo_gres_cnt_avail);
			xfree(gres_data->topo_type_id);
			xfree(gres_data->topo_type_name);
		}
		gres_data->topo_cnt = 0;
	}

	return rc;
}

/*
 * Validate a node's configuration and put a gres record onto a list
 * Called immediately after gres_plugin_node_config_unpack().
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from merged slurm.conf/gres.conf
 * IN/OUT new_config - Updated gres info from slurm.conf
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN threads_per_core - Count of CPUs (threads) per core on this node
 * IN cores_per_sock - Count of cores per socket on this node
 * IN sock_cnt - Count of sockets on this node
 * IN config_overrides - true: Don't validate hardware, use slurm.conf
 *                             configuration
 *		         false: Validate hardware config, but use slurm.conf
 *                              config
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int gres_plugin_node_config_validate(char *node_name,
					    char *orig_config,
					    char **new_config,
					    List *gres_list,
					    int threads_per_core,
					    int cores_per_sock, int sock_cnt,
					    bool config_overrides,
					    char **reason_down)
{
	int i, rc, rc2;
	gres_state_t *gres_ptr, *gres_gpu_ptr = NULL, *gres_mps_ptr = NULL;
	int core_cnt = sock_cnt * cores_per_sock;
	int cpu_cnt  = core_cnt * threads_per_core;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL))
		*gres_list = list_create(_gres_node_list_delete);
	for (i = 0; i < gres_context_cnt; i++) {
		/* Find or create gres_state entry on the list */
		gres_ptr = list_find_first(*gres_list, _gres_find_id,
		                           &gres_context[i].plugin_id);
		if (gres_ptr == NULL) {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = gres_context[i].plugin_id;
			list_append(*gres_list, gres_ptr);
		}
		rc2 = _node_config_validate(node_name, orig_config,
					    gres_ptr, cpu_cnt, core_cnt,
					    sock_cnt, config_overrides,
					    reason_down, &gres_context[i]);
		rc = MAX(rc, rc2);
		if (gres_ptr->plugin_id == gpu_plugin_id)
			gres_gpu_ptr = gres_ptr;
		else if (gres_ptr->plugin_id == mps_plugin_id)
			gres_mps_ptr = gres_ptr;
	}
	_sync_node_mps_to_gpu(gres_mps_ptr, gres_gpu_ptr);
	_build_node_gres_str(gres_list, new_config, cores_per_sock, sock_cnt);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/* Convert number to new value with suffix (e.g. 2096 -> 2K) */
static void _gres_scale_value(uint64_t gres_size, uint64_t *gres_scaled,
			      char **suffix)
{
	uint64_t tmp_gres_size = gres_size;
	int i;

	tmp_gres_size = gres_size;
	for (i = 0; i < 4; i++) {
		if ((tmp_gres_size != 0) && ((tmp_gres_size % 1024) == 0))
			tmp_gres_size /= 1024;
		else
			break;
	}

	*gres_scaled = tmp_gres_size;
	if (i == 0)
		*suffix = "";
	else if (i == 1)
		*suffix = "K";
	else if (i == 2)
		*suffix = "M";
	else if (i == 3)
		*suffix = "G";
	else
		*suffix = "T";
}

/*
 * Add a GRES from node_feature plugin
 * IN node_name - name of the node for which the gres information applies
 * IN gres_name - name of the GRES being added or updated from the plugin
 * IN gres_size - count of this GRES on this node
 * IN/OUT new_config - Updated GRES info from slurm.conf
 * IN/OUT gres_list - List of GRES records for this node to track usage
 */
extern void gres_plugin_node_feature(char *node_name,
				     char *gres_name, uint64_t gres_size,
				     char **new_config, List *gres_list)
{
	char *new_gres = NULL, *tok, *save_ptr = NULL, *sep = "", *suffix = "";
	gres_state_t *gres_ptr;
	gres_node_state_t *gres_node_ptr;
	uint32_t plugin_id;
	uint64_t gres_scaled = 0;
	int gres_name_len;

	xassert(gres_name);
	gres_name_len = strlen(gres_name);
	plugin_id = gres_plugin_build_id(gres_name);
	if (*new_config) {
		tok = strtok_r(*new_config, ",", &save_ptr);
		while (tok) {
			if (!strncmp(tok, gres_name, gres_name_len) &&
			    ((tok[gres_name_len] == ':') ||
			     (tok[gres_name_len] == '\0'))) {
				/* Skip this record */
			} else {
				xstrfmtcat(new_gres, "%s%s", sep, tok);
				sep = ",";
			}
			tok = strtok_r(NULL, ",", &save_ptr);
		}
	}
	_gres_scale_value(gres_size, &gres_scaled, &suffix);
	xstrfmtcat(new_gres, "%s%s:%"PRIu64"%s",
		   sep, gres_name, gres_scaled, suffix);
	xfree(*new_config);
	*new_config = new_gres;

	slurm_mutex_lock(&gres_context_lock);
	if (gres_context_cnt > 0) {
		if (*gres_list == NULL)
			*gres_list = list_create(_gres_node_list_delete);
		gres_ptr = list_find_first(*gres_list, _gres_find_id,
		                           &plugin_id);
		if (gres_ptr == NULL) {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = plugin_id;
			gres_ptr->gres_data = _build_gres_node_state();
			list_append(*gres_list, gres_ptr);
		}
		gres_node_ptr = gres_ptr->gres_data;
		if (gres_size >= gres_node_ptr->gres_cnt_alloc) {
			gres_node_ptr->gres_cnt_avail = gres_size -
						gres_node_ptr->gres_cnt_alloc;
		} else {
			error("%s: Changed size count of GRES %s from %"PRIu64
			      " to %"PRIu64", resource over allocated",
			      __func__, gres_name,
			      gres_node_ptr->gres_cnt_avail, gres_size);
			gres_node_ptr->gres_cnt_avail = 0;
		}
		gres_node_ptr->gres_cnt_config = gres_size;
		gres_node_ptr->gres_cnt_found = gres_size;
		gres_node_ptr->node_feature = true;
	}
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Check validity of a GRES change. Specifically if a GRES type has "Files"
 * configured then the only valid new counts are the current count or zero
 *
 * RET true of the requested change is valid
 */
static int _node_reconfig_test(char *node_name, char *new_gres,
			       gres_state_t *gres_ptr,
			       slurm_gres_context_t *context_ptr)
{
	gres_node_state_t *orig_gres_data, *new_gres_data;
	int rc = SLURM_SUCCESS;

	xassert(gres_ptr);
	if (!(context_ptr->config_flags & GRES_CONF_HAS_FILE))
		return SLURM_SUCCESS;

	orig_gres_data = gres_ptr->gres_data;
	new_gres_data = _build_gres_node_state();
	_get_gres_cnt(new_gres_data, new_gres,
		      context_ptr->gres_name,
		      context_ptr->gres_name_colon,
		      context_ptr->gres_name_colon_len);
	if ((new_gres_data->gres_cnt_config != 0) &&
	    (new_gres_data->gres_cnt_config !=
	     orig_gres_data->gres_cnt_config)) {
		error("Attempt to change gres/%s Count on node %s from %"
		      PRIu64" to %"PRIu64" invalid with File configuration",
		      context_ptr->gres_name, node_name,
		      orig_gres_data->gres_cnt_config,
		      new_gres_data->gres_cnt_config);
		rc = ESLURM_INVALID_GRES;
	}
	_gres_node_state_delete(new_gres_data);

	return rc;
}

static int _node_reconfig(char *node_name, char *new_gres, char **gres_str,
			  gres_state_t *gres_ptr, bool config_overrides,
			  slurm_gres_context_t *context_ptr,
			  bool *updated_gpu_cnt)
{
	int i;
	gres_node_state_t *gres_data;
	uint64_t gres_bits, orig_cnt;

	xassert(gres_ptr);
	xassert(updated_gpu_cnt);
	*updated_gpu_cnt = false;
	if (gres_ptr->gres_data == NULL)
		gres_ptr->gres_data = _build_gres_node_state();
	gres_data = gres_ptr->gres_data;
	orig_cnt = gres_data->gres_cnt_config;

	_get_gres_cnt(gres_data, new_gres,
		      context_ptr->gres_name,
		      context_ptr->gres_name_colon,
		      context_ptr->gres_name_colon_len);

	if (gres_data->gres_cnt_config == orig_cnt)
		return SLURM_SUCCESS;	/* No change in count */

	/* Update count */
	context_ptr->total_cnt -= orig_cnt;
	context_ptr->total_cnt += gres_data->gres_cnt_config;

	if (!gres_data->gres_cnt_config)
		gres_data->gres_cnt_avail = gres_data->gres_cnt_config;
	else if (gres_data->gres_cnt_found != NO_VAL64)
		gres_data->gres_cnt_avail = gres_data->gres_cnt_found;
	else if (gres_data->gres_cnt_avail == NO_VAL64)
		gres_data->gres_cnt_avail = 0;

	if (context_ptr->config_flags & GRES_CONF_HAS_FILE) {
		if (_shared_gres(context_ptr->plugin_id))
			gres_bits = gres_data->topo_cnt;
		else
			gres_bits = gres_data->gres_cnt_avail;

		_gres_bit_alloc_resize(gres_data, gres_bits);
	} else if (gres_data->gres_bit_alloc &&
		   !_shared_gres(context_ptr->plugin_id)) {
		/*
		 * If GRES count changed in configuration between reboots,
		 * update bitmap sizes as needed.
		 */
		gres_bits = gres_data->gres_cnt_avail;
		if (gres_bits != bit_size(gres_data->gres_bit_alloc)) {
			info("gres/%s count changed on node %s to %"PRIu64,
			     context_ptr->gres_name, node_name, gres_bits);
			if (_sharing_gres(context_ptr->plugin_id))
				*updated_gpu_cnt = true;
			gres_data->gres_bit_alloc =
				bit_realloc(gres_data->gres_bit_alloc,
					    gres_bits);
			for (i = 0; i < gres_data->topo_cnt; i++) {
				if (gres_data->topo_gres_bitmap &&
				    gres_data->topo_gres_bitmap[i] &&
				    (gres_bits !=
				     bit_size(gres_data->topo_gres_bitmap[i]))){
					gres_data->topo_gres_bitmap[i] =
						bit_realloc(
						gres_data->topo_gres_bitmap[i],
						gres_bits);
				}
			}
		}
	}

	return SLURM_SUCCESS;
}

/* The GPU count on a node changed. Update MPS data structures to match */
static void _sync_node_mps_to_gpu(gres_state_t *mps_gres_ptr,
				  gres_state_t *gpu_gres_ptr)
{
	gres_node_state_t *gpu_gres_data, *mps_gres_data;
	uint64_t gpu_cnt, mps_alloc = 0, mps_rem;
	int i;

	if (!gpu_gres_ptr || !mps_gres_ptr)
		return;

	gpu_gres_data = gpu_gres_ptr->gres_data;
	mps_gres_data = mps_gres_ptr->gres_data;

	gpu_cnt = gpu_gres_data->gres_cnt_avail;
	if (mps_gres_data->gres_bit_alloc) {
		if (gpu_cnt == bit_size(mps_gres_data->gres_bit_alloc))
			return;		/* No change for gres/mps */
	}

	if (gpu_cnt == 0)
		return;			/* Still no GPUs */

	/* Free any excess gres/mps topo records */
	for (i = gpu_cnt; i < mps_gres_data->topo_cnt; i++) {
		if (mps_gres_data->topo_core_bitmap)
			FREE_NULL_BITMAP(mps_gres_data->topo_core_bitmap[i]);
		if (mps_gres_data->topo_gres_bitmap)
			FREE_NULL_BITMAP(mps_gres_data->topo_gres_bitmap[i]);
		xfree(mps_gres_data->topo_type_name[i]);
	}

	if (mps_gres_data->gres_cnt_avail == 0) {
		/* No gres/mps on this node */
		mps_gres_data->topo_cnt = 0;
		return;
	}

	if (!mps_gres_data->gres_bit_alloc) {
		mps_gres_data->gres_bit_alloc = bit_alloc(gpu_cnt);
	} else {
		mps_gres_data->gres_bit_alloc =
				bit_realloc(mps_gres_data->gres_bit_alloc,
					    gpu_cnt);
	}

	/* Add any additional required gres/mps topo records */
	if (mps_gres_data->topo_cnt) {
		mps_gres_data->topo_core_bitmap =
			xrealloc(mps_gres_data->topo_core_bitmap,
				 sizeof(bitstr_t *) * gpu_cnt);
		mps_gres_data->topo_gres_bitmap =
			xrealloc(mps_gres_data->topo_gres_bitmap,
				 sizeof(bitstr_t *) * gpu_cnt);
		mps_gres_data->topo_gres_cnt_alloc =
			xrealloc(mps_gres_data->topo_gres_cnt_alloc,
				 sizeof(uint64_t) * gpu_cnt);
		mps_gres_data->topo_gres_cnt_avail =
			xrealloc(mps_gres_data->topo_gres_cnt_avail,
				 sizeof(uint64_t) * gpu_cnt);
		mps_gres_data->topo_type_id =
			xrealloc(mps_gres_data->topo_type_id,
				 sizeof(uint32_t) * gpu_cnt);
		mps_gres_data->topo_type_name =
			xrealloc(mps_gres_data->topo_type_name,
				 sizeof(char *) * gpu_cnt);
	} else {
		mps_gres_data->topo_core_bitmap =
			xcalloc(gpu_cnt, sizeof(bitstr_t *));
		mps_gres_data->topo_gres_bitmap =
			xcalloc(gpu_cnt, sizeof(bitstr_t *));
		mps_gres_data->topo_gres_cnt_alloc =
			xcalloc(gpu_cnt, sizeof(uint64_t));
		mps_gres_data->topo_gres_cnt_avail =
			xcalloc(gpu_cnt, sizeof(uint64_t));
		mps_gres_data->topo_type_id =
			xcalloc(gpu_cnt, sizeof(uint32_t));
		mps_gres_data->topo_type_name =
			xcalloc(gpu_cnt, sizeof(char *));
	}

	/*
	 * Evenly distribute any remaining MPS counts.
	 * Counts get reset as needed when the node registers.
	 */
	for (i = 0; i < mps_gres_data->topo_cnt; i++)
		mps_alloc += mps_gres_data->topo_gres_cnt_avail[i];
	if (mps_alloc >= mps_gres_data->gres_cnt_avail)
		mps_rem = 0;
	else
		mps_rem = mps_gres_data->gres_cnt_avail - mps_alloc;
	for (i = mps_gres_data->topo_cnt; i < gpu_cnt; i++) {
		mps_gres_data->topo_gres_bitmap[i] = bit_alloc(gpu_cnt);
		bit_set(mps_gres_data->topo_gres_bitmap[i], i);
		mps_alloc = mps_rem / (gpu_cnt - i);
		mps_gres_data->topo_gres_cnt_avail[i] = mps_alloc;
		mps_rem -= mps_alloc;
	}
	mps_gres_data->topo_cnt = gpu_cnt;

	for (i = 0; i < mps_gres_data->topo_cnt; i++) {
		if (mps_gres_data->topo_gres_bitmap &&
		    mps_gres_data->topo_gres_bitmap[i] &&
		    (gpu_cnt != bit_size(mps_gres_data->topo_gres_bitmap[i]))) {
			mps_gres_data->topo_gres_bitmap[i] =
				bit_realloc(mps_gres_data->topo_gres_bitmap[i],
					    gpu_cnt);
		}
	}
}

/* Convert core bitmap into socket string, xfree return value */
static char *_core_bitmap2str(bitstr_t *core_map, int cores_per_sock,
			      int sock_per_node)
{
	char *sock_info = NULL, tmp[256];
	bitstr_t *sock_map;
	int c, s, core_offset, max_core;
	bool any_set = false;

	xassert(core_map);
	max_core = bit_size(core_map) - 1;
	sock_map = bit_alloc(sock_per_node);
	for (s = 0; s < sock_per_node; s++) {
		core_offset = s * cores_per_sock;
		for (c = 0; c < cores_per_sock; c++) {
			if (core_offset > max_core) {
				error("%s: bad core offset (%d >= %d)",
				      __func__, core_offset, max_core);
				break;
			}
			if (bit_test(core_map, core_offset++)) {
				bit_set(sock_map, s);
				any_set = true;
				break;
			}
		}
	}
	if (any_set) {
		bit_fmt(tmp, sizeof(tmp), sock_map);
		xstrfmtcat(sock_info, "(S:%s)", tmp);
	} else {
		/* We have a core bitmap with no bits set */
		sock_info = xstrdup("");
	}
	bit_free(sock_map);

	return sock_info;
}

/* Given a count, modify it as needed and return suffix (e.g. "M" for mega ) */
static char *_get_suffix(uint64_t *count)
{
	if (*count == 0)
		return "";
	if ((*count % ((uint64_t)1024 * 1024 * 1024 * 1024 * 1024)) == 0) {
		*count /= ((uint64_t)1024 * 1024 * 1024 * 1024 * 1024);
		return "P";
	} else if ((*count % ((uint64_t)1024 * 1024 * 1024 * 1024)) == 0) {
		*count /= ((uint64_t)1024 * 1024 * 1024 * 1024);
		return "T";
	} else if ((*count % ((uint64_t)1024 * 1024 * 1024)) == 0) {
		*count /= ((uint64_t)1024 * 1024 * 1024);
		return "G";
	} else if ((*count % (1024 * 1024)) == 0) {
		*count /= (1024 * 1024);
		return "M";
	} else if ((*count % 1024) == 0) {
		*count /= 1024;
		return "K";
	} else {
		return "";
	}
}

/* Build node's GRES string based upon data in that node's GRES list */
static void _build_node_gres_str(List *gres_list, char **gres_str,
				 int cores_per_sock, int sock_per_node)
{
	gres_state_t *gres_ptr;
	gres_node_state_t *gres_node_state;
	bitstr_t *done_topo, *core_map;
	uint64_t gres_sum;
	char *sep = "", *suffix, *sock_info = NULL, *sock_str;
	int c, i, j;

	xassert(gres_str);
	xfree(*gres_str);
	for (c = 0; c < gres_context_cnt; c++) {
		/* Find gres_state entry on the list */
		gres_ptr = list_find_first(*gres_list, _gres_find_id,
		                           &gres_context[c].plugin_id);
		if (gres_ptr == NULL)
			continue;	/* Node has none of this GRES */

		gres_node_state = (gres_node_state_t *) gres_ptr->gres_data;
		if (gres_node_state->topo_cnt &&
		    gres_node_state->gres_cnt_avail) {
			done_topo = bit_alloc(gres_node_state->topo_cnt);
			for (i = 0; i < gres_node_state->topo_cnt; i++) {
				if (bit_test(done_topo, i))
					continue;
				bit_set(done_topo, i);
				gres_sum = gres_node_state->
					   topo_gres_cnt_avail[i];
				if (gres_node_state->topo_core_bitmap[i]) {
					core_map = bit_copy(
							gres_node_state->
							topo_core_bitmap[i]);
				} else
					core_map = NULL;
				for (j = 0; j < gres_node_state->topo_cnt; j++){
					if (gres_node_state->topo_type_id[i] !=
					    gres_node_state->topo_type_id[j])
						continue;
					if (bit_test(done_topo, j))
						continue;
					bit_set(done_topo, j);
					gres_sum += gres_node_state->
						    topo_gres_cnt_avail[j];
					if (core_map &&
					    gres_node_state->
					    topo_core_bitmap[j]) {
						bit_or(core_map,
						       gres_node_state->
						       topo_core_bitmap[j]);
					} else if (gres_node_state->
						   topo_core_bitmap[j]) {
						core_map = bit_copy(
							   gres_node_state->
							   topo_core_bitmap[j]);
					}
				}
				if (core_map) {
					sock_info = _core_bitmap2str(core_map,
							cores_per_sock,
							sock_per_node);
					bit_free(core_map);
					sock_str = sock_info;
				} else
					sock_str = "";
				suffix = _get_suffix(&gres_sum);
				if (gres_node_state->topo_type_name[i]) {
					xstrfmtcat(*gres_str,
						   "%s%s:%s:%"PRIu64"%s%s", sep,
						   gres_context[c].gres_name,
						   gres_node_state->
						   topo_type_name[i],
						   gres_sum, suffix, sock_str);
				} else {
					xstrfmtcat(*gres_str,
						   "%s%s:%"PRIu64"%s%s", sep,
						   gres_context[c].gres_name,
						   gres_sum, suffix, sock_str);
				}
				xfree(sock_info);
				sep = ",";
			}
			bit_free(done_topo);
		} else if (gres_node_state->type_cnt &&
			   gres_node_state->gres_cnt_avail) {
			for (i = 0; i < gres_node_state->type_cnt; i++) {
				gres_sum = gres_node_state->type_cnt_avail[i];
				suffix = _get_suffix(&gres_sum);
				xstrfmtcat(*gres_str, "%s%s:%s:%"PRIu64"%s",
					   sep, gres_context[c].gres_name,
					   gres_node_state->type_name[i],
					   gres_sum, suffix);
				sep = ",";
			}
		} else if (gres_node_state->gres_cnt_avail) {
			gres_sum = gres_node_state->gres_cnt_avail;
			suffix = _get_suffix(&gres_sum);
			xstrfmtcat(*gres_str, "%s%s:%"PRIu64"%s",
				   sep, gres_context[c].gres_name,
				   gres_sum, suffix);
			sep = ",";
		}
	}
}

/*
 * Note that a node's configuration has been modified (e.g. "scontol update ..")
 * IN node_name - name of the node for which the gres information applies
 * IN new_gres - Updated GRES information supplied from slurm.conf or scontrol
 * IN/OUT gres_str - Node's current GRES string, updated as needed
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN config_overrides - true: Don't validate hardware, use slurm.conf
 *                             configuration
 *		         false: Validate hardware config, but use slurm.conf
 *                              config
 * IN cores_per_sock - Number of cores per socket on this node
 * IN sock_per_node - Total count of sockets on this node (on any board)
 */
extern int gres_plugin_node_reconfig(char *node_name,
				     char *new_gres,
				     char **gres_str,
				     List *gres_list,
				     bool config_overrides,
				     int cores_per_sock,
				     int sock_per_node)
{
	int i, rc;
	ListIterator gres_iter;
	gres_state_t *gres_ptr = NULL, **gres_ptr_array;
	gres_state_t *gpu_gres_ptr = NULL, *mps_gres_ptr;

	rc = gres_plugin_init();
	slurm_mutex_lock(&gres_context_lock);
	gres_ptr_array = xcalloc(gres_context_cnt, sizeof(gres_state_t *));
	if ((gres_context_cnt > 0) && (*gres_list == NULL))
		*gres_list = list_create(_gres_node_list_delete);

	/* First validate all of the requested GRES changes */
	for (i = 0; (rc == SLURM_SUCCESS) && (i < gres_context_cnt); i++) {
		/* Find gres_state entry on the list */
		gres_ptr = list_find_first(*gres_list, _gres_find_id,
		                           &gres_context[i].plugin_id);
		if (gres_ptr == NULL)
			continue;
		gres_ptr_array[i] = gres_ptr;
		rc = _node_reconfig_test(node_name, new_gres, gres_ptr,
					 &gres_context[i]);
	}

	/* Now update the GRES counts */
	for (i = 0; (rc == SLURM_SUCCESS) && (i < gres_context_cnt); i++) {
		bool updated_gpu_cnt = false;
		if (gres_ptr_array[i] == NULL)
			continue;
		rc = _node_reconfig(node_name, new_gres, gres_str,
				    gres_ptr_array[i], config_overrides,
				    &gres_context[i], &updated_gpu_cnt);
		if (updated_gpu_cnt)
			gpu_gres_ptr = gres_ptr;
	}

	/* Now synchronize gres/gpu and gres/mps state */
	if (gpu_gres_ptr && have_mps) {
		/* Update gres/mps counts and bitmaps to match gres/gpu */
		gres_iter = list_iterator_create(*gres_list);
		while ((mps_gres_ptr = (gres_state_t *) list_next(gres_iter))) {
			if (_shared_gres(mps_gres_ptr->plugin_id))
				break;
		}
		list_iterator_destroy(gres_iter);
		_sync_node_mps_to_gpu(mps_gres_ptr, gpu_gres_ptr);
	}

	/* Build new per-node gres_str */
	_build_node_gres_str(gres_list, gres_str, cores_per_sock,sock_per_node);
	slurm_mutex_unlock(&gres_context_lock);
	xfree(gres_ptr_array);

	return rc;
}

/*
 * Pack a node's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_plugin_node_config_validate()
 * IN/OUT buffer - location to write state to
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_plugin_node_state_pack(List gres_list, Buf buffer,
				       char *node_name)
{
	int rc = SLURM_SUCCESS;
	uint32_t top_offset, tail_offset;
	uint32_t magic = GRES_MAGIC;
	uint16_t gres_bitmap_size, rec_cnt = 0;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;
	gres_node_state_t *gres_node_ptr;

	if (gres_list == NULL) {
		pack16(rec_cnt, buffer);
		return rc;
	}

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		gres_node_ptr = (gres_node_state_t *) gres_ptr->gres_data;
		pack32(magic, buffer);
		pack32(gres_ptr->plugin_id, buffer);
		pack64(gres_node_ptr->gres_cnt_avail, buffer);
		/*
		 * Just note if gres_bit_alloc exists.
		 * Rebuild it based upon the state of recovered jobs
		 */
		if (gres_node_ptr->gres_bit_alloc)
			gres_bitmap_size = bit_size(gres_node_ptr->gres_bit_alloc);
		else
			gres_bitmap_size = 0;
		pack16(gres_bitmap_size, buffer);
		rec_cnt++;
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	tail_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, top_offset);
	pack16(rec_cnt, buffer);
	set_buf_offset(buffer, tail_offset);

	return rc;
}

/*
 * Unpack a node's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_node_state_pack()
 * IN/OUT buffer - location to read state from
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_plugin_node_state_unpack(List *gres_list, Buf buffer,
					 char *node_name,
					 uint16_t protocol_version)
{
	int i, rc;
	uint32_t magic = 0, plugin_id = 0;
	uint64_t gres_cnt_avail = 0;
	uint16_t gres_bitmap_size = 0, rec_cnt = 0;
	gres_state_t *gres_ptr;
	gres_node_state_t *gres_node_ptr;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL))
		*gres_list = list_create(_gres_node_list_delete);

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;
		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			safe_unpack64(&gres_cnt_avail, buffer);
			safe_unpack16(&gres_bitmap_size, buffer);
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			goto unpack_error;
		}
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_context[i].plugin_id == plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("%s: no plugin configured to unpack data type %u from node %s",
			      __func__, plugin_id, node_name);
			/*
			 * A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data.
			 */
			continue;
		}
		gres_node_ptr = _build_gres_node_state();
		gres_node_ptr->gres_cnt_avail = gres_cnt_avail;
		if (gres_bitmap_size) {
			gres_node_ptr->gres_bit_alloc =
				bit_alloc(gres_bitmap_size);
		}
		gres_ptr = xmalloc(sizeof(gres_state_t));
		gres_ptr->plugin_id = gres_context[i].plugin_id;
		gres_ptr->gres_data = gres_node_ptr;
		list_append(*gres_list, gres_ptr);
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error from node %s", __func__, node_name);
	slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

static void *_node_state_dup(void *gres_data)
{
	int i, j;
	gres_node_state_t *gres_ptr = (gres_node_state_t *) gres_data;
	gres_node_state_t *new_gres;

	if (gres_ptr == NULL)
		return NULL;

	new_gres = xmalloc(sizeof(gres_node_state_t));
	new_gres->gres_cnt_found  = gres_ptr->gres_cnt_found;
	new_gres->gres_cnt_config = gres_ptr->gres_cnt_config;
	new_gres->gres_cnt_avail  = gres_ptr->gres_cnt_avail;
	new_gres->gres_cnt_alloc  = gres_ptr->gres_cnt_alloc;
	new_gres->no_consume      = gres_ptr->no_consume;
	if (gres_ptr->gres_bit_alloc)
		new_gres->gres_bit_alloc = bit_copy(gres_ptr->gres_bit_alloc);

	if (gres_ptr->links_cnt && gres_ptr->link_len) {
		new_gres->links_cnt = xcalloc(gres_ptr->link_len,
					      sizeof(int *));
		j = sizeof(int) * gres_ptr->link_len;
		for (i = 0; i < gres_ptr->link_len; i++) {
			new_gres->links_cnt[i] = xmalloc(j);
			memcpy(new_gres->links_cnt[i],gres_ptr->links_cnt[i],j);
		}
		new_gres->link_len = gres_ptr->link_len;
	}

	if (gres_ptr->topo_cnt) {
		new_gres->topo_cnt         = gres_ptr->topo_cnt;
		new_gres->topo_core_bitmap = xcalloc(gres_ptr->topo_cnt,
						     sizeof(bitstr_t *));
		new_gres->topo_gres_bitmap = xcalloc(gres_ptr->topo_cnt,
						     sizeof(bitstr_t *));
		new_gres->topo_gres_cnt_alloc = xcalloc(gres_ptr->topo_cnt,
							sizeof(uint64_t));
		new_gres->topo_gres_cnt_avail = xcalloc(gres_ptr->topo_cnt,
							sizeof(uint64_t));
		new_gres->topo_type_id = xcalloc(gres_ptr->topo_cnt,
						 sizeof(uint32_t));
		new_gres->topo_type_name = xcalloc(gres_ptr->topo_cnt,
						   sizeof(char *));
		for (i = 0; i < gres_ptr->topo_cnt; i++) {
			if (gres_ptr->topo_core_bitmap[i]) {
				new_gres->topo_core_bitmap[i] =
					bit_copy(gres_ptr->topo_core_bitmap[i]);
			}
			new_gres->topo_gres_bitmap[i] =
				bit_copy(gres_ptr->topo_gres_bitmap[i]);
			new_gres->topo_gres_cnt_alloc[i] =
				gres_ptr->topo_gres_cnt_alloc[i];
			new_gres->topo_gres_cnt_avail[i] =
				gres_ptr->topo_gres_cnt_avail[i];
			new_gres->topo_type_id[i] = gres_ptr->topo_type_id[i];
			new_gres->topo_type_name[i] =
				xstrdup(gres_ptr->topo_type_name[i]);
		}
	}

	if (gres_ptr->type_cnt) {
		new_gres->type_cnt       = gres_ptr->type_cnt;
		new_gres->type_cnt_alloc = xcalloc(gres_ptr->type_cnt,
						   sizeof(uint64_t));
		new_gres->type_cnt_avail = xcalloc(gres_ptr->type_cnt,
						   sizeof(uint64_t));
		new_gres->type_id = xcalloc(gres_ptr->type_cnt,
					    sizeof(uint32_t));
		new_gres->type_name = xcalloc(gres_ptr->type_cnt,
					      sizeof(char *));
		for (i = 0; i < gres_ptr->type_cnt; i++) {
			new_gres->type_cnt_alloc[i] =
				gres_ptr->type_cnt_alloc[i];
			new_gres->type_cnt_avail[i] =
				gres_ptr->type_cnt_avail[i];
			new_gres->type_id[i] = gres_ptr->type_id[i];
			new_gres->type_name[i] =
				xstrdup(gres_ptr->type_name[i]);
		}
	}

	return new_gres;
}

/*
 * Duplicate a node gres status (used for will-run logic)
 * IN gres_list - node gres state information
 * RET a copy of gres_list or NULL on failure
 */
extern List gres_plugin_node_state_dup(List gres_list)
{
	int i;
	List new_list = NULL;
	ListIterator gres_iter;
	gres_state_t *gres_ptr, *new_gres;
	void *gres_data;

	if (gres_list == NULL)
		return new_list;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0)) {
		new_list = list_create(_gres_node_list_delete);
	}
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		for (i=0; i<gres_context_cnt; i++) {
			if (gres_ptr->plugin_id != gres_context[i].plugin_id)
				continue;
			gres_data = _node_state_dup(gres_ptr->gres_data);
			if (gres_data) {
				new_gres = xmalloc(sizeof(gres_state_t));
				new_gres->plugin_id = gres_ptr->plugin_id;
				new_gres->gres_data = gres_data;
				list_append(new_list, new_gres);
			}
			break;
		}
		if (i >= gres_context_cnt) {
			error("Could not find plugin id %u to dup node record",
			      gres_ptr->plugin_id);
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return new_list;
}

static void _node_state_dealloc(gres_state_t *gres_ptr)
{
	int i;
	gres_node_state_t *gres_node_ptr;
	char *gres_name = NULL;

	gres_node_ptr = (gres_node_state_t *) gres_ptr->gres_data;
	gres_node_ptr->gres_cnt_alloc = 0;
	if (gres_node_ptr->gres_bit_alloc) {
		int i = bit_size(gres_node_ptr->gres_bit_alloc) - 1;
		if (i >= 0)
			bit_nclear(gres_node_ptr->gres_bit_alloc, 0, i);
	}

	if (gres_node_ptr->topo_cnt && !gres_node_ptr->topo_gres_cnt_alloc) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_ptr->plugin_id == gres_context[i].plugin_id) {
				gres_name = gres_context[i].gres_name;
				break;
			}
		}
		error("gres_plugin_node_state_dealloc_all: gres/%s topo_cnt!=0 "
		      "and topo_gres_cnt_alloc is NULL", gres_name);
	} else if (gres_node_ptr->topo_cnt) {
		for (i = 0; i < gres_node_ptr->topo_cnt; i++) {
			gres_node_ptr->topo_gres_cnt_alloc[i] = 0;
		}
	} else {
		/*
		 * This array can be set at startup if a job has been allocated
		 * specific GRES and the node has not registered with the
		 * details needed to track individual GRES (rather than only
		 * a GRES count).
		 */
		xfree(gres_node_ptr->topo_gres_cnt_alloc);
	}

	for (i = 0; i < gres_node_ptr->type_cnt; i++) {
		gres_node_ptr->type_cnt_alloc[i] = 0;
	}
}

/*
 * Deallocate all resources on this node previous allocated to any jobs.
 *	This function isused to synchronize state after slurmctld restarts or
 *	is reconfigured.
 * IN gres_list - node gres state information
 */
extern void gres_plugin_node_state_dealloc_all(List gres_list)
{
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	if (gres_list == NULL)
		return;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		_node_state_dealloc(gres_ptr);
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

static char *_node_gres_used(void *gres_data, char *gres_name)
{
	gres_node_state_t *gres_node_ptr;
	char *sep = "";
	int i, j;

	xassert(gres_data);
	gres_node_ptr = (gres_node_state_t *) gres_data;

	if ((gres_node_ptr->topo_cnt != 0) &&
	    (gres_node_ptr->no_consume == false)) {
		bitstr_t *topo_printed = bit_alloc(gres_node_ptr->topo_cnt);
		xfree(gres_node_ptr->gres_used);    /* Free any cached value */
		for (i = 0; i < gres_node_ptr->topo_cnt; i++) {
			bitstr_t *topo_gres_bitmap = NULL;
			uint64_t gres_alloc_cnt = 0;
			char *gres_alloc_idx, tmp_str[64];
			if (bit_test(topo_printed, i))
				continue;
			bit_set(topo_printed, i);
			if (gres_node_ptr->topo_gres_bitmap[i]) {
				topo_gres_bitmap =
					bit_copy(gres_node_ptr->
						 topo_gres_bitmap[i]);
			}
			for (j = i + 1; j < gres_node_ptr->topo_cnt; j++) {
				if (bit_test(topo_printed, j))
					continue;
				if (gres_node_ptr->topo_type_id[i] !=
				    gres_node_ptr->topo_type_id[j])
					continue;
				bit_set(topo_printed, j);
				if (gres_node_ptr->topo_gres_bitmap[j]) {
					if (!topo_gres_bitmap) {
						topo_gres_bitmap =
							bit_copy(gres_node_ptr->
								 topo_gres_bitmap[j]);
					} else if (bit_size(topo_gres_bitmap) ==
						   bit_size(gres_node_ptr->
							    topo_gres_bitmap[j])){
						bit_or(topo_gres_bitmap,
						       gres_node_ptr->
						       topo_gres_bitmap[j]);
					}
				}		
			}
			if (gres_node_ptr->gres_bit_alloc && topo_gres_bitmap &&
			    (bit_size(topo_gres_bitmap) ==
			     bit_size(gres_node_ptr->gres_bit_alloc))) {
				bit_and(topo_gres_bitmap,
					gres_node_ptr->gres_bit_alloc);
				gres_alloc_cnt = bit_set_count(topo_gres_bitmap);
			}
			if (gres_alloc_cnt > 0) {
				bit_fmt(tmp_str, sizeof(tmp_str),
					topo_gres_bitmap);
				gres_alloc_idx = tmp_str;
			} else {
				gres_alloc_idx = "N/A";
			}
			xstrfmtcat(gres_node_ptr->gres_used,
				   "%s%s:%s:%"PRIu64"(IDX:%s)", sep, gres_name,
				   gres_node_ptr->topo_type_name[i],
				   gres_alloc_cnt, gres_alloc_idx);
			sep = ",";
			FREE_NULL_BITMAP(topo_gres_bitmap);
		}
		FREE_NULL_BITMAP(topo_printed);
	} else if (gres_node_ptr->gres_used) {
		;	/* Used cached value */
	} else if (gres_node_ptr->type_cnt == 0) {
		if (gres_node_ptr->no_consume) {
			xstrfmtcat(gres_node_ptr->gres_used, "%s:0", gres_name);
		} else {
			xstrfmtcat(gres_node_ptr->gres_used, "%s:%"PRIu64,
				   gres_name, gres_node_ptr->gres_cnt_alloc);
		}
	} else {
		for (i = 0; i < gres_node_ptr->type_cnt; i++) {
			if (gres_node_ptr->no_consume) {
				xstrfmtcat(gres_node_ptr->gres_used,
					   "%s%s:%s:0", sep, gres_name,
					   gres_node_ptr->type_name[i]);
			} else {
				xstrfmtcat(gres_node_ptr->gres_used,
					   "%s%s:%s:%"PRIu64, sep, gres_name,
					   gres_node_ptr->type_name[i],
					   gres_node_ptr->type_cnt_alloc[i]);
			}
			sep = ",";
		}
	}

	return gres_node_ptr->gres_used;
}

static void _node_state_log(void *gres_data, char *node_name, char *gres_name)
{
	gres_node_state_t *gres_node_ptr;
	int i, j;
	char *buf = NULL, *sep, tmp_str[128];

	xassert(gres_data);
	gres_node_ptr = (gres_node_state_t *) gres_data;

	info("gres/%s: state for %s", gres_name, node_name);
	if (gres_node_ptr->gres_cnt_found == NO_VAL64) {
		snprintf(tmp_str, sizeof(tmp_str), "TBD");
	} else {
		snprintf(tmp_str, sizeof(tmp_str), "%"PRIu64,
			 gres_node_ptr->gres_cnt_found);
	}

	if (gres_node_ptr->no_consume) {
		info("  gres_cnt found:%s configured:%"PRIu64" "	
		     "avail:%"PRIu64" no_consume",
		     tmp_str, gres_node_ptr->gres_cnt_config,
		     gres_node_ptr->gres_cnt_avail);
	} else {
		info("  gres_cnt found:%s configured:%"PRIu64" "
		     "avail:%"PRIu64" alloc:%"PRIu64"",
		     tmp_str, gres_node_ptr->gres_cnt_config,
		     gres_node_ptr->gres_cnt_avail,
		     gres_node_ptr->gres_cnt_alloc);
	}

	if (gres_node_ptr->gres_bit_alloc) {
		bit_fmt(tmp_str, sizeof(tmp_str),gres_node_ptr->gres_bit_alloc);
		info("  gres_bit_alloc:%s of %d",
		     tmp_str, (int) bit_size(gres_node_ptr->gres_bit_alloc));
	} else {
		info("  gres_bit_alloc:NULL");
	}

	info("  gres_used:%s", gres_node_ptr->gres_used);

	if (gres_node_ptr->links_cnt && gres_node_ptr->link_len) {
		for (i = 0; i < gres_node_ptr->link_len; i++) {
			sep = "";
			for (j = 0; j < gres_node_ptr->link_len; j++) {
				xstrfmtcat(buf, "%s%d", sep,
					   gres_node_ptr->links_cnt[i][j]);
				sep = ", ";
			}
			info("  links[%d]:%s", i, buf);
			xfree(buf);
		}
	}

	for (i = 0; i < gres_node_ptr->topo_cnt; i++) {
		info("  topo[%d]:%s(%u)", i, gres_node_ptr->topo_type_name[i],
		     gres_node_ptr->topo_type_id[i]);
		if (gres_node_ptr->topo_core_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_node_ptr->topo_core_bitmap[i]);
			info("   topo_core_bitmap[%d]:%s of %d", i, tmp_str,
			     (int)bit_size(gres_node_ptr->topo_core_bitmap[i]));
		} else
			info("   topo_core_bitmap[%d]:NULL", i);
		if (gres_node_ptr->topo_gres_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_node_ptr->topo_gres_bitmap[i]);
			info("   topo_gres_bitmap[%d]:%s of %d", i, tmp_str,
			     (int)bit_size(gres_node_ptr->topo_gres_bitmap[i]));
		} else
			info("   topo_gres_bitmap[%d]:NULL", i);
		info("   topo_gres_cnt_alloc[%d]:%"PRIu64"", i,
		     gres_node_ptr->topo_gres_cnt_alloc[i]);
		info("   topo_gres_cnt_avail[%d]:%"PRIu64"", i,
		     gres_node_ptr->topo_gres_cnt_avail[i]);
	}

	for (i = 0; i < gres_node_ptr->type_cnt; i++) {
		info("  type[%d]:%s(%u)", i, gres_node_ptr->type_name[i],
		     gres_node_ptr->type_id[i]);
		info("   type_cnt_alloc[%d]:%"PRIu64, i,
		     gres_node_ptr->type_cnt_alloc[i]);
		info("   type_cnt_avail[%d]:%"PRIu64, i,
		     gres_node_ptr->type_cnt_avail[i]);
	}
}

/*
 * Log a node's current gres state
 * IN gres_list - generated by gres_plugin_node_config_validate()
 * IN node_name - name of the node for which the gres information applies
 */
extern void gres_plugin_node_state_log(List gres_list, char *node_name)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES) || !gres_list)
		return;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			_node_state_log(gres_ptr->gres_data, node_name,
					gres_context[i].gres_name);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Build a string indicating a node's drained GRES
 * IN gres_list - generated by gres_plugin_node_config_validate()
 * RET - string, must be xfreed by caller
 */
extern char *gres_get_node_drain(List gres_list)
{
	char *node_drain = xstrdup("N/A");

	return node_drain;
}

/*
 * Build a string indicating a node's used GRES
 * IN gres_list - generated by gres_plugin_node_config_validate()
 * RET - string, must be xfreed by caller
 */
extern char *gres_get_node_used(List gres_list)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;
	char *gres_used = NULL, *tmp;

	if (!gres_list)
		return gres_used;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			tmp = _node_gres_used(gres_ptr->gres_data,
					      gres_context[i].gres_name);
			if (!tmp)
				continue;
			if (gres_used)
				xstrcat(gres_used, ",");
			xstrcat(gres_used, tmp);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return gres_used;
}

/*
 * Give the total system count of a given GRES
 * Returns NO_VAL64 if name not found
 */
extern uint64_t gres_get_system_cnt(char *name)
{
	uint64_t count = NO_VAL64;
	int i;

	if (!name)
		return NO_VAL64;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (!xstrcmp(gres_context[i].gres_name, name)) {
			count = gres_context[i].total_cnt;
			break;
		}
	}
	slurm_mutex_unlock(&gres_context_lock);
	return count;
}


/*
 * Get the count of a node's GRES
 * IN gres_list - List of Gres records for this node to track usage
 * IN name - name of gres
 */
extern uint64_t gres_plugin_node_config_cnt(List gres_list, char *name)
{
	int i;
	gres_state_t *gres_ptr;
	gres_node_state_t *data_ptr;
	uint64_t count = 0;

	if (!gres_list || !name || !list_count(gres_list))
		return count;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (!xstrcmp(gres_context[i].gres_name, name)) {
			/* Find or create gres_state entry on the list */
			gres_ptr = list_find_first(gres_list, _gres_find_id,
			                           &gres_context[i].plugin_id);

			if (!gres_ptr || !gres_ptr->gres_data)
				break;
			data_ptr = (gres_node_state_t *)gres_ptr->gres_data;
			count = data_ptr->gres_cnt_config;
			break;
		} else if (!xstrncmp(name, gres_context[i].gres_name_colon,
				     gres_context[i].gres_name_colon_len)) {
			int type;
			uint32_t type_id;
			char *type_str = NULL;

			if (!(type_str = strchr(name, ':'))) {
				error("Invalid gres name '%s'", name);
				break;
			}
			type_str++;

			gres_ptr = list_find_first(gres_list, _gres_find_id,
			                           &gres_context[i].plugin_id);

			if (!gres_ptr || !gres_ptr->gres_data)
				break;
			data_ptr = (gres_node_state_t *)gres_ptr->gres_data;
			type_id = gres_plugin_build_id(type_str);
			for (type = 0; type < data_ptr->type_cnt; type++) {
				if (data_ptr->type_id[type] == type_id) {
					count = data_ptr->type_cnt_avail[type];
					break;
				}
			}
			break;
		}
	}
	slurm_mutex_unlock(&gres_context_lock);

	return count;
}

static void _job_state_delete(void *gres_data)
{
	int i;
	gres_job_state_t *gres_ptr = (gres_job_state_t *) gres_data;

	if (gres_ptr == NULL)
		return;

	for (i = 0; i < gres_ptr->node_cnt; i++) {
		if (gres_ptr->gres_bit_alloc)
			FREE_NULL_BITMAP(gres_ptr->gres_bit_alloc[i]);
		if (gres_ptr->gres_bit_step_alloc)
			FREE_NULL_BITMAP(gres_ptr->gres_bit_step_alloc[i]);
	}
	xfree(gres_ptr->gres_bit_alloc);
	xfree(gres_ptr->gres_cnt_node_alloc);
	xfree(gres_ptr->gres_bit_step_alloc);
	xfree(gres_ptr->gres_cnt_step_alloc);
	if (gres_ptr->gres_bit_select) {
		for (i = 0; i < gres_ptr->total_node_cnt; i++)
			FREE_NULL_BITMAP(gres_ptr->gres_bit_select[i]);
		xfree(gres_ptr->gres_bit_select);
	}
	xfree(gres_ptr->gres_cnt_node_alloc);
	xfree(gres_ptr->gres_cnt_node_select);
	xfree(gres_ptr->gres_name);
	xfree(gres_ptr->type_name);
	xfree(gres_ptr);
}

static void _gres_job_list_delete(void *list_element)
{
	gres_state_t *gres_ptr;

	if (gres_plugin_init() != SLURM_SUCCESS)
		return;

	gres_ptr = (gres_state_t *) list_element;
	slurm_mutex_lock(&gres_context_lock);
	_job_state_delete(gres_ptr->gres_data);
	xfree(gres_ptr);
	slurm_mutex_unlock(&gres_context_lock);
}

static int _clear_cpus_per_gres(void *x, void *arg)
{
	gres_state_t *gres_ptr = (gres_state_t *) x;
	gres_job_state_t *job_gres_data;
	job_gres_data = (gres_job_state_t *) gres_ptr->gres_data;
	job_gres_data->cpus_per_gres = 0;
	return 0;
}
static int _clear_gres_per_job(void *x, void *arg)
{
	gres_state_t *gres_ptr = (gres_state_t *) x;
	gres_job_state_t *job_gres_data;
	job_gres_data = (gres_job_state_t *) gres_ptr->gres_data;
	job_gres_data->gres_per_job = 0;
	return 0;
}
static int _clear_gres_per_node(void *x, void *arg)
{
	gres_state_t *gres_ptr = (gres_state_t *) x;
	gres_job_state_t *job_gres_data;
	job_gres_data = (gres_job_state_t *) gres_ptr->gres_data;
	job_gres_data->gres_per_node = 0;
	return 0;
}
static int _clear_gres_per_socket(void *x, void *arg)
{
	gres_state_t *gres_ptr = (gres_state_t *) x;
	gres_job_state_t *job_gres_data;
	job_gres_data = (gres_job_state_t *) gres_ptr->gres_data;
	job_gres_data->gres_per_socket = 0;
	return 0;
}
static int _clear_gres_per_task(void *x, void *arg)
{
	gres_state_t *gres_ptr = (gres_state_t *) x;
	gres_job_state_t *job_gres_data;
	job_gres_data = (gres_job_state_t *) gres_ptr->gres_data;
	job_gres_data->gres_per_task = 0;
	return 0;
}
static int _clear_mem_per_gres(void *x, void *arg)
{
	gres_state_t *gres_ptr = (gres_state_t *) x;
	gres_job_state_t *job_gres_data;
	job_gres_data = (gres_job_state_t *) gres_ptr->gres_data;
	job_gres_data->mem_per_gres = 0;
	return 0;
}
static int _clear_total_gres(void *x, void *arg)
{
	gres_state_t *gres_ptr = (gres_state_t *) x;
	gres_job_state_t *job_gres_data;
	job_gres_data = (gres_job_state_t *) gres_ptr->gres_data;
	job_gres_data->total_gres = 0;
	return 0;
}

/*
 * Ensure consistency of gres_per_* options
 * Modify task and node count as needed for consistentcy with GRES options
 * RET -1 on failure, 0 on success
 */
static int _test_gres_cnt(gres_job_state_t *job_gres_data,
			  uint32_t *num_tasks,
			  uint32_t *min_nodes, uint32_t *max_nodes,
			  uint16_t *ntasks_per_node,
			  uint16_t *ntasks_per_socket,
			  uint16_t *sockets_per_node,
			  uint16_t *cpus_per_task)
{
	int req_nodes, req_tasks, req_tasks_per_node, req_tasks_per_socket;
	int req_sockets, req_cpus_per_task;
	uint16_t cpus_per_gres;

	/* Ensure gres_per_job >= gres_per_node >= gres_per_socket */
	if (job_gres_data->gres_per_job &&
	    ((job_gres_data->gres_per_node &&
	      (job_gres_data->gres_per_node > job_gres_data->gres_per_job)) ||
	     (job_gres_data->gres_per_task &&
	      (job_gres_data->gres_per_task > job_gres_data->gres_per_job)) ||
	     (job_gres_data->gres_per_socket &&
	      (job_gres_data->gres_per_socket >
	       job_gres_data->gres_per_job)))) {
		log_flag(GRES, "Failed to ensure gres_per_job >= gres_per_node >= gres_per_socket");
		return -1;
	}

	/* Ensure gres_per_job >= gres_per_task */
	if (job_gres_data->gres_per_node &&
	    ((job_gres_data->gres_per_task &&
	      (job_gres_data->gres_per_task > job_gres_data->gres_per_node)) ||
	     (job_gres_data->gres_per_socket &&
	      (job_gres_data->gres_per_socket >
	       job_gres_data->gres_per_node)))) {
		log_flag(GRES, "Failed to ensure gres_per_job >= gres_per_task");
		return -1;
	}

	/* gres_per_socket requires sockets-per-node count specification */
	if (job_gres_data->gres_per_socket) {
		if (*sockets_per_node == NO_VAL16)
			return -1;
	}

	/*
	 * Ensure gres_per_job is multiple of gres_per_node
	 * Ensure node count is consistent with GRES parameters
	 */
	if (job_gres_data->gres_per_job && job_gres_data->gres_per_node) {
		if (job_gres_data->gres_per_job % job_gres_data->gres_per_node){
			/* gres_per_job not multiple of gres_per_node */
			log_flag(GRES, "Failed to validate job spec, gres_per_job is not multiple of gres_per_node");
			return -1;
		}
		req_nodes = job_gres_data->gres_per_job /
			    job_gres_data->gres_per_node;
		if ((req_nodes < *min_nodes) || (req_nodes > *max_nodes)) {
			log_flag(GRES, "Failed to validate job spec. Based on gres_per_job and gres_per_node required nodes (%u) doesn't fall between min_nodes (%u) and max_nodes (%u) boundaries.", req_nodes, *min_nodes, *max_nodes);
			return -1;
		}
		*min_nodes = *max_nodes = req_nodes;
	}

	/*
	 * Ensure gres_per_node is multiple of gres_per_socket
	 * Ensure task count is consistent with GRES parameters
	 */
	if (job_gres_data->gres_per_node && job_gres_data->gres_per_socket) {
		if (job_gres_data->gres_per_node %
		    job_gres_data->gres_per_socket) {
			/* gres_per_node not multiple of gres_per_socket */
			log_flag(GRES, "Failed to validate job spec, gres_per_node not multiple of gres_per_socket.");
			return -1;
		}
		req_sockets = job_gres_data->gres_per_node /
			      job_gres_data->gres_per_socket;
		if (*sockets_per_node == NO_VAL16)
			*sockets_per_node = req_sockets;
		else if (*sockets_per_node != req_sockets) {
			log_flag(GRES, "Failed to validate job spec. Based on gres_per_node and gres_per_socket required number of sockets differ from --sockets-per-node.");
			return -1;
		}
	}
	/*
	 * Ensure gres_per_job is multiple of gres_per_task
	 * Ensure task count is consistent with GRES parameters
	 */
	if (job_gres_data->gres_per_task) {
		if(job_gres_data->gres_per_job) {
			if (job_gres_data->gres_per_job %
			    job_gres_data->gres_per_task) {
				/* gres_per_job not multiple of gres_per_task */
				log_flag(GRES, "Failed to validate job spec, gres_per_job not multiple of gres_per_task");
				return -1;
			}
			req_tasks = job_gres_data->gres_per_job /
				    job_gres_data->gres_per_task;
			if (*num_tasks == NO_VAL)
				*num_tasks = req_tasks;
			else if (*num_tasks != req_tasks) {
				log_flag(GRES, "Failed to validate job spec. Based on gres_per_job and gres_per_task number of requested tasks differ from -n/--ntasks.");
				return -1;
			}
		} else if (*num_tasks != NO_VAL) {
			job_gres_data->gres_per_job = *num_tasks *
						job_gres_data->gres_per_task;
		} else {
			log_flag(GRES, "Failed to validate job spec. gres_per_task used without either gres_per_job or -n/--ntasks is not allowed.");
			return -1;
		}
	}

	/*
	 * Ensure gres_per_node is multiple of gres_per_task
	 * Ensure tasks_per_node is consistent with GRES parameters
	 */
	if (job_gres_data->gres_per_node && job_gres_data->gres_per_task) {
		if (job_gres_data->gres_per_node %
		    job_gres_data->gres_per_task) {
			/* gres_per_node not multiple of gres_per_task */
			log_flag(GRES, "Failed to validate job spec, gres_per_node not multiple of gres_per_task.");
			return -1;
		}
		req_tasks_per_node = job_gres_data->gres_per_node /
				     job_gres_data->gres_per_task;
		if ((*ntasks_per_node == NO_VAL16) ||
		    (*ntasks_per_node == 0))
			*ntasks_per_node = req_tasks_per_node;
		else if (*ntasks_per_node != req_tasks_per_node) {
			log_flag(GRES, "Failed to validate job spec. Based on gres_per_node and gres_per_task requested number of tasks per node differ from --ntasks-per-node.");
			return -1;
		}
	}

	/*
	 * Ensure gres_per_socket is multiple of gres_per_task
	 * Ensure ntasks_per_socket is consistent with GRES parameters
	 */
	if (job_gres_data->gres_per_socket && job_gres_data->gres_per_task) {
		if (job_gres_data->gres_per_socket %
		    job_gres_data->gres_per_task) {
			/* gres_per_socket not multiple of gres_per_task */
			log_flag(GRES, "Failed to validate job spec, gres_per_socket not multiple of gres_per_task.");
			return -1;
		}
		req_tasks_per_socket = job_gres_data->gres_per_socket /
				       job_gres_data->gres_per_task;
		if ((*ntasks_per_socket == NO_VAL16) ||
		    (*ntasks_per_socket == 0))
			*ntasks_per_socket = req_tasks_per_socket;
		else if (*ntasks_per_socket != req_tasks_per_socket) {
			log_flag(GRES, "Failed to validate job spec. Based on gres_per_socket and gres_per_task requested number of tasks per sockets differ from --ntasks-per-socket.");
			return -1;
		}
	}

	/* Ensure that cpus_per_gres * gres_per_task == cpus_per_task */
	if (job_gres_data->cpus_per_gres)
		cpus_per_gres = job_gres_data->cpus_per_gres;
	else
		cpus_per_gres = job_gres_data->def_cpus_per_gres;
	if (cpus_per_gres && job_gres_data->gres_per_task) {
		req_cpus_per_task = cpus_per_gres *job_gres_data->gres_per_task;
		if ((*cpus_per_task == NO_VAL16) ||
		    (*cpus_per_task == 0))
			*cpus_per_task = req_cpus_per_task;
		else if (*cpus_per_task != req_cpus_per_task) {
			log_flag(GRES, "Failed to validate job spec. Based on cpus_per_gres and gres_per_task requested number of cpus differ from -c/--cpus-per-task.");
			return -1;
		}
	}

	/* Ensure tres_per_job >= node count */
	if (job_gres_data->gres_per_job) {
		if (job_gres_data->gres_per_job < *min_nodes) {
			log_flag(GRES, "Failed to validate job spec, gres_per_job < min_nodes (-N)");
			return -1;
		}
		if ((*max_nodes != NO_VAL) &&
		    (job_gres_data->gres_per_job < *max_nodes)) {
			*max_nodes = job_gres_data->gres_per_job;
		}
	}

	return 0;
}

/*
 * Translate a string, with optional suffix, into its equivalent numeric value
 * tok IN - the string to translate
 * value IN - numeric value
 * RET true if "tok" is a valid number
 */
static bool _is_valid_number(char *tok, unsigned long long int *value)
{
	unsigned long long int tmp_val;
	uint64_t mult;
	char *end_ptr = NULL;

	tmp_val = strtoull(tok, &end_ptr, 10);
	if (tmp_val == ULLONG_MAX)
		return false;
	if ((mult = suffix_mult(end_ptr)) == NO_VAL64)
		return false;
	tmp_val *= mult;
	*value = tmp_val;
	return true;
}

/*
 * Reentrant TRES specification parse logic
 * in_val IN - initial input string
 * type OUT -  must be xfreed by caller
 * cnt OUT - count of values
 * flags OUT - user flags (GRES_NO_CONSUME)
 * save_ptr IN/OUT - NULL on initial call, otherwise value from previous call
 * RET rc - error code
 */
static int _get_next_gres(char *in_val, char **type_ptr, int *context_inx_ptr,
			  uint64_t *cnt, uint16_t *flags, char **save_ptr)
{
	char *comma, *sep, *sep2, *name = NULL, *type = NULL;
	int i, rc = SLURM_SUCCESS;
	unsigned long long int value = 0;

	xassert(cnt);
	xassert(flags);
	xassert(save_ptr);
	*flags = 0;

	if (!in_val && (*save_ptr == NULL)) {
		return rc;
	}

	if (*save_ptr == NULL) {
		*save_ptr = in_val;
	}

next:	if (*save_ptr[0] == '\0') {	/* Empty input token */
		*save_ptr = NULL;
		goto fini;
	}

	name = xstrdup(*save_ptr);
	comma = strchr(name, ',');
	if (comma) {
		*save_ptr += (comma - name + 1);
		comma[0] = '\0';
	} else {
		*save_ptr += strlen(name);
	}

	if (name[0] == '\0') {
		/* Nothing but a comma */
		xfree(name);
		goto next;
	}

	sep = strchr(name, ':');
	if (sep) {
		sep[0] = '\0';
		sep++;
		sep2 = strchr(sep, ':');
		if (sep2) {
			sep2[0] = '\0';
			sep2++;
		}
	} else {
		sep2 = NULL;
	}

	if (sep2) {		/* Two colons */
		/* We have both type and count */
		if ((sep[0] == '\0') || (sep2[0] == '\0')) {
			/* Bad format (e.g. "gpu:tesla:" or "gpu::1") */
			rc = ESLURM_INVALID_GRES;
			goto fini;
		}
		type = xstrdup(sep);
		if (!_is_valid_number(sep2, &value)) {
			debug("%s: Invalid count value GRES %s:%s:%s", __func__,
			      name, type, sep2);
			rc = ESLURM_INVALID_GRES;
			goto fini;
		}
	} else if (sep) {	/* One colon */
		if (sep[0] == '\0') {
			/* Bad format (e.g. "gpu:") */
			rc = ESLURM_INVALID_GRES;
			goto fini;
		} else if (_is_valid_number(sep, &value)) {
			/* We have count, but no type */
			type = NULL;
		} else {
			/* We have type with implicit count of 1 */
			type = xstrdup(sep);
			value = 1;
		}
	} else {		/* No colon */
		/* We have no type and implicit count of 1 */
		type = NULL;
		value = 1;
	}
	if (value == 0) {
		xfree(name);
		xfree(type);
		goto next;
	}

	for (i = 0; i < gres_context_cnt; i++) {
		if (!xstrcmp(name, gres_context[i].gres_name) ||
		    !xstrncmp(name, gres_context[i].gres_name_colon,
			      gres_context[i].gres_name_colon_len))
			break;	/* GRES name match found */
	}
	if (i >= gres_context_cnt) {
		debug("%s: Failed to locate GRES %s", __func__, name);
		rc = ESLURM_INVALID_GRES;
		goto fini;
	}
	*context_inx_ptr = i;

fini:	if (rc != SLURM_SUCCESS) {
		*save_ptr = NULL;
		if (rc == ESLURM_INVALID_GRES) {
			info("%s: Invalid GRES job specification %s", __func__,
			     in_val);
		}
		xfree(type);
		*type_ptr = NULL;
	} else {
		*cnt = value;
		*type_ptr = type;
	}
	xfree(name);

	return rc;
}

/*
 * TRES specification parse logic
 * in_val IN - initial input string
 * cnt OUT - count of values
 * gres_list IN/OUT - where to search for (or add) new job TRES record
 * save_ptr IN/OUT - NULL on initial call, otherwise value from previous call
 * rc OUT - unchanged or an error code
 * RET gres - job record to set value in, found or created by this function
 */
static gres_job_state_t *_get_next_job_gres(char *in_val, uint64_t *cnt,
					    List gres_list, char **save_ptr,
					    int *rc)
{
	static char *prev_save_ptr = NULL;
	int context_inx = NO_VAL, my_rc = SLURM_SUCCESS;
	gres_job_state_t *job_gres_data = NULL;
	gres_state_t *gres_ptr;
	gres_key_t job_search_key;
	char *type = NULL, *name = NULL;
	uint16_t flags = 0;

	xassert(save_ptr);
	if (!in_val && (*save_ptr == NULL)) {
		return NULL;
	}

	if (*save_ptr == NULL) {
		prev_save_ptr = in_val;
	} else if (*save_ptr != prev_save_ptr) {
		error("%s: parsing error", __func__);
		my_rc = SLURM_ERROR;
		goto fini;
	}

	if (prev_save_ptr[0] == '\0') {	/* Empty input token */
		*save_ptr = NULL;
		return NULL;
	}

	if ((my_rc = _get_next_gres(in_val, &type, &context_inx,
				    cnt, &flags, &prev_save_ptr)) ||
	    (context_inx == NO_VAL)) {
		prev_save_ptr = NULL;
		goto fini;
	}

	/* Find the job GRES record */
	job_search_key.plugin_id = gres_context[context_inx].plugin_id;
	job_search_key.type_id = gres_plugin_build_id(type);
	gres_ptr = list_find_first(gres_list, _gres_find_job_by_key,
				   &job_search_key);

	if (gres_ptr) {
		job_gres_data = gres_ptr->gres_data;
	} else {
		job_gres_data = xmalloc(sizeof(gres_job_state_t));
		job_gres_data->gres_name =
			xstrdup(gres_context[context_inx].gres_name);
		job_gres_data->type_id = gres_plugin_build_id(type);
		job_gres_data->type_name = type;
		type = NULL;	/* String moved above */
		gres_ptr = xmalloc(sizeof(gres_state_t));
		gres_ptr->plugin_id = gres_context[context_inx].plugin_id;
		gres_ptr->gres_data = job_gres_data;
		list_append(gres_list, gres_ptr);
	}
	job_gres_data->flags = flags;

fini:	xfree(name);
	xfree(type);
	if (my_rc != SLURM_SUCCESS) {
		prev_save_ptr = NULL;
		if (my_rc == ESLURM_INVALID_GRES) {
			info("%s: Invalid GRES job specification %s", __func__,
			     in_val);
		}
		*rc = my_rc;
	}
	*save_ptr = prev_save_ptr;
	return job_gres_data;
}

/* Return true if job specification only includes cpus_per_gres or mem_per_gres
 * Return false if any other field set
 */
static bool _generic_job_state(gres_job_state_t *job_state)
{
	if (job_state->gres_per_job ||
	    job_state->gres_per_node ||
	    job_state->gres_per_socket ||
	    job_state->gres_per_task)
		return false;
	return true;
}

/*
 * Given a job's requested GRES configuration, validate it and build a GRES list
 * Note: This function can be used for a new request with gres_list==NULL or
 *	 used to update an existing job, in which case gres_list is a copy
 *	 of the job's original value (so we can clear fields as needed)
 * IN *tres* - job requested gres input string
 * IN/OUT num_tasks - requested task count, may be reset to provide
 *		      consistent gres_per_node/task values
 * IN/OUT min_nodes - requested minimum node count, may be reset to provide
 *		      consistent gres_per_node/task values
 * IN/OUT max_nodes - requested maximum node count, may be reset to provide
 *		      consistent gres_per_node/task values
 * IN/OUT ntasks_per_node - requested tasks_per_node count, may be reset to
 *		      provide consistent gres_per_node/task values
 * IN/OUT ntasks_per_socket - requested ntasks_per_socket count, may be reset to
 *		      provide consistent gres_per_node/task values
 * IN/OUT sockets_per_node - requested sockets_per_node count, may be reset to
 *		      provide consistent gres_per_socket/node values
 * IN/OUT cpus_per_task - requested cpus_per_task count, may be reset to
 *		      provide consistent gres_per_task/cpus_per_gres values
 * IN/OUT ntasks_per_tres - requested ntasks_per_tres count
 * OUT gres_list - List of GRES records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_job_state_validate(char *cpus_per_tres,
					  char *tres_freq,
					  char *tres_per_job,
					  char *tres_per_node,
					  char *tres_per_socket,
					  char *tres_per_task,
					  char *mem_per_tres,
					  uint32_t *num_tasks,
					  uint32_t *min_nodes,
					  uint32_t *max_nodes,
					  uint16_t *ntasks_per_node,
					  uint16_t *ntasks_per_socket,
					  uint16_t *sockets_per_node,
					  uint16_t *cpus_per_task,
					  uint16_t *ntasks_per_tres,
					  List *gres_list)
{
	typedef struct overlap_check {
		gres_job_state_t *without_model_state;
		uint32_t plugin_id;
		bool with_model;
		bool without_model;
	} overlap_check_t;
	overlap_check_t *over_list;
	int i, over_count = 0, rc = SLURM_SUCCESS, size;
	bool have_gres_gpu = false, have_gres_mps = false;
	bool overlap_merge = false;
	gres_state_t *gres_state;
	gres_job_state_t *job_gres_data;
	uint64_t cnt = 0;
	ListIterator iter;

	if (!cpus_per_tres && !tres_per_job && !tres_per_node &&
	    !tres_per_socket && !tres_per_task && !mem_per_tres &&
	    !ntasks_per_tres)
		return SLURM_SUCCESS;

	if ((tres_per_task || (*ntasks_per_tres != NO_VAL16)) &&
	    (*num_tasks == NO_VAL) && (*min_nodes != NO_VAL) &&
	    (*min_nodes == *max_nodes)) {
		/* Implicitly set task count */
		if (*ntasks_per_tres != NO_VAL16)
			*num_tasks = *min_nodes * *ntasks_per_tres;
		else if (*ntasks_per_node != NO_VAL16)
			*num_tasks = *min_nodes * *ntasks_per_node;
		else if (*cpus_per_task == NO_VAL16)
			*num_tasks = *min_nodes;
	}

	if ((rc = gres_plugin_init()) != SLURM_SUCCESS)
		return rc;

	if ((select_plugin_type != SELECT_TYPE_CONS_TRES) &&
	    (cpus_per_tres || tres_per_job || tres_per_socket ||
	     tres_per_task || mem_per_tres))
		return ESLURM_UNSUPPORTED_GRES;

	/*
	 * Clear fields as requested by job update (i.e. input value is "")
	 */
	if (*gres_list)
		(void) list_for_each(*gres_list, _clear_total_gres, NULL);
	if (*gres_list && cpus_per_tres && (cpus_per_tres[0] == '\0')) {
		(void) list_for_each(*gres_list, _clear_cpus_per_gres, NULL);
		cpus_per_tres = NULL;
	}
	if (*gres_list && tres_per_job && (tres_per_job[0] == '\0')) {
		(void) list_for_each(*gres_list, _clear_gres_per_job, NULL);
		tres_per_job = NULL;
	}
	if (*gres_list && tres_per_node && (tres_per_node[0] == '\0')) {
		(void) list_for_each(*gres_list, _clear_gres_per_node, NULL);
		tres_per_node = NULL;
	}
	if (*gres_list && tres_per_socket && (tres_per_socket[0] == '\0')) {
		(void) list_for_each(*gres_list, _clear_gres_per_socket, NULL);
		tres_per_socket = NULL;
	}
	if (*gres_list && tres_per_task && (tres_per_task[0] == '\0')) {
		(void) list_for_each(*gres_list, _clear_gres_per_task, NULL);
		tres_per_task = NULL;
	}
	if (*gres_list && mem_per_tres && (mem_per_tres[0] == '\0')) {
		(void) list_for_each(*gres_list, _clear_mem_per_gres, NULL);
		mem_per_tres = NULL;
	}

	/*
	 * Set new values as requested
	 */
	if (*gres_list == NULL)
		*gres_list = list_create(_gres_job_list_delete);
	slurm_mutex_lock(&gres_context_lock);
	if (cpus_per_tres) {
		char *in_val = cpus_per_tres, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->cpus_per_gres = cnt;
			in_val = NULL;
			job_gres_data->ntasks_per_gres = *ntasks_per_tres;
		}
	}
	if (tres_per_job) {
		char *in_val = tres_per_job, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->gres_per_job = cnt;
			in_val = NULL;
			job_gres_data->total_gres =
				MAX(job_gres_data->total_gres, cnt);
			job_gres_data->ntasks_per_gres = *ntasks_per_tres;
		}
	}
	if (tres_per_node) {
		char *in_val = tres_per_node, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->gres_per_node = cnt;
			in_val = NULL;
			if (*min_nodes != NO_VAL)
				cnt *= *min_nodes;
			job_gres_data->total_gres =
				MAX(job_gres_data->total_gres, cnt);
			job_gres_data->ntasks_per_gres = *ntasks_per_tres;
		}
	}
	if (tres_per_socket) {
		char *in_val = tres_per_socket, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->gres_per_socket = cnt;
			in_val = NULL;
			if ((*min_nodes != NO_VAL) &&
			    (*sockets_per_node != NO_VAL16)) {
				cnt *= (*min_nodes * *sockets_per_node);
			} else if ((*num_tasks != NO_VAL) &&
				   (*ntasks_per_socket != NO_VAL16)) {
				cnt *= ((*num_tasks + *ntasks_per_socket - 1) /
				        *ntasks_per_socket);
			}
			job_gres_data->total_gres =
				MAX(job_gres_data->total_gres, cnt);
			job_gres_data->ntasks_per_gres = *ntasks_per_tres;
		}
	}
	if (tres_per_task) {
		char *in_val = tres_per_task, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->gres_per_task = cnt;
			in_val = NULL;
			if (*num_tasks != NO_VAL)
				cnt *= *num_tasks;
			job_gres_data->total_gres =
				MAX(job_gres_data->total_gres, cnt);
			job_gres_data->ntasks_per_gres = *ntasks_per_tres;
		}
	}
	if (mem_per_tres) {
		char *in_val = mem_per_tres, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->mem_per_gres = cnt;
			in_val = NULL;
			job_gres_data->ntasks_per_gres = *ntasks_per_tres;
		}
	}

	/* *num_tasks and *ntasks_per_tres could be 0 on requeue */
	if (!ntasks_per_tres || !*ntasks_per_tres ||
	    (*ntasks_per_tres == NO_VAL16)) {
		/* do nothing */
	} else if (list_count(*gres_list) != 0) {
		/* Set num_tasks = gpus * ntasks/gpu */
		uint64_t gpus = _get_job_gres_list_cnt(*gres_list, "gpu", NULL);
		if (gpus != NO_VAL64)
			*num_tasks = gpus * *ntasks_per_tres;
		else
			error("%s: Can't set num_tasks = gpus * *ntasks_per_tres because there are no allocated GPUs",
			      __func__);
	} else if (*num_tasks && (*num_tasks != NO_VAL)) {
		/*
		 * If job_gres_list empty, and ntasks_per_tres is specified,
		 * then derive GPUs according to how many tasks there are.
		 * GPU GRES = [ntasks / (ntasks_per_tres)]
		 * For now, only generate type-less GPUs.
		 */
		uint32_t gpus = *num_tasks / *ntasks_per_tres;
		char *save_ptr = NULL, *gres = NULL, *in_val;
		xstrfmtcat(gres, "gpu:%u", gpus);
		in_val = gres;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->ntasks_per_gres = *ntasks_per_tres;
			/* Simulate a tres_per_job specification */
			job_gres_data->gres_per_job = cnt;
			job_gres_data->total_gres =
				MAX(job_gres_data->total_gres, cnt);
			in_val = NULL;
		}
		if (list_count(*gres_list) == 0)
			error("%s: Failed to add generated GRES %s (via ntasks_per_tres) to gres_list",
			      __func__, gres);
		xfree(gres);
	} else {
		error("%s: --ntasks-per-tres needs either a GRES GPU specification or a node/ntask specification",
		      __func__);
	}

	slurm_mutex_unlock(&gres_context_lock);

	if (rc != SLURM_SUCCESS)
		return rc;
	size = list_count(*gres_list);
	if (size == 0) {
		FREE_NULL_LIST(*gres_list);
		return rc;
	}

	/*
	 * Check for record overlap (e.g. "gpu:2,gpu:tesla:1")
	 * Ensure tres_per_job >= tres_per_node >= tres_per_socket
	 */
	over_list = xcalloc(size, sizeof(overlap_check_t));
	iter = list_iterator_create(*gres_list);
	while ((gres_state = (gres_state_t *) list_next(iter))) {
		job_gres_data = (gres_job_state_t *) gres_state->gres_data;
		if (_test_gres_cnt(job_gres_data, num_tasks, min_nodes,
				   max_nodes, ntasks_per_node,
				   ntasks_per_socket, sockets_per_node,
				   cpus_per_task) != 0) {
			rc = ESLURM_INVALID_GRES;
			break;
		}
		if (!have_gres_gpu && !xstrcmp(job_gres_data->gres_name, "gpu"))
			have_gres_gpu = true;
		if (!xstrcmp(job_gres_data->gres_name, "mps")) {
			have_gres_mps = true;
			/*
			 * gres/mps only supports a per-node count,
			 * set either explicitly or implicitly.
			 */
			if (job_gres_data->gres_per_job &&
			    (*max_nodes != 1)) {
				rc = ESLURM_INVALID_GRES;
				break;
			}
			if (job_gres_data->gres_per_socket &&
			    (*sockets_per_node != 1)) {
				rc = ESLURM_INVALID_GRES;
				break;
			}
			if (job_gres_data->gres_per_task && (*num_tasks != 1)) {
				rc = ESLURM_INVALID_GRES;
				break;
			}
		}
		if (have_gres_gpu && have_gres_mps) {
			rc = ESLURM_INVALID_GRES;
			break;
		}

		for (i = 0; i < over_count; i++) {
			if (over_list[i].plugin_id == gres_state->plugin_id)
				break;
		}
		if (i >= over_count) {
			over_list[over_count++].plugin_id =
				gres_state->plugin_id;
			if (job_gres_data->type_name) {
				over_list[i].with_model = true;
			} else {
				over_list[i].without_model = true;
				over_list[i].without_model_state =
					job_gres_data;
			}
		} else if (job_gres_data->type_name) {
			over_list[i].with_model = true;
			if (over_list[i].without_model)
				overlap_merge = true;
		} else {
			over_list[i].without_model = true;
			over_list[i].without_model_state = job_gres_data;
			if (over_list[i].with_model)
				overlap_merge = true;
		}
	}
	if (have_gres_mps && (rc == SLURM_SUCCESS) && tres_freq &&
	    strstr(tres_freq, "gpu")) {
		rc = ESLURM_INVALID_GRES;
	}

	if (overlap_merge) {	/* Merge generic data if possible */
		uint16_t cpus_per_gres;
		uint64_t mem_per_gres;
		for (i = 0; i < over_count; i++) {
			if (!over_list[i].with_model ||
			    !over_list[i].without_model_state)
				continue;
			if (!_generic_job_state(
					over_list[i].without_model_state)) {
				rc = ESLURM_INVALID_GRES_TYPE;
				break;
			}
			/* Propagate generic parameters */
			cpus_per_gres =
				over_list[i].without_model_state->cpus_per_gres;
			mem_per_gres =
				over_list[i].without_model_state->mem_per_gres;
			list_iterator_reset(iter);
			while ((gres_state = (gres_state_t *)list_next(iter))) {
				job_gres_data = (gres_job_state_t *)
					gres_state->gres_data;
				if (over_list[i].plugin_id !=
				    gres_state->plugin_id)
					continue;
				if (job_gres_data ==
				    over_list[i].without_model_state) {
					list_remove(iter);
					continue;
				}
				if (job_gres_data->cpus_per_gres == 0) {
					job_gres_data->cpus_per_gres =
						cpus_per_gres;
				}
				if (job_gres_data->mem_per_gres == 0) {
					job_gres_data->mem_per_gres =
						mem_per_gres;
				}
			}
		}
	}
	list_iterator_destroy(iter);
	xfree(over_list);

	return rc;
}

/*
 * Determine if a job's specified GRES can be supported. This is designed to
 * prevent the running of a job using the GRES options only supported by the
 * select/cons_tres plugin when switching (on slurmctld restart) from the
 * cons_tres plugin to any other select plugin.
 *
 * IN gres_list - List of GRES records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_job_revalidate(List gres_list)
{
	gres_state_t *gres_state;
	gres_job_state_t *job_gres_data;
	ListIterator iter;
	int rc = SLURM_SUCCESS;

	if (!gres_list || (select_plugin_type == SELECT_TYPE_CONS_TRES))
		return SLURM_SUCCESS;

	iter = list_iterator_create(gres_list);
	while ((gres_state = (gres_state_t *) list_next(iter))) {
		job_gres_data = (gres_job_state_t *) gres_state->gres_data;
		if (job_gres_data->gres_per_job ||
		    job_gres_data->gres_per_socket ||
		    job_gres_data->gres_per_task) {
			rc = ESLURM_UNSUPPORTED_GRES;
			break;
		}
	}
	list_iterator_destroy(iter);

	return rc;
}

/*
 * Return TRUE if any of this job's GRES has a populated gres_bit_alloc element.
 * This indicates the allocated GRES has a File configuration parameter and is
 * tracking individual file assignments.
 */
static bool _job_has_gres_bits(List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *gres_ptr;
	gres_job_state_t *job_gres_ptr;
	bool rc = false;
	int i;

	if (!job_gres_list)
		return false;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		job_gres_ptr = gres_ptr->gres_data;
		if (!job_gres_ptr)
			continue;
		for (i = 0; i < job_gres_ptr->node_cnt; i++) {
			if (job_gres_ptr->gres_bit_alloc &&
			    job_gres_ptr->gres_bit_alloc[i]) {
				rc = true;
				break;
			}
		}
		if (rc)
			break;
	}
	list_iterator_destroy(job_gres_iter);

	return rc;
}

/*
 * Return count of configured GRES.
 * NOTE: For gres/mps return count of gres/gpu
 */
static int _get_node_gres_cnt(List node_gres_list, uint32_t plugin_id)
{
	ListIterator node_gres_iter;
	gres_node_state_t *gres_node_ptr;
	gres_state_t *gres_ptr;
	int gres_cnt = 0;

	if (!node_gres_list)
		return 0;

	if (plugin_id == mps_plugin_id)
		plugin_id = gpu_plugin_id;
	node_gres_iter = list_iterator_create(node_gres_list);
        while ((gres_ptr = (gres_state_t *) list_next(node_gres_iter))) {
		if (gres_ptr->plugin_id != plugin_id)
			continue;
		gres_node_ptr = (gres_node_state_t *) gres_ptr->gres_data;
		gres_cnt = (int) gres_node_ptr->gres_cnt_config;
		break;
	}
	list_iterator_destroy(node_gres_iter);

	return gres_cnt;
}

/*
 * Return TRUE if the identified node in the job allocation can satisfy the
 * job's GRES specification without change in its bitmaps. In other words,
 * return FALSE if the job allocation identifies specific GRES devices and the
 * count of those devices on this node has changed.
 *
 * IN job_gres_list - List of GRES records for this job to track usage
 * IN node_inx - zero-origin index into this job's node allocation
 * IN node_gres_list - List of GRES records for this node
 */
static bool _validate_node_gres_cnt(uint32_t job_id, List job_gres_list,
				    int node_inx, List node_gres_list,
				    char *node_name)
{
	ListIterator job_gres_iter;
	gres_state_t *gres_ptr;
	gres_job_state_t *job_gres_ptr;
	bool rc = true;
	int job_gres_cnt, node_gres_cnt;

	if (!job_gres_list)
		return true;

	(void) gres_plugin_init();

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		job_gres_ptr = gres_ptr->gres_data;
		if (!job_gres_ptr || !job_gres_ptr->gres_bit_alloc)
			continue;
		if ((node_inx >= job_gres_ptr->node_cnt) ||
		    !job_gres_ptr->gres_bit_alloc[node_inx])
			continue;
		job_gres_cnt = bit_size(job_gres_ptr->gres_bit_alloc[node_inx]);
		node_gres_cnt = _get_node_gres_cnt(node_gres_list,
						   gres_ptr->plugin_id);
		if (job_gres_cnt != node_gres_cnt) {
			error("%s: Killing job %u: gres/%s count mismatch on node "
			      "%s (%d != %d)",
			      __func__, job_id, job_gres_ptr->gres_name,
			      node_name, job_gres_cnt, node_gres_cnt);
			rc = false;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);

	return rc;
}

/*
 * Determine if a job's specified GRES are currently valid. This is designed to
 * manage jobs allocated GRES which are either no longer supported or a GRES
 * configured with the "File" option in gres.conf where the count has changed,
 * in which case we don't know how to map the job's old GRES bitmap onto the
 * current GRES bitmaps.
 *
 * IN job_id - ID of job being validated (used for logging)
 * IN job_gres_list - List of GRES records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_job_revalidate2(uint32_t job_id, List job_gres_list,
				       bitstr_t *node_bitmap)
{
	node_record_t *node_ptr;
	int rc = SLURM_SUCCESS;
	int i_first, i_last, i;
	int node_inx = -1;

	if (!job_gres_list || !node_bitmap ||
	    !_job_has_gres_bits(job_gres_list))
		return SLURM_SUCCESS;

	i_first = bit_ffs(node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr + i;
		node_inx++;
		if (!_validate_node_gres_cnt(job_id, job_gres_list, node_inx,
					     node_ptr->gres_list,
					     node_ptr->name)) {
			rc = ESLURM_INVALID_GRES;
			break;
		}
	}

	return rc;
}

/*
 * Find a sock_gres_t record in a list by matching the plugin_id and type_id
 *	from a gres_state_t job record
 * IN x - a sock_gres_t record to test
 * IN key - the gres_state_t record (from a job) we want to match
 * RET 1 on match, otherwise 0
 */
static int _find_sock_by_job_gres(void *x, void *key)
{
	sock_gres_t *sock_data = (sock_gres_t *) x;
	gres_state_t *job_gres_state = (gres_state_t *) key;
	gres_job_state_t *job_data;

	job_data = (gres_job_state_t *) job_gres_state->gres_data;
	if ((sock_data->plugin_id == job_gres_state->plugin_id) &&
	    (sock_data->type_id   == job_data->type_id))
		return 1;
	return 0;
}

/*
 * Find a gres_state_t job record in a list by matching the plugin_id and
 *	type_id from a sock_gres_t record
 * IN x - a gres_state_t record (from a job) to test
 * IN key - the sock_gres_t record we want to match
 * RET 1 on match, otherwise 0
 */
static int _find_job_by_sock_gres(void *x, void *key)
{
	gres_state_t *job_gres_state = (gres_state_t *) x;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data = (sock_gres_t *) key;

	job_data = (gres_job_state_t *) job_gres_state->gres_data;
	if ((sock_data->plugin_id == job_gres_state->plugin_id) &&
	    (sock_data->type_id   == job_data->type_id))
		return 1;
	return 0;
}

/*
 * Clear GRES allocation info for all job GRES at start of scheduling cycle
 * Return TRUE if any gres_per_job constraints to satisfy
 */
extern bool gres_plugin_job_sched_init(List job_gres_list)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	bool rc = false;

	if (!job_gres_list)
		return rc;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = (gres_state_t *) list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (!job_data->gres_per_job)
			continue;
		job_data->total_gres = 0;
		rc = true;
	}
	list_iterator_destroy(iter);

	return rc;
}

/*
 * Return TRUE if all gres_per_job specifications are satisfied
 */
extern bool gres_plugin_job_sched_test(List job_gres_list, uint32_t job_id)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	bool rc = true;

	if (!job_gres_list)
		return rc;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = (gres_state_t *) list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (job_data->gres_per_job &&
		    (job_data->gres_per_job > job_data->total_gres)) {
			rc = false;
			break;
		}
	}
	list_iterator_destroy(iter);

	return rc;
}

/*
 * Return TRUE if all gres_per_job specifications will be satisfied with
 *	the addtitional resources provided by a single node
 * IN job_gres_list - List of job's GRES requirements (job_gres_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 * IN job_id - The job being tested
 */
extern bool gres_plugin_job_sched_test2(List job_gres_list, List sock_gres_list,
					uint32_t job_id)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data;
	bool rc = true;

	if (!job_gres_list)
		return rc;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = (gres_state_t *) list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if ((job_data->gres_per_job == 0) ||
		    (job_data->gres_per_job < job_data->total_gres))
			continue;
		sock_data = list_find_first(sock_gres_list,
					    _find_sock_by_job_gres,
					    job_gres_state);
		if (!sock_data ||
		    (job_data->gres_per_job >
		     (job_data->total_gres + sock_data->total_cnt))) {
			rc = false;
			break;
		}
	}
	list_iterator_destroy(iter);

	return rc;
}

/*
 * Update a job's total_gres counter as we add a node to potential allocation
 * IN job_gres_list - List of job's GRES requirements (job_gres_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 * IN avail_cpus - CPUs currently available on this node
 */
extern void gres_plugin_job_sched_add(List job_gres_list, List sock_gres_list,
				      uint16_t avail_cpus)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data;
	uint64_t gres_limit;

	if (!job_gres_list)
		return;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = (gres_state_t *) list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (!job_data->gres_per_job)	/* Don't care about totals */
			continue;
		sock_data = list_find_first(sock_gres_list,
					    _find_sock_by_job_gres,
					    job_gres_state);
		if (!sock_data)		/* None of this GRES available */
			continue;
		if (job_data->cpus_per_gres) {
			gres_limit = avail_cpus / job_data->cpus_per_gres;
			gres_limit = MIN(gres_limit, sock_data->total_cnt);
		} else
			gres_limit = sock_data->total_cnt;
		job_data->total_gres += gres_limit;
	}
	list_iterator_destroy(iter);
}

/*
 * Create/update List GRES that can be made available on the specified node
 * IN/OUT consec_gres - List of sock_gres_t that can be made available on
 *			a set of nodes
 * IN job_gres_list - List of job's GRES requirements (gres_job_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 */
extern void gres_plugin_job_sched_consec(List *consec_gres, List job_gres_list,
					 List sock_gres_list)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data, *consec_data;

	if (!job_gres_list)
		return;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = (gres_state_t *) list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (!job_data->gres_per_job)	/* Don't care about totals */
			continue;
		sock_data = list_find_first(sock_gres_list,
					    _find_sock_by_job_gres,
					    job_gres_state);
		if (!sock_data)		/* None of this GRES available */
			continue;
		if (*consec_gres == NULL)
			*consec_gres = list_create(_sock_gres_del);
		consec_data = list_find_first(*consec_gres,
					      _find_sock_by_job_gres,
					      job_gres_state);
		if (!consec_data) {
			consec_data = xmalloc(sizeof(sock_gres_t));
			consec_data->plugin_id = sock_data->plugin_id;
			consec_data->type_id   = sock_data->type_id;
			list_append(*consec_gres, consec_data);
		}
		consec_data->total_cnt += sock_data->total_cnt;
	}
	list_iterator_destroy(iter);
}

/*
 * Determine if the additional sock_gres_list resources will result in
 * satisfying the job's gres_per_job constraints
 * IN job_gres_list - job's GRES requirements
 * IN sock_gres_list - available GRES in a set of nodes, data structure built
 *		       by gres_plugin_job_sched_consec()
 */
extern bool gres_plugin_job_sched_sufficient(List job_gres_list,
					     List sock_gres_list)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data;
	bool rc = true;

	if (!job_gres_list)
		return true;
	if (!sock_gres_list)
		return false;

	iter = list_iterator_create(job_gres_list);
	while ((job_gres_state = (gres_state_t *) list_next(iter))) {
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (!job_data->gres_per_job)	/* Don't care about totals */
			continue;
		if (job_data->total_gres >= job_data->gres_per_job)
			continue;
		sock_data = list_find_first(sock_gres_list,
					    _find_sock_by_job_gres,
					    job_gres_state);
		if (!sock_data)	{	/* None of this GRES available */
			rc = false;
			break;
		}
		if ((job_data->total_gres + sock_data->total_cnt) <
		    job_data->gres_per_job) {
			rc = false;
			break;
		}
	}
	list_iterator_destroy(iter);

	return rc;
}

/*
 * Given a List of sock_gres_t entries, return a string identifying the
 * count of each GRES available on this set of nodes
 * IN sock_gres_list - count of GRES available in this group of nodes
 * IN job_gres_list - job GRES specification, used only to get GRES name/type
 * RET xfree the returned string
 */
extern char *gres_plugin_job_sched_str(List sock_gres_list, List job_gres_list)
{
	ListIterator iter;
	sock_gres_t *sock_data;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	char *out_str = NULL, *sep;

	if (!sock_gres_list)
		return NULL;

	iter = list_iterator_create(sock_gres_list);
	while ((sock_data = (sock_gres_t *) list_next(iter))) {
		job_gres_state = list_find_first(job_gres_list,
					   _find_job_by_sock_gres, sock_data);
		if (!job_gres_state) {	/* Should never happen */
			error("%s: Could not find job GRES for type %u:%u",
			      __func__, sock_data->plugin_id,
			      sock_data->type_id);
			continue;
		}
		job_data = (gres_job_state_t *) job_gres_state->gres_data;
		if (out_str)
			sep = ",";
		else
			sep = "GRES:";
		if (job_data->type_name) {
			xstrfmtcat(out_str, "%s%s:%s:%"PRIu64, sep,
				   job_data->gres_name, job_data->type_name,
				   sock_data->total_cnt);
		} else {
			xstrfmtcat(out_str, "%s%s:%"PRIu64, sep,
				   job_data->gres_name, sock_data->total_cnt);
		}
	}
	list_iterator_destroy(iter);

	return out_str;
}

/*
 * Create a (partial) copy of a job's gres state for job binding
 * IN gres_list - List of Gres records for this job to track usage
 * RET The copy or NULL on failure
 * NOTE: Only job details are copied, NOT the job step details
 */
extern List gres_plugin_job_state_dup(List gres_list)
{
	return gres_plugin_job_state_extract(gres_list, -1);
}

/* Copy gres_job_state_t record for ALL nodes */
static void *_job_state_dup(void *gres_data)
{

	int i;
	gres_job_state_t *gres_ptr = (gres_job_state_t *) gres_data;
	gres_job_state_t *new_gres_ptr;

	if (gres_ptr == NULL)
		return NULL;

	new_gres_ptr = xmalloc(sizeof(gres_job_state_t));
	new_gres_ptr->cpus_per_gres	= gres_ptr->cpus_per_gres;
	new_gres_ptr->gres_name		= xstrdup(gres_ptr->gres_name);
	new_gres_ptr->gres_per_job	= gres_ptr->gres_per_job;
	new_gres_ptr->gres_per_node	= gres_ptr->gres_per_node;
	new_gres_ptr->gres_per_socket	= gres_ptr->gres_per_socket;
	new_gres_ptr->gres_per_task	= gres_ptr->gres_per_task;
	new_gres_ptr->mem_per_gres	= gres_ptr->mem_per_gres;
	new_gres_ptr->node_cnt		= gres_ptr->node_cnt;
	new_gres_ptr->total_gres	= gres_ptr->total_gres;
	new_gres_ptr->type_id		= gres_ptr->type_id;
	new_gres_ptr->type_name		= xstrdup(gres_ptr->type_name);

	if (gres_ptr->gres_cnt_node_alloc) {
		i = sizeof(uint64_t) * gres_ptr->node_cnt;
		new_gres_ptr->gres_cnt_node_alloc = xmalloc(i);
		memcpy(new_gres_ptr->gres_cnt_node_alloc,
		       gres_ptr->gres_cnt_node_alloc, i);
	}
	if (gres_ptr->gres_bit_alloc) {
		new_gres_ptr->gres_bit_alloc = xcalloc(gres_ptr->node_cnt,
						       sizeof(bitstr_t *));
		for (i = 0; i < gres_ptr->node_cnt; i++) {
			if (gres_ptr->gres_bit_alloc[i] == NULL)
				continue;
			new_gres_ptr->gres_bit_alloc[i] =
				bit_copy(gres_ptr->gres_bit_alloc[i]);
		}
	}
	return new_gres_ptr;
}

/* Copy gres_job_state_t record for one specific node */
static void *_job_state_dup2(void *gres_data, int node_index)
{

	gres_job_state_t *gres_ptr = (gres_job_state_t *) gres_data;
	gres_job_state_t *new_gres_ptr;

	if (gres_ptr == NULL)
		return NULL;

	new_gres_ptr = xmalloc(sizeof(gres_job_state_t));
	new_gres_ptr->cpus_per_gres	= gres_ptr->cpus_per_gres;
	new_gres_ptr->gres_name		= xstrdup(gres_ptr->gres_name);
	new_gres_ptr->gres_per_job	= gres_ptr->gres_per_job;
	new_gres_ptr->gres_per_node	= gres_ptr->gres_per_node;
	new_gres_ptr->gres_per_socket	= gres_ptr->gres_per_socket;
	new_gres_ptr->gres_per_task	= gres_ptr->gres_per_task;
	new_gres_ptr->mem_per_gres	= gres_ptr->mem_per_gres;
	new_gres_ptr->node_cnt		= 1;
	new_gres_ptr->total_gres	= gres_ptr->total_gres;
	new_gres_ptr->type_id		= gres_ptr->type_id;
	new_gres_ptr->type_name		= xstrdup(gres_ptr->type_name);

	if (gres_ptr->gres_cnt_node_alloc) {
		new_gres_ptr->gres_cnt_node_alloc = xmalloc(sizeof(uint64_t));
		new_gres_ptr->gres_cnt_node_alloc[0] =
		       gres_ptr->gres_cnt_node_alloc[node_index];
	}
	if (gres_ptr->gres_bit_alloc && gres_ptr->gres_bit_alloc[node_index]) {
		new_gres_ptr->gres_bit_alloc	= xmalloc(sizeof(bitstr_t *));
		new_gres_ptr->gres_bit_alloc[0] =
				bit_copy(gres_ptr->gres_bit_alloc[node_index]);
	}
	return new_gres_ptr;
}

/*
 * Create a (partial) copy of a job's gres state for a particular node index
 * IN gres_list - List of Gres records for this job to track usage
 * IN node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
extern List gres_plugin_job_state_extract(List gres_list, int node_index)
{
	ListIterator gres_iter;
	gres_state_t *gres_ptr, *new_gres_state;
	List new_gres_list = NULL;
	void *new_gres_data;

	if (gres_list == NULL)
		return new_gres_list;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		if (node_index == -1)
			new_gres_data = _job_state_dup(gres_ptr->gres_data);
		else {
			new_gres_data = _job_state_dup2(gres_ptr->gres_data,
							node_index);
		}
		if (new_gres_data == NULL)
			break;
		if (new_gres_list == NULL) {
			new_gres_list = list_create(_gres_job_list_delete);
		}
		new_gres_state = xmalloc(sizeof(gres_state_t));
		new_gres_state->plugin_id = gres_ptr->plugin_id;
		new_gres_state->gres_data = new_gres_data;
		list_append(new_gres_list, new_gres_state);
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return new_gres_list;
}

/*
 * Pack a job's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_plugin_job_config_validate()
 * IN/OUT buffer - location to write state to
 * IN job_id - job's ID
 * IN details - if set then pack job step allocation details (only needed to
 *	 	save/restore job state, not needed in job credential for
 *		slurmd task binding)
 *
 * NOTE: A job's allocation to steps is not recorded here, but recovered with
 *	 the job step state information upon slurmctld restart.
 */
extern int gres_plugin_job_state_pack(List gres_list, Buf buffer,
				      uint32_t job_id, bool details,
				      uint16_t protocol_version)
{
	int i, rc = SLURM_SUCCESS;
	uint32_t top_offset, tail_offset;
	uint32_t magic = GRES_MAGIC;
	uint16_t rec_cnt = 0;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;
	gres_job_state_t *gres_job_ptr;

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	if (gres_list == NULL)
		return rc;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		gres_job_ptr = (gres_job_state_t *) gres_ptr->gres_data;

		if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			pack16(gres_job_ptr->cpus_per_gres, buffer);
			pack16(gres_job_ptr->flags, buffer);
			pack64(gres_job_ptr->gres_per_job, buffer);
			pack64(gres_job_ptr->gres_per_node, buffer);
			pack64(gres_job_ptr->gres_per_socket, buffer);
			pack64(gres_job_ptr->gres_per_task, buffer);
			pack64(gres_job_ptr->mem_per_gres, buffer);
			pack16(gres_job_ptr->ntasks_per_gres, buffer);
			pack64(gres_job_ptr->total_gres, buffer);
			packstr(gres_job_ptr->type_name, buffer);
			pack32(gres_job_ptr->node_cnt, buffer);

			if (gres_job_ptr->gres_cnt_node_alloc) {
				pack8((uint8_t) 1, buffer);
				pack64_array(gres_job_ptr->gres_cnt_node_alloc,
					     gres_job_ptr->node_cnt, buffer);
			} else {
				pack8((uint8_t) 0, buffer);
			}

			if (gres_job_ptr->gres_bit_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					pack_bit_str_hex(gres_job_ptr->
							 gres_bit_alloc[i],
							 buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (details && gres_job_ptr->gres_bit_step_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					pack_bit_str_hex(gres_job_ptr->
							 gres_bit_step_alloc[i],
							 buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (details && gres_job_ptr->gres_cnt_step_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					pack64(gres_job_ptr->
					       gres_cnt_step_alloc[i],
					       buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			rec_cnt++;
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			pack16(gres_job_ptr->cpus_per_gres, buffer);
			pack16(gres_job_ptr->flags, buffer);
			pack64(gres_job_ptr->gres_per_job, buffer);
			pack64(gres_job_ptr->gres_per_node, buffer);
			pack64(gres_job_ptr->gres_per_socket, buffer);
			pack64(gres_job_ptr->gres_per_task, buffer);
			pack64(gres_job_ptr->mem_per_gres, buffer);
			pack64(gres_job_ptr->total_gres, buffer);
			packstr(gres_job_ptr->type_name, buffer);
			pack32(gres_job_ptr->node_cnt, buffer);

			if (gres_job_ptr->gres_cnt_node_alloc) {
				pack8((uint8_t) 1, buffer);
				pack64_array(gres_job_ptr->gres_cnt_node_alloc,
					     gres_job_ptr->node_cnt, buffer);
			} else {
				pack8((uint8_t) 0, buffer);
			}

			if (gres_job_ptr->gres_bit_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					pack_bit_str_hex(gres_job_ptr->
							 gres_bit_alloc[i],
							 buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (details && gres_job_ptr->gres_bit_step_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					pack_bit_str_hex(gres_job_ptr->
							 gres_bit_step_alloc[i],
							 buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (details && gres_job_ptr->gres_cnt_step_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					pack64(gres_job_ptr->
					       gres_cnt_step_alloc[i],
					       buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			rec_cnt++;
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	tail_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, top_offset);
	pack16(rec_cnt, buffer);
	set_buf_offset(buffer, tail_offset);

	return rc;
}

/*
 * Unpack a job's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_job_state_pack()
 * IN/OUT buffer - location to read state from
 * IN job_id - job's ID
 */
extern int gres_plugin_job_state_unpack(List *gres_list, Buf buffer,
					uint32_t job_id,
					uint16_t protocol_version)
{
	int i = 0, rc;
	uint32_t magic = 0, plugin_id = 0, utmp32 = 0;
	uint16_t rec_cnt = 0;
	uint8_t  has_more = 0;
	gres_state_t *gres_ptr;
	gres_job_state_t *gres_job_ptr = NULL;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_job_list_delete);
	}

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;

		if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_job_ptr = xmalloc(sizeof(gres_job_state_t));
			safe_unpack16(&gres_job_ptr->cpus_per_gres, buffer);
			safe_unpack16(&gres_job_ptr->flags, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_job, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_node, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_socket, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_task, buffer);
			safe_unpack64(&gres_job_ptr->mem_per_gres, buffer);
			safe_unpack16(&gres_job_ptr->ntasks_per_gres, buffer);
			safe_unpack64(&gres_job_ptr->total_gres, buffer);
			safe_unpackstr_xmalloc(&gres_job_ptr->type_name,
					       &utmp32, buffer);
			gres_job_ptr->type_id =
				gres_plugin_build_id(gres_job_ptr->type_name);
			safe_unpack32(&gres_job_ptr->node_cnt, buffer);
			if (gres_job_ptr->node_cnt > NO_VAL)
				goto unpack_error;

			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_unpack64_array(
					&gres_job_ptr->gres_cnt_node_alloc,
					&utmp32, buffer);
			}

			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_xcalloc(gres_job_ptr->gres_bit_alloc,
					     gres_job_ptr->node_cnt,
					     sizeof(bitstr_t *));
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_job_ptr->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_xcalloc(gres_job_ptr->gres_bit_step_alloc,
					     gres_job_ptr->node_cnt,
					     sizeof(bitstr_t *));
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_job_ptr->
							   gres_bit_step_alloc[i],
							   buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_xcalloc(gres_job_ptr->gres_cnt_step_alloc,
					     gres_job_ptr->node_cnt,
					     sizeof(uint64_t));
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					safe_unpack64(&gres_job_ptr->
						      gres_cnt_step_alloc[i],
						      buffer);
				}
			}
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_job_ptr = xmalloc(sizeof(gres_job_state_t));
			safe_unpack16(&gres_job_ptr->cpus_per_gres, buffer);
			safe_unpack16(&gres_job_ptr->flags, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_job, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_node, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_socket, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_task, buffer);
			safe_unpack64(&gres_job_ptr->mem_per_gres, buffer);
			gres_job_ptr->ntasks_per_gres = NO_VAL16;
			safe_unpack64(&gres_job_ptr->total_gres, buffer);
			safe_unpackstr_xmalloc(&gres_job_ptr->type_name,
					       &utmp32, buffer);
			gres_job_ptr->type_id =
				gres_plugin_build_id(gres_job_ptr->type_name);
			safe_unpack32(&gres_job_ptr->node_cnt, buffer);
			if (gres_job_ptr->node_cnt > NO_VAL)
				goto unpack_error;

			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_unpack64_array(
					&gres_job_ptr->gres_cnt_node_alloc,
					&utmp32, buffer);
			}

			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_xcalloc(gres_job_ptr->gres_bit_alloc,
					     gres_job_ptr->node_cnt,
					     sizeof(bitstr_t *));
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_job_ptr->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_xcalloc(gres_job_ptr->gres_bit_step_alloc,
					     gres_job_ptr->node_cnt,
					     sizeof(bitstr_t *));
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_job_ptr->
							   gres_bit_step_alloc[i],
							   buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_xcalloc(gres_job_ptr->gres_cnt_step_alloc,
					     gres_job_ptr->node_cnt,
					     sizeof(uint64_t));
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					safe_unpack64(&gres_job_ptr->
						      gres_cnt_step_alloc[i],
						      buffer);
				}
			}
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			goto unpack_error;
		}

		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_context[i].plugin_id == plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			/*
			 * A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data.
			 */
			error("%s: no plugin configured to unpack data type %u from job %u. This is likely due to a difference in the GresTypes configured in slurm.conf on different cluster nodes.",
			      __func__, plugin_id, job_id);
			_job_state_delete(gres_job_ptr);
			continue;
		}
		gres_job_ptr->gres_name = xstrdup(gres_context[i].gres_name);
		gres_ptr = xmalloc(sizeof(gres_state_t));
		gres_ptr->plugin_id = gres_context[i].plugin_id;
		gres_ptr->gres_data = gres_job_ptr;
		gres_job_ptr = NULL;	/* nothing left to free on error */
		list_append(*gres_list, gres_ptr);
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error from job %u", __func__, job_id);
	if (gres_job_ptr)
		_job_state_delete(gres_job_ptr);
	slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

/*
 * Pack a job's allocated gres information for use by prolog/epilog
 * IN gres_list - generated by gres_plugin_job_config_validate()
 * IN/OUT buffer - location to write state to
 */
extern int gres_plugin_job_alloc_pack(List gres_list, Buf buffer,
				      uint16_t protocol_version)
{
	int i, rc = SLURM_SUCCESS;
	uint32_t top_offset, tail_offset;
	uint32_t magic = GRES_MAGIC;
	uint16_t rec_cnt = 0;
	ListIterator gres_iter;
	gres_epilog_info_t *gres_job_ptr;

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	if (gres_list == NULL)
		return rc;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_job_ptr = (gres_epilog_info_t *) list_next(gres_iter))) {
		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_job_ptr->plugin_id, buffer);
			pack32(gres_job_ptr->node_cnt, buffer);
			if (gres_job_ptr->gres_cnt_node_alloc) {
				pack8((uint8_t) 1, buffer);
				pack64_array(gres_job_ptr->gres_cnt_node_alloc,
					     gres_job_ptr->node_cnt, buffer);
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (gres_job_ptr->gres_bit_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					pack_bit_str_hex(gres_job_ptr->
							 gres_bit_alloc[i],
							 buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			rec_cnt++;
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	tail_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, top_offset);
	pack16(rec_cnt, buffer);
	set_buf_offset(buffer, tail_offset);

	return rc;
}

static void _epilog_list_del(void *x)
{
	gres_epilog_info_t *epilog_info = (gres_epilog_info_t *) x;
	int i;

	if (!epilog_info)
		return;

	if (epilog_info->gres_bit_alloc) {
		for (i = 0; i < epilog_info->node_cnt; i++)
			FREE_NULL_BITMAP(epilog_info->gres_bit_alloc[i]);
		xfree(epilog_info->gres_bit_alloc);
	}
	xfree(epilog_info->gres_cnt_node_alloc);
	xfree(epilog_info->node_list);
	xfree(epilog_info);
}

/*
 * Unpack a job's allocated gres information for use by prolog/epilog
 * OUT gres_list - restored state stored by gres_plugin_job_alloc_pack()
 * IN/OUT buffer - location to read state from
 */
extern int gres_plugin_job_alloc_unpack(List *gres_list, Buf buffer,
					uint16_t protocol_version)
{
	int i = 0, rc;
	uint32_t magic = 0, utmp32 = 0;
	uint16_t rec_cnt = 0;
	uint8_t filled = 0;
	gres_epilog_info_t *gres_job_ptr = NULL;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_epilog_list_del);
	}

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;

		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			gres_job_ptr = xmalloc(sizeof(gres_epilog_info_t));
			safe_unpack32(&gres_job_ptr->plugin_id, buffer);
			safe_unpack32(&gres_job_ptr->node_cnt, buffer);
			if (gres_job_ptr->node_cnt > NO_VAL)
				goto unpack_error;
			safe_unpack8(&filled, buffer);
			if (filled) {
				safe_unpack64_array(
					&gres_job_ptr->gres_cnt_node_alloc,
					&utmp32, buffer);
			}
			safe_unpack8(&filled, buffer);
			if (filled) {
				safe_xcalloc(gres_job_ptr->gres_bit_alloc,
					     gres_job_ptr->node_cnt,
					     sizeof(bitstr_t *));
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_job_ptr->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			goto unpack_error;
		}

		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_context[i].plugin_id ==
			    gres_job_ptr->plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			/*
			 * A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data.
			 */
			error("%s: no plugin configured to unpack data type %u",
			      __func__, gres_job_ptr->plugin_id);
			_epilog_list_del(gres_job_ptr);
			continue;
		}
		list_append(*gres_list, gres_job_ptr);
		gres_job_ptr = NULL;
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error", __func__);
	if (gres_job_ptr)
		_epilog_list_del(gres_job_ptr);
	slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

/*
 * Build List of information needed to set job's Prolog or Epilog environment
 * variables
 *
 * IN job_gres_list - job's GRES allocation info
 * IN hostlist - list of nodes associated with the job
 * RET information about the job's GRES allocation needed by Prolog or Epilog
 */
extern List gres_plugin_epilog_build_env(List job_gres_list, char *node_list)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_ptr = NULL;
	gres_epilog_info_t *epilog_info;
	List epilog_gres_list = NULL;

	if (!job_gres_list)
		return NULL;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(job_gres_list);
	while ((gres_ptr = list_next(gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_ptr->plugin_id == gres_context[i].plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("%s: gres not found in context.  This should never happen",
			      __func__);
			continue;
		}

		if (!gres_context[i].ops.epilog_build_env)
			continue;	/* No plugin to call */
		epilog_info = (*(gres_context[i].ops.epilog_build_env))
				(gres_ptr->gres_data);
		if (!epilog_info)
			continue;	/* No info to add for this plugin */
		if (!epilog_gres_list)
			epilog_gres_list = list_create(_epilog_list_del);
		epilog_info->plugin_id = gres_context[i].plugin_id;
		epilog_info->node_list = xstrdup(node_list);
		list_append(epilog_gres_list, epilog_info);
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return epilog_gres_list;
}

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 *
 * IN/OUT epilog_env_ptr - environment variable array
 * IN epilog_gres_list - generated by TBD
 * IN node_inx - zero origin node index
 */
extern void gres_plugin_epilog_set_env(char ***epilog_env_ptr,
				       List epilog_gres_list, int node_inx)
{
	int i;
	ListIterator epilog_iter;
	gres_epilog_info_t *epilog_info;

	*epilog_env_ptr = NULL;
	if (!epilog_gres_list)
		return;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	epilog_iter = list_iterator_create(epilog_gres_list);
	while ((epilog_info = list_next(epilog_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (epilog_info->plugin_id == gres_context[i].plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("%s: GRES ID %u not found in context",
			      __func__, epilog_info->plugin_id);
			continue;
		}

		if (!gres_context[i].ops.epilog_set_env)
			continue;	/* No plugin to call */
		(*(gres_context[i].ops.epilog_set_env))
			(epilog_env_ptr, epilog_info, node_inx);
	}
	list_iterator_destroy(epilog_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * If core bitmap from slurmd differs in size from that in slurmctld,
 * then modify bitmap from slurmd so we can use bit_and, bit_or, etc.
 */
static bitstr_t *_core_bitmap_rebuild(bitstr_t *old_core_bitmap, int new_size)
{
	int i, j, old_size, ratio;
	bitstr_t *new_core_bitmap;

	new_core_bitmap = bit_alloc(new_size);
	old_size = bit_size(old_core_bitmap);
	if (old_size > new_size) {
		ratio = old_size / new_size;
		for (i = 0; i < new_size; i++) {
			for (j = 0; j < ratio; j++) {
				if (bit_test(old_core_bitmap, i*ratio+j)) {
					bit_set(new_core_bitmap, i);
					break;
				}
			}
		}
	} else {
		ratio = new_size / old_size;
		for (i = 0; i < old_size; i++) {
			if (!bit_test(old_core_bitmap, i))
				continue;
			for (j = 0; j < ratio; j++) {
				bit_set(new_core_bitmap, i*ratio+j);
			}
		}
	}

	return new_core_bitmap;
}

static void _validate_gres_node_cores(gres_node_state_t *node_gres_ptr,
				      int cores_ctld, char *node_name)
{
	int i, cores_slurmd;
	bitstr_t *new_core_bitmap;
	int log_mismatch = true;

	if (node_gres_ptr->topo_cnt == 0)
		return;

	if (node_gres_ptr->topo_core_bitmap == NULL) {
		error("Gres topo_core_bitmap is NULL on node %s", node_name);
		return;
	}


	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		if (!node_gres_ptr->topo_core_bitmap[i])
			continue;
		cores_slurmd = bit_size(node_gres_ptr->topo_core_bitmap[i]);
		if (cores_slurmd == cores_ctld)
			continue;
		if (log_mismatch) {
			debug("Rebuilding node %s gres core bitmap (%d != %d)",
			      node_name, cores_slurmd, cores_ctld);
			log_mismatch = false;
		}
		new_core_bitmap = _core_bitmap_rebuild(
					node_gres_ptr->topo_core_bitmap[i],
					cores_ctld);
		FREE_NULL_BITMAP(node_gres_ptr->topo_core_bitmap[i]);
		node_gres_ptr->topo_core_bitmap[i] = new_core_bitmap;
	}
}

static void	_job_core_filter(void *job_gres_data, void *node_gres_data,
				 bool use_total_gres, bitstr_t *core_bitmap,
				 int core_start_bit, int core_end_bit,
				 char *gres_name, char *node_name,
				 uint32_t plugin_id)
{
	int i, j, core_ctld;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bitstr_t *avail_core_bitmap = NULL;
	bool use_busy_dev = false;

	if (!node_gres_ptr->topo_cnt || !core_bitmap ||	/* No topology info */
	    !job_gres_ptr->gres_per_node)		/* No job GRES */
		return;

	if (!use_total_gres &&
	    (plugin_id == mps_plugin_id) &&
	    (node_gres_ptr->gres_cnt_alloc != 0)) {
		/* We must use the ONE already active GRES of this type */
		use_busy_dev = true;
	}

	/* Determine which specific cores can be used */
	avail_core_bitmap = bit_copy(core_bitmap);
	bit_nclear(avail_core_bitmap, core_start_bit, core_end_bit);
	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		if (node_gres_ptr->topo_gres_cnt_avail[i] == 0)
			continue;
		if (!use_total_gres &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] >=
		     node_gres_ptr->topo_gres_cnt_avail[i]))
			continue;
		if (use_busy_dev &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] == 0))
			continue;
		if (job_gres_ptr->type_name &&
		    (!node_gres_ptr->topo_type_name[i] ||
		     (job_gres_ptr->type_id != node_gres_ptr->topo_type_id[i])))
			continue;
		if (!node_gres_ptr->topo_core_bitmap[i]) {
			FREE_NULL_BITMAP(avail_core_bitmap);	/* No filter */
			return;
		}
		core_ctld = core_end_bit - core_start_bit + 1;
		_validate_gres_node_cores(node_gres_ptr, core_ctld, node_name);
		core_ctld = bit_size(node_gres_ptr->topo_core_bitmap[i]);
		for (j = 0; j < core_ctld; j++) {
			if (bit_test(node_gres_ptr->topo_core_bitmap[i], j)) {
				bit_set(avail_core_bitmap, core_start_bit + j);
			}
		}
	}
	bit_and(core_bitmap, avail_core_bitmap);
	FREE_NULL_BITMAP(avail_core_bitmap);
}

static uint32_t _job_test(void *job_gres_data, void *node_gres_data,
			  bool use_total_gres, bitstr_t *core_bitmap,
			  int core_start_bit, int core_end_bit, bool *topo_set,
			  uint32_t job_id, char *node_name, char *gres_name,
			  uint32_t plugin_id, bool disable_binding)
{
	int i, j, core_size, core_ctld, top_inx = -1;
	uint64_t gres_avail = 0, gres_max = 0, gres_total, gres_tmp;
	uint64_t min_gres_node = 0;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	uint32_t *cores_addnt = NULL; /* Additional cores avail from this GRES */
	uint32_t *cores_avail = NULL; /* cores initially avail from this GRES */
	uint32_t core_cnt = 0;
	bitstr_t *alloc_core_bitmap = NULL;
	bitstr_t *avail_core_bitmap = NULL;
	bool shared_gres = _shared_gres(plugin_id);
	bool use_busy_dev = false;

	if (node_gres_ptr->no_consume)
		use_total_gres = true;

	if (!use_total_gres &&
	    (plugin_id == mps_plugin_id) &&
	    (node_gres_ptr->gres_cnt_alloc != 0)) {
		/* We must use the ONE already active GRES of this type */
		use_busy_dev = true;
	}

	/* Determine minimum GRES count needed on this node */
	if (job_gres_ptr->gres_per_job)
		min_gres_node = 1;
	min_gres_node = MAX(min_gres_node, job_gres_ptr->gres_per_node);
	min_gres_node = MAX(min_gres_node, job_gres_ptr->gres_per_socket);
	min_gres_node = MAX(min_gres_node, job_gres_ptr->gres_per_task);

	if (min_gres_node && node_gres_ptr->topo_cnt && *topo_set) {
		/*
		 * Need to determine how many GRES available for these
		 * specific cores
		 */
		if (core_bitmap) {
			core_ctld = core_end_bit - core_start_bit + 1;
			if (core_ctld < 1) {
				error("gres/%s: job %u cores on node %s < 1",
				      gres_name, job_id, node_name);
				return (uint32_t) 0;
			}
			_validate_gres_node_cores(node_gres_ptr, core_ctld,
						  node_name);
		}
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			if (job_gres_ptr->type_name &&
			    (!node_gres_ptr->topo_type_name[i] ||
			     (node_gres_ptr->topo_type_id[i] !=
			      job_gres_ptr->type_id)))
				continue;
			if (use_busy_dev &&
			    (node_gres_ptr->topo_gres_cnt_alloc[i] == 0))
				continue;
			if (!node_gres_ptr->topo_core_bitmap[i]) {
				gres_avail += node_gres_ptr->
					      topo_gres_cnt_avail[i];
				if (!use_total_gres) {
					gres_avail -= node_gres_ptr->
						      topo_gres_cnt_alloc[i];
				}
				if (shared_gres)
					gres_max = MAX(gres_max, gres_avail);
				continue;
			}
			core_ctld = bit_size(node_gres_ptr->
					     topo_core_bitmap[i]);
			for (j = 0; j < core_ctld; j++) {
				if (core_bitmap &&
				    !bit_test(core_bitmap, core_start_bit + j))
					continue;
				if (!bit_test(node_gres_ptr->
					      topo_core_bitmap[i], j))
					continue; /* not avail for this gres */
				gres_avail += node_gres_ptr->
					      topo_gres_cnt_avail[i];
				if (!use_total_gres) {
					gres_avail -= node_gres_ptr->
						      topo_gres_cnt_alloc[i];
				}
				if (shared_gres)
					gres_max = MAX(gres_max, gres_avail);
				break;
			}
		}
		if (shared_gres)
			gres_avail = gres_max;
		if (min_gres_node > gres_avail)
			return (uint32_t) 0;	/* insufficient GRES avail */
		return NO_VAL;
	} else if (min_gres_node && node_gres_ptr->topo_cnt &&
		   !disable_binding) {
		/* Need to determine which specific cores can be used */
		gres_avail = node_gres_ptr->gres_cnt_avail;
		if (!use_total_gres)
			gres_avail -= node_gres_ptr->gres_cnt_alloc;
		if (min_gres_node > gres_avail)
			return (uint32_t) 0;	/* insufficient GRES avail */

		core_ctld = core_end_bit - core_start_bit + 1;
		if (core_bitmap) {
			if (core_ctld < 1) {
				error("gres/%s: job %u cores on node %s < 1",
				      gres_name, job_id, node_name);
				return (uint32_t) 0;
			}
			_validate_gres_node_cores(node_gres_ptr, core_ctld,
						  node_name);
		} else {
			for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
				if (!node_gres_ptr->topo_core_bitmap[i])
					continue;
				core_ctld = bit_size(node_gres_ptr->
						     topo_core_bitmap[i]);
				break;
			}
		}

		alloc_core_bitmap = bit_alloc(core_ctld);
		if (core_bitmap) {
			for (j = 0; j < core_ctld; j++) {
				if (bit_test(core_bitmap, core_start_bit + j))
					bit_set(alloc_core_bitmap, j);
			}
		} else {
			bit_nset(alloc_core_bitmap, 0, core_ctld - 1);
		}

		avail_core_bitmap = bit_copy(alloc_core_bitmap);
		cores_addnt = xcalloc(node_gres_ptr->topo_cnt,
				      sizeof(uint32_t));
		cores_avail = xcalloc(node_gres_ptr->topo_cnt,
				      sizeof(uint32_t));
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			if (node_gres_ptr->topo_gres_cnt_avail[i] == 0)
				continue;
			if (use_busy_dev &&
			    (node_gres_ptr->topo_gres_cnt_alloc[i] == 0))
				continue;
			if (!use_total_gres &&
			    (node_gres_ptr->topo_gres_cnt_alloc[i] >=
			     node_gres_ptr->topo_gres_cnt_avail[i]))
				continue;
			if (job_gres_ptr->type_name &&
			    (!node_gres_ptr->topo_type_name[i] ||
			     (node_gres_ptr->topo_type_id[i] !=
			      job_gres_ptr->type_id)))
				continue;
			if (!node_gres_ptr->topo_core_bitmap[i]) {
				cores_avail[i] = core_end_bit -
						 core_start_bit + 1;
				continue;
			}
			core_size = bit_size(node_gres_ptr->topo_core_bitmap[i]);
			for (j = 0; j < core_size; j++) {
				if (core_bitmap &&
				    !bit_test(core_bitmap, core_start_bit + j))
					continue;
				if (bit_test(node_gres_ptr->
					     topo_core_bitmap[i], j)) {
					cores_avail[i]++;
				}
			}
		}

		/* Pick the topology entries with the most cores available */
		gres_avail = 0;
		gres_total = 0;
		while (gres_avail < min_gres_node) {
			top_inx = -1;
			for (j = 0; j < node_gres_ptr->topo_cnt; j++) {
				if ((gres_avail == 0) || (cores_avail[j] == 0) ||
				    !node_gres_ptr->topo_core_bitmap[j]) {
					cores_addnt[j] = cores_avail[j];
				} else {
					cores_addnt[j] = cores_avail[j] -
						bit_overlap(alloc_core_bitmap,
							    node_gres_ptr->
							    topo_core_bitmap[j]);
				}

				if (top_inx == -1) {
					if (cores_avail[j])
						top_inx = j;
				} else if (cores_addnt[j] > cores_addnt[top_inx])
					top_inx = j;
			}
			if ((top_inx < 0) || (cores_avail[top_inx] == 0)) {
				if (gres_total < min_gres_node)
					core_cnt = 0;
				break;
			}
			cores_avail[top_inx] = 0;	/* Flag as used */
			gres_tmp = node_gres_ptr->topo_gres_cnt_avail[top_inx];
			if (!use_total_gres &&
			    (gres_tmp >=
			     node_gres_ptr->topo_gres_cnt_alloc[top_inx])) {
				gres_tmp -= node_gres_ptr->
					    topo_gres_cnt_alloc[top_inx];
			} else if (!use_total_gres) {
				gres_tmp = 0;
			}
			if (gres_tmp == 0) {
				error("gres/%s: topology allocation error on node %s",
				      gres_name, node_name);
				break;
			}
			/* update counts of allocated cores and GRES */
			if (shared_gres) {
				/*
				 * Process outside of loop after specific
				 * device selected
				 */
			} else if (!node_gres_ptr->topo_core_bitmap[top_inx]) {
				bit_nset(alloc_core_bitmap, 0, core_ctld - 1);
			} else if (gres_avail) {
				bit_or(alloc_core_bitmap,
				       node_gres_ptr->
				       topo_core_bitmap[top_inx]);
				if (core_bitmap)
					bit_and(alloc_core_bitmap,
						avail_core_bitmap);
			} else {
				bit_and(alloc_core_bitmap,
					node_gres_ptr->
					topo_core_bitmap[top_inx]);
			}
			if (shared_gres) {
				gres_total = MAX(gres_total, gres_tmp);
				gres_avail = gres_total;
			} else {
				/*
				 * Available GRES count is up to gres_tmp,
				 * but take 1 per loop to maximize available
				 * core count
				 */
				gres_avail += 1;
				gres_total += gres_tmp;
				core_cnt = bit_set_count(alloc_core_bitmap);
			}
		}
		if (shared_gres && (top_inx >= 0) &&
		    (gres_avail >= min_gres_node)) {
			if (!node_gres_ptr->topo_core_bitmap[top_inx]) {
				bit_nset(alloc_core_bitmap, 0, core_ctld - 1);
			} else {
				bit_or(alloc_core_bitmap,
				       node_gres_ptr->
				       topo_core_bitmap[top_inx]);
				if (core_bitmap)
					bit_and(alloc_core_bitmap,
						avail_core_bitmap);
			}
			core_cnt = bit_set_count(alloc_core_bitmap);
		}
		if (core_bitmap && (core_cnt > 0)) {
			*topo_set = true;
			for (i = 0; i < core_ctld; i++) {
				if (!bit_test(alloc_core_bitmap, i)) {
					bit_clear(core_bitmap,
						  core_start_bit + i);
				}
			}
		}
		FREE_NULL_BITMAP(alloc_core_bitmap);
		FREE_NULL_BITMAP(avail_core_bitmap);
		xfree(cores_addnt);
		xfree(cores_avail);
		return core_cnt;
	} else if (job_gres_ptr->type_name) {
		for (i = 0; i < node_gres_ptr->type_cnt; i++) {
			if (node_gres_ptr->type_name[i] &&
			    (node_gres_ptr->type_id[i] ==
			     job_gres_ptr->type_id))
				break;
		}
		if (i >= node_gres_ptr->type_cnt)
			return (uint32_t) 0;	/* no such type */
		gres_avail = node_gres_ptr->type_cnt_avail[i];
		if (!use_total_gres)
			gres_avail -= node_gres_ptr->type_cnt_alloc[i];
		gres_tmp = node_gres_ptr->gres_cnt_avail;
		if (!use_total_gres)
			gres_tmp -= node_gres_ptr->gres_cnt_alloc;
		gres_avail = MIN(gres_avail, gres_tmp);
		if (min_gres_node > gres_avail)
			return (uint32_t) 0;	/* insufficient GRES avail */
		return NO_VAL;
	} else {
		gres_avail = node_gres_ptr->gres_cnt_avail;
		if (!use_total_gres)
			gres_avail -= node_gres_ptr->gres_cnt_alloc;
		if (min_gres_node > gres_avail)
			return (uint32_t) 0;	/* insufficient GRES avail */
		return NO_VAL;
	}
}

/*
 * Clear the core_bitmap for cores which are not usable by this job (i.e. for
 *	cores which are already bound to other jobs or lack GRES)
 * IN job_gres_list   - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list  - node's gres_list built by
 *                      gres_plugin_node_config_validate()
 * IN use_total_gres  - if set then consider all GRES resources as available,
 *		        and none are commited to running jobs
 * IN/OUT core_bitmap - Identification of available cores (NULL if no restriction)
 * IN core_start_bit  - index into core_bitmap for this node's first cores
 * IN core_end_bit    - index into core_bitmap for this node's last cores
 */
extern void gres_plugin_job_core_filter(List job_gres_list, List node_gres_list,
					bool use_total_gres,
					bitstr_t *core_bitmap,
					int core_start_bit, int core_end_bit,
					char *node_name)
{
	int i;
	ListIterator  job_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

	if ((job_gres_list == NULL) || (core_bitmap == NULL))
		return;
	if (node_gres_list == NULL) {
		bit_nclear(core_bitmap, core_start_bit, core_end_bit);
		return;
	}

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		node_gres_ptr = list_find_first(node_gres_list, _gres_find_id,
		                                &job_gres_ptr->plugin_id);
		if (node_gres_ptr == NULL) {
			/* node lack resources required by the job */
			bit_nclear(core_bitmap, core_start_bit, core_end_bit);
			break;
		}

		for (i = 0; i < gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			_job_core_filter(job_gres_ptr->gres_data,
					 node_gres_ptr->gres_data,
					 use_total_gres, core_bitmap,
					 core_start_bit, core_end_bit,
					 gres_context[i].gres_name, node_name,
					 job_gres_ptr->plugin_id);
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return;
}

/*
 * Determine how many cores on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN core_bitmap    - Identification of available cores (NULL if no restriction)
 * IN core_start_bit - index into core_bitmap for this node's first core
 * IN core_end_bit   - index into core_bitmap for this node's last core
 * IN job_id         - job's ID (for logging)
 * IN node_name      - name of the node (for logging)
 * IN disable binding- --gres-flags=disable-binding
 * RET: NO_VAL    - All cores on node are available
 *      otherwise - Count of available cores
 */
extern uint32_t gres_plugin_job_test(List job_gres_list, List node_gres_list,
				     bool use_total_gres, bitstr_t *core_bitmap,
				     int core_start_bit, int core_end_bit,
				     uint32_t job_id, char *node_name,
				     bool disable_binding)
{
	int i;
	uint32_t core_cnt, tmp_cnt;
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;
	bool topo_set = false;

	if (job_gres_list == NULL)
		return NO_VAL;
	if (node_gres_list == NULL)
		return 0;

	core_cnt = NO_VAL;
	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		node_gres_ptr = list_find_first(node_gres_list, _gres_find_id,
		                                &job_gres_ptr->plugin_id);
		if (node_gres_ptr == NULL) {
			/* node lack resources required by the job */
			core_cnt = 0;
			break;
		}

		for (i = 0; i < gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			tmp_cnt = _job_test(job_gres_ptr->gres_data,
					    node_gres_ptr->gres_data,
					    use_total_gres, core_bitmap,
					    core_start_bit, core_end_bit,
					    &topo_set, job_id, node_name,
					    gres_context[i].gres_name,
					    gres_context[i].plugin_id,
					    disable_binding);
			if (tmp_cnt != NO_VAL) {
				if (core_cnt == NO_VAL)
					core_cnt = tmp_cnt;
				else
					core_cnt = MIN(tmp_cnt, core_cnt);
			}
			break;
		}
		if (core_cnt == 0)
			break;
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return core_cnt;
}

static void _sock_gres_del(void *x)
{
	sock_gres_t *sock_gres = (sock_gres_t *) x;
	int s;

	if (sock_gres) {
		FREE_NULL_BITMAP(sock_gres->bits_any_sock);
		if (sock_gres->bits_by_sock) {
			for (s = 0; s < sock_gres->sock_cnt; s++)
				FREE_NULL_BITMAP(sock_gres->bits_by_sock[s]);
			xfree(sock_gres->bits_by_sock);
		}
		xfree(sock_gres->cnt_by_sock);
		xfree(sock_gres->gres_name);
		/* NOTE: sock_gres->job_specs is just a pointer, do not free */
		xfree(sock_gres->type_name);
		xfree(sock_gres);
	}
}

/*
 * Build a string containing the GRES details for a given node and socket
 * sock_gres_list IN - List of sock_gres_t entries
 * sock_inx IN - zero-origin socket for which information is to be returned
 *		 if value < 0, then report GRES unconstrained by core
 * RET string, must call xfree() to release memory
 */
extern char *gres_plugin_sock_str(List sock_gres_list, int sock_inx)
{
	ListIterator iter;
	sock_gres_t *sock_gres;
	char *gres_str = NULL, *sep = "";

	if (!sock_gres_list)
		return NULL;

	iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(iter))) {
		if (sock_inx < 0) {
			if (sock_gres->cnt_any_sock) {
				if (sock_gres->type_name) {
					xstrfmtcat(gres_str, "%s%s:%s:%"PRIu64,
						   sep, sock_gres->gres_name,
						   sock_gres->type_name,
						   sock_gres->cnt_any_sock);
				} else {
					xstrfmtcat(gres_str, "%s%s:%"PRIu64,
						   sep, sock_gres->gres_name,
						   sock_gres->cnt_any_sock);
				}
				sep = " ";
			}
			continue;
		}
		if (!sock_gres->cnt_by_sock ||
		    (sock_gres->cnt_by_sock[sock_inx] == 0))
			continue;
		if (sock_gres->type_name) {
			xstrfmtcat(gres_str, "%s%s:%s:%"PRIu64, sep,
				   sock_gres->gres_name, sock_gres->type_name,
				   sock_gres->cnt_by_sock[sock_inx]);
		} else {
			xstrfmtcat(gres_str, "%s%s:%"PRIu64, sep,
				   sock_gres->gres_name,
				   sock_gres->cnt_by_sock[sock_inx]);
		}
		sep = " ";
	}
	list_iterator_destroy(iter);
	return gres_str;
}

/*
 * Determine how many GRES of a given type can be used by this job on a
 * given node and return a structure with the details. Note that multiple
 * GRES of a given type model can be distributed over multiple topo structures,
 * so we need to OR the core_bitmap over all of them.
 */
static sock_gres_t *_build_sock_gres_by_topo(gres_job_state_t *job_gres_ptr,
				gres_node_state_t *node_gres_ptr,
				bool use_total_gres, bitstr_t *core_bitmap,
				uint16_t sockets, uint16_t cores_per_sock,
				uint32_t job_id, char *node_name,
				bool enforce_binding, uint32_t s_p_n,
				bitstr_t **req_sock_map,
				uint32_t main_plugin_id, uint32_t alt_plugin_id,
				gres_node_state_t *alt_node_gres_ptr,
				uint32_t user_id, const uint32_t node_inx)
{
	int i, j, s, c, tot_cores;
	sock_gres_t *sock_gres;
	int64_t add_gres;
	uint64_t avail_gres, min_gres = 1;
	bool match = false;
	bool use_busy_dev = false;

	if (node_gres_ptr->gres_cnt_avail == 0)
		return NULL;

	if (!use_total_gres &&
	    (main_plugin_id == mps_plugin_id) &&
	    (node_gres_ptr->gres_cnt_alloc != 0)) {
		/* We must use the ONE already active GRES of this type */
		use_busy_dev = true;
	}

	sock_gres = xmalloc(sizeof(sock_gres_t));
	sock_gres->sock_cnt = sockets;
	sock_gres->bits_by_sock = xcalloc(sockets, sizeof(bitstr_t *));
	sock_gres->cnt_by_sock = xcalloc(sockets, sizeof(uint64_t));
	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		bool use_all_sockets = false;
		if (job_gres_ptr->type_name &&
		    (job_gres_ptr->type_id != node_gres_ptr->topo_type_id[i]))
			continue;	/* Wrong type_model */
		if (use_busy_dev &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] == 0))
			continue;
		if (!use_total_gres && !node_gres_ptr->no_consume &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] >=
		     node_gres_ptr->topo_gres_cnt_avail[i])) {
			continue;	/* No GRES remaining */
		}

		if (!use_total_gres && !node_gres_ptr->no_consume) {
			avail_gres = node_gres_ptr->topo_gres_cnt_avail[i] -
				     node_gres_ptr->topo_gres_cnt_alloc[i];
		} else {
			avail_gres = node_gres_ptr->topo_gres_cnt_avail[i];
		}
		if (avail_gres == 0)
			continue;

		/*
		 * Job requested GPUs or MPS. Filter out resources already
		 * allocated to the other GRES type.
		 */
		if (alt_node_gres_ptr && alt_node_gres_ptr->gres_bit_alloc &&
		    node_gres_ptr->topo_gres_bitmap[i]) {
			c = bit_overlap(node_gres_ptr->topo_gres_bitmap[i],
					alt_node_gres_ptr->gres_bit_alloc);
			if ((alt_plugin_id == gpu_plugin_id) && (c > 0))
				continue;
			if ((alt_plugin_id == mps_plugin_id) && (c > 0)) {
				avail_gres -= c;
				if (avail_gres == 0)
					continue;
			}
		}

		/* gres/mps can only use one GPU per node */
		if ((main_plugin_id == mps_plugin_id) &&
		    (avail_gres > sock_gres->max_node_gres))
			sock_gres->max_node_gres = avail_gres;

		/*
		 * If some GRES is available on every socket,
		 * treat like no topo_core_bitmap is specified
		 */
		tot_cores = sockets * cores_per_sock;
		if (node_gres_ptr->topo_core_bitmap &&
		    node_gres_ptr->topo_core_bitmap[i]) {
			use_all_sockets = true;
			for (s = 0; s < sockets; s++) {
				bool use_this_socket = false;
				for (c = 0; c < cores_per_sock; c++) {
					j = (s * cores_per_sock) + c;
					if (bit_test(node_gres_ptr->
						     topo_core_bitmap[i], j)) {
						use_this_socket = true;
						break;
					}
				}
				if (!use_this_socket) {
					use_all_sockets = false;
					break;
				}
			}
		}

		if (!node_gres_ptr->topo_core_bitmap ||
		    !node_gres_ptr->topo_core_bitmap[i] ||
		    use_all_sockets) {
			/*
			 * Not constrained by core, but only specific
			 * GRES may be available (save their bitmap)
			 */
			sock_gres->cnt_any_sock += avail_gres;
			sock_gres->total_cnt += avail_gres;
			if (!sock_gres->bits_any_sock) {
				sock_gres->bits_any_sock =
					bit_copy(node_gres_ptr->
						 topo_gres_bitmap[i]);
			} else {
				bit_or(sock_gres->bits_any_sock,
				       node_gres_ptr->topo_gres_bitmap[i]);
			}
			match = true;
			continue;
		}

		/* Constrained by core */
		if (core_bitmap)
			tot_cores = MIN(tot_cores, bit_size(core_bitmap));
		if (node_gres_ptr->topo_core_bitmap[i]) {
			tot_cores = MIN(tot_cores,
					bit_size(node_gres_ptr->
						 topo_core_bitmap[i]));
		}
		for (s = 0; ((s < sockets) && avail_gres); s++) {
			if (enforce_binding && core_bitmap) {
				for (c = 0; c < cores_per_sock; c++) {
					j = (s * cores_per_sock) + c;
					if (bit_test(core_bitmap, j))
						break;
				}
				if (c >= cores_per_sock) {
					/* No available cores on this socket */
					continue;
				}
			}
			for (c = 0; c < cores_per_sock; c++) {
				j = (s * cores_per_sock) + c;
				if (j >= tot_cores)
					break;	/* Off end of core bitmap */
				if (node_gres_ptr->topo_core_bitmap[i] &&
				    !bit_test(node_gres_ptr->topo_core_bitmap[i],
					      j))
					continue;
				if (!node_gres_ptr->topo_gres_bitmap[i]) {
					error("%s: topo_gres_bitmap NULL on node %s",
					      __func__, node_name);
					continue;
				}
				if (!sock_gres->bits_by_sock[s]) {
					sock_gres->bits_by_sock[s] =
						bit_copy(node_gres_ptr->
							 topo_gres_bitmap[i]);
				} else {
					bit_or(sock_gres->bits_by_sock[s],
					       node_gres_ptr->topo_gres_bitmap[i]);
				}
				sock_gres->cnt_by_sock[s] += avail_gres;
				sock_gres->total_cnt += avail_gres;
				avail_gres = 0;
				match = true;
				break;
			}
		}
	}

	/* Process per-GRES limits */
	if (match && job_gres_ptr->gres_per_socket) {
		/*
		 * Clear core bitmap on sockets with insufficient GRES
		 * and disable excess GRES per socket
		 */
		for (s = 0; s < sockets; s++) {
			if (sock_gres->cnt_by_sock[s] <
			    job_gres_ptr->gres_per_socket) {
				/* Insufficient GRES, clear count */
				sock_gres->total_cnt -=
					sock_gres->cnt_by_sock[s];
				sock_gres->cnt_by_sock[s] = 0;
				if (enforce_binding && core_bitmap) {
					i = s * cores_per_sock;
					bit_nclear(core_bitmap, i,
						   i + cores_per_sock - 1);
				}
			} else if (sock_gres->cnt_by_sock[s] >
				   job_gres_ptr->gres_per_socket) {
				/* Excess GRES, reduce count */
				i = sock_gres->cnt_by_sock[s] -
				    job_gres_ptr->gres_per_socket;
				sock_gres->cnt_by_sock[s] =
					job_gres_ptr->gres_per_socket;
				sock_gres->total_cnt -= i;
			}
		}
	}

	/*
	 * Satisfy sockets-per-node (s_p_n) limit by selecting the sockets with
	 * the most GRES. Sockets with low GRES counts have their core_bitmap
	 * cleared so that _allocate_sc() in cons_tres/job_test.c does not
	 * remove sockets needed to satisfy the job's GRES specification.
	 */
	if (match && enforce_binding && core_bitmap && (s_p_n < sockets)) {
		int avail_sock = 0;
		bool *avail_sock_flag = xcalloc(sockets, sizeof(bool));
		for (s = 0; s < sockets; s++) {
			if (sock_gres->cnt_by_sock[s] == 0)
				continue;
			for (c = 0; c < cores_per_sock; c++) {
				i = (s * cores_per_sock) + c;
				if (!bit_test(core_bitmap, i))
					continue;
				avail_sock++;
				avail_sock_flag[s] = true;
				break;
			}
		}
		while (avail_sock > s_p_n) {
			int low_gres_sock_inx = -1;
			for (s = 0; s < sockets; s++) {
				if (!avail_sock_flag[s])
					continue;
				if ((low_gres_sock_inx == -1) ||
				    (sock_gres->cnt_by_sock[s] <
				     sock_gres->cnt_by_sock[low_gres_sock_inx]))
					low_gres_sock_inx = s;
			}
			if (low_gres_sock_inx == -1)
				break;
			s = low_gres_sock_inx;
			i = s * cores_per_sock;
			bit_nclear(core_bitmap, i, i + cores_per_sock - 1);
			sock_gres->total_cnt -= sock_gres->cnt_by_sock[s];
			sock_gres->cnt_by_sock[s] = 0;
			avail_sock--;
			avail_sock_flag[s] = false;
		}
		xfree(avail_sock_flag);
	}

	if (match) {
		if (job_gres_ptr->gres_per_node)
			min_gres = job_gres_ptr->gres_per_node;
		if (job_gres_ptr->gres_per_task)
			min_gres = MAX(min_gres, job_gres_ptr->gres_per_task);
		if (sock_gres->total_cnt < min_gres)
			match = false;
	}


	/*
	 * If sockets-per-node (s_p_n) not specified then identify sockets
	 * which are required to satisfy gres_per_node or task specification
	 * so that allocated tasks can be distributed over multiple sockets
	 * if necessary.
	 */
	add_gres = min_gres - sock_gres->cnt_any_sock;
	if (match && core_bitmap && (s_p_n == NO_VAL) && (add_gres > 0) &&
	    job_gres_ptr->gres_per_node) {
		int avail_sock = 0, best_sock_inx = -1;
		bool *avail_sock_flag = xcalloc(sockets, sizeof(bool));
		for (s = 0; s < sockets; s++) {
			if (sock_gres->cnt_by_sock[s] == 0)
				continue;
			for (c = 0; c < cores_per_sock; c++) {
				i = (s * cores_per_sock) + c;
				if (!bit_test(core_bitmap, i))
					continue;
				avail_sock++;
				avail_sock_flag[s] = true;
				if ((best_sock_inx == -1) ||
				    (sock_gres->cnt_by_sock[s] >
				     sock_gres->cnt_by_sock[best_sock_inx])) {
					best_sock_inx = s;
				}
				break;
			}
		}
		while ((best_sock_inx != -1) && (add_gres > 0)) {
			if (*req_sock_map == NULL)
				*req_sock_map = bit_alloc(sockets);
			bit_set(*req_sock_map, best_sock_inx);
			add_gres -= sock_gres->cnt_by_sock[best_sock_inx];
			avail_sock_flag[best_sock_inx] = false;
			if (add_gres <= 0)
				break;
			/* Find next best socket */
			best_sock_inx = -1;
			for (s = 0; s < sockets; s++) {
				if ((sock_gres->cnt_by_sock[s] == 0) ||
				    !avail_sock_flag[s])
					continue;
				if ((best_sock_inx == -1) ||
				    (sock_gres->cnt_by_sock[s] >
				     sock_gres->cnt_by_sock[best_sock_inx])) {
					best_sock_inx = s;
				}
			}
		}
		xfree(avail_sock_flag);
	}

	if (match) {
		sock_gres->type_id = job_gres_ptr->type_id;
		sock_gres->type_name = xstrdup(job_gres_ptr->type_name);
	} else {
		_sock_gres_del(sock_gres);
		sock_gres = NULL;
	}
	return sock_gres;
}

/*
 * Determine how many GRES of a given type can be used by this job on a
 * given node and return a structure with the details. Note that multiple
 * GRES of a given type model can be configured, so pick the right one.
 */
static sock_gres_t *_build_sock_gres_by_type(gres_job_state_t *job_gres_ptr,
				gres_node_state_t *node_gres_ptr,
				bool use_total_gres, bitstr_t *core_bitmap,
				uint16_t sockets, uint16_t cores_per_sock,
				uint32_t job_id, char *node_name)
{
	int i;
	sock_gres_t *sock_gres;
	uint64_t avail_gres, min_gres = 1, gres_tmp;
	bool match = false;

	if (job_gres_ptr->gres_per_node)
		min_gres = job_gres_ptr-> gres_per_node;
	if (job_gres_ptr->gres_per_socket)
		min_gres = MAX(min_gres, job_gres_ptr->gres_per_socket);
	if (job_gres_ptr->gres_per_task)
		min_gres = MAX(min_gres, job_gres_ptr->gres_per_task);
	sock_gres = xmalloc(sizeof(sock_gres_t));
	for (i = 0; i < node_gres_ptr->type_cnt; i++) {
		if (job_gres_ptr->type_name &&
		    (job_gres_ptr->type_id != node_gres_ptr->type_id[i]))
			continue;	/* Wrong type_model */
		if (!use_total_gres &&
		    (node_gres_ptr->type_cnt_alloc[i] >=
		     node_gres_ptr->type_cnt_avail[i])) {
			continue;	/* No GRES remaining */
		} else if (!use_total_gres) {
			avail_gres = node_gres_ptr->type_cnt_avail[i] -
				     node_gres_ptr->type_cnt_alloc[i];
		} else {
			avail_gres = node_gres_ptr->type_cnt_avail[i];
		}
		gres_tmp = node_gres_ptr->gres_cnt_avail;
		if (!use_total_gres)
			gres_tmp -= node_gres_ptr->gres_cnt_alloc;
		avail_gres = MIN(avail_gres, gres_tmp);
		if (avail_gres < min_gres)
			continue;	/* Insufficient GRES remaining */
		sock_gres->cnt_any_sock += avail_gres;
		sock_gres->total_cnt += avail_gres;
		match = true;
	}
	if (match) {
		sock_gres->type_id = job_gres_ptr->type_id;
		sock_gres->type_name = xstrdup(job_gres_ptr->type_name);
	} else
		xfree(sock_gres);

	return sock_gres;
}

/*
 * Determine how many GRES of a given type can be used by this job on a
 * given node and return a structure with the details. No GRES type.
 */
static sock_gres_t *_build_sock_gres_basic(gres_job_state_t *job_gres_ptr,
				gres_node_state_t *node_gres_ptr,
				bool use_total_gres, bitstr_t *core_bitmap,
				uint16_t sockets, uint16_t cores_per_sock,
				uint32_t job_id, char *node_name)
{
	sock_gres_t *sock_gres;
	uint64_t avail_gres, min_gres = 1;

	if (job_gres_ptr->type_name)
		return NULL;
	if (!use_total_gres &&
	    (node_gres_ptr->gres_cnt_alloc >= node_gres_ptr->gres_cnt_avail))
		return NULL;	/* No GRES remaining */

	if (job_gres_ptr->gres_per_node)
		min_gres = job_gres_ptr-> gres_per_node;
	if (job_gres_ptr->gres_per_socket)
		min_gres = MAX(min_gres, job_gres_ptr->gres_per_socket);
	if (job_gres_ptr->gres_per_task)
		min_gres = MAX(min_gres, job_gres_ptr->gres_per_task);
	if (!use_total_gres) {
		avail_gres = node_gres_ptr->gres_cnt_avail -
			     node_gres_ptr->gres_cnt_alloc;
	} else
		avail_gres = node_gres_ptr->gres_cnt_avail;
	if (avail_gres < min_gres)
		return NULL;	/* Insufficient GRES remaining */

	sock_gres = xmalloc(sizeof(sock_gres_t));
	sock_gres->cnt_any_sock += avail_gres;
	sock_gres->total_cnt += avail_gres;

	return sock_gres;
}

static void _sock_gres_log(List sock_gres_list, char *node_name)
{
	sock_gres_t *sock_gres;
	ListIterator iter;
	int i, len = -1;
	char tmp[32] = "";

	if (!sock_gres_list)
		return;

	info("Sock_gres state for %s", node_name);
	iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(iter))) {
		info("Gres:%s Type:%s TotalCnt:%"PRIu64" MaxNodeGres:%"PRIu64,
		     sock_gres->gres_name, sock_gres->type_name,
		     sock_gres->total_cnt, sock_gres->max_node_gres);
		if (sock_gres->bits_any_sock) {
			bit_fmt(tmp, sizeof(tmp), sock_gres->bits_any_sock);
			len = bit_size(sock_gres->bits_any_sock);
		}
		info("  Sock[ANY]Cnt:%"PRIu64" Bits:%s of %d",
		     sock_gres->cnt_any_sock, tmp, len);

		for (i = 0; i < sock_gres->sock_cnt; i++) {
			if (sock_gres->cnt_by_sock[i] == 0)
				continue;
			tmp[0] = '\0';
			len = -1;
			if (sock_gres->bits_by_sock &&
			    sock_gres->bits_by_sock[i]) {
				bit_fmt(tmp, sizeof(tmp),
					sock_gres->bits_by_sock[i]);
				len = bit_size(sock_gres->bits_by_sock[i]);
			}
			info("  Sock[%d]Cnt:%"PRIu64" Bits:%s of %d", i,
			     sock_gres->cnt_by_sock[i], tmp, len);
		}
	}
	list_iterator_destroy(iter);
}

/*
 * Determine how many cores on each socket of a node can be used by this job
 * IN job_gres_list   - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list  - node's gres_list built by gres_plugin_node_config_validate()
 * IN use_total_gres  - if set then consider all gres resources as available,
 *		        and none are commited to running jobs
 * IN/OUT core_bitmap - Identification of available cores on this node
 * IN sockets         - Count of sockets on the node
 * IN cores_per_sock  - Count of cores per socket on this node
 * IN job_id          - job's ID (for logging)
 * IN node_name       - name of the node (for logging)
 * IN enforce_binding - if true then only use GRES with direct access to cores
 * IN s_p_n           - Expected sockets_per_node (NO_VAL if not limited)
 * OUT req_sock_map   - bitmap of specific requires sockets
 * IN user_id         - job's user ID
 * IN node_inx        - index of node to be evaluated
 * RET: List of sock_gres_t entries identifying what resources are available on
 *	each socket. Returns NULL if none available. Call FREE_NULL_LIST() to
 *	release memory.
 */
extern List gres_plugin_job_test2(List job_gres_list, List node_gres_list,
				  bool use_total_gres, bitstr_t *core_bitmap,
				  uint16_t sockets, uint16_t cores_per_sock,
				  uint32_t job_id, char *node_name,
				  bool enforce_binding, uint32_t s_p_n,
				  bitstr_t **req_sock_map, uint32_t user_id,
				  const uint32_t node_inx)
{
	List sock_gres_list = NULL;
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;
	gres_job_state_t  *job_data_ptr;
	gres_node_state_t *node_data_ptr;
	uint32_t local_s_p_n;

	if (!job_gres_list || (list_count(job_gres_list) == 0))
		return sock_gres_list;
	if (!node_gres_list)	/* Node lacks GRES to match */
		return sock_gres_list;
	(void) gres_plugin_init();

	sock_gres_list = list_create(_sock_gres_del);
	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		sock_gres_t *sock_gres = NULL;
		node_gres_ptr = list_find_first(node_gres_list, _gres_find_id,
		                                &job_gres_ptr->plugin_id);
		if (node_gres_ptr == NULL) {
			/* node lack GRES of type required by the job */
			FREE_NULL_LIST(sock_gres_list);
			break;
		}
		job_data_ptr = (gres_job_state_t *) job_gres_ptr->gres_data;
		node_data_ptr = (gres_node_state_t *) node_gres_ptr->gres_data;

		if (job_data_ptr->gres_per_job &&
		    !job_data_ptr->gres_per_socket)
			local_s_p_n = s_p_n;	/* Maximize GRES per node */
		else
			local_s_p_n = NO_VAL;	/* No need to optimize socket */
		if (core_bitmap && (bit_ffs(core_bitmap) == -1)) {
			sock_gres = NULL;	/* No cores available */
		} else if (node_data_ptr->topo_cnt) {
			uint32_t alt_plugin_id = 0;
			gres_node_state_t *alt_node_data_ptr = NULL;
			if (!use_total_gres && have_gpu && have_mps) {
				if (job_gres_ptr->plugin_id == gpu_plugin_id)
					alt_plugin_id = mps_plugin_id;
				if (job_gres_ptr->plugin_id == mps_plugin_id)
					alt_plugin_id = gpu_plugin_id;
			}
			if (alt_plugin_id) {
				node_gres_ptr = list_find_first(node_gres_list,
				                                _gres_find_id,
				                                &alt_plugin_id);
			}
			if (alt_plugin_id && node_gres_ptr) {
				alt_node_data_ptr = (gres_node_state_t *)
						    node_gres_ptr->gres_data;
			} else {
				/* GRES of interest not on this node */
				alt_plugin_id = 0;
			}
			sock_gres = _build_sock_gres_by_topo(job_data_ptr,
					node_data_ptr, use_total_gres,
					core_bitmap, sockets, cores_per_sock,
					job_id, node_name, enforce_binding,
					local_s_p_n, req_sock_map,
					job_gres_ptr->plugin_id,
					alt_plugin_id, alt_node_data_ptr,
					user_id, node_inx);
		} else if (node_data_ptr->type_cnt) {
			sock_gres = _build_sock_gres_by_type(job_data_ptr,
					node_data_ptr, use_total_gres,
					core_bitmap, sockets, cores_per_sock,
					job_id, node_name);
		} else {
			sock_gres = _build_sock_gres_basic(job_data_ptr,
					node_data_ptr, use_total_gres,
					core_bitmap, sockets, cores_per_sock,
					job_id, node_name);
		}
		if (!sock_gres) {
			/* node lack available resources required by the job */
			bit_clear_all(core_bitmap);
			FREE_NULL_LIST(sock_gres_list);
			break;
		}
		sock_gres->job_specs  = job_data_ptr;
		sock_gres->gres_name  = xstrdup(job_data_ptr->gres_name);
		sock_gres->node_specs = node_data_ptr;
		sock_gres->plugin_id  = job_gres_ptr->plugin_id;
		list_append(sock_gres_list, sock_gres);
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES)
		_sock_gres_log(sock_gres_list, node_name);

	return sock_gres_list;
}

static bool *_build_avail_cores_by_sock(bitstr_t *core_bitmap,
					uint16_t sockets,
					uint16_t cores_per_sock)
{
	bool *avail_cores_by_sock = xcalloc(sockets, sizeof(bool));
	int s, c, i, lim = 0;

	lim = bit_size(core_bitmap);
	for (s = 0; s < sockets; s++) {
		for (c = 0; c < cores_per_sock; c++) {
			i = (s * cores_per_sock) + c;
			if (i >= lim)
				goto fini;	/* should never happen */
			if (bit_test(core_bitmap, i)) {
				avail_cores_by_sock[s] = true;
				break;
			}
		}
	}

fini:	return avail_cores_by_sock;
}

/* Set max_node_gres if it is unset or greater than val */
static bool _set_max_node_gres(sock_gres_t *sock_gres, uint64_t val)
{
	if (val &&
	    ((sock_gres->max_node_gres == 0) ||
	     (sock_gres->max_node_gres > val))) {
		sock_gres->max_node_gres = val;
		return true;
	}

	return false;
}

/*
 * Determine which GRES can be used on this node given the available cores.
 *	Filter out unusable GRES.
 * IN sock_gres_list  - list of sock_gres_t entries built by gres_plugin_job_test2()
 * IN avail_mem       - memory available for the job
 * IN max_cpus        - maximum CPUs available on this node (limited by
 *                      specialized cores and partition CPUs-per-node)
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN core_bitmap     - Identification of available cores on this node
 * IN sockets         - Count of sockets on the node
 * IN cores_per_sock  - Count of cores per socket on this node
 * IN cpus_per_core   - Count of CPUs per core on this node
 * IN sock_per_node   - sockets requested by job per node or NO_VAL
 * IN task_per_node   - tasks requested by job per node or NO_VAL16
 * IN cpus_per_task   - Count of CPUs per task
 * IN whole_node      - we are requesting the whole node or not
 * OUT avail_gpus     - Count of available GPUs on this node
 * OUT near_gpus      - Count of GPUs available on sockets with available CPUs
 * RET - 0 if job can use this node, -1 otherwise (some GRES limit prevents use)
 */
extern int gres_plugin_job_core_filter2(List sock_gres_list, uint64_t avail_mem,
					uint16_t max_cpus,
					bool enforce_binding,
					bitstr_t *core_bitmap,
					uint16_t sockets,
					uint16_t cores_per_sock,
					uint16_t cpus_per_core,
					uint32_t sock_per_node,
					uint16_t task_per_node,
					uint16_t cpus_per_task,
					bool whole_node,
					uint16_t *avail_gpus,
					uint16_t *near_gpus)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	bool *avail_cores_by_sock = NULL;
	uint64_t max_gres, mem_per_gres = 0, near_gres_cnt = 0;
	uint16_t cpus_per_gres;
	int s, rc = 0;

	*avail_gpus = 0;
	*near_gpus = 0;
	if (!core_bitmap || !sock_gres_list ||
	    (list_count(sock_gres_list) == 0))
		return rc;

	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))) {
		uint64_t min_gres = 1, tmp_u64;
		if (sock_gres->job_specs) {
			gres_job_state_t *job_gres_ptr = sock_gres->job_specs;
			if (whole_node)
				min_gres = sock_gres->total_cnt;
			else if (job_gres_ptr->gres_per_node)
				min_gres = job_gres_ptr-> gres_per_node;
			if (job_gres_ptr->gres_per_socket) {
				tmp_u64 = job_gres_ptr->gres_per_socket;
				if (sock_per_node != NO_VAL)
					tmp_u64 *= sock_per_node;
				min_gres = MAX(min_gres, tmp_u64);
			}
			if (job_gres_ptr->gres_per_task) {
				tmp_u64 = job_gres_ptr->gres_per_task;
				if (task_per_node != NO_VAL16)
					tmp_u64 *= task_per_node;
				min_gres = MAX(min_gres, tmp_u64);
			}
		}
		if (!sock_gres->job_specs)
			cpus_per_gres = 0;
		else if (sock_gres->job_specs->cpus_per_gres)
			cpus_per_gres = sock_gres->job_specs->cpus_per_gres;
		else if (sock_gres->job_specs->ntasks_per_gres &&
			 (sock_gres->job_specs->ntasks_per_gres != NO_VAL16))
			cpus_per_gres = sock_gres->job_specs->ntasks_per_gres *
				cpus_per_task;
		else
			cpus_per_gres = sock_gres->job_specs->def_cpus_per_gres;
		if (cpus_per_gres) {
			max_gres = max_cpus / cpus_per_gres;
			if ((max_gres == 0) ||
			    (sock_gres->job_specs->gres_per_node > max_gres) ||
			    (sock_gres->job_specs->gres_per_task > max_gres) ||
			    (sock_gres->job_specs->gres_per_socket > max_gres)){
				log_flag(GRES, "%s: Insufficient CPUs for any GRES: max_gres (%"PRIu64") = max_cpus (%d) / cpus_per_gres (%d)",
					 __func__, max_gres, max_cpus,
					 cpus_per_gres);
				rc = -1;
				break;
			}
		}
		if (!sock_gres->job_specs)
			mem_per_gres = 0;
		else if (sock_gres->job_specs->mem_per_gres)
			mem_per_gres = sock_gres->job_specs->mem_per_gres;
		else
			mem_per_gres = sock_gres->job_specs->def_mem_per_gres;
		if (mem_per_gres && avail_mem) {
			if (mem_per_gres <= avail_mem) {
				sock_gres->max_node_gres = avail_mem /
							   mem_per_gres;
			} else {
				log_flag(GRES, "%s: Insufficient memory for any GRES: mem_per_gres (%"PRIu64") > avail_mem (%"PRIu64")",
					 __func__, mem_per_gres, avail_mem);
				rc = -1;
				break;
			}
		}
		if (sock_gres->cnt_by_sock || enforce_binding) {
			if (!avail_cores_by_sock) {
				avail_cores_by_sock =_build_avail_cores_by_sock(
							core_bitmap, sockets,
							cores_per_sock);
			}
		}
		/*
		 * NOTE: gres_per_socket enforcement is performed by
		 * _build_sock_gres_by_topo(), called by gres_plugin_job_test2()
		 */
		if (sock_gres->cnt_by_sock && enforce_binding) {
			for (s = 0; s < sockets; s++) {
				if (avail_cores_by_sock[s] == 0) {
					sock_gres->total_cnt -=
						sock_gres->cnt_by_sock[s];
					sock_gres->cnt_by_sock[s] = 0;
				}
			}
			near_gres_cnt = sock_gres->total_cnt;
		} else if (sock_gres->cnt_by_sock) { /* NO enforce_binding */
			near_gres_cnt = sock_gres->total_cnt;
			for (s = 0; s < sockets; s++) {
				if (avail_cores_by_sock[s] == 0) {
					near_gres_cnt -=
						sock_gres->cnt_by_sock[s];
				}
			}
		} else {
			near_gres_cnt = sock_gres->total_cnt;
		}
		if (sock_gres->job_specs && !whole_node) {
			/* If gres_per_node isn't set, try gres_per_job */
			if (!_set_max_node_gres(
				    sock_gres,
				    sock_gres->job_specs->gres_per_node))
				(void)_set_max_node_gres(
					sock_gres,
					sock_gres->job_specs->gres_per_job);
		}
		/* Avoid max_node_gres with ntasks_per_gres and whole node */
		if (cpus_per_gres &&
		    ((sock_gres->job_specs->ntasks_per_gres == NO_VAL16) ||
		     !whole_node)) {
			int cpu_cnt;
			cpu_cnt = bit_set_count(core_bitmap);
			cpu_cnt *= cpus_per_core;
			max_gres = cpu_cnt / cpus_per_gres;
			if (max_gres == 0) {
				log_flag(GRES, "%s: max_gres == 0 == cpu_cnt (%d) / cpus_per_gres (%d)",
					 __func__, cpu_cnt, cpus_per_gres);
				rc = -1;
				break;
			} else if ((sock_gres->max_node_gres == 0) ||
				   (sock_gres->max_node_gres > max_gres)) {
				sock_gres->max_node_gres = max_gres;
			}
		}
		if (mem_per_gres) {
			max_gres = avail_mem / mem_per_gres;
			sock_gres->total_cnt = MIN(sock_gres->total_cnt,
						   max_gres);
		}
		if ((sock_gres->total_cnt < min_gres) ||
		    ((sock_gres->max_node_gres != 0) &&
		     (sock_gres->max_node_gres < min_gres))) {
			log_flag(GRES, "%s: min_gres (%"PRIu64") is > max_node_gres (%"PRIu64") or sock_gres->total_cnt (%"PRIu64")",
				 __func__, min_gres, sock_gres->max_node_gres,
				 sock_gres->total_cnt);
			rc = -1;
			break;
		}

		if (_sharing_gres(sock_gres->plugin_id)) {
			 *avail_gpus += sock_gres->total_cnt;
			if (sock_gres->max_node_gres &&
			    (sock_gres->max_node_gres < near_gres_cnt))
				near_gres_cnt = sock_gres->max_node_gres;
			if (*near_gpus < 0xff)	/* avoid overflow */
				*near_gpus += near_gres_cnt;
		}
	}
	list_iterator_destroy(sock_gres_iter);
	xfree(avail_cores_by_sock);

	return rc;
}

/* Order GRES scheduling. Schedule GRES requiring specific sockets first */
static int _sock_gres_sort(void *x, void *y)
{
	sock_gres_t *sock_gres1 = *(sock_gres_t **) x;
	sock_gres_t *sock_gres2 = *(sock_gres_t **) y;
	int weight1 = 0, weight2 = 0;

	if (sock_gres1->node_specs && !sock_gres1->node_specs->topo_cnt)
		weight1 += 0x02;
	if (sock_gres1->job_specs && !sock_gres1->job_specs->gres_per_socket)
		weight1 += 0x01;

	if (sock_gres2->node_specs && !sock_gres2->node_specs->topo_cnt)
		weight2 += 0x02;
	if (sock_gres2->job_specs && !sock_gres2->job_specs->gres_per_socket)
		weight2 += 0x01;

	return weight1 - weight2;
}

int _sort_sockets_by_avail_cores(const void *x, const void *y,
				 void *socket_avail_cores)
{
	uint16_t *sockets = (uint16_t *)socket_avail_cores;
	return (sockets[*(int *)y] - sockets[*(int *)x]);
}

/*
 * Determine how many tasks can be started on a given node and which
 *	sockets/cores are required
 * IN mc_ptr - job's multi-core specs, NO_VAL and INFINITE mapped to zero
 * IN sock_gres_list - list of sock_gres_t entries built by gres_plugin_job_test2()
 * IN sockets - Count of sockets on the node
 * IN cores_per_socket - Count of cores per socket on the node
 * IN cpus_per_core - Count of CPUs per core on the node
 * IN avail_cpus - Count of available CPUs on the node, UPDATED
 * IN min_tasks_this_node - Minimum count of tasks that can be started on this
 *                          node, UPDATED
 * IN max_tasks_this_node - Maximum count of tasks that can be started on this
 *                          node or NO_VAL, UPDATED
 * IN rem_nodes - desired additional node count to allocate, including this node
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN first_pass - set if first scheduling attempt for this job, use
 *		   co-located GRES and cores if possible
 * IN avail_core - cores available on this node, UPDATED
 */
extern void gres_plugin_job_core_filter3(gres_mc_data_t *mc_ptr,
					 List sock_gres_list,
					 uint16_t sockets,
					 uint16_t cores_per_socket,
					 uint16_t cpus_per_core,
					 uint16_t *avail_cpus,
					 uint32_t *min_tasks_this_node,
					 uint32_t *max_tasks_this_node,
					 int rem_nodes,
					 bool enforce_binding,
					 bool first_pass,
					 bitstr_t *avail_core)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	gres_job_state_t *job_specs;
	int i, j, c, s, sock_cnt = 0, req_cores, rem_sockets, full_socket;
	int tot_core_cnt = 0, min_core_cnt = 1;
	uint64_t cnt_avail_sock, cnt_avail_total;
	uint64_t tot_gres_sock, max_tasks;
	uint32_t task_cnt_incr;
	bool *req_sock = NULL;	/* Required socket */
	int *socket_index = NULL; /* Socket indexes */
	uint16_t *avail_cores_per_sock, cpus_per_gres;
	uint16_t avail_cores_tot;

	if (*max_tasks_this_node == 0)
		return;

	xassert(avail_core);
	avail_cores_per_sock = xcalloc(sockets, sizeof(uint16_t));
	for (s = 0; s < sockets; s++) {
		for (c = 0; c < cores_per_socket; c++) {
			i = (s * cores_per_socket) + c;
			if (bit_test(avail_core, i))
				avail_cores_per_sock[s]++;
		}
		tot_core_cnt += avail_cores_per_sock[s];
	}

	task_cnt_incr = *min_tasks_this_node;
	req_sock = xcalloc(sockets, sizeof(bool));
	socket_index = xcalloc(sockets, sizeof(int));

	list_sort(sock_gres_list, _sock_gres_sort);
	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))) {
		bool sufficient_gres;
		uint64_t max_gres = 0, rem_gres = 0;

		job_specs = sock_gres->job_specs;
		if (!job_specs)
			continue;
		if (job_specs->gres_per_job &&
		    (job_specs->total_gres < job_specs->gres_per_job)) {
			rem_gres = job_specs->gres_per_job -
				   job_specs->total_gres;
		}

		/*
		 * gres_plugin_job_core_filter2() sets sock_gres->max_node_gres
		 * for mem_per_gres enforcement; use it to set GRES limit for
		 * this node (max_gres).
		 */
		if (sock_gres->max_node_gres) {
			if (rem_gres && (rem_gres < sock_gres->max_node_gres))
				max_gres = rem_gres;
			else
				max_gres = sock_gres->max_node_gres;
		}
		rem_nodes = MAX(rem_nodes, 1);
		rem_sockets = MAX(1, mc_ptr->sockets_per_node);
		if (max_gres &&
		    ((job_specs->gres_per_node > max_gres) ||
		     ((job_specs->gres_per_socket * rem_sockets) > max_gres))) {
			*max_tasks_this_node = 0;
			break;
		}
		if (job_specs->gres_per_node && job_specs->gres_per_task) {
			max_tasks = job_specs->gres_per_node /
				    job_specs->gres_per_task;
			if ((max_tasks == 0) ||
			    (max_tasks > *max_tasks_this_node) ||
			    (max_tasks < *min_tasks_this_node)) {
				*max_tasks_this_node = 0;
				break;
			}
			if ((*max_tasks_this_node == NO_VAL) ||
			    (*max_tasks_this_node > max_tasks))
				*max_tasks_this_node = max_gres;
		}

		min_core_cnt = MAX(*min_tasks_this_node, 1) *
			       MAX(mc_ptr->cpus_per_task, 1);
		min_core_cnt = (min_core_cnt + cpus_per_core - 1) /
			       cpus_per_core;

		if (job_specs->cpus_per_gres)
			cpus_per_gres = job_specs->cpus_per_gres;
		else if (job_specs->ntasks_per_gres &&
			 (job_specs->ntasks_per_gres != NO_VAL16))
			cpus_per_gres = job_specs->ntasks_per_gres *
				mc_ptr->cpus_per_task;
		else
			cpus_per_gres = job_specs->def_cpus_per_gres;

		/* Filter out unusable GRES by socket */
		avail_cores_tot = 0;
		cnt_avail_total = sock_gres->cnt_any_sock;
		sufficient_gres = false;
		for (s = 0; s < sockets; s++)
			socket_index[s] = s;
		qsort_r(socket_index, sockets, sizeof(int),
			_sort_sockets_by_avail_cores, avail_cores_per_sock);

		for (j = 0; j < sockets; j++) {
			/*
			 * Test for sufficient gres_per_socket
			 *
			 * Start with socket with most cores available,
			 * so we know that we have max number of cores on socket
			 * with allocated GRES.
			 */
			s = socket_index[j];

			if (sock_gres->cnt_by_sock) {
				cnt_avail_sock = sock_gres->cnt_by_sock[s];
			} else
				cnt_avail_sock = 0;

			/*
			 * If enforce binding number of gres allocated per
			 * socket has to be limited by cpus_per_gres
			 */
			if ((enforce_binding || first_pass) && cpus_per_gres) {
				int max_gres_socket = (avail_cores_per_sock[s] *
						       cpus_per_core) /
						      cpus_per_gres;
				cnt_avail_sock = MIN(cnt_avail_sock,
						     max_gres_socket);
			}

			tot_gres_sock = sock_gres->cnt_any_sock +
					cnt_avail_sock;
			if ((job_specs->gres_per_socket > tot_gres_sock) ||
			    (tot_gres_sock == 0)) {
				/*
				 * Insufficient GRES on this socket
				 * GRES removed here won't be used in 2nd pass
				 */
				if (((job_specs->gres_per_socket >
				      tot_gres_sock) ||
				     enforce_binding) &&
				    sock_gres->cnt_by_sock) {
					sock_gres->total_cnt -=
						sock_gres->cnt_by_sock[s];
					sock_gres->cnt_by_sock[s] = 0;
				}
				if (first_pass &&
				    (tot_core_cnt > min_core_cnt)) {
					for (c = cores_per_socket - 1;
					     c >= 0; c--) {
						i = (s * cores_per_socket) + c;
						if (!bit_test(avail_core, i))
							continue;
						bit_clear(avail_core, i);

						avail_cores_per_sock[s]--;
						if (bit_set_count(avail_core) *
						    cpus_per_core <
						    *avail_cpus) {
							*avail_cpus -=
								cpus_per_core;
						}
						if (--tot_core_cnt <=
						    min_core_cnt)
							break;
					}
				}
			}

			avail_cores_tot += avail_cores_per_sock[s];
			/* Test for available cores on this socket */
			if ((enforce_binding || first_pass) &&
			    (avail_cores_per_sock[s] == 0))
				continue;

			cnt_avail_total += cnt_avail_sock;
			if (!sufficient_gres) {
				req_sock[s] = true;
				sock_cnt++;
			}

			if (job_specs->gres_per_node &&
			    (cnt_avail_total >= job_specs->gres_per_node) &&
			    !sock_gres->cnt_any_sock) {
				/*
				 * Sufficient gres will leave remaining CPUs as
				 * !req_sock. We do this only when we
				 * collected enough and all collected gres of
				 * considered type are bound to socket.
				 */
				sufficient_gres = true;
			}
		}

		if (cpus_per_gres) {
			max_gres = *avail_cpus / cpus_per_gres;
			cnt_avail_total = MIN(cnt_avail_total, max_gres);
		}
		if ((cnt_avail_total == 0) ||
		    (job_specs->gres_per_node > cnt_avail_total) ||
		    (job_specs->gres_per_task > cnt_avail_total)) {
			*max_tasks_this_node = 0;
		}
		if (job_specs->gres_per_task) {
			max_tasks = cnt_avail_total / job_specs->gres_per_task;
			*max_tasks_this_node = MIN(*max_tasks_this_node,
						   max_tasks);
		}

		/*
		 * min_tasks_this_node and max_tasks_this_node must be multiple
		 * of original min_tasks_this_node value. This is to support
		 * ntasks_per_* option and we just need to select a count of
		 * tasks, sockets, etc. Round the values down.
		 */
		*min_tasks_this_node = (*min_tasks_this_node / task_cnt_incr) *
				       task_cnt_incr;
		*max_tasks_this_node = (*max_tasks_this_node / task_cnt_incr) *
				       task_cnt_incr;

		if (*max_tasks_this_node == 0)
			break;

		/*
		 * Remove cores on not required sockets when enforce-binding,
		 * this has to happen also when max_tasks_this_node == NO_VAL
		 */
		if (enforce_binding || first_pass) {
			for (s = 0; s < sockets; s++) {
				if (req_sock[s])
					continue;
				for (c = cores_per_socket - 1; c >= 0; c--) {
					i = (s * cores_per_socket) + c;
					if (!bit_test(avail_core, i))
						continue;
					bit_clear(avail_core, i);
					if (bit_set_count(avail_core) *
					    cpus_per_core < *avail_cpus) {
						*avail_cpus -= cpus_per_core;
					}
					avail_cores_tot--;
					avail_cores_per_sock[s]--;
				}
			}
		}

		if (*max_tasks_this_node == NO_VAL) {
			if (cpus_per_gres) {
				i = *avail_cpus / cpus_per_gres;
				sock_gres->total_cnt =
					MIN(i, sock_gres->total_cnt);
			}
			log_flag(GRES, "%s: max_tasks_this_node is set to NO_VAL, won't clear non-needed cores",
				 __func__);
			continue;
		}
		if (*max_tasks_this_node < *min_tasks_this_node) {
			error("%s: min_tasks_this_node:%u > max_tasks_this_node:%u",
			      __func__,
			      *min_tasks_this_node,
			      *max_tasks_this_node);
		}

		/*
		 * Determine how many cores are needed for this job.
		 * Consider rounding errors if cpus_per_task not divisible
		 * by cpus_per_core
		 */
		req_cores = *max_tasks_this_node;
		if (mc_ptr->cpus_per_task) {
			int threads_per_core, removed_tasks = 0;
			int efctv_cpt = mc_ptr->cpus_per_task;

			if (mc_ptr->threads_per_core)
				threads_per_core =
					MIN(cpus_per_core,
					    mc_ptr->threads_per_core);
			else
				threads_per_core = cpus_per_core;

			if ((mc_ptr->ntasks_per_core == 1) &&
			    (efctv_cpt % threads_per_core)) {
				efctv_cpt /= threads_per_core;
				efctv_cpt++;
				efctv_cpt *= threads_per_core;
			}

			req_cores *= efctv_cpt;

			while (*max_tasks_this_node >= *min_tasks_this_node) {
				/* round up by full threads per core */
				req_cores += threads_per_core - 1;
				req_cores /= threads_per_core;
				if (req_cores <= avail_cores_tot) {
					if (removed_tasks)
						log_flag(GRES, "%s: settings required_cores=%d by max_tasks_this_node=%u(reduced=%d) cpus_per_task=%d cpus_per_core=%d threads_per_core:%d",
							 __func__,
							 req_cores,
							 *max_tasks_this_node,
							 removed_tasks,
							 mc_ptr->cpus_per_task,
							 cpus_per_core,
							 mc_ptr->
							 threads_per_core);
					break;
				}
				removed_tasks++;
				(*max_tasks_this_node)--;
				req_cores = *max_tasks_this_node;
				req_cores *= efctv_cpt;
			}
		}
		if (cpus_per_gres) {
			if (job_specs->gres_per_node) {
				i = job_specs->gres_per_node;
				log_flag(GRES, "%s: estimating req_cores gres_per_node=%"PRIu64,
					 __func__, job_specs->gres_per_node);
			} else if (job_specs->gres_per_socket) {
				i = job_specs->gres_per_socket * sock_cnt;
				log_flag(GRES, "%s: estimating req_cores gres_per_socket=%"PRIu64,
					 __func__, job_specs->gres_per_socket);
			} else if (job_specs->gres_per_task) {
				i = job_specs->gres_per_task *
				    *max_tasks_this_node;
				log_flag(GRES, "%s: estimating req_cores max_tasks_this_node=%u gres_per_task=%"PRIu64,
					 __func__,
					 *max_tasks_this_node,
					 job_specs->gres_per_task);
			} else if (cnt_avail_total) {
				i = cnt_avail_total;
				log_flag(GRES, "%s: estimating req_cores cnt_avail_total=%"PRIu64,
					 __func__, cnt_avail_total);
			} else {
				i = 1;
				log_flag(GRES, "%s: estimating req_cores default to 1 task",
					 __func__);
			}
			i *= cpus_per_gres;
			i = (i + cpus_per_core - 1) / cpus_per_core;
			if (req_cores < i)
				log_flag(GRES, "%s: Increasing req_cores=%d from cpus_per_gres=%d cpus_per_core=%"PRIu16,
					 __func__, i, cpus_per_gres,
					 cpus_per_core);
			req_cores = MAX(req_cores, i);
		}

		if (req_cores > avail_cores_tot) {
			log_flag(GRES, "%s: Job cannot run on node req_cores:%d > aval_cores_tot:%d",
				 __func__, req_cores, avail_cores_tot);
			*max_tasks_this_node = 0;
			break;
		}

		/*
		 * Clear extra avail_core bits on sockets we don't need
		 * up to required number of cores based on max_tasks_this_node.
		 * In case of enforce-binding those are already cleared.
		 */
		if ((avail_cores_tot > req_cores) &&
		     !enforce_binding && !first_pass) {
			for (s = 0; s < sockets; s++) {
				if (avail_cores_tot == req_cores)
					break;
				if (req_sock[s])
					continue;
				for (c = cores_per_socket - 1; c >= 0; c--) {
					i = (s * cores_per_socket) + c;
					if (!bit_test(avail_core, i))
						continue;
					bit_clear(avail_core, i);
					if (bit_set_count(avail_core) *
					    cpus_per_core < *avail_cpus) {
						*avail_cpus -= cpus_per_core;
					}
					avail_cores_tot--;
					avail_cores_per_sock[s]--;
					if (avail_cores_tot == req_cores)
						break;
				}
			}
		}

		/*
		 * Clear extra avail_core bits on sockets we do need, but
		 * spread them out so that every socket has some cores
		 * available to use with the nearby GRES that we do need.
		 */
		while (avail_cores_tot > req_cores) {
			full_socket = -1;
			for (s = 0; s < sockets; s++) {
				if (avail_cores_tot == req_cores)
					break;
				if (!req_sock[s] ||
				    (avail_cores_per_sock[s] == 0))
					continue;
				if ((full_socket == -1) ||
				    (avail_cores_per_sock[full_socket] <
				     avail_cores_per_sock[s])) {
					full_socket = s;
				}
			}
			if (full_socket == -1)
				break;
			s = full_socket;
			for (c = cores_per_socket - 1; c >= 0; c--) {
				i = (s * cores_per_socket) + c;
				if (!bit_test(avail_core, i))
					continue;
				bit_clear(avail_core, i);
				if (bit_set_count(avail_core) * cpus_per_core <
				    *avail_cpus) {
					*avail_cpus -= cpus_per_core;
				}
				avail_cores_per_sock[s]--;
				avail_cores_tot--;
				break;
			}
		}
		if (cpus_per_gres) {
			i = *avail_cpus / cpus_per_gres;
			sock_gres->total_cnt = MIN(i, sock_gres->total_cnt);
			if ((job_specs->gres_per_node > sock_gres->total_cnt) ||
			    (job_specs->gres_per_task > sock_gres->total_cnt)) {
				*max_tasks_this_node = 0;
			}
		}
	}
	list_iterator_destroy(sock_gres_iter);
	xfree(avail_cores_per_sock);
	xfree(req_sock);
	xfree(socket_index);

	if ((mc_ptr->cpus_per_task > 1) ||
	    !(slurm_conf.select_type_param & CR_ONE_TASK_PER_CORE)) {
		/*
		 * Only adjust *avail_cpus for the maximum task count if
		 * cpus_per_task is explicitly set. There is currently no way
		 * to tell if cpus_per_task==1 is explicitly set by the job
		 * when SelectTypeParameters includes CR_ONE_TASK_PER_CORE.
		 */
		*avail_cpus = MIN(*avail_cpus,
				  *max_tasks_this_node * mc_ptr->cpus_per_task);
	}
}

/*
 * Return the maximum number of tasks that can be started on a node with
 * sock_gres_list (per-socket GRES details for some node)
 */
extern uint32_t gres_plugin_get_task_limit(List sock_gres_list)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	uint32_t max_tasks = NO_VAL;
	uint64_t task_limit;

	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))) {
		xassert(sock_gres->job_specs);
		if (sock_gres->job_specs->gres_per_task == 0)
			continue;
		task_limit = sock_gres->total_cnt /
			     sock_gres->job_specs->gres_per_task;
		max_tasks = MIN(max_tasks, task_limit);
	}
	list_iterator_destroy(sock_gres_iter);

	return max_tasks;
}

/*
 * Return count of sockets allocated to this job on this node
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * RET socket count
 */
static int _get_sock_cnt(struct job_resources *job_res, int node_inx,
			 int job_node_inx)
{
	int core_offset, used_sock_cnt = 0;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int c, i, rc, s;

	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count", __func__);
		return 1;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset", __func__);
		return 1;
	}
	for (s = 0; s < sock_cnt; s++) {
		for (c = 0; c < cores_per_socket_cnt; c++) {
			i = (s * cores_per_socket_cnt) + c;
			if (bit_test(job_res->core_bitmap, (core_offset + i)))
				used_sock_cnt++;
		}
	}
	if (used_sock_cnt == 0) {
		error("%s: No allocated cores found", __func__);
		return 1;
	}
	return used_sock_cnt;
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-job resource specification. Use only socket-local GRES
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * rem_nodes IN - count of nodes remaining to place resources on
 * job_specs IN - job request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * node_specs IN - node resource request specifications
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 * cpus_per_core IN - CPUs per core on this node
 * RET 0:more work, 1:fini
 */
static int _set_job_bits1(struct job_resources *job_res, int node_inx,
			  int job_node_inx, int rem_nodes,
			  sock_gres_t *sock_gres, uint32_t job_id,
			  gres_mc_data_t *tres_mc_ptr, uint16_t cpus_per_core)
{
	int core_offset, gres_cnt;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int c, i, g, rc, s;
	gres_job_state_t *job_specs;
	gres_node_state_t *node_specs;
	int *cores_on_sock = NULL, alloc_gres_cnt = 0;
	int max_gres, pick_gres, total_cores = 0;
	int fini = 0;
	uint16_t cpus_per_gres = 0;

	job_specs = sock_gres->job_specs;
	node_specs = sock_gres->node_specs;
	if (job_specs->gres_per_job == job_specs->total_gres)
		fini = 1;
	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count for job %u on node %d",
		      __func__, job_id, node_inx);
		return rc;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset for job %u on node %d",
		      __func__, job_id, node_inx);
		return rc;
	}
	i = sock_gres->sock_cnt;
	if ((i != 0) && (i != sock_cnt)) {
		error("%s: Inconsistent socket count (%d != %d) for job %u on node %d",
		      __func__, i, sock_cnt, job_id, node_inx);
		sock_cnt = MIN(sock_cnt, i);
	}
	xassert(job_res->core_bitmap);
	if (job_node_inx == 0)
		job_specs->total_gres = 0;
	max_gres = job_specs->gres_per_job - job_specs->total_gres -
		   (rem_nodes - 1);
	cores_on_sock = xcalloc(sock_cnt, sizeof(int));
	gres_cnt = bit_size(job_specs->gres_bit_select[node_inx]);
	for (s = 0; s < sock_cnt; s++) {
		for (c = 0; c < cores_per_socket_cnt; c++) {
			i = (s * cores_per_socket_cnt) + c;
			if (bit_test(job_res->core_bitmap, (core_offset + i))) {
				cores_on_sock[s]++;
				total_cores++;
			}
		}
	}
	if (job_specs->cpus_per_gres) {
		cpus_per_gres = job_specs->cpus_per_gres;
	} else if (job_specs->ntasks_per_gres &&
		   (job_specs->ntasks_per_gres != NO_VAL16)) {
		cpus_per_gres = job_specs->ntasks_per_gres *
				tres_mc_ptr->cpus_per_task;
	}
	if (cpus_per_gres) {
		max_gres = MIN(max_gres,
			       ((total_cores * cpus_per_core) /
				cpus_per_gres));
	}
	if ((max_gres > 1) && (node_specs->link_len == gres_cnt))
		pick_gres  = NO_VAL16;
	else
		pick_gres = max_gres;
	/*
	 * Now pick specific GRES for these sockets.
	 * First select all GRES that we might possibly use, starting with
	 * those not constrained by socket, then contrained by socket.
	 * Then remove those which are not required and not "best".
	 */
	for (s = -1;	/* Socket == - 1 if GRES avail from any socket */
	     ((s < sock_cnt) && (alloc_gres_cnt < pick_gres)); s++) {
		if ((s >= 0) && !cores_on_sock[s])
			continue;
		for (g = 0; ((g < gres_cnt) && (alloc_gres_cnt < pick_gres));
		     g++) {
			if ((s == -1) &&
			    (!sock_gres->bits_any_sock ||
			     !bit_test(sock_gres->bits_any_sock, g)))
				continue;   /* GRES not avail any socket */
			if ((s >= 0) &&
			    (!sock_gres->bits_by_sock ||
			     !sock_gres->bits_by_sock[s] ||
			     !bit_test(sock_gres->bits_by_sock[s], g)))
				continue;   /* GRES not on this socket */
			if (bit_test(node_specs->gres_bit_alloc, g) ||
			    bit_test(job_specs->gres_bit_select[node_inx], g))
				continue;   /* Already allocated GRES */
			bit_set(job_specs->gres_bit_select[node_inx], g);
			job_specs->gres_cnt_node_select[node_inx]++;
			alloc_gres_cnt++;
			job_specs->total_gres++;
		}
	}
	if (alloc_gres_cnt == 0) {
		for (s = 0; ((s < sock_cnt) && (alloc_gres_cnt == 0)); s++) {
			if (cores_on_sock[s])
				continue;
			for (g = 0; g < gres_cnt; g++) {
				if (!sock_gres->bits_by_sock ||
				     !sock_gres->bits_by_sock[s] ||
				     !bit_test(sock_gres->bits_by_sock[s], g))
					continue;   /* GRES not on this socket */
				if (bit_test(node_specs->gres_bit_alloc, g) ||
				    bit_test(job_specs->
					     gres_bit_select[node_inx], g))
					continue;   /* Already allocated GRES */
				bit_set(job_specs->gres_bit_select[node_inx],g);
				job_specs->gres_cnt_node_select[node_inx]++;
				alloc_gres_cnt++;
				job_specs->total_gres++;
				break;
			}
		}
	}
	if (alloc_gres_cnt == 0) {
		error("%s: job %u failed to find any available GRES on node %d",
		      __func__, job_id, node_inx);
	}
	/* Now pick the "best" max_gres GRES with respect to link counts. */
	if (alloc_gres_cnt > max_gres) {
		int best_link_cnt = -1, best_inx = -1;
		for (s = 0; s < gres_cnt; s++) {
			if (!bit_test(job_specs->gres_bit_select[node_inx], s))
				continue;
			for (g = s + 1; g < gres_cnt; g++) {
				if (!bit_test(job_specs->
					      gres_bit_select[node_inx], g))
					continue;
				if (node_specs->links_cnt[s][g] <=
				    best_link_cnt)
					continue;
				best_link_cnt = node_specs->links_cnt[s][g];
				best_inx = s;
			}
		}
		while ((alloc_gres_cnt > max_gres) && (best_link_cnt != -1)) {
			int worst_inx = -1, worst_link_cnt = NO_VAL16;
			for (g = 0; g < gres_cnt; g++) {
				if (g == best_inx)
					continue;
				if (!bit_test(job_specs->
					      gres_bit_select[node_inx], g))
					continue;
				if (node_specs->links_cnt[best_inx][g] >=
				    worst_link_cnt)
					continue;
				worst_link_cnt =
					node_specs->links_cnt[best_inx][g];
				worst_inx = g;
			}
			if (worst_inx == -1) {
				error("%s: error managing links_cnt", __func__);
				break;
			}
			bit_clear(job_specs->gres_bit_select[node_inx],
				  worst_inx);
			job_specs->gres_cnt_node_select[node_inx]--;
			alloc_gres_cnt--;
			job_specs->total_gres--;
		}
	}

	xfree(cores_on_sock);
	if (job_specs->total_gres >= job_specs->gres_per_job)
		fini = 1;
	return fini;
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-job resource specification. Use any GRES on the node
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * job_specs IN - job request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * node_specs IN - node resource request specifications
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 * RET 0:more work, 1:fini
 */
static int _set_job_bits2(struct job_resources *job_res, int node_inx,
			  int job_node_inx, sock_gres_t *sock_gres,
			  uint32_t job_id, gres_mc_data_t *tres_mc_ptr)
{
	int core_offset, gres_cnt;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int i, g, l, rc, s;
	gres_job_state_t *job_specs;
	gres_node_state_t *node_specs;
	int fini = 0;
	int best_link_cnt = 0, best_inx = -1;

	job_specs = sock_gres->job_specs;
	node_specs = sock_gres->node_specs;
	if (job_specs->gres_per_job == job_specs->total_gres) {
		fini = 1;
		return fini;
	}
	if (!job_specs->gres_bit_select ||
	    !job_specs->gres_bit_select[node_inx]) {
		error("%s: gres_bit_select NULL for job %u on node %d",
		      __func__, job_id, node_inx);
		return SLURM_ERROR;
	}
	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count for job %u on node %d",
		      __func__, job_id, node_inx);
		return rc;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset for job %u on node %d",
		      __func__, job_id, node_inx);
		return rc;
	}
	i = sock_gres->sock_cnt;
	if ((i != 0) && (i != sock_cnt)) {
		error("%s: Inconsistent socket count (%d != %d) for job %u on node %d",
		      __func__, i, sock_cnt, job_id, node_inx);
		sock_cnt = MIN(sock_cnt, i);
	}

	/*
	 * Identify the GRES (if any) that we want to use as a basis for
	 * maximizing link count (connectivity of the GRES).
	 */
	xassert(job_res->core_bitmap);
	gres_cnt = bit_size(job_specs->gres_bit_select[node_inx]);
	if ((job_specs->gres_per_job > job_specs->total_gres) &&
	    (node_specs->link_len == gres_cnt)) {
		for (g = 0; g < gres_cnt; g++) {
			if (!bit_test(job_specs->gres_bit_select[node_inx], g))
				continue;
			best_inx = g;
			for (s = 0; s < gres_cnt; s++) {
				best_link_cnt = MAX(node_specs->links_cnt[s][g],
						    best_link_cnt);
			}
			break;
		}
	}

	/*
	 * Now pick specific GRES for these sockets.
	 * Start with GRES available from any socket, then specific sockets
	 */
	for (l = best_link_cnt;
	     ((l >= 0) && (job_specs->gres_per_job > job_specs->total_gres));
	     l--) {
		for (s = -1;   /* Socket == - 1 if GRES avail from any socket */
		     ((s < sock_cnt) &&
		      (job_specs->gres_per_job > job_specs->total_gres)); s++) {
			for (g = 0;
			     ((g < gres_cnt) &&
			      (job_specs->gres_per_job >job_specs->total_gres));
			     g++) {
				if ((l > 0) &&
				    (node_specs->links_cnt[best_inx][g] < l))
					continue;   /* Want better link count */
				if ((s == -1) &&
				    (!sock_gres->bits_any_sock ||
				     !bit_test(sock_gres->bits_any_sock, g)))
					continue;  /* GRES not avail any sock */
				if ((s >= 0) &&
				    (!sock_gres->bits_by_sock ||
				     !sock_gres->bits_by_sock[s] ||
				     !bit_test(sock_gres->bits_by_sock[s], g)))
					continue;  /* GRES not on this socket */
				if (bit_test(node_specs->gres_bit_alloc, g) ||
				    bit_test(job_specs->gres_bit_select[node_inx],
					     g))
					continue;   /* Already allocated GRES */
				bit_set(job_specs->gres_bit_select[node_inx],g);
				job_specs->gres_cnt_node_select[node_inx]++;
				job_specs->total_gres++;
			}
		}
	}
	if (job_specs->gres_per_job == job_specs->total_gres)
		fini = 1;
	return fini;
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-node resource specification
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * job_specs IN - job request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * node_specs IN - node resource request specifications
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 */
static void _set_node_bits(struct job_resources *job_res, int node_inx,
			   int job_node_inx, sock_gres_t *sock_gres,
			   uint32_t job_id, gres_mc_data_t *tres_mc_ptr)
{
	int core_offset, gres_cnt;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int c, i, g, l, rc, s;
	gres_job_state_t *job_specs;
	gres_node_state_t *node_specs;
	int *used_sock = NULL, alloc_gres_cnt = 0;
	int *links_cnt = NULL, best_link_cnt = 0;
	uint64_t gres_per_bit = 1;

	job_specs = sock_gres->job_specs;
	node_specs = sock_gres->node_specs;
	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	i = sock_gres->sock_cnt;
	if ((i != 0) && (i != sock_cnt)) {
		error("%s: Inconsistent socket count (%d != %d) for job %u on node %d",
		      __func__, i, sock_cnt, job_id, node_inx);
		sock_cnt = MIN(sock_cnt, i);
	}

	xassert(job_res->core_bitmap);
	used_sock = xcalloc(sock_cnt, sizeof(int));
	gres_cnt = bit_size(job_specs->gres_bit_select[node_inx]);
	for (s = 0; s < sock_cnt; s++) {
		for (c = 0; c < cores_per_socket_cnt; c++) {
			i = (s * cores_per_socket_cnt) + c;
			if (bit_test(job_res->core_bitmap, (core_offset + i))) {
				used_sock[s]++;
				break;
			}
		}
	}

	/*
	 * Now pick specific GRES for these sockets.
	 * First: Try to place one GRES per socket in this job's allocation.
	 * Second: Try to place additional GRES on allocated sockets.
	 * Third: Use any additional available GRES.
	 */
	if (node_specs->link_len == gres_cnt)
		links_cnt = xcalloc(gres_cnt, sizeof(int));
	if (_shared_gres(sock_gres->plugin_id))
		gres_per_bit = job_specs->gres_per_node;
	for (s = -1;	/* Socket == - 1 if GRES avail from any socket */
	     ((s < sock_cnt) && (alloc_gres_cnt < job_specs->gres_per_node));
	     s++) {
		if ((s >= 0) && !used_sock[s])
			continue;
		for (g = 0; g < gres_cnt; g++) {
			if ((s == -1) &&
			    (!sock_gres->bits_any_sock ||
			     !bit_test(sock_gres->bits_any_sock, g)))
				continue;   /* GRES not avail any socket */
			if ((s >= 0) &&
			    (!sock_gres->bits_by_sock ||
			     !sock_gres->bits_by_sock[s] ||
			     !bit_test(sock_gres->bits_by_sock[s], g)))
				continue;   /* GRES not on this socket */
			if (bit_test(job_specs->gres_bit_select[node_inx], g) ||
			    ((gres_per_bit == 1) &&
			     bit_test(node_specs->gres_bit_alloc, g)))
				continue;   /* Already allocated GRES */
			bit_set(job_specs->gres_bit_select[node_inx], g);
			job_specs->gres_cnt_node_select[node_inx] +=
								gres_per_bit;
			alloc_gres_cnt += gres_per_bit;
			for (l = 0; links_cnt && (l < gres_cnt); l++) {
				if ((l == g) ||
				    bit_test(node_specs->gres_bit_alloc, l))
					continue;
				links_cnt[l] += node_specs->links_cnt[g][l];
			}
			break;
		}
	}

	if (links_cnt) {
		for (l = 0; l < gres_cnt; l++)
			best_link_cnt = MAX(links_cnt[l], best_link_cnt);
		if (best_link_cnt > 4) {
			/* Scale down to reasonable iteration count (<= 4) */
			g = (best_link_cnt + 3) / 4;
			best_link_cnt = 0;
			for (l = 0; l < gres_cnt; l++) {
				links_cnt[l] /= g;
				best_link_cnt = MAX(links_cnt[l],best_link_cnt);
			}
		}
	}

	/*
	 * Try to place additional GRES on allocated sockets. Favor use of
	 * GRES which are best linked to GRES which have already been selected.
	 */
	for (l = best_link_cnt;
	     ((l >= 0) && (alloc_gres_cnt < job_specs->gres_per_node)); l--) {
		for (s = -1;   /* Socket == - 1 if GRES avail from any socket */
		     ((s < sock_cnt) &&
		      (alloc_gres_cnt < job_specs->gres_per_node)); s++) {
			if ((s >= 0) && !used_sock[s])
				continue;
			for (g = 0; g < gres_cnt; g++) {
				if (links_cnt && (links_cnt[g] < l))
					continue;
				if ((s == -1) &&
				    (!sock_gres->bits_any_sock ||
				     !bit_test(sock_gres->bits_any_sock, g)))
					continue;/* GRES not avail any socket */
				if ((s >= 0) &&
				    (!sock_gres->bits_by_sock ||
				     !sock_gres->bits_by_sock[s] ||
				     !bit_test(sock_gres->bits_by_sock[s], g)))
					continue;  /* GRES not on this socket */
				if (bit_test(job_specs->gres_bit_select[node_inx],
					     g) ||
				    ((gres_per_bit == 1) &&
				     bit_test(node_specs->gres_bit_alloc, g)))
					continue;   /* Already allocated GRES */
				bit_set(job_specs->gres_bit_select[node_inx],g);
				job_specs->gres_cnt_node_select[node_inx] +=
								gres_per_bit;
				alloc_gres_cnt += gres_per_bit;
				if (alloc_gres_cnt >= job_specs->gres_per_node)
					break;
			}
		}
	}

	/*
	 * Use any additional available GRES. Again, favor use of GRES
	 * which are best linked to GRES which have already been selected.
	 */
	for (l = best_link_cnt;
	     ((l >= 0) && (alloc_gres_cnt < job_specs->gres_per_node)); l--) {
		for (s = 0;
		     ((s < sock_cnt) &&
		      (alloc_gres_cnt < job_specs->gres_per_node)); s++) {
			if (used_sock[s])
				continue;
			for (g = 0; g < gres_cnt; g++) {
				if (links_cnt && (links_cnt[g] < l))
					continue;
				if (!sock_gres->bits_by_sock ||
				     !sock_gres->bits_by_sock[s] ||
				     !bit_test(sock_gres->bits_by_sock[s], g))
					continue;  /* GRES not on this socket */
				if (bit_test(job_specs->gres_bit_select[node_inx],
					     g) ||
				    ((gres_per_bit == 1) &&
				     bit_test(node_specs->gres_bit_alloc, g)))
					continue;   /* Already allocated GRES */
				bit_set(job_specs->gres_bit_select[node_inx],g);
				job_specs->gres_cnt_node_select[node_inx] +=
								gres_per_bit;
				alloc_gres_cnt += gres_per_bit;
				if (alloc_gres_cnt >= job_specs->gres_per_node)
					break;
			}
		}
	}

	xfree(links_cnt);
	xfree(used_sock);
}

/*
 * Select one specific GRES topo entry (set GRES bitmap) for this job on this
 *	node based upon per-node resource specification
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * job_specs IN - job request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * node_specs IN - node resource request specifications
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 */
static void _pick_specific_topo(struct job_resources *job_res, int node_inx,
				int job_node_inx, sock_gres_t *sock_gres,
				uint32_t job_id, gres_mc_data_t *tres_mc_ptr)
{
	int core_offset;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int c, i, rc, s, t;
	gres_job_state_t *job_specs;
	gres_node_state_t *node_specs;
	int *used_sock = NULL, alloc_gres_cnt = 0;
	uint64_t gres_per_bit;
	bool use_busy_dev = false;

	job_specs = sock_gres->job_specs;
	gres_per_bit = job_specs->gres_per_node;
	node_specs = sock_gres->node_specs;
	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	i = sock_gres->sock_cnt;
	if ((i != 0) && (i != sock_cnt)) {
		error("%s: Inconsistent socket count (%d != %d) for job %u on node %d",
		      __func__, i, sock_cnt, job_id, node_inx);
		sock_cnt = MIN(sock_cnt, i);
	}

	xassert(job_res->core_bitmap);
	used_sock = xcalloc(sock_cnt, sizeof(int));
	for (s = 0; s < sock_cnt; s++) {
		for (c = 0; c < cores_per_socket_cnt; c++) {
			i = (s * cores_per_socket_cnt) + c;
			if (bit_test(job_res->core_bitmap, (core_offset + i))) {
				used_sock[s]++;
				break;
			}
		}
	}

	if ((sock_gres->plugin_id == mps_plugin_id) &&
	    (node_specs->gres_cnt_alloc != 0)) {
		/* We must use the ONE already active GRES of this type */
		use_busy_dev = true;
	}

	/*
	 * Now pick specific GRES for these sockets.
	 * First: Try to select a GRES local to allocated socket with
	 *	sufficient resources.
	 * Second: Use available GRES with sufficient resources.
	 * Third: Use any available GRES.
	 */
	for (s = -1;	/* Socket == - 1 if GRES avail from any socket */
	     (s < sock_cnt) && (alloc_gres_cnt == 0); s++) {
		if ((s >= 0) && !used_sock[s])
			continue;
		for (t = 0; t < node_specs->topo_cnt; t++) {
			if (use_busy_dev &&
			    (node_specs->topo_gres_cnt_alloc[t] == 0))
				continue;
			if (node_specs->topo_gres_cnt_alloc    &&
			    node_specs->topo_gres_cnt_avail    &&
			    ((node_specs->topo_gres_cnt_avail[t] -
			      node_specs->topo_gres_cnt_alloc[t]) <
			     gres_per_bit))
				continue;	/* Insufficient resources */
			if ((s == -1) &&
			    (!sock_gres->bits_any_sock ||
			     !bit_test(sock_gres->bits_any_sock, t)))
				continue;  /* GRES not avail any socket */
			if ((s >= 0) &&
			    (!sock_gres->bits_by_sock ||
			     !sock_gres->bits_by_sock[s] ||
			     !bit_test(sock_gres->bits_by_sock[s], t)))
				continue;   /* GRES not on this socket */
			bit_set(job_specs->gres_bit_select[node_inx], t);
			job_specs->gres_cnt_node_select[node_inx] +=
								gres_per_bit;
			alloc_gres_cnt += gres_per_bit;
			break;
		}
	}

	/* Select available GRES with sufficient resources */
	for (t = 0; (t < node_specs->topo_cnt) && (alloc_gres_cnt == 0); t++) {
		if (use_busy_dev &&
		    (node_specs->topo_gres_cnt_alloc[t] == 0))
			continue;
		if (node_specs->topo_gres_cnt_alloc    &&
		    node_specs->topo_gres_cnt_avail    &&
		    node_specs->topo_gres_cnt_avail[t] &&
		    ((node_specs->topo_gres_cnt_avail[t] -
		      node_specs->topo_gres_cnt_alloc[t]) < gres_per_bit))
			continue;	/* Insufficient resources */
		bit_set(job_specs->gres_bit_select[node_inx], t);
		job_specs->gres_cnt_node_select[node_inx] += gres_per_bit;
		alloc_gres_cnt += gres_per_bit;
		break;
	}

	/* Select available GRES with any resources */
	for (t = 0; (t < node_specs->topo_cnt) && (alloc_gres_cnt == 0); t++) {
		if (node_specs->topo_gres_cnt_alloc    &&
		    node_specs->topo_gres_cnt_avail    &&
		    node_specs->topo_gres_cnt_avail[t])
			continue;	/* No resources */
		bit_set(job_specs->gres_bit_select[node_inx], t);
		job_specs->gres_cnt_node_select[node_inx] += gres_per_bit;
		alloc_gres_cnt += gres_per_bit;
	}

	xfree(used_sock);
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-socket resource specification
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * job_specs IN - job request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * node_specs IN - node resource request specifications
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 */
static void _set_sock_bits(struct job_resources *job_res, int node_inx,
			   int job_node_inx, sock_gres_t *sock_gres,
			   uint32_t job_id, gres_mc_data_t *tres_mc_ptr)
{
	int core_offset, gres_cnt;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;
	int c, i, g, l, rc, s;
	gres_job_state_t *job_specs;
	gres_node_state_t *node_specs;
	int *used_sock = NULL, used_sock_cnt = 0;
	int *links_cnt = NULL, best_link_cnt = 0;

	job_specs = sock_gres->job_specs;
	node_specs = sock_gres->node_specs;
	rc = get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
				   &cores_per_socket_cnt);
	if (rc != SLURM_SUCCESS) {
		error("%s: Invalid socket/core count for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	core_offset = get_job_resources_offset(job_res, job_node_inx, 0, 0);
	if (core_offset < 0) {
		error("%s: Invalid core offset for job %u on node %d",
		      __func__, job_id, node_inx);
		return;
	}
	i = sock_gres->sock_cnt;
	if ((i != 0) && (i != sock_cnt)) {
		error("%s: Inconsistent socket count (%d != %d) for job %u on node %d",
		      __func__, i, sock_cnt, job_id, node_inx);
		sock_cnt = MIN(sock_cnt, i);
	}

	xassert(job_res->core_bitmap);
	used_sock = xcalloc(sock_cnt, sizeof(int));
	gres_cnt = bit_size(job_specs->gres_bit_select[node_inx]);
	for (s = 0; s < sock_cnt; s++) {
		for (c = 0; c < cores_per_socket_cnt; c++) {
			i = (s * cores_per_socket_cnt) + c;
			if (bit_test(job_res->core_bitmap, (core_offset + i))) {
				used_sock[s]++;
				used_sock_cnt++;
				break;
			}
		}
	}
	if (tres_mc_ptr && tres_mc_ptr->sockets_per_node     &&
	    (tres_mc_ptr->sockets_per_node != used_sock_cnt) &&
	    node_specs->gres_bit_alloc && sock_gres->bits_by_sock) {
		if (tres_mc_ptr->sockets_per_node > used_sock_cnt) {
			/* Somehow we have too few sockets in job allocation */
			error("%s: Inconsistent requested/allocated socket count "
			      "(%d > %d) for job %u on node %d",
			      __func__, tres_mc_ptr->sockets_per_node,
			      used_sock_cnt, job_id, node_inx);
			for (s = 0; s < sock_cnt; s++) {
				if (used_sock[s] || !sock_gres->bits_by_sock[s])
					continue;
				/* Determine currently free GRES by socket */
				used_sock[s] = bit_set_count(
						sock_gres->bits_by_sock[s]) -
					       bit_overlap(
						sock_gres->bits_by_sock[s],
						node_specs->gres_bit_alloc);
				if ((used_sock[s] == 0) ||
				    (used_sock[s] < job_specs->gres_per_socket)){
					used_sock[s] = 0;
				} else if (++used_sock_cnt ==
					   tres_mc_ptr->sockets_per_node) {
					break;
				}
			}
		} else {
			/* May have needed extra CPUs, exceeding socket count */
			debug("%s: Inconsistent requested/allocated socket count "
			      "(%d < %d) for job %u on node %d",
			      __func__, tres_mc_ptr->sockets_per_node,
			      used_sock_cnt, job_id, node_inx);
			for (s = 0; s < sock_cnt; s++) {
				if (!used_sock[s] ||
				    !sock_gres->bits_by_sock[s])
					continue;
				/* Determine currently free GRES by socket */
				used_sock[s] = bit_set_count(
						sock_gres->bits_by_sock[s]) -
					       bit_overlap(
						sock_gres->bits_by_sock[s],
						node_specs->gres_bit_alloc);
				if (used_sock[s] == 0)
					used_sock_cnt--;
			}
			/* Exclude sockets with low GRES counts */
			while (tres_mc_ptr->sockets_per_node > used_sock_cnt) {
				int low_sock_inx = -1;
				for (s = sock_cnt - 1; s >= 0; s--) {
					if (used_sock[s] == 0)
						continue;
					if ((low_sock_inx == -1) ||
					    (used_sock[s] <
					     used_sock[low_sock_inx]))
						low_sock_inx = s;
				}
				if (low_sock_inx == -1)
					break;
				used_sock[low_sock_inx] = 0;
				used_sock_cnt--;
			}
		}
	}

	/*
	 * Identify the available GRES with best connectivity
	 * (i.e. higher link_cnt)
	 */
	if (node_specs->link_len == gres_cnt) {
		links_cnt = xcalloc(gres_cnt, sizeof(int));
		for (g = 0; g < gres_cnt; g++) {
			if (bit_test(node_specs->gres_bit_alloc, g))
				continue;
			for (l = 0; l < gres_cnt; l++) {
				if ((l == g) ||
				    bit_test(node_specs->gres_bit_alloc, l))
					continue;
				links_cnt[l] += node_specs->links_cnt[g][l];
			}
		}
		for (l = 0; l < gres_cnt; l++)
			best_link_cnt = MAX(links_cnt[l], best_link_cnt);
		if (best_link_cnt > 4) {
			/* Scale down to reasonable iteration count (<= 4) */
			g = (best_link_cnt + 3) / 4;
			best_link_cnt = 0;
			for (l = 0; l < gres_cnt; l++) {
				links_cnt[l] /= g;
				best_link_cnt = MAX(links_cnt[l],best_link_cnt);
			}
		}
	}

	/*
	 * Now pick specific GRES for these sockets.
	 * Try to use GRES with best connectivity (higher link_cnt values)
	 */
	for (s = 0; s < sock_cnt; s++) {
		if (!used_sock[s])
			continue;
		i = 0;
		for (l = best_link_cnt;
		     ((l >= 0) && (i < job_specs->gres_per_socket)); l--) {
			for (g = 0; g < gres_cnt; g++) {
				if (!sock_gres->bits_by_sock ||
				     !sock_gres->bits_by_sock[s] ||
				     !bit_test(sock_gres->bits_by_sock[s], g))
					continue;  /* GRES not on this socket */
				if (node_specs->gres_bit_alloc &&
				    bit_test(node_specs->gres_bit_alloc, g))
					continue;   /* Already allocated GRES */
				if (job_specs->gres_bit_select[node_inx] &&
				    bit_test(job_specs->gres_bit_select[node_inx],
					     g))
					continue;   /* Already allocated GRES */
				bit_set(job_specs->gres_bit_select[node_inx],g);
				job_specs->gres_cnt_node_select[node_inx]++;
				if (++i == job_specs->gres_per_socket)
					break;
			}
		}
		if ((i < job_specs->gres_per_socket) &&
		    sock_gres->bits_any_sock) {
			/* Add GRES unconstrained by socket as needed */
			for (g = 0; g < gres_cnt; g++) {
				if (!sock_gres->bits_any_sock ||
				    !bit_test(sock_gres->bits_any_sock, g))
					continue;  /* GRES not on this socket */
				if (node_specs->gres_bit_alloc &&
				    bit_test(node_specs->gres_bit_alloc, g))
					continue;   /* Already allocated GRES */
				if (job_specs->gres_bit_select[node_inx] &&
				    bit_test(job_specs->gres_bit_select[node_inx],
					     g))
					continue;   /* Already allocated GRES */
				bit_set(job_specs->gres_bit_select[node_inx],g);
				job_specs->gres_cnt_node_select[node_inx]++;
				if (++i == job_specs->gres_per_socket)
					break;
			}
		}
	}
	xfree(links_cnt);
	xfree(used_sock);
}

/*
 * Select specific GRES (set GRES bitmap) for this job on this node based upon
 *	per-task resource specification
 * job_res IN - job resource allocation
 * node_inx IN - global node index
 * job_node_inx IN - node index for this job's allocation
 * job_specs IN - job request specifications, UPDATED: set bits in
 *		  gres_bit_select
 * node_specs IN - node resource request specifications
 * job_id IN - job ID for logging
 * tres_mc_ptr IN - job's multi-core options
 */
static void _set_task_bits(struct job_resources *job_res, int node_inx,
			   int job_node_inx, sock_gres_t *sock_gres,
			   uint32_t job_id, gres_mc_data_t *tres_mc_ptr,
			   uint32_t **tasks_per_node_socket)
{
	uint16_t sock_cnt = 0;
	int gres_cnt, g, l, s;
	gres_job_state_t *job_specs;
	gres_node_state_t *node_specs;
	uint32_t total_tasks = 0;
	uint64_t total_gres_cnt = 0, total_gres_goal;
	int *links_cnt = NULL, best_link_cnt = 0;

	job_specs = sock_gres->job_specs;
	node_specs = sock_gres->node_specs;
	sock_cnt = sock_gres->sock_cnt;
	gres_cnt = bit_size(job_specs->gres_bit_select[node_inx]);
	if (node_specs->link_len == gres_cnt)
		links_cnt = xcalloc(gres_cnt, sizeof(int));

	/* First pick GRES for acitve sockets */
	for (s = -1;	/* Socket == - 1 if GRES avail from any socket */
	     s < sock_cnt; s++) {
		if ((s > 0) &&
		    (!tasks_per_node_socket[node_inx] ||
		     (tasks_per_node_socket[node_inx][s] == 0)))
			continue;
		total_tasks += tasks_per_node_socket[node_inx][s];
		total_gres_goal = total_tasks * job_specs->gres_per_task;
		for (g = 0; g < gres_cnt; g++) {
			if (total_gres_cnt >= total_gres_goal)
				break;
			if ((s == -1) &&
			    (!sock_gres->bits_any_sock ||
			     !bit_test(sock_gres->bits_any_sock, g)))
				continue;  /* GRES not avail any sock */
			if ((s >= 0) &&
			    (!sock_gres->bits_by_sock ||
			     !sock_gres->bits_by_sock[s] ||
			     !bit_test(sock_gres->bits_by_sock[s], g)))
				continue;   /* GRES not on this socket */
			if (bit_test(node_specs->gres_bit_alloc, g))
				continue;   /* Already allocated GRES */
			if (bit_test(node_specs->gres_bit_alloc, g) ||
			    bit_test(job_specs->gres_bit_select[node_inx], g))
				continue;   /* Already allocated GRES */
			bit_set(job_specs->gres_bit_select[node_inx], g);
			job_specs->gres_cnt_node_select[node_inx]++;
			total_gres_cnt++;
			for (l = 0; links_cnt && (l < gres_cnt); l++) {
				if ((l == g) ||
				    bit_test(node_specs->gres_bit_alloc, l))
					continue;
				links_cnt[l] += node_specs->links_cnt[g][l];
			}
		}
	}

	if (links_cnt) {
		for (l = 0; l < gres_cnt; l++)
			best_link_cnt = MAX(links_cnt[l], best_link_cnt);
		if (best_link_cnt > 4) {
			/* Scale down to reasonable iteration count (<= 4) */
			g = (best_link_cnt + 3) / 4;
			best_link_cnt = 0;
			for (l = 0; l < gres_cnt; l++) {
				links_cnt[l] /= g;
				best_link_cnt = MAX(links_cnt[l],best_link_cnt);
			}
		}
	}

	/*
	 * Next pick additional GRES as needed. Favor use of GRES which
	 * are best linked to GRES which have already been selected.
	 */
	total_gres_goal = total_tasks * job_specs->gres_per_task;
	for (l = best_link_cnt;
	     ((l >= 0) && (total_gres_cnt < total_gres_goal)); l--) {
		for (s = -1;   /* Socket == - 1 if GRES avail from any socket */
		     ((s < sock_cnt) && (total_gres_cnt < total_gres_goal));
		     s++) {
			for (g = 0;
			     ((g < gres_cnt) &&
			      (total_gres_cnt < total_gres_goal)); g++) {
				if (links_cnt && (links_cnt[g] < l))
					continue;
				if ((s == -1) &&
				    (!sock_gres->bits_any_sock ||
				     !bit_test(sock_gres->bits_any_sock, g)))
					continue;  /* GRES not avail any sock */
				if ((s >= 0) &&
				    (!sock_gres->bits_by_sock ||
				     !sock_gres->bits_by_sock[s] ||
				     !bit_test(sock_gres->bits_by_sock[s], g)))
					continue;  /* GRES not on this socket */
				if (bit_test(node_specs->gres_bit_alloc, g) ||
				    bit_test(job_specs->gres_bit_select[node_inx],
					     g))
					continue;   /* Already allocated GRES */
				bit_set(job_specs->gres_bit_select[node_inx],g);
				job_specs->gres_cnt_node_select[node_inx]++;
				total_gres_cnt++;
			}
		}
	}
	xfree(links_cnt);

	if (total_gres_cnt < total_gres_goal) {
		/* Something bad happened on task layout for this GRES type */
		error("%s: Insufficient gres/%s allocated for job %u on node_inx %u "
		      "(%"PRIu64" < %"PRIu64")",  __func__,
		      sock_gres->gres_name, job_id, node_inx,
		      total_gres_cnt, total_gres_goal);
	}
}

/* Build array to identify task count for each node-socket pair */
static uint32_t **_build_tasks_per_node_sock(struct job_resources *job_res,
					     uint8_t overcommit,
					     gres_mc_data_t *tres_mc_ptr,
					     node_record_t *node_table_ptr)
{
	uint32_t **tasks_per_node_socket;
	int i, i_first, i_last, j, node_cnt, job_node_inx = 0;
	int c, s, core_offset;
	int cpus_per_task = 1, cpus_per_node, cpus_per_core;
	int task_per_node_limit = 0;
	int32_t rem_tasks, excess_tasks;
	uint16_t sock_cnt = 0, cores_per_socket_cnt = 0;

	rem_tasks = tres_mc_ptr->ntasks_per_job;
	node_cnt = bit_size(job_res->node_bitmap);
	tasks_per_node_socket = xcalloc(node_cnt, sizeof(uint32_t *));
	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first != -1)
		i_last  = bit_fls(job_res->node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		int tasks_per_node = 0;
		if (!bit_test(job_res->node_bitmap, i))
			continue;
		if (get_job_resources_cnt(job_res, job_node_inx, &sock_cnt,
					  &cores_per_socket_cnt)) {
			error("%s: failed to get socket/core count", __func__);
			/* Set default of 1 task on socket 0 */
			tasks_per_node_socket[i] = xmalloc(sizeof(uint32_t));
			tasks_per_node_socket[i][0] = 1;
			rem_tasks--;
			continue;
		}
		tasks_per_node_socket[i] = xcalloc(sock_cnt, sizeof(uint32_t));
		if (tres_mc_ptr->ntasks_per_node) {
			task_per_node_limit = tres_mc_ptr->ntasks_per_node;
		} else if (job_res->tasks_per_node &&
			   job_res->tasks_per_node[job_node_inx]) {
			task_per_node_limit =
				job_res->tasks_per_node[job_node_inx];
		} else {
			/*
			 * NOTE: We should never get here.
			 * cpus_per_node reports CPUs actually used by this
			 * job on this node. Divide by cpus_per_task to yield
			 * valid task count on this node. This can be bad on
			 * cores with more than one thread and job fails to
			 * use all threads.
			 */
			error("%s: tasks_per_node not set", __func__);
			cpus_per_node = get_job_resources_cpus(job_res,
							       job_node_inx);
			if (cpus_per_node < 1) {
				error("%s: failed to get cpus_per_node count",
				      __func__);
				/* Set default of 1 task on socket 0 */
				tasks_per_node_socket[i][0] = 1;
				rem_tasks--;
				continue;
			}
			if (tres_mc_ptr->cpus_per_task)
				cpus_per_task = tres_mc_ptr->cpus_per_task;
			else
				cpus_per_task = 1;
			task_per_node_limit = cpus_per_node / cpus_per_task;
		}
		core_offset = get_job_resources_offset(job_res, job_node_inx++,
						       0, 0);
		if (node_table_ptr[i].cores) {
			cpus_per_core = node_table_ptr[i].cpus /
					node_table_ptr[i].cores;
		} else
			cpus_per_core = 1;
		for (s = 0; s < sock_cnt; s++) {
			int tasks_per_socket = 0, tpc, skip_cores = 0;
			for (c = 0; c < cores_per_socket_cnt; c++) {
				j = (s * cores_per_socket_cnt) + c;
				j += core_offset;
				if (!bit_test(job_res->core_bitmap, j))
					continue;
				if (skip_cores > 0) {
					skip_cores--;
					continue;
				}
				if (tres_mc_ptr->ntasks_per_core) {
					tpc = tres_mc_ptr->ntasks_per_core;
				} else {
					tpc = cpus_per_core / cpus_per_task;
					if (tpc < 1) {
						tpc = 1;
						skip_cores = cpus_per_task /
							     cpus_per_core;
						skip_cores--;	/* This core */
					}
					/* Start with 1 task per core */
				}
				tasks_per_node_socket[i][s] += tpc;
				tasks_per_node += tpc;
				tasks_per_socket += tpc;
				rem_tasks -= tpc;
				if (task_per_node_limit) {
					if (tasks_per_node >
					    task_per_node_limit) {
						excess_tasks = tasks_per_node -
							task_per_node_limit;
						tasks_per_node_socket[i][s] -=
							excess_tasks;
						rem_tasks += excess_tasks;
					}
					if (tasks_per_node >=
					    task_per_node_limit) {
						s = sock_cnt;
						break;
					}
				}
				/* NOTE: No support for ntasks_per_board */
				if (tres_mc_ptr->ntasks_per_socket) {
					if (tasks_per_socket >
					    tres_mc_ptr->ntasks_per_socket) {
						excess_tasks = tasks_per_socket-
						 tres_mc_ptr->ntasks_per_socket;
						tasks_per_node_socket[i][s] -=
							excess_tasks;
						rem_tasks += excess_tasks;
					}
					if (tasks_per_socket >=
					    tres_mc_ptr->ntasks_per_socket) {
						break;
					}
				}
			}
		}
	}
	while ((rem_tasks > 0) && overcommit) {
		for (i = i_first; (rem_tasks > 0) && (i <= i_last); i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			for (s = 0; (rem_tasks > 0) && (s < sock_cnt); s++) {
				for (c = 0; c < cores_per_socket_cnt; c++) {
					j = (s * cores_per_socket_cnt) + c;
					if (!bit_test(job_res->core_bitmap, j))
						continue;
					tasks_per_node_socket[i][s]++;
					rem_tasks--;
					break;
				}
			}
		}
	}
	if (rem_tasks > 0)	/* This should never happen */
		error("%s: rem_tasks not zero (%d > 0)", __func__, rem_tasks);

	return tasks_per_node_socket;
}

static void _free_tasks_per_node_sock(uint32_t **tasks_per_node_socket,
				      int node_cnt)
{
	int n;

	if (!tasks_per_node_socket)
		return;

	for (n = 0; n < node_cnt; n++)
		xfree(tasks_per_node_socket[n]);
	xfree(tasks_per_node_socket);
}

/* Return the count of tasks for a job on a given node */
static uint32_t _get_task_cnt_node(uint32_t **tasks_per_node_socket,
				   int node_inx, int sock_cnt)
{
	uint32_t task_cnt = 0;
	int s;

	if (!tasks_per_node_socket || !tasks_per_node_socket[node_inx]) {
		error("%s: tasks_per_node_socket is NULL", __func__);
		return 1;	/* Best guess if no data structure */
	}

	for (s = 0; s < sock_cnt; s++)
		task_cnt += tasks_per_node_socket[node_inx][s];

	return task_cnt;
}

/* Determine maximum GRES allocation count on this node; no topology */
static uint64_t _get_job_cnt(sock_gres_t *sock_gres,
			     gres_node_state_t *node_specs, int rem_node_cnt)
{
	uint64_t avail_gres, max_gres;
	gres_job_state_t *job_specs = sock_gres->job_specs;

	avail_gres = node_specs->gres_cnt_avail - node_specs->gres_cnt_alloc;
	/* Ensure at least one GRES per node on remaining nodes */
	max_gres = job_specs->gres_per_job - job_specs->total_gres -
		   (rem_node_cnt - 1);
	max_gres = MIN(avail_gres, max_gres);

	return max_gres;
}

/* Return count of GRES on this node */
static int _get_gres_node_cnt(gres_node_state_t *node_specs, int node_inx)
{
	int i, gres_cnt = 0;

	if (node_specs->gres_bit_alloc) {
		gres_cnt = bit_size(node_specs->gres_bit_alloc);
		return gres_cnt;
	}

	/* This logic should be redundant */
	if (node_specs->topo_gres_bitmap && node_specs->topo_gres_bitmap[0]) {
		gres_cnt = bit_size(node_specs->topo_gres_bitmap[0]);
		return gres_cnt;
	}

	/* This logic should also be redundant */
	gres_cnt = 0;
	for (i = 0; i < node_specs->topo_cnt; i++)
		gres_cnt += node_specs->topo_gres_cnt_avail[i];
	return gres_cnt;
}

/*
 * Make final GRES selection for the job
 * sock_gres_list IN - per-socket GRES details, one record per allocated node
 * job_id IN - job ID for logging
 * job_res IN - job resource allocation
 * overcommit IN - job's ability to overcommit resources
 * tres_mc_ptr IN - job's multi-core options
 * node_table_ptr IN - slurmctld's node records
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_core_filter4(List *sock_gres_list, uint32_t job_id,
					struct job_resources *job_res,
					uint8_t overcommit,
					gres_mc_data_t *tres_mc_ptr,
					node_record_t *node_table_ptr)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	gres_job_state_t *job_specs;
	gres_node_state_t *node_specs;
	int i, i_first, i_last, node_inx = -1, gres_cnt;
	int node_cnt, rem_node_cnt;
	int job_fini = -1;	/* -1: not applicable, 0: more work, 1: fini */
	uint32_t **tasks_per_node_socket = NULL;
	int rc = SLURM_SUCCESS;

	if (!job_res || !job_res->node_bitmap)
		return SLURM_ERROR;

	node_cnt = bit_size(job_res->node_bitmap);
	rem_node_cnt = bit_set_count(job_res->node_bitmap);
	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first != -1)
		i_last  = bit_fls(job_res->node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(job_res->node_bitmap, i))
			continue;
		sock_gres_iter =
			list_iterator_create(sock_gres_list[++node_inx]);
		while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))){
			job_specs = sock_gres->job_specs;
			node_specs = sock_gres->node_specs;
			if (!job_specs || !node_specs)
				continue;
			if (job_specs->gres_per_task &&	/* Data needed */
			    !tasks_per_node_socket) {	/* Not built yet */
				tasks_per_node_socket =
					_build_tasks_per_node_sock(job_res,
								overcommit,
								tres_mc_ptr,
								node_table_ptr);
			}
			if (job_specs->total_node_cnt == 0) {
				job_specs->total_node_cnt = node_cnt;
				job_specs->total_gres = 0;
			}
			if (!job_specs->gres_cnt_node_select) {
				job_specs->gres_cnt_node_select =
					xcalloc(node_cnt, sizeof(uint64_t));
			}
			if (i == i_first)	/* Reinitialize counter */
				job_specs->total_gres = 0;

			if (node_specs->topo_cnt == 0) {
				/* No topology, just set a count */
				if (job_specs->gres_per_node) {
					job_specs->gres_cnt_node_select[i] =
						job_specs->gres_per_node;
				} else if (job_specs->gres_per_socket) {
					job_specs->gres_cnt_node_select[i] =
						job_specs->gres_per_socket;
					job_specs->gres_cnt_node_select[i] *=
						_get_sock_cnt(job_res, i,
							      node_inx);
				} else if (job_specs->gres_per_task) {
					job_specs->gres_cnt_node_select[i] =
						job_specs->gres_per_task;
					job_specs->gres_cnt_node_select[i] *=
						_get_task_cnt_node(
						tasks_per_node_socket, i,
						node_table_ptr[i].tot_sockets);
				} else if (job_specs->gres_per_job) {
					job_specs->gres_cnt_node_select[i] =
						_get_job_cnt(sock_gres,
							     node_specs,
							     rem_node_cnt);
				}
				job_specs->total_gres +=
					job_specs->gres_cnt_node_select[i];
				continue;
			}

			/* Working with topology, need to pick specific GRES */
			if (!job_specs->gres_bit_select) {
				job_specs->gres_bit_select =
					xcalloc(node_cnt, sizeof(bitstr_t *));
			}
			gres_cnt = _get_gres_node_cnt(node_specs, node_inx);
			FREE_NULL_BITMAP(job_specs->gres_bit_select[i]);
			job_specs->gres_bit_select[i] = bit_alloc(gres_cnt);
			job_specs->gres_cnt_node_select[i] = 0;

			if (job_specs->gres_per_node &&
			    _shared_gres(sock_gres->plugin_id)) {
				/* gres/mps: select specific topo bit for job */
				_pick_specific_topo(job_res, i, node_inx,
						    sock_gres, job_id,
						    tres_mc_ptr);
			} else if (job_specs->gres_per_node) {
				_set_node_bits(job_res, i, node_inx,
					       sock_gres, job_id, tres_mc_ptr);
			} else if (job_specs->gres_per_socket) {
				_set_sock_bits(job_res, i, node_inx,
					       sock_gres, job_id, tres_mc_ptr);
			} else if (job_specs->gres_per_task) {
				_set_task_bits(job_res, i, node_inx,
					       sock_gres, job_id, tres_mc_ptr,
					       tasks_per_node_socket);
			} else if (job_specs->gres_per_job) {
				uint16_t cpus_per_core;
				cpus_per_core = node_table_ptr[i].cpus /
						node_table_ptr[i].tot_sockets /
						node_table_ptr[i].cores;
				job_fini = _set_job_bits1(job_res, i, node_inx,
					       rem_node_cnt, sock_gres,
					       job_id, tres_mc_ptr,
						cpus_per_core);
			} else {
				error("%s job %u job_spec lacks GRES counter",
				      __func__, job_id);
			}
			if (job_fini == -1) {
				/*
				 * _set_job_bits1() updates total_gres counter,
				 * this handle other cases.
				 */
				job_specs->total_gres +=
					job_specs->gres_cnt_node_select[i];
			}
		}
		rem_node_cnt--;
		list_iterator_destroy(sock_gres_iter);
	}

	if (job_fini == 0) {
		/*
		 * Need more GRES to satisfy gres-per-job option with bitmaps.
		 * This logic will make use of GRES that are not on allocated
		 * sockets and are thus generally less desirable to use.
		 */
		node_inx = -1;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			sock_gres_iter =
				list_iterator_create(sock_gres_list[++node_inx]);
			while ((sock_gres = (sock_gres_t *)
					    list_next(sock_gres_iter))) {
				job_specs = sock_gres->job_specs;
				node_specs = sock_gres->node_specs;
				if (!job_specs || !node_specs)
					continue;
				job_fini = _set_job_bits2(job_res, i, node_inx,
							  sock_gres, job_id,
							  tres_mc_ptr);
				if (job_fini == 1)
					break;
			}
			list_iterator_destroy(sock_gres_iter);
			if (job_fini == 1)
				break;
		}
		if (job_fini == 0) {
			error("%s job %u failed to satisfy gres-per-job counter",
			      __func__, job_id);
			rc = ESLURM_NODE_NOT_AVAIL;
		}
	}
	_free_tasks_per_node_sock(tasks_per_node_socket, node_cnt);

	return rc;
}

/*
 * Determine if job GRES specification includes a tres-per-task specification
 * RET TRUE if any GRES requested by the job include a tres-per-task option
 */
extern bool gres_plugin_job_tres_per_task(List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_data_ptr;
	bool have_gres_per_task = false;

	if (!job_gres_list)
		return false;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		job_data_ptr = (gres_job_state_t *) job_gres_ptr->gres_data;
		if (job_data_ptr->gres_per_task == 0)
			continue;
		have_gres_per_task = true;
		break;
	}
	list_iterator_destroy(job_gres_iter);

	return have_gres_per_task;
}

/*
 * Determine if the job GRES specification includes a mem-per-tres specification
 * RET largest mem-per-tres specification found
 */
extern uint64_t gres_plugin_job_mem_max(List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_data_ptr;
	uint64_t mem_max = 0, mem_per_gres;

	if (!job_gres_list)
		return 0;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		job_data_ptr = (gres_job_state_t *) job_gres_ptr->gres_data;
		if (job_data_ptr->mem_per_gres)
			mem_per_gres = job_data_ptr->mem_per_gres;
		else
			mem_per_gres = job_data_ptr->def_mem_per_gres;
		mem_max = MAX(mem_max, mem_per_gres);
	}
	list_iterator_destroy(job_gres_iter);

	return mem_max;
}

/*
 * Set per-node memory limits based upon GRES assignments
 * RET TRUE if mem-per-tres specification used to set memory limits
 */
extern bool gres_plugin_job_mem_set(List job_gres_list,
				    job_resources_t *job_res)
{
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_data_ptr;
	bool rc = false, first_set = true;
	uint64_t gres_cnt, mem_size, mem_per_gres;
	int i, i_first, i_last, node_off;

	if (!job_gres_list)
		return false;

	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first < 0)
		return false;
	i_last = bit_fls(job_res->node_bitmap);

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		job_data_ptr = (gres_job_state_t *) job_gres_ptr->gres_data;
		if (job_data_ptr->mem_per_gres)
			mem_per_gres = job_data_ptr->mem_per_gres;
		else
			mem_per_gres = job_data_ptr->def_mem_per_gres;
		/*
		 * The logic below is correct because the only mem_per_gres
		 * is --mem-per-gpu adding another option will require change
		 * to take MAX of mem_per_gres for all types.
		 */
		if ((mem_per_gres == 0) || !job_data_ptr->gres_cnt_node_select)
			continue;
		rc = true;
		node_off = -1;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			node_off++;
			if (job_res->whole_node == 1) {
				gres_state_t *node_gres_ptr;
				gres_node_state_t *node_state_ptr;

				node_gres_ptr = list_find_first(
					node_record_table_ptr[i].gres_list,
					_gres_find_id,
					&job_gres_ptr->plugin_id);
				if (!node_gres_ptr)
					continue;
				node_state_ptr = node_gres_ptr->gres_data;
				gres_cnt = node_state_ptr->gres_cnt_avail;
			} else
				gres_cnt =
					job_data_ptr->gres_cnt_node_select[i];
			mem_size = mem_per_gres * gres_cnt;
			if (first_set)
				job_res->memory_allocated[node_off] = mem_size;
			else
				job_res->memory_allocated[node_off] += mem_size;
		}
		first_set = false;
	}
	list_iterator_destroy(job_gres_iter);

	return rc;
}

/*
 * Determine the minimum number of CPUs required to satify the job's GRES
 *	request (based upon total GRES times cpus_per_gres value)
 * node_count IN - count of nodes in job allocation
 * sockets_per_node IN - count of sockets per node in job allocation
 * task_count IN - count of tasks in job allocation
 * job_gres_list IN - job GRES specification
 * RET count of required CPUs for the job
 */
extern int gres_plugin_job_min_cpus(uint32_t node_count,
				    uint32_t sockets_per_node,
				    uint32_t task_count,
				    List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t  *job_data_ptr;
	int tmp, min_cpus = 0;
	uint16_t cpus_per_gres;

	if (!job_gres_list || (list_count(job_gres_list) == 0))
		return 0;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		uint64_t total_gres = 0;
		job_data_ptr = (gres_job_state_t *) job_gres_ptr->gres_data;
		if (job_data_ptr->cpus_per_gres)
			cpus_per_gres = job_data_ptr->cpus_per_gres;
		else
			cpus_per_gres = job_data_ptr->def_cpus_per_gres;
		if (cpus_per_gres == 0)
			continue;
		if (job_data_ptr->gres_per_job) {
			total_gres = job_data_ptr->gres_per_job;
		} else if (job_data_ptr->gres_per_node) {
			total_gres = job_data_ptr->gres_per_node *
				     node_count;
		} else if (job_data_ptr->gres_per_socket) {
			total_gres = job_data_ptr->gres_per_socket *
				     node_count * sockets_per_node;
		} else if (job_data_ptr->gres_per_task) {
			total_gres = job_data_ptr->gres_per_task * task_count;
		} else
			continue;
		tmp = cpus_per_gres * total_gres;
		min_cpus = MAX(min_cpus, tmp);
	}
	list_iterator_destroy(job_gres_iter);
	return min_cpus;
}

/*
 * Determine the minimum number of tasks required to satisfy the job's GRES
 *	request (based upon total GRES times ntasks_per_tres value). If
 *	ntasks_per_tres is not specified, returns 0.
 * node_count IN - count of nodes in job allocation
 * sockets_per_node IN - count of sockets per node in job allocation
 * ntasks_per_tres IN - # of tasks per GPU
 * gres_name IN - (optional) Filter GRES by name. If NULL, check all GRES
 * job_gres_list IN - job GRES specification
 * RET count of required tasks for the job
 */
extern int gres_plugin_job_min_tasks(uint32_t node_count,
				     uint32_t sockets_per_node,
				     uint16_t ntasks_per_tres,
				     char *gres_name,
				     List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *gres_ptr;
	gres_job_state_t *gres_data;
	int tmp, min_tasks = 0;
	uint32_t plugin_id = 0;

	if (ntasks_per_tres == NO_VAL16)
		return 0;

	if (!job_gres_list || (list_count(job_gres_list) == 0))
		return 0;

	if (gres_name && (gres_name[0] != '\0'))
		plugin_id = gres_plugin_build_id(gres_name);

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_ptr = list_next(job_gres_iter))) {
		uint64_t total_gres = 0;
		/* Filter on GRES name, if specified */
		if (plugin_id && (plugin_id != gres_ptr->plugin_id))
			continue;

		gres_data = (gres_job_state_t *)gres_ptr->gres_data;

		if (gres_data->gres_per_job) {
			total_gres = gres_data->gres_per_job;
		} else if (gres_data->gres_per_node) {
			total_gres = gres_data->gres_per_node * node_count;
		} else if (gres_data->gres_per_socket) {
			total_gres = gres_data->gres_per_socket * node_count *
				sockets_per_node;
		} else if (gres_data->gres_per_task) {
			error("%s: gres_per_task and ntasks_per_tres conflict",
			      __func__);
		} else
			continue;

		tmp = ntasks_per_tres * total_gres;
		min_tasks = MAX(min_tasks, tmp);
	}
	list_iterator_destroy(job_gres_iter);
	return min_tasks;
}

/*
 * Determine the minimum number of CPUs required to satify the job's GRES
 *	request on one node
 * sockets_per_node IN - count of sockets per node in job allocation
 * tasks_per_node IN - count of tasks per node in job allocation
 * job_gres_list IN - job GRES specification
 * RET count of required CPUs for the job
 */
extern int gres_plugin_job_min_cpu_node(uint32_t sockets_per_node,
					uint32_t tasks_per_node,
					List job_gres_list)
{
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t  *job_data_ptr;
	int tmp, min_cpus = 0;
	uint16_t cpus_per_gres;

	if (!job_gres_list || (list_count(job_gres_list) == 0))
		return 0;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		uint64_t total_gres = 0;
		job_data_ptr = (gres_job_state_t *) job_gres_ptr->gres_data;
		if (job_data_ptr->cpus_per_gres)
			cpus_per_gres = job_data_ptr->cpus_per_gres;
		else
			cpus_per_gres = job_data_ptr->def_cpus_per_gres;
		if (cpus_per_gres == 0)
			continue;
		if (job_data_ptr->gres_per_node) {
			total_gres = job_data_ptr->gres_per_node;
		} else if (job_data_ptr->gres_per_socket) {
			total_gres = job_data_ptr->gres_per_socket *
				     sockets_per_node;
		} else if (job_data_ptr->gres_per_task) {
			total_gres = job_data_ptr->gres_per_task *
				     tasks_per_node;
		} else
			total_gres = 1;
		tmp = cpus_per_gres * total_gres;
		min_cpus = MAX(min_cpus, tmp);
	}
	list_iterator_destroy(job_gres_iter);
	return min_cpus;
}

/*
 * Determine if specific GRES index on node is available to a job's allocated
 *	cores
 * IN core_bitmap - bitmap of cores allocated to the job on this node
 * IN/OUT alloc_core_bitmap - cores already allocated, NULL if don't care,
 *		updated when the function returns true
 * IN node_gres_ptr - GRES data for this node
 * IN gres_inx - index of GRES being considered for use
 * IN job_gres_ptr - GRES data for this job
 * RET true if available to those core, false otherwise
 */
static bool _cores_on_gres(bitstr_t *core_bitmap, bitstr_t *alloc_core_bitmap,
			   gres_node_state_t *node_gres_ptr, int gres_inx,
			   gres_job_state_t *job_gres_ptr)
{
	int i, avail_cores;

	if ((core_bitmap == NULL) || (node_gres_ptr->topo_cnt == 0))
		return true;

	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		if (!node_gres_ptr->topo_gres_bitmap[i])
			continue;
		if (bit_size(node_gres_ptr->topo_gres_bitmap[i]) < gres_inx)
			continue;
		if (!bit_test(node_gres_ptr->topo_gres_bitmap[i], gres_inx))
			continue;
		if (job_gres_ptr->type_name &&
		    (!node_gres_ptr->topo_type_name[i] ||
		     (job_gres_ptr->type_id != node_gres_ptr->topo_type_id[i])))
			continue;
		if (!node_gres_ptr->topo_core_bitmap[i])
			return true;
		if (bit_size(node_gres_ptr->topo_core_bitmap[i]) !=
		    bit_size(core_bitmap))
			break;
		avail_cores = bit_overlap(node_gres_ptr->topo_core_bitmap[i],
					  core_bitmap);
		if (avail_cores && alloc_core_bitmap) {
			avail_cores -= bit_overlap(node_gres_ptr->
						   topo_core_bitmap[i],
						   alloc_core_bitmap);
			if (avail_cores) {
				bit_or(alloc_core_bitmap,
				       node_gres_ptr->topo_core_bitmap[i]);
			}
		}
		if (avail_cores)
			return true;
	}
	return false;
}

/* Clear any vestigial job gres state. This may be needed on job requeue. */
extern void gres_plugin_job_clear(List job_gres_list)
{
	int i;
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_state_ptr;

	if (job_gres_list == NULL)
		return;

	(void) gres_plugin_init();
	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		job_state_ptr = (gres_job_state_t *) job_gres_ptr->gres_data;
		for (i = 0; i < job_state_ptr->node_cnt; i++) {
			if (job_state_ptr->gres_bit_alloc) {
				FREE_NULL_BITMAP(job_state_ptr->
						 gres_bit_alloc[i]);
			}
			if (job_state_ptr->gres_bit_step_alloc) {
				FREE_NULL_BITMAP(job_state_ptr->
						 gres_bit_step_alloc[i]);
			}
		}
		xfree(job_state_ptr->gres_bit_alloc);
		xfree(job_state_ptr->gres_bit_step_alloc);
		xfree(job_state_ptr->gres_cnt_step_alloc);
		xfree(job_state_ptr->gres_cnt_node_alloc);
		job_state_ptr->node_cnt = 0;
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

static int _job_alloc(void *job_gres_data, void *node_gres_data, int node_cnt,
		      int node_index, int node_offset, char *gres_name,
		      uint32_t job_id, char *node_name,
		      bitstr_t *core_bitmap, uint32_t plugin_id,
		      uint32_t user_id)
{
	int j, sz1, sz2;
	int64_t gres_cnt, i;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bool type_array_updated = false;
	bitstr_t *alloc_core_bitmap = NULL;
	uint64_t gres_per_bit = 1;
	bool log_cnt_err = true;
	char *log_type;
	bool shared_gres = false, use_busy_dev = false;

	/*
	 * Validate data structures. Either job_gres_data->node_cnt and
	 * job_gres_data->gres_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_cnt);
	xassert(node_offset >= 0);
	xassert(job_gres_ptr);
	xassert(node_gres_ptr);

	if (node_gres_ptr->no_consume) {
		job_gres_ptr->total_gres = NO_CONSUME_VAL64;
		return SLURM_SUCCESS;
	}

	if (_shared_gres(plugin_id)) {
		shared_gres = true;
		gres_per_bit = job_gres_ptr->gres_per_node;
	}
	if ((plugin_id == mps_plugin_id) &&
	    (node_gres_ptr->gres_cnt_alloc != 0)) {
		/* We must use the ONE already active GRES of this type */
		use_busy_dev = true;
	}

	if (job_gres_ptr->type_name && !job_gres_ptr->type_name[0])
		xfree(job_gres_ptr->type_name);

	xfree(node_gres_ptr->gres_used);	/* Clear cache */
	if (job_gres_ptr->node_cnt == 0) {
		job_gres_ptr->node_cnt = node_cnt;
		if (job_gres_ptr->gres_bit_alloc) {
			error("gres/%s: job %u node_cnt==0 and gres_bit_alloc is set",
			      gres_name, job_id);
			xfree(job_gres_ptr->gres_bit_alloc);
		}
	}
	/*
	 * These next 2 checks were added long before job resizing was allowed.
	 * They are not errors as we need to keep the original size around for
	 * any steps that might still be out there with the larger size.  If the
	 * job was sized up the gres_plugin_job_merge() function handles the
	 * resize so we are set there.
	 */
	else if (job_gres_ptr->node_cnt < node_cnt) {
		debug2("gres/%s: job %u node_cnt is now larger than it was when allocated from %u to %d",
		      gres_name, job_id, job_gres_ptr->node_cnt, node_cnt);
		if (node_offset >= job_gres_ptr->node_cnt)
			return SLURM_ERROR;
	} else if (job_gres_ptr->node_cnt > node_cnt) {
		debug2("gres/%s: job %u node_cnt is now smaller than it was when allocated %u to %d",
		      gres_name, job_id, job_gres_ptr->node_cnt, node_cnt);
	}

	if (!job_gres_ptr->gres_bit_alloc) {
		job_gres_ptr->gres_bit_alloc = xcalloc(node_cnt,
						       sizeof(bitstr_t *));
	}
	if (!job_gres_ptr->gres_cnt_node_alloc) {
		job_gres_ptr->gres_cnt_node_alloc = xcalloc(node_cnt,
							    sizeof(uint64_t));
	}

	/*
	 * select/cons_tres pre-selects the resources and we just need to update
	 * the data structures to reflect the selected GRES.
	 */
	/* Resuming job */
	if (job_gres_ptr->gres_cnt_node_alloc[node_offset]) {
		gres_cnt = job_gres_ptr->
			gres_cnt_node_alloc[node_offset];
	} else if (job_gres_ptr->gres_bit_alloc[node_offset]) {
		gres_cnt = bit_set_count(
			job_gres_ptr->gres_bit_alloc[node_offset]);
		gres_cnt *= gres_per_bit;
	} else if (job_gres_ptr->total_node_cnt) {
		/* Using pre-selected GRES */
		if (job_gres_ptr->gres_cnt_node_select &&
		    job_gres_ptr->gres_cnt_node_select[node_index]) {
			gres_cnt = job_gres_ptr->
				   gres_cnt_node_select[node_index];
		} else if (job_gres_ptr->gres_bit_select &&
			   job_gres_ptr->gres_bit_select[node_index]) {
			gres_cnt = bit_set_count(
				    job_gres_ptr->gres_bit_select[node_index]);
			gres_cnt *= gres_per_bit;
		} else {
			error("gres/%s: job %u node %s no resources selected",
			      gres_name, job_id, node_name);
			return SLURM_ERROR;
		}
	} else {
		gres_cnt = job_gres_ptr->gres_per_node;
	}

	/*
	 * Check that sufficient resources exist on this node
	 */
	job_gres_ptr->gres_cnt_node_alloc[node_offset] = gres_cnt;
	i = node_gres_ptr->gres_cnt_alloc + gres_cnt;
	if (i > node_gres_ptr->gres_cnt_avail) {
		error("gres/%s: job %u node %s overallocated resources by %"
		      PRIu64", (%"PRIu64" > %"PRIu64")",
		      gres_name, job_id, node_name,
		      i - node_gres_ptr->gres_cnt_avail,
		      i, node_gres_ptr->gres_cnt_avail);
		/* proceed with request, give job what is available */
	}

	if (!node_offset && job_gres_ptr->gres_cnt_step_alloc) {
		uint64_t *tmp = xcalloc(job_gres_ptr->node_cnt,
					sizeof(uint64_t));
		memcpy(tmp, job_gres_ptr->gres_cnt_step_alloc,
		       sizeof(uint64_t) * MIN(node_cnt,
					      job_gres_ptr->node_cnt));
		xfree(job_gres_ptr->gres_cnt_step_alloc);
		job_gres_ptr->gres_cnt_step_alloc = tmp;
	}
	if (job_gres_ptr->gres_cnt_step_alloc == NULL) {
		job_gres_ptr->gres_cnt_step_alloc =
			xcalloc(job_gres_ptr->node_cnt, sizeof(uint64_t));
	}

	/*
	 * Select and/or allocate specific resources for this job.
	 */
	if (job_gres_ptr->gres_bit_alloc[node_offset]) {
		/*
		 * Restarted slurmctld with active job or resuming a suspended
		 * job. In any case, the resources already selected.
		 */
		if (node_gres_ptr->gres_bit_alloc == NULL) {
			node_gres_ptr->gres_bit_alloc =
				bit_copy(job_gres_ptr->
					 gres_bit_alloc[node_offset]);
			node_gres_ptr->gres_cnt_alloc +=
				bit_set_count(node_gres_ptr->gres_bit_alloc);
			node_gres_ptr->gres_cnt_alloc *= gres_per_bit;
		} else if (node_gres_ptr->gres_bit_alloc) {
			gres_cnt = (int64_t)MIN(
				bit_size(node_gres_ptr->gres_bit_alloc),
				bit_size(job_gres_ptr->
					 gres_bit_alloc[node_offset]));
			for (i = 0; i < gres_cnt; i++) {
				if (bit_test(job_gres_ptr->
					     gres_bit_alloc[node_offset], i) &&
				    (shared_gres ||
				     !bit_test(node_gres_ptr->gres_bit_alloc,
					       i))) {
					bit_set(node_gres_ptr->gres_bit_alloc,i);
					node_gres_ptr->gres_cnt_alloc +=
								gres_per_bit;
				}
			}
		}
	} else if (job_gres_ptr->total_node_cnt &&
		   job_gres_ptr->gres_bit_select &&
		   job_gres_ptr->gres_bit_select[node_index] &&
		   job_gres_ptr->gres_cnt_node_select) {
		/* Specific GRES already selected, update the node record */
		bool job_mod = false;
		sz1 = bit_size(job_gres_ptr->gres_bit_select[node_index]);
		sz2 = bit_size(node_gres_ptr->gres_bit_alloc);
		if (sz1 > sz2) {
			error("gres/%s: job %u node %s gres bitmap size bad (%d > %d)",
			      gres_name, job_id, node_name, sz1, sz2);
			job_gres_ptr->gres_bit_select[node_index] =
				bit_realloc(
				job_gres_ptr->gres_bit_select[node_index], sz2);
			job_mod = true;
		} else if (sz1 < sz2) {
			error("gres/%s: job %u node %s gres bitmap size bad (%d < %d)",
			      gres_name, job_id, node_name, sz1, sz2);
			job_gres_ptr->gres_bit_select[node_index] =
				bit_realloc(
				job_gres_ptr->gres_bit_select[node_index], sz2);
		}

		if (!shared_gres &&
		    bit_overlap_any(job_gres_ptr->gres_bit_select[node_index],
				    node_gres_ptr->gres_bit_alloc)) {
			error("gres/%s: job %u node %s gres bitmap overlap",
			      gres_name, job_id, node_name);
			bit_and_not(job_gres_ptr->gres_bit_select[node_index],
				    node_gres_ptr->gres_bit_alloc);
		}
		job_gres_ptr->gres_bit_alloc[node_offset] =
			bit_copy(job_gres_ptr->gres_bit_select[node_index]);
		job_gres_ptr->gres_cnt_node_alloc[node_offset] =
			job_gres_ptr->gres_cnt_node_select[node_index];
		if (!node_gres_ptr->gres_bit_alloc) {
			node_gres_ptr->gres_bit_alloc =
				bit_copy(job_gres_ptr->
					 gres_bit_alloc[node_offset]);
		} else {
			bit_or(node_gres_ptr->gres_bit_alloc,
			       job_gres_ptr->gres_bit_alloc[node_offset]);
		}
		if (job_mod) {
			node_gres_ptr->gres_cnt_alloc =
				bit_set_count(node_gres_ptr->gres_bit_alloc);
			node_gres_ptr->gres_cnt_alloc *= gres_per_bit;
		} else {
			node_gres_ptr->gres_cnt_alloc += gres_cnt;
		}
	} else if (node_gres_ptr->gres_bit_alloc) {
		int64_t gres_avail = node_gres_ptr->gres_cnt_avail;

		i = bit_size(node_gres_ptr->gres_bit_alloc);
		if (plugin_id == mps_plugin_id)
			gres_avail = i;
		else if (i < gres_avail) {
			error("gres/%s: node %s gres bitmap size bad (%"PRIi64" < %"PRIi64")",
			      gres_name, node_name,
			      i, gres_avail);
			node_gres_ptr->gres_bit_alloc =
				bit_realloc(node_gres_ptr->gres_bit_alloc,
					    gres_avail);
		}

		job_gres_ptr->gres_bit_alloc[node_offset] =
			bit_alloc(gres_avail);

		if (core_bitmap)
			alloc_core_bitmap = bit_alloc(bit_size(core_bitmap));
		/* Pass 1: Allocate GRES overlapping all allocated cores */
		for (i=0; i<gres_avail && gres_cnt>0; i++) {
			if (bit_test(node_gres_ptr->gres_bit_alloc, i))
				continue;
			if (!_cores_on_gres(core_bitmap, alloc_core_bitmap,
					    node_gres_ptr, i, job_gres_ptr))
				continue;
			bit_set(node_gres_ptr->gres_bit_alloc, i);
			bit_set(job_gres_ptr->gres_bit_alloc[node_offset], i);
			node_gres_ptr->gres_cnt_alloc += gres_per_bit;
			gres_cnt -= gres_per_bit;
		}
		FREE_NULL_BITMAP(alloc_core_bitmap);
		/* Pass 2: Allocate GRES overlapping any allocated cores */
		for (i=0; i<gres_avail && gres_cnt>0; i++) {
			if (bit_test(node_gres_ptr->gres_bit_alloc, i))
				continue;
			if (!_cores_on_gres(core_bitmap, NULL, node_gres_ptr, i,
					    job_gres_ptr))
				continue;
			bit_set(node_gres_ptr->gres_bit_alloc, i);
			bit_set(job_gres_ptr->gres_bit_alloc[node_offset], i);
			node_gres_ptr->gres_cnt_alloc += gres_per_bit;
			gres_cnt -= gres_per_bit;
		}
		if (gres_cnt) {
			verbose("gres/%s topology sub-optimal for job %u",
				gres_name, job_id);
		}
		/* Pass 3: Allocate any available GRES */
		for (i=0; i<gres_avail && gres_cnt>0; i++) {
			if (bit_test(node_gres_ptr->gres_bit_alloc, i))
				continue;
			bit_set(node_gres_ptr->gres_bit_alloc, i);
			bit_set(job_gres_ptr->gres_bit_alloc[node_offset], i);
			node_gres_ptr->gres_cnt_alloc += gres_per_bit;
			gres_cnt -= gres_per_bit;
		}
	} else {
		node_gres_ptr->gres_cnt_alloc += gres_cnt;
	}

	if (job_gres_ptr->gres_bit_alloc[node_offset] &&
	    node_gres_ptr->topo_gres_bitmap &&
	    node_gres_ptr->topo_gres_cnt_alloc) {
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			if (job_gres_ptr->type_name &&
			    (!node_gres_ptr->topo_type_name[i] ||
			     (job_gres_ptr->type_id !=
			      node_gres_ptr->topo_type_id[i])))
				continue;
			if (use_busy_dev &&
			    (node_gres_ptr->topo_gres_cnt_alloc[i] == 0))
				continue;
			sz1 = bit_size(job_gres_ptr->gres_bit_alloc[node_offset]);
			sz2 = bit_size(node_gres_ptr->topo_gres_bitmap[i]);

			if ((sz1 != sz2) && log_cnt_err) {
				if (_shared_gres(plugin_id))
					log_type = "File";
				else
					log_type = "Count";
				/* Avoid abort on bit_overlap below */
				error("gres/%s %s mismatch for node %s (%d != %d)",
				      gres_name, log_type, node_name, sz1, sz2);
				log_cnt_err = false;
			}
			if (sz1 != sz2)
				continue;	/* See error above */
			gres_cnt = bit_overlap(job_gres_ptr->
					       gres_bit_alloc[node_offset],
					       node_gres_ptr->
					       topo_gres_bitmap[i]);
			gres_cnt *= gres_per_bit;
			node_gres_ptr->topo_gres_cnt_alloc[i] += gres_cnt;
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_type_name == NULL) ||
			    (node_gres_ptr->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_name[j] ||
				    (node_gres_ptr->topo_type_id[i] !=
				     node_gres_ptr->type_id[j]))
					continue;
				node_gres_ptr->type_cnt_alloc[j] += gres_cnt;
				break;
			}
		}
		type_array_updated = true;
	} else if (job_gres_ptr->gres_bit_alloc[node_offset]) {
		int len;	/* length of the gres bitmap on this node */
		len = bit_size(job_gres_ptr->gres_bit_alloc[node_offset]);
		if (!node_gres_ptr->topo_gres_cnt_alloc) {
			node_gres_ptr->topo_gres_cnt_alloc =
				xcalloc(len, sizeof(uint64_t));
		} else {
			len = MIN(len, node_gres_ptr->gres_cnt_config);
		}

		if ((node_gres_ptr->topo_cnt == 0) && shared_gres) {
			/*
			 * Need to add node topo arrays for slurmctld restart
			 * and job state recovery (with GRES counts per topo)
			 */
			node_gres_ptr->topo_cnt =
			    bit_size(job_gres_ptr->gres_bit_alloc[node_offset]);
			node_gres_ptr->topo_core_bitmap =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(bitstr_t *));
			node_gres_ptr->topo_gres_bitmap =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(bitstr_t *));
			node_gres_ptr->topo_gres_cnt_alloc =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(uint64_t));
			node_gres_ptr->topo_gres_cnt_avail =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(uint64_t));
			node_gres_ptr->topo_type_id =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(uint32_t));
			node_gres_ptr->topo_type_name =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(char *));
			for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
				node_gres_ptr->topo_gres_bitmap[i] =
					bit_alloc(node_gres_ptr->topo_cnt);
				bit_set(node_gres_ptr->topo_gres_bitmap[i], i);
			}
		}

		for (i = 0; i < len; i++) {
			gres_cnt = 0;
			if (!bit_test(job_gres_ptr->
				      gres_bit_alloc[node_offset], i))
				continue;
			/*
			 * NOTE: Immediately after slurmctld restart and before
			 * the node's registration, the GRES type and topology
			 * information will not be available and we will be
			 * unable to update topo_gres_cnt_alloc or
			 * type_cnt_alloc. This results in some incorrect
			 * internal bookkeeping, but does not cause failures
			 * in terms of allocating GRES to jobs.
			 */
			for (j = 0; j < node_gres_ptr->topo_cnt; j++) {
				if (use_busy_dev &&
				    (node_gres_ptr->topo_gres_cnt_alloc[j] == 0))
					continue;
				if (node_gres_ptr->topo_gres_bitmap &&
				    node_gres_ptr->topo_gres_bitmap[j] &&
				    bit_test(node_gres_ptr->topo_gres_bitmap[j],
					     i)) {
					node_gres_ptr->topo_gres_cnt_alloc[i] +=
								gres_per_bit;
					gres_cnt += gres_per_bit;
				}
			}
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_type_name == NULL) ||
			    (node_gres_ptr->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_name[j] ||
				    (node_gres_ptr->topo_type_id[i] !=
				     node_gres_ptr->type_id[j]))
					continue;
				node_gres_ptr->type_cnt_alloc[j] += gres_cnt;
				break;
			}
		}
		type_array_updated = true;
		if (job_gres_ptr->type_name && job_gres_ptr->type_name[0]) {
			/*
			 * We may not know how many GRES of this type will be
			 * available on this node, but need to track how many
			 * are allocated to this job from here to avoid
			 * underflows when this job is deallocated
			 */
			_add_gres_type(job_gres_ptr->type_name, node_gres_ptr,
				       0);
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (job_gres_ptr->type_id !=
				    node_gres_ptr->type_id[j])
					continue;
				node_gres_ptr->type_cnt_alloc[j] +=
					job_gres_ptr->gres_per_node;
				break;
			}
		}
	}

	if (!type_array_updated && job_gres_ptr->type_name) {
		gres_cnt = job_gres_ptr->gres_per_node;
		for (j = 0; j < node_gres_ptr->type_cnt; j++) {
			int64_t k;
			if (job_gres_ptr->type_id !=
			    node_gres_ptr->type_id[j])
				continue;
			k = node_gres_ptr->type_cnt_avail[j] -
			    node_gres_ptr->type_cnt_alloc[j];
			k = MIN(gres_cnt, k);
			node_gres_ptr->type_cnt_alloc[j] += k;
			gres_cnt -= k;
			if (gres_cnt == 0)
				break;
		}
	}

	return SLURM_SUCCESS;
}

static void _job_select_whole_node_internal(
	gres_key_t *job_search_key, gres_node_state_t *node_state_ptr,
	int type_inx, int context_inx, List job_gres_list)
{
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_state_ptr;

	if (!(job_gres_ptr = list_find_first(job_gres_list,
					     _gres_find_job_by_key,
					     job_search_key))) {
		job_state_ptr = xmalloc(sizeof(gres_job_state_t));

		job_gres_ptr = xmalloc(sizeof(gres_state_t));
		job_gres_ptr->plugin_id = job_search_key->plugin_id;
		job_gres_ptr->gres_data = job_state_ptr;
		job_state_ptr->gres_name =
			xstrdup(gres_context[context_inx].gres_name);
		if (type_inx != -1)
			job_state_ptr->type_name =
				xstrdup(node_state_ptr->type_name[type_inx]);
		job_state_ptr->type_id = job_search_key->type_id;

		list_append(job_gres_list, job_gres_ptr);
	} else
		job_state_ptr = job_gres_ptr->gres_data;

	/*
	 * Add the total_gres here but no count, that will be done after
	 * allocation.
	 */
	if (node_state_ptr->no_consume) {
		job_state_ptr->total_gres = NO_CONSUME_VAL64;
	} else if (type_inx != -1)
		job_state_ptr->total_gres +=
			node_state_ptr->type_cnt_avail[type_inx];
	else
		job_state_ptr->total_gres += node_state_ptr->gres_cnt_avail;
}

static int _job_alloc_whole_node_internal(
	gres_key_t *job_search_key, gres_node_state_t *node_state_ptr,
	List job_gres_list, int node_cnt, int node_index, int node_offset,
	int type_index, uint32_t job_id, char *node_name,
	bitstr_t *core_bitmap, uint32_t user_id)
{
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_state_ptr;

	if (!(job_gres_ptr = list_find_first(job_gres_list,
					     _gres_find_job_by_key,
					     job_search_key))) {
		error("%s: This should never happen, we couldn't find the gres %u:%u",
		      __func__,
		      job_search_key->plugin_id,
		      job_search_key->type_id);
		return SLURM_ERROR;
	}

	job_state_ptr = (gres_job_state_t *)job_gres_ptr->gres_data;

	/*
	 * As the amount of gres on each node could
	 * differ. We need to set the gres_per_node
	 * correctly here to avoid heterogeneous node
	 * issues.
	 */
	if (type_index != -1)
		job_state_ptr->gres_per_node =
			node_state_ptr->type_cnt_avail[type_index];
	else
		job_state_ptr->gres_per_node = node_state_ptr->gres_cnt_avail;

	return _job_alloc(job_state_ptr, node_state_ptr,
			  node_cnt, node_index, node_offset,
			  job_state_ptr->gres_name,
			  job_id, node_name, core_bitmap,
			  job_gres_ptr->plugin_id,
			  user_id);
}

/*
 * Select and allocate GRES to a job and update node and job GRES information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN node_cnt    - total number of nodes originally allocated to the job
 * IN node_index  - zero-origin global node index
 * IN node_offset - zero-origin index in job allocation to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN core_bitmap - cores allocated to this job on this node (NULL if not
 *                  available)
 * IN user_id     - job's user ID
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_alloc(List job_gres_list, List node_gres_list,
				 int node_cnt, int node_index, int node_offset,
				 uint32_t job_id, char *node_name,
				 bitstr_t *core_bitmap, uint32_t user_id)
{
	int i, rc, rc2;
	ListIterator job_gres_iter,  node_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id ==
			    gres_context[i].plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("%s: no plugin configured for data type %u for job %u and node %s",
			      __func__, job_gres_ptr->plugin_id, job_id,
			      node_name);
			/* A likely sign that GresPlugins has changed */
			continue;
		}

		node_gres_iter = list_iterator_create(node_gres_list);
		while ((node_gres_ptr = (gres_state_t *)
				list_next(node_gres_iter))) {
			if (job_gres_ptr->plugin_id == node_gres_ptr->plugin_id)
				break;
		}
		list_iterator_destroy(node_gres_iter);
		if (node_gres_ptr == NULL) {
			error("%s: job %u allocated gres/%s on node %s lacking that gres",
			      __func__, job_id, gres_context[i].gres_name,
			      node_name);
			continue;
		}

		rc2 = _job_alloc(job_gres_ptr->gres_data,
				 node_gres_ptr->gres_data, node_cnt, node_index,
				 node_offset, gres_context[i].gres_name,
				 job_id, node_name, core_bitmap,
				 job_gres_ptr->plugin_id, user_id);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Fill in job_gres_list with the total amount of GRES on a node.
 * OUT job_gres_list - This list will be destroyed and remade with all GRES on
 *                     node.
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_select_whole_node(
	List *job_gres_list, List node_gres_list,
	uint32_t job_id, char *node_name)
{
	int i;
	ListIterator node_gres_iter;
	gres_state_t *node_gres_ptr;
	gres_node_state_t *node_state_ptr;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	if (!*job_gres_list)
		*job_gres_list = list_create(_gres_job_list_delete);

	if (gres_plugin_init() != SLURM_SUCCESS)
		return SLURM_ERROR;

	slurm_mutex_lock(&gres_context_lock);
	node_gres_iter = list_iterator_create(node_gres_list);
	while ((node_gres_ptr = list_next(node_gres_iter))) {
		gres_key_t job_search_key;
		node_state_ptr = (gres_node_state_t *) node_gres_ptr->gres_data;

		/*
		 * Don't check for no_consume here, we need them added here and
		 * will filter them out in gres_plugin_job_alloc_whole_node()
		 */
		if (!node_state_ptr->gres_cnt_config)
			continue;

		for (i = 0; i < gres_context_cnt; i++) {
			if (node_gres_ptr->plugin_id ==
			    gres_context[i].plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("%s: no plugin configured for data type %u for job %u and node %s",
			      __func__, node_gres_ptr->plugin_id, job_id,
			      node_name);
			/* A likely sign that GresPlugins has changed */
			continue;
		}

		job_search_key.plugin_id = node_gres_ptr->plugin_id;

		if (!node_state_ptr->type_cnt) {
			job_search_key.type_id = 0;
			_job_select_whole_node_internal(
				&job_search_key, node_state_ptr,
				-1, i, *job_gres_list);
		} else {
			for (int j = 0; j < node_state_ptr->type_cnt; j++) {
				job_search_key.type_id = gres_plugin_build_id(
					node_state_ptr->type_name[j]);
				_job_select_whole_node_internal(
					&job_search_key, node_state_ptr,
					j, i, *job_gres_list);
			}
		}
	}
	list_iterator_destroy(node_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return SLURM_SUCCESS;
}

/*
 * Select and allocate all GRES on a node to a job and update node and job GRES
 * information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_whole_node().
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN node_cnt    - total number of nodes originally allocated to the job
 * IN node_index  - zero-origin global node index
 * IN node_offset - zero-origin index in job allocation to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN core_bitmap - cores allocated to this job on this node (NULL if not
 *                  available)
 * IN user_id     - job's user ID
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_alloc_whole_node(
	List job_gres_list, List node_gres_list,
	int node_cnt, int node_index, int node_offset,
	uint32_t job_id, char *node_name,
	bitstr_t *core_bitmap, uint32_t user_id)
{
	int i, rc, rc2;
	ListIterator node_gres_iter;
	gres_state_t *node_gres_ptr;
	gres_node_state_t *node_state_ptr;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	node_gres_iter = list_iterator_create(node_gres_list);
	while ((node_gres_ptr = list_next(node_gres_iter))) {
		gres_key_t job_search_key;
		node_state_ptr = (gres_node_state_t *) node_gres_ptr->gres_data;

		if (node_state_ptr->no_consume ||
		    !node_state_ptr->gres_cnt_config)
			continue;

		for (i = 0; i < gres_context_cnt; i++) {
			if (node_gres_ptr->plugin_id ==
			    gres_context[i].plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("%s: no plugin configured for data type %u for job %u and node %s",
			      __func__, node_gres_ptr->plugin_id, job_id,
			      node_name);
			/* A likely sign that GresPlugins has changed */
			continue;
		}

		job_search_key.plugin_id = node_gres_ptr->plugin_id;

		if (!node_state_ptr->type_cnt) {
			job_search_key.type_id = 0;
			rc2 = _job_alloc_whole_node_internal(
				&job_search_key, node_state_ptr,
				job_gres_list, node_cnt, node_index,
				node_offset, -1, job_id, node_name,
				core_bitmap, user_id);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
		} else {
			for (int j = 0; j < node_state_ptr->type_cnt; j++) {
				job_search_key.type_id = gres_plugin_build_id(
					node_state_ptr->type_name[j]);
				rc2 = _job_alloc_whole_node_internal(
					&job_search_key, node_state_ptr,
					job_gres_list, node_cnt, node_index,
					node_offset, j, job_id, node_name,
					core_bitmap, user_id);
				if (rc2 != SLURM_SUCCESS)
					rc = rc2;
			}
		}
	}
	list_iterator_destroy(node_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

static int _job_dealloc(void *job_gres_data, void *node_gres_data,
			int node_offset, char *gres_name, uint32_t job_id,
			char *node_name, bool old_job, uint32_t plugin_id,
			uint32_t user_id, bool job_fini)
{
	int i, j, len, sz1, sz2;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bool type_array_updated = false;
	uint64_t gres_cnt = 0, k;
	uint64_t gres_per_bit = 1;

	/*
	 * Validate data structures. Either job_gres_data->node_cnt and
	 * job_gres_data->gres_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_offset >= 0);
	xassert(job_gres_ptr);
	xassert(node_gres_ptr);

	if (node_gres_ptr->no_consume)
		return SLURM_SUCCESS;

	if (job_gres_ptr->node_cnt <= node_offset) {
		error("gres/%s: job %u dealloc of node %s bad node_offset %d "
		      "count is %u", gres_name, job_id, node_name, node_offset,
		      job_gres_ptr->node_cnt);
		return SLURM_ERROR;
	}

	if (_shared_gres(plugin_id))
		gres_per_bit = job_gres_ptr->gres_per_node;

	xfree(node_gres_ptr->gres_used);	/* Clear cache */
	if (node_gres_ptr->gres_bit_alloc && job_gres_ptr->gres_bit_alloc &&
	    job_gres_ptr->gres_bit_alloc[node_offset]) {
		len = bit_size(job_gres_ptr->gres_bit_alloc[node_offset]);
		i   = bit_size(node_gres_ptr->gres_bit_alloc);
		if (i != len) {
			error("gres/%s: job %u and node %s bitmap sizes differ "
			      "(%d != %d)", gres_name, job_id, node_name, len,
			       i);
			len = MIN(len, i);
			/* proceed with request, make best effort */
		}
		for (i = 0; i < len; i++) {
			if (!bit_test(job_gres_ptr->gres_bit_alloc[node_offset],
				      i)) {
				continue;
			}
			bit_clear(node_gres_ptr->gres_bit_alloc, i);

			/*
			 * NOTE: Do not clear bit from
			 * job_gres_ptr->gres_bit_alloc[node_offset]
			 * since this may only be an emulated deallocate
			 */
			if (node_gres_ptr->gres_cnt_alloc >= gres_per_bit) {
				node_gres_ptr->gres_cnt_alloc -= gres_per_bit;
			} else {
				error("gres/%s: job %u dealloc node %s GRES count underflow (%"PRIu64" < %"PRIu64")",
				      gres_name, job_id, node_name,
				      node_gres_ptr->gres_cnt_alloc,
				      gres_per_bit);
				node_gres_ptr->gres_cnt_alloc = 0;
			}
		}
	} else if (job_gres_ptr->gres_cnt_node_alloc) {
		gres_cnt = job_gres_ptr->gres_cnt_node_alloc[node_offset];
	} else {
		gres_cnt = job_gres_ptr->gres_per_node;
	}
	if (gres_cnt && (node_gres_ptr->gres_cnt_alloc >= gres_cnt))
		node_gres_ptr->gres_cnt_alloc -= gres_cnt;
	else if (gres_cnt) {
		error("gres/%s: job %u node %s GRES count underflow (%"PRIu64" < %"PRIu64")",
		      gres_name, job_id, node_name,
		      node_gres_ptr->gres_cnt_alloc, gres_cnt);
		node_gres_ptr->gres_cnt_alloc = 0;
	}

	if (job_gres_ptr->gres_bit_alloc &&
	    job_gres_ptr->gres_bit_alloc[node_offset] &&
	    node_gres_ptr->topo_gres_bitmap &&
	    node_gres_ptr->topo_gres_cnt_alloc) {
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			sz1 = bit_size(
				job_gres_ptr->gres_bit_alloc[node_offset]);
			sz2 = bit_size(node_gres_ptr->topo_gres_bitmap[i]);
			if (sz1 != sz2)
				continue;
			gres_cnt = (uint64_t)bit_overlap(
				job_gres_ptr->gres_bit_alloc[node_offset],
				node_gres_ptr->topo_gres_bitmap[i]);
			gres_cnt *= gres_per_bit;
			if (node_gres_ptr->topo_gres_cnt_alloc[i] >= gres_cnt) {
				node_gres_ptr->topo_gres_cnt_alloc[i] -=
					gres_cnt;
			} else if (old_job) {
				node_gres_ptr->topo_gres_cnt_alloc[i] = 0;
			} else {
				error("gres/%s: job %u dealloc node %s topo gres count underflow "
				      "(%"PRIu64" %"PRIu64")",
				      gres_name, job_id, node_name,
				      node_gres_ptr->topo_gres_cnt_alloc[i],
				      gres_cnt);
				node_gres_ptr->topo_gres_cnt_alloc[i] = 0;
			}
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_type_name == NULL) ||
			    (node_gres_ptr->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_name[j] ||
				    (node_gres_ptr->topo_type_id[i] !=
				     node_gres_ptr->type_id[j]))
					continue;
				if (node_gres_ptr->type_cnt_alloc[j] >=
				    gres_cnt) {
					node_gres_ptr->type_cnt_alloc[j] -=
						gres_cnt;
				} else if (old_job) {
					node_gres_ptr->type_cnt_alloc[j] = 0;
				} else {
					error("gres/%s: job %u dealloc node %s type %s gres count underflow "
					      "(%"PRIu64" %"PRIu64")",
					      gres_name, job_id, node_name,
					      node_gres_ptr->type_name[j],
					      node_gres_ptr->type_cnt_alloc[j],
					      gres_cnt);
					node_gres_ptr->type_cnt_alloc[j] = 0;
				}
			}
		}
		type_array_updated = true;
	} else if (job_gres_ptr->gres_bit_alloc &&
		   job_gres_ptr->gres_bit_alloc[node_offset] &&
		   node_gres_ptr->topo_gres_cnt_alloc) {
		/* Avoid crash if configuration inconsistent */
		len = MIN(node_gres_ptr->gres_cnt_config,
			  bit_size(job_gres_ptr->
				   gres_bit_alloc[node_offset]));
		for (i = 0; i < len; i++) {
			if (!bit_test(job_gres_ptr->
				      gres_bit_alloc[node_offset], i) ||
			    !node_gres_ptr->topo_gres_cnt_alloc[i])
				continue;
			if (node_gres_ptr->topo_gres_cnt_alloc[i] >=
			    gres_per_bit) {
				node_gres_ptr->topo_gres_cnt_alloc[i] -=
								gres_per_bit;
			} else {
				error("gres/%s: job %u dealloc node %s "
				      "topo_gres_cnt_alloc[%d] count underflow "
				      "(%"PRIu64" %"PRIu64")",
				      gres_name, job_id, node_name, i,
				      node_gres_ptr->topo_gres_cnt_alloc[i],
				      gres_per_bit);
				node_gres_ptr->topo_gres_cnt_alloc[i] = 0;
			}
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_type_name == NULL) ||
			    (node_gres_ptr->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_name[j] ||
				    (node_gres_ptr->topo_type_id[i] !=
				     node_gres_ptr->type_id[j]))
					continue;
				if (node_gres_ptr->type_cnt_alloc[j] >=
				    gres_per_bit) {
					node_gres_ptr->type_cnt_alloc[j] -=
								gres_per_bit;
				} else {
					error("gres/%s: job %u dealloc node %s "
					      "type %s type_cnt_alloc count underflow "
					      "(%"PRIu64" %"PRIu64")",
					      gres_name, job_id, node_name,
					      node_gres_ptr->type_name[j],
					      node_gres_ptr->type_cnt_alloc[j],
					      gres_per_bit);
					node_gres_ptr->type_cnt_alloc[j] = 0;
				}
 			}
		}
		type_array_updated = true;
	}

	if (!type_array_updated && job_gres_ptr->type_name) {
		gres_cnt = job_gres_ptr->gres_per_node;
		for (j = 0; j < node_gres_ptr->type_cnt; j++) {
			if (job_gres_ptr->type_id !=
			    node_gres_ptr->type_id[j])
				continue;
			k = MIN(gres_cnt, node_gres_ptr->type_cnt_alloc[j]);
			node_gres_ptr->type_cnt_alloc[j] -= k;
			gres_cnt -= k;
			if (gres_cnt == 0)
				break;
		}
 	}

	return SLURM_SUCCESS;
}

/*
 * Deallocate resource from a job and update node and job gres information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		gres_plugin_node_config_validate()
 * IN node_offset - zero-origin index to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN old_job     - true if job started before last slurmctld reboot.
 *		    Immediately after slurmctld restart and before the node's
 *		    registration, the GRES type and topology. This results in
 *		    some incorrect internal bookkeeping, but does not cause
 *		    failures in terms of allocating GRES to jobs.
 * IN user_id     - job's user ID
 * IN: job_fini   - job fully terminating on this node (not just a test)
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_dealloc(List job_gres_list, List node_gres_list,
				   int node_offset, uint32_t job_id,
				   char *node_name, bool old_job,
				   uint32_t user_id, bool job_fini)
{
	int i, rc, rc2;
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;
	char *gres_name = NULL;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id ==
			    gres_context[i].plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("%s: no plugin configured for data type %u for job %u and node %s",
			      __func__, job_gres_ptr->plugin_id, job_id,
			      node_name);
			/* A likely sign that GresPlugins has changed */
			gres_name = "UNKNOWN";
		} else
			gres_name = gres_context[i].gres_name;

		node_gres_ptr = list_find_first(node_gres_list, _gres_find_id,
						&job_gres_ptr->plugin_id);

		if (node_gres_ptr == NULL) {
			error("%s: node %s lacks gres/%s for job %u", __func__,
			      node_name, gres_name , job_id);
			continue;
		}

		rc2 = _job_dealloc(job_gres_ptr->gres_data,
				   node_gres_ptr->gres_data, node_offset,
				   gres_name, job_id, node_name, old_job,
				   job_gres_ptr->plugin_id, user_id, job_fini);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Merge one job's gres allocation into another job's gres allocation.
 * IN from_job_gres_list - List of gres records for the job being merged
 *			into another job
 * IN from_job_node_bitmap - bitmap of nodes for the job being merged into
 *			another job
 * IN/OUT to_job_gres_list - List of gres records for the job being merged
 *			into job
 * IN to_job_node_bitmap - bitmap of nodes for the job being merged into
 */
extern void gres_plugin_job_merge(List from_job_gres_list,
				  bitstr_t *from_job_node_bitmap,
				  List to_job_gres_list,
				  bitstr_t *to_job_node_bitmap)
{
	static int select_hetero = -1;
	ListIterator gres_iter;
	gres_state_t *gres_ptr, *gres_ptr2;
	gres_job_state_t *gres_job_ptr, *gres_job_ptr2;
	int new_node_cnt;
	int i_first, i_last, i;
	int from_inx, to_inx, new_inx;
	bitstr_t **new_gres_bit_alloc, **new_gres_bit_step_alloc;
	uint64_t *new_gres_cnt_step_alloc, *new_gres_cnt_node_alloc;
	bool free_to_job_gres_list = false;

	if (select_hetero == -1) {
		/*
		 * Determine if the select plugin supports heterogeneous
		 * GRES allocations (count differ by node): 1=yes, 0=no
		 */
		char *select_type = slurm_get_select_type();
		if (select_type &&
		    (strstr(select_type, "cons_tres") ||
		     (strstr(select_type, "cray_aries") &&
		      (slurm_conf.select_type_param & CR_OTHER_CONS_TRES)))) {
			select_hetero = 1;
		} else
			select_hetero = 0;
		xfree(select_type);
	}

	(void) gres_plugin_init();
	new_node_cnt = bit_set_count(from_job_node_bitmap) +
		       bit_set_count(to_job_node_bitmap) -
		       bit_overlap(from_job_node_bitmap, to_job_node_bitmap);
	i_first = MIN(bit_ffs(from_job_node_bitmap),
		      bit_ffs(to_job_node_bitmap));
	i_first = MAX(i_first, 0);
	i_last  = MAX(bit_fls(from_job_node_bitmap),
		      bit_fls(to_job_node_bitmap));
	if (i_last == -1) {
		error("%s: node_bitmaps are empty", __func__);
		return;
	}

	slurm_mutex_lock(&gres_context_lock);

	/* Step one - Expand the gres data structures in "to" job */
	if (!to_job_gres_list)
		goto step2;
	gres_iter = list_iterator_create(to_job_gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		gres_job_ptr = (gres_job_state_t *) gres_ptr->gres_data;
		new_gres_bit_alloc = xcalloc(new_node_cnt, sizeof(bitstr_t *));
		new_gres_cnt_node_alloc = xcalloc(new_node_cnt,
						  sizeof(uint64_t));
		new_gres_bit_step_alloc = xcalloc(new_node_cnt,
						  sizeof(bitstr_t *));
		new_gres_cnt_step_alloc = xcalloc(new_node_cnt,
						  sizeof(uint64_t));

		from_inx = to_inx = new_inx = -1;
		for (i = i_first; i <= i_last; i++) {
			bool from_match = false, to_match = false;
			if (bit_test(to_job_node_bitmap, i)) {
				to_match = true;
				to_inx++;
			}
			if (bit_test(from_job_node_bitmap, i)) {
				from_match = true;
				from_inx++;
			}
			if (from_match || to_match)
				new_inx++;
			if (to_match) {
				if (gres_job_ptr->gres_bit_alloc) {
					new_gres_bit_alloc[new_inx] =
						gres_job_ptr->
						gres_bit_alloc[to_inx];
				}
				if (gres_job_ptr->gres_cnt_node_alloc) {
					new_gres_cnt_node_alloc[new_inx] =
						gres_job_ptr->
						gres_cnt_node_alloc[to_inx];
				}
				if (gres_job_ptr->gres_bit_step_alloc) {
					new_gres_bit_step_alloc[new_inx] =
						gres_job_ptr->
						gres_bit_step_alloc[to_inx];
				}
				if (gres_job_ptr->gres_cnt_step_alloc) {
					new_gres_cnt_step_alloc[new_inx] =
						gres_job_ptr->
						gres_cnt_step_alloc[to_inx];
				}
			}
		}
		gres_job_ptr->node_cnt = new_node_cnt;
		xfree(gres_job_ptr->gres_bit_alloc);
		gres_job_ptr->gres_bit_alloc = new_gres_bit_alloc;
		xfree(gres_job_ptr->gres_cnt_node_alloc);
		gres_job_ptr->gres_cnt_node_alloc = new_gres_cnt_node_alloc;
		xfree(gres_job_ptr->gres_bit_step_alloc);
		gres_job_ptr->gres_bit_step_alloc = new_gres_bit_step_alloc;
		xfree(gres_job_ptr->gres_cnt_step_alloc);
		gres_job_ptr->gres_cnt_step_alloc = new_gres_cnt_step_alloc;
	}
	list_iterator_destroy(gres_iter);

	/*
	 * Step two - Merge the gres information from the "from" job into the
	 * existing gres information for the "to" job
	 */
step2:	if (!from_job_gres_list)
		goto step3;
	if (!to_job_gres_list) {
		to_job_gres_list = list_create(_gres_job_list_delete);
		free_to_job_gres_list = true;
	}
	gres_iter = list_iterator_create(from_job_gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		gres_job_ptr = (gres_job_state_t *) gres_ptr->gres_data;
		gres_ptr2 = list_find_first(to_job_gres_list, _gres_find_id,
					    &gres_ptr->plugin_id);
		if (gres_ptr2) {
			gres_job_ptr2 = gres_ptr2->gres_data;
		} else {
			gres_ptr2 = xmalloc(sizeof(gres_state_t));
			gres_job_ptr2 = xmalloc(sizeof(gres_job_state_t));
			gres_ptr2->plugin_id = gres_ptr->plugin_id;
			gres_ptr2->gres_data = gres_job_ptr2;
			gres_job_ptr2->gres_name =
					xstrdup(gres_job_ptr->gres_name);
			gres_job_ptr2->cpus_per_gres =
					gres_job_ptr->cpus_per_gres;
			gres_job_ptr2->gres_per_job =
					gres_job_ptr->gres_per_job;
			gres_job_ptr2->gres_per_job =
					gres_job_ptr->gres_per_job;
			gres_job_ptr2->gres_per_socket =
					gres_job_ptr->gres_per_socket;
			gres_job_ptr2->gres_per_task =
					gres_job_ptr->gres_per_task;
			gres_job_ptr2->mem_per_gres =
					gres_job_ptr->mem_per_gres;
			gres_job_ptr2->ntasks_per_gres =
					gres_job_ptr->ntasks_per_gres;
			gres_job_ptr2->node_cnt = new_node_cnt;
			gres_job_ptr2->gres_bit_alloc =
				xcalloc(new_node_cnt, sizeof(bitstr_t *));
			gres_job_ptr2->gres_cnt_node_alloc =
				xcalloc(new_node_cnt, sizeof(uint64_t));
			gres_job_ptr2->gres_bit_step_alloc =
				xcalloc(new_node_cnt, sizeof(bitstr_t *));
			gres_job_ptr2->gres_cnt_step_alloc =
				xcalloc(new_node_cnt, sizeof(uint64_t));
			list_append(to_job_gres_list, gres_ptr2);
		}
		from_inx = to_inx = new_inx = -1;
		for (i = i_first; i <= i_last; i++) {
			bool from_match = false, to_match = false;
			if (bit_test(to_job_node_bitmap, i)) {
				to_match = true;
				to_inx++;
			}
			if (bit_test(from_job_node_bitmap, i)) {
				from_match = true;
				from_inx++;
			}
			if (from_match || to_match)
				new_inx++;
			if (from_match) {
				if (!gres_job_ptr->gres_bit_alloc) {
					;
				} else if (select_hetero &&
					   gres_job_ptr2->
					   gres_bit_alloc[new_inx] &&
					   gres_job_ptr->gres_bit_alloc &&
					   gres_job_ptr->
					   gres_bit_alloc[new_inx]) {
					/* Merge job's GRES bitmaps */
					bit_or(gres_job_ptr2->
					       gres_bit_alloc[new_inx],
					       gres_job_ptr->
					       gres_bit_alloc[from_inx]);
				} else if (gres_job_ptr2->
					   gres_bit_alloc[new_inx]) {
					/* Keep original job's GRES bitmap */
				} else {
					gres_job_ptr2->gres_bit_alloc[new_inx] =
						gres_job_ptr->
						gres_bit_alloc[from_inx];
					gres_job_ptr->
						gres_bit_alloc
						[from_inx] = NULL;
				}
				if (!gres_job_ptr->gres_cnt_node_alloc) {
					;
				} else if (select_hetero &&
					   gres_job_ptr2->
					   gres_cnt_node_alloc[new_inx] &&
					   gres_job_ptr->gres_cnt_node_alloc &&
					   gres_job_ptr->
					   gres_cnt_node_alloc[new_inx]) {
					gres_job_ptr2->
						gres_cnt_node_alloc[new_inx] +=
						gres_job_ptr->
						gres_cnt_node_alloc[from_inx];
				} else if (gres_job_ptr2->
					   gres_cnt_node_alloc[new_inx]) {
					/* Keep original job's GRES bitmap */
				} else {
					gres_job_ptr2->
						gres_cnt_node_alloc[new_inx] =
						gres_job_ptr->
						gres_cnt_node_alloc[from_inx];
					gres_job_ptr->
						gres_cnt_node_alloc[from_inx]=0;
				}
				if (gres_job_ptr->gres_cnt_step_alloc &&
				    gres_job_ptr->
				    gres_cnt_step_alloc[from_inx]) {
					error("Attempt to merge gres, from "
					      "job has active steps");
				}
			}
		}
	}
	list_iterator_destroy(gres_iter);

step3:	slurm_mutex_unlock(&gres_context_lock);
	if (free_to_job_gres_list)
		FREE_NULL_LIST(to_job_gres_list);
	return;
}

/*
 * Set environment variables as required for a batch job
 * IN/OUT job_env_ptr - environment variable array
 * IN gres_list - generated by gres_plugin_job_alloc()
 * IN node_inx - zero origin node index
 */
extern void gres_plugin_job_set_env(char ***job_env_ptr, List job_gres_list,
				    int node_inx)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_ptr = NULL;
	bool found;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i=0; i<gres_context_cnt; i++) {
		if (gres_context[i].ops.job_set_env == NULL)
			continue;	/* No plugin to call */
		found = false;
		if (job_gres_list) {
			gres_iter = list_iterator_create(job_gres_list);
			while ((gres_ptr = (gres_state_t *)
				list_next(gres_iter))) {
				if (gres_ptr->plugin_id !=
				    gres_context[i].plugin_id)
					continue;
				(*(gres_context[i].ops.job_set_env))
					(job_env_ptr, gres_ptr->gres_data,
					 node_inx,
					 GRES_INTERNAL_FLAG_NONE);
				found = true;
			}
			list_iterator_destroy(gres_iter);
		}
		/*
		 * We call the job_set_env of the gres even if this one is not
		 * requested in the job. This may be convenient on certain
		 * plugins, i.e. setting an env variable to say the GRES is not
		 * available.
		 */
		if (!found) {
			(*(gres_context[i].ops.job_set_env))
				(job_env_ptr, NULL, node_inx,
				 GRES_INTERNAL_FLAG_NONE);
		}
	}
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Set job default parameters in a given element of a list
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN gres_name - name of gres, apply defaults to all elements (e.g. updates to
 *		  gres_name="gpu" would apply to "gpu:tesla", "gpu:volta", etc.)
 * IN cpu_per_gpu - value to set as default
 * IN mem_per_gpu - value to set as default
 * OUT *cpus_per_tres - CpusPerTres string displayed by scontrol show job
 * OUT *mem_per_tres - MemPerTres string displayed by scontrol show job
 */
extern void gres_plugin_job_set_defs(List job_gres_list, char *gres_name,
				     uint64_t cpu_per_gpu, uint64_t mem_per_gpu,
				     char **cpus_per_tres, char **mem_per_tres,
				     uint16_t *cpus_per_task)
{
	uint32_t plugin_id;
	ListIterator gres_iter;
	gres_state_t *gres_ptr = NULL;
	gres_job_state_t *job_gres_data;

	/*
	 * Currently only GPU supported, check how cpus_per_tres/mem_per_tres
	 * is handled in _fill_job_desc_from_sbatch_opts and
	 * _job_desc_msg_create_from_opts.
	 */
	xassert(!xstrcmp(gres_name, "gpu"));

	if (!job_gres_list)
		return;

	plugin_id = gres_plugin_build_id(gres_name);
	gres_iter = list_iterator_create(job_gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		if (gres_ptr->plugin_id != plugin_id)
			continue;
		job_gres_data = (gres_job_state_t *) gres_ptr->gres_data;
		if (!job_gres_data)
			continue;
		job_gres_data->def_cpus_per_gres = cpu_per_gpu;
		job_gres_data->def_mem_per_gres = mem_per_gpu;
		if (!job_gres_data->cpus_per_gres) {
			xfree(*cpus_per_tres);
			if (cpu_per_gpu)
				xstrfmtcat(*cpus_per_tres, "gpu:%"PRIu64,
					   cpu_per_gpu);
		}
		if (!job_gres_data->mem_per_gres) {
			xfree(*mem_per_tres);
			if (mem_per_gpu)
				xstrfmtcat(*mem_per_tres, "gpu:%"PRIu64,
					   mem_per_gpu);
		}
		if (cpu_per_gpu && job_gres_data->gres_per_task) {
			*cpus_per_task = MAX(*cpus_per_task,
					     (job_gres_data->gres_per_task *
					      cpu_per_gpu));
		}
	}
	list_iterator_destroy(gres_iter);
}

/*
 * Translate GRES flag to string.
 * NOT reentrant
 */
static char *_gres_flags_str(uint16_t flags)
{
	if (flags & GRES_NO_CONSUME)
		return "no_consume";
	return "";
}

static void _job_state_log(void *gres_data, uint32_t job_id, uint32_t plugin_id)
{
	gres_job_state_t *gres_ptr;
	char *sparse_msg = "", tmp_str[128];
	int i;

	xassert(gres_data);
	gres_ptr = (gres_job_state_t *) gres_data;
	info("gres:%s(%u) type:%s(%u) job:%u flags:%s state",
	      gres_ptr->gres_name, plugin_id, gres_ptr->type_name,
	      gres_ptr->type_id, job_id, _gres_flags_str(gres_ptr->flags));
	if (gres_ptr->cpus_per_gres)
		info("  cpus_per_gres:%u", gres_ptr->cpus_per_gres);
	else if (gres_ptr->def_cpus_per_gres)
		info("  def_cpus_per_gres:%u", gres_ptr->def_cpus_per_gres);
	if (gres_ptr->gres_per_job)
		info("  gres_per_job:%"PRIu64, gres_ptr->gres_per_job);
	if (gres_ptr->gres_per_node) {
		info("  gres_per_node:%"PRIu64" node_cnt:%u",
		     gres_ptr->gres_per_node, gres_ptr->node_cnt);
	}
	if (gres_ptr->gres_per_socket)
		info("  gres_per_socket:%"PRIu64, gres_ptr->gres_per_socket);
	if (gres_ptr->gres_per_task)
		info("  gres_per_task:%"PRIu64, gres_ptr->gres_per_task);
	if (gres_ptr->mem_per_gres)
		info("  mem_per_gres:%"PRIu64, gres_ptr->mem_per_gres);
	if (gres_ptr->ntasks_per_gres)
		info("  ntasks_per_gres:%u", gres_ptr->ntasks_per_gres);
	else if (gres_ptr->def_mem_per_gres)
		info("  def_mem_per_gres:%"PRIu64, gres_ptr->def_mem_per_gres);

	if (gres_ptr->node_cnt == 0)
		return;
	if (gres_ptr->gres_bit_alloc == NULL)
		info("  gres_bit_alloc:NULL");
	if (gres_ptr->gres_cnt_node_alloc == NULL)
		info("  gres_cnt_node_alloc:NULL");
	if (gres_ptr->gres_bit_step_alloc == NULL)
		info("  gres_bit_step_alloc:NULL");
	if (gres_ptr->gres_cnt_step_alloc == NULL)
		info("  gres_cnt_step_alloc:NULL");
	if (gres_ptr->gres_bit_select == NULL)
		info("  gres_bit_select:NULL");
	if (gres_ptr->gres_cnt_node_select == NULL)
		info("  gres_cnt_node_select:NULL");

	for (i = 0; i < gres_ptr->node_cnt; i++) {
		if (gres_ptr->gres_cnt_node_alloc &&
		    gres_ptr->gres_cnt_node_alloc[i]) {
			info("  gres_cnt_node_alloc[%d]:%"PRIu64,
			     i, gres_ptr->gres_cnt_node_alloc[i]);
		} else if (gres_ptr->gres_cnt_node_alloc)
			info("  gres_cnt_node_alloc[%d]:NULL", i);

		if (gres_ptr->gres_bit_alloc && gres_ptr->gres_bit_alloc[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_ptr->gres_bit_alloc[i]);
			info("  gres_bit_alloc[%d]:%s of %d", i, tmp_str,
			     (int) bit_size(gres_ptr->gres_bit_alloc[i]));
		} else if (gres_ptr->gres_bit_alloc)
			info("  gres_bit_alloc[%d]:NULL", i);

		if (gres_ptr->gres_bit_step_alloc &&
		    gres_ptr->gres_bit_step_alloc[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_ptr->gres_bit_step_alloc[i]);
			info("  gres_bit_step_alloc[%d]:%s of %d", i, tmp_str,
			     (int) bit_size(gres_ptr->gres_bit_step_alloc[i]));
		} else if (gres_ptr->gres_bit_step_alloc)
			info("  gres_bit_step_alloc[%d]:NULL", i);

		if (gres_ptr->gres_cnt_step_alloc) {
			info("  gres_cnt_step_alloc[%d]:%"PRIu64"", i,
			     gres_ptr->gres_cnt_step_alloc[i]);
		}
	}

	/*
	 * These arrays are only used for resource selection and may include
	 * data for many nodes not used in the resources eventually allocated
	 * to this job.
	 */
	if (gres_ptr->total_node_cnt)
		sparse_msg = " (sparsely populated for resource selection)";
	info("  total_node_cnt:%u%s", gres_ptr->total_node_cnt, sparse_msg);
	for (i = 0; i < gres_ptr->total_node_cnt; i++) {
		if (gres_ptr->gres_cnt_node_select &&
		    gres_ptr->gres_cnt_node_select[i]) {
			info("  gres_cnt_node_select[%d]:%"PRIu64,
			     i, gres_ptr->gres_cnt_node_select[i]);
		}
		if (gres_ptr->gres_bit_select &&
		    gres_ptr->gres_bit_select[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_ptr->gres_bit_select[i]);
			info("  gres_bit_select[%d]:%s of %d", i, tmp_str,
			     (int) bit_size(gres_ptr->gres_bit_select[i]));
		}
	}
}

/*
 * Extract from the job record's gres_list the count of allocated resources of
 * 	the named gres type.
 * IN job_gres_list  - job record's gres_list.
 * IN gres_name_type - the name of the gres type to retrieve the associated
 *	value from.
 * RET The value associated with the gres type or NO_VAL if not found.
 */
extern uint64_t gres_plugin_get_job_value_by_type(List job_gres_list,
						  char *gres_name_type)
{
	uint64_t gres_val;
	uint32_t gres_name_type_id;
	ListIterator  job_gres_iter;
	gres_state_t *job_gres_ptr;

	if (job_gres_list == NULL)
		return NO_VAL64;

	slurm_mutex_lock(&gres_context_lock);
	gres_name_type_id = gres_plugin_build_id(gres_name_type);
	gres_val = NO_VAL64;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		if (job_gres_ptr->plugin_id == gres_name_type_id) {
			gres_val = ((gres_job_state_t *)
				   (job_gres_ptr->gres_data))->gres_per_node;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);

	slurm_mutex_unlock(&gres_context_lock);
	return gres_val;
}

/*
 * Extract from the job/step gres_list the count of GRES of the specified name
 * and (optionally) type. If no type is specified, then the count will include
 * all GRES of that name, regardless of type.
 *
 * IN gres_list  - job/step record's gres_list.
 * IN gres_name - the name of the GRES to query.
 * IN gres_type - (optional) the type of the GRES to query.
 * IN is_job - True if the GRES list is for the job, false if for the step.
 * RET The number of GRES in the job/step gres_list or NO_VAL64 if not found.
 */
static uint64_t _get_gres_list_cnt(List gres_list, char *gres_name,
				   char *gres_type, bool is_job)
{
	uint64_t gres_val = NO_VAL64;
	uint32_t plugin_id;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;
	bool filter_type;

	if ((gres_list == NULL) || (list_count(gres_list) == 0))
		return gres_val;

	plugin_id = gres_plugin_build_id(gres_name);

	if (gres_type && (gres_type[0] != '\0'))
		filter_type = true;
	else
		filter_type = false;

	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = list_next(gres_iter))) {
		uint64_t total_gres;
		void *type_name;

		if (gres_ptr->plugin_id != plugin_id)
			continue;

		if (is_job) {
			gres_job_state_t *gres =
				(gres_job_state_t *)gres_ptr->gres_data;
			type_name = gres->type_name;
			total_gres = gres->total_gres;
		} else {
			gres_step_state_t *gres =
				(gres_step_state_t *)gres_ptr->gres_data;
			type_name = gres->type_name;
			total_gres = gres->total_gres;
		}

		/* If we are filtering on GRES type, ignore other types */
		if (filter_type &&
		    xstrcasecmp(gres_type, type_name))
			continue;

		if ((total_gres == NO_VAL64) || (total_gres == 0))
			continue;

		if (gres_val == NO_VAL64)
			gres_val = total_gres;
		else
			gres_val += total_gres;
	}
	list_iterator_destroy(gres_iter);

	return gres_val;
}

static uint64_t _get_job_gres_list_cnt(List gres_list, char *gres_name,
				       char *gres_type)
{
	return _get_gres_list_cnt(gres_list, gres_name, gres_type, true);
}

static uint64_t _get_step_gres_list_cnt(List gres_list, char *gres_name,
					char *gres_type)
{
	return _get_gres_list_cnt(gres_list, gres_name, gres_type, false);
}

/*
 * Log a job's current gres state
 * IN gres_list - generated by gres_plugin_job_state_validate()
 * IN job_id - job's ID
 */
extern void gres_plugin_job_state_log(List gres_list, uint32_t job_id)
{
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES) || !gres_list)
		return;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		_job_state_log(gres_ptr->gres_data, job_id,
			       gres_ptr->plugin_id);
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

static int _find_device(void *x, void *key)
{
	gres_device_t *device_x = (gres_device_t *)x;
	gres_device_t *device_key = (gres_device_t *)key;

	if (!xstrcmp(device_x->path, device_key->path))
		return 1;

	return 0;
}

extern List gres_plugin_get_allocated_devices(List gres_list, bool is_job)
{
	int j;
	ListIterator gres_itr, dev_itr;
	gres_state_t *gres_ptr;
	bitstr_t **local_bit_alloc = NULL;
	uint32_t node_cnt;
	gres_device_t *gres_device;
	List gres_devices;
	List device_list = NULL;

	(void) gres_plugin_init();

	/*
	 * Create a unique device list of all possible GRES device files.
	 * Initialize each device to deny.
	 */
	for (j = 0; j < gres_context_cnt; j++) {
		if (!gres_context[j].ops.get_devices)
			continue;
		gres_devices = (*(gres_context[j].ops.get_devices))();
		if (!gres_devices || !list_count(gres_devices))
			continue;
		dev_itr = list_iterator_create(gres_devices);
		while ((gres_device = list_next(dev_itr))) {
			if (!device_list)
				device_list = list_create(NULL);
			gres_device->alloc = 0;
			/*
			 * Keep the list unique by not adding duplicates (in the
			 * case of MPS and GPU)
			 */
			if (!list_find_first(device_list, _find_device,
					     gres_device))
				list_append(device_list, gres_device);
		}
		list_iterator_destroy(dev_itr);
	}

	if (!gres_list)
		return device_list;

	slurm_mutex_lock(&gres_context_lock);
	gres_itr = list_iterator_create(gres_list);
	while ((gres_ptr = list_next(gres_itr))) {
		for (j = 0; j < gres_context_cnt; j++) {
			if (gres_ptr->plugin_id == gres_context[j].plugin_id)
				break;
		}

		if (j >= gres_context_cnt) {
			error("We were unable to find the gres in the context!!!  This should never happen");
			continue;
		}

		if (!gres_ptr->gres_data)
			continue;

		if (is_job) {
			gres_job_state_t *gres_data_ptr =
				(gres_job_state_t *)gres_ptr->gres_data;
			local_bit_alloc = gres_data_ptr->gres_bit_alloc;
			node_cnt = gres_data_ptr->node_cnt;
		} else {
			gres_step_state_t *gres_data_ptr =
				(gres_step_state_t *)gres_ptr->gres_data;
			local_bit_alloc = gres_data_ptr->gres_bit_alloc;
			node_cnt = gres_data_ptr->node_cnt;
		}

		if ((node_cnt != 1) ||
		    !local_bit_alloc ||
		    !local_bit_alloc[0] ||
		    !gres_context[j].ops.get_devices)
			continue;

		gres_devices = (*(gres_context[j].ops.get_devices))();
		if (!gres_devices) {
			error("We should had got gres_devices, but for some reason none were set in the plugin.");
			continue;
		}

		dev_itr = list_iterator_create(gres_devices);
		while ((gres_device = list_next(dev_itr))) {
			if (bit_test(local_bit_alloc[0], gres_device->index)) {
				gres_device_t *gres_device2;
				/*
				 * search for the device among the unique
				 * devices list (since two plugins could have
				 * device records that point to the same file,
				 * like with GPU and MPS)
				 */
				gres_device2 = list_find_first(device_list,
							       _find_device,
							       gres_device);
				/*
				 * Set both, in case they point to different
				 * records
				 */
				gres_device->alloc = 1;
				if (gres_device2)
					gres_device2->alloc = 1;
			}
		}
		list_iterator_destroy(dev_itr);
	}
	list_iterator_destroy(gres_itr);
	slurm_mutex_unlock(&gres_context_lock);

	return device_list;
}

static void _step_state_delete(void *gres_data)
{
	int i;
	gres_step_state_t *gres_ptr = (gres_step_state_t *) gres_data;

	if (gres_ptr == NULL)
		return;

	FREE_NULL_BITMAP(gres_ptr->node_in_use);
	if (gres_ptr->gres_bit_alloc) {
		for (i = 0; i < gres_ptr->node_cnt; i++)
			FREE_NULL_BITMAP(gres_ptr->gres_bit_alloc[i]);
		xfree(gres_ptr->gres_bit_alloc);
	}
	xfree(gres_ptr->gres_cnt_node_alloc);
	xfree(gres_ptr->type_name);
	xfree(gres_ptr);
}

static void _gres_step_list_delete(void *list_element)
{
	gres_state_t *gres_ptr = (gres_state_t *) list_element;

	_step_state_delete(gres_ptr->gres_data);
	xfree(gres_ptr);
}

static uint64_t _step_test(void *step_gres_data, void *job_gres_data,
			   int node_offset, bool first_step_node,
			   uint16_t cpus_per_task, int max_rem_nodes,
			   bool ignore_alloc, slurm_step_id_t *step_id,
			   uint32_t plugin_id)
{
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint64_t core_cnt, gres_cnt, min_gres = 1, task_cnt;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if ((node_offset >= job_gres_ptr->node_cnt) &&
	    (job_gres_ptr->node_cnt != 0)) {	/* GRES is type no_consume */
		error("gres/%s: %s %ps node offset invalid (%d >= %u)",
		      job_gres_ptr->gres_name, __func__, step_id, node_offset,
		      job_gres_ptr->node_cnt);
		return 0;
	}

	if (first_step_node) {
		if (ignore_alloc)
			step_gres_ptr->gross_gres = 0;
		else
			step_gres_ptr->total_gres = 0;
	}
	if (step_gres_ptr->gres_per_node)
		min_gres = step_gres_ptr-> gres_per_node;
	if (step_gres_ptr->gres_per_socket)
		min_gres = MAX(min_gres, step_gres_ptr->gres_per_socket);
	if (step_gres_ptr->gres_per_task)
		min_gres = MAX(min_gres, step_gres_ptr->gres_per_task);
	if (step_gres_ptr->gres_per_step &&
	    (step_gres_ptr->gres_per_step > step_gres_ptr->total_gres) &&
	    (max_rem_nodes == 1)) {
		gres_cnt = step_gres_ptr->gres_per_step;
		if (ignore_alloc)
			   gres_cnt -= step_gres_ptr->gross_gres;
		else
			   gres_cnt -= step_gres_ptr->total_gres;
		min_gres = MAX(min_gres, gres_cnt);
	}

	if (!_shared_gres(plugin_id) &&
	    job_gres_ptr->gres_bit_alloc &&
	    job_gres_ptr->gres_bit_alloc[node_offset]) {
		gres_cnt = bit_set_count(job_gres_ptr->
					 gres_bit_alloc[node_offset]);
		if (!ignore_alloc &&
		    job_gres_ptr->gres_bit_step_alloc &&
		    job_gres_ptr->gres_bit_step_alloc[node_offset]) {
			gres_cnt -= bit_set_count(job_gres_ptr->
						  gres_bit_step_alloc
						  [node_offset]);
		}
		if (min_gres > gres_cnt) {
			core_cnt = 0;
		} else if (step_gres_ptr->gres_per_task) {
			task_cnt = (gres_cnt + step_gres_ptr->gres_per_task - 1)
				   / step_gres_ptr->gres_per_task;
			core_cnt = task_cnt * cpus_per_task;
		} else
			core_cnt = NO_VAL64;
	} else if (job_gres_ptr->gres_cnt_node_alloc &&
		   job_gres_ptr->gres_cnt_step_alloc) {
		gres_cnt = job_gres_ptr->gres_cnt_node_alloc[node_offset];
		if (!ignore_alloc) {
			gres_cnt -= job_gres_ptr->
				    gres_cnt_step_alloc[node_offset];
		}
		if (min_gres > gres_cnt) {
			core_cnt = 0;
		} else if (step_gres_ptr->gres_per_task) {
			task_cnt = (gres_cnt + step_gres_ptr->gres_per_task - 1)
				   / step_gres_ptr->gres_per_task;
			core_cnt = task_cnt * cpus_per_task;
		} else
			core_cnt = NO_VAL64;
	} else {
		debug3("gres/%s: %s %ps gres_bit_alloc and gres_cnt_node_alloc are NULL",
		       job_gres_ptr->gres_name, __func__, step_id);
		gres_cnt = 0;
		core_cnt = NO_VAL64;
	}
	if (core_cnt != 0) {
		if (ignore_alloc)
			step_gres_ptr->gross_gres += gres_cnt;
		else
			step_gres_ptr->total_gres += gres_cnt;
	}

	return core_cnt;
}

/*
 * TRES specification parse logic
 * in_val IN - initial input string
 * cnt OUT - count of values
 * gres_list IN/OUT - where to search for (or add) new step TRES record
 * save_ptr IN/OUT - NULL on initial call, otherwise value from previous call
 * rc OUT - unchanged or an error code
 * RET gres - step record to set value in, found or created by this function
 */
static gres_step_state_t *_get_next_step_gres(char *in_val, uint64_t *cnt,
					      List gres_list, char **save_ptr,
					      int *rc)
{
	static char *prev_save_ptr = NULL;
	int context_inx = NO_VAL, my_rc = SLURM_SUCCESS;
	gres_step_state_t *step_gres_data = NULL;
	gres_state_t *gres_ptr;
	gres_key_t step_search_key;
	char *type = NULL, *name = NULL;
	uint16_t flags = 0;

	xassert(save_ptr);
	if (!in_val && (*save_ptr == NULL)) {
		return NULL;
	}

	if (*save_ptr == NULL) {
		prev_save_ptr = in_val;
	} else if (*save_ptr != prev_save_ptr) {
		error("%s: parsing error", __func__);
		my_rc = SLURM_ERROR;
		goto fini;
	}

	if (prev_save_ptr[0] == '\0') {	/* Empty input token */
		*save_ptr = NULL;
		return NULL;
	}

	if ((my_rc = _get_next_gres(in_val, &type, &context_inx,
				    cnt, &flags, &prev_save_ptr)) ||
	    (context_inx == NO_VAL)) {
		prev_save_ptr = NULL;
		goto fini;
	}

	/* Find the step GRES record */
	step_search_key.plugin_id = gres_context[context_inx].plugin_id;
	step_search_key.type_id = gres_plugin_build_id(type);
	gres_ptr = list_find_first(gres_list, _gres_find_step_by_key,
				   &step_search_key);

	if (gres_ptr) {
		step_gres_data = gres_ptr->gres_data;
	} else {
		step_gres_data = xmalloc(sizeof(gres_step_state_t));
		step_gres_data->type_id = gres_plugin_build_id(type);
		step_gres_data->type_name = type;
		type = NULL;	/* String moved above */
		gres_ptr = xmalloc(sizeof(gres_state_t));
		gres_ptr->plugin_id = gres_context[context_inx].plugin_id;
		gres_ptr->gres_data = step_gres_data;
		list_append(gres_list, gres_ptr);
	}
	step_gres_data->flags = flags;

fini:	xfree(name);
	xfree(type);
	if (my_rc != SLURM_SUCCESS) {
		prev_save_ptr = NULL;
		if (my_rc == ESLURM_INVALID_GRES)
			info("Invalid GRES job specification %s", in_val);
		*rc = my_rc;
	}
	*save_ptr = prev_save_ptr;
	return step_gres_data;
}

/* Test that the step does not request more GRES than the job contains */
static void _validate_step_counts(List step_gres_list, List job_gres_list,
				  int *rc)
{
	ListIterator iter;
	gres_state_t *job_gres_ptr, *step_gres_ptr;
	gres_job_state_t *job_gres_data;
	gres_step_state_t *step_gres_data;
	gres_key_t job_search_key;
	uint16_t cpus_per_gres;
	uint64_t mem_per_gres;

	if (!step_gres_list || (list_count(step_gres_list) == 0))
		return;
	if (!job_gres_list  || (list_count(job_gres_list)  == 0)) {
		*rc = ESLURM_INVALID_GRES;
		return;
	}

	iter = list_iterator_create(step_gres_list);
	while ((step_gres_ptr = (gres_state_t *) list_next(iter))) {
		step_gres_data = (gres_step_state_t *) step_gres_ptr->gres_data;
		job_search_key.plugin_id = step_gres_ptr->plugin_id;
		if (step_gres_data->type_id == 0)
			job_search_key.type_id = NO_VAL;
		else
			job_search_key.type_id = step_gres_data->type_id;
		job_gres_ptr = list_find_first(job_gres_list,
					       _gres_find_job_by_key,
					       &job_search_key);
		if (!job_gres_ptr || !job_gres_ptr->gres_data) {
			*rc = ESLURM_INVALID_GRES;
			break;
		}
		job_gres_data = (gres_job_state_t *) job_gres_ptr->gres_data;
		if (job_gres_data->cpus_per_gres)
			cpus_per_gres = job_gres_data->cpus_per_gres;
		else
			cpus_per_gres = job_gres_data->def_cpus_per_gres;
		if (cpus_per_gres && step_gres_data->cpus_per_gres &&
		    (cpus_per_gres < step_gres_data->cpus_per_gres)) {
			*rc = ESLURM_INVALID_GRES;
			break;
		}
		if (job_gres_data->gres_per_job &&
		    step_gres_data->gres_per_step &&
		    (job_gres_data->gres_per_job <
		     step_gres_data->gres_per_step)) {
			*rc = ESLURM_INVALID_GRES;
			break;
		}
		if (job_gres_data->gres_per_node &&
		    step_gres_data->gres_per_node &&
		    (job_gres_data->gres_per_node <
		     step_gres_data->gres_per_node)) {
			*rc = ESLURM_INVALID_GRES;
			break;
		}
		if (job_gres_data->gres_per_socket &&
		    step_gres_data->gres_per_socket &&
		    (job_gres_data->gres_per_socket <
		     step_gres_data->gres_per_socket)) {
			*rc = ESLURM_INVALID_GRES;
			break;
		}
		if (job_gres_data->gres_per_task &&
		    step_gres_data->gres_per_task &&
		    (job_gres_data->gres_per_task <
		     step_gres_data->gres_per_task)) {
			*rc = ESLURM_INVALID_GRES;
			break;
		}
		if (job_gres_data->mem_per_gres)
			mem_per_gres = job_gres_data->mem_per_gres;
		else
			mem_per_gres = job_gres_data->def_mem_per_gres;
		if (mem_per_gres && step_gres_data->mem_per_gres &&
		    (mem_per_gres < step_gres_data->mem_per_gres)) {
			*rc = ESLURM_INVALID_GRES;
			break;
		}

	}
	list_iterator_destroy(iter);
}


static int _handle_ntasks_per_tres_step(List new_step_list,
					 uint16_t ntasks_per_tres,
					 uint32_t *num_tasks,
					 uint32_t *cpu_count)
{
	gres_step_state_t *step_gres_data;
	uint64_t cnt = 0;
	int rc = SLURM_SUCCESS;

	uint64_t tmp = _get_step_gres_list_cnt(new_step_list, "gpu", NULL);
	if ((tmp == NO_VAL64) && (*num_tasks != NO_VAL)) {
		/*
		 * Generate GPUs from ntasks_per_tres when not specified
		 * and ntasks is specified
		 */
		uint32_t gpus = *num_tasks / ntasks_per_tres;
		/* For now, do type-less GPUs */
		char *save_ptr = NULL, *gres = NULL, *in_val;
		xstrfmtcat(gres, "gpu:%u", gpus);
		in_val = gres;
		if (*num_tasks != ntasks_per_tres * gpus) {
			log_flag(GRES, "%s: -n/--ntasks %u is not a multiply of --ntasks-per-gpu=%u",
				 __func__, *num_tasks, ntasks_per_tres);
			return ESLURM_INVALID_GRES;
		}
		while ((step_gres_data =
				_get_next_step_gres(in_val, &cnt,
						    new_step_list,
						    &save_ptr, &rc))) {
			/* Simulate a tres_per_job specification */
			step_gres_data->gres_per_step = cnt;
			step_gres_data->total_gres =
				MAX(step_gres_data->total_gres, cnt);
			in_val = NULL;
		}
		xfree(gres);
		xassert(list_count(new_step_list) != 0);
	} else if (tmp != NO_VAL64) {
		tmp = tmp * ntasks_per_tres;
		if (*num_tasks < tmp) {
			*num_tasks = tmp;
		}
		if (*cpu_count < tmp) {
			*cpu_count = tmp;
		}
	} else {
		error("%s: ntasks_per_tres was specified, but there was either no task count or no GPU specification to go along with it, or both were already specified.",
		      __func__);
		rc = SLURM_ERROR;
	}

	return rc;
}

/*
 * Given a step's requested gres configuration, validate it and build gres list
 * IN *tres* - step's requested gres input string
 * OUT step_gres_list - List of Gres records for this step to track usage
 * IN job_gres_list - List of Gres records for this job
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_step_state_validate(char *cpus_per_tres,
					   char *tres_per_step,
					   char *tres_per_node,
					   char *tres_per_socket,
					   char *tres_per_task,
					   char *mem_per_tres,
					   uint16_t ntasks_per_tres,
					   List *step_gres_list,
					   List job_gres_list, uint32_t job_id,
					   uint32_t step_id,
					   uint32_t *num_tasks,
					   uint32_t *cpu_count)
{
	int rc;
	gres_step_state_t *step_gres_data;
	List new_step_list;
	uint64_t cnt = 0;

	*step_gres_list = NULL;
	if ((rc = gres_plugin_init()) != SLURM_SUCCESS)
		return rc;

	slurm_mutex_lock(&gres_context_lock);
	new_step_list = list_create(_gres_step_list_delete);
	if (cpus_per_tres) {
		char *in_val = cpus_per_tres, *save_ptr = NULL;
		while ((step_gres_data = _get_next_step_gres(in_val, &cnt,
							    new_step_list,
							    &save_ptr, &rc))) {
			step_gres_data->cpus_per_gres = cnt;
			in_val = NULL;
		}
	}
	if (tres_per_step) {
		char *in_val = tres_per_step, *save_ptr = NULL;
		while ((step_gres_data = _get_next_step_gres(in_val, &cnt,
							    new_step_list,
							    &save_ptr, &rc))) {
			step_gres_data->gres_per_step = cnt;
			in_val = NULL;
			step_gres_data->total_gres =
				MAX(step_gres_data->total_gres, cnt);
		}
	}
	if (tres_per_node) {
		char *in_val = tres_per_node, *save_ptr = NULL;
		while ((step_gres_data = _get_next_step_gres(in_val, &cnt,
							    new_step_list,
							    &save_ptr, &rc))) {
			step_gres_data->gres_per_node = cnt;
			in_val = NULL;
			/* Step only has 1 node, always */
			step_gres_data->total_gres =
				MAX(step_gres_data->total_gres, cnt);
		}
	}
	if (tres_per_socket) {
		char *in_val = tres_per_socket, *save_ptr = NULL;
		while ((step_gres_data = _get_next_step_gres(in_val, &cnt,
							    new_step_list,
							    &save_ptr, &rc))) {
			step_gres_data->gres_per_socket = cnt;
			in_val = NULL;
			// TODO: What is sockets_per_node and ntasks_per_socket?
			// if (*sockets_per_node != NO_VAL16) {
			// 	cnt *= *sockets_per_node;
			// } else if ((*num_tasks != NO_VAL) &&
			// 	   (*ntasks_per_socket != NO_VAL16)) {
			// 	cnt *= ((*num_tasks + *ntasks_per_socket - 1) /
			// 	        *ntasks_per_socket);
			// }
			// step_gres_data->total_gres =
			// 	MAX(step_gres_data->total_gres, cnt);
		}
	}
	if (tres_per_task) {
		char *in_val = tres_per_task, *save_ptr = NULL;
		while ((step_gres_data = _get_next_step_gres(in_val, &cnt,
							    new_step_list,
							    &save_ptr, &rc))) {
			step_gres_data->gres_per_task = cnt;
			in_val = NULL;
			if (*num_tasks != NO_VAL)
				cnt *= *num_tasks;
			step_gres_data->total_gres =
				MAX(step_gres_data->total_gres, cnt);
		}
	}
	if (mem_per_tres) {
		char *in_val = mem_per_tres, *save_ptr = NULL;
		while ((step_gres_data = _get_next_step_gres(in_val, &cnt,
							    new_step_list,
							    &save_ptr, &rc))) {
			step_gres_data->mem_per_gres = cnt;
			in_val = NULL;
		}
	}

	if ((ntasks_per_tres != NO_VAL16) && num_tasks && cpu_count) {
		rc = _handle_ntasks_per_tres_step(new_step_list,
						  ntasks_per_tres,
						  num_tasks,
						  cpu_count);
	}

	if (list_count(new_step_list) == 0) {
		FREE_NULL_LIST(new_step_list);
	} else {
		if (rc == SLURM_SUCCESS)
			_validate_step_counts(new_step_list, job_gres_list,
					      &rc);
		if (rc == SLURM_SUCCESS)
			*step_gres_list = new_step_list;
		else
			FREE_NULL_LIST(new_step_list);
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;
}

static void *_step_state_dup(void *gres_data)
{

	int i;
	gres_step_state_t *gres_ptr = (gres_step_state_t *) gres_data;
	gres_step_state_t *new_gres_ptr;

	xassert(gres_ptr);
	new_gres_ptr = xmalloc(sizeof(gres_step_state_t));
	new_gres_ptr->cpus_per_gres	= gres_ptr->cpus_per_gres;
	new_gres_ptr->gres_per_step	= gres_ptr->gres_per_step;
	new_gres_ptr->gres_per_node	= gres_ptr->gres_per_node;
	new_gres_ptr->gres_per_socket	= gres_ptr->gres_per_socket;
	new_gres_ptr->gres_per_task	= gres_ptr->gres_per_task;
	new_gres_ptr->mem_per_gres	= gres_ptr->mem_per_gres;
	new_gres_ptr->node_cnt		= gres_ptr->node_cnt;
	new_gres_ptr->total_gres	= gres_ptr->total_gres;

	if (gres_ptr->node_in_use)
		new_gres_ptr->node_in_use = bit_copy(gres_ptr->node_in_use);

	if (gres_ptr->gres_cnt_node_alloc) {
		i = sizeof(uint64_t) * gres_ptr->node_cnt;
		new_gres_ptr->gres_cnt_node_alloc = xmalloc(i);
		memcpy(new_gres_ptr->gres_cnt_node_alloc,
		       gres_ptr->gres_cnt_node_alloc, i);
	}
	if (gres_ptr->gres_bit_alloc) {
		new_gres_ptr->gres_bit_alloc = xcalloc(gres_ptr->node_cnt,
						       sizeof(bitstr_t *));
		for (i = 0; i < gres_ptr->node_cnt; i++) {
			if (gres_ptr->gres_bit_alloc[i] == NULL)
				continue;
			new_gres_ptr->gres_bit_alloc[i] =
				bit_copy(gres_ptr->gres_bit_alloc[i]);
		}
	}
	return new_gres_ptr;
}

	uint64_t *gres_cnt_node_alloc;	/* Per node GRES allocated, */

static void *_step_state_dup2(void *gres_data, int node_index)
{

	gres_step_state_t *gres_ptr = (gres_step_state_t *) gres_data;
	gres_step_state_t *new_gres_ptr;

	xassert(gres_ptr);
	new_gres_ptr = xmalloc(sizeof(gres_step_state_t));
	new_gres_ptr->cpus_per_gres	= gres_ptr->cpus_per_gres;
	new_gres_ptr->gres_per_step	= gres_ptr->gres_per_step;
	new_gres_ptr->gres_per_node	= gres_ptr->gres_per_node;
	new_gres_ptr->gres_per_socket	= gres_ptr->gres_per_socket;
	new_gres_ptr->gres_per_task	= gres_ptr->gres_per_task;
	new_gres_ptr->mem_per_gres	= gres_ptr->mem_per_gres;
	new_gres_ptr->node_cnt		= 1;
	new_gres_ptr->total_gres	= gres_ptr->total_gres;

	if (gres_ptr->node_in_use)
		new_gres_ptr->node_in_use = bit_copy(gres_ptr->node_in_use);

	if (gres_ptr->gres_cnt_node_alloc) {
		new_gres_ptr->gres_cnt_node_alloc = xmalloc(sizeof(uint64_t));
		new_gres_ptr->gres_cnt_node_alloc[0] =
		       gres_ptr->gres_cnt_node_alloc[node_index];
	}

	if ((node_index < gres_ptr->node_cnt) && gres_ptr->gres_bit_alloc &&
	    gres_ptr->gres_bit_alloc[node_index]) {
		new_gres_ptr->gres_bit_alloc = xmalloc(sizeof(bitstr_t *));
		new_gres_ptr->gres_bit_alloc[0] =
			bit_copy(gres_ptr->gres_bit_alloc[node_index]);
	}
	return new_gres_ptr;
}

/*
 * Create a copy of a step's gres state
 * IN gres_list - List of Gres records for this step to track usage
 * RET The copy or NULL on failure
 */
List gres_plugin_step_state_dup(List gres_list)
{
	return gres_plugin_step_state_extract(gres_list, -1);
}

/*
 * Create a copy of a step's gres state for a particular node index
 * IN gres_list - List of Gres records for this step to track usage
 * IN node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
List gres_plugin_step_state_extract(List gres_list, int node_index)
{
	ListIterator gres_iter;
	gres_state_t *gres_ptr, *new_gres_state;
	List new_gres_list = NULL;
	void *new_gres_data;

	if (gres_list == NULL)
		return new_gres_list;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		if (node_index == -1)
			new_gres_data = _step_state_dup(gres_ptr->gres_data);
		else {
			new_gres_data = _step_state_dup2(gres_ptr->gres_data,
							 node_index);
		}
		if (new_gres_list == NULL) {
			new_gres_list = list_create(_gres_step_list_delete);
		}
		new_gres_state = xmalloc(sizeof(gres_state_t));
		new_gres_state->plugin_id = gres_ptr->plugin_id;
		new_gres_state->gres_data = new_gres_data;
		list_append(new_gres_list, new_gres_state);
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return new_gres_list;
}

/*
 * A job allocation size has changed. Update the job step gres information
 * bitmaps and other data structures.
 * IN gres_list - List of Gres records for this step to track usage
 * IN orig_job_node_bitmap - bitmap of nodes in the original job allocation
 * IN new_job_node_bitmap  - bitmap of nodes in the new job allocation
 */
void gres_plugin_step_state_rebase(List gres_list,
				   bitstr_t *orig_job_node_bitmap,
				   bitstr_t *new_job_node_bitmap)
{
	ListIterator gres_iter;
	gres_state_t *gres_ptr;
	gres_step_state_t *gres_step_ptr;
	int new_node_cnt;
	int i_first, i_last, i;
	int old_inx, new_inx;
	bitstr_t *new_node_in_use;
	bitstr_t **new_gres_bit_alloc = NULL;

	if (gres_list == NULL)
		return;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		gres_step_ptr = (gres_step_state_t *) gres_ptr->gres_data;
		if (!gres_step_ptr)
			continue;
		if (!gres_step_ptr->node_in_use) {
			error("gres_plugin_step_state_rebase: node_in_use is NULL");
			continue;
		}
		new_node_cnt = bit_set_count(new_job_node_bitmap);
		i_first = MIN(bit_ffs(orig_job_node_bitmap),
			      bit_ffs(new_job_node_bitmap));
		i_first = MAX(i_first, 0);
		i_last  = MAX(bit_fls(orig_job_node_bitmap),
			      bit_fls(new_job_node_bitmap));
		if (i_last == -1) {
			error("gres_plugin_step_state_rebase: node_bitmaps "
			      "are empty");
			continue;
		}
		new_node_in_use = bit_alloc(new_node_cnt);

		old_inx = new_inx = -1;
		for (i = i_first; i <= i_last; i++) {
			bool old_match = false, new_match = false;
			if (bit_test(orig_job_node_bitmap, i)) {
				old_match = true;
				old_inx++;
			}
			if (bit_test(new_job_node_bitmap, i)) {
				new_match = true;
				new_inx++;
			}
			if (old_match && new_match) {
				bit_set(new_node_in_use, new_inx);
				if (gres_step_ptr->gres_bit_alloc) {
					if (!new_gres_bit_alloc) {
						new_gres_bit_alloc =
							xcalloc(new_node_cnt,
								sizeof(bitstr_t *));
					}
					new_gres_bit_alloc[new_inx] =
						gres_step_ptr->gres_bit_alloc[old_inx];
				}
			} else if (old_match &&
				   gres_step_ptr->gres_bit_alloc &&
				   gres_step_ptr->gres_bit_alloc[old_inx]) {
				/* Node removed from job allocation,
				 * release step's resources */
				bit_free(gres_step_ptr->
					 gres_bit_alloc[old_inx]);
			}
		}

		gres_step_ptr->node_cnt = new_node_cnt;
		bit_free(gres_step_ptr->node_in_use);
		gres_step_ptr->node_in_use = new_node_in_use;
		xfree(gres_step_ptr->gres_bit_alloc);
		gres_step_ptr->gres_bit_alloc = new_gres_bit_alloc;
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return;
}

/*
 * Pack a step's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_plugin_step_alloc()
 * IN/OUT buffer - location to write state to
 * IN step_id - job and step ID for logging
 */
extern int gres_plugin_step_state_pack(List gres_list, Buf buffer,
				       slurm_step_id_t *step_id,
				       uint16_t protocol_version)
{
	int i, rc = SLURM_SUCCESS;
	uint32_t top_offset, tail_offset, magic = GRES_MAGIC;
	uint16_t rec_cnt = 0;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;
	gres_step_state_t *gres_step_ptr;

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	if (gres_list == NULL)
		return rc;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		gres_step_ptr = (gres_step_state_t *) gres_ptr->gres_data;

		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			pack16(gres_step_ptr->cpus_per_gres, buffer);
			pack16(gres_step_ptr->flags, buffer);
			pack64(gres_step_ptr->gres_per_step, buffer);
			pack64(gres_step_ptr->gres_per_node, buffer);
			pack64(gres_step_ptr->gres_per_socket, buffer);
			pack64(gres_step_ptr->gres_per_task, buffer);
			pack64(gres_step_ptr->mem_per_gres, buffer);
			pack64(gres_step_ptr->total_gres, buffer);
			pack32(gres_step_ptr->node_cnt, buffer);
			pack_bit_str_hex(gres_step_ptr->node_in_use, buffer);
			if (gres_step_ptr->gres_cnt_node_alloc) {
				pack8((uint8_t) 1, buffer);
				pack64_array(gres_step_ptr->gres_cnt_node_alloc,
					     gres_step_ptr->node_cnt, buffer);
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (gres_step_ptr->gres_bit_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_step_ptr->node_cnt; i++)
					pack_bit_str_hex(gres_step_ptr->
							 gres_bit_alloc[i],
							 buffer);
			} else {
				pack8((uint8_t) 0, buffer);
			}
			rec_cnt++;
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	tail_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, top_offset);
	pack16(rec_cnt, buffer);
	set_buf_offset(buffer, tail_offset);

	return rc;
}

/*
 * Unpack a step's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_step_state_pack()
 * IN/OUT buffer - location to read state from
 * IN step_id - job and step ID for logging
 */
extern int gres_plugin_step_state_unpack(List *gres_list, Buf buffer,
					 slurm_step_id_t *step_id,
					 uint16_t protocol_version)
{
	int i, rc;
	uint32_t magic = 0, plugin_id = 0, uint32_tmp = 0;
	uint16_t rec_cnt = 0;
	uint8_t data_flag = 0;
	gres_state_t *gres_ptr;
	gres_step_state_t *gres_step_ptr = NULL;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_step_list_delete);
	}

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;
		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_step_ptr = xmalloc(sizeof(gres_step_state_t));
			safe_unpack16(&gres_step_ptr->cpus_per_gres, buffer);
			safe_unpack16(&gres_step_ptr->flags, buffer);
			safe_unpack64(&gres_step_ptr->gres_per_step, buffer);
			safe_unpack64(&gres_step_ptr->gres_per_node, buffer);
			safe_unpack64(&gres_step_ptr->gres_per_socket, buffer);
			safe_unpack64(&gres_step_ptr->gres_per_task, buffer);
			safe_unpack64(&gres_step_ptr->mem_per_gres, buffer);
			safe_unpack64(&gres_step_ptr->total_gres, buffer);
			safe_unpack32(&gres_step_ptr->node_cnt, buffer);
			if (gres_step_ptr->node_cnt > NO_VAL)
				goto unpack_error;
			unpack_bit_str_hex(&gres_step_ptr->node_in_use, buffer);
			safe_unpack8(&data_flag, buffer);
			if (data_flag) {
				safe_unpack64_array(
					&gres_step_ptr->gres_cnt_node_alloc,
					&uint32_tmp, buffer);
			}
			safe_unpack8(&data_flag, buffer);
			if (data_flag) {
				gres_step_ptr->gres_bit_alloc =
					xcalloc(gres_step_ptr->node_cnt,
						sizeof(bitstr_t *));
				for (i = 0; i < gres_step_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_step_ptr->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			goto unpack_error;
		}

		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_context[i].plugin_id == plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			/*
			 * A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data.
			 */
			info("%s: no plugin configured to unpack data type %u from %ps",
			      __func__, plugin_id, step_id);
			_step_state_delete(gres_step_ptr);
			gres_step_ptr = NULL;
			continue;
		}
		gres_ptr = xmalloc(sizeof(gres_state_t));
		gres_ptr->plugin_id = gres_context[i].plugin_id;
		gres_ptr->gres_data = gres_step_ptr;
		gres_step_ptr = NULL;
		list_append(*gres_list, gres_ptr);
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error from %ps", __func__, step_id);
	if (gres_step_ptr)
		_step_state_delete(gres_step_ptr);
	slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

/* Return the count of GRES of a specific name on this machine
 * IN step_gres_list - generated by gres_plugin_step_alloc()
 * IN gres_name - name of the GRES to match
 * RET count of GRES of this specific name available to the job or NO_VAL64
 */
extern uint64_t gres_plugin_step_count(List step_gres_list, char *gres_name)
{
	uint64_t gres_cnt = NO_VAL64;
	gres_state_t *gres_ptr = NULL;
	gres_step_state_t *gres_step_ptr = NULL;
	ListIterator gres_iter;
	int i;

	if (!step_gres_list)
		return gres_cnt;

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (xstrcmp(gres_context[i].gres_name, gres_name))
			continue;
		gres_iter = list_iterator_create(step_gres_list);
		while ((gres_ptr = (gres_state_t *)list_next(gres_iter))) {
			if (gres_ptr->plugin_id != gres_context[i].plugin_id)
				continue;
			gres_step_ptr = (gres_step_state_t*)gres_ptr->gres_data;
			if (gres_cnt == NO_VAL64)
				gres_cnt = gres_step_ptr->gres_per_node;
			else
				gres_cnt += gres_step_ptr->gres_per_node;
		}
		list_iterator_destroy(gres_iter);
		break;
	}
	slurm_mutex_unlock(&gres_context_lock);

	return gres_cnt;
}

/*
 * Given a GRES context index, return a bitmap representing those GRES
 * which are available from the CPUs current allocated to this process.
 * This function only works with task/cgroup and constrained devices or
 * if the job step has access to the entire node's resources.
 */
static bitstr_t * _get_usable_gres(int context_inx)
{
#if defined(__APPLE__)
	return NULL;
#else
#ifdef __NetBSD__
	// On NetBSD, cpuset_t is an opaque data type
	cpuset_t *mask = cpuset_create();
#else
	cpu_set_t mask;
#endif
	bitstr_t *usable_gres = NULL;
	int i, i_last, rc;
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	int gres_inx = 0;

	if (!gres_conf_list) {
		error("gres_conf_list is null!");
		return NULL;
	}

	CPU_ZERO(&mask);
#ifdef __FreeBSD__
	rc = cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1,
				sizeof(mask), &mask);
#else
	rc = sched_getaffinity(0, sizeof(mask), &mask);
#endif
	if (rc) {
		error("sched_getaffinity error: %m");
		return usable_gres;
	}

	usable_gres = bit_alloc(MAX_GRES_BITMAP);
	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = (gres_slurmd_conf_t *) list_next(iter))) {
		if (gres_slurmd_conf->plugin_id !=
		    gres_context[context_inx].plugin_id)
			continue;
		if ((gres_inx + gres_slurmd_conf->count) >= MAX_GRES_BITMAP) {
			error("GRES %s bitmap overflow ((%d + %"PRIu64") >= %d)",
			      gres_slurmd_conf->name, gres_inx,
			      gres_slurmd_conf->count, MAX_GRES_BITMAP);
			continue;
		}
		if (!gres_slurmd_conf->cpus_bitmap) {
			bit_nset(usable_gres, gres_inx,
				 gres_inx + gres_slurmd_conf->count - 1);
		} else {
			i_last = bit_fls(gres_slurmd_conf->cpus_bitmap);
			for (i = 0; i <= i_last; i++) {
				if (!bit_test(gres_slurmd_conf->cpus_bitmap, i))
					continue;
				if (!CPU_ISSET(i, &mask))
					continue;
				bit_nset(usable_gres, gres_inx,
					 gres_inx + gres_slurmd_conf->count -1);
				break;
			}
		}
		gres_inx += gres_slurmd_conf->count;
	}
	list_iterator_destroy(iter);

#ifdef __NetBSD__
	cpuset_destroy(mask);
#endif

	return usable_gres;
#endif
}

/*
 * If ntasks_per_gres is > 0, modify usable_gres so that this task can only use
 * one GPU. This will make it so only one GPU can be bound to this task later
 * on. Use local_proc_id (task rank) and ntasks_per_gres to determine which GPU
 * to bind to. Assign out tasks to GPUs in a block-like distribution.
 * TODO: This logic needs improvement when tasks and GPUs span sockets.
 *
 * IN/OUT - usable_gres
 * IN - ntasks_per_gres
 * IN - local_proc_id
 */
static void _filter_usable_gres(bitstr_t *usable_gres, int ntasks_per_gres,
				int local_proc_id)
{
	int gpu_count, n, idx;
	char *str;
	if (ntasks_per_gres <= 0)
		return;

	/* # of GPUs this task has an affinity to */
	gpu_count = bit_set_count(usable_gres);

	str = bit_fmt_hexmask_trim(usable_gres);
	log_flag(GRES, "%s: local_proc_id = %d; usable_gres (ALL): %s",
		 __func__, local_proc_id, str);
	xfree(str);

	/* No need to filter if no usable_gres or already only 1 to use */
	if ((gpu_count == 0) || (gpu_count == 1)) {
		log_flag(GRES, "%s: (task %d) No need to filter since usable_gres count is 0 or 1",
			 __func__, local_proc_id);
		return;
	}

	/* Map task rank to one of the GPUs (block distribution) */
	n = (local_proc_id / ntasks_per_gres) % gpu_count;
	/* Find the nth set bit in usable_gres */
	idx = bit_get_bit_num(usable_gres, n);

	log_flag(GRES, "%s: local_proc_id = %d; n = %d; ntasks_per_gres = %d; idx = %d",
		 __func__, local_proc_id, n, ntasks_per_gres, idx);

	if (idx == -1) {
		error("%s: (task %d) usable_gres did not have >= %d set GPUs, so can't do a single bind on set GPU #%d. Defaulting back to the original usable_gres.",
		      __func__, local_proc_id, n + 1, n);
		return;
	}

	/* Return a bitmap with this as the only usable GRES */
	bit_clear_all(usable_gres);
	bit_set(usable_gres, idx);
	str = bit_fmt_hexmask_trim(usable_gres);
	log_flag(GRES, "%s: local_proc_id = %d; usable_gres (single filter): %s",
		 __func__, local_proc_id, str);
	xfree(str);
}

/*
 * Configure the GRES hardware allocated to the current step while privileged
 *
 * IN step_gres_list - Step's GRES specification
 * IN node_id        - relative position of this node in step
 * IN settings       - string containing configuration settings for the hardware
 */
extern void gres_plugin_step_hardware_init(List step_gres_list,
					   uint32_t node_id, char *settings)
{
	int i;
	ListIterator iter;
	gres_state_t *gres_ptr;
	gres_step_state_t *gres_step_ptr;
	bitstr_t *devices;

	if (!step_gres_list)
		return;

	(void) gres_plugin_init();
	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (gres_context[i].ops.step_hardware_init == NULL)
			continue;

		iter = list_iterator_create(step_gres_list);
		while ((gres_ptr = list_next(iter))) {
			if (gres_ptr->plugin_id == gres_context[i].plugin_id)
				break;
		}
		list_iterator_destroy(iter);
		if (!gres_ptr || !gres_ptr->gres_data)
			continue;
		gres_step_ptr = (gres_step_state_t *) gres_ptr->gres_data;
		if ((gres_step_ptr->node_cnt != 1) ||
		    !gres_step_ptr->gres_bit_alloc ||
		    !gres_step_ptr->gres_bit_alloc[0])
			continue;

		devices = gres_step_ptr->gres_bit_alloc[0];
		if (settings)
			debug2("settings: %s", settings);
		if (devices) {
			char *dev_str = bit_fmt_full(devices);
			info("devices: %s", dev_str);
			xfree(dev_str);
		}
		(*(gres_context[i].ops.step_hardware_init))(devices, settings);
	}
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Optionally undo GRES hardware configuration while privileged
 */
extern void gres_plugin_step_hardware_fini(void)
{
	int i;
	(void) gres_plugin_init();
	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (gres_context[i].ops.step_hardware_fini == NULL) {
			continue;
		}
		(*(gres_context[i].ops.step_hardware_fini)) ();
	}
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Given a set GRES maps and the local process ID, return the bitmap of
 * GRES that should be available to this task.
 */
static bitstr_t *_get_gres_map(char *map_gres, int local_proc_id)
{
	bitstr_t *usable_gres = NULL;
	char *tmp, *tok, *save_ptr = NULL, *mult;
	int task_offset = 0, task_mult;
	int map_value;

	if (!map_gres || !map_gres[0])
		return NULL;

	while (usable_gres == NULL) {
		tmp = xstrdup(map_gres);
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			if ((mult = strchr(tok, '*'))) {
				mult[0] = '\0';
				task_mult = atoi(mult + 1);
			} else
				task_mult = 1;
			if (task_mult == 0) {
				error("Repetition count of 0 not allowed in --gpu-bind=map_gpu, using 1 instead");
				task_mult = 1;
			}
			if ((local_proc_id >= task_offset) &&
			    (local_proc_id <= (task_offset + task_mult - 1))) {
				map_value = strtol(tok, NULL, 0);
				if ((map_value < 0) ||
				    (map_value >= MAX_GRES_BITMAP)) {
					error("Invalid --gpu-bind=map_gpu value specified.");
					xfree(tmp);
					goto end;	/* Bad value */
				}
				usable_gres = bit_alloc(MAX_GRES_BITMAP);
				bit_set(usable_gres, map_value);
				break;	/* All done */
			} else {
				task_offset += task_mult;
			}
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);
	}
end:

	return usable_gres;
}

/*
 * Given a set GRES masks and the local process ID, return the bitmap of
 * GRES that should be available to this task.
 */
static bitstr_t * _get_gres_mask(char *mask_gres, int local_proc_id)
{
	bitstr_t *usable_gres = NULL;
	char *tmp, *tok, *save_ptr = NULL, *mult;
	int i, task_offset = 0, task_mult;
	uint64_t mask_value;

	if (!mask_gres || !mask_gres[0])
		return NULL;

	while (usable_gres == NULL) {
		tmp = xstrdup(mask_gres);
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			if ((mult = strchr(tok, '*')))
				task_mult = atoi(mult + 1);
			else
				task_mult = 1;
			if (task_mult == 0) {
				error("Repetition count of 0 not allowed in --gpu-bind=mask_gpu, using 1 instead");
				task_mult = 1;
			}
			if ((local_proc_id >= task_offset) &&
			    (local_proc_id <= (task_offset + task_mult - 1))) {
				mask_value = strtol(tok, NULL, 0);
				if ((mask_value <= 0) ||
				    (mask_value >= 0xffffffff)) {
					error("Invalid --gpu-bind=mask_gpu value specified.");
					xfree(tmp);
					goto end;	/* Bad value */
				}
				usable_gres = bit_alloc(MAX_GRES_BITMAP);
				for (i = 0; i < 64; i++) {
					if ((mask_value >> i) & 0x1)
						bit_set(usable_gres, i);
				}
				break;	/* All done */
			} else {
				task_offset += task_mult;
			}
			tok = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);
	}
end:

	return usable_gres;
}

/*
 * Set environment as required for all tasks of a job step
 * IN/OUT job_env_ptr - environment variable array
 * IN step_gres_list - generated by gres_plugin_step_alloc()
 * IN accel_bind_type - GRES binding options (old format, a bitmap)
 * IN tres_bind - TRES binding directives (new format, a string)
 * IN local_proc_id - task rank, local to this compute node only
 */
extern void gres_plugin_step_set_env(char ***job_env_ptr, List step_gres_list,
				     uint16_t accel_bind_type, char *tres_bind,
				     int local_proc_id)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_ptr = NULL;
	bool bind_gpu = accel_bind_type & ACCEL_BIND_CLOSEST_GPU;
	bool bind_nic = accel_bind_type & ACCEL_BIND_CLOSEST_NIC;
	bool bind_mic = accel_bind_type & ACCEL_BIND_CLOSEST_MIC;
	char *sep, *map_gpu = NULL, *mask_gpu = NULL;
	bitstr_t *usable_gres = NULL;
	bool found;
	gres_internal_flags_t gres_internal_flags = GRES_INTERNAL_FLAG_NONE;
	int tasks_per_gres = 0;

	if (!bind_gpu && tres_bind && (sep = strstr(tres_bind, "gpu:"))) {
		sep += 4;
		if (!strncasecmp(sep, "verbose,", 8)) {
			gres_internal_flags |= GRES_INTERNAL_FLAG_VERBOSE;
			sep += 8;
		}
		if (!strncasecmp(sep, "single:", 7)) {
			sep += 7;
			tasks_per_gres = strtol(sep, NULL, 0);
			if ((tasks_per_gres <= 0) ||
			    (tasks_per_gres == LONG_MAX)) {
				error("%s: single:%s does not specify a valid number. Defaulting to 1.",
				      __func__, sep);
				tasks_per_gres = 1;
			}
			bind_gpu = true;
		} else if (!strncasecmp(sep, "closest", 7))
			bind_gpu = true;
		else if (!strncasecmp(sep, "map_gpu:", 8))
			map_gpu = sep + 8;
		else if (!strncasecmp(sep, "mask_gpu:", 9))
			mask_gpu = sep + 9;
	}

	(void) gres_plugin_init();
	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (!gres_context[i].ops.step_set_env)
			continue;	/* No plugin to call */
		if (bind_gpu || bind_mic || bind_nic || map_gpu || mask_gpu) {
			if (!xstrcmp(gres_context[i].gres_name, "gpu")) {
				if (map_gpu) {
					usable_gres = _get_gres_map(map_gpu,
								local_proc_id);
				} else if (mask_gpu) {
					usable_gres = _get_gres_mask(mask_gpu,
								local_proc_id);
				} else if (bind_gpu) {
					usable_gres = _get_usable_gres(i);
					_filter_usable_gres(usable_gres,
							    tasks_per_gres,
							    local_proc_id);
				}
				else
					continue;
			} else if (!xstrcmp(gres_context[i].gres_name,
					    "mic")) {
				if (bind_mic)
					usable_gres = _get_usable_gres(i);
				else
					continue;
			} else if (!xstrcmp(gres_context[i].gres_name,
					    "nic")) {
				if (bind_nic)
					usable_gres = _get_usable_gres(i);
				else
					continue;
			} else {
				continue;
			}
		}
		found = false;
		if (step_gres_list) {
			gres_iter = list_iterator_create(step_gres_list);
			while ((gres_ptr = (gres_state_t *)
				list_next(gres_iter))) {
				if (gres_ptr->plugin_id !=
				    gres_context[i].plugin_id)
					continue;
				if (accel_bind_type || tres_bind) {
					(*(gres_context[i].ops.step_reset_env))
						(job_env_ptr,
						 gres_ptr->gres_data,
						 usable_gres,
						 gres_internal_flags);
				} else {
					(*(gres_context[i].ops.step_set_env))
						(job_env_ptr,
						 gres_ptr->gres_data,
						 gres_internal_flags);
				}
				found = true;
			}
			list_iterator_destroy(gres_iter);
		}
		if (!found) { /* No data fond */
			if (accel_bind_type || tres_bind) {
				(*(gres_context[i].ops.step_reset_env))
					(job_env_ptr, NULL, NULL,
					 gres_internal_flags);
			} else {
				(*(gres_context[i].ops.step_set_env))
					(job_env_ptr, NULL,
					 gres_internal_flags);
			}
		}
		FREE_NULL_BITMAP(usable_gres);
	}
	slurm_mutex_unlock(&gres_context_lock);
	FREE_NULL_BITMAP(usable_gres);
}

static void _step_state_log(void *gres_data, slurm_step_id_t *step_id,
			    char *gres_name)
{
	gres_step_state_t *gres_ptr = (gres_step_state_t *) gres_data;
	char tmp_str[128];
	int i;

	xassert(gres_ptr);
	info("gres:%s type:%s(%u) %ps flags:%s state", gres_name,
	     gres_ptr->type_name, gres_ptr->type_id, step_id,
	     _gres_flags_str(gres_ptr->flags));
	if (gres_ptr->cpus_per_gres)
		info("  cpus_per_gres:%u", gres_ptr->cpus_per_gres);
	if (gres_ptr->gres_per_step)
		info("  gres_per_step:%"PRIu64, gres_ptr->gres_per_step);
	if (gres_ptr->gres_per_node) {
		info("  gres_per_node:%"PRIu64" node_cnt:%u",
		     gres_ptr->gres_per_node, gres_ptr->node_cnt);
	}
	if (gres_ptr->gres_per_socket)
		info("  gres_per_socket:%"PRIu64, gres_ptr->gres_per_socket);
	if (gres_ptr->gres_per_task)
		info("  gres_per_task:%"PRIu64, gres_ptr->gres_per_task);
	if (gres_ptr->mem_per_gres)
		info("  mem_per_gres:%"PRIu64, gres_ptr->mem_per_gres);

	if (gres_ptr->node_in_use == NULL)
		info("  node_in_use:NULL");
	else if (gres_ptr->gres_bit_alloc == NULL)
		info("  gres_bit_alloc:NULL");
	else {
		for (i = 0; i < gres_ptr->node_cnt; i++) {
			if (!bit_test(gres_ptr->node_in_use, i))
				continue;
			if (gres_ptr->gres_bit_alloc[i]) {
				bit_fmt(tmp_str, sizeof(tmp_str),
					gres_ptr->gres_bit_alloc[i]);
				info("  gres_bit_alloc[%d]:%s of %d", i,
				     tmp_str,
				     (int)bit_size(gres_ptr->gres_bit_alloc[i]));
			} else
				info("  gres_bit_alloc[%d]:NULL", i);
		}
	}
}

/*
 * Log a step's current gres state
 * IN gres_list - generated by gres_plugin_step_alloc()
 * IN job_id - job's ID
 */
extern void gres_plugin_step_state_log(List gres_list, uint32_t job_id,
				       uint32_t step_id)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;
	slurm_step_id_t tmp_step_id;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES) || !gres_list)
		return;

	(void) gres_plugin_init();

	tmp_step_id.job_id = job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = step_id;

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_ptr->plugin_id != gres_context[i].plugin_id)
				continue;
			_step_state_log(gres_ptr->gres_data, &tmp_step_id,
					gres_context[i].gres_name);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Determine how many cores of a job's allocation can be allocated to a step
 *	on a specific node
 * IN job_gres_list - a running job's gres info
 * IN/OUT step_gres_list - a pending job step's gres requirements
 * IN node_offset - index into the job's node allocation
 * IN first_step_node - true if this is node zero of the step (do initialization)
 * IN cpus_per_task - number of CPUs required per task
 * IN max_rem_nodes - maximum nodes remaining for step (including this one)
 * IN ignore_alloc - if set ignore resources already allocated to running steps
 * IN job_id, step_id - ID of the step being allocated.
 * RET Count of available cores on this node (sort of):
 *     NO_VAL64 if no limit or 0 if node is not usable
 */
extern uint64_t gres_plugin_step_test(List step_gres_list, List job_gres_list,
				      int node_offset, bool first_step_node,
				      uint16_t cpus_per_task, int max_rem_nodes,
				      bool ignore_alloc,
				      uint32_t job_id, uint32_t step_id)
{
	uint64_t core_cnt, tmp_cnt;
	ListIterator step_gres_iter;
	gres_state_t *job_gres_ptr, *step_gres_ptr;
	gres_step_state_t *step_data_ptr = NULL;
	slurm_step_id_t tmp_step_id;

	if (step_gres_list == NULL)
		return NO_VAL64;
	if (job_gres_list == NULL)
		return 0;

	if (cpus_per_task == 0)
		cpus_per_task = 1;
	core_cnt = NO_VAL64;
	(void) gres_plugin_init();

	tmp_step_id.job_id = job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = step_id;

	slurm_mutex_lock(&gres_context_lock);
	step_gres_iter = list_iterator_create(step_gres_list);
	while ((step_gres_ptr = (gres_state_t *) list_next(step_gres_iter))) {
		gres_key_t job_search_key;
		step_data_ptr = (gres_step_state_t *)step_gres_ptr->gres_data;
		job_search_key.plugin_id = step_gres_ptr->plugin_id;
		if (step_data_ptr->type_name)
			job_search_key.type_id = step_data_ptr->type_id;
		else
			job_search_key.type_id = NO_VAL;

		job_search_key.node_offset = node_offset;
		if (!(job_gres_ptr = list_find_first(
			      job_gres_list,
			      _gres_find_job_by_key_with_cnt,
			      &job_search_key))) {
			/* job lack resources required by the step */
			core_cnt = 0;
			break;
		}

		tmp_cnt = _step_test(step_data_ptr,
				     job_gres_ptr->gres_data,
				     node_offset, first_step_node,
				     cpus_per_task, max_rem_nodes,
				     ignore_alloc,
				     &tmp_step_id,
				     step_gres_ptr->plugin_id);
		if ((tmp_cnt != NO_VAL64) && (tmp_cnt < core_cnt))
			core_cnt = tmp_cnt;

		if (core_cnt == 0)
			break;
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return core_cnt;
}

/*
 * Return TRUE if this plugin ID consumes GRES count > 1 for a single device
 * file (e.g. MPS)
 */
static bool _shared_gres(uint32_t plugin_id)
{
	if (plugin_id == mps_plugin_id)
		return true;
	return false;
}
/*
 * Return TRUE if this plugin ID shares resources with another GRES that
 * consumes subsets of its resources (e.g. GPU)
 */
static bool _sharing_gres(uint32_t plugin_id)
{
	if (plugin_id == gpu_plugin_id)
		return true;
	return false;
}

static int _step_alloc(void *step_gres_data, void *job_gres_data,
		       uint32_t plugin_id, int node_offset,
		       bool first_step_node,
		       slurm_step_id_t *step_id,
		       uint16_t tasks_on_node, uint32_t rem_nodes)
{
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint64_t gres_needed, gres_avail, max_gres = 0;
	bitstr_t *gres_bit_alloc;
	int i, len;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if (job_gres_ptr->node_cnt == 0)	/* no_consume */
		return SLURM_SUCCESS;

	if (node_offset >= job_gres_ptr->node_cnt) {
		error("gres/%s: %s for %ps, node offset invalid (%d >= %u)",
		      job_gres_ptr->gres_name, __func__, step_id, node_offset,
		      job_gres_ptr->node_cnt);
		return SLURM_ERROR;
	}

	if (first_step_node)
		step_gres_ptr->total_gres = 0;
	if (step_gres_ptr->gres_per_node) {
		gres_needed = step_gres_ptr->gres_per_node;
	} else if (step_gres_ptr->gres_per_task) {
		gres_needed = step_gres_ptr->gres_per_task * tasks_on_node;
	} else if (step_gres_ptr->gres_per_step && (rem_nodes == 1)) {
		gres_needed = step_gres_ptr->gres_per_step -
			      step_gres_ptr->total_gres;
	} else if (step_gres_ptr->gres_per_step) {
		/* Leave at least one GRES per remaining node */
		max_gres = step_gres_ptr->gres_per_step -
			   step_gres_ptr->total_gres - (rem_nodes - 1);
		gres_needed = 1;
	} else {
		/*
		 * No explicit step GRES specification.
		 * Note that gres_per_socket is not supported for steps
		 */
		gres_needed = job_gres_ptr->gres_cnt_node_alloc[node_offset];
	}
	if (step_gres_ptr->node_cnt == 0)
		step_gres_ptr->node_cnt = job_gres_ptr->node_cnt;
	if (!step_gres_ptr->gres_cnt_node_alloc) {
		step_gres_ptr->gres_cnt_node_alloc =
			xcalloc(step_gres_ptr->node_cnt, sizeof(uint64_t));
	}

	if (job_gres_ptr->gres_cnt_node_alloc &&
	    job_gres_ptr->gres_cnt_node_alloc[node_offset])
		gres_avail = job_gres_ptr->gres_cnt_node_alloc[node_offset];
	else if (job_gres_ptr->gres_bit_select &&
		 job_gres_ptr->gres_bit_select[node_offset])
		gres_avail = bit_set_count(
				job_gres_ptr->gres_bit_select[node_offset]);
	else if (job_gres_ptr->gres_cnt_node_alloc)
		gres_avail = job_gres_ptr->gres_cnt_node_alloc[node_offset];
	else
		gres_avail = job_gres_ptr->gres_per_node;
	if (gres_needed > gres_avail) {
		error("gres/%s: %s for %ps, step's > job's "
		      "for node %d (%"PRIu64" > %"PRIu64")",
		      job_gres_ptr->gres_name, __func__,
		      step_id, node_offset, gres_needed, gres_avail);
		return SLURM_ERROR;
	}

	if (!job_gres_ptr->gres_cnt_step_alloc) {
		job_gres_ptr->gres_cnt_step_alloc =
			xcalloc(job_gres_ptr->node_cnt, sizeof(uint64_t));
	}

	if (gres_needed >
	    (gres_avail - job_gres_ptr->gres_cnt_step_alloc[node_offset])) {
		error("gres/%s: %s for %ps, step's > job's "
		      "remaining for node %d (%"PRIu64" > "
		      "(%"PRIu64" - %"PRIu64"))",
		      job_gres_ptr->gres_name, __func__,
		      step_id, node_offset, gres_needed, gres_avail,
		      job_gres_ptr->gres_cnt_step_alloc[node_offset]);
		return SLURM_ERROR;
	}
	gres_avail -= job_gres_ptr->gres_cnt_step_alloc[node_offset];
	if (max_gres)
		gres_needed = MIN(gres_avail, max_gres);

	if (step_gres_ptr->gres_cnt_node_alloc &&
	    (node_offset < step_gres_ptr->node_cnt))
		step_gres_ptr->gres_cnt_node_alloc[node_offset] = gres_needed;
	step_gres_ptr->total_gres += gres_needed;

	if (step_gres_ptr->node_in_use == NULL) {
		step_gres_ptr->node_in_use = bit_alloc(job_gres_ptr->node_cnt);
	}
	bit_set(step_gres_ptr->node_in_use, node_offset);
	job_gres_ptr->gres_cnt_step_alloc[node_offset] += gres_needed;

	if ((job_gres_ptr->gres_bit_alloc == NULL) ||
	    (job_gres_ptr->gres_bit_alloc[node_offset] == NULL)) {
		debug3("gres/%s: %s gres_bit_alloc for %ps is NULL",
		       job_gres_ptr->gres_name, __func__, step_id);
		return SLURM_SUCCESS;
	}

	gres_bit_alloc = bit_copy(job_gres_ptr->gres_bit_alloc[node_offset]);
	len = bit_size(gres_bit_alloc);
	if (_shared_gres(plugin_id)) {
		for (i = 0; i < len; i++) {
			if (gres_needed > 0) {
				if (bit_test(gres_bit_alloc, i))
					gres_needed = 0;
			} else {
				bit_clear(gres_bit_alloc, i);
			}
		}
	} else {
		if (job_gres_ptr->gres_bit_step_alloc &&
		    job_gres_ptr->gres_bit_step_alloc[node_offset]) {
			bit_and_not(gres_bit_alloc,
				job_gres_ptr->gres_bit_step_alloc[node_offset]);
		}
		for (i = 0; i < len; i++) {
			if (gres_needed > 0) {
				if (bit_test(gres_bit_alloc, i))
					gres_needed--;
			} else {
				bit_clear(gres_bit_alloc, i);
			}
		}
	}
	if (gres_needed) {
		error("gres/%s: %s %ps oversubscribed resources on node %d",
		      job_gres_ptr->gres_name, __func__, step_id, node_offset);
	}

	if (job_gres_ptr->gres_bit_step_alloc == NULL) {
		job_gres_ptr->gres_bit_step_alloc =
			xcalloc(job_gres_ptr->node_cnt, sizeof(bitstr_t *));
	}
	if (job_gres_ptr->gres_bit_step_alloc[node_offset]) {
		bit_or(job_gres_ptr->gres_bit_step_alloc[node_offset],
		       gres_bit_alloc);
	} else {
		job_gres_ptr->gres_bit_step_alloc[node_offset] =
			bit_copy(gres_bit_alloc);
	}
	if (step_gres_ptr->gres_bit_alloc == NULL) {
		step_gres_ptr->gres_bit_alloc = xcalloc(job_gres_ptr->node_cnt,
							sizeof(bitstr_t *));
	}
	if (step_gres_ptr->gres_bit_alloc[node_offset]) {
		error("gres/%s: %s %ps bit_alloc already exists",
		      job_gres_ptr->gres_name, __func__, step_id);
		bit_or(step_gres_ptr->gres_bit_alloc[node_offset],
		       gres_bit_alloc);
		FREE_NULL_BITMAP(gres_bit_alloc);
	} else {
		step_gres_ptr->gres_bit_alloc[node_offset] = gres_bit_alloc;
	}

	return SLURM_SUCCESS;
}

/*
 * Allocate resource to a step and update job and step gres information
 * IN step_gres_list - step's gres_list built by
 *		gres_plugin_step_state_validate()
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_offset - job's zero-origin index to the node of interest
 * IN first_step_node - true if this is the first node in the step's allocation
 * IN tasks_on_node - number of tasks to be launched on this node
 * IN rem_nodes - desired additional node count to allocate, including this node
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_step_alloc(List step_gres_list, List job_gres_list,
				  int node_offset, bool first_step_node,
				  uint16_t tasks_on_node, uint32_t rem_nodes,
				  uint32_t job_id, uint32_t step_id)
{
	int rc, rc2;
	ListIterator step_gres_iter;
	gres_state_t *step_gres_ptr, *job_gres_ptr;
	slurm_step_id_t tmp_step_id;

	if (step_gres_list == NULL)
		return SLURM_SUCCESS;
	if (job_gres_list == NULL) {
		error("%s: step allocates GRES, but job %u has none",
		      __func__, job_id);
		return SLURM_ERROR;
	}

	rc = gres_plugin_init();

	tmp_step_id.job_id = job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = step_id;

	slurm_mutex_lock(&gres_context_lock);
	step_gres_iter = list_iterator_create(step_gres_list);
	while ((step_gres_ptr = (gres_state_t *) list_next(step_gres_iter))) {
		gres_step_state_t *step_data_ptr =
			(gres_step_state_t *) step_gres_ptr->gres_data;
		gres_key_t job_search_key;
		step_data_ptr = (gres_step_state_t *)step_gres_ptr->gres_data;
		job_search_key.plugin_id = step_gres_ptr->plugin_id;
		if (step_data_ptr->type_name)
			job_search_key.type_id = step_data_ptr->type_id;
		else
			job_search_key.type_id = NO_VAL;

		job_search_key.node_offset = node_offset;
		if (!(job_gres_ptr = list_find_first(
			      job_gres_list,
			      _gres_find_job_by_key_with_cnt,
			      &job_search_key))) {
			/* job lack resources required by the step */
			rc = ESLURM_INVALID_GRES;
			break;
		}

		rc2 = _step_alloc(step_data_ptr,
				  job_gres_ptr->gres_data,
				  step_gres_ptr->plugin_id, node_offset,
				  first_step_node, &tmp_step_id, tasks_on_node,
				  rem_nodes);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}


static int _step_dealloc(gres_state_t *step_gres_ptr, List job_gres_list,
			 slurm_step_id_t *step_id)
{
	gres_state_t *job_gres_ptr;
	gres_step_state_t *step_data_ptr =
		(gres_step_state_t *)step_gres_ptr->gres_data;
	gres_job_state_t *job_data_ptr;
	uint32_t i, j;
	uint64_t gres_cnt;
	int len_j, len_s;
	gres_key_t job_search_key;

	xassert(job_gres_list);
	xassert(step_data_ptr);

	job_search_key.plugin_id = step_gres_ptr->plugin_id;
	if (step_data_ptr->type_name)
		job_search_key.type_id = step_data_ptr->type_id;
	else
		job_search_key.type_id = NO_VAL;
	for (i = 0; i < step_data_ptr->node_cnt; i++) {
		job_search_key.node_offset = i;
		if (!(job_gres_ptr = list_find_first(
			      job_gres_list,
			      _gres_find_job_by_key_with_cnt,
			      &job_search_key)))
			continue;

		job_data_ptr = (gres_job_state_t *)job_gres_ptr->gres_data;
		if (job_data_ptr->node_cnt == 0) {	/* no_consume */
			xassert(!step_data_ptr->node_in_use);
			xassert(!step_data_ptr->gres_bit_alloc);
			return SLURM_SUCCESS;
		} else if (job_data_ptr->node_cnt < i)
			return SLURM_SUCCESS;

		if (!step_data_ptr->node_in_use) {
			error("gres/%s: %s %ps dealloc, node_in_use is NULL",
			      job_data_ptr->gres_name, __func__, step_id);
			return SLURM_ERROR;
		}

		if (!bit_test(step_data_ptr->node_in_use, i))
			continue;

		if (step_data_ptr->gres_cnt_node_alloc)
			gres_cnt = step_data_ptr->gres_cnt_node_alloc[i];
		else
			gres_cnt = step_data_ptr->gres_per_node;

		if (job_data_ptr->gres_cnt_step_alloc) {
			if (job_data_ptr->gres_cnt_step_alloc[i] >=
			    gres_cnt) {
				job_data_ptr->gres_cnt_step_alloc[i] -=
					gres_cnt;
			} else {
				error("gres/%s: %s %ps dealloc count underflow",
				      job_data_ptr->gres_name, __func__,
				      step_id);
				job_data_ptr->gres_cnt_step_alloc[i] = 0;
			}
		}
		if ((step_data_ptr->gres_bit_alloc == NULL) ||
		    (step_data_ptr->gres_bit_alloc[i] == NULL))
			continue;
		if (job_data_ptr->gres_bit_alloc[i] == NULL) {
			error("gres/%s: %s job %u gres_bit_alloc[%d] is NULL",
			      job_data_ptr->gres_name, __func__,
			      step_id->job_id, i);
			continue;
		}
		len_j = bit_size(job_data_ptr->gres_bit_alloc[i]);
		len_s = bit_size(step_data_ptr->gres_bit_alloc[i]);
		if (len_j != len_s) {
			error("gres/%s: %s %ps dealloc, bit_alloc[%d] size mis-match (%d != %d)",
			      job_data_ptr->gres_name, __func__,
			      step_id, i, len_j, len_s);
			len_j = MIN(len_j, len_s);
		}
		for (j = 0; j < len_j; j++) {
			if (!bit_test(step_data_ptr->gres_bit_alloc[i], j))
				continue;
			if (job_data_ptr->gres_bit_step_alloc &&
			    job_data_ptr->gres_bit_step_alloc[i]) {
				bit_clear(job_data_ptr->gres_bit_step_alloc[i],
					  j);
			}
		}
		FREE_NULL_BITMAP(step_data_ptr->gres_bit_alloc[i]);
	}

	return SLURM_SUCCESS;
}

/*
 * Deallocate resource to a step and update job and step gres information
 * IN step_gres_list - step's gres_list built by
 *		gres_plugin_step_state_validate()
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_step_dealloc(List step_gres_list, List job_gres_list,
				    uint32_t job_id, uint32_t step_id)
{
	int rc, rc2;
	ListIterator step_gres_iter;
	gres_state_t *step_gres_ptr;
	slurm_step_id_t tmp_step_id;

	if (step_gres_list == NULL)
		return SLURM_SUCCESS;
	if (job_gres_list == NULL) {
		error("%s: step deallocates gres, but job %u has none",
		      __func__, job_id);
		return SLURM_ERROR;
	}

	rc = gres_plugin_init();

	tmp_step_id.job_id = job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = step_id;

	slurm_mutex_lock(&gres_context_lock);
	step_gres_iter = list_iterator_create(step_gres_list);
	while ((step_gres_ptr = list_next(step_gres_iter))) {
		rc2 = _step_dealloc(step_gres_ptr,
				    job_gres_list,
				    &tmp_step_id);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Determine total count GRES of a given type are allocated to a job across
 * all nodes
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN gres_name - name of a GRES type
 * RET count of this GRES allocated to this job
 */
extern uint64_t gres_get_value_by_type(List job_gres_list, char *gres_name)
{
	int i;
	uint32_t plugin_id;
	uint64_t gres_cnt = 0;
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_gres_data;

	if (job_gres_list == NULL)
		return NO_VAL64;

	gres_cnt = NO_VAL64;
	(void) gres_plugin_init();
	plugin_id = gres_plugin_build_id(gres_name);

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id != plugin_id)
				continue;
			job_gres_data = (gres_job_state_t *)
					job_gres_ptr->gres_data;
			gres_cnt = job_gres_data->gres_per_node;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return gres_cnt;
}

/*
 * Fill in an array of GRES type ids contained within the given node gres_list
 *		and an array of corresponding counts of those GRES types.
 * IN gres_list - a List of GRES types found on a node.
 * IN arrlen - Length of the arrays (the number of elements in the gres_list).
 * IN gres_count_ids, gres_count_vals - the GRES type ID's and values found
 *	 	in the gres_list.
 * IN val_type - Type of value desired, see GRES_VAL_TYPE_*
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_node_count(List gres_list, int arr_len,
				  uint32_t *gres_count_ids,
				  uint64_t *gres_count_vals,
				  int val_type)
{
	ListIterator  node_gres_iter;
	gres_state_t* node_gres_ptr;
	void*         node_gres_data;
	uint64_t      val;
	int           rc, ix = 0;

	rc = gres_plugin_init();
	if ((rc == SLURM_SUCCESS) && (arr_len <= 0))
		rc = EINVAL;
	if (rc != SLURM_SUCCESS)
		return rc;

	slurm_mutex_lock(&gres_context_lock);

	node_gres_iter = list_iterator_create(gres_list);
	while ((node_gres_ptr = (gres_state_t*) list_next(node_gres_iter))) {
		gres_node_state_t *node_gres_state_ptr;
		val = 0;
		node_gres_data = node_gres_ptr->gres_data;
		node_gres_state_ptr = (gres_node_state_t *) node_gres_data;
		xassert(node_gres_state_ptr);

		switch (val_type) {
		case (GRES_VAL_TYPE_FOUND):
			val = node_gres_state_ptr->gres_cnt_found;
			break;
		case (GRES_VAL_TYPE_CONFIG):
			val = node_gres_state_ptr->gres_cnt_config;
			break;
		case (GRES_VAL_TYPE_AVAIL):
			val = node_gres_state_ptr->gres_cnt_avail;
			break;
		case (GRES_VAL_TYPE_ALLOC):
			val = node_gres_state_ptr->gres_cnt_alloc;
		}

		gres_count_ids[ix]  = node_gres_ptr->plugin_id;
		gres_count_vals[ix] = val;
		if (++ix >= arr_len)
			break;
	}
	list_iterator_destroy(node_gres_iter);

	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void gres_plugin_send_stepd(int fd, slurm_msg_t *msg)
{
	int len;

	/* Setup the gres_device list and other plugin-specific data */
	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	xassert(gres_context_buf);

	len = get_buf_offset(gres_context_buf);
	safe_write(fd, &len, sizeof(len));
	safe_write(fd, get_buf_data(gres_context_buf), len);

	slurm_mutex_unlock(&gres_context_lock);

	if (msg->msg_type != REQUEST_BATCH_JOB_LAUNCH) {
		launch_tasks_request_msg_t *job =
			(launch_tasks_request_msg_t *)msg->data;
		/* Send the merged slurm.conf/gres.conf and autodetect data */
		if (job->accel_bind_type || job->tres_bind || job->tres_freq) {
			len = get_buf_offset(gres_conf_buf);
			safe_write(fd, &len, sizeof(len));
			safe_write(fd, get_buf_data(gres_conf_buf), len);
		}
	}

	return;
rwfail:
	error("%s: failed", __func__);
	slurm_mutex_unlock(&gres_context_lock);

	return;
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void gres_plugin_recv_stepd(int fd, slurm_msg_t *msg)
{
	int len, rc;
	Buf buffer = NULL;

	slurm_mutex_lock(&gres_context_lock);

	safe_read(fd, &len, sizeof(int));

	buffer = init_buf(len);
	safe_read(fd, buffer->head, len);

	rc = _unpack_context_buf(buffer);

	if (rc == SLURM_ERROR)
		goto rwfail;

	FREE_NULL_BUFFER(buffer);
	if (msg->msg_type != REQUEST_BATCH_JOB_LAUNCH) {
		launch_tasks_request_msg_t *job =
			(launch_tasks_request_msg_t *)msg->data;
		/* Recv the merged slurm.conf/gres.conf and autodetect data */
		if (job->accel_bind_type || job->tres_bind || job->tres_freq) {
			safe_read(fd, &len, sizeof(int));

			buffer = init_buf(len);
			safe_read(fd, buffer->head, len);

			rc = _unpack_gres_conf(buffer);

			if (rc == SLURM_ERROR)
				goto rwfail;

			FREE_NULL_BUFFER(buffer);
		}
	}

	slurm_mutex_unlock(&gres_context_lock);

	/* Set debug flags and init_run only */
	(void) gres_plugin_init();

	return;
rwfail:
	FREE_NULL_BUFFER(buffer);
	error("%s: failed", __func__);
	slurm_mutex_unlock(&gres_context_lock);

	/* Set debug flags and init_run only */
	(void) gres_plugin_init();

	return;
}

/* Get generic GRES data types here. Call the plugin for others */
static int _get_job_info(int gres_inx, gres_job_state_t *job_gres_data,
			 uint32_t node_inx, enum gres_job_data_type data_type,
			 void *data)
{
	uint64_t *u64_data = (uint64_t *) data;
	bitstr_t **bit_data = (bitstr_t **) data;
	int rc = SLURM_SUCCESS;

	if (!job_gres_data || !data)
		return EINVAL;
	if (node_inx >= job_gres_data->node_cnt)
		return ESLURM_INVALID_NODE_COUNT;
	if (data_type == GRES_JOB_DATA_COUNT) {
		*u64_data = job_gres_data->gres_per_node;
	} else if (data_type == GRES_JOB_DATA_BITMAP) {
		if (job_gres_data->gres_bit_alloc)
			*bit_data = job_gres_data->gres_bit_alloc[node_inx];
		else
			*bit_data = NULL;
	} else {
		/* Support here for plugin-specific data types */
		rc = (*(gres_context[gres_inx].ops.job_info))
			(job_gres_data, node_inx, data_type, data);
	}

	return rc;
}

/*
 * get data from a job's GRES data structure
 * IN job_gres_list  - job's GRES data structure
 * IN gres_name - name of a GRES type
 * IN node_inx - zero-origin index of the node within the job's allocation
 *	for which data is desired
 * IN data_type - type of data to get from the job's data
 * OUT data - pointer to the data from job's GRES data structure
 *            DO NOT FREE: This is a pointer into the job's data structure
 * RET - SLURM_SUCCESS or error code
 */
extern int gres_get_job_info(List job_gres_list, char *gres_name,
			     uint32_t node_inx,
			     enum gres_job_data_type data_type, void *data)
{
	int i, rc = ESLURM_INVALID_GRES;
	uint32_t plugin_id;
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_gres_data;

	if (data == NULL)
		return EINVAL;
	if (job_gres_list == NULL)	/* No GRES allocated */
		return ESLURM_INVALID_GRES;

	(void) gres_plugin_init();
	plugin_id = gres_plugin_build_id(gres_name);

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id != plugin_id)
				continue;
			job_gres_data = (gres_job_state_t *)
					job_gres_ptr->gres_data;
			rc = _get_job_info(i, job_gres_data, node_inx,
					   data_type, data);
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/* Given a job's GRES data structure, return the indecies for selected elements
 * IN job_gres_list  - job's GRES data structure
 * OUT gres_detail_cnt - Number of elements (nodes) in gres_detail_str
 * OUT gres_detail_str - Description of GRES on each node
 * OUT total_gres_str - String containing all gres in the job and counts.
 */
extern void gres_build_job_details(List job_gres_list,
				   uint32_t *gres_detail_cnt,
				   char ***gres_detail_str,
				   char **total_gres_str)
{
	int i, j;
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_gres_data;
	char *sep1, *sep2, tmp_str[128], *type, **my_gres_details = NULL;
	uint32_t my_gres_cnt = 0;
	char *gres_name, *gres_str = NULL;
	uint64_t gres_cnt;

	/* Release any vestigial data (e.g. from job requeue) */
	for (i = 0; i < *gres_detail_cnt; i++)
		xfree(gres_detail_str[0][i]);
	xfree(*gres_detail_str);
	xfree(*total_gres_str);
	*gres_detail_cnt = 0;

	if (job_gres_list == NULL)	/* No GRES allocated */
		return;

	(void) gres_plugin_init();

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		job_gres_data = (gres_job_state_t *) job_gres_ptr->gres_data;
		if (job_gres_data->gres_bit_alloc == NULL)
			continue;
		if (my_gres_details == NULL) {
			my_gres_cnt = job_gres_data->node_cnt;
			my_gres_details = xcalloc(my_gres_cnt, sizeof(char *));
		}

		if (job_gres_data->type_name) {
			sep2 = ":";
			type = job_gres_data->type_name;
		} else {
			sep2 = "";
			type = "";
		}

		gres_name = xstrdup_printf(
			"%s%s%s",
			job_gres_data->gres_name, sep2, type);
		gres_cnt = 0;

		for (j = 0; j < my_gres_cnt; j++) {
			if (j >= job_gres_data->node_cnt)
				break;	/* node count mismatch */
			if (my_gres_details[j])
				sep1 = ",";
			else
				sep1 = "";

			gres_cnt += job_gres_data->gres_cnt_node_alloc[j];

			if (job_gres_data->gres_bit_alloc[j]) {
				bit_fmt(tmp_str, sizeof(tmp_str),
					job_gres_data->gres_bit_alloc[j]);
				xstrfmtcat(my_gres_details[j],
					   "%s%s:%"PRIu64"(IDX:%s)",
					   sep1, gres_name,
					   job_gres_data->
					   gres_cnt_node_alloc[j],
					   tmp_str);
			} else if (job_gres_data->gres_cnt_node_alloc[j]) {
				xstrfmtcat(my_gres_details[j],
					   "%s%s(CNT:%"PRIu64")",
					   sep1, gres_name,
					   job_gres_data->
					   gres_cnt_node_alloc[j]);
			}
		}

		xstrfmtcat(gres_str, "%s%s:%"PRIu64,
			   gres_str ? "," : "", gres_name, gres_cnt);
		xfree(gres_name);
	}
	list_iterator_destroy(job_gres_iter);
	*gres_detail_cnt = my_gres_cnt;
	*gres_detail_str = my_gres_details;
	*total_gres_str = gres_str;
}

/* Get generic GRES data types here. Call the plugin for others */
static int _get_step_info(int gres_inx, gres_step_state_t *step_gres_data,
			  uint32_t node_inx, enum gres_step_data_type data_type,
			  void *data)
{
	uint64_t *u64_data = (uint64_t *) data;
	bitstr_t **bit_data = (bitstr_t **) data;
	int rc = SLURM_SUCCESS;

	if (!step_gres_data || !data)
		return EINVAL;
	if (node_inx >= step_gres_data->node_cnt)
		return ESLURM_INVALID_NODE_COUNT;
	if (data_type == GRES_STEP_DATA_COUNT) {
		*u64_data = step_gres_data->gres_per_node;
	} else if (data_type == GRES_STEP_DATA_BITMAP) {
		if (step_gres_data->gres_bit_alloc)
			*bit_data = step_gres_data->gres_bit_alloc[node_inx];
		else
			*bit_data = NULL;
	} else {
		/* Support here for plugin-specific data types */
		rc = (*(gres_context[gres_inx].ops.step_info))
			(step_gres_data, node_inx, data_type, data);
	}

	return rc;
}

/*
 * get data from a step's GRES data structure
 * IN step_gres_list  - step's GRES data structure
 * IN gres_name - name of a GRES type
 * IN node_inx - zero-origin index of the node within the job's allocation
 *	for which data is desired. Note this can differ from the step's
 *	node allocation index.
 * IN data_type - type of data to get from the step's data
 * OUT data - pointer to the data from step's GRES data structure
 *            DO NOT FREE: This is a pointer into the step's data structure
 * RET - SLURM_SUCCESS or error code
 */
extern int gres_get_step_info(List step_gres_list, char *gres_name,
			      uint32_t node_inx,
			      enum gres_step_data_type data_type, void *data)
{
	int i, rc = ESLURM_INVALID_GRES;
	uint32_t plugin_id;
	ListIterator step_gres_iter;
	gres_state_t *step_gres_ptr;
	gres_step_state_t *step_gres_data;

	if (data == NULL)
		return EINVAL;
	if (step_gres_list == NULL)	/* No GRES allocated */
		return ESLURM_INVALID_GRES;

	(void) gres_plugin_init();
	plugin_id = gres_plugin_build_id(gres_name);

	slurm_mutex_lock(&gres_context_lock);
	step_gres_iter = list_iterator_create(step_gres_list);
	while ((step_gres_ptr = (gres_state_t *) list_next(step_gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (step_gres_ptr->plugin_id != plugin_id)
				continue;
			step_gres_data = (gres_step_state_t *)
					 step_gres_ptr->gres_data;
			rc = _get_step_info(i, step_gres_data, node_inx,
					    data_type, data);
			break;
		}
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

extern gres_step_state_t *gres_get_step_state(List gres_list, char *name)
{
	gres_state_t *gres_state_ptr;

	if (!gres_list || !name || !list_count(gres_list))
		return NULL;

	slurm_mutex_lock(&gres_context_lock);
	gres_state_ptr = list_find_first(gres_list, _gres_step_find_name, name);
	slurm_mutex_unlock(&gres_context_lock);

	if (!gres_state_ptr)
		return NULL;

	return (gres_step_state_t *)gres_state_ptr->gres_data;
}

extern gres_job_state_t *gres_get_job_state(List gres_list, char *name)
{
	gres_state_t *gres_state_ptr;

	if (!gres_list || !name || !list_count(gres_list))
		return NULL;

	slurm_mutex_lock(&gres_context_lock);
	gres_state_ptr = list_find_first(gres_list, _gres_job_find_name, name);
	slurm_mutex_unlock(&gres_context_lock);

	if (!gres_state_ptr)
		return NULL;

	return (gres_job_state_t *)gres_state_ptr->gres_data;
}

extern uint32_t gres_get_autodetect_flags(void)
{
	return autodetect_flags;
}

static void _gres_2_tres_str_internal(char **tres_str,
				      char *gres_name, char *gres_type,
				      uint64_t count, bool find_other_types)
{
	slurmdb_tres_rec_t *tres_rec;
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_req;

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_req, 0, sizeof(slurmdb_tres_rec_t));
		tres_req.type = "gres";
	}

	xassert(verify_assoc_lock(TRES_LOCK, READ_LOCK));
	xassert(gres_name);
	xassert(tres_str);

	tres_req.name = gres_name;
	tres_rec = assoc_mgr_find_tres_rec(&tres_req);

	if (tres_rec &&
	    slurmdb_find_tres_count_in_string(
		    *tres_str, tres_rec->id) == INFINITE64)
		/* New gres */
		xstrfmtcat(*tres_str, "%s%u=%"PRIu64,
			   *tres_str ? "," : "",
			   tres_rec->id, count);

	if (!find_other_types)
		return;

	if (gres_type) {
		/*
		 * Now let's put of the : name TRES if we are
		 * tracking it as well.  This would be handy
		 * for GRES like "gpu:tesla", where you might
		 * want to track both as TRES.
		 */
		tres_req.name = xstrdup_printf("%s:%s", gres_name, gres_type);
		tres_rec = assoc_mgr_find_tres_rec(&tres_req);
		xfree(tres_req.name);
	} else {
		/*
		 * Job allocated GRES without "type"
		 * specification, but Slurm is only accounting
		 * for this GRES by specific "type", so pick
		 * some valid "type" to get some accounting.
		 * Although the reported "type" may not be
		 * accurate, it is better than nothing...
		 */
		tres_rec = assoc_mgr_find_tres_rec2(&tres_req);
	}

	if (tres_rec &&
	    slurmdb_find_tres_count_in_string(
		    *tres_str, tres_rec->id) == INFINITE64)
		/* New GRES */
		xstrfmtcat(*tres_str, "%s%u=%"PRIu64,
			   *tres_str ? "," : "",
			   tres_rec->id, count);
}

extern char *gres_2_tres_str(List gres_list, bool is_job, bool locked)
{
	ListIterator itr;
	gres_state_t *gres_state_ptr;
	int i;
	uint64_t count;
	char *col_name = NULL;
	char *tres_str = NULL;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (!gres_list)
		return NULL;

	/* must be locked first before gres_contrex_lock!!! */
	if (!locked)
		assoc_mgr_lock(&locks);

	slurm_mutex_lock(&gres_context_lock);
	itr = list_iterator_create(gres_list);
	while ((gres_state_ptr = list_next(itr))) {
		char *gres_name = NULL;
		if (is_job) {
			gres_job_state_t *gres_data_ptr = (gres_job_state_t *)
				gres_state_ptr->gres_data;
			col_name = gres_data_ptr->type_name;
			count = gres_data_ptr->total_gres;
		} else {
			gres_step_state_t *gres_data_ptr = (gres_step_state_t *)
				gres_state_ptr->gres_data;
			col_name = gres_data_ptr->type_name;
			count = gres_data_ptr->total_gres;
		}

		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_context[i].plugin_id ==
			    gres_state_ptr->plugin_id) {
				gres_name = gres_context[i].gres_name;
				break;
			}
		}

		if (!gres_name) {
			debug("%s: couldn't find name", __func__);
			continue;
		}

		/* If we are no_consume, print a 0 */
		if (count == NO_CONSUME_VAL64)
			count = 0;

		_gres_2_tres_str_internal(&tres_str, gres_name, col_name, count,
					  (i < gres_context_cnt));
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&gres_context_lock);

	if (!locked)
		assoc_mgr_unlock(&locks);

	return tres_str;
}


/* Given a job's GRES data structure, return a simple tres string of gres
 * allocated on the node_inx requested
 * IN job_gres_list  - job's GRES data structure
 * IN node_inx - position of node in job_state_ptr->gres_cnt_node_alloc
 * IN locked - if the assoc_mgr tres read locked is locked or not
 *
 * RET - simple string containing gres this job is allocated on the node
 * requested.
 */
extern char *gres_job_gres_on_node_as_tres(List job_gres_list,
					   int node_inx,
					   bool locked)
{
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	char *tres_str = NULL;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	(void) gres_plugin_init();

	if (!job_gres_list)	/* No GRES allocated */
		return NULL;

	/* must be locked first before gres_contrex_lock!!! */
	if (!locked)
		assoc_mgr_lock(&locks);

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = list_next(job_gres_iter))) {
		uint64_t count;
		gres_job_state_t *job_state_ptr =
			(gres_job_state_t *)job_gres_ptr->gres_data;
		if (!job_state_ptr->gres_bit_alloc)
			continue;

		if (node_inx > job_state_ptr->node_cnt)
			break;

		if (!job_state_ptr->gres_name) {
			debug("%s: couldn't find name", __func__);
			continue;
		}

		/* If we are no_consume, print a 0 */
		if (job_state_ptr->total_gres == NO_CONSUME_VAL64)
			count = 0;
		else if (job_state_ptr->gres_cnt_node_alloc[node_inx])
			count = job_state_ptr->gres_cnt_node_alloc[node_inx];
		else /* If this gres isn't on the node skip it */
			continue;
		_gres_2_tres_str_internal(&tres_str,
					  job_state_ptr->gres_name,
					  job_state_ptr->type_name,
					  count, true);
	}
	list_iterator_destroy(job_gres_iter);

	slurm_mutex_unlock(&gres_context_lock);

	if (!locked)
		assoc_mgr_unlock(&locks);

	return tres_str;
}

/* Fill in job/node TRES arrays with allocated GRES. */
static void _set_type_tres_cnt(gres_state_type_enum_t state_type,
			       List gres_list,
			       uint32_t node_cnt,
			       uint64_t *tres_cnt,
			       bool locked)
{
	ListIterator itr;
	gres_state_t *gres_state_ptr;
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_rec;
	char *col_name = NULL;
	uint64_t count;
	int i, tres_pos;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
		tres_rec.type = "gres";
	}

	if (!gres_list || !tres_cnt ||
	    ((state_type == GRES_STATE_TYPE_JOB) &&
	     (!node_cnt || (node_cnt == NO_VAL))))
		return;

	/* must be locked first before gres_contrex_lock!!! */
	if (!locked)
		assoc_mgr_lock(&locks);

	slurm_mutex_lock(&gres_context_lock);
	/* Initialize all GRES counters to zero. Increment them later. */
	for (i = 0; i < gres_context_cnt; i++) {
		tres_rec.name =	gres_context[i].gres_name;
		if (tres_rec.name &&
		    ((tres_pos = assoc_mgr_find_tres_pos(&tres_rec,true)) !=-1))
			tres_cnt[tres_pos] = 0;
	}

	itr = list_iterator_create(gres_list);
	while ((gres_state_ptr = list_next(itr))) {
		bool set_total = false;
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_context[i].plugin_id ==
			    gres_state_ptr->plugin_id) {
				tres_rec.name =	gres_context[i].gres_name;
				break;
			}
		}
		if (!tres_rec.name) {
			debug("%s: couldn't find name", __func__);
			continue;
		}

		/* Get alloc count for main GRES. */
		switch (state_type) {
		case GRES_STATE_TYPE_JOB:
		{
			gres_job_state_t *gres_data_ptr = (gres_job_state_t *)
				gres_state_ptr->gres_data;
			count = gres_data_ptr->total_gres;
			break;
		}
		case GRES_STATE_TYPE_NODE:
		{
			gres_node_state_t *gres_data_ptr = (gres_node_state_t *)
				gres_state_ptr->gres_data;
			count = gres_data_ptr->gres_cnt_alloc;
			break;
		}
		default:
			error("%s: unsupported state type %d", __func__,
			      state_type);
			continue;
		}
		/*
		 * Set main TRES's count (i.e. if no GRES "type" is being
		 * accounted for). We need to increment counter since the job
		 * may have been allocated multiple GRES types, but Slurm is
		 * only configured to track the total count. For example, a job
		 * allocated 1 GPU of type "tesla" and 1 GPU of type "volta",
		 * but we want to record that the job was allocated a total of
		 * 2 GPUs.
		 */
		if ((tres_pos = assoc_mgr_find_tres_pos(&tres_rec,true)) != -1){
			if (count == NO_CONSUME_VAL64)
				tres_cnt[tres_pos] = NO_CONSUME_VAL64;
			else
				tres_cnt[tres_pos] += count;
			set_total = true;
		}

		/*
		 * Set TRES count for GRES model types. This would be handy for
		 * GRES like "gpu:tesla", where you might want to track both as
		 * TRES.
		 */
		switch (state_type) {
		case GRES_STATE_TYPE_JOB:
		{
			gres_job_state_t *gres_data_ptr = (gres_job_state_t *)
				gres_state_ptr->gres_data;

			col_name = gres_data_ptr->type_name;
			if (col_name) {
				tres_rec.name = xstrdup_printf(
					"%s%s",
					gres_context[i].gres_name_colon,
					col_name);
				if ((tres_pos = assoc_mgr_find_tres_pos(
					     &tres_rec, true)) != -1)
					tres_cnt[tres_pos] = count;
				xfree(tres_rec.name);
			} else if (!set_total) {
				/*
				 * Job allocated GRES without "type"
				 * specification, but Slurm is only accounting
				 * for this GRES by specific "type", so pick
				 * some valid "type" to get some accounting.
				 * Although the reported "type" may not be
				 * accurate, it is better than nothing...
				 */
				tres_rec.name = xstrdup_printf(
					"%s", gres_context[i].gres_name);
				if ((tres_pos = assoc_mgr_find_tres_pos2(
					     &tres_rec, true)) != -1)
					tres_cnt[tres_pos] = count;
				xfree(tres_rec.name);
			}
			break;
		}
		case GRES_STATE_TYPE_NODE:
		{
			int type;
			gres_node_state_t *gres_data_ptr = (gres_node_state_t *)
				gres_state_ptr->gres_data;

			for (type = 0; type < gres_data_ptr->type_cnt; type++) {
				col_name = gres_data_ptr->type_name[type];
				if (!col_name)
					continue;

				tres_rec.name = xstrdup_printf(
						"%s%s",
						gres_context[i].gres_name_colon,
						col_name);

				count = gres_data_ptr->type_cnt_alloc[type];

				if ((tres_pos = assoc_mgr_find_tres_pos(
							&tres_rec, true)) != -1)
					tres_cnt[tres_pos] = count;
				xfree(tres_rec.name);
			}
			break;
		}
		default:
			error("%s: unsupported state type %d", __func__,
			      state_type);
			continue;
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&gres_context_lock);

	if (!locked)
		assoc_mgr_unlock(&locks);

	return;
}

extern void gres_set_job_tres_cnt(List gres_list,
				  uint32_t node_cnt,
				  uint64_t *tres_cnt,
				  bool locked)
{
	_set_type_tres_cnt(GRES_STATE_TYPE_JOB,
			   gres_list, node_cnt, tres_cnt, locked);
}

extern void gres_set_node_tres_cnt(List gres_list,
				   uint64_t *tres_cnt,
				   bool locked)
{
	_set_type_tres_cnt(GRES_STATE_TYPE_NODE,
			   gres_list, 0, tres_cnt, locked);
}

extern char *gres_device_major(char *dev_path)
{
	int loc_major, loc_minor;
	char *ret_major = NULL;
	struct stat fs;

	if (stat(dev_path, &fs) < 0) {
		error("%s: stat(%s): %m", __func__, dev_path);
		return NULL;
	}
	loc_major = (int)major(fs.st_rdev);
	loc_minor = (int)minor(fs.st_rdev);
	debug3("%s : %s major %d, minor %d",
	       __func__, dev_path, loc_major, loc_minor);
	if (S_ISBLK(fs.st_mode)) {
		xstrfmtcat(ret_major, "b %d:", loc_major);
		//info("device is block ");
	}
	if (S_ISCHR(fs.st_mode)) {
		xstrfmtcat(ret_major, "c %d:", loc_major);
		//info("device is character ");
	}
	xstrfmtcat(ret_major, "%d rwm", loc_minor);

	return ret_major;
}

/* Free memory for gres_device_t record */
extern void destroy_gres_device(void *gres_device_ptr)
{
	gres_device_t *gres_device = (gres_device_t *) gres_device_ptr;

	if (!gres_device)
		return;
	xfree(gres_device->path);
	xfree(gres_device->major);
	xfree(gres_device);
}

/* Destroy a gres_slurmd_conf_t record, free it's memory */
extern void destroy_gres_slurmd_conf(void *x)
{
	gres_slurmd_conf_t *p = (gres_slurmd_conf_t *) x;

	xassert(p);
	xfree(p->cpus);
	FREE_NULL_BITMAP(p->cpus_bitmap);
	xfree(p->file);		/* Only used by slurmd */
	xfree(p->links);
	xfree(p->name);
	xfree(p->type_name);
	xfree(p);
}


/*
 * Convert GRES config_flags to a string. The pointer returned references local
 * storage in this function, which is not re-entrant.
 */
extern char *gres_flags2str(uint8_t config_flags)
{
	static char flag_str[128];
	char *sep = "";

	flag_str[0] = '\0';
	if (config_flags & GRES_CONF_COUNT_ONLY) {
		strcat(flag_str, sep);
		strcat(flag_str, "CountOnly");
		sep = ",";
	}

	if (config_flags & GRES_CONF_HAS_FILE) {
		strcat(flag_str, sep);
		strcat(flag_str, "HAS_FILE");
		sep = ",";
	}

	if (config_flags & GRES_CONF_LOADED) {
		strcat(flag_str, sep);
		strcat(flag_str, "LOADED");
		sep = ",";
	}

	if (config_flags & GRES_CONF_HAS_TYPE) {
		strcat(flag_str, sep);
		strcat(flag_str, "HAS_TYPE");
		sep = ",";
	}

	return flag_str;
}

/*
 * Creates a gres_slurmd_conf_t record to add to a list of gres_slurmd_conf_t
 * records
 */
extern void add_gres_to_list(List gres_list, char *name, uint64_t device_cnt,
			     int cpu_cnt, char *cpu_aff_abs_range,
			     bitstr_t *cpu_aff_mac_bitstr, char *device_file,
			     char *type, char *links)
{
	gres_slurmd_conf_t *gpu_record;
	bool use_empty_first_record = false;
	ListIterator itr = list_iterator_create(gres_list);

	/*
	 * If the first record already exists and has a count of 0 then
	 * overwrite it.
	 * This is a placeholder record created in _merge_config()
	 */
	gpu_record = list_next(itr);
	if (gpu_record && (gpu_record->count == 0))
		use_empty_first_record = true;
	else
		gpu_record = xmalloc(sizeof(gres_slurmd_conf_t));
	gpu_record->cpu_cnt = cpu_cnt;
	if (cpu_aff_mac_bitstr)
		gpu_record->cpus_bitmap = bit_copy(cpu_aff_mac_bitstr);
	if (device_file)
		gpu_record->config_flags |= GRES_CONF_HAS_FILE;
	if (type)
		gpu_record->config_flags |= GRES_CONF_HAS_TYPE;
	gpu_record->cpus = xstrdup(cpu_aff_abs_range);
	gpu_record->type_name = xstrdup(type);
	gpu_record->name = xstrdup(name);
	gpu_record->file = xstrdup(device_file);
	gpu_record->links = xstrdup(links);
	gpu_record->count = device_cnt;
	gpu_record->plugin_id = gres_plugin_build_id(name);
	if (!use_empty_first_record)
		list_append(gres_list, gpu_record);
	list_iterator_destroy(itr);
}
