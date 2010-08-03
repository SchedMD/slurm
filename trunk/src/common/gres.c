/*****************************************************************************\
 *  gres.c - driver for gres plugin
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include <sys/stat.h>

#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define GRES_MAGIC 0x438a34d4

/* Gres symbols provided by the plugin */
typedef struct slurm_gres_ops {
	int		(*node_config_load)	( List gres_conf_list );
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
static char *gres_plugin_list = NULL;
static pthread_mutex_t gres_context_lock = PTHREAD_MUTEX_INITIALIZER;
static List gres_conf_list = NULL;

/* Local functions */
static gres_node_state_t *
		_build_gres_node_state(void);
static uint32_t	_build_id(char *gres_name);
static bitstr_t *_cpu_bitmap_rebuild(bitstr_t *old_cpu_bitmap, int new_size);
static void	_destroy_gres_slurmd_conf(void *x);
static uint32_t	_get_gres_cnt(char *orig_config, char *gres_name,
			      char *gres_name_colon, int gres_name_colon_len);
static char *	_get_gres_conf(void);
static uint32_t	_get_tot_gres_cnt(uint32_t plugin_id, uint32_t *set_cnt);
static void	_gres_job_list_delete(void *list_element);
static int	_job_config_validate(char *config, uint32_t *gres_cnt,
				     slurm_gres_context_t *context_ptr);
static void	_job_state_delete(void *gres_data);
static void *	_job_state_dup(void *gres_data);
static int	_job_state_validate(char *config, void **gres_data,
				    slurm_gres_context_t *gres_name);
extern uint32_t	_job_test(void *job_gres_data, void *node_gres_data,
			  bool use_total_gres, bitstr_t *cpu_bitmap,
			  int cpu_start_bit, int cpu_end_bit, bool *topo_set);
static int	_load_gres_plugin(char *plugin_name,
				  slurm_gres_context_t *plugin_context);
static int	_log_gres_slurmd_conf(void *x, void *arg);
static void	_my_stat(char *file_name);
static int	_node_config_init(char *node_name, char *orig_config,
				  slurm_gres_context_t *context_ptr,
				  gres_state_t *gres_ptr);
static int	_node_reconfig(char *node_name, char *orig_config,
			       char **new_config, gres_state_t *gres_ptr,
			       uint16_t fast_schedule,
			       slurm_gres_context_t *context_ptr);
static void	_node_state_dealloc(gres_state_t *gres_ptr);
static void *	_node_state_dup(void *gres_data);
static void	_node_state_log(void *gres_data, char *node_name,
				char *gres_name);
static int	_node_state_realloc(void *job_gres_data, int node_offset,
				    void *node_gres_data, char *gres_name);
static int	_parse_gres_config(void **dest, slurm_parser_enum_t type,
				   const char *key, const char *value,
				   const char *line, char **leftover);
static void	_set_gres_cnt(char *orig_config, char **new_config,
			      uint32_t new_cnt, char *gres_name,
			      char *gres_name_colon, int gres_name_colon_len);
static int	_step_state_validate(char *config, void **gres_data,
				     slurm_gres_context_t *context_ptr);
static uint32_t	_step_test(void *step_gres_data, void *job_gres_data,
			   int node_offset, bool ignore_alloc, char *gres_name);
static int	_strcmp(const char *s1, const char *s2);
static int	_unload_gres_plugin(slurm_gres_context_t *plugin_context);
static void	_validate_config(slurm_gres_context_t *context_ptr);
static int	_validate_file(char *path_name, char *gres_name);
static void	_validate_gres_node_cpus(gres_node_state_t *node_gres_ptr,
					 int cpus_ctld);


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

/* Variant of strcmp that will accept NULL string pointers */
static int  _strcmp(const char *s1, const char *s2)
{
	if ((s1 != NULL) && (s2 == NULL))
		return 1;
	if ((s1 == NULL) && (s2 == NULL))
		return 0;
	if ((s1 == NULL) && (s2 != NULL))
		return -1;
	return strcmp(s1, s2);
}

static int _load_gres_plugin(char *plugin_name,
			     slurm_gres_context_t *plugin_context)
{
	/*
	 * Must be synchronized with slurm_gres_ops_t above.
	 */
	static const char *syms[] = {
		"node_config_load",
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

	verbose("gres: Couldn't find the specified plugin name for %s "
		"looking at all files",
	      plugin_context->gres_type);

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
		verbose("Cannot find plugin of type %s",
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
			if (!strcmp(full_name, gres_context[i].gres_type))
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
	if (gres_context_cnt < 0)
		goto fini;

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
	for (i=0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		if ((strlen(msg) + strlen(gres_context[i].gres_name) + 9) >
		    msg_size)
 			break;
		strcat(msg, gres_context[i].gres_name);
		strcat(msg, "[:count]\n");
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

	*did_change = false;
	slurm_mutex_lock(&gres_context_lock);
	if (slurm_get_debug_flags() & DEBUG_FLAG_GRES)
		gres_debug = true;
	else
		gres_debug = false;

	if (_strcmp(plugin_names, gres_plugin_list))
		plugin_change = true;
	else
		plugin_change = false;
	slurm_mutex_unlock(&gres_context_lock);

	if (plugin_change) {
		error("GresPlugins changed from %s to %s ignored",
		     gres_plugin_list, plugin_names);
		error("Restart the slurmctld daemon to change GresPlugins");
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
 * Return the pathname of the gres.conf file
 */
static char *_get_gres_conf(void)
{
	char *val = getenv("SLURM_CONF");
	char *rc = NULL;
	int i;

	if (!val)
		return xstrdup(GRES_CONFIG_FILE);

	/* Replace file name on end of path */
	i = strlen(val) - strlen("slurm.conf") + strlen("gres.conf") + 1;
	rc = xmalloc(i);
	strcpy(rc, val);
	val = strrchr(rc, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = rc;
	strcpy(val, "gres.conf");
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
	xfree(p);
}

/*
 * Log the contents of a gres_slurmd_conf_t record
 */
static int _log_gres_slurmd_conf(void *x, void *arg)
{
	gres_slurmd_conf_t *p;

	if (!gres_debug)
		return 0;

	p = (gres_slurmd_conf_t *) x;
	xassert(p);
	if (p->cpus) {
		info("Gres Name:%s Count:%u ID:%u File:%s CPUs:%s CpuCnt:%u",
		     p->name, p->count, p->plugin_id, p->file, p->cpus,
		     p->cpu_cnt);
	} else if (p->file) {
		info("Gres Name:%s Count:%u ID:%u File:%s",
		     p->name, p->count, p->plugin_id, p->file);
	} else {
		info("Gres Name:%s Count:%u ID:%u", p->name, p->count,
		     p->plugin_id);
	}

	return 0;
}

static void _my_stat(char *file_name)
{
	struct stat config_stat;

	if (stat(file_name, &config_stat) < 0)
		fatal("can't stat gres.conf file %s: %m", file_name);
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
	p->name = xstrdup(value);
	p->cpu_cnt = gres_cpu_cnt;
	if (s_p_get_string(&p->cpus, "CPUs", tbl)) {
		bitstr_t *cpu_bitmap;	/* Just use to validate config */
		cpu_bitmap = bit_alloc(gres_cpu_cnt);
		if (cpu_bitmap == NULL)
			fatal("bit_alloc: malloc failure");
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
		else if (last[0] != '\0') {
			fatal("Invalid gres data for %s, Count=%s", p->name,
			      tmp_str);
		}
		if (tmp_long == 0)
			fatal("Invalid gres data for %s, Count=0", p->name);
		if (p->count && (p->count != tmp_long)) {
			fatal("Invalid gres data for %s, Count does not match "
			      "File value", p->name);
		}
		p->count = tmp_long;
		xfree(tmp_str);
	} else if (p->count == 0)
		p->count = 1;

	s_p_hashtbl_destroy(tbl);

	for (i=0; i<gres_context_cnt; i++) {
		if (strcasecmp(value, gres_context[i].gres_name) == 0)
			break;
	}
	if (i >= gres_context_cnt) {
		error("Ignoring gres.conf Name=%s", value);
		_destroy_gres_slurmd_conf(p);
		return 0;
	}
	p->plugin_id = gres_context[i].plugin_id;
	*dest = (void *)p;
	return 1;
}

static void _validate_config(slurm_gres_context_t *context_ptr)
{
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	int has_file = -1, rec_count = 0;

	iter = list_iterator_create(gres_conf_list);
	if (iter == NULL)
		fatal("list_iterator_create: malloc failure");
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
		if ((has_file == 0) && (rec_count > 1)) {
			fatal("gres.conf duplicate records for %s",
			      context_ptr->gres_name);
		}
	}
	list_iterator_destroy(iter);
}

/*
 * Load this node's configuration (how many resources it has, topology, etc.)
 * IN cpu_cnt - Number of CPUs on configured on this node
 */
extern int gres_plugin_node_config_load(uint32_t cpu_cnt)
{
	static s_p_options_t _gres_options[] = {
		{"Name", S_P_ARRAY, _parse_gres_config, NULL},
		{NULL}
	};

	int count, i, rc;
	struct stat config_stat;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t **gres_array;
	char *gres_conf_file = _get_gres_conf();

	rc = gres_plugin_init();
	if (gres_context_cnt == 0)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&gres_context_lock);
	gres_cpu_cnt = cpu_cnt;
	if (stat(gres_conf_file, &config_stat) < 0)
		fatal("can't stat gres.conf file %s: %m", gres_conf_file);
	tbl = s_p_hashtbl_create(_gres_options);
	if (s_p_parse_file(tbl, NULL, gres_conf_file) == SLURM_ERROR)
		fatal("error opening/reading %s", gres_conf_file);
	gres_conf_list = list_create(_destroy_gres_slurmd_conf);
	if (gres_conf_list == NULL)
		fatal("list_create: malloc failure");
	if (s_p_get_array((void ***) &gres_array, &count, "Name", tbl)) {
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
	uint16_t rec_cnt = 0, version= SLURM_PROTOCOL_VERSION;
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
		if (iter == NULL)
			fatal("list_iterator_create: malloc failure");
		while ((gres_slurmd_conf = 
				(gres_slurmd_conf_t *) list_next(iter))) {
			pack32(magic, buffer);
			pack32(gres_slurmd_conf->count, buffer);
			pack32(gres_slurmd_conf->cpu_cnt, buffer);
			pack8(gres_slurmd_conf->has_file, buffer);
			pack32(gres_slurmd_conf->plugin_id, buffer);
			packstr(gres_slurmd_conf->cpus, buffer);
			packstr(gres_slurmd_conf->name, buffer);
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
	char *tmp_cpus, *tmp_name;
	gres_slurmd_conf_t *p;

	rc = gres_plugin_init();

	FREE_NULL_LIST(gres_conf_list);
	gres_conf_list = list_create(_destroy_gres_slurmd_conf);
	if (gres_conf_list == NULL)
		fatal("list_create: malloc failure");

	safe_unpack16(&version, buffer);
	if (version != SLURM_2_2_PROTOCOL_VERSION)
		return SLURM_ERROR;

	safe_unpack16(&rec_cnt, buffer);
	if (rec_cnt == 0)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&gres_context_lock);
	for (i=0; i<rec_cnt; i++) {
		safe_unpack32(&magic, buffer);
		if (magic != GRES_MAGIC)
			goto unpack_error;
		safe_unpack32(&count, buffer);
		safe_unpack32(&cpu_cnt, buffer);
		safe_unpack8(&has_file, buffer);
		safe_unpack32(&plugin_id, buffer);
		safe_unpackstr_xmalloc(&tmp_cpus, &utmp32, buffer);
		safe_unpackstr_xmalloc(&tmp_name, &utmp32, buffer);
 		for (j=0; j<gres_context_cnt; j++) {
 			if (gres_context[j].plugin_id != plugin_id)
				continue;
			if (strcmp(gres_context[j].gres_name, tmp_name)) {
				/* Should be caught in gres_plugin_init() */
				error("gres_plugin_node_config_unpack: gres/%s"
				      " duplicate plugin ID with %s, unable "
				      "to process",
				      tmp_name, gres_context[j].gres_name);
				continue;
			}
			if (gres_context[j].has_file && !has_file) {
				error("gres_plugin_node_config_unpack: gres/%s"
				      " lacks File parameter for node %s",
				      tmp_name, node_name);
				has_file = 1;
			}
			if (has_file && (count > 1024)) {
				/* Avoid over-subscribing memory with huge
				 * bitmaps */
				error("gres_plugin_node_config_unpack: gres/%s"
				      " has File plus very large Count (%u) "
				      "for node %s, resetting value to 1024",
				      tmp_name, count, node_name);
				count = 1024;
			}
			gres_context[j].has_file = has_file;
			break;
 		}
		if (j >= gres_context_cnt) {
			/* A sign that GresPlugins is inconsistently
			 * configured. Not a fatal error. Skip this data. */
			error("gres_plugin_node_config_unpack: no plugin "
			      "configured to unpack data type %s from node %s",
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
		xfree(tmp_name);	/* Don't bother to preserve */
		p->plugin_id = plugin_id;
		list_append(gres_conf_list, p);
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
	for (i=0; i<gres_node_ptr->topo_cnt; i++) {
		FREE_NULL_BITMAP(gres_node_ptr->topo_cpus_bitmap[i]);
		FREE_NULL_BITMAP(gres_node_ptr->topo_gres_bitmap[i]);
	}
	xfree(gres_node_ptr->topo_cpus_bitmap);
	xfree(gres_node_ptr->topo_gres_bitmap);
	xfree(gres_node_ptr->topo_gres_cnt_alloc);
	xfree(gres_node_ptr->topo_gres_cnt_avail);
	xfree(gres_node_ptr);
	xfree(gres_ptr);
}

static uint32_t _get_gres_cnt(char *orig_config, char *gres_name,
			      char *gres_name_colon, int gres_name_colon_len)
{
	char *node_gres_config, *tok, *last_num = NULL, *last_tok = NULL;
	uint32_t gres_config_cnt = 0;

	if (orig_config == NULL)
		return gres_config_cnt;

	node_gres_config = xstrdup(orig_config);
	tok = strtok_r(node_gres_config, ",", &last_tok);
	while (tok) {
		if (!strcmp(tok, gres_name)) {
			gres_config_cnt = 1;
			break;
		}
		if (!strncmp(tok, gres_name_colon, gres_name_colon_len)) {
			tok += gres_name_colon_len;
			gres_config_cnt = strtol(tok, &last_num, 10);
			if (last_num[0] == '\0')
				;
			else if ((last_num[0] == 'k') || (last_num[0] == 'K'))
				gres_config_cnt *= 1024;
			else if ((last_num[0] == 'm') || (last_num[0] == 'M'))
				gres_config_cnt *= (1024 * 1024);
			break;
		}
		tok = strtok_r(NULL, ",", &last_tok);
	}
	xfree(node_gres_config);

	return gres_config_cnt;
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
		if (strcmp(tok, gres_name) &&
		    strncmp(tok, gres_name_colon, gres_name_colon_len)) {
			xstrcat(new_configured_res, tok);
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
	uint32_t gres_config_cnt = 0;
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

	gres_config_cnt = _get_gres_cnt(orig_config,
					context_ptr->gres_name,
					context_ptr->gres_name_colon,
					context_ptr->gres_name_colon_len);
	gres_data->gres_cnt_config = gres_config_cnt;
	/* Use count from recovered state, if higher */
	gres_data->gres_cnt_avail  = MAX(gres_data->gres_cnt_avail,
					 gres_config_cnt);
	if ((gres_data->gres_bit_alloc != NULL) &&
	    (gres_data->gres_cnt_avail > 
	     bit_size(gres_data->gres_bit_alloc))) {
		gres_data->gres_bit_alloc =
			bit_realloc(gres_data->gres_bit_alloc,
				    gres_data->gres_cnt_avail);
		if (gres_data->gres_bit_alloc == NULL)
			fatal("bit_alloc: malloc failure");
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
		if (*gres_list == NULL)
			fatal("list_create malloc failure");
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
	if (iter == NULL)
		fatal("list_iterator_create: malloc failure");
	while ((gres_slurmd_conf = (gres_slurmd_conf_t *) list_next(iter))) {
		if (gres_slurmd_conf->plugin_id != plugin_id)
			continue;
		gres_cnt += gres_slurmd_conf->count;
		rec_cnt++;
		if (gres_slurmd_conf->cpus)
			cpu_set_cnt++;
	}
	list_iterator_destroy(iter);
	if (cpu_set_cnt)
		*set_cnt = rec_cnt;
	return gres_cnt;
}

extern int _node_config_validate(char *node_name, char *orig_config,
				 char **new_config, gres_state_t *gres_ptr,
				 uint16_t fast_schedule, char **reason_down,
				 slurm_gres_context_t *context_ptr)
{
	int i, j, gres_inx, rc = SLURM_SUCCESS;
	uint32_t gres_cnt, set_cnt = 0;
	bool updated_config = false;
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
		gres_data->gres_cnt_found = gres_cnt;
		updated_config = true;
	}
	if (updated_config == false)
		return SLURM_SUCCESS;

	if ((set_cnt == 0) && (set_cnt != gres_data->topo_cnt)) {
		/* Need to clear topology info */
		xfree(gres_data->topo_gres_cnt_alloc);
		xfree(gres_data->topo_gres_cnt_avail);
		for (i=0; i<gres_data->topo_cnt; i++) {
			FREE_NULL_BITMAP(gres_data->topo_gres_bitmap[i]);
			FREE_NULL_BITMAP(gres_data->topo_cpus_bitmap[i]);
		}
		xfree(gres_data->topo_gres_bitmap);
		xfree(gres_data->topo_cpus_bitmap);
		gres_data->topo_cnt = set_cnt;
	}
	if (context_ptr->has_file && (set_cnt != gres_data->topo_cnt)) {
		/* Need to rebuild topology info */
		/* Resize the data structures here */
		gres_data->topo_gres_cnt_alloc = 
			xrealloc(gres_data->topo_gres_cnt_alloc,
				 set_cnt * sizeof(uint32_t));
		if (gres_data->topo_gres_cnt_alloc == NULL)
			fatal("xrealloc: malloc failure");
		gres_data->topo_gres_cnt_avail = 
			xrealloc(gres_data->topo_gres_cnt_avail,
				 set_cnt * sizeof(uint32_t));
		if (gres_data->topo_gres_cnt_alloc == NULL)
			fatal("xrealloc: malloc failure");
		for (i=0; i<gres_data->topo_cnt; i++)
			FREE_NULL_BITMAP(gres_data->topo_gres_bitmap[i]);
		gres_data->topo_gres_bitmap = 
			xrealloc(gres_data->topo_gres_bitmap,
				 set_cnt * sizeof(bitstr_t *));
		if (gres_data->topo_gres_bitmap == NULL)
			fatal("xrealloc: malloc failure");
		for (i=0; i<gres_data->topo_cnt; i++)
			FREE_NULL_BITMAP(gres_data->topo_cpus_bitmap[i]);
		gres_data->topo_cpus_bitmap = 
			xrealloc(gres_data->topo_cpus_bitmap,
				 set_cnt * sizeof(bitstr_t *));
		if (gres_data->topo_cpus_bitmap == NULL)
			fatal("xrealloc: malloc failure");
		gres_data->topo_cnt = set_cnt;

		iter = list_iterator_create(gres_conf_list);
		if (iter == NULL)
			fatal("list_iterator_create: malloc failure");
		gres_inx = i = 0;
		while ((gres_slurmd_conf = (gres_slurmd_conf_t *) 
			list_next(iter))) {
			if (gres_slurmd_conf->plugin_id !=
			    context_ptr->plugin_id)
				continue;
			gres_data->topo_gres_cnt_avail[i] =
					gres_slurmd_conf->count;
			gres_data->topo_cpus_bitmap[i] =
					bit_alloc(gres_slurmd_conf->cpu_cnt);
			if (gres_data->topo_cpus_bitmap[i] == NULL)
				fatal("bit_alloc: malloc failure");
			if (gres_slurmd_conf->cpus) {
				bit_unfmt(gres_data->topo_cpus_bitmap[i],
					  gres_slurmd_conf->cpus);
			} else {
				error("%s: has CPUs configured for only some "
				      "of the records on node %s",
				      context_ptr->gres_type, node_name);
				bit_nset(gres_data->topo_cpus_bitmap[i], 0,
					 (gres_slurmd_conf->cpu_cnt - 1));
			}
			gres_data->topo_gres_bitmap[i] = bit_alloc(gres_cnt);
			if (gres_data->topo_gres_bitmap[i] == NULL)
				fatal("bit_alloc: malloc failure");
			for (j=0; j<gres_slurmd_conf->count; j++) {
				bit_set(gres_data->topo_gres_bitmap[i],
					gres_inx++);
			}
			i++;
		}
		list_iterator_destroy(iter);
	}

	if ((orig_config == NULL) || (orig_config[0] == '\0'))
		gres_data->gres_cnt_config = 0;
	else if (gres_data->gres_cnt_config == NO_VAL) {
		/* This should have been filled in by _node_config_init() */
		gres_data->gres_cnt_config =
			_get_gres_cnt(orig_config, context_ptr->gres_name,
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
		if (gres_data->gres_bit_alloc == NULL) {
			gres_data->gres_bit_alloc =
				bit_alloc(gres_data->gres_cnt_avail);
		} else if (gres_data->gres_cnt_avail !=
			   bit_size(gres_data->gres_bit_alloc)) {
			gres_data->gres_bit_alloc =
				bit_realloc(gres_data->gres_bit_alloc,
					    gres_data->gres_cnt_avail);
		}
		if (gres_data->gres_bit_alloc == NULL)
			fatal("bit_alloc: malloc failure");
	}

	if ((fast_schedule < 2) && 
	    (gres_data->gres_cnt_found < gres_data->gres_cnt_config)) {
		if (reason_down && (*reason_down == NULL)) {
			xstrfmtcat(*reason_down, "%s count too low",
				   context_ptr->gres_type);
		}
		rc = EINVAL;
	} else if ((fast_schedule == 2) && gres_data->topo_cnt &&
		   (gres_data->gres_cnt_found != gres_data->gres_cnt_config)) {
		error("%s on node %s configured for %u resources but %u found,"
		      " ignoring topology support",
		      context_ptr->gres_type, node_name,
		      gres_data->gres_cnt_config, gres_data->gres_cnt_found);
		if (gres_data->topo_cpus_bitmap) {
			for (i=0; i<gres_data->topo_cnt; i++) {
				FREE_NULL_BITMAP(gres_data->topo_cpus_bitmap[i]);
				FREE_NULL_BITMAP(gres_data->topo_gres_bitmap[i]);
			}
			xfree(gres_data->topo_cpus_bitmap);
			xfree(gres_data->topo_gres_bitmap);
			xfree(gres_data->topo_gres_cnt_alloc);
			xfree(gres_data->topo_gres_cnt_avail);
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
		if (*gres_list == NULL)
			fatal("list_create malloc failure");
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

	gres_data->gres_cnt_config = _get_gres_cnt(orig_config, 
						   context_ptr->gres_name,
						   context_ptr->gres_name_colon,
						   context_ptr->
						   gres_name_colon_len);
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
		if (gres_data->gres_bit_alloc == NULL)
			fatal("bit_alloc: malloc failure");
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
		if (*gres_list == NULL)
			fatal("list_create malloc failure");
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
					 char *node_name)
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
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_node_list_delete);
		if (*gres_list == NULL)
			fatal("list_create malloc failure");
	}

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;
		safe_unpack32(&magic, buffer);
		if (magic != GRES_MAGIC)
			goto unpack_error;
		safe_unpack32(&plugin_id, buffer);
		safe_unpack32(&gres_cnt_avail, buffer);
		safe_unpack8(&has_bitmap, buffer);
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
			if (gres_node_ptr->gres_bit_alloc == NULL)
				fatal("bit_alloc: malloc failure");
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
	for (i=0; i<gres_ptr->topo_cnt; i++) {
		new_gres->topo_cpus_bitmap[i] =
			bit_copy(gres_ptr->topo_cpus_bitmap[i]);
		new_gres->topo_gres_bitmap[i] =
			bit_copy(gres_ptr->topo_gres_bitmap[i]);
		if ((new_gres->topo_cpus_bitmap[i] == NULL) ||
		    (new_gres->topo_gres_bitmap[i] == NULL))
			fatal("bit_copy: malloc failure");
		new_gres->topo_gres_cnt_alloc[i] =
			gres_ptr->topo_gres_cnt_alloc[i];
		new_gres->topo_gres_cnt_avail[i] =
			gres_ptr->topo_gres_cnt_avail[i];
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
		if (new_list == NULL)
			fatal("list_create malloc failure");
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
		if (i > 0)
			bit_nclear(gres_node_ptr->gres_bit_alloc, 0, i);
	}
	if (gres_node_ptr->topo_cnt && !gres_node_ptr->topo_gres_cnt_alloc) {
		for (i=0; i<gres_context_cnt; i++) {
			if (gres_ptr->plugin_id == gres_context[i].plugin_id) {
				gres_name = gres_context[i].gres_name;
				break;
			}
		}
		error("gres_plugin_node_state_dealloc_all: gres/%s topo_cnt!=0 "
		      "and topo_gres_cnt_alloc is NULL", gres_name);
	} else {
		for (i=0; i<gres_node_ptr->topo_cnt; i++) {
			gres_node_ptr->topo_gres_cnt_alloc[i] = 0;
		}
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

static void _node_state_log(void *gres_data, char *node_name, char *gres_name)
{
	gres_node_state_t *gres_node_ptr;
	int i;
	char tmp_str[128];

	xassert(gres_data);
	gres_node_ptr = (gres_node_state_t *) gres_data;
	info("gres/%s: state for %s", gres_name, node_name);
	info("  gres_cnt found:%u configured:%u avail:%u alloc:%u",
	     gres_node_ptr->gres_cnt_found, gres_node_ptr->gres_cnt_config,
	     gres_node_ptr->gres_cnt_avail, gres_node_ptr->gres_cnt_alloc);
	if (gres_node_ptr->gres_bit_alloc) {
		bit_fmt(tmp_str, sizeof(tmp_str), gres_node_ptr->gres_bit_alloc);
		info("  gres_bit_alloc:%s", tmp_str);
	} else {
		info("  gres_bit_alloc:NULL");
	}
	for (i=0; i<gres_node_ptr->topo_cnt; i++) {
		if (gres_node_ptr->topo_cpus_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_node_ptr->topo_cpus_bitmap[i]);
			info("  topo_cpu_bitmap[%d]:%s", i, tmp_str);
		} else
			info("  topo_cpu_bitmap[%d]:NULL", i);
		if (gres_node_ptr->topo_cpus_bitmap[i]) {
			bit_fmt(tmp_str, sizeof(tmp_str),
				gres_node_ptr->topo_gres_bitmap[i]);
			info("  topo_gres_bitmap[%d]:%s", i, tmp_str);
		} else
			info("  topo_gres_bitmap[%d]:NULL", i);
		info("  topo_gres_cnt_alloc[%d]:%u",i,
		     gres_node_ptr->topo_gres_cnt_alloc[i]);
		info("  topo_gres_cnt_avail[%d]:%u",i,
		     gres_node_ptr->topo_gres_cnt_avail[i]);
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
		for (i=0; i<gres_context_cnt; i++) {
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

static void _job_state_delete(void *gres_data)
{
	int i;
	gres_job_state_t *gres_ptr = (gres_job_state_t *) gres_data;

	if (gres_ptr == NULL)
		return;

	for (i=0; i<gres_ptr->node_cnt; i++) {
		if (gres_ptr->gres_bit_alloc)
			FREE_NULL_BITMAP(gres_ptr->gres_bit_alloc[i]);
		if (gres_ptr->gres_bit_step_alloc)
			FREE_NULL_BITMAP(gres_ptr->gres_bit_step_alloc[i]);
	}
	xfree(gres_ptr->gres_bit_alloc);
	xfree(gres_ptr->gres_bit_step_alloc);
	xfree(gres_ptr->gres_cnt_step_alloc);
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

static int _job_config_validate(char *config, uint32_t *gres_cnt,
				slurm_gres_context_t *context_ptr)
{
	char *last_num = NULL;
	int cnt;

	if (!strcmp(config, context_ptr->gres_name)) {
		cnt = 1;
	} else if (!strncmp(config, context_ptr->gres_name_colon,
			    context_ptr->gres_name_colon_len)) {
		config += context_ptr->gres_name_colon_len;
		cnt = strtol(config, &last_num, 10);
		if (last_num[0] == '\0')
			;
		else if ((last_num[0] == 'k') || (last_num[0] == 'K'))
			cnt *= 1024;
		else if ((last_num[0] == 'm') || (last_num[0] == 'M'))
			cnt *= (1024 * 1024);
		else
			return SLURM_ERROR;
		if (cnt <= 0)
			return SLURM_ERROR;
	} else
		return SLURM_ERROR;

	*gres_cnt = (uint32_t) cnt;
	return SLURM_SUCCESS;
}

static int _job_state_validate(char *config, void **gres_data,
			       slurm_gres_context_t *context_ptr)
{
	int rc;
	uint32_t gres_cnt;

	rc = _job_config_validate(config, &gres_cnt, context_ptr);
	if (rc == SLURM_SUCCESS) {
		gres_job_state_t *gres_ptr;
		gres_ptr = xmalloc(sizeof(gres_job_state_t));
		gres_ptr->gres_cnt_alloc = gres_cnt;
		*gres_data = gres_ptr;
	}

	return rc;
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
	void *gres_data;

	if ((req_config == NULL) || (req_config[0] == '\0')) {
		*gres_list = NULL;
		return SLURM_SUCCESS;
	}

	if ((rc = gres_plugin_init()) != SLURM_SUCCESS)
		return rc;

	slurm_mutex_lock(&gres_context_lock);
	tmp_str = xstrdup(req_config);
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_job_list_delete);
		if (*gres_list == NULL)
			fatal("list_create malloc failure");
	}

	tok = strtok_r(tmp_str, ",", &last);
	while (tok && (rc == SLURM_SUCCESS)) {
		rc2 = SLURM_ERROR;
		for (i=0; i<gres_context_cnt; i++) {
			rc2 = _job_state_validate(tok, &gres_data,
						  &gres_context[i]);
			if (rc2 != SLURM_SUCCESS)
				continue;
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = gres_context[i].plugin_id;
			gres_ptr->gres_data = gres_data;
			list_append(*gres_list, gres_ptr);
			break;		/* processed it */
		}
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
	if (gres_ptr->gres_bit_alloc) {
		new_gres_ptr->gres_bit_alloc	= xmalloc(sizeof(bitstr_t *) *
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

/*
 * Create a (partial) copy of a job's gres state for job binding
 * IN gres_list - List of Gres records for this job to track usage
 * RET The copy or NULL on failure
 * NOTE: Only gres_cnt_alloc, node_cnt and gres_bit_alloc are copied
 *	 Job step details are NOT copied.
 */
List gres_plugin_job_state_dup(List gres_list)
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
		new_gres_data = _job_state_dup(gres_ptr->gres_data);
		if (new_gres_data == NULL)
			break;
		if (new_gres_list == NULL) {
			new_gres_list = list_create(_gres_job_list_delete);
			if (new_gres_list == NULL)
				fatal("list_create: malloc failure");
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
				      uint32_t job_id, bool details)
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
		pack32(magic, buffer);
		pack32(gres_ptr->plugin_id, buffer);
		pack32(gres_job_ptr->gres_cnt_alloc, buffer);
		pack32(gres_job_ptr->node_cnt, buffer);
		if (gres_job_ptr->gres_bit_alloc) {
			pack8((uint8_t) 1, buffer);
			for (i=0; i<gres_job_ptr->node_cnt; i++) {
				pack_bit_str(gres_job_ptr->gres_bit_alloc[i],
					     buffer);
			}
		} else {
			pack8((uint8_t) 0, buffer);
		}
		if (details && gres_job_ptr->gres_bit_step_alloc) {
			pack8((uint8_t) 1, buffer);
			for (i=0; i<gres_job_ptr->node_cnt; i++) {
				pack_bit_str(gres_job_ptr->
					     gres_bit_step_alloc[i], buffer);
			}
		} else {
			pack8((uint8_t) 0, buffer);
		}
		if (details && gres_job_ptr->gres_cnt_step_alloc) {
			pack8((uint8_t) 1, buffer);
			for (i=0; i<gres_job_ptr->node_cnt; i++) {
				pack32(gres_job_ptr->gres_cnt_step_alloc[i],
				       buffer);
			}
		} else {
			pack8((uint8_t) 0, buffer);
		}
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
 * Unpack a job's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_job_state_pack()
 * IN/OUT buffer - location to read state from
 * IN job_id - job's ID
 */
extern int gres_plugin_job_state_unpack(List *gres_list, Buf buffer,
					uint32_t job_id)
{
	int i, rc;
	uint32_t magic, plugin_id;
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
		if (*gres_list == NULL)
			fatal("list_create malloc failure");
	}

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;
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
				unpack_bit_str(&gres_job_ptr->gres_bit_alloc[i],
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
					       gres_bit_step_alloc[i], buffer);
			}
		}
		safe_unpack8(&has_more, buffer);
		if (has_more) {
			gres_job_ptr->gres_cnt_step_alloc =
				xmalloc(sizeof(uint32_t) *
					gres_job_ptr->node_cnt);
			for (i=0; i<gres_job_ptr->node_cnt; i++) {
				safe_unpack32(&gres_job_ptr->
					      gres_cnt_step_alloc[i], buffer);
			}
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
	if (new_cpu_bitmap == NULL)
		fatal("bit_alloc: malloc failure");
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
				     int cpus_ctld)
{
	int i, cpus_slurmd;
	bitstr_t *new_cpu_bitmap;

	if (node_gres_ptr->topo_cnt == 0)
		return;
	cpus_slurmd = bit_size(node_gres_ptr->topo_cpus_bitmap[0]);
	if (cpus_slurmd == cpus_ctld)
		return;

	debug("Gres CPU count mismatch (%d != %d)", cpus_slurmd, cpus_ctld);
	for (i=0; i<node_gres_ptr->topo_cnt; i++) {
		if (i != 0) {
			cpus_slurmd = bit_size(node_gres_ptr->
					       topo_cpus_bitmap[i]);
		}
		if (cpus_slurmd == cpus_ctld)	/* should never happen here */
			continue;
		new_cpu_bitmap = _cpu_bitmap_rebuild(node_gres_ptr->
						     topo_cpus_bitmap[i],
						     cpus_ctld);
		FREE_NULL_BITMAP(node_gres_ptr->topo_cpus_bitmap[i]);
		node_gres_ptr->topo_cpus_bitmap[i] = new_cpu_bitmap;
	}
}

extern uint32_t _job_test(void *job_gres_data, void *node_gres_data,
			  bool use_total_gres, bitstr_t *cpu_bitmap,
			  int cpu_start_bit, int cpu_end_bit, bool *topo_set)
{
	int i, j, cpus_ctld, gres_avail = 0, top_inx;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	uint32_t *cpus_avail = NULL, cpu_cnt = 0;
	bitstr_t *alloc_cpu_bitmap = NULL;

	if (job_gres_ptr->gres_cnt_alloc && node_gres_ptr->topo_cnt &&
	    *topo_set) {
		/* Need to determine how many gres available for these 
		 * specific CPUs */
		if (cpu_bitmap) {
			cpus_ctld = cpu_end_bit - cpu_start_bit + 1;
			if (cpus_ctld < 1) {
				error("gres_plugin_job_test: cpus on node < 1");
				return (uint32_t) 0;
			}
			_validate_gres_node_cpus(node_gres_ptr, cpus_ctld);
		} else {
			cpus_ctld = bit_size(node_gres_ptr->topo_cpus_bitmap[0]);
		}
		for (i=0; i<node_gres_ptr->topo_cnt; i++) {
			for (j=0; j<cpus_ctld; j++) {
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
		if (cpu_bitmap) {
			cpus_ctld = cpu_end_bit - cpu_start_bit + 1;
			if (cpus_ctld < 1) {
				error("gres_plugin_job_test: cpus on node < 1");
				return (uint32_t) 0;
			}
			_validate_gres_node_cpus(node_gres_ptr, cpus_ctld);
		} else {
			cpus_ctld = bit_size(node_gres_ptr->topo_cpus_bitmap[0]);
		}
		cpus_avail = xmalloc(sizeof(uint32_t) * node_gres_ptr->topo_cnt);
		alloc_cpu_bitmap = bit_alloc(cpus_ctld);
		if (alloc_cpu_bitmap == NULL)
			fatal("bit_alloc: malloc failure");
		for (i=0; i<node_gres_ptr->topo_cnt; i++) {
			if (node_gres_ptr->topo_gres_cnt_avail[i] == 0)
				continue;
			if (!use_total_gres &&
			    (node_gres_ptr->topo_gres_cnt_alloc[i] >=
			     node_gres_ptr->topo_gres_cnt_avail[i]))
				continue;
			for (j=0; j<cpus_ctld; j++) {
				if (cpu_bitmap && 
				    !bit_test(cpu_bitmap, cpu_start_bit+j))
					continue;
				if (bit_test(node_gres_ptr->
					     topo_cpus_bitmap[i], j) &&
				    !bit_test(alloc_cpu_bitmap, j)) {
					bit_set(alloc_cpu_bitmap, j);
					cpus_avail[i]++;
				}
			}
		}

		/* Pick the gres with the most CPUs available */
		bit_nclear(alloc_cpu_bitmap, 0, (cpus_ctld - 1));
		for (i=0; i<job_gres_ptr->gres_cnt_alloc; i++) {
			top_inx = -1;
			for (j=0; j<node_gres_ptr->topo_cnt; j++) {
				if (top_inx == -1) {
					if (cpus_avail[j])
						top_inx = j;
				} else if (cpus_avail[j] > cpus_avail[top_inx])
					top_inx = j;
			}
			if ((top_inx < 0) || (cpus_avail[top_inx] == 0)) {
				cpu_cnt = 0;
				break;
			}
			cpu_cnt += cpus_avail[top_inx];
			cpus_avail[top_inx] = 0;
			bit_or(alloc_cpu_bitmap,
			       node_gres_ptr->topo_cpus_bitmap[top_inx]);
		}
		if (cpu_bitmap && (cpu_cnt > 0)) {
			*topo_set = true;
			for (i=0; i<cpus_ctld; i++) {
				if (!bit_test(alloc_cpu_bitmap, i))
					bit_clear(cpu_bitmap, cpu_start_bit+i);
			}
		}
		FREE_NULL_BITMAP(alloc_cpu_bitmap);
		xfree(cpus_avail);
		return cpu_cnt;
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
 * Determine how many CPUs on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN cpu_bitmap     - Identification of available CPUs (NULL if no restriction)
 * IN cpu_start_bit  - index into cpu_bitmap for this node's first CPU
 * IN cpu_end_bit    - index into cpu_bitmap for this node's last CPU
 * RET: NO_VAL    - All CPUs on node are available
 *      otherwise - Specific CPU count
 */
extern uint32_t gres_plugin_job_test(List job_gres_list, List node_gres_list, 
				     bool use_total_gres, bitstr_t *cpu_bitmap,
				     int cpu_start_bit, int cpu_end_bit)
{
	int i;
	uint32_t cpu_cnt, tmp_cnt;
	ListIterator job_gres_iter,  node_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;
	bool topo_set = false;

	if (job_gres_list == NULL)
		return NO_VAL;
	if (node_gres_list == NULL)
		return NO_VAL;

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
					    &topo_set);
			cpu_cnt = MIN(tmp_cnt, cpu_cnt);
			break;
		}
		if (cpu_cnt == 0)
			break;
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return cpu_cnt;
}

extern int _job_alloc(void *job_gres_data, void *node_gres_data,
		      int node_cnt, int node_offset, uint32_t cpu_cnt,
		      char *gres_name)
{
	int i;
	uint32_t gres_cnt;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;

	/*
	 * Validate data structures. Either job_gres_data->node_cnt and
	 * job_gres_data->gres_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_cnt);
	xassert(node_offset >= 0);
	xassert(job_gres_ptr);
	xassert(node_gres_ptr);
	if (job_gres_ptr->node_cnt == 0) {
		job_gres_ptr->node_cnt = node_cnt;
		if (job_gres_ptr->gres_bit_alloc) {
			error("gres/%s: node_cnt==0 and bit_alloc is set",
			      gres_name);
			xfree(job_gres_ptr->gres_bit_alloc);
		}
		job_gres_ptr->gres_bit_alloc = xmalloc(sizeof(bitstr_t *) *
						      node_cnt);
	} else if (job_gres_ptr->node_cnt < node_cnt) {
		error("gres/%s: node_cnt increase from %u to %d",
		      gres_name, job_gres_ptr->node_cnt, node_cnt);
		if (node_offset >= job_gres_ptr->node_cnt)
			return SLURM_ERROR;
	} else if (job_gres_ptr->node_cnt > node_cnt) {
		error("gres/%s: node_cnt decrease from %u to %d",
		      gres_name, job_gres_ptr->node_cnt, node_cnt);
	}

	/*
	 * Check that sufficient resources exist on this node
	 */
	gres_cnt = (job_gres_ptr->gres_cnt_alloc * cpu_cnt);
	i =  node_gres_ptr->gres_cnt_alloc + gres_cnt;
	i -= node_gres_ptr->gres_cnt_avail;
	if (i > 0) {
		error("gres/%s: overallocated resources by %d", gres_name, i);
		/* proceed with request, give job what's available */
	}

	if (job_gres_ptr->gres_cnt_step_alloc == NULL) {
		job_gres_ptr->gres_cnt_step_alloc =
			xmalloc(sizeof(uint32_t) * node_cnt);
	}

	/*
	 * Select the specific resources to use for this job.
	 * We'll need to add topology information in the future
	 */	
	if (job_gres_ptr->gres_bit_alloc[node_offset]) {
		/* Resuming a suspended job, resources already allocated */
		debug("gres/%s: job's bit_alloc is already set for node %d",
		      gres_name, node_offset);
		if (node_gres_ptr->gres_bit_alloc) {
			gres_cnt = MIN(bit_size(node_gres_ptr->gres_bit_alloc),
				       bit_size(job_gres_ptr->
						gres_bit_alloc[node_offset]));
			for (i=0; i<gres_cnt; i++) {
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
		if (job_gres_ptr->gres_bit_alloc[node_offset] == NULL)
			fatal("bit_copy: malloc failure");
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

	return SLURM_SUCCESS;
}

/*
 * Allocate resource to a job and update node and job gres information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		gres_plugin_node_config_validate()
 * IN node_cnt - total number of nodes originally allocated to the job
 * IN node_offset - zero-origin index to the node of interest
 * IN cpu_cnt - number of CPUs allocated to this job on this node
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_alloc(List job_gres_list, List node_gres_list, 
				 int node_cnt, int node_offset,
				 uint32_t cpu_cnt)
{
	int i, rc, rc2;
	ListIterator job_gres_iter,  node_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL)
		return SLURM_ERROR;

	rc = gres_plugin_init();

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
		if (node_gres_ptr == NULL)
			continue;

		for (i=0; i<gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id != 
			    gres_context[i].plugin_id)
				continue;
			rc2 = _job_alloc(job_gres_ptr->gres_data, 
					 node_gres_ptr->gres_data, node_cnt,
					 node_offset, cpu_cnt,
					 gres_context[i].gres_name);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

static int _job_dealloc(void *job_gres_data, void *node_gres_data,
		        int node_offset, char *gres_name)
{
	int i, len;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;

	/*
	 * Validate data structures. Either job_gres_data->node_cnt and
	 * job_gres_data->gres_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_offset >= 0);
	xassert(job_gres_ptr);
	xassert(node_gres_ptr);
	if (job_gres_ptr->node_cnt <= node_offset) {
		error("gres/%s bad node_offset %d count is %u",
		      gres_name, node_offset, job_gres_ptr->node_cnt);
		return SLURM_ERROR;
	}

	if (node_gres_ptr->gres_bit_alloc && job_gres_ptr->gres_bit_alloc &&
	    job_gres_ptr->gres_bit_alloc[node_offset]) {    
		len = bit_size(job_gres_ptr->gres_bit_alloc[node_offset]);
		i   = bit_size(node_gres_ptr->gres_bit_alloc);
		if (i != len) {
			error("gres/%s: job and node bitmap sizes differ "
			      "(%d != %d)", gres_name, len, i);
			len = MIN(len, i);
			/* proceed with request, make best effort */
		}
		for (i=0; i<len; i++) {
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
				error("gres/%s: job's gres count underflow",
				      gres_name);
			}
		}
	} else if (node_gres_ptr->gres_cnt_alloc >= 
		   job_gres_ptr->gres_cnt_alloc) {
		node_gres_ptr->gres_cnt_alloc -= job_gres_ptr->gres_cnt_alloc;
	} else {
		node_gres_ptr->gres_cnt_alloc = 0;
		error("gres/%s: job's gres count underflow", gres_name);
	}

	return SLURM_SUCCESS;
}

/*
 * Deallocate resource from a job and update node and job gres information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		gres_plugin_node_config_validate()
 * IN node_offset - zero-origin index to the node of interest
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_job_dealloc(List job_gres_list, List node_gres_list, 
				   int node_offset)
{
	int i, rc, rc2;
	ListIterator job_gres_iter,  node_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL)
		return SLURM_ERROR;

	rc = gres_plugin_init();

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
		if (node_gres_ptr == NULL)
			continue;

		for (i=0; i<gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id != 
			    gres_context[i].plugin_id)
				continue;
			rc2 = _job_dealloc(job_gres_ptr->gres_data, 
					   node_gres_ptr->gres_data,
					   node_offset,
					   gres_context[i].gres_name);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

static void _job_state_log(void *gres_data, uint32_t job_id, char *gres_name)
{
	gres_job_state_t *gres_ptr;
	char tmp_str[128];
	int i;

	xassert(gres_data);
	gres_ptr = (gres_job_state_t *) gres_data;
	info("gres: %s state for job %u", gres_name, job_id);
	info("  gres_cnt:%u node_cnt:%u", gres_ptr->gres_cnt_alloc,
	     gres_ptr->node_cnt);
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

static void _step_state_delete(void *gres_data)
{
	int i;
	gres_step_state_t *gres_ptr = (gres_step_state_t *) gres_data;

	if (gres_ptr == NULL)
		return;

	if (gres_ptr->gres_bit_alloc) {
		for (i=0; i<gres_ptr->node_cnt; i++)
			FREE_NULL_BITMAP(gres_ptr->gres_bit_alloc[i]);
		xfree(gres_ptr->gres_bit_alloc);
	}
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
	int rc;
	uint32_t gres_cnt;

	rc = _job_config_validate(config, &gres_cnt, context_ptr);
	if (rc == SLURM_SUCCESS) {
		gres_step_state_t *gres_ptr;
		gres_ptr = xmalloc(sizeof(gres_step_state_t));
		gres_ptr->gres_cnt_alloc = gres_cnt;
		*gres_data = gres_ptr;
	}

	return rc;
}

static uint32_t _step_test(void *step_gres_data, void *job_gres_data,
			   int node_offset, bool ignore_alloc, char *gres_name)
{
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint32_t gres_cnt;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if (node_offset == NO_VAL) {
		if (step_gres_ptr->gres_cnt_alloc > job_gres_ptr->gres_cnt_alloc)
			return 0;
		return NO_VAL;
	}

	if (node_offset >= job_gres_ptr->node_cnt) {
		error("gres/%s: step_test node offset invalid (%d >= %u)",
		      gres_name, node_offset, job_gres_ptr->node_cnt);
		return 0;
	}

	if (job_gres_ptr->gres_cnt_step_alloc) {
		if (step_gres_ptr->gres_cnt_alloc >
		    (job_gres_ptr->gres_cnt_alloc -
		     job_gres_ptr->gres_cnt_step_alloc[node_offset]))
			return 0;
	} else {
		error("gres/%s: gres_cnt_step_alloc gres_bit_alloc is NULL",
		      gres_name);
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
		debug("gres/%s: step_test gres_bit_alloc is NULL", gres_name);
		gres_cnt = NO_VAL;
	}

	return gres_cnt;
}

/*
 * Given a step's requested gres configuration, validate it and build gres list
 * IN req_config - step request's gres input string
 * OUT step_gres_list - List of Gres records for this step to track usage
 * IN job_gres_list - List of Gres records for this job
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_plugin_step_state_validate(char *req_config,
					   List *step_gres_list,
					   List job_gres_list)
{
	char *tmp_str, *tok, *last = NULL;
	int i, rc, rc2, rc3;
	gres_state_t *step_gres_ptr, *job_gres_ptr;
	void *step_gres_data, *job_gres_data;
	ListIterator job_gres_iter;

	*step_gres_list = NULL;
	if ((req_config == NULL) || (req_config[0] == '\0'))
		return SLURM_SUCCESS;
	if (job_gres_list == NULL) {
		info("step has gres spec, while job has none");
		return ESLURM_INVALID_GRES;
	}

	if ((rc = gres_plugin_init()) != SLURM_SUCCESS)
		return rc;

	slurm_mutex_lock(&gres_context_lock);
	tmp_str = xstrdup(req_config);
	if ((gres_context_cnt > 0) && (*step_gres_list == NULL)) {
		*step_gres_list = list_create(_gres_step_list_delete);
		if (*step_gres_list == NULL)
			fatal("list_create malloc failure");
	}

	tok = strtok_r(tmp_str, ",", &last);
	while (tok && (rc == SLURM_SUCCESS)) {
		rc2 = SLURM_ERROR;
		for (i=0; i<gres_context_cnt; i++) {
			rc2 = _step_state_validate(tok, &step_gres_data,
						   &gres_context[i]);
			if (rc2 != SLURM_SUCCESS)
				break;
			/* Now make sure the step's request isn't too big for
			 * the job's gres allocation */
			job_gres_iter = list_iterator_create(job_gres_list);
			while ((job_gres_ptr = (gres_state_t *)
					list_next(job_gres_iter))) {
				if (job_gres_ptr->plugin_id ==
				    gres_context[i].plugin_id)
					break;
			}
			list_iterator_destroy(job_gres_iter);
			if (job_gres_ptr == NULL) {
				info("Step gres request not in job alloc %s",
				     tok);
				rc = ESLURM_INVALID_GRES;
				_step_state_delete(step_gres_data);
				break;
			}
			job_gres_data = job_gres_ptr->gres_data;
			rc3 = _step_test(step_gres_data, job_gres_data, NO_VAL,
					 true, gres_context[i].gres_name);
			if (rc3 == 0) {
				info("Step gres higher than in job allocation "
				     "%s", tok);
				rc = ESLURM_INVALID_GRES;
				_step_state_delete(step_gres_data);
				break;
			}

			step_gres_ptr = xmalloc(sizeof(gres_state_t));
			step_gres_ptr->plugin_id = gres_context[i].plugin_id;
			step_gres_ptr->gres_data = step_gres_data;
			list_append(*step_gres_list, step_gres_ptr);
			break;		/* processed it */
		}
		if (rc2 != SLURM_SUCCESS) {
			info("Invalid gres step specification %s", tok);
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

/*
 * Create a copy of a step's gres state
 * IN gres_list - List of Gres records for this step to track usage
 * RET The copy or NULL on failure
 */
List gres_plugin_step_state_dup(List gres_list)
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
		new_gres_data = _step_state_dup(gres_ptr->gres_data);
		if (new_gres_list == NULL) {
			new_gres_list = list_create(_gres_step_list_delete);
			if (new_gres_list == NULL)
				fatal("list_create: malloc failure");
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
 * Pack a step's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_plugin_step_allocate()
 * IN/OUT buffer - location to write state to
 * IN job_id, step_id - job and step ID for logging
 */
extern int gres_plugin_step_state_pack(List gres_list, Buf buffer,
				       uint32_t job_id, uint32_t step_id)
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
		pack32(magic, buffer);
		pack32(gres_ptr->plugin_id, buffer);
		pack32(gres_step_ptr->gres_cnt_alloc, buffer);
		pack32(gres_step_ptr->node_cnt, buffer);
		if (gres_step_ptr->gres_bit_alloc) {
			pack8((uint8_t) 1, buffer);
			for (i=0; i<gres_step_ptr->node_cnt; i++)
				pack_bit_str(gres_step_ptr->gres_bit_alloc[i],
					     buffer);
		} else {
			pack8((uint8_t) 0, buffer);
		}
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
 * Unpack a step's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_step_state_pack()
 * IN/OUT buffer - location to read state from
 * IN job_id, step_id - job and step ID for logging
 */
extern int gres_plugin_step_state_unpack(List *gres_list, Buf buffer,
					 uint32_t job_id, uint32_t step_id)
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
		if (*gres_list == NULL)
			fatal("list_create malloc failure");
	}

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;
		safe_unpack32(&magic, buffer);
		if (magic != GRES_MAGIC)
			goto unpack_error;
		safe_unpack32(&plugin_id, buffer);
		gres_step_ptr = xmalloc(sizeof(gres_step_state_t));
		safe_unpack32(&gres_step_ptr->gres_cnt_alloc, buffer);
		safe_unpack32(&gres_step_ptr->node_cnt, buffer);
		safe_unpack8(&has_file, buffer);
		if (has_file) {
			gres_step_ptr->gres_bit_alloc =
				xmalloc(sizeof(bitstr_t) *
					gres_step_ptr->node_cnt);
			for (i=0; i<gres_step_ptr->node_cnt; i++) {
				unpack_bit_str(&gres_step_ptr->gres_bit_alloc[i],
                                               buffer);
			}
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

static void _step_state_log(void *gres_data, uint32_t job_id, uint32_t step_id,
			    char *gres_name)
{
	gres_step_state_t *gres_ptr = (gres_step_state_t *) gres_data;
	char tmp_str[128];
	int i;

	xassert(gres_ptr);
	info("gres/%s state for step %u.%u", gres_name, job_id, step_id);
	info("  gres_cnt:%u node_cnt:%u", gres_ptr->gres_cnt_alloc,
	     gres_ptr->node_cnt);

	if (gres_ptr->gres_bit_alloc == NULL)
		info("  gres_bit_alloc:NULL");
	else {
		for (i=0; i<gres_ptr->node_cnt; i++) {
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
 * RET Count of available CPUs on this node, NO_VAL if no limit
 */
extern uint32_t gres_plugin_step_test(List step_gres_list, List job_gres_list,
				      int node_offset, bool ignore_alloc)
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
					     gres_context[i].gres_name);
			cpu_cnt = MIN(tmp_cnt, cpu_cnt);
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
		       int node_offset, int cpu_cnt, char *gres_name)
{
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint32_t gres_avail, gres_needed;
	bitstr_t *gres_bit_alloc;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);
	step_gres_ptr->node_cnt = job_gres_ptr->node_cnt;	/* FIXME */
	if (node_offset >= job_gres_ptr->node_cnt) {
		error("gres/%s: step_alloc node offset invalid (%d >= %u)",
		      gres_name, node_offset, job_gres_ptr->node_cnt);
		return SLURM_ERROR;
	}
	if (job_gres_ptr->gres_cnt_step_alloc) {
		if (step_gres_ptr->gres_cnt_alloc >
		    (job_gres_ptr->gres_cnt_alloc - 
		     job_gres_ptr->gres_cnt_step_alloc[node_offset]))
			return SLURM_ERROR;
		job_gres_ptr->gres_cnt_step_alloc[node_offset] +=
			step_gres_ptr->gres_cnt_alloc;
	} else {
		error("gres/%s: gres_cnt_step_alloc gres_bit_alloc is NULL",
		      gres_name);
		return SLURM_ERROR;
	}
	if ((job_gres_ptr->gres_bit_alloc == NULL) ||
	    (job_gres_ptr->gres_bit_alloc[node_offset] == NULL)) {
		debug("gres/%s: step_alloc gres_bit_alloc is NULL", gres_name);
		return SLURM_SUCCESS;
	}

	gres_bit_alloc = bit_copy(job_gres_ptr->gres_bit_alloc[node_offset]);
	if (gres_bit_alloc == NULL)
		fatal("bit_copy malloc failure");
	if (job_gres_ptr->gres_bit_step_alloc &&
	    job_gres_ptr->gres_bit_step_alloc[node_offset]) {
		bit_not(job_gres_ptr->gres_bit_step_alloc[node_offset]);
		bit_and(gres_bit_alloc,
			job_gres_ptr->gres_bit_step_alloc[node_offset]);
		bit_not(job_gres_ptr->gres_bit_step_alloc[node_offset]);
	}
	gres_avail  = bit_set_count(gres_bit_alloc);
	gres_needed = step_gres_ptr->gres_cnt_alloc;
	if (gres_needed > gres_avail) {
		error("gres/%s: step oversubscribing resources on node %d",
		      gres_name, node_offset);
	} else {
		int gres_rem = gres_needed;
		int i, len = bit_size(gres_bit_alloc);
		for (i=0; i<len; i++) {
			if (gres_rem > 0) {
				if (bit_test(gres_bit_alloc, i))
					gres_rem--;
			} else {
				bit_clear(gres_bit_alloc, i);
			}
		}
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
		error("gres/%s: step bit_alloc already exists", gres_name);
		bit_or(step_gres_ptr->gres_bit_alloc[node_offset],gres_bit_alloc);
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
 * IN node_offset - zero-origin index to the node of interest
 * IN cpu_cnt - number of CPUs allocated to this job on this node
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_step_alloc(List step_gres_list, List job_gres_list,
				  int node_offset, int cpu_cnt)
{
	int i, rc, rc2;
	ListIterator step_gres_iter,  job_gres_iter;
	gres_state_t *step_gres_ptr, *job_gres_ptr;

	if (step_gres_list == NULL)
		return SLURM_SUCCESS;
	if (job_gres_list == NULL)
		return SLURM_ERROR;

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
			rc2 = _step_alloc(step_gres_ptr->gres_data, 
					  job_gres_ptr->gres_data,
					  node_offset, cpu_cnt,
					  gres_context[i].gres_name);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
			break;
		}
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}


static int _step_dealloc(void *step_gres_data, void *job_gres_data,
			 char *gres_name)
{

	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint32_t i, j, node_cnt;
	int len_j, len_s;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);
	node_cnt = MIN(job_gres_ptr->node_cnt, step_gres_ptr->node_cnt);
	for (i=0; i<node_cnt; i++) {
		if (job_gres_ptr->gres_cnt_step_alloc) {
			if (job_gres_ptr->gres_cnt_step_alloc[i] >=
			    step_gres_ptr->gres_cnt_alloc) {
				job_gres_ptr->gres_cnt_step_alloc[i] -=
					step_gres_ptr->gres_cnt_alloc;
			} else {
				error("gres/%s: step dealloc count underflow",
				      gres_name);
				job_gres_ptr->gres_cnt_step_alloc[i] = 0;
			}
		}
		if ((step_gres_ptr->gres_bit_alloc == NULL) ||
		    (step_gres_ptr->gres_bit_alloc[i] == NULL))
			continue;
		if (job_gres_ptr->gres_bit_alloc[i] == NULL) {
			error("gres/%s: step dealloc, job's bit_alloc[%d] is "
			      "NULL", gres_name, i);
			continue;
		}
		len_j = bit_size(job_gres_ptr->gres_bit_alloc[i]);
		len_s = bit_size(step_gres_ptr->gres_bit_alloc[i]);
		if (len_j != len_s) {
			error("gres/%s: step dealloc, bit_alloc[%d] size "
			      "mis-match (%d != %d)",
			      gres_name, i, len_j, len_s);
			len_j = MIN(len_j, len_s);
		}
		for (j=0; j<len_j; j++) {
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
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_step_dealloc(List step_gres_list, List job_gres_list)
{
	int i, rc, rc2;
	ListIterator step_gres_iter,  job_gres_iter;
	gres_state_t *step_gres_ptr, *job_gres_ptr;

	if (step_gres_list == NULL)
		return SLURM_SUCCESS;
	if (job_gres_list == NULL)
		return SLURM_ERROR;

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
					   gres_context[i].gres_name);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
			break;
		}
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}
