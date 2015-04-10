/*****************************************************************************\
 *  gres.c - driver for gres plugin
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2014 SchedMD LLC
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#if HAVE_CONFIG_H
#  include "config.h"
#  if STDC_HEADERS
#    include <string.h>
#  endif
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif /* HAVE_SYS_TYPES_H */
#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else /* ! HAVE_INTTYPES_H */
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif /* HAVE_INTTYPES_H */
#  ifdef HAVE_LIMITS_H
#    include <limits.h>
#  endif
#else /* ! HAVE_CONFIG_H */
#  include <limits.h>
#  include <sys/types.h>
#  include <stdint.h>
#  include <stdlib.h>
#  include <string.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
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
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define GRES_MAGIC 0x438a34d4
#define MAX_GRES_BITMAP 1024

/* Gres symbols provided by the plugin */
typedef struct slurm_gres_ops {
	int		(*node_config_load)	( List gres_conf_list );
	void		(*job_set_env)		( char ***job_env_ptr,
						  void *gres_ptr );
	void		(*step_set_env)		( char ***job_env_ptr,
						  void *gres_ptr );
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
} slurm_gres_context_t;

/* Generic gres data structure for adding to a list. Depending upon the
 * context, gres_data points to gres_node_state_t, gres_job_state_t or
 * gres_step_state_t */
typedef struct gres_state {
	uint32_t	plugin_id;
	void		*gres_data;
} gres_state_t;

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
static uint32_t	_build_id(char *gres_name);
static bitstr_t *_cpu_bitmap_rebuild(bitstr_t *old_cpu_bitmap, int new_size);
static void	_destroy_gres_slurmd_conf(void *x);
static void	_get_gres_cnt(gres_node_state_t *gres_data, char *orig_config,
			      char *gres_name, char *gres_name_colon,
			      int gres_name_colon_len);
static uint32_t	_get_tot_gres_cnt(uint32_t plugin_id, uint32_t *set_cnt);
static int	_gres_find_id(void *x, void *key);
static void	_gres_job_list_delete(void *list_element);
static bool	_is_gres_cnt_zero(char *config);
static int	_job_alloc(void *job_gres_data, void *node_gres_data,
			   int node_cnt, int node_offset, uint32_t cpu_cnt,
			   char *gres_name, uint32_t job_id, char *node_name,
			   bitstr_t *core_bitmap);
static void	_job_core_filter(void *job_gres_data, void *node_gres_data,
				 bool use_total_gres, bitstr_t *cpu_bitmap,
				 int cpu_start_bit, int cpu_end_bit,
				 char *gres_name, char *node_name);
static int	_job_dealloc(void *job_gres_data, void *node_gres_data,
			     int node_offset, char *gres_name, uint32_t job_id,
			     char *node_name);
static void	_job_state_delete(void *gres_data);
static void *	_job_state_dup(void *gres_data);
static void *	_job_state_dup2(void *gres_data, int node_index);
static void	_job_state_log(void *gres_data, uint32_t job_id,
			       char *gres_name);
static int	_job_state_validate(char *config, void **gres_data,
				    slurm_gres_context_t *gres_name);
static uint32_t	_job_test(void *job_gres_data, void *node_gres_data,
			  bool use_total_gres, bitstr_t *cpu_bitmap,
			  int cpu_start_bit, int cpu_end_bit, bool *topo_set,
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
			      uint32_t new_cnt, char *gres_name,
			      char *gres_name_colon, int gres_name_colon_len);
static int	_step_alloc(void *step_gres_data, void *job_gres_data,
			    int node_offset, int cpu_cnt, char *gres_name,
			    uint32_t job_id, uint32_t step_id);
static int	_step_dealloc(void *step_gres_data, void *job_gres_data,
			      char *gres_name, uint32_t job_id,
			      uint32_t step_id);
static void *	_step_state_dup(void *gres_data);
static void *	_step_state_dup2(void *gres_data, int node_index);
static void	_step_state_log(void *gres_data, uint32_t job_id,
				uint32_t step_id, char *gres_name);
static int	_step_state_validate(char *config, void **gres_data,
				     slurm_gres_context_t *context_ptr);
static uint32_t	_step_test(void *step_gres_data, void *job_gres_data,
			   int node_offset, bool ignore_alloc, char *gres_name,
			   uint32_t job_id, uint32_t step_id);
static int	_unload_gres_plugin(slurm_gres_context_t *plugin_context);
static void	_validate_config(slurm_gres_context_t *context_ptr);
static int	_validate_file(char *path_name, char *gres_name);
static void	_validate_gres_node_cpus(gres_node_state_t *node_gres_ptr,
					 int cpus_ctld, char *node_name);
static int	_valid_gres_type(char *gres_name, gres_node_state_t *gres_data,
				 uint16_t fast_schedule, char **reason_down);

/* Convert a gres_name into a number for faster comparision operations */
static uint32_t	_build_id(char *gres_name)
{
	int i, j;
	uint32_t id = 0;

	for (i=0, j=0; gres_name[i]; i++) {
		id += (gres_name[i] << j);
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
		"send_stepd",
		"recv_stepd",
		"job_info",
		"step_info",
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
 * Returns a SLURM errno.
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

	/* Insure that plugin_id is valid and unique */
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

/*
 * Terminate the gres plugin. Free memory.
 *
 * Returns a SLURM errno.
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
	xfree(p->file);		/* Only used by slurmd */
	xfree(p->name);
	xfree(p->type);
	xfree(p);
}

/*
 * Log the contents of a gres_slurmd_conf_t record
 */
static int _log_gres_slurmd_conf(void *x, void *arg)
{
	gres_slurmd_conf_t *p;

	p = (gres_slurmd_conf_t *) x;
	xassert(p);

	if (!gres_debug) {
		verbose("Gres Name=%s Type=%s Count=%u",
			p->name, p->type, p->count);
		return 0;
	}

	if (p->cpus) {
		info("Gres Name=%s Type=%s Count=%u ID=%u File=%s CPUs=%s "
		     "CpuCnt=%u",
		     p->name, p->type, p->count, p->plugin_id, p->file, p->cpus,
		     p->cpu_cnt);
	} else if (p->file) {
		info("Gres Name=%s Type=%s Count=%u ID=%u File=%s",
		     p->name, p->type, p->count, p->plugin_id, p->file);
	} else {
		info("Gres Name=%s Type=%s Count=%u ID=%u", p->name, p->type,
		     p->count, p->plugin_id);
	}

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
 * Build gres_slurmd_conf_t record based upon a line from the gres.conf file
 */
static int _parse_gres_config(void **dest, slurm_parser_enum_t type,
			      const char *key, const char *value,
			      const char *line, char **leftover)
{
	static s_p_options_t _gres_options[] = {
		{"Count", S_P_STRING},	/* Number of Gres available */
		{"CPUs" , S_P_STRING},	/* CPUs to bind to Gres resource */
		{"File",  S_P_STRING},	/* Path to Gres device */
		{"Name",  S_P_STRING},	/* Gres name */
		{"Type",  S_P_STRING},	/* Gres type (e.g. model name) */
		{NULL}
	};
	int i;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t *p;
	long tmp_long;
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
	if (s_p_get_string(&p->cpus, "CPUs", tbl)) {
		bitstr_t *cpu_bitmap;	/* Just use to validate config */
		cpu_bitmap = bit_alloc(gres_cpu_cnt);
		i = bit_unfmt(cpu_bitmap, p->cpus);
		if (i != 0) {
			fatal("Invalid gres data for %s, CPUs=%s (only %u CPUs"
			      " are available)",
			      p->name, p->cpus, gres_cpu_cnt);
		}
		FREE_NULL_BITMAP(cpu_bitmap);
	}

	if (s_p_get_string(&p->file, "File", tbl)) {
		p->count = _validate_file(p->file, p->name);
		p->has_file = 1;
	}

	if (s_p_get_string(&p->type, "Type", tbl) && !p->file) {
		p->file = xstrdup("/dev/null");
		p->has_file = 1;
	}

	if (s_p_get_string(&tmp_str, "Count", tbl)) {
		tmp_long = strtol(tmp_str, &last, 10);
		if ((tmp_long == LONG_MIN) || (tmp_long == LONG_MAX)) {
			fatal("Invalid gres data for %s, Count=%s", p->name,
			      tmp_str);
		}
		if ((last[0] == 'k') || (last[0] == 'K'))
			tmp_long *= 1024;
		else if ((last[0] == 'm') || (last[0] == 'M'))
			tmp_long *= (1024 * 1024);
		else if ((last[0] == 'g') || (last[0] == 'G'))
			tmp_long *= (1024 * 1024 * 1024);
		else if (last[0] != '\0') {
			fatal("Invalid gres data for %s, Count=%s", p->name,
			      tmp_str);
		}
		if (p->count && (p->count != tmp_long)) {
			fatal("Invalid gres data for %s, Count does not match "
			      "File value", p->name);
		}
		if ((tmp_long < 0) || (tmp_long >= NO_VAL)) {
			fatal("Gres %s has invalid count value %ld",
			      p->name, tmp_long);
		}
		p->count = tmp_long;
		xfree(tmp_str);
	} else if (p->count == 0)
		p->count = 1;

	s_p_hashtbl_destroy(tbl);

	for (i=0; i<gres_context_cnt; i++) {
		if (strcasecmp(p->name, gres_context[i].gres_name) == 0)
			break;
	}
	if (i >= gres_context_cnt) {
		error("Ignoring gres.conf Name=%s", p->name);
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
		{"File",  S_P_STRING},	/* Path to Gres device */
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
			has_type = (int) (gres_slurmd_conf->type != NULL);
		} else if (( has_type && !gres_slurmd_conf->type) ||
			   (!has_type &&  gres_slurmd_conf->type)) {
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

extern int gres_plugin_node_config_devices_path(char **dev_path,
						char **gres_name,
						int array_len,
						char *node_name)
{
	static s_p_options_t _gres_options[] = {
		{"Name",     S_P_ARRAY, _parse_gres_config,  NULL},
		{"NodeName", S_P_ARRAY, _parse_gres_config2, NULL},
		{NULL}
	};

	int count = 0, count2 = 0, i, j;
	struct stat config_stat;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t **gres_array;
	char *gres_conf_file;

	gres_plugin_init();
	gres_conf_file = get_extra_conf_path("gres.conf");
	if (stat(gres_conf_file, &config_stat) < 0) {
		error("can't stat gres.conf file %s: %m", gres_conf_file);
		xfree(gres_conf_file);
		return 0;
	}

	slurm_mutex_lock(&gres_context_lock);
	if (!gres_node_name && node_name)
		gres_node_name = xstrdup(node_name);
	tbl = s_p_hashtbl_create(_gres_options);
	if (s_p_parse_file(tbl, NULL, gres_conf_file, false) == SLURM_ERROR)
		fatal("error opening/reading %s", gres_conf_file);
	FREE_NULL_LIST(gres_conf_list);
	gres_conf_list = list_create(_destroy_gres_slurmd_conf);
	if (s_p_get_array((void ***) &gres_array, &count, "Name", tbl)) {
		if (count > array_len) {
			error("GRES device count exceeds array size (%d > %d)",
			      count, array_len);
			count = array_len;
		}
		for (i = 0; i < count; i++) {
			if ((gres_array[i]) && (gres_array[i]->file)) {
				dev_path[i]   = gres_array[i]->file;
				gres_name[i]  = gres_array[i]->name;
				gres_array[i] = NULL;
			}
		}
	}
	if (s_p_get_array((void ***) &gres_array, &count2, "NodeName", tbl)) {
		if ((count + count2) > array_len) {
			error("GRES device count exceeds array size (%d > %d)",
			      (count + count2), array_len);
			count2 = array_len - count;
		}
		for (i = 0, j = count; i < count2; i++, j++) {
			if ((gres_array[i]) && (gres_array[i]->file)) {
				dev_path[j]   = gres_array[i]->file;
				gres_name[j]  = gres_array[i]->name;
				gres_array[i] = NULL;
			}
		}
	}
	s_p_hashtbl_destroy(tbl);
	slurm_mutex_unlock(&gres_context_lock);

	xfree(gres_conf_file);
	return (count + count2);
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
 */
extern int gres_plugin_node_config_load(uint32_t cpu_cnt, char *node_name)
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

	for (i=0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
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
			pack32(gres_slurmd_conf->count, buffer);
			pack32(gres_slurmd_conf->cpu_cnt, buffer);
			pack8(gres_slurmd_conf->has_file, buffer);
			pack32(gres_slurmd_conf->plugin_id, buffer);
			packstr(gres_slurmd_conf->cpus, buffer);
			packstr(gres_slurmd_conf->name, buffer);
			packstr(gres_slurmd_conf->type, buffer);
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
extern int gres_plugin_node_config_unpack(Buf buffer, char* node_name)
{
	int i, j, rc;
	uint32_t count, cpu_cnt, magic, plugin_id, utmp32;
	uint16_t rec_cnt, version;
	uint8_t has_file;
	char *tmp_cpus, *tmp_name, *tmp_type;
	gres_slurmd_conf_t *p;

	rc = gres_plugin_init();

	FREE_NULL_LIST(gres_conf_list);
	gres_conf_list = list_create(_destroy_gres_slurmd_conf);

	safe_unpack16(&version, buffer);

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&gres_context_lock);
	if (version >= SLURM_14_11_PROTOCOL_VERSION) {
		for (i = 0; i < rec_cnt; i++) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;

			safe_unpack32(&count, buffer);
			safe_unpack32(&cpu_cnt, buffer);
			safe_unpack8(&has_file, buffer);
			safe_unpack32(&plugin_id, buffer);
			safe_unpackstr_xmalloc(&tmp_cpus, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_name, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_type, &utmp32, buffer);

	 		for (j = 0; j < gres_context_cnt; j++) {
	 			if (gres_context[j].plugin_id != plugin_id)
					continue;
				if (xstrcmp(gres_context[j].gres_name,
					    tmp_name)) {
					/* Should have beeen caught in
					 * gres_plugin_init() */
					error("gres_plugin_node_config_unpack: "
					      "gres/%s duplicate plugin ID with"
					      " %s, unable to process",
					      tmp_name,
					      gres_context[j].gres_name);
					continue;
				}
				if (gres_context[j].has_file &&
				    !has_file && count) {
					error("gres_plugin_node_config_unpack: "
					      "gres/%s lacks File parameter "
					      "for node %s",
					      tmp_name, node_name);
					has_file = 1;
				}
				if (has_file && (count > MAX_GRES_BITMAP)) {
					/* Avoid over-subscribing memory with
					 * huge bitmaps */
					error("%s: gres/%s has File plus very "
					      "large Count (%u) for node %s, "
					      "resetting value to %d",
					      __func__, tmp_name, count,
					      node_name, MAX_GRES_BITMAP);
					count = MAX_GRES_BITMAP;
				}
				if (has_file)	/* Don't clear if already set */
					gres_context[j].has_file = has_file;
				break;
	 		}
			if (j >= gres_context_cnt) {
				/* GresPlugins is inconsistently configured.
				 * Not a fatal error. Skip this data. */
				error("gres_plugin_node_config_unpack: no "
				      "plugin configured to unpack data "
				      "type %s from node %s",
				      tmp_name, node_name);
				xfree(tmp_cpus);
				xfree(tmp_name);
				continue;
			}
			p = xmalloc(sizeof(gres_slurmd_conf_t));
			p->count = count;
			p->cpu_cnt = cpu_cnt;
			p->has_file = has_file;
			p->cpus = tmp_cpus;
			tmp_cpus = NULL;	/* Nothing left to xfree */
			p->name = tmp_name;     /* Preserve for accounting! */
			p->type = tmp_type;
			tmp_type = NULL;	/* Nothing left to xfree */
			p->plugin_id = plugin_id;
			list_append(gres_conf_list, p);
		}
	} else {
		for (i = 0; i < rec_cnt; i++) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;

			safe_unpack32(&count, buffer);
			safe_unpack32(&cpu_cnt, buffer);
			safe_unpack8(&has_file, buffer);
			safe_unpack32(&plugin_id, buffer);
			safe_unpackstr_xmalloc(&tmp_cpus, &utmp32, buffer);
			safe_unpackstr_xmalloc(&tmp_name, &utmp32, buffer);

	 		for (j = 0; j < gres_context_cnt; j++) {
	 			if (gres_context[j].plugin_id != plugin_id)
					continue;
				if (xstrcmp(gres_context[j].gres_name,
					    tmp_name)) {
					/* Should have beeen caught in
					 * gres_plugin_init() */
					error("gres_plugin_node_config_unpack: "
					      "gres/%s duplicate plugin ID with"
					      " %s, unable to process",
					      tmp_name,
					      gres_context[j].gres_name);
					continue;
				}
				if (gres_context[j].has_file &&
				    !has_file && count) {
					error("gres_plugin_node_config_unpack: "
					      "gres/%s lacks File parameter "
					      "for node %s",
					      tmp_name, node_name);
					has_file = 1;
				}
				if (has_file && (count > 1024)) {
					/* Avoid over-subscribing memory with
					 * huge bitmaps */
					error("gres_plugin_node_config_unpack: "
					      "gres/%s has File plus very "
					      "large Count (%u) for node %s, "
					      "resetting value to 1024",
					      tmp_name, count, node_name);
					count = 1024;
				}
				if (has_file)	/* Don't clear if already set */
					gres_context[j].has_file = has_file;
				break;
	 		}
			if (j >= gres_context_cnt) {
				/* GresPlugins is inconsistently configured.
				 * Not a fatal error. Skip this data. */
				error("gres_plugin_node_config_unpack: no "
				      "plugin configured to unpack data "
				      "type %s from node %s",
				      tmp_name, node_name);
				xfree(tmp_cpus);
				xfree(tmp_name);
				continue;
			}
			p = xmalloc(sizeof(gres_slurmd_conf_t));
			p->count = count;
			p->cpu_cnt = cpu_cnt;
			p->has_file = has_file;
			p->cpus = tmp_cpus;
			tmp_cpus = NULL;	/* Nothing left to xfree */
			p->name = tmp_name;     /* Preserve for accounting! */
			p->plugin_id = plugin_id;
			list_append(gres_conf_list, p);
		}
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("gres_plugin_node_config_unpack: unpack error from node %s",
	      node_name);
	xfree(tmp_cpus);
	xfree(tmp_name);
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
		if (gres_node_ptr->topo_cpus_bitmap)
			FREE_NULL_BITMAP(gres_node_ptr->topo_cpus_bitmap[i]);
		if (gres_node_ptr->topo_gres_bitmap)
			FREE_NULL_BITMAP(gres_node_ptr->topo_gres_bitmap[i]);
		xfree(gres_node_ptr->topo_model[i]);
	}
	xfree(gres_node_ptr->topo_cpus_bitmap);
	xfree(gres_node_ptr->topo_gres_bitmap);
	xfree(gres_node_ptr->topo_gres_cnt_alloc);
	xfree(gres_node_ptr->topo_gres_cnt_avail);
	xfree(gres_node_ptr->topo_model);
	for (i = 0; i < gres_node_ptr->type_cnt; i++) {
		xfree(gres_node_ptr->type_model[i]);
	}
	xfree(gres_node_ptr->type_cnt_alloc);
	xfree(gres_node_ptr->type_cnt_avail);
	xfree(gres_node_ptr->type_model);
	xfree(gres_node_ptr);
	xfree(gres_ptr);
}

static void _add_gres_type(char *type, gres_node_state_t *gres_data,
			   uint32_t tmp_gres_cnt)
{
	int i;

	if (!strcasecmp(type, "no_consume")) {
		gres_data->no_consume = true;
		return;
	}

	for (i = 0; i < gres_data->type_cnt; i++) {
		if (xstrcmp(gres_data->type_model[i], type))
			continue;
		gres_data->type_cnt_avail[i] += tmp_gres_cnt;
		break;
	}

	if (i >= gres_data->type_cnt) {
		gres_data->type_cnt++;
		gres_data->type_cnt_alloc =
			xrealloc(gres_data->type_cnt_alloc,
				 sizeof(uint32_t) * gres_data->type_cnt);
		gres_data->type_cnt_avail =
			xrealloc(gres_data->type_cnt_avail,
				 sizeof(uint32_t) * gres_data->type_cnt);
		gres_data->type_model =
			xrealloc(gres_data->type_model,
				 sizeof(char *) * gres_data->type_cnt);
		gres_data->type_cnt_avail[i] += tmp_gres_cnt;
		gres_data->type_model[i] = xstrdup(type);
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
	uint32_t gres_config_cnt = 0, tmp_gres_cnt = 0;
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
			tmp_gres_cnt = strtol(num + 1, &last_num, 10);
			if (last_num[0] == '\0')
				;
			else if ((last_num[0] == 'k') || (last_num[0] == 'K'))
				tmp_gres_cnt *= 1024;
			else if ((last_num[0] == 'm') || (last_num[0] == 'M'))
				tmp_gres_cnt *= (1024 * 1024);
			else if ((last_num[0] == 'g') || (last_num[0] == 'G'))
				tmp_gres_cnt *= (1024 * 1024 * 1024);
			else {
				error("Bad GRES configuration: %s", tok);
				break;
			}
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
	uint32_t model_cnt;

	if (gres_data->type_cnt == 0)
		return 0;

	for (i = 0; i < gres_data->type_cnt; i++) {
		model_cnt = 0;
		for (j = 0; j < gres_data->topo_cnt; j++) {
			if (!xstrcmp(gres_data->type_model[i],
				     gres_data->topo_model[j]))
				model_cnt += gres_data->topo_gres_cnt_avail[j];
		}
		if (fast_schedule >= 2) {
			gres_data->type_cnt_avail[i] = model_cnt;
		} else if (model_cnt < gres_data->type_cnt_avail[i]) {
			xstrfmtcat(*reason_down,
				   "%s:%s count too low (%u < %u)",
				   gres_name, gres_data->type_model[i],
				   model_cnt, gres_data->type_cnt_avail[i]);
			return -1;
		}
	}
	return 0;
}

static void _set_gres_cnt(char *orig_config, char **new_config,
			  uint32_t new_cnt, char *gres_name,
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
			xstrfmtcat(new_configured_res, "%s:%uG",
				   gres_name, new_cnt);
		} else if ((new_cnt % (1024 * 1024)) == 0) {
			new_cnt /= (1024 * 1024);
			xstrfmtcat(new_configured_res, "%s:%uM",
				   gres_name, new_cnt);
		} else if ((new_cnt % 1024) == 0) {
			new_cnt /= 1024;
			xstrfmtcat(new_configured_res, "%s:%uK",
				   gres_name, new_cnt);
		} else {
			xstrfmtcat(new_configured_res, "%s:%u",
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
	gres_data->gres_cnt_config = NO_VAL;
	gres_data->gres_cnt_found  = NO_VAL;

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
	for (i=0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
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
static uint32_t _get_tot_gres_cnt(uint32_t plugin_id, uint32_t *set_cnt)
{
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	uint32_t gres_cnt = 0, cpu_set_cnt = 0, rec_cnt = 0;

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
		if (gres_slurmd_conf->cpus || gres_slurmd_conf->type)
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
		info("%s--The gres_conf_list is NULL!!!\n", __FUNCTION__);
		snprintf(gres_name, gres_name_len, "%u", gres_id);
		return rc;
	}

	iter = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = (gres_slurmd_conf_t *) list_next(iter))) {
		if (gres_slurmd_conf->plugin_id != gres_id)
			continue;
		strncpy(gres_name, gres_slurmd_conf->name, gres_name_len);
		found = 1;
		break;
	}
	list_iterator_destroy(iter);

	if (!found)	/* Could not find GRES type name, use id */
		snprintf(gres_name, gres_name_len, "%u", gres_id);

	return rc;
}

extern int _node_config_validate(char *node_name, char *orig_config,
				 char **new_config, gres_state_t *gres_ptr,
				 uint16_t fast_schedule, char **reason_down,
				 slurm_gres_context_t *context_ptr)
{
	int i, j, gres_inx, rc = SLURM_SUCCESS;
	uint32_t gres_cnt, set_cnt = 0;
	bool cpus_config = false, updated_config = false;
	gres_node_state_t *gres_data;
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;

	if (gres_ptr->gres_data == NULL)
		gres_ptr->gres_data = _build_gres_node_state();
	gres_data = (gres_node_state_t *) gres_ptr->gres_data;

	gres_cnt = _get_tot_gres_cnt(context_ptr->plugin_id, &set_cnt);
	if (gres_data->gres_cnt_found != gres_cnt) {
		if (gres_data->gres_cnt_found != NO_VAL) {
			info("%s: count changed for node %s from %u to %u",
			     context_ptr->gres_type, node_name,
			     gres_data->gres_cnt_found, gres_cnt);
		}
		if ((gres_data->gres_cnt_found != NO_VAL) &&
		    (gres_data->gres_cnt_alloc != 0)) {
			if (reason_down && (*reason_down == NULL)) {
				xstrfmtcat(*reason_down,
					   "%s count changed and jobs are "
					   "using them (%u != %u)",
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
			if (gres_data->topo_gres_bitmap) {
				FREE_NULL_BITMAP(gres_data->
						 topo_gres_bitmap[i]);
			}
			if (gres_data->topo_cpus_bitmap) {
				FREE_NULL_BITMAP(gres_data->
						 topo_cpus_bitmap[i]);
			}
			xfree(gres_data->topo_model[i]);
		}
		xfree(gres_data->topo_gres_bitmap);
		xfree(gres_data->topo_cpus_bitmap);
		xfree(gres_data->topo_model);
		gres_data->topo_cnt = set_cnt;
	}

	if (context_ptr->has_file && (set_cnt != gres_data->topo_cnt)) {
		/* Need to rebuild topology info */
		/* Resize the data structures here */
		gres_data->topo_gres_cnt_alloc =
			xrealloc(gres_data->topo_gres_cnt_alloc,
				 set_cnt * sizeof(uint32_t));
		gres_data->topo_gres_cnt_avail =
			xrealloc(gres_data->topo_gres_cnt_avail,
				 set_cnt * sizeof(uint32_t));
		for (i = 0; i < gres_data->topo_cnt; i++) {
			if (gres_data->topo_gres_bitmap) {
				FREE_NULL_BITMAP(gres_data->
						 topo_gres_bitmap[i]);
			}
			if (gres_data->topo_cpus_bitmap) {
				FREE_NULL_BITMAP(gres_data->
						 topo_cpus_bitmap[i]);
			}
			xfree(gres_data->topo_model[i]);
		}
		gres_data->topo_gres_bitmap =
			xrealloc(gres_data->topo_gres_bitmap,
				 set_cnt * sizeof(bitstr_t *));
		gres_data->topo_cpus_bitmap =
			xrealloc(gres_data->topo_cpus_bitmap,
				 set_cnt * sizeof(bitstr_t *));
		gres_data->topo_model = xrealloc(gres_data->topo_model,
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
				gres_data->topo_cpus_bitmap[i] =
					bit_alloc(gres_slurmd_conf->cpu_cnt);
				bit_unfmt(gres_data->topo_cpus_bitmap[i],
					  gres_slurmd_conf->cpus);
				cpus_config = true;
			} else if (cpus_config) {
				error("%s: has CPUs configured for only"
				      " some of the records on node %s",
				      context_ptr->gres_type, node_name);
			}
			gres_data->topo_gres_bitmap[i] = bit_alloc(gres_cnt);
			for (j = 0; j < gres_slurmd_conf->count; j++) {
				bit_set(gres_data->topo_gres_bitmap[i],
					gres_inx++);
			}
			gres_data->topo_model[i] = xstrdup(gres_slurmd_conf->
							   type);
			i++;
		}
		list_iterator_destroy(iter);
	}

	if ((orig_config == NULL) || (orig_config[0] == '\0'))
		gres_data->gres_cnt_config = 0;
	else if (gres_data->gres_cnt_config == NO_VAL) {
		/* This should have been filled in by _node_config_init() */
		_get_gres_cnt(gres_data, orig_config,
			      context_ptr->gres_name,
			      context_ptr->gres_name_colon,
			      context_ptr->gres_name_colon_len);
	}

	if ((gres_data->gres_cnt_config == 0) || (fast_schedule > 0))
		gres_data->gres_cnt_avail = gres_data->gres_cnt_config;
	else if (gres_data->gres_cnt_found != NO_VAL)
		gres_data->gres_cnt_avail = gres_data->gres_cnt_found;
	else if (gres_data->gres_cnt_avail == NO_VAL)
		gres_data->gres_cnt_avail = 0;

	if (context_ptr->has_file) {
		if (gres_data->gres_cnt_avail > MAX_GRES_BITMAP) {
			error("%s: gres/%s has File plus very large Count (%u) "
			      "for node %s, resetting value to %u",
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
			xstrfmtcat(*reason_down, "%s count too low (%u < %u)",
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
		error("%s on node %s configured for %u resources but %u found,"
		      " ignoring topology support",
		      context_ptr->gres_type, node_name,
		      gres_data->gres_cnt_config, gres_data->gres_cnt_found);
		if (gres_data->topo_cpus_bitmap) {
			for (i = 0; i < gres_data->topo_cnt; i++) {
				if (gres_data->topo_cpus_bitmap) {
					FREE_NULL_BITMAP(gres_data->
							 topo_cpus_bitmap[i]);
				}
				if (gres_data->topo_gres_bitmap) {
					FREE_NULL_BITMAP(gres_data->
							 topo_gres_bitmap[i]);
				}
				xfree(gres_data->topo_model[i]);
			}
			xfree(gres_data->topo_cpus_bitmap);
			xfree(gres_data->topo_gres_bitmap);
			xfree(gres_data->topo_gres_cnt_alloc);
			xfree(gres_data->topo_gres_cnt_avail);
			xfree(gres_data->topo_model);
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
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int gres_plugin_node_config_validate(char *node_name,
					    char *orig_config,
					    char **new_config,
					    List *gres_list,
					    uint16_t fast_schedule,
					    char **reason_down)
{
	int i, rc, rc2;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_node_list_delete);
	}
	for (i=0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
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
					    gres_ptr, fast_schedule,
					    reason_down, &gres_context[i]);
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
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
	_get_gres_cnt(gres_data, orig_config,
		      context_ptr->gres_name,
		      context_ptr->gres_name_colon,
		      context_ptr->gres_name_colon_len);
	if ((gres_data->gres_cnt_config == 0) || (fast_schedule > 0))
		gres_data->gres_cnt_avail = gres_data->gres_cnt_config;
	else if (gres_data->gres_cnt_found != NO_VAL)
		gres_data->gres_cnt_avail = gres_data->gres_cnt_found;
	else if (gres_data->gres_cnt_avail == NO_VAL)
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
	    (gres_data->gres_cnt_found != NO_VAL) &&
	    (gres_data->gres_cnt_found <  gres_data->gres_cnt_config)) {
		/* Do not set node DOWN, but give the node
		 * a chance to register with more resources */
		gres_data->gres_cnt_found = NO_VAL;
	} else if ((fast_schedule == 0) &&
		   (gres_data->gres_cnt_found != NO_VAL) &&
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
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_node_list_delete);
	}
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

	if (gres_list == NULL)
		return rc;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		gres_node_ptr = (gres_node_state_t *) gres_ptr->gres_data;
		pack32(magic, buffer);
		pack32(gres_ptr->plugin_id, buffer);
		pack32(gres_node_ptr->gres_cnt_avail, buffer);
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
	uint32_t gres_cnt_avail, magic, plugin_id;
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
			safe_unpack32(&gres_cnt_avail, buffer);
			safe_unpack8(&has_bitmap, buffer);
		} else {
			error("gres_plugin_node_state_unpack: protocol_version"
			      " %hu not supported", protocol_version);
			goto unpack_error;
		}
		for (i=0; i<gres_context_cnt; i++) {
			if (gres_context[i].plugin_id == plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("gres_plugin_node_state_unpack: no plugin "
			      "configured to unpack data type %u from node %s",
			      plugin_id, node_name);
			/* A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data. */
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
	new_gres->topo_cpus_bitmap = xmalloc(gres_ptr->topo_cnt *
					     sizeof(bitstr_t *));
	new_gres->topo_gres_bitmap = xmalloc(gres_ptr->topo_cnt *
					     sizeof(bitstr_t *));
	new_gres->topo_gres_cnt_alloc = xmalloc(gres_ptr->topo_cnt *
						sizeof(uint32_t));
	new_gres->topo_gres_cnt_avail = xmalloc(gres_ptr->topo_cnt *
						sizeof(uint32_t));
	new_gres->topo_model = xmalloc(gres_ptr->topo_cnt * sizeof(char *));
	for (i = 0; i < gres_ptr->topo_cnt; i++) {
		if (gres_ptr->topo_cpus_bitmap[i]) {
			new_gres->topo_cpus_bitmap[i] =
				bit_copy(gres_ptr->topo_cpus_bitmap[i]);
		}
		new_gres->topo_gres_bitmap[i] =
			bit_copy(gres_ptr->topo_gres_bitmap[i]);
		new_gres->topo_gres_cnt_alloc[i] =
			gres_ptr->topo_gres_cnt_alloc[i];
		new_gres->topo_gres_cnt_avail[i] =
			gres_ptr->topo_gres_cnt_avail[i];
		new_gres->topo_model[i] = xstrdup(gres_ptr->topo_model[i]);
	}

	new_gres->type_cnt       = gres_ptr->type_cnt;
	new_gres->type_cnt_alloc = xmalloc(gres_ptr->type_cnt *
					   sizeof(uint32_t));
	new_gres->type_cnt_avail = xmalloc(gres_ptr->type_cnt *
					   sizeof(uint32_t));
	new_gres->type_model = xmalloc(gres_ptr->type_cnt * sizeof(char *));
	for (i = 0; i < gres_ptr->type_cnt; i++) {
		new_gres->type_cnt_alloc[i] = gres_ptr->type_cnt_alloc[i];
		new_gres->type_cnt_avail[i] = gres_ptr->type_cnt_avail[i];
		new_gres->type_model[i] = xstrdup(gres_ptr->type_model[i]);
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
	int i;

	xassert(gres_data);
	gres_node_ptr = (gres_node_state_t *) gres_data;

	if (gres_node_ptr->gres_used) {
		;	/* Used cached value */
	} else if (gres_node_ptr->type_cnt == 0) {
		if (gres_node_ptr->no_consume) {
			xstrfmtcat(gres_node_ptr->gres_used, "%s:0", gres_name);
		} else {
			xstrfmtcat(gres_node_ptr->gres_used, "%s:%u",
				   gres_name, gres_node_ptr->gres_cnt_alloc);
		}
	} else {
		for (i = 0; i < gres_node_ptr->type_cnt; i++) {
			if (gres_node_ptr->no_consume) {
				xstrfmtcat(gres_node_ptr->gres_used,
					   "%s%s:%s:0", sep, gres_name,
					   gres_node_ptr->type_model[i]);
			} else {
				xstrfmtcat(gres_node_ptr->gres_used,
					   "%s%s:%s:%u", sep, gres_name,
					   gres_node_ptr->type_model[i],
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
	if (gres_node_ptr->gres_cnt_found == NO_VAL) {
		snprintf(tmp_str, sizeof(tmp_str), "TBD");
	} else {
		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 gres_node_ptr->gres_cnt_found);
	}

	if (gres_node_ptr->no_consume) {
		info("  gres_cnt found:%s configured:%u avail:%u no_consume",
		     tmp_str, gres_node_ptr->gres_cnt_config,
		     gres_node_ptr->gres_cnt_avail);
	} else {
		info("  gres_cnt found:%s configured:%u avail:%u alloc:%u",
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
		if (gres_node_ptr->topo_cpus_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_node_ptr->topo_cpus_bitmap[i]);
			info("  topo_cpus_bitmap[%d]:%s", i, tmp_str);
		} else
			info("  topo_cpus_bitmap[%d]:NULL", i);
		if (gres_node_ptr->topo_gres_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_node_ptr->topo_gres_bitmap[i]);
			info("  topo_gres_bitmap[%d]:%s", i, tmp_str);
		} else
			info("  topo_gres_bitmap[%d]:NULL", i);
		info("  topo_gres_cnt_alloc[%d]:%u", i,
		     gres_node_ptr->topo_gres_cnt_alloc[i]);
		info("  topo_gres_cnt_avail[%d]:%u", i,
		     gres_node_ptr->topo_gres_cnt_avail[i]);
		info("  type[%d]:%s", i, gres_node_ptr->topo_model[i]);
	}

	for (i = 0; i < gres_node_ptr->type_cnt; i++) {
		info("  type_cnt_alloc[%d]:%u", i,
		     gres_node_ptr->type_cnt_alloc[i]);
		info("  type_cnt_avail[%d]:%u", i,
		     gres_node_ptr->type_cnt_avail[i]);
		info("  type[%d]:%s", i, gres_node_ptr->type_model[i]);
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
	xfree(gres_ptr->gres_bit_step_alloc);
	xfree(gres_ptr->gres_cnt_step_alloc);
	xfree(gres_ptr->type_model);
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

static int _job_state_validate(char *config, void **gres_data,
			       slurm_gres_context_t *context_ptr)
{
	gres_job_state_t *gres_ptr;
	char *type = NULL, *num = NULL, *last_num = NULL;
	long cnt;

	if (!xstrcmp(config, context_ptr->gres_name)) {
		cnt = 1;
	} else if (!xstrncmp(config, context_ptr->gres_name_colon,
			     context_ptr->gres_name_colon_len)) {
		type = strchr(config, ':');
		num = strrchr(config, ':');
		if (!num)
			return SLURM_ERROR;
		cnt = strtol(num + 1, &last_num, 10);
		if (last_num[0] == '\0')
			;
		else if ((last_num[0] == 'k') || (last_num[0] == 'K'))
			cnt *= 1024;
		else if ((last_num[0] == 'm') || (last_num[0] == 'M'))
			cnt *= (1024 * 1024);
		else if ((last_num[0] == 'g') || (last_num[0] == 'G'))
			cnt *= (1024 * 1024 * 1024);
		else
			return SLURM_ERROR;
		if ((cnt < 0) || (cnt > 0xffffffff))
			return SLURM_ERROR;
	} else {
		/* Did not find this GRES name, check for zero value */
		num = strrchr(config, ':');
		if (num) {
			cnt = strtol(num + 1, &last_num, 10);
			if ((last_num[0] != '\0') || (cnt != 0))
				return SLURM_ERROR;
		} else
			return SLURM_ERROR;
	}

	if (cnt == 0) {
		*gres_data = NULL;
	} else {
		gres_ptr = xmalloc(sizeof(gres_job_state_t));
		gres_ptr->gres_cnt_alloc = (uint32_t) cnt;
		if (type && num && (type != num)) {
			type++;
			num[0] = '\0';
			gres_ptr->type_model = xstrdup(type);
		}
		*gres_data = gres_ptr;
	}

	return SLURM_SUCCESS;
}

static bool _is_gres_cnt_zero(char *config)
{
	char *num = NULL;
	long cnt;

	num = strrchr(config, ':');
	if (num)
		cnt = strtol(num + 1, NULL, 10);
	else
		cnt = 1;
	if (cnt == 0)
		return true;
	return false;
}

/*
 * Given a job's requested gres configuration, validate it and build a gres list
 * IN req_config - job request's gres input string
 * OUT gres_list - List of Gres records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_job_state_validate(char *req_config, List *gres_list)
{
	char *tmp_str, *tok, *last = NULL;
	int i, rc, rc2;
	gres_state_t *gres_ptr;
	void *job_gres_data;

	if ((req_config == NULL) || (req_config[0] == '\0')) {
		*gres_list = NULL;
		return SLURM_SUCCESS;
	}

	if ((rc = gres_plugin_init()) != SLURM_SUCCESS)
		return rc;

	slurm_mutex_lock(&gres_context_lock);
	tmp_str = xstrdup(req_config);
	tok = strtok_r(tmp_str, ",", &last);
	while (tok && (rc == SLURM_SUCCESS)) {
		rc2 = SLURM_ERROR;
		for (i = 0; i < gres_context_cnt; i++) {
			job_gres_data = NULL;
			rc2 = _job_state_validate(tok, &job_gres_data,
						  &gres_context[i]);
			if (rc2 != SLURM_SUCCESS)
				continue;
			if (job_gres_data == NULL)    /* Name match, count=0 */
				break;
			if (*gres_list == NULL)
				*gres_list = list_create(_gres_job_list_delete);
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = gres_context[i].plugin_id;
			gres_ptr->gres_data = job_gres_data;
			list_append(*gres_list, gres_ptr);
			break;		/* processed it */
		}
		if ((i >= gres_context_cnt) && _is_gres_cnt_zero(tok))
			rc2 = SLURM_SUCCESS;
		if (rc2 != SLURM_SUCCESS) {
			info("Invalid gres job specification %s", tok);
			rc = ESLURM_INVALID_GRES;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	slurm_mutex_unlock(&gres_context_lock);

	xfree(tmp_str);
	return rc;
}

static void *_job_state_dup(void *gres_data)
{

	int i;
	gres_job_state_t *gres_ptr = (gres_job_state_t *) gres_data;
	gres_job_state_t *new_gres_ptr;

	if (gres_ptr == NULL)
		return NULL;

	new_gres_ptr = xmalloc(sizeof(gres_job_state_t));
	new_gres_ptr->gres_cnt_alloc	= gres_ptr->gres_cnt_alloc;
	new_gres_ptr->node_cnt		= gres_ptr->node_cnt;
	new_gres_ptr->type_model	= xstrdup(gres_ptr->type_model);

	if (gres_ptr->gres_bit_alloc) {
		new_gres_ptr->gres_bit_alloc = xmalloc(sizeof(bitstr_t *) *
						       gres_ptr->node_cnt);
		for (i=0; i<gres_ptr->node_cnt; i++) {
			if (gres_ptr->gres_bit_alloc[i] == NULL)
				continue;
			new_gres_ptr->gres_bit_alloc[i] =
				bit_copy(gres_ptr->gres_bit_alloc[i]);
		}
	}
	return new_gres_ptr;
}

static void *_job_state_dup2(void *gres_data, int node_index)
{

	gres_job_state_t *gres_ptr = (gres_job_state_t *) gres_data;
	gres_job_state_t *new_gres_ptr;

	if (gres_ptr == NULL)
		return NULL;

	new_gres_ptr = xmalloc(sizeof(gres_job_state_t));
	new_gres_ptr->gres_cnt_alloc	= gres_ptr->gres_cnt_alloc;
	new_gres_ptr->node_cnt		= 1;
	new_gres_ptr->type_model	= xstrdup(gres_ptr->type_model);

	if (gres_ptr->gres_bit_alloc && gres_ptr->gres_bit_alloc[node_index]) {
		new_gres_ptr->gres_bit_alloc	= xmalloc(sizeof(bitstr_t *));
		new_gres_ptr->gres_bit_alloc[0] =
				bit_copy(gres_ptr->gres_bit_alloc[node_index]);
	}
	return new_gres_ptr;
}

/*
 * Create a (partial) copy of a job's gres state for job binding
 * IN gres_list - List of Gres records for this job to track usage
 * RET The copy or NULL on failure
 * NOTE: Only gres_cnt_alloc, node_cnt and gres_bit_alloc are copied
 *	 Job step details are NOT copied.
 */
List gres_plugin_job_state_dup(List gres_list)
{
	return gres_plugin_job_state_extract(gres_list, -1);
}

/*
 * Create a (partial) copy of a job's gres state for a particular node index
 * IN gres_list - List of Gres records for this job to track usage
 * IN node_index - zero-origin index to the node
 * RET The copy or NULL on failure
 */
List gres_plugin_job_state_extract(List gres_list, int node_index)
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
		if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			pack32(gres_job_ptr->gres_cnt_alloc, buffer);
			packstr(gres_job_ptr->type_model, buffer);
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
					pack32(gres_job_ptr->
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
			pack32(gres_job_ptr->gres_cnt_alloc, buffer);
			pack32(gres_job_ptr->node_cnt, buffer);
			if (gres_job_ptr->gres_bit_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					pack_bit_str(gres_job_ptr->
						     gres_bit_alloc[i],
						     buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (details && gres_job_ptr->gres_bit_step_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					pack_bit_str(gres_job_ptr->
						     gres_bit_step_alloc[i],
						     buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			if (details && gres_job_ptr->gres_cnt_step_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i = 0; i < gres_job_ptr->node_cnt; i++) {
					pack32(gres_job_ptr->
					       gres_cnt_step_alloc[i],
					       buffer);
				}
			} else {
				pack8((uint8_t) 0, buffer);
			}
			rec_cnt++;
		} else {
			error("gres_plugin_node_state_pack: protocol_version"
			      " %hu not supported", protocol_version);
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
	uint32_t magic, plugin_id, utmp32;
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

		if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_job_ptr = xmalloc(sizeof(gres_job_state_t));
			safe_unpack32(&gres_job_ptr->gres_cnt_alloc, buffer);
			safe_unpackstr_xmalloc(&gres_job_ptr->type_model,
					       &utmp32, buffer);
			safe_unpack32(&gres_job_ptr->node_cnt, buffer);
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
					xmalloc(sizeof(uint32_t) *
						gres_job_ptr->node_cnt);
				for (i=0; i<gres_job_ptr->node_cnt; i++) {
					safe_unpack32(&gres_job_ptr->
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
			safe_unpack32(&gres_job_ptr->gres_cnt_alloc, buffer);
			safe_unpack32(&gres_job_ptr->node_cnt, buffer);
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				gres_job_ptr->gres_bit_alloc =
					xmalloc(sizeof(bitstr_t *) *
						gres_job_ptr->node_cnt);
				for (i=0; i<gres_job_ptr->node_cnt; i++) {
					unpack_bit_str(&gres_job_ptr->
						       gres_bit_alloc[i],
						       buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				gres_job_ptr->gres_bit_step_alloc =
					xmalloc(sizeof(bitstr_t *) *
						gres_job_ptr->node_cnt);
				for (i=0; i<gres_job_ptr->node_cnt; i++) {
					unpack_bit_str(&gres_job_ptr->
						       gres_bit_step_alloc[i],
						       buffer);
				}
			}
			safe_unpack8(&has_more, buffer);
			if (has_more) {
				gres_job_ptr->gres_cnt_step_alloc =
					xmalloc(sizeof(uint32_t) *
						gres_job_ptr->node_cnt);
				for (i=0; i<gres_job_ptr->node_cnt; i++) {
					safe_unpack32(&gres_job_ptr->
						      gres_cnt_step_alloc[i],
						      buffer);
				}
			}
		} else {
			error("gres_plugin_job_state_unpack: protocol_version"
			      " %hu not supported", protocol_version);
			goto unpack_error;
		}

		for (i=0; i<gres_context_cnt; i++) {
			if (gres_context[i].plugin_id == plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			/* A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data. */
			error("gres_plugin_job_state_unpack: no plugin "
			      "configured to unpack data type %u from job %u",
			      plugin_id, job_id);
			_job_state_delete(gres_job_ptr);
			continue;
		}
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

/* If CPU bitmap from slurmd differs in size from that in slurmctld,
 * then modify bitmap from slurmd so we can use bit_and, bit_or, etc. */
static bitstr_t *_cpu_bitmap_rebuild(bitstr_t *old_cpu_bitmap, int new_size)
{
	int i, j, old_size, ratio;
	bitstr_t *new_cpu_bitmap;

	new_cpu_bitmap = bit_alloc(new_size);
	old_size = bit_size(old_cpu_bitmap);
	if (old_size > new_size) {
		ratio = old_size / new_size;
		for (i=0; i<new_size; i++) {
			for (j=0; j<ratio; j++) {
				if (bit_test(old_cpu_bitmap, i*ratio+j)) {
					bit_set(new_cpu_bitmap, i);
					break;
				}
			}
		}
	} else {
		ratio = new_size / old_size;
		for (i=0; i<old_size; i++) {
			if (!bit_test(old_cpu_bitmap, i))
				continue;
			for (j=0; j<ratio; j++) {
				bit_set(new_cpu_bitmap, i*ratio+j);
			}
		}
	}

	return new_cpu_bitmap;
}

static void _validate_gres_node_cpus(gres_node_state_t *node_gres_ptr,
				     int cpus_ctld, char *node_name)
{
	int i, cpus_slurmd;
	bitstr_t *new_cpu_bitmap;
	int log_mismatch = true;

	if (node_gres_ptr->topo_cnt == 0)
		return;

	if (node_gres_ptr->topo_cpus_bitmap == NULL) {
		error("Gres topo_cpus_bitmap is NULL on node %s", node_name);
		return;
	}


	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		if (!node_gres_ptr->topo_cpus_bitmap[i])
			continue;
		cpus_slurmd = bit_size(node_gres_ptr->topo_cpus_bitmap[i]);
		if (cpus_slurmd == cpus_ctld)
			continue;
		if (log_mismatch) {
			debug("Gres CPU count mismatch on node %s (%d != %d)",
			      node_name, cpus_slurmd, cpus_ctld);
			log_mismatch = false;
		}
		new_cpu_bitmap = _cpu_bitmap_rebuild(node_gres_ptr->
						     topo_cpus_bitmap[i],
						     cpus_ctld);
		FREE_NULL_BITMAP(node_gres_ptr->topo_cpus_bitmap[i]);
		node_gres_ptr->topo_cpus_bitmap[i] = new_cpu_bitmap;
	}
}

static void	_job_core_filter(void *job_gres_data, void *node_gres_data,
				 bool use_total_gres, bitstr_t *cpu_bitmap,
				 int cpu_start_bit, int cpu_end_bit,
				 char *gres_name, char *node_name)
{
	int i, j, cpus_ctld;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bitstr_t *avail_cpu_bitmap = NULL;

	if (!node_gres_ptr->topo_cnt || !cpu_bitmap ||	/* No topology info */
	    !job_gres_ptr->gres_cnt_alloc)		/* No job GRES */
		return;

	/* Determine which specific CPUs can be used */
	avail_cpu_bitmap = bit_copy(cpu_bitmap);
	bit_nclear(avail_cpu_bitmap, cpu_start_bit, cpu_end_bit);
	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		if (node_gres_ptr->topo_gres_cnt_avail[i] == 0)
			continue;
		if (!use_total_gres &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] >=
		     node_gres_ptr->topo_gres_cnt_avail[i]))
			continue;
		if (job_gres_ptr->type_model &&
		    (!node_gres_ptr->topo_model[i] ||
		     xstrcmp(job_gres_ptr->type_model,
			     node_gres_ptr->topo_model[i])))
			continue;
		if (!node_gres_ptr->topo_cpus_bitmap[i]) {
			FREE_NULL_BITMAP(avail_cpu_bitmap);	/* No filter */
			return;
		}
		cpus_ctld = cpu_end_bit - cpu_start_bit + 1;
		_validate_gres_node_cpus(node_gres_ptr, cpus_ctld, node_name);
		cpus_ctld = bit_size(node_gres_ptr->topo_cpus_bitmap[i]);
		for (j = 0; j < cpus_ctld; j++) {
			if (bit_test(node_gres_ptr->topo_cpus_bitmap[i], j)) {
				bit_set(avail_cpu_bitmap, cpu_start_bit + j);
			}
		}
	}
	bit_and(cpu_bitmap, avail_cpu_bitmap);
	FREE_NULL_BITMAP(avail_cpu_bitmap);
}

static uint32_t _job_test(void *job_gres_data, void *node_gres_data,
			  bool use_total_gres, bitstr_t *cpu_bitmap,
			  int cpu_start_bit, int cpu_end_bit, bool *topo_set,
			  uint32_t job_id, char *node_name, char *gres_name)
{
	int i, j, cpu_size, cpus_ctld, gres_avail = 0, gres_total, top_inx;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	uint32_t *cpus_addnt = NULL;  /* Additional CPUs avail from this GRES */
	uint32_t *cpus_avail = NULL;  /* CPUs initially avail from this GRES */
	uint32_t cpu_cnt = 0;
	bitstr_t *alloc_cpu_bitmap = NULL;

	if (node_gres_ptr->no_consume)
		use_total_gres = true;

	if (job_gres_ptr->gres_cnt_alloc && node_gres_ptr->topo_cnt &&
	    *topo_set) {
		/* Need to determine how many gres available for these
		 * specific CPUs */
		if (cpu_bitmap) {
			cpus_ctld = cpu_end_bit - cpu_start_bit + 1;
			if (cpus_ctld < 1) {
				error("gres/%s: job %u cpus on node %s < 1",
				      gres_name, job_id, node_name);
				return (uint32_t) 0;
			}
			_validate_gres_node_cpus(node_gres_ptr, cpus_ctld,
						 node_name);
		}
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			if (job_gres_ptr->type_model &&
			    (!node_gres_ptr->topo_model[i] ||
			     xstrcmp(node_gres_ptr->topo_model[i],
				     job_gres_ptr->type_model)))
				continue;
			if (!node_gres_ptr->topo_cpus_bitmap[i]) {
				gres_avail += node_gres_ptr->
					      topo_gres_cnt_avail[i];
				if (!use_total_gres) {
					gres_avail -= node_gres_ptr->
						      topo_gres_cnt_alloc[i];
				}
				continue;
			}
			cpus_ctld = bit_size(node_gres_ptr->
					     topo_cpus_bitmap[i]);
			for (j = 0; j < cpus_ctld; j++) {
				if (cpu_bitmap &&
				    !bit_test(cpu_bitmap, cpu_start_bit+j))
					continue;
				if (!bit_test(node_gres_ptr->
					      topo_cpus_bitmap[i], j))
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
		if (job_gres_ptr->gres_cnt_alloc > gres_avail)
			return (uint32_t) 0;	/* insufficient, gres to use */
		return NO_VAL;
	} else if (job_gres_ptr->gres_cnt_alloc && node_gres_ptr->topo_cnt) {
		/* Need to determine which specific CPUs can be used */
		gres_avail = node_gres_ptr->gres_cnt_avail;
		if (!use_total_gres)
			gres_avail -= node_gres_ptr->gres_cnt_alloc;
		if (job_gres_ptr->gres_cnt_alloc > gres_avail)
			return (uint32_t) 0;	/* insufficient, gres to use */

		cpus_ctld = cpu_end_bit - cpu_start_bit + 1;
		if (cpu_bitmap) {
			if (cpus_ctld < 1) {
				error("gres/%s: job %u cpus on node %s < 1",
				      gres_name, job_id, node_name);
				return (uint32_t) 0;
			}
			_validate_gres_node_cpus(node_gres_ptr, cpus_ctld,
						 node_name);
		} else {
			for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
				if (!node_gres_ptr->topo_cpus_bitmap[i])
					continue;
				cpus_ctld = bit_size(node_gres_ptr->
						     topo_cpus_bitmap[i]);
				break;
			}
		}

		alloc_cpu_bitmap = bit_alloc(cpus_ctld);
		if (cpu_bitmap) {
			for (j = 0; j < cpus_ctld; j++) {
				if (bit_test(cpu_bitmap, cpu_start_bit+j))
					bit_set(alloc_cpu_bitmap, j);
			}
		} else {
			bit_nset(alloc_cpu_bitmap, 0, cpus_ctld - 1);
		}

		cpus_addnt = xmalloc(sizeof(uint32_t)*node_gres_ptr->topo_cnt);
		cpus_avail = xmalloc(sizeof(uint32_t)*node_gres_ptr->topo_cnt);
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			if (node_gres_ptr->topo_gres_cnt_avail[i] == 0)
				continue;
			if (!use_total_gres &&
			    (node_gres_ptr->topo_gres_cnt_alloc[i] >=
			     node_gres_ptr->topo_gres_cnt_avail[i]))
				continue;
			if (job_gres_ptr->type_model &&
			    (!node_gres_ptr->topo_model[i] ||
			     xstrcmp(node_gres_ptr->topo_model[i],
				     job_gres_ptr->type_model)))
				continue;
			if (!node_gres_ptr->topo_cpus_bitmap[i]) {
				cpus_avail[i] = cpu_end_bit - cpu_start_bit + 1;
				continue;
			}
			cpu_size = bit_size(node_gres_ptr->topo_cpus_bitmap[i]);
			for (j = 0; j < cpu_size; j++) {
				if (cpu_bitmap &&
				    !bit_test(cpu_bitmap, cpu_start_bit+j))
					continue;
				if (bit_test(node_gres_ptr->
					     topo_cpus_bitmap[i], j)) {
					cpus_avail[i]++;
				}
			}
		}

		/* Pick the topology entries with the most CPUs available */
		gres_avail = 0;
		gres_total = 0;
		while (gres_avail < job_gres_ptr->gres_cnt_alloc) {
			top_inx = -1;
			for (j = 0; j < node_gres_ptr->topo_cnt; j++) {
				if ((gres_avail == 0) || (cpus_avail[j] == 0) ||
				    !node_gres_ptr->topo_cpus_bitmap[j]) {
					cpus_addnt[j] = cpus_avail[j];
				} else {
					cpus_addnt[j] = cpus_avail[j] -
						bit_overlap(alloc_cpu_bitmap,
							    node_gres_ptr->
							    topo_cpus_bitmap[j]);
				}

				if (top_inx == -1) {
					if (cpus_avail[j])
						top_inx = j;
				} else if (cpus_addnt[j] > cpus_addnt[top_inx])
					top_inx = j;
			}
			if ((top_inx < 0) || (cpus_avail[top_inx] == 0)) {
				if (gres_total < job_gres_ptr->gres_cnt_alloc)
					cpu_cnt = 0;
				break;
			}
			cpus_avail[top_inx] = 0;	/* Flag as used */
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
			/* update counts of allocated CPUs and GRES */
			if (!node_gres_ptr->topo_cpus_bitmap[top_inx]) {
				bit_nset(alloc_cpu_bitmap, 0, cpus_ctld - 1);
			} else if (gres_avail) {
				bit_or(alloc_cpu_bitmap,
				       node_gres_ptr->
				       topo_cpus_bitmap[top_inx]);
			} else {
				bit_and(alloc_cpu_bitmap,
					node_gres_ptr->
					topo_cpus_bitmap[top_inx]);
			}
			if (i > 0) {
				/* Available GRES count is up to i, but take 1
				 * per loop to maximize available CPUs count */
				gres_avail += 1;
				gres_total += i;
			}
			cpu_cnt = bit_set_count(alloc_cpu_bitmap);
		}
		if (cpu_bitmap && (cpu_cnt > 0)) {
			*topo_set = true;
			for (i = 0; i < cpus_ctld; i++) {
				if (!bit_test(alloc_cpu_bitmap, i))
					bit_clear(cpu_bitmap, cpu_start_bit+i);
			}
		}
		FREE_NULL_BITMAP(alloc_cpu_bitmap);
		xfree(cpus_addnt);
		xfree(cpus_avail);
		return cpu_cnt;
	} else if (job_gres_ptr->type_model) {
		for (i = 0; i < node_gres_ptr->type_cnt; i++) {
			if (node_gres_ptr->type_model[i] &&
			    !xstrcmp(node_gres_ptr->type_model[i],
				     job_gres_ptr->type_model))
				break;
		}
		if (i >= node_gres_ptr->type_cnt)
			return (uint32_t) 0;	/* no such type */
		gres_avail = node_gres_ptr->type_cnt_avail[i];
		if (!use_total_gres)
			gres_avail -= node_gres_ptr->type_cnt_alloc[i];
		if (job_gres_ptr->gres_cnt_alloc > gres_avail)
			return (uint32_t) 0;	/* insufficient, gres to use */
		return NO_VAL;
	} else {
		gres_avail = node_gres_ptr->gres_cnt_avail;
		if (!use_total_gres)
			gres_avail -= node_gres_ptr->gres_cnt_alloc;
		if (job_gres_ptr->gres_cnt_alloc > gres_avail)
			return (uint32_t) 0;	/* insufficient, gres to use */
		return NO_VAL;
	}
}

/*
 * Clear the cpu_bitmap for CPUs which are not usable by this job (i.e. for
 *	CPUs which are already bound to other jobs or lack GRES)
 * IN job_gres_list  - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *                     gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN/OUT cpu_bitmap - Identification of available CPUs (NULL if no restriction)
 * IN cpu_start_bit  - index into cpu_bitmap for this node's first CPU
 * IN cpu_end_bit    - index into cpu_bitmap for this node's last CPU
 */
extern void gres_plugin_job_core_filter(List job_gres_list, List node_gres_list,
					bool use_total_gres,
					bitstr_t *cpu_bitmap,
					int cpu_start_bit, int cpu_end_bit,
					char *node_name)
{
	int i;
	ListIterator  job_gres_iter, node_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

	if ((job_gres_list == NULL) || (cpu_bitmap == NULL))
		return;
	if (node_gres_list == NULL) {
		bit_nclear(cpu_bitmap, cpu_start_bit, cpu_end_bit);
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
			bit_nclear(cpu_bitmap, cpu_start_bit, cpu_end_bit);
			break;
		}

		for (i = 0; i < gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			_job_core_filter(job_gres_ptr->gres_data,
					 node_gres_ptr->gres_data,
					 use_total_gres, cpu_bitmap,
					 cpu_start_bit, cpu_end_bit,
					 gres_context[i].gres_name, node_name);
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return;
}

/*
 * Determine how many CPUs on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN cpu_bitmap     - Identification of available CPUs (NULL if no restriction)
 * IN cpu_start_bit  - index into cpu_bitmap for this node's first CPU
 * IN cpu_end_bit    - index into cpu_bitmap for this node's last CPU
 * IN job_id         - job's ID (for logging)
 * IN node_name      - name of the node (for logging)
 * RET: NO_VAL    - All cores on node are available
 *      otherwise - Count of available cores
 */
extern uint32_t gres_plugin_job_test(List job_gres_list, List node_gres_list,
				     bool use_total_gres, bitstr_t *cpu_bitmap,
				     int cpu_start_bit, int cpu_end_bit,
				     uint32_t job_id, char *node_name)
{
	int i;
	uint32_t cpu_cnt, tmp_cnt;
	ListIterator job_gres_iter,  node_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;
	bool topo_set = false;

	if (job_gres_list == NULL)
		return NO_VAL;
	if (node_gres_list == NULL)
		return 0;

	cpu_cnt = NO_VAL;
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
			cpu_cnt = 0;
			break;
		}

		for (i=0; i<gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			tmp_cnt = _job_test(job_gres_ptr->gres_data,
					    node_gres_ptr->gres_data,
					    use_total_gres, cpu_bitmap,
					    cpu_start_bit, cpu_end_bit,
					    &topo_set, job_id, node_name,
					    gres_context[i].gres_name);
			if (tmp_cnt != NO_VAL) {
				if (cpu_cnt == NO_VAL)
					cpu_cnt = tmp_cnt;
				else
					cpu_cnt = MIN(tmp_cnt, cpu_cnt);
			}
			break;
		}
		if (cpu_cnt == 0)
			break;
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return cpu_cnt;
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
		if (job_gres_ptr->type_model &&
		    (!node_gres_ptr->topo_model[i] ||
		     xstrcmp(job_gres_ptr->type_model,
			     node_gres_ptr->topo_model[i])))
			continue;
		if (!node_gres_ptr->topo_cpus_bitmap[i])
			return true;
		if (bit_size(node_gres_ptr->topo_cpus_bitmap[i]) !=
		    bit_size(core_bitmap))
			break;
		avail_cores = bit_overlap(node_gres_ptr->topo_cpus_bitmap[i],
					  core_bitmap);
		if (avail_cores && alloc_core_bitmap) {
			avail_cores -= bit_overlap(node_gres_ptr->
						   topo_cpus_bitmap[i],
						   alloc_core_bitmap);
			if (avail_cores) {
				bit_or(alloc_core_bitmap,
				       node_gres_ptr->topo_cpus_bitmap[i]);
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
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		if (!job_gres_ptr)
			continue;
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
}

static int _job_alloc(void *job_gres_data, void *node_gres_data,
		      int node_cnt, int node_offset, uint32_t cpu_cnt,
		      char *gres_name, uint32_t job_id, char *node_name,
		      bitstr_t *core_bitmap)
{
	int i, j, k, sz1, sz2;
	uint32_t gres_cnt;
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
			error("gres/%s: job %u node_cnt==0 and bit_alloc is "
			      "set", gres_name, job_id);
			xfree(job_gres_ptr->gres_bit_alloc);
		}
		job_gres_ptr->gres_bit_alloc = xmalloc(sizeof(bitstr_t *) *
						       node_cnt);
	} else if (job_gres_ptr->node_cnt < node_cnt) {
		error("gres/%s: job %u node_cnt increase from %u to %d",
		      gres_name, job_id, job_gres_ptr->node_cnt, node_cnt);
		if (node_offset >= job_gres_ptr->node_cnt)
			return SLURM_ERROR;
	} else if (job_gres_ptr->node_cnt > node_cnt) {
		error("gres/%s: job %u node_cnt decrease from %u to %d",
		      gres_name, job_id, job_gres_ptr->node_cnt, node_cnt);
	}

	/*
	 * Check that sufficient resources exist on this node
	 */
	gres_cnt = job_gres_ptr->gres_cnt_alloc;
	i = node_gres_ptr->gres_cnt_alloc + gres_cnt;
	i -= node_gres_ptr->gres_cnt_avail;
	if (i > 0) {
		error("gres/%s: job %u node %s overallocated resources by %d",
		      gres_name, job_id, node_name, i);
		/* proceed with request, give job what's available */
	}

	if (node_offset == 0)	/* Avoids memory leak on requeue */
		xfree(job_gres_ptr->gres_cnt_step_alloc);
	if (job_gres_ptr->gres_cnt_step_alloc == NULL) {
		job_gres_ptr->gres_cnt_step_alloc =
			xmalloc(sizeof(uint32_t) * node_cnt);
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
			gres_cnt = MIN(bit_size(node_gres_ptr->gres_bit_alloc),
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
			error("gres/%s: node %s gres bitmap size bad (%d < %u)",
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
		node_gres_ptr->gres_cnt_alloc += job_gres_ptr->gres_cnt_alloc;
	}

	if (job_gres_ptr->gres_bit_alloc &&
	    job_gres_ptr->gres_bit_alloc[node_offset] &&
	    node_gres_ptr->topo_gres_bitmap &&
	    node_gres_ptr->topo_gres_cnt_alloc) {
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			/* Insure that if specific CPUs are associated with
			 * specific GRES and the CPU count matches the
			 * slurmctld configuration that we only use the GRES
			 * on the CPUs that have already been allocated. */
			if (job_gres_ptr->type_model &&
			    (!node_gres_ptr->topo_model[i] ||
			     xstrcmp(job_gres_ptr->type_model,
				     node_gres_ptr->topo_model[i])))
				continue;
			if (core_bitmap && node_gres_ptr->topo_cpus_bitmap[i] &&
			    (bit_size(core_bitmap) ==
			     bit_size(node_gres_ptr->topo_cpus_bitmap[i])) &&
			    !bit_overlap(core_bitmap,
					 node_gres_ptr->topo_cpus_bitmap[i]))
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
			    (node_gres_ptr->topo_model == NULL) ||
			    (node_gres_ptr->topo_model[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_model[j] ||
				    xstrcmp(node_gres_ptr->topo_model[i],
					    node_gres_ptr->type_model[j]))
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
				xmalloc(sizeof(uint32_t) * len);
		} else {
			len = MIN(len, node_gres_ptr->gres_cnt_config);
		}
		for (i = 0; i < len; i++) {
			if (!bit_test(job_gres_ptr->
				      gres_bit_alloc[node_offset], i))
				continue;
			node_gres_ptr->topo_gres_cnt_alloc[i]++;
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_model == NULL) ||
			    (node_gres_ptr->topo_model[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_model[j] ||
				    xstrcmp(node_gres_ptr->topo_model[i],
					    node_gres_ptr->type_model[j]))
					continue;
				node_gres_ptr->type_cnt_alloc[j]++;
			}
		}
		type_array_updated = true;
	}

	if (!type_array_updated && job_gres_ptr->type_model) {
		gres_cnt = job_gres_ptr->gres_cnt_alloc;
		for (j = 0; j < node_gres_ptr->type_cnt; j++) {
			if (!node_gres_ptr->type_model[j] ||
			    xstrcmp(job_gres_ptr->type_model,
				    node_gres_ptr->type_model[j]))
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
 * Allocate resource to a job and update node and job gres information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN node_cnt    - total number of nodes originally allocated to the job
 * IN node_offset - zero-origin index to the node of interest
 * IN cpu_cnt     - number of CPUs allocated to this job on this node
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN core_bitmap - cores allocated to this job on this node (NULL if not
 *                  available)
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_alloc(List job_gres_list, List node_gres_list,
				 int node_cnt, int node_offset,
				 uint32_t cpu_cnt, uint32_t job_id,
				 char *node_name, bitstr_t *core_bitmap)
{
	int i, rc, rc2;
	ListIterator job_gres_iter,  node_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("gres_job_alloc: job %u has gres specification while "
		      "node %s has none", job_id, node_name);
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
			error("gres_plugin_job_alloc: no plugin configured "
			      "for data type %u for job %u and node %s",
			      job_gres_ptr->plugin_id, job_id, node_name);
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
			error("gres_plugin_job_alloc: job %u allocated gres/%s "
			      "on node %s lacking that gres",
			      job_id, gres_context[i].gres_name, node_name);
			continue;
		}

		rc2 = _job_alloc(job_gres_ptr->gres_data,
				 node_gres_ptr->gres_data, node_cnt,
				 node_offset, cpu_cnt,
				 gres_context[i].gres_name, job_id, node_name,
				 core_bitmap);
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
	int i, j, k, len, gres_cnt, sz1, sz2;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bool type_array_updated = false;

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
	} else if (node_gres_ptr->gres_cnt_alloc >=
		   job_gres_ptr->gres_cnt_alloc) {
		node_gres_ptr->gres_cnt_alloc -= job_gres_ptr->gres_cnt_alloc;
	} else {
		node_gres_ptr->gres_cnt_alloc = 0;
		error("gres/%s: job %u node %s gres count underflow",
		      gres_name, job_id, node_name);
	}

	if (job_gres_ptr->gres_bit_alloc &&
	    job_gres_ptr->gres_bit_alloc[node_offset] &&
	    node_gres_ptr->topo_gres_bitmap &&
	    node_gres_ptr->topo_gres_cnt_alloc) {
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			sz1 = bit_size(job_gres_ptr->gres_bit_alloc[node_offset]);
			sz2 = bit_size(node_gres_ptr->topo_gres_bitmap[i]);
			if (sz1 != sz2)
				continue;
			gres_cnt = bit_overlap(job_gres_ptr->
					       gres_bit_alloc[node_offset],
					       node_gres_ptr->
					       topo_gres_bitmap[i]);
			if (node_gres_ptr->topo_gres_cnt_alloc[i] >= gres_cnt) {
				node_gres_ptr->topo_gres_cnt_alloc[i] -=
					gres_cnt;
			} else {
				error("gres/%s: job %u dealloc node %s topo "
				      "gres count underflow", gres_name, job_id,
				      node_name);
				node_gres_ptr->topo_gres_cnt_alloc[i] = 0;
			}
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_model == NULL) ||
			    (node_gres_ptr->topo_model[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_model[j] ||
				    xstrcmp(node_gres_ptr->topo_model[i],
					    node_gres_ptr->type_model[j]))
					continue;
				if (node_gres_ptr->type_cnt_alloc[j] >=
				    gres_cnt) {
					node_gres_ptr->type_cnt_alloc[j] -=
						gres_cnt;
				} else {
					error("gres/%s: job %u dealloc node %s "
					      "type %s gres count underflow",
					      gres_name, job_id, node_name,
					      node_gres_ptr->type_model[j]);
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
			    (node_gres_ptr->topo_model == NULL) ||
			    (node_gres_ptr->topo_model[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_model[j] ||
				    xstrcmp(node_gres_ptr->topo_model[i],
					    node_gres_ptr->type_model[j]))
					continue;
				node_gres_ptr->type_cnt_alloc[j]--;
 			}
		}
		type_array_updated = true;
	}

	if (!type_array_updated && job_gres_ptr->type_model) {
		gres_cnt = job_gres_ptr->gres_cnt_alloc;
		for (j = 0; j < node_gres_ptr->type_cnt; j++) {
			if (!node_gres_ptr->type_model[j] ||
			    xstrcmp(job_gres_ptr->type_model,
				    node_gres_ptr->type_model[j]))
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
		error("gres_job_dealloc: job %u has gres specification while "
		      "node %s has none", job_id, node_name);
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
			error("gres_plugin_job_dealloc: no plugin configured "
			      "for data type %u for job %u and node %s",
			      job_gres_ptr->plugin_id, job_id, node_name);
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
			error("gres_plugin_job_dealloc: node %s lacks gres/%s "
			      "for job %u", node_name, gres_name , job_id);
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
	uint32_t *new_gres_cnt_step_alloc;

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
		new_gres_cnt_step_alloc = xmalloc(sizeof(uint32_t) *
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
			gres_job_ptr2->gres_cnt_alloc = gres_job_ptr->
							gres_cnt_alloc;
			gres_job_ptr2->node_cnt = new_node_cnt;
			gres_job_ptr2->gres_bit_alloc =
				xmalloc(sizeof(bitstr_t *) * new_node_cnt);
			gres_job_ptr2->gres_bit_step_alloc =
				xmalloc(sizeof(bitstr_t *) * new_node_cnt);
			gres_job_ptr2->gres_cnt_step_alloc =
				xmalloc(sizeof(uint32_t) * new_node_cnt);
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
  */
extern void gres_plugin_job_set_env(char ***job_env_ptr, List job_gres_list)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_ptr = NULL;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i=0; i<gres_context_cnt; i++) {
		if (gres_context[i].ops.job_set_env == NULL)
			continue;	/* No plugin to call */
		if (job_gres_list) {
			gres_iter = list_iterator_create(job_gres_list);
			while ((gres_ptr = (gres_state_t *)
					   list_next(gres_iter))) {
				if (gres_ptr->plugin_id !=
				    gres_context[i].plugin_id)
					continue;
				(*(gres_context[i].ops.job_set_env))
					(job_env_ptr, gres_ptr->gres_data);
				break;
			}
			list_iterator_destroy(gres_iter);
		}
		if (gres_ptr == NULL) { /* No data found */
			(*(gres_context[i].ops.job_set_env))
					(job_env_ptr, NULL);
		}
	}
	slurm_mutex_unlock(&gres_context_lock);
}

static void _job_state_log(void *gres_data, uint32_t job_id, char *gres_name)
{
	gres_job_state_t *gres_ptr;
	char tmp_str[128];
	int i;

	xassert(gres_data);
	gres_ptr = (gres_job_state_t *) gres_data;
	info("gres: %s state for job %u", gres_name, job_id);
	info("  gres_cnt:%u node_cnt:%u type:%s", gres_ptr->gres_cnt_alloc,
	     gres_ptr->node_cnt, gres_ptr->type_model);
	if (gres_ptr->node_cnt == 0)
		return;

	if (gres_ptr->gres_bit_alloc == NULL)
		info("  gres_bit_alloc:NULL");
	if (gres_ptr->gres_bit_step_alloc == NULL)
		info("  gres_bit_step_alloc:NULL");
	if (gres_ptr->gres_cnt_step_alloc == NULL)
		info("  gres_cnt_step_alloc:NULL");

	for (i=0; i<gres_ptr->node_cnt; i++) {
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
			info("  gres_cnt_step_alloc[%d]:%u", i,
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
extern uint32_t gres_plugin_get_job_value_by_type(List job_gres_list,
						  char *gres_name_type)
{
	uint32_t gres_val, gres_name_type_id;
	ListIterator  job_gres_iter;
	gres_state_t *job_gres_ptr;

	if (job_gres_list == NULL)
		return NO_VAL;

	gres_name_type_id = _build_id(gres_name_type);
	gres_val = NO_VAL;

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		if (job_gres_ptr->plugin_id == gres_name_type_id) {
			gres_val = ((gres_job_state_t*)
				   (job_gres_ptr->gres_data))->gres_cnt_alloc;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);

	return gres_val;
}

/*
 * Log a job's current gres state
 * IN gres_list - generated by gres_plugin_job_state_validate()
 * IN job_id - job's ID
 */
extern void gres_plugin_job_state_log(List gres_list, uint32_t job_id)
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
		for (i=0; i<gres_context_cnt; i++) {
			if (gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			_job_state_log(gres_ptr->gres_data, job_id,
				       gres_context[i].gres_name);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

extern void gres_plugin_job_state_file(List gres_list, int *gres_bit_alloc,
				       int *gres_count)
{
	int i, j, gres_cnt=0, len, p, found=0;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;
	gres_job_state_t *gres_job_ptr;

	if (gres_list == NULL)
		return;
	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);

	for (j=0; j<gres_context_cnt; j++) {
		found = 0;
		list_iterator_reset(gres_iter);
		while ((gres_ptr = (gres_state_t *) list_next(gres_iter))){
			if (gres_ptr->plugin_id !=
			    gres_context[j].plugin_id ) {
				continue;
			}
			found = 1;
			gres_job_ptr = (gres_job_state_t *) gres_ptr->gres_data;
			if ((gres_job_ptr != NULL) &&
			    (gres_job_ptr->node_cnt == 1) &&
			    (gres_job_ptr->gres_bit_alloc != NULL) &&
			    (gres_job_ptr->gres_bit_alloc[0] != NULL)) {
			     	len = bit_size(gres_job_ptr->gres_bit_alloc[0]);
				for (i=0; i<len; i++) {
					if (!bit_test(gres_job_ptr->
						      gres_bit_alloc[0], i))
						gres_bit_alloc[gres_cnt] = 0;
					else
						gres_bit_alloc[gres_cnt] = 1;
					gres_cnt++;
				}
			}
			break;
		}
		if (found == 0) {
			for (p=0; p<gres_count[j]; p++){
				gres_bit_alloc[gres_cnt] = 0;
				gres_cnt++;
			}
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
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
	xfree(gres_ptr->type_model);
	xfree(gres_ptr);
}

static void _gres_step_list_delete(void *list_element)
{
	gres_state_t *gres_ptr = (gres_state_t *) list_element;

	_step_state_delete(gres_ptr->gres_data);
	xfree(gres_ptr);
}

static int _step_state_validate(char *config, void **gres_data,
				slurm_gres_context_t *context_ptr)
{
	gres_step_state_t *gres_ptr;
	char *type = NULL, *num = NULL, *last_num = NULL;
	int cnt;

	if (!xstrcmp(config, context_ptr->gres_name)) {
		cnt = 1;
	} else if (!xstrncmp(config, context_ptr->gres_name_colon,
			     context_ptr->gres_name_colon_len)) {
		type = strchr(config, ':');
		num = strrchr(config, ':');
		if (!num)
			return SLURM_ERROR;
		cnt = strtol(num + 1, &last_num, 10);
		if (last_num[0] == '\0')
			;
		else if ((last_num[0] == 'k') || (last_num[0] == 'K'))
			cnt *= 1024;
		else if ((last_num[0] == 'm') || (last_num[0] == 'M'))
			cnt *= (1024 * 1024);
		else if ((last_num[0] == 'g') || (last_num[0] == 'G'))
			cnt *= (1024 * 1024 * 1024);
		else
			return SLURM_ERROR;
		if (cnt < 0)
			return SLURM_ERROR;
	} else {
		/* Did not find this GRES name, check for zero value */
		num = strrchr(config, ':');
		if (num) {
			 cnt = strtol(num + 1, &last_num, 10);
			if ((last_num[0] != '\0') || (cnt != 0))
				return SLURM_ERROR;
		} else
			return SLURM_ERROR;
	}

	if (cnt == 0) {
		*gres_data = NULL;
	} else {
		gres_ptr = xmalloc(sizeof(gres_step_state_t));
		gres_ptr->gres_cnt_alloc = (uint32_t) cnt;
		if (type && num && (type != num)) {
			type++;
			num[0] = '\0';
			gres_ptr->type_model = xstrdup(type);
		}
		*gres_data = gres_ptr;
	}
	return SLURM_SUCCESS;
}

static uint32_t _step_test(void *step_gres_data, void *job_gres_data,
			   int node_offset, bool ignore_alloc, char *gres_name,
			   uint32_t job_id, uint32_t step_id)
{
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint32_t gres_cnt;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if ((node_offset == NO_VAL) ||
	    (0 == job_gres_ptr->node_cnt)) {	/* no_consume */
		if (step_gres_ptr->gres_cnt_alloc >
		    job_gres_ptr->gres_cnt_alloc)
			return 0;
		return NO_VAL;
	}

	if (node_offset >= job_gres_ptr->node_cnt) {
		error("gres/%s: %s %u.%u node offset invalid (%d >= %u)",
		      gres_name, __func__, job_id, step_id, node_offset,
		      job_gres_ptr->node_cnt);
		return 0;
	}

	if (job_gres_ptr->gres_cnt_step_alloc) {
		uint32_t job_gres_avail = job_gres_ptr->gres_cnt_alloc;
		if (!ignore_alloc) {
			job_gres_avail -= job_gres_ptr->
					  gres_cnt_step_alloc[node_offset];
		}
		if (step_gres_ptr->gres_cnt_alloc > job_gres_avail)
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
		if (step_gres_ptr->gres_cnt_alloc > gres_cnt)
			gres_cnt = 0;
		else
			gres_cnt = NO_VAL;
	} else if (job_gres_ptr->gres_cnt_step_alloc &&
		   job_gres_ptr->gres_cnt_step_alloc[node_offset]) {
		gres_cnt = job_gres_ptr->gres_cnt_alloc -
			   job_gres_ptr->gres_cnt_step_alloc[node_offset];
		if (step_gres_ptr->gres_cnt_alloc > gres_cnt)
			gres_cnt = 0;
		else
			gres_cnt = NO_VAL;
	} else {
		/* Note: We already validated the gres count above */
		debug("gres/%s: %s %u.%u gres_bit_alloc is NULL",
		      gres_name, __func__, job_id, step_id);
		gres_cnt = NO_VAL;
	}

	return gres_cnt;
}

/*
 * Given a step's requested gres configuration, validate it and build gres list
 * IN req_config - step request's gres input string
 * OUT step_gres_list - List of Gres records for this step to track usage
 * IN job_gres_list - List of Gres records for this job
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_step_state_validate(char *req_config,
					   List *step_gres_list,
					   List job_gres_list, uint32_t job_id,
					   uint32_t step_id)
{
	char *tmp_str, *tok, *last = NULL;
	int i, rc, rc2, rc3;
	gres_state_t *step_gres_ptr, *job_gres_ptr;
	void *step_gres_data, *job_gres_data;
	ListIterator job_gres_iter;
	gres_step_state_t *step_gres_state;
	gres_job_state_t *job_gres_state;

	*step_gres_list = NULL;
	if ((req_config == NULL) || (req_config[0] == '\0'))
		return SLURM_SUCCESS;

	if ((rc = gres_plugin_init()) != SLURM_SUCCESS)
		return rc;

	slurm_mutex_lock(&gres_context_lock);
	tmp_str = xstrdup(req_config);
	tok = strtok_r(tmp_str, ",", &last);
	while (tok && (rc == SLURM_SUCCESS)) {
		rc2 = SLURM_ERROR;
		for (i = 0; i < gres_context_cnt; i++) {
			step_gres_data = NULL;
			rc2 = _step_state_validate(tok, &step_gres_data,
						   &gres_context[i]);
			if (rc2 != SLURM_SUCCESS)
				continue;
			if (step_gres_data == NULL)    /* Name match, count=0 */
				break;
			if (job_gres_list == NULL) {
				info("step %u.%u has gres spec, job has none",
				     job_id, step_id);
				rc2 = ESLURM_INVALID_GRES;
				continue;
			}
			/* Now make sure the step's request isn't too big for
			 * the job's gres allocation */
			step_gres_state = (gres_step_state_t *) step_gres_data;
			job_gres_iter = list_iterator_create(job_gres_list);
			while ((job_gres_ptr = (gres_state_t *)
					list_next(job_gres_iter))) {
				if (job_gres_ptr->plugin_id !=
				    gres_context[i].plugin_id)
					continue;
				if (!step_gres_state->type_model)
					break;	/* match GRES name only */
				job_gres_state = (gres_job_state_t *)
						 job_gres_ptr->gres_data;
				if (!job_gres_state->type_model ||
				    xstrcmp(job_gres_state->type_model,
					    step_gres_state->type_model))
					continue;
				break;	/* match GRES name and model */
			}
			list_iterator_destroy(job_gres_iter);
			if (job_gres_ptr == NULL) {
				info("Step %u.%u gres request not in job "
				     "alloc %s", job_id, step_id, tok);
				rc = ESLURM_INVALID_GRES;
				_step_state_delete(step_gres_data);
				break;
			}
			job_gres_data = job_gres_ptr->gres_data;
			rc3 = _step_test(step_gres_data, job_gres_data, NO_VAL,
					 true, gres_context[i].gres_name,
					 job_id, step_id);
			if (rc3 == 0) {
				info("Step %u.%u gres higher than in job "
				     "allocation %s", job_id, step_id, tok);
				rc = ESLURM_INVALID_GRES;
				_step_state_delete(step_gres_data);
				break;
			}

			if (*step_gres_list == NULL) {
				*step_gres_list = list_create(
						  _gres_step_list_delete);
			}
			step_gres_ptr = xmalloc(sizeof(gres_state_t));
			step_gres_ptr->plugin_id = gres_context[i].plugin_id;
			step_gres_ptr->gres_data = step_gres_data;
			list_append(*step_gres_list, step_gres_ptr);
			break;		/* processed it */
		}
		if ((i >= gres_context_cnt) && _is_gres_cnt_zero(tok))
			rc2 = SLURM_SUCCESS;
		if (rc2 != SLURM_SUCCESS) {
			info("Invalid gres step %u.%u specification %s",
			     job_id, step_id, tok);
			rc = ESLURM_INVALID_GRES;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	slurm_mutex_unlock(&gres_context_lock);

	xfree(tmp_str);
	return rc;
}

static void *_step_state_dup(void *gres_data)
{

	int i;
	gres_step_state_t *gres_ptr = (gres_step_state_t *) gres_data;
	gres_step_state_t *new_gres_ptr;

	xassert(gres_ptr);
	new_gres_ptr = xmalloc(sizeof(gres_step_state_t));
	new_gres_ptr->gres_cnt_alloc	= gres_ptr->gres_cnt_alloc;
	new_gres_ptr->node_cnt		= gres_ptr->node_cnt;

	if (gres_ptr->node_in_use)
		new_gres_ptr->node_in_use = bit_copy(gres_ptr->node_in_use);

	if (gres_ptr->gres_bit_alloc) {
		new_gres_ptr->gres_bit_alloc = xmalloc(sizeof(bitstr_t *) *
					       gres_ptr->node_cnt);
		for (i=0; i<gres_ptr->node_cnt; i++) {
			if (gres_ptr->gres_bit_alloc[i] == NULL)
				continue;
			new_gres_ptr->gres_bit_alloc[i] = bit_copy(gres_ptr->
							  gres_bit_alloc[i]);
		}
	}
	return new_gres_ptr;
}

static void *_step_state_dup2(void *gres_data, int node_index)
{

	gres_step_state_t *gres_ptr = (gres_step_state_t *) gres_data;
	gres_step_state_t *new_gres_ptr;

	xassert(gres_ptr);
	new_gres_ptr = xmalloc(sizeof(gres_step_state_t));
	new_gres_ptr->gres_cnt_alloc	= gres_ptr->gres_cnt_alloc;
	new_gres_ptr->node_cnt		= 1;

	if (gres_ptr->node_in_use)
		new_gres_ptr->node_in_use = bit_copy(gres_ptr->node_in_use);

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
 * IN gres_list - generated by gres_plugin_step_allocate()
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
		if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			pack32(gres_step_ptr->gres_cnt_alloc, buffer);
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
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			pack32(gres_step_ptr->gres_cnt_alloc, buffer);
			pack32(gres_step_ptr->node_cnt, buffer);
			pack_bit_str(gres_step_ptr->node_in_use, buffer);
			if (gres_step_ptr->gres_bit_alloc) {
				pack8((uint8_t) 1, buffer);
				for (i=0; i<gres_step_ptr->node_cnt; i++)
					pack_bit_str(gres_step_ptr->
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
	uint32_t magic, plugin_id;
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

		if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_step_ptr = xmalloc(sizeof(gres_step_state_t));
			safe_unpack32(&gres_step_ptr->gres_cnt_alloc, buffer);
			safe_unpack32(&gres_step_ptr->node_cnt, buffer);
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
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpack32(&magic, buffer);
			if (magic != GRES_MAGIC)
				goto unpack_error;
			safe_unpack32(&plugin_id, buffer);
			gres_step_ptr = xmalloc(sizeof(gres_step_state_t));
			safe_unpack32(&gres_step_ptr->gres_cnt_alloc, buffer);
			safe_unpack32(&gres_step_ptr->node_cnt, buffer);
			unpack_bit_str(&gres_step_ptr->node_in_use, buffer);
			safe_unpack8(&has_file, buffer);
			if (has_file) {
				gres_step_ptr->gres_bit_alloc =
					xmalloc(sizeof(bitstr_t *) *
						gres_step_ptr->node_cnt);
				for (i=0; i<gres_step_ptr->node_cnt; i++) {
					unpack_bit_str(&gres_step_ptr->
						       gres_bit_alloc[i],
						       buffer);
				}
			}
		} else {
			error("gres_plugin_step_state_unpack: protocol_version"
			      " %hu not supported", protocol_version);
			goto unpack_error;
		}

		for (i=0; i<gres_context_cnt; i++) {
			if (gres_context[i].plugin_id == plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			/* A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data. */
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

/*
 * Set environment variables as required for all tasks of a job step
 * IN/OUT job_env_ptr - environment variable array
 * IN gres_list - generated by gres_plugin_step_allocate()
  */
extern void gres_plugin_step_set_env(char ***job_env_ptr, List step_gres_list)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_ptr = NULL;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i=0; i<gres_context_cnt; i++) {
		if (gres_context[i].ops.step_set_env == NULL)
			continue;	/* No plugin to call */
		if (step_gres_list) {
			gres_iter = list_iterator_create(step_gres_list);
			while ((gres_ptr = (gres_state_t *)
					   list_next(gres_iter))) {
				if (gres_ptr->plugin_id !=
				    gres_context[i].plugin_id)
					continue;
				(*(gres_context[i].ops.step_set_env))
					(job_env_ptr, gres_ptr->gres_data);
				break;
			}
			list_iterator_destroy(gres_iter);
		}
		if (gres_ptr == NULL) { /* No data found */
			(*(gres_context[i].ops.step_set_env))
					(job_env_ptr, NULL);
		}
	}
	slurm_mutex_unlock(&gres_context_lock);
}

static void _step_state_log(void *gres_data, uint32_t job_id, uint32_t step_id,
			    char *gres_name)
{
	gres_step_state_t *gres_ptr = (gres_step_state_t *) gres_data;
	char tmp_str[128];
	int i;

	xassert(gres_ptr);
	info("gres/%s state for step %u.%u", gres_name, job_id, step_id);
	info("  gres_cnt:%u node_cnt:%u type:%s", gres_ptr->gres_cnt_alloc,
	     gres_ptr->node_cnt, gres_ptr->type_model);

	if (gres_ptr->node_in_use == NULL)
		info("  node_in_use:NULL");
	else if (gres_ptr->gres_bit_alloc == NULL)
		info("  gres_bit_alloc:NULL");
	else {
		for (i=0; i<gres_ptr->node_cnt; i++) {
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
 * IN gres_list - generated by gres_plugin_step_allocate()
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
		for (i=0; i<gres_context_cnt; i++) {
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
 * Determine how many CPUs of a job's allocation can be allocated to a job
 *	on a specific node
 * IN job_gres_list - a running job's gres info
 * IN/OUT step_gres_list - a pending job step's gres requirements
 * IN node_offset - index into the job's node allocation
 * IN ignore_alloc - if set ignore resources already allocated to running steps
 * IN job_id, step_id - ID of the step being allocated.
 * RET Count of available CPUs on this node, NO_VAL if no limit
 */
extern uint32_t gres_plugin_step_test(List step_gres_list, List job_gres_list,
				      int node_offset, bool ignore_alloc,
				      uint32_t job_id, uint32_t step_id)
{
	int i;
	uint32_t cpu_cnt, tmp_cnt;
	ListIterator  job_gres_iter, step_gres_iter;
	gres_state_t *job_gres_ptr, *step_gres_ptr;

	if (step_gres_list == NULL)
		return NO_VAL;
	if (job_gres_list == NULL)
		return 0;

	cpu_cnt = NO_VAL;
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
			cpu_cnt = 0;
			break;
		}

		for (i=0; i<gres_context_cnt; i++) {
			if (step_gres_ptr->plugin_id !=
			    gres_context[i].plugin_id)
				continue;
			tmp_cnt = _step_test(step_gres_ptr->gres_data,
					     job_gres_ptr->gres_data,
					     node_offset, ignore_alloc,
					     gres_context[i].gres_name,
					     job_id, step_id);
			if (tmp_cnt != NO_VAL) {
				if (cpu_cnt == NO_VAL)
					cpu_cnt = tmp_cnt;
				else
					cpu_cnt = MIN(tmp_cnt, cpu_cnt);
			}
			break;
		}
		if (cpu_cnt == 0)
			break;
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return cpu_cnt;
}

static int _step_alloc(void *step_gres_data, void *job_gres_data,
		       int node_offset, int cpu_cnt, char *gres_name,
		       uint32_t job_id, uint32_t step_id)
{
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint32_t gres_needed;
	bitstr_t *gres_bit_alloc;
	int i, len;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if (0 == job_gres_ptr->node_cnt)	/* no_consume */
		return SLURM_SUCCESS;

	if (node_offset >= job_gres_ptr->node_cnt) {
		error("gres/%s: %s for %u.%u, node offset invalid "
		      "(%d >= %u)",
		      gres_name, __func__, job_id, step_id, node_offset,
		      job_gres_ptr->node_cnt);
		return SLURM_ERROR;
	}

	if (step_gres_ptr->gres_cnt_alloc > job_gres_ptr->gres_cnt_alloc) {
		error("gres/%s: %s for %u.%u, step's > job's "
		      "for node %d (%d > %u)",
		      gres_name, __func__, job_id, step_id, node_offset,
		      step_gres_ptr->gres_cnt_alloc,
		      job_gres_ptr->gres_cnt_alloc);
		return SLURM_ERROR;
	}

	if (job_gres_ptr->gres_cnt_step_alloc == NULL) {
		job_gres_ptr->gres_cnt_step_alloc =
			xmalloc(sizeof(uint32_t) * job_gres_ptr->node_cnt);
	}

	if (step_gres_ptr->gres_cnt_alloc >
	    (job_gres_ptr->gres_cnt_alloc -
	     job_gres_ptr->gres_cnt_step_alloc[node_offset])) {
		error("gres/%s: %s for %u.%u, step's > job's "
		      "remaining for node %d (%d > (%u - %u))",
		      gres_name, __func__, job_id, step_id, node_offset,
		      step_gres_ptr->gres_cnt_alloc,
		      job_gres_ptr->gres_cnt_alloc,
		      job_gres_ptr->gres_cnt_step_alloc[node_offset]);
		return SLURM_ERROR;
	}

	step_gres_ptr->node_cnt = job_gres_ptr->node_cnt;
	if (step_gres_ptr->node_in_use == NULL) {
		step_gres_ptr->node_in_use = bit_alloc(job_gres_ptr->node_cnt);
	}
	bit_set(step_gres_ptr->node_in_use, node_offset);
	job_gres_ptr->gres_cnt_step_alloc[node_offset] +=
		step_gres_ptr->gres_cnt_alloc;

	if ((job_gres_ptr->gres_bit_alloc == NULL) ||
	    (job_gres_ptr->gres_bit_alloc[node_offset] == NULL)) {
		debug("gres/%s: %s gres_bit_alloc for %u.%u is NULL",
		      gres_name, __func__, job_id, step_id);
		return SLURM_SUCCESS;
	}

	gres_bit_alloc = bit_copy(job_gres_ptr->gres_bit_alloc[node_offset]);
	if (job_gres_ptr->gres_bit_step_alloc &&
	    job_gres_ptr->gres_bit_step_alloc[node_offset]) {
		bit_not(job_gres_ptr->gres_bit_step_alloc[node_offset]);
		bit_and(gres_bit_alloc,
			job_gres_ptr->gres_bit_step_alloc[node_offset]);
		bit_not(job_gres_ptr->gres_bit_step_alloc[node_offset]);
	}

	gres_needed = step_gres_ptr->gres_cnt_alloc;
	len = bit_size(gres_bit_alloc);
	for (i=0; i<len; i++) {
		if (gres_needed > 0) {
			if (bit_test(gres_bit_alloc, i))
				gres_needed--;
		} else {
			bit_clear(gres_bit_alloc, i);
		}
	}
	if (gres_needed) {
		error("gres/%s: %s step %u.%u oversubscribed resources on "
		      "node %d",
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
 * IN cpu_cnt - number of CPUs allocated to this job on this node
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_step_alloc(List step_gres_list, List job_gres_list,
				  int node_offset, int cpu_cnt,
				  uint32_t job_id, uint32_t step_id)
{
	int i, rc, rc2;
	ListIterator step_gres_iter,  job_gres_iter;
	gres_state_t *step_gres_ptr, *job_gres_ptr;

	if (step_gres_list == NULL)
		return SLURM_SUCCESS;
	if (job_gres_list == NULL) {
		error("gres_plugin_step_alloc: step allocates gres, but job "
		      "%u has none", job_id);
		return SLURM_ERROR;
	}

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	step_gres_iter = list_iterator_create(step_gres_list);
	while ((step_gres_ptr = (gres_state_t *) list_next(step_gres_iter))) {
		for (i=0; i<gres_context_cnt; i++) {
			if (step_gres_ptr->plugin_id ==
			    gres_context[i].plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("gres: step_alloc, could not find plugin %u for "
			      "step %u.%u",
			      step_gres_ptr->plugin_id, job_id, step_id);
			rc = ESLURM_INVALID_GRES;
			break;
		}

		job_gres_iter = list_iterator_create(job_gres_list);
		while ((job_gres_ptr = (gres_state_t *)
				list_next(job_gres_iter))) {
			if (step_gres_ptr->plugin_id == job_gres_ptr->plugin_id)
				break;
		}
		list_iterator_destroy(job_gres_iter);
		if (job_gres_ptr == NULL) {
			info("gres: job %u lacks gres/%s for step %u",
			     job_id, gres_context[i].gres_name, step_id);
			rc = ESLURM_INVALID_GRES;
			break;
		}

		rc2 = _step_alloc(step_gres_ptr->gres_data,
				  job_gres_ptr->gres_data, node_offset,
				  cpu_cnt, gres_context[i].gres_name, job_id,
				  step_id);
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
	int len_j, len_s;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if (0 == job_gres_ptr->node_cnt) {	/* no_consume */
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

		if (job_gres_ptr->gres_cnt_step_alloc) {
			if (job_gres_ptr->gres_cnt_step_alloc[i] >=
			    step_gres_ptr->gres_cnt_alloc) {
				job_gres_ptr->gres_cnt_step_alloc[i] -=
					step_gres_ptr->gres_cnt_alloc;
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
		error("gres_plugin_step_alloc: step deallocates gres, but job "
		      "%u has none", job_id);
		return SLURM_ERROR;
	}

	rc = gres_plugin_init();

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
		if (job_gres_ptr == NULL)
			continue;

		for (i=0; i<gres_context_cnt; i++) {
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
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Determine how many GRES of a given type are allocated to a job
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN gres_name - name of a GRES type
 * RET count of this GRES allocated to this job
 */
extern uint32_t gres_get_value_by_type(List job_gres_list, char* gres_name)
{
	int i;
	uint32_t gres_cnt = 0, plugin_id;
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_gres_data;

	if (job_gres_list == NULL)
		return NO_VAL;

	gres_cnt = NO_VAL;
	(void) gres_plugin_init();
	plugin_id = _build_id(gres_name);

	slurm_mutex_lock(&gres_context_lock);
	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		for (i=0; i<gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id != plugin_id)
				continue;
			job_gres_data = (gres_job_state_t *)
					job_gres_ptr->gres_data;
			gres_cnt = job_gres_data->gres_cnt_alloc;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return gres_cnt;
}

/*
 * Fill in an array of GRES type ids contained within the given job gres_list
 *		and an array of corresponding counts of those GRES types.
 * IN gres_list - a List of GRES types allocated to a job.
 * IN arr_len - Length of the arrays (the number of elements in the gres_list).
 * IN gres_count_ids, gres_count_vals - the GRES type ID's and values found
 *	 	in the gres_list.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_count(List gres_list, int arr_len,
				 int *gres_count_ids, int *gres_count_vals)
{
	ListIterator  job_gres_iter;
	gres_state_t* job_gres_ptr;
	void*         job_gres_data;
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
		gres_count_vals[ix] = job_gres_state_ptr->gres_cnt_alloc;
		if (++ix >= arr_len)
			break;
	}
	list_iterator_destroy(job_gres_iter);

	slurm_mutex_unlock(&gres_context_lock);

	return rc;
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

		switch(val_type) {
		case(GRES_VAL_TYPE_FOUND):
			val = node_gres_state_ptr->gres_cnt_found;
			break;
		case(GRES_VAL_TYPE_CONFIG):
			val = node_gres_state_ptr->gres_cnt_config;
			break;
		case(GRES_VAL_TYPE_AVAIL):
			val = node_gres_state_ptr->gres_cnt_avail;
			break;
		case(GRES_VAL_TYPE_ALLOC):
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

extern void gres_plugin_step_state_file(List gres_list, int *gres_bit_alloc,
					int *gres_count)
{
	int i, j, p, gres_cnt = 0, len, found;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;
	gres_step_state_t *gres_step_ptr;

	if (gres_list == NULL)
		return;
	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);

	for (j=0; j<gres_context_cnt; j++) {
		found = 0;
		list_iterator_reset(gres_iter);
		while ((gres_ptr = (gres_state_t *) list_next(gres_iter))){
			if (gres_ptr->plugin_id !=
			    gres_context[j].plugin_id) {
				continue;
			}
			found = 1;
			gres_step_ptr = (gres_step_state_t *) gres_ptr->gres_data;
			if ((gres_step_ptr != NULL) &&
			    (gres_step_ptr->node_cnt == 1) &&
			    (gres_step_ptr->gres_bit_alloc != NULL) &&
			    (gres_step_ptr->gres_bit_alloc[0] != NULL)) {
				len = bit_size(gres_step_ptr->gres_bit_alloc[0]);
				for (i=0; i<len; i++) {
					 if (!bit_test(gres_step_ptr->
						       gres_bit_alloc[0], i))
						 gres_bit_alloc[gres_cnt] = 0;
					 else
						 gres_bit_alloc[gres_cnt] = 1;
					gres_cnt++;
				}
			}
			break;
		}
		if (found == 0) {
			for (p=0; p<gres_count[j]; p++){
				gres_bit_alloc[gres_cnt] = 0;
				gres_cnt++;
			}
		}
	}

	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
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
		break;
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
		break;
	}
	slurm_mutex_unlock(&gres_context_lock);
}

/* Get generic GRES data types here. Call the plugin for others */
static int _get_job_info(int gres_inx, gres_job_state_t *job_gres_data,
			 uint32_t node_inx, enum gres_job_data_type data_type,
			 void *data)
{
	uint32_t *u32_data = (uint32_t *) data;
	bitstr_t **bit_data = (bitstr_t **) data;
	int rc = SLURM_SUCCESS;

	if (!job_gres_data || !data)
		return EINVAL;
	if (node_inx >= job_gres_data->node_cnt)
		return ESLURM_INVALID_NODE_COUNT;
	if (data_type == GRES_JOB_DATA_COUNT) {
		*u32_data = job_gres_data->gres_cnt_alloc;
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

/* Get generic GRES data types here. Call the plugin for others */
static int _get_step_info(int gres_inx, gres_step_state_t *step_gres_data,
			  uint32_t node_inx, enum gres_step_data_type data_type,
			  void *data)
{
	uint32_t *u32_data = (uint32_t *) data;
	bitstr_t **bit_data = (bitstr_t **) data;
	int rc = SLURM_SUCCESS;

	if (!step_gres_data || !data)
		return EINVAL;
	if (node_inx >= step_gres_data->node_cnt)
		return ESLURM_INVALID_NODE_COUNT;
	if (data_type == GRES_STEP_DATA_COUNT) {
		*u32_data = step_gres_data->gres_cnt_alloc;
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
