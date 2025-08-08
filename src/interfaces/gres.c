/*****************************************************************************\
 *  gres.c - driver for gres plugin
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
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

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef MAJOR_IN_MKDEV
#  include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#  include <sys/sysmacros.h>
#endif

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/interfaces/cgroup.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/gpu.h"
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
#include "src/interfaces/select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/xsched.h"
#include "src/common/xstring.h"

#define MAX_GRES_BITMAP 1024

strong_alias(gres_find_id, slurm_gres_find_id);
strong_alias(gres_find_job_by_key_exact_type,
	     slurm_gres_find_job_by_key_exact_type);
strong_alias(gres_find_sock_by_job_state, slurm_gres_find_sock_by_job_state);
strong_alias(gres_get_node_used, slurm_gres_get_node_used);
strong_alias(gres_get_system_cnt, slurm_gres_get_system_cnt);
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
	int		(*node_config_load)	( list_t *gres_conf_list,
						  node_config_load_t *node_conf);
	void		(*job_set_env)		( char ***job_env_ptr,
						  bitstr_t *gres_bit_alloc,
						  uint64_t gres_cnt,
						  gres_internal_flags_t flags);
	void		(*step_set_env)		( char ***step_env_ptr,
						  bitstr_t *gres_bit_alloc,
						  uint64_t gres_cnt,
						  gres_internal_flags_t flags);
	void		(*task_set_env)		( char ***task_env_ptr,
						  bitstr_t *gres_bit_alloc,
						  uint64_t gres_cnt,
						  bitstr_t *usable_gres,
						  gres_internal_flags_t flags);
	void		(*send_stepd)		( buf_t *buffer );
	void		(*recv_stepd)		( buf_t *buffer );
	list_t *(*get_devices)(void);
	void            (*step_hardware_init)	( bitstr_t *, char * );
	void            (*step_hardware_fini)	( void );
	gres_prep_t *(*prep_build_env)(gres_job_state_t *gres_js);
	void            (*prep_set_env)	( char ***prep_env_ptr,
					  gres_prep_t *gres_prep,
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
	list_t *np_gres_devices; /* list of devices when we don't have a plugin */
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

typedef struct {
	slurm_gres_context_t *gres_ctx;
	int new_has_file;
	int new_has_type;
	int rec_count;
} foreach_gres_conf_t;

typedef struct {
	bitstr_t **gres_bit_alloc;
	uint64_t gres_cnt;
	uint64_t **gres_per_bit;
	bool is_job;
	int node_inx;
	uint32_t plugin_id;
	bool sharing_gres_allocated;
} foreach_gres_accumulate_device_t;

typedef struct {
	node_config_load_t *config;
	list_t **gres_devices;
	int index;
	int max_dev_num;
	list_t *names_list;
	int rc;
} foreach_fill_in_gres_devices_t;

typedef struct {
	char *node_list;
	list_t *prep_gres_list;
} foreach_prep_build_env_t;

typedef struct {
	int node_inx;
	char ***prep_env_ptr;
} foreach_prep_set_env_t;

typedef struct {
	uint32_t core_cnt;
	int core_end_bit;
	int core_start_bit;
	uint32_t job_id;
	list_t *node_gres_list;
	char *node_name;
	bool use_total_gres;
} foreach_job_test_t;

typedef struct {
	void *data;
	enum gres_step_data_type data_type;
	uint32_t node_inx;
	uint32_t plugin_id;
	int rc;
} foreach_step_info_t;

typedef struct {
	char *gres_str;
	char *sep;
	int sock_inx;
} foreach_sock_str_t;

typedef struct {
	list_t *device_list;
	bitstr_t *gres_bit_alloc;
	bitstr_t *usable_gres;
} foreach_alloc_gres_device_t;

typedef struct {
	bool filter_type;
	uint64_t gres_cnt;
	char *gres_type;
	bool is_job;
	uint32_t plugin_id;
} foreach_gres_list_cnt_t;

typedef struct {
	int job_node_index;
	list_t *new_gres_list;
} foreach_state_list_dup_t;

typedef struct {
	int bitmap_size;
	int gres_inx;
	uint32_t plugin_id;
	bitstr_t *task_cpus_bitmap;
	bitstr_t *usable_gres;
} foreach_closest_usable_gres_t;

typedef struct {
	int best_slot;
	int gres_inx;
	bitstr_t *gres_slots;
	int ntasks_per_gres;
	bool overlap;
	uint32_t plugin_id;
	bitstr_t *task_cpus_bitmap;
} foreach_gres_to_task_t;

typedef struct {
	int array_len;
	uint32_t *gres_count_ids;
	uint64_t *gres_count_vals;
	int index;
	int val_type;
} foreach_node_count_t;

/* Pointers to functions in src/slurmd/common/xcpuinfo.h that we may use */
typedef struct xcpuinfo_funcs {
	int (*xcpuinfo_abs_to_mac) (char *abs, char **mac);
} xcpuinfo_funcs_t;
xcpuinfo_funcs_t xcpuinfo_ops;

typedef struct {
	uint32_t flags;
	uint32_t name_hash;
	bool no_gpu_env;
} prev_gres_flags_t;

typedef struct {
	uint32_t config_flags;
	int config_type_cnt;
	uint32_t cpu_set_cnt;
	uint64_t gres_cnt;
	uint32_t plugin_id;
	uint32_t rec_cnt;
	uint64_t topo_cnt;
} tot_from_slurmd_conf_t;

typedef struct {
	int core_cnt;
	int cores_per_sock;
	bool cpu_config_err;
	int cpus_config;
	uint64_t dev_cnt;
	slurm_gres_context_t *gres_ctx;
	gres_node_state_t *gres_ns;
	int gres_inx;
	int topo_cnt;
	bool has_file;
	char *node_name;
	int rc;
	char **reason_down;
	int sock_cnt;
	uint64_t tot_gres_cnt;
} rebuild_topo_t;

typedef struct {
	slurm_gres_context_t *gres_ctx;
	gres_node_state_t *gres_ns;
} add_gres_info_t;

typedef struct {
	uint64_t count;
	slurm_gres_context_t *gres_ctx;
	char *type_name;
} conf_cnt_t;

typedef struct {
	list_t *gres_conf_list;
	slurm_gres_context_t *gres_ctx;
} check_conf_t;

typedef struct {
	uint64_t cpu_cnt;
	list_t *gres_conf_list;
	slurm_gres_context_t *gres_ctx;
	list_t *new_list;
} merge_gres_t;

typedef struct {
	void *generic_gres_data;
	bool is_job;
	uint32_t plugin_id;
} merge_generic_t;

typedef struct {
	uint32_t cpus_per_gres;
	gres_job_state_validate_t *gres_js_val;
	bool have_gres_shared;
	bool have_gres_sharing;
	bool is_job;
	bool overlap_merge;
	int over_count;
	overlap_check_t *over_array;
	int rc;
	uint32_t tmp_min_cpus;
} job_validate_t;

typedef struct {
	uint32_t job_id;
	list_t *node_gres_list;
	int node_inx;
	char *node_name;
} validate_job_gres_cnt_t;

typedef struct {
	int job_node_index;
	list_t *new_list;
} job_state_extract_t;

typedef struct {
	buf_t *buffer;
	bool details;
	uint32_t magic;
	uint16_t protocol_version;
} pack_state_t;

/* Local variables */
static int gres_context_cnt = -1;
static uint32_t gres_cpu_cnt = 0;
static slurm_gres_context_t *gres_context = NULL;
static char *gres_node_name = NULL;
static char *local_plugins_str = NULL;
static pthread_mutex_t gres_context_lock = PTHREAD_MUTEX_INITIALIZER;
static list_t *gres_conf_list = NULL;
static uint32_t gpu_plugin_id = NO_VAL;
static volatile uint32_t autodetect_flags = GRES_AUTODETECT_UNSET;
static buf_t *gres_context_buf = NULL;
static buf_t *gres_conf_buf = NULL;
static bool reset_prev = true;
static bool use_local_index = false;
static bool dev_index_mode_set = false;

/* Local functions */
static void _accumulate_job_gres_alloc(gres_job_state_t *gres_js,
				       int node_inx,
				       bitstr_t **gres_bit_alloc,
				       uint64_t *gres_cnt);
static void _accumulate_step_gres_alloc(gres_state_t *gres_state_step,
					bitstr_t **gres_bit_alloc,
					uint64_t *gres_cnt,
					uint64_t **gres_per_bit);
static void _add_gres_context(char *gres_name);
static gres_node_state_t *_build_gres_node_state(void);
static void	_build_node_gres_str(list_t **gres_list, char **gres_str,
				     int cores_per_sock, int sock_per_node);
static bitstr_t *_core_bitmap_rebuild(bitstr_t *old_core_bitmap, int new_size);
static void	_prep_list_del(void *x);
static void	_get_gres_cnt(gres_node_state_t *gres_ns, char *orig_config,
			      char *gres_name, char *gres_name_colon,
			      int gres_name_colon_len);
static uint64_t _get_job_gres_list_cnt(list_t *gres_list, char *gres_name,
				       char *gres_type);
static void *	_job_state_dup2(gres_job_state_t *gres_js, int job_node_index);
static int	_load_plugin(slurm_gres_context_t *gres_ctx);
static int	_log_gres_slurmd_conf(void *x, void *arg);
static void	_my_stat(char *file_name);
static void	_node_config_init(char *orig_config,
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
static void *	_node_state_dup(gres_node_state_t *gres_ns);
static int	_parse_gres_config(void **dest, slurm_parser_enum_t type,
				   const char *key, const char *value,
				   const char *line, char **leftover);
static int	_parse_gres_config_node(void **dest, slurm_parser_enum_t type,
					const char *key, const char *value,
					const char *line, char **leftover);
static int	_post_plugin_gres_conf(void *x, void *arg);
static void *	_step_state_dup(gres_step_state_t *gres_ss);
static void *	_step_state_dup2(gres_step_state_t *gres_ss,
				 int job_node_index);
static int	_unload_plugin(slurm_gres_context_t *gres_ctx);
static void	_validate_slurm_conf(list_t *slurm_conf_list,
				     slurm_gres_context_t *gres_ctx);
static void	_validate_gres_conf(list_t *gres_conf_list,
				    slurm_gres_context_t *gres_ctx);
static int	_validate_file(char *path_name, char *gres_name);
static int	_valid_gres_type(char *gres_name, gres_node_state_t *gres_ns,
				 bool config_overrides, char **reason_down);
static void _parse_accel_bind_type(uint16_t accel_bind_type,
				   char *tres_bind_str);
static int _get_usable_gres(int context_inx, int proc_id,
			    char *tres_bind_str, bitstr_t **usable_gres_ptr,
			    bitstr_t *gres_bit_alloc,  bool get_devices,
			    stepd_step_rec_t *step, uint64_t *gres_per_bit,
			    gres_internal_flags_t *flags);

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

	if (dev_index_mode_set)
		return use_local_index;
	dev_index_mode_set = true;

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
		"gres_p_get_devices",
		"gres_p_step_hardware_init",
		"gres_p_step_hardware_fini",
		"gres_p_prep_build_env",
		"gres_p_prep_set_env"
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

	if (errno != ESLURM_PLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      gres_ctx->gres_type, slurm_strerror(errno));
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
	FREE_NULL_LIST(gres_ctx->np_gres_devices);

	return rc;
}

extern bool gres_is_shared_name(char *name)
{
	if (!xstrcmp(name, "mps") ||
	    !xstrcmp(name, "shard"))
		return true;
	return false;
}

static void _set_shared_flag(char *name, uint32_t *config_flags)
{
	if (gres_is_shared_name(name))
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
		if (gres_is_shared_name(one_name)) {
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
	if (have_shared && running_in_slurmctld() && !running_cons_tres()) {
		fatal("Use of shared gres requires the use of select/cons_tres");
	}

	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

extern int gres_get_gres_cnt(void)
{
	static int cnt = -1;

	if (cnt != -1)
		return cnt;

	xassert(gres_context_cnt >= 0);

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

	xassert(gres_context_cnt >= 0);

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

	reset_prev = true;

	/* Reset the flags so when the node checks in we believe that */
	for (int i = 0; i < gres_context_cnt; i++)
		gres_context[i].config_flags |= GRES_CONF_FROM_STATE;

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
		warning("Ignoring file-less GPU %s:%s from final GRES list",
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
	int index = -1, offset, mult = 1;

	p = (gres_slurmd_conf_t *) x;
	xassert(p);

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES)) {
		verbose("Gres Name=%s Type=%s Count=%"PRIu64" Flags=%s",
			p->name, p->type_name, p->count,
			gres_flags2str(p->config_flags));
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

	if (p->cpus && (index != -1)) {
		info("Gres Name=%s Type=%s Count=%"PRIu64" Index=%d ID=%u File=%s Cores=%s CoreCnt=%u Links=%s Flags=%s",
		     p->name,
		     p->type_name,
		     p->count,
		     index,
		     p->plugin_id,
		     p->file,
		     p->cpus,
		     p->cpu_cnt,
		     p->links,
		     gres_flags2str(p->config_flags));
	} else if (index != -1) {
		info("Gres Name=%s Type=%s Count=%"PRIu64" Index=%d ID=%u File=%s Links=%s Flags=%s",
		     p->name,
		     p->type_name,
		     p->count,
		     index,
		     p->plugin_id,
		     p->file,
		     p->links,
		     gres_flags2str(p->config_flags));
	} else if (p->file) {
		info("Gres Name=%s Type=%s Count=%"PRIu64" ID=%u File=%s Links=%s Flags=%s",
		     p->name,
		     p->type_name,
		     p->count,
		     p->plugin_id,
		     p->file,
		     p->links,
		     gres_flags2str(p->config_flags));
	} else {
		info("Gres Name=%s Type=%s Count=%"PRIu64" ID=%u Links=%s Flags=%s",
		     p->name,
		     p->type_name,
		     p->count,
		     p->plugin_id,
		     p->links,
		     gres_flags2str(p->config_flags));
	}

	return 0;
}


static int _post_plugin_gres_conf(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	slurm_gres_context_t *gres_ctx = arg;

	if (gres_slurmd_conf->plugin_id != gres_ctx->plugin_id)
		return 0;

	if (gres_slurmd_conf->config_flags & GRES_CONF_GLOBAL_INDEX)
		gres_ctx->config_flags |= GRES_CONF_GLOBAL_INDEX;

	return 1;
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
	hostlist_t *hl;
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
		else if (autodetect_flags & GRES_AUTODETECT_GPU_NRT)
			xstrfmtcat(flags, "%snrt", flags ? "," : "");
		else if (autodetect_flags & GRES_AUTODETECT_GPU_NVIDIA)
			xstrfmtcat(flags, "%snvidia", flags ? "," : "");
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
	else if (xstrcasestr(str, "nrt"))
		flags |= GRES_AUTODETECT_GPU_NRT;
	else if (xstrcasestr(str, "nvidia"))
		flags |= GRES_AUTODETECT_GPU_NVIDIA;
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

static int _get_match(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf1 = x;
	gres_slurmd_conf_t *gres_slurmd_conf2 = arg;

	/* We only need to check type name because they should all be gpus */
	if (!gres_slurmd_conf1->type_name && !gres_slurmd_conf2->type_name)
		return 1;

	if (!gres_slurmd_conf1->type_name || !gres_slurmd_conf2->type_name)
		return 0;

	if (!xstrcmp(gres_slurmd_conf1->type_name,
		     gres_slurmd_conf2->type_name))
		return 1;

	return 0;
}

static int _merge_by_type(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x, *merged_gres_slurmd_conf;
	list_t *gres_list_merged = arg;

	merged_gres_slurmd_conf = list_find_first(gres_list_merged, _get_match,
						  gres_slurmd_conf);

	/* We are merging types and don't care about files or links */
	if (merged_gres_slurmd_conf)
		merged_gres_slurmd_conf->count++;
	else
		list_append(gres_list_merged, gres_slurmd_conf);

	return SLURM_SUCCESS;
}

static int _slurm_conf_gres_str(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	char **gres_str = arg;
	if (gres_slurmd_conf && gres_slurmd_conf->name) {
		bool has_type = gres_slurmd_conf->type_name &&
				gres_slurmd_conf->type_name[0];
		xstrfmtcat(*gres_str, "%s%s:%s%s%ld",
			   gres_str && gres_str[0] ? "," : "",
			   gres_slurmd_conf->name,
			   has_type ? gres_slurmd_conf->type_name : "",
			   has_type ? ":" : "",
			   gres_slurmd_conf->count);
	}
	return SLURM_SUCCESS;
}

extern void gres_get_autodetected_gpus(node_config_load_t node_conf,
				       char **first_gres_str,
				       char **autodetect_str)
{
	list_t *gres_list_system = NULL, *gres_list_merged = NULL;

	char *gres_str = NULL;
	char *autodetect_option_name = NULL;

	int autodetect_options[] = {
		GRES_AUTODETECT_GPU_NVML,
		GRES_AUTODETECT_GPU_NVIDIA,
		GRES_AUTODETECT_GPU_RSMI,
		GRES_AUTODETECT_GPU_ONEAPI,
		GRES_AUTODETECT_GPU_NRT,
		GRES_AUTODETECT_UNSET /* For loop is done */
	};

	for (int i = 0; autodetect_options[i] != GRES_AUTODETECT_UNSET; i++) {
		autodetect_flags = autodetect_options[i];
		if (gpu_plugin_init() != SLURM_SUCCESS)
			continue;
		gres_list_system = gpu_g_get_system_gpu_list(&node_conf);
		if (gres_list_system) {
			gres_list_merged = list_create(NULL);
			list_for_each(gres_list_system, _merge_by_type,
				      gres_list_merged);
			list_for_each(gres_list_merged, _slurm_conf_gres_str,
				      &gres_str);
		}
		FREE_NULL_LIST(gres_list_merged);
		FREE_NULL_LIST(gres_list_system);
		gpu_plugin_fini();

		if (!gres_str)
			continue;

		if (autodetect_flags == GRES_AUTODETECT_GPU_NVML)
			i++; /* Skip NVIDIA if NVML finds gpus */

		autodetect_option_name = _get_autodetect_flags_str();
		xstrfmtcat(*autodetect_str, "%sFound %s with Autodetect=%s (Substring of gpu name may be used instead)",
			   (*autodetect_str ? "\n" : ""),
			   gres_str,
			   autodetect_option_name);
		xfree(autodetect_option_name);

		if (!*first_gres_str){
			*first_gres_str = gres_str;
			gres_str = NULL;
		} else {
			xfree(gres_str);
		}
	}
}

/*
 * Check to see if current GRES record matches the name of the previous GRES
 * record that set env flags.
 */
static bool _same_gres_name_as_prev(prev_gres_flags_t *prev_gres,
				    gres_slurmd_conf_t *p)
{
	if ((gres_build_id(p->name) == prev_gres->name_hash))
		return true;
	else
		return false;
}

/*
 * Save off env flags, GRES name, and no_gpu_env (for the next gres.conf line to
 * possibly inherit or to check against).
 */
static void _set_prev_gres_flags(prev_gres_flags_t *prev_gres,
				 gres_slurmd_conf_t *p, uint32_t env_flags,
				 bool no_gpu_env)
{
	prev_gres->flags = env_flags;
	prev_gres->name_hash = gres_build_id(p->name);
	prev_gres->no_gpu_env = no_gpu_env;
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
	if (xstrcasestr(input, "explicit"))
		flags |= GRES_CONF_EXPLICIT;
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
	static prev_gres_flags_t prev_gres = { 0 };

	if (reset_prev) {
		memset(&prev_gres, 0, sizeof(prev_gres));
		reset_prev = false;
	}

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
		p->config_flags |= flags;

		if (env_flags && no_gpu_env)
			fatal("Invalid GRES record name=%s type=%s: Flags (%s) contains \"no_gpu_env\", which must be mutually exclusive to all other GRES env flags of same node and name",
			      p->name, p->type_name, tmp_str);

		set_default_envs = false;
		/*
		 * Make sure that Flags are consistent with each other
		 * if set for multiple lines of the same GRES.
		 */
		if (prev_gres.name_hash &&
		    _same_gres_name_as_prev(&prev_gres, p) &&
		    ((prev_gres.flags != flags) ||
		     (prev_gres.no_gpu_env != no_gpu_env)))
			fatal("Invalid GRES record name=%s type=%s: Flags (%s) does not match env flags for previous GRES of same node and name",
			      p->name, p->type_name, tmp_str);

		_set_prev_gres_flags(&prev_gres, p, flags,
				     no_gpu_env);

		xfree(tmp_str);
	} else if ((prev_gres.flags || prev_gres.no_gpu_env) &&
		   _same_gres_name_as_prev(&prev_gres, p)) {
		/* Inherit flags from previous GRES line with same name */
		set_default_envs = false;
		p->config_flags |= prev_gres.flags;
	} else {
		if (!xstrcasecmp(p->name, "mps"))
			p->config_flags |= GRES_CONF_ONE_SHARING;
	}

	/* Flags not set. By default, all env vars are set for GPUs */
	if (set_default_envs && !xstrcasecmp(p->name, "gpu")) {
		uint32_t env_flags = GRES_CONF_ENV_SET | GRES_CONF_ENV_DEF;
		p->config_flags |= env_flags;
		_set_prev_gres_flags(&prev_gres, p, env_flags, false);
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
		hostlist_t *hl;
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

static void _validate_slurm_conf(list_t *slurm_conf_list,
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
	if (gres_slurmd_conf->config_flags & GRES_CONF_EXPLICIT)
		gres_ctx->config_flags |= GRES_CONF_EXPLICIT;

	if (gres_slurmd_conf->config_flags & GRES_CONF_COUNT_ONLY)
		gres_ctx->config_flags |= GRES_CONF_COUNT_ONLY;

	if (gres_slurmd_conf->config_flags & GRES_CONF_HAS_FILE)
		gres_ctx->config_flags |= GRES_CONF_HAS_FILE;

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

static void _validate_gres_conf(list_t *gres_conf_list,
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
		 * This means there was no gres.conf line for this gres found.
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
 * according to the current slurm.conf GRES. Modify the count of
 * gres_slurmd_conf to keep track of this. Any gres.conf records
 * with a count > 0 means that slurm.conf did not account for it completely.
 *
 * gres_slurmd_conf - (in/out) pointer to conf we are looking at.
 *                    This should be a temporary copy that we can modify.
 * conf_cnt->count - (in) The count of the current slurm.conf GRES record.
 * conf_cnt->type_name - (in) The type of the current slurm.conf GRES record.
 */
static int _foreach_compare_conf_counts(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	conf_cnt_t *conf_cnt = arg;

	/* Note: plugin type filter already applied */
	/* Check that type is the same */
	if (gres_slurmd_conf->type_name &&
	    xstrcasecmp(gres_slurmd_conf->type_name, conf_cnt->type_name))
		return 0;
	/* Keep track of counts */
	if (gres_slurmd_conf->count > conf_cnt->count) {
		gres_slurmd_conf->count -= conf_cnt->count;
		/* This slurm.conf GRES specification is now used up */
		return -1;
	} else {
		conf_cnt->count -= gres_slurmd_conf->count;
		gres_slurmd_conf->count = 0;
	}
	return 0;
}

static int _lite_copy_gres_slurmd_conf(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	check_conf_t *check_conf = arg;
	gres_slurmd_conf_t *gres_slurmd_conf_tmp;

	if (gres_slurmd_conf->plugin_id != check_conf->gres_ctx->plugin_id)
		return 0;

	gres_slurmd_conf_tmp = xmalloc(sizeof(*gres_slurmd_conf_tmp));
	gres_slurmd_conf_tmp->name = xstrdup(gres_slurmd_conf->name);
	gres_slurmd_conf_tmp->type_name = xstrdup(gres_slurmd_conf->type_name);
	gres_slurmd_conf_tmp->count = gres_slurmd_conf->count;
	list_append(check_conf->gres_conf_list, gres_slurmd_conf_tmp);

	return 0;
}

static int _foreach_slurm_conf_mismatch_comp(void *x, void *arg)
{
	gres_state_t *gres_state_node = x;
	check_conf_t *check_conf = arg;
	gres_node_state_t *gres_ns;
	conf_cnt_t conf_cnt = { 0 };

	if (gres_state_node->plugin_id != check_conf->gres_ctx->plugin_id)
		return 0;

	/* Determine if typed or untyped, and act accordingly */
	gres_ns = gres_state_node->gres_data;
	if (!gres_ns->type_name) {
		conf_cnt.count = gres_ns->gres_cnt_config;
		conf_cnt.type_name = NULL;
		(void) list_for_each(check_conf->gres_conf_list,
				     _foreach_compare_conf_counts,
				     &conf_cnt);
		return 0;
	}

	for (int i = 0; i < gres_ns->type_cnt; ++i) {
		conf_cnt.count = gres_ns->type_cnt_avail[i];
		conf_cnt.type_name = gres_ns->type_name[i];
		(void) list_for_each(check_conf->gres_conf_list,
				     _foreach_compare_conf_counts,
				     &conf_cnt);
	}

	return 0;
}

int _print_slurm_conf_mismatch(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;

	if (gres_slurmd_conf->count > 0)
		warning("A line in gres.conf for GRES %s%s%s has %"PRIu64" more configured than expected in slurm.conf. Ignoring extra GRES.",
			gres_slurmd_conf->name,
			(gres_slurmd_conf->type_name) ? ":" : "",
			(gres_slurmd_conf->type_name) ?
			gres_slurmd_conf->type_name : "",
			gres_slurmd_conf->count);
	return 0;
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
static void _check_conf_mismatch(list_t *slurm_conf_list, list_t *gres_conf_list,
				 slurm_gres_context_t *gres_ctx)
{
	check_conf_t check_conf = {
		.gres_ctx = gres_ctx,
	};

	/* E.g. slurm_conf_list will be NULL in the case of --gpu-bind */
	if (!slurm_conf_list || !gres_conf_list)
		return;

	/*
	 * Duplicate the gres.conf list with records relevant to this GRES
	 * plugin only so we can mangle records. Only add records under the
	 * current plugin.
	 */
	check_conf.gres_conf_list = list_create(destroy_gres_slurmd_conf);
	(void) list_for_each(gres_conf_list,
			     _lite_copy_gres_slurmd_conf,
			     &check_conf);

	/*
	 * Loop through the slurm.conf list and see if there are more gres.conf
	 * GRES than expected.
	 */
	(void) list_for_each(slurm_conf_list,
			     _foreach_slurm_conf_mismatch_comp,
			     &check_conf);

	/*
	 * Loop through gres_conf_list_tmp to print errors for gres.conf
	 * records that were not completely accounted for in slurm.conf.
	 */
	(void) list_for_each(check_conf.gres_conf_list,
			     _print_slurm_conf_mismatch,
			     NULL);

	FREE_NULL_LIST(check_conf.gres_conf_list);
}

/*
 * Match the type of a GRES from slurm.conf to a GRES in the gres.conf list. If
 * a match is found, pop it off the gres.conf list and return it.
 *
 * gres_context   - (in) Which GRES plugin we are currently working in.
 * type_name      - (in) The type of the slurm.conf GRES record. If null, then
 *			 it's an untyped GRES.
 *
 * Returns the first gres.conf record from gres_conf_list with the same type
 * name as the slurm.conf record.
 */
static int _match_type(void *x, void *key)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	conf_cnt_t *conf_cnt = key;

	if (gres_slurmd_conf->plugin_id != conf_cnt->gres_ctx->plugin_id)
		return 0;

	/*
	 * If type_name is NULL we will take the first matching
	 * gres_slurmd_conf that we find.  This means we also will
	 * remove the type from the gres_slurmd_conf to match 18.08
	 * stylings.
	 */
	if (!conf_cnt->type_name) {
		xfree(gres_slurmd_conf->type_name);
		gres_slurmd_conf->config_flags &= ~GRES_CONF_HAS_TYPE;
	} else if (xstrcasecmp(gres_slurmd_conf->type_name,
			       conf_cnt->type_name))
		return 0;

	return 1;
}

/*
 * Add a GRES conf record with count == 0 to gres_list.
 *
 * new_list - (in/out) The gres list to add to.
 * gres_ctx - (in) The GRES plugin to add a GRES record for.
 * count - (in) The cpu count configured for the node.
 */
static void _add_gres_config_empty(merge_gres_t *merge_gres)
{
	gres_slurmd_conf_t *gres_slurmd_conf =
		xmalloc(sizeof(*gres_slurmd_conf));
	gres_slurmd_conf->cpu_cnt = merge_gres->cpu_cnt;
	gres_slurmd_conf->name = xstrdup(merge_gres->gres_ctx->gres_name);
	gres_slurmd_conf->plugin_id = merge_gres->gres_ctx->plugin_id;
	list_append(merge_gres->new_list, gres_slurmd_conf);
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
	hostlist_t *hl = hostlist_create(gres_slurmd_conf->file);
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
static void _merge_gres2(merge_gres_t *merge_gres,
			 uint64_t count, char *type_name)
{
	gres_slurmd_conf_t *match;
	gres_slurmd_conf_t gres_slurmd_conf = {
		.cpu_cnt = merge_gres->cpu_cnt,
		.name = merge_gres->gres_ctx->gres_name,
		.type_name = type_name,
	};
	conf_cnt_t conf_cnt = {
		.count = count,
		.gres_ctx = merge_gres->gres_ctx,
		.type_name = type_name,
	};

	/* If slurm.conf count is initially 0, don't waste time on it */
	if (count == 0)
		return;

	/*
	 * There can be multiple gres.conf GRES lines contained within a
	 * single slurm.conf GRES line, due to different values of Cores
	 * and Links. Append them to the list where possible.
	 */
	while ((match = list_remove_first(
			merge_gres->gres_conf_list, _match_type, &conf_cnt))) {
		list_append(merge_gres->new_list, match);

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
	if (!xstrcasecmp(merge_gres->gres_ctx->gres_name, "gpu"))
		gres_slurmd_conf.config_flags |=
			(GRES_CONF_ENV_SET | GRES_CONF_ENV_DEF);
	if (merge_gres->gres_ctx->config_flags & GRES_CONF_COUNT_ONLY)
		gres_slurmd_conf.config_flags |= GRES_CONF_COUNT_ONLY;

	gres_slurmd_conf.count = count;

	add_gres_to_list(merge_gres->new_list, &gres_slurmd_conf);
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
static int _merge_gres(void *x, void *arg)
{
	gres_state_t *gres_state_node = x;
	merge_gres_t *merge_gres = arg;
	gres_node_state_t *gres_ns;

	if (gres_state_node->plugin_id != merge_gres->gres_ctx->plugin_id)
		return 0;

	gres_ns = gres_state_node->gres_data;
	/* If this GRES has no types, merge in the single untyped GRES */
	if (gres_ns->type_cnt == 0) {
		_merge_gres2(merge_gres,
			     gres_ns->gres_cnt_config, NULL);
		return 0;
	}

	/* If this GRES has types, merge in each typed GRES */
	for (int i = 0; i < gres_ns->type_cnt; i++) {
		_merge_gres2(merge_gres,
			     gres_ns->type_cnt_avail[i],
			     gres_ns->type_name[i]);
	}

	return 0;
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
static void _merge_config(node_config_load_t *node_conf, list_t *gres_conf_list,
			  list_t *slurm_conf_list)
{
	merge_gres_t merge_gres = {
		.cpu_cnt = node_conf->cpu_cnt,
		.gres_conf_list = gres_conf_list,
		.new_list = list_create(destroy_gres_slurmd_conf),
	};

	for (int i = 0; i < gres_context_cnt; i++) {
		merge_gres.gres_ctx = &gres_context[i];

		/* Copy GRES configuration from slurm.conf */
		if (slurm_conf_list) {
			if (list_for_each(slurm_conf_list,
					  _merge_gres,
					  &merge_gres) > 0)
				continue;
		}

		/* Add GRES record with zero count */
		_add_gres_config_empty(&merge_gres);
	}
	/* Set gres_conf_list to be the new merged list */
	list_flush(gres_conf_list);
	list_transfer(gres_conf_list, merge_gres.new_list);
	FREE_NULL_LIST(merge_gres.new_list);
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
	gres_send_stepd(buffer, gres_ctx->np_gres_devices);
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
	safe_unpackstr(&gres_ctx->gres_name, buffer);
	safe_unpackstr(&gres_ctx->gres_name_colon, buffer);
	safe_unpack32(&uint32_tmp, buffer);
	gres_ctx->gres_name_colon_len = (int)uint32_tmp;
	safe_unpackstr(&gres_ctx->gres_type, buffer);
	gres_recv_stepd(buffer, &gres_ctx->np_gres_devices);
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
	safe_unpackstr(&gres_slurmd_conf->cpus, buffer);
	unpack_bit_str_hex(&gres_slurmd_conf->cpus_bitmap, buffer);
	safe_unpackstr(&gres_slurmd_conf->file, buffer);
	safe_unpackstr(&gres_slurmd_conf->links, buffer);
	safe_unpackstr(&gres_slurmd_conf->name, buffer);
	safe_unpackstr(&gres_slurmd_conf->type_name, buffer);
	safe_unpackstr(&gres_slurmd_conf->unique_id, buffer);
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

/* List helper function for gres_node_config_load */
static void _free_name_list(void *x)
{
	free(x);
}

/* Fills major and minor information for a gres_device_t dev */
static int _set_gres_device_desc(gres_device_t *dev)
{
	struct stat fs;

	dev->dev_desc.type = DEV_TYPE_NONE;
	dev->dev_desc.major = NO_VAL;
	dev->dev_desc.minor = NO_VAL;

	if (stat(dev->path, &fs) < 0) {
		error("%s: stat(%s): %m", __func__, dev->path);
		return SLURM_ERROR;
	}

	dev->dev_desc.major = major(fs.st_rdev);
	dev->dev_desc.minor = minor(fs.st_rdev);
	log_flag(GRES, "%s : %s major %d, minor %d", __func__, dev->path,
		 dev->dev_desc.major, dev->dev_desc.minor);

	if (S_ISBLK(fs.st_mode))
		dev->dev_desc.type = DEV_TYPE_BLOCK;
	else if (S_ISCHR(fs.st_mode))
		dev->dev_desc.type = DEV_TYPE_CHAR;
	else {
		error("%s is not a valid character or block device, fix your gres.conf",
		      dev->path);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}


/*
 * Creates and initializes a gres_device_t from a path, an bitmap index and a
 * unique_id. At failure return NULL.
 */
static gres_device_t *_init_gres_device(int index, char *one_name,
					char *unique_id)
{
	int tmp, digit = -1;
	gres_device_t *gres_device = xmalloc(sizeof(gres_device_t));

	gres_device->dev_num = -1;
	gres_device->index = index;
	gres_device->path = xstrdup(one_name);
	gres_device->unique_id = xstrdup(unique_id);

	if (_set_gres_device_desc(gres_device) != SLURM_SUCCESS) {
		xfree(gres_device);
		return NULL;
	}

	tmp = strlen(one_name);
	for (int i = 1;  i <= tmp; i++) {
		if (isdigit(one_name[tmp - i])) {
			digit = tmp - i;
			continue;
		}
		break;
	}
	if (digit >= 0)
		gres_device->dev_num = atoi(one_name + digit);
	else
		gres_device->dev_num = -1;

	return gres_device;
}

/* Load the specific GRES plugins here */
static int _load_specific_gres_plugins(void)
{
	int rc;

	if ((rc = gpu_plugin_init()) != SLURM_SUCCESS)
		return rc;

	return rc;
}

static int _foreach_fill_in_gres_devices(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	foreach_fill_in_gres_devices_t *fill_in_gres_devices = arg;
	node_config_load_t *config = fill_in_gres_devices->config;
	hostlist_t *hl;
	char *one_name;

	if (!(gres_slurmd_conf->config_flags & GRES_CONF_HAS_FILE) ||
	    !gres_slurmd_conf->file ||
	    xstrcmp(gres_slurmd_conf->name, config->gres_name))
		return 0;

	if (!(hl = hostlist_create(gres_slurmd_conf->file))) {
		error("can't parse gres.conf file record (%s)",
		      gres_slurmd_conf->file);
		return 0;
	}

	while ((one_name = hostlist_shift(hl))) {
		/* We don't care about gres_devices in slurmctld */
		if (config->in_slurmd) {
			gres_device_t *gres_device;
			if (!*fill_in_gres_devices->gres_devices)
				*fill_in_gres_devices->gres_devices =
					list_create(destroy_gres_device);

			if (!(gres_device = _init_gres_device(
				      fill_in_gres_devices->index, one_name,
				      gres_slurmd_conf->unique_id))) {
				free(one_name);
				continue;
			}

			if (gres_device->dev_num >
			    fill_in_gres_devices->max_dev_num)
				fill_in_gres_devices->max_dev_num =
					gres_device->dev_num;

			list_append(*fill_in_gres_devices->gres_devices,
				    gres_device);
		}

		/*
		 * Don't check for file duplicates or increment the
		 * device bitmap index if this is a MultipleFiles GRES
		 */
		if (gres_slurmd_conf->config_flags & GRES_CONF_HAS_MULT) {
			free(one_name);
			continue;
		}

		if ((fill_in_gres_devices->rc == SLURM_SUCCESS) &&
		    list_find_first(fill_in_gres_devices->names_list,
				    slurm_find_char_exact_in_list,
				    one_name)) {
			error("%s duplicate device file name (%s)",
			      config->gres_name, one_name);
			fill_in_gres_devices->rc = SLURM_ERROR;
		}

		list_append(fill_in_gres_devices->names_list, one_name);

		/* Increment device bitmap index */
		fill_in_gres_devices->index++;
	}
	hostlist_destroy(hl);

	if (gres_slurmd_conf->config_flags & GRES_CONF_HAS_MULT)
		fill_in_gres_devices->index++;

	return 0;
}

static int _foreach_fill_in_gres_devices_dev_id(void *x, void *arg)
{
	gres_device_t *gres_device = x;
	foreach_fill_in_gres_devices_t *fill_in_gres_devices = arg;

	if (gres_device->dev_num == -1)
		gres_device->dev_num = ++fill_in_gres_devices->max_dev_num;

	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES) {
		char *dev_id_str = gres_device_id2str(&gres_device->dev_desc);
		log_flag(GRES, "%s device number %d(%s):%s",
			 fill_in_gres_devices->config->gres_name,
			 gres_device->dev_num,
			 gres_device->path,
			 dev_id_str);
		xfree(dev_id_str);
	}

	return 0;
}

extern int gres_node_config_load(list_t *gres_conf_list,
				 node_config_load_t *config,
				 list_t **gres_devices)
{
	foreach_fill_in_gres_devices_t fill_in_gres_devices = {
		.config = config,
		.gres_devices = gres_devices,
		.index = 0,
		.max_dev_num = -1,
		.names_list = list_create(_free_name_list),
		.rc = SLURM_SUCCESS,
	};
	xassert(gres_conf_list);
	xassert(gres_devices);

	(void) list_for_each(gres_conf_list, _foreach_fill_in_gres_devices,
			     &fill_in_gres_devices);
	FREE_NULL_LIST(fill_in_gres_devices.names_list);

	if (*gres_devices)
		(void) list_for_each(*gres_devices,
				     _foreach_fill_in_gres_devices_dev_id,
				     &fill_in_gres_devices);

	return fill_in_gres_devices.rc;
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
				   list_t *gres_list,
				   void *xcpuinfo_abs_to_mac,
				   void *xcpuinfo_mac_to_abs)
{
	static s_p_options_t _gres_conf_options[] = {
		{"AutoDetect", S_P_STRING},
		{"Name",     S_P_ARRAY, _parse_gres_config,  NULL},
		{"NodeName", S_P_ARRAY, _parse_gres_config_node, NULL},
		{NULL}
	};
	list_t *tmp_gres_conf_list = NULL;

	int count = 0, i, rc, rc2;
	struct stat config_stat;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t **gres_array;
	char *gres_conf_file = NULL;
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

	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);

	if (gres_context_cnt == 0) {
		rc = SLURM_SUCCESS;
		goto fini;
	}

	tmp_gres_conf_list = list_create(destroy_gres_slurmd_conf);
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
		if (s_p_parse_file(tbl, NULL, gres_conf_file, 0, NULL) ==
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
		      GRES_AUTODETECT_GPU_OFF)) {
			rc = ESLURM_UNSUPPORTED_GRES;
			error("Cannot use AutoDetect on cloud/dynamic node \"%s\"",
			      gres_node_name);
			s_p_hashtbl_destroy(tbl);
			goto fini;
		}

		if (s_p_get_array((void ***) &gres_array,
				  &count, "Name", tbl)) {
			for (i = 0; i < count; i++) {
				list_append(tmp_gres_conf_list, gres_array[i]);
				gres_array[i] = NULL;
			}
		}
		if (s_p_get_array((void ***) &gres_array,
				  &count, "NodeName", tbl)) {
			for (i = 0; i < count; i++) {
				list_append(tmp_gres_conf_list, gres_array[i]);
				gres_array[i] = NULL;
			}
		}
		s_p_hashtbl_destroy(tbl);
	}
	FREE_NULL_LIST(gres_conf_list);
	gres_conf_list = tmp_gres_conf_list;
	tmp_gres_conf_list = NULL;

	/* Validate gres.conf and slurm.conf somewhat before merging */
	for (i = 0; i < gres_context_cnt; i++) {
		_validate_slurm_conf(gres_list, &gres_context[i]);
		_validate_gres_conf(gres_conf_list, &gres_context[i]);
		_check_conf_mismatch(gres_list, gres_conf_list,
				     &gres_context[i]);
	}

	/* Merge slurm.conf and gres.conf together into gres_conf_list */
	_merge_config(&node_conf, gres_conf_list, gres_list);

	if ((rc = _load_specific_gres_plugins()) != SLURM_SUCCESS) {
		goto fini;
	}

	for (i = 0; i < gres_context_cnt; i++) {
		node_conf.gres_name = gres_context[i].gres_name;
		if (gres_context[i].ops.node_config_load)
			rc2 = (*(gres_context[i].ops.node_config_load))(
				gres_conf_list, &node_conf);
		else if (gres_context[i].config_flags & GRES_CONF_HAS_FILE) {
			rc2 = gres_node_config_load(
				gres_conf_list, &node_conf,
				&gres_context[i].np_gres_devices);
		} else
			continue;

		if (rc == SLURM_SUCCESS)
			rc = rc2;
	}

	/* Postprocess gres_conf_list after all plugins' node_config_load */

	/* Remove every GPU with an empty File */
	(void) list_delete_all(gres_conf_list, _find_fileless_gres,
			       &gpu_plugin_id);

	list_for_each(gres_conf_list, _log_gres_slurmd_conf, NULL);

	for (i = 0; i < gres_context_cnt; i++) {
		list_for_each(gres_conf_list, _post_plugin_gres_conf,
			      &gres_context[i]);
	}

fini:
	xfree(gres_conf_file);
	FREE_NULL_LIST(tmp_gres_conf_list);
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
	int rc = SLURM_SUCCESS;
	uint32_t magic = GRES_MAGIC;
	uint16_t rec_cnt = 0, version = SLURM_PROTOCOL_VERSION;
	list_itr_t *iter;
	gres_slurmd_conf_t *gres_slurmd_conf;

	pack16(version, buffer);
	if (gres_conf_list)
		rec_cnt = list_count(gres_conf_list);
	pack16(rec_cnt, buffer);
	if (rec_cnt) {
		/*
		 * It might be tempting to convert this to slurm_pack_list,
		 * The problem with that is how we unpack things in the function
		 * below this. It uses 'node_name' all throughout which can not
		 * be passed to slurm_unpack_list. This function is not called
		 * very often (only when the slurmd registers). The efforts to
		 * make this work are just not worth it.
		 */
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

	return rc;
}

/*
 * Unpack this node's configuration from a buffer (built/packed by slurmd)
 * IN/OUT buffer - message buffer to unpack
 * IN node_name - name of node whose data is being unpacked
 */
extern int gres_node_config_unpack(buf_t *buffer, char *node_name)
{
	int i, rc = SLURM_SUCCESS;
	uint32_t cpu_cnt = 0, magic = 0, plugin_id = 0;
	uint64_t count64 = 0;
	uint16_t rec_cnt = 0, protocol_version = 0;
	uint32_t config_flags = 0;
	char *tmp_cpus = NULL, *tmp_links = NULL, *tmp_name = NULL;
	char *tmp_type = NULL;
	char *tmp_unique_id = NULL;
	gres_slurmd_conf_t *p;
	bool locked = false;
	slurm_gres_context_t *gres_ctx;

	xassert(gres_context_cnt >= 0);

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
		if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;

			safe_unpack64(&count64, buffer);
			safe_unpack32(&cpu_cnt, buffer);
			safe_unpack32(&config_flags, buffer);
			safe_unpack32(&plugin_id, buffer);
			safe_unpackstr(&tmp_cpus, buffer);
			safe_unpackstr(&tmp_links, buffer);
			safe_unpackstr(&tmp_name, buffer);
			safe_unpackstr(&tmp_type, buffer);
			safe_unpackstr(&tmp_unique_id, buffer);
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
		if (new_has_file && (count64 > MAX_GRES_BITMAP) &&
		    !gres_id_shared(config_flags)) {
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

		/*
		 * If we read in from state we want to take the slurmd's view
		 * over our state.
		 */
		if (gres_ctx->config_flags & GRES_CONF_FROM_STATE)
			gres_ctx->config_flags = config_flags;
		else
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
		if (gres_ns->topo_res_core_bitmap)
			FREE_NULL_BITMAP(gres_ns->topo_res_core_bitmap[i]);
		xfree(gres_ns->topo_type_name[i]);
	}
	xfree(gres_ns->topo_gres_bitmap);
	xfree(gres_ns->topo_core_bitmap);
	xfree(gres_ns->topo_gres_cnt_alloc);
	xfree(gres_ns->topo_gres_cnt_avail);
	xfree(gres_ns->topo_res_core_bitmap);
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

static int _find_gres_type(gres_node_state_t *gres_ns, uint32_t type_id)
{
	int type_index = -1;
	for (int i = 0; i < gres_ns->type_cnt; i++) {
		if(type_id == gres_ns->type_id[i]) {
			type_index = i;
			break;
		}
	}
	return type_index;
}

static int _valid_gres_type(char *gres_name, gres_node_state_t *gres_ns,
			    bool config_overrides, char **reason_down)
{
	int i, j;
	uint64_t model_cnt;
	int num_type_rem = 0;

	if (gres_ns->type_cnt == 0)
		return SLURM_SUCCESS;

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
				if (gres_ns->topo_type_id[i] ==
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
			return SLURM_ERROR;
		}
	}

	/*
	 * Remove types with 0 available. This happens when updating the type
	 * of a gres in slurm.conf during a reconfig
	 */
	for (int i = 0; i < gres_ns->type_cnt; i++) {
		if (gres_ns->type_cnt_avail[i])
			continue;
		num_type_rem++;
	}

	if (num_type_rem) {
		int tmp_cnt;
		uint64_t *tmp_type_cnt_alloc, *tmp_type_cnt_avail;
		uint32_t *tmp_type_id;
		char **tmp_type_name;

		tmp_cnt = gres_ns->type_cnt - num_type_rem;
		tmp_type_id = xcalloc(tmp_cnt, sizeof(*tmp_type_id));
		tmp_type_cnt_alloc =
			xcalloc(tmp_cnt, sizeof(*tmp_type_cnt_alloc));
		tmp_type_cnt_avail =
			xcalloc(tmp_cnt, sizeof(*tmp_type_cnt_avail));
		tmp_type_name =
			xcalloc(tmp_cnt, sizeof(*tmp_type_name));

		for (int j = 0, i = 0; i < gres_ns->type_cnt; i++) {
			if (!gres_ns->type_cnt_avail[i]) {
				xfree(gres_ns->type_name[i]);
				continue;
			}
			tmp_type_cnt_alloc[j] =
				gres_ns->type_cnt_alloc[i];
			tmp_type_cnt_avail[j] =
				gres_ns->type_cnt_avail[i];
			tmp_type_id[j] = gres_ns->type_id[i];
			tmp_type_name[j] = gres_ns->type_name[i];
			j++;
		}

		xfree(gres_ns->type_cnt_alloc);
		xfree(gres_ns->type_cnt_avail);
		xfree(gres_ns->type_id);
		xfree(gres_ns->type_name);

		gres_ns->type_cnt_alloc = tmp_type_cnt_alloc;
		gres_ns->type_cnt_avail = tmp_type_cnt_avail;
		gres_ns->type_id = tmp_type_id;
		gres_ns->type_name = tmp_type_name;
		gres_ns->type_cnt -= num_type_rem;
	}

	for (int i = 0; i < gres_ns->topo_cnt; i++) {
		if (_find_gres_type(gres_ns, gres_ns->topo_type_id[i]) < 0) {
			if (reason_down && (*reason_down == NULL)) {
				xstrfmtcat(*reason_down,
					   "%s type (%s) reported but not configured",
					   gres_name,
					   gres_ns->topo_type_name[i]);
			}
			return SLURM_ERROR;
		}
	}

	return SLURM_SUCCESS;
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
static void _node_config_init(char *orig_config, slurm_gres_context_t *gres_ctx,
			      gres_state_t *gres_state_node)
{
	gres_node_state_t *gres_ns;

	if (!gres_state_node->gres_data)
		gres_state_node->gres_data = _build_gres_node_state();
	gres_ns = (gres_node_state_t *) gres_state_node->gres_data;

	/* If the resource isn't configured for use with this node */
	if ((orig_config == NULL) || (orig_config[0] == '\0')) {
		gres_ns->gres_cnt_config = 0;
		return;
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
}

/* Set up the shared/sharing pointers for easy look up later */
static void _set_alt_gres(gres_state_t *gres_state_node_shared,
			  gres_state_t *gres_state_node_sharing)
{
	if (gres_state_node_shared) {
		if (!gres_state_node_sharing) {
			error("we have a shared gres of '%s' but no gres that is sharing",
			      gres_state_node_shared->gres_name);
		} else {
			gres_node_state_t *gres_ns_shared =
				gres_state_node_shared->gres_data;
			gres_node_state_t *gres_ns_sharing =
				gres_state_node_sharing->gres_data;
			gres_ns_shared->alt_gres = gres_state_node_sharing;
			gres_ns_sharing->alt_gres = gres_state_node_shared;
		}
	}
}

/*
 * Build a node's gres record based only upon the slurm.conf contents
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT gres_list - List of Gres records for this node to track usage
 */
extern void gres_init_node_config(char *orig_config, list_t **gres_list)
{
	gres_state_t *gres_state_node, *gres_state_node_sharing = NULL,
		*gres_state_node_shared = NULL;

	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_node_list_delete);
	}
	for (int i = 0; i < gres_context_cnt; i++) {
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

		_node_config_init(orig_config, &gres_context[i],
				  gres_state_node);

		gres_ns = gres_state_node->gres_data;
		if (gres_ns && gres_ns->gres_cnt_config) {
			if (gres_id_sharing(gres_state_node->plugin_id))
				gres_state_node_sharing = gres_state_node;
			else if (gres_id_shared(gres_state_node->config_flags))
				gres_state_node_shared = gres_state_node;
		}
	}
	slurm_mutex_unlock(&gres_context_lock);

	_set_alt_gres(gres_state_node_shared, gres_state_node_sharing);
}

static int _foreach_get_tot_from_slurmd_conf(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	tot_from_slurmd_conf_t *slurmd_conf_tot = arg;

	if (gres_slurmd_conf->plugin_id != slurmd_conf_tot->plugin_id)
		return 0;

	slurmd_conf_tot->config_flags |= gres_slurmd_conf->config_flags;

	slurmd_conf_tot->gres_cnt += gres_slurmd_conf->count;
	slurmd_conf_tot->rec_cnt++;

	if (gres_slurmd_conf->cpus || gres_slurmd_conf->type_name)
		slurmd_conf_tot->cpu_set_cnt++;

	return 0;
}

/*
 * Determine GRES availability on some node
 *
 * tot_from_slurmd_conf_t:
 * plugin_id IN - plugin number to search for
 * config_flags OUT - config flags from slurmd
 * topo_cnt OUT - count of gres.conf records of this ID found by slurmd
 *		  (each can have different topology)
 * config_type_cnt OUT - Count of records for this GRES found in configuration,
 *		  each of this represents a different Type of of GRES with
 *		  this name (e.g. GPU model)
 * gres_cnt OUT - total number of GRES available of this ID on this node in (sum
 * 		  across all records of this ID)
 */
static void _get_tot_from_slurmd_conf(tot_from_slurmd_conf_t *slurmd_conf_tot)
{
	xassert(slurmd_conf_tot);

	slurmd_conf_tot->config_flags = 0;
	slurmd_conf_tot->cpu_set_cnt = 0;
	slurmd_conf_tot->config_type_cnt = 0;
	slurmd_conf_tot->topo_cnt = 0;
	slurmd_conf_tot->gres_cnt = 0;
	slurmd_conf_tot->rec_cnt = 0;

	if (gres_conf_list == NULL)
		return;

	(void) list_for_each(gres_conf_list, _foreach_get_tot_from_slurmd_conf,
			     slurmd_conf_tot);

	slurmd_conf_tot->config_type_cnt = slurmd_conf_tot->rec_cnt;
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
	if (rc) {
		error("%s: %s If using AutoDetect the amount of GPUs configured in slurm.conf does not match what was detected. If this is intentional, please turn off AutoDetect and manually specify them in gres.conf.",
		      __func__, tmp);
		if (reason_down && !(*reason_down)) {
			*reason_down = tmp;
			tmp = NULL;
		} else
			xfree(tmp);

		/* create zeroed-out links array (NVLINK_NONE == 0) */
		memset(gres_ns->links_cnt[gres_inx], 0, gres_cnt * sizeof(int));
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

/*
 * Job scheduling handles gres affinity on a socket basis internally.
 * However, the interface for setting affinity is to specify cores. This can
 * lead to the faulty expectation that the core affinity will be respected by
 * the Slurm scheduler.
 *
 * Therefore this check was added to avoid users setting the cores limit and
 * expecting Slurm to respect it (which it doesn't and never has).
 *
 * In addition to misleading users, a bug can arise where steps and jobs don't
 * line up because steps do look at the cores rather than the sockets like the
 * jobs. (i.e. job allocates a core the the step rejects), if we just wanted to
 * solve this bug we would just expand the cpu list to fill the socket here
 * instead of throwing an error.
 */
static int _check_core_range_matches_sock(bitstr_t *tmp_bitmap,
					  rebuild_topo_t *rebuild_topo,
					  gres_slurmd_conf_t *gres_slurmd_conf)
{
	for (int i = 0; (i < rebuild_topo->sock_cnt); i++) {
		int first = i * rebuild_topo->cores_per_sock;
		int last = (i + 1) * rebuild_topo->cores_per_sock;
		int core_cnt = bit_set_count_range(tmp_bitmap, first, last);

		if (core_cnt && (core_cnt != rebuild_topo->cores_per_sock)) {
			slurm_gres_context_t *gres_ctx = rebuild_topo->gres_ctx;
			gres_node_state_t *gres_ns = rebuild_topo->gres_ns;
			char *gres_cores_str = bit_fmt_full(tmp_bitmap);
			char *tmp;

			if (gres_slurmd_conf->config_flags &
			    GRES_CONF_AUTODETECT) {
				tmp = xstrdup_printf(
					"%s GRES autodetected core affinity %s on node %s doesn't match socket boundaries. (Socket %d is cores %d-%d). "
					"Consider setting SlurmdParameters=l3cache_as_socket (recommended) or override this by manually specifying core affinity in gres.conf.",
					gres_ctx->gres_type, gres_cores_str,
					rebuild_topo->node_name, i, first,
					(last - 1));
			} else {
				tmp = xstrdup_printf(
					"%s GRES core specification %s for node %s doesn't match socket boundaries. (Socket %d is cores %d-%d)",
					gres_ctx->gres_type, gres_cores_str,
					rebuild_topo->node_name, i, first,
					(last - 1));
			}
			xfree(gres_cores_str);
			FREE_NULL_BITMAP(gres_ns->topo_core_bitmap[
						 rebuild_topo->topo_cnt]);
			rebuild_topo->rc = EINVAL;
			error("%s: %s", __func__, tmp);
			if (rebuild_topo->reason_down &&
			    !(*rebuild_topo->reason_down))
				xstrfmtcat(*rebuild_topo->reason_down, "%s",
					   tmp);
			xfree(tmp);
			return SLURM_ERROR;
		}
	}
	return SLURM_SUCCESS;
}

static int _foreach_rebuild_topo(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	rebuild_topo_t *rebuild_topo = arg;
	slurm_gres_context_t *gres_ctx = rebuild_topo->gres_ctx;
	gres_node_state_t *gres_ns = rebuild_topo->gres_ns;
	int topo_cnt = rebuild_topo->topo_cnt;

	if (gres_slurmd_conf->plugin_id != gres_ctx->plugin_id)
		return 0;

	if (gres_ns->gres_bit_alloc && !gres_id_shared(gres_ctx->config_flags))
		gres_ns->topo_gres_cnt_alloc[topo_cnt] = 0;
	gres_ns->topo_gres_cnt_avail[topo_cnt] = gres_slurmd_conf->count;
	if (gres_slurmd_conf->cpus) {
		/* NOTE: gres_slurmd_conf->cpus is cores */
		bitstr_t *tmp_bitmap = bit_alloc(rebuild_topo->core_cnt);
		int ret = bit_unfmt(tmp_bitmap, gres_slurmd_conf->cpus);
		if (ret != SLURM_SUCCESS) {
			error("%s: %s: invalid GRES core specification (%s) on node %s",
			      __func__, gres_ctx->gres_type,
			      gres_slurmd_conf->cpus,
			      rebuild_topo->node_name);
			FREE_NULL_BITMAP(tmp_bitmap);
			rebuild_topo->rc = ESLURM_INVALID_GRES;
			return -1;
		} else {
			FREE_NULL_BITMAP(
				gres_ns->topo_core_bitmap[topo_cnt]);
			gres_ns->topo_core_bitmap[topo_cnt] = tmp_bitmap;
		}
		if (_check_core_range_matches_sock(tmp_bitmap, rebuild_topo,
						   gres_slurmd_conf))
			return -1;

		rebuild_topo->cpus_config = rebuild_topo->core_cnt;
	} else if (rebuild_topo->cpus_config && !rebuild_topo->cpu_config_err) {
		rebuild_topo->cpu_config_err = true;
		error("%s: %s: has CPUs configured for only some of the records on node %s",
		      __func__, gres_ctx->gres_type, rebuild_topo->node_name);
	}

	if (gres_slurmd_conf->links) {
		if (gres_ns->links_cnt &&
		    (gres_ns->link_len != rebuild_topo->tot_gres_cnt)) {
			/* Size changed, need to rebuild */
			for (int j = 0; j < gres_ns->link_len; j++)
				xfree(gres_ns->links_cnt[j]);
			xfree(gres_ns->links_cnt);
		}
		if (!gres_ns->links_cnt) {
			gres_ns->link_len = rebuild_topo->tot_gres_cnt;
			gres_ns->links_cnt = xcalloc(rebuild_topo->tot_gres_cnt,
						     sizeof(int *));
			for (int j = 0; j < rebuild_topo->tot_gres_cnt; j++) {
				gres_ns->links_cnt[j] =
					xcalloc(rebuild_topo->tot_gres_cnt,
						sizeof(int));
			}
		}
	}
	if (gres_id_shared(gres_slurmd_conf->config_flags)) {
		/* If running jobs recovered then already set */
		if (!gres_ns->topo_gres_bitmap[topo_cnt]) {
			gres_ns->topo_gres_bitmap[topo_cnt] =
				bit_alloc(rebuild_topo->dev_cnt);
			bit_set(gres_ns->topo_gres_bitmap[topo_cnt],
				rebuild_topo->gres_inx);
		}
		rebuild_topo->gres_inx++;
	} else if (!rebuild_topo->dev_cnt) {
		/*
		 * Slurmd found GRES, but slurmctld can't use
		 * them. Avoid creating zero-size bitmaps.
		 */
		rebuild_topo->has_file = false;
	} else {
		FREE_NULL_BITMAP(gres_ns->topo_gres_bitmap[topo_cnt]);
		gres_ns->topo_gres_bitmap[topo_cnt] =
			bit_alloc(rebuild_topo->dev_cnt);
		for (int j = 0; j < gres_slurmd_conf->count; j++) {
			if (rebuild_topo->gres_inx >= rebuild_topo->dev_cnt) {
				/* Ignore excess GRES on node */
				break;
			}
			bit_set(gres_ns->topo_gres_bitmap[topo_cnt],
				rebuild_topo->gres_inx);
			if (gres_ns->gres_bit_alloc &&
			    bit_test(gres_ns->gres_bit_alloc,
				     rebuild_topo->gres_inx)) {
				/* Set by recovered job */
				gres_ns->topo_gres_cnt_alloc[topo_cnt]++;
			}
			if (_links_str2array(
				    gres_slurmd_conf->links,
				    rebuild_topo->node_name, gres_ns,
				    rebuild_topo->gres_inx,
				    rebuild_topo->tot_gres_cnt,
				    rebuild_topo->reason_down) != SLURM_SUCCESS)
				rebuild_topo->rc = EINVAL;

			rebuild_topo->gres_inx++;
		}
	}
	gres_ns->topo_type_id[topo_cnt] =
		gres_build_id(gres_slurmd_conf->type_name);
	xfree(gres_ns->topo_type_name[topo_cnt]);
	gres_ns->topo_type_name[topo_cnt] =
		xstrdup(gres_slurmd_conf->type_name);
	rebuild_topo->topo_cnt++;
	if (rebuild_topo->topo_cnt >= gres_ns->topo_cnt)
		return -1;

	return 0;
}

static int _foreach_rebuild_topo_no_cpus(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	rebuild_topo_t *rebuild_topo = arg;
	slurm_gres_context_t *gres_ctx = rebuild_topo->gres_ctx;
	gres_node_state_t *gres_ns = rebuild_topo->gres_ns;

	if (gres_slurmd_conf->plugin_id != gres_ctx->plugin_id)
		return 0;

	for (int j = 0; j < rebuild_topo->topo_cnt; j++) {
		if (gres_ns->topo_core_bitmap[j])
			continue;
		gres_ns->topo_core_bitmap[j] =
			bit_alloc(rebuild_topo->core_cnt);
		bit_set_all(gres_ns->topo_core_bitmap[j]);
	}

	return 0;
}

static int _foreach_add_gres_info(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	add_gres_info_t *add_gres_info = arg;
	slurm_gres_context_t *gres_ctx = add_gres_info->gres_ctx;
	gres_node_state_t *gres_ns = add_gres_info->gres_ns;
	uint32_t type_id;
	int i;

	if (gres_slurmd_conf->plugin_id != gres_ctx->plugin_id)
		return 0;

	type_id = gres_build_id(gres_slurmd_conf->type_name);

	for (i = 0; i < gres_ns->type_cnt; i++) {
		if (type_id == gres_ns->type_id[i])
			break;
	}
	if (i < gres_ns->type_cnt) {
		/* Update count as needed */
		gres_ns->type_cnt_avail[i] = gres_slurmd_conf->count;
	} else {
		gres_add_type(gres_slurmd_conf->type_name,
			      gres_ns,
			      gres_slurmd_conf->count);
	}

	return 0;
}

static int _node_config_validate(char *node_name, char *orig_config,
				 gres_state_t *gres_state_node,
				 int cpu_cnt, int core_cnt, int sock_cnt,
				 int cores_per_sock,
				 bool config_overrides, char **reason_down,
				 slurm_gres_context_t *gres_ctx)
{
	int i, rc = SLURM_SUCCESS;
	uint64_t dev_cnt;
	bool updated_config = false;
	gres_node_state_t *gres_ns;
	bool has_file, has_type, first_time = false, rebuild_topo = false;
	tot_from_slurmd_conf_t slurmd_conf_tot = {
		.plugin_id = gres_ctx->plugin_id,
	};
	xassert(core_cnt);
	if (gres_state_node->gres_data == NULL)
		gres_state_node->gres_data = _build_gres_node_state();
	gres_ns = (gres_node_state_t *) gres_state_node->gres_data;
	if (gres_ns->node_feature)
		return rc;

	_get_tot_from_slurmd_conf(&slurmd_conf_tot);

	/* If the gres is sharing we need to have topo configured. */
	if (slurmd_conf_tot.cpu_set_cnt ||
	    (gres_id_sharing(slurmd_conf_tot.plugin_id) && gres_ns->alt_gres))
		slurmd_conf_tot.topo_cnt = slurmd_conf_tot.rec_cnt;

	/*
	 * Check existing config_flags before overriding from
	 * slurmd_conf_tot.config_flags.
	 */
	if (gres_state_node->config_flags & GRES_CONF_UPDATE_CONFIG)
		updated_config = true;

	/* Make sure these are insync after we get it from the slurmd */
	gres_state_node->config_flags = slurmd_conf_tot.config_flags;

	if (gres_ns->gres_cnt_config > slurmd_conf_tot.gres_cnt) {
		if (reason_down && (*reason_down == NULL)) {
			xstrfmtcat(*reason_down,
				   "%s count reported lower than configured "
				   "(%"PRIu64" < %"PRIu64")",
				   gres_ctx->gres_type,
				   slurmd_conf_tot.gres_cnt,
				   gres_ns->gres_cnt_config);
		}
		rc = EINVAL;
	}
	if ((slurmd_conf_tot.gres_cnt > gres_ns->gres_cnt_config)) {
		debug("%s: %s: Ignoring excess count on node %s (%"
		      PRIu64" > %"PRIu64")",
		      __func__, gres_ctx->gres_type, node_name,
		      slurmd_conf_tot.gres_cnt,
		      gres_ns->gres_cnt_config);
		slurmd_conf_tot.gres_cnt = gres_ns->gres_cnt_config;
	}
	if (gres_ns->gres_cnt_found != slurmd_conf_tot.gres_cnt) {
		if (gres_ns->gres_cnt_found != NO_VAL64) {
			info("%s: %s: Count changed on node %s (%"PRIu64" != %"PRIu64")",
			     __func__, gres_ctx->gres_type, node_name,
			     gres_ns->gres_cnt_found,
			     slurmd_conf_tot.gres_cnt);
		}
		if ((gres_ns->gres_cnt_found != NO_VAL64) &&
		    (gres_ns->gres_cnt_alloc != 0)) {
			if (reason_down && (*reason_down == NULL)) {
				xstrfmtcat(*reason_down,
					   "%s count changed and jobs are using them "
					   "(%"PRIu64" != %"PRIu64")",
					   gres_ctx->gres_type,
					   gres_ns->gres_cnt_found,
					   slurmd_conf_tot.gres_cnt);
			}
			rc = EINVAL;
		} else {
			gres_ns->gres_cnt_found = slurmd_conf_tot.gres_cnt;
			updated_config = true;
			first_time = true;
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

	if (!first_time && gres_ns->type_cnt && gres_ns->topo_cnt) {
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			int type_index = _find_gres_type(gres_ns,
						gres_ns->topo_type_id[i]);
			/*
			 * On a reconfig if a type was removed from slurm.conf
			 * its type_cnt_avail will be set to 0. If the type is
			 * not found then the topo is from a previous invalid
			 * registration.
			 */
			if ((type_index < 0) ||
			    (gres_ns->type_cnt_avail[type_index] == 0 &&
			     gres_ns->topo_gres_cnt_avail[i])) {
				if (gres_ns->gres_cnt_alloc != 0) {
					if (reason_down &&
					    (*reason_down == NULL)) {
						xstrfmtcat(*reason_down,
							   "%s type changed and jobs are using them",
							   gres_ctx->gres_type);
					}
					rc = EINVAL;
					updated_config = false;
				} else {
					updated_config = true;
				}
			}

		}
	}

	if (!updated_config)
		return rc;

	if (gres_id_sharing(slurmd_conf_tot.plugin_id) && gres_ns->alt_gres) {
		/*
		 * Tell the shared gres to update itself if the sharing gres is
		 * updated -- which will happen in a subsequent call to
		 * _node_config_validate() since gres_node_config_validate() is
		 * looping on all gres_contexts.
		 */
		gres_ns->alt_gres->config_flags |= GRES_CONF_UPDATE_CONFIG;
	}

	if ((slurmd_conf_tot.gres_cnt > gres_ns->gres_cnt_config) &&
	    config_overrides) {
		info("%s: %s: count on node %s inconsistent with slurmctld count (%"PRIu64" != %"PRIu64")",
		     __func__, gres_ctx->gres_type, node_name,
		     slurmd_conf_tot.gres_cnt, gres_ns->gres_cnt_config);
		slurmd_conf_tot.gres_cnt = gres_ns->gres_cnt_config;
		/* Ignore excess GRES */
	}
	if ((slurmd_conf_tot.topo_cnt == 0) &&
	    (slurmd_conf_tot.topo_cnt != gres_ns->topo_cnt)) {
		/* Need to clear topology info */
		_gres_node_state_delete_topo(gres_ns);

		gres_ns->topo_cnt = slurmd_conf_tot.topo_cnt;
	}

	has_file = gres_ctx->config_flags & GRES_CONF_HAS_FILE;
	has_type = gres_ctx->config_flags & GRES_CONF_HAS_TYPE;
	if (gres_id_shared(gres_ctx->config_flags))
		dev_cnt = slurmd_conf_tot.topo_cnt;
	else
		dev_cnt = slurmd_conf_tot.gres_cnt;
	if (has_file && (slurmd_conf_tot.topo_cnt != gres_ns->topo_cnt) &&
	    (dev_cnt == 0)) {
		/*
		 * Clear any vestigial GRES node state info.
		 */
		_gres_node_state_delete_topo(gres_ns);

		xfree(gres_ns->gres_bit_alloc);

		gres_ns->topo_cnt = 0;
	} else if (has_file &&
		   (slurmd_conf_tot.topo_cnt != gres_ns->topo_cnt)) {
		/*
		 * Need to rebuild topology info.
		 * Resize the data structures here.
		 */
		rebuild_topo = true;
		/*
		 * Clear any vestigial GRES node state info.
		 */
		_gres_node_state_delete_topo(gres_ns);

		gres_ns->topo_gres_cnt_alloc =
			xrealloc(gres_ns->topo_gres_cnt_alloc,
				 slurmd_conf_tot.topo_cnt * sizeof(uint64_t));
		gres_ns->topo_gres_cnt_avail =
			xrealloc(gres_ns->topo_gres_cnt_avail,
				 slurmd_conf_tot.topo_cnt * sizeof(uint64_t));
		gres_ns->topo_gres_bitmap =
			xrealloc(gres_ns->topo_gres_bitmap,
				 slurmd_conf_tot.topo_cnt *
				 sizeof(bitstr_t *));
		gres_ns->topo_core_bitmap =
			xrealloc(gres_ns->topo_core_bitmap,
				 slurmd_conf_tot.topo_cnt *
				 sizeof(bitstr_t *));
		gres_ns->topo_res_core_bitmap =
			xrealloc(gres_ns->topo_res_core_bitmap,
				 slurmd_conf_tot.topo_cnt *
				 sizeof(bitstr_t *));
		gres_ns->topo_type_id = xrealloc(gres_ns->topo_type_id,
						 slurmd_conf_tot.topo_cnt *
						 sizeof(uint32_t));
		gres_ns->topo_type_name = xrealloc(gres_ns->topo_type_name,
						   slurmd_conf_tot.topo_cnt *
						   sizeof(char *));
		if (gres_ns->gres_bit_alloc)
			bit_realloc(gres_ns->gres_bit_alloc, dev_cnt);
		gres_ns->topo_cnt = slurmd_conf_tot.topo_cnt;
	} else if (gres_ns->topo_cnt) {
		/*
		 * Need to rebuild topology info to recover state after
		 * slurmctld restart with running jobs. The number of gpus,
		 * cores, and type might have changed in slurm.conf
		 */
		rebuild_topo = true;
	}

	if (rebuild_topo) {
		rebuild_topo_t rebuild_topo = {
			.core_cnt = core_cnt,
			.cores_per_sock = cores_per_sock,
			.dev_cnt = dev_cnt,
			.gres_ctx = gres_ctx,
			.gres_ns = gres_ns,
			.has_file = has_file,
			.node_name = node_name,
			.rc = rc,
			.reason_down = reason_down,
			.sock_cnt = sock_cnt,
			.tot_gres_cnt = slurmd_conf_tot.gres_cnt,
		};
		(void) list_for_each(gres_conf_list, _foreach_rebuild_topo,
				     &rebuild_topo);
		rc = rebuild_topo.rc;
		has_file = rebuild_topo.has_file;

		if (rebuild_topo.cpu_config_err) {
			/*
			 * Some GRES of this type have "CPUs" configured. Set
			 * topo_core_bitmap for all others with all bits set.
			 */
			(void) list_for_each(gres_conf_list,
					     _foreach_rebuild_topo_no_cpus,
					     &rebuild_topo);
		}
	} else if (!has_file && has_type) {
		add_gres_info_t add_gres_info = {
			.gres_ctx = gres_ctx,
			.gres_ns = gres_ns,
		};
		/* Add GRES Type information as needed */
		(void) list_for_each(gres_conf_list,
				     _foreach_add_gres_info,
				     &add_gres_info);
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
			gres_bits = slurmd_conf_tot.topo_cnt;
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

	gres_validate_node_cores(gres_ns, core_cnt, node_name);

	if ((slurmd_conf_tot.config_type_cnt > 1) &&
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

	if (!sharing_gres_ns->alt_gres)
		return;

	shared_gres_ns = sharing_gres_ns->alt_gres->gres_data;

	sharing_cnt = sharing_gres_ns->gres_cnt_avail;
	if (shared_gres_ns->gres_bit_alloc) {
		if ((sharing_cnt == bit_size(shared_gres_ns->gres_bit_alloc)) &&
		    (sharing_cnt == shared_gres_ns->topo_cnt)) {
			debug3("No change for gres/'shared'");
			return;
		}
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
		shared_gres_ns->topo_res_core_bitmap =
			xrealloc(shared_gres_ns->topo_res_core_bitmap,
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
		shared_gres_ns->topo_res_core_bitmap =
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
				     list_t **gres_list,
				     int threads_per_core,
				     int cores_per_sock, int sock_cnt,
				     bool config_overrides,
				     char **reason_down)
{
	int i, rc = SLURM_SUCCESS, rc2;
	gres_state_t *gres_state_node, *gres_gpu_ptr = NULL;
	int core_cnt = sock_cnt * cores_per_sock;
	int cpu_cnt  = core_cnt * threads_per_core;

	xassert(gres_context_cnt >= 0);

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
					    sock_cnt, cores_per_sock,
					    config_overrides,
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
			      char **new_config, list_t **gres_list)
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
	FREE_NULL_BITMAP(sock_map);

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
static void _build_node_gres_str(list_t **gres_list, char **gres_str,
				 int cores_per_sock, int sock_per_node)
{
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;
	bitstr_t *done_topo, *core_map;
	uint64_t gres_sum;
	char *sep = "", *suffix, *sock_info = NULL, *sock_str, *no_consume_str;
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
		no_consume_str = gres_ns->no_consume ? ":no_consume" : "";
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
					FREE_NULL_BITMAP(core_map);
					sock_str = sock_info;
				} else
					sock_str = "";
				suffix = _get_suffix(&gres_sum);
				if (gres_ns->topo_type_name[i]) {
					xstrfmtcat(*gres_str,
						   "%s%s:%s%s:%"PRIu64"%s%s", sep,
						   gres_context[c].gres_name,
						   gres_ns->
						   topo_type_name[i],
						   no_consume_str, gres_sum,
						   suffix, sock_str);
				} else {
					xstrfmtcat(*gres_str,
						   "%s%s%s:%"PRIu64"%s%s", sep,
						   gres_context[c].gres_name,
						   no_consume_str, gres_sum,
						   suffix, sock_str);
				}
				xfree(sock_info);
				sep = ",";
			}
			FREE_NULL_BITMAP(done_topo);
		} else if (gres_ns->type_cnt &&
			   gres_ns->gres_cnt_avail) {
			for (i = 0; i < gres_ns->type_cnt; i++) {
				gres_sum = gres_ns->type_cnt_avail[i];
				suffix = _get_suffix(&gres_sum);
				xstrfmtcat(*gres_str, "%s%s:%s%s:%"PRIu64"%s",
					   sep, gres_context[c].gres_name,
					   gres_ns->type_name[i],
					   no_consume_str, gres_sum, suffix);
				sep = ",";
			}
		} else if (gres_ns->gres_cnt_avail) {
			gres_sum = gres_ns->gres_cnt_avail;
			suffix = _get_suffix(&gres_sum);
			xstrfmtcat(*gres_str, "%s%s%s:%"PRIu64"%s",
				   sep, gres_context[c].gres_name,
				   no_consume_str, gres_sum, suffix);
			sep = ",";
		}
	}
}

static int _foreach_node_state_pack(void *x, void *arg)
{
	gres_state_t *gres_state_node = x;
	pack_state_t *pack_state = arg;
	gres_node_state_t *gres_ns = gres_state_node->gres_data;
	uint16_t gres_bitmap_size;

	if (pack_state->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(pack_state->magic, pack_state->buffer);
		pack32(gres_state_node->plugin_id, pack_state->buffer);
		pack32(gres_state_node->config_flags, pack_state->buffer);
		pack64(gres_ns->gres_cnt_avail, pack_state->buffer);
		/*
		 * Just note if gres_bit_alloc exists.
		 * Rebuild it based upon the state of recovered jobs
		 */
		if (gres_ns->gres_bit_alloc)
			gres_bitmap_size = bit_size(gres_ns->gres_bit_alloc);
		else
			gres_bitmap_size = 0;
		pack16(gres_bitmap_size, pack_state->buffer);

		pack16(gres_ns->topo_cnt, pack_state->buffer);
		for (int i = 0; i < gres_ns->topo_cnt; i++) {
			pack_bit_str_hex(gres_ns->topo_core_bitmap[i],
					 pack_state->buffer);
			pack_bit_str_hex(gres_ns->topo_gres_bitmap[i],
					 pack_state->buffer);
			pack_bit_str_hex(gres_ns->topo_res_core_bitmap[i],
					 pack_state->buffer);
		}
		pack64_array(gres_ns->topo_gres_cnt_alloc, gres_ns->topo_cnt,
			     pack_state->buffer);
		pack64_array(gres_ns->topo_gres_cnt_avail, gres_ns->topo_cnt,
			     pack_state->buffer);
		pack32_array(gres_ns->topo_type_id, gres_ns->topo_cnt,
			     pack_state->buffer);
		packstr_array(gres_ns->topo_type_name, gres_ns->topo_cnt,
			      pack_state->buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, pack_state->protocol_version);
		return -1;
	}

	return 0;
}

static int _foreach_job_state_pack(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	pack_state_t *pack_state = arg;
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	int i;

	if (pack_state->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(pack_state->magic, pack_state->buffer);
		pack32(gres_state_job->plugin_id, pack_state->buffer);
		pack16(gres_js->cpus_per_gres, pack_state->buffer);
		pack16(gres_js->flags, pack_state->buffer);
		pack64(gres_js->gres_per_job, pack_state->buffer);
		pack64(gres_js->gres_per_node, pack_state->buffer);
		pack64(gres_js->gres_per_socket, pack_state->buffer);
		pack64(gres_js->gres_per_task, pack_state->buffer);
		pack64(gres_js->mem_per_gres, pack_state->buffer);
		pack16(gres_js->ntasks_per_gres, pack_state->buffer);
		pack64(gres_js->total_gres, pack_state->buffer);
		packstr(gres_js->type_name, pack_state->buffer);
		pack32(gres_js->node_cnt, pack_state->buffer);

		if (gres_js->gres_cnt_node_alloc) {
			pack8((uint8_t) 1, pack_state->buffer);
			pack64_array(gres_js->gres_cnt_node_alloc,
				     gres_js->node_cnt, pack_state->buffer);
		} else {
			pack8((uint8_t) 0, pack_state->buffer);
		}

		if (gres_js->gres_bit_alloc) {
			pack8((uint8_t) 1, pack_state->buffer);
			for (i = 0; i < gres_js->node_cnt; i++) {
				pack_bit_str_hex(gres_js->
						 gres_bit_alloc[i],
						 pack_state->buffer);
			}
		} else {
			pack8((uint8_t) 0, pack_state->buffer);
		}
		for (i = 0; i < gres_js->node_cnt; i++) {
			if (!gres_js->gres_per_bit_alloc ||
			    !gres_js->gres_per_bit_alloc[i] ||
			    !gres_js->gres_bit_alloc ||
			    !gres_js->gres_bit_alloc[i]) {
				pack8((uint8_t)0, pack_state->buffer);
				continue;
			}
			pack8((uint8_t)1, pack_state->buffer);
			pack64_array(
				gres_js->gres_per_bit_alloc[i],
				bit_size(gres_js->gres_bit_alloc[i]),
				pack_state->buffer);
		}
		if (pack_state->details && gres_js->gres_bit_step_alloc) {
			pack8((uint8_t) 1, pack_state->buffer);
			for (i = 0; i < gres_js->node_cnt; i++) {
				pack_bit_str_hex(gres_js->
						 gres_bit_step_alloc[i],
						 pack_state->buffer);
			}
		} else {
			pack8((uint8_t) 0, pack_state->buffer);
		}
		if (pack_state->details && gres_js->gres_cnt_step_alloc) {
			pack8((uint8_t) 1, pack_state->buffer);
			for (i = 0; i < gres_js->node_cnt; i++) {
				pack64(gres_js->
				       gres_cnt_step_alloc[i],
				       pack_state->buffer);
			}
		} else {
			pack8((uint8_t) 0, pack_state->buffer);
		}
		for (i = 0; i < gres_js->node_cnt; i++) {
			if (!pack_state->details ||
			    !gres_js->gres_per_bit_step_alloc ||
			    !gres_js->gres_per_bit_step_alloc[i] ||
			    !gres_js->gres_bit_step_alloc ||
			    !gres_js->gres_bit_step_alloc[i]) {
				pack8((uint8_t)0, pack_state->buffer);
				continue;
			}
			pack8((uint8_t)1, pack_state->buffer);
			pack64_array(
				gres_js->gres_per_bit_step_alloc[i],
				bit_size(gres_js->gres_bit_step_alloc[i]),
				pack_state->buffer);
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, pack_state->protocol_version);
		return -1;
	}

	return 0;
}

static int _foreach_step_state_pack(void *x, void *arg)
{
	gres_state_t *gres_state_step = x;
	pack_state_t *pack_state = arg;
	gres_step_state_t *gres_ss = gres_state_step->gres_data;
	int i;

	if (pack_state->protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		pack32(pack_state->magic, pack_state->buffer);
		pack32(gres_state_step->plugin_id, pack_state->buffer);
		pack16(gres_ss->cpus_per_gres, pack_state->buffer);
		pack16(gres_ss->flags, pack_state->buffer);
		pack64(gres_ss->gres_per_step, pack_state->buffer);
		pack64(gres_ss->gres_per_node, pack_state->buffer);
		pack64(gres_ss->gres_per_socket, pack_state->buffer);
		pack64(gres_ss->gres_per_task, pack_state->buffer);
		pack64(gres_ss->mem_per_gres, pack_state->buffer);
		pack64(gres_ss->total_gres, pack_state->buffer);
		packstr(gres_ss->type_name, pack_state->buffer);
		pack32(gres_ss->node_cnt, pack_state->buffer);
		pack_bit_str_hex(gres_ss->node_in_use, pack_state->buffer);
		if (gres_ss->gres_cnt_node_alloc) {
			pack8((uint8_t) 1, pack_state->buffer);
			pack64_array(gres_ss->gres_cnt_node_alloc,
				     gres_ss->node_cnt, pack_state->buffer);
		} else {
			pack8((uint8_t) 0, pack_state->buffer);
		}
		if (gres_ss->gres_bit_alloc) {
			pack8((uint8_t) 1, pack_state->buffer);
			for (i = 0; i < gres_ss->node_cnt; i++)
				pack_bit_str_hex(gres_ss->gres_bit_alloc[i],
						 pack_state->buffer);
		} else {
			pack8((uint8_t) 0, pack_state->buffer);
		}
		for (i = 0; i < gres_ss->node_cnt; i++) {
			if (!gres_ss->gres_per_bit_alloc ||
			    !gres_ss->gres_per_bit_alloc[i] ||
			    !gres_ss->gres_bit_alloc ||
			    !gres_ss->gres_bit_alloc[i]) {
				pack8((uint8_t)0, pack_state->buffer);
				continue;
			}
			pack8((uint8_t)1, pack_state->buffer);
			pack64_array(gres_ss->gres_per_bit_alloc[i],
				     bit_size(gres_ss->gres_bit_alloc[i]),
				     pack_state->buffer);
		}
	} else if (pack_state->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(pack_state->magic, pack_state->buffer);
		pack32(gres_state_step->plugin_id, pack_state->buffer);
		pack16(gres_ss->cpus_per_gres, pack_state->buffer);
		pack16(gres_ss->flags, pack_state->buffer);
		pack64(gres_ss->gres_per_step, pack_state->buffer);
		pack64(gres_ss->gres_per_node, pack_state->buffer);
		pack64(gres_ss->gres_per_socket, pack_state->buffer);
		pack64(gres_ss->gres_per_task, pack_state->buffer);
		pack64(gres_ss->mem_per_gres, pack_state->buffer);
		pack64(gres_ss->total_gres, pack_state->buffer);
		pack32(gres_ss->node_cnt, pack_state->buffer);
		pack_bit_str_hex(gres_ss->node_in_use, pack_state->buffer);
		if (gres_ss->gres_cnt_node_alloc) {
			pack8((uint8_t) 1, pack_state->buffer);
			pack64_array(gres_ss->gres_cnt_node_alloc,
				     gres_ss->node_cnt, pack_state->buffer);
		} else {
			pack8((uint8_t) 0, pack_state->buffer);
		}
		if (gres_ss->gres_bit_alloc) {
			pack8((uint8_t) 1, pack_state->buffer);
			for (i = 0; i < gres_ss->node_cnt; i++)
				pack_bit_str_hex(gres_ss->gres_bit_alloc[i],
						 pack_state->buffer);
		} else {
			pack8((uint8_t) 0, pack_state->buffer);
		}
		for (i = 0; i < gres_ss->node_cnt; i++) {
			if (!gres_ss->gres_per_bit_alloc ||
			    !gres_ss->gres_per_bit_alloc[i] ||
			    !gres_ss->gres_bit_alloc ||
			    !gres_ss->gres_bit_alloc[i]) {
				pack8((uint8_t)0, pack_state->buffer);
				continue;
			}
			pack8((uint8_t)1, pack_state->buffer);
			pack64_array(gres_ss->gres_per_bit_alloc[i],
				     bit_size(gres_ss->gres_bit_alloc[i]),
				     pack_state->buffer);
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, pack_state->protocol_version);
		return -1;
	}
	return 0;
}

static int _pack_state(list_t *gres_list, pack_state_t *pack_state,
		       int (*pack_function) (void *x, void *key))
{
	int rc = SLURM_SUCCESS;
	uint32_t top_offset, tail_offset;
	uint16_t rec_cnt = 0;

	top_offset = get_buf_offset(pack_state->buffer);
	pack16(rec_cnt, pack_state->buffer);	/* placeholder if data */

	if (!gres_list)
		return rc;

	rec_cnt = list_for_each(gres_list, pack_function, pack_state);

	if (rec_cnt > 0) {
		tail_offset = get_buf_offset(pack_state->buffer);
		set_buf_offset(pack_state->buffer, top_offset);
		pack16(rec_cnt, pack_state->buffer);
		set_buf_offset(pack_state->buffer, tail_offset);
	}

	return rc;
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
			      list_t **gres_list,
			      bool config_overrides,
			      int cores_per_sock,
			      int sock_per_node)
{
	int i, rc = SLURM_SUCCESS;
	gres_state_t *gres_state_node = NULL, **gres_state_node_array;
	gres_state_t *gpu_gres_state_node = NULL;

	xassert(gres_context_cnt >= 0);
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

extern void gres_node_remove(node_record_t *node_ptr)
{
	if (!node_ptr->gres_list)
		return;

	slurm_mutex_lock(&gres_context_lock);
	for (int i = 0; i < gres_context_cnt; i++) {
		gres_state_t *gres_state_node;

		if (!(gres_state_node =
		      list_find_first(node_ptr->gres_list, gres_find_id,
				      &gres_context[i].plugin_id)))
			continue;

		if (gres_state_node->gres_data) {
			gres_node_state_t *gres_ns = gres_state_node->gres_data;
			gres_context[i].total_cnt -= gres_ns->gres_cnt_config;
		}
	}
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Pack a node's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_node_config_validate()
 * IN/OUT buffer - location to write state to
 */
extern int gres_node_state_pack(list_t *gres_list, buf_t *buffer,
				uint16_t protocol_version)
{
	pack_state_t pack_state = {
		.buffer = buffer,
		.magic = GRES_MAGIC,
		.protocol_version = protocol_version,
	};

	return _pack_state(gres_list, &pack_state, _foreach_node_state_pack);
}

/*
 * Unpack a node's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_node_state_pack()
 * IN/OUT buffer - location to read state from
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_node_state_unpack(list_t **gres_list, buf_t *buffer,
				  char *node_name,
				  uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	uint32_t magic = 0, plugin_id = 0, config_flags = 0;
	uint16_t gres_bitmap_size = 0, rec_cnt = 0;
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns = NULL;
	bool locked = false;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);
	locked = true;
	if ((gres_context_cnt > 0) && (*gres_list == NULL))
		*gres_list = list_create(_gres_node_list_delete);

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		uint32_t tmp_uint32;
		uint32_t full_config_flags = 0;
		slurm_gres_context_t *gres_ctx;
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;

		gres_ns = _build_gres_node_state();

		if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			safe_unpack32(&config_flags, buffer);
			safe_unpack64(&gres_ns->gres_cnt_avail, buffer);
			safe_unpack16(&gres_bitmap_size, buffer);

			safe_unpack16(&gres_ns->topo_cnt, buffer);
			if (gres_ns->topo_cnt) {
				gres_ns->topo_core_bitmap =
					xcalloc(gres_ns->topo_cnt,
						sizeof(bitstr_t *));
				gres_ns->topo_gres_bitmap =
					xcalloc(gres_ns->topo_cnt,
						sizeof(bitstr_t *));
				gres_ns->topo_res_core_bitmap =
					xcalloc(gres_ns->topo_cnt,
						sizeof(bitstr_t *));
				for (int i = 0; i < gres_ns->topo_cnt; i++) {
					unpack_bit_str_hex(
						&gres_ns->topo_core_bitmap[i],
						buffer);
					unpack_bit_str_hex(
						&gres_ns->topo_gres_bitmap[i],
						buffer);
					unpack_bit_str_hex(
						&gres_ns->
						topo_res_core_bitmap[i],
						buffer);
				}
			}
			safe_unpack64_array(&gres_ns->topo_gres_cnt_alloc,
					    &tmp_uint32, buffer);
			safe_unpack64_array(&gres_ns->topo_gres_cnt_avail,
					    &tmp_uint32, buffer);
			safe_unpack32_array(&gres_ns->topo_type_id, &tmp_uint32,
					    buffer);
			safe_unpackstr_array(&gres_ns->topo_type_name,
					     &tmp_uint32, buffer);
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			safe_unpack32(&config_flags, buffer);
			safe_unpack64(&gres_ns->gres_cnt_avail, buffer);
			safe_unpack16(&gres_bitmap_size, buffer);

			safe_unpack16(&gres_ns->topo_cnt, buffer);
			if (gres_ns->topo_cnt) {
				gres_ns->topo_core_bitmap =
					xcalloc(gres_ns->topo_cnt,
						sizeof(bitstr_t *));
				gres_ns->topo_gres_bitmap =
					xcalloc(gres_ns->topo_cnt,
						sizeof(bitstr_t *));
				gres_ns->topo_res_core_bitmap =
					xcalloc(gres_ns->topo_cnt,
						sizeof(bitstr_t *));
				for (int i = 0; i < gres_ns->topo_cnt; i++) {
					unpack_bit_str_hex(
						&gres_ns->topo_core_bitmap[i],
						buffer);
					unpack_bit_str_hex(
						&gres_ns->topo_gres_bitmap[i],
						buffer);
				}
			}
			safe_unpack64_array(&gres_ns->topo_gres_cnt_alloc,
					    &tmp_uint32, buffer);
			safe_unpack64_array(&gres_ns->topo_gres_cnt_avail,
					    &tmp_uint32, buffer);
			safe_unpack32_array(&gres_ns->topo_type_id, &tmp_uint32,
					    buffer);
			safe_unpackstr_array(&gres_ns->topo_type_name,
					     &tmp_uint32, buffer);
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
			_gres_node_state_delete(gres_ns);
			continue;
		}

		if (gres_bitmap_size) {
			gres_ns->gres_bit_alloc =
				bit_alloc(gres_bitmap_size);
		}

		/* We don't want to lose flags from gres_ctx */
		full_config_flags = gres_ctx->config_flags;

		/*
		 * Flag this as flags read from state so we only use them until
		 * the node checks in.
		 */
		gres_ctx->config_flags = config_flags | GRES_CONF_FROM_STATE;

		gres_state_node = gres_create_state(
			gres_ctx, GRES_STATE_SRC_CONTEXT_PTR,
			GRES_STATE_TYPE_NODE, gres_ns);
		list_append(*gres_list, gres_state_node);
		gres_ctx->config_flags |= full_config_flags;
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error from node %s", __func__, node_name);
	_gres_node_state_delete(gres_ns);
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
		new_gres_ns->topo_res_core_bitmap = xcalloc(gres_ns->topo_cnt,
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
			if (gres_ns->topo_res_core_bitmap[i]) {
				new_gres_ns->topo_res_core_bitmap[i] =
					bit_copy(gres_ns->
						 topo_res_core_bitmap[i]);
			}
			if (gres_ns->topo_gres_bitmap[i]) {
				new_gres_ns->topo_gres_bitmap[i] =
					bit_copy(gres_ns->topo_gres_bitmap[i]);
			}
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

static int _foreach_node_state_dup(void *x, void *arg)
{
	gres_state_t *gres_state_node = x, *new_gres;
	list_t *new_list = arg;
	void *gres_ns;

	if (!_find_context_by_id(gres_state_node->plugin_id)) {
		error("Could not find plugin id %u to dup node record",
		      gres_state_node->plugin_id);
		return 0;
	}

	gres_ns = _node_state_dup(gres_state_node->gres_data);
	if (gres_ns) {
		new_gres = gres_create_state(
			gres_state_node, GRES_STATE_SRC_STATE_PTR,
			GRES_STATE_TYPE_NODE, gres_ns);
		/*
		 * Because "gres/'shared'" follows "gres/gpu" (see gres_init)
		 * the sharing gres will be in new list already.
		 */
		if (gres_id_shared(new_gres->config_flags)) {
			/*
			 * gres_id_sharing currently only includes gpus so we
			 * can just search for that.
			 */
			_set_alt_gres(new_gres,
				      list_find_first(new_list, gres_find_id,
						      &gpu_plugin_id));
		}
		list_append(new_list, new_gres);
	}
	return 0;
}

/*
 * Duplicate a node gres status (used for will-run logic)
 * IN gres_list - node gres state information
 * RET a copy of gres_list or NULL on failure
 */
extern list_t *gres_node_state_list_dup(list_t *gres_list)
{
	list_t *new_list = NULL;

	if (gres_list == NULL)
		return new_list;

	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0)) {
		new_list = list_create(_gres_node_list_delete);
		(void) list_for_each(gres_list,
				     _foreach_node_state_dup,
				     new_list);
	}
	slurm_mutex_unlock(&gres_context_lock);

	return new_list;
}

static int _node_state_dealloc(void *x, void *arg)
{
	gres_state_t *gres_state_node = x;
	int i;
	gres_node_state_t *gres_ns;

	gres_ns = (gres_node_state_t *) gres_state_node->gres_data;
	gres_ns->gres_cnt_alloc = 0;
	if (gres_ns->gres_bit_alloc)
		bit_clear_all(gres_ns->gres_bit_alloc);

	if (gres_ns->topo_cnt && !gres_ns->topo_gres_cnt_alloc) {
		error("gres_node_state_dealloc_all: gres/%s topo_cnt!=0 "
		      "and topo_gres_cnt_alloc is NULL",
		      gres_state_node->gres_name);
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

	return 0;
}

/*
 * Deallocate all resources on this node previous allocated to any jobs.
 *	This function isused to synchronize state after slurmctld restarts or
 *	is reconfigured.
 * IN gres_list - node gres state information
 */
extern void gres_node_state_dealloc_all(list_t *gres_list)
{
	if (gres_list == NULL)
		return;

	xassert(gres_context_cnt >= 0);

	(void) list_for_each(gres_list, _node_state_dealloc, NULL);
}

static char *_node_gres_used(gres_node_state_t *gres_ns, char *gres_name)
{
	char *sep = "";
	int i, j;

	xassert(gres_ns);

	if (!gres_ns->gres_cnt_avail) {
		return NULL;
	} else if ((gres_ns->topo_cnt != 0) && (gres_ns->no_consume == false)) {
		bitstr_t *topo_printed = bit_alloc(gres_ns->topo_cnt);
		xfree(gres_ns->gres_used);    /* Free any cached value */
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			/*
			 * For non-shared gres, we record which indices have
			 * gres allocated. For shared gres, we record the count
			 * of allocated gres at each index (may be >1, as
			 * opposed to non-shared gres which is never >1)
			 *
			 * topo_gres_bitmap is used for non-shared gres, while
			 * topo_gres_cnt_alloc_str is used for shared gres
			 * (shard, mps).
			 */
			bitstr_t *topo_gres_bitmap = NULL;
			char *topo_gres_cnt_alloc_str = NULL;

			uint64_t gres_alloc_cnt = 0;
			char *gres_alloc_idx, tmp_str[64];
			bool is_shared;

			if (bit_test(topo_printed, i))
				continue;
			bit_set(topo_printed, i);

			is_shared = gres_is_shared_name(gres_name);
			if (is_shared) {
				uint64_t alloc, avail;
				alloc = gres_ns->topo_gres_cnt_alloc[i];
				avail = gres_ns->topo_gres_cnt_avail[i];
				xstrfmtcat(topo_gres_cnt_alloc_str,
					   "%"PRIu64"/%"PRIu64,
					   alloc, avail);
				gres_alloc_cnt += alloc;
			} else if (gres_ns->topo_gres_bitmap[i]) {
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
				if (is_shared) {
					uint64_t alloc, avail;
					alloc = gres_ns->topo_gres_cnt_alloc[j];
					avail = gres_ns->topo_gres_cnt_avail[j];
					xstrfmtcat(topo_gres_cnt_alloc_str,
						   ",%"PRIu64"/%"PRIu64,
						   alloc, avail);
					gres_alloc_cnt += alloc;
				} else if (gres_ns->topo_gres_bitmap[j]) {
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
			if (!is_shared && gres_ns->gres_bit_alloc &&
			    topo_gres_bitmap &&
			    (bit_size(topo_gres_bitmap) ==
			     bit_size(gres_ns->gres_bit_alloc))) {
				bit_and(topo_gres_bitmap,
					gres_ns->gres_bit_alloc);
				gres_alloc_cnt = bit_set_count(topo_gres_bitmap);
			}
			if (is_shared) {
				gres_alloc_idx = topo_gres_cnt_alloc_str;
			} else if (gres_alloc_cnt > 0) {
				bit_fmt(tmp_str, sizeof(tmp_str),
					topo_gres_bitmap);
				gres_alloc_idx = tmp_str;
			} else {
				gres_alloc_idx = "N/A";
			}
			xstrfmtcat(gres_ns->gres_used,
				   "%s%s:%s:%"PRIu64"(%s%s)", sep, gres_name,
				   gres_ns->topo_type_name[i], gres_alloc_cnt,
				   is_shared ? "" : "IDX:", gres_alloc_idx);
			sep = ",";
			FREE_NULL_BITMAP(topo_gres_bitmap);
			xfree(topo_gres_cnt_alloc_str);
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

static int _foreach_node_state_log(void *x, void *arg)
{
	gres_state_t *gres_state_node = x;
	gres_node_state_t *gres_ns = gres_state_node->gres_data;
	char *gres_name = gres_state_node->gres_name;
	char *node_name = arg;

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

	return 0;
}

/*
 * Log a node's current gres state
 * IN gres_list - generated by gres_node_config_validate()
 * IN node_name - name of the node for which the gres information applies
 */
extern void gres_node_state_log(list_t *gres_list, char *node_name)
{
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES) || !gres_list)
		return;

	xassert(gres_context_cnt >= 0);

	(void) list_for_each(gres_list, _foreach_node_state_log, node_name);
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

extern bool gres_node_state_list_has_alloc_gres(list_t *gres_list)
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
extern char *gres_get_node_drain(list_t *gres_list)
{
	char *node_drain = xstrdup("N/A");

	return node_drain;
}

static int _foreach_get_node_used(void *x, void *arg)
{
	gres_state_t *gres_state_node = x;
	char **gres_usedp = arg;
	char *gres_used = *gres_usedp;
	char *tmp = NULL;

	if (!(tmp = _node_gres_used(gres_state_node->gres_data,
				    gres_state_node->gres_name)))
		return 0;

	if (gres_used)
		xstrcat(gres_used, ",");
	xstrcat(gres_used, tmp);

	*gres_usedp = gres_used;

	return 0;
}

/*
 * Build a string indicating a node's used GRES
 * IN gres_list - generated by gres_node_config_validate()
 * RET - string, must be xfreed by caller
 */
extern char *gres_get_node_used(list_t *gres_list)
{
	char *gres_used = NULL;

	if (gres_list)
		(void) list_for_each(gres_list,
				     _foreach_get_node_used,
				     &gres_used);

	return gres_used;
}

/*
 * Give the total system count of a given GRES
 * Returns NO_VAL64 if name not found
 */
extern uint64_t gres_get_system_cnt(char *name, bool case_insensitive)
{
	uint64_t count = NO_VAL64;
	int i;

	if (!name)
		return NO_VAL64;

	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (case_insensitive ?
		    !xstrcasecmp(gres_context[i].gres_name, name) :
		    !xstrcmp(gres_context[i].gres_name, name)) {
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
extern uint64_t gres_node_config_cnt(list_t *gres_list, char *name)
{
	int i;
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;
	uint64_t count = 0;

	if (!gres_list || !name || !list_count(gres_list))
		return count;

	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (!xstrcasecmp(gres_context[i].gres_name, name)) {
			/* Find or create gres_state entry on the list */
			gres_state_node = list_find_first(
				gres_list, gres_find_id,
				&gres_context[i].plugin_id);

			if (!gres_state_node || !gres_state_node->gres_data)
				break;
			gres_ns = gres_state_node->gres_data;
			count = gres_ns->gres_cnt_config;
			break;
		} else if (!xstrncasecmp(name, gres_context[i].gres_name_colon,
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

extern void gres_job_state_delete(gres_job_state_t *gres_js)
{
	int i;

	if (gres_js == NULL)
		return;

	gres_job_clear_alloc(gres_js);

	if (gres_js->gres_bit_select) {
		for (i = 0; i < gres_js->total_node_cnt; i++)
			FREE_NULL_BITMAP(gres_js->gres_bit_select[i]);
		xfree(gres_js->gres_bit_select);
	}
	if (gres_js->gres_per_bit_select) {
		for (i = 0; i < gres_js->total_node_cnt; i++){
			xfree(gres_js->gres_per_bit_select[i]);
		}
		xfree(gres_js->gres_per_bit_select);
	}

	if (gres_js->res_gpu_cores) {
		for (i = 0; i < gres_js->res_array_size; i++) {
			FREE_NULL_BITMAP(gres_js->res_gpu_cores[i]);
		}
		xfree(gres_js->res_gpu_cores);
	}

	xfree(gres_js->gres_cnt_node_alloc);
	xfree(gres_js->gres_cnt_node_select);
	xfree(gres_js->type_name);
	xfree(gres_js);
}

extern void gres_job_clear_alloc(gres_job_state_t *gres_js)
{
	for (int i = 0; i < gres_js->node_cnt; i++) {
		if (gres_js->gres_bit_alloc)
			FREE_NULL_BITMAP(gres_js->gres_bit_alloc[i]);
		if (gres_js->gres_bit_step_alloc)
			FREE_NULL_BITMAP(gres_js->gres_bit_step_alloc[i]);
		if (gres_js->gres_per_bit_alloc)
			xfree(gres_js->gres_per_bit_alloc[i]);
		if (gres_js->gres_per_bit_step_alloc)
			xfree(gres_js->gres_per_bit_step_alloc[i]);
	}

	xfree(gres_js->gres_bit_alloc);
	xfree(gres_js->gres_bit_step_alloc);
	xfree(gres_js->gres_per_bit_alloc);
	xfree(gres_js->gres_per_bit_step_alloc);
	xfree(gres_js->gres_cnt_step_alloc);
	xfree(gres_js->gres_cnt_node_alloc);
	gres_js->node_cnt = 0;
}

extern void gres_job_list_delete(void *list_element)
{
	gres_state_t *gres_state_job;

	gres_state_job = (gres_state_t *) list_element;
	gres_job_state_delete(gres_state_job->gres_data);
	gres_state_job->gres_data = NULL;
	_gres_state_delete_members(gres_state_job);
}

/*
 * Ensure consistency of gres_per_* options
 * Modify task and node count as needed for consistentcy with GRES options
 * RET -1 on failure, 0 on success
 */
static int _test_gres_cnt(gres_state_t *gres_state_job,
			  gres_job_state_validate_t *gres_js_val)
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
		if (*gres_js_val->sockets_per_node == NO_VAL16) {
			error("--%ss-per-socket option requires --sockets-per-node specification",
			      gres_state_job->gres_name);
			return -1;
		}
	}

	/* make sure --cpu-per-gres is not combined with --cpus-per-task */
	if (!running_in_slurmctld() && gres_js->cpus_per_gres &&
	    (*gres_js_val->cpus_per_task != NO_VAL16)) {
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
		if (((*gres_js_val->min_nodes != NO_VAL) &&
		     (req_nodes < *gres_js_val->min_nodes)) ||
		    (req_nodes > *gres_js_val->max_nodes)) {
			error("Failed to validate job spec. Based on --%s and --gres=%s/--%ss-per-node required nodes (%u) doesn't fall between min_nodes (%u) and max_nodes (%u) boundaries.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name,
			      gres_state_job->gres_name,
			      req_nodes,
			      *gres_js_val->min_nodes,
			      *gres_js_val->max_nodes);
			return -1;
		}
		*gres_js_val->min_nodes = *gres_js_val->max_nodes = req_nodes;
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
		if (*gres_js_val->sockets_per_node == NO_VAL16)
			*gres_js_val->sockets_per_node = req_sockets;
		else if (*gres_js_val->sockets_per_node != req_sockets) {
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
	    (*gres_js_val->num_tasks != NO_VAL)) {
		int tmp = *gres_js_val->num_tasks / gres_js->ntasks_per_gres;
		if ((tmp * gres_js->ntasks_per_gres) !=
		    *gres_js_val->num_tasks) {
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
			if (*gres_js_val->num_tasks == NO_VAL)
				*gres_js_val->num_tasks = req_tasks;
			else if (*gres_js_val->num_tasks != req_tasks) {
				if (running_in_slurmctld()) {
					/* requesting new task count */
					gres_js->total_gres =
						gres_js->gres_per_job =
						*gres_js_val->num_tasks *
						gres_js->gres_per_task;
				} else {
					/*
					 * Anywhere outside of the slurmctld we
					 * are asking for something incorrect.
					 */
					error("Failed to validate job spec. Based on --%ss and --%ss-per-task number of requested tasks differ from -n/--ntasks.",
					      gres_state_job->gres_name,
					      gres_state_job->gres_name);
					return -1;
				}
			}
		} else if (*gres_js_val->num_tasks != NO_VAL) {
			gres_js->gres_per_job = *gres_js_val->num_tasks *
				gres_js->gres_per_task;
		} else if (!xstrcmp(gres_state_job->gres_name, "gpu")) {
			error("Failed to validate job spec. --%ss-per-task or --tres-per-task used without either --%ss or -n/--ntasks is not allowed.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		} else {
			error("Failed to validate job spec. --tres-per-task used without -n/--ntasks is not allowed.");
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
		if ((*gres_js_val->ntasks_per_node == NO_VAL16) ||
		    (*gres_js_val->ntasks_per_node == 0))
			*gres_js_val->ntasks_per_node = req_tasks_per_node;
		else if (*gres_js_val->ntasks_per_node != req_tasks_per_node) {
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
		if ((*gres_js_val->ntasks_per_socket == NO_VAL16) ||
		    (*gres_js_val->ntasks_per_socket == 0))
			*gres_js_val->ntasks_per_socket = req_tasks_per_socket;
		else if (*gres_js_val->ntasks_per_socket !=
			 req_tasks_per_socket) {
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
		req_cpus_per_task = cpus_per_gres * gres_js->gres_per_task;
		if ((*gres_js_val->cpus_per_task == NO_VAL16) ||
		    (*gres_js_val->cpus_per_task == 0))
			*gres_js_val->cpus_per_task = req_cpus_per_task;
		else if (*gres_js_val->cpus_per_task != req_cpus_per_task) {
			error("Failed to validate job spec. Based on --cpus-per-%s and --%ss-per-task requested number of cpus differ from -c/--cpus-per-task.",
			      gres_state_job->gres_name,
			      gres_state_job->gres_name);
			return -1;
		}
	}

	/* Ensure tres_per_job >= node count */
	if (gres_js->gres_per_job) {
		if ((*gres_js_val->min_nodes != NO_VAL) &&
		    (gres_js->gres_per_job < *gres_js_val->min_nodes)) {
			error("Failed to validate job spec, --%ss < -N",
			      gres_state_job->gres_name);
			return -1;
		}
		if ((*gres_js_val->max_nodes != NO_VAL) &&
		    (gres_js->gres_per_job < *gres_js_val->max_nodes)) {
			*gres_js_val->max_nodes = gres_js->gres_per_job;
		}
	}

	return 0;
}

/*
 * Reentrant TRES specification parse logic
 * in_val IN - initial input string
 * type OUT -  must be xfreed by caller
 * cnt OUT - count of values
 * save_ptr IN/OUT - NULL on initial call, otherwise value from previous call
 * RET rc - error code
 */
static int _get_next_gres(char *in_val, char **type_ptr, int *context_inx_ptr,
			  uint64_t *cnt, char **save_ptr)
{
	char *name = NULL, *type = NULL, *tres_type = "gres";
	int i, rc = SLURM_SUCCESS;
	uint64_t value = 0;

	xassert(cnt);
	xassert(save_ptr);

	rc = slurm_get_next_tres(&tres_type, in_val, &name, &type,
				 &value, save_ptr);
	if (name) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (!xstrcmp(name, gres_context[i].gres_name) ||
			    !xstrncmp(name, gres_context[i].gres_name_colon,
				      gres_context[i].gres_name_colon_len))
				break;	/* GRES name match found */
		}
		if (i >= gres_context_cnt) {
			debug("%s: Failed to locate GRES %s", __func__, name);
			rc = ESLURM_INVALID_GRES;
		} else
			*context_inx_ptr = i;
		xfree(name);
	}

	if (rc != SLURM_SUCCESS) {
		*save_ptr = NULL;
		if ((rc == ESLURM_INVALID_TRES) && running_in_slurmctld()) {
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
					list_t *gres_list, char **save_ptr,
					int *rc)
{
	static char *prev_save_ptr = NULL;
	int context_inx = NO_VAL, my_rc = SLURM_SUCCESS;
	gres_job_state_t *gres_js = NULL;
	gres_state_t *gres_state_job = NULL;
	gres_key_t job_search_key;
	char *type = NULL, *name = NULL;

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
				    cnt, &prev_save_ptr)) ||
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
		gres_js->type_id = job_search_key.type_id;
		gres_js->type_name = type;
		type = NULL;	/* String moved above */

		gres_state_job = gres_create_state(
			&gres_context[context_inx], GRES_STATE_SRC_CONTEXT_PTR,
			GRES_STATE_TYPE_JOB, gres_js);
		list_append(gres_list, gres_state_job);
	}

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
 * Setup over_array to mark if we have gres of the same type.
 */
static void _set_over_array(gres_state_t *gres_state,
			   job_validate_t *job_validate)
{
	char *type_name = job_validate->is_job ?
		((gres_job_state_t *) gres_state->gres_data)->type_name:
		((gres_step_state_t *) gres_state->gres_data)->type_name;
	int i;
	overlap_check_t *overlap_check = NULL;

	xassert(job_validate->over_array);

	for (i = 0; i < job_validate->over_count; i++) {
		if (job_validate->over_array[i].plugin_id ==
		    gres_state->plugin_id)
			break;
	}

	/*
	 * Set overlap_check after the loop since when over_count is 0 the loop
	 * won't happen.
	 */
	overlap_check = &job_validate->over_array[i];
	xassert(overlap_check);

	if (i >= job_validate->over_count) {
		job_validate->over_count++;
		overlap_check->plugin_id = gres_state->plugin_id;
		if (type_name) {
			overlap_check->with_type = true;
		} else {
			overlap_check->without_type = true;
			overlap_check->without_type_state =
				gres_state->gres_data;
		}
	} else if (type_name) {
		overlap_check->with_type = true;
		if (overlap_check->without_type)
			job_validate->overlap_merge = true;
	} else {
		overlap_check->without_type = true;
		overlap_check->without_type_state = gres_state->gres_data;
		if (overlap_check->with_type)
			job_validate->overlap_merge = true;
	}

	return;
}

static int _foreach_merge_generic_data(void *x, void *arg)
{
	gres_state_t *gres_state = x;
	merge_generic_t *merge_generic = arg;

	if (merge_generic->plugin_id != gres_state->plugin_id)
		return 0;

	if (merge_generic->generic_gres_data == gres_state->gres_data)
		return 1;

	if (merge_generic->is_job) {
		gres_job_state_t *gres_js_in = merge_generic->generic_gres_data;
		gres_job_state_t *gres_js = gres_state->gres_data;

		if (!gres_js->cpus_per_gres)
			gres_js->cpus_per_gres = gres_js_in->cpus_per_gres;
		if (!gres_js->mem_per_gres)
			gres_js->mem_per_gres = gres_js_in->mem_per_gres;
	} else {
		gres_step_state_t *gres_ss_in =
			merge_generic->generic_gres_data;
		gres_step_state_t *gres_ss = gres_state->gres_data;

		if (!gres_ss->cpus_per_gres)
			gres_ss->cpus_per_gres = gres_ss_in->cpus_per_gres;
		if (!gres_ss->mem_per_gres)
			gres_ss->mem_per_gres = gres_ss_in->mem_per_gres;
	}

	return 0;
}

/*
 * Put generic data (*_per_gres) on other gres of the same kind.
 */
static int _merge_generic_data(
	list_t *gres_list, job_validate_t *job_validate)
{
	int rc = SLURM_SUCCESS;
	merge_generic_t merge_generic = {
		.is_job = job_validate->is_job,
	};

	for (int i = 0; i < job_validate->over_count; i++) {
		overlap_check_t *overlap_check = &job_validate->over_array[i];
		if (!overlap_check->with_type ||
		    !overlap_check->without_type_state)
			continue;
		if (!_generic_state(overlap_check->without_type_state,
				    job_validate->is_job)) {
			rc = ESLURM_INVALID_GRES_TYPE;
			break;
		}

		/* Propagate generic parameters */
		merge_generic.generic_gres_data =
			overlap_check->without_type_state;
		merge_generic.plugin_id = overlap_check->plugin_id;

		(void) list_delete_all(gres_list,
				       _foreach_merge_generic_data,
				       &merge_generic);
	}

	return rc;
}

static int _foreach_set_over_array(void *x, void *arg)
{
	_set_over_array(x, arg);

	return 0;
}

static int _foreach_job_state_validate(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	job_validate_t *job_validate = arg;

	if (_test_gres_cnt(gres_state_job, job_validate->gres_js_val) != 0) {
		job_validate->rc = ESLURM_INVALID_GRES;
		return -1;
	}
	if (!job_validate->have_gres_sharing &&
	    gres_id_sharing(gres_state_job->plugin_id))
		job_validate->have_gres_sharing = true;
	if (gres_id_shared(gres_state_job->config_flags)) {
		job_validate->have_gres_shared = true;
	}
	if (job_validate->have_gres_sharing && job_validate->have_gres_shared) {
		job_validate->rc = ESLURM_INVALID_GRES;
		return -1;
	}

	if (job_validate->cpus_per_gres &&
	    (gres_state_job->plugin_id == gres_get_gpu_plugin_id()))
		job_validate->tmp_min_cpus +=
			job_validate->cpus_per_gres * gres_js->total_gres;

	(void) _foreach_set_over_array(gres_state_job, job_validate);

	return 0;
}

extern int gres_job_state_validate(gres_job_state_validate_t *gres_js_val)
{
	int rc = SLURM_SUCCESS, size;
	bool requested_gpu = false;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
	uint64_t cnt = 0;
	char *cpus_per_tres;
	char *mem_per_tres;
	char *tres_freq;
	char *tres_per_job;
	char *tres_per_node;
	char *tres_per_socket;
	char *tres_per_task;
	job_validate_t job_validate = {
		.gres_js_val = gres_js_val,
		.is_job = true,
		.rc = SLURM_SUCCESS,
	};

	xassert(gres_js_val);
	xassert(gres_js_val->gres_list);
	xassert(!*gres_js_val->gres_list);

	cpus_per_tres = gres_js_val->cpus_per_tres;
	mem_per_tres = gres_js_val->mem_per_tres;
	tres_freq = gres_js_val->tres_freq;
	tres_per_job = gres_js_val->tres_per_job;
	tres_per_node = gres_js_val->tres_per_node;
	tres_per_socket = gres_js_val->tres_per_socket;
	tres_per_task = gres_js_val->tres_per_task;

	if (tres_per_task && running_in_slurmctld() && !running_cons_tres()) {
		char *tmp = xstrdup(tres_per_task);
		/*
		 * Check if cpus_per_task is the only part of tres_per_task. If
		 * so, continue with validation. If not, then the request is
		 * invalid: reject the request.
		 */
		slurm_option_update_tres_per_task(0, "cpu", &tmp);
		if (tmp) {
			xfree(tmp);
			return ESLURM_UNSUPPORTED_GRES;
		}
	}

	if (running_in_slurmctld() && !running_cons_tres() &&
	    (cpus_per_tres || tres_per_job || tres_per_socket || mem_per_tres))
		return ESLURM_UNSUPPORTED_GRES;

	if (!cpus_per_tres && !tres_per_job && !tres_per_node &&
	    !tres_per_socket && !tres_per_task && !mem_per_tres &&
	    !gres_js_val->ntasks_per_tres)
		return SLURM_SUCCESS;

	if ((tres_per_task || (*gres_js_val->ntasks_per_tres != NO_VAL16)) &&
	    (*gres_js_val->num_tasks == NO_VAL) &&
	    (*gres_js_val->min_nodes != NO_VAL) &&
	    (*gres_js_val->min_nodes == *gres_js_val->max_nodes)) {
		/* Implicitly set task count */
		if (*gres_js_val->ntasks_per_tres != NO_VAL16)
			*gres_js_val->num_tasks = *gres_js_val->min_nodes *
				*gres_js_val->ntasks_per_tres;
		else if (*gres_js_val->ntasks_per_node != NO_VAL16)
			*gres_js_val->num_tasks = *gres_js_val->min_nodes *
				*gres_js_val->ntasks_per_node;
		else if (*gres_js_val->cpus_per_task == NO_VAL16)
			*gres_js_val->num_tasks = *gres_js_val->min_nodes;
	}

	xassert(gres_context_cnt >= 0);

	/*
	 * Set new values as requested
	 */
	*gres_js_val->gres_list = list_create(gres_job_list_delete);

	slurm_mutex_lock(&gres_context_lock);
	if (cpus_per_tres) {
		char *in_val = cpus_per_tres, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_js_val->
							    gres_list,
							    &save_ptr, &rc))) {
			gres_js = gres_state_job->gres_data;
			gres_js->cpus_per_gres = cnt;
			in_val = NULL;
			gres_js->ntasks_per_gres =
				*gres_js_val->ntasks_per_tres;

			/*
			 * In theory MAX(cpus_per_gres) shouldn't matter because
			 * we should only allow one gres name to have
			 * cpus_per_gres and it should be the same for all types
			 * (e.g., gpu:k80 vs gpu:tesla) of that same gres (gpu)
			 */
			job_validate.cpus_per_gres =
				MAX(job_validate.cpus_per_gres, cnt);
		}
	}
	if (tres_per_job) {
		char *in_val = tres_per_job, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_js_val->
							    gres_list,
							    &save_ptr, &rc))) {
			if (!requested_gpu &&
			    (!xstrcmp(gres_state_job->gres_name, "gpu")))
				requested_gpu = true;
			gres_js = gres_state_job->gres_data;
			gres_js->gres_per_job = cnt;
			in_val = NULL;
			gres_js->total_gres =
				MAX(gres_js->total_gres, cnt);
			gres_js->ntasks_per_gres =
				*gres_js_val->ntasks_per_tres;
		}
	}
	if (tres_per_node) {
		char *in_val = tres_per_node, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_js_val->
							    gres_list,
							    &save_ptr, &rc))) {
			if (!requested_gpu &&
			    (!xstrcmp(gres_state_job->gres_name, "gpu")))
				requested_gpu = true;
			gres_js = gres_state_job->gres_data;
			gres_js->gres_per_node = cnt;
			in_val = NULL;
			if (*gres_js_val->min_nodes != NO_VAL)
				cnt *= *gres_js_val->min_nodes;
			gres_js->total_gres =
				MAX(gres_js->total_gres, cnt);
			gres_js->ntasks_per_gres =
				*gres_js_val->ntasks_per_tres;
		}
	}
	if (tres_per_socket) {
		char *in_val = tres_per_socket, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_js_val->
							    gres_list,
							    &save_ptr, &rc))) {
			if (!requested_gpu &&
			    (!xstrcmp(gres_state_job->gres_name, "gpu")))
				requested_gpu = true;
			gres_js = gres_state_job->gres_data;
			gres_js->gres_per_socket = cnt;
			in_val = NULL;
			if ((*gres_js_val->min_nodes != NO_VAL) &&
			    (*gres_js_val->sockets_per_node != NO_VAL16)) {
				cnt *= (*gres_js_val->min_nodes *
					*gres_js_val->sockets_per_node);
			} else if ((*gres_js_val->num_tasks != NO_VAL) &&
				   (*gres_js_val->ntasks_per_socket !=
				    NO_VAL16)) {
				cnt *= ROUNDUP(*gres_js_val->num_tasks,
					       *gres_js_val->ntasks_per_socket);
			} else if (*gres_js_val->sockets_per_node != NO_VAL16) {
				/* default 1 node */
				cnt *= *gres_js_val->sockets_per_node;
			}
			gres_js->total_gres =
				MAX(gres_js->total_gres, cnt);
			gres_js->ntasks_per_gres =
				*gres_js_val->ntasks_per_tres;
		}
	}
	if (tres_per_task) {
		char *in_val = tres_per_task, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_js_val->
							    gres_list,
							    &save_ptr, &rc))) {
			if (!requested_gpu &&
			    (!xstrcmp(gres_state_job->gres_name, "gpu")))
				requested_gpu = true;
			gres_js = gres_state_job->gres_data;
			gres_js->gres_per_task = cnt;
			in_val = NULL;
			if (*gres_js_val->num_tasks != NO_VAL)
				cnt *= *gres_js_val->num_tasks;
			gres_js->total_gres =
				MAX(gres_js->total_gres, cnt);
			gres_js->ntasks_per_gres =
				*gres_js_val->ntasks_per_tres;
		}
	}
	if (mem_per_tres) {
		char *in_val = mem_per_tres, *save_ptr = NULL;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_js_val->
							    gres_list,
							    &save_ptr, &rc))) {
			gres_js = gres_state_job->gres_data;
			gres_js->mem_per_gres = cnt;
			in_val = NULL;
			gres_js->ntasks_per_gres =
				*gres_js_val->ntasks_per_tres;
		}
	}

	/*
	 * *gres_js_val->num_tasks and *gres_js_val->ntasks_per_tres could be 0
	 * on requeue
	 */
	if (!gres_js_val->ntasks_per_tres ||
	    !*gres_js_val->ntasks_per_tres ||
	    (*gres_js_val->ntasks_per_tres == NO_VAL16)) {
		/* do nothing */
	} else if (requested_gpu && list_count(*gres_js_val->gres_list)) {
		/* Set num_tasks = gpus * ntasks/gpu */
		uint64_t gpus = _get_job_gres_list_cnt(
			*gres_js_val->gres_list, "gpu", NULL);
		if (gpus != NO_VAL64)
			*gres_js_val->num_tasks =
				gpus * *gres_js_val->ntasks_per_tres;
		else {
			error("%s: Can't set num_tasks = gpus * *ntasks_per_tres because there are no allocated GPUs",
			      __func__);
			rc = ESLURM_INVALID_GRES;
		}
	} else if (*gres_js_val->num_tasks &&
		   (*gres_js_val->num_tasks != NO_VAL)) {
		/*
		 * If job_gres_list empty, and ntasks_per_tres is specified,
		 * then derive GPUs according to how many tasks there are.
		 * GPU GRES = [ntasks / (ntasks_per_tres)]
		 * For now, only generate type-less GPUs.
		 */
		uint32_t gpus = *gres_js_val->num_tasks /
			*gres_js_val->ntasks_per_tres;
		char *save_ptr = NULL, *gres = NULL, *in_val;
		xstrfmtcat(gres, "gres/gpu:%u", gpus);
		in_val = gres;
		while ((gres_state_job = _get_next_job_gres(in_val, &cnt,
							    *gres_js_val->
							    gres_list,
							    &save_ptr, &rc))) {
			gres_js = gres_state_job->gres_data;
			gres_js->ntasks_per_gres =
				*gres_js_val->ntasks_per_tres;
			/* Simulate a tres_per_job specification */
			gres_js->gres_per_job = cnt;
			gres_js->total_gres =
				MAX(gres_js->total_gres, cnt);
			in_val = NULL;
		}
		if (list_count(*gres_js_val->gres_list) == 0)
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
	size = list_count(*gres_js_val->gres_list);
	if (size == 0) {
		FREE_NULL_LIST(*gres_js_val->gres_list);
		return rc;
	}

	/*
	 * If someone requested [mem|cpus]_per_tres but didn't request any GPUs
	 * (even if --exclusive was used), then error. For now we only test for
	 * GPUs since --[mem|cpus]-per-gpu are the only allowed
	 * [mem|cpus]_per_gres options. Even though --exclusive means that you
	 * will be allocated all of the GRES on the node, we still require that
	 * GPUs are explicitly requested when --[mem|cpus]-per-gpu is used.
	 */
	if (mem_per_tres && (!requested_gpu)) {
		error("Requested mem_per_tres=%s but did not request any GPU.",
		      mem_per_tres);
		return ESLURM_INVALID_GRES;
	}
	if (cpus_per_tres && (!requested_gpu)) {
		error("Requested cpus_per_tres=%s but did not request any GPU.",
		      cpus_per_tres);
		return ESLURM_INVALID_GRES;
	}

	/*
	 * Check for record overlap (e.g. "gpu:2,gpu:tesla:1")
	 * Ensure tres_per_job >= tres_per_node >= tres_per_socket
	 */
	job_validate.over_array = xcalloc(size, sizeof(overlap_check_t));

	(void) list_for_each(*gres_js_val->gres_list,
			     _foreach_job_state_validate,
			     &job_validate);

	if (job_validate.tmp_min_cpus > *gres_js_val->min_cpus)
		*gres_js_val->min_cpus = job_validate.tmp_min_cpus;

	if (((*gres_js_val->cpus_per_task) != NO_VAL16) &&
	    ((*gres_js_val->num_tasks) != NO_VAL)) {
		cnt = (*gres_js_val->cpus_per_task) * (*gres_js_val->num_tasks);
		if (*gres_js_val->min_cpus < cnt)
			*gres_js_val->min_cpus = cnt;
	}

	if (job_validate.have_gres_shared &&
	    (job_validate.rc == SLURM_SUCCESS) &&
	    tres_freq &&
	    strstr(tres_freq, "gpu")) {
		job_validate.rc = ESLURM_INVALID_GRES;
	}

	if (job_validate.overlap_merge) /* Merge generic data if possible */
		job_validate.rc = _merge_generic_data(*gres_js_val->gres_list,
						      &job_validate);

	xfree(job_validate.over_array);

	return job_validate.rc;
}

static int _find_gres_per_jst(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	gres_job_state_t *gres_js = gres_state_job->gres_data;

	if (gres_js->gres_per_job ||
	    gres_js->gres_per_socket ||
	    gres_js->gres_per_task)
		return 1;

	return 0;
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
extern int gres_job_revalidate(list_t *gres_list)
{
	if (!gres_list || running_cons_tres())
		return SLURM_SUCCESS;

	if (list_find_first(gres_list, _find_gres_per_jst, NULL))
		return ESLURM_UNSUPPORTED_GRES;

	return SLURM_SUCCESS;
}

/*
 * Return TRUE if any of this job's GRES has a populated gres_bit_alloc element.
 * This indicates the allocated GRES has a File configuration parameter and is
 * tracking individual file assignments.
 */
static int _find_job_has_gres_bits(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	gres_job_state_t *gres_js = gres_state_job->gres_data;;

	for (int i = 0; i < gres_js->node_cnt; i++) {
		if (gres_js->gres_bit_alloc && gres_js->gres_bit_alloc[i])
			return 1;
	}

	return 0;
}


static int _find_invalid_job_gres_on_node(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	validate_job_gres_cnt_t *validate_job_gres_cnt = arg;
	gres_state_t *gres_state_node;
	uint32_t plugin_id;
	int job_gres_cnt, node_gres_cnt = 0;

	if (!gres_js ||
	    !gres_js->gres_bit_alloc ||
	    (gres_js->node_cnt <= validate_job_gres_cnt->node_inx) ||
	    !gres_js->gres_bit_alloc[validate_job_gres_cnt->node_inx])
		return 0;

	job_gres_cnt = bit_size(
		gres_js->gres_bit_alloc[validate_job_gres_cnt->node_inx]);

	if (gres_id_shared(gres_state_job->config_flags))
		plugin_id = gpu_plugin_id;
	else
		plugin_id = gres_state_job->plugin_id;

	if ((gres_state_node = list_find_first(validate_job_gres_cnt->
					       node_gres_list,
					       gres_find_id,
					       &plugin_id))) {
		gres_node_state_t *gres_ns = gres_state_node->gres_data;
		node_gres_cnt = (int) gres_ns->gres_cnt_config;
		if (gres_js->type_id) {
			bool found_type = false;
			gres_node_state_t *gres_ns = gres_state_node->gres_data;

			for (int i = 0; i < gres_ns->type_cnt; i++) {
				if (gres_ns->type_id[i] == gres_js->type_id) {
					found_type = true;
					break;
				}
			}
			if (!found_type) {
				error("%s: Killing job %u: gres/%s type %s not found on node %s",
				      __func__,
				      validate_job_gres_cnt->job_id,
				      gres_state_job->gres_name,
				      gres_js->type_name,
				      validate_job_gres_cnt->node_name);
				return 1;
			}
		}
	}

	if (job_gres_cnt != node_gres_cnt) {
		error("%s: Killing job %u: gres/%s count mismatch on node "
		      "%s (%d != %d)",
		      __func__, validate_job_gres_cnt->job_id,
		      gres_state_job->gres_name,
		      validate_job_gres_cnt-> node_name,
		      job_gres_cnt, node_gres_cnt);
		return 1;
	}

	return 0;
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
extern int gres_job_revalidate2(uint32_t job_id, list_t *job_gres_list,
				bitstr_t *node_bitmap)
{
	node_record_t *node_ptr;
	int rc = SLURM_SUCCESS;
	validate_job_gres_cnt_t validate_job_gres_cnt = {
		.job_id = job_id,
		.node_inx = -1,
	};

	if (!job_gres_list || !node_bitmap ||
	    !list_find_first(job_gres_list, _find_job_has_gres_bits, NULL))
		return SLURM_SUCCESS;

	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		/* If no node_ptr->gres_list we are invalid */
		if (!node_ptr->gres_list)
			return ESLURM_INVALID_GRES;

		validate_job_gres_cnt.node_inx++;
		validate_job_gres_cnt.node_gres_list = node_ptr->gres_list;
		validate_job_gres_cnt.node_name = node_ptr->name;

		if (list_find_first(job_gres_list,
				    _find_invalid_job_gres_on_node,
				    &validate_job_gres_cnt))
			return ESLURM_INVALID_GRES;
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
extern list_t *gres_job_state_list_dup(list_t *gres_list)
{
	return gres_job_state_extract(gres_list, -1);
}

static gres_job_state_t *_job_state_dup_common(gres_job_state_t *gres_js)
{
	gres_job_state_t *new_gres_js = xmalloc(sizeof(gres_job_state_t));

	new_gres_js->cpus_per_gres = gres_js->cpus_per_gres;
	new_gres_js->def_cpus_per_gres = gres_js->def_cpus_per_gres;
	new_gres_js->def_mem_per_gres = gres_js->def_mem_per_gres;
	new_gres_js->flags = gres_js->flags;
	new_gres_js->gres_per_job = gres_js->gres_per_job;
	new_gres_js->gres_per_node = gres_js->gres_per_node;
	new_gres_js->gres_per_socket = gres_js->gres_per_socket;
	new_gres_js->gres_per_task = gres_js->gres_per_task;
	new_gres_js->mem_per_gres = gres_js->mem_per_gres;
	new_gres_js->ntasks_per_gres = gres_js->ntasks_per_gres;
	new_gres_js->node_cnt = gres_js->node_cnt;
	new_gres_js->res_array_size = gres_js->res_array_size;
	new_gres_js->total_gres	= gres_js->total_gres;
	new_gres_js->total_node_cnt = gres_js->total_node_cnt;
	new_gres_js->type_id = gres_js->type_id;
	new_gres_js->type_name = xstrdup(gres_js->type_name);

	return new_gres_js;
}

/* Copy gres_job_state_t record for ALL nodes */
extern void *gres_job_state_dup(gres_job_state_t *gres_js)
{

	int i;
	gres_job_state_t *new_gres_js;

	if (gres_js == NULL)
		return NULL;

	new_gres_js = _job_state_dup_common(gres_js);

	if (gres_js->gres_cnt_node_alloc) {
		i = sizeof(uint64_t) * gres_js->node_cnt;
		new_gres_js->gres_cnt_node_alloc = xmalloc(i);
		memcpy(new_gres_js->gres_cnt_node_alloc,
		       gres_js->gres_cnt_node_alloc, i);
	}
	if (gres_js->gres_cnt_step_alloc) {
		new_gres_js->gres_cnt_step_alloc = xcalloc(
			gres_js->node_cnt,
			sizeof(*new_gres_js->gres_cnt_step_alloc));
		memcpy(new_gres_js->gres_cnt_step_alloc,
		       gres_js->gres_cnt_step_alloc,
		       (sizeof(*new_gres_js->gres_cnt_step_alloc) *
			gres_js->node_cnt));
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
	if (gres_js->gres_per_bit_alloc && gres_js->gres_bit_alloc) {
		new_gres_js->gres_per_bit_alloc = xcalloc(gres_js->node_cnt,
							  sizeof(uint64_t *));
		for (i = 0; i < gres_js->node_cnt; i++) {
			int bit_cnt = bit_size(gres_js->gres_bit_alloc[i]);
			new_gres_js->gres_per_bit_alloc[i] = xcalloc(
				bit_cnt, sizeof(uint64_t));
			memcpy(new_gres_js->gres_per_bit_alloc[i],
			       gres_js->gres_per_bit_alloc[i], bit_cnt);
		}
	}
	if (gres_js->gres_bit_step_alloc) {
		new_gres_js->gres_bit_step_alloc = xcalloc(gres_js->node_cnt,
							   sizeof(bitstr_t *));
		for (i = 0; i < gres_js->node_cnt; i++) {
			if (!gres_js->gres_bit_step_alloc[i])
				continue;
			new_gres_js->gres_bit_step_alloc[i] =
				bit_copy(gres_js->gres_bit_step_alloc[i]);
		}
	}
	if (gres_js->gres_per_bit_step_alloc && gres_js->gres_bit_alloc) {
		new_gres_js->gres_per_bit_step_alloc = xcalloc(
			gres_js->node_cnt, sizeof(uint64_t *));
		for (i = 0; i < gres_js->node_cnt; i++) {
			int bit_cnt = bit_size(gres_js->gres_bit_alloc[i]);
			new_gres_js->gres_per_bit_step_alloc[i] = xcalloc(
				bit_cnt, sizeof(uint64_t));
			memcpy(new_gres_js->gres_per_bit_step_alloc[i],
			       gres_js->gres_per_bit_step_alloc[i],
			       bit_cnt * sizeof(uint64_t));
		}
	}
	if (gres_js->gres_cnt_node_select) {
		i = sizeof(uint64_t) * gres_js->total_node_cnt;
		new_gres_js->gres_cnt_node_select = xmalloc(i);
		memcpy(new_gres_js->gres_cnt_node_select,
		       gres_js->gres_cnt_node_select, i);
	}
	if (gres_js->gres_bit_select) {
		new_gres_js->gres_bit_select = xcalloc(gres_js->total_node_cnt,
						       sizeof(bitstr_t *));
		for (i = 0; i < gres_js->total_node_cnt; i++) {
			if (gres_js->gres_bit_select[i] == NULL)
				continue;
			new_gres_js->gres_bit_select[i] =
				bit_copy(gres_js->gres_bit_select[i]);
		}
	}
	if (gres_js->gres_per_bit_select && gres_js->gres_bit_select) {
		new_gres_js->gres_per_bit_select =
			xcalloc(gres_js->total_node_cnt, sizeof(uint64_t *));
		for (i = 0; i < gres_js->total_node_cnt; i++) {
			int bit_cnt;

			if (!gres_js->gres_bit_select[i])
				continue;

			bit_cnt = bit_size(gres_js->gres_bit_select[i]);
			new_gres_js->gres_per_bit_select[i] = xcalloc(
				bit_cnt, sizeof(uint64_t));
			memcpy(new_gres_js->gres_per_bit_select[i],
			       gres_js->gres_per_bit_select[i], bit_cnt);
		}
	}

	if (gres_js->res_gpu_cores) {
		new_gres_js->res_gpu_cores = xcalloc(gres_js->res_array_size,
						     sizeof(bitstr_t *));
		for (i = 0; i < gres_js->res_array_size; i++) {
			if (gres_js->res_gpu_cores[i] == NULL)
				continue;
			new_gres_js->res_gpu_cores[i] =
				bit_copy(gres_js->res_gpu_cores[i]);
		}
	}

	return new_gres_js;
}

/* Copy gres_job_state_t record for one specific node (stepd) */
static void *_job_state_dup2(gres_job_state_t *gres_js, int job_node_index)
{
	gres_job_state_t *new_gres_js;

	if (gres_js == NULL)
		return NULL;

	new_gres_js = _job_state_dup_common(gres_js);
	new_gres_js->total_node_cnt = 1;
	new_gres_js->node_cnt		= 1;

	if (gres_js->gres_cnt_node_alloc) {
		new_gres_js->gres_cnt_node_alloc = xmalloc(sizeof(uint64_t));
		new_gres_js->gres_cnt_node_alloc[0] =
			gres_js->gres_cnt_node_alloc[job_node_index];
	}
	if (gres_js->gres_bit_alloc &&
	    gres_js->gres_bit_alloc[job_node_index]) {
		new_gres_js->gres_bit_alloc	= xmalloc(sizeof(bitstr_t *));
		new_gres_js->gres_bit_alloc[0] =
			bit_copy(gres_js->gres_bit_alloc[job_node_index]);
	}
	if (gres_js->gres_per_bit_alloc &&
	    gres_js->gres_bit_alloc &&
	    gres_js->gres_bit_alloc[job_node_index]) {
		new_gres_js->gres_per_bit_alloc = xmalloc(sizeof(uint64_t *));
		new_gres_js->gres_per_bit_alloc[0] = xcalloc(
			bit_size(gres_js->gres_bit_alloc[job_node_index]),
			sizeof(uint64_t));
		memcpy(new_gres_js->gres_per_bit_alloc[0],
		       gres_js->gres_per_bit_alloc[job_node_index],
		       bit_size(gres_js->gres_bit_alloc[job_node_index]) *
		       sizeof(uint64_t));
	}

	/*
	 * No reason to do
	 *
	 * gres_js->gres_cnt_node_select
	 * gres_js->gres_bit_select
	 *
	 * they are based off the entire cluster this is not needed for the
	 * stepd.
	 */

	return new_gres_js;
}

static int _foreach_job_state_extract(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	job_state_extract_t *job_state_extract = arg;
	gres_state_t *new_gres_state;
	void *new_gres_data;

	if (job_state_extract->job_node_index == -1)
		new_gres_data = gres_job_state_dup(
			gres_state_job->gres_data);
	else
		new_gres_data = _job_state_dup2(
			gres_state_job->gres_data,
			job_state_extract->job_node_index);

	if (!new_gres_data)
		return -1;

	if (!job_state_extract->new_list)
		job_state_extract->new_list = list_create(gres_job_list_delete);

	new_gres_state = gres_create_state(
		gres_state_job, GRES_STATE_SRC_STATE_PTR,
		GRES_STATE_TYPE_JOB, new_gres_data);
	list_append(job_state_extract->new_list, new_gres_state);

	return 0;
}

/*
 * Create a (partial) copy of a job's gres state for a particular node index
 * IN gres_list - List of Gres records for this job to track usage
 * IN job_node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
extern list_t *gres_job_state_extract(list_t *gres_list, int job_node_index)
{
	job_state_extract_t job_state_extract = {
		.job_node_index = job_node_index,
	};

	if (gres_list)
		(void) list_for_each(gres_list,
				     _foreach_job_state_extract,
				     &job_state_extract);

	return job_state_extract.new_list;
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
extern int gres_job_state_pack(list_t *gres_list, buf_t *buffer,
			       uint32_t job_id, bool details,
			       uint16_t protocol_version)
{
	pack_state_t pack_state = {
		.buffer = buffer,
		.details = details,
		.magic = GRES_MAGIC,
		.protocol_version = protocol_version,
	};

	return _pack_state(gres_list, &pack_state, _foreach_job_state_pack);
}

/*
 * Unpack a job's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_job_state_pack()
 * IN/OUT buffer - location to read state from
 * IN job_id - job's ID
 */
extern int gres_job_state_unpack(list_t **gres_list, buf_t *buffer,
				 uint32_t job_id,
				 uint16_t protocol_version)
{
	int i = 0, rc = SLURM_SUCCESS;
	uint32_t magic = 0, plugin_id = 0, utmp32 = 0;
	uint16_t rec_cnt = 0;
	uint8_t  has_more = 0;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js = NULL;
	bool locked = false;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	xassert(gres_context_cnt >= 0);

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
			safe_unpackstr(&gres_js->type_name, buffer);
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
			for (i = 0; i < gres_js->node_cnt; i++) {
				safe_unpack8(&has_more, buffer);
				if (!has_more)
					continue;
				if (!gres_js->gres_per_bit_alloc)
					safe_xcalloc(
						gres_js->gres_per_bit_alloc,
						gres_js->node_cnt,
						sizeof(uint64_t *));
				safe_unpack64_array(
					&gres_js->gres_per_bit_alloc[i],
					&utmp32, buffer);
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
			for (i = 0; i < gres_js->node_cnt; i++) {
				safe_unpack8(&has_more, buffer);
				if (!has_more)
					continue;
				if (!gres_js->gres_per_bit_step_alloc)
					safe_xcalloc(
						gres_js->gres_per_bit_step_alloc,
						gres_js->node_cnt,
						sizeof(uint64_t *));
				safe_unpack64_array(
					&gres_js->gres_per_bit_step_alloc[i],
					&utmp32, buffer);
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
			gres_job_state_delete(gres_js);
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
		gres_job_state_delete(gres_js);
	if (locked)
		slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

extern void gres_prep_pack(void *in, uint16_t protocol_version, buf_t *buffer)
{
	uint32_t magic = GRES_MAGIC;
	gres_prep_t *gres_prep = in;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(magic, buffer);
		pack32(gres_prep->plugin_id, buffer);
		pack32(gres_prep->node_cnt, buffer);
		if (gres_prep->gres_cnt_node_alloc) {
			pack8((uint8_t) 1, buffer);
			pack64_array(gres_prep->gres_cnt_node_alloc,
				     gres_prep->node_cnt, buffer);
		} else {
			pack8((uint8_t) 0, buffer);
		}
		if (gres_prep->gres_bit_alloc) {
			pack8((uint8_t) 1, buffer);
			for (int i = 0; i < gres_prep->node_cnt; i++) {
				pack_bit_str_hex(gres_prep->
						 gres_bit_alloc[i],
						 buffer);
			}
		} else {
			pack8((uint8_t) 0, buffer);
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

/*
 * Pack a job's allocated gres information for use by prolog/epilog
 * IN gres_list - generated by gres_job_config_validate()
 * IN/OUT buffer - location to write state to
 *
 * When 24.11 is no longer supported this can be removed.
 */
extern int gres_prep_pack_legacy(list_t *gres_list, buf_t *buffer,
				 uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	uint32_t top_offset, tail_offset;
	uint16_t rec_cnt = 0;
	list_itr_t *gres_iter;
	gres_prep_t *gres_prep;

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	if (gres_list == NULL)
		return rc;

	if (protocol_version < SLURM_MIN_PROTOCOL_VERSION) {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		return rc;
	}

	gres_iter = list_iterator_create(gres_list);
	while ((gres_prep = list_next(gres_iter))) {
		gres_prep_pack(gres_prep, protocol_version, buffer);
		rec_cnt++;
	}
	list_iterator_destroy(gres_iter);

	tail_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, top_offset);
	pack16(rec_cnt, buffer);
	set_buf_offset(buffer, tail_offset);

	return rc;
}

static void _prep_list_del(void *x)
{
	gres_prep_t *gres_prep = (gres_prep_t *) x;
	int i;

	if (!gres_prep)
		return;

	if (gres_prep->gres_bit_alloc) {
		for (i = 0; i < gres_prep->node_cnt; i++)
			FREE_NULL_BITMAP(gres_prep->gres_bit_alloc[i]);
		xfree(gres_prep->gres_bit_alloc);
	}
	xfree(gres_prep->gres_cnt_node_alloc);
	xfree(gres_prep->node_list);
	xfree(gres_prep);
}

static int _gres_prep_unpack(void **object, uint16_t protocol_version,
			     buf_t *buffer)
{
	uint32_t magic = 0, utmp32 = 0;
	uint8_t filled = 0;
	gres_prep_t *gres_prep = NULL;

	gres_prep = xmalloc(sizeof(gres_prep_t));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&magic, buffer);
		if (magic != GRES_MAGIC)
			goto unpack_error;
		safe_unpack32(&gres_prep->plugin_id, buffer);
		safe_unpack32(&gres_prep->node_cnt, buffer);
		if (gres_prep->node_cnt > NO_VAL)
			goto unpack_error;
		safe_unpack8(&filled, buffer);
		if (filled) {
			safe_unpack64_array(
				&gres_prep->gres_cnt_node_alloc,
				&utmp32, buffer);
		}
		safe_unpack8(&filled, buffer);
		if (filled) {
			safe_xcalloc(gres_prep->gres_bit_alloc,
				     gres_prep->node_cnt,
				     sizeof(bitstr_t *));
			for (int i = 0; i < gres_prep->node_cnt; i++) {
				unpack_bit_str_hex(&gres_prep->
						   gres_bit_alloc[i],
						   buffer);
			}
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	if (!_find_context_by_id(gres_prep->plugin_id)) {
		/*
		 * A likely sign that GresPlugins has changed.
		 * Not a fatal error, skip over the data.
		 */
		error("%s: no plugin configured to unpack data type %u",
		      __func__, gres_prep->plugin_id);
		_prep_list_del(gres_prep);
		gres_prep = NULL;
		/* Don't return SLURM_ERROR */
	}

	*object = gres_prep;

	return SLURM_SUCCESS;

unpack_error:
	error("%s: unpack error", __func__);
	_prep_list_del(gres_prep);

	return SLURM_ERROR;
}

extern int gres_prep_unpack_list(list_t **out, buf_t *buffer,
				 uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;

	/* We have to have gres_context_lock locked to call the unpack */
	slurm_mutex_lock(&gres_context_lock);
	if ((rc = slurm_unpack_list(out, _gres_prep_unpack, _prep_list_del,
				    buffer, protocol_version)) != SLURM_SUCCESS)
		FREE_NULL_LIST(*out);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Unpack a job's allocated gres information for use by prolog/epilog
 * OUT gres_list - restored state stored by gres_prep_pack()
 * IN/OUT buffer - location to read state from
 *
 * When 24.11 is no longer supported this can be removed.
 */
extern int gres_prep_unpack_legacy(list_t **gres_list, buf_t *buffer,
				   uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	uint16_t rec_cnt = 0;
	gres_prep_t *gres_prep = NULL;
	bool locked = false;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);
	locked = true;
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_prep_list_del);
	}

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;

		if (_gres_prep_unpack((void **)&gres_prep, protocol_version,
				      buffer) != SLURM_SUCCESS)
			goto unpack_error;

		if (gres_prep) {
			list_append(*gres_list, gres_prep);
			gres_prep = NULL;
		}
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("%s: unpack error", __func__);
	if (gres_prep)
		_prep_list_del(gres_prep);
	if (locked)
		slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}


static int _foreach_prep_build_env(void *x, void *arg)
{
	gres_state_t *gres_ptr = x;
	foreach_prep_build_env_t *foreach_prep_build_env = arg;
	slurm_gres_context_t *gres_ctx;
	gres_prep_t *gres_prep;

	if (!(gres_ctx = _find_context_by_id(gres_ptr->plugin_id))) {
		error("%s: gres not found in context. This should never happen",
		      __func__);
		return 0;
	}

	if (!gres_ctx->ops.prep_build_env) /* No plugin to call */
		return 0;

	gres_prep = (*(gres_ctx->ops.prep_build_env))(gres_ptr->gres_data);
	if (!gres_prep) /* No info to add for this plugin */
		return 0;

	if (!foreach_prep_build_env->prep_gres_list)
		foreach_prep_build_env->prep_gres_list =
			list_create(_prep_list_del);

	gres_prep->plugin_id = gres_ctx->plugin_id;
	gres_prep->node_list = xstrdup(foreach_prep_build_env->node_list);
	list_append(foreach_prep_build_env->prep_gres_list, gres_prep);

	return 0;
}

/*
 * Build List of information needed to set job's Prolog or Epilog environment
 * variables
 *
 * IN job_gres_list - job's GRES allocation info
 * IN hostlist - list of nodes associated with the job
 * RET information about the job's GRES allocation needed by Prolog or Epilog
 */
extern list_t *gres_g_prep_build_env(list_t *job_gres_list, char *node_list)
{
	foreach_prep_build_env_t foreach_prep_build_env = {
		.node_list = node_list,
	};

	if (!job_gres_list)
		return NULL;

	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);
	(void) list_for_each(job_gres_list, _foreach_prep_build_env,
			     &foreach_prep_build_env);
	slurm_mutex_unlock(&gres_context_lock);

	return foreach_prep_build_env.prep_gres_list;
}

static int _foreach_prep_set_env(void *x, void *arg)
{
	gres_prep_t *gres_prep = x;
	foreach_prep_set_env_t *foreach_prep_set_env = arg;
	slurm_gres_context_t *gres_ctx;

	if (!(gres_ctx = _find_context_by_id(gres_prep->plugin_id))) {
		error("%s: GRES ID %u not found in context",
		      __func__, gres_prep->plugin_id);
		return 0;
	}

	if (!gres_ctx->ops.prep_set_env) /* No plugin to call */
		return 0;

	(*(gres_ctx->ops.prep_set_env))
		(foreach_prep_set_env->prep_env_ptr, gres_prep,
		 foreach_prep_set_env->node_inx);

	return 0;
}

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 *
 * IN/OUT prep_env_ptr - environment variable array
 * IN prep_gres_list - generated by TBD
 * IN node_inx - zero origin node index
 */
extern void gres_g_prep_set_env(char ***prep_env_ptr,
				list_t *prep_gres_list, int node_inx)
{
	foreach_prep_set_env_t foreach_prep_set_env = {
		.node_inx = node_inx,
		.prep_env_ptr = prep_env_ptr,
	};

	*prep_env_ptr = NULL;
	if (!prep_gres_list)
		return;

	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);
	(void) list_for_each(prep_gres_list, _foreach_prep_set_env,
			     &foreach_prep_set_env);
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
			  bool use_total_gres,
			  int core_start_bit, int core_end_bit,
			  uint32_t job_id, char *node_name)
{
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	gres_node_state_t *gres_ns = gres_state_node->gres_data;
	char *gres_name = gres_state_job->gres_name;
	int i, j, core_size, core_ctld, top_inx = -1;
	uint64_t gres_avail = 0, gres_total, gres_tmp;
	uint64_t min_gres_node = 0;
	uint32_t *cores_addnt = NULL; /* Additional cores avail from this GRES */
	uint32_t *cores_avail = NULL; /* cores initially avail from this GRES */
	uint32_t core_cnt = 0;
	bitstr_t *alloc_core_bitmap = NULL;
	bitstr_t *avail_core_bitmap = NULL;
	bool use_single_dev = (gres_id_shared(gres_state_job->config_flags) &&
			       !(slurm_conf.select_type_param &
				 MULTIPLE_SHARING_GRES_PJ));
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

	if (min_gres_node && gres_ns->topo_cnt) {
		/* Need to determine which specific cores can be used */
		gres_avail = gres_ns->gres_cnt_avail;
		if (!use_total_gres)
			gres_avail -= gres_ns->gres_cnt_alloc;
		if (min_gres_node > gres_avail)
			return (uint32_t) 0;	/* insufficient GRES avail */

		core_ctld = core_end_bit - core_start_bit + 1;
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			if (!gres_ns->topo_core_bitmap[i])
				continue;
			core_ctld = bit_size(gres_ns->
						topo_core_bitmap[i]);
			break;
		}

		alloc_core_bitmap = bit_alloc(core_ctld);
		bit_set_all(alloc_core_bitmap);


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
			if (gres_id_shared(gres_state_job->config_flags) &&
			    gres_js->gres_per_task) {
				/*
				 * Remove remaining shared gres_per_task
				 * Because we don't allocate shared
				 * gres_per_task across multiple sharing gres.
				 * See _set_shared_task_bits() in
				 * gres_select_filter.c
				 */
				gres_tmp -= (gres_tmp % gres_js->gres_per_task);
			}
			if (gres_tmp == 0) {
				error("gres/%s: topology allocation error on node %s",
				      gres_name, node_name);
				break;
			}
			/* update counts of allocated cores and GRES */
			if (use_single_dev) {
				/*
				 * Process outside of loop after specific
				 * device selected
				 */
			} else if (!gres_ns->topo_core_bitmap[top_inx]) {
				bit_set_all(alloc_core_bitmap);
			} else if (gres_avail) {
				bit_or(alloc_core_bitmap,
				       gres_ns->
				       topo_core_bitmap[top_inx]);
			} else {
				bit_and(alloc_core_bitmap,
					gres_ns->
					topo_core_bitmap[top_inx]);
			}
			if (use_single_dev) {
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
		if (use_single_dev && (top_inx >= 0) &&
		    (gres_avail >= min_gres_node)) {
			if (!gres_ns->topo_core_bitmap[top_inx]) {
				bit_set_all(alloc_core_bitmap);
			} else {
				bit_or(alloc_core_bitmap,
				       gres_ns->
				       topo_core_bitmap[top_inx]);
			}
			core_cnt = bit_set_count(alloc_core_bitmap);
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

static int _foreach_job_test(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	foreach_job_test_t *foreach_job_test = arg;
	uint32_t tmp_cnt;
	gres_state_t *gres_state_node =
		list_find_first(foreach_job_test->node_gres_list,
				gres_find_id,
				&gres_state_job->plugin_id);
	if (!gres_state_node) {
		/* node lack resources required by the job */
		foreach_job_test->core_cnt = 0;
		return -1;
	}

	tmp_cnt = _job_test(gres_state_job, gres_state_node,
			    foreach_job_test->use_total_gres,
			    foreach_job_test->core_start_bit,
			    foreach_job_test->core_end_bit,
			    foreach_job_test->job_id,
			    foreach_job_test->node_name);
	if (tmp_cnt != NO_VAL) {
		if (foreach_job_test->core_cnt == NO_VAL)
			foreach_job_test->core_cnt = tmp_cnt;
		else
			foreach_job_test->core_cnt =
				MIN(tmp_cnt, foreach_job_test->core_cnt);
	}

	if (foreach_job_test->core_cnt == 0)
		return -1;

	return 0;
}

/*
 * Determine how many cores on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_job_state_validate()
 * IN node_gres_list - node's gres_list built by gres_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are committed to running jobs
 * IN core_start_bit - index into core_bitmap for this node's first core
 * IN core_end_bit   - index into core_bitmap for this node's last core
 * IN job_id         - job's ID (for logging)
 * IN node_name      - name of the node (for logging)
 * IN disable binding- --gres-flags=disable-binding
 * RET: NO_VAL    - All cores on node are available
 *      otherwise - Count of available cores
 */
extern uint32_t gres_job_test(list_t *job_gres_list, list_t *node_gres_list,
			      bool use_total_gres,
			      int core_start_bit, int core_end_bit,
			      uint32_t job_id, char *node_name)
{
	foreach_job_test_t foreach_job_test = {
		.core_cnt = NO_VAL,
		.core_end_bit = core_end_bit,
		.core_start_bit = core_start_bit,
		.job_id = job_id,
		.node_gres_list = node_gres_list,
		.node_name = node_name,
		.use_total_gres = use_total_gres,
	};

	if (job_gres_list == NULL)
		return NO_VAL;
	if (node_gres_list == NULL)
		return 0;

	(void) list_for_each(job_gres_list, _foreach_job_test,
			     &foreach_job_test);

	return foreach_job_test.core_cnt;
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
		xfree(sock_gres);
	}
}

static int _foreach_sock_str(void *x, void *arg)
{
	sock_gres_t *sock_gres = x;
	foreach_sock_str_t *foreach_sock_str = arg;
	char *gres_name = sock_gres->gres_state_job->gres_name;
	gres_job_state_t *gres_js = sock_gres->gres_state_job->gres_data;
	char *type_name = gres_js->type_name;

	if (foreach_sock_str->sock_inx < 0) {
		if (sock_gres->cnt_any_sock) {
			if (type_name) {
				xstrfmtcat(foreach_sock_str->gres_str,
					   "%s%s:%s:%"PRIu64,
					   foreach_sock_str->sep,
					   gres_name,
					   type_name,
					   sock_gres->cnt_any_sock);
			} else {
				xstrfmtcat(foreach_sock_str->gres_str,
					   "%s%s:%"PRIu64,
					   foreach_sock_str->sep, gres_name,
					   sock_gres->cnt_any_sock);
			}
			foreach_sock_str->sep = " ";
		}
		return 0;
	}
	if (!sock_gres->cnt_by_sock ||
	    (sock_gres->cnt_by_sock[foreach_sock_str->sock_inx] == 0))
		return 0;
	if (type_name) {
		xstrfmtcat(foreach_sock_str->gres_str, "%s%s:%s:%"PRIu64,
			   foreach_sock_str->sep,
			   gres_name, type_name,
			   sock_gres->cnt_by_sock[foreach_sock_str->sock_inx]);
	} else {
		xstrfmtcat(foreach_sock_str->gres_str, "%s%s:%"PRIu64,
			   foreach_sock_str->sep,
			   gres_name,
			   sock_gres->cnt_by_sock[foreach_sock_str->sock_inx]);
	}
	foreach_sock_str->sep = " ";
	return 0;
}

/*
 * Build a string containing the GRES details for a given node and socket
 * sock_gres_list IN - List of sock_gres_t entries
 * sock_inx IN - zero-origin socket for which information is to be returned
 *		 if value < 0, then report GRES unconstrained by core
 * RET string, must call xfree() to release memory
 */
extern char *gres_sock_str(list_t *sock_gres_list, int sock_inx)
{
	foreach_sock_str_t foreach_sock_str = {
		.gres_str = NULL,
		.sep = "",
		.sock_inx = sock_inx,
	};

	if (!sock_gres_list)
		return NULL;

	(void) list_for_each(sock_gres_list, _foreach_sock_str,
			     &foreach_sock_str);

	return foreach_sock_str.gres_str;
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

static int _accumulate_gres_device(void *x, void *arg)
{
	gres_state_t *gres_ptr = x;
	foreach_gres_accumulate_device_t *foreach_gres_accumulate_device = arg;

	if (gres_ptr->plugin_id != foreach_gres_accumulate_device->plugin_id)
		return 0;

	if (foreach_gres_accumulate_device->is_job) {
		_accumulate_job_gres_alloc(
			gres_ptr->gres_data,
			foreach_gres_accumulate_device->node_inx,
			foreach_gres_accumulate_device->gres_bit_alloc,
			&foreach_gres_accumulate_device->gres_cnt);
	} else {
		_accumulate_step_gres_alloc(
			gres_ptr,
			foreach_gres_accumulate_device->gres_bit_alloc,
			&foreach_gres_accumulate_device->gres_cnt,
			foreach_gres_accumulate_device->gres_per_bit);
	}

	/* Does job have a sharing GRES (GPU)? */
	if (gres_id_sharing(foreach_gres_accumulate_device->plugin_id))
		foreach_gres_accumulate_device->sharing_gres_allocated = true;

	return 0;
}

/*
 * Set environment variables as required for a batch or interactive step
 */
extern void gres_g_job_set_env(stepd_step_rec_t *step, int node_inx)
{
	int i;
	gres_internal_flags_t flags = GRES_INTERNAL_FLAG_NONE;
	bitstr_t *gres_bit_alloc = NULL;
	foreach_gres_accumulate_device_t foreach_gres_accumulate_device = {
		.gres_bit_alloc = &gres_bit_alloc,
		.is_job = true,
		.node_inx = node_inx,
	};

	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		slurm_gres_context_t *gres_ctx = &gres_context[i];
		if (!gres_ctx->ops.job_set_env)
			continue;	/* No plugin to call */
		if (step->job_gres_list) {
			foreach_gres_accumulate_device.plugin_id =
				gres_ctx->plugin_id;
			(void) list_for_each(step->job_gres_list,
					     _accumulate_gres_device,
					     &foreach_gres_accumulate_device);
		}

		/*
		 * Do not let MPS or Shard (shared GRES) clear any envs set for
		 * a GPU (sharing GRES) when a GPU is allocated but an
		 * MPS/Shard is not. Sharing GRES plugins always run before
		 * shared GRES, so we don't need to protect MPS/Shard from GPU.
		 */
		if (gres_id_shared(gres_ctx->config_flags) &&
		    foreach_gres_accumulate_device.sharing_gres_allocated)
			flags |= GRES_INTERNAL_FLAG_PROTECT_ENV;

		if ((step->flags & LAUNCH_EXT_LAUNCHER)) {
			/*
			 * We need the step environment variables, but still
			 * use all the job's gres.
			 */
			(*(gres_ctx->ops.step_set_env))(
				&step->env,
				gres_bit_alloc,
				foreach_gres_accumulate_device.gres_cnt,
				flags);
		} else
			(*(gres_ctx->ops.job_set_env))(
				&step->env,
				gres_bit_alloc,
				foreach_gres_accumulate_device.gres_cnt,
				flags);
		foreach_gres_accumulate_device.gres_cnt = 0;
		FREE_NULL_BITMAP(gres_bit_alloc);
	}
	slurm_mutex_unlock(&gres_context_lock);
}

static int _job_state_log(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	uint32_t job_id = *(uint32_t *)arg;
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	char *sparse_msg = "", tmp_str[128];
	int i;

	xassert(gres_js);
	info("gres_job_state gres:%s(%u) type:%s(%u) job:%u flags:%s",
	     gres_state_job->gres_name, gres_state_job->plugin_id,
	     gres_js->type_name,
	     gres_js->type_id, job_id, gres_flags2str(gres_js->flags));
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
		if (gres_js->gres_bit_select &&
		    gres_js->gres_bit_select[i] &&
		    gres_js->gres_per_bit_select &&
		    gres_js->gres_per_bit_select[i]) {
			for (int j = 0;
			     (j = bit_ffs_from_bit(gres_js->gres_bit_select[i],
						   j)) >= 0;
			     j++) {
				info("  gres_per_bit_select[%d][%d]:%"PRIu64,
				     i, j, gres_js->gres_per_bit_select[i][j]);
			}
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

		if (gres_js->gres_bit_alloc &&
		    gres_js->gres_bit_alloc[i] &&
		    gres_js->gres_per_bit_alloc &&
		    gres_js->gres_per_bit_alloc[i]) {
			for (int j = 0;
			     (j = bit_ffs_from_bit(gres_js->gres_bit_alloc[i],
						   j)) >= 0;
			     j++) {
				info("  gres_per_bit_alloc[%d][%d]:%"PRIu64,
				     i, j, gres_js->gres_per_bit_alloc[i][j]);
			}
		}

		if (gres_js->gres_bit_step_alloc &&
		    gres_js->gres_bit_step_alloc[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_js->gres_bit_step_alloc[i]);
			info("  gres_bit_step_alloc[%d]:%s of %d", i, tmp_str,
			     (int) bit_size(gres_js->gres_bit_step_alloc[i]));
		} else if (gres_js->gres_bit_step_alloc)
			info("  gres_bit_step_alloc[%d]:NULL", i);

		if (gres_js->gres_bit_step_alloc &&
		    gres_js->gres_bit_step_alloc[i] &&
		    gres_js->gres_per_bit_step_alloc &&
		    gres_js->gres_per_bit_step_alloc[i]) {
			for (int j = 0;
			     (j = bit_ffs_from_bit(
				      gres_js->gres_bit_step_alloc[i], j)) >= 0;
			     j++) {
				info("  gres_per_bit_step_alloc[%d][%d]:%"PRIu64,
				     i, j,
				     gres_js->gres_per_bit_step_alloc[i][j]);
			}
		}

		if (gres_js->gres_cnt_step_alloc) {
			info("  gres_cnt_step_alloc[%d]:%"PRIu64"", i,
			     gres_js->gres_cnt_step_alloc[i]);
		}
	}

	return 0;
}

static int _foreach_gres_list_cnt(void *x, void *arg)
{
	gres_state_t *gres_state_ptr = x;
	foreach_gres_list_cnt_t *foreach_gres_list_cnt = arg;
	uint64_t total_gres;
	void *type_name;

	if (gres_state_ptr->plugin_id != foreach_gres_list_cnt->plugin_id)
		return 0;

	if (foreach_gres_list_cnt->is_job) {
		gres_job_state_t *gres_js = gres_state_ptr->gres_data;
		type_name = gres_js->type_name;
		total_gres = gres_js->total_gres;
	} else {
		gres_step_state_t *gres_ss = gres_state_ptr->gres_data;
		type_name = gres_ss->type_name;
		total_gres = gres_ss->total_gres;
	}

	/* If we are filtering on GRES type, ignore other types */
	if (foreach_gres_list_cnt->filter_type &&
	    xstrcasecmp(foreach_gres_list_cnt->gres_type, type_name))
		return 0;

	if ((total_gres == NO_VAL64) || (total_gres == 0))
		return 0;

	if (foreach_gres_list_cnt->gres_cnt == NO_VAL64)
		foreach_gres_list_cnt->gres_cnt = total_gres;
	else
		foreach_gres_list_cnt->gres_cnt += total_gres;

	return 0;
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
static uint64_t _get_gres_list_cnt(list_t *gres_list, char *gres_name,
				   char *gres_type, bool is_job)
{
	foreach_gres_list_cnt_t foreach_gres_list_cnt = {
		.gres_cnt = NO_VAL64,
		.gres_type = gres_type,
		.is_job = is_job,
	};

	if ((gres_list == NULL) || (list_count(gres_list) == 0))
		return foreach_gres_list_cnt.gres_cnt;

	foreach_gres_list_cnt.plugin_id = gres_build_id(gres_name);

	if (gres_type && (gres_type[0] != '\0'))
		foreach_gres_list_cnt.filter_type = true;

	(void) list_for_each(gres_list, _foreach_gres_list_cnt,
			     &foreach_gres_list_cnt);

	return foreach_gres_list_cnt.gres_cnt;
}

static uint64_t _get_job_gres_list_cnt(list_t *gres_list, char *gres_name,
				       char *gres_type)
{
	return _get_gres_list_cnt(gres_list, gres_name, gres_type, true);
}

static uint64_t _get_step_gres_list_cnt(list_t *gres_list, char *gres_name,
					char *gres_type)
{
	return _get_gres_list_cnt(gres_list, gres_name, gres_type, false);
}

/*
 * Log a job's current gres state
 * IN gres_list - generated by gres_job_state_validate()
 * IN job_id - job's ID
 */
extern void gres_job_state_log(list_t *gres_list, uint32_t job_id)
{
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES) || !gres_list)
		return;

	(void) list_for_each(gres_list, _job_state_log, &job_id);
}

static int _find_device(void *x, void *key)
{
	gres_device_t *device_x = (gres_device_t *)x;
	gres_device_t *device_key = (gres_device_t *)key;

	if (!xstrcmp(device_x->path, device_key->path))
		return 1;

	return 0;
}

static int _foreach_init_device_list(void *x, void *arg)
{
	gres_device_t *gres_device = x;
	list_t **device_list = arg;

	if (!*device_list)
		*device_list = list_create(NULL);
	gres_device->alloc = 0;
	/*
	 * Keep the list unique by not adding duplicates (in the
	 * case of MPS and GPU)
	 */
	if (!list_find_first(*device_list, _find_device, gres_device))
		list_append(*device_list, gres_device);

	return 0;
}

static int _foreach_alloc_gres_device(void *x, void *arg)
{
	gres_device_t *gres_device = x;
	foreach_alloc_gres_device_t *foreach_alloc_gres_device = arg;
	if (!bit_test(foreach_alloc_gres_device->gres_bit_alloc,
		      gres_device->index))
		return 0;

	if (!foreach_alloc_gres_device->usable_gres ||
	    bit_test(foreach_alloc_gres_device->usable_gres,
		     gres_device->index)) {
		/*
		 * Search for the device among the unique
		 * devices list (since two plugins could have
		 * device records that point to the same file,
		 * like with GPU and MPS)
		 */
		gres_device_t *gres_device2 = list_find_first(
			foreach_alloc_gres_device->device_list,
			_find_device,
			gres_device);
		/*
		 * Set both, in case they point to different records
		 */
		gres_device->alloc = 1;
		if (gres_device2)
			gres_device2->alloc = 1;
	}

	return 0;
}

extern list_t *gres_g_get_devices(list_t *gres_list, bool is_job,
				  uint16_t accel_bind_type, char *tres_bind_str,
				  int local_proc_id, stepd_step_rec_t *step)
{
	int j;
	bitstr_t *gres_bit_alloc = NULL;
	uint64_t *gres_per_bit = NULL;
	list_t *gres_devices;
	list_t *device_list = NULL;
	bitstr_t *usable_gres = NULL;

	xassert(gres_context_cnt >= 0);

	/*
	 * Create a unique device list of all possible GRES device files.
	 * Initialize each device to deny.
	 */
	slurm_mutex_lock(&gres_context_lock);
	for (j = 0; j < gres_context_cnt; j++) {
		if (!gres_context[j].ops.get_devices){
			gres_devices = gres_context[j].np_gres_devices;
		} else {
			gres_devices = (*(gres_context[j].ops.get_devices))();
		}
		if (!gres_devices || !list_count(gres_devices))
			continue;

		(void) list_for_each(gres_devices, _foreach_init_device_list,
				     &device_list);
	}

	if (!gres_list) {
		slurm_mutex_unlock(&gres_context_lock);
		return device_list;
	}

	if (accel_bind_type)
		_parse_accel_bind_type(accel_bind_type, tres_bind_str);

	for (j = 0; j < gres_context_cnt; j++) {
		/* We need to get a gres_bit_alloc with all the gres types
		 * merged (accumulated) together */
		foreach_gres_accumulate_device_t arg = {
			.gres_bit_alloc = &gres_bit_alloc,
			.gres_per_bit = &gres_per_bit,
			.is_job = is_job,
			.plugin_id = gres_context[j].plugin_id,
		};
		foreach_alloc_gres_device_t foreach_alloc_gres_device = {
			.device_list = device_list,
		};

		(void) list_for_each(gres_list, _accumulate_gres_device, &arg);

		if (!gres_bit_alloc)
			continue;
		if (!gres_context[j].ops.get_devices){
			gres_devices = gres_context[j].np_gres_devices;
		} else {
			gres_devices = (*(gres_context[j].ops.get_devices))();
		}
		if (!gres_devices) {
			error("We should had got gres_devices, but for some reason none were set in the plugin.");
			continue;
		}

		if (_get_usable_gres(j, local_proc_id, tres_bind_str,
				     &usable_gres, gres_bit_alloc, true, step,
				     gres_per_bit, NULL) == SLURM_ERROR)
			continue;

		foreach_alloc_gres_device.gres_bit_alloc = gres_bit_alloc;
		foreach_alloc_gres_device.usable_gres = usable_gres;

		(void) list_for_each(gres_devices, _foreach_alloc_gres_device,
				     &foreach_alloc_gres_device);

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
	if (gres_ss->gres_per_bit_alloc) {
		for (i = 0; i < gres_ss->node_cnt; i++){
			xfree(gres_ss->gres_per_bit_alloc[i]);
		}
		xfree(gres_ss->gres_per_bit_alloc);
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

/*
 * TRES specification parse logic
 * in_val IN - initial input string
 * cnt OUT - count of values
 * gres_list IN/OUT - where to search for (or add) new step TRES record
 * save_ptr IN/OUT - NULL on initial call, otherwise value from previous call
 * rc OUT - unchanged or an error code
 * RET gres - step record to set value in, found or created by this function
 */
static gres_state_t *_get_next_step_gres(char *in_val, uint64_t *cnt,
					 list_t *gres_list, char **save_ptr,
					 int *rc)
{
	static char *prev_save_ptr = NULL;
	int context_inx = NO_VAL, my_rc = SLURM_SUCCESS;
	gres_step_state_t *gres_ss = NULL;
	gres_state_t *gres_state_step = NULL;
	gres_key_t step_search_key;
	char *type = NULL, *name = NULL;

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
				    cnt, &prev_save_ptr)) ||
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
		gres_ss->type_id = step_search_key.type_id;
		gres_ss->type_name = type;
		type = NULL;	/* String moved above */
		gres_state_step = gres_create_state(
			&gres_context[context_inx], GRES_STATE_SRC_CONTEXT_PTR,
			GRES_STATE_TYPE_STEP, gres_ss);
		list_append(gres_list, gres_state_step);
	}

fini:	xfree(name);
	xfree(type);
	if (my_rc != SLURM_SUCCESS) {
		prev_save_ptr = NULL;
		if (my_rc == ESLURM_INVALID_GRES && running_in_slurmctld())
			info("Invalid GRES step specification %s", in_val);
		*rc = my_rc;
	}
	*save_ptr = prev_save_ptr;
	return gres_state_step;
}

static int _handle_ntasks_per_tres_step(list_t *new_step_list,
					uint16_t ntasks_per_tres,
					uint32_t *num_tasks,
					uint32_t *cpu_count)
{
	gres_state_t *gres_state_step;
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
		xstrfmtcat(gres, "gres/gpu:%u", gpus);
		in_val = gres;
		if (*num_tasks != ntasks_per_tres * gpus) {
			log_flag(GRES, "%s: -n/--ntasks %u is not a multiple of --ntasks-per-gpu=%u",
				 __func__, *num_tasks, ntasks_per_tres);
			return ESLURM_INVALID_GRES;
		}
		while ((gres_state_step =
			_get_next_step_gres(in_val, &cnt,
					    new_step_list,
					    &save_ptr, &rc))) {
			gres_ss = gres_state_step->gres_data;
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
			uint32_t cpus_per_task = *cpu_count / *num_tasks;
			*num_tasks = tmp;
			tmp = tmp * cpus_per_task;
			if (*cpu_count && (*cpu_count < tmp)) {
				/* step_spec->cpu_count == 0 means SSF_OVERSUBSCRIBE */
				*cpu_count = tmp;
			}
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
				    list_t **step_gres_list,
				    uint32_t job_id,
				    uint32_t step_id,
				    uint32_t *num_tasks,
				    uint32_t *cpu_count, char **err_msg)
{
	int rc = SLURM_SUCCESS;
	gres_step_state_t *gres_ss;
	gres_state_t *gres_state_step;
	list_t *new_step_list;
	uint64_t cnt = 0;
	uint16_t cpus_per_gres = 0;
	char *cpus_per_gres_name = NULL;
	char *cpus_per_gres_type = NULL;

	*step_gres_list = NULL;
	xassert(gres_context_cnt >= 0);
	xassert(num_tasks);
	xassert(cpu_count);

	slurm_mutex_lock(&gres_context_lock);
	new_step_list = list_create(gres_step_list_delete);
	if (cpus_per_tres) {
		char *in_val = cpus_per_tres, *save_ptr = NULL;
		while ((gres_state_step = _get_next_step_gres(
						in_val, &cnt,
						new_step_list,
						&save_ptr, &rc))) {
			gres_ss = gres_state_step->gres_data;
			gres_ss->cpus_per_gres = cnt;
			in_val = NULL;
			/* Only a single cpus_per_tres value is allowed. */
			if (cpus_per_gres) {
				if (err_msg)
					*err_msg = xstrdup("You may only request cpus_per_tres for one tres");
				else
					error("You may only request cpus_per_tres for one tres");
				rc = ESLURM_INVALID_GRES;
				FREE_NULL_LIST(new_step_list);
				goto fini;
			} else {
				cpus_per_gres = cnt;
				cpus_per_gres_name = gres_state_step->gres_name;
				cpus_per_gres_type = gres_ss->type_name;
			}
		}
	}
	if (tres_per_step) {
		char *in_val = tres_per_step, *save_ptr = NULL;
		while ((gres_state_step = _get_next_step_gres(
						in_val, &cnt,
						new_step_list,
						&save_ptr, &rc))) {
			gres_ss = gres_state_step->gres_data;
			gres_ss->gres_per_step = cnt;
			in_val = NULL;
			gres_ss->total_gres =
				MAX(gres_ss->total_gres, cnt);
		}
	}
	if (tres_per_node) {
		char *in_val = tres_per_node, *save_ptr = NULL;
		while ((gres_state_step = _get_next_step_gres(
						in_val, &cnt,
						new_step_list,
						&save_ptr, &rc))) {
			gres_ss = gres_state_step->gres_data;
			gres_ss->gres_per_node = cnt;
			in_val = NULL;
			gres_ss->total_gres =
				MAX(gres_ss->total_gres, step_min_nodes * cnt);
		}
	}
	if (tres_per_socket) {
		char *in_val = tres_per_socket, *save_ptr = NULL;
		while ((gres_state_step = _get_next_step_gres(
						in_val, &cnt,
						new_step_list,
						&save_ptr, &rc))) {
			gres_ss = gres_state_step->gres_data;
			gres_ss->gres_per_socket = cnt;
			in_val = NULL;
			// TODO: What is sockets_per_node and ntasks_per_socket?
			// if (*sockets_per_node != NO_VAL16) {
			//	cnt *= *sockets_per_node;
			// } else if ((*num_tasks != NO_VAL) &&
			//	   (*ntasks_per_socket != NO_VAL16)) {
			//	cnt *= ROUNDUP(*num_tasks, *ntasks_per_socket);
			// }
			// gres_ss->total_gres =
			//	MAX(gres_ss->total_gres, cnt);
		}
	}
	if (tres_per_task) {
		char *in_val = tres_per_task, *save_ptr = NULL;
		while ((gres_state_step = _get_next_step_gres(
						in_val, &cnt,
						new_step_list,
						&save_ptr, &rc))) {
			gres_ss = gres_state_step->gres_data;
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
		while ((gres_state_step = _get_next_step_gres(
						in_val, &cnt,
						new_step_list,
						&save_ptr, &rc))) {
			gres_ss = gres_state_step->gres_data;
			gres_ss->mem_per_gres = cnt;
			in_val = NULL;
		}
	}

	if ((ntasks_per_tres != NO_VAL16)) {
		rc = _handle_ntasks_per_tres_step(new_step_list,
						  ntasks_per_tres,
						  num_tasks,
						  cpu_count);
	}

	if ((rc == SLURM_SUCCESS) && cpus_per_gres && *cpu_count &&
	    running_in_slurmctld()) {
		/*
		 * Update cpu_count = the total requested gres * cpus_per_gres
		 *
		 * If SSF_OVERCOMMIT (step_spec->cpu_count == 0), don't update.
		 * Only update if in slurmctld because the step can inherit
		 * gres from the job_gres_list_req, which only exists in
		 * slurmctld.
		 */
		uint64_t gpu_cnt = _get_step_gres_list_cnt(new_step_list,
							   cpus_per_gres_name,
							   cpus_per_gres_type);

		if (gpu_cnt == NO_VAL64) {
			if (err_msg)
				*err_msg = xstrdup("cpus_per_gres also requires specifying the same gres");
			else
				error("cpus_per_gres also requires specifying the same gres");
			rc = ESLURM_INVALID_GRES;
			FREE_NULL_LIST(new_step_list);
		} else
			*cpu_count = gpu_cnt * cpus_per_gres;
	}

	if (list_count(new_step_list) == 0) {
		FREE_NULL_LIST(new_step_list);
	} else {
		if (rc == SLURM_SUCCESS) {
			job_validate_t job_validate = {
				.over_array = xcalloc(list_count(new_step_list),
						      sizeof(overlap_check_t)),
			};

			(void) list_for_each(new_step_list,
					     _foreach_set_over_array,
					     &job_validate);

			if (job_validate.overlap_merge)
				rc = _merge_generic_data(new_step_list,
							 &job_validate);
			xfree(job_validate.over_array);
		}
		if (rc == SLURM_SUCCESS)
			*step_gres_list = new_step_list;
		else
			FREE_NULL_LIST(new_step_list);
	}
fini:
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
	if (new_gres_ss->gres_per_bit_alloc && gres_ss->gres_bit_alloc) {
		new_gres_ss->gres_per_bit_alloc = xcalloc(gres_ss->node_cnt,
							  sizeof(uint64_t *));
		for (i = 0; i < gres_ss->node_cnt; i++) {
			int bit_cnt = bit_size(gres_ss->gres_bit_alloc[i]);
			new_gres_ss->gres_per_bit_alloc[i] = xcalloc(
				bit_cnt, sizeof(uint64_t));
			memcpy(new_gres_ss->gres_per_bit_alloc[i],
			       gres_ss->gres_per_bit_alloc[i],
			       bit_cnt * sizeof(uint64_t));
		}
	}
	return new_gres_ss;
}

static void *_step_state_dup2(gres_step_state_t *gres_ss, int job_node_index)
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
			gres_ss->gres_cnt_node_alloc[job_node_index];
	}

	if ((job_node_index < gres_ss->node_cnt) && gres_ss->gres_bit_alloc &&
	    gres_ss->gres_bit_alloc[job_node_index]) {
		new_gres_ss->gres_bit_alloc = xmalloc(sizeof(bitstr_t *));
		new_gres_ss->gres_bit_alloc[0] =
			bit_copy(gres_ss->gres_bit_alloc[job_node_index]);
	}
	if (gres_ss->gres_per_bit_alloc &&
	    (job_node_index < gres_ss->node_cnt) && gres_ss->gres_bit_alloc &&
	    gres_ss->gres_bit_alloc[job_node_index]) {
		int bit_cnt = bit_size(gres_ss->gres_bit_alloc[job_node_index]);
		new_gres_ss->gres_per_bit_alloc = xmalloc(sizeof(uint64_t *));
		new_gres_ss->gres_per_bit_alloc[0] = xcalloc(bit_cnt,
							     sizeof(uint64_t));
		memcpy(new_gres_ss->gres_per_bit_alloc[0],
		       gres_ss->gres_per_bit_alloc[job_node_index],
		       bit_cnt * sizeof(uint64_t));
	}
	return new_gres_ss;
}

/*
 * Create a copy of a step's gres state
 * IN gres_list - List of Gres records for this step to track usage
 * RET The copy or NULL on failure
 */
list_t *gres_step_state_list_dup(list_t *gres_list)
{
	return gres_step_state_extract(gres_list, -1);
}

static int _foreach_step_state_extract(void *x, void *arg)
{
	gres_state_t *gres_state_step = x;
	foreach_state_list_dup_t *foreach_state_list_dup = arg;
	gres_state_t *new_gres_state_step;
	void *new_gres_data;

	if (foreach_state_list_dup->job_node_index == -1)
		new_gres_data = _step_state_dup(gres_state_step->gres_data);
	else
		new_gres_data = _step_state_dup2(
			gres_state_step->gres_data,
			foreach_state_list_dup->job_node_index);

	if (!foreach_state_list_dup->new_gres_list)
		foreach_state_list_dup->new_gres_list =
			list_create(gres_step_list_delete);

	new_gres_state_step = gres_create_state(
		gres_state_step, GRES_STATE_SRC_STATE_PTR,
		GRES_STATE_TYPE_STEP, new_gres_data);
	list_append(foreach_state_list_dup->new_gres_list, new_gres_state_step);

	return 0;
}

/*
 * Create a copy of a step's gres state for a particular node index
 * IN gres_list - List of Gres records for this step to track usage
 * IN job_node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
list_t *gres_step_state_extract(list_t *gres_list, int job_node_index)
{
	foreach_state_list_dup_t foreach_state_list_dup = {
		.job_node_index = job_node_index,
	};

	if (gres_list)
		(void) list_for_each(gres_list, _foreach_step_state_extract,
				     &foreach_state_list_dup);

	return foreach_state_list_dup.new_gres_list;
}

/*
 * Pack a step's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_stepmgr_step_alloc()
 * IN/OUT buffer - location to write state to
 * IN step_id - job and step ID for logging
 */
extern int gres_step_state_pack(list_t *gres_list, buf_t *buffer,
				slurm_step_id_t *step_id,
				uint16_t protocol_version)
{
	pack_state_t pack_state = {
		.buffer = buffer,
		.magic = GRES_MAGIC,
		.protocol_version = protocol_version,
	};

	return _pack_state(gres_list, &pack_state, _foreach_step_state_pack);
}

/*
 * Unpack a step's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_step_state_pack()
 * IN/OUT buffer - location to read state from
 * IN step_id - job and step ID for logging
 */
extern int gres_step_state_unpack(list_t **gres_list, buf_t *buffer,
				  slurm_step_id_t *step_id,
				  uint16_t protocol_version)
{
	int i, rc = SLURM_SUCCESS;
	uint32_t magic = 0, plugin_id = 0, uint32_tmp = 0;
	uint16_t rec_cnt = 0;
	uint8_t data_flag = 0;
	gres_state_t *gres_state_step;
	gres_step_state_t *gres_ss = NULL;
	bool locked = false;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	xassert(gres_context_cnt >= 0);

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
		if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
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
			safe_unpackstr(&gres_ss->type_name, buffer);
			gres_ss->type_id = gres_build_id(gres_ss->type_name);
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
			for (i = 0; i < gres_ss->node_cnt; i++) {
				safe_unpack8(&data_flag, buffer);
				if (!data_flag)
					continue;
				if (!gres_ss->gres_per_bit_alloc)
					safe_xcalloc(
						gres_ss->gres_per_bit_alloc,
						gres_ss->node_cnt,
						sizeof(uint64_t *));
				safe_unpack64_array(
					&gres_ss->gres_per_bit_alloc[i],
					&uint32_tmp, buffer);
			}
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
			for (i = 0; i < gres_ss->node_cnt; i++) {
				safe_unpack8(&data_flag, buffer);
				if (!data_flag)
					continue;
				if (!gres_ss->gres_per_bit_alloc)
					safe_xcalloc(
						gres_ss->gres_per_bit_alloc,
						gres_ss->node_cnt,
						sizeof(uint64_t *));
				safe_unpack64_array(
					&gres_ss->gres_per_bit_alloc[i],
					&uint32_tmp, buffer);
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

static int _foreach_step_count(void *x, void *arg)
{
	gres_state_t *gres_state_step = x;
	foreach_gres_list_cnt_t *foreach_gres_list_cnt = arg;
	gres_step_state_t *gres_ss = gres_state_step->gres_data;

	if (gres_state_step->plugin_id != foreach_gres_list_cnt->plugin_id)
		return 0;

	/* gres_cnt_node_alloc has one element in slurmstepd */
	if (foreach_gres_list_cnt->gres_cnt == NO_VAL64)
		foreach_gres_list_cnt->gres_cnt =
			gres_ss->gres_cnt_node_alloc[0];
	else
		foreach_gres_list_cnt->gres_cnt +=
			gres_ss->gres_cnt_node_alloc[0];
	return 0;
}

/* Return the count of GRES of a specific name on this machine
 * IN step_gres_list - generated by gres_stepmgr_step_alloc()
 * IN gres_name - name of the GRES to match
 * RET count of GRES of this specific name available to the job or NO_VAL64
 */
extern uint64_t gres_step_count(list_t *step_gres_list, char *gres_name)
{
	foreach_gres_list_cnt_t foreach_gres_list_cnt = {
		.gres_cnt = NO_VAL64,
	};

	if (step_gres_list) {
		slurm_mutex_lock(&gres_context_lock);
		for (int i = 0; i < gres_context_cnt; i++) {
			if (!xstrcmp(gres_context[i].gres_name, gres_name)) {
				foreach_gres_list_cnt.plugin_id =
					gres_context[i].plugin_id;
				(void) list_for_each(step_gres_list,
						     _foreach_step_count,
						     &foreach_gres_list_cnt);
				break;
			}
		}
		slurm_mutex_unlock(&gres_context_lock);
	}

	return foreach_gres_list_cnt.gres_cnt;
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

bitstr_t *cpu_set_to_bit_str(cpu_set_t *cpu_set, int cpu_count)
{
	bitstr_t *cpu_bitstr = bit_alloc(cpu_count);

	if (cpu_set) {
		for (int i = 0; i < cpu_count; i++)
			if (CPU_ISSET(i, cpu_set))
				bit_set(cpu_bitstr, i);
	} else {
		bit_set_all(cpu_bitstr);
	}

	return cpu_bitstr;

}

static int _foreach_closest_usable_gres(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	foreach_closest_usable_gres_t *foreach_closest_usable_gres = arg;

	if (gres_slurmd_conf->plugin_id !=
	    foreach_closest_usable_gres->plugin_id)
		return 0;
	if ((foreach_closest_usable_gres->gres_inx + gres_slurmd_conf->count) >
	    foreach_closest_usable_gres->bitmap_size) {
		error("GRES %s bitmap overflow ((%d + %"PRIu64") > %d)",
		      gres_slurmd_conf->name,
		      foreach_closest_usable_gres->gres_inx,
		      gres_slurmd_conf->count,
		      foreach_closest_usable_gres->bitmap_size);
		return 0;
	}
	if (!gres_slurmd_conf->cpus_bitmap ||
	    bit_overlap_any(gres_slurmd_conf->cpus_bitmap,
			    foreach_closest_usable_gres->task_cpus_bitmap)) {
		bit_nset(foreach_closest_usable_gres->usable_gres,
			 foreach_closest_usable_gres->gres_inx,
			 foreach_closest_usable_gres->gres_inx +
			 gres_slurmd_conf->count - 1);
	}
	foreach_closest_usable_gres->gres_inx += gres_slurmd_conf->count;

	return 0;
}

/*
 * Given a GRES context index, return a bitmap representing those GRES
 * which are available from the CPUs current allocated to this process.
 * This function only works with task/cgroup and constrained devices or
 * if the job step has access to the entire node's resources.
 */
static bitstr_t *_get_closest_usable_gres(uint32_t plugin_id,
					  bitstr_t *gres_bit_alloc,
					  cpu_set_t *task_cpu_set)
{
	foreach_closest_usable_gres_t foreach_closest_usable_gres = {
		.gres_inx = 0,
		.plugin_id = plugin_id,
	};

	if (!gres_conf_list) {
		error("gres_conf_list is null!");
		return NULL;
	}

	foreach_closest_usable_gres.task_cpus_bitmap = cpu_set_to_bit_str(
		task_cpu_set,
		((gres_slurmd_conf_t *)list_peek(gres_conf_list))->cpu_cnt);
	foreach_closest_usable_gres.bitmap_size = bit_size(gres_bit_alloc);
	foreach_closest_usable_gres.usable_gres =
		bit_alloc(foreach_closest_usable_gres.bitmap_size);

	(void) list_for_each(gres_conf_list, _foreach_closest_usable_gres,
			     &foreach_closest_usable_gres);

	FREE_NULL_BITMAP(foreach_closest_usable_gres.task_cpus_bitmap);

	bit_and(foreach_closest_usable_gres.usable_gres, gres_bit_alloc);

	return foreach_closest_usable_gres.usable_gres;
}

static int _foreach_gres_to_task(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	foreach_gres_to_task_t *foreach_gres_to_task = arg;
	int start, end;

	if (gres_slurmd_conf->plugin_id != foreach_gres_to_task->plugin_id)
		return 0;

	start = foreach_gres_to_task->gres_inx *
		foreach_gres_to_task->ntasks_per_gres;
	foreach_gres_to_task->gres_inx += gres_slurmd_conf->count;
	end = foreach_gres_to_task->gres_inx *
		foreach_gres_to_task->ntasks_per_gres;

	if (!bit_set_count_range(foreach_gres_to_task->gres_slots, start, end))
		return 0;

	if (gres_slurmd_conf->cpus_bitmap) {
		if (bit_super_set(foreach_gres_to_task->task_cpus_bitmap,
				  gres_slurmd_conf->cpus_bitmap)) {
			foreach_gres_to_task->best_slot = bit_ffs_from_bit(
				foreach_gres_to_task->gres_slots, start);
			return -1;
		}

		if (foreach_gres_to_task->overlap)
			return 0;

		if (bit_overlap_any(foreach_gres_to_task->task_cpus_bitmap,
				    gres_slurmd_conf->cpus_bitmap)) {
			foreach_gres_to_task->best_slot = bit_ffs_from_bit(
				foreach_gres_to_task->gres_slots, start);
			foreach_gres_to_task->overlap = true;
			return 0;
		}
	}

	if (foreach_gres_to_task->best_slot == -1)
		foreach_gres_to_task->best_slot = bit_ffs_from_bit(
			foreach_gres_to_task->gres_slots, start);

	return 0;
}

/* Select the best available gres from gres_slots */
static int _assign_gres_to_task(cpu_set_t *task_cpu_set, int ntasks_per_gres,
				bitstr_t *gres_slots, uint32_t plugin_id)
{
	foreach_gres_to_task_t foreach_gres_to_task = {
		.best_slot = -1,
		.gres_inx = 0,
		.gres_slots = gres_slots,
		.ntasks_per_gres = ntasks_per_gres,
		.overlap = false,
		.plugin_id = plugin_id,
		.task_cpus_bitmap = cpu_set_to_bit_str(
			task_cpu_set,
			((gres_slurmd_conf_t *)list_peek(gres_conf_list))->
			cpu_cnt),
	};

	(void) list_for_each(gres_conf_list, _foreach_gres_to_task,
			     &foreach_gres_to_task);
	FREE_NULL_BITMAP(foreach_gres_to_task.task_cpus_bitmap);

	if (foreach_gres_to_task.best_slot != -1) {
		bit_clear(foreach_gres_to_task.gres_slots,
			  foreach_gres_to_task.best_slot);
		return (foreach_gres_to_task.best_slot /
			foreach_gres_to_task.ntasks_per_gres);
	} else {
		log_flag(GRES, "%s Can't find free slot", __func__);
		return -1;
	}
}

/*
 * Given the cpu affinity of all tasks, return a bitmap binding a single gres to
 * this task.
 */
static bitstr_t *_get_single_usable_gres(int context_inx,
					 int ntasks_per_gres,
					 int local_proc_id,
					 stepd_step_rec_t *step,
					 bitstr_t *gres_bit_alloc)
{
	int idx = 0;
	bitstr_t *usable_gres = NULL;
	bitstr_t *gres_slots = NULL;
	int32_t gres_count = bit_set_count(gres_bit_alloc);


	/* No need to select gres if there is only 1 to use */
	if (gres_count <= 1) {
		log_flag(GRES, "%s: (task %d) No need to select single gres since count is 0 or 1",
			 __func__, local_proc_id);
		return bit_copy(gres_bit_alloc);
	}

	/*
	 * Create bitmap called gres_slots. This represents the available slots
	 * for tasks on that gres based off of ntasks_per_gres and if that gres
	 * is allocated to the step.
	 */
	if (ntasks_per_gres == 1)
		gres_slots = bit_copy(gres_bit_alloc);
	else {
		gres_slots = bit_alloc(bit_size(gres_bit_alloc) *
				       ntasks_per_gres);
		for (int i = -1;
		     (i = bit_ffs_from_bit(gres_bit_alloc, i + 1)) >= 0;) {
			bit_nset(gres_slots, i * ntasks_per_gres,
				 (((i + 1) * ntasks_per_gres) - 1));
		}
	}

	/*
	 * To ensure no task gets more than ntasks_per_gres, here we one by one,
	 * select an available gres_slot for each task and clear a gres_slot.
	 * Once we reach the current task we can take the gres assignment and
	 * quit the loop
	 */
	for (int i = 0; i <= local_proc_id; i++) {
		idx = _assign_gres_to_task(step->task[i]->cpu_set,
					   ntasks_per_gres, gres_slots,
					   gres_context[context_inx].plugin_id);
	}
	FREE_NULL_BITMAP(gres_slots);

	/* Return a bitmap with this as the only usable GRES */
	usable_gres = bit_alloc(bit_size(gres_bit_alloc));
	if (idx < 0) {
		int n;
		error("%s Can't find free slot for local_proc_id = %d, continue using block distribution",
		     __func__, local_proc_id);
		n = local_proc_id % gres_count;
		idx = bit_get_bit_num(gres_bit_alloc, n);
	}

	bit_set(usable_gres, idx);

	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES){
		char *usable_gres_str = bit_fmt_hexmask_trim(usable_gres);
		log_flag(GRES, "%s: local_proc_id = %d; usable_gres: %s",
			 __func__, local_proc_id, usable_gres_str);
		xfree(usable_gres_str);
	}

	return usable_gres;
}

/*
 * Configure the GRES hardware allocated to the current step while privileged
 *
 * IN step_gres_list - Step's GRES specification
 * IN node_id        - relative position of this node in step
 * IN settings       - string containing configuration settings for the hardware
 */
extern void gres_g_step_hardware_init(list_t *step_gres_list,
				      uint32_t node_id, char *settings)
{
	int i;
	gres_state_t *gres_state_step;
	gres_step_state_t *gres_ss;
	bitstr_t *devices;

	if (!step_gres_list)
		return;

	xassert(gres_context_cnt >= 0);
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
	xassert(gres_context_cnt >= 0);
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
		strtok(tmp,"+");
		tok = strtok_r(tmp, ",", &save_ptr);
		while (tok) {
			if ((mult = strchr(tok, '*')))
				task_mult = atoi(mult + 1);
			else
				task_mult = 1;
			if (task_mult == 0) {
				error("Repetition count of 0 not allowed in gres binding mask, using 1 instead");
				task_mult = 1;
			}
			if ((local_proc_id >= task_offset) &&
			    (local_proc_id <= (task_offset + task_mult - 1))) {
				value = strtol(tok, NULL, 0);
				usable_gres = bit_alloc(bitmap_size);
				if ((value < min) || (value > max)) {
					error("Invalid map or mask value specified.");
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
			bit_consolidate(usable_gres);
		}
	} else {
		bit_and(usable_gres, gres_bit_alloc);
	}

	return usable_gres;
}

static void _accumulate_step_gres_alloc(gres_state_t *gres_state_step,
					bitstr_t **gres_bit_alloc,
					uint64_t *gres_cnt,
					uint64_t **gres_per_bit)
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
	if (gres_per_bit &&
	    gres_ss->gres_per_bit_alloc &&
	    gres_ss->gres_per_bit_alloc[0] &&
	    gres_ss->gres_bit_alloc &&
	    gres_ss->gres_bit_alloc[0]) {
		if (!*gres_per_bit)
			*gres_per_bit = xcalloc(
				bit_size(gres_ss->gres_bit_alloc[0]),
				sizeof(uint64_t));
		for (int i = 0; i < bit_size(gres_ss->gres_bit_alloc[0]); i++) {
			(*gres_per_bit)[i] += gres_ss->gres_per_bit_alloc[0][i];
		}
	}
}

static void _filter_gres_per_task(bitstr_t *test_gres,
				  bitstr_t *gres_bit_avail,
				  bitstr_t *usable_gres,
				  uint64_t *gres_needed,
				  bool set_usable_gres)
{
	for (int bit = 0;
	     *gres_needed && (bit = bit_ffs_from_bit(test_gres, bit)) >= 0;
	     bit++) {
		(*gres_needed)--;
		bit_clear(gres_bit_avail, bit);
		if (set_usable_gres)
			bit_set(usable_gres, bit);
	}
}

/*
 * Given a required gres_per_task count, determine which gres should be assigned
 * to this task. Prefer gres with cpu affinity that match the task.
 *
 * RET usable_gres
 */
static bitstr_t *_get_gres_per_task(bitstr_t *gres_bit_alloc,
				    uint64_t gres_per_task,
				    stepd_step_rec_t *step,
				    uint32_t plugin_id,
				    int local_proc_id)
{
	uint64_t gres_needed;
	bitstr_t *usable_gres, *gres_bit_avail;

	usable_gres = bit_alloc(bit_size(gres_bit_alloc));
	gres_bit_avail = bit_copy(gres_bit_alloc);

	/*
	 * We must determine what the previous tasks are taking first to know
	 * which gres are available to be assigned to this task.
	 */
	for (int i = 0; i <= local_proc_id; i++) {
		gres_needed = gres_per_task;

		/* First: Try to select device with with cpu affinity */
		if (gres_needed) {
			bitstr_t *closest_gres = _get_closest_usable_gres(
				plugin_id, gres_bit_avail,
				step->task[i]->cpu_set);
			_filter_gres_per_task(closest_gres, gres_bit_avail,
					      usable_gres, &gres_needed,
					      (i == local_proc_id));
			FREE_NULL_BITMAP(closest_gres);
		}

		/* Second: Select any available device */
		if (gres_needed)
			_filter_gres_per_task(gres_bit_avail, gres_bit_avail,
					      usable_gres, &gres_needed,
					      (i == local_proc_id));

		if (gres_needed) {
			error("Not enough gres to bind %"PRIu64" per task",
			      gres_per_task);
			break;
		}
	}
	FREE_NULL_BITMAP(gres_bit_avail);
	return usable_gres;
}

static void _filter_shared_gres_per_task(bitstr_t *test_gres,
					 bitstr_t *usable_gres,
					 uint64_t *gres_per_bit_avail,
					 uint64_t *gres_needed,
					 bool use_single_dev,
					 bool set_usable_gres)
{
	for (int bit = 0;
	     *gres_needed && (bit = bit_ffs_from_bit(test_gres, bit)) >= 0;
	     bit++) {
		uint64_t dec = MIN(gres_per_bit_avail[bit], *gres_needed);

		if (dec < (use_single_dev ? *gres_needed : 1))
			continue;

		gres_per_bit_avail[bit] -= dec;
		*gres_needed -= dec;

		if (set_usable_gres)
			bit_set(usable_gres, bit);
	}
}

/*
 * Given a required gres_per_task count, determine which shared gres should be
 * assigned to this task. Prefer gres with core affinity that match the task
 * and prefer allocating shared gres belonging to a single device if possible.
 */
static bitstr_t *_get_shared_gres_per_task(bitstr_t *gres_bit_alloc,
					   uint64_t *gres_per_bit,
					   uint64_t gres_per_task,
					   stepd_step_rec_t *step,
					   uint32_t sharing_plugin_id,
					   int local_proc_id)
{
	uint64_t gres_needed;
	bitstr_t *usable_gres, *closest_gres;
	uint64_t *gres_per_bit_avail;

	usable_gres = bit_alloc(bit_size(gres_bit_alloc));
	gres_per_bit_avail = xcalloc(bit_size(gres_bit_alloc),
				     sizeof(uint64_t));
	memcpy(gres_per_bit_avail, gres_per_bit,
	       bit_size(gres_bit_alloc) * sizeof(uint64_t));

	/*
	 * We must determine what the previous tasks are taking first to know
	 * which gres are available to be assigned to this task.
	 */
	for (int i = 0; i <= local_proc_id; i++) {
		closest_gres = _get_closest_usable_gres(sharing_plugin_id,
							gres_bit_alloc,
							step->task[i]->cpu_set);

		gres_needed = gres_per_task;

		/*
		 * Compare this selection priority with _set_shared_task_bits()
		 * in gres_select_filter.c
		 *
		 * First: Get a single device with core affinity with sufficient
		 *	available shared gres.
		 * Second: Get a single device with sufficient available shared
		 *	gres
		 * Third: Get devices with core affinity with any available
		 *	shared gres
		 * Fourth: Get devices with any available shared gres
		 */
		if (gres_needed)
			_filter_shared_gres_per_task(closest_gres, usable_gres,
						     gres_per_bit_avail,
						     &gres_needed, true,
						     (i == local_proc_id));
		if (gres_needed)
			_filter_shared_gres_per_task(gres_bit_alloc,
						     usable_gres,
						     gres_per_bit_avail,
						     &gres_needed, true,
						     (i == local_proc_id));
		if (gres_needed)
			_filter_shared_gres_per_task(closest_gres, usable_gres,
						     gres_per_bit_avail,
						     &gres_needed, false,
						     (i == local_proc_id));
		if (gres_needed)
			_filter_shared_gres_per_task(gres_bit_alloc,
						     usable_gres,
						     gres_per_bit_avail,
						     &gres_needed, false,
						     (i == local_proc_id));
		FREE_NULL_BITMAP(closest_gres);
		if (gres_needed) {
			error("Not enough shared gres to bind %"PRIu64" per task",
			      gres_per_task);
			break;
		}
	}
	xfree(gres_per_bit_avail);
	return usable_gres;
}

/* Convert old binding options to current gres binding format
 *
 * IN accel_bind_type - GRES binding options (old format, a bitmap)
 * IN/OUT tres_bind_str - TRES binding directives (new format, a string)
 */
static void _parse_accel_bind_type(uint16_t accel_bind_type, char *tres_bind_str)
{
	if (accel_bind_type & ACCEL_BIND_CLOSEST_GPU) {
		xstrfmtcat(tres_bind_str, "%sgres/gpu:closest",
			   tres_bind_str ? "+" : "");
	}
	if (accel_bind_type & ACCEL_BIND_CLOSEST_NIC) {
		xstrfmtcat(tres_bind_str, "%sgres/nic:closest",
			   tres_bind_str ? "+" : "");
	}
}

static int _get_usable_gres(int context_inx, int proc_id,
			    char *tres_bind_str, bitstr_t **usable_gres_ptr,
			    bitstr_t *gres_bit_alloc,  bool get_devices,
			    stepd_step_rec_t *step, uint64_t *gres_per_bit,
			    gres_internal_flags_t *flags)
{
	char *tres_name = NULL, *sep;
	bitstr_t *usable_gres = NULL;
	uint32_t plugin_id = gres_context[context_inx].plugin_id;
	*usable_gres_ptr = NULL;

	if (!gres_bit_alloc || !tres_bind_str)
		return SLURM_SUCCESS;

	tres_name = xstrdup_printf("gres/%s:",
				   gres_context[context_inx].gres_name);
	sep = xstrstr(tres_bind_str, tres_name);
	if (!sep) {
		xfree(tres_name);
		return SLURM_SUCCESS;
	}
	sep += strlen(tres_name);
	xfree(tres_name);

	if (!xstrncasecmp(sep, "verbose,", 8)){
		sep += 8;
		if (flags)
			*flags |= GRES_INTERNAL_FLAG_VERBOSE;
	}

	if (step->flags & LAUNCH_GRES_ALLOW_TASK_SHARING) {
		if (get_devices)
			return SLURM_SUCCESS;
		/*
		* Overwrite device index setting to use the global node/job GRES
		* index, rather than the index local to the task. This ensures
		* that the GRES environment variable is set correctly on the
		* task when multiple devices are constrained to the task, and
		* only the environment variables are bound to specific GRES.
		*/
		use_local_index = false;
		dev_index_mode_set = true;

		/*
		 * Consolidate allocated gres bitstring so that we get the GRES
		 * device index of the GRES within the context of the job, and
		 * not within the context of the whole node, unless specifically
		 * required with the GRES_CONF_GLOBAL_INDEX flag.
		 */
		if (!(gres_context[context_inx].config_flags &
		      GRES_CONF_GLOBAL_INDEX))
			bit_consolidate(gres_bit_alloc);
	}

	if (gres_context[context_inx].config_flags & GRES_CONF_GLOBAL_INDEX) {
		use_local_index = false;
		dev_index_mode_set = true;
	}

	if (!gres_id_shared(gres_context[context_inx].config_flags)) {
		if (!xstrncasecmp(sep, "map_gpu:", 8)) { // Old Syntax
			usable_gres = _get_usable_gres_map_or_mask(
				(sep + 8), proc_id, gres_bit_alloc,
				true, get_devices);
		} else if (!xstrncasecmp(sep, "mask_gpu:", 9)) { // Old Syntax
			usable_gres = _get_usable_gres_map_or_mask(
				(sep + 9), proc_id, gres_bit_alloc,
				false, get_devices);
		} else if (!xstrncasecmp(sep, "map:", 4)) {
			usable_gres = _get_usable_gres_map_or_mask(
				(sep + 4), proc_id, gres_bit_alloc,
				true, get_devices);
		} else if (!xstrncasecmp(sep, "mask:", 5)) {
			usable_gres = _get_usable_gres_map_or_mask(
				(sep + 5), proc_id, gres_bit_alloc,
				false, get_devices);
		} else if (!xstrncasecmp(sep, "single:", 7)) {
			if (!get_devices && gres_use_local_device_index()) {
				usable_gres = bit_alloc(
					bit_size(gres_bit_alloc));
				bit_set(usable_gres, 0);
			} else {
				usable_gres = _get_single_usable_gres(
					context_inx, slurm_atoul(sep + 7),
					proc_id, step, gres_bit_alloc);
			}
		} else if (!xstrncasecmp(sep, "closest", 7)) {
			usable_gres = _get_closest_usable_gres(
				plugin_id, gres_bit_alloc,
				step->task[proc_id]->cpu_set);
			if (!get_devices && gres_use_local_device_index())
				bit_consolidate(usable_gres);
		} else if (!xstrncasecmp(sep, "per_task:", 9)) {
			if (!get_devices && gres_use_local_device_index()) {
				usable_gres = bit_alloc(
					bit_size(gres_bit_alloc));
				bit_nset(usable_gres, 0,
					 slurm_atoul(sep + 9) - 1);
			} else {
				usable_gres = _get_gres_per_task(
					gres_bit_alloc, slurm_atoul(sep + 9),
					step, plugin_id, proc_id);
			}
		} else if (!xstrncasecmp(sep, "none", 4)) {
			usable_gres = bit_copy(gres_bit_alloc);
		} else
			return SLURM_ERROR;
	} else { // Shared gres only support per_task binding for now
		if (!xstrncasecmp(sep, "per_task:", 9)) {
			usable_gres = _get_shared_gres_per_task(
				gres_bit_alloc, gres_per_bit,
				slurm_atoul(sep + 9),
				step, gpu_plugin_id, proc_id);
			if (!get_devices && gres_use_local_device_index())
				bit_consolidate(usable_gres);
		} else if (!xstrncasecmp(sep, "none", 4)) {
			usable_gres = bit_copy(gres_bit_alloc);
		} else
			return SLURM_ERROR;
	}

	if (usable_gres && !bit_set_count(usable_gres)) {
		error("Bind request %s does not specify any devices within the allocation for task %d. Binding to the first device in the allocation instead.",
		      tres_bind_str, proc_id);
		if (!get_devices && gres_use_local_device_index())
			bit_set(usable_gres, 0);
		else
			bit_set(usable_gres, bit_ffs(gres_bit_alloc));
	}

	*usable_gres_ptr = usable_gres;

	return SLURM_SUCCESS;
}

/*
 * Set environment as required for all tasks of a job step
 */
extern void gres_g_step_set_env(stepd_step_rec_t *step)
{
	int i;
	bitstr_t *gres_bit_alloc = NULL;
	gres_internal_flags_t flags = GRES_INTERNAL_FLAG_NONE;
	foreach_gres_accumulate_device_t foreach_gres_accumulate_device = {
		.gres_bit_alloc = &gres_bit_alloc,
		.is_job = false,
	};

	xassert(gres_context_cnt >= 0);
	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		slurm_gres_context_t *gres_ctx = &gres_context[i];
		if (!gres_ctx->ops.step_set_env)
			continue;	/* No plugin to call */
		if (!step->step_gres_list) {
			/* Clear GRES environment variables */
			(*(gres_ctx->ops.step_set_env))(
				&step->env, NULL, 0, GRES_INTERNAL_FLAG_NONE);
			continue;
		}
		foreach_gres_accumulate_device.plugin_id = gres_ctx->plugin_id;
		(void) list_for_each(step->step_gres_list,
				     _accumulate_gres_device,
				     &foreach_gres_accumulate_device);

		/*
		 * Do not let MPS or Shard (shared GRES) clear any envs set for
		 * a GPU (sharing GRES) when a GPU is allocated but an
		 * MPS/Shard is not. Sharing GRES plugins always run before
		 * shared GRES, so we don't need to protect MPS/Shard from GPU.
		 */
		if (gres_id_shared(gres_ctx->config_flags) &&
		    foreach_gres_accumulate_device.sharing_gres_allocated)
			flags |= GRES_INTERNAL_FLAG_PROTECT_ENV;

		(*(gres_ctx->ops.step_set_env))(
			&step->env,
			gres_bit_alloc,
			foreach_gres_accumulate_device.gres_cnt,
			flags);
		foreach_gres_accumulate_device.gres_cnt = 0;
		FREE_NULL_BITMAP(gres_bit_alloc);
	}
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Change the task's inherited environment (from the step, and set by
 * gres_g_step_set_env()). Use this to implement GPU task binding.
 */
extern void gres_g_task_set_env(stepd_step_rec_t *step, int local_proc_id)
{
	int i;
	bitstr_t *usable_gres = NULL;
	bitstr_t *gres_bit_alloc = NULL;
	uint64_t *gres_per_bit = NULL;
	foreach_gres_accumulate_device_t foreach_gres_accumulate_device = {
		.gres_bit_alloc = &gres_bit_alloc,
		.gres_per_bit = &gres_per_bit,
		.is_job = false,
	};

	if (step->accel_bind_type)
		_parse_accel_bind_type(step->accel_bind_type, step->tres_bind);

	xassert(gres_context_cnt >= 0);
	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		gres_internal_flags_t flags = GRES_INTERNAL_FLAG_NONE;
		slurm_gres_context_t *gres_ctx = &gres_context[i];
		if (!gres_ctx->ops.task_set_env)
			continue;	/* No plugin to call */
		if (!step->step_gres_list) {
			/* Clear GRES environment variables */
			(*(gres_ctx->ops.task_set_env))(
				&step->envtp->env, NULL, 0, NULL,
				GRES_INTERNAL_FLAG_NONE);
			continue;
		}
		foreach_gres_accumulate_device.plugin_id = gres_ctx->plugin_id;
		(void) list_for_each(step->step_gres_list,
				     _accumulate_gres_device,
				     &foreach_gres_accumulate_device);

		if (_get_usable_gres(i, local_proc_id, step->tres_bind,
				     &usable_gres, gres_bit_alloc, false, step,
				     gres_per_bit, &flags) == SLURM_ERROR) {
			goto next;
		}

		/*
		 * Do not let MPS or Shard (shared GRES) clear any envs set for
		 * a GPU (sharing GRES) when a GPU is allocated but an
		 * MPS/Shard is not. Sharing GRES plugins always run before
		 * shared GRES, so we don't need to protect MPS/Shard from GPU.
		 */
		if (gres_id_shared(gres_ctx->config_flags) &&
		    foreach_gres_accumulate_device.sharing_gres_allocated)
			flags |= GRES_INTERNAL_FLAG_PROTECT_ENV;

		(*(gres_ctx->ops.task_set_env))(
			&step->envtp->env,
			gres_bit_alloc,
			foreach_gres_accumulate_device.gres_cnt,
			usable_gres, flags);
	next:
		foreach_gres_accumulate_device.gres_cnt = 0;
		xfree(gres_per_bit);
		FREE_NULL_BITMAP(gres_bit_alloc);
		FREE_NULL_BITMAP(usable_gres);
	}
	slurm_mutex_unlock(&gres_context_lock);
}

static void _step_state_log_node(gres_step_state_t *gres_ss, int i)
{
	char tmp_str[128];
	if (gres_ss->gres_bit_alloc[i]) {
		bit_fmt(tmp_str, sizeof(tmp_str), gres_ss->gres_bit_alloc[i]);
		info("  gres_bit_alloc[%d]:%s of %d", i, tmp_str,
		     (int)bit_size(gres_ss->gres_bit_alloc[i]));
	} else
		info("  gres_bit_alloc[%d]:NULL", i);

	if (gres_ss->gres_per_bit_alloc && gres_ss->gres_per_bit_alloc[i]) {
		for (int j = 0;
		     (j = bit_ffs_from_bit(gres_ss->gres_bit_alloc[i], j)) >= 0;
		     j++) {
			info("  gres_per_bit_alloc[%d][%d]:%" PRIu64, i, j,
			     gres_ss->gres_per_bit_alloc[i][j]);
		}
	}
}

static int _step_state_log(void *x, void *arg)
{
	gres_state_t *gres_state_step = x;
	gres_step_state_t *gres_ss = gres_state_step->gres_data;
	char *gres_name = gres_state_step->gres_name;
	slurm_step_id_t *step_id = arg;
	int i;

	xassert(gres_ss);
	info("gres:%s type:%s(%u) %ps flags:%s state", gres_name,
	     gres_ss->type_name, gres_ss->type_id, step_id,
	     gres_flags2str(gres_ss->flags));
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
			if (bit_test(gres_ss->node_in_use, i))
				_step_state_log_node(gres_ss, i);
		}
	}

	return 0;
}

/*
 * Log a step's current gres state
 * IN gres_list - generated by gres_stepmgr_step_alloc()
 * IN job_id - job's ID
 * IN step_id - step's ID
 */
extern void gres_step_state_log(list_t *gres_list, uint32_t job_id,
				uint32_t step_id)
{
	slurm_step_id_t tmp_step_id = {
		.job_id = job_id,
		.step_het_comp = NO_VAL,
		.step_id = step_id,
	};

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_GRES) || !gres_list)
		return;

	(void) list_for_each(gres_list, _step_state_log, &tmp_step_id);
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

static int _foreach_node_count(void *x, void *arg)
{
	gres_state_t *gres_state_node = x;
	foreach_node_count_t *foreach_node_count = arg;
	gres_node_state_t *gres_ns = gres_state_node->gres_data;
	uint64_t val = 0;

	xassert(gres_ns);

	switch (foreach_node_count->val_type) {
	case GRES_VAL_TYPE_FOUND:
		val = gres_ns->gres_cnt_found;
		break;
	case GRES_VAL_TYPE_CONFIG:
		val = gres_ns->gres_cnt_config;
		break;
	case GRES_VAL_TYPE_AVAIL:
		val = gres_ns->gres_cnt_avail;
		break;
	case GRES_VAL_TYPE_ALLOC:
		val = gres_ns->gres_cnt_alloc;
		break;
	}

	foreach_node_count->gres_count_ids[foreach_node_count->index] =
		gres_state_node->plugin_id;
	foreach_node_count->gres_count_vals[foreach_node_count->index] = val;

	if (++foreach_node_count->index >= foreach_node_count->array_len)
		return -1;
	return 0;
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
extern int gres_node_count(list_t *gres_list, int arr_len,
			   uint32_t *gres_count_ids,
			   uint64_t *gres_count_vals,
			   int val_type)
{
	foreach_node_count_t foreach_node_count = {
		.array_len = arr_len,
		.gres_count_ids = gres_count_ids,
		.gres_count_vals = gres_count_vals,
		.val_type = val_type,
	};

	if (arr_len <= 0)
		return EINVAL;

	(void) list_for_each(gres_list, _foreach_node_count,
			     &foreach_node_count);

	return SLURM_SUCCESS;
}
static void _gres_device_pack(
	void *in, uint16_t protocol_version, buf_t *buffer)
{
	gres_device_t *gres_device = in;

	/* DON'T PACK gres_device->alloc */
	pack32(gres_device->index, buffer);
	pack32(gres_device->dev_num, buffer);
	pack32(gres_device->dev_desc.type, buffer);
	pack32(gres_device->dev_desc.major, buffer);
	pack32(gres_device->dev_desc.minor, buffer);
	packstr(gres_device->path, buffer);
	packstr(gres_device->unique_id, buffer);
}

extern void gres_send_stepd(buf_t *buffer, list_t *gres_devices)
{
	slurm_pack_list(gres_devices, _gres_device_pack, buffer,
			SLURM_PROTOCOL_VERSION);
}

static int _gres_device_unpack(void **object, uint16_t protocol_version,
			       buf_t *buffer)
{
	uint32_t uint32_tmp = 0;
	gres_device_t *gres_device = xmalloc(sizeof(gres_device_t));

	safe_unpack32(&uint32_tmp, buffer);
	gres_device->index = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	gres_device->dev_num = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	gres_device->dev_desc.type = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	gres_device->dev_desc.major = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	gres_device->dev_desc.minor = uint32_tmp;
	safe_unpackstr(&gres_device->path, buffer);
	safe_unpackstr(&gres_device->unique_id, buffer);
	/* info("adding %d %s %s", gres_device->dev_num, */
	/*      gres_device->major, gres_device->path); */

	*object = gres_device;

	return SLURM_SUCCESS;

unpack_error:
	error("%s: failed", __func__);
	destroy_gres_device(gres_device);
	return SLURM_ERROR;
}

extern void gres_recv_stepd(buf_t *buffer, list_t **gres_devices)
{
	(void) slurm_unpack_list(gres_devices, _gres_device_unpack,
				 destroy_gres_device,
				 buffer, SLURM_PROTOCOL_VERSION);
}

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void gres_g_send_stepd(int fd, slurm_msg_t *msg)
{
	int len;
	uint32_t step_id;
	cred_data_enum_t check;
	slurm_cred_t *cred = NULL;

	/* Setup the gres_device list and other plugin-specific data */
	xassert(gres_context_cnt >= 0);

	slurm_mutex_lock(&gres_context_lock);
	xassert(gres_context_buf);

	len = get_buf_offset(gres_context_buf);
	safe_write(fd, &len, sizeof(len));
	safe_write(fd, get_buf_data(gres_context_buf), len);

	slurm_mutex_unlock(&gres_context_lock);

	if (msg->msg_type == REQUEST_BATCH_JOB_LAUNCH) {
		batch_job_launch_msg_t *job = msg->data;
		step_id = SLURM_BATCH_SCRIPT;
		cred = job->cred;
	} else {
		launch_tasks_request_msg_t *job = msg->data;
		step_id = job->step_id.step_id;
		cred = job->cred;
	}

	/* If we are a special step we get the JOB_GRES_LIST */
	if (step_id >= SLURM_MAX_NORMAL_STEP_ID)
		check = CRED_DATA_JOB_GRES_LIST;
	else
		check = CRED_DATA_STEP_GRES_LIST;
	/* Send the merged slurm.conf/gres.conf and autodetect data */
	if (slurm_cred_get(cred, check)) {
		len = get_buf_offset(gres_conf_buf);
		safe_write(fd, &len, sizeof(len));
		safe_write(fd, get_buf_data(gres_conf_buf), len);
	}

	return;
rwfail:
	error("%s: failed", __func__);
	slurm_mutex_unlock(&gres_context_lock);

	return;
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern int gres_g_recv_stepd(int fd, slurm_msg_t *msg)
{
	int len, rc = SLURM_ERROR;
	buf_t *buffer = NULL;
	uint32_t step_id;
	cred_data_enum_t check;
	slurm_cred_t *cred = NULL;

	slurm_mutex_lock(&gres_context_lock);

	safe_read(fd, &len, sizeof(int));

	buffer = init_buf(len);
	safe_read(fd, buffer->head, len);

	rc = _unpack_context_buf(buffer);

	if (rc == SLURM_ERROR)
		goto rwfail;

	FREE_NULL_BUFFER(buffer);

	if (msg->msg_type == REQUEST_BATCH_JOB_LAUNCH) {
		batch_job_launch_msg_t *job = msg->data;
		step_id = SLURM_BATCH_SCRIPT;
		cred = job->cred;
	} else {
		launch_tasks_request_msg_t *job = msg->data;
		step_id = job->step_id.step_id;
		cred = job->cred;
	}

	/* If we are a special step we get the JOB_GRES_LIST */
	if (step_id >= SLURM_MAX_NORMAL_STEP_ID)
		check = CRED_DATA_JOB_GRES_LIST;
	else
		check = CRED_DATA_STEP_GRES_LIST;
	/* Recv the merged slurm.conf/gres.conf and autodetect data */
	if (slurm_cred_get(cred, check)) {
		safe_read(fd, &len, sizeof(int));

		buffer = init_buf(len);
		safe_read(fd, buffer->head, len);

		rc = _unpack_gres_conf(buffer);

		if (rc == SLURM_ERROR)
			goto rwfail;

		FREE_NULL_BUFFER(buffer);
	}
	slurm_mutex_unlock(&gres_context_lock);

	/* Set debug flags only */
	(void) gres_init();

	rc = _load_specific_gres_plugins();

	return rc;
rwfail:
	FREE_NULL_BUFFER(buffer);
	error("%s: failed", __func__);
	slurm_mutex_unlock(&gres_context_lock);

	/* Set debug flags only */
	(void) gres_init();

	rc = _load_specific_gres_plugins();

	return rc;
}

/* Get generic GRES data types here. Call the plugin for others */
static int _get_step_info(gres_step_state_t *gres_ss,
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

	switch (data_type) {
	case GRES_STEP_DATA_COUNT:
		*u64_data += gres_ss->gres_cnt_node_alloc[node_inx];
		break;
	case GRES_STEP_DATA_BITMAP:
		if (gres_ss->gres_bit_alloc) {
			if (!*bit_data) {
				*bit_data = bit_copy(
					gres_ss->gres_bit_alloc[node_inx]);
			} else {
				xassert(bit_size(*bit_data) ==
					bit_size(gres_ss->gres_bit_alloc[
							 node_inx]));
				bit_or(*bit_data,
				       gres_ss->gres_bit_alloc[node_inx]);
			}
		}
		break;
	default:
		error("%s: unknown enum given %d", __func__, data_type);
		rc = EINVAL;
		break;
	}

	return rc;
}

static int _foreach_get_step_info(void *x, void *arg)
{
	gres_state_t *gres_state_step = x;
	foreach_step_info_t *foreach_step_info = arg;

	if (gres_state_step->plugin_id != foreach_step_info->plugin_id)
		return 0;

	foreach_step_info->rc = _get_step_info(gres_state_step->gres_data,
					       foreach_step_info->node_inx,
					       foreach_step_info->data_type,
					       foreach_step_info->data);
	if (foreach_step_info->rc != SLURM_SUCCESS)
		return -1;
	return 0;
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
extern int gres_get_step_info(list_t *step_gres_list, char *gres_name,
			      uint32_t node_inx,
			      enum gres_step_data_type data_type, void *data)
{
	foreach_step_info_t foreach_step_info = {
		.data = data,
		.data_type = data_type,
		.node_inx = node_inx,
		.rc = ESLURM_INVALID_GRES,
	};
	if (data == NULL)
		return EINVAL;
	if (step_gres_list == NULL)	/* No GRES allocated */
		return ESLURM_INVALID_GRES;

	xassert(gres_context_cnt >= 0);
	foreach_step_info.plugin_id = gres_build_id(gres_name);

	(void) list_for_each(step_gres_list, _foreach_get_step_info,
			     &foreach_step_info);

	return foreach_step_info.rc;
}

extern uint32_t gres_get_autodetect_flags(void)
{
	return autodetect_flags;
}

extern void gres_clear_tres_cnt(uint64_t *tres_cnt, bool locked)
{
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	/*
	 * If gres_context_lock is ever locked/unlocked here, it should happen
	 * in between assoc_mgr_lock() and before assoc_mgr_unlock().
	 */

	if (!locked)
		assoc_mgr_lock(&locks);

	/* Initialize all GRES counters to zero. Increment them later. */
	for (int i = 0; i < g_tres_count; ++i) {
		/* Skip all non-GRES TRES */
		if (xstrcasecmp(assoc_mgr_tres_array[i]->type, "gres"))
			continue;
		tres_cnt[i] = 0;
	}

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

	if (config_flags & GRES_CONF_EXPLICIT) {
		strcat(flag_str, sep);
		strcat(flag_str, "Explicit");
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
extern void add_gres_to_list(list_t *gres_list,
			     gres_slurmd_conf_t *gres_slurmd_conf_in)
{
	gres_slurmd_conf_t *gres_slurmd_conf;
	bool use_empty_first_record = false;

	/*
	 * If the first record already exists and has a count of 0 then
	 * overwrite it.
	 * This is a placeholder record created in _merge_config()
	 */
	gres_slurmd_conf = list_peek(gres_list);
	if (gres_slurmd_conf && (gres_slurmd_conf->count == 0))
		use_empty_first_record = true;
	else
		gres_slurmd_conf = xmalloc(sizeof(gres_slurmd_conf_t));
	gres_slurmd_conf->cpu_cnt = gres_slurmd_conf_in->cpu_cnt;
	if (gres_slurmd_conf_in->cpus_bitmap) {
		bitstr_t *cpu_aff = bit_copy(gres_slurmd_conf_in->cpus_bitmap);

		/*
		 * Size down (or possibly up) cpus_bitmap, if necessary, so that
		 * the size of cpus_bitmap for system-detected devices matches
		 * the size of cpus_bitmap for configured devices.
		 */
		if (bit_size(cpu_aff) != gres_slurmd_conf_in->cpu_cnt) {
			/* Calculate minimum size to hold CPU affinity */
			int64_t size = bit_fls(cpu_aff) + 1;
			if (size > gres_slurmd_conf_in->cpu_cnt) {
				char *cpu_str = bit_fmt_hexmask_trim(cpu_aff);
				fatal("This CPU affinity bitmask (%s) does not fit within the CPUs configured for this node (%d). Make sure that the node's CPU count is configured correctly.",
				      cpu_str, gres_slurmd_conf_in->cpu_cnt);
				xfree(cpu_str);
			}
			bit_realloc(cpu_aff, gres_slurmd_conf_in->cpu_cnt);
		}
		gres_slurmd_conf->cpus_bitmap = cpu_aff;
	}

	/* Set default env flags, if necessary */
	if ((gres_slurmd_conf_in->config_flags & GRES_CONF_ENV_DEF) &&
	    ((gres_slurmd_conf_in->config_flags & GRES_CONF_ENV_SET) !=
	     GRES_CONF_ENV_SET))
		gres_slurmd_conf_in->config_flags |= GRES_CONF_ENV_SET;

	gres_slurmd_conf->config_flags = gres_slurmd_conf_in->config_flags;

	if (gres_slurmd_conf_in->file) {
		hostlist_t *hl = hostlist_create(gres_slurmd_conf_in->file);
		gres_slurmd_conf->config_flags |= GRES_CONF_HAS_FILE;
		if (hostlist_count(hl) > 1)
			gres_slurmd_conf->config_flags |= GRES_CONF_HAS_MULT;
		hostlist_destroy(hl);
	}
	if (gres_slurmd_conf_in->type_name)
		gres_slurmd_conf->config_flags |= GRES_CONF_HAS_TYPE;
	gres_slurmd_conf->cpus = xstrdup(gres_slurmd_conf_in->cpus);
	gres_slurmd_conf->type_name = xstrdup(gres_slurmd_conf_in->type_name);
	gres_slurmd_conf->name = xstrdup(gres_slurmd_conf_in->name);
	gres_slurmd_conf->file = xstrdup(gres_slurmd_conf_in->file);
	gres_slurmd_conf->links = xstrdup(gres_slurmd_conf_in->links);
	gres_slurmd_conf->unique_id = xstrdup(gres_slurmd_conf_in->unique_id);
	gres_slurmd_conf->count = gres_slurmd_conf_in->count;
	gres_slurmd_conf->plugin_id = gres_build_id(gres_slurmd_conf_in->name);
	if (!use_empty_first_record)
		list_append(gres_list, gres_slurmd_conf);
}

extern char *gres_prepend_tres_type(const char *gres_str)
{
	char *output = NULL;

	if (gres_str) {
		output = xstrdup_printf("gres/%s", gres_str);
		xstrsubstituteall(output, ",", ",gres/");
		xstrsubstituteall(output, "gres/gres/", "gres/");
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

/* Return the plugin id made from gres_build_id("gpu") */
extern uint32_t gres_get_gpu_plugin_id(void)
{
	return gpu_plugin_id;
}

extern bool gres_valid_name(char *name)
{
	if (!name || (name[0] == '\0'))
		return false;
	if (gres_get_system_cnt(name, false) != NO_VAL64)
		return true;

	return false;
}
