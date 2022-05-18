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
#include "src/common/cgroup.h"
#include "src/common/gres.h"
#include "src/common/job_resources.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_conf.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define MAX_GRES_BITMAP 1024

strong_alias(gres_find_id, slurm_gres_find_id);
strong_alias(gres_find_sock_by_job_state, slurm_gres_find_sock_by_job_state);
strong_alias(gres_get_node_used, slurm_gres_get_node_used);
strong_alias(gres_get_system_cnt, slurm_gres_get_system_cnt);
strong_alias(gres_get_value_by_type, slurm_gres_get_value_by_type);
strong_alias(gres_get_job_info, slurm_gres_get_job_info);
strong_alias(gres_get_step_info, slurm_gres_get_step_info);
strong_alias(gres_sock_delete, slurm_gres_sock_delete);
strong_alias(gres_job_list_delete, slurm_gres_job_list_delete);
strong_alias(destroy_gres_device, slurm_destroy_gres_device);
strong_alias(destroy_gres_slurmd_conf, slurm_destroy_gres_slurmd_conf);

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

/* Gres symbols provided by the plugin */
typedef struct slurm_gres_ops {
	int		(*node_config_load)	( List gres_conf_list,
						  node_config_load_t *node_conf);
	void		(*job_set_env)		( char ***job_env_ptr,
						  bitstr_t *gres_bit_alloc,
						  uint64_t gres_cnt,
						  gres_internal_flags_t flags);
	void		(*step_set_env)		( char ***step_env_ptr,
						  bitstr_t *gres_bit_alloc,
						  uint64_t gres_cnt,
						  gres_internal_flags_t flags);
	void		(*task_set_env)		( char ***step_env_ptr,
						  bitstr_t *gres_bit_alloc,
						  uint64_t gres_cnt,
						  bitstr_t *usable_gres,
						  gres_internal_flags_t flags);
	void		(*send_stepd)		( buf_t *buffer );
	void		(*recv_stepd)		( buf_t *buffer );
	int		(*job_info)		( gres_job_state_t *gres_js,
						  uint32_t node_inx,
						  enum gres_job_data_type data_type,
						  void *data);
	int		(*step_info)		( gres_step_state_t *gres_ss,
						  uint32_t node_inx,
						  enum gres_step_data_type data_type,
						  void *data);
	List            (*get_devices)		( void );
	void            (*step_hardware_init)	( bitstr_t *, char * );
	void            (*step_hardware_fini)	( void );
	gres_epilog_info_t *(*epilog_build_env)(gres_job_state_t *gres_js);
	void            (*epilog_set_env)	( char ***epilog_env_ptr,
						  gres_epilog_info_t *gres_ei,
						  int node_inx );
} slurm_gres_ops_t;

/*
 * Gres plugin context, one for each gres type.
 * Add to gres_context through _add_gres_context().
 */
typedef struct slurm_gres_context {
	plugin_handle_t	cur_plugin;
	uint32_t	config_flags;		/* See GRES_CONF_* in gres.h */
	char *		gres_name;		/* name (e.g. "gpu") */
	char *		gres_name_colon;	/* name + colon (e.g. "gpu:") */
	int		gres_name_colon_len;	/* size of gres_name_colon */
	char *		gres_type;		/* plugin name (e.g. "gres/gpu") */
	slurm_gres_ops_t ops;			/* pointers to plugin symbols */
	uint32_t	plugin_id;		/* key for searches */
	plugrack_t	*plugin_list;		/* plugrack info */
	uint64_t        total_cnt;		/* Total GRES across all nodes */
} slurm_gres_context_t;

typedef struct {
	uint32_t plugin_id;
	bool with_type;
	bool without_type;
	void *without_type_state; /* gres_[job|step]_state_t */
} overlap_check_t;

/* These are the options that are currently supported with --tres-bind */
typedef struct {
	bool bind_gpu; /* If we are binding to a gpu or not. */
	bool bind_nic; /* If we are binding to a nic or not. */
	uint32_t gpus_per_task; /* How many gpus per task requested. */
	gres_internal_flags_t gres_internal_flags;
	char *map_gpu; /* GPU map requested. */
	char *mask_gpu; /* GPU mask requested. */
	char *request;
	uint32_t tasks_per_gres; /* How many tasks per gres requested */
} tres_bind_t;

typedef struct {
	slurm_gres_context_t *gres_ctx;
	int new_has_file;
	int new_has_type;
	int rec_count;
} foreach_gres_conf_t;

typedef struct {
	uint64_t gres_cnt;
	bool ignore_alloc;
	gres_key_t *job_search_key;
	slurm_step_id_t *step_id;
} foreach_gres_cnt_t;

typedef struct {
	bitstr_t **gres_bit_alloc;
	bool is_job;
	uint32_t plugin_id;
} foreach_gres_accumulate_device_t;

/* Pointers to functions in src/slurmd/common/xcpuinfo.h that we may use */
typedef struct xcpuinfo_funcs {
	int (*xcpuinfo_abs_to_mac) (char *abs, char **mac);
} xcpuinfo_funcs_t;
xcpuinfo_funcs_t xcpuinfo_ops;

typedef struct {
	uint32_t flags;
	uint32_t name_hash;
	bool no_gpu_env;
} prev_env_flags_t;

/* Local variables */
static int gres_context_cnt = -1;
static uint32_t gres_cpu_cnt = 0;
static slurm_gres_context_t *gres_context = NULL;
static char *gres_node_name = NULL;
static char *local_plugins_str = NULL;
static pthread_mutex_t gres_context_lock = PTHREAD_MUTEX_INITIALIZER;
static List gres_conf_list = NULL;
static bool init_run = false;
static uint32_t gpu_plugin_id = NO_VAL;
static volatile uint32_t autodetect_flags = GRES_AUTODETECT_UNSET;
static uint32_t select_plugin_type = NO_VAL;
static buf_t *gres_context_buf = NULL;
static buf_t *gres_conf_buf = NULL;

/* Local functions */
static void _accumulate_job_gres_alloc(gres_job_state_t *gres_js,
				       int node_inx,
				       bitstr_t **gres_bit_alloc,
				       uint64_t *gres_cnt);
static void _accumulate_step_gres_alloc(gres_state_t *gres_state_step,
					bitstr_t **gres_bit_alloc,
					uint64_t *gres_cnt);
static void _add_gres_context(char *gres_name);
static gres_node_state_t *_build_gres_node_state(void);
static void	_build_node_gres_str(List *gres_list, char **gres_str,
				     int cores_per_sock, int sock_per_node);
static bitstr_t *_core_bitmap_rebuild(bitstr_t *old_core_bitmap, int new_size);
static void	_epilog_list_del(void *x);
static void	_get_gres_cnt(gres_node_state_t *gres_ns, char *orig_config,
			      char *gres_name, char *gres_name_colon,
			      int gres_name_colon_len);
static uint64_t _get_job_gres_list_cnt(List gres_list, char *gres_name,
				       char *gres_type);
static uint64_t	_get_tot_gres_cnt(uint32_t plugin_id, uint64_t *topo_cnt,
				  int *config_type_cnt);
static void	_job_state_delete(gres_job_state_t *gres_js);
static void *	_job_state_dup2(gres_job_state_t *gres_js, int node_index);
static void	_job_state_log(gres_state_t *gres_js, uint32_t job_id);
static int	_load_plugin(slurm_gres_context_t *gres_ctx);
static int	_log_gres_slurmd_conf(void *x, void *arg);
static void	_my_stat(char *file_name);
static int	_node_config_init(char *orig_config,
				  slurm_gres_context_t *gres_ctx,
				  gres_state_t *gres_state_node);
static char *	_node_gres_used(gres_node_state_t *gres_ns, char *gres_name);
static int	_node_reconfig(char *node_name, char *new_gres, char **gres_str,
			       gres_state_t *gres_state_node,
			       bool config_overrides,
			       slurm_gres_context_t *gres_ctx,
			       bool *updated_gpu_cnt);
static int	_node_reconfig_test(char *node_name, char *new_gres,
				    gres_state_t *gres_state_node,
				    slurm_gres_context_t *gres_ctx);
static void	_node_state_dealloc(gres_state_t *gres_state_node);
static void *	_node_state_dup(gres_node_state_t *gres_ns);
static void	_node_state_log(gres_node_state_t *gres_ns, char *node_name,
				char *gres_name);
static int	_parse_gres_config(void **dest, slurm_parser_enum_t type,
				   const char *key, const char *value,
				   const char *line, char **leftover);
static int _parse_gres_config_dummy(void **dest, slurm_parser_enum_t type,
				    const char *key, const char *value,
				    const char *line, char **leftover);
static int	_parse_gres_config_node(void **dest, slurm_parser_enum_t type,
					const char *key, const char *value,
					const char *line, char **leftover);
static void *	_step_state_dup(gres_step_state_t *gres_ss);
static void *	_step_state_dup2(gres_step_state_t *gres_ss, int node_index);
static void	_step_state_log(gres_step_state_t *gres_ss,
				slurm_step_id_t *step_id,
				char *gres_name);
static int	_unload_plugin(slurm_gres_context_t *gres_ctx);
static void	_validate_slurm_conf(List slurm_conf_list,
				     slurm_gres_context_t *gres_ctx);
static void	_validate_gres_conf(List gres_conf_list,
				    slurm_gres_context_t *gres_ctx);
static int	_validate_file(char *path_name, char *gres_name);
static int	_valid_gres_type(char *gres_name, gres_node_state_t *gres_ns,
				 bool config_overrides, char **reason_down);
static void _parse_tres_bind(uint16_t accel_bind_type, char *tres_bind_str,
			     tres_bind_t *tres_bind);
static int _get_usable_gres(char *gres_name, int context_inx, int proc_id,
			    pid_t pid, tres_bind_t *tres_bind,
			    bitstr_t **usable_gres_ptr,
			    bitstr_t *gres_bit_alloc,  bool get_devices);

extern uint32_t gres_build_id(char *name)
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

extern int gres_find_id(void *x, void *key)
{
	uint32_t *plugin_id = (uint32_t *)key;
	gres_state_t *state_ptr = (gres_state_t *) x;
	if (state_ptr->plugin_id == *plugin_id)
		return 1;
	return 0;
}

extern int gres_find_flags(void *x, void *key)
{
	gres_state_t *state_ptr = x;
	uint32_t flags = *(uint32_t *)key;
	if (state_ptr->config_flags & flags)
		return 1;
	return 0;
}

/* Find job record with matching name and type */
extern int gres_find_job_by_key_exact_type(void *x, void *key)
{
	gres_state_t *gres_state_job = (gres_state_t *) x;
	gres_key_t *job_key = (gres_key_t *) key;
	gres_job_state_t *gres_js;
	gres_js = (gres_job_state_t *)gres_state_job->gres_data;

	if ((gres_state_job->plugin_id == job_key->plugin_id) &&
	    (gres_js->type_id == job_key->type_id))
		return 1;
	return 0;
}

/* Find job record with matching name and type */
extern int gres_find_job_by_key(void *x, void *key)
{
	gres_state_t *gres_state_job = (gres_state_t *) x;
	gres_key_t *job_key = (gres_key_t *) key;
	gres_job_state_t *gres_js;
	gres_js = (gres_job_state_t *)gres_state_job->gres_data;

	if ((gres_state_job->plugin_id == job_key->plugin_id) &&
	    ((job_key->type_id == NO_VAL) ||
	     (gres_js->type_id == job_key->type_id)))
		return 1;
	return 0;
}

/* Find job record with matching name and type */
extern int gres_find_job_by_key_with_cnt(void *x, void *key)
{
	gres_state_t *gres_state_job = (gres_state_t *) x;
	gres_key_t *job_key = (gres_key_t *) key;
	gres_job_state_t *gres_js;
	gres_js = (gres_job_state_t *)gres_state_job->gres_data;

	if (!gres_find_job_by_key(x, key))
		return 0;

	/* This gres has been allocated on this node */
	if (!gres_js->node_cnt ||
	    ((job_key->node_offset < gres_js->node_cnt) &&
	     gres_js->gres_cnt_node_alloc[job_key->node_offset]))
		return 1;

	return 0;
}

extern int gres_find_step_by_key(void *x, void *key)
{
	gres_state_t *state_ptr = (gres_state_t *) x;
	gres_key_t *step_key = (gres_key_t *) key;
	gres_step_state_t *gres_ss = (gres_step_state_t *)state_ptr->gres_data;

	if ((state_ptr->plugin_id == step_key->plugin_id) &&
	    (gres_ss->type_id == step_key->type_id))
		return 1;
	return 0;
}


extern bool gres_use_local_device_index(void)
{
	bool use_cgroup = false;
	static bool use_local_index = false;
	static bool is_set = false;

	if (is_set)
		return use_local_index;
	is_set = true;

	if (!slurm_conf.task_plugin)
		return use_local_index;

	if (xstrstr(slurm_conf.task_plugin, "cgroup"))
		use_cgroup = true;
	if (!use_cgroup)
		return use_local_index;

	cgroup_conf_init();
	if (slurm_cgroup_conf.constrain_devices)
		use_local_index = true;

	return use_local_index;
}

extern gres_state_t *gres_create_state(void *src_ptr,
				       gres_state_src_t state_src,
				       gres_state_type_enum_t state_type,
				       void *gres_data)
{
	gres_state_t *new_gres_state = xmalloc(sizeof(gres_state_t));

	new_gres_state->gres_data = gres_data;
	new_gres_state->state_type = state_type;

	switch (state_src) {
	case GRES_STATE_SRC_STATE_PTR:
	{
		gres_state_t *gres_state = src_ptr;
		new_gres_state->config_flags = gres_state->config_flags;
		new_gres_state->plugin_id = gres_state->plugin_id;
		new_gres_state->gres_name = xstrdup(gres_state->gres_name);
		break;
	}
	case GRES_STATE_SRC_CONTEXT_PTR:
	{
		slurm_gres_context_t *gres_ctx = src_ptr;
		new_gres_state->config_flags = gres_ctx->config_flags;
		new_gres_state->plugin_id = gres_ctx->plugin_id;
		new_gres_state->gres_name = xstrdup(gres_ctx->gres_name);
		break;
	}
	case GRES_STATE_SRC_KEY_PTR:
	{
		gres_key_t *search_key = src_ptr;
		new_gres_state->config_flags = search_key->config_flags;
		new_gres_state->plugin_id = search_key->plugin_id;
		/*
		 * gres_name should be handled after this since search_key
		 * doesn't have that
		 */
		break;
	}
	default:
		error("%s: No way to create gres_state given", __func__);
		xfree(new_gres_state);
		break;
	}

	return new_gres_state;
}

/*
 * Find a gres_context by plugin_id
 * Must hold gres_context_lock before calling.
 */
static slurm_gres_context_t *_find_context_by_id(uint32_t plugin_id)
{
	for (int j = 0; j < gres_context_cnt; j++)
		if (gres_context[j].plugin_id == plugin_id)
			return &gres_context[j];
	return NULL;
}


static int _load_plugin(slurm_gres_context_t *gres_ctx)
{
	/*
	 * Must be synchronized with slurm_gres_ops_t above.
	 */
	static const char *syms[] = {
		"gres_p_node_config_load",
		"gres_p_job_set_env",
		"gres_p_step_set_env",
		"gres_p_task_set_env",
		"gres_p_send_stepd",
		"gres_p_recv_stepd",
		"gres_p_get_job_info",
		"gres_p_get_step_info",
		"gres_p_get_devices",
		"gres_p_step_hardware_init",
		"gres_p_step_hardware_fini",
		"gres_p_epilog_build_env",
		"gres_p_epilog_set_env"
	};
	int n_syms = sizeof(syms) / sizeof(char *);

	/* Find the correct plugin */
	if (gres_ctx->config_flags & GRES_CONF_COUNT_ONLY) {
		debug("Plugin of type %s only tracks gres counts",
		      gres_ctx->gres_type);
		return SLURM_SUCCESS;
	}

	gres_ctx->cur_plugin = plugin_load_and_link(
		gres_ctx->gres_type,
		n_syms, syms,
		(void **) &gres_ctx->ops);
	if (gres_ctx->cur_plugin != PLUGIN_INVALID_HANDLE)
		return SLURM_SUCCESS;

	if (errno != EPLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      gres_ctx->gres_type, plugin_strerror(errno));
		return SLURM_ERROR;
	}

	debug("gres: Couldn't find the specified plugin name for %s looking "
	      "at all files", gres_ctx->gres_type);

	/* Get plugin list */
	if (gres_ctx->plugin_list == NULL) {
		gres_ctx->plugin_list = plugrack_create("gres");
		plugrack_read_dir(gres_ctx->plugin_list,
				  slurm_conf.plugindir);
	}

	gres_ctx->cur_plugin = plugrack_use_by_type(
		gres_ctx->plugin_list,
		gres_ctx->gres_type );
	if (gres_ctx->cur_plugin == PLUGIN_INVALID_HANDLE) {
		debug("Cannot find plugin of type %s, just track gres counts",
		      gres_ctx->gres_type);
		gres_ctx->config_flags |= GRES_CONF_COUNT_ONLY;
		return SLURM_ERROR;
	}

	/* Dereference the API. */
	if (plugin_get_syms(gres_ctx->cur_plugin,
			    n_syms, syms,
			    (void **) &gres_ctx->ops ) < n_syms ) {
		error("Incomplete %s plugin detected",
		      gres_ctx->gres_type);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _unload_plugin(slurm_gres_context_t *gres_ctx)
{
	int rc;

	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if (gres_ctx->plugin_list)
		rc = plugrack_destroy(gres_ctx->plugin_list);
	else {
		rc = SLURM_SUCCESS;
		plugin_unload(gres_ctx->cur_plugin);
	}
	xfree(gres_ctx->gres_name);
	xfree(gres_ctx->gres_name_colon);
	xfree(gres_ctx->gres_type);

	return rc;
}

static bool _is_shared_name(char *name)
{
	if (!xstrcmp(name, "mps") ||
	    !xstrcmp(name, "shard"))
		return true;
	return false;
}

static void _set_shared_flag(char *name, uint32_t *config_flags)
{
	if (_is_shared_name(name))
		*config_flags |= GRES_CONF_SHARED;
}

/*
 * Add new gres context to gres_context array and load the plugin.
 * Must hold gres_context_lock before calling.
 */
static void _add_gres_context(char *gres_name)
{
	slurm_gres_context_t *gres_ctx;

	if (!gres_name || !gres_name[0])
		fatal("%s: invalid empty gres_name", __func__);

	xrecalloc(gres_context, (gres_context_cnt + 1),
		  sizeof(slurm_gres_context_t));

	gres_ctx = &gres_context[gres_context_cnt];
	_set_shared_flag(gres_name, &gres_ctx->config_flags);
	gres_ctx->gres_name = xstrdup(gres_name);
	gres_ctx->plugin_id = gres_build_id(gres_name);
	gres_ctx->gres_type = xstrdup_printf("gres/%s", gres_name);
	gres_ctx->plugin_list = NULL;
	gres_ctx->cur_plugin = PLUGIN_INVALID_HANDLE;

	gres_context_cnt++;
}

/*
 * Initialize the GRES plugins.
 *
 * Returns a Slurm errno.
 */
extern int gres_init(void)
{
	int i, j, rc = SLURM_SUCCESS;
	char *last = NULL, *names, *one_name, *full_name;
	char *sorted_names = NULL, *sep = "", *shared_names = NULL;
	bool have_gpu = false, have_shared = false;
	char *shared_sep = "";

	if (init_run && (gres_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&gres_context_lock);

	if (gres_context_cnt >= 0)
		goto fini;

	local_plugins_str = xstrdup(slurm_conf.gres_plugins);
	gres_context_cnt = 0;
	if ((local_plugins_str == NULL) || (local_plugins_str[0] == '\0'))
		goto fini;

	/* Ensure that "gres/'shared'" follows "gres/gpu" */
	have_gpu = false;
	have_shared = false;
	names = xstrdup(local_plugins_str);
	one_name = strtok_r(names, ",", &last);
	while (one_name) {
		bool skip_name = false;
		if (_is_shared_name(one_name)) {
			have_shared = true;
			if (!have_gpu) {
				/* "shared" must follow "gpu" */
				skip_name = true;
				xstrfmtcat(shared_names, "%s%s",
					   shared_sep, one_name);
				shared_sep = ",";
			}
		} else if (!xstrcmp(one_name, "gpu")) {
			have_gpu = true;
			gpu_plugin_id = gres_build_id("gpu");
		}
		if (!skip_name) {
			xstrfmtcat(sorted_names, "%s%s", sep, one_name);
			sep = ",";
		}
		one_name = strtok_r(NULL, ",", &last);
	}
	if (shared_names) {
		if (!have_gpu)
			fatal("GresTypes: gres/'shared' requires that gres/gpu also be configured");
		xstrfmtcat(sorted_names, "%s%s", sep, shared_names);
		xfree(shared_names);
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
	if (have_shared && running_in_slurmctld() &&
	    (select_plugin_type != SELECT_TYPE_CONS_TRES)) {
		fatal("Use of gres/mps requires the use of select/cons_tres");
	}

	init_run = true;
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

extern int gres_get_gres_cnt(void)
{
	static int cnt = -1;

	if (cnt != -1)
		return cnt;

	gres_init();

	slurm_mutex_lock(&gres_context_lock);
	cnt = gres_context_cnt;
	slurm_mutex_unlock(&gres_context_lock);

	return cnt;
}

/*
 * Add a GRES record. This is used by the node_features plugin after the
 * slurm.conf file is read and the initial GRES records are built by
 * gres_init().
 */
extern void gres_add(char *gres_name)
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
extern char *gres_name_filter(char *orig_gres, char *nodes)
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
extern int gres_fini(void)
{
	int i, j, rc = SLURM_SUCCESS;

	slurm_mutex_lock(&gres_context_lock);
	xfree(gres_node_name);
	if (gres_context_cnt < 0)
		goto fini;

	init_run = false;
	for (i = 0; i < gres_context_cnt; i++) {
		j = _unload_plugin(gres_context + i);
		if (j != SLURM_SUCCESS)
			rc = j;
	}
	xfree(gres_context);
	xfree(local_plugins_str);
	FREE_NULL_LIST(gres_conf_list);
	FREE_NULL_BUFFER(gres_context_buf);
	FREE_NULL_BUFFER(gres_conf_buf);
	gres_context_cnt = -1;

fini:	slurm_mutex_unlock(&gres_context_lock);
	return rc;
}

/*
 * ************************************************************************
 *                          P L U G I N   C A L L S                       *
 * ************************************************************************
 */

/*
 * Return a plugin-specific help message for salloc, sbatch and srun
 * Result must be xfree()'d.
 *
 * NOTE: GRES "type" (e.g. model) information is only available from slurmctld
 * after slurmd registers. It is not readily available from srun (as used here).
 */
extern char *gres_help_msg(void)
{
	int i;
	char *msg = xstrdup("Valid gres options are:\n");

	gres_init();

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
extern int gres_reconfig(void)
{
	int rc = SLURM_SUCCESS;
	bool plugin_change;

	slurm_mutex_lock(&gres_context_lock);

	if (xstrcmp(slurm_conf.gres_plugins, local_plugins_str))
		plugin_change = true;
	else
		plugin_change = false;
	slurm_mutex_unlock(&gres_context_lock);

	if (plugin_change) {
		error("GresPlugins changed from %s to %s ignored",
		      local_plugins_str, slurm_conf.gres_plugins);
		error("Restart the slurmctld daemon to change GresPlugins");
#if 0
		/* This logic would load new plugins, but we need the old
		 * plugins to persist in order to process old state
		 * information. */
		rc = gres_fini();
		if (rc == SLURM_SUCCESS)
			rc = gres_init();
#endif
	}

	return rc;
}

/* Return 1 if a gres_conf record is the correct plugin_id and has no file */
static int _find_fileless_gres(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = (gres_slurmd_conf_t *)x;
	uint32_t plugin_id = *(uint32_t *)arg;

	if ((gres_slurmd_conf->plugin_id == plugin_id) &&
	    !gres_slurmd_conf->file) {
		debug("Removing file-less GPU %s:%s from final GRES list",
		      gres_slurmd_conf->name, gres_slurmd_conf->type_name);
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
 * Create and return a comma-separated zeroed-out links string with a -1 in the
 * given GPU position indicated by index. Caller must xfree() the returned
 * string.
 *
 * Used to record the enumeration order (PCI bus ID order) of GPUs for sorting,
 * even when the GPU does not support nvlinks. E.g. for three total GPUs, their
 * links strings would look like this:
 *
 * GPU at index 0: -1,0,0
 * GPU at index 1: 0,-1,0
 * GPU at index 2: -0,0,-1
 */
extern char *gres_links_create_empty(unsigned int index,
				     unsigned int device_count)
{
	char *links_str = NULL;

	for (unsigned int i = 0; i < device_count; ++i) {
		xstrfmtcat(links_str, "%s%d",
			   i ? "," : "",
			   (i == index) ? -1 : 0);
	}

	return links_str;
}

/*
 * Check that we have a comma-delimited list of numbers, and return the index of
 * the GPU (-1) in the links string.
 *
 * Returns a non-zero-based index of the GPU in the links string, if found.
 * If not found, returns a negative value.
 * Return values:
 * 0+: GPU index
 * -1: links string is NULL.
 * -2: links string is not NULL, but is invalid. Possible invalid reasons:
 *     * error parsing the comma-delimited links string
 *     * links string is an empty string
 *     * the 'self' GPU identifier isn't found (i.e. no -1)
 *     * there is more than one 'self' GPU identifier found
 */
extern int gres_links_validate(char *links)
{
	char *tmp, *tok, *save_ptr = NULL, *end_ptr = NULL;
	long int val;
	int rc;
	int i;

	if (!links)
		return -1;
	if (links[0] == '\0') {
		error("%s: Links is an empty string", __func__);
		return -2;
	}

	tmp = xstrdup(links);
	tok = strtok_r(tmp, ",", &save_ptr);
	rc = -1;
	i = 0;
	while (tok) {
		val = strtol(tok, &end_ptr, 10);
		if ((val < -2) || (val > GRES_MAX_LINK) || (val == LONG_MIN) ||
		    (end_ptr[0] != '\0')) {
			error("%s: Failed to parse token '%s' in links string '%s'",
			      __func__, tok, links);
			rc = -2;
			break;
		}
		if (val == -1) {
			if (rc != -1) {
				error("%s: links string '%s' has more than one -1",
				      __func__, links);
				rc = -2;
				break;
			}
			rc = i;
		}
		i++;
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	/* If the current GPU (-1) wasn't found, that's an error */
	if (rc == -1) {
		error("%s: -1 wasn't found in links string '%s'", __func__,
		      links);
		rc = -2;
	}

	return rc;
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
		else if (autodetect_flags & GRES_AUTODETECT_GPU_ONEAPI)
			xstrfmtcat(flags, "%soneapi", flags ? "," : "");
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
	else if (xstrcasestr(str, "oneapi"))
		flags |= GRES_AUTODETECT_GPU_ONEAPI;
	else if (!xstrcasecmp(str, "off"))
		flags |= GRES_AUTODETECT_GPU_OFF;
	else
		error("unknown autodetect flag '%s'", str);

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
 * Check to see if current GRES record matches the name of the previous GRES
 * record that set env flags.
 */
static bool _same_gres_type_as_prev(prev_env_flags_t *prev_env,
				    gres_slurmd_conf_t *p)
{
	if ((gres_build_id(p->name) == prev_env->name_hash))
		return true;
	else
		return false;
}

/*
 * Save off env flags, GRES name, and no_gpu_env (for the next gres.conf line to
 * possibly inherit or to check against).
 */
static void _set_prev_env_flags(prev_env_flags_t *prev_env,
				gres_slurmd_conf_t *p, uint32_t env_flags,
				bool no_gpu_env)
{
	prev_env->flags = env_flags;
	prev_env->name_hash = gres_build_id(p->name);
	prev_env->no_gpu_env = no_gpu_env;
}

/*
 * Parse a gres.conf Flags string
 */
extern uint32_t gres_flags_parse(char *input, bool *no_gpu_env,
				 bool *sharing_mentioned)
{
	uint32_t flags = 0;
	if (xstrcasestr(input, "CountOnly"))
		flags |= GRES_CONF_COUNT_ONLY;
	if (xstrcasestr(input, "nvidia_gpu_env"))
		flags |= GRES_CONF_ENV_NVML;
	if (xstrcasestr(input, "amd_gpu_env"))
		flags |= GRES_CONF_ENV_RSMI;
	if (xstrcasestr(input, "intel_gpu_env"))
		flags |= GRES_CONF_ENV_ONEAPI;
	if (xstrcasestr(input, "opencl_env"))
		flags |= GRES_CONF_ENV_OPENCL;
	if (xstrcasestr(input, "one_sharing"))
		flags |= GRES_CONF_ONE_SHARING;
	/* String 'no_gpu_env' will clear all GPU env vars */
	if (no_gpu_env)
		*no_gpu_env = xstrcasestr(input, "no_gpu_env");
	if (sharing_mentioned) {
		if ((flags & GRES_CONF_ONE_SHARING) ||
		    xstrcasestr(input, "all_sharing"))
			*sharing_mentioned = true;
	}
	return flags;
}

/*
 * Build gres_slurmd_conf_t record based upon a line from the gres.conf file
 */
static int _parse_gres_config(void **dest, slurm_parser_enum_t type,
			      const char *key, const char *value,
			      const char *line, char **leftover)
{
	int i;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t *p;
	uint64_t tmp_uint64, mult;
	char *tmp_str, *last;
	bool cores_flag = false, cpus_flag = false;
	char *type_str = NULL;
	char *autodetect_string = NULL;
	bool autodetect = false, set_default_envs = true;
	/* Remember the last-set Flags value */
	static prev_env_flags_t prev_env;

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

	if (s_p_get_string(&p->type_name, "Type", tbl)) {
		p->config_flags |= GRES_CONF_HAS_TYPE;
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
		int file_count = 0;
		if (p->config_flags & GRES_CONF_HAS_FILE)
			fatal("File and MultipleFiles options are mutually exclusive");
		p->count = 1;
		file_count = _validate_file(p->file, p->name);
		if (file_count < 2)
			fatal("MultipleFiles does not contain multiple files. Use File instead");
		p->config_flags |= GRES_CONF_HAS_FILE;
		p->config_flags |= GRES_CONF_HAS_MULT;
	}

	if (s_p_get_string(&tmp_str, "Flags", tbl)) {
		uint32_t env_flags = 0;
		bool no_gpu_env = false;
		bool sharing_mentioned = false;
		uint32_t flags = gres_flags_parse(tmp_str, &no_gpu_env,
						  &sharing_mentioned);

		/* The default for MPS is to have only one gpu sharing */
		if (!sharing_mentioned && !xstrcasecmp(p->name, "mps"))
			flags |= GRES_CONF_ONE_SHARING;

		/* Break out flags into env flags and non-env flags */
		env_flags = flags & GRES_CONF_ENV_SET;
		p->config_flags |= flags & ~GRES_CONF_ENV_SET;

		if (env_flags && no_gpu_env)
			fatal("Invalid GRES record name=%s type=%s: Flags (%s) contains \"no_gpu_env\", which must be mutually exclusive to all other GRES env flags of same node and name",
			      p->name, p->type_name, tmp_str);

		if (env_flags || no_gpu_env) {
			set_default_envs = false;
			/*
			 * Make sure that Flags are consistent with each other
			 * if set for multiple lines of the same GRES.
			 */
			if (prev_env.name_hash &&
			    _same_gres_type_as_prev(&prev_env, p) &&
			    ((prev_env.flags != env_flags) ||
			     (prev_env.no_gpu_env != no_gpu_env)))
				fatal("Invalid GRES record name=%s type=%s: Flags (%s) does not match env flags for previous GRES of same node and name",
				      p->name, p->type_name, tmp_str);
			p->config_flags |= env_flags;
			_set_prev_env_flags(&prev_env, p, env_flags,
					    no_gpu_env);
		}

		xfree(tmp_str);
	} else if ((prev_env.flags || prev_env.no_gpu_env) &&
		   _same_gres_type_as_prev(&prev_env, p)) {
		/* Inherit env flags from previous GRES line with same name */
		set_default_envs = false;
		if (!prev_env.no_gpu_env)
			p->config_flags |= prev_env.flags;
	} else {
		if (!xstrcasecmp(p->name, "mps"))
			p->config_flags |= GRES_CONF_ONE_SHARING;
	}

	/* Flags not set. By default, all env vars are set for GPUs */
	if (set_default_envs && !xstrcasecmp(p->name, "gpu")) {
		uint32_t env_flags = GRES_CONF_ENV_SET | GRES_CONF_ENV_DEF;
		p->config_flags |= env_flags;
		_set_prev_env_flags(&prev_env, p, env_flags, false);
	}

	if (s_p_get_string(&p->links, "Link",  tbl) ||
	    s_p_get_string(&p->links, "Links", tbl)) {
		if (gres_links_validate(p->links) < -1) {
			error("gres.conf: Ignoring invalid Links=%s for Name=%s",
			      p->links, p->name);
			xfree(p->links);
		}

	}

	_set_shared_flag(p->name, &p->config_flags);

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
		    !gres_id_shared(p->config_flags)) {
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
static int _parse_gres_config_node(void **dest, slurm_parser_enum_t type,
				   const char *key, const char *value,
				   const char *line, char **leftover)
{
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
	gres_state_t *gres_state_node = (gres_state_t *)x;
	slurm_gres_context_t *gres_ctx = (slurm_gres_context_t *)arg;
	gres_node_state_t *gres_ns;
	uint64_t tmp_count = 0;

	/* Only look at the GRES under the current plugin (same name) */
	if (gres_state_node->plugin_id != gres_ctx->plugin_id)
		return 0;

	gres_ns = (gres_node_state_t *)gres_state_node->gres_data;

	/*
	 * gres_cnt_config should equal the combined count from
	 * type_cnt_avail if there are no untyped GRES
	 */
	for (uint16_t i = 0; i < gres_ns->type_cnt; i++)
		tmp_count += gres_ns->type_cnt_avail[i];

	/* Forbid mixing typed and untyped GRES under the same name */
	if (gres_ns->type_cnt &&
	    gres_ns->gres_cnt_config > tmp_count)
		fatal("%s: Some %s GRES in slurm.conf have a type while others do not (gres_ns->gres_cnt_config (%"PRIu64") > tmp_count (%"PRIu64"))",
		      __func__, gres_ctx->gres_name,
		      gres_ns->gres_cnt_config, tmp_count);
	return 1;
}

static void _validate_slurm_conf(List slurm_conf_list,
				 slurm_gres_context_t *gres_ctx)
{
	if (!slurm_conf_list)
		return;

	(void)list_for_each_nobreak(slurm_conf_list, _foreach_slurm_conf,
				    gres_ctx);
}

static int _foreach_gres_conf(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = (gres_slurmd_conf_t *)x;
	foreach_gres_conf_t *foreach_gres_conf = (foreach_gres_conf_t *)arg;
	slurm_gres_context_t *gres_ctx = foreach_gres_conf->gres_ctx;
	bool orig_has_file, orig_has_type;

	/* Only look at the GRES under the current plugin (same name) */
	if (gres_slurmd_conf->plugin_id != gres_ctx->plugin_id)
		return 0;

	/*
	 * If any plugin of this type has this set it will virally set
	 * any other to be the same as we use the gres_ctx from here
	 * on out.
	 */
	if (gres_slurmd_conf->config_flags & GRES_CONF_COUNT_ONLY)
		gres_ctx->config_flags |= GRES_CONF_COUNT_ONLY;

	if (gres_slurmd_conf->config_flags & GRES_CONF_ONE_SHARING)
		gres_ctx->config_flags |= GRES_CONF_ONE_SHARING;
	/*
	 * Since there could be multiple types of the same plugin we
	 * need to only make sure we load it once.
	 */
	if (!(gres_ctx->config_flags & GRES_CONF_LOADED)) {
		/*
		 * Ignore return code, as we will still support the gres
		 * with or without the plugin.
		 */
		if (_load_plugin(gres_ctx) == SLURM_SUCCESS)
			gres_ctx->config_flags |= GRES_CONF_LOADED;
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
		      gres_ctx->gres_name);
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
		      gres_ctx->gres_name);
	}

	if (!foreach_gres_conf->new_has_file &&
	    !foreach_gres_conf->new_has_type &&
	    (foreach_gres_conf->rec_count > 1)) {
		fatal("gres.conf duplicate records for %s",
		      gres_ctx->gres_name);
	}

	if (foreach_gres_conf->new_has_file)
		gres_ctx->config_flags |= GRES_CONF_HAS_FILE;

	return 0;
}

static void _validate_gres_conf(List gres_conf_list,
				slurm_gres_context_t *gres_ctx)
{
	foreach_gres_conf_t gres_conf = {
		.gres_ctx = gres_ctx,
		.new_has_file = -1,
		.new_has_type = -1,
		.rec_count = 0,
	};

	(void)list_for_each_nobreak(gres_conf_list, _foreach_gres_conf,
				    &gres_conf);

	if (!(gres_ctx->config_flags & GRES_CONF_LOADED)) {
		/*
		 * This means there was no gre.conf line for this gres found.
		 * We still need to try to load it for AutoDetect's sake.
		 * If we fail loading we will treat it as a count
		 * only GRES since the stepd will try to load it elsewise.
		 */
		if (_load_plugin(gres_ctx) != SLURM_SUCCESS)
			gres_ctx->config_flags |= GRES_CONF_COUNT_ONLY;
	} else
		/* Remove as this is only really used locally */
		gres_ctx->config_flags &= (~GRES_CONF_LOADED);
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
	gres_slurmd_conf_t *gres_slurmd_conf;
	ListIterator iter = list_iterator_create(gres_conf_list_tmp);
	while ((gres_slurmd_conf = list_next(iter))) {
		/* Note: plugin type filter already applied */
		/* Check that type is the same */
		if (xstrcasecmp(gres_slurmd_conf->type_name, type_name))
			continue;
		/* Keep track of counts */
		if (gres_slurmd_conf->count > count) {
			gres_slurmd_conf->count -= count;
			/* This slurm.conf GRES specification is now used up */
			list_iterator_destroy(iter);
			return;
		} else {
			count -= gres_slurmd_conf->count;
			gres_slurmd_conf->count = 0;
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
 * gres_ctx     - (in) Which GRES plugin we are currently working in.
 */
static void _check_conf_mismatch(List slurm_conf_list, List gres_conf_list,
				 slurm_gres_context_t *gres_ctx)
{
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	gres_state_t *gres_state_node;
	List gres_conf_list_tmp;

	/* E.g. slurm_conf_list will be NULL in the case of --gpu-bind */
	if (!slurm_conf_list || !gres_conf_list)
		return;

	/*
	 * Duplicate the gres.conf list with records relevant to this GRES
	 * plugin only so we can mangle records. Only add records under the
	 * current plugin.
	 */
	gres_conf_list_tmp = list_create(destroy_gres_slurmd_conf);
	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(iter))) {
		gres_slurmd_conf_t *gres_slurmd_conf_tmp;
		if (gres_slurmd_conf->plugin_id != gres_ctx->plugin_id)
			continue;

		gres_slurmd_conf_tmp = xmalloc(sizeof(*gres_slurmd_conf_tmp));
		gres_slurmd_conf_tmp->name = xstrdup(gres_slurmd_conf->name);
		gres_slurmd_conf_tmp->type_name =
			xstrdup(gres_slurmd_conf->type_name);
		gres_slurmd_conf_tmp->count = gres_slurmd_conf->count;
		list_append(gres_conf_list_tmp, gres_slurmd_conf_tmp);
	}
	list_iterator_destroy(iter);

	/*
	 * Loop through the slurm.conf list and see if there are more gres.conf
	 * GRES than expected.
	 */
	iter = list_iterator_create(slurm_conf_list);
	while ((gres_state_node = list_next(iter))) {
		gres_node_state_t *gres_ns;

		if (gres_state_node->plugin_id != gres_ctx->plugin_id)
			continue;

		/* Determine if typed or untyped, and act accordingly */
		gres_ns = (gres_node_state_t *)gres_state_node->gres_data;
		if (!gres_ns->type_name) {
			_compare_conf_counts(gres_conf_list_tmp,
					     gres_ns->gres_cnt_config, NULL);
			continue;
		}

		for (int i = 0; i < gres_ns->type_cnt; ++i) {
			_compare_conf_counts(gres_conf_list_tmp,
					     gres_ns->type_cnt_avail[i],
					     gres_ns->type_name[i]);
		}
	}
	list_iterator_destroy(iter);

	/*
	 * Loop through gres_conf_list_tmp to print errors for gres.conf
	 * records that were not completely accounted for in slurm.conf.
	 */
	iter = list_iterator_create(gres_conf_list_tmp);
	while ((gres_slurmd_conf = list_next(iter)))
		if (gres_slurmd_conf->count > 0)
			info("WARNING: A line in gres.conf for GRES %s%s%s has %"PRIu64" more configured than expected in slurm.conf. Ignoring extra GRES.",
			     gres_slurmd_conf->name,
			     (gres_slurmd_conf->type_name) ? ":" : "",
			     (gres_slurmd_conf->type_name) ?
			     gres_slurmd_conf->type_name : "",
			     gres_slurmd_conf->count);
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
 *			 it's an untyped GRES.
 *
 * Returns the first gres.conf record from gres_conf_list with the same type
 * name as the slurm.conf record.
 */
static gres_slurmd_conf_t *_match_type(List gres_conf_list,
				       slurm_gres_context_t *gres_ctx,
				       char *type_name)
{
	ListIterator gres_conf_itr;
	gres_slurmd_conf_t *gres_slurmd_conf = NULL;

	gres_conf_itr = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(gres_conf_itr))) {
		if (gres_slurmd_conf->plugin_id != gres_ctx->plugin_id)
			continue;

		/*
		 * If type_name is NULL we will take the first matching
		 * gres_slurmd_conf that we find.  This means we also will
		 * remove the type from the gres_slurmd_conf to match 18.08
		 * stylings.
		 */
		if (!type_name) {
			xfree(gres_slurmd_conf->type_name);
			gres_slurmd_conf->config_flags &= ~GRES_CONF_HAS_TYPE;
		} else if (xstrcasecmp(gres_slurmd_conf->type_name, type_name))
			continue;

		/* We found a match, so remove from gres_conf_list and break */
		list_remove(gres_conf_itr);
		break;
	}
	list_iterator_destroy(gres_conf_itr);

	return gres_slurmd_conf;
}

/*
 * Add a GRES conf record with count == 0 to gres_list.
 *
 * gres_list    - (in/out) The gres list to add to.
 * gres_context - (in) The GRES plugin to add a GRES record for.
 * cpu_cnt      - (in) The cpu count configured for the node.
 */
static void _add_gres_config_empty(List gres_list,
				   slurm_gres_context_t *gres_ctx,
				   uint32_t cpu_cnt)
{
	gres_slurmd_conf_t *gres_slurmd_conf =
		xmalloc(sizeof(*gres_slurmd_conf));
	gres_slurmd_conf->cpu_cnt = cpu_cnt;
	gres_slurmd_conf->name = xstrdup(gres_ctx->gres_name);
	gres_slurmd_conf->plugin_id = gres_ctx->plugin_id;
	list_append(gres_list, gres_slurmd_conf);
}

/*
 * Truncate the File hostrange string of a GRES record to be at most
 * new_count entries. The extra entries will be removed.
 *
 * gres_slurmd_conf - (in/out) The GRES record to modify.
 * count     - (in) The new number of entries in File
 */
static void _set_file_subset(gres_slurmd_conf_t *gres_slurmd_conf,
			     uint64_t new_count)
{
	/* Convert file to hostrange */
	hostlist_t hl = hostlist_create(gres_slurmd_conf->file);
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
	       gres_slurmd_conf->name, gres_slurmd_conf->type_name, old_count,
	       gres_slurmd_conf->file);

	/* Set file to the new subset */
	xfree(gres_slurmd_conf->file);
	gres_slurmd_conf->file = hostlist_ranged_string_xmalloc(hl);

	debug3("%s: to (%"PRIu64") %s", __func__, new_count,
	       gres_slurmd_conf->file);
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
			 char *type_name, slurm_gres_context_t *gres_ctx,
			 uint32_t cpu_count)
{
	gres_slurmd_conf_t *match;
	uint32_t flags;

	/* If slurm.conf count is initially 0, don't waste time on it */
	if (count == 0)
		return;

	/*
	 * There can be multiple gres.conf GRES lines contained within a
	 * single slurm.conf GRES line, due to different values of Cores
	 * and Links. Append them to the list where possible.
	 */
	while ((match = _match_type(gres_conf_list, gres_ctx, type_name))) {
		list_append(new_list, match);

		debug3("%s: From gres.conf, using %s:%s:%"PRIu64":%s", __func__,
		       match->name, match->type_name, match->count,
		       match->file);

		/*
		 * See if we need to merge with any more gres.conf records.
		 * NOTE: _set_file_subset() won't run on a MultipleFiles GRES,
		 * since match->count will always be 1 and count is always >= 1
		 */
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

	/* Set default env flags, and allow AutoDetect to override */
	flags = 0;
	if (!xstrcasecmp(gres_ctx->gres_name, "gpu"))
		flags |= (GRES_CONF_ENV_SET | GRES_CONF_ENV_DEF);
	if (gres_ctx->config_flags & GRES_CONF_COUNT_ONLY)
		flags |= GRES_CONF_COUNT_ONLY;

	add_gres_to_list(new_list, gres_ctx->gres_name, count, cpu_count,
			 NULL, NULL, NULL, type_name, NULL, NULL, flags);
}

/*
 * Merge a single slurm.conf GRES specification with any relevant gres.conf
 * records and append the result to new_list.
 *
 * gres_conf_list - (in) The gres.conf list.
 * new_list       - (out) The new merged [slurm|gres].conf list.
 * ptr            - (in) A slurm.conf GRES record.
 * gres_ctx   - (in) Which GRES plugin we are working in.
 * cpu_cnt        - (in) A count of CPUs on the node.
 */
static void _merge_gres(List gres_conf_list, List new_list,
			gres_node_state_t *gres_ns,
			slurm_gres_context_t *gres_ctx, uint32_t cpu_cnt)
{
	/* If this GRES has no types, merge in the single untyped GRES */
	if (gres_ns->type_cnt == 0) {
		_merge_gres2(gres_conf_list, new_list,
			     gres_ns->gres_cnt_config, NULL, gres_ctx,
			     cpu_cnt);
		return;
	}

	/* If this GRES has types, merge in each typed GRES */
	for (int i = 0; i < gres_ns->type_cnt; i++) {
		_merge_gres2(gres_conf_list, new_list,
			     gres_ns->type_cnt_avail[i],
			     gres_ns->type_name[i], gres_ctx, cpu_cnt);
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
 *		     merged slurm.conf/gres.conf list.
 * slurm_conf_list - (in) GRES data from slurm.conf.
 */
static void _merge_config(node_config_load_t *node_conf, List gres_conf_list,
			  List slurm_conf_list)
{
	int i;
	gres_state_t *gres_state_node;
	ListIterator iter;
	bool found;

	List new_gres_list = list_create(destroy_gres_slurmd_conf);

	for (i = 0; i < gres_context_cnt; i++) {
		/* Copy GRES configuration from slurm.conf */
		if (slurm_conf_list) {
			found = false;
			iter = list_iterator_create(slurm_conf_list);
			while ((gres_state_node = list_next(iter))) {
				if (gres_state_node->plugin_id !=
				    gres_context[i].plugin_id)
					continue;
				found = true;
				_merge_gres(gres_conf_list, new_gres_list,
					    gres_state_node->gres_data,
					    &gres_context[i],
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

static void _pack_gres_context(slurm_gres_context_t *gres_ctx, buf_t *buffer)
{
	/* gres_ctx->cur_plugin: DON'T PACK will be filled in on the other
	 * side */
	pack32(gres_ctx->config_flags, buffer);
	packstr(gres_ctx->gres_name, buffer);
	packstr(gres_ctx->gres_name_colon, buffer);
	pack32((uint32_t)gres_ctx->gres_name_colon_len, buffer);
	packstr(gres_ctx->gres_type, buffer);
	/* gres_ctx->ops: DON'T PACK will be filled in on the other side */
	pack32(gres_ctx->plugin_id, buffer);
	/* gres_ctx->plugin_list: DON'T PACK will be filled in on the other
	 * side */
	pack64(gres_ctx->total_cnt, buffer);
}

static int _unpack_gres_context(slurm_gres_context_t *gres_ctx, buf_t *buffer)
{
	uint32_t uint32_tmp;

	/* gres_ctx->cur_plugin: filled in later with _load_plugin() */
	safe_unpack32(&gres_ctx->config_flags, buffer);
	safe_unpackstr_xmalloc(&gres_ctx->gres_name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&gres_ctx->gres_name_colon, &uint32_tmp, buffer);
	safe_unpack32(&uint32_tmp, buffer);
	gres_ctx->gres_name_colon_len = (int)uint32_tmp;
	safe_unpackstr_xmalloc(&gres_ctx->gres_type, &uint32_tmp, buffer);
	/* gres_ctx->ops: filled in later with _load_plugin() */
	safe_unpack32(&gres_ctx->plugin_id, buffer);
	/* gres_ctx->plugin_list: filled in later with _load_plugin() */
	safe_unpack64(&gres_ctx->total_cnt, buffer);
	return SLURM_SUCCESS;

unpack_error:
	error("%s: unpack_error", __func__);
	return SLURM_ERROR;
}

static void _pack_gres_slurmd_conf(void *in, uint16_t protocol_version,
				   buf_t *buffer)
{
	gres_slurmd_conf_t *gres_slurmd_conf = (gres_slurmd_conf_t *)in;

	/*
	 * Ignore protocol_version at the time of writing this only deals with
	 * communication from the slurmd to a new stepd which should always be
	 * the same version.  This function is called from slurm_pack_list which
	 * requires protocol_version.
	 */

	/* Pack gres_slurmd_conf_t */
	pack32(gres_slurmd_conf->config_flags, buffer);
	pack64(gres_slurmd_conf->count, buffer);
	pack32(gres_slurmd_conf->cpu_cnt, buffer);
	packstr(gres_slurmd_conf->cpus, buffer);
	pack_bit_str_hex(gres_slurmd_conf->cpus_bitmap, buffer);
	packstr(gres_slurmd_conf->file, buffer);
	packstr(gres_slurmd_conf->links, buffer);
	packstr(gres_slurmd_conf->name, buffer);
	packstr(gres_slurmd_conf->type_name, buffer);
	packstr(gres_slurmd_conf->unique_id, buffer);
	pack32(gres_slurmd_conf->plugin_id, buffer);
}

static int _unpack_gres_slurmd_conf(void **object, uint16_t protocol_version,
				    buf_t *buffer)
{
	uint32_t uint32_tmp;
	gres_slurmd_conf_t *gres_slurmd_conf =
		xmalloc(sizeof(*gres_slurmd_conf));

	/*
	 * Ignore protocol_version at the time of writing this only deals with
	 * communication from the slurmd to a new stepd which should always be
	 * the same version.  This function is called from slurm_unpack_list
	 * which requires protocol_version.
	 */

	/* Unpack gres_slurmd_conf_t */
	safe_unpack32(&gres_slurmd_conf->config_flags, buffer);
	safe_unpack64(&gres_slurmd_conf->count, buffer);
	safe_unpack32(&gres_slurmd_conf->cpu_cnt, buffer);
	safe_unpackstr_xmalloc(&gres_slurmd_conf->cpus, &uint32_tmp, buffer);
	unpack_bit_str_hex(&gres_slurmd_conf->cpus_bitmap, buffer);
	safe_unpackstr_xmalloc(&gres_slurmd_conf->file, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&gres_slurmd_conf->links, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&gres_slurmd_conf->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&gres_slurmd_conf->type_name,
			       &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&gres_slurmd_conf->unique_id,
			       &uint32_tmp, buffer);
	safe_unpack32(&gres_slurmd_conf->plugin_id, buffer);

	*object = gres_slurmd_conf;
	return SLURM_SUCCESS;

unpack_error:
	destroy_gres_slurmd_conf(gres_slurmd_conf);
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
		slurm_gres_context_t *gres_ctx = &gres_context[i];
		_pack_gres_context(gres_ctx, gres_context_buf);
		if (gres_ctx->ops.send_stepd)
			(*(gres_ctx->ops.send_stepd))(gres_context_buf);
	}
}

static int _unpack_context_buf(buf_t *buffer)
{
	uint32_t cnt;
	safe_unpack32(&cnt, buffer);

	gres_context_cnt = cnt;

	if (!gres_context_cnt)
		return SLURM_SUCCESS;

	xrecalloc(gres_context, gres_context_cnt, sizeof(slurm_gres_context_t));
	for (int i = 0; i < gres_context_cnt; i++) {
		slurm_gres_context_t *gres_ctx = &gres_context[i];
		if (_unpack_gres_context(gres_ctx, buffer) != SLURM_SUCCESS)
			goto unpack_error;
		(void)_load_plugin(gres_ctx);
		if (gres_ctx->ops.recv_stepd)
			(*(gres_ctx->ops.recv_stepd))(buffer);
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

static int _unpack_gres_conf(buf_t *buffer)
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
 * IN cpu_cnt - Number of CPUs configured for node node_name.
 * IN node_name - Name of the node to load the GRES config for.
 * IN gres_list - Node's GRES information as loaded from slurm.conf by slurmd
 * IN xcpuinfo_abs_to_mac - Pointer to xcpuinfo_abs_to_mac() funct. If
 *	specified, Slurm will convert gres_slurmd_conf->cpus_bitmap (a bitmap
 *	derived from gres.conf's "Cores" range string) into machine format
 *	(normal slrumd/stepd operation). If not, it will remain unconverted (for
 *	testing purposes or when unused).
 * IN xcpuinfo_mac_to_abs - Pointer to xcpuinfo_mac_to_abs() funct. Used to
 *	convert CPU affinities from machine format (as collected from NVML and
 *	others) into abstract format, for sanity checking purposes.
 * NOTE: Called from slurmd (and from slurmctld for each cloud node)
 */
extern int gres_g_node_config_load(uint32_t cpu_cnt, char *node_name,
				   List gres_list,
				   void *xcpuinfo_abs_to_mac,
				   void *xcpuinfo_mac_to_abs)
{
	/* Keep options in sync with gres_parse_config_dummy(). */
	static s_p_options_t _gres_conf_options[] = {
		{"AutoDetect", S_P_STRING},
		{"Name",     S_P_ARRAY, _parse_gres_config,  NULL},
		{"NodeName", S_P_ARRAY, _parse_gres_config_node, NULL},
		{NULL}
	};

	int count = 0, i, rc, rc2;
	struct stat config_stat;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t **gres_array;
	char *gres_conf_file;
	char *autodetect_string = NULL;
	bool in_slurmd = running_in_slurmd();

	node_config_load_t node_conf = {
		.cpu_cnt = cpu_cnt,
		.in_slurmd = in_slurmd,
		.xcpuinfo_mac_to_abs = xcpuinfo_mac_to_abs
	};

	if (cpu_cnt == 0) {
		error("%s: Invalid cpu_cnt of 0 for node %s",
		      __func__, node_name);
		return ESLURM_INVALID_CPU_COUNT;
	}

	if (xcpuinfo_abs_to_mac)
		xcpuinfo_ops.xcpuinfo_abs_to_mac = xcpuinfo_abs_to_mac;

	rc = gres_init();

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
		tbl = s_p_hashtbl_create(_gres_conf_options);
		if (s_p_parse_file(tbl, NULL, gres_conf_file, false, NULL) ==
		    SLURM_ERROR)
			fatal("error opening/reading %s", gres_conf_file);

		/* Overwrite unspecified local AutoDetect with global default */
		if (s_p_get_string(&autodetect_string, "Autodetect", tbl)) {
			_handle_global_autodetect(autodetect_string);
			xfree(autodetect_string);
		}

		/* AutoDetect cannot run on the slurmctld node */
		if (running_in_slurmctld() &&
		    autodetect_flags &&
		    !((autodetect_flags & GRES_AUTODETECT_GPU_FLAGS) &
		      GRES_AUTODETECT_GPU_OFF))
			fatal("Cannot use AutoDetect on cloud node \"%s\"",
			      gres_node_name);

		if (s_p_get_array((void ***) &gres_array,
				  &count, "Name", tbl)) {
			for (i = 0; i < count; i++) {
				list_append(gres_conf_list, gres_array[i]);
				gres_array[i] = NULL;
			}
		}
		if (s_p_get_array((void ***) &gres_array,
				  &count, "NodeName", tbl)) {
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
extern int gres_node_config_pack(buf_t *buffer)
{
	int rc;
	uint32_t magic = GRES_MAGIC;
	uint16_t rec_cnt = 0, version = SLURM_PROTOCOL_VERSION;
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;

	rc = gres_init();

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
			pack32(gres_slurmd_conf->config_flags, buffer);
			pack32(gres_slurmd_conf->plugin_id, buffer);
			packstr(gres_slurmd_conf->cpus, buffer);
			packstr(gres_slurmd_conf->links, buffer);
			packstr(gres_slurmd_conf->name, buffer);
			packstr(gres_slurmd_conf->type_name, buffer);
			packstr(gres_slurmd_conf->unique_id, buffer);
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
extern int gres_node_config_unpack(buf_t *buffer, char *node_name)
{
	int i, rc;
	uint32_t cpu_cnt = 0, magic = 0, plugin_id = 0, utmp32 = 0;
	uint64_t count64 = 0;
	uint16_t rec_cnt = 0, protocol_version = 0;
	uint32_t config_flags = 0;
	char *tmp_cpus = NULL, *tmp_links = NULL, *tmp_name = NULL;
	char *tmp_type = NULL;
	char *tmp_unique_id = NULL;
	gres_slurmd_conf_t *p;
	bool locked = false;
	slurm_gres_context_t *gres_ctx;

	rc = gres_init();

	FREE_NULL_LIST(gres_conf_list);
	gres_conf_list = list_create(destroy_gres_slurmd_conf);

	safe_unpack16(&protocol_version, buffer);

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;
	if (rec_cnt > NO_VAL16)
		goto unpack_error;

	slurm_mutex_lock(&gres_context_lock);
	locked = true;
	if (protocol_version < SLURM_MIN_PROTOCOL_VERSION) {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}
	for (i = 0; i < rec_cnt; i++) {
		bool new_has_file;
		bool orig_has_file;
		if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;

			safe_unpack64(&count64, buffer);
			safe_unpack32(&cpu_cnt, buffer);
			safe_unpack32(&config_flags, buffer);
			safe_unpack32(&plugin_id, buffer);
			safe_unpackstr_xmalloc(&tmp_cpus, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_links, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_name, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_type, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_unique_id, &utmp32, buffer);
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			uint8_t tmp_8;
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;

			safe_unpack64(&count64, buffer);
			safe_unpack32(&cpu_cnt, buffer);
			safe_unpack8(&tmp_8, buffer);
			config_flags = tmp_8;
			safe_unpack32(&plugin_id, buffer);
			safe_unpackstr_xmalloc(&tmp_cpus, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_links, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_name, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_type, &utmp32, buffer);
		}

		if (!count64)
			goto empty;

		log_flag(GRES, "Node:%s Gres:%s Type:%s UniqueId:%s Flags:%s CPU_IDs:%s CPU#:%u Count:%"PRIu64" Links:%s",
			 node_name, tmp_name, tmp_type, tmp_unique_id,
			 gres_flags2str(config_flags), tmp_cpus, cpu_cnt,
			 count64, tmp_links);

		if (!(gres_ctx = _find_context_by_id(plugin_id))) {
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
			xfree(tmp_unique_id);
			continue;
		}

		if (xstrcmp(gres_ctx->gres_name, tmp_name)) {
			/*
			 * Should have been caught in
			 * gres_init()
			 */
			error("%s: gres/%s duplicate plugin ID with %s, unable to process",
			      __func__, tmp_name,
			      gres_ctx->gres_name);
			continue;
		}
		new_has_file = config_flags & GRES_CONF_HAS_FILE;
		orig_has_file = gres_ctx->config_flags &
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

		/*
		 * If one node in the bunch said a gres has removed
		 * GRES_CONF_ONE_SHARING then remove it from the
		 * context.
		 */
		if ((gres_ctx->config_flags & GRES_CONF_LOADED) &&
		    gres_id_shared(config_flags))  {
			bool gc_one_sharing =
				gres_ctx->config_flags &
				GRES_CONF_ONE_SHARING;
			bool got_one_sharing =
				config_flags & GRES_CONF_ONE_SHARING;
			if (gc_one_sharing == got_one_sharing) {
			} else if (!gc_one_sharing && got_one_sharing) {
				log_flag(GRES, "gres/%s was already set up to share all ignoring one_sharing from %s",
					 tmp_name, node_name);
				config_flags &= ~GRES_CONF_ONE_SHARING;
			} else if (!got_one_sharing) {
				log_flag(GRES, "gres/%s was already set up to only share one, but we just found the opposite from %s. Removing flag.",
					 tmp_name, node_name);
				gres_ctx->config_flags &=
					~GRES_CONF_ONE_SHARING;
			}
		}

		gres_ctx->config_flags |= config_flags;

		/*
		 * On the slurmctld we need to load the plugins to
		 * correctly set env vars.  We want to call this only
		 * after we have the config_flags so we can tell if we
		 * are CountOnly or not.
		 */
		if (!(gres_ctx->config_flags &
		      GRES_CONF_LOADED)) {
			(void)_load_plugin(gres_ctx);
			gres_ctx->config_flags |=
				GRES_CONF_LOADED;
		}
	empty:
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
		p->unique_id = tmp_unique_id;
		tmp_unique_id = NULL;
		if (gres_links_validate(p->links) < -1) {
			error("%s: Ignoring invalid Links=%s for Name=%s",
			      __func__, p->links, p->name);
			xfree(p->links);
		}
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
	if (locked)
		slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

static void _gres_state_delete_members(void *x)
{
	gres_state_t *gres_ptr = (gres_state_t *) x;

	if (!gres_ptr)
		return;

	xfree(gres_ptr->gres_name);
	xassert(!gres_ptr->gres_data); /* This must be freed beforehand */
	xfree(gres_ptr);
}

static void _gres_node_state_delete_topo(gres_node_state_t *gres_ns)
{
	int i;

	for (i = 0; i < gres_ns->topo_cnt; i++) {
		if (gres_ns->topo_gres_bitmap)
			FREE_NULL_BITMAP(gres_ns->topo_gres_bitmap[i]);
		if (gres_ns->topo_core_bitmap)
			FREE_NULL_BITMAP(gres_ns->topo_core_bitmap[i]);
		xfree(gres_ns->topo_type_name[i]);
	}
	xfree(gres_ns->topo_gres_bitmap);
	xfree(gres_ns->topo_core_bitmap);
	xfree(gres_ns->topo_gres_cnt_alloc);
	xfree(gres_ns->topo_gres_cnt_avail);
	xfree(gres_ns->topo_type_id);
	xfree(gres_ns->topo_type_name);
}

static void _gres_node_state_delete(gres_node_state_t *gres_ns)
{
	int i;

	FREE_NULL_BITMAP(gres_ns->gres_bit_alloc);
	xfree(gres_ns->gres_used);
	if (gres_ns->links_cnt) {
		for (i = 0; i < gres_ns->link_len; i++)
			xfree(gres_ns->links_cnt[i]);
		xfree(gres_ns->links_cnt);
	}

	_gres_node_state_delete_topo(gres_ns);

	for (i = 0; i < gres_ns->type_cnt; i++) {
		xfree(gres_ns->type_name[i]);
	}
	xfree(gres_ns->type_cnt_alloc);
	xfree(gres_ns->type_cnt_avail);
	xfree(gres_ns->type_id);
	xfree(gres_ns->type_name);
	xfree(gres_ns);
}

/*
 * Delete an element placed on gres_list by _node_config_validate()
 * free associated memory
 */
static void _gres_node_list_delete(void *list_element)
{
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;

	gres_state_node = (gres_state_t *) list_element;
	gres_ns = (gres_node_state_t *) gres_state_node->gres_data;
	_gres_node_state_delete(gres_ns);
	gres_state_node->gres_data = NULL;
	_gres_state_delete_members(gres_state_node);
}

extern void gres_add_type(char *type, gres_node_state_t *gres_ns,
			  uint64_t tmp_gres_cnt)
{
	int i;
	uint32_t type_id;

	if (!xstrcasecmp(type, "no_consume")) {
		gres_ns->no_consume = true;
		return;
	}

	type_id = gres_build_id(type);
	for (i = 0; i < gres_ns->type_cnt; i++) {
		if (gres_ns->type_id[i] != type_id)
			continue;
		gres_ns->type_cnt_avail[i] += tmp_gres_cnt;
		break;
	}

	if (i >= gres_ns->type_cnt) {
		gres_ns->type_cnt++;
		gres_ns->type_cnt_alloc =
			xrealloc(gres_ns->type_cnt_alloc,
				 sizeof(uint64_t) * gres_ns->type_cnt);
		gres_ns->type_cnt_avail =
			xrealloc(gres_ns->type_cnt_avail,
				 sizeof(uint64_t) * gres_ns->type_cnt);
		gres_ns->type_id =
			xrealloc(gres_ns->type_id,
				 sizeof(uint32_t) * gres_ns->type_cnt);
		gres_ns->type_name =
			xrealloc(gres_ns->type_name,
				 sizeof(char *) * gres_ns->type_cnt);
		gres_ns->type_cnt_avail[i] += tmp_gres_cnt;
		gres_ns->type_id[i] = type_id;
		gres_ns->type_name[i] = xstrdup(type);
	}
}

/*
 * Compute the total GRES count for a particular gres_name.
 * Note that a given gres_name can appear multiple times in the orig_config
 * string for multiple types (e.g. "gres=gpu:kepler:1,gpu:tesla:2").
 * IN/OUT gres_ns - set gres_cnt_config field in this structure
 * IN orig_config - gres configuration from slurm.conf
 * IN gres_name - name of the gres type (e.g. "gpu")
 * IN gres_name_colon - gres name with appended colon
 * IN gres_name_colon_len - size of gres_name_colon
 * RET - Total configured count for this GRES type
 */
static void _get_gres_cnt(gres_node_state_t *gres_ns, char *orig_config,
			  char *gres_name, char *gres_name_colon,
			  int gres_name_colon_len)
{
	char *node_gres_config, *tok, *last_tok = NULL;
	char *sub_tok, *last_sub_tok = NULL;
	char *num, *paren, *last_num = NULL;
	uint64_t gres_config_cnt = 0, tmp_gres_cnt = 0, mult;
	int i;

	xassert(gres_ns);
	if (orig_config == NULL) {
		gres_ns->gres_cnt_config = 0;
		return;
	}

	for (i = 0; i < gres_ns->type_cnt; i++) {
		gres_ns->type_cnt_avail[i] = 0;
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
				gres_add_type(sub_tok, gres_ns,
					      tmp_gres_cnt);
				sub_tok = strtok_r(NULL, ":", &last_sub_tok);
			}
		}
		tok = strtok_r(NULL, ",", &last_tok);
	}
	xfree(node_gres_config);

	gres_ns->gres_cnt_config = gres_config_cnt;
}

static int _valid_gres_type(char *gres_name, gres_node_state_t *gres_ns,
			    bool config_overrides, char **reason_down)
{
	int i, j;
	uint64_t model_cnt;

	if (gres_ns->type_cnt == 0)
		return 0;

	for (i = 0; i < gres_ns->type_cnt; i++) {
		model_cnt = 0;
		if (gres_ns->type_cnt) {
			for (j = 0; j < gres_ns->type_cnt; j++) {
				if (gres_ns->type_id[i] ==
				    gres_ns->type_id[j])
					model_cnt +=
						gres_ns->type_cnt_avail[j];
			}
		} else {
			for (j = 0; j < gres_ns->topo_cnt; j++) {
				if (gres_ns->type_id[i] ==
				    gres_ns->topo_type_id[j])
					model_cnt += gres_ns->
						topo_gres_cnt_avail[j];
			}
		}
		if (config_overrides) {
			gres_ns->type_cnt_avail[i] = model_cnt;
		} else if (model_cnt < gres_ns->type_cnt_avail[i]) {
			if (reason_down) {
				xstrfmtcat(*reason_down,
					   "%s:%s count too low "
					   "(%"PRIu64" < %"PRIu64")",
					   gres_name, gres_ns->type_name[i],
					   model_cnt,
					   gres_ns->type_cnt_avail[i]);
			}
			return -1;
		}
	}
	return 0;
}

static gres_node_state_t *_build_gres_node_state(void)
{
	gres_node_state_t *gres_ns;

	gres_ns = xmalloc(sizeof(gres_node_state_t));
	gres_ns->gres_cnt_config = NO_VAL64;
	gres_ns->gres_cnt_found  = NO_VAL64;

	return gres_ns;
}

/*
 * Build a node's gres record based only upon the slurm.conf contents
 */
static int _node_config_init(char *orig_config,
			     slurm_gres_context_t *gres_ctx,
			     gres_state_t *gres_state_node)
{
	int rc = SLURM_SUCCESS;
	gres_node_state_t *gres_ns;

	if (!gres_state_node->gres_data)
		gres_state_node->gres_data = _build_gres_node_state();
	gres_ns = (gres_node_state_t *) gres_state_node->gres_data;

	/* If the resource isn't configured for use with this node */
	if ((orig_config == NULL) || (orig_config[0] == '\0')) {
		gres_ns->gres_cnt_config = 0;
		return rc;
	}

	_get_gres_cnt(gres_ns, orig_config,
		      gres_ctx->gres_name,
		      gres_ctx->gres_name_colon,
		      gres_ctx->gres_name_colon_len);

	gres_ctx->total_cnt += gres_ns->gres_cnt_config;

	/* Use count from recovered state, if higher */
	gres_ns->gres_cnt_avail = MAX(gres_ns->gres_cnt_avail,
				      gres_ns->gres_cnt_config);
	if ((gres_ns->gres_bit_alloc != NULL) &&
	    (gres_ns->gres_cnt_avail >
	     bit_size(gres_ns->gres_bit_alloc)) &&
	    !gres_id_shared(gres_ctx->config_flags)) {
		bit_realloc(gres_ns->gres_bit_alloc,
			    gres_ns->gres_cnt_avail);
	}

	return rc;
}

/*
 * Build a node's gres record based only upon the slurm.conf contents
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT gres_list - List of Gres records for this node to track usage
 */
extern int gres_init_node_config(char *orig_config, List *gres_list)
{
	int i, rc, rc2;
	gres_state_t *gres_state_node, *gres_state_node_sharing = NULL,
		*gres_state_node_shared = NULL;

	rc = gres_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_node_list_delete);
	}
	for (i = 0; i < gres_context_cnt; i++) {
		gres_node_state_t *gres_ns;
		/* Find or create gres_state entry on the list */
		gres_state_node = list_find_first(*gres_list, gres_find_id,
						  &gres_context[i].plugin_id);
		if (gres_state_node == NULL) {
			gres_state_node = gres_create_state(
				&gres_context[i], GRES_STATE_SRC_CONTEXT_PTR,
				GRES_STATE_TYPE_NODE, _build_gres_node_state());
			list_append(*gres_list, gres_state_node);
		}

		rc2 = _node_config_init(orig_config,
					&gres_context[i], gres_state_node);
		if (rc == SLURM_SUCCESS)
			rc = rc2;
		gres_ns = gres_state_node->gres_data;
		if (gres_ns && gres_ns->gres_cnt_config) {
			if (gres_id_sharing(gres_state_node->plugin_id))
				gres_state_node_sharing = gres_state_node;
			else if (gres_id_shared(gres_state_node->config_flags))
				gres_state_node_shared = gres_state_node;
		}
	}
	slurm_mutex_unlock(&gres_context_lock);

	/* Set up the shared/sharing pointers for easy look up later */
	if (gres_state_node_shared) {
		if (!gres_state_node_sharing) {
			error("we have a shared gres of '%s' but no gres that is sharing",
			      gres_state_node_shared->gres_name);
		} else {
			gres_node_state_t *gres_ns_shared =
				gres_state_node_shared->gres_data;
			gres_node_state_t *gres_ns_sharing =
				gres_state_node_sharing->gres_data;
			gres_ns_shared->alt_gres_ns = gres_ns_sharing;
			gres_ns_sharing->alt_gres_ns = gres_ns_shared;
		}
	}

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

/* Convert comma-delimited array of link counts to an integer array */
static int _links_str2array(char *links, char *node_name,
			    gres_node_state_t *gres_ns,
			    int gres_inx, int gres_cnt,
			    char **reason_down)
{
	char *start_ptr, *end_ptr = NULL, *tmp = NULL;
	int i = 0, rc = SLURM_SUCCESS;

	if (!links)	/* No "Links=" data */
		return SLURM_SUCCESS;
	if (gres_inx >= gres_ns->link_len) {
		tmp = xstrdup_printf("Invalid GRES index (%d >= %d)",
				     gres_inx, gres_cnt);
		rc = SLURM_ERROR;
		goto end_it;
	}

	start_ptr = links;
	while (1) {
		gres_ns->links_cnt[gres_inx][i] =
			strtol(start_ptr, &end_ptr, 10);
		if (gres_ns->links_cnt[gres_inx][i] < -2) {
			tmp = xstrdup_printf("Invalid GRES Links value (%s) on node %s: Link value '%d' < -2",
					     links, node_name,
					     gres_ns->links_cnt[gres_inx][i]);

			gres_ns->links_cnt[gres_inx][i] = 0;
			rc = SLURM_ERROR;
			goto end_it;
		}
		if (end_ptr[0] == '\0')
			return SLURM_SUCCESS;
		if (end_ptr[0] != ',') {
			tmp = xstrdup_printf("Invalid GRES Links value (%s) on node %s: end_ptr[0]='%c' != ','",
					     links, node_name, end_ptr[0]);
			rc = SLURM_ERROR;
			goto end_it;
		}
		if (++i >= gres_ns->link_len) {
			tmp = xstrdup_printf("Invalid GRES Links value (%s) on node %s: i=%d >= link_len=%d.",
					     links, node_name,
					     i, gres_ns->link_len);
			rc = SLURM_ERROR;
			goto end_it;
		}
		start_ptr = end_ptr + 1;
	}

end_it:
	if (tmp) {
		error("%s: %s If using AutoDetect the amount of GPUs configured in slurm.conf does not match what was detected. If this is intentional, please turn off AutoDetect and manually specify them in gres.conf.",
		      __func__, tmp);
		if (reason_down && !(*reason_down)) {
			*reason_down = tmp;
			tmp = NULL;
		} else
			xfree(tmp);
	}

	return rc;
}

static bool _valid_gres_types(char *gres_name, gres_node_state_t *gres_ns,
			      char **reason_down)
{
	bool rc = true;
	uint64_t gres_cnt_found = 0, gres_sum;
	int topo_inx, type_inx;

	if ((gres_ns->type_cnt == 0) || (gres_ns->topo_cnt == 0))
		return rc;

	for (type_inx = 0; type_inx < gres_ns->type_cnt; type_inx++) {
		gres_cnt_found = 0;
		for (topo_inx = 0; topo_inx < gres_ns->topo_cnt; topo_inx++) {
			if (gres_ns->topo_type_id[topo_inx] !=
			    gres_ns->type_id[type_inx])
				continue;
			gres_sum = gres_cnt_found +
				gres_ns->topo_gres_cnt_avail[topo_inx];
			if (gres_sum > gres_ns->type_cnt_avail[type_inx]) {
				gres_ns->topo_gres_cnt_avail[topo_inx] -=
					(gres_sum -
					 gres_ns->type_cnt_avail[type_inx]);
			}
			gres_cnt_found +=
				gres_ns->topo_gres_cnt_avail[topo_inx];
		}
		if (gres_cnt_found < gres_ns->type_cnt_avail[type_inx]) {
			rc = false;
			break;
		}
	}
	if (!rc && reason_down && (*reason_down == NULL)) {
		xstrfmtcat(*reason_down,
			   "%s:%s count too low (%"PRIu64" < %"PRIu64")",
			   gres_name, gres_ns->type_name[type_inx],
			   gres_cnt_found, gres_ns->type_cnt_avail[type_inx]);
	}

	return rc;
}

static void _gres_bit_alloc_resize(gres_node_state_t *gres_ns,
				   uint64_t gres_bits)
{
	if (!gres_bits) {
		FREE_NULL_BITMAP(gres_ns->gres_bit_alloc);
		return;
	}

	if (!gres_ns->gres_bit_alloc)
		gres_ns->gres_bit_alloc = bit_alloc(gres_bits);
	else if (gres_bits != bit_size(gres_ns->gres_bit_alloc))
		bit_realloc(gres_ns->gres_bit_alloc, gres_bits);
}

static int _node_config_validate(char *node_name, char *orig_config,
				 gres_state_t *gres_state_node,
				 int cpu_cnt, int core_cnt, int sock_cnt,
				 bool config_overrides, char **reason_down,
				 slurm_gres_context_t *gres_ctx)
{
	int cpus_config = 0, i, j, gres_inx, rc = SLURM_SUCCESS;
	int config_type_cnt = 0;
	uint64_t dev_cnt, gres_cnt, topo_cnt = 0;
	bool cpu_config_err = false, updated_config = false;
	gres_node_state_t *gres_ns;
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	bool has_file, has_type, rebuild_topo = false;
	uint32_t type_id;

	xassert(core_cnt);
	if (gres_state_node->gres_data == NULL)
		gres_state_node->gres_data = _build_gres_node_state();
	gres_ns = (gres_node_state_t *) gres_state_node->gres_data;
	if (gres_ns->node_feature)
		return rc;

	/* Make sure these are insync after we get it from the slurmd */
	gres_state_node->config_flags = gres_ctx->config_flags;

	gres_cnt = _get_tot_gres_cnt(gres_ctx->plugin_id, &topo_cnt,
				     &config_type_cnt);
	if (gres_ns->gres_cnt_config > gres_cnt) {
		if (reason_down && (*reason_down == NULL)) {
			xstrfmtcat(*reason_down,
				   "%s count reported lower than configured "
				   "(%"PRIu64" < %"PRIu64")",
				   gres_ctx->gres_type,
				   gres_cnt, gres_ns->gres_cnt_config);
		}
		rc = EINVAL;
	}
	if ((gres_cnt > gres_ns->gres_cnt_config)) {
		debug("%s: %s: Ignoring excess count on node %s (%"
		      PRIu64" > %"PRIu64")",
		      __func__, gres_ctx->gres_type, node_name, gres_cnt,
		      gres_ns->gres_cnt_config);
		gres_cnt = gres_ns->gres_cnt_config;
	}
	if (gres_ns->gres_cnt_found != gres_cnt) {
		if (gres_ns->gres_cnt_found != NO_VAL64) {
			info("%s: %s: Count changed on node %s (%"PRIu64" != %"PRIu64")",
			     __func__, gres_ctx->gres_type, node_name,
			     gres_ns->gres_cnt_found, gres_cnt);
		}
		if ((gres_ns->gres_cnt_found != NO_VAL64) &&
		    (gres_ns->gres_cnt_alloc != 0)) {
			if (reason_down && (*reason_down == NULL)) {
				xstrfmtcat(*reason_down,
					   "%s count changed and jobs are using them "
					   "(%"PRIu64" != %"PRIu64")",
					   gres_ctx->gres_type,
					   gres_ns->gres_cnt_found, gres_cnt);
			}
			rc = EINVAL;
		} else {
			gres_ns->gres_cnt_found = gres_cnt;
			updated_config = true;
		}
	}
	if (!updated_config && gres_ns->type_cnt) {
		/*
		 * This is needed to address the GRES specification in
		 * gres.conf having a Type option, while the GRES specification
		 * in slurm.conf does not.
		 */
		for (i = 0; i < gres_ns->type_cnt; i++) {
			if (gres_ns->type_cnt_avail[i])
				continue;
			updated_config = true;
			break;
		}
	}
	if (!updated_config)
		return rc;
	if ((gres_cnt > gres_ns->gres_cnt_config) && config_overrides) {
		info("%s: %s: count on node %s inconsistent with slurmctld count (%"PRIu64" != %"PRIu64")",
		     __func__, gres_ctx->gres_type, node_name,
		     gres_cnt, gres_ns->gres_cnt_config);
		gres_cnt = gres_ns->gres_cnt_config;	/* Ignore excess GRES */
	}
	if ((topo_cnt == 0) && (topo_cnt != gres_ns->topo_cnt)) {
		/* Need to clear topology info */
		_gres_node_state_delete_topo(gres_ns);

		gres_ns->topo_cnt = topo_cnt;
	}

	has_file = gres_ctx->config_flags & GRES_CONF_HAS_FILE;
	has_type = gres_ctx->config_flags & GRES_CONF_HAS_TYPE;
	if (gres_id_shared(gres_ctx->config_flags))
		dev_cnt = topo_cnt;
	else
		dev_cnt = gres_cnt;
	if (has_file && (topo_cnt != gres_ns->topo_cnt) && (dev_cnt == 0)) {
		/*
		 * Clear any vestigial GRES node state info.
		 */
		_gres_node_state_delete_topo(gres_ns);

		xfree(gres_ns->gres_bit_alloc);

		gres_ns->topo_cnt = 0;
	} else if (has_file && (topo_cnt != gres_ns->topo_cnt)) {
		/*
		 * Need to rebuild topology info.
		 * Resize the data structures here.
		 */
		rebuild_topo = true;
		gres_ns->topo_gres_cnt_alloc =
			xrealloc(gres_ns->topo_gres_cnt_alloc,
				 topo_cnt * sizeof(uint64_t));
		gres_ns->topo_gres_cnt_avail =
			xrealloc(gres_ns->topo_gres_cnt_avail,
				 topo_cnt * sizeof(uint64_t));
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			if (gres_ns->topo_gres_bitmap) {
				FREE_NULL_BITMAP(gres_ns->
						 topo_gres_bitmap[i]);
			}
			if (gres_ns->topo_core_bitmap) {
				FREE_NULL_BITMAP(gres_ns->
						 topo_core_bitmap[i]);
			}
			xfree(gres_ns->topo_type_name[i]);
		}
		gres_ns->topo_gres_bitmap =
			xrealloc(gres_ns->topo_gres_bitmap,
				 topo_cnt * sizeof(bitstr_t *));
		gres_ns->topo_core_bitmap =
			xrealloc(gres_ns->topo_core_bitmap,
				 topo_cnt * sizeof(bitstr_t *));
		gres_ns->topo_type_id = xrealloc(gres_ns->topo_type_id,
						 topo_cnt * sizeof(uint32_t));
		gres_ns->topo_type_name = xrealloc(gres_ns->topo_type_name,
						   topo_cnt * sizeof(char *));
		if (gres_ns->gres_bit_alloc)
			bit_realloc(gres_ns->gres_bit_alloc, dev_cnt);
		gres_ns->topo_cnt = topo_cnt;
	} else if (gres_id_shared(gres_ctx->config_flags) &&
		   gres_ns->topo_cnt) {
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
			    gres_ctx->plugin_id)
				continue;
			if ((gres_ns->gres_bit_alloc) &&
			    !gres_id_shared(gres_ctx->config_flags))
				gres_ns->topo_gres_cnt_alloc[i] = 0;
			gres_ns->topo_gres_cnt_avail[i] =
				gres_slurmd_conf->count;
			if (gres_slurmd_conf->cpus) {
				/* NOTE: gres_slurmd_conf->cpus is cores */
				bitstr_t *tmp_bitmap = bit_alloc(core_cnt);
				int ret = bit_unfmt(tmp_bitmap,
						    gres_slurmd_conf->cpus);
				if (ret != SLURM_SUCCESS) {
					error("%s: %s: invalid GRES core specification (%s) on node %s",
					      __func__, gres_ctx->gres_type,
					      gres_slurmd_conf->cpus,
					      node_name);
					FREE_NULL_BITMAP(tmp_bitmap);
				} else
					gres_ns->topo_core_bitmap[i] =
						tmp_bitmap;
				cpus_config = core_cnt;
			} else if (cpus_config && !cpu_config_err) {
				cpu_config_err = true;
				error("%s: %s: has CPUs configured for only some of the records on node %s",
				      __func__, gres_ctx->gres_type,
				      node_name);
			}

			if (gres_slurmd_conf->links) {
				if (gres_ns->links_cnt &&
				    (gres_ns->link_len != gres_cnt)) {
					/* Size changed, need to rebuild */
					for (j = 0; j < gres_ns->link_len;j++)
						xfree(gres_ns->links_cnt[j]);
					xfree(gres_ns->links_cnt);
				}
				if (!gres_ns->links_cnt) {
					gres_ns->link_len = gres_cnt;
					gres_ns->links_cnt =
						xcalloc(gres_cnt,
							sizeof(int *));
					for (j = 0; j < gres_cnt; j++) {
						gres_ns->links_cnt[j] =
							xcalloc(gres_cnt,
								sizeof(int));
					}
				}
			}
			if (gres_id_shared(gres_slurmd_conf->config_flags)) {
				/* If running jobs recovered then already set */
				if (!gres_ns->topo_gres_bitmap[i]) {
					gres_ns->topo_gres_bitmap[i] =
						bit_alloc(dev_cnt);
					bit_set(gres_ns->topo_gres_bitmap[i],
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
				gres_ns->topo_gres_bitmap[i] =
					bit_alloc(dev_cnt);
				for (j = 0; j < gres_slurmd_conf->count; j++) {
					if (gres_inx >= dev_cnt) {
						/* Ignore excess GRES on node */
						break;
					}
					bit_set(gres_ns->topo_gres_bitmap[i],
						gres_inx);
					if (gres_ns->gres_bit_alloc &&
					    bit_test(gres_ns->gres_bit_alloc,
						     gres_inx)) {
						/* Set by recovered job */
						gres_ns->topo_gres_cnt_alloc[i]++;
					}
					if (_links_str2array(
						    gres_slurmd_conf->links,
						    node_name, gres_ns,
						    gres_inx, gres_cnt,
						    reason_down) !=
					    SLURM_SUCCESS)
						return EINVAL;

					gres_inx++;
				}
			}
			gres_ns->topo_type_id[i] =
				gres_build_id(gres_slurmd_conf->
					      type_name);
			gres_ns->topo_type_name[i] =
				xstrdup(gres_slurmd_conf->type_name);
			i++;
			if (i >= gres_ns->topo_cnt)
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
				    gres_ctx->plugin_id)
					continue;
				for (j = 0; j < i; j++) {
					if (gres_ns->topo_core_bitmap[j])
						continue;
					gres_ns->topo_core_bitmap[j] =
						bit_alloc(core_cnt);
					bit_set_all(gres_ns->
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
			    gres_ctx->plugin_id)
				continue;
			type_id = gres_build_id(
				gres_slurmd_conf->type_name);
			for (i = 0; i < gres_ns->type_cnt; i++) {
				if (type_id == gres_ns->type_id[i])
					break;
			}
			if (i < gres_ns->type_cnt) {
				/* Update count as needed */
				gres_ns->type_cnt_avail[i] =
					gres_slurmd_conf->count;
			} else {
				gres_add_type(gres_slurmd_conf->type_name,
					      gres_ns,
					      gres_slurmd_conf->count);
			}

		}
		list_iterator_destroy(iter);
	}

	if ((orig_config == NULL) || (orig_config[0] == '\0'))
		gres_ns->gres_cnt_config = 0;
	else if (gres_ns->gres_cnt_config == NO_VAL64) {
		/* This should have been filled in by _node_config_init() */
		_get_gres_cnt(gres_ns, orig_config,
			      gres_ctx->gres_name,
			      gres_ctx->gres_name_colon,
			      gres_ctx->gres_name_colon_len);
	}

	gres_ns->gres_cnt_avail = gres_ns->gres_cnt_config;

	if (has_file) {
		uint64_t gres_bits;
		if (gres_id_shared(gres_ctx->config_flags)) {
			gres_bits = topo_cnt;
		} else {
			if (gres_ns->gres_cnt_avail > MAX_GRES_BITMAP) {
				error("%s: %s has \"File\" plus very large \"Count\" "
				      "(%"PRIu64") for node %s, resetting value to %u",
				      __func__, gres_ctx->gres_type,
				      gres_ns->gres_cnt_avail, node_name,
				      MAX_GRES_BITMAP);
				gres_ns->gres_cnt_avail = MAX_GRES_BITMAP;
				gres_ns->gres_cnt_found = MAX_GRES_BITMAP;
			}
			gres_bits = gres_ns->gres_cnt_avail;
		}

		_gres_bit_alloc_resize(gres_ns, gres_bits);
	}

	if ((config_type_cnt > 1) &&
	    !_valid_gres_types(gres_ctx->gres_type, gres_ns, reason_down)){
		rc = EINVAL;
	} else if (!config_overrides &&
		   (gres_ns->gres_cnt_found < gres_ns->gres_cnt_config)) {
		if (reason_down && (*reason_down == NULL)) {
			xstrfmtcat(*reason_down,
				   "%s count too low (%"PRIu64" < %"PRIu64")",
				   gres_ctx->gres_type,
				   gres_ns->gres_cnt_found,
				   gres_ns->gres_cnt_config);
		}
		rc = EINVAL;
	} else if (_valid_gres_type(gres_ctx->gres_type, gres_ns,
				    config_overrides, reason_down)) {
		rc = EINVAL;
	} else if (config_overrides && gres_ns->topo_cnt &&
		   (gres_ns->gres_cnt_found != gres_ns->gres_cnt_config)) {
		error("%s on node %s configured for %"PRIu64" resources but "
		      "%"PRIu64" found, ignoring topology support",
		      gres_ctx->gres_type, node_name,
		      gres_ns->gres_cnt_config, gres_ns->gres_cnt_found);
		if (gres_ns->topo_core_bitmap) {
			for (i = 0; i < gres_ns->topo_cnt; i++) {
				if (gres_ns->topo_core_bitmap) {
					FREE_NULL_BITMAP(gres_ns->
							 topo_core_bitmap[i]);
				}
				if (gres_ns->topo_gres_bitmap) {
					FREE_NULL_BITMAP(gres_ns->
							 topo_gres_bitmap[i]);
				}
				xfree(gres_ns->topo_type_name[i]);
			}
			xfree(gres_ns->topo_core_bitmap);
			xfree(gres_ns->topo_gres_bitmap);
			xfree(gres_ns->topo_gres_cnt_alloc);
			xfree(gres_ns->topo_gres_cnt_avail);
			xfree(gres_ns->topo_type_id);
			xfree(gres_ns->topo_type_name);
		}
		gres_ns->topo_cnt = 0;
	}

	return rc;
}

/* The GPU count on a node changed. Update SHARED data structures to match */
static void _sync_node_shared_to_sharing(gres_state_t *sharing_gres_state_node)
{
	gres_node_state_t *sharing_gres_ns, *shared_gres_ns;
	uint64_t sharing_cnt, shared_alloc = 0, shared_rem;
	int i;

	if (!sharing_gres_state_node)
		return;

	sharing_gres_ns = sharing_gres_state_node->gres_data;
	shared_gres_ns = sharing_gres_ns->alt_gres_ns;

	if (!shared_gres_ns)
		return;

	sharing_cnt = sharing_gres_ns->gres_cnt_avail;
	if (shared_gres_ns->gres_bit_alloc) {
		if (sharing_cnt == bit_size(shared_gres_ns->gres_bit_alloc))
			return;		/* No change for gres/'shared' */
	}

	if (sharing_cnt == 0)
		return;			/* Still no SHARINGs */

	/* Free any excess gres/'shared' topo records */
	for (i = sharing_cnt; i < shared_gres_ns->topo_cnt; i++) {
		if (shared_gres_ns->topo_core_bitmap)
			FREE_NULL_BITMAP(shared_gres_ns->topo_core_bitmap[i]);
		if (shared_gres_ns->topo_gres_bitmap)
			FREE_NULL_BITMAP(shared_gres_ns->topo_gres_bitmap[i]);
		xfree(shared_gres_ns->topo_type_name[i]);
	}

	if (shared_gres_ns->gres_cnt_avail == 0) {
		/* No gres/'shared' on this node */
		shared_gres_ns->topo_cnt = 0;
		return;
	}

	if (!shared_gres_ns->gres_bit_alloc) {
		shared_gres_ns->gres_bit_alloc = bit_alloc(sharing_cnt);
	} else {
		bit_realloc(shared_gres_ns->gres_bit_alloc, sharing_cnt);
	}

	/* Add any additional required gres/'shared' topo records */
	if (shared_gres_ns->topo_cnt) {
		shared_gres_ns->topo_core_bitmap =
			xrealloc(shared_gres_ns->topo_core_bitmap,
				 sizeof(bitstr_t *) * sharing_cnt);
		shared_gres_ns->topo_gres_bitmap =
			xrealloc(shared_gres_ns->topo_gres_bitmap,
				 sizeof(bitstr_t *) * sharing_cnt);
		shared_gres_ns->topo_gres_cnt_alloc =
			xrealloc(shared_gres_ns->topo_gres_cnt_alloc,
				 sizeof(uint64_t) * sharing_cnt);
		shared_gres_ns->topo_gres_cnt_avail =
			xrealloc(shared_gres_ns->topo_gres_cnt_avail,
				 sizeof(uint64_t) * sharing_cnt);
		shared_gres_ns->topo_type_id =
			xrealloc(shared_gres_ns->topo_type_id,
				 sizeof(uint32_t) * sharing_cnt);
		shared_gres_ns->topo_type_name =
			xrealloc(shared_gres_ns->topo_type_name,
				 sizeof(char *) * sharing_cnt);
	} else {
		shared_gres_ns->topo_core_bitmap =
			xcalloc(sharing_cnt, sizeof(bitstr_t *));
		shared_gres_ns->topo_gres_bitmap =
			xcalloc(sharing_cnt, sizeof(bitstr_t *));
		shared_gres_ns->topo_gres_cnt_alloc =
			xcalloc(sharing_cnt, sizeof(uint64_t));
		shared_gres_ns->topo_gres_cnt_avail =
			xcalloc(sharing_cnt, sizeof(uint64_t));
		shared_gres_ns->topo_type_id =
			xcalloc(sharing_cnt, sizeof(uint32_t));
		shared_gres_ns->topo_type_name =
			xcalloc(sharing_cnt, sizeof(char *));
	}

	/*
	 * Evenly distribute any remaining SHARED counts.
	 * Counts get reset as needed when the node registers.
	 */
	for (i = 0; i < shared_gres_ns->topo_cnt; i++)
		shared_alloc += shared_gres_ns->topo_gres_cnt_avail[i];
	if (shared_alloc >= shared_gres_ns->gres_cnt_avail)
		shared_rem = 0;
	else
		shared_rem = shared_gres_ns->gres_cnt_avail - shared_alloc;
	for (i = shared_gres_ns->topo_cnt; i < sharing_cnt; i++) {
		shared_gres_ns->topo_gres_bitmap[i] = bit_alloc(sharing_cnt);
		bit_set(shared_gres_ns->topo_gres_bitmap[i], i);
		shared_alloc = shared_rem / (sharing_cnt - i);
		shared_gres_ns->topo_gres_cnt_avail[i] = shared_alloc;
		shared_rem -= shared_alloc;
	}
	shared_gres_ns->topo_cnt = sharing_cnt;

	for (i = 0; i < shared_gres_ns->topo_cnt; i++) {
		if (shared_gres_ns->topo_gres_bitmap &&
		    shared_gres_ns->topo_gres_bitmap[i] &&
		    (sharing_cnt !=
		     bit_size(shared_gres_ns->topo_gres_bitmap[i]))) {
			bit_realloc(shared_gres_ns->topo_gres_bitmap[i],
				    sharing_cnt);
		}
	}
}

/*
 * Validate a node's configuration and put a gres record onto a list
 * Called immediately after gres_node_config_unpack().
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from merged slurm.conf/gres.conf
 * IN/OUT new_config - Updated gres info from slurm.conf
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN threads_per_core - Count of CPUs (threads) per core on this node
 * IN cores_per_sock - Count of cores per socket on this node
 * IN sock_cnt - Count of sockets on this node
 * IN config_overrides - true: Don't validate hardware, use slurm.conf
 *                             configuration
 *			 false: Validate hardware config, but use slurm.conf
 *                              config
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int gres_node_config_validate(char *node_name,
				     char *orig_config,
				     char **new_config,
				     List *gres_list,
				     int threads_per_core,
				     int cores_per_sock, int sock_cnt,
				     bool config_overrides,
				     char **reason_down)
{
	int i, rc, rc2;
	gres_state_t *gres_state_node, *gres_gpu_ptr = NULL;
	int core_cnt = sock_cnt * cores_per_sock;
	int cpu_cnt  = core_cnt * threads_per_core;

	rc = gres_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL))
		*gres_list = list_create(_gres_node_list_delete);
	for (i = 0; i < gres_context_cnt; i++) {
		/* Find or create gres_state entry on the list */
		gres_state_node = list_find_first(*gres_list, gres_find_id,
						  &gres_context[i].plugin_id);
		if (gres_state_node == NULL) {
			gres_state_node = gres_create_state(
				&gres_context[i], GRES_STATE_SRC_CONTEXT_PTR,
				GRES_STATE_TYPE_NODE, _build_gres_node_state());
			list_append(*gres_list, gres_state_node);
		}
		rc2 = _node_config_validate(node_name, orig_config,
					    gres_state_node, cpu_cnt, core_cnt,
					    sock_cnt, config_overrides,
					    reason_down, &gres_context[i]);
		rc = MAX(rc, rc2);
		if (gres_id_sharing(gres_state_node->plugin_id))
			gres_gpu_ptr = gres_state_node;
	}
	_sync_node_shared_to_sharing(gres_gpu_ptr);
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
extern void gres_node_feature(char *node_name,
			      char *gres_name, uint64_t gres_size,
			      char **new_config, List *gres_list)
{
	char *new_gres = NULL, *tok, *save_ptr = NULL, *sep = "", *suffix = "";
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;
	uint32_t plugin_id;
	uint64_t gres_scaled = 0;
	int gres_name_len;

	xassert(gres_name);
	gres_name_len = strlen(gres_name);
	plugin_id = gres_build_id(gres_name);
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
		gres_state_node = list_find_first(*gres_list, gres_find_id,
						  &plugin_id);
		if (gres_state_node == NULL) {
			gres_state_node = xmalloc(sizeof(gres_state_t));
			/* FIXME: no config_flags known at this moment */
			/* gres_state_node->config_flags = ; */
			gres_state_node->plugin_id = plugin_id;
			gres_state_node->gres_data = _build_gres_node_state();
			gres_state_node->gres_name = xstrdup(gres_name);
			gres_state_node->state_type = GRES_STATE_TYPE_NODE;
			list_append(*gres_list, gres_state_node);
		}
		gres_ns = gres_state_node->gres_data;
		if (gres_size >= gres_ns->gres_cnt_alloc) {
			gres_ns->gres_cnt_avail = gres_size -
				gres_ns->gres_cnt_alloc;
		} else {
			error("%s: Changed size count of GRES %s from %"PRIu64
			      " to %"PRIu64", resource over allocated",
			      __func__, gres_name,
			      gres_ns->gres_cnt_avail, gres_size);
			gres_ns->gres_cnt_avail = 0;
		}
		gres_ns->gres_cnt_config = gres_size;
		gres_ns->gres_cnt_found = gres_size;
		gres_ns->node_feature = true;
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
			       gres_state_t *gres_state_node,
			       slurm_gres_context_t *gres_ctx)
{
	gres_node_state_t *orig_gres_ns, *new_gres_ns;
	int rc = SLURM_SUCCESS;

	xassert(gres_state_node);
	if (!(gres_ctx->config_flags & GRES_CONF_HAS_FILE))
		return SLURM_SUCCESS;

	orig_gres_ns = gres_state_node->gres_data;
	new_gres_ns = _build_gres_node_state();
	_get_gres_cnt(new_gres_ns, new_gres,
		      gres_ctx->gres_name,
		      gres_ctx->gres_name_colon,
		      gres_ctx->gres_name_colon_len);
	if ((new_gres_ns->gres_cnt_config != 0) &&
	    (new_gres_ns->gres_cnt_config !=
	     orig_gres_ns->gres_cnt_config)) {
		error("Attempt to change gres/%s Count on node %s from %"
		      PRIu64" to %"PRIu64" invalid with File configuration",
		      gres_ctx->gres_name, node_name,
		      orig_gres_ns->gres_cnt_config,
		      new_gres_ns->gres_cnt_config);
		rc = ESLURM_INVALID_GRES;
	}
	_gres_node_state_delete(new_gres_ns);

	return rc;
}

static int _node_reconfig(char *node_name, char *new_gres, char **gres_str,
			  gres_state_t *gres_state_node, bool config_overrides,
			  slurm_gres_context_t *gres_ctx,
			  bool *updated_gpu_cnt)
{
	int i;
	gres_node_state_t *gres_ns;
	uint64_t gres_bits, orig_cnt;

	xassert(gres_state_node);
	xassert(updated_gpu_cnt);
	*updated_gpu_cnt = false;
	if (gres_state_node->gres_data == NULL)
		gres_state_node->gres_data = _build_gres_node_state();
	gres_ns = gres_state_node->gres_data;
	orig_cnt = gres_ns->gres_cnt_config;

	_get_gres_cnt(gres_ns, new_gres,
		      gres_ctx->gres_name,
		      gres_ctx->gres_name_colon,
		      gres_ctx->gres_name_colon_len);

	if (gres_ns->gres_cnt_config == orig_cnt)
		return SLURM_SUCCESS;	/* No change in count */

	/* Update count */
	gres_ctx->total_cnt -= orig_cnt;
	gres_ctx->total_cnt += gres_ns->gres_cnt_config;

	gres_ns->gres_cnt_avail = gres_ns->gres_cnt_config;

	if (gres_ctx->config_flags & GRES_CONF_HAS_FILE) {
		if (gres_id_shared(gres_ctx->config_flags))
			gres_bits = gres_ns->topo_cnt;
		else
			gres_bits = gres_ns->gres_cnt_avail;

		_gres_bit_alloc_resize(gres_ns, gres_bits);
	} else if (gres_ns->gres_bit_alloc &&
		   !gres_id_shared(gres_ctx->config_flags)) {
		/*
		 * If GRES count changed in configuration between reboots,
		 * update bitmap sizes as needed.
		 */
		gres_bits = gres_ns->gres_cnt_avail;
		if (gres_bits != bit_size(gres_ns->gres_bit_alloc)) {
			info("gres/%s count changed on node %s to %"PRIu64,
			     gres_ctx->gres_name, node_name, gres_bits);
			if (gres_id_sharing(gres_ctx->plugin_id))
				*updated_gpu_cnt = true;
			bit_realloc(gres_ns->gres_bit_alloc, gres_bits);
			for (i = 0; i < gres_ns->topo_cnt; i++) {
				if (gres_ns->topo_gres_bitmap &&
				    gres_ns->topo_gres_bitmap[i] &&
				    (gres_bits !=
				     bit_size(gres_ns->topo_gres_bitmap[i]))){
					bit_realloc(gres_ns->topo_gres_bitmap[i],
						    gres_bits);
				}
			}
		}
	}

	return SLURM_SUCCESS;
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
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;
	bitstr_t *done_topo, *core_map;
	uint64_t gres_sum;
	char *sep = "", *suffix, *sock_info = NULL, *sock_str;
	int c, i, j;

	xassert(gres_str);
	xfree(*gres_str);
	for (c = 0; c < gres_context_cnt; c++) {
		/* Find gres_state entry on the list */
		gres_state_node = list_find_first(*gres_list, gres_find_id,
						  &gres_context[c].plugin_id);
		if (gres_state_node == NULL)
			continue;	/* Node has none of this GRES */

		gres_ns = (gres_node_state_t *) gres_state_node->gres_data;
		if (gres_ns->topo_cnt &&
		    gres_ns->gres_cnt_avail) {
			done_topo = bit_alloc(gres_ns->topo_cnt);
			for (i = 0; i < gres_ns->topo_cnt; i++) {
				if (bit_test(done_topo, i))
					continue;
				bit_set(done_topo, i);
				gres_sum = gres_ns->
					topo_gres_cnt_avail[i];
				if (gres_ns->topo_core_bitmap[i]) {
					core_map = bit_copy(
						gres_ns->
						topo_core_bitmap[i]);
				} else
					core_map = NULL;
				for (j = 0; j < gres_ns->topo_cnt; j++){
					if (gres_ns->topo_type_id[i] !=
					    gres_ns->topo_type_id[j])
						continue;
					if (bit_test(done_topo, j))
						continue;
					bit_set(done_topo, j);
					gres_sum += gres_ns->
						topo_gres_cnt_avail[j];
					if (core_map &&
					    gres_ns->
					    topo_core_bitmap[j]) {
						bit_or(core_map,
						       gres_ns->
						       topo_core_bitmap[j]);
					} else if (gres_ns->
						   topo_core_bitmap[j]) {
						core_map = bit_copy(
							gres_ns->
							topo_core_bitmap[j]);
					}
				}
				if (core_map) {
					sock_info = _core_bitmap2str(
						core_map,
						cores_per_sock,
						sock_per_node);
					bit_free(core_map);
					sock_str = sock_info;
				} else
					sock_str = "";
				suffix = _get_suffix(&gres_sum);
				if (gres_ns->topo_type_name[i]) {
					xstrfmtcat(*gres_str,
						   "%s%s:%s:%"PRIu64"%s%s", sep,
						   gres_context[c].gres_name,
						   gres_ns->
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
		} else if (gres_ns->type_cnt &&
			   gres_ns->gres_cnt_avail) {
			for (i = 0; i < gres_ns->type_cnt; i++) {
				gres_sum = gres_ns->type_cnt_avail[i];
				suffix = _get_suffix(&gres_sum);
				xstrfmtcat(*gres_str, "%s%s:%s:%"PRIu64"%s",
					   sep, gres_context[c].gres_name,
					   gres_ns->type_name[i],
					   gres_sum, suffix);
				sep = ",";
			}
		} else if (gres_ns->gres_cnt_avail) {
			gres_sum = gres_ns->gres_cnt_avail;
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
 *			 false: Validate hardware config, but use slurm.conf
 *                              config
 * IN cores_per_sock - Number of cores per socket on this node
 * IN sock_per_node - Total count of sockets on this node (on any board)
 */
extern int gres_node_reconfig(char *node_name,
			      char *new_gres,
			      char **gres_str,
			      List *gres_list,
			      bool config_overrides,
			      int cores_per_sock,
			      int sock_per_node)
{
	int i, rc;
	gres_state_t *gres_state_node = NULL, **gres_state_node_array;
	gres_state_t *gpu_gres_state_node = NULL;

	rc = gres_init();
	slurm_mutex_lock(&gres_context_lock);
	gres_state_node_array = xcalloc(gres_context_cnt,
					sizeof(gres_state_t *));
	if ((gres_context_cnt > 0) && (*gres_list == NULL))
		*gres_list = list_create(_gres_node_list_delete);

	/* First validate all of the requested GRES changes */
	for (i = 0; (rc == SLURM_SUCCESS) && (i < gres_context_cnt); i++) {
		/* Find gres_state entry on the list */
		gres_state_node = list_find_first(*gres_list, gres_find_id,
						  &gres_context[i].plugin_id);
		if (gres_state_node == NULL)
			continue;
		gres_state_node_array[i] = gres_state_node;
		rc = _node_reconfig_test(node_name, new_gres, gres_state_node,
					 &gres_context[i]);
	}

	/* Now update the GRES counts */
	for (i = 0; (rc == SLURM_SUCCESS) && (i < gres_context_cnt); i++) {
		bool updated_gpu_cnt = false;
		if (gres_state_node_array[i] == NULL)
			continue;
		rc = _node_reconfig(node_name, new_gres, gres_str,
				    gres_state_node_array[i], config_overrides,
				    &gres_context[i], &updated_gpu_cnt);
		if (updated_gpu_cnt)
			gpu_gres_state_node = gres_state_node;
	}

	/* Now synchronize gres/gpu and gres/'shared' state */
	if (gpu_gres_state_node) {
		/* Update gres/'shared' counts and bitmaps to match gres/gpu */
		_sync_node_shared_to_sharing(gpu_gres_state_node);
	}

	/* Build new per-node gres_str */
	_build_node_gres_str(gres_list, gres_str, cores_per_sock,sock_per_node);
	slurm_mutex_unlock(&gres_context_lock);
	xfree(gres_state_node_array);

	return rc;
}

/*
 * Pack a node's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_node_config_validate()
 * IN/OUT buffer - location to write state to
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_node_state_pack(List gres_list, buf_t *buffer,
				char *node_name)
{
	int rc = SLURM_SUCCESS;
	uint32_t top_offset, tail_offset;
	uint32_t magic = GRES_MAGIC;
	uint16_t gres_bitmap_size, rec_cnt = 0;
	ListIterator gres_iter;
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;

	if (gres_list == NULL) {
		pack16(rec_cnt, buffer);
		return rc;
	}

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_node = (gres_state_t *) list_next(gres_iter))) {
		gres_ns = (gres_node_state_t *) gres_state_node->gres_data;
		pack32(magic, buffer);
		pack32(gres_state_node->plugin_id, buffer);
		pack64(gres_ns->gres_cnt_avail, buffer);
		/*
		 * Just note if gres_bit_alloc exists.
		 * Rebuild it based upon the state of recovered jobs
		 */
		if (gres_ns->gres_bit_alloc)
			gres_bitmap_size = bit_size(gres_ns->gres_bit_alloc);
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
 * OUT gres_list - restored state stored by gres_node_state_pack()
 * IN/OUT buffer - location to read state from
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_node_state_unpack(List *gres_list, buf_t *buffer,
				  char *node_name,
				  uint16_t protocol_version)
{
	int rc;
	uint32_t magic = 0, plugin_id = 0;
	uint64_t gres_cnt_avail = 0;
	uint16_t gres_bitmap_size = 0, rec_cnt = 0;
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;
	bool locked = false;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	rc = gres_init();

	slurm_mutex_lock(&gres_context_lock);
	locked = true;
	if ((gres_context_cnt > 0) && (*gres_list == NULL))
		*gres_list = list_create(_gres_node_list_delete);

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		slurm_gres_context_t *gres_ctx;
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

		if (!(gres_ctx = _find_context_by_id(plugin_id))) {
			error("%s: no plugin configured to unpack data type %u from node %s",
			      __func__, plugin_id, node_name);
			/*
			 * A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data.
			 */
			continue;
		}
		gres_ns = _build_gres_node_state();
		gres_ns->gres_cnt_avail = gres_cnt_avail;
		if (gres_bitmap_size) {
			gres_ns->gres_bit_alloc =
				bit_alloc(gres_bitmap_size);
		}

		gres_state_node = gres_create_state(
			gres_ctx, GRES_STATE_SRC_CONTEXT_PTR,
			GRES_STATE_TYPE_NODE, gres_ns);
		list_append(*gres_list, gres_state_node);
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error from node %s", __func__, node_name);
	if (locked)
		slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

static void *_node_state_dup(gres_node_state_t *gres_ns)
{
	int i, j;
	gres_node_state_t *new_gres_ns;

	if (gres_ns == NULL)
		return NULL;

	new_gres_ns = xmalloc(sizeof(gres_node_state_t));
	new_gres_ns->gres_cnt_found  = gres_ns->gres_cnt_found;
	new_gres_ns->gres_cnt_config = gres_ns->gres_cnt_config;
	new_gres_ns->gres_cnt_avail  = gres_ns->gres_cnt_avail;
	new_gres_ns->gres_cnt_alloc  = gres_ns->gres_cnt_alloc;
	new_gres_ns->no_consume      = gres_ns->no_consume;
	if (gres_ns->gres_bit_alloc)
		new_gres_ns->gres_bit_alloc = bit_copy(gres_ns->gres_bit_alloc);

	if (gres_ns->links_cnt && gres_ns->link_len) {
		new_gres_ns->links_cnt = xcalloc(gres_ns->link_len,
						 sizeof(int *));
		j = sizeof(int) * gres_ns->link_len;
		for (i = 0; i < gres_ns->link_len; i++) {
			new_gres_ns->links_cnt[i] = xmalloc(j);
			memcpy(new_gres_ns->links_cnt[i],
			       gres_ns->links_cnt[i], j);
		}
		new_gres_ns->link_len = gres_ns->link_len;
	}

	if (gres_ns->topo_cnt) {
		new_gres_ns->topo_cnt         = gres_ns->topo_cnt;
		new_gres_ns->topo_core_bitmap = xcalloc(gres_ns->topo_cnt,
							sizeof(bitstr_t *));
		new_gres_ns->topo_gres_bitmap = xcalloc(gres_ns->topo_cnt,
							sizeof(bitstr_t *));
		new_gres_ns->topo_gres_cnt_alloc = xcalloc(gres_ns->topo_cnt,
							   sizeof(uint64_t));
		new_gres_ns->topo_gres_cnt_avail = xcalloc(gres_ns->topo_cnt,
							   sizeof(uint64_t));
		new_gres_ns->topo_type_id = xcalloc(gres_ns->topo_cnt,
						    sizeof(uint32_t));
		new_gres_ns->topo_type_name = xcalloc(gres_ns->topo_cnt,
						      sizeof(char *));
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			if (gres_ns->topo_core_bitmap[i]) {
				new_gres_ns->topo_core_bitmap[i] =
					bit_copy(gres_ns->topo_core_bitmap[i]);
			}
			new_gres_ns->topo_gres_bitmap[i] =
				bit_copy(gres_ns->topo_gres_bitmap[i]);
			new_gres_ns->topo_gres_cnt_alloc[i] =
				gres_ns->topo_gres_cnt_alloc[i];
			new_gres_ns->topo_gres_cnt_avail[i] =
				gres_ns->topo_gres_cnt_avail[i];
			new_gres_ns->topo_type_id[i] = gres_ns->topo_type_id[i];
			new_gres_ns->topo_type_name[i] =
				xstrdup(gres_ns->topo_type_name[i]);
		}
	}

	if (gres_ns->type_cnt) {
		new_gres_ns->type_cnt       = gres_ns->type_cnt;
		new_gres_ns->type_cnt_alloc = xcalloc(gres_ns->type_cnt,
						      sizeof(uint64_t));
		new_gres_ns->type_cnt_avail = xcalloc(gres_ns->type_cnt,
						      sizeof(uint64_t));
		new_gres_ns->type_id = xcalloc(gres_ns->type_cnt,
					       sizeof(uint32_t));
		new_gres_ns->type_name = xcalloc(gres_ns->type_cnt,
						 sizeof(char *));
		for (i = 0; i < gres_ns->type_cnt; i++) {
			new_gres_ns->type_cnt_alloc[i] =
				gres_ns->type_cnt_alloc[i];
			new_gres_ns->type_cnt_avail[i] =
				gres_ns->type_cnt_avail[i];
			new_gres_ns->type_id[i] = gres_ns->type_id[i];
			new_gres_ns->type_name[i] =
				xstrdup(gres_ns->type_name[i]);
		}
	}

	return new_gres_ns;
}

/*
 * Duplicate a node gres status (used for will-run logic)
 * IN gres_list - node gres state information
 * RET a copy of gres_list or NULL on failure
 */
extern List gres_node_state_list_dup(List gres_list)
{
	List new_list = NULL;
	ListIterator gres_iter;
	gres_state_t *gres_state_node, *new_gres;
	void *gres_ns;

	if (gres_list == NULL)
		return new_list;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0)) {
		new_list = list_create(_gres_node_list_delete);
	}
	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_node = (gres_state_t *) list_next(gres_iter))) {
		if (!_find_context_by_id(gres_state_node->plugin_id)) {
			error("Could not find plugin id %u to dup node record",
			      gres_state_node->plugin_id);
			continue;
		}

		gres_ns = _node_state_dup(gres_state_node->gres_data);
		if (gres_ns) {
			new_gres = gres_create_state(
				gres_state_node, GRES_STATE_SRC_STATE_PTR,
				GRES_STATE_TYPE_NODE, gres_ns);
			list_append(new_list, new_gres);
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return new_list;
}

static void _node_state_dealloc(gres_state_t *gres_state_node)
{
	int i;
	gres_node_state_t *gres_ns;
	char *gres_name = NULL;

	gres_ns = (gres_node_state_t *) gres_state_node->gres_data;
	gres_ns->gres_cnt_alloc = 0;
	if (gres_ns->gres_bit_alloc) {
		int i = bit_size(gres_ns->gres_bit_alloc) - 1;
		if (i >= 0)
			bit_nclear(gres_ns->gres_bit_alloc, 0, i);
	}

	if (gres_ns->topo_cnt && !gres_ns->topo_gres_cnt_alloc) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_state_node->plugin_id == gres_context[i].plugin_id) {
				gres_name = gres_context[i].gres_name;
				break;
			}
		}
		error("gres_node_state_dealloc_all: gres/%s topo_cnt!=0 "
		      "and topo_gres_cnt_alloc is NULL", gres_name);
	} else if (gres_ns->topo_cnt) {
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			gres_ns->topo_gres_cnt_alloc[i] = 0;
		}
	} else {
		/*
		 * This array can be set at startup if a job has been allocated
		 * specific GRES and the node has not registered with the
		 * details needed to track individual GRES (rather than only
		 * a GRES count).
		 */
		xfree(gres_ns->topo_gres_cnt_alloc);
	}

	for (i = 0; i < gres_ns->type_cnt; i++) {
		gres_ns->type_cnt_alloc[i] = 0;
	}
}

/*
 * Deallocate all resources on this node previous allocated to any jobs.
 *	This function isused to synchronize state after slurmctld restarts or
 *	is reconfigured.
 * IN gres_list - node gres state information
 */
extern void gres_node_state_dealloc_all(List gres_list)
{
	ListIterator gres_iter;
	gres_state_t *gres_state_node;

	if (gres_list == NULL)
		return;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_node = (gres_state_t *) list_next(gres_iter))) {
		_node_state_dealloc(gres_state_node);
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

static char *_node_gres_used(gres_node_state_t *gres_ns, char *gres_name)
{
	char *sep = "";
	int i, j;

	xassert(gres_ns);

	if ((gres_ns->topo_cnt != 0) &&
	    (gres_ns->no_consume == false)) {
		bitstr_t *topo_printed = bit_alloc(gres_ns->topo_cnt);
		xfree(gres_ns->gres_used);    /* Free any cached value */
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			bitstr_t *topo_gres_bitmap = NULL;
			uint64_t gres_alloc_cnt = 0;
			char *gres_alloc_idx, tmp_str[64];
			if (bit_test(topo_printed, i))
				continue;
			bit_set(topo_printed, i);
			if (gres_ns->topo_gres_bitmap[i]) {
				topo_gres_bitmap =
					bit_copy(gres_ns->
						 topo_gres_bitmap[i]);
			}
			for (j = i + 1; j < gres_ns->topo_cnt; j++) {
				if (bit_test(topo_printed, j))
					continue;
				if (gres_ns->topo_type_id[i] !=
				    gres_ns->topo_type_id[j])
					continue;
				bit_set(topo_printed, j);
				if (gres_ns->topo_gres_bitmap[j]) {
					if (!topo_gres_bitmap) {
						topo_gres_bitmap =
							bit_copy(gres_ns->
								 topo_gres_bitmap[j]);
					} else if (bit_size(topo_gres_bitmap) ==
						   bit_size(gres_ns->
							    topo_gres_bitmap[j])){
						bit_or(topo_gres_bitmap,
						       gres_ns->
						       topo_gres_bitmap[j]);
					}
				}
			}
			if (gres_ns->gres_bit_alloc && topo_gres_bitmap &&
			    (bit_size(topo_gres_bitmap) ==
			     bit_size(gres_ns->gres_bit_alloc))) {
				bit_and(topo_gres_bitmap,
					gres_ns->gres_bit_alloc);
				gres_alloc_cnt = bit_set_count(topo_gres_bitmap);
			}
			if (gres_alloc_cnt > 0) {
				bit_fmt(tmp_str, sizeof(tmp_str),
					topo_gres_bitmap);
				gres_alloc_idx = tmp_str;
			} else {
				gres_alloc_idx = "N/A";
			}
			xstrfmtcat(gres_ns->gres_used,
				   "%s%s:%s:%"PRIu64"(IDX:%s)", sep, gres_name,
				   gres_ns->topo_type_name[i],
				   gres_alloc_cnt, gres_alloc_idx);
			sep = ",";
			FREE_NULL_BITMAP(topo_gres_bitmap);
		}
		FREE_NULL_BITMAP(topo_printed);
	} else if (gres_ns->gres_used) {
		;	/* Used cached value */
	} else if (gres_ns->type_cnt == 0) {
		if (gres_ns->no_consume) {
			xstrfmtcat(gres_ns->gres_used, "%s:0", gres_name);
		} else {
			xstrfmtcat(gres_ns->gres_used, "%s:%"PRIu64,
				   gres_name, gres_ns->gres_cnt_alloc);
		}
	} else {
		for (i = 0; i < gres_ns->type_cnt; i++) {
			if (gres_ns->no_consume) {
				xstrfmtcat(gres_ns->gres_used,
					   "%s%s:%s:0", sep, gres_name,
					   gres_ns->type_name[i]);
			} else {
				xstrfmtcat(gres_ns->gres_used,
					   "%s%s:%s:%"PRIu64, sep, gres_name,
					   gres_ns->type_name[i],
					   gres_ns->type_cnt_alloc[i]);
			}
			sep = ",";
		}
	}

	return gres_ns->gres_used;
}

static void _node_state_log(gres_node_state_t *gres_ns,
			    char *node_name, char *gres_name)
{
	int i, j;
	char *buf = NULL, *sep, tmp_str[128];

	xassert(gres_ns);

	info("gres/%s: state for %s", gres_name, node_name);
	if (gres_ns->gres_cnt_found == NO_VAL64) {
		snprintf(tmp_str, sizeof(tmp_str), "TBD");
	} else {
		snprintf(tmp_str, sizeof(tmp_str), "%"PRIu64,
			 gres_ns->gres_cnt_found);
	}

	if (gres_ns->no_consume) {
		info("  gres_cnt found:%s configured:%"PRIu64" "
		     "avail:%"PRIu64" no_consume",
		     tmp_str, gres_ns->gres_cnt_config,
		     gres_ns->gres_cnt_avail);
	} else {
		info("  gres_cnt found:%s configured:%"PRIu64" "
		     "avail:%"PRIu64" alloc:%"PRIu64"",
		     tmp_str, gres_ns->gres_cnt_config,
		     gres_ns->gres_cnt_avail,
		     gres_ns->gres_cnt_alloc);
	}

	if (gres_ns->gres_bit_alloc) {
		bit_fmt(tmp_str, sizeof(tmp_str),gres_ns->gres_bit_alloc);
		info("  gres_bit_alloc:%s of %d",
		     tmp_str, (int) bit_size(gres_ns->gres_bit_alloc));
	} else {
		info("  gres_bit_alloc:NULL");
	}

	info("  gres_used:%s", gres_ns->gres_used);

	if (gres_ns->links_cnt && gres_ns->link_len) {
		for (i = 0; i < gres_ns->link_len; i++) {
			sep = "";
			for (j = 0; j < gres_ns->link_len; j++) {
				xstrfmtcat(buf, "%s%d", sep,
					   gres_ns->links_cnt[i][j]);
				sep = ", ";
			}
			info("  links[%d]:%s", i, buf);
			xfree(buf);
		}
	}

	for (i = 0; i < gres_ns->topo_cnt; i++) {
		info("  topo[%d]:%s(%u)", i, gres_ns->topo_type_name[i],
		     gres_ns->topo_type_id[i]);
		if (gres_ns->topo_core_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_ns->topo_core_bitmap[i]);
			info("   topo_core_bitmap[%d]:%s of %d", i, tmp_str,
			     (int)bit_size(gres_ns->topo_core_bitmap[i]));
		} else
			info("   topo_core_bitmap[%d]:NULL", i);
		if (gres_ns->topo_gres_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_ns->topo_gres_bitmap[i]);
			info("   topo_gres_bitmap[%d]:%s of %d", i, tmp_str,
			     (int)bit_size(gres_ns->topo_gres_bitmap[i]));
		} else
			info("   topo_gres_bitmap[%d]:NULL", i);
		info("   topo_gres_cnt_alloc[%d]:%"PRIu64"", i,
		     gres_ns->topo_gres_cnt_alloc[i]);
		info("   topo_gres_cnt_avail[%d]:%"PRIu64"", i,
		     gres_ns->topo_gres_cnt_avail[i]);
	}

	for (i = 0; i < gres_ns->type_cnt; i++) {
		info("  type[%d]:%s(%u)", i, gres_ns->type_name[i],
		     gres_ns->type_id[i]);
		info("   type_cnt_alloc[%d]:%"PRIu64, i,
		     gres_ns->type_cnt_alloc[i]);
		info("   type_cnt_avail[%d]:%"PRIu64, i,
		     gres_ns->type_cnt_avail[i]);
	}
}

/*
 * Log a node's current gres state
 * IN gres_list - generated by gres_node_config_validate()
 * IN node_name - name of the node for which the gres information applies
 */
extern void gres_node_state_log(List gres_list, char *node_name)
{
	ListIterator gres_iter;
	gres_state_t *gres_state_node;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES) || !gres_list)
		return;

	(void) gres_init();

	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_node = (gres_state_t *) list_next(gres_iter))) {
		_node_state_log(gres_state_node->gres_data, node_name,
				gres_state_node->gres_name);
	}
	list_iterator_destroy(gres_iter);
}

/* Find node_state_t gres record with any allocated gres (key is unused) */
static int _find_node_state_with_alloc_gres(void *x, void *key)
{
	gres_state_t *gres_state_node = (gres_state_t *) x;

	if (((gres_node_state_t *) gres_state_node->gres_data)->gres_cnt_alloc)
		return 1;
	else
		return 0;
}

extern bool gres_node_state_list_has_alloc_gres(List gres_list)
{
	if (!gres_list)
		return false;

	return list_find_first(gres_list,
			       _find_node_state_with_alloc_gres, NULL);
}

/*
 * Build a string indicating a node's drained GRES
 * IN gres_list - generated by gres_node_config_validate()
 * RET - string, must be xfreed by caller
 */
extern char *gres_get_node_drain(List gres_list)
{
	char *node_drain = xstrdup("N/A");

	return node_drain;
}

/*
 * Build a string indicating a node's used GRES
 * IN gres_list - generated by gres_node_config_validate()
 * RET - string, must be xfreed by caller
 */
extern char *gres_get_node_used(List gres_list)
{
	ListIterator gres_iter;
	gres_state_t *gres_state_node;
	char *gres_used = NULL, *tmp;

	if (!gres_list)
		return gres_used;

	(void) gres_init();

	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_node = (gres_state_t *) list_next(gres_iter))) {
		tmp = _node_gres_used(gres_state_node->gres_data,
				      gres_state_node->gres_name);
		if (!tmp)
			continue;
		if (gres_used)
			xstrcat(gres_used, ",");
		xstrcat(gres_used, tmp);
	}
	list_iterator_destroy(gres_iter);

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

	(void) gres_init();

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
extern uint64_t gres_node_config_cnt(List gres_list, char *name)
{
	int i;
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;
	uint64_t count = 0;

	if (!gres_list || !name || !list_count(gres_list))
		return count;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (!xstrcmp(gres_context[i].gres_name, name)) {
			/* Find or create gres_state entry on the list */
			gres_state_node = list_find_first(
				gres_list, gres_find_id,
				&gres_context[i].plugin_id);

			if (!gres_state_node || !gres_state_node->gres_data)
				break;
			gres_ns = gres_state_node->gres_data;
			count = gres_ns->gres_cnt_config;
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

			gres_state_node = list_find_first(
				gres_list, gres_find_id,
				&gres_context[i].plugin_id);

			if (!gres_state_node || !gres_state_node->gres_data)
				break;
			gres_ns = gres_state_node->gres_data;
			type_id = gres_build_id(type_str);
			for (type = 0; type < gres_ns->type_cnt; type++) {
				if (gres_ns->type_id[type] == type_id) {
					count = gres_ns->type_cnt_avail[type];
					break;
				}
			}
			break;
		}
	}
	slurm_mutex_unlock(&gres_context_lock);

	return count;
}

static void _job_state_delete(gres_job_state_t *gres_js)
{
	int i;

	if (gres_js == NULL)
		return;

	for (i = 0; i < gres_js->node_cnt; i++) {
		if (gres_js->gres_bit_alloc)
			FREE_NULL_BITMAP(gres_js->gres_bit_alloc[i]);
		if (gres_js->gres_bit_step_alloc)
			FREE_NULL_BITMAP(gres_js->gres_bit_step_alloc[i]);
	}
	xfree(gres_js->gres_bit_alloc);
	xfree(gres_js->gres_cnt_node_alloc);
	xfree(gres_js->gres_bit_step_alloc);
	xfree(gres_js->gres_cnt_step_alloc);
	if (gres_js->gres_bit_select) {
		for (i = 0; i < gres_js->total_node_cnt; i++)
			FREE_NULL_BITMAP(gres_js->gres_bit_select[i]);
		xfree(gres_js->gres_bit_select);
	}
	xfree(gres_js->gres_cnt_node_alloc);
	xfree(gres_js->gres_cnt_node_select);
	xfree(gres_js->type_name);
	xfree(gres_js);
}

extern void gres_job_list_delete(void *list_element)
{
	gres_state_t *gres_state_job;

	if (gres_init() != SLURM_SUCCESS)
		return;

	gres_state_job = (gres_state_t *) list_element;
	slurm_mutex_lock(&gres_context_lock);
	_job_state_delete(gres_state_job->gres_data);
	gres_state_job->gres_data = NULL;
	_gres_state_delete_members(gres_state_job);
	slurm_mutex_unlock(&gres_context_lock);
}

static int _clear_cpus_per_gres(void *x, void *arg)
{
	gres_state_t *gres_state_job = (gres_state_t *) x;
	gres_job_state_t *gres_js;
	gres_js = (gres_job_state_t *) gres_state_job->gres_data;
	gres_js->cpus_per_gres = 0;
	return 0;
}
static int _clear_gres_per_job(void *x, void *arg)
{
	gres_state_t *gres_state_job = (gres_state_t *) x;
	gres_job_state_t *gres_js;
	gres_js = (gres_job_state_t *) gres_state_job->gres_data;
	gres_js->gres_per_job = 0;
	return 0;
}
static int _clear_gres_per_node(void *x, void *arg)
{
	gres_state_t *gres_state_job = (gres_state_t *) x;
	gres_job_state_t *gres_js;
	gres_js = (gres_job_state_t *) gres_state_job->gres_data;
	gres_js->gres_per_node = 0;
	return 0;
}
static int _clear_gres_per_socket(void *x, void *arg)
{
	gres_state_t *gres_state_job = (gres_state_t *) x;
	gres_job_state_t *gres_js;
	gres_js = (gres_job_state_t *) gres_state_job->gres_data;
	gres_js->gres_per_socket = 0;
	return 0;
}
static int _clear_gres_per_task(void *x, void *arg)
{
	gres_state_t *gres_state_job = (gres_state_t *) x;
	gres_job_state_t *gres_js;
	gres_js = (gres_job_state_t *) gres_state_job->gres_data;
	gres_js->gres_per_task = 0;
	return 0;
}
static int _clear_mem_per_gres(void *x, void *arg)
{
	gres_state_t *gres_state_job = (gres_state_t *) x;
	gres_job_state_t *gres_js;
	gres_js = (gres_job_state_t *) gres_state_job->gres_data;
	gres_js->mem_per_gres = 0;
	return 0;
}
static int _clear_total_gres(void *x, void *arg)
{
	gres_state_t *gres_state_job = (gres_state_t *) x;
	gres_job_state_t *gres_js;
	gres_js = (gres_job_state_t *) gres_state_job->gres_data;
	gres_js->total_gres = 0;
	return 0;
}

/*
 * Ensure consistency of gres_per_* options
 * Modify task and node count as needed for consistentcy with GRES options
 * RET -1 on failure, 0 on success
 */
static int _test_gres_cnt(gres_state_t *gres_state_job,
			  uint32_t *num_tasks,
			  uint32_t *min_nodes, uint32_t *max_nodes,
			  uint16_t *ntasks_per_node,
			  uint16_t *ntasks_per_socket,
			  uint16_t *sockets_per_node,
			  uint16_t *cpus_per_task)
{
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	int req_nodes, req_tasks, req_tasks_per_node, req_tasks_per_socket;
	int req_sockets, req_cpus_per_task;
	uint16_t cpus_per_gres;

	/* Ensure gres_per_job >= gres_per_node >= gres_per_socket */
	if (gres_js->gres_per_job &&
	    ((gres_js->gres_per_node &&
	      (gres_js->gres_per_node > gres_js->gres_per_job)) ||
	     (gres_js->gres_per_task &&
	      (gres_js->gres_per_task > gres_js->gres_per_job)) ||
	     (gres_js->gres_per_socket &&
	      (gres_js->gres_per_socket >
	       gres_js->gres_per_job)))) {
		error("Failed to ensure --%ss >= --gres=%s/--%ss-per-node >= --%ss-per-socket",
		      gres_state_job->gres_name,
		      gres_state_job->gres_name,
		      gres_state_job->gres_name,
		      gres_state_job->gres_name);
		return -1;
	}

	/* Ensure gres_per_job >= gres_per_task */
	if (gres_js->gres_per_node &&
	    ((gres_js->gres_per_task &&
	      (gres_js->gres_per_task > gres_js->gres_per_node)) ||
	     (gres_js->gres_per_socket &&
	      (gres_js->gres_per_socket >
	       gres_js->gres_per_node)))) {
		error("Failed to ensure --%ss >= --%ss-per-task",
		      gres_state_job->gres_name,
		      gres_state_job->gres_name);
		return -1;
	}

	/* gres_per_socket requires sockets-per-node count specification */
	if (gres_js->gres_per_socket) {
		if (*sockets_per_node == NO_VAL16) {
			error("--%ss-per-socket option requires --sockets-per-node specification",
			      gres_state_job->gres_name);
			return -1;
		}
	}

	/* make sure --cpu-per-gres is not combined with --cpus-per-task */
	if (!running_in_slurmctld() && gres_js->cpus_per_gres &&
	    (*cpus_per_task != NO_VAL16)) {
		error("--cpus-per-%s is mutually exclusive with --cpus-per-task",
		      gres_state_job->gres_name);
		return -1;
	}


	/*
	 * Ensure gres_per_job is multiple of gres_per_node
	 * Ensure node count is consistent with GRES parameters
	 */
	if (gres_js->gres_per_job && gres_js->gres_per_node) {
		if (gres_js->gres_per_job % gres_js->gres_per_node){
			/* gres_per_job not multiple of gres_per_node */
			error("Failed to validate job spec, --%ss is not multiple of --gres=%s/--%ss-per-node",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		}
		req_nodes = gres_js->gres_per_job /
			gres_js->gres_per_node;
		if (((*min_nodes != NO_VAL) && (req_nodes < *min_nodes)) ||
		    (req_nodes > *max_nodes)) {
			error("Failed to validate job spec. Based on --%s and --gres=%s/--%ss-per-node required nodes (%u) doesn't fall between min_nodes (%u) and max_nodes (%u) boundaries.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name,
			      gres_state_job->gres_name,
			      req_nodes,
			      *min_nodes,
			      *max_nodes);
			return -1;
		}
		*min_nodes = *max_nodes = req_nodes;
	}

	/*
	 * Ensure gres_per_node is multiple of gres_per_socket
	 * Ensure task count is consistent with GRES parameters
	 */
	if (gres_js->gres_per_node && gres_js->gres_per_socket) {
		if (gres_js->gres_per_node %
		    gres_js->gres_per_socket) {
			/* gres_per_node not multiple of gres_per_socket */
			error("Failed to validate job spec, --gres=%s/--%ss-per-node not multiple of --%ss-per-socket.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		}
		req_sockets = gres_js->gres_per_node /
			gres_js->gres_per_socket;
		if (*sockets_per_node == NO_VAL16)
			*sockets_per_node = req_sockets;
		else if (*sockets_per_node != req_sockets) {
			error("Failed to validate job spec. Based on --gres=%s/--%ss-per-node and --%ss-per-socket required number of sockets differ from --sockets-per-node.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		}
	}

	/*
	 * Ensure ntasks_per_tres is multiple of num_tasks
	 */
	if (gres_js->ntasks_per_gres &&
	    (gres_js->ntasks_per_gres != NO_VAL16) &&
	    (*num_tasks != NO_VAL)) {
		int tmp = *num_tasks / gres_js->ntasks_per_gres;
		if ((tmp * gres_js->ntasks_per_gres) != *num_tasks) {
			error("Failed to validate job spec, -n/--ntasks has to be a multiple of --ntasks-per-%s.",
			      gres_state_job->gres_name);
			return -1;
		}
	}

	/*
	 * Ensure gres_per_job is multiple of gres_per_task
	 * Ensure task count is consistent with GRES parameters
	 */
	if (gres_js->gres_per_task) {
		if(gres_js->gres_per_job) {
			if (gres_js->gres_per_job %
			    gres_js->gres_per_task) {
				/* gres_per_job not multiple of gres_per_task */
				error("Failed to validate job spec, --%ss not multiple of --%ss-per-task",
				      gres_state_job->gres_name,
				      gres_state_job->gres_name);
				return -1;
			}
			req_tasks = gres_js->gres_per_job /
				gres_js->gres_per_task;
			if (*num_tasks == NO_VAL)
				*num_tasks = req_tasks;
			else if (*num_tasks != req_tasks) {
				error("Failed to validate job spec. Based on --%ss and --%ss-per-task number of requested tasks differ from -n/--ntasks.",
				      gres_state_job->gres_name,
				      gres_state_job->gres_name);
				return -1;
			}
		} else if (*num_tasks != NO_VAL) {
			gres_js->gres_per_job = *num_tasks *
				gres_js->gres_per_task;
		} else {
			error("Failed to validate job spec. --%ss-per-task used without either --%ss or -n/--ntasks is not allowed.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		}
	}

	/*
	 * Ensure gres_per_node is multiple of gres_per_task
	 * Ensure tasks_per_node is consistent with GRES parameters
	 */
	if (gres_js->gres_per_node && gres_js->gres_per_task) {
		if (gres_js->gres_per_node %
		    gres_js->gres_per_task) {
			/* gres_per_node not multiple of gres_per_task */
			error("Failed to validate job spec, --gres=%s/--%ss-per-node not multiple of --%ss-per-task.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		}
		req_tasks_per_node = gres_js->gres_per_node /
			gres_js->gres_per_task;
		if ((*ntasks_per_node == NO_VAL16) ||
		    (*ntasks_per_node == 0))
			*ntasks_per_node = req_tasks_per_node;
		else if (*ntasks_per_node != req_tasks_per_node) {
			error("Failed to validate job spec. Based on --gres=%s/--%ss-per-node and --%ss-per-task requested number of tasks per node differ from --ntasks-per-node.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		}
	}

	/*
	 * Ensure gres_per_socket is multiple of gres_per_task
	 * Ensure ntasks_per_socket is consistent with GRES parameters
	 */
	if (gres_js->gres_per_socket && gres_js->gres_per_task) {
		if (gres_js->gres_per_socket %
		    gres_js->gres_per_task) {
			/* gres_per_socket not multiple of gres_per_task */
			error("Failed to validate job spec, --%ss-per-socket not multiple of --%ss-per-task.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		}
		req_tasks_per_socket = gres_js->gres_per_socket /
			gres_js->gres_per_task;
		if ((*ntasks_per_socket == NO_VAL16) ||
		    (*ntasks_per_socket == 0))
			*ntasks_per_socket = req_tasks_per_socket;
		else if (*ntasks_per_socket != req_tasks_per_socket) {
			error("Failed to validate job spec. Based on --%ss-per-socket and --%ss-per-task requested number of tasks per sockets differ from --ntasks-per-socket.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		}
	}

	/* Ensure that cpus_per_gres * gres_per_task == cpus_per_task */
	if (gres_js->cpus_per_gres)
		cpus_per_gres = gres_js->cpus_per_gres;
	else
		cpus_per_gres = gres_js->def_cpus_per_gres;
	if (cpus_per_gres && gres_js->gres_per_task) {
		req_cpus_per_task = cpus_per_gres *gres_js->gres_per_task;
		if ((*cpus_per_task == NO_VAL16) ||
		    (*cpus_per_task == 0))
			*cpus_per_task = req_cpus_per_task;
		else if (*cpus_per_task != req_cpus_per_task) {
			error("Failed to validate job spec. Based on --cpus-per-%s and --%ss-per-task requested number of cpus differ from -c/--cpus-per-task.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		}
	}

	/* Ensure tres_per_job >= node count */
	if (gres_js->gres_per_job) {
		if ((*min_nodes != NO_VAL) &&
		    (gres_js->gres_per_job < *min_nodes)) {
			error("Failed to validate job spec, --%ss < -N",
			      gres_state_job->gres_name);
			return -1;
		}
		if ((*max_nodes != NO_VAL) &&
		    (gres_js->gres_per_job < *max_nodes)) {
			*max_nodes = gres_js->gres_per_job;
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

	if (!(sep = xstrstr(*save_ptr, "gres:"))) {
		debug2("%s is not a gres", *save_ptr);
		xfree(name);
		*save_ptr = NULL;
		goto fini;
	} else {
		sep += 5; /* strlen "gres:" */
		*save_ptr = sep;
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
		if ((rc == ESLURM_INVALID_GRES) && running_in_slurmctld()) {
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
static gres_state_t *_get_next_job_gres(char *in_val, uint64_t *cnt,
					List gres_list, char **save_ptr,
					int *rc)
{
	static char *prev_save_ptr = NULL;
	int context_inx = NO_VAL, my_rc = SLURM_SUCCESS;
	gres_job_state_t *gres_js = NULL;
	gres_state_t *gres_state_job = NULL;
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
	job_search_key.config_flags = gres_context[context_inx].config_flags;
	job_search_key.plugin_id = gres_context[context_inx].plugin_id;
	job_search_key.type_id = gres_build_id(type);
	gres_state_job = list_find_first(gres_list, gres_find_job_by_key,
					 &job_search_key);

	if (gres_state_job) {
		gres_js = gres_state_job->gres_data;
	} else {
		gres_js = xmalloc(sizeof(gres_job_state_t));
		gres_js->type_id = gres_build_id(type);
		gres_js->type_name = type;
		type = NULL;	/* String moved above */

		gres_state_job = gres_create_state(
			&gres_context[context_inx], GRES_STATE_SRC_CONTEXT_PTR,
			GRES_STATE_TYPE_JOB, gres_js);
		list_append(gres_list, gres_state_job);
	}
	gres_js->flags = flags;

fini:	xfree(name);
	xfree(type);
	if (my_rc != SLURM_SUCCESS) {
		prev_save_ptr = NULL;
		if ((my_rc == ESLURM_INVALID_GRES) && running_in_slurmctld()) {
			info("%s: Invalid GRES job specification %s", __func__,
			     in_val);
		}
		*rc = my_rc;
	}
	*save_ptr = prev_save_ptr;
	return gres_state_job;
}

/* Return true if job specification only includes cpus_per_gres or mem_per_gres
 * Return false if any other field set
 */
static bool _generic_state(void *gres_data, bool is_job)
{
	if (is_job) {
		gres_job_state_t *gres_js = gres_data;
		if (gres_js->gres_per_job ||
		    gres_js->gres_per_node ||
		    gres_js->gres_per_socket ||
		    gres_js->gres_per_task)
			return false;
	} else {
		gres_step_state_t *gres_ss = gres_data;
		if (gres_ss->gres_per_step ||
		    gres_ss->gres_per_node ||
		    gres_ss->gres_per_socket ||
		    gres_ss->gres_per_task)
			return false;
	}

	return true;
}

/*
 * Setup over_list to mark if we have gres of the same type.
 */
static bool _set_over_list(gres_state_t *gres_state,
			   overlap_check_t *over_list,
			   int *over_count, bool is_job)
{
	char *type_name = is_job ?
		((gres_job_state_t *) gres_state->gres_data)->type_name:
		((gres_step_state_t *) gres_state->gres_data)->type_name;
	int i;
	bool overlap_merge = false;

	xassert(over_list);
	xassert(over_count);

	for (i = 0; i < *over_count; i++) {
		if (over_list[i].plugin_id == gres_state->plugin_id)
			break;
	}

	if (i >= *over_count) {
		over_list[(*over_count)++].plugin_id = gres_state->plugin_id;
		if (type_name) {
			over_list[i].with_type = true;
		} else {
			over_list[i].without_type = true;
			over_list[i].without_type_state = gres_state->gres_data;
		}
	} else if (type_name) {
		over_list[i].with_type = true;
		if (over_list[i].without_type)
			overlap_merge = true;
	} else {
		over_list[i].without_type = true;
		over_list[i].without_type_state = gres_state->gres_data;
		if (over_list[i].with_type)
			overlap_merge = true;
	}

	return overlap_merge;
}

/*
 * Put generic data (*_per_gres) on other gres of the same kind.
 */
static int _merge_generic_data(
	List gres_list, overlap_check_t *over_list, int over_count, bool is_job)
{
	int rc = SLURM_SUCCESS;
	uint16_t cpus_per_gres;
	uint64_t mem_per_gres;
	gres_state_t *gres_state;
	gres_job_state_t *gres_js;
	gres_step_state_t *gres_ss;
	void *generic_gres_data;
	ListIterator iter = list_iterator_create(gres_list);

	for (int i = 0; i < over_count; i++) {
		if (!over_list[i].with_type || !over_list[i].without_type_state)
			continue;
		if (!_generic_state(over_list[i].without_type_state, is_job)) {
			rc = ESLURM_INVALID_GRES_TYPE;
			break;
		}

		/* Propagate generic parameters */
		if (is_job) {
			generic_gres_data = gres_js =
				over_list[i].without_type_state;
			cpus_per_gres =	gres_js->cpus_per_gres;
			mem_per_gres = gres_js->mem_per_gres;
		} else {
			generic_gres_data = gres_ss =
				over_list[i].without_type_state;
			cpus_per_gres =	gres_ss->cpus_per_gres;
			mem_per_gres = gres_ss->mem_per_gres;
		}

		while ((gres_state = list_next(iter))) {
			if (over_list[i].plugin_id != gres_state->plugin_id)
				continue;
			if (generic_gres_data == gres_state->gres_data) {
				list_remove(iter);
				continue;
			}

			if (is_job) {
				gres_js = gres_state->gres_data;
				if (!gres_js->cpus_per_gres) {
					gres_js->cpus_per_gres =
						cpus_per_gres;
				}
				if (!gres_js->mem_per_gres) {
					gres_js->mem_per_gres =
						mem_per_gres;
				}
			} else {
				gres_ss = gres_state->gres_data;
				if (!gres_ss->cpus_per_gres) {
					gres_ss->cpus_per_gres =
						cpus_per_gres;
				}
				if (!gres_ss->mem_per_gres) {
					gres_ss->mem_per_gres =
						mem_per_gres;
				}
			}
		}
		list_iterator_reset(iter);
	}

	list_iterator_destroy(iter);

	return rc;
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
extern int gres_job_state_validate(char *cpus_per_tres,
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
	overlap_check_t *over_list;
	int over_count = 0, rc = SLURM_SUCCESS, size;
	bool have_gres_sharing = false, have_gres_shared = false;
	bool requested_gpu = false;
	bool overlap_merge = false;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
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

	if ((rc = gres_init()) != SLURM_SUCCESS)
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
		*gres_list = list_create(gres_job_list_delete);
	slurm_mutex_lock(&gres_context_lock);
	if (cpus_per_tres) {
		char *in_val = cpus_per_tres, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_list,
							    &save_ptr, &rc))) {
			gres_js = gres_state_job->gres_data;
			gres_js->cpus_per_gres = cnt;
			in_val = NULL;
			gres_js->ntasks_per_gres = *ntasks_per_tres;
		}
	}
	if (tres_per_job) {
		char *in_val = tres_per_job, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_list,
							    &save_ptr, &rc))) {
			if (!requested_gpu &&
			    (!xstrcmp(gres_state_job->gres_name, "gpu")))
				requested_gpu = true;
			gres_js = gres_state_job->gres_data;
			gres_js->gres_per_job = cnt;
			in_val = NULL;
			gres_js->total_gres =
				MAX(gres_js->total_gres, cnt);
			gres_js->ntasks_per_gres = *ntasks_per_tres;
		}
	}
	if (tres_per_node) {
		char *in_val = tres_per_node, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_list,
							    &save_ptr, &rc))) {
			if (!requested_gpu &&
			    (!xstrcmp(gres_state_job->gres_name, "gpu")))
				requested_gpu = true;
			gres_js = gres_state_job->gres_data;
			gres_js->gres_per_node = cnt;
			in_val = NULL;
			if (*min_nodes != NO_VAL)
				cnt *= *min_nodes;
			gres_js->total_gres =
				MAX(gres_js->total_gres, cnt);
			gres_js->ntasks_per_gres = *ntasks_per_tres;
		}
	}
	if (tres_per_socket) {
		char *in_val = tres_per_socket, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_list,
							    &save_ptr, &rc))) {
			if (!requested_gpu &&
			    (!xstrcmp(gres_state_job->gres_name, "gpu")))
				requested_gpu = true;
			gres_js = gres_state_job->gres_data;
			gres_js->gres_per_socket = cnt;
			in_val = NULL;
			if ((*min_nodes != NO_VAL) &&
			    (*sockets_per_node != NO_VAL16)) {
				cnt *= (*min_nodes * *sockets_per_node);
			} else if ((*num_tasks != NO_VAL) &&
				   (*ntasks_per_socket != NO_VAL16)) {
				cnt *= ((*num_tasks + *ntasks_per_socket - 1) /
					*ntasks_per_socket);
			}
			gres_js->total_gres =
				MAX(gres_js->total_gres, cnt);
			gres_js->ntasks_per_gres = *ntasks_per_tres;
		}
	}
	if (tres_per_task) {
		char *in_val = tres_per_task, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
						     *gres_list,
						     &save_ptr, &rc))) {
			if (!requested_gpu &&
			    (!xstrcmp(gres_state_job->gres_name, "gpu")))
				requested_gpu = true;
			gres_js = gres_state_job->gres_data;
			gres_js->gres_per_task = cnt;
			in_val = NULL;
			if (*num_tasks != NO_VAL)
				cnt *= *num_tasks;
			gres_js->total_gres =
				MAX(gres_js->total_gres, cnt);
			gres_js->ntasks_per_gres = *ntasks_per_tres;
		}
	}
	if (mem_per_tres) {
		char *in_val = mem_per_tres, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_list,
							    &save_ptr, &rc))) {
			gres_js = gres_state_job->gres_data;
			gres_js->mem_per_gres = cnt;
			in_val = NULL;
			gres_js->ntasks_per_gres = *ntasks_per_tres;
		}
	}

	/* *num_tasks and *ntasks_per_tres could be 0 on requeue */
	if (!ntasks_per_tres || !*ntasks_per_tres ||
	    (*ntasks_per_tres == NO_VAL16)) {
		/* do nothing */
	} else if (requested_gpu && list_count(*gres_list)) {
		/* Set num_tasks = gpus * ntasks/gpu */
		uint64_t gpus = _get_job_gres_list_cnt(*gres_list, "gpu", NULL);
		if (gpus != NO_VAL64)
			*num_tasks = gpus * *ntasks_per_tres;
		else {
			error("%s: Can't set num_tasks = gpus * *ntasks_per_tres because there are no allocated GPUs",
			      __func__);
			rc = ESLURM_INVALID_GRES;
		}
	} else if (*num_tasks && (*num_tasks != NO_VAL)) {
		/*
		 * If job_gres_list empty, and ntasks_per_tres is specified,
		 * then derive GPUs according to how many tasks there are.
		 * GPU GRES = [ntasks / (ntasks_per_tres)]
		 * For now, only generate type-less GPUs.
		 */
		uint32_t gpus = *num_tasks / *ntasks_per_tres;
		char *save_ptr = NULL, *gres = NULL, *in_val;
		xstrfmtcat(gres, "gres:gpu:%u", gpus);
		in_val = gres;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_list,
							    &save_ptr, &rc))) {
			gres_js = gres_state_job->gres_data;
			gres_js->ntasks_per_gres = *ntasks_per_tres;
			/* Simulate a tres_per_job specification */
			gres_js->gres_per_job = cnt;
			gres_js->total_gres =
				MAX(gres_js->total_gres, cnt);
			in_val = NULL;
		}
		if (list_count(*gres_list) == 0)
			error("%s: Failed to add generated GRES %s (via ntasks_per_tres) to gres_list",
			      __func__, gres);
		else
			requested_gpu = true;
		xfree(gres);
	} else {
		error("%s: --ntasks-per-tres needs either a GRES GPU specification or a node/ntask specification",
		      __func__);
		rc = ESLURM_INVALID_GRES;
	}

	slurm_mutex_unlock(&gres_context_lock);

	if (rc != SLURM_SUCCESS)
		return rc;
	size = list_count(*gres_list);
	if (size == 0) {
		FREE_NULL_LIST(*gres_list);
		return rc;
	}

	if (mem_per_tres && (!requested_gpu)) {
		/*
		 * If someone requested mem_per_tres but didn't request any
		 * GPUs (even if --exclusive was used), then error.
		 * For now we only test for GPUs since --mem-per-gpu is the
		 * only allowed mem_per_gres option.
		 * Even though --exclusive means that you will be allocated all
		 * of the GRES on the node, we still require that GPUs are
		 * explicitly requested when --mem-per-gpu is used.
		 */
		error("Requested mem_per_tres=%s but did not request any GPU.",
		      mem_per_tres);
		return ESLURM_INVALID_GRES;
	}

	/*
	 * Check for record overlap (e.g. "gpu:2,gpu:tesla:1")
	 * Ensure tres_per_job >= tres_per_node >= tres_per_socket
	 */
	over_list = xcalloc(size, sizeof(overlap_check_t));
	iter = list_iterator_create(*gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(iter))) {
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		if (_test_gres_cnt(gres_state_job, num_tasks, min_nodes,
				   max_nodes, ntasks_per_node,
				   ntasks_per_socket, sockets_per_node,
				   cpus_per_task) != 0) {
			rc = ESLURM_INVALID_GRES;
			break;
		}
		if (!have_gres_sharing &&
		    gres_id_sharing(gres_state_job->plugin_id))
			have_gres_sharing = true;
		if (gres_id_shared(gres_state_job->config_flags)) {
			have_gres_shared = true;
			/*
			 * Shared gres (e.g. gres/'shared') only supports a
			 * per-node count,
			 * set either explicitly or implicitly.
			 */
			if (gres_js->gres_per_job &&
			    (*max_nodes != 1)) {
				rc = ESLURM_INVALID_GRES;
				break;
			}
			if (gres_js->gres_per_socket &&
			    (*sockets_per_node != 1)) {
				rc = ESLURM_INVALID_GRES;
				break;
			}
			if (gres_js->gres_per_task && (*num_tasks != 1)) {
				rc = ESLURM_INVALID_GRES;
				break;
			}
		}
		if (have_gres_sharing && have_gres_shared) {
			rc = ESLURM_INVALID_GRES;
			break;
		}

		if (_set_over_list(gres_state_job, over_list, &over_count, 1))
			overlap_merge = true;
	}
	list_iterator_destroy(iter);

	if (have_gres_shared && (rc == SLURM_SUCCESS) && tres_freq &&
	    strstr(tres_freq, "gpu")) {
		rc = ESLURM_INVALID_GRES;
	}

	if (overlap_merge) /* Merge generic data if possible */
		rc = _merge_generic_data(*gres_list, over_list, over_count, 1);

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
extern int gres_job_revalidate(List gres_list)
{
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
	ListIterator iter;
	int rc = SLURM_SUCCESS;

	if (!gres_list || (select_plugin_type == SELECT_TYPE_CONS_TRES))
		return SLURM_SUCCESS;

	iter = list_iterator_create(gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(iter))) {
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		if (gres_js->gres_per_job ||
		    gres_js->gres_per_socket ||
		    gres_js->gres_per_task) {
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
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
	bool rc = false;
	int i;

	if (!job_gres_list)
		return false;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		gres_js = gres_state_job->gres_data;
		if (!gres_js)
			continue;
		for (i = 0; i < gres_js->node_cnt; i++) {
			if (gres_js->gres_bit_alloc &&
			    gres_js->gres_bit_alloc[i]) {
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
 * NOTE: For gres/'shared' return count of gres/gpu
 */
static int _get_node_gres_cnt(List node_gres_list, gres_state_t *gres_state_job)
{
	gres_node_state_t *gres_ns;
	gres_state_t *gres_state_node;
	int gres_cnt = 0;
	uint32_t plugin_id;

	if (!node_gres_list)
		return 0;

	if (gres_id_shared(gres_state_job->config_flags))
		plugin_id = gpu_plugin_id;
	else
		plugin_id = gres_state_job->plugin_id;

	if ((gres_state_node = list_find_first(node_gres_list, gres_find_id,
					       &plugin_id))) {
		gres_ns = gres_state_node->gres_data;
		gres_cnt = (int) gres_ns->gres_cnt_config;
	}

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
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
	bool rc = true;
	int job_gres_cnt, node_gres_cnt;

	if (!job_gres_list)
		return true;

	(void) gres_init();

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		gres_js = gres_state_job->gres_data;
		if (!gres_js || !gres_js->gres_bit_alloc)
			continue;
		if ((node_inx >= gres_js->node_cnt) ||
		    !gres_js->gres_bit_alloc[node_inx])
			continue;
		job_gres_cnt = bit_size(gres_js->gres_bit_alloc[node_inx]);
		node_gres_cnt = _get_node_gres_cnt(node_gres_list,
						   gres_state_job);
		if (job_gres_cnt != node_gres_cnt) {
			error("%s: Killing job %u: gres/%s count mismatch on node "
			      "%s (%d != %d)",
			      __func__, job_id, gres_state_job->gres_name,
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
extern int gres_job_revalidate2(uint32_t job_id, List job_gres_list,
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
		node_ptr = node_record_table_ptr[i];
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
extern int gres_find_sock_by_job_state(void *x, void *key)
{
	sock_gres_t *sock_data = (sock_gres_t *) x;
	gres_state_t *job_gres_state = (gres_state_t *) key;
	gres_job_state_t *sock_gres_js, *gres_js;

	gres_js = (gres_job_state_t *) job_gres_state->gres_data;
	sock_gres_js = sock_data->gres_state_job->gres_data;

	if ((sock_data->gres_state_job->plugin_id ==
	     job_gres_state->plugin_id) &&
	    (sock_gres_js->type_id == gres_js->type_id))
		return 1;
	return 0;
}

/*
 * Create a (partial) copy of a job's gres state for job binding
 * IN gres_list - List of Gres records for this job to track usage
 * RET The copy or NULL on failure
 * NOTE: Only job details are copied, NOT the job step details
 */
extern List gres_job_state_list_dup(List gres_list)
{
	return gres_job_state_extract(gres_list, -1);
}

/* Copy gres_job_state_t record for ALL nodes */
extern void *gres_job_state_dup(gres_job_state_t *gres_js)
{

	int i;
	gres_job_state_t *new_gres_js;

	if (gres_js == NULL)
		return NULL;

	new_gres_js = xmalloc(sizeof(gres_job_state_t));
	new_gres_js->cpus_per_gres	= gres_js->cpus_per_gres;
	new_gres_js->def_cpus_per_gres	= gres_js->def_cpus_per_gres;
	new_gres_js->gres_per_job	= gres_js->gres_per_job;
	new_gres_js->gres_per_node	= gres_js->gres_per_node;
	new_gres_js->gres_per_socket	= gres_js->gres_per_socket;
	new_gres_js->gres_per_task	= gres_js->gres_per_task;
	new_gres_js->mem_per_gres	= gres_js->mem_per_gres;
	new_gres_js->def_mem_per_gres	= gres_js->def_mem_per_gres;
	new_gres_js->node_cnt		= gres_js->node_cnt;
	new_gres_js->total_gres	= gres_js->total_gres;
	new_gres_js->type_id		= gres_js->type_id;
	new_gres_js->type_name		= xstrdup(gres_js->type_name);

	if (gres_js->gres_cnt_node_alloc) {
		i = sizeof(uint64_t) * gres_js->node_cnt;
		new_gres_js->gres_cnt_node_alloc = xmalloc(i);
		memcpy(new_gres_js->gres_cnt_node_alloc,
		       gres_js->gres_cnt_node_alloc, i);
	}
	if (gres_js->gres_bit_alloc) {
		new_gres_js->gres_bit_alloc = xcalloc(gres_js->node_cnt,
						      sizeof(bitstr_t *));
		for (i = 0; i < gres_js->node_cnt; i++) {
			if (gres_js->gres_bit_alloc[i] == NULL)
				continue;
			new_gres_js->gres_bit_alloc[i] =
				bit_copy(gres_js->gres_bit_alloc[i]);
		}
	}
	return new_gres_js;
}

/* Copy gres_job_state_t record for one specific node */
static void *_job_state_dup2(gres_job_state_t *gres_js, int node_index)
{
	gres_job_state_t *new_gres_js;

	if (gres_js == NULL)
		return NULL;

	new_gres_js = xmalloc(sizeof(gres_job_state_t));
	new_gres_js->cpus_per_gres	= gres_js->cpus_per_gres;
	new_gres_js->def_cpus_per_gres	= gres_js->def_cpus_per_gres;
	new_gres_js->gres_per_job	= gres_js->gres_per_job;
	new_gres_js->gres_per_node	= gres_js->gres_per_node;
	new_gres_js->gres_per_socket	= gres_js->gres_per_socket;
	new_gres_js->gres_per_task	= gres_js->gres_per_task;
	new_gres_js->mem_per_gres	= gres_js->mem_per_gres;
	new_gres_js->def_mem_per_gres	= gres_js->def_mem_per_gres;
	new_gres_js->node_cnt		= 1;
	new_gres_js->total_gres	= gres_js->total_gres;
	new_gres_js->type_id		= gres_js->type_id;
	new_gres_js->type_name		= xstrdup(gres_js->type_name);

	if (gres_js->gres_cnt_node_alloc) {
		new_gres_js->gres_cnt_node_alloc = xmalloc(sizeof(uint64_t));
		new_gres_js->gres_cnt_node_alloc[0] =
			gres_js->gres_cnt_node_alloc[node_index];
	}
	if (gres_js->gres_bit_alloc && gres_js->gres_bit_alloc[node_index]) {
		new_gres_js->gres_bit_alloc	= xmalloc(sizeof(bitstr_t *));
		new_gres_js->gres_bit_alloc[0] =
			bit_copy(gres_js->gres_bit_alloc[node_index]);
	}
	return new_gres_js;
}

/*
 * Create a (partial) copy of a job's gres state for a particular node index
 * IN gres_list - List of Gres records for this job to track usage
 * IN node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
extern List gres_job_state_extract(List gres_list, int node_index)
{
	ListIterator gres_iter;
	gres_state_t *gres_state_job, *new_gres_state;
	List new_gres_list = NULL;
	void *new_gres_data;

	if (gres_list == NULL)
		return new_gres_list;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(gres_iter))) {
		if (node_index == -1)
			new_gres_data = gres_job_state_dup(
				gres_state_job->gres_data);
		else {
			new_gres_data = _job_state_dup2(
				gres_state_job->gres_data,
				node_index);
		}
		if (new_gres_data == NULL)
			break;
		if (new_gres_list == NULL) {
			new_gres_list = list_create(gres_job_list_delete);
		}
		new_gres_state = gres_create_state(
			gres_state_job, GRES_STATE_SRC_STATE_PTR,
			GRES_STATE_TYPE_JOB, new_gres_data);
		list_append(new_gres_list, new_gres_state);
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return new_gres_list;
}

/*
 * Pack a job's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_job_config_validate()
 * IN/OUT buffer - location to write state to
 * IN job_id - job's ID
 * IN details - if set then pack job step allocation details (only needed to
 *		save/restore job state, not needed in job credential for
 *		slurmd task binding)
 *
 * NOTE: A job's allocation to steps is not recorded here, but recovered with
 *	 the job step state information upon slurmctld restart.
 */
extern int gres_job_state_pack(List gres_list, buf_t *buffer,
			       uint32_t job_id, bool details,
			       uint16_t protocol_version)
{
	int i, rc = SLURM_SUCCESS;
	uint32_t top_offset, tail_offset;
	uint32_t magic = GRES_MAGIC;
	uint16_t rec_cnt = 0;
	ListIterator gres_iter;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	if (gres_list == NULL)
		return rc;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(gres_iter))) {
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;

		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_state_job->plugin_id, buffer);
			pack16(gres_js->cpus_per_gres, buffer);
			pack16(gres_js->flags, buffer);
			pack64(gres_js->gres_per_job, buffer);
			pack64(gres_js->gres_per_node, buffer);
			pack64(gres_js->gres_per_socket, buffer);
			pack64(gres_js->gres_per_task, buffer);
			pack64(gres_js->mem_per_gres, buffer);
			pack16(gres_js->ntasks_per_gres, buffer);
			pack64(gres_js->total_gres, buffer);
			packstr(gres_js->type_name, buffer);
			pack32(gres_js->node_cnt, buffer);

			if (gres_js->gres_cnt_node_alloc) {
				pack8((uint8_t) 1, buffer);
				pack64_array(gres_js->gres_cnt_node_alloc,
					     gres_js->node_cnt, buffer);
			} else {
				pack8((uint8_t) 0, buffer);
			}

			if (gres_js->gres_bit_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_js->node_cnt; i++) {
					pack_bit_str_hex(gres_js->
							 gres_bit_alloc[i],
							 buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (details && gres_js->gres_bit_step_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_js->node_cnt; i++) {
					pack_bit_str_hex(gres_js->
							 gres_bit_step_alloc[i],
							 buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (details && gres_js->gres_cnt_step_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_js->node_cnt; i++) {
					pack64(gres_js->
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
 * OUT gres_list - restored state stored by gres_job_state_pack()
 * IN/OUT buffer - location to read state from
 * IN job_id - job's ID
 */
extern int gres_job_state_unpack(List *gres_list, buf_t *buffer,
				 uint32_t job_id,
				 uint16_t protocol_version)
{
	int i = 0, rc;
	uint32_t magic = 0, plugin_id = 0, utmp32 = 0;
	uint16_t rec_cnt = 0;
	uint8_t  has_more = 0;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js = NULL;
	bool locked = false;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	rc = gres_init();

	slurm_mutex_lock(&gres_context_lock);
	locked = true;
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(gres_job_list_delete);
	}

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		slurm_gres_context_t *gres_ctx;
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;

		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_js = xmalloc(sizeof(gres_job_state_t));
			safe_unpack16(&gres_js->cpus_per_gres, buffer);
			safe_unpack16(&gres_js->flags, buffer);
			safe_unpack64(&gres_js->gres_per_job, buffer);
			safe_unpack64(&gres_js->gres_per_node, buffer);
			safe_unpack64(&gres_js->gres_per_socket, buffer);
			safe_unpack64(&gres_js->gres_per_task, buffer);
			safe_unpack64(&gres_js->mem_per_gres, buffer);
			safe_unpack16(&gres_js->ntasks_per_gres, buffer);
			safe_unpack64(&gres_js->total_gres, buffer);
			safe_unpackstr_xmalloc(&gres_js->type_name,
					       &utmp32, buffer);
			gres_js->type_id =
				gres_build_id(gres_js->type_name);
			safe_unpack32(&gres_js->node_cnt, buffer);
			if (gres_js->node_cnt > NO_VAL)
				goto unpack_error;

			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_unpack64_array(
					&gres_js->gres_cnt_node_alloc,
					&utmp32, buffer);
			}

			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_xcalloc(gres_js->gres_bit_alloc,
					     gres_js->node_cnt,
					     sizeof(bitstr_t *));
				for (i = 0; i < gres_js->node_cnt; i++) {
					unpack_bit_str_hex(&gres_js->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_xcalloc(gres_js->gres_bit_step_alloc,
					     gres_js->node_cnt,
					     sizeof(bitstr_t *));
				for (i = 0; i < gres_js->node_cnt; i++) {
					unpack_bit_str_hex(&gres_js->
							   gres_bit_step_alloc[i],
							   buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				safe_xcalloc(gres_js->gres_cnt_step_alloc,
					     gres_js->node_cnt,
					     sizeof(uint64_t));
				for (i = 0; i < gres_js->node_cnt; i++) {
					safe_unpack64(&gres_js->
						      gres_cnt_step_alloc[i],
						      buffer);
				}
			}
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			goto unpack_error;
		}

		if (!(gres_ctx = _find_context_by_id(plugin_id))) {
			/*
			 * A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data.
			 */
			error("%s: no plugin configured to unpack data type %u from job %u. This is likely due to a difference in the GresTypes configured in slurm.conf on different cluster nodes.",
			      __func__, plugin_id, job_id);
			_job_state_delete(gres_js);
			continue;
		}

		gres_state_job = gres_create_state(
			gres_ctx, GRES_STATE_SRC_CONTEXT_PTR,
			GRES_STATE_TYPE_JOB, gres_js);
		gres_js = NULL;	/* nothing left to free on error */
		list_append(*gres_list, gres_state_job);
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error from job %u", __func__, job_id);
	if (gres_js)
		_job_state_delete(gres_js);
	if (locked)
		slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

/*
 * Pack a job's allocated gres information for use by prolog/epilog
 * IN gres_list - generated by gres_job_config_validate()
 * IN/OUT buffer - location to write state to
 */
extern int gres_job_alloc_pack(List gres_list, buf_t *buffer,
			       uint16_t protocol_version)
{
	int i, rc = SLURM_SUCCESS;
	uint32_t top_offset, tail_offset;
	uint32_t magic = GRES_MAGIC;
	uint16_t rec_cnt = 0;
	ListIterator gres_iter;
	gres_epilog_info_t *gres_ei;

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	if (gres_list == NULL)
		return rc;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ei = list_next(gres_iter))) {
		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ei->plugin_id, buffer);
			pack32(gres_ei->node_cnt, buffer);
			if (gres_ei->gres_cnt_node_alloc) {
				pack8((uint8_t) 1, buffer);
				pack64_array(gres_ei->gres_cnt_node_alloc,
					     gres_ei->node_cnt, buffer);
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (gres_ei->gres_bit_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_ei->node_cnt; i++) {
					pack_bit_str_hex(gres_ei->
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
	gres_epilog_info_t *gres_ei = (gres_epilog_info_t *) x;
	int i;

	if (!gres_ei)
		return;

	if (gres_ei->gres_bit_alloc) {
		for (i = 0; i < gres_ei->node_cnt; i++)
			FREE_NULL_BITMAP(gres_ei->gres_bit_alloc[i]);
		xfree(gres_ei->gres_bit_alloc);
	}
	xfree(gres_ei->gres_cnt_node_alloc);
	xfree(gres_ei->node_list);
	xfree(gres_ei);
}

/*
 * Unpack a job's allocated gres information for use by prolog/epilog
 * OUT gres_list - restored state stored by gres_job_alloc_pack()
 * IN/OUT buffer - location to read state from
 */
extern int gres_job_alloc_unpack(List *gres_list, buf_t *buffer,
				 uint16_t protocol_version)
{
	int i = 0, rc;
	uint32_t magic = 0, utmp32 = 0;
	uint16_t rec_cnt = 0;
	uint8_t filled = 0;
	gres_epilog_info_t *gres_ei = NULL;
	bool locked = false;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	rc = gres_init();

	slurm_mutex_lock(&gres_context_lock);
	locked = true;
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_epilog_list_del);
	}

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		slurm_gres_context_t *gres_ctx;
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;

		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			gres_ei = xmalloc(sizeof(gres_epilog_info_t));
			safe_unpack32(&gres_ei->plugin_id, buffer);
			safe_unpack32(&gres_ei->node_cnt, buffer);
			if (gres_ei->node_cnt > NO_VAL)
				goto unpack_error;
			safe_unpack8(&filled, buffer);
			if (filled) {
				safe_unpack64_array(
					&gres_ei->gres_cnt_node_alloc,
					&utmp32, buffer);
			}
			safe_unpack8(&filled, buffer);
			if (filled) {
				safe_xcalloc(gres_ei->gres_bit_alloc,
					     gres_ei->node_cnt,
					     sizeof(bitstr_t *));
				for (i = 0; i < gres_ei->node_cnt; i++) {
					unpack_bit_str_hex(&gres_ei->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			goto unpack_error;
		}

		if (!(gres_ctx = _find_context_by_id(gres_ei->plugin_id))) {
			/*
			 * A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data.
			 */
			error("%s: no plugin configured to unpack data type %u",
			      __func__, gres_ei->plugin_id);
			_epilog_list_del(gres_ei);
			continue;
		}
		list_append(*gres_list, gres_ei);
		gres_ei = NULL;
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error", __func__);
	if (gres_ei)
		_epilog_list_del(gres_ei);
	if (locked)
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
extern List gres_g_epilog_build_env(List job_gres_list, char *node_list)
{
	ListIterator gres_iter;
	gres_state_t *gres_ptr = NULL;
	gres_epilog_info_t *gres_ei;
	List epilog_gres_list = NULL;

	if (!job_gres_list)
		return NULL;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(job_gres_list);
	while ((gres_ptr = list_next(gres_iter))) {
		slurm_gres_context_t *gres_ctx;
		if (!(gres_ctx = _find_context_by_id(gres_ptr->plugin_id))) {
			error("%s: gres not found in context.  This should never happen",
			      __func__);
			continue;
		}

		if (!gres_ctx->ops.epilog_build_env)
			continue;	/* No plugin to call */
		gres_ei = (*(gres_ctx->ops.epilog_build_env))
			(gres_ptr->gres_data);
		if (!gres_ei)
			continue;	/* No info to add for this plugin */
		if (!epilog_gres_list)
			epilog_gres_list = list_create(_epilog_list_del);
		gres_ei->plugin_id = gres_ctx->plugin_id;
		gres_ei->node_list = xstrdup(node_list);
		list_append(epilog_gres_list, gres_ei);
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
extern void gres_g_epilog_set_env(char ***epilog_env_ptr,
				  List epilog_gres_list, int node_inx)
{
	ListIterator epilog_iter;
	gres_epilog_info_t *gres_ei;

	*epilog_env_ptr = NULL;
	if (!epilog_gres_list)
		return;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	epilog_iter = list_iterator_create(epilog_gres_list);
	while ((gres_ei = list_next(epilog_iter))) {
		slurm_gres_context_t *gres_ctx;
		if (!(gres_ctx = _find_context_by_id(gres_ei->plugin_id))) {
			error("%s: GRES ID %u not found in context",
			      __func__, gres_ei->plugin_id);
			continue;
		}

		if (!gres_ctx->ops.epilog_set_env)
			continue;	/* No plugin to call */
		(*(gres_ctx->ops.epilog_set_env))
			(epilog_env_ptr, gres_ei, node_inx);
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

extern void gres_validate_node_cores(gres_node_state_t *gres_ns,
				     int cores_ctld, char *node_name)
{
	int i, cores_slurmd;
	bitstr_t *new_core_bitmap;
	int log_mismatch = true;

	if (gres_ns->topo_cnt == 0)
		return;

	if (gres_ns->topo_core_bitmap == NULL) {
		error("Gres topo_core_bitmap is NULL on node %s", node_name);
		return;
	}


	for (i = 0; i < gres_ns->topo_cnt; i++) {
		if (!gres_ns->topo_core_bitmap[i])
			continue;
		cores_slurmd = bit_size(gres_ns->topo_core_bitmap[i]);
		if (cores_slurmd == cores_ctld)
			continue;
		if (log_mismatch) {
			debug("Rebuilding node %s gres core bitmap (%d != %d)",
			      node_name, cores_slurmd, cores_ctld);
			log_mismatch = false;
		}
		new_core_bitmap = _core_bitmap_rebuild(
			gres_ns->topo_core_bitmap[i],
			cores_ctld);
		FREE_NULL_BITMAP(gres_ns->topo_core_bitmap[i]);
		gres_ns->topo_core_bitmap[i] = new_core_bitmap;
	}
}

static uint32_t _job_test(gres_state_t *gres_state_job,
			  gres_state_t *gres_state_node,
			  bool use_total_gres, bitstr_t *core_bitmap,
			  int core_start_bit, int core_end_bit, bool *topo_set,
			  uint32_t job_id, char *node_name,
			  bool disable_binding)
{
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	gres_node_state_t *gres_ns = gres_state_node->gres_data;
	char *gres_name = gres_state_job->gres_name;
	int i, j, core_size, core_ctld, top_inx = -1;
	uint64_t gres_avail = 0, gres_max = 0, gres_total, gres_tmp;
	uint64_t min_gres_node = 0;
	uint32_t *cores_addnt = NULL; /* Additional cores avail from this GRES */
	uint32_t *cores_avail = NULL; /* cores initially avail from this GRES */
	uint32_t core_cnt = 0;
	bitstr_t *alloc_core_bitmap = NULL;
	bitstr_t *avail_core_bitmap = NULL;
	bool shared_gres = gres_id_shared(gres_state_job->config_flags);
	bool use_busy_dev;

	if (gres_ns->no_consume)
		use_total_gres = true;

	use_busy_dev = gres_use_busy_dev(gres_state_node, use_total_gres);

	/* Determine minimum GRES count needed on this node */
	if (gres_js->gres_per_job)
		min_gres_node = 1;
	min_gres_node = MAX(min_gres_node, gres_js->gres_per_node);
	min_gres_node = MAX(min_gres_node, gres_js->gres_per_socket);
	min_gres_node = MAX(min_gres_node, gres_js->gres_per_task);

	if (min_gres_node && gres_ns->topo_cnt && *topo_set) {
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
			gres_validate_node_cores(gres_ns, core_ctld,
						 node_name);
		}
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			if (gres_js->type_name &&
			    (!gres_ns->topo_type_name[i] ||
			     (gres_ns->topo_type_id[i] !=
			      gres_js->type_id)))
				continue;
			if (use_busy_dev &&
			    (gres_ns->topo_gres_cnt_alloc[i] == 0))
				continue;
			if (!gres_ns->topo_core_bitmap[i]) {
				gres_avail += gres_ns->
					topo_gres_cnt_avail[i];
				if (!use_total_gres) {
					gres_avail -= gres_ns->
						topo_gres_cnt_alloc[i];
				}
				if (shared_gres)
					gres_max = MAX(gres_max, gres_avail);
				continue;
			}
			core_ctld = bit_size(gres_ns->
					     topo_core_bitmap[i]);
			for (j = 0; j < core_ctld; j++) {
				if (core_bitmap &&
				    !bit_test(core_bitmap, core_start_bit + j))
					continue;
				if (!bit_test(gres_ns->
					      topo_core_bitmap[i], j))
					continue; /* not avail for this gres */
				gres_avail += gres_ns->
					topo_gres_cnt_avail[i];
				if (!use_total_gres) {
					gres_avail -= gres_ns->
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
	} else if (min_gres_node && gres_ns->topo_cnt &&
		   !disable_binding) {
		/* Need to determine which specific cores can be used */
		gres_avail = gres_ns->gres_cnt_avail;
		if (!use_total_gres)
			gres_avail -= gres_ns->gres_cnt_alloc;
		if (min_gres_node > gres_avail)
			return (uint32_t) 0;	/* insufficient GRES avail */

		core_ctld = core_end_bit - core_start_bit + 1;
		if (core_bitmap) {
			if (core_ctld < 1) {
				error("gres/%s: job %u cores on node %s < 1",
				      gres_name, job_id, node_name);
				return (uint32_t) 0;
			}
			gres_validate_node_cores(gres_ns, core_ctld,
						 node_name);
		} else {
			for (i = 0; i < gres_ns->topo_cnt; i++) {
				if (!gres_ns->topo_core_bitmap[i])
					continue;
				core_ctld = bit_size(gres_ns->
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
		cores_addnt = xcalloc(gres_ns->topo_cnt,
				      sizeof(uint32_t));
		cores_avail = xcalloc(gres_ns->topo_cnt,
				      sizeof(uint32_t));
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			if (gres_ns->topo_gres_cnt_avail[i] == 0)
				continue;
			if (use_busy_dev &&
			    (gres_ns->topo_gres_cnt_alloc[i] == 0))
				continue;
			if (!use_total_gres &&
			    (gres_ns->topo_gres_cnt_alloc[i] >=
			     gres_ns->topo_gres_cnt_avail[i]))
				continue;
			if (gres_js->type_name &&
			    (!gres_ns->topo_type_name[i] ||
			     (gres_ns->topo_type_id[i] !=
			      gres_js->type_id)))
				continue;
			if (!gres_ns->topo_core_bitmap[i]) {
				cores_avail[i] = core_end_bit -
					core_start_bit + 1;
				continue;
			}
			core_size = bit_size(gres_ns->topo_core_bitmap[i]);
			for (j = 0; j < core_size; j++) {
				if (core_bitmap &&
				    !bit_test(core_bitmap, core_start_bit + j))
					continue;
				if (bit_test(gres_ns->
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
			for (j = 0; j < gres_ns->topo_cnt; j++) {
				if ((gres_avail == 0) ||
				    (cores_avail[j] == 0) ||
				    !gres_ns->topo_core_bitmap[j]) {
					cores_addnt[j] = cores_avail[j];
				} else {
					cores_addnt[j] = cores_avail[j] -
						bit_overlap(alloc_core_bitmap,
							    gres_ns->
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
			gres_tmp = gres_ns->topo_gres_cnt_avail[top_inx];
			if (!use_total_gres &&
			    (gres_tmp >=
			     gres_ns->topo_gres_cnt_alloc[top_inx])) {
				gres_tmp -= gres_ns->
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
			} else if (!gres_ns->topo_core_bitmap[top_inx]) {
				bit_nset(alloc_core_bitmap, 0, core_ctld - 1);
			} else if (gres_avail) {
				bit_or(alloc_core_bitmap,
				       gres_ns->
				       topo_core_bitmap[top_inx]);
				if (core_bitmap)
					bit_and(alloc_core_bitmap,
						avail_core_bitmap);
			} else {
				bit_and(alloc_core_bitmap,
					gres_ns->
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
			if (!gres_ns->topo_core_bitmap[top_inx]) {
				bit_nset(alloc_core_bitmap, 0, core_ctld - 1);
			} else {
				bit_or(alloc_core_bitmap,
				       gres_ns->
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
	} else if (gres_js->type_name) {
		for (i = 0; i < gres_ns->type_cnt; i++) {
			if (gres_ns->type_name[i] &&
			    (gres_ns->type_id[i] ==
			     gres_js->type_id))
				break;
		}
		if (i >= gres_ns->type_cnt)
			return (uint32_t) 0;	/* no such type */
		gres_avail = gres_ns->type_cnt_avail[i];
		if (!use_total_gres)
			gres_avail -= gres_ns->type_cnt_alloc[i];
		gres_tmp = gres_ns->gres_cnt_avail;
		if (!use_total_gres)
			gres_tmp -= gres_ns->gres_cnt_alloc;
		gres_avail = MIN(gres_avail, gres_tmp);
		if (min_gres_node > gres_avail)
			return (uint32_t) 0;	/* insufficient GRES avail */
		return NO_VAL;
	} else {
		gres_avail = gres_ns->gres_cnt_avail;
		if (!use_total_gres)
			gres_avail -= gres_ns->gres_cnt_alloc;
		if (min_gres_node > gres_avail)
			return (uint32_t) 0;	/* insufficient GRES avail */
		return NO_VAL;
	}
}

/*
 * Determine how many cores on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_job_state_validate()
 * IN node_gres_list - node's gres_list built by gres_node_config_validate()
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
extern uint32_t gres_job_test(List job_gres_list, List node_gres_list,
			      bool use_total_gres, bitstr_t *core_bitmap,
			      int core_start_bit, int core_end_bit,
			      uint32_t job_id, char *node_name,
			      bool disable_binding)
{
	uint32_t core_cnt, tmp_cnt;
	ListIterator job_gres_iter;
	gres_state_t *gres_state_job, *gres_state_node;
	bool topo_set = false;

	if (job_gres_list == NULL)
		return NO_VAL;
	if (node_gres_list == NULL)
		return 0;

	core_cnt = NO_VAL;
	(void) gres_init();

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		gres_state_node = list_find_first(node_gres_list, gres_find_id,
						  &gres_state_job->plugin_id);
		if (gres_state_node == NULL) {
			/* node lack resources required by the job */
			core_cnt = 0;
			break;
		}

		tmp_cnt = _job_test(gres_state_job, gres_state_node,
				    use_total_gres, core_bitmap,
				    core_start_bit, core_end_bit,
				    &topo_set, job_id, node_name,
				    disable_binding);
		if (tmp_cnt != NO_VAL) {
			if (core_cnt == NO_VAL)
				core_cnt = tmp_cnt;
			else
				core_cnt = MIN(tmp_cnt, core_cnt);
		}

		if (core_cnt == 0)
			break;
	}
	list_iterator_destroy(job_gres_iter);

	return core_cnt;
}

extern void gres_sock_delete(void *x)
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
		/* NOTE: sock_gres->job_specs is just a pointer, do not free */
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
extern char *gres_sock_str(List sock_gres_list, int sock_inx)
{
	ListIterator iter;
	sock_gres_t *sock_gres;
	char *gres_str = NULL, *sep = "";

	if (!sock_gres_list)
		return NULL;

	iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(iter))) {
		char *gres_name = sock_gres->gres_state_job->gres_name;
		gres_job_state_t *gres_js =
			sock_gres->gres_state_job->gres_data;
		char *type_name = gres_js->type_name;

		if (sock_inx < 0) {
			if (sock_gres->cnt_any_sock) {
				if (type_name) {
					xstrfmtcat(gres_str, "%s%s:%s:%"PRIu64,
						   sep,
						   gres_name,
						   type_name,
						   sock_gres->cnt_any_sock);
				} else {
					xstrfmtcat(gres_str, "%s%s:%"PRIu64,
						   sep, gres_name,
						   sock_gres->cnt_any_sock);
				}
				sep = " ";
			}
			continue;
		}
		if (!sock_gres->cnt_by_sock ||
		    (sock_gres->cnt_by_sock[sock_inx] == 0))
			continue;
		if (type_name) {
			xstrfmtcat(gres_str, "%s%s:%s:%"PRIu64, sep,
				   gres_name, type_name,
				   sock_gres->cnt_by_sock[sock_inx]);
		} else {
			xstrfmtcat(gres_str, "%s%s:%"PRIu64, sep,
				   gres_name,
				   sock_gres->cnt_by_sock[sock_inx]);
		}
		sep = " ";
	}
	list_iterator_destroy(iter);
	return gres_str;
}

static void _accumulate_job_gres_alloc(gres_job_state_t *gres_js,
				       int node_inx,
				       bitstr_t **gres_bit_alloc,
				       uint64_t *gres_cnt)
{
	if (gres_js->node_cnt <= node_inx) {
		error("gres_job_state_t node count less than node_inx. This should never happen");
		return;
	}

	if ((node_inx >= 0) && (node_inx < gres_js->node_cnt) &&
	    gres_js->gres_bit_alloc &&
	    gres_js->gres_bit_alloc[node_inx]) {
		if (!*gres_bit_alloc) {
			*gres_bit_alloc = bit_alloc(
				bit_size(gres_js->
					 gres_bit_alloc[node_inx]));
		}
		bit_or(*gres_bit_alloc, gres_js->gres_bit_alloc[node_inx]);
	}
	if (gres_cnt && gres_js->gres_cnt_node_alloc)
		*gres_cnt += gres_js->gres_cnt_node_alloc[node_inx];
}

/*
 * Set environment variables as required for a batch job
 * IN/OUT job_env_ptr - environment variable array
 * IN gres_list - generated by gres_job_alloc()
 * IN node_inx - zero origin node index
 */
extern void gres_g_job_set_env(char ***job_env_ptr, List job_gres_list,
			       int node_inx)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_state_job = NULL;
	uint64_t gres_cnt = 0;
	bitstr_t *gres_bit_alloc = NULL;
	bool sharing_gres_alloced = false;
	gres_internal_flags_t flags = GRES_INTERNAL_FLAG_NONE;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i=0; i<gres_context_cnt; i++) {
		if (gres_context[i].ops.job_set_env == NULL)
			continue;	/* No plugin to call */
		if (job_gres_list) {
			gres_iter = list_iterator_create(job_gres_list);
			while ((gres_state_job = (gres_state_t *)
				list_next(gres_iter))) {
				if (gres_state_job->plugin_id !=
				    gres_context[i].plugin_id)
					continue;
				_accumulate_job_gres_alloc(
					gres_state_job->gres_data,
					node_inx,
					&gres_bit_alloc,
					&gres_cnt);
				/* Does job have a sharing GRES (GPU)? */
				if (gres_id_sharing(gres_context[i].plugin_id))
					sharing_gres_alloced = true;
			}
			list_iterator_destroy(gres_iter);
		}

		/*
		 * Do not let MPS or Shard (shared GRES) clear any envs set for
		 * a GPU (sharing GRES) when a GPU is allocated but an
		 * MPS/Shard is not. Sharing GRES plugins always run before
		 * shared GRES, so we don't need to protect MPS/Shard from GPU.
		 */
		if (gres_id_shared(gres_context[i].config_flags) &&
		    sharing_gres_alloced)
			flags |= GRES_INTERNAL_FLAG_PROTECT_ENV;

		(*(gres_context[i].ops.job_set_env))(job_env_ptr,
						     gres_bit_alloc, gres_cnt,
						     flags);
		gres_cnt = 0;
		FREE_NULL_BITMAP(gres_bit_alloc);
	}
	slurm_mutex_unlock(&gres_context_lock);
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

static void _job_state_log(gres_state_t *gres_state_job, uint32_t job_id)
{
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	char *sparse_msg = "", tmp_str[128];
	int i;

	xassert(gres_js);
	info("gres_job_state gres:%s(%u) type:%s(%u) job:%u flags:%s",
	     gres_state_job->gres_name, gres_state_job->plugin_id,
	     gres_js->type_name,
	     gres_js->type_id, job_id, _gres_flags_str(gres_js->flags));
	if (gres_js->cpus_per_gres)
		info("  cpus_per_gres:%u", gres_js->cpus_per_gres);
	else if (gres_js->def_cpus_per_gres)
		info("  def_cpus_per_gres:%u", gres_js->def_cpus_per_gres);
	if (gres_js->gres_per_job)
		info("  gres_per_job:%"PRIu64, gres_js->gres_per_job);
	if (gres_js->gres_per_node) {
		info("  gres_per_node:%"PRIu64" node_cnt:%u",
		     gres_js->gres_per_node, gres_js->node_cnt);
	}
	if (gres_js->gres_per_socket)
		info("  gres_per_socket:%"PRIu64, gres_js->gres_per_socket);
	if (gres_js->gres_per_task)
		info("  gres_per_task:%"PRIu64, gres_js->gres_per_task);
	if (gres_js->mem_per_gres)
		info("  mem_per_gres:%"PRIu64, gres_js->mem_per_gres);
	else if (gres_js->def_mem_per_gres)
		info("  def_mem_per_gres:%"PRIu64, gres_js->def_mem_per_gres);
	if (gres_js->ntasks_per_gres)
		info("  ntasks_per_gres:%u", gres_js->ntasks_per_gres);

	/*
	 * These arrays are only used for resource selection and may include
	 * data for many nodes not used in the resources eventually allocated
	 * to this job.
	 */
	if (gres_js->total_node_cnt) {
		sparse_msg = " (sparsely populated for resource selection)";
		info("  total_node_cnt:%u%s", gres_js->total_node_cnt,
		     sparse_msg);
	}
	for (i = 0; i < gres_js->total_node_cnt; i++) {
		if (gres_js->gres_cnt_node_select &&
		    gres_js->gres_cnt_node_select[i]) {
			info("  gres_cnt_node_select[%d]:%"PRIu64,
			     i, gres_js->gres_cnt_node_select[i]);
		}
		if (gres_js->gres_bit_select &&
		    gres_js->gres_bit_select[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_js->gres_bit_select[i]);
			info("  gres_bit_select[%d]:%s of %d", i, tmp_str,
			     (int) bit_size(gres_js->gres_bit_select[i]));
		}
	}

	if (gres_js->total_gres)
		info("  total_gres:%"PRIu64, gres_js->total_gres);
	if (gres_js->node_cnt)
		info("  node_cnt:%u", gres_js->node_cnt);
	for (i = 0; i < gres_js->node_cnt; i++) {
		if (gres_js->gres_cnt_node_alloc &&
		    gres_js->gres_cnt_node_alloc[i]) {
			info("  gres_cnt_node_alloc[%d]:%"PRIu64,
			     i, gres_js->gres_cnt_node_alloc[i]);
		} else if (gres_js->gres_cnt_node_alloc)
			info("  gres_cnt_node_alloc[%d]:NULL", i);

		if (gres_js->gres_bit_alloc && gres_js->gres_bit_alloc[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_js->gres_bit_alloc[i]);
			info("  gres_bit_alloc[%d]:%s of %d", i, tmp_str,
			     (int) bit_size(gres_js->gres_bit_alloc[i]));
		} else if (gres_js->gres_bit_alloc)
			info("  gres_bit_alloc[%d]:NULL", i);

		if (gres_js->gres_bit_step_alloc &&
		    gres_js->gres_bit_step_alloc[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_js->gres_bit_step_alloc[i]);
			info("  gres_bit_step_alloc[%d]:%s of %d", i, tmp_str,
			     (int) bit_size(gres_js->gres_bit_step_alloc[i]));
		} else if (gres_js->gres_bit_step_alloc)
			info("  gres_bit_step_alloc[%d]:NULL", i);

		if (gres_js->gres_cnt_step_alloc) {
			info("  gres_cnt_step_alloc[%d]:%"PRIu64"", i,
			     gres_js->gres_cnt_step_alloc[i]);
		}
	}
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
	gres_state_t *gres_state_ptr;
	bool filter_type;

	if ((gres_list == NULL) || (list_count(gres_list) == 0))
		return gres_val;

	plugin_id = gres_build_id(gres_name);

	if (gres_type && (gres_type[0] != '\0'))
		filter_type = true;
	else
		filter_type = false;

	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_ptr = list_next(gres_iter))) {
		uint64_t total_gres;
		void *type_name;

		if (gres_state_ptr->plugin_id != plugin_id)
			continue;

		if (is_job) {
			gres_job_state_t *gres_js =
				(gres_job_state_t *)gres_state_ptr->gres_data;
			type_name = gres_js->type_name;
			total_gres = gres_js->total_gres;
		} else {
			gres_step_state_t *gres_ss =
				(gres_step_state_t *)gres_state_ptr->gres_data;
			type_name = gres_ss->type_name;
			total_gres = gres_ss->total_gres;
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
 * IN gres_list - generated by gres_job_state_validate()
 * IN job_id - job's ID
 */
extern void gres_job_state_log(List gres_list, uint32_t job_id)
{
	ListIterator gres_iter;
	gres_state_t *gres_state_job;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES) || !gres_list)
		return;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(gres_iter))) {
		_job_state_log(gres_state_job, job_id);
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

static int _accumulate_gres_device(void *x, void *arg)
{
	gres_state_t *gres_ptr = x;
	foreach_gres_accumulate_device_t *args = arg;

	if (gres_ptr->plugin_id != args->plugin_id)
		return 0;

	if (args->is_job) {
		_accumulate_job_gres_alloc(gres_ptr->gres_data, 0,
					   args->gres_bit_alloc, NULL);
	} else {
		_accumulate_step_gres_alloc(gres_ptr, args->gres_bit_alloc,
					    NULL);
	}

	return 0;
}

extern List gres_g_get_devices(List gres_list, bool is_job,
			       uint16_t accel_bind_type, char *tres_bind_str,
			       int local_proc_id, pid_t pid)
{
	int j;
	ListIterator dev_itr;
	bitstr_t *gres_bit_alloc = NULL;
	gres_device_t *gres_device;
	List gres_devices;
	List device_list = NULL;
	bitstr_t *usable_gres = NULL;
	tres_bind_t tres_bind;

	(void) gres_init();

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

	if (accel_bind_type || tres_bind_str)
		_parse_tres_bind(accel_bind_type, tres_bind_str, &tres_bind);
	else
		memset(&tres_bind, 0, sizeof(tres_bind));

	slurm_mutex_lock(&gres_context_lock);
	for (j = 0; j < gres_context_cnt; j++) {
		/* We need to get a gres_bit_alloc with all the gres types
		 * merged (accumulated) together */
		foreach_gres_accumulate_device_t args = {
			.gres_bit_alloc = &gres_bit_alloc,
			.is_job = is_job,
			.plugin_id = gres_context[j].plugin_id,
		};
		(void) list_for_each(gres_list, _accumulate_gres_device, &args);

		if (!gres_bit_alloc ||
		    !gres_context[j].ops.get_devices)
			continue;

		gres_devices = (*(gres_context[j].ops.get_devices))();
		if (!gres_devices) {
			error("We should had got gres_devices, but for some reason none were set in the plugin.");
			continue;
		}

		if (_get_usable_gres(gres_context[j].gres_name, j,
				     local_proc_id, pid, &tres_bind,
				     &usable_gres, gres_bit_alloc,
				     true) == SLURM_ERROR)
			continue;

		dev_itr = list_iterator_create(gres_devices);
		while ((gres_device = list_next(dev_itr))) {
			if (!bit_test(gres_bit_alloc, gres_device->index))
				continue;

			if (!usable_gres ||
			    bit_test(usable_gres, gres_device->index)) {
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
		FREE_NULL_BITMAP(gres_bit_alloc);
		FREE_NULL_BITMAP(usable_gres);
	}
	slurm_mutex_unlock(&gres_context_lock);

	return device_list;
}

static void _step_state_delete(void *gres_data)
{
	int i;
	gres_step_state_t *gres_ss = (gres_step_state_t *) gres_data;

	if (gres_ss == NULL)
		return;

	FREE_NULL_BITMAP(gres_ss->node_in_use);
	if (gres_ss->gres_bit_alloc) {
		for (i = 0; i < gres_ss->node_cnt; i++)
			FREE_NULL_BITMAP(gres_ss->gres_bit_alloc[i]);
		xfree(gres_ss->gres_bit_alloc);
	}
	xfree(gres_ss->gres_cnt_node_alloc);
	xfree(gres_ss->type_name);
	xfree(gres_ss);
}

extern void gres_step_list_delete(void *list_element)
{
	gres_state_t *gres_state_step = (gres_state_t *) list_element;

	_step_state_delete(gres_state_step->gres_data);
	gres_state_step->gres_data = NULL;
	_gres_state_delete_members(gres_state_step);
}


static int _step_get_gres_cnt(void *x, void *arg)
{
	gres_state_t *gres_state_job = (gres_state_t *)x;
	foreach_gres_cnt_t *foreach_gres_cnt = (foreach_gres_cnt_t *)arg;
	gres_job_state_t *gres_js;
	gres_key_t *job_search_key = foreach_gres_cnt->job_search_key;
	bool ignore_alloc = foreach_gres_cnt->ignore_alloc;
	slurm_step_id_t *step_id = foreach_gres_cnt->step_id;
	int node_offset = job_search_key->node_offset;

	/* This isn't the gres we are looking for */
	if (!gres_find_job_by_key_with_cnt(gres_state_job, job_search_key))
		return 0;

	/* This is the first time we have found a matching GRES. */
	if (foreach_gres_cnt->gres_cnt == INFINITE64)
		foreach_gres_cnt->gres_cnt = 0;

	gres_js = gres_state_job->gres_data;

	if (gres_js->total_gres == NO_CONSUME_VAL64) {
		foreach_gres_cnt->gres_cnt = NO_CONSUME_VAL64;
		return -1;
	}

	if ((node_offset >= gres_js->node_cnt)) {
		error("gres/%s: %s %ps node offset invalid (%d >= %u)",
		      gres_state_job->gres_name, __func__, step_id,
		      node_offset, gres_js->node_cnt);
		foreach_gres_cnt->gres_cnt = 0;
		return -1;
	}
	if (!gres_id_shared(job_search_key->config_flags) &&
	    gres_js->gres_bit_alloc &&
	    gres_js->gres_bit_alloc[node_offset]) {
		foreach_gres_cnt->gres_cnt += bit_set_count(
			gres_js->gres_bit_alloc[node_offset]);
		if (!ignore_alloc &&
		    gres_js->gres_bit_step_alloc &&
		    gres_js->gres_bit_step_alloc[node_offset]) {
			foreach_gres_cnt->gres_cnt -=
				bit_set_count(gres_js->
					      gres_bit_step_alloc[node_offset]);
		}
	} else if (gres_js->gres_cnt_node_alloc &&
		   gres_js->gres_cnt_step_alloc) {
		foreach_gres_cnt->gres_cnt +=
			gres_js->gres_cnt_node_alloc[node_offset];
		if (!ignore_alloc) {
			foreach_gres_cnt->gres_cnt -= gres_js->
				gres_cnt_step_alloc[node_offset];
		}
	} else {
		debug3("gres/%s:%s: %s %ps gres_bit_alloc and gres_cnt_node_alloc are NULL",
		       gres_state_job->gres_name, gres_js->type_name,
		       __func__, step_id);
		foreach_gres_cnt->gres_cnt = NO_VAL64;
		return -1;
	}
	return 0;
}

static uint64_t _step_test(gres_step_state_t *gres_ss, bool first_step_node,
			   uint16_t cpus_per_task, int max_rem_nodes,
			   bool ignore_alloc, uint64_t gres_cnt, bool test_mem,
			   int node_offset, slurm_step_id_t *step_id,
			   job_resources_t *job_resrcs_ptr, int *err_code)
{
	uint64_t core_cnt, min_gres = 1, task_cnt;

	xassert(gres_ss);

	if (!gres_cnt)
		return 0;

	if (first_step_node) {
		gres_ss->gross_gres = 0;
		gres_ss->total_gres = 0;
	}
	if (gres_ss->gres_per_node)
		min_gres = gres_ss-> gres_per_node;
	if (gres_ss->gres_per_socket)
		min_gres = MAX(min_gres, gres_ss->gres_per_socket);
	if (gres_ss->gres_per_task)
		min_gres = MAX(min_gres, gres_ss->gres_per_task);
	if (gres_ss->gres_per_step &&
	    (gres_ss->gres_per_step > gres_ss->total_gres) &&
	    (max_rem_nodes == 1)) {
		uint64_t gres_per_step = gres_ss->gres_per_step;
		if (ignore_alloc)
			gres_per_step -= gres_ss->gross_gres;
		else
			gres_per_step -= gres_ss->total_gres;
		min_gres = MAX(min_gres, gres_per_step);
	}

	if (gres_cnt != NO_VAL64) {
		if (min_gres > gres_cnt) {
			core_cnt = 0;
		} else if (gres_ss->gres_per_task) {
			task_cnt = (gres_cnt + gres_ss->gres_per_task - 1)
				/ gres_ss->gres_per_task;
			core_cnt = task_cnt * cpus_per_task;
		} else
			core_cnt = NO_VAL64;
	} else {
		gres_cnt = 0;
		core_cnt = NO_VAL64;
	}

	/* Test if there is enough memory available to run the step. */
	if (test_mem && core_cnt && gres_cnt && gres_ss->mem_per_gres &&
	    (gres_ss->mem_per_gres != NO_VAL64)) {
		uint64_t mem_per_gres, mem_req, mem_avail;

		mem_per_gres = gres_ss->mem_per_gres;
		mem_req = min_gres * mem_per_gres;
		mem_avail = job_resrcs_ptr->memory_allocated[node_offset];
		if (!ignore_alloc)
			mem_avail -= job_resrcs_ptr->memory_used[node_offset];

		if (mem_avail < mem_req) {
			log_flag(STEPS, "%s: JobId=%u: Usable memory on node: %"PRIu64" is less than requested %"PRIu64", skipping the node",
				 __func__, step_id->job_id, mem_avail,
				 mem_req);
			core_cnt = 0;
			*err_code = ESLURM_INVALID_TASK_MEMORY;
		}
	}

	if (core_cnt != 0) {
		if (ignore_alloc)
			gres_ss->gross_gres += gres_cnt;
		else
			gres_ss->total_gres += gres_cnt;
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
	gres_step_state_t *gres_ss = NULL;
	gres_state_t *gres_state_step;
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
	step_search_key.config_flags = gres_context[context_inx].config_flags;
	step_search_key.plugin_id = gres_context[context_inx].plugin_id;
	step_search_key.type_id = gres_build_id(type);
	gres_state_step = list_find_first(gres_list, gres_find_step_by_key,
					  &step_search_key);

	if (gres_state_step) {
		gres_ss = gres_state_step->gres_data;
	} else {
		gres_ss = xmalloc(sizeof(gres_step_state_t));
		gres_ss->type_id = gres_build_id(type);
		gres_ss->type_name = type;
		type = NULL;	/* String moved above */
		gres_state_step = gres_create_state(
			&gres_context[context_inx], GRES_STATE_SRC_CONTEXT_PTR,
			GRES_STATE_TYPE_STEP, gres_ss);
		list_append(gres_list, gres_state_step);
	}
	gres_ss->flags = flags;

fini:	xfree(name);
	xfree(type);
	if (my_rc != SLURM_SUCCESS) {
		prev_save_ptr = NULL;
		if (my_rc == ESLURM_INVALID_GRES && running_in_slurmctld())
			info("Invalid GRES step specification %s", in_val);
		*rc = my_rc;
	}
	*save_ptr = prev_save_ptr;
	return gres_ss;
}

/*
 * Test that the step does not request more GRES than what the job requested.
 * This function does *not* check the step's requested GRES against the job's
 * allocated GRES. The job may be allocated more GRES than what it requested
 * (for example, when --exclusive is used the job is allocated all the GRES
 * on the node), but that's okay. We check the step request against the job's
 * allocated GRES in gres_step_test().
 */
static void _validate_step_counts(List step_gres_list, List job_gres_list_req,
				  uint32_t step_min_nodes, int *rc,
				  char **err_msg)
{
	ListIterator iter;
	gres_state_t *gres_state_job, *gres_state_step;
	gres_job_state_t *gres_js;
	gres_step_state_t *gres_ss;
	gres_key_t job_search_key;
	uint16_t cpus_per_gres;
	char *msg = NULL;

	if (!step_gres_list || (list_count(step_gres_list) == 0))
		return;
	if (!job_gres_list_req  || (list_count(job_gres_list_req)  == 0)) {
		if (err_msg) {
			xfree(*err_msg);
			xstrfmtcat(*err_msg, "Step requested GRES but job doesn't have GRES");
		}
		*rc = ESLURM_INVALID_GRES;
		return;
	}

	iter = list_iterator_create(step_gres_list);
	while ((gres_state_step = (gres_state_t *) list_next(iter))) {
		gres_ss = (gres_step_state_t *) gres_state_step->gres_data;
		job_search_key.config_flags = gres_state_step->config_flags;
		job_search_key.plugin_id = gres_state_step->plugin_id;
		if (gres_ss->type_id == 0)
			job_search_key.type_id = NO_VAL;
		else
			job_search_key.type_id = gres_ss->type_id;
		gres_state_job = list_find_first(job_gres_list_req,
						 gres_find_job_by_key,
						 &job_search_key);
		if (!gres_state_job || !gres_state_job->gres_data) {
			xstrfmtcat(msg, "Step requested GRES (%s:%s) not found in the job",
				   gres_state_step->gres_name,
				   gres_ss->type_name);
			*rc = ESLURM_INVALID_GRES;
			break;
		}
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		if (gres_js->cpus_per_gres)
			cpus_per_gres = gres_js->cpus_per_gres;
		else
			cpus_per_gres = gres_js->def_cpus_per_gres;
		if (cpus_per_gres && gres_ss->cpus_per_gres &&
		    (cpus_per_gres < gres_ss->cpus_per_gres)) {
			xstrfmtcat(msg, "Step requested cpus_per_gres=%u is more than the job's cpu_per_gres=%u",
				   gres_ss->cpus_per_gres,
				   cpus_per_gres);
			*rc = ESLURM_INVALID_GRES;
			break;
		}
		if (gres_ss->gres_per_step) {
			/*
			 * This isn't a perfect check because step_min_nodes
			 * isn't always set by this point, but if it is set
			 * then we can check that the number of gres requested
			 * for the step is at least the number of nodes in
			 * the step.
			 */
			if (step_min_nodes &&
			    (gres_ss->gres_per_step < step_min_nodes)) {
				xstrfmtcat(msg, "Step requested gres=%s:%"PRIu64" is less than the requested min nodes=%u",
					   gres_state_step->gres_name,
					   gres_ss->gres_per_step,
					   step_min_nodes);
				*rc = ESLURM_INVALID_GRES;
				break;
			}
			if (gres_js->gres_per_job &&
			    (gres_js->gres_per_job <
			     gres_ss->gres_per_step)) {
				xstrfmtcat(msg, "Step requested gres=%s:%"PRIu64" is more than the job's gres=%s:%"PRIu64,
					   gres_state_step->gres_name,
					   gres_ss->gres_per_step,
					   gres_state_job->gres_name,
					   gres_js->gres_per_job);
				*rc = ESLURM_INVALID_GRES;
				break;
			}
		}
		if (gres_js->gres_per_node &&
		    gres_ss->gres_per_node &&
		    (gres_js->gres_per_node <
		     gres_ss->gres_per_node)) {
			xstrfmtcat(msg, "Step requested gres_per_node=%s:%"PRIu64" is more than the job's gres_per_node=%s:%"PRIu64,
				   gres_state_step->gres_name,
				   gres_ss->gres_per_node,
				   gres_state_job->gres_name,
				   gres_js->gres_per_node);
			*rc = ESLURM_INVALID_GRES;
			break;
		}
		if (gres_js->gres_per_socket &&
		    gres_ss->gres_per_socket &&
		    (gres_js->gres_per_socket <
		     gres_ss->gres_per_socket)) {
			xstrfmtcat(msg, "Step requested gres_per_socket=%s:%"PRIu64" is more than the job's gres_per_socket=%s:%"PRIu64,
				   gres_state_step->gres_name,
				   gres_ss->gres_per_socket,
				   gres_state_job->gres_name,
				   gres_js->gres_per_socket);
			*rc = ESLURM_INVALID_GRES;
			break;
		}
		if (gres_js->gres_per_task &&
		    gres_ss->gres_per_task &&
		    (gres_js->gres_per_task <
		     gres_ss->gres_per_task)) {
			xstrfmtcat(msg, "Step requested gres_per_task=%s:%"PRIu64" is more than the job's gres_per_task=%s:%"PRIu64,
				   gres_state_step->gres_name,
				   gres_ss->gres_per_task,
				   gres_state_job->gres_name,
				   gres_js->gres_per_task);
			*rc = ESLURM_INVALID_GRES;
			break;
		}

	}
	list_iterator_destroy(iter);

	if (msg) {
		if (err_msg) {
			xfree(*err_msg);
			*err_msg = msg;
		} else {
			error("%s", msg);
			xfree(msg);
		}
	}
}


static int _handle_ntasks_per_tres_step(List new_step_list,
					uint16_t ntasks_per_tres,
					uint32_t *num_tasks,
					uint32_t *cpu_count)
{
	gres_step_state_t *gres_ss;
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
		xstrfmtcat(gres, "gres:gpu:%u", gpus);
		in_val = gres;
		if (*num_tasks != ntasks_per_tres * gpus) {
			log_flag(GRES, "%s: -n/--ntasks %u is not a multiple of --ntasks-per-gpu=%u",
				 __func__, *num_tasks, ntasks_per_tres);
			return ESLURM_INVALID_GRES;
		}
		while ((gres_ss =
			_get_next_step_gres(in_val, &cnt,
					    new_step_list,
					    &save_ptr, &rc))) {
			/* Simulate a tres_per_job specification */
			gres_ss->gres_per_step = cnt;
			gres_ss->ntasks_per_gres = ntasks_per_tres;
			gres_ss->total_gres =
				MAX(gres_ss->total_gres, cnt);
			in_val = NULL;
		}
		xfree(gres);
		xassert(list_count(new_step_list) != 0);
	} else if (tmp != NO_VAL64) {
		tmp = tmp * ntasks_per_tres;
		if (*num_tasks < tmp) {
			*num_tasks = tmp;
		}
		if (*cpu_count && (*cpu_count < tmp)) {
			/* step_spec->cpu_count == 0 means SSF_OVERSUBSCRIBE */
			*cpu_count = tmp;
		}
	} else {
		error("%s: ntasks_per_tres was specified, but there was either no task count or no GPU specification to go along with it, or both were already specified.",
		      __func__);
		rc = SLURM_ERROR;
	}

	return rc;
}

extern int gres_step_state_validate(char *cpus_per_tres,
				    char *tres_per_step,
				    char *tres_per_node,
				    char *tres_per_socket,
				    char *tres_per_task,
				    char *mem_per_tres,
				    uint16_t ntasks_per_tres,
				    uint32_t step_min_nodes,
				    List *step_gres_list,
				    List job_gres_list_req, uint32_t job_id,
				    uint32_t step_id,
				    uint32_t *num_tasks,
				    uint32_t *cpu_count, char **err_msg)
{
	int rc;
	gres_step_state_t *gres_ss;
	List new_step_list;
	uint64_t cnt = 0;

	*step_gres_list = NULL;
	if ((rc = gres_init()) != SLURM_SUCCESS)
		return rc;

	slurm_mutex_lock(&gres_context_lock);
	new_step_list = list_create(gres_step_list_delete);
	if (cpus_per_tres) {
		char *in_val = cpus_per_tres, *save_ptr = NULL;
		while ((gres_ss = _get_next_step_gres(in_val, &cnt,
						      new_step_list,
						      &save_ptr, &rc))) {
			gres_ss->cpus_per_gres = cnt;
			in_val = NULL;
		}
	}
	if (tres_per_step) {
		char *in_val = tres_per_step, *save_ptr = NULL;
		while ((gres_ss = _get_next_step_gres(in_val, &cnt,
						      new_step_list,
						      &save_ptr, &rc))) {
			gres_ss->gres_per_step = cnt;
			in_val = NULL;
			gres_ss->total_gres =
				MAX(gres_ss->total_gres, cnt);
		}
	}
	if (tres_per_node) {
		char *in_val = tres_per_node, *save_ptr = NULL;
		while ((gres_ss = _get_next_step_gres(in_val, &cnt,
						      new_step_list,
						      &save_ptr, &rc))) {
			gres_ss->gres_per_node = cnt;
			in_val = NULL;
			/* Step only has 1 node, always */
			gres_ss->total_gres =
				MAX(gres_ss->total_gres, cnt);
		}
	}
	if (tres_per_socket) {
		char *in_val = tres_per_socket, *save_ptr = NULL;
		while ((gres_ss = _get_next_step_gres(in_val, &cnt,
						      new_step_list,
						      &save_ptr, &rc))) {
			gres_ss->gres_per_socket = cnt;
			in_val = NULL;
			// TODO: What is sockets_per_node and ntasks_per_socket?
			// if (*sockets_per_node != NO_VAL16) {
			//	cnt *= *sockets_per_node;
			// } else if ((*num_tasks != NO_VAL) &&
			//	   (*ntasks_per_socket != NO_VAL16)) {
			//	cnt *= ((*num_tasks + *ntasks_per_socket - 1) /
			//		*ntasks_per_socket);
			// }
			// gres_ss->total_gres =
			//	MAX(gres_ss->total_gres, cnt);
		}
	}
	if (tres_per_task) {
		char *in_val = tres_per_task, *save_ptr = NULL;
		while ((gres_ss = _get_next_step_gres(in_val, &cnt,
						      new_step_list,
						      &save_ptr, &rc))) {
			gres_ss->gres_per_task = cnt;
			in_val = NULL;
			if (*num_tasks != NO_VAL)
				cnt *= *num_tasks;
			gres_ss->total_gres =
				MAX(gres_ss->total_gres, cnt);
		}
	}
	if (mem_per_tres) {
		char *in_val = mem_per_tres, *save_ptr = NULL;
		while ((gres_ss = _get_next_step_gres(in_val, &cnt,
						      new_step_list,
						      &save_ptr, &rc))) {
			gres_ss->mem_per_gres = cnt;
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
		/*
		 * If called from a client we don't have a job_gres_list_req so
		 * don't check against that.
		 */
		if (rc == SLURM_SUCCESS && running_in_slurmctld())
			_validate_step_counts(new_step_list, job_gres_list_req,
					      step_min_nodes, &rc, err_msg);
		if (rc == SLURM_SUCCESS) {
			bool overlap_merge = false;
			int over_count = 0;
			gres_state_t *gres_state;
			overlap_check_t *over_list =
				xcalloc(list_count(new_step_list),
					sizeof(overlap_check_t));
			ListIterator iter = list_iterator_create(new_step_list);
			while ((gres_state = list_next(iter))) {
				if (_set_over_list(gres_state, over_list,
						   &over_count, 0))
					overlap_merge = true;

			}
			list_iterator_destroy(iter);
			if (overlap_merge)
				rc = _merge_generic_data(new_step_list,
							 over_list,
							 over_count,
							 0);
			xfree(over_list);
		}
		if (rc == SLURM_SUCCESS)
			*step_gres_list = new_step_list;
		else
			FREE_NULL_LIST(new_step_list);
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;
}

static void *_step_state_dup(gres_step_state_t *gres_ss)
{

	int i;
	gres_step_state_t *new_gres_ss;

	xassert(gres_ss);
	new_gres_ss = xmalloc(sizeof(gres_step_state_t));
	new_gres_ss->cpus_per_gres	= gres_ss->cpus_per_gres;
	new_gres_ss->gres_per_step	= gres_ss->gres_per_step;
	new_gres_ss->gres_per_node	= gres_ss->gres_per_node;
	new_gres_ss->gres_per_socket	= gres_ss->gres_per_socket;
	new_gres_ss->gres_per_task	= gres_ss->gres_per_task;
	new_gres_ss->mem_per_gres	= gres_ss->mem_per_gres;
	new_gres_ss->node_cnt		= gres_ss->node_cnt;
	new_gres_ss->total_gres	= gres_ss->total_gres;

	if (gres_ss->node_in_use)
		new_gres_ss->node_in_use = bit_copy(gres_ss->node_in_use);

	if (gres_ss->gres_cnt_node_alloc) {
		i = sizeof(uint64_t) * gres_ss->node_cnt;
		new_gres_ss->gres_cnt_node_alloc = xmalloc(i);
		memcpy(new_gres_ss->gres_cnt_node_alloc,
		       gres_ss->gres_cnt_node_alloc, i);
	}
	if (gres_ss->gres_bit_alloc) {
		new_gres_ss->gres_bit_alloc = xcalloc(gres_ss->node_cnt,
						      sizeof(bitstr_t *));
		for (i = 0; i < gres_ss->node_cnt; i++) {
			if (gres_ss->gres_bit_alloc[i] == NULL)
				continue;
			new_gres_ss->gres_bit_alloc[i] =
				bit_copy(gres_ss->gres_bit_alloc[i]);
		}
	}
	return new_gres_ss;
}

static void *_step_state_dup2(gres_step_state_t *gres_ss, int node_index)
{
	gres_step_state_t *new_gres_ss;

	xassert(gres_ss);
	new_gres_ss = xmalloc(sizeof(gres_step_state_t));
	new_gres_ss->cpus_per_gres	= gres_ss->cpus_per_gres;
	new_gres_ss->gres_per_step	= gres_ss->gres_per_step;
	new_gres_ss->gres_per_node	= gres_ss->gres_per_node;
	new_gres_ss->gres_per_socket	= gres_ss->gres_per_socket;
	new_gres_ss->gres_per_task	= gres_ss->gres_per_task;
	new_gres_ss->mem_per_gres	= gres_ss->mem_per_gres;
	new_gres_ss->node_cnt		= 1;
	new_gres_ss->total_gres	= gres_ss->total_gres;

	if (gres_ss->node_in_use)
		new_gres_ss->node_in_use = bit_copy(gres_ss->node_in_use);

	if (gres_ss->gres_cnt_node_alloc) {
		new_gres_ss->gres_cnt_node_alloc = xmalloc(sizeof(uint64_t));
		new_gres_ss->gres_cnt_node_alloc[0] =
			gres_ss->gres_cnt_node_alloc[node_index];
	}

	if ((node_index < gres_ss->node_cnt) && gres_ss->gres_bit_alloc &&
	    gres_ss->gres_bit_alloc[node_index]) {
		new_gres_ss->gres_bit_alloc = xmalloc(sizeof(bitstr_t *));
		new_gres_ss->gres_bit_alloc[0] =
			bit_copy(gres_ss->gres_bit_alloc[node_index]);
	}
	return new_gres_ss;
}

/*
 * Create a copy of a step's gres state
 * IN gres_list - List of Gres records for this step to track usage
 * RET The copy or NULL on failure
 */
List gres_step_state_list_dup(List gres_list)
{
	return gres_step_state_extract(gres_list, -1);
}

/*
 * Create a copy of a step's gres state for a particular node index
 * IN gres_list - List of Gres records for this step to track usage
 * IN node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
List gres_step_state_extract(List gres_list, int node_index)
{
	ListIterator gres_iter;
	gres_state_t *gres_state_step, *new_gres_state_step;
	List new_gres_list = NULL;
	void *new_gres_data;

	if (gres_list == NULL)
		return new_gres_list;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_step = (gres_state_t *) list_next(gres_iter))) {
		if (node_index == -1)
			new_gres_data = _step_state_dup(
				gres_state_step->gres_data);
		else {
			new_gres_data = _step_state_dup2(
				gres_state_step->gres_data, node_index);
		}
		if (new_gres_list == NULL) {
			new_gres_list = list_create(gres_step_list_delete);
		}
		new_gres_state_step = gres_create_state(
			gres_state_step, GRES_STATE_SRC_STATE_PTR,
			GRES_STATE_TYPE_STEP, new_gres_data);
		list_append(new_gres_list, new_gres_state_step);
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return new_gres_list;
}

/*
 * Pack a step's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_ctld_step_alloc()
 * IN/OUT buffer - location to write state to
 * IN step_id - job and step ID for logging
 */
extern int gres_step_state_pack(List gres_list, buf_t *buffer,
				slurm_step_id_t *step_id,
				uint16_t protocol_version)
{
	int i, rc = SLURM_SUCCESS;
	uint32_t top_offset, tail_offset, magic = GRES_MAGIC;
	uint16_t rec_cnt = 0;
	ListIterator gres_iter;
	gres_state_t *gres_state_step;
	gres_step_state_t *gres_ss;

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	if (gres_list == NULL)
		return rc;

	(void) gres_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_step = (gres_state_t *) list_next(gres_iter))) {
		gres_ss = (gres_step_state_t *) gres_state_step->gres_data;

		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_state_step->plugin_id, buffer);
			pack16(gres_ss->cpus_per_gres, buffer);
			pack16(gres_ss->flags, buffer);
			pack64(gres_ss->gres_per_step, buffer);
			pack64(gres_ss->gres_per_node, buffer);
			pack64(gres_ss->gres_per_socket, buffer);
			pack64(gres_ss->gres_per_task, buffer);
			pack64(gres_ss->mem_per_gres, buffer);
			pack64(gres_ss->total_gres, buffer);
			pack32(gres_ss->node_cnt, buffer);
			pack_bit_str_hex(gres_ss->node_in_use, buffer);
			if (gres_ss->gres_cnt_node_alloc) {
				pack8((uint8_t) 1, buffer);
				pack64_array(gres_ss->gres_cnt_node_alloc,
					     gres_ss->node_cnt, buffer);
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (gres_ss->gres_bit_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_ss->node_cnt; i++)
					pack_bit_str_hex(gres_ss->
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
 * OUT gres_list - restored state stored by gres_step_state_pack()
 * IN/OUT buffer - location to read state from
 * IN step_id - job and step ID for logging
 */
extern int gres_step_state_unpack(List *gres_list, buf_t *buffer,
				  slurm_step_id_t *step_id,
				  uint16_t protocol_version)
{
	int i, rc;
	uint32_t magic = 0, plugin_id = 0, uint32_tmp = 0;
	uint16_t rec_cnt = 0;
	uint8_t data_flag = 0;
	gres_state_t *gres_state_step;
	gres_step_state_t *gres_ss = NULL;
	bool locked = false;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	rc = gres_init();

	slurm_mutex_lock(&gres_context_lock);
	locked = true;
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(gres_step_list_delete);
	}

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		slurm_gres_context_t *gres_ctx;
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;
		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_ss = xmalloc(sizeof(gres_step_state_t));
			safe_unpack16(&gres_ss->cpus_per_gres, buffer);
			safe_unpack16(&gres_ss->flags, buffer);
			safe_unpack64(&gres_ss->gres_per_step, buffer);
			safe_unpack64(&gres_ss->gres_per_node, buffer);
			safe_unpack64(&gres_ss->gres_per_socket, buffer);
			safe_unpack64(&gres_ss->gres_per_task, buffer);
			safe_unpack64(&gres_ss->mem_per_gres, buffer);
			safe_unpack64(&gres_ss->total_gres, buffer);
			safe_unpack32(&gres_ss->node_cnt, buffer);
			if (gres_ss->node_cnt > NO_VAL)
				goto unpack_error;
			unpack_bit_str_hex(&gres_ss->node_in_use, buffer);
			safe_unpack8(&data_flag, buffer);
			if (data_flag) {
				safe_unpack64_array(
					&gres_ss->gres_cnt_node_alloc,
					&uint32_tmp, buffer);
			}
			safe_unpack8(&data_flag, buffer);
			if (data_flag) {
				gres_ss->gres_bit_alloc =
					xcalloc(gres_ss->node_cnt,
						sizeof(bitstr_t *));
				for (i = 0; i < gres_ss->node_cnt; i++) {
					unpack_bit_str_hex(&gres_ss->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			goto unpack_error;
		}

		if (!(gres_ctx = _find_context_by_id(plugin_id))) {
			/*
			 * A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data.
			 */
			info("%s: no plugin configured to unpack data type %u from %ps",
			     __func__, plugin_id, step_id);
			_step_state_delete(gres_ss);
			gres_ss = NULL;
			continue;
		}
		gres_state_step = gres_create_state(
			gres_ctx, GRES_STATE_SRC_CONTEXT_PTR,
			GRES_STATE_TYPE_STEP, gres_ss);
		gres_ss = NULL;
		list_append(*gres_list, gres_state_step);
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error from %ps", __func__, step_id);
	if (gres_ss)
		_step_state_delete(gres_ss);
	if (locked)
		slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

/* Return the count of GRES of a specific name on this machine
 * IN step_gres_list - generated by gres_ctld_step_alloc()
 * IN gres_name - name of the GRES to match
 * RET count of GRES of this specific name available to the job or NO_VAL64
 */
extern uint64_t gres_step_count(List step_gres_list, char *gres_name)
{
	uint64_t gres_cnt = NO_VAL64;
	gres_state_t *gres_state_step = NULL;
	gres_step_state_t *gres_ss = NULL;
	ListIterator gres_iter;
	int i;

	if (!step_gres_list)
		return gres_cnt;

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (xstrcmp(gres_context[i].gres_name, gres_name))
			continue;
		gres_iter = list_iterator_create(step_gres_list);
		while ((gres_state_step = list_next(gres_iter))) {
			if (gres_state_step->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			gres_ss =
				(gres_step_state_t*) gres_state_step->gres_data;
			/* gres_cnt_node_alloc has one element in slurmstepd */
			if (gres_cnt == NO_VAL64)
				gres_cnt =
					gres_ss->gres_cnt_node_alloc[0];
			else
				gres_cnt +=
					gres_ss->gres_cnt_node_alloc[0];
		}
		list_iterator_destroy(gres_iter);
		break;
	}
	slurm_mutex_unlock(&gres_context_lock);

	return gres_cnt;
}

/*
 * Here we convert usable_gres from a mask just for the gres in the allocation
 * to one for the gres on the node. Essentially putting in a '0' for gres not
 * in the allocation
 *
 * IN/OUT - usable_gres
 * IN - gres_bit_alloc
 */
static void _translate_step_to_global_device_index(bitstr_t **usable_gres,
						   bitstr_t *gres_bit_alloc)
{
	bitstr_t *tmp = bit_alloc(bit_size(gres_bit_alloc));
	int i_last, bit, bit2 = 0;

	i_last = bit_fls(gres_bit_alloc);
	for (bit = 0; bit <= i_last; bit++) {
		if (bit_test(gres_bit_alloc, bit)) {
			if (bit_test(*usable_gres, bit2)) {
				bit_set(tmp, bit);
			}
			bit2++;
		}
	}
	FREE_NULL_BITMAP(*usable_gres);
	*usable_gres = tmp;
}

/*
 * Given a GRES context index, return a bitmap representing those GRES
 * which are available from the CPUs current allocated to this process.
 * This function only works with task/cgroup and constrained devices or
 * if the job step has access to the entire node's resources.
 */
static bitstr_t *_get_usable_gres_cpu_affinity(int context_inx,
					       pid_t pid,
					       bitstr_t *gres_bit_alloc)
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
	int bitmap_size;

	if (!gres_conf_list) {
		error("gres_conf_list is null!");
		return NULL;
	}

	CPU_ZERO(&mask);
#ifdef __FreeBSD__
	rc = cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, pid ? pid : -1,
				sizeof(mask), &mask);
#else
	rc = sched_getaffinity(pid, sizeof(mask), &mask);
#endif
	if (rc) {
		error("sched_getaffinity error: %m");
		return usable_gres;
	}

	bitmap_size = bit_size(gres_bit_alloc);
	usable_gres = bit_alloc(bitmap_size);
	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = (gres_slurmd_conf_t *) list_next(iter))) {
		if (gres_slurmd_conf->plugin_id !=
		    gres_context[context_inx].plugin_id)
			continue;
		if ((gres_inx + gres_slurmd_conf->count) > bitmap_size) {
			error("GRES %s bitmap overflow ((%d + %"PRIu64") > %d)",
			      gres_slurmd_conf->name, gres_inx,
			      gres_slurmd_conf->count, bitmap_size);
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

	bit_and(usable_gres, gres_bit_alloc);

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
extern void gres_g_step_hardware_init(List step_gres_list,
				      uint32_t node_id, char *settings)
{
	int i;
	gres_state_t *gres_state_step;
	gres_step_state_t *gres_ss;
	bitstr_t *devices;

	if (!step_gres_list)
		return;

	(void) gres_init();
	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (gres_context[i].ops.step_hardware_init == NULL)
			continue;

		gres_state_step = list_find_first(step_gres_list, gres_find_id,
						  &gres_context[i].plugin_id);
		if (!gres_state_step || !gres_state_step->gres_data)
			continue;
		gres_ss = (gres_step_state_t *) gres_state_step->gres_data;
		if ((gres_ss->node_cnt != 1) ||
		    !gres_ss->gres_bit_alloc ||
		    !gres_ss->gres_bit_alloc[0])
			continue;

		devices = gres_ss->gres_bit_alloc[0];
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
extern void gres_g_step_hardware_fini(void)
{
	int i;
	(void) gres_init();
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
 * Given a set of GRES masks or maps and the local process ID, return the bitmap
 * of GRES that should be available to this task.
 *
 * IN map_or_mask
 * IN local_proc_id
 * IN gres_bit_alloc
 * IN is_map
 * IN get_devices
 *
 * RET usable_gres
 */
static bitstr_t *_get_usable_gres_map_or_mask(char *map_or_mask,
					      int local_proc_id,
					      bitstr_t *gres_bit_alloc,
					      bool is_map,
					      bool get_devices)
{
	bitstr_t *usable_gres = NULL;
	char *tmp, *tok, *save_ptr = NULL, *mult;
	int i, task_offset = 0, task_mult, bitmap_size;
	int value, min, max;

	if (!map_or_mask || !map_or_mask[0])
		return NULL;

	bitmap_size = bit_size(gres_bit_alloc);
	min = (is_map ?  0 : 1);
	max = (is_map ? bitmap_size - 1 : ~(-1 << bitmap_size));
	while (usable_gres == NULL) {
		tmp = xstrdup(map_or_mask);
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
				value = strtol(tok, NULL, 0);
				usable_gres = bit_alloc(bitmap_size);
				if ((value < min) || (value > max)) {
					error("Invalid --gpu-bind= value specified.");
					xfree(tmp);
					goto end;	/* Bad value */
				}
				if (is_map)
					bit_set(usable_gres, value);
				else
					for (i = 0; i < bitmap_size; i++) {
						if ((value >> i) & 0x1)
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
	if (gres_use_local_device_index()) {
		if (get_devices)
			_translate_step_to_global_device_index(
				&usable_gres, gres_bit_alloc);
		else{
			bit_and(usable_gres, gres_bit_alloc);
			bit_consolidate(usable_gres);
		}
	} else {
		bit_and(usable_gres, gres_bit_alloc);
	}

	return usable_gres;
}

static void _accumulate_step_gres_alloc(gres_state_t *gres_state_step,
				        bitstr_t **gres_bit_alloc,
				        uint64_t *gres_cnt)
{
	gres_step_state_t *gres_ss =
		(gres_step_state_t *)gres_state_step->gres_data;

	/* Since this should only run on the node node_cnt should always be 1 */
	if (gres_ss->node_cnt != 1) {
		error("gres_step_state_t node count not 1 while on node. This should never happen");
		return;
	}

	if (gres_ss->gres_bit_alloc &&
	    gres_ss->gres_bit_alloc[0]) {
		if (!*gres_bit_alloc) {
			*gres_bit_alloc = bit_alloc(
				bit_size(gres_ss->gres_bit_alloc[0]));
		}
		bit_or(*gres_bit_alloc, gres_ss->gres_bit_alloc[0]);
	}
	if (gres_cnt && gres_ss->gres_cnt_node_alloc)
		*gres_cnt += gres_ss->gres_cnt_node_alloc[0];
}

/*
 * Filter usable_gres to include the correct gpus per task.
 *
 * IN/OUT - usable_gres
 * IN - gpus_per_task_str
 * IN - local_proc_id
 */
static void _filter_gres_per_task(bitstr_t *usable_gres, uint32_t gpus_per_task,
				  int local_proc_id)
{
	int nskip = local_proc_id * gpus_per_task;
	int i_first, i_last, bit;

	i_first = bit_ffs(usable_gres);

	if (i_first == -1)
		return;

	i_last = bit_fls(usable_gres);
	for (bit = i_first; bit <= i_last; bit++) {
		if (!bit_test(usable_gres, bit))
			continue;
		if (nskip) {
			bit_clear(usable_gres, bit);
			nskip--;
		} else if (gpus_per_task) {
			gpus_per_task--;
		} else {
			bit_nclear(usable_gres, bit,
				   bit_size(usable_gres) - 1);
			break;
		}
	}

	if (gpus_per_task)
		error("Not enough gpus to bind for gpus per task");
}

/* Parse bind information to find which gres is usable by the task.
 *
 * IN accel_bind_type - GRES binding options (old format, a bitmap)
 * IN tres_bind - TRES binding directives (new format, a string)
 * OUT tres_bind - String parsed filled into structure.
 */
static void _parse_tres_bind(uint16_t accel_bind_type, char *tres_bind_str,
			     tres_bind_t *tres_bind)
{
	char *sep;

	xassert(tres_bind);
	memset(tres_bind, 0, sizeof(tres_bind_t));

	tres_bind->gres_internal_flags = GRES_INTERNAL_FLAG_NONE;

	tres_bind->bind_gpu = accel_bind_type & ACCEL_BIND_CLOSEST_GPU;
	tres_bind->bind_nic = accel_bind_type & ACCEL_BIND_CLOSEST_NIC;
	if (!tres_bind->bind_gpu && (sep = xstrstr(tres_bind_str, "gpu:"))) {
		sep += 4;
		if (!xstrncasecmp(sep, "verbose,", 8)) {
			sep += 8;
			tres_bind->gres_internal_flags |=
				GRES_INTERNAL_FLAG_VERBOSE;
		}
		if (!xstrncasecmp(sep, "single:", 7)) {
			long tasks_per_gres;
			sep += 7;
			tasks_per_gres = strtol(sep, NULL, 0);
			if ((tasks_per_gres <= 0) ||
			    (tasks_per_gres > UINT32_MAX)) {
				error("%s: single:%s does not specify a valid number. Defaulting to 1.",
				      __func__, sep);
				tasks_per_gres = 1;
			}
			tres_bind->tasks_per_gres = tasks_per_gres;
			tres_bind->bind_gpu = true;
		} else if (!xstrncasecmp(sep, "closest", 7))
			tres_bind->bind_gpu = true;
		else if (!xstrncasecmp(sep, "map_gpu:", 8))
			tres_bind->map_gpu = sep + 8;
		else if (!xstrncasecmp(sep, "mask_gpu:", 9))
			tres_bind->mask_gpu = sep + 9;
		else if (!xstrncasecmp(sep, "per_task:", 9))
			tres_bind->gpus_per_task = slurm_atoul(sep + 9);
	}
	tres_bind->request = tres_bind_str;
}

static int _get_usable_gres(char *gres_name, int context_inx, int proc_id,
			    pid_t pid, tres_bind_t *tres_bind,
			    bitstr_t **usable_gres_ptr,
			    bitstr_t *gres_bit_alloc,  bool get_devices)
{
	bitstr_t *usable_gres;
	*usable_gres_ptr = NULL;

	if (!tres_bind->bind_gpu && !tres_bind->bind_nic &&
	    !tres_bind->map_gpu && !tres_bind->mask_gpu &&
	    !tres_bind->gpus_per_task)
		return SLURM_SUCCESS;

	if (!gres_bit_alloc)
		return SLURM_SUCCESS;

	if (!xstrcmp(gres_name, "gpu")) {
		if (tres_bind->map_gpu) {
			usable_gres = _get_usable_gres_map_or_mask(
				tres_bind->map_gpu, proc_id, gres_bit_alloc,
				true, get_devices);
		} else if (tres_bind->mask_gpu) {
			usable_gres = _get_usable_gres_map_or_mask(
				tres_bind->mask_gpu, proc_id, gres_bit_alloc,
				false, get_devices);
		} else if (tres_bind->bind_gpu) {
			usable_gres = _get_usable_gres_cpu_affinity(
				context_inx, pid, gres_bit_alloc);
			_filter_usable_gres(usable_gres,
					    tres_bind->tasks_per_gres,
					    proc_id);
			if (!get_devices && gres_use_local_device_index())
				bit_consolidate(usable_gres);
		} else if (tres_bind->gpus_per_task) {
			if(!get_devices && gres_use_local_device_index()){
				usable_gres = bit_alloc(
					bit_size(gres_bit_alloc));
				bit_nset(usable_gres, 0,
					 tres_bind->gpus_per_task - 1);
			} else {
				usable_gres = bit_copy(gres_bit_alloc);
				_filter_gres_per_task(usable_gres,
						      tres_bind->gpus_per_task,
						      proc_id);
			}
		} else
			return SLURM_ERROR;
	} else if (!xstrcmp(gres_name, "nic")) {
		if (tres_bind->bind_nic) {
			usable_gres = _get_usable_gres_cpu_affinity(
				context_inx, pid, gres_bit_alloc);
			if (!get_devices && gres_use_local_device_index())
				bit_consolidate(usable_gres);
		}
		else
			return SLURM_ERROR;
	} else {
		return SLURM_ERROR;
	}

	if (!bit_set_count(usable_gres)) {
		error("Bind request %s does not specify any devices within the allocation for task %d. Binding to the first device in the allocation instead.",
		      tres_bind->request, proc_id);
		if (!get_devices && gres_use_local_device_index())
			bit_set(usable_gres,0);
		else
			bit_set(usable_gres, bit_ffs(gres_bit_alloc));

	}

	*usable_gres_ptr = usable_gres;

	return SLURM_SUCCESS;
}

/*
 * Set environment as required for all tasks of a job step
 * IN/OUT job_env_ptr - environment variable array
 * IN step_gres_list - generated by gres_ctld_step_alloc()
 */
extern void gres_g_step_set_env(char ***job_env_ptr, List step_gres_list)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_state_step = NULL;
	uint64_t gres_cnt = 0;
	bitstr_t *gres_bit_alloc = NULL;
	bool sharing_gres_alloced = false;
	gres_internal_flags_t flags = GRES_INTERNAL_FLAG_NONE;

	(void) gres_init();
	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		slurm_gres_context_t *gres_ctx = &gres_context[i];
		if (!gres_ctx->ops.step_set_env)
			continue;	/* No plugin to call */
		if (!step_gres_list) {
			/* Clear GRES environment variables */
			(*(gres_ctx->ops.step_set_env))(
				job_env_ptr, NULL, 0, GRES_INTERNAL_FLAG_NONE);
			continue;
		}
		gres_iter = list_iterator_create(step_gres_list);
		while ((gres_state_step = list_next(gres_iter))) {
			if (gres_state_step->plugin_id != gres_ctx->plugin_id)
				continue;
			_accumulate_step_gres_alloc(
				gres_state_step, &gres_bit_alloc, &gres_cnt);
			/* Does step have a sharing GRES (GPU)? */
			if (gres_id_sharing(gres_ctx->plugin_id))
				sharing_gres_alloced = true;
		}
		list_iterator_destroy(gres_iter);

		/*
		 * Do not let MPS or Shard (shared GRES) clear any envs set for
		 * a GPU (sharing GRES) when a GPU is allocated but an
		 * MPS/Shard is not. Sharing GRES plugins always run before
		 * shared GRES, so we don't need to protect MPS/Shard from GPU.
		 */
		if (gres_id_shared(gres_ctx->config_flags) &&
		    sharing_gres_alloced)
			flags |= GRES_INTERNAL_FLAG_PROTECT_ENV;

		(*(gres_ctx->ops.step_set_env))(job_env_ptr, gres_bit_alloc,
					       gres_cnt, flags);
		gres_cnt = 0;
		FREE_NULL_BITMAP(gres_bit_alloc);
	}
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Change the task's inherited environment (from the step, and set by
 * gres_g_step_set_env()). Use this to implement GPU task binding.
 *
 * IN/OUT job_env_ptr - environment variable array
 * IN step_gres_list - generated by gres_ctld_step_alloc()
 * IN accel_bind_type - GRES binding options (old format, a bitmap)
 * IN tres_bind_str - TRES binding directives (new format, a string)
 * IN local_proc_id - task rank, local to this compute node only
 */
extern void gres_g_task_set_env(char ***job_env_ptr, List step_gres_list,
				uint16_t accel_bind_type, char *tres_bind_str,
				int local_proc_id)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_state_step = NULL;
	bitstr_t *usable_gres = NULL;
	uint64_t gres_cnt = 0;
	bitstr_t *gres_bit_alloc = NULL;
	tres_bind_t tres_bind;
	bool sharing_gres_alloced = false;
	gres_internal_flags_t flags;

	_parse_tres_bind(accel_bind_type, tres_bind_str, &tres_bind);
	flags = tres_bind.gres_internal_flags;

	(void) gres_init();
	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		slurm_gres_context_t *gres_ctx = &gres_context[i];
		if (!gres_ctx->ops.task_set_env)
			continue;	/* No plugin to call */
		if (!step_gres_list) {
			/* Clear GRES environment variables */
			(*(gres_ctx->ops.task_set_env))(
				job_env_ptr, NULL, 0, NULL,
				GRES_INTERNAL_FLAG_NONE);
			continue;
		}


		gres_iter = list_iterator_create(step_gres_list);
		while ((gres_state_step = list_next(gres_iter))) {
			if (gres_state_step->plugin_id != gres_ctx->plugin_id)
				continue;
			_accumulate_step_gres_alloc(
				gres_state_step, &gres_bit_alloc, &gres_cnt);
			/* Does task have a sharing GRES (GPU)? */
			if (gres_id_sharing(gres_ctx->plugin_id))
				sharing_gres_alloced = true;
		}
		if (_get_usable_gres(gres_ctx->gres_name, i, local_proc_id, 0,
				     &tres_bind, &usable_gres, gres_bit_alloc,
				     false) == SLURM_ERROR)
			continue;

		list_iterator_destroy(gres_iter);

		/*
		 * Do not let MPS or Shard (shared GRES) clear any envs set for
		 * a GPU (sharing GRES) when a GPU is allocated but an
		 * MPS/Shard is not. Sharing GRES plugins always run before
		 * shared GRES, so we don't need to protect MPS/Shard from GPU.
		 */
		if (gres_id_shared(gres_ctx->config_flags) &&
		    sharing_gres_alloced)
			flags |= GRES_INTERNAL_FLAG_PROTECT_ENV;

		(*(gres_ctx->ops.task_set_env))(job_env_ptr, gres_bit_alloc,
					        gres_cnt, usable_gres, flags);
		gres_cnt = 0;
		FREE_NULL_BITMAP(gres_bit_alloc);
		FREE_NULL_BITMAP(usable_gres);
	}
	slurm_mutex_unlock(&gres_context_lock);
}

static void _step_state_log(gres_step_state_t *gres_ss,
			    slurm_step_id_t *step_id,
			    char *gres_name)
{
	char tmp_str[128];
	int i;

	xassert(gres_ss);
	info("gres:%s type:%s(%u) %ps flags:%s state", gres_name,
	     gres_ss->type_name, gres_ss->type_id, step_id,
	     _gres_flags_str(gres_ss->flags));
	if (gres_ss->cpus_per_gres)
		info("  cpus_per_gres:%u", gres_ss->cpus_per_gres);
	if (gres_ss->gres_per_step)
		info("  gres_per_step:%"PRIu64, gres_ss->gres_per_step);
	if (gres_ss->gres_per_node) {
		info("  gres_per_node:%"PRIu64" node_cnt:%u",
		     gres_ss->gres_per_node, gres_ss->node_cnt);
	}
	if (gres_ss->gres_per_socket)
		info("  gres_per_socket:%"PRIu64, gres_ss->gres_per_socket);
	if (gres_ss->gres_per_task)
		info("  gres_per_task:%"PRIu64, gres_ss->gres_per_task);
	if (gres_ss->mem_per_gres)
		info("  mem_per_gres:%"PRIu64, gres_ss->mem_per_gres);

	if (gres_ss->node_in_use == NULL)
		info("  node_in_use:NULL");
	else if (gres_ss->gres_bit_alloc == NULL)
		info("  gres_bit_alloc:NULL");
	else {
		for (i = 0; i < gres_ss->node_cnt; i++) {
			if (!bit_test(gres_ss->node_in_use, i))
				continue;
			if (gres_ss->gres_bit_alloc[i]) {
				bit_fmt(tmp_str, sizeof(tmp_str),
					gres_ss->gres_bit_alloc[i]);
				info("  gres_bit_alloc[%d]:%s of %d", i,
				     tmp_str,
				     (int)bit_size(gres_ss->gres_bit_alloc[i]));
			} else
				info("  gres_bit_alloc[%d]:NULL", i);
		}
	}
}

/*
 * Log a step's current gres state
 * IN gres_list - generated by gres_ctld_step_alloc()
 * IN job_id - job's ID
 * IN step_id - step's ID
 */
extern void gres_step_state_log(List gres_list, uint32_t job_id,
				uint32_t step_id)
{
	ListIterator gres_iter;
	gres_state_t *gres_state_step;
	slurm_step_id_t tmp_step_id;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES) || !gres_list)
		return;

	(void) gres_init();

	tmp_step_id.job_id = job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = step_id;

	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_step = (gres_state_t *) list_next(gres_iter))) {
		_step_state_log(gres_state_step->gres_data, &tmp_step_id,
				gres_state_step->gres_name);
	}
	list_iterator_destroy(gres_iter);
}

/*
 * Determine how many cores of a job's allocation can be allocated to a step
 *	on a specific node
 * IN job_gres_list - a running job's allocated gres info
 * IN/OUT step_gres_list - a pending job step's gres requirements
 * IN node_offset - index into the job's node allocation
 * IN first_step_node - true if this is node zero of the step (do initialization)
 * IN cpus_per_task - number of CPUs required per task
 * IN max_rem_nodes - maximum nodes remaining for step (including this one)
 * IN ignore_alloc - if set ignore resources already allocated to running steps
 * IN job_id, step_id - ID of the step being allocated.
 * IN test_mem - true if we should test if mem_per_gres would exceed a limit.
 * IN job_resrcs_ptr - pointer to this job's job_resources_t; used to know
 *                     how much of the job's memory is available.
 * OUT err_code - If an error occurred, set this to tell the caller why the
 *                error happend.
 * RET Count of available cores on this node (sort of):
 *     NO_VAL64 if no limit or 0 if node is not usable
 */
extern uint64_t gres_step_test(List step_gres_list, List job_gres_list,
			       int node_offset, bool first_step_node,
			       uint16_t cpus_per_task, int max_rem_nodes,
			       bool ignore_alloc,
			       uint32_t job_id, uint32_t step_id,
			       bool test_mem, job_resources_t *job_resrcs_ptr,
			       int *err_code)
{
	uint64_t core_cnt, tmp_cnt;
	ListIterator step_gres_iter;
	gres_state_t *gres_state_step;
	gres_step_state_t *gres_ss = NULL;
	slurm_step_id_t tmp_step_id;
	foreach_gres_cnt_t foreach_gres_cnt;

	if (step_gres_list == NULL)
		return NO_VAL64;
	if (job_gres_list == NULL)
		return 0;

	if (cpus_per_task == 0)
		cpus_per_task = 1;
	core_cnt = NO_VAL64;
	(void) gres_init();
	*err_code = SLURM_SUCCESS;

	tmp_step_id.job_id = job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = step_id;

	memset(&foreach_gres_cnt, 0, sizeof(foreach_gres_cnt));
	foreach_gres_cnt.ignore_alloc = ignore_alloc;
	foreach_gres_cnt.step_id = &tmp_step_id;

	slurm_mutex_lock(&gres_context_lock);
	step_gres_iter = list_iterator_create(step_gres_list);
	while ((gres_state_step = (gres_state_t *) list_next(step_gres_iter))) {
		gres_key_t job_search_key;

		gres_ss = (gres_step_state_t *)gres_state_step->gres_data;
		job_search_key.config_flags = gres_state_step->config_flags;
		job_search_key.plugin_id = gres_state_step->plugin_id;
		if (gres_ss->type_name)
			job_search_key.type_id = gres_ss->type_id;
		else
			job_search_key.type_id = NO_VAL;

		job_search_key.node_offset = node_offset;

		foreach_gres_cnt.job_search_key = &job_search_key;
		foreach_gres_cnt.gres_cnt = INFINITE64;

		(void)list_for_each(job_gres_list, _step_get_gres_cnt,
				    &foreach_gres_cnt);

		if (foreach_gres_cnt.gres_cnt == INFINITE64) {
			log_flag(GRES, "%s: Job lacks GRES (%s:%s) required by the step",
				 __func__, gres_state_step->gres_name,
				 gres_ss->type_name);
			core_cnt = 0;
			break;
		}

		if (foreach_gres_cnt.gres_cnt == NO_CONSUME_VAL64) {
			core_cnt = NO_VAL64;
			break;
		}

		tmp_cnt = _step_test(gres_ss, first_step_node,
				     cpus_per_task, max_rem_nodes,
				     ignore_alloc, foreach_gres_cnt.gres_cnt,
				     test_mem, node_offset,
				     &tmp_step_id,
				     job_resrcs_ptr, err_code);
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
extern bool gres_id_shared(uint32_t config_flags)
{
	if (config_flags & GRES_CONF_SHARED)
		return true;
	return false;
}
/*
 * Return TRUE if this plugin ID shares resources with another GRES that
 * consumes subsets of its resources (e.g. GPU)
 */
extern bool gres_id_sharing(uint32_t plugin_id)
{
	if (plugin_id == gpu_plugin_id)
		return true;
	return false;
}

/*
 * Determine total count GRES of a given type are allocated to a job across
 * all nodes
 * IN job_gres_list - job's gres_list built by gres_job_state_validate()
 * IN gres_name - name of a GRES type
 * RET count of this GRES allocated to this job
 */
extern uint64_t gres_get_value_by_type(List job_gres_list, char *gres_name)
{
	int i;
	uint32_t plugin_id;
	uint64_t gres_cnt = 0;
	ListIterator job_gres_iter;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;

	if (job_gres_list == NULL)
		return NO_VAL64;

	gres_cnt = NO_VAL64;
	(void) gres_init();
	plugin_id = gres_build_id(gres_name);

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_state_job->plugin_id != plugin_id)
				continue;
			gres_js = (gres_job_state_t *)
				gres_state_job->gres_data;
			gres_cnt = gres_js->gres_per_node;
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
 *		in the gres_list.
 * IN val_type - Type of value desired, see GRES_VAL_TYPE_*
 * RET SLURM_SUCCESS or error code
 */
extern int gres_node_count(List gres_list, int arr_len,
			   uint32_t *gres_count_ids,
			   uint64_t *gres_count_vals,
			   int val_type)
{
	ListIterator  node_gres_iter;
	gres_state_t *gres_state_node;
	uint64_t      val;
	int           rc, ix = 0;

	rc = gres_init();
	if ((rc == SLURM_SUCCESS) && (arr_len <= 0))
		rc = EINVAL;
	if (rc != SLURM_SUCCESS)
		return rc;

	slurm_mutex_lock(&gres_context_lock);

	node_gres_iter = list_iterator_create(gres_list);
	while ((gres_state_node = (gres_state_t*) list_next(node_gres_iter))) {
		gres_node_state_t *gres_ns;
		val = 0;
		gres_ns = (gres_node_state_t *) gres_state_node->gres_data;
		xassert(gres_ns);

		switch (val_type) {
		case (GRES_VAL_TYPE_FOUND):
			val = gres_ns->gres_cnt_found;
			break;
		case (GRES_VAL_TYPE_CONFIG):
			val = gres_ns->gres_cnt_config;
			break;
		case (GRES_VAL_TYPE_AVAIL):
			val = gres_ns->gres_cnt_avail;
			break;
		case (GRES_VAL_TYPE_ALLOC):
			val = gres_ns->gres_cnt_alloc;
		}

		gres_count_ids[ix]  = gres_state_node->plugin_id;
		gres_count_vals[ix] = val;
		if (++ix >= arr_len)
			break;
	}
	list_iterator_destroy(node_gres_iter);

	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void gres_g_send_stepd(int fd, slurm_msg_t *msg)
{
	int len;

	/* Setup the gres_device list and other plugin-specific data */
	(void) gres_init();

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
extern void gres_g_recv_stepd(int fd, slurm_msg_t *msg)
{
	int len, rc;
	buf_t *buffer = NULL;

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
	(void) gres_init();

	return;
rwfail:
	FREE_NULL_BUFFER(buffer);
	error("%s: failed", __func__);
	slurm_mutex_unlock(&gres_context_lock);

	/* Set debug flags and init_run only */
	(void) gres_init();

	return;
}

/* Get generic GRES data types here. Call the plugin for others */
static int _get_job_info(int gres_inx, gres_job_state_t *gres_js,
			 uint32_t node_inx, enum gres_job_data_type data_type,
			 void *data)
{
	uint64_t *u64_data = (uint64_t *) data;
	bitstr_t **bit_data = (bitstr_t **) data;
	int rc = SLURM_SUCCESS;

	if (!gres_js || !data)
		return EINVAL;
	if (node_inx >= gres_js->node_cnt)
		return ESLURM_INVALID_NODE_COUNT;
	if (data_type == GRES_JOB_DATA_COUNT) {
		*u64_data = gres_js->gres_per_node;
	} else if (data_type == GRES_JOB_DATA_BITMAP) {
		if (gres_js->gres_bit_alloc)
			*bit_data = gres_js->gres_bit_alloc[node_inx];
		else
			*bit_data = NULL;
	} else {
		/* Support here for plugin-specific data types */
		rc = (*(gres_context[gres_inx].ops.job_info))
			(gres_js, node_inx, data_type, data);
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
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;

	if (data == NULL)
		return EINVAL;
	if (job_gres_list == NULL)	/* No GRES allocated */
		return ESLURM_INVALID_GRES;

	(void) gres_init();
	plugin_id = gres_build_id(gres_name);

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_state_job->plugin_id != plugin_id)
				continue;
			gres_js = (gres_job_state_t *)
				gres_state_job->gres_data;
			rc = _get_job_info(i, gres_js, node_inx,
					   data_type, data);
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/* Get generic GRES data types here. Call the plugin for others */
static int _get_step_info(int gres_inx, gres_step_state_t *gres_ss,
			  uint32_t node_inx, enum gres_step_data_type data_type,
			  void *data)
{
	uint64_t *u64_data = (uint64_t *) data;
	bitstr_t **bit_data = (bitstr_t **) data;
	int rc = SLURM_SUCCESS;

	if (!gres_ss || !data)
		return EINVAL;
	if (node_inx >= gres_ss->node_cnt)
		return ESLURM_INVALID_NODE_COUNT;
	if (data_type == GRES_STEP_DATA_COUNT) {
		*u64_data = gres_ss->gres_per_node;
	} else if (data_type == GRES_STEP_DATA_BITMAP) {
		if (gres_ss->gres_bit_alloc)
			*bit_data = gres_ss->gres_bit_alloc[node_inx];
		else
			*bit_data = NULL;
	} else {
		/* Support here for plugin-specific data types */
		rc = (*(gres_context[gres_inx].ops.step_info))
			(gres_ss, node_inx, data_type, data);
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
	gres_state_t *gres_state_step;
	gres_step_state_t *gres_ss;

	if (data == NULL)
		return EINVAL;
	if (step_gres_list == NULL)	/* No GRES allocated */
		return ESLURM_INVALID_GRES;

	(void) gres_init();
	plugin_id = gres_build_id(gres_name);

	slurm_mutex_lock(&gres_context_lock);
	step_gres_iter = list_iterator_create(step_gres_list);
	while ((gres_state_step = (gres_state_t *) list_next(step_gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_state_step->plugin_id != plugin_id)
				continue;
			gres_ss = (gres_step_state_t *)
				gres_state_step->gres_data;
			rc = _get_step_info(i, gres_ss, node_inx,
					    data_type, data);
			break;
		}
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

extern uint32_t gres_get_autodetect_flags(void)
{
	return autodetect_flags;
}

extern void gres_clear_tres_cnt(uint64_t *tres_cnt, bool locked)
{
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_rec;
	int tres_pos;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
		tres_rec.type = "gres";
	}

	/* must be locked first before gres_contrex_lock!!! */
	if (!locked)
		assoc_mgr_lock(&locks);

	slurm_mutex_lock(&gres_context_lock);
	/* Initialize all GRES counters to zero. Increment them later. */
	for (int i = 0; i < gres_context_cnt; i++) {
		tres_rec.name =	gres_context[i].gres_name;
		if (tres_rec.name &&
		    ((tres_pos = assoc_mgr_find_tres_pos(
			      &tres_rec, true)) !=-1))
			tres_cnt[tres_pos] = 0;
	}
	slurm_mutex_unlock(&gres_context_lock);

	/* must be locked first before gres_contrex_lock!!! */
	if (!locked)
		assoc_mgr_unlock(&locks);
}

extern char *gres_device_id2str(gres_device_id_t *gres_dev)
{
	char *res = NULL;

	xstrfmtcat(res, "%c %u:%u rwm",
		   gres_dev->type == DEV_TYPE_BLOCK ? 'b' : 'c',
		   gres_dev->major, gres_dev->minor);

	return res;
}


/* Free memory for gres_device_t record */
extern void destroy_gres_device(void *gres_device_ptr)
{
	gres_device_t *gres_device = (gres_device_t *) gres_device_ptr;

	if (!gres_device)
		return;
	xfree(gres_device->path);
	xfree(gres_device->unique_id);
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
	xfree(p->unique_id);
	xfree(p);
}


/*
 * Convert GRES config_flags to a string. The pointer returned references local
 * storage in this function, which is not re-entrant.
 */
extern char *gres_flags2str(uint32_t config_flags)
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

	if (config_flags & GRES_CONF_ENV_NVML) {
		strcat(flag_str, sep);
		strcat(flag_str, "ENV_NVML");
		sep = ",";
	}

	if (config_flags & GRES_CONF_ENV_RSMI) {
		strcat(flag_str, sep);
		strcat(flag_str, "ENV_RSMI");
		sep = ",";
	}

	if (config_flags & GRES_CONF_ENV_ONEAPI) {
		strcat(flag_str, sep);
		strcat(flag_str, "ENV_ONEAPI");
		sep = ",";
	}

	if (config_flags & GRES_CONF_ENV_OPENCL) {
		strcat(flag_str, sep);
		strcat(flag_str, "ENV_OPENCL");
		sep = ",";
	}

	if (config_flags & GRES_CONF_ENV_DEF) {
		strcat(flag_str, sep);
		strcat(flag_str, "ENV_DEFAULT");
		sep = ",";
	}

	if (config_flags & GRES_CONF_SHARED) {
		strcat(flag_str, sep);
		strcat(flag_str, "SHARED");
		sep = ",";
	}

	if (config_flags & GRES_CONF_ONE_SHARING) {
		strcat(flag_str, sep);
		strcat(flag_str, "ONE_SHARING");
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
			     char *type, char *links, char *unique_id,
			     uint32_t flags)
{
	gres_slurmd_conf_t *gres_slurmd_conf;
	bool use_empty_first_record = false;
	ListIterator itr = list_iterator_create(gres_list);

	/*
	 * If the first record already exists and has a count of 0 then
	 * overwrite it.
	 * This is a placeholder record created in _merge_config()
	 */
	gres_slurmd_conf = list_next(itr);
	if (gres_slurmd_conf && (gres_slurmd_conf->count == 0))
		use_empty_first_record = true;
	else
		gres_slurmd_conf = xmalloc(sizeof(gres_slurmd_conf_t));
	gres_slurmd_conf->cpu_cnt = cpu_cnt;
	if (cpu_aff_mac_bitstr) {
		bitstr_t *cpu_aff = bit_copy(cpu_aff_mac_bitstr);

		/*
		 * Size down (or possibly up) cpus_bitmap, if necessary, so that
		 * the size of cpus_bitmap for system-detected devices matches
		 * the size of cpus_bitmap for configured devices.
		 */
		if (bit_size(cpu_aff) != cpu_cnt) {
			/* Calculate minimum size to hold CPU affinity */
			int64_t size = bit_fls(cpu_aff) + 1;
			if (size > cpu_cnt) {
				char *cpu_str = bit_fmt_hexmask_trim(cpu_aff);
				fatal("This CPU affinity bitmask (%s) does not fit within the CPUs configured for this node (%d). Make sure that the node's CPU count is configured correctly.",
				      cpu_str, cpu_cnt);
				xfree(cpu_str);
			}
			bit_realloc(cpu_aff, cpu_cnt);
		}
		gres_slurmd_conf->cpus_bitmap = cpu_aff;
	}
	gres_slurmd_conf->config_flags = flags;

	/* Set default env flags, if necessary */
	if ((flags & GRES_CONF_ENV_DEF) &&
	    ((flags & GRES_CONF_ENV_SET) != GRES_CONF_ENV_SET))
		flags |= GRES_CONF_ENV_SET;

	if (device_file) {
		hostlist_t hl = hostlist_create(device_file);
		gres_slurmd_conf->config_flags |= GRES_CONF_HAS_FILE;
		if (hostlist_count(hl) > 1)
			gres_slurmd_conf->config_flags |= GRES_CONF_HAS_MULT;
		hostlist_destroy(hl);
	}
	if (type)
		gres_slurmd_conf->config_flags |= GRES_CONF_HAS_TYPE;
	gres_slurmd_conf->cpus = xstrdup(cpu_aff_abs_range);
	gres_slurmd_conf->type_name = xstrdup(type);
	gres_slurmd_conf->name = xstrdup(name);
	gres_slurmd_conf->file = xstrdup(device_file);
	gres_slurmd_conf->links = xstrdup(links);
	gres_slurmd_conf->unique_id = xstrdup(unique_id);
	gres_slurmd_conf->count = device_cnt;
	gres_slurmd_conf->plugin_id = gres_build_id(name);
	if (!use_empty_first_record)
		list_append(gres_list, gres_slurmd_conf);
	list_iterator_destroy(itr);
}

extern char *gres_prepend_tres_type(const char *gres_str)
{
	char *output = NULL;

	if (gres_str) {
		output = xstrdup_printf("gres:%s", gres_str);
		xstrsubstituteall(output, ",", ",gres:");
		xstrsubstituteall(output, "gres:gres:", "gres:");
	}
	return output;
}

extern bool gres_use_busy_dev(gres_state_t *gres_state_node,
			      bool use_total_gres)
{
	gres_node_state_t *gres_ns = gres_state_node->gres_data;

	if (!use_total_gres &&
	    gres_id_shared(gres_state_node->config_flags) &&
	    (gres_state_node->config_flags & GRES_CONF_ONE_SHARING) &&
	    (gres_ns->gres_cnt_alloc != 0)) {
		/* We must use the ONE already active GRES of this type */
		return true;
	}

	return false;
}

static int _parse_gres_config_dummy(void **dest, slurm_parser_enum_t type,
				    const char *key, const char *value,
				    const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl = s_p_hashtbl_create(_gres_options);
	s_p_parse_line(tbl, *leftover, leftover);
	s_p_hashtbl_destroy(tbl);

	return SLURM_SUCCESS;
}

extern void gres_parse_config_dummy(void)
{
	/* Keep options in sync with node_config_load(). */
	static s_p_options_t _gres_conf_options[] = {
		{"AutoDetect", S_P_STRING},
		{"Name", S_P_ARRAY, _parse_gres_config_dummy, NULL},
		{"NodeName", S_P_ARRAY, _parse_gres_config_dummy, NULL},
		{NULL}
	};
	struct stat stat_buf;
	s_p_hashtbl_t *tbl;
	char *gres_conf_file = get_extra_conf_path("gres.conf");

	if (stat(gres_conf_file, &stat_buf) < 0) {
		xfree(gres_conf_file);
		return;
	}

	tbl = s_p_hashtbl_create(_gres_conf_options);
	s_p_parse_file(tbl, NULL, gres_conf_file, false, NULL);
	s_p_hashtbl_destroy(tbl);
	xfree(gres_conf_file);
}
