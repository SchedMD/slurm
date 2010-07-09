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
#else /* ! HAVE_CONFIG_H */
#  include <sys/types.h>
#  include <unistd.h>
#  include <stdint.h>
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

typedef struct slurm_gres_ops {
	uint32_t	(*plugin_id);
	char		(*gres_name);
	char		(*help_msg);
	int		(*node_config_load)	( List gres_conf_list );
	int		(*node_config_validate)	( char *node_name,
						  uint32_t gres_cnt,
						  char *orig_config,
						  char **new_config,
						  void **gres_data,
						  uint16_t fast_schedule,
						  char **reason_down );
	int		(*node_reconfig)	( char *node_name,
						  char *orig_config,
						  char **new_config,
						  void **gres_data,
						  uint16_t fast_schedule );
	int		(*node_state_pack)	( void *gres_data,
						  Buf buffer );
	int		(*node_state_unpack)	( void **gres_data,
						  Buf buffer );
	void *		(*node_state_dup)	( void *gres_data );
	void		(*node_state_dealloc)	( void *gres_data );
	int		(*node_state_realloc)	( void *job_gres_data,
						  int node_offset,
						  void *node_gres_data );
	void		(*node_state_log)	( void *gres_data,
						  char *node_name );

	void		(*job_state_delete)	( void *gres_data );
	int		(*job_state_validate)	( char *config,
						  void **gres_data );
	void *		(*job_state_dup)	( void *gres_data );
	int		(*job_state_pack)	( void *gres_data,
						  Buf buffer );
	int		(*job_state_unpack)	( void **gres_data,
						  Buf buffer );
	void		(*job_state_log)	( void *gres_data,
						  uint32_t job_id );
	uint32_t	(*job_test)		( void *job_gres_data,
						  void *node_gres_data,
						  bool use_total_gres );
	int		(*job_alloc)		( void *job_gres_data,
						  void *node_gres_data,
						  int node_cnt,
						  int node_offset,
						  uint32_t cpu_cnt );
	int		(*job_dealloc)		( void *job_gres_data,
						  void *node_gres_data,
						  int node_offset );
	void		(*step_state_delete)	( void *gres_data );
	int		(*step_state_validate)	( char *config,
						  void **gres_data );
	void *		(*step_state_dup)	( void *gres_data );
	int		(*step_state_pack)	( void *gres_data,
						  Buf buffer );
	int		(*step_state_unpack)	( void **gres_data,
						  Buf buffer );
	void		(*step_state_log)	( void *gres_data,
						  uint32_t job_id,
						  uint32_t step_id );
	uint32_t	(*step_test)		( void *job_gres_data,
						  void *step_gres_data,
						  int node_offset,
						  bool ignore_alloc );
	uint32_t	(*step_alloc)		( void *job_gres_data,
						  void *step_gres_data,
						  int node_offset,
						  uint32_t cpu_cnt );
	uint32_t	(*step_dealloc)		( void *job_gres_data,
						  void *step_gres_data );
} slurm_gres_ops_t;

typedef struct slurm_gres_context {
	char	       	*gres_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		gres_errno;
	slurm_gres_ops_t ops;
	bool		unpacked_info;
} slurm_gres_context_t;

static int gres_context_cnt = -1;
static bool gres_debug = false;
static slurm_gres_context_t *gres_context = NULL;
static char *gres_plugin_list = NULL;
static pthread_mutex_t gres_context_lock = PTHREAD_MUTEX_INITIALIZER;
static List gres_conf_list = NULL;

typedef struct gres_state {
	uint32_t	plugin_id;
	void		*gres_data;
} gres_state_t;

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
		"plugin_id",
		"gres_name",
		"help_msg",
		"node_config_load",
		"node_config_validate",
		"node_reconfig",
		"node_state_pack",
		"node_state_unpack",
		"node_state_dup",
		"node_state_dealloc",
		"node_state_realloc",
		"node_state_log",
		"job_state_delete",
		"job_state_validate",
		"job_state_dup",
		"job_state_pack",
		"job_state_unpack",
		"job_state_log",
		"job_test",
		"job_alloc",
		"job_dealloc",
		"step_state_delete",
		"step_state_validate",
		"step_state_dup",
		"step_state_pack",
		"step_state_unpack",
		"step_state_log",
		"step_test",
		"step_alloc",
		"step_dealloc"
	};
	int n_syms = sizeof(syms) / sizeof(char *);

	/* Find the correct plugin */
	plugin_context->gres_type	= xstrdup("gres/");
	xstrcat(plugin_context->gres_type, plugin_name);
	plugin_context->plugin_list	= NULL;
	plugin_context->cur_plugin	= PLUGIN_INVALID_HANDLE;
	plugin_context->gres_errno 	= SLURM_SUCCESS;

	plugin_context->cur_plugin = plugin_load_and_link(
					plugin_context->gres_type,
					n_syms, syms,
					(void **) &plugin_context->ops);
	if (plugin_context->cur_plugin != PLUGIN_INVALID_HANDLE)
		return SLURM_SUCCESS;

	error("gres: Couldn't find the specified plugin name for %s "
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
		error("gres: cannot find scheduler plugin for %s",
		       plugin_context->gres_type);
		return SLURM_ERROR;
	}

	/* Dereference the API. */
	if (plugin_get_syms(plugin_context->cur_plugin,
			    n_syms, syms,
			    (void **) &plugin_context->ops ) < n_syms ) {
		error("gres: incomplete %s plugin detected",
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
			rc = _load_gres_plugin(one_name,
					       gres_context + gres_context_cnt);
			if (rc != SLURM_SUCCESS)
				break;
			gres_context_cnt++;
		}
		one_name = strtok_r(NULL, ",", &last);
	}
	xfree(names);

	/* Insure that plugin_id is valid and unique */
	for (i=0; i<gres_context_cnt; i++) {
		for (j=i+1; j<gres_context_cnt; j++) {
			if (*(gres_context[i].ops.plugin_id) !=
			    *(gres_context[j].ops.plugin_id))
				continue;
			fatal("GresPlugins: Duplicate plugin_id %u for %s and %s",
			      *(gres_context[i].ops.plugin_id),
			      gres_context[i].gres_type,
			      gres_context[j].gres_type);
		}
		if (*(gres_context[i].ops.plugin_id) < 100) {
			fatal("GresPlugins: Invalid plugin_id %u (<100) %s",
			      *(gres_context[i].ops.plugin_id),
			      gres_context[i].gres_type);
		}

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
	char *tmp_msg;

	if (msg_size < 1)
		return EINVAL;

	msg[0] = '\0';
	tmp_msg = xmalloc(msg_size);
	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i=0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		tmp_msg = (gres_context[i].ops.help_msg);
		if ((tmp_msg == NULL) || (tmp_msg[0] == '\0'))
			continue;
		if ((strlen(msg) + strlen(tmp_msg) + 2) > msg_size)
			break;
		if (msg[0])
			strcat(msg, "\n");
		strcat(msg, tmp_msg);
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
	xfree(p->file);
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
	info("Gres Name:%s File:%s CPUs:%s Count:%u",
	     p->name, p->file, p->cpus, p->count);
	return 0;
}

/*
 * Build gres_slurmd_conf_t record based upon a line from the gres.conf file
 */
static int _parse_gres_config(void **dest, slurm_parser_enum_t type,
			      const char *key, const char *value,
			      const char *line, char **leftover)
{
	static s_p_options_t _gres_options[] = {
		{"Count", S_P_UINT32},	/* Number of Gres available */
		{"CPUs", S_P_STRING},	/* CPUs to bind to Gres resource */
		{"File", S_P_STRING},	/* Path to Gres device */
		{NULL}
	};
	int i;
	s_p_hashtbl_t *tbl;
	gres_slurmd_conf_t *p;

	tbl = s_p_hashtbl_create(_gres_options);
	s_p_parse_line(tbl, *leftover, leftover);

	p = xmalloc(sizeof(gres_slurmd_conf_t));
	p->name = xstrdup(value);
	if (!s_p_get_uint32(&p->count, "Count", tbl))
		p->count = 1;
	if (s_p_get_string(&p->cpus, "CPUs", tbl)) {
//FIXME: change to bitmap, size? change from cpuset/numa to slurmctld format
//bit_unfmt(bimap, p->cpus);
	}
	if (s_p_get_string(&p->file, "File", tbl)) {
		struct stat config_stat;
		if (stat(p->file, &config_stat) < 0)
			fatal("can't stat gres.conf file %s: %m", p->file);
	}
	s_p_hashtbl_destroy(tbl);

	for (i=0; i<gres_context_cnt; i++) {
		if (strcasecmp(value, gres_context[i].ops.gres_name) == 0)
			break;
	}
	if (i >= gres_context_cnt) {
		error("Ignoring gres.conf Name=%s", value);
		_destroy_gres_slurmd_conf(p);
		return 0;
	}

	*dest = (void *)p;
	return 1;
}

/*
 * Load this node's gres configuration (i.e. how many resources it has)
 */
extern int gres_plugin_node_config_load(void)
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
			pack32(gres_slurmd_conf->plugin_id, buffer);
			pack32(gres_slurmd_conf->count, buffer);
			packstr(gres_slurmd_conf->cpus, buffer);
		}
		list_iterator_destroy(iter);
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Unpack this node's configuration from a buffer (build/packed by slurmd)
 * IN/OUT buffer - message buffer to unpack
 * IN node_name - name of node whose data is being unpacked
 */
extern int gres_plugin_node_config_unpack(Buf buffer, char* node_name)
{
	int i, j, rc;
	uint32_t count, magic, plugin_id, utmp32;
	uint16_t rec_cnt, version;
	char *tmp_cpus;
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
 	for (j=0; j<gres_context_cnt; j++)
 		gres_context[j].unpacked_info = false;
	for (i=0; i<rec_cnt; i++) {
		safe_unpack32(&magic, buffer);
		if (magic != GRES_MAGIC)
			goto unpack_error;
		safe_unpack32(&plugin_id, buffer);
		safe_unpack32(&count, buffer);
		safe_unpackstr_xmalloc(&tmp_cpus, &utmp32, buffer);
 		for (j=0; j<gres_context_cnt; j++) {
 			if (*(gres_context[j].ops.plugin_id) == plugin_id) {
				gres_context[j].unpacked_info = true;
 				break;
			}
 		}
		if (j >= gres_context_cnt) {
			/* A likely sign that GresPlugins is inconsistently
			 * configured. Not a fatal error, skip over the data. */
			error("gres_plugin_node_config_unpack: no plugin "
			      "configured to unpack data type %u from node %s",
			      plugin_id, node_name);
			xfree(tmp_cpus);
			continue;
		}
		p = xmalloc(sizeof(gres_slurmd_conf_t));
		p->count = count;
		p->plugin_id = plugin_id;
		p->cpus = tmp_cpus;
		tmp_cpus = NULL;	/* Nothing left to xfree */
		list_append(gres_conf_list, p);
	}
 	for (j=0; j<gres_context_cnt; j++) {
 		if (gres_context[j].unpacked_info)
			continue;

		/* A likely sign GresPlugins is inconsistently configured. */
		error("gres_plugin_node_config_unpack: no data type of type %s "
		      "from node %s", gres_context[j].gres_type, node_name);
	}
	slurm_mutex_unlock(&gres_context_lock);
	return rc;

unpack_error:
	error("gres_plugin_node_config_unpack: unpack error from node %s",
	      node_name);
	xfree(tmp_cpus);
	slurm_mutex_unlock(&gres_context_lock);
	return SLURM_ERROR;
}

/*
 * Delete an element placed on gres_list by node_config_validate()
 * free associated memory
 */
static void _gres_node_list_delete(void *list_element)
{
	gres_state_t *gres_ptr;
	gres_node_state_t *gres_node_ptr;

	gres_ptr = (gres_state_t *) list_element;
	gres_node_ptr = (gres_node_state_t *) gres_ptr->gres_data;
	FREE_NULL_BITMAP(gres_node_ptr->gres_bit_alloc);
	xfree(gres_node_ptr);
	xfree(gres_ptr);
}

/*
 * Build a node's gres record based only upon the slurm.conf contents
 */
extern int _node_config_init(char *node_name, char *orig_config,
			     char *gres_name, void **gres_data)
{
	int rc = SLURM_SUCCESS;
	gres_node_state_t *gres_ptr;
	char *node_gres_config, *tok, *last = NULL;
	char name_colon[128];
	int32_t gres_config_cnt = 0;
	bool updated_config = false;

	xassert(gres_data);
	gres_ptr = (gres_node_state_t *) *gres_data;
	if (gres_ptr == NULL) {
		gres_ptr = xmalloc(sizeof(gres_node_state_t));
		*gres_data = gres_ptr;
		gres_ptr->gres_cnt_config = NO_VAL;
		gres_ptr->gres_cnt_found  = NO_VAL;
		updated_config = true;
	}

	/* If the resource isn't configured for use with this node,
	 * just return leaving gres_cnt_config=0, gres_cnt_avail=0, etc. */
	if ((orig_config == NULL) || (orig_config[0] == '\0') ||
	    (updated_config == false))
		return rc;

	snprintf(name_colon, sizeof(name_colon), "%s:", gres_name);
	node_gres_config = xstrdup(orig_config);
	tok = strtok_r(node_gres_config, ",", &last);
	while (tok) {
		if (!strcmp(tok, gres_name)) {
			gres_config_cnt = 1;
			break;
		}
		if (!strncmp(tok, name_colon, 4)) {
			gres_config_cnt = strtol(tok+4, &last, 10);
			if (last[0] == '\0')
				;
			else if ((last[0] == 'k') || (last[0] == 'K'))
				gres_config_cnt *= 1024;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(node_gres_config);

	gres_ptr->gres_cnt_config = gres_config_cnt;
	gres_ptr->gres_cnt_avail  = gres_config_cnt;
	if (gres_ptr->gres_bit_alloc == NULL) {
		gres_ptr->gres_bit_alloc = bit_alloc(gres_ptr->gres_cnt_avail);
	} else if (gres_ptr->gres_cnt_avail > 
		   bit_size(gres_ptr->gres_bit_alloc)) {
		gres_ptr->gres_bit_alloc = bit_realloc(gres_ptr->gres_bit_alloc,
						       gres_ptr->gres_cnt_avail);
	}
	if (gres_ptr->gres_bit_alloc == NULL)
		fatal("bit_alloc: malloc failure");

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
			if (gres_ptr->plugin_id ==
			    *(gres_context[i].ops.plugin_id))
				break;
		}
		list_iterator_destroy(gres_iter);
		if (gres_ptr == NULL) {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = *(gres_context[i].ops.plugin_id);
			list_append(*gres_list, gres_ptr);
		}

		rc = _node_config_init(node_name, orig_config,
				       gres_context[i].ops.gres_name,
				       &gres_ptr->gres_data);
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

static uint32_t _get_tot_gres_cnt(uint32_t plugin_id)
{
	ListIterator iter;
	gres_slurmd_conf_t *gres_slurmd_conf;
	uint32_t gres_cnt = 0;

	if (gres_conf_list == NULL)
		return gres_cnt;

	iter = list_iterator_create(gres_conf_list);
	if (iter == NULL)
		fatal("list_iterator_create: malloc failure");
	while ((gres_slurmd_conf = (gres_slurmd_conf_t *) list_next(iter))) {
		if (gres_slurmd_conf->plugin_id == plugin_id)
			gres_cnt += gres_slurmd_conf->count;
	}
	list_iterator_destroy(iter);
	return gres_cnt;
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
	uint32_t gres_cnt;

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
			if (gres_ptr->plugin_id ==
			    *(gres_context[i].ops.plugin_id))
				break;
		}
		list_iterator_destroy(gres_iter);
		if (gres_ptr == NULL) {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = *(gres_context[i].ops.plugin_id);
			list_append(*gres_list, gres_ptr);
		}
		gres_cnt = _get_tot_gres_cnt(*gres_context[i].ops.plugin_id);
		rc2 = (*(gres_context[i].ops.node_config_validate))
			(node_name, gres_cnt, orig_config, new_config,
			 &gres_ptr->gres_data, fast_schedule, reason_down);
		rc = MAX(rc, rc2);
	}
	slurm_mutex_unlock(&gres_context_lock);

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
		/* Find gres_state entry on the list */
		gres_iter = list_iterator_create(*gres_list);
		while ((gres_ptr = (gres_state_t *) list_next(gres_iter))){
			if (gres_ptr->plugin_id ==
			    *(gres_context[i].ops.plugin_id))
				break;
		}
		list_iterator_destroy(gres_iter);
		if (gres_ptr == NULL)
			continue;

		rc = (*(gres_context[i].ops.node_reconfig))
			(node_name, orig_config, new_config,
			 &gres_ptr->gres_data, fast_schedule);
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
	int i, rc = SLURM_SUCCESS, rc2;
	uint32_t top_offset, gres_size = 0;
	uint32_t header_offset, size_offset, data_offset, tail_offset;
	uint32_t magic = GRES_MAGIC;
	uint16_t rec_cnt = 0;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

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
		for (i=0; i<gres_context_cnt; i++) {
			if (gres_ptr->plugin_id !=
			    *(gres_context[i].ops.plugin_id))
				continue;
			header_offset = get_buf_offset(buffer);
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			size_offset = get_buf_offset(buffer);
			pack32(gres_size, buffer);	/* placeholder */
			data_offset = get_buf_offset(buffer);
			rc2 = (*(gres_context[i].ops.node_state_pack))
					(gres_ptr->gres_data, buffer);
			if (rc2 != SLURM_SUCCESS) {
				rc = rc2;
				set_buf_offset(buffer, header_offset);
				break;
			}
			tail_offset = get_buf_offset(buffer);
			set_buf_offset(buffer, size_offset);
			gres_size = tail_offset - data_offset;
			pack32(gres_size, buffer);
			set_buf_offset(buffer, tail_offset);
			rec_cnt++;
			break;
		}
		if (i >= gres_context_cnt) {
			error("Could not find plugin id %u to pack record for "
			      "node %s",
			      gres_ptr->plugin_id, node_name);
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
 * Unpack a node's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_node_state_pack()
 * IN/OUT buffer - location to read state from
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_plugin_node_state_unpack(List *gres_list, Buf buffer,
					 char *node_name)
{
	int i, rc, rc2;
	uint32_t gres_size, magic, tail_offset, plugin_id;
	uint16_t rec_cnt;
	gres_state_t *gres_ptr;
	void *gres_data;

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

	for (i=0; i<gres_context_cnt; i++)
		gres_context[i].unpacked_info = false;

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;
		safe_unpack32(&magic, buffer);
		if (magic != GRES_MAGIC)
			goto unpack_error;
		safe_unpack32(&plugin_id, buffer);
		safe_unpack32(&gres_size, buffer);
		for (i=0; i<gres_context_cnt; i++) {
			if (*(gres_context[i].ops.plugin_id) == plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("gres_plugin_node_state_unpack: no plugin "
			      "configured to unpack data type %u from node %s",
			      plugin_id, node_name);
			/* A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data. */
			tail_offset = get_buf_offset(buffer);
			tail_offset += gres_size;
			set_buf_offset(buffer, tail_offset);
			continue;
		}
		gres_context[i].unpacked_info = true;
		rc2 = (*(gres_context[i].ops.node_state_unpack))
				(&gres_data, buffer);
		if (rc2 != SLURM_SUCCESS) {
			rc = rc2;
		} else {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = *(gres_context[i].ops.plugin_id);
			gres_ptr->gres_data = gres_data;
			list_append(*gres_list, gres_ptr);
		}
	}

fini:	/* Insure that every gres plugin is called for unpack, even if no data
	 * was packed by the node. A likely sign that GresPlugins is
	 * inconsistently configured. */
	for (i=0; i<gres_context_cnt; i++) {
		if (gres_context[i].unpacked_info)
			continue;
		error("gres_plugin_node_state_unpack: no info packed for %s "
		      "by node %s",
		      gres_context[i].gres_type, node_name);
		rc2 = (*(gres_context[i].ops.node_state_unpack))
				(&gres_data, NULL);
		if (rc2 != SLURM_SUCCESS) {
			rc = rc2;
		} else {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = *(gres_context[i].ops.plugin_id);
			gres_ptr->gres_data = gres_data;
			list_append(*gres_list, gres_ptr);
		}
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;

unpack_error:
	error("gres_plugin_node_state_unpack: unpack error from node %s",
	      node_name);
	rc = SLURM_ERROR;
	goto fini;
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
			if (gres_ptr->plugin_id !=
			    *(gres_context[i].ops.plugin_id))
				continue;
			gres_data = (*(gres_context[i].ops.node_state_dup))
					(gres_ptr->gres_data);
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

/*
 * Deallocate all resources on this node previous allocated to any jobs.
 *	This function isused to synchronize state after slurmctld restarts or
 *	is reconfigured.
 * IN gres_list - node gres state information
 */
extern void gres_plugin_node_state_dealloc(List gres_list)
{
	int i;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	if (gres_list == NULL)
		return;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		for (i=0; i<gres_context_cnt; i++) {
			if (gres_ptr->plugin_id !=
			    *(gres_context[i].ops.plugin_id))
				continue;
			(*(gres_context[i].ops.node_state_dealloc))
					(gres_ptr->gres_data);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Allocate in this nodes record the resources previously allocated to this
 *	job. This function isused to synchronize state after slurmctld restarts
 *	or is reconfigured.
 * IN job_gres_list - job gres state information
 * IN node_offset - zero-origin index of this node in the job's allocation
 * IN node_gres_list - node gres state information
 * RET SLURM_SUCCESS or error code
 */
extern int gres_plugin_node_state_realloc(List job_gres_list, int node_offset,
					  List node_gres_list)
{
	ListIterator job_gres_iter,  node_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;
	int i;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL)
		return SLURM_ERROR;

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
			error("Could not find plugin id %u to realloc job",
			      job_gres_ptr->plugin_id);
			continue;
		}

		for (i=0; i<gres_context_cnt; i++) {
			if (job_gres_ptr->plugin_id !=
			    *(gres_context[i].ops.plugin_id))
				continue;
			(*(gres_context[i].ops.node_state_realloc))
					(job_gres_ptr->gres_data, node_offset,
					 node_gres_ptr->gres_data);
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return SLURM_SUCCESS;
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
			    *(gres_context[i].ops.plugin_id))
				continue;
			(*(gres_context[i].ops.node_state_log))
					(gres_ptr->gres_data, node_name);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

static void _gres_job_list_delete(void *list_element)
{
	int i;
	gres_state_t *gres_ptr;

	if (gres_plugin_init() != SLURM_SUCCESS)
		return;

	gres_ptr = (gres_state_t *) list_element;
	slurm_mutex_lock(&gres_context_lock);
	for (i=0; i<gres_context_cnt; i++) {
		if (gres_ptr->plugin_id != *(gres_context[i].ops.plugin_id))
			continue;
		(*(gres_context[i].ops.job_state_delete))(gres_ptr->gres_data);
		xfree(gres_ptr);
		break;
	}
	slurm_mutex_unlock(&gres_context_lock);
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
			rc2 = (*(gres_context[i].ops.job_state_validate))
					(tok, &gres_data);
			if (rc2 != SLURM_SUCCESS)
				continue;
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = *(gres_context[i].ops.plugin_id);
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

/*
 * Create a copy of a job's gres state
 * IN gres_list - List of Gres records for this job to track usage
 * RET The copy or NULL on failure
 */
List gres_plugin_job_state_dup(List gres_list)
{
	int i;
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
		for (i=0; i<gres_context_cnt; i++) {
			if (gres_ptr->plugin_id !=
			    *(gres_context[i].ops.plugin_id))
				continue;
			new_gres_data = (*(gres_context[i].ops.job_state_dup))
					(gres_ptr->gres_data);
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
			break;
		}
		if (i >= gres_context_cnt) {
			error("Could not find plugin id %u to dup job record",
			      gres_ptr->plugin_id);
		}
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
 */
extern int gres_plugin_job_state_pack(List gres_list, Buf buffer,
				      uint32_t job_id)
{
	int i, rc = SLURM_SUCCESS, rc2;
	uint32_t top_offset, gres_size = 0;
	uint32_t header_offset, size_offset, data_offset, tail_offset;
	uint32_t magic = GRES_MAGIC;
	uint16_t rec_cnt = 0;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	if (gres_list == NULL)
		return rc;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		for (i=0; i<gres_context_cnt; i++) {
			if (gres_ptr->plugin_id !=
			    *(gres_context[i].ops.plugin_id))
				continue;
			header_offset = get_buf_offset(buffer);
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			size_offset = get_buf_offset(buffer);
			pack32(gres_size, buffer);	/* placeholder */
			data_offset = get_buf_offset(buffer);
			rc2 = (*(gres_context[i].ops.job_state_pack))
					(gres_ptr->gres_data, buffer);
			if (rc2 != SLURM_SUCCESS) {
				rc = rc2;
				set_buf_offset(buffer, header_offset);
				continue;
			}
			tail_offset = get_buf_offset(buffer);
			set_buf_offset(buffer, size_offset);
			gres_size = tail_offset - data_offset;
			pack32(gres_size, buffer);
			set_buf_offset(buffer, tail_offset);
			rec_cnt++;
			break;
		}
		if (i >= gres_context_cnt) {
			error("Could not find plugin id %u to pack record for "
			      "job %u",
			      gres_ptr->plugin_id, job_id);
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
					uint32_t job_id)
{
	int i, rc, rc2;
	uint32_t gres_size, magic, tail_offset, plugin_id;
	uint16_t rec_cnt;
	gres_state_t *gres_ptr;
	void *gres_data;

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

	for (i=0; i<gres_context_cnt; i++)
		gres_context[i].unpacked_info = false;

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;
		safe_unpack32(&magic, buffer);
		if (magic != GRES_MAGIC)
			goto unpack_error;
		safe_unpack32(&plugin_id, buffer);
		safe_unpack32(&gres_size, buffer);
		for (i=0; i<gres_context_cnt; i++) {
			if (*(gres_context[i].ops.plugin_id) == plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("gres_plugin_job_state_unpack: no plugin "
			      "configured to unpack data type %u from job %u",
			      plugin_id, job_id);
			/* A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data. */
			tail_offset = get_buf_offset(buffer);
			tail_offset += gres_size;
			set_buf_offset(buffer, tail_offset);
			continue;
		}
		gres_context[i].unpacked_info = true;
		rc2 = (*(gres_context[i].ops.job_state_unpack))
				(&gres_data, buffer);
		if (rc2 != SLURM_SUCCESS) {
			rc = rc2;
		} else {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = *(gres_context[i].ops.plugin_id);
			gres_ptr->gres_data = gres_data;
			list_append(*gres_list, gres_ptr);
		}
	}

fini:	/* Insure that every gres plugin is called for unpack, even if no data
	 * was packed by the job. A likely sign that GresPlugins is
	 * inconsistently configured. */
	for (i=0; i<gres_context_cnt; i++) {
		if (gres_context[i].unpacked_info)
			continue;
		debug("gres_plugin_job_state_unpack: no info packed for %s "
		      "by job %u",
		      gres_context[i].gres_type, job_id);
		rc2 = (*(gres_context[i].ops.job_state_unpack))
				(&gres_data, NULL);
		if (rc2 != SLURM_SUCCESS) {
			rc = rc2;
		} else {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = *(gres_context[i].ops.plugin_id);
			gres_ptr->gres_data = gres_data;
			list_append(*gres_list, gres_ptr);
		}
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;

unpack_error:
	error("gres_plugin_job_state_unpack: unpack error from job %u",
	      job_id);
	rc = SLURM_ERROR;
	goto fini;
}

/*
 * Determine how many CPUs on the node can be used by this job
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *                     gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * RET: NO_VAL    - All CPUs on node are available
 *      otherwise - Specific CPU count
 */
extern uint32_t gres_plugin_job_test(List job_gres_list, List node_gres_list,
				     bool use_total_gres)
{
	int i;
	uint32_t cpu_cnt, tmp_cnt;
	ListIterator job_gres_iter,  node_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

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
			    *(gres_context[i].ops.plugin_id))
				continue;
			tmp_cnt = (*(gres_context[i].ops.job_test))
					(job_gres_ptr->gres_data,
					 node_gres_ptr->gres_data,
					 use_total_gres);
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
			    *(gres_context[i].ops.plugin_id))
				continue;
			rc2 = (*(gres_context[i].ops.job_alloc))
					(job_gres_ptr->gres_data, 
					 node_gres_ptr->gres_data, node_cnt,
					 node_offset, cpu_cnt);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
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
			    *(gres_context[i].ops.plugin_id))
				continue;
			rc2 = (*(gres_context[i].ops.job_dealloc))
					(job_gres_ptr->gres_data, 
					 node_gres_ptr->gres_data, node_offset);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
			break;
		}
	}
	list_iterator_destroy(job_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
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
			    *(gres_context[i].ops.plugin_id))
				continue;
			(*(gres_context[i].ops.job_state_log))
					(gres_ptr->gres_data, job_id);
			break;
		}
	}
	list_iterator_destroy(gres_iter);
	slurm_mutex_unlock(&gres_context_lock);
}

static void _gres_step_list_delete(void *list_element)
{
	int i;
	gres_state_t *gres_ptr;

	if (gres_plugin_init() != SLURM_SUCCESS)
		return;

	gres_ptr = (gres_state_t *) list_element;
	slurm_mutex_lock(&gres_context_lock);
	for (i=0; i<gres_context_cnt; i++) {
		if (gres_ptr->plugin_id != *(gres_context[i].ops.plugin_id))
			continue;
		(*(gres_context[i].ops.step_state_delete))(gres_ptr->gres_data);
		xfree(gres_ptr);
		break;
	}
	slurm_mutex_unlock(&gres_context_lock);
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
		return SLURM_ERROR;
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
			rc2 = (*(gres_context[i].ops.step_state_validate))
					(tok, &step_gres_data);
			if (rc2 != SLURM_SUCCESS)
				continue;
			/* Now make sure the step's request isn't too big for
			 * the job's gres allocation */
			job_gres_iter = list_iterator_create(job_gres_list);
			while ((job_gres_ptr = (gres_state_t *)
					list_next(job_gres_iter))) {
				if (job_gres_ptr->plugin_id ==
				    *(gres_context[i].ops.plugin_id))
					break;
			}
			list_iterator_destroy(job_gres_iter);
			if (job_gres_ptr == NULL) {
				info("Step gres request not in job alloc %s",
				     tok);
				rc = ESLURM_INVALID_GRES;
				break;
			}
			job_gres_data = job_gres_ptr->gres_data;
			rc3 = (*(gres_context[i].ops.step_test))
					(step_gres_data, job_gres_data, NO_VAL,
					 true);
			if (rc3 == 0) {
				info("Step gres more than in job allocation %s",
				     tok);
				rc = ESLURM_INVALID_GRES;
				break;
			}

			step_gres_ptr = xmalloc(sizeof(gres_state_t));
			step_gres_ptr->plugin_id = *(gres_context[i].ops.
						     plugin_id);
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

/*
 * Create a copy of a step's gres state
 * IN gres_list - List of Gres records for this step to track usage
 * RET The copy or NULL on failure
 */
List gres_plugin_step_state_dup(List gres_list)
{
	int i;
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
		for (i=0; i<gres_context_cnt; i++) {
			if (gres_ptr->plugin_id !=
			    *(gres_context[i].ops.plugin_id))
				continue;
			new_gres_data = (*(gres_context[i].ops.step_state_dup))
					(gres_ptr->gres_data);
			if (new_gres_data == NULL)
				break;
			if (new_gres_list == NULL) {
				new_gres_list = list_create(_gres_step_list_delete);
				if (new_gres_list == NULL)
					fatal("list_create: malloc failure");
			}
			new_gres_state = xmalloc(sizeof(gres_state_t));
			new_gres_state->plugin_id = gres_ptr->plugin_id;
			new_gres_state->gres_data = new_gres_data;
			list_append(new_gres_list, new_gres_state);
			break;
		}
		if (i >= gres_context_cnt) {
			error("Could not find plugin id %u to dup step record",
			      gres_ptr->plugin_id);
		}
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
	int i, rc = SLURM_SUCCESS, rc2;
	uint32_t top_offset, gres_size = 0;
	uint32_t header_offset, size_offset, data_offset, tail_offset;
	uint32_t magic = GRES_MAGIC;
	uint16_t rec_cnt = 0;
	ListIterator gres_iter;
	gres_state_t *gres_ptr;

	top_offset = get_buf_offset(buffer);
	pack16(rec_cnt, buffer);	/* placeholder if data */

	if (gres_list == NULL)
		return rc;

	(void) gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	gres_iter = list_iterator_create(gres_list);
	while ((gres_ptr = (gres_state_t *) list_next(gres_iter))) {
		for (i=0; i<gres_context_cnt; i++) {
			if (gres_ptr->plugin_id !=
			    *(gres_context[i].ops.plugin_id))
				continue;
			header_offset = get_buf_offset(buffer);
			pack32(magic, buffer);
			pack32(gres_ptr->plugin_id, buffer);
			size_offset = get_buf_offset(buffer);
			pack32(gres_size, buffer);	/* placeholder */
			data_offset = get_buf_offset(buffer);
			rc2 = (*(gres_context[i].ops.step_state_pack))
					(gres_ptr->gres_data, buffer);
			if (rc2 != SLURM_SUCCESS) {
				rc = rc2;
				set_buf_offset(buffer, header_offset);
				continue;
			}
			tail_offset = get_buf_offset(buffer);
			set_buf_offset(buffer, size_offset);
			gres_size = tail_offset - data_offset;
			pack32(gres_size, buffer);
			set_buf_offset(buffer, tail_offset);
			rec_cnt++;
			break;
		}
		if (i >= gres_context_cnt) {
			error("Could not find plugin id %u to pack record for "
			      "step %u.%u",
			      gres_ptr->plugin_id, job_id, step_id);
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
					 uint32_t job_id, uint32_t step_id)
{
	int i, rc, rc2;
	uint32_t gres_size, magic, tail_offset, plugin_id;
	uint16_t rec_cnt;
	gres_state_t *gres_ptr;
	void *gres_data;

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

	for (i=0; i<gres_context_cnt; i++)
		gres_context[i].unpacked_info = false;

	while ((rc == SLURM_SUCCESS) && (rec_cnt)) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		rec_cnt--;
		safe_unpack32(&magic, buffer);
		if (magic != GRES_MAGIC)
			goto unpack_error;
		safe_unpack32(&plugin_id, buffer);
		safe_unpack32(&gres_size, buffer);
		for (i=0; i<gres_context_cnt; i++) {
			if (*(gres_context[i].ops.plugin_id) == plugin_id)
				break;
		}
		if (i >= gres_context_cnt) {
			error("gres_plugin_step_state_unpack: no plugin "
			      "configured to unpack data type %u from "
			      "step %u.%u",
			      plugin_id, job_id, step_id);
			/* A likely sign that GresPlugins has changed.
			 * Not a fatal error, skip over the data. */
			tail_offset = get_buf_offset(buffer);
			tail_offset += gres_size;
			set_buf_offset(buffer, tail_offset);
			continue;
		}
		gres_context[i].unpacked_info = true;
		rc2 = (*(gres_context[i].ops.step_state_unpack))
				(&gres_data, buffer);
		if (rc2 != SLURM_SUCCESS) {
			rc = rc2;
		} else {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = *(gres_context[i].ops.plugin_id);
			gres_ptr->gres_data = gres_data;
			list_append(*gres_list, gres_ptr);
		}
	}

fini:	/* Insure that every gres plugin is called for unpack, even if no data
	 * was packed by the job. A likely sign that GresPlugins is
	 * inconsistently configured. */
	for (i=0; i<gres_context_cnt; i++) {
		if (gres_context[i].unpacked_info)
			continue;
		debug("gres_plugin_job_state_unpack: no info packed for %s "
		      "by step %u.%u",
		      gres_context[i].gres_type, job_id, step_id);
		rc2 = (*(gres_context[i].ops.job_state_unpack))
				(&gres_data, NULL);
		if (rc2 != SLURM_SUCCESS) {
			rc = rc2;
		} else {
			gres_ptr = xmalloc(sizeof(gres_state_t));
			gres_ptr->plugin_id = *(gres_context[i].ops.plugin_id);
			gres_ptr->gres_data = gres_data;
			list_append(*gres_list, gres_ptr);
		}
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;

unpack_error:
	error("gres_plugin_job_state_unpack: unpack error from step %u.%u",
	      job_id, step_id);
	rc = SLURM_ERROR;
	goto fini;
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
			if (gres_ptr->plugin_id !=
			    *(gres_context[i].ops.plugin_id))
				continue;
			(*(gres_context[i].ops.step_state_log))
					(gres_ptr->gres_data, job_id, step_id);
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
			    *(gres_context[i].ops.plugin_id))
				continue;
			tmp_cnt = (*(gres_context[i].ops.step_test))
					(step_gres_ptr->gres_data,
					 job_gres_ptr->gres_data,
					 node_offset, ignore_alloc);
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
			    *(gres_context[i].ops.plugin_id))
				continue;
			rc2 = (*(gres_context[i].ops.step_alloc))
					(step_gres_ptr->gres_data, 
					 job_gres_ptr->gres_data,
					 node_offset, cpu_cnt);
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
			    *(gres_context[i].ops.plugin_id))
				continue;
			rc2 = (*(gres_context[i].ops.step_dealloc))
					(step_gres_ptr->gres_data, 
					 job_gres_ptr->gres_data);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
			break;
		}
	}
	list_iterator_destroy(step_gres_iter);
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}
