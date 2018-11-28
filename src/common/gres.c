/*****************************************************************************\
 *  gres.c - driver for gres plugin
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2014-2018 SchedMD LLC
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
#define SCHED_GETAFFINITY_THREE_ARGS
#endif

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
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

/* Gres symbols provided by the plugin */
typedef struct slurm_gres_ops {
	int		(*node_config_load)	( List gres_conf_list );
	void		(*job_set_env)		( char ***job_env_ptr,
						  void *gres_ptr, int node_inx );
	void		(*step_set_env)		( char ***job_env_ptr,
						  void *gres_ptr );
	void		(*step_reset_env)	( char ***job_env_ptr,
						  void *gres_ptr,
						  bitstr_t *usable_gres );
	void		(*send_stepd)		( int fd );
	void		(*recv_stepd)		( int fd );
	int		(*job_info)		( gres_job_state_t *job_gres_data,
						  uint32_t node_inx,
						  enum gres_job_data_type data_type,
						  void *data);
	int		(*step_info)		( gres_step_state_t *step_gres_data,
						  uint32_t node_inx,
						  enum gres_step_data_type data_type,
						  void *data);
	List            (*get_devices)		( void );
} slurm_gres_ops_t;

/* Gres plugin context, one for each gres type */
typedef struct slurm_gres_context {
	plugin_handle_t	cur_plugin;
	char *		gres_name;		/* name (e.g. "gpu") */
	char *		gres_name_colon;	/* name + colon (e.g. "gpu:") */
	int		gres_name_colon_len;	/* size of gres_name_colon */
	char *		gres_type;		/* plugin name (e.g. "gres/gpu") */
	bool		has_file;		/* found "File=" in slurm.conf */
	slurm_gres_ops_t ops;			/* pointers to plugin symbols */
	uint32_t	plugin_id;		/* key for searches */
	plugrack_t	plugin_list;		/* plugrack info */
	uint64_t        total_cnt;
} slurm_gres_context_t;

/* Generic gres data structure for adding to a list. Depending upon the
 * context, gres_data points to gres_node_state_t, gres_job_state_t or
 * gres_step_state_t */
typedef struct gres_state {
	uint32_t	plugin_id;
	void		*gres_data;
} gres_state_t;

typedef struct gres_search_key {
	uint32_t plugin_id;
	uint32_t type_id;
} gres_key_t;

/* Pointers to functions in src/slurmd/common/xcpuinfo.h that we may use */
typedef struct xcpuinfo_funcs {
	int (*xcpuinfo_abs_to_mac) (char *abs, char **mac);
} xcpuinfo_funcs_t;
xcpuinfo_funcs_t xcpuinfo_ops;

/* Local variables */
static int gres_context_cnt = -1;
static uint32_t gres_cpu_cnt = 0;
static bool gres_debug = false;
static slurm_gres_context_t *gres_context = NULL;
static char *gres_node_name = NULL;
static char *gres_plugin_list = NULL;
static pthread_mutex_t gres_context_lock = PTHREAD_MUTEX_INITIALIZER;
static List gres_conf_list = NULL;
static bool init_run = false;

/* Local functions */
static gres_node_state_t *
		_build_gres_node_state(void);
static uint32_t	_build_id(char *name);
static bitstr_t *_core_bitmap_rebuild(bitstr_t *old_core_bitmap, int new_size);
static void	_destroy_gres_slurmd_conf(void *x);
static int	_find_job_by_sock_gres(void *x, void *key);
static int	_find_sock_by_job_gres(void *x, void *key);
static void	_get_gres_cnt(gres_node_state_t *gres_data, char *orig_config,
			      char *gres_name, char *gres_name_colon,
			      int gres_name_colon_len);
static uint64_t	_get_tot_gres_cnt(uint32_t plugin_id, uint64_t *set_cnt);
static int	_gres_find_id(void *x, void *key);
static int	_gres_find_job_by_key(void *x, void *key);
static int	_gres_find_step_by_key(void *x, void *key);
static void	_gres_job_list_delete(void *list_element);
static int	_job_alloc(void *job_gres_data, void *node_gres_data,
			   int node_cnt, int node_offset, char *gres_name,
			   uint32_t job_id, char *node_name,
			   bitstr_t *core_bitmap);
static void	_job_core_filter(void *job_gres_data, void *node_gres_data,
				 bool use_total_gres, bitstr_t *core_bitmap,
				 int core_start_bit, int core_end_bit,
				 char *gres_name, char *node_name);
static int	_job_dealloc(void *job_gres_data, void *node_gres_data,
			     int node_offset, char *gres_name, uint32_t job_id,
			     char *node_name);
static void	_job_state_delete(void *gres_data);
static void *	_job_state_dup(void *gres_data);
static void *	_job_state_dup2(void *gres_data, int node_index);
static void	_job_state_log(void *gres_data, uint32_t job_id,
			       uint32_t plugin_id);
static uint32_t _job_test(void *job_gres_data, void *node_gres_data,
			  bool use_total_gres, bitstr_t *core_bitmap,
			  int core_start_bit, int core_end_bit, bool *topo_set,
			  uint32_t job_id, char *node_name, char *gres_name);
static int	_load_gres_plugin(char *plugin_name,
				  slurm_gres_context_t *plugin_context);
static int	_log_gres_slurmd_conf(void *x, void *arg);
static void	_my_stat(char *file_name);
static int	_node_config_init(char *node_name, char *orig_config,
				  slurm_gres_context_t *context_ptr,
				  gres_state_t *gres_ptr);
static char *	_node_gres_used(void *gres_data, char *gres_name);
static int	_node_reconfig(char *node_name, char *orig_config,
			       char **new_config, gres_state_t *gres_ptr,
			       uint16_t fast_schedule,
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
static void	_set_gres_cnt(char *orig_config, char **new_config,
			      uint64_t new_cnt, char *gres_name,
			      char *gres_name_colon, int gres_name_colon_len);
static void	_sock_gres_del(void *x);
static int	_step_alloc(void *step_gres_data, void *job_gres_data,
			    int node_offset, char *gres_name,
			    uint32_t job_id, uint32_t step_id);
static int	_step_dealloc(void *step_gres_data, void *job_gres_data,
			      char *gres_name, uint32_t job_id,
			      uint32_t step_id);
static void *	_step_state_dup(void *gres_data);
static void *	_step_state_dup2(void *gres_data, int node_index);
static void	_step_state_log(void *gres_data, uint32_t job_id,
				uint32_t step_id, char *gres_name);
static uint64_t	_step_test(void *step_gres_data, void *job_gres_data,
			   int node_offset, bool ignore_alloc, char *gres_name,
			   uint32_t job_id, uint32_t step_id);
static int	_unload_gres_plugin(slurm_gres_context_t *plugin_context);
static void	_validate_config(slurm_gres_context_t *context_ptr);
static int	_validate_file(char *path_name, char *gres_name);
static void	_validate_links(gres_slurmd_conf_t *p);
static void	_validate_gres_node_cores(gres_node_state_t *node_gres_ptr,
					  int cpus_ctld, char *node_name);
static int	_valid_gres_type(char *gres_name, gres_node_state_t *gres_data,
				 uint16_t fast_schedule, char **reason_down);

/*
 * Convert a GRES name or model into a number for faster comparision operations
 */
static uint32_t	_build_id(char *name)
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
			debug("%s: couldn't find name (%s)", __func__, name);
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

static int _load_gres_plugin(char *plugin_name,
			     slurm_gres_context_t *plugin_context)
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
	};
	int n_syms = sizeof(syms) / sizeof(char *);

	/* Find the correct plugin */
	plugin_context->gres_type	= xstrdup("gres/");
	xstrcat(plugin_context->gres_type, plugin_name);
	plugin_context->plugin_list	= NULL;
	plugin_context->cur_plugin	= PLUGIN_INVALID_HANDLE;

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
		char *plugin_dir;
		plugin_context->plugin_list = plugrack_create();
		if (plugin_context->plugin_list == NULL) {
			error("gres: cannot create plugin manager");
			return SLURM_ERROR;
		}
		plugrack_set_major_type(plugin_context->plugin_list,
					"gres");
		plugrack_set_paranoia(plugin_context->plugin_list,
				      PLUGRACK_PARANOIA_NONE, 0);
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir(plugin_context->plugin_list, plugin_dir);
		xfree(plugin_dir);
	}

	plugin_context->cur_plugin = plugrack_use_by_type(
					plugin_context->plugin_list,
					plugin_context->gres_type );
	if (plugin_context->cur_plugin == PLUGIN_INVALID_HANDLE) {
		debug("Cannot find plugin of type %s, just track gres counts",
		      plugin_context->gres_type);
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
 * Initialize the gres plugin.
 *
 * Returns a Slurm errno.
 */
extern int gres_plugin_init(void)
{
	int i, j, rc = SLURM_SUCCESS;
	char *last = NULL, *names, *one_name, *full_name;

	if (init_run && (gres_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&gres_context_lock);
	if (slurm_get_debug_flags() & DEBUG_FLAG_GRES)
		gres_debug = true;
	else
		gres_debug = false;

	if (gres_context_cnt >= 0)
		goto fini;

	gres_plugin_list = slurm_get_gres_plugins();
	gres_context_cnt = 0;
	if ((gres_plugin_list == NULL) || (gres_plugin_list[0] == '\0'))
		goto fini;

	gres_context_cnt = 0;
	names = xstrdup(gres_plugin_list);
	one_name = strtok_r(names, ",", &last);
	while (one_name) {
		full_name = xstrdup("gres/");
		xstrcat(full_name, one_name);
		for (i=0; i<gres_context_cnt; i++) {
			if (!xstrcmp(full_name, gres_context[i].gres_type))
				break;
		}
		xfree(full_name);
		if (i<gres_context_cnt) {
			error("Duplicate plugin %s ignored",
			      gres_context[i].gres_type);
		} else {
			xrealloc(gres_context, (sizeof(slurm_gres_context_t) *
				 (gres_context_cnt + 1)));
			(void) _load_gres_plugin(one_name,
						 gres_context +
						 gres_context_cnt);
			/* Ignore return code.
			 * Proceed to support gres even without the plugin */
			gres_context[gres_context_cnt].gres_name =
				xstrdup(one_name);
			gres_context[gres_context_cnt].plugin_id =
				_build_id(one_name);
			gres_context_cnt++;
		}
		one_name = strtok_r(NULL, ",", &last);
	}
	xfree(names);

	/* Ensure that plugin_id is valid and unique */
	for (i=0; i<gres_context_cnt; i++) {
		for (j=i+1; j<gres_context_cnt; j++) {
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
	init_run = true;

fini:	slurm_mutex_unlock(&gres_context_lock);
	return rc;
}

/* Add a GRES record. This is used by the node_features plugin after the
 * slurm.conf file is read and the initial GRES records are built by
 * gres_plugin_init(). */
extern void gres_plugin_add(char *gres_name)
{
	int i;

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (!xstrcmp(gres_context[i].gres_name, gres_name))
			goto fini;
	}

	xrealloc(gres_context,
		 (sizeof(slurm_gres_context_t) * (gres_context_cnt + 1)));
	(void) _load_gres_plugin(gres_name, gres_context + gres_context_cnt);
	/* Ignore return code. Support gres even without the plugin */
	gres_context[gres_context_cnt].gres_name = xstrdup(gres_name);
	gres_context[gres_context_cnt].plugin_id =_build_id(gres_name);
	gres_context_cnt++;
fini:	slurm_mutex_unlock(&gres_context_lock);
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
	for (i=0; i<gres_context_cnt; i++) {
		j = _unload_gres_plugin(gres_context + i);
		if (j != SLURM_SUCCESS)
			rc = j;
	}
	xfree(gres_context);
	xfree(gres_plugin_list);
	FREE_NULL_LIST(gres_conf_list);
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
 * Provide a plugin-specific help message for salloc, sbatch and srun
 * IN/OUT msg - buffer provided by caller and filled in by plugin
 * IN msg_size - size of msg buffer in bytes
 *
 * NOTE: GRES "type" (e.g. model) information is only available from slurmctld
 * after slurmd registers. It is not readily available from srun (as used here).
 */
extern int gres_plugin_help_msg(char *msg, int msg_size)
{
	int i, rc;
	char *header = "Valid gres options are:\n";

	if (msg_size < 1)
		return EINVAL;

	msg[0] = '\0';
	rc = gres_plugin_init();

	if ((strlen(header) + 2) <= msg_size)
		strcat(msg, header);
	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		if ((strlen(msg) + strlen(gres_context[i].gres_name) + 9) >
		    msg_size)
 			break;
		strcat(msg, gres_context[i].gres_name);
		strcat(msg, "[[:type]:count]\n");
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Perform reconfig, re-read any configuration files
 * OUT did_change - set if gres configuration changed
 */
extern int gres_plugin_reconfig(bool *did_change)
{
	int rc = SLURM_SUCCESS;
	char *plugin_names = slurm_get_gres_plugins();
	bool plugin_change;


	if (did_change)
		*did_change = false;
	slurm_mutex_lock(&gres_context_lock);
	if (slurm_get_debug_flags() & DEBUG_FLAG_GRES)
		gres_debug = true;
	else
		gres_debug = false;

	if (xstrcmp(plugin_names, gres_plugin_list))
		plugin_change = true;
	else
		plugin_change = false;

	slurm_mutex_unlock(&gres_context_lock);

	if (plugin_change) {
		error("GresPlugins changed from %s to %s ignored",
		     gres_plugin_list, plugin_names);
		error("Restart the slurmctld daemon to change GresPlugins");
		if (did_change)
			*did_change = true;
#if 0
		/* This logic would load new plugins, but we need the old
		 * plugins to persist in order to process old state
		 * information. */
		rc = gres_plugin_fini();
		if (rc == SLURM_SUCCESS)
			rc = gres_plugin_init();
#endif
	}
	xfree(plugin_names);

	return rc;
}

/*
 * Destroy a gres_slurmd_conf_t record, free it's memory
 */
static void _destroy_gres_slurmd_conf(void *x)
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
 * Log the contents of a gres_slurmd_conf_t record
 */
static int _log_gres_slurmd_conf(void *x, void *arg)
{
	gres_slurmd_conf_t *p;
	char *links = NULL;

	p = (gres_slurmd_conf_t *) x;
	xassert(p);

	if (!gres_debug) {
		verbose("Gres Name=%s Type=%s Count=%"PRIu64,
			p->name, p->type_name, p->count);
		return 0;
	}

	if (p->links)
		xstrfmtcat(links, "Links=%s", p->links);
	if (p->cpus) {
		info("Gres Name=%s Type=%s Count=%"PRIu64" ID=%u File=%s "
		     "Cores=%s CoreCnt=%u %s",
		     p->name, p->type_name, p->count, p->plugin_id, p->file,
		     p->cpus, p->cpu_cnt, links);
	} else if (p->file) {
		info("Gres Name=%s Type=%s Count=%"PRIu64" ID=%u File=%s %s",
		     p->name, p->type_name, p->count, p->plugin_id, p->file, links);
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

static int _validate_file(char *path_name, char *gres_name)
{
	char *file_name, *slash, *one_name, *root_path;
	char *formatted_path = NULL;
	hostlist_t hl;
	int i, file_count = 0;

	i = strlen(path_name);
	if ((i < 3) || (path_name[i-1] != ']')) {
		_my_stat(path_name);
		return 1;
	}

	slash = strrchr(path_name, '/');
	if (slash) {
		i = strlen(path_name);
		formatted_path = xmalloc(i+1);
		slash[0] = '\0';
		root_path = xstrdup(path_name);
		xstrcat(root_path, "/");
		slash[0] = '/';
		file_name = slash + 1;
	} else {
		file_name = path_name;
		root_path = NULL;
	}
	hl = hostlist_create(file_name);
	if (hl == NULL)
		fatal("can't parse File=%s", path_name);
	while ((one_name = hostlist_shift(hl))) {
		if (slash) {
			sprintf(formatted_path, "%s/%s", root_path, one_name);
			_my_stat(formatted_path);
		} else {
			_my_stat(one_name);
		}
		file_count++;
		free(one_name);
	}
	hostlist_destroy(hl);
	xfree(formatted_path);
	xfree(root_path);

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
		if ((val < 0) || (val > GRES_MAX_LINK) || (val == LONG_MIN) ||
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
 * Build gres_slurmd_conf_t record based upon a line from the gres.conf file
 */
static int _parse_gres_config(void **dest, slurm_parser_enum_t type,
			      const char *key, const char *value,
			      const char *line, char **leftover)
{
	static s_p_options_t _gres_options[] = {
		{"Count", S_P_STRING},	/* Number of Gres available */
		{"CPUs" , S_P_STRING},	/* CPUs to bind to Gres resource
					 * (deprecated, use Cores) */
		{"Cores", S_P_STRING},	/* Cores to bind to Gres resource */
		{"File",  S_P_STRING},	/* Path to Gres device */
		{"Link",  S_P_STRING},	/* Communication link IDs */
		{"Links", S_P_STRING},	/* Communication link IDs */
		{"Name",  S_P_STRING},	/* Gres name */
		{"Type",  S_P_STRING},	/* Gres type (e.g. model name) */
		{NULL}
	};
	int i;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t *p;
	uint64_t tmp_uint64;
	char *tmp_str, *last;

	tbl = s_p_hashtbl_create(_gres_options);
	s_p_parse_line(tbl, *leftover, leftover);

	p = xmalloc(sizeof(gres_slurmd_conf_t));
	if (!value) {
		if (!s_p_get_string(&p->name, "Name", tbl)) {
			error("Invalid gres data, no type name (%s)", line);
			xfree(p);
			s_p_hashtbl_destroy(tbl);
			return 0;
		}
	} else {
		p->name = xstrdup(value);
	}

	p->cpu_cnt = gres_cpu_cnt;
	if (s_p_get_string(&p->cpus, "Cores", tbl) ||
	    s_p_get_string(&p->cpus, "CPUs", tbl)) {
		char *local_cpus = NULL;
		p->cpus_bitmap = bit_alloc(gres_cpu_cnt);
		if (xcpuinfo_ops.xcpuinfo_abs_to_mac) {
			i = (xcpuinfo_ops.xcpuinfo_abs_to_mac)
				(p->cpus, &local_cpus);
			if (i != SLURM_SUCCESS) {
				fatal("Invalid gres data for %s, Cores=%s",
				      p->name, p->cpus);
			}
		} else
			local_cpus = xstrdup(p->cpus);
		if ((bit_size(p->cpus_bitmap) == 0) ||
		    bit_unfmt(p->cpus_bitmap, local_cpus) != 0) {
			fatal("Invalid gres data for %s, Cores=%s (only %u Cores are available)",
			      p->name, p->cpus, gres_cpu_cnt);
		}
		xfree(local_cpus);
	}

	if (s_p_get_string(&p->file, "File", tbl)) {
		p->count = _validate_file(p->file, p->name);
		p->has_file = 1;
	}

	if (s_p_get_string(&p->links, "Link",  tbl) ||
	    s_p_get_string(&p->links, "Links", tbl)) {
		_validate_links(p);
	}

	if (s_p_get_string(&p->type_name, "Type", tbl) && !p->file) {
		p->file = xstrdup("/dev/null");
		p->has_file = 2;
	}

	if (s_p_get_string(&tmp_str, "Count", tbl)) {
		tmp_uint64 = strtoll(tmp_str, &last, 10);
		if ((tmp_uint64 == LONG_MIN) || (tmp_uint64 == LONG_MAX)) {
			fatal("Invalid gres record for %s, invalid count %s",
			      p->name, tmp_str);
		}
		if ((last[0] == 'k') || (last[0] == 'K'))
			tmp_uint64 *= 1024;
		else if ((last[0] == 'm') || (last[0] == 'M'))
			tmp_uint64 *= (1024 * 1024);
		else if ((last[0] == 'g') || (last[0] == 'G'))
			tmp_uint64 *= ((uint64_t)1024 * 1024 * 1024);
		else if ((last[0] == 't') || (last[0] == 'T'))
			tmp_uint64 *= ((uint64_t)1024 * 1024 * 1024 * 1024);
		else if ((last[0] == 'p') || (last[0] == 'P'))
			tmp_uint64 *= ((uint64_t)1024 * 1024 * 1024 * 1024 *
				       1024);
		else if (last[0] != '\0') {
			fatal("Invalid gres record for %s, invalid count %s",
			      p->name, tmp_str);
		}
		if (p->count && (p->count != tmp_uint64)) {
			fatal("Invalid gres record for %s, count does not match File value",
			      p->name);
		}
		if (tmp_uint64 >= NO_VAL64) {
			fatal("Gres %s has invalid count value %"PRIu64,
			      p->name, tmp_uint64);
		}
		p->count = tmp_uint64;
		xfree(tmp_str);
	} else if (p->count == 0)
		p->count = 1;

	s_p_hashtbl_destroy(tbl);

	for (i=0; i<gres_context_cnt; i++) {
		if (xstrcasecmp(p->name, gres_context[i].gres_name) == 0)
			break;
	}
	if (i >= gres_context_cnt) {
		error("Ignoring gres.conf record, invalid name: %s", p->name);
		_destroy_gres_slurmd_conf(p);
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
		{"Count", S_P_STRING},	/* Number of Gres available */
		{"CPUs" , S_P_STRING},	/* CPUs to bind to Gres resource */
		{"Cores", S_P_STRING},	/* Cores to bind to Gres resource */
		{"File",  S_P_STRING},	/* Path to Gres device */
		{"Link",  S_P_STRING},	/* Communication link IDs */
		{"Links", S_P_STRING},	/* Communication link IDs */
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

static void _validate_config(slurm_gres_context_t *context_ptr)
{
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	int has_file = -1, has_type = -1, rec_count = 0;

	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = (gres_slurmd_conf_t *) list_next(iter))) {
		if (gres_slurmd_conf->plugin_id != context_ptr->plugin_id)
			continue;
		rec_count++;
		if (has_file == -1)
			has_file = (int) gres_slurmd_conf->has_file;
		else if (( has_file && !gres_slurmd_conf->has_file) ||
			 (!has_file &&  gres_slurmd_conf->has_file)) {
			fatal("gres.conf for %s, some records have File "
			      "specification while others do not",
			      context_ptr->gres_name);
		}
		if (has_type == -1) {
			has_type = (int) (gres_slurmd_conf->type_name != NULL);
		} else if (( has_type && !gres_slurmd_conf->type_name) ||
			   (!has_type &&  gres_slurmd_conf->type_name)) {
			fatal("gres.conf for %s, some records have Type "
			      "specification while others do not",
			      context_ptr->gres_name);
		}
		if ((has_file == 0) && (has_type == 0) && (rec_count > 1)) {
			fatal("gres.conf duplicate records for %s",
			      context_ptr->gres_name);
		}
	}
	list_iterator_destroy(iter);
}

/* No gres.conf file found.
 * Initialize gres table with zero counts of all resources.
 * Counts can be altered by node_config_load() in the gres plugin. */
static int _no_gres_conf(uint32_t cpu_cnt)
{
	int i, rc = SLURM_SUCCESS;
	gres_slurmd_conf_t *p;

	slurm_mutex_lock(&gres_context_lock);
	FREE_NULL_LIST(gres_conf_list);
	gres_conf_list = list_create(_destroy_gres_slurmd_conf);
	for (i = 0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		p = xmalloc(sizeof(gres_slurmd_conf_t));
		p->cpu_cnt	= cpu_cnt;
		p->name		= xstrdup(gres_context[i].gres_name);
		p->plugin_id	= gres_context[i].plugin_id;
		list_append(gres_conf_list, p);
		/* If there is no plugin specific shared
		 * library the exported methods are NULL.
		 */
		if (gres_context[i].ops.node_config_load) {
			rc = (*(gres_context[i].ops.node_config_load))
				(gres_conf_list);
		}
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Load this node's configuration (how many resources it has, topology, etc.)
 * IN cpu_cnt - Number of CPUs on configured on this node
 * IN node_name - Name of this node
 * IN xcpuinfo_abs_to_mac - Pointer to xcpuinfo_abs_to_mac() funct, if available
 */
extern int gres_plugin_node_config_load(uint32_t cpu_cnt, char *node_name,
					void *xcpuinfo_abs_to_mac)
{
	static s_p_options_t _gres_options[] = {
		{"Name",     S_P_ARRAY, _parse_gres_config,  NULL},
		{"NodeName", S_P_ARRAY, _parse_gres_config2, NULL},
		{NULL}
	};

	int count = 0, i, rc;
	struct stat config_stat;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t **gres_array;
	char *gres_conf_file;

	if (xcpuinfo_abs_to_mac)
		xcpuinfo_ops.xcpuinfo_abs_to_mac = xcpuinfo_abs_to_mac;

	rc = gres_plugin_init();
	if (gres_context_cnt == 0)
		return SLURM_SUCCESS;

	gres_conf_file = get_extra_conf_path("gres.conf");
	if (stat(gres_conf_file, &config_stat) < 0) {
		error("can't stat gres.conf file %s, assuming zero resource "
		      "counts", gres_conf_file);
		xfree(gres_conf_file);
		return _no_gres_conf(cpu_cnt);
	}

	slurm_mutex_lock(&gres_context_lock);
	if (!gres_node_name && node_name)
		gres_node_name = xstrdup(node_name);
	gres_cpu_cnt = cpu_cnt;
	tbl = s_p_hashtbl_create(_gres_options);
	if (s_p_parse_file(tbl, NULL, gres_conf_file, false) == SLURM_ERROR)
		fatal("error opening/reading %s", gres_conf_file);
	FREE_NULL_LIST(gres_conf_list);
	gres_conf_list = list_create(_destroy_gres_slurmd_conf);
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
	list_for_each(gres_conf_list, _log_gres_slurmd_conf, NULL);

	for (i = 0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		_validate_config(&gres_context[i]);
		if (gres_context[i].ops.node_config_load == NULL)
			continue;	/* No plugin */
		rc = (*(gres_context[i].ops.node_config_load))(gres_conf_list);
	}
	slurm_mutex_unlock(&gres_context_lock);

	xfree(gres_conf_file);
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
			pack8(gres_slurmd_conf->has_file, buffer);
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
	uint32_t cpu_cnt, magic, plugin_id, utmp32;
	uint64_t count64;
	uint16_t rec_cnt, protocol_version;
	uint8_t has_file;
	char *tmp_cpus, *tmp_links, *tmp_name, *tmp_type;
	gres_slurmd_conf_t *p;

	rc = gres_plugin_init();

	FREE_NULL_LIST(gres_conf_list);
	gres_conf_list = list_create(_destroy_gres_slurmd_conf);

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
		if (protocol_version >= SLURM_18_08_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;

			safe_unpack64(&count64, buffer);
			safe_unpack32(&cpu_cnt, buffer);
			safe_unpack8(&has_file, buffer);
			safe_unpack32(&plugin_id, buffer);
			safe_unpackstr_xmalloc(&tmp_cpus, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_links, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_name, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_type, &utmp32, buffer);
		} else {  /* protocol_version >= SLURM_MIN_PROTOCOL_VERSION */
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;

			safe_unpack64(&count64, buffer);
			safe_unpack32(&cpu_cnt, buffer);
			safe_unpack8(&has_file, buffer);
			safe_unpack32(&plugin_id, buffer);
			safe_unpackstr_xmalloc(&tmp_cpus, &utmp32, buffer);
			tmp_links = NULL;
			safe_unpackstr_xmalloc(&tmp_name, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_type, &utmp32, buffer);
		}
	 	for (j = 0; j < gres_context_cnt; j++) {
	 		if (gres_context[j].plugin_id != plugin_id)
				continue;
			if (xstrcmp(gres_context[j].gres_name, tmp_name)) {
				/*
				 * Should have beeen caught in
				 * gres_plugin_init()
				 */
				error("%s: gres/%s duplicate plugin ID with"
				      " %s, unable to process",
				      __func__, tmp_name,
				      gres_context[j].gres_name);
				continue;
			}
			if (gres_context[j].has_file && !has_file && count64) {
				error("%s: gres/%s lacks File parameter for node %s",
				      __func__, tmp_name, node_name);
				has_file = 1;
			}
			if (has_file && (count64 > MAX_GRES_BITMAP)) {
				/*
				 * Avoid over-subscribing memory with
				 * huge bitmaps
				 */
				error("%s: gres/%s has File plus very "
				      "large Count (%"PRIu64") for "
				      "node %s, resetting value to %d",
				      __func__, tmp_name, count64,
				      node_name, MAX_GRES_BITMAP);
				count64 = MAX_GRES_BITMAP;
			}
			if (has_file)	/* Don't clear if already set */
				gres_context[j].has_file = true;
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
		p->count = count64;
		p->cpu_cnt = cpu_cnt;
		p->has_file = has_file;
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

/*
 * Delete an element placed on gres_list by _node_config_validate()
 * free associated memory
 */
static void _gres_node_list_delete(void *list_element)
{
	int i;
	gres_state_t *gres_ptr;
	gres_node_state_t *gres_node_ptr;

	gres_ptr = (gres_state_t *) list_element;
	gres_node_ptr = (gres_node_state_t *) gres_ptr->gres_data;
	FREE_NULL_BITMAP(gres_node_ptr->gres_bit_alloc);
	xfree(gres_node_ptr->gres_used);
	for (i = 0; i < gres_node_ptr->topo_cnt; i++) {
		if (gres_node_ptr->links_bitmap)
			FREE_NULL_BITMAP(gres_node_ptr->links_bitmap[i]);
		if (gres_node_ptr->topo_core_bitmap)
			FREE_NULL_BITMAP(gres_node_ptr->topo_core_bitmap[i]);
		if (gres_node_ptr->topo_gres_bitmap)
			FREE_NULL_BITMAP(gres_node_ptr->topo_gres_bitmap[i]);
		xfree(gres_node_ptr->topo_type_name[i]);
	}
	xfree(gres_node_ptr->links_bitmap);
	xfree(gres_node_ptr->topo_core_bitmap);
	xfree(gres_node_ptr->topo_gres_bitmap);
	xfree(gres_node_ptr->topo_gres_cnt_alloc);
	xfree(gres_node_ptr->topo_gres_cnt_avail);
	xfree(gres_node_ptr->topo_type_id);
	xfree(gres_node_ptr->topo_type_name);
	for (i = 0; i < gres_node_ptr->type_cnt; i++) {
		xfree(gres_node_ptr->type_name[i]);
	}
	xfree(gres_node_ptr->type_cnt_alloc);
	xfree(gres_node_ptr->type_cnt_avail);
	xfree(gres_node_ptr->type_id);
	xfree(gres_node_ptr->type_name);
	xfree(gres_node_ptr);
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

	type_id = _build_id(type);
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
	char *num, *last_num = NULL;
	uint64_t gres_config_cnt = 0, tmp_gres_cnt = 0;
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
			num = strrchr(tok, ':');
			if (!num) {
				error("Bad GRES configuration: %s", tok);
				break;
			}
			tmp_gres_cnt = strtoll(num + 1, &last_num, 10);
			if (last_num[0] == '\0')
				;
			else if ((last_num[0] == 'k') || (last_num[0] == 'K'))
				tmp_gres_cnt *= 1024;
			else if ((last_num[0] == 'm') || (last_num[0] == 'M'))
				tmp_gres_cnt *= (1024 * 1024);
			else if ((last_num[0] == 'g') || (last_num[0] == 'G'))
				tmp_gres_cnt *= ((uint64_t)1024 * 1024 * 1024);
			else if ((last_num[0] == 't') || (last_num[0] == 'T'))
				tmp_gres_cnt *= ((uint64_t)1024 * 1024 * 1024 *
						 1024);
			else if ((last_num[0] == 'p') || (last_num[0] == 'P'))
				tmp_gres_cnt *= ((uint64_t)1024 * 1024 * 1024 *
						 1024 * 1024);
			else {
				error("Bad GRES configuration: %s", tok);
				break;
			}

			/*
			 * If we have a GRES that has a type but not a count we
			 * will have 0 here, so set it correctly.
			 */
			if (!tmp_gres_cnt)
				tmp_gres_cnt = 1;

			gres_config_cnt += tmp_gres_cnt;
			num[0] = '\0';

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
			    uint16_t fast_schedule, char **reason_down)
{
	int i, j;
	uint64_t model_cnt;

	if (gres_data->type_cnt == 0)
		return 0;

	for (i = 0; i < gres_data->type_cnt; i++) {
		model_cnt = 0;
		for (j = 0; j < gres_data->topo_cnt; j++) {
			if (gres_data->type_id[i] == gres_data->topo_type_id[j])
				model_cnt += gres_data->topo_gres_cnt_avail[j];
		}
		if (fast_schedule >= 2) {
			gres_data->type_cnt_avail[i] = model_cnt;
		} else if (model_cnt < gres_data->type_cnt_avail[i]) {
			xstrfmtcat(*reason_down,
				   "%s:%s count too low "
				   "(%"PRIu64" < %"PRIu64")",
				   gres_name, gres_data->type_name[i],
				   model_cnt, gres_data->type_cnt_avail[i]);
			return -1;
		}
	}
	return 0;
}

static void _set_gres_cnt(char *orig_config, char **new_config,
			  uint64_t new_cnt, char *gres_name,
			  char *gres_name_colon, int gres_name_colon_len)
{
	char *new_configured_res = NULL, *node_gres_config;
	char *last_tok = NULL, *tok;

	if (*new_config)
		node_gres_config = xstrdup(*new_config);
	else if (orig_config)
		node_gres_config = xstrdup(orig_config);
	else
		return;

	tok = strtok_r(node_gres_config, ",", &last_tok);
	while (tok) {
		if (new_configured_res)
			xstrcat(new_configured_res, ",");
		if (xstrcmp(tok, gres_name) &&
		    xstrncmp(tok, gres_name_colon, gres_name_colon_len)) {
			xstrcat(new_configured_res, tok);
		} else if ((new_cnt % (1024 * 1024 * 1024)) == 0) {
			new_cnt /= (1024 * 1024 * 1024);
			xstrfmtcat(new_configured_res, "%s:%"PRIu64"G",
				   gres_name, new_cnt);
		} else if ((new_cnt % (1024 * 1024)) == 0) {
			new_cnt /= (1024 * 1024);
			xstrfmtcat(new_configured_res, "%s:%"PRIu64"M",
				   gres_name, new_cnt);
		} else if ((new_cnt % 1024) == 0) {
			new_cnt /= 1024;
			xstrfmtcat(new_configured_res, "%s:%"PRIu64"K",
				   gres_name, new_cnt);
		} else {
			xstrfmtcat(new_configured_res, "%s:%"PRIu64"",
				   gres_name, new_cnt);
		}
		tok = strtok_r(NULL, ",", &last_tok);
	}
	xfree(node_gres_config);
	xfree(*new_config);
	*new_config = new_configured_res;
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
	bool updated_config = false;
	gres_node_state_t *gres_data;

	if (gres_ptr->gres_data == NULL) {
		gres_ptr->gres_data = _build_gres_node_state();
		updated_config = true;
	}
	gres_data = (gres_node_state_t *) gres_ptr->gres_data;

	/* If the resource isn't configured for use with this node*/
	if ((orig_config == NULL) || (orig_config[0] == '\0') ||
	    (updated_config == false)) {
		gres_data->gres_cnt_config = 0;
		return rc;
	}

	_get_gres_cnt(gres_data, orig_config,
		      context_ptr->gres_name,
		      context_ptr->gres_name_colon,
		      context_ptr->gres_name_colon_len);

	context_ptr->total_cnt += gres_data->gres_cnt_config;

	/* Use count from recovered state, if higher */
	gres_data->gres_cnt_avail  = MAX(gres_data->gres_cnt_avail,
					 gres_data->gres_cnt_config);
	if ((gres_data->gres_bit_alloc != NULL) &&
	    (gres_data->gres_cnt_avail >
	     bit_size(gres_data->gres_bit_alloc))) {
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
	int i, rc;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_node_list_delete);
	}
	for (i = 0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
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

		rc = _node_config_init(node_name, orig_config,
				       &gres_context[i], gres_ptr);
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Determine gres availability on some node
 * plugin_id IN - plugin number to search for
 * set_cnt OUT - count of gres.conf records of this id found by slurmd
 *		 (each can have different topology)
 * RET - total number of gres available of this ID on this node in (sum
 *	 across all records of this ID)
 */
static uint64_t _get_tot_gres_cnt(uint32_t plugin_id, uint64_t *set_cnt)
{
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	uint32_t cpu_set_cnt = 0, rec_cnt = 0;
	uint64_t gres_cnt = 0;

	xassert(set_cnt);
	*set_cnt = 0;
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
	if (cpu_set_cnt)
		*set_cnt = rec_cnt;
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
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	int rc = SLURM_SUCCESS;
	int      found = 0;

	if (gres_conf_list == NULL) {
		/* Should not reach this as if there are GRES id's then there
		 * must have been a gres_conf_list.
		 */
		info("%s--The gres_conf_list is NULL!!!", __func__);
		snprintf(gres_name, gres_name_len, "%u", gres_id);
		return rc;
	}

	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = (gres_slurmd_conf_t *) list_next(iter))) {
		if (gres_slurmd_conf->plugin_id != gres_id)
			continue;
		strlcpy(gres_name, gres_slurmd_conf->name, gres_name_len);
		found = 1;
		break;
	}
	list_iterator_destroy(iter);

	if (!found)	/* Could not find GRES type name, use id */
		snprintf(gres_name, gres_name_len, "%u", gres_id);

	return rc;
}

static bitstr_t *_links_str2bitmap(char *links, char *node_name)
{
	bitstr_t *link_bitmap = NULL;

	if (!links || !links[0])
		return NULL;
	link_bitmap = bit_alloc(GRES_MAX_LINK + 1);
	if (bit_unfmt(link_bitmap, links) == 0)
		return link_bitmap;
	info("%s: Ignoring invalid GRES links (%s) for node %s", __func__,
	     links, node_name);
	bit_free(link_bitmap);
	return NULL;
}

static int _node_config_validate(char *node_name, char *orig_config,
				 char **new_config, gres_state_t *gres_ptr,
				 int cpu_cnt, int core_cnt,
				 uint16_t fast_schedule, char **reason_down,
				 slurm_gres_context_t *context_ptr)
{
	int i, j, gres_inx, rc = SLURM_SUCCESS;
	uint64_t gres_cnt, set_cnt = 0;
	bool cpus_config = false, updated_config = false;
	gres_node_state_t *gres_data;
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;

	if (gres_ptr->gres_data == NULL)
		gres_ptr->gres_data = _build_gres_node_state();
	gres_data = (gres_node_state_t *) gres_ptr->gres_data;
	if (gres_data->node_feature)
		return rc;

	gres_cnt = _get_tot_gres_cnt(context_ptr->plugin_id, &set_cnt);
	if (gres_data->gres_cnt_found != gres_cnt) {
		if (gres_data->gres_cnt_found != NO_VAL64) {
			info("%s: count changed for node %s from %"PRIu64" "
			     "to %"PRIu64"",
			     context_ptr->gres_type, node_name,
			     gres_data->gres_cnt_found, gres_cnt);
		}
		if ((gres_data->gres_cnt_found != NO_VAL64) &&
		    (gres_data->gres_cnt_alloc != 0)) {
			if (reason_down && (*reason_down == NULL)) {
				xstrfmtcat(*reason_down,
					   "%s count changed and jobs are "
					   "using them (%"PRIu64" != %"PRIu64")",
					   context_ptr->gres_type,
					   gres_data->gres_cnt_found, gres_cnt);
			}
			rc = EINVAL;
		} else {
			gres_data->gres_cnt_found = gres_cnt;
			updated_config = true;
		}
	}
	if (updated_config == false)
		return rc;

	if ((set_cnt == 0) && (set_cnt != gres_data->topo_cnt)) {
		/* Need to clear topology info */
		xfree(gres_data->topo_gres_cnt_alloc);
		xfree(gres_data->topo_gres_cnt_avail);
		for (i = 0; i < gres_data->topo_cnt; i++) {
			if (gres_data->links_bitmap)
				FREE_NULL_BITMAP(gres_data->links_bitmap[i]);
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
		xfree(gres_data->links_bitmap);
		xfree(gres_data->topo_gres_bitmap);
		xfree(gres_data->topo_core_bitmap);
		xfree(gres_data->topo_type_id);
		xfree(gres_data->topo_type_name);
		gres_data->topo_cnt = set_cnt;
	}

	if (context_ptr->has_file && (set_cnt != gres_data->topo_cnt)) {
		/*
		 * Need to rebuild topology info
		 * Resize the data structures here
		 */
		gres_data->topo_gres_cnt_alloc =
			xrealloc(gres_data->topo_gres_cnt_alloc,
				 set_cnt * sizeof(uint64_t));
		gres_data->topo_gres_cnt_avail =
			xrealloc(gres_data->topo_gres_cnt_avail,
				 set_cnt * sizeof(uint64_t));
		for (i = 0; i < gres_data->topo_cnt; i++) {
			if (gres_data->links_bitmap)
				FREE_NULL_BITMAP(gres_data->links_bitmap[i]);
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
		gres_data->links_bitmap =
			xrealloc(gres_data->links_bitmap,
				 set_cnt * sizeof(bitstr_t *));
		gres_data->topo_gres_bitmap =
			xrealloc(gres_data->topo_gres_bitmap,
				 set_cnt * sizeof(bitstr_t *));
		gres_data->topo_core_bitmap =
			xrealloc(gres_data->topo_core_bitmap,
				 set_cnt * sizeof(bitstr_t *));
		gres_data->topo_type_id = xrealloc(gres_data->topo_type_id,
						   set_cnt * sizeof(uint32_t));
		gres_data->topo_type_name = xrealloc(gres_data->topo_type_name,
						     set_cnt * sizeof(char *));
		gres_data->topo_cnt = set_cnt;

		iter = list_iterator_create(gres_conf_list);
		gres_inx = i = 0;
		while ((gres_slurmd_conf = (gres_slurmd_conf_t *)
			list_next(iter))) {
			if (gres_slurmd_conf->plugin_id !=
			    context_ptr->plugin_id)
				continue;
			gres_data->topo_gres_cnt_avail[i] =
					gres_slurmd_conf->count;
			if (gres_slurmd_conf->cpus) {
				bitstr_t *tmp_bitmap;
				tmp_bitmap =
					bit_alloc(gres_slurmd_conf->cpu_cnt);
				bit_unfmt(tmp_bitmap, gres_slurmd_conf->cpus);
				if (gres_slurmd_conf->cpu_cnt == core_cnt) {
					gres_data->topo_core_bitmap[i] =
						tmp_bitmap;
					tmp_bitmap = NULL; /* Nothing to free */
				} else if (gres_slurmd_conf->cpu_cnt ==
					   cpu_cnt) {
					/* Translate CPU to core bitmap */
					int cpus_per_core = cpu_cnt / core_cnt;
					int j, core_inx;
					gres_data->topo_core_bitmap[i] =
						bit_alloc(core_cnt);
					for (j = 0; j < cpu_cnt; j++) {
						if (!bit_test(tmp_bitmap, j))
							continue;
						core_inx = j / cpus_per_core;
						bit_set(gres_data->
							topo_core_bitmap[i],
							core_inx);
					}
				} else if (i == 0) {
					error("%s: invalid GRES cpu count (%u) on node %s",
					      context_ptr->gres_type,
					      gres_slurmd_conf->cpu_cnt,
					      node_name);
				}
				FREE_NULL_BITMAP(tmp_bitmap);
				cpus_config = true;
			} else if (cpus_config) {
				error("%s: has CPUs configured for only"
				      " some of the records on node %s",
				      context_ptr->gres_type, node_name);
			}

			gres_data->links_bitmap[i] =
				_links_str2bitmap(gres_slurmd_conf->links,
						  node_name);
			gres_data->topo_gres_bitmap[i] = bit_alloc(gres_cnt);
			for (j = 0; j < gres_slurmd_conf->count; j++) {
				bit_set(gres_data->topo_gres_bitmap[i],
					gres_inx++);
			}
			gres_data->topo_type_id[i] =
				_build_id(gres_slurmd_conf->type_name);
			gres_data->topo_type_name[i] =
				xstrdup(gres_slurmd_conf->type_name);
			i++;
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

	if ((gres_data->gres_cnt_config == 0) || (fast_schedule > 0))
		gres_data->gres_cnt_avail = gres_data->gres_cnt_config;
	else if (gres_data->gres_cnt_found != NO_VAL64)
		gres_data->gres_cnt_avail = gres_data->gres_cnt_found;
	else if (gres_data->gres_cnt_avail == NO_VAL64)
		gres_data->gres_cnt_avail = 0;

	if (context_ptr->has_file) {
		if (gres_data->gres_cnt_avail > MAX_GRES_BITMAP) {
			error("%s: gres/%s has File plus very large Count "
			      "(%"PRIu64") for node %s, resetting value to %u",
			      __func__, context_ptr->gres_type,
			      gres_data->gres_cnt_avail, node_name,
			      MAX_GRES_BITMAP);
			gres_data->gres_cnt_avail = MAX_GRES_BITMAP;
		}
		if (gres_data->gres_bit_alloc == NULL) {
			gres_data->gres_bit_alloc =
				bit_alloc(gres_data->gres_cnt_avail);
		} else if (gres_data->gres_cnt_avail !=
			   bit_size(gres_data->gres_bit_alloc)) {
			gres_data->gres_bit_alloc =
				bit_realloc(gres_data->gres_bit_alloc,
					    gres_data->gres_cnt_avail);
		}
	}

	if ((fast_schedule < 2) &&
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
				    fast_schedule, reason_down)) {
		rc = EINVAL;
	} else if ((fast_schedule == 2) && gres_data->topo_cnt &&
		   (gres_data->gres_cnt_found != gres_data->gres_cnt_config)) {
		error("%s on node %s configured for %"PRIu64" resources but "
		      "%"PRIu64" found, ignoring topology support",
		      context_ptr->gres_type, node_name,
		      gres_data->gres_cnt_config, gres_data->gres_cnt_found);
		if (gres_data->topo_core_bitmap) {
			for (i = 0; i < gres_data->topo_cnt; i++) {
				if (gres_data->links_bitmap) {
					FREE_NULL_BITMAP(gres_data->
							 links_bitmap[i]);
				}
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
			xfree(gres_data->links_bitmap);
			xfree(gres_data->topo_core_bitmap);
			xfree(gres_data->topo_gres_bitmap);
			xfree(gres_data->topo_gres_cnt_alloc);
			xfree(gres_data->topo_gres_cnt_avail);
			xfree(gres_data->topo_type_id);
			xfree(gres_data->topo_type_name);
		}
		gres_data->topo_cnt = 0;
	} else if ((fast_schedule == 0) &&
		   (gres_data->gres_cnt_found > gres_data->gres_cnt_config)) {
		/* need to rebuild new_config */
		_set_gres_cnt(orig_config, new_config,
			      gres_data->gres_cnt_found,
			      context_ptr->gres_name,
			      context_ptr->gres_name_colon,
			      context_ptr->gres_name_colon_len);
	}

	return rc;
}

/*
 * Validate a node's configuration and put a gres record onto a list
 * Called immediately after gres_plugin_node_config_unpack().
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT new_config - Updated gres info from slurm.conf if FastSchedule=0
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN cpu_cnt - Count of CPUs (threads) on this node
 * IN core_cnt - Count of cores on this node
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int gres_plugin_node_config_validate(char *node_name,
					    char *orig_config,
					    char **new_config,
					    List *gres_list,
					    int cpu_cnt, int core_cnt,
					    uint16_t fast_schedule,
					    char **reason_down)
{
	int i, rc, rc2;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL))
		*gres_list = list_create(_gres_node_list_delete);
	for (i = 0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
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
		rc2 = _node_config_validate(node_name, orig_config, new_config,
					    gres_ptr, cpu_cnt, core_cnt,
					    fast_schedule, reason_down,
					    &gres_context[i]);
		rc = MAX(rc, rc2);
	}
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
	ListIterator gres_iter;
	uint32_t plugin_id;
	uint64_t gres_scaled = 0;
	int gres_name_len;

	xassert(gres_name);
	gres_name_len = strlen(gres_name);
	plugin_id = _build_id(gres_name);
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
		gres_iter = list_iterator_create(*gres_list);
		while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
			if (gres_ptr->plugin_id == plugin_id)
				break;
		}
		list_iterator_destroy(gres_iter);
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

static int _node_reconfig(char *node_name, char *orig_config, char **new_config,
			  gres_state_t *gres_ptr, uint16_t fast_schedule,
			  slurm_gres_context_t *context_ptr)
{
	int rc = SLURM_SUCCESS;
	gres_node_state_t *gres_data;

	xassert(gres_ptr);
	if (gres_ptr->gres_data == NULL)
		gres_ptr->gres_data = _build_gres_node_state();
	gres_data = gres_ptr->gres_data;

	/* remove the last count */
	context_ptr->total_cnt -= gres_data->gres_cnt_config;

	_get_gres_cnt(gres_data, orig_config,
		      context_ptr->gres_name,
		      context_ptr->gres_name_colon,
		      context_ptr->gres_name_colon_len);

	/* add the new */
	context_ptr->total_cnt += gres_data->gres_cnt_config;

	if ((gres_data->gres_cnt_config == 0) || (fast_schedule > 0))
		gres_data->gres_cnt_avail = gres_data->gres_cnt_config;
	else if (gres_data->gres_cnt_found != NO_VAL64)
		gres_data->gres_cnt_avail = gres_data->gres_cnt_found;
	else if (gres_data->gres_cnt_avail == NO_VAL64)
		gres_data->gres_cnt_avail = 0;

	if (context_ptr->has_file) {
		if (gres_data->gres_bit_alloc == NULL) {
			gres_data->gres_bit_alloc =
				bit_alloc(gres_data->gres_cnt_avail);
		} else if (gres_data->gres_cnt_avail !=
			   bit_size(gres_data->gres_bit_alloc)) {
			gres_data->gres_bit_alloc =
				bit_realloc(gres_data->gres_bit_alloc,
					    gres_data->gres_cnt_avail);
		}
	}

	if ((fast_schedule < 2) &&
	    (gres_data->gres_cnt_found != NO_VAL64) &&
	    (gres_data->gres_cnt_found <  gres_data->gres_cnt_config)) {
		/* Do not set node DOWN, but give the node
		 * a chance to register with more resources */
		gres_data->gres_cnt_found = NO_VAL64;
	} else if ((fast_schedule == 0) &&
		   (gres_data->gres_cnt_found != NO_VAL64) &&
		   (gres_data->gres_cnt_found >  gres_data->gres_cnt_config)) {
		_set_gres_cnt(orig_config, new_config,
			      gres_data->gres_cnt_found,
			      context_ptr->gres_name,
			      context_ptr->gres_name_colon,
			      context_ptr->gres_name_colon_len);
	}

	return rc;
}

/*
 * Note that a node's configuration has been modified (e.g. "scontol update ..")
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT new_config - Updated gres info from slurm.conf if FastSchedule=0
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 */
extern int gres_plugin_node_reconfig(char *node_name,
				     char *orig_config,
				     char **new_config,
				     List *gres_list,
				     uint16_t fast_schedule)
{
	int i, rc, rc2;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL))
		*gres_list = list_create(_gres_node_list_delete);
	for (i=0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		/* Find gres_state entry on the list */
		gres_iter = list_iterator_create(*gres_list);
		while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
			if (gres_ptr->plugin_id == gres_context[i].plugin_id)
				break;
		}
		list_iterator_destroy(gres_iter);
		if (gres_ptr == NULL)
			continue;

		rc2 = _node_reconfig(node_name, orig_config, new_config,
				     gres_ptr, fast_schedule, &gres_context[i]);
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&gres_context_lock);

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
	uint16_t rec_cnt = 0;
	uint8_t  has_bitmap;
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
		/* Just note if gres_bit_alloc exists.
		 * Rebuild it based upon the state of recovered jobs */
		if (gres_node_ptr->gres_bit_alloc)
			has_bitmap = 1;
		else
			has_bitmap = 0;
		pack8(has_bitmap, buffer);
		rec_cnt++;
		break;
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
	uint32_t magic, plugin_id;
	uint64_t gres_cnt_avail;
	uint16_t rec_cnt;
	uint8_t  has_bitmap;
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
			safe_unpack8(&has_bitmap, buffer);
		} else {
			error("gres_plugin_node_state_unpack: protocol_version"
			      " %hu not supported", protocol_version);
			goto unpack_error;
		}
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_context[i].plugin_id == plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("gres_plugin_node_state_unpack: no plugin "
			      "configured to unpack data type %u from node %s",
			      plugin_id, node_name);
			/*
			 * A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data.
			 */
			continue;
		}
		gres_node_ptr = _build_gres_node_state();
		gres_node_ptr->gres_cnt_avail = gres_cnt_avail;
		if (has_bitmap) {
			gres_node_ptr->gres_bit_alloc =
				bit_alloc(gres_cnt_avail);
		}
		gres_ptr = xmalloc(sizeof(gres_state_t));
		gres_ptr->plugin_id = gres_context[i].plugin_id;
		gres_ptr->gres_data = gres_node_ptr;
		list_append(*gres_list, gres_ptr);
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("gres_plugin_node_state_unpack: unpack error from node %s",
	      node_name);
	slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

static void *_node_state_dup(void *gres_data)
{
	int i;
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
	if (gres_ptr->topo_cnt == 0)
		return new_gres;

	new_gres->topo_cnt         = gres_ptr->topo_cnt;
	new_gres->links_bitmap = xmalloc(gres_ptr->topo_cnt  *
					 sizeof(bitstr_t *));
	new_gres->topo_core_bitmap = xmalloc(gres_ptr->topo_cnt *
					     sizeof(bitstr_t *));
	new_gres->topo_gres_bitmap = xmalloc(gres_ptr->topo_cnt *
					     sizeof(bitstr_t *));
	new_gres->topo_gres_cnt_alloc = xmalloc(gres_ptr->topo_cnt *
						sizeof(uint64_t));
	new_gres->topo_gres_cnt_avail = xmalloc(gres_ptr->topo_cnt *
						sizeof(uint64_t));
	new_gres->topo_type_id = xmalloc(gres_ptr->topo_cnt * sizeof(uint32_t));
	new_gres->topo_type_name = xmalloc(gres_ptr->topo_cnt * sizeof(char *));
	for (i = 0; i < gres_ptr->topo_cnt; i++) {
		if (gres_ptr->links_bitmap[i]) {
			new_gres->links_bitmap[i] =
				bit_copy(gres_ptr->links_bitmap[i]);
		}
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

	new_gres->type_cnt       = gres_ptr->type_cnt;
	new_gres->type_cnt_alloc = xmalloc(gres_ptr->type_cnt *
					   sizeof(uint64_t));
	new_gres->type_cnt_avail = xmalloc(gres_ptr->type_cnt *
					   sizeof(uint64_t));
	new_gres->type_id = xmalloc(gres_ptr->type_cnt * sizeof(uint32_t));
	new_gres->type_name = xmalloc(gres_ptr->type_cnt * sizeof(char *));
	for (i = 0; i < gres_ptr->type_cnt; i++) {
		new_gres->type_cnt_alloc[i] = gres_ptr->type_cnt_alloc[i];
		new_gres->type_cnt_avail[i] = gres_ptr->type_cnt_avail[i];
		new_gres->type_id[i] = gres_ptr->type_id[i];
		new_gres->type_name[i] = xstrdup(gres_ptr->type_name[i]);
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
		/* This array can be set at startup if a job has been allocated
		 * specific GRES and the node has not registered with the
		 * details needed to track individual GRES (rather than only
		 * a GRES count). */
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
	int i;
	char tmp_str[128];

	xassert(gres_data);
	gres_node_ptr = (gres_node_state_t *) gres_data;

	info("gres/%s: state for %s", gres_name, node_name);
	if (gres_node_ptr->gres_cnt_found == NO_VAL64) {
		snprintf(tmp_str, sizeof(tmp_str), "TBD");
	} else {
		snprintf(tmp_str, sizeof(tmp_str), "%"PRIu64"",
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
		bit_fmt(tmp_str, sizeof(tmp_str), gres_node_ptr->gres_bit_alloc);
		info("  gres_bit_alloc:%s", tmp_str);
	} else {
		info("  gres_bit_alloc:NULL");
	}

	info("  gres_used:%s", gres_node_ptr->gres_used);

	for (i = 0; i < gres_node_ptr->topo_cnt; i++) {
		info("  type[%d]:%s(%u)", i, gres_node_ptr->topo_type_name[i],
		     gres_node_ptr->topo_type_id[i]);
		if (gres_node_ptr->links_bitmap &&
		    gres_node_ptr->links_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_node_ptr->links_bitmap[i]);
			info("   links_bitmap[%d]:%s", i, tmp_str);
		}
		if (gres_node_ptr->topo_core_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_node_ptr->topo_core_bitmap[i]);
			info("   topo_core_bitmap[%d]:%s", i, tmp_str);
		} else
			info("   topo_core_bitmap[%d]:NULL", i);
		if (gres_node_ptr->topo_gres_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_node_ptr->topo_gres_bitmap[i]);
			info("   topo_gres_bitmap[%d]:%s", i, tmp_str);
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
		info("   type_cnt_alloc[%d]:%"PRIu64"", i,
		     gres_node_ptr->type_cnt_alloc[i]);
		info("   type_cnt_avail[%d]:%"PRIu64"", i,
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

	if (!gres_debug || (gres_list == NULL))
		return;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		for (i = 0; i<gres_context_cnt; i++) {
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

extern uint64_t gres_get_system_cnt(char *name)
{
	uint64_t count = 0;
	int i;

	if (!name)
		return 0;

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
	ListIterator gres_iter;
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
			gres_iter = list_iterator_create(gres_list);
			while ((gres_ptr = list_next(gres_iter))) {
				if (gres_ptr->plugin_id ==
				    gres_context[i].plugin_id)
					break;
			}
			list_iterator_destroy(gres_iter);

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

			gres_iter = list_iterator_create(gres_list);
			while ((gres_ptr = list_next(gres_iter))) {
				if (gres_ptr->plugin_id ==
				    gres_context[i].plugin_id)
					break;
			}
			list_iterator_destroy(gres_iter);

			if (!gres_ptr || !gres_ptr->gres_data)
				break;
			data_ptr = (gres_node_state_t *)gres_ptr->gres_data;
			type_id = _build_id(type_str);
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

/*
 * Insure consistency of gres_per_* options
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

	/* Insure gres_per_job >= gres_per_node >= gres_per_socket */
	if (job_gres_data->gres_per_job &&
	    ((job_gres_data->gres_per_node &&
	      (job_gres_data->gres_per_node > job_gres_data->gres_per_job)) ||
	     (job_gres_data->gres_per_task &&
	      (job_gres_data->gres_per_task > job_gres_data->gres_per_job)) ||
	     (job_gres_data->gres_per_socket &&
	      (job_gres_data->gres_per_socket > job_gres_data->gres_per_job))))
		return -1;

	/* Insure gres_per_job >= gres_per_task */
	if (job_gres_data->gres_per_node &&
	    ((job_gres_data->gres_per_task &&
	      (job_gres_data->gres_per_task > job_gres_data->gres_per_node)) ||
	     (job_gres_data->gres_per_socket &&
	      (job_gres_data->gres_per_socket > job_gres_data->gres_per_node))))
		return -1;

	/* gres_per_socket requires sockets-per-node count specification */
	if (job_gres_data->gres_per_socket) {
		if (*sockets_per_node == NO_VAL16)
			return -1;
	}

	/* gres_per_task requires task count specification */
	if (job_gres_data->gres_per_task) {
		if (*num_tasks == NO_VAL)
			return -1;
	}

	/*
	 * Insure gres_per_job is multiple of gres_per_node
	 * Insure node count is consistent with GRES parameters
	 */
	if (job_gres_data->gres_per_job && job_gres_data->gres_per_node) {
		if (job_gres_data->gres_per_job % job_gres_data->gres_per_node){
			/* gres_per_job not multiple of gres_per_node */
			return -1;
		}
		req_nodes = job_gres_data->gres_per_job /
			    job_gres_data->gres_per_node;
		if ((req_nodes < *min_nodes) || (req_nodes > *max_nodes))
			return -1;
		*min_nodes = *max_nodes = req_nodes;
	}

	/*
	 * Insure gres_per_node is multiple of gres_per_socket
	 * Insure task count is consistent with GRES parameters
	 */
	if (job_gres_data->gres_per_node && job_gres_data->gres_per_socket) {
		if (job_gres_data->gres_per_node %
		    job_gres_data->gres_per_socket) {
			/* gres_per_node not multiple of gres_per_socket */
			return -1;
		}
		req_sockets = job_gres_data->gres_per_node /
			      job_gres_data->gres_per_socket;
		if (*sockets_per_node == NO_VAL16)
			*sockets_per_node = req_sockets;
		else if (*sockets_per_node != req_sockets)
			return -1;
	}
	/*
	 * Insure gres_per_job is multiple of gres_per_task
	 * Insure task count is consistent with GRES parameters
	 */
	if (job_gres_data->gres_per_job && job_gres_data->gres_per_task) {
		if (job_gres_data->gres_per_job % job_gres_data->gres_per_task){
			/* gres_per_job not multiple of gres_per_task */
			return -1;
		}
		req_tasks = job_gres_data->gres_per_job /
			    job_gres_data->gres_per_task;
		if (*num_tasks == NO_VAL)
			*num_tasks = req_tasks;
		else if (*num_tasks != req_tasks)
			return -1;
	}

	/*
	 * Insure gres_per_node is multiple of gres_per_task
	 * Insure tasks_per_node is consistent with GRES parameters
	 */
	if (job_gres_data->gres_per_node && job_gres_data->gres_per_task) {
		if (job_gres_data->gres_per_node %
		    job_gres_data->gres_per_task) {
			/* gres_per_node not multiple of gres_per_task */
			return -1;
		}
		req_tasks_per_node = job_gres_data->gres_per_node /
				     job_gres_data->gres_per_task;
		if ((*ntasks_per_node == NO_VAL16) ||
		    (*ntasks_per_node == 0))
			*ntasks_per_node = req_tasks_per_node;
		else if (*ntasks_per_node != req_tasks_per_node)
			return -1;
	}

	/*
	 * Insure gres_per_socket is multiple of gres_per_task
	 * Insure ntasks_per_socket is consistent with GRES parameters
	 */
	if (job_gres_data->gres_per_socket && job_gres_data->gres_per_task) {
		if (job_gres_data->gres_per_socket %
		    job_gres_data->gres_per_task) {
			/* gres_per_socket not multiple of gres_per_task */
			return -1;
		}
		req_tasks_per_socket = job_gres_data->gres_per_socket /
				       job_gres_data->gres_per_task;
		if ((*ntasks_per_socket == NO_VAL16) ||
		    (*ntasks_per_socket == 0))
			*ntasks_per_socket = req_tasks_per_socket;
		else if (*ntasks_per_socket != req_tasks_per_socket)
			return -1;
	}

	/* Insure that cpus_per_gres * gres_per_task == cpus_per_task */
	if (job_gres_data->cpus_per_gres && job_gres_data->gres_per_task) {
		req_cpus_per_task = job_gres_data->cpus_per_gres *
				    job_gres_data->gres_per_task;
		if ((*cpus_per_task == NO_VAL16) ||
		    (*cpus_per_task == 0))
			*cpus_per_task = req_cpus_per_task;
		else if (*cpus_per_task != req_cpus_per_task)
			return -1;
	}

	return 0;
}

/*
 * Reentrant TRES specification parse logic
 * in_val IN - initial input string
 * cnt OUT - count of values
 * gres_list IN - where to search for (or add) new job TRES record
 * save_ptr IN/OUT - NULL on initial call, otherwise value from previous call
 * rc OUT - unchanged or an error code
 * RET gres - job record to set value in, found or created by this function
 */
static gres_job_state_t *_get_next_job_gres(char *in_val, uint64_t *cnt,
					    List gres_list, char **save_ptr,
					    int *rc)
{
	static char *prev_save_ptr = NULL;
	char *end_ptr = NULL, *comma, *sep, *sep2, *name = NULL, *type = NULL;
	int context_inx, i, my_rc = SLURM_SUCCESS, offset = 0;
	unsigned long long int value;
	gres_job_state_t *job_gres_data = NULL;
	gres_state_t *gres_ptr;
	gres_key_t job_search_key;

	xassert(save_ptr);
	if (!in_val && (*save_ptr == NULL)) {
		*save_ptr = NULL;
		return NULL;
	}

	if (*save_ptr == NULL) {
		prev_save_ptr = in_val;
	} else if (*save_ptr != prev_save_ptr) {
		my_rc = SLURM_ERROR;
		goto fini;
	}

next:	if (prev_save_ptr[0] == '\0') {	/* Empty input token */
		*save_ptr = NULL;
		return NULL;
	}

	/* Identify the appropriate context for input token */
	name = xstrdup(prev_save_ptr);
	comma = strchr(name, ',');
	sep =   strchr(name, ':');
	if (sep && (!comma || (sep < comma))) {
		sep[0] = '\0';
		sep++;
		sep2 = strchr(sep, ':');
		if (sep2 && (!comma || (sep2 < comma)))
			sep2++;
		else
			sep2 = sep;
		if ((sep2[0] == '0') &&
		    ((value = strtoull(sep2, &end_ptr, 10)) == 0)) {
			/* Ignore GRES with explicit zero count */
			offset = end_ptr - name + 1;
			xfree(name);
			if (!comma) {
				prev_save_ptr = NULL;
				goto fini;
			} else {
				prev_save_ptr += offset;
				goto next;
			}
		}
	} else if (!comma) {
		/* TRES name only, implied count of 1 */
		sep = NULL;
	} else {
		comma[0] = '\0';
		sep = NULL;
	}

	for (i = 0; i < gres_context_cnt; i++) {
		if (!xstrcmp(name, gres_context[i].gres_name) ||
		    !xstrncmp(name, gres_context[i].gres_name_colon,
			      gres_context[i].gres_name_colon_len))
			break;	/* GRES name match found */
	}
	if (i >= gres_context_cnt) {
		debug("%s: Failed to locate GRES %s", __func__, name);
		my_rc = ESLURM_INVALID_GRES;
		goto fini;
	}
	context_inx = i;

	/* Identify GRES type/model name (value is optional) */
	if (!sep) {
		/* No type or count */
		type = NULL;
	} else if ((sep[0] < '0') || (sep[0] > '9')) {
		type = xstrdup(sep);
		if ((sep2 = strchr(type, ':'))) {
			sep2[0] = '\0';
			offset = sep2 - type + 1;
			sep += offset;
		} else {
			sep = NULL;
		}
	} else {
		/* Count in this field, no type */
		type = NULL;
	}

	/* Identify numeric value, including suffix */
	if (!sep) {
		/* No type or explicit count. Count is 1 by default */
		*cnt = 1;
		if (comma) {
			offset = (comma + 1) - name;
			prev_save_ptr += offset;
		} else	/* No more GRES */
			prev_save_ptr = NULL;
	} else if ((sep[0] >= '0') && (sep[0] <= '9')) {
		value = strtoull(sep, &end_ptr, 10);
		if (value == ULLONG_MAX) {
			my_rc = ESLURM_INVALID_GRES;
			goto fini;
		}
		if ((end_ptr[0] == 'k') || (end_ptr[0] == 'K')) {
			value *= 1024;
			end_ptr++;
		} else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M')) {
			value *= (1024 * 1024);
			end_ptr++;
		} else if ((end_ptr[0] == 'g') || (end_ptr[0] == 'G')) {
			value *= ((uint64_t)1024 * 1024 * 1024);
			end_ptr++;
		} else if ((end_ptr[0] == 't') || (end_ptr[0] == 'T')) {
			value *= ((uint64_t)1024 * 1024 * 1024 * 1024);
			end_ptr++;
		} else if ((end_ptr[0] == 'p') || (end_ptr[0] == 'P')) {
			value *= ((uint64_t)1024 * 1024 * 1024 * 1024 * 1024);
			end_ptr++;
		}
		if (end_ptr[0] == ',') {
			end_ptr++;
		} else if (end_ptr[0] != '\0') {
			my_rc = ESLURM_INVALID_GRES;
			goto fini;
		}
		*cnt = value;
		offset = end_ptr - name;
		prev_save_ptr += offset;
	} else {
		/* Malformed input (e.g. "gpu:tesla:") */
		my_rc = ESLURM_INVALID_GRES;
		goto fini;
	}

	/* Find the job GRES record */
	job_search_key.plugin_id = gres_context[context_inx].plugin_id;
	job_search_key.type_id = _build_id(type);
	gres_ptr = list_find_first(gres_list, _gres_find_job_by_key,
				   &job_search_key);

	if (gres_ptr) {
		job_gres_data = gres_ptr->gres_data;
	} else {
		job_gres_data = xmalloc(sizeof(gres_job_state_t));
		job_gres_data->gres_name =
			xstrdup(gres_context[context_inx].gres_name);
		job_gres_data->type_id = _build_id(type);
		job_gres_data->type_name = type;
		type = NULL;	/* String moved above */
		gres_ptr = xmalloc(sizeof(gres_state_t));
		gres_ptr->plugin_id = gres_context[context_inx].plugin_id;
		gres_ptr->gres_data = job_gres_data;
		list_append(gres_list, gres_ptr);
	}

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
 * IN/OUT cpus_per_task - requested ntasks_per_socket count, may be reset to
 *		      provide consistent gres_per_task/cpus_per_gres values
 * OUT gres_list - List of GRES records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_job_state_validate(char *cpus_per_tres,
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
	bool overlap_merge = false;
	gres_state_t *gres_state;
	gres_job_state_t *job_gres_data;
	uint64_t cnt = 0;
	ListIterator iter;

	if (!cpus_per_tres && !tres_per_job && !tres_per_node &&
	    !tres_per_socket && !tres_per_task && !mem_per_tres)
		return SLURM_SUCCESS;

	if ((rc = gres_plugin_init()) != SLURM_SUCCESS)
		return rc;

	/*
	 * Clear fields as requested by job update (i.e. input value is "")
	 */
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
		}
	}
	if (tres_per_job) {
		char *in_val = tres_per_job, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->gres_per_job = cnt;
			in_val = NULL;
		}
	}
	if (tres_per_node) {
		char *in_val = tres_per_node, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->gres_per_node = cnt;
			in_val = NULL;
		}
	}
	if (tres_per_socket) {
		char *in_val = tres_per_socket, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->gres_per_socket = cnt;
			in_val = NULL;
		}
	}
	if (tres_per_task) {
		char *in_val = tres_per_task, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->gres_per_task = cnt;
			in_val = NULL;
		}
	}
	if (mem_per_tres) {
		char *in_val = mem_per_tres, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->mem_per_gres = cnt;
			in_val = NULL;
		}
	}
	if (tres_per_socket) {
		char *in_val = tres_per_socket, *save_ptr = NULL;
		while ((job_gres_data = _get_next_job_gres(in_val, &cnt,
							   *gres_list,
							   &save_ptr, &rc))) {
			job_gres_data->gres_per_socket = cnt;
			in_val = NULL;
		}
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
	 * Insure tres_per_job >= tres_per_node >= tres_per_socket
	 */
	over_list = xmalloc(sizeof(overlap_check_t) * size);
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
 * Update a job's total_gres counter as we add a node to potential allocaiton
 * IN job_gres_list - List of job's GRES requirements (job_gres_state_t)
 * IN sock_gres_list - Per socket GRES availability on this node (sock_gres_t)
 */
extern void gres_plugin_job_sched_add(List job_gres_list, List sock_gres_list)
{
	ListIterator iter;
	gres_state_t *job_gres_state;
	gres_job_state_t *job_data;
	sock_gres_t *sock_data;

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
		job_data->total_gres += sock_data->total_cnt;
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
		new_gres_ptr->gres_bit_alloc = xmalloc(sizeof(bitstr_t *) *
						       gres_ptr->node_cnt);
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

		if (protocol_version >= SLURM_18_08_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			pack16(gres_job_ptr->cpus_per_gres, buffer);
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
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			pack64(gres_job_ptr->gres_per_node, buffer);
			packstr(gres_job_ptr->type_name, buffer);
			pack32(gres_job_ptr->node_cnt, buffer);

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
	uint32_t magic, plugin_id, utmp32 = 0;
	uint16_t rec_cnt;
	uint8_t  has_more;
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

		if (protocol_version >= SLURM_18_08_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_job_ptr = xmalloc(sizeof(gres_job_state_t));
			safe_unpack16(&gres_job_ptr->cpus_per_gres, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_job, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_node, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_socket, buffer);
			safe_unpack64(&gres_job_ptr->gres_per_task, buffer);
			safe_unpack64(&gres_job_ptr->mem_per_gres, buffer);
			safe_unpack64(&gres_job_ptr->total_gres, buffer);
			safe_unpackstr_xmalloc(&gres_job_ptr->type_name,
					       &utmp32, buffer);
			gres_job_ptr->type_id =
				_build_id(gres_job_ptr->type_name);
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
				gres_job_ptr->gres_bit_alloc =
					xmalloc(sizeof(bitstr_t *) *
						gres_job_ptr->node_cnt);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_job_ptr->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				gres_job_ptr->gres_bit_step_alloc =
					xmalloc(sizeof(bitstr_t *) *
						gres_job_ptr->node_cnt);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_job_ptr->
							   gres_bit_step_alloc[i],
							   buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				gres_job_ptr->gres_cnt_step_alloc =
					xmalloc(sizeof(uint64_t) *
						gres_job_ptr->node_cnt);
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
			safe_unpack64(&gres_job_ptr->gres_per_node, buffer);
			safe_unpackstr_xmalloc(&gres_job_ptr->type_name,
					       &utmp32, buffer);
			gres_job_ptr->type_id =
				_build_id(gres_job_ptr->type_name);
			safe_unpack32(&gres_job_ptr->node_cnt, buffer);
			if (gres_job_ptr->node_cnt > NO_VAL)
				goto unpack_error;
			safe_unpack8(&has_more, buffer);

			if (has_more) {
				gres_job_ptr->gres_bit_alloc =
					xmalloc(sizeof(bitstr_t *) *
						gres_job_ptr->node_cnt);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_job_ptr->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				gres_job_ptr->gres_bit_step_alloc =
					xmalloc(sizeof(bitstr_t *) *
						gres_job_ptr->node_cnt);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_job_ptr->
							   gres_bit_step_alloc[i],
							   buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				gres_job_ptr->gres_cnt_step_alloc =
					xmalloc(sizeof(uint64_t) *
						gres_job_ptr->node_cnt);
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
			error("%s: no plugin configured to unpack data type %u from job %u",
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
	error("gres_plugin_job_state_unpack: unpack error from job %u",
	      job_id);
	if (gres_job_ptr)
		_job_state_delete(gres_job_ptr);
	slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
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
				 char *gres_name, char *node_name)
{
	int i, j, core_ctld;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bitstr_t *avail_core_bitmap = NULL;

	if (!node_gres_ptr->topo_cnt || !core_bitmap ||	/* No topology info */
	    !job_gres_ptr->gres_per_node)		/* No job GRES */
		return;

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
			  uint32_t job_id, char *node_name, char *gres_name)
{
	int i, j, core_size, core_ctld, top_inx;
	uint64_t gres_avail = 0, gres_total;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	uint32_t *cores_addnt = NULL; /* Additional cores avail from this GRES */
	uint32_t *cores_avail = NULL; /* cores initially avail from this GRES */
	uint32_t core_cnt = 0;
	bitstr_t *alloc_core_bitmap = NULL;
	bitstr_t *avail_core_bitmap = NULL;

	if (node_gres_ptr->no_consume)
		use_total_gres = true;

	if (job_gres_ptr->gres_per_node && node_gres_ptr->topo_cnt &&
	    *topo_set) {
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
			if (!node_gres_ptr->topo_core_bitmap[i]) {
				gres_avail += node_gres_ptr->
					      topo_gres_cnt_avail[i];
				if (!use_total_gres) {
					gres_avail -= node_gres_ptr->
						      topo_gres_cnt_alloc[i];
				}
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
				break;
			}
		}
		if (job_gres_ptr->gres_per_node > gres_avail)
			return (uint32_t) 0;	/* insufficient, GRES to use */
		return NO_VAL;
	} else if (job_gres_ptr->gres_per_node && node_gres_ptr->topo_cnt) {
		/* Need to determine which specific cores can be used */
		gres_avail = node_gres_ptr->gres_cnt_avail;
		if (!use_total_gres)
			gres_avail -= node_gres_ptr->gres_cnt_alloc;
		if (job_gres_ptr->gres_per_node > gres_avail)
			return (uint32_t) 0;	/* insufficient, GRES to use */

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
		cores_addnt = xmalloc(sizeof(uint32_t)*node_gres_ptr->topo_cnt);
		cores_avail = xmalloc(sizeof(uint32_t)*node_gres_ptr->topo_cnt);
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			if (node_gres_ptr->topo_gres_cnt_avail[i] == 0)
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
		while (gres_avail < job_gres_ptr->gres_per_node) {
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
				if (gres_total < job_gres_ptr->gres_per_node)
					core_cnt = 0;
				break;
			}
			cores_avail[top_inx] = 0;	/* Flag as used */
			i = node_gres_ptr->topo_gres_cnt_avail[top_inx];
			if (!use_total_gres) {
				i -= node_gres_ptr->
				     topo_gres_cnt_alloc[top_inx];
			}
			if (i < 0) {
				error("gres/%s: topology allocation error on "
				      "node %s", gres_name, node_name);
				continue;
			}
			/* update counts of allocated cores and GRES */
			if (!node_gres_ptr->topo_core_bitmap[top_inx]) {
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
			if (i > 0) {
				/* Available GRES count is up to i, but take 1
				 * per loop to maximize available core count */
				gres_avail += 1;
				gres_total += i;
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
		if (job_gres_ptr->gres_per_node > gres_avail)
			return (uint32_t) 0;	/* insufficient, GRES to use */
		return NO_VAL;
	} else {
		gres_avail = node_gres_ptr->gres_cnt_avail;
		if (!use_total_gres)
			gres_avail -= node_gres_ptr->gres_cnt_alloc;
		if (job_gres_ptr->gres_per_node > gres_avail)
			return (uint32_t) 0;	/* insufficient, GRES to use */
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
	ListIterator  job_gres_iter, node_gres_iter;
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
		node_gres_iter = list_iterator_create(node_gres_list);
		while ((node_gres_ptr = (gres_state_t *)
				list_next(node_gres_iter))) {
			if (job_gres_ptr->plugin_id == node_gres_ptr->plugin_id)
				break;
		}
		list_iterator_destroy(node_gres_iter);
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
					 gres_context[i].gres_name, node_name);
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
 * RET: NO_VAL    - All cores on node are available
 *      otherwise - Count of available cores
 */
extern uint32_t gres_plugin_job_test(List job_gres_list, List node_gres_list,
				     bool use_total_gres, bitstr_t *core_bitmap,
				     int core_start_bit, int core_end_bit,
				     uint32_t job_id, char *node_name)
{
	int i;
	uint32_t core_cnt, tmp_cnt;
	ListIterator job_gres_iter,  node_gres_iter;
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
		node_gres_iter = list_iterator_create(node_gres_list);
		while ((node_gres_ptr = (gres_state_t *)
				list_next(node_gres_iter))) {
			if (job_gres_ptr->plugin_id == node_gres_ptr->plugin_id)
				break;
		}
		list_iterator_destroy(node_gres_iter);
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
					    gres_context[i].gres_name);
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
	if (sock_gres) {
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
				bool enforce_binding, uint32_t s_p_n)
{
	int i, j, s, c, tot_cores;
	sock_gres_t *sock_gres;
	uint64_t avail_gres, min_gres = 1;
	bool match = false;

	sock_gres = xmalloc(sizeof(sock_gres_t));
	sock_gres->cnt_by_sock = xmalloc(sizeof(uint64_t) * sockets);
	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		if (job_gres_ptr->type_name &&
		    (job_gres_ptr->type_id != node_gres_ptr->topo_type_id[i]))
			continue;	/* Wrong type_model */
		if (!use_total_gres && !node_gres_ptr->no_consume &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] >=
		     node_gres_ptr->topo_gres_cnt_avail[i])) {
			continue;	/* No GRES remaining */
		} else if (!use_total_gres && !node_gres_ptr->no_consume) {
			avail_gres = node_gres_ptr->topo_gres_cnt_avail[i] -
				     node_gres_ptr->topo_gres_cnt_alloc[i];
		} else {
			avail_gres = node_gres_ptr->topo_gres_cnt_avail[i];
		}
		if (avail_gres == 0)
			continue;

		if (!node_gres_ptr->topo_core_bitmap ||
		    !node_gres_ptr->topo_core_bitmap[i]) {
			/* Not constrained by core */
			sock_gres->cnt_any_sock += avail_gres;
			sock_gres->total_cnt += avail_gres;
			match = true;
			continue;
		}

		/* Constrained by core */
		tot_cores = sockets * cores_per_sock;
		if (core_bitmap)
			tot_cores = MIN(tot_cores, bit_size(core_bitmap));
		if (node_gres_ptr->topo_core_bitmap[i]) {
			tot_cores = MIN(tot_cores,
					bit_size(node_gres_ptr->
						 topo_core_bitmap[i]));
		}
		for (s = 0; ((s < sockets) && avail_gres); s++) {
			if (core_bitmap) {
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
		bool *avail_sock_flag = xmalloc(sizeof(bool) * sockets);
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

	if (match) {
		sock_gres->type_id = job_gres_ptr->type_id;
		sock_gres->type_name = xstrdup(job_gres_ptr->type_name);
	} else {
		xfree(sock_gres->cnt_by_sock);
		xfree(sock_gres);
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
	uint64_t avail_gres, min_gres = 1;
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
		if (avail_gres < min_gres)
			continue;	/* Insufficient GRES remaining */
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
	sock_gres->total_cnt += avail_gres;

	return sock_gres;
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
 * RET: List of sock_gres_t entries identifying what resources are available on
 *	each core. Returns NULL if none available. Call FREE_NULL_LIST() to
 *	release memory.
 */
extern List gres_plugin_job_test2(List job_gres_list, List node_gres_list,
				  bool use_total_gres, bitstr_t *core_bitmap,
				  uint16_t sockets, uint16_t cores_per_sock,
				  uint32_t job_id, char *node_name,
				  bool enforce_binding, uint32_t s_p_n)
{
	List sock_gres_list = NULL;
	ListIterator job_gres_iter,  node_gres_iter;
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
		node_gres_iter = list_iterator_create(node_gres_list);
		while ((node_gres_ptr = (gres_state_t *)
				list_next(node_gres_iter))) {
			if (job_gres_ptr->plugin_id == node_gres_ptr->plugin_id)
				break;
		}
		list_iterator_destroy(node_gres_iter);
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
		if (core_bitmap && (bit_set_count(core_bitmap) == 0)) {
			sock_gres = NULL;	/* No cores available */
		} else if (node_data_ptr->topo_cnt) {
			sock_gres = _build_sock_gres_by_topo(job_data_ptr,
					node_data_ptr, use_total_gres,
					core_bitmap, sockets, cores_per_sock,
					job_id, node_name, enforce_binding,
					local_s_p_n);
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
		sock_gres->job_specs = job_data_ptr;
		sock_gres->gres_name = xstrdup(job_data_ptr->gres_name);
		sock_gres->plugin_id = job_gres_ptr->plugin_id;
		list_append(sock_gres_list, sock_gres);
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return sock_gres_list;
}

static bool *_build_avail_cores_by_sock(bitstr_t *core_bitmap,
					uint16_t sockets,
					uint16_t cores_per_sock)
{
	bool *avail_cores_by_sock = xmalloc(sizeof(bool) * sockets);
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
					uint16_t *avail_gpus,
					uint16_t *near_gpus)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	bool *avail_cores_by_sock = NULL;
	uint64_t max_gres, mem_per_gres = 0, near_gres_cnt = 0;
	uint32_t gpu_plugin_id;
	int s, rc = 0;

	*avail_gpus = 0;
	*near_gpus = 0;
	if (!core_bitmap || !sock_gres_list ||
	    (list_count(sock_gres_list) == 0))
		return rc;

	gpu_plugin_id = _build_id("gpu");
	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))) {
		uint64_t min_gres = 1, tmp_u64;
		if (sock_gres->job_specs) {
			gres_job_state_t *job_gres_ptr = sock_gres->job_specs;
			if (job_gres_ptr->gres_per_node)
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
		if (sock_gres->job_specs &&
		    sock_gres->job_specs->cpus_per_gres) {
			max_gres = max_cpus /
				   sock_gres->job_specs->cpus_per_gres;
			if ((max_gres == 0) ||
			    (sock_gres->job_specs->gres_per_node > max_gres) ||
			    (sock_gres->job_specs->gres_per_task > max_gres) ||
			    (sock_gres->job_specs->gres_per_socket > max_gres)){
				/* Insufficient CPUs for any GRES */
				rc = -1;
				break;
			}
		}
		if (sock_gres->job_specs && avail_mem) {
			if (sock_gres->job_specs->mem_per_gres) {
				mem_per_gres =
					sock_gres->job_specs->mem_per_gres;
			} else {
				mem_per_gres =
					sock_gres->job_specs->def_mem_per_gres;
			}
			if (mem_per_gres == 0) {
				/* No memory limit enforcement */
			} else if (mem_per_gres <= avail_mem) {
				sock_gres->max_node_gres = avail_mem /
							   mem_per_gres;
			} else { /* Insufficient memory for any GRES */
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
		if (sock_gres->job_specs &&
		    sock_gres->job_specs->gres_per_node) {
			if ((sock_gres->max_node_gres == 0) ||
			    (sock_gres->max_node_gres >
			     sock_gres->job_specs->gres_per_node)) {
				sock_gres->max_node_gres =
					sock_gres->job_specs->gres_per_node;
			}
		}
		if (sock_gres->job_specs &&
		    sock_gres->job_specs->cpus_per_gres) {
			int cpu_cnt;
			cpu_cnt = bit_set_count(core_bitmap);
			cpu_cnt *= cpus_per_core;
			max_gres = cpu_cnt /
				   sock_gres->job_specs->cpus_per_gres;
			if (max_gres == 0) {
				rc = -1;
				break;
			} else if ((sock_gres->max_node_gres == 0) ||
				   (sock_gres->max_node_gres > max_gres)) {
				sock_gres->max_node_gres = max_gres;
			}
		}
		if ((sock_gres->total_cnt < min_gres) ||
		    ((sock_gres->max_node_gres != 0) &&
		     (sock_gres->max_node_gres < min_gres))) {
			rc = -1;
			break;
		}

		if (sock_gres->plugin_id == gpu_plugin_id) {
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

/*
 * Determine how many tasks can be started on a given node and which
 *	sockets/cores are required
 * IN mc_ptr - job's multi-core specs, NO_VAL and INFINITE mapped to zero
 * IN sock_gres_list - list of sock_gres_t entries built by gres_plugin_job_test2()
 * IN avail_cores_per_sock - Count of available cores on each socket
 * IN sockets - Count of sockets on the node
 * IN cores_per_socket - Count of cores per socket on the node
 * IN cpus_per_core - Count of CPUs per core on the node
 * IN avail_cpus - Count of available CPUs on the node, UPDATED
 * IN min_tasks_this_node - Minimum count of tasks that can be started on this
 *                          node, UPDATED
 * IN max_tasks_this_node - Maximum count of tasks that can be started on this
 *                          node, UPDATED
 * IN rem_nodes - desired additional node count to allocate, including this node
 * IN rem_tasks - desired additional task count to allocate
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN first_pass - set if first scheduling attempt for this job, use
 *		   co-located GRES and cores if possible
 * IN avail_cores - cores available on this node, UPDATED
 */
extern void gres_plugin_job_core_filter3(gres_mc_data_t *mc_ptr,
					 List sock_gres_list,
					 uint16_t *avail_cores_per_sock,
					 uint16_t sockets,
					 uint16_t cores_per_socket,
					 uint16_t cpus_per_core,
					 uint16_t *avail_cpus,
					 int *min_tasks_this_node,
					 int *max_tasks_this_node,
					 int rem_nodes,
					 int rem_tasks,
					 bool enforce_binding,
					 bool first_pass,
					 bitstr_t *avail_core)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	gres_job_state_t *job_specs;
	int i, c, s, core_cnt, sock_cnt, req_cores, rem_sockets;
	uint64_t cnt_avail_sock, cnt_avail_total, max_gres = 0, rem_gres = 0;
	bool *req_sock = NULL;	/* Required socket */

	if (*max_tasks_this_node == 0)
		return;

	xassert(avail_core);
	req_sock = xmalloc(sizeof(bool) * sockets);
	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))) {
		job_specs = sock_gres->job_specs;
		if (!job_specs)
			continue;
		/*
		 * gres_plugin_job_core_filter2() sets sock_gres->max_node_gres
		 * for mem_per_gres enforcement. Ammend it for remaining nodes.
		 */
		if (job_specs->gres_per_job) {
			if (job_specs->total_gres >= job_specs->gres_per_job) {
				*max_tasks_this_node = 0;
				break;
			}
			rem_gres = job_specs->gres_per_job -
				   job_specs->total_gres;
		}
		if (sock_gres->max_node_gres) {
			if (rem_gres && (rem_gres < sock_gres->max_node_gres))
				max_gres = rem_gres;
			else
				max_gres = sock_gres->max_node_gres;
		}
		rem_nodes = MAX(rem_nodes, 1);
		rem_sockets = rem_nodes * MAX(1, mc_ptr->sockets_per_node);
		rem_tasks = MAX(rem_tasks, rem_nodes);
		if (max_gres &&
		    (((job_specs->gres_per_node   * rem_nodes)   > max_gres) ||
		     ((job_specs->gres_per_socket * rem_sockets) > max_gres) ||
		     ((job_specs->gres_per_task   * rem_tasks)   > max_gres))) {
			*max_tasks_this_node = 0;
			break;
		}
		if (job_specs->gres_per_node && job_specs->gres_per_task) {
			max_gres = job_specs->gres_per_node /
				   job_specs->gres_per_task;
			if ((max_gres == 0) ||
			    (max_gres > *max_tasks_this_node) ||
			    (max_gres < *min_tasks_this_node)) {
				*max_tasks_this_node = 0;
				break;
			}
			if (*max_tasks_this_node > max_gres)
				*max_tasks_this_node = max_gres;
			if (*min_tasks_this_node < max_gres)
				*min_tasks_this_node = max_gres;
		}

		/* Filter out unusable GRES by socket */
		cnt_avail_total = sock_gres->cnt_any_sock;
		for (s = 0; s < sockets; s++) {
			/* Test for sufficient gres_per_socket */
			if (sock_gres->cnt_by_sock) {
				cnt_avail_sock = sock_gres->cnt_by_sock[s];
			} else
				cnt_avail_sock = 0;
			if (job_specs->gres_per_socket >
			    (sock_gres->cnt_any_sock + cnt_avail_sock)) {
				if (sock_gres->cnt_by_sock) {
					sock_gres->total_cnt -=
						sock_gres->cnt_by_sock[s];
					sock_gres->cnt_by_sock[s] = 0;
				}
				continue;
			}

			/* Test for available cores on this socket */
			if ((enforce_binding || first_pass) &&
			    avail_cores_per_sock &&
			    (avail_cores_per_sock[s] == 0))
				continue;

			cnt_avail_total += cnt_avail_sock;
			req_sock[s] = true;
			if (job_specs->gres_per_node &&
			    (job_specs->gres_per_node >= cnt_avail_total))
				break;	/* Sufficient GRES */
		}

		if (job_specs->cpus_per_gres) {
			max_gres = *avail_cpus / job_specs->cpus_per_gres;
			cnt_avail_total = MIN(cnt_avail_total, max_gres);
		}
		if ((cnt_avail_total == 0) ||
		    (job_specs->gres_per_node > cnt_avail_total) ||
		    (job_specs->gres_per_task > cnt_avail_total)) {
			*max_tasks_this_node = 0;
		}
		if (job_specs->gres_per_task) {
			uint64_t max_tasks = cnt_avail_total /
					     job_specs->gres_per_task;
			*max_tasks_this_node = MIN(*max_tasks_this_node,
						   max_tasks);
		}
		if (*max_tasks_this_node == 0)
			break;

		/*
		 * Clear avail_core as needed to force resource allocations
		 * onto cores closest to GRES that we will use.
		 *
		 * First determine how many cores are on required sockets.
		 */
		core_cnt = 0;
		sock_cnt = 0;
		for (s = 0; s < sockets; s++) {
			if (!req_sock[s])
				continue;
			sock_cnt++;
			for (c = 0; c < cores_per_socket; c++) {
				i = (s * cores_per_socket) + c;
				if (bit_test(avail_core, i))
					core_cnt++;
			}
		}
		/* Next determine how many cores are needed for this job */
		req_cores = *max_tasks_this_node;
		if (mc_ptr->cpus_per_task)
			req_cores *= mc_ptr->cpus_per_task;
		if (job_specs->cpus_per_gres) {
			if (job_specs->gres_per_node) {
				i = job_specs->gres_per_node;
			} else if (job_specs->gres_per_socket) {
				i = job_specs->gres_per_socket * sock_cnt;
			} else if (job_specs->gres_per_task) {
				i = job_specs->gres_per_task *
				    *max_tasks_this_node;
			} else if (sock_gres->total_cnt) {
				i = sock_gres->total_cnt;
			} else {
				i = 1;
			}
			i *= job_specs->cpus_per_gres;
			i /= cpus_per_core;
			req_cores = MAX(req_cores, i);
		}

		/* Now clear the extra avail_core bits */
		if (core_cnt < req_cores) {
			for (s = 0; s < sockets; s++) {
				if (req_sock[s])
					continue;
				for (c = 0; c < cores_per_socket; c++) {
					i = (s * cores_per_socket) + c;
					if (!bit_test(avail_core, i))
						continue;
					if (core_cnt < req_cores) {
						core_cnt++;
					} else {
						bit_clear(avail_core, i);
						*avail_cpus -= cpus_per_core;
					}
				}
			}
		}
	}
	list_iterator_destroy(sock_gres_iter);
	xfree(req_sock);
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
		job_state_ptr->node_cnt = 0;
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

static int _job_alloc(void *job_gres_data, void *node_gres_data,
		      int node_cnt, int node_offset, char *gres_name,
		      uint32_t job_id, char *node_name,
		      bitstr_t *core_bitmap)
{
	int j, k, sz1, sz2;
	uint64_t gres_cnt, i;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bool type_array_updated = false;
	bitstr_t *alloc_core_bitmap = NULL;

	/*
	 * Validate data structures. Either job_gres_data->node_cnt and
	 * job_gres_data->gres_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_cnt);
	xassert(node_offset >= 0);
	xassert(job_gres_ptr);
	xassert(node_gres_ptr);

	if (node_gres_ptr->no_consume)
		return SLURM_SUCCESS;

	xfree(node_gres_ptr->gres_used);	/* Clear cache */
	if (job_gres_ptr->node_cnt == 0) {
		job_gres_ptr->node_cnt = node_cnt;
		if (job_gres_ptr->gres_bit_alloc) {
			error("gres/%s: job %u node_cnt==0 and bit_alloc is set",
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

	if (!job_gres_ptr->gres_bit_alloc)
		job_gres_ptr->gres_bit_alloc =
			xmalloc(sizeof(bitstr_t *) * node_cnt);
	if (!job_gres_ptr->gres_cnt_node_alloc)
		job_gres_ptr->gres_cnt_node_alloc =
			xmalloc(sizeof(uint64_t) * node_cnt);

	/*
	 * Check that sufficient resources exist on this node
	 */
	gres_cnt = job_gres_ptr->gres_per_node;
	job_gres_ptr->gres_cnt_node_alloc[node_offset] = gres_cnt;
	i = node_gres_ptr->gres_cnt_alloc + gres_cnt;

	if (i > node_gres_ptr->gres_cnt_avail) {
		error("gres/%s: job %u node %s overallocated resources by %"
		      PRIu64", (%"PRIu64" > %"PRIu64")",
		      gres_name, job_id, node_name,
		      i - node_gres_ptr->gres_cnt_avail,
		      i, node_gres_ptr->gres_cnt_avail);
		/* proceed with request, give job what's available */
	}

	if (!node_offset && job_gres_ptr->gres_cnt_step_alloc) {
		uint64_t *tmp = xmalloc(sizeof(uint64_t) *
					job_gres_ptr->node_cnt);
		memcpy(tmp, job_gres_ptr->gres_cnt_step_alloc,
		       sizeof(uint64_t) * MIN(node_cnt,
					      job_gres_ptr->node_cnt));
		xfree(job_gres_ptr->gres_cnt_step_alloc);
		job_gres_ptr->gres_cnt_step_alloc = tmp;
	}
	if (job_gres_ptr->gres_cnt_step_alloc == NULL) {
		job_gres_ptr->gres_cnt_step_alloc =
			xmalloc(sizeof(uint64_t) * job_gres_ptr->node_cnt);
	}

	/*
	 * Select the specific resources to use for this job.
	 */
	if (job_gres_ptr->gres_bit_alloc[node_offset]) {
		/* Resuming a suspended job, resources already allocated */
		if (node_gres_ptr->gres_bit_alloc == NULL) {
			node_gres_ptr->gres_bit_alloc =
				bit_copy(job_gres_ptr->
					 gres_bit_alloc[node_offset]);
			node_gres_ptr->gres_cnt_alloc +=
				bit_set_count(node_gres_ptr->gres_bit_alloc);
		} else if (node_gres_ptr->gres_bit_alloc) {
			gres_cnt = (uint64_t)MIN(
				bit_size(node_gres_ptr->gres_bit_alloc),
				bit_size(job_gres_ptr->
					 gres_bit_alloc[node_offset]));
			for (i = 0; i < gres_cnt; i++) {
				if (bit_test(job_gres_ptr->
					     gres_bit_alloc[node_offset], i) &&
				    !bit_test(node_gres_ptr->gres_bit_alloc,i)){
					bit_set(node_gres_ptr->gres_bit_alloc,i);
					node_gres_ptr->gres_cnt_alloc++;
				}
			}
		}
	} else if (node_gres_ptr->gres_bit_alloc) {
		job_gres_ptr->gres_bit_alloc[node_offset] =
				bit_alloc(node_gres_ptr->gres_cnt_avail);
		i = bit_size(node_gres_ptr->gres_bit_alloc);
		if (i < node_gres_ptr->gres_cnt_avail) {
			error("gres/%s: node %s gres bitmap size bad "
			      "(%"PRIu64" < %"PRIu64")",
			      gres_name, node_name,
			      i, node_gres_ptr->gres_cnt_avail);
			node_gres_ptr->gres_bit_alloc =
				bit_realloc(node_gres_ptr->gres_bit_alloc,
					    node_gres_ptr->gres_cnt_avail);
		}
		if (core_bitmap)
			alloc_core_bitmap = bit_alloc(bit_size(core_bitmap));
		/* Pass 1: Allocate GRES overlapping all allocated cores */
		for (i=0; i<node_gres_ptr->gres_cnt_avail && gres_cnt>0; i++) {
			if (bit_test(node_gres_ptr->gres_bit_alloc, i))
				continue;
			if (!_cores_on_gres(core_bitmap, alloc_core_bitmap,
					    node_gres_ptr, i, job_gres_ptr))
				continue;
			bit_set(node_gres_ptr->gres_bit_alloc, i);
			bit_set(job_gres_ptr->gres_bit_alloc[node_offset], i);
			node_gres_ptr->gres_cnt_alloc++;
			gres_cnt--;
		}
		FREE_NULL_BITMAP(alloc_core_bitmap);
		/* Pass 2: Allocate GRES overlapping any allocated cores */
		for (i=0; i<node_gres_ptr->gres_cnt_avail && gres_cnt>0; i++) {
			if (bit_test(node_gres_ptr->gres_bit_alloc, i))
				continue;
			if (!_cores_on_gres(core_bitmap, NULL, node_gres_ptr, i,
					    job_gres_ptr))
				continue;
			bit_set(node_gres_ptr->gres_bit_alloc, i);
			bit_set(job_gres_ptr->gres_bit_alloc[node_offset], i);
			node_gres_ptr->gres_cnt_alloc++;
			gres_cnt--;
		}
		if (gres_cnt)
			verbose("Gres topology sub-optimal for job %u", job_id);
		/* Pass 3: Allocate any available GRES */
		for (i=0; i<node_gres_ptr->gres_cnt_avail && gres_cnt>0; i++) {
			if (bit_test(node_gres_ptr->gres_bit_alloc, i))
				continue;
			bit_set(node_gres_ptr->gres_bit_alloc, i);
			bit_set(job_gres_ptr->gres_bit_alloc[node_offset], i);
			node_gres_ptr->gres_cnt_alloc++;
			gres_cnt--;
		}
	} else {
		node_gres_ptr->gres_cnt_alloc += job_gres_ptr->gres_per_node;
	}

	if (job_gres_ptr->gres_bit_alloc &&
	    job_gres_ptr->gres_bit_alloc[node_offset] &&
	    node_gres_ptr->topo_gres_bitmap &&
	    node_gres_ptr->topo_gres_cnt_alloc) {
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			if (job_gres_ptr->type_name &&
			    (!node_gres_ptr->topo_type_name[i] ||
			     (job_gres_ptr->type_id !=
			      node_gres_ptr->topo_type_id[i])))
				continue;
			sz1 = bit_size(job_gres_ptr->gres_bit_alloc[node_offset]);
			sz2 = bit_size(node_gres_ptr->topo_gres_bitmap[i]);
			if (sz1 != sz2) {
				/* Avoid abort on bit_overlap below */
				error("Gres count mismatch for node %s "
				      "(%d != %d)", node_name, sz1, sz2);
				continue;
			}
			gres_cnt = bit_overlap(job_gres_ptr->
					       gres_bit_alloc[node_offset],
					       node_gres_ptr->
					       topo_gres_bitmap[i]);
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
			}
		}
		type_array_updated = true;
	} else if (job_gres_ptr->gres_bit_alloc &&
		   job_gres_ptr->gres_bit_alloc[node_offset]) {
		int len;	/* length of the gres bitmap on this node */
		len = bit_size(job_gres_ptr->gres_bit_alloc[node_offset]);
		if (!node_gres_ptr->topo_gres_cnt_alloc) {
			node_gres_ptr->topo_gres_cnt_alloc =
				xmalloc(sizeof(uint64_t) * len);
		} else {
			len = MIN(len, node_gres_ptr->gres_cnt_config);
		}
		for (i = 0; i < len; i++) {
			if (!bit_test(job_gres_ptr->
				      gres_bit_alloc[node_offset], i))
				continue;
			node_gres_ptr->topo_gres_cnt_alloc[i]++;
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_type_name == NULL) ||
			    (node_gres_ptr->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_name[j] ||
				    (node_gres_ptr->topo_type_id[i] !=
				     node_gres_ptr->type_id[j]))
					continue;
				node_gres_ptr->type_cnt_alloc[j]++;
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
			if (!node_gres_ptr->type_name[j] ||
			    (job_gres_ptr->type_id !=
			     node_gres_ptr->type_id[j]))
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

/*
 * Select and allocate GRES to a job and update node and job GRES information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN node_cnt    - total number of nodes originally allocated to the job
 * IN node_offset - zero-origin index to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN core_bitmap - cores allocated to this job on this node (NULL if not
 *                  available)
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_alloc(List job_gres_list, List node_gres_list,
				 int node_cnt, int node_offset,
				 uint32_t job_id, char *node_name,
				 bitstr_t *core_bitmap)
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
				 node_gres_ptr->gres_data, node_cnt,
				 node_offset, gres_context[i].gres_name,
				 job_id, node_name, core_bitmap);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

static int _job_dealloc(void *job_gres_data, void *node_gres_data,
			int node_offset, char *gres_name, uint32_t job_id,
			char *node_name)
{
	int i, j, len, sz1, sz2;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bool type_array_updated = false;
	uint64_t gres_cnt = 0, k;

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
			/* NOTE: Do not clear bit from
			 * job_gres_ptr->gres_bit_alloc[node_offset]
			 * since this may only be an emulated deallocate */
			if (node_gres_ptr->gres_cnt_alloc)
				node_gres_ptr->gres_cnt_alloc--;
			else {
				error("gres/%s: job %u dealloc node %s gres "
				      "count underflow", gres_name, job_id,
				      node_name);
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
		error("gres/%s: job %u node %s GRES count underflow "
		      "(%"PRIu64" < %"PRIu64")",
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
			if (node_gres_ptr->topo_gres_cnt_alloc[i] >= gres_cnt) {
				node_gres_ptr->topo_gres_cnt_alloc[i] -=
					gres_cnt;
			} else {
				error("gres/%s: job %u dealloc node %s topo "
				      "gres count underflow "
				      "(%"PRIu64" %"PRIu64")",
				      gres_name, job_id,
				      node_name,
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
				} else {
					error("gres/%s: job %u dealloc node %s "
					      "type %s gres count underflow "
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
			node_gres_ptr->topo_gres_cnt_alloc[i]--;
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_type_name == NULL) ||
			    (node_gres_ptr->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_name[j] ||
				    (node_gres_ptr->topo_type_id[i] !=
				     node_gres_ptr->type_id[j]))
					continue;
				node_gres_ptr->type_cnt_alloc[j]--;
 			}
		}
		type_array_updated = true;
	}

	if (!type_array_updated && job_gres_ptr->type_name) {
		gres_cnt = job_gres_ptr->gres_per_node;
		for (j = 0; j < node_gres_ptr->type_cnt; j++) {
			if (!node_gres_ptr->type_name[j] ||
			    (job_gres_ptr->type_id !=
			     node_gres_ptr->type_id[j]))
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
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_dealloc(List job_gres_list, List node_gres_list,
				   int node_offset, uint32_t job_id,
				   char *node_name)
{
	int i, rc, rc2;
	ListIterator job_gres_iter,  node_gres_iter;
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

		node_gres_iter = list_iterator_create(node_gres_list);
		while ((node_gres_ptr = (gres_state_t *)
				list_next(node_gres_iter))) {
			if (job_gres_ptr->plugin_id == node_gres_ptr->plugin_id)
				break;
		}
		list_iterator_destroy(node_gres_iter);
		if (node_gres_ptr == NULL) {
			error("%s: node %s lacks gres/%s for job %u", __func__,
			      node_name, gres_name , job_id);
			continue;
		}

		rc2 = _job_dealloc(job_gres_ptr->gres_data,
				   node_gres_ptr->gres_data, node_offset,
				   gres_name, job_id, node_name);
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
	ListIterator gres_iter;
	gres_state_t *gres_ptr, *gres_ptr2;
	gres_job_state_t *gres_job_ptr, *gres_job_ptr2;
	int new_node_cnt;
	int i_first, i_last, i;
	int from_inx, to_inx, new_inx;
	bitstr_t **new_gres_bit_alloc, **new_gres_bit_step_alloc;
	uint64_t *new_gres_cnt_step_alloc;

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
		error("gres_plugin_job_merge: node_bitmaps are empty");
		return;
	}

	slurm_mutex_lock(&gres_context_lock);

	/* Step one - Expand the gres data structures in "to" job */
	if (!to_job_gres_list)
		goto step2;
	gres_iter = list_iterator_create(to_job_gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		gres_job_ptr = (gres_job_state_t *) gres_ptr->gres_data;
		new_gres_bit_alloc = xmalloc(sizeof(bitstr_t *) *
					     new_node_cnt);
		new_gres_bit_step_alloc = xmalloc(sizeof(bitstr_t *) *
						  new_node_cnt);
		new_gres_cnt_step_alloc = xmalloc(sizeof(uint64_t) *
						  new_node_cnt);

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
		xfree(gres_job_ptr->gres_bit_step_alloc);
		gres_job_ptr->gres_bit_step_alloc = new_gres_bit_step_alloc;
		xfree(gres_job_ptr->gres_cnt_step_alloc);
		gres_job_ptr->gres_cnt_step_alloc = new_gres_cnt_step_alloc;
	}
	list_iterator_destroy(gres_iter);

	/* Step two - Merge the gres information from the "from" job into the
	 * existing gres information for the "to" job */
step2:	if (!from_job_gres_list)
		goto step3;
	if (!to_job_gres_list) {
		to_job_gres_list = list_create(_gres_job_list_delete);
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
			gres_job_ptr2->node_cnt = new_node_cnt;
			gres_job_ptr2->gres_bit_alloc =
				xmalloc(sizeof(bitstr_t *) * new_node_cnt);
			gres_job_ptr2->gres_bit_step_alloc =
				xmalloc(sizeof(bitstr_t *) * new_node_cnt);
			gres_job_ptr2->gres_cnt_step_alloc =
				xmalloc(sizeof(uint64_t) * new_node_cnt);
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
				} else if (gres_job_ptr2->
					   gres_bit_alloc[new_inx]) {
					/* Do not merge GRES allocations on
					 * a node, just keep original job's */
#if 0
					bit_or(gres_job_ptr2->
					       gres_bit_alloc[new_inx],
					       gres_job_ptr->
					       gres_bit_alloc[from_inx]);
#endif
				} else {
					gres_job_ptr2->gres_bit_alloc[new_inx] =
						gres_job_ptr->
						gres_bit_alloc[from_inx];
					gres_job_ptr->
						gres_bit_alloc
						[from_inx] = NULL;
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

	if (!job_gres_list)
		return;

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

		if (!gres_context[i].ops.job_set_env)
			continue;	/* No plugin to call */
		(*(gres_context[i].ops.job_set_env))
			(job_env_ptr, gres_ptr->gres_data, node_inx);
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Set job default parameters in a given element of a list
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN gres_name - name of gres, apply defaults to all elements (e.g. updates to
 *		  gres_name="gpu" would apply to "gpu:tesla", "gpu:volta", etc.)
 * IN cpu_per_gpu - value to set as default
 * IN mem_per_gpu - value to set as default
 */
extern void gres_plugin_job_set_defs(List job_gres_list, char *gres_name,
				     uint64_t cpu_per_gpu,
				     uint64_t mem_per_gpu)
{
	uint32_t plugin_id;
	ListIterator gres_iter;
	gres_state_t *gres_ptr = NULL;
	gres_job_state_t *job_gres_data;

	if (!job_gres_list)
		return;

	plugin_id = _build_id(gres_name);
	gres_iter = list_iterator_create(job_gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		if (gres_ptr->plugin_id != plugin_id)
			continue;
		job_gres_data = (gres_job_state_t *) gres_ptr->gres_data;
		if (!job_gres_data)
			continue;
		job_gres_data->def_cpus_per_gres = cpu_per_gpu;
		job_gres_data->def_mem_per_gres = mem_per_gpu;
	}
	list_iterator_destroy(gres_iter);
}

static void _job_state_log(void *gres_data, uint32_t job_id, uint32_t plugin_id)
{
	gres_job_state_t *gres_ptr;
	char tmp_str[128];
	int i;

	xassert(gres_data);
	gres_ptr = (gres_job_state_t *) gres_data;
	info("gres:%s(%u) type:%s(%u) job:%u state",
	      gres_ptr->gres_name, plugin_id, gres_ptr->type_name,
	      gres_ptr->type_id, job_id);
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
	else if (gres_ptr->def_mem_per_gres)
		info("  def_mem_per_gres:%"PRIu64, gres_ptr->def_mem_per_gres);

	if (gres_ptr->node_cnt == 0)
		return;
	if (gres_ptr->gres_bit_alloc == NULL)
		info("  gres_bit_alloc:NULL");
	if (gres_ptr->gres_bit_step_alloc == NULL)
		info("  gres_bit_step_alloc:NULL");
	if (gres_ptr->gres_cnt_step_alloc == NULL)
		info("  gres_cnt_step_alloc:NULL");

	for (i = 0; i < gres_ptr->node_cnt; i++) {
		if (gres_ptr->gres_bit_alloc && gres_ptr->gres_bit_alloc[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_ptr->gres_bit_alloc[i]);
			info("  gres_bit_alloc[%d]:%s", i, tmp_str);
		} else if (gres_ptr->gres_bit_alloc)
			info("  gres_bit_alloc[%d]:NULL", i);

		if (gres_ptr->gres_bit_step_alloc &&
		    gres_ptr->gres_bit_step_alloc[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_ptr->gres_bit_step_alloc[i]);
			info("  gres_bit_step_alloc[%d]:%s", i, tmp_str);
		} else if (gres_ptr->gres_bit_step_alloc)
			info("  gres_bit_step_alloc[%d]:NULL", i);

		if (gres_ptr->gres_cnt_step_alloc) {
			info("  gres_cnt_step_alloc[%d]:%"PRIu64"", i,
			     gres_ptr->gres_cnt_step_alloc[i]);
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
	gres_name_type_id = _build_id(gres_name_type);
	gres_val = NO_VAL64;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		if (job_gres_ptr->plugin_id == gres_name_type_id) {
			gres_val = ((gres_job_state_t*)
				   (job_gres_ptr->gres_data))->gres_per_node;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);

	slurm_mutex_unlock(&gres_context_lock);

	return gres_val;
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

	if (!gres_debug || (gres_list == NULL))
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

extern List gres_plugin_get_allocated_devices(List gres_list, bool is_job)
{
	int i, j;
	ListIterator gres_itr, dev_itr;
	gres_state_t *gres_ptr;
	bitstr_t **local_bit_alloc = NULL;
	uint32_t node_cnt;
	gres_device_t *gres_device;
	List gres_devices;
	List device_list = NULL;

	(void) gres_plugin_init();

	/*
	 * Set up every device we have so we know.  This way we have the full
	 * deny list and alter the alloc variable later if it were allocated.
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
		} else if ((int)bit_size(local_bit_alloc[0]) !=
			   list_count(gres_devices)) {
			error("We got %d gres devices when we were only told about %d.  This should never happen.",
			      list_count(gres_devices),
			      (int)bit_size(local_bit_alloc[0]));
			continue;

		}

		dev_itr = list_iterator_create(gres_devices);
		i = 0;
		while ((gres_device = list_next(dev_itr))) {
			if (bit_test(local_bit_alloc[0], i))
				gres_device->alloc = 1;
			//info("%d is %d", i, gres_device->alloc);
			i++;
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
			   int node_offset, bool ignore_alloc, char *gres_name,
			   uint32_t job_id, uint32_t step_id)
{
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint64_t core_cnt, gres_cnt;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if ((node_offset == NO_VAL) ||
	    (0 == job_gres_ptr->node_cnt)) {	/* no_consume */
		if ((step_gres_ptr->gres_per_step >
		     job_gres_ptr->gres_per_job) ||
		    (job_gres_ptr->gres_per_node &&
		     (step_gres_ptr->gres_per_node >
		      job_gres_ptr->gres_per_node)) ||
		    (job_gres_ptr->gres_per_socket &&
		     (step_gres_ptr->gres_per_socket >
		      job_gres_ptr->gres_per_socket)))
			return 0;
		return NO_VAL64;
	}

	if (node_offset >= job_gres_ptr->node_cnt) {
		error("gres/%s: %s %u.%u node offset invalid (%d >= %u)",
		      gres_name, __func__, job_id, step_id, node_offset,
		      job_gres_ptr->node_cnt);
		return 0;
	}

//FIXME: Needs updating for new TRES fields
	if (job_gres_ptr->gres_cnt_step_alloc) {
		uint64_t job_gres_avail = job_gres_ptr->gres_per_node;
		if (!ignore_alloc) {
			job_gres_avail -= job_gres_ptr->
					  gres_cnt_step_alloc[node_offset];
		}
		if (step_gres_ptr->gres_per_node > job_gres_avail)
			return 0;
	} else {
		error("gres/%s: %s %u.%u gres_cnt_step_alloc is NULL",
		      gres_name, __func__, job_id, step_id);
		return 0;
	}

	if (job_gres_ptr->gres_bit_alloc &&
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
		if (step_gres_ptr->gres_per_node > gres_cnt)
			core_cnt = 0;
		else
			core_cnt = NO_VAL64;
	} else if (job_gres_ptr->gres_cnt_step_alloc &&
		   job_gres_ptr->gres_cnt_step_alloc[node_offset]) {
		gres_cnt = job_gres_ptr->gres_per_node;
		if (!ignore_alloc) {
			gres_cnt -= job_gres_ptr->
				    gres_cnt_step_alloc[node_offset];
		}
		if (step_gres_ptr->gres_per_node > gres_cnt)
			core_cnt = 0;
		else
			core_cnt = NO_VAL64;
	} else {
		/* Note: We already validated the GRES count above */
		debug3("gres/%s: %s %u.%u gres_bit_alloc is NULL",
		       gres_name, __func__, job_id, step_id);
		core_cnt = NO_VAL64;
	}

	return core_cnt;
}

/*
 * Reentrant TRES specification parse logic
 * in_val IN - initial input string
 * cnt OUT - count of values
 * gres_list IN - where to search for (or add) new step TRES record
 * save_ptr IN/OUT - NULL on initial call, otherwise value from previous call
 * rc OUT - unchanged or an error code
 * RET gres - step record to set value in, found or created by this function
 */
static gres_step_state_t *_get_next_step_gres(char *in_val, uint64_t *cnt,
					      List gres_list, char **save_ptr,
					      int *rc)
{
	static char *prev_save_ptr = NULL;
	char *end_ptr = NULL, *comma, *sep, *sep2, *name = NULL, *type = NULL;
	int context_inx, i, my_rc = SLURM_SUCCESS, offset = 0;
	unsigned long long int value;
	gres_step_state_t *step_gres_data = NULL;
	gres_state_t *gres_ptr;
	gres_key_t step_search_key;

	xassert(save_ptr);
	if (!in_val && (*save_ptr == NULL)) {
		*save_ptr = NULL;
		return NULL;
	}

	if (*save_ptr == NULL) {
		prev_save_ptr = in_val;
	} else if (*save_ptr != prev_save_ptr) {
		my_rc = SLURM_ERROR;
		goto fini;
	}

next:	if (prev_save_ptr[0] == '\0') {	/* Empty input token */
		*save_ptr = NULL;
		return NULL;
	}

	/* Identify the appropriate context for input token */
	name = xstrdup(prev_save_ptr);
	comma = strchr(name, ',');
	sep =   strchr(name, ':');
	if (sep && (!comma || (sep < comma))) {
		sep[0] = '\0';
		sep++;
		sep2 = strchr(sep, ':');
		if (sep2 && (!comma || (sep2 < comma)))
			sep2++;
		else
			sep2 = sep;
		if ((sep2[0] == '0') &&
		    ((value = strtoull(sep2, &end_ptr, 10)) == 0)) {
			/* Ignore GRES with explicit zero count */
			offset = end_ptr - name + 1;
			xfree(name);
			if (!comma) {
				prev_save_ptr = NULL;
				goto fini;
			} else {
				prev_save_ptr += offset;
				goto next;
			}
		}
	} else if (!comma) {
		/* TRES name only, implied count of 1 */
		sep = NULL;
	} else {
		comma[0] = '\0';
		sep = NULL;
	}

	for (i = 0; i < gres_context_cnt; i++) {
		if (!xstrcmp(name, gres_context[i].gres_name) ||
		    !xstrncmp(name, gres_context[i].gres_name_colon,
			      gres_context[i].gres_name_colon_len))
			break;	/* GRES name match found */
	}
	if (i >= gres_context_cnt) {
		debug("%s: Failed to locate GRES %s", __func__, name);
		my_rc = ESLURM_INVALID_GRES;
		goto fini;
	}
	context_inx = i;

	/* Identify GRES type/model name (value is optional) */
	if (!sep) {
		/* No type or count */
		type = NULL;
	} else if ((sep[0] < '0') || (sep[0] > '9')) {
		type = xstrdup(sep);
		if ((sep2 = strchr(type, ':'))) {
			sep2[0] = '\0';
			offset = sep2 - type + 1;
			sep += offset;
		} else {
			sep = NULL;
		}
	} else {
		/* Count in this field, no type */
		type = NULL;
	}

	/* Identify numeric value, including suffix */
	if (!sep) {
		/* No type or explicit count. Count is 1 by default */
		*cnt = 1;
		if (comma)
			prev_save_ptr += (comma + 1) - name;
		else
			prev_save_ptr += strlen(name);
	} else if ((sep[0] >= '0') && (sep[0] <= '9')) {
		value = strtoull(sep, &end_ptr, 10);
		if (value == ULLONG_MAX) {
			my_rc = ESLURM_INVALID_GRES;
			goto fini;
		}
		if ((end_ptr[0] == 'k') || (end_ptr[0] == 'K')) {
			value *= 1024;
			end_ptr++;
		} else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M')) {
			value *= (1024 * 1024);
			end_ptr++;
		} else if ((end_ptr[0] == 'g') || (end_ptr[0] == 'G')) {
			value *= ((uint64_t)1024 * 1024 * 1024);
			end_ptr++;
		} else if ((end_ptr[0] == 't') || (end_ptr[0] == 'T')) {
			value *= ((uint64_t)1024 * 1024 * 1024 * 1024);
			end_ptr++;
		} else if ((end_ptr[0] == 'p') || (end_ptr[0] == 'P')) {
			value *= ((uint64_t)1024 * 1024 * 1024 * 1024 * 1024);
			end_ptr++;
		}
		if (end_ptr[0] == ',') {
			end_ptr++;
		} else if (end_ptr[0] != '\0') {
			my_rc = ESLURM_INVALID_GRES;
			goto fini;
		}
		*cnt = value;
		offset = end_ptr - name;
		prev_save_ptr += offset;
	}

	/* Find the step GRES record */
	step_search_key.plugin_id = gres_context[context_inx].plugin_id;
	step_search_key.type_id = _build_id(type);
	gres_ptr = list_find_first(gres_list, _gres_find_step_by_key,
				   &step_search_key);

	if (gres_ptr) {
		step_gres_data = gres_ptr->gres_data;
	} else {
		step_gres_data = xmalloc(sizeof(gres_step_state_t));
		step_gres_data->type_id = _build_id(type);
		step_gres_data->type_name = type;
		type = NULL;	/* String moved above */
		gres_ptr = xmalloc(sizeof(gres_state_t));
		gres_ptr->plugin_id = gres_context[context_inx].plugin_id;
		gres_ptr->gres_data = step_gres_data;
		list_append(gres_list, gres_ptr);
	}

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
		if (job_gres_data->cpus_per_gres &&
		    step_gres_data->cpus_per_gres &&
		    (job_gres_data->cpus_per_gres <
		     step_gres_data->cpus_per_gres)) {
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
		if (job_gres_data->mem_per_gres &&
		    step_gres_data->mem_per_gres &&
		    (job_gres_data->mem_per_gres <
		     step_gres_data->mem_per_gres)) {
			*rc = ESLURM_INVALID_GRES;
			break;
		}

	}
	list_iterator_destroy(iter);
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
					   List *step_gres_list,
					   List job_gres_list, uint32_t job_id,
					   uint32_t step_id)
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
		}
	}
	if (tres_per_node) {
		char *in_val = tres_per_node, *save_ptr = NULL;
		while ((step_gres_data = _get_next_step_gres(in_val, &cnt,
							    new_step_list,
							    &save_ptr, &rc))) {
			step_gres_data->gres_per_node = cnt;
			in_val = NULL;
		}
	}
	if (tres_per_socket) {
		char *in_val = tres_per_socket, *save_ptr = NULL;
		while ((step_gres_data = _get_next_step_gres(in_val, &cnt,
							    new_step_list,
							    &save_ptr, &rc))) {
			step_gres_data->gres_per_socket = cnt;
			in_val = NULL;
		}
	}
	if (tres_per_task) {
		char *in_val = tres_per_task, *save_ptr = NULL;
		while ((step_gres_data = _get_next_step_gres(in_val, &cnt,
							    new_step_list,
							    &save_ptr, &rc))) {
			step_gres_data->gres_per_task = cnt;
			in_val = NULL;
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
		new_gres_ptr->gres_bit_alloc = xmalloc(sizeof(bitstr_t *) *
					       gres_ptr->node_cnt);
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
			error("gres_plugin_step_state_rebase: node_in_use is "
			      "NULL");
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
							xmalloc(
							sizeof(bitstr_t *) *
							new_node_cnt);
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
 * IN job_id, step_id - job and step ID for logging
 */
extern int gres_plugin_step_state_pack(List gres_list, Buf buffer,
				       uint32_t job_id, uint32_t step_id,
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


		if (protocol_version >= SLURM_18_08_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			pack16(gres_step_ptr->cpus_per_gres, buffer);
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
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			pack64(gres_step_ptr->gres_per_node, buffer);
			pack32(gres_step_ptr->node_cnt, buffer);
			pack_bit_str_hex(gres_step_ptr->node_in_use, buffer);
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
			error("gres_plugin_step_state_pack: protocol_version "
			      "%hu not supported", protocol_version);
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
 * IN job_id, step_id - job and step ID for logging
 */
extern int gres_plugin_step_state_unpack(List *gres_list, Buf buffer,
					 uint32_t job_id, uint32_t step_id,
					 uint16_t protocol_version)
{
	int i, rc;
	uint32_t magic, plugin_id, uint32_tmp = 0;
	uint16_t rec_cnt;
	uint8_t has_file;
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
		if (protocol_version >= SLURM_18_08_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_step_ptr = xmalloc(sizeof(gres_step_state_t));
			safe_unpack16(&gres_step_ptr->cpus_per_gres, buffer);
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
			safe_unpack8(&has_file, buffer);
			if (has_file) {
				safe_unpack64_array(
					&gres_step_ptr->gres_cnt_node_alloc,
					&uint32_tmp, buffer);
			}
			safe_unpack8(&has_file, buffer);
			if (has_file) {
				gres_step_ptr->gres_bit_alloc =
					xmalloc(sizeof(bitstr_t *) *
						gres_step_ptr->node_cnt);
				for (i = 0; i < gres_step_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_step_ptr->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_step_ptr = xmalloc(sizeof(gres_step_state_t));
			safe_unpack64(&gres_step_ptr->gres_per_node, buffer);
			safe_unpack32(&gres_step_ptr->node_cnt, buffer);
			if (gres_step_ptr->node_cnt > NO_VAL)
				goto unpack_error;
			unpack_bit_str_hex(&gres_step_ptr->node_in_use, buffer);
			safe_unpack8(&has_file, buffer);
			if (has_file) {
				gres_step_ptr->gres_bit_alloc =
					xmalloc(sizeof(bitstr_t *) *
						gres_step_ptr->node_cnt);
				for (i = 0; i < gres_step_ptr->node_cnt; i++) {
					unpack_bit_str_hex(&gres_step_ptr->
							   gres_bit_alloc[i],
							   buffer);
				}
			}
		} else {
			error("gres_plugin_step_state_unpack: protocol_version"
			      " %hu not supported", protocol_version);
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
			info("gres_plugin_step_state_unpack: no plugin "
			      "configured to unpack data type %u from "
			      "step %u.%u",
			      plugin_id, job_id, step_id);
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
	error("gres_plugin_step_state_unpack: unpack error from step %u.%u",
	      job_id, step_id);
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

/* Given a GRES context index, return a bitmap representing those GRES
 * which are available from the CPUs current allocated to this process */
static bitstr_t * _get_usable_gres(int context_inx)
{
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

	CPU_ZERO(&mask);
#ifdef __FreeBSD__
	rc = cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1,
				sizeof(mask), &mask);
#elif defined SCHED_GETAFFINITY_THREE_ARGS
	rc = sched_getaffinity(0, sizeof(mask), &mask);
#else
	rc = sched_getaffinity(0, &mask);
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
		if (gres_inx + gres_slurmd_conf->count >= MAX_GRES_BITMAP) {
			error("GRES %s bitmap overflow",gres_slurmd_conf->name);
			continue;
		}
		if (!gres_slurmd_conf->cpus_bitmap) {
			bit_nset(usable_gres, gres_inx,
				 gres_inx + gres_slurmd_conf->count - 1);
		} else {
			i_last = bit_fls(gres_slurmd_conf->cpus_bitmap);
			for (i = 0; i <= i_last; i++) {
				if (!bit_test(gres_slurmd_conf->cpus_bitmap,i))
					continue;
				if (!CPU_ISSET(i, &mask))
					continue;
				bit_nset(usable_gres, gres_inx,
					 gres_inx + gres_slurmd_conf->count-1);
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
}

/*
 * Set environment variables as required for all tasks of a job step
 * IN/OUT job_env_ptr - environment variable array
 * IN step_gres_list - generated by gres_plugin_step_alloc()
 * IN accel_bind_type - GRES binding options
 */
extern void gres_plugin_step_set_env(char ***job_env_ptr, List step_gres_list,
				     uint16_t accel_bind_type)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_ptr = NULL;
	bool bind_gpu = accel_bind_type & ACCEL_BIND_CLOSEST_GPU;
	bool bind_nic = accel_bind_type & ACCEL_BIND_CLOSEST_NIC;
	bool bind_mic = accel_bind_type & ACCEL_BIND_CLOSEST_MIC;
	bitstr_t *usable_gres = NULL;

	if (step_gres_list == NULL)
		return;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(step_gres_list);
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

		if (!gres_context[i].ops.step_set_env)
			continue;	/* No plugin to call */
		if (bind_gpu || bind_mic || bind_nic) {
			if (!xstrcmp(gres_context[i].gres_name, "gpu")) {
				if (!bind_gpu)
					continue;
			} else if (!xstrcmp(gres_context[i].gres_name, "mic")) {
				if (!bind_mic)
					continue;
			} else if (!xstrcmp(gres_context[i].gres_name, "nic")) {
				if (!bind_nic)
					continue;
			} else {
				continue;
			}
			usable_gres = _get_usable_gres(i);
		}

		if (accel_bind_type)
			(*(gres_context[i].ops.step_reset_env))
				(job_env_ptr,
				 gres_ptr->gres_data,
				 usable_gres);
		else
			(*(gres_context[i].ops.step_set_env))
				(job_env_ptr,
				 gres_ptr->gres_data);

		FREE_NULL_BITMAP(usable_gres);
	}
	list_iterator_destroy(gres_iter);

	slurm_mutex_unlock(&gres_context_lock);
}

static void _step_state_log(void *gres_data, uint32_t job_id, uint32_t step_id,
			    char *gres_name)
{
	gres_step_state_t *gres_ptr = (gres_step_state_t *) gres_data;
	char tmp_str[128];
	int i;

	xassert(gres_ptr);
	info("gres:%s type:%s(%u) step:%u.%u state", gres_name,
	     gres_ptr->type_name, gres_ptr->type_id, job_id, step_id);
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
				info("  gres_bit_alloc[%d]:%s", i, tmp_str);
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

	if (!gres_debug || (gres_list == NULL))
		return;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_ptr->plugin_id != gres_context[i].plugin_id)
				continue;
			_step_state_log(gres_ptr->gres_data, job_id, step_id,
					gres_context[i].gres_name);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Determine how many cores of a job's allocation can be allocated to a job
 *	on a specific node
 * IN job_gres_list - a running job's gres info
 * IN/OUT step_gres_list - a pending job step's gres requirements
 * IN node_offset - index into the job's node allocation
 * IN ignore_alloc - if set ignore resources already allocated to running steps
 * IN job_id, step_id - ID of the step being allocated.
 * RET Count of available cores on this node (sort of):
 *     NO_VAL64 if no limit or 0 if node is not usable
 */
extern uint64_t gres_plugin_step_test(List step_gres_list, List job_gres_list,
				      int node_offset, bool ignore_alloc,
				      uint32_t job_id, uint32_t step_id)
{
	int i;
	uint64_t core_cnt, tmp_cnt;
	ListIterator  job_gres_iter, step_gres_iter;
	gres_state_t *job_gres_ptr, *step_gres_ptr;

	if (step_gres_list == NULL)
		return NO_VAL64;
	if (job_gres_list == NULL)
		return 0;

	core_cnt = NO_VAL64;
	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	step_gres_iter = list_iterator_create(step_gres_list);
	while ((step_gres_ptr = (gres_state_t *) list_next(step_gres_iter))) {
		job_gres_iter = list_iterator_create(job_gres_list);
		while ((job_gres_ptr = (gres_state_t *)
				list_next(job_gres_iter))) {
			if (step_gres_ptr->plugin_id == job_gres_ptr->plugin_id)
				break;
		}
		list_iterator_destroy(job_gres_iter);
		if (job_gres_ptr == NULL) {
			/* job lack resources required by the step */
			core_cnt = 0;
			break;
		}

		for (i = 0; i < gres_context_cnt; i++) {
			if (step_gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			tmp_cnt = _step_test(step_gres_ptr->gres_data,
					     job_gres_ptr->gres_data,
					     node_offset, ignore_alloc,
					     gres_context[i].gres_name,
					     job_id, step_id);
			if ((tmp_cnt != NO_VAL64) && (tmp_cnt < core_cnt))
				core_cnt = tmp_cnt;
			break;
		}
		if (core_cnt == 0)
			break;
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return core_cnt;
}

static int _step_alloc(void *step_gres_data, void *job_gres_data,
		       int node_offset, char *gres_name,
		       uint32_t job_id, uint32_t step_id)
{
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint64_t gres_needed, gres_avail;
	bitstr_t *gres_bit_alloc;
	int i, len;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if (job_gres_ptr->node_cnt == 0)	/* no_consume */
		return SLURM_SUCCESS;

	if (node_offset >= job_gres_ptr->node_cnt) {
		error("gres/%s: %s for %u.%u, node offset invalid "
		      "(%d >= %u)",
		      gres_name, __func__, job_id, step_id, node_offset,
		      job_gres_ptr->node_cnt);
		return SLURM_ERROR;
	}

//FIXME: Add support for other GRES count specifications
	gres_needed = step_gres_ptr->gres_per_node;
	if (step_gres_ptr->node_cnt == 0)
		step_gres_ptr->node_cnt = job_gres_ptr->node_cnt;
	if (!step_gres_ptr->gres_cnt_node_alloc) {
		step_gres_ptr->gres_cnt_node_alloc =
			xmalloc(sizeof(uint64_t) * step_gres_ptr->node_cnt);
	}
	if (job_gres_ptr->gres_cnt_node_alloc)
		gres_avail = job_gres_ptr->gres_cnt_node_alloc[node_offset];
	else
		gres_avail = job_gres_ptr->gres_per_node;
	if (gres_needed > gres_avail) {
		error("gres/%s: %s for %u.%u, step's > job's "
		      "for node %d (%"PRIu64" > %"PRIu64")",
		      gres_name, __func__, job_id, step_id, node_offset,
		      gres_needed, gres_avail);
		return SLURM_ERROR;
	}
	if (step_gres_ptr->gres_cnt_node_alloc &&
	    (node_offset < step_gres_ptr->node_cnt))
		step_gres_ptr->gres_cnt_node_alloc[node_offset] = gres_needed;
	if (!job_gres_ptr->gres_cnt_step_alloc) {
		job_gres_ptr->gres_cnt_step_alloc =
			xmalloc(sizeof(uint64_t) * job_gres_ptr->node_cnt);
	}

	if (gres_needed >
	    (gres_avail - job_gres_ptr->gres_cnt_step_alloc[node_offset])) {
		error("gres/%s: %s for %u.%u, step's > job's "
		      "remaining for node %d (%"PRIu64" > "
		      "(%"PRIu64" - %"PRIu64"))",
		      gres_name, __func__, job_id, step_id, node_offset,
		      gres_needed, gres_avail,
		      job_gres_ptr->gres_cnt_step_alloc[node_offset]);
		return SLURM_ERROR;
	}

	if (step_gres_ptr->node_in_use == NULL) {
		step_gres_ptr->node_in_use = bit_alloc(job_gres_ptr->node_cnt);
	}
	bit_set(step_gres_ptr->node_in_use, node_offset);
	job_gres_ptr->gres_cnt_step_alloc[node_offset] += gres_needed;

	if ((job_gres_ptr->gres_bit_alloc == NULL) ||
	    (job_gres_ptr->gres_bit_alloc[node_offset] == NULL)) {
		debug3("gres/%s: %s gres_bit_alloc for %u.%u is NULL",
		       gres_name, __func__, job_id, step_id);
		return SLURM_SUCCESS;
	}

	gres_bit_alloc = bit_copy(job_gres_ptr->gres_bit_alloc[node_offset]);
	if (job_gres_ptr->gres_bit_step_alloc &&
	    job_gres_ptr->gres_bit_step_alloc[node_offset]) {
		bit_and_not(gres_bit_alloc,
			job_gres_ptr->gres_bit_step_alloc[node_offset]);
	}

	len = bit_size(gres_bit_alloc);
	for (i = 0; i < len; i++) {
		if (gres_needed > 0) {
			if (bit_test(gres_bit_alloc, i))
				gres_needed--;
		} else {
			bit_clear(gres_bit_alloc, i);
		}
	}
	if (gres_needed) {
		error("gres/%s: %s step %u.%u oversubscribed resources on node %d",
		      gres_name, __func__, job_id, step_id, node_offset);
	}

	if (job_gres_ptr->gres_bit_step_alloc == NULL) {
		job_gres_ptr->gres_bit_step_alloc =
			xmalloc(sizeof(bitstr_t *) * job_gres_ptr->node_cnt);
	}
	if (job_gres_ptr->gres_bit_step_alloc[node_offset]) {
		bit_or(job_gres_ptr->gres_bit_step_alloc[node_offset],
		       gres_bit_alloc);
	} else {
		job_gres_ptr->gres_bit_step_alloc[node_offset] =
			bit_copy(gres_bit_alloc);
	}
	if (step_gres_ptr->gres_bit_alloc == NULL) {
		step_gres_ptr->gres_bit_alloc = xmalloc(sizeof(bitstr_t *) *
						       job_gres_ptr->node_cnt);
	}
	if (step_gres_ptr->gres_bit_alloc[node_offset]) {
		error("gres/%s: %s step %u.%u bit_alloc already exists",
		      gres_name, __func__, job_id, step_id);
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
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_step_alloc(List step_gres_list, List job_gres_list,
				  int node_offset, uint32_t job_id,
				  uint32_t step_id)
{
	int i, rc, rc2;
	ListIterator step_gres_iter,  job_gres_iter;
	gres_state_t *step_gres_ptr, *job_gres_ptr;

	if (step_gres_list == NULL)
		return SLURM_SUCCESS;
	if (job_gres_list == NULL) {
		error("%s: step allocates gres, but job %u has none",
		      __func__, job_id);
		return SLURM_ERROR;
	}

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	step_gres_iter = list_iterator_create(step_gres_list);
	while ((step_gres_ptr = (gres_state_t *) list_next(step_gres_iter))) {
		for (i = 0; i < gres_context_cnt; i++) {
			if (step_gres_ptr->plugin_id ==
			    gres_context[i].plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("%s: could not find plugin %u for step %u.%u",
			      __func__, step_gres_ptr->plugin_id,
			      job_id, step_id);
			rc = ESLURM_INVALID_GRES;
			break;
		}

		job_gres_iter = list_iterator_create(job_gres_list);
		while ((job_gres_ptr = (gres_state_t *)
				list_next(job_gres_iter))) {
			gres_job_state_t  *d_job_gres_ptr =
				(gres_job_state_t *) job_gres_ptr->gres_data;
			gres_step_state_t *d_step_gres_ptr =
				(gres_step_state_t *) step_gres_ptr->gres_data;
			/*
			 * Here we need to check the type along with the
			 * plugin_id just in case we have more than one plugin
			 * with the same name.
			 */
			if ((step_gres_ptr->plugin_id ==
			     job_gres_ptr->plugin_id) &&
			    (!d_step_gres_ptr->type_name ||
			     (d_job_gres_ptr->type_id ==
			      d_step_gres_ptr->type_id)))
				break;
		}
		list_iterator_destroy(job_gres_iter);
		if (job_gres_ptr == NULL) {
			info("%s: job %u lacks gres/%s for step %u", __func__,
			     job_id, gres_context[i].gres_name, step_id);
			rc = ESLURM_INVALID_GRES;
			break;
		}

		rc2 = _step_alloc(step_gres_ptr->gres_data,
				  job_gres_ptr->gres_data, node_offset,
				  gres_context[i].gres_name, job_id, step_id);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}


static int _step_dealloc(void *step_gres_data, void *job_gres_data,
			 char *gres_name, uint32_t job_id, uint32_t step_id)
{

	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint32_t i, j, node_cnt;
	uint64_t gres_cnt;
	int len_j, len_s;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if (job_gres_ptr->node_cnt == 0) {	/* no_consume */
		xassert(!step_gres_ptr->node_in_use);
		xassert(!step_gres_ptr->gres_bit_alloc);
		return SLURM_SUCCESS;
	}

	if (step_gres_ptr->node_in_use == NULL) {
		error("gres/%s: %s step %u.%u dealloc, node_in_use is NULL",
		      gres_name, __func__, job_id, step_id);
		return SLURM_ERROR;
	}

	node_cnt = MIN(job_gres_ptr->node_cnt, step_gres_ptr->node_cnt);
	for (i = 0; i < node_cnt; i++) {
		if (!bit_test(step_gres_ptr->node_in_use, i))
			continue;

		if (step_gres_ptr->gres_cnt_node_alloc)
			gres_cnt = step_gres_ptr->gres_cnt_node_alloc[i];
		else
			gres_cnt = step_gres_ptr->gres_per_node;
		if (job_gres_ptr->gres_cnt_step_alloc) {
			if (job_gres_ptr->gres_cnt_step_alloc[i] >=
			    gres_cnt) {
				job_gres_ptr->gres_cnt_step_alloc[i] -=
					gres_cnt;
			} else {
				error("gres/%s: %s step %u.%u dealloc count "
				      "underflow",
				      gres_name, __func__, job_id, step_id);
				job_gres_ptr->gres_cnt_step_alloc[i] = 0;
			}
		}
		if ((step_gres_ptr->gres_bit_alloc == NULL) ||
		    (step_gres_ptr->gres_bit_alloc[i] == NULL))
			continue;
		if (job_gres_ptr->gres_bit_alloc[i] == NULL) {
			error("gres/%s: %s job %u gres_bit_alloc[%d]"
			      " is NULL", __func__, gres_name, job_id, i);
			continue;
		}
		len_j = bit_size(job_gres_ptr->gres_bit_alloc[i]);
		len_s = bit_size(step_gres_ptr->gres_bit_alloc[i]);
		if (len_j != len_s) {
			error("gres/%s: %s step %u.%u dealloc, bit_alloc[%d] "
			      "size mis-match (%d != %d)",
			      gres_name, __func__, job_id, step_id,
			      i, len_j, len_s);
			len_j = MIN(len_j, len_s);
		}
		for (j = 0; j < len_j; j++) {
			if (!bit_test(step_gres_ptr->gres_bit_alloc[i], j))
				continue;
			if (job_gres_ptr->gres_bit_step_alloc &&
			    job_gres_ptr->gres_bit_step_alloc[i]) {
				bit_clear(job_gres_ptr->gres_bit_step_alloc[i],
					  j);
			}
		}
		FREE_NULL_BITMAP(step_gres_ptr->gres_bit_alloc[i]);
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
	int i, rc, rc2;
	ListIterator step_gres_iter,  job_gres_iter;
	gres_state_t *step_gres_ptr, *job_gres_ptr;

	if (step_gres_list == NULL)
		return SLURM_SUCCESS;
	if (job_gres_list == NULL) {
		error("%s: step deallocates gres, but job %u has none",
		      __func__, job_id);
		return SLURM_ERROR;
	}

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	step_gres_iter = list_iterator_create(step_gres_list);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((step_gres_ptr = list_next(step_gres_iter))) {
		list_iterator_reset(job_gres_iter);
		while ((job_gres_ptr = list_next(job_gres_iter))) {
			gres_job_state_t  *d_job_gres_ptr =
				(gres_job_state_t *) job_gres_ptr->gres_data;
			gres_step_state_t *d_step_gres_ptr =
				(gres_step_state_t *) step_gres_ptr->gres_data;
			/*
			 * Here we need to check the type along with the
			 * plugin_id just in case we have more than one plugin
			 * with the same name.
			 */
			if ((step_gres_ptr->plugin_id ==
			     job_gres_ptr->plugin_id) &&
			    (!d_step_gres_ptr->type_name ||
			     (d_job_gres_ptr->type_id ==
			      d_step_gres_ptr->type_id)))
				break;
		}

		if (job_gres_ptr == NULL)
			continue;

		for (i = 0; i < gres_context_cnt; i++) {
			if (step_gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			rc2 = _step_dealloc(step_gres_ptr->gres_data,
					   job_gres_ptr->gres_data,
					   gres_context[i].gres_name, job_id,
					   step_id);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
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
	plugin_id = _build_id(gres_name);

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
 * Fill in an array of GRES type IDs contained within the given job gres_list
 *		and an array of corresponding counts of those GRES types.
 * IN gres_list - a List of GRES types allocated to a job.
 * IN arr_len - Length of the arrays (the number of elements in the gres_list).
 * IN gres_count_ids, gres_count_vals - the GRES type ID's and values found
 *	 	in the gres_list.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_count(List gres_list, int arr_len,
				 uint32_t *gres_count_ids,
				 uint64_t *gres_count_vals)
{
	ListIterator  job_gres_iter;
	gres_state_t *job_gres_ptr;
	void         *job_gres_data;
	int           rc, ix = 0;

	rc = gres_plugin_init();
	if ((rc == SLURM_SUCCESS) && (arr_len <= 0))
		rc = EINVAL;
	if (rc != SLURM_SUCCESS)
		return rc;

	slurm_mutex_lock(&gres_context_lock);

	job_gres_iter = list_iterator_create(gres_list);
	while ((job_gres_ptr = (gres_state_t*) list_next(job_gres_iter))) {
		gres_job_state_t *job_gres_state_ptr;
		job_gres_data = job_gres_ptr->gres_data;
		job_gres_state_ptr = (gres_job_state_t *) job_gres_data;
		xassert(job_gres_state_ptr);

		gres_count_ids[ix]  = job_gres_ptr->plugin_id;
		gres_count_vals[ix] = job_gres_state_ptr->gres_per_node;
		if (++ix >= arr_len)
			break;
	}
	list_iterator_destroy(job_gres_iter);

	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Build a string identifying total GRES counts of each type
 * IN gres_list - a List of GRES types allocated to a job.
 * RET string containing comma-separated list of gres type:model:count
 *     must release memory using xfree()
 */
extern char *gres_plugin_job_alloc_count(List gres_list)
{
	ListIterator  job_gres_iter;
	gres_state_t *job_gres_ptr;
	void         *job_gres_data;
	char         *gres_alloc = NULL, *gres_name, *sep = "";
	int           i;

	(void) gres_plugin_init();
	slurm_mutex_lock(&gres_context_lock);

	job_gres_iter = list_iterator_create(gres_list);
	while ((job_gres_ptr = (gres_state_t*) list_next(job_gres_iter))) {
		gres_job_state_t *job_gres_state_ptr;
		job_gres_data = job_gres_ptr->gres_data;
		job_gres_state_ptr = (gres_job_state_t *) job_gres_data;
		if (!job_gres_state_ptr) {
			error("%s: job gres_data is NULL", __func__);
			continue;
		}
		gres_name = "UNKNOWN";
		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_context[i].plugin_id !=
			    job_gres_ptr->plugin_id)
				continue;
			gres_name = gres_context[i].gres_name;
		}

		if (job_gres_state_ptr->type_name) {
			xstrfmtcat(gres_alloc, "%s%s:%s:%"PRIu64, sep,
				   gres_name, job_gres_state_ptr->type_name,
				   job_gres_state_ptr->total_gres);
		} else {
			xstrfmtcat(gres_alloc, "%s%s:%"PRIu64, sep, gres_name,
				   job_gres_state_ptr->total_gres);
		}
		sep = ",";
	}
	list_iterator_destroy(job_gres_iter);

	slurm_mutex_unlock(&gres_context_lock);

	return gres_alloc;
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
				  int* gres_count_ids, int* gres_count_vals,
				  int val_type)
{
	ListIterator  node_gres_iter;
	gres_state_t* node_gres_ptr;
	void*         node_gres_data;
	uint32_t      val;
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
extern void gres_plugin_send_stepd(int fd)
{
	int i;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (gres_context[i].ops.send_stepd == NULL)
			continue;	/* No plugin to call */
		(*(gres_context[i].ops.send_stepd)) (fd);
	}
	slurm_mutex_unlock(&gres_context_lock);
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void gres_plugin_recv_stepd(int fd)
{
	int i;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i = 0; i < gres_context_cnt; i++) {
		if (gres_context[i].ops.recv_stepd == NULL)
			continue;	/* No plugin to call */
		(*(gres_context[i].ops.recv_stepd)) (fd);
	}
	slurm_mutex_unlock(&gres_context_lock);
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
	plugin_id = _build_id(gres_name);

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
 */
extern void gres_build_job_details(List job_gres_list,
				   uint32_t *gres_detail_cnt,
				   char ***gres_detail_str)
{
	int i, j;
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_gres_data;
	char *sep1, *sep2, tmp_str[128], *type, **my_gres_details = NULL;
	uint32_t my_gres_cnt = 0;

	/* Release any vestigial data (e.g. from job requeue) */
	for (i = 0; i < *gres_detail_cnt; i++)
		xfree(gres_detail_str[0][i]);
	xfree(*gres_detail_str);
	*gres_detail_cnt = 0;

	if (job_gres_list == NULL)	/* No GRES allocated */
		return;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		job_gres_data = (gres_job_state_t *) job_gres_ptr->gres_data;
		if (job_gres_data->gres_bit_alloc == NULL)
			continue;
		if (my_gres_details == NULL) {
			my_gres_cnt = job_gres_data->node_cnt;
			my_gres_details = xmalloc(sizeof(char *) * my_gres_cnt);
		}
		for (i = 0; i < gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			for (j = 0; j < my_gres_cnt; j++) {
				if (j >= job_gres_data->node_cnt)
					break;	/* node count mismatch */
				if (job_gres_data->gres_bit_alloc[j] == NULL)
					continue;
				if (my_gres_details[j])
					sep1 = ",";
				else
					sep1 = "";
				if (job_gres_data->type_name) {
					sep2 = ":";
					type = job_gres_data->type_name;
				} else {
					sep2 = "";
					type = "";
				}
				bit_fmt(tmp_str, sizeof(tmp_str),
                                        job_gres_data->gres_bit_alloc[j]);
				xstrfmtcat(my_gres_details[j], "%s%s%s%s(IDX:%s)",
					   sep1, gres_context[i].gres_name,
					   sep2, type, tmp_str);
			}
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
	*gres_detail_cnt = my_gres_cnt;
	*gres_detail_str = my_gres_details;
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
	plugin_id = _build_id(gres_name);

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

extern char *gres_2_tres_str(List gres_list, bool is_job, bool locked)
{
	ListIterator itr;
	slurmdb_tres_rec_t *tres_rec;
	gres_state_t *gres_state_ptr;
	int i;
	uint64_t count;
	char *col_name = NULL;
	char *tres_str = NULL;
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_req;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_req, 0, sizeof(slurmdb_tres_rec_t));
		tres_req.type = "gres";
	}

	if (!gres_list)
		return NULL;

	/* must be locked first before gres_contrex_lock!!! */
	if (!locked)
		assoc_mgr_lock(&locks);

	slurm_mutex_lock(&gres_context_lock);
	itr = list_iterator_create(gres_list);
	while ((gres_state_ptr = list_next(itr))) {
		if (is_job) {
			gres_job_state_t *gres_data_ptr = (gres_job_state_t *)
				gres_state_ptr->gres_data;
			col_name = gres_data_ptr->type_name;
//FIXME: Change to total_gres check below once field is set
			count = gres_data_ptr->gres_per_node *
				(uint64_t)gres_data_ptr->node_cnt;
		} else {
			gres_step_state_t *gres_data_ptr = (gres_step_state_t *)
				gres_state_ptr->gres_data;
			col_name = gres_data_ptr->type_name;
//FIXME: Change to total_gres check below once field is set
			count = gres_data_ptr->gres_per_node *
				(uint64_t)gres_data_ptr->node_cnt;
		}

		for (i = 0; i < gres_context_cnt; i++) {
			if (gres_context[i].plugin_id ==
			    gres_state_ptr->plugin_id) {
				tres_req.name = gres_context[i].gres_name;
				break;
			}
		}

		if (!tres_req.name) {
			debug("%s: couldn't find name", __func__);
			continue;
		}

		tres_rec = assoc_mgr_find_tres_rec(&tres_req);

		if (tres_rec &&
		    slurmdb_find_tres_count_in_string(
			    tres_str, tres_rec->id) == INFINITE64)
			/* New gres */
			xstrfmtcat(tres_str, "%s%u=%"PRIu64,
				   tres_str ? "," : "",
				   tres_rec->id, count);

		/* Now lets put of the : name tres if we are tracking
		 * it as well.  This would be handy for gres like
		 * gpu:tesla, where you might want to track both as
		 * TRES.
		 */
		if (col_name && (i < gres_context_cnt)) {
			tres_req.name = xstrdup_printf(
				"%s%s",
				gres_context[i].gres_name_colon,
				col_name);
			tres_rec = assoc_mgr_find_tres_rec(&tres_req);
			xfree(tres_req.name);
			if (tres_rec &&
			    slurmdb_find_tres_count_in_string(
				    tres_str, tres_rec->id) == INFINITE64)
				/* New gres */
				xstrfmtcat(tres_str, "%s%u=%"PRIu64,
					   tres_str ? "," : "",
					   tres_rec->id, count);
		}
	}
	list_iterator_destroy(itr);
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
	itr = list_iterator_create(gres_list);
	while ((gres_state_ptr = list_next(itr))) {
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
//FIXME: Change to total_gres check below once field is set
			count = gres_data_ptr->gres_per_node *
				(uint64_t)node_cnt;
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
			error("unsupported state type %d in %s",
			      state_type, __func__);
			continue;
		}
		/* Set main tres's count. */
		if ((tres_pos = assoc_mgr_find_tres_pos(&tres_rec, true)) != -1)
			tres_cnt[tres_pos] = count;

		/*
		 * Set TRES count for GRES model types. This would be handy for
		 * GRES like gpu:tesla, where you might want to track both as
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
			error("unsupported state type %d in %s",
			      state_type, __func__);
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

extern void destroy_gres_device(void *p)
{
	gres_device_t *gres_device = (gres_device_t *)p;
	if (!gres_device)
		return;

	xfree(gres_device->path);
	xfree(gres_device->major);
	xfree(gres_device);
}
