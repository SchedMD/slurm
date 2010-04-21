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
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define GRES_MAGIC 0x438a34d4

typedef struct slurm_gres_ops {
	uint32_t	(*plugin_id);
	int		(*help_msg)		( char *msg, int msg_size );
	int		(*load_node_config)	( void );
	int		(*pack_node_config)	( Buf buffer );
	int		(*unpack_node_config)	( Buf buffer );
	int		(*node_config_validate)	( char *node_name,
						  char **configured_gres,
						  List *gres_list,
						  uint16_t fast_schedule,
						  char **reason_down );
	int		(*node_config_delete)	( void *list_element );
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
static slurm_gres_context_t *gres_context = NULL;
static char *gres_plugin_list = NULL;
static pthread_mutex_t gres_context_lock = PTHREAD_MUTEX_INITIALIZER;

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
		"help_msg",
		"load_node_config",
		"pack_node_config",
		"unpack_node_config",
		"node_config_validate",
		"node_config_delete"
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
	int i, rc = SLURM_SUCCESS;
	char *last = NULL, *names, *one_name, *full_name;

	slurm_mutex_lock(&gres_context_lock);
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
 * Provide a plugin-specific help message
 * IN/OUT msg - buffer provided by caller and filled in by plugin
 * IN msg_size - size of msg in bytes
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
		rc = (*(gres_context[i].ops.help_msg))(tmp_msg, msg_size);
		if ((rc != SLURM_SUCCESS) || (tmp_msg[0] == '\0'))
			continue;
		if ((strlen(msg) + strlen(tmp_msg) + 2) > msg_size)
			break;
		if (msg[0])
			strcat(msg, "\n");
		strcat(msg, tmp_msg);
		tmp_msg[0] = '\0';
	}
	slurm_mutex_unlock(&gres_context_lock);

	xfree(tmp_msg);
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
	if (_strcmp(plugin_names, gres_plugin_list))
		plugin_change = true;
	else
		plugin_change = false;
	slurm_mutex_unlock(&gres_context_lock);

	if (plugin_change) {
		info("GresPlugins changed from %s to %s",
		     gres_plugin_list, plugin_names);
		*did_change = true;
		rc = gres_plugin_fini();
		if (rc == SLURM_SUCCESS)
			rc = gres_plugin_init();
	}
	xfree(plugin_names);

	return rc;
}

/*
 * Load this node's gres configuration (i.e. how many resources it has)
 */
extern int gres_plugin_load_node_config(void)
{
	int i, rc;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i=0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		rc = (*(gres_context[i].ops.load_node_config))();
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Pack this node's gres configuration into a buffer
 *
 * Data format:
 *	uint32_t	magic cookie
 *	char *		gres name
 *	uint32_t	gres data block size
 *	void *		gres data
 */
extern int gres_plugin_pack_node_config(Buf buffer)
{
	int i, rc;
	uint32_t gres_size = 0, magic = GRES_MAGIC;
	uint32_t header_offset, data_offset, tail_offset;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i=0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		pack32(magic, buffer);
		packstr(gres_context[i].gres_type, buffer);
		header_offset = get_buf_offset(buffer);
		pack32(gres_size, buffer);	/* Placeholder for now */
		data_offset = get_buf_offset(buffer);
		rc = (*(gres_context[i].ops.pack_node_config))(buffer);
		tail_offset = get_buf_offset(buffer);
		gres_size = tail_offset - data_offset;
		set_buf_offset(buffer, header_offset);
		pack32(gres_size, buffer);	/* The actual buffer size */
		set_buf_offset(buffer, tail_offset);
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}

/*
 * Unpack this node's configuration from a buffer
 * IN buffer - message buffer to unpack
 * IN node_name - name of node whose data is being unpacked
 */
extern int gres_plugin_unpack_node_config(Buf buffer, char* node_name)
{
	int i, rc, rc2;
	uint32_t gres_size, magic, tail_offset, uint32_tmp;
	char *gres_type = NULL;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	for (i=0; i<gres_context_cnt; i++)
		gres_context[i].unpacked_info = false;

	while (rc == SLURM_SUCCESS) {
		if ((buffer == NULL) || (remaining_buf(buffer) == 0))
			break;
		safe_unpack32(&magic, buffer);
		if (magic != GRES_MAGIC) 
			goto unpack_error;
		safe_unpackstr_xmalloc(&gres_type, &uint32_tmp, buffer);
		if (gres_type == NULL)
			goto unpack_error;
		safe_unpack32(&gres_size, buffer);
		for (i=0; i<gres_context_cnt; i++) {
			if (!strcmp(gres_type, gres_context[i].gres_type))
				break;
		}
		if (i >= gres_context_cnt) {
			error("gres_plugin_unpack_node_config: no plugin "
			      "configured to unpack %s data from node %s", 
			      gres_type, node_name);
			/* A likely sign that GresPlugins is inconsistently
			 * configured. Not a fatal error, skip over the data. */
			tail_offset = get_buf_offset(buffer);
			tail_offset += gres_size;
			set_buf_offset(buffer, tail_offset);
		} else {
			gres_context[i].unpacked_info = true;
			rc = (*(gres_context[i].ops.unpack_node_config))(buffer);
		}
		xfree(gres_type);
	}

fini:	/* Insure that every gres plugin is called for unpack, even if no data
	 * was packed by the node. A likely sign that GresPlugins is 
	 * inconsistently configured. */
	for (i=0; i<gres_context_cnt; i++) {
		if (gres_context[i].unpacked_info)
			continue;
		error("gres_plugin_unpack_node_config: no info packed for %s "
		      "by node %s",
		      gres_context[i].gres_type, node_name);
		rc2 = (*(gres_context[i].ops.unpack_node_config))(NULL);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;

unpack_error:
	xfree(gres_type);
	error("gres_plugin_unpack_node_config: unpack error from node %s",
	      node_name);
	rc = SLURM_ERROR;
	goto fini;
}

static void _gres_list_delete(void *list_element)
{
	int i, rc;

	if (gres_plugin_init() != SLURM_SUCCESS)
		return;

	slurm_mutex_lock(&gres_context_lock);
	for (i=0; i<gres_context_cnt; i++) {
		rc = (*(gres_context[i].ops.node_config_delete))(list_element);
		if (rc == SLURM_SUCCESS)
			break;
	}
	slurm_mutex_unlock(&gres_context_lock);
}

/*
 * Validate a node's configuration and put a gres record onto a list
 * Called immediately after gres_plugin_unpack_node_config().
 * IN node_name - name of the node for which the gres information applies
 * IN/OUT configured_res - Gres information suppled from slurm.conf,
 *		may be updated with actual configuration if FastSchedule=0
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int gres_plugin_node_config_validate(char *node_name,
					    char **configured_gres,
					    List *gres_list,
					    uint16_t fast_schedule,
					    char **reason_down)
{
	int i, rc;

	rc = gres_plugin_init();

	slurm_mutex_lock(&gres_context_lock);
	if ((gres_context_cnt > 0) && (*gres_list == NULL)) {
		*gres_list = list_create(_gres_list_delete);
		if (*gres_list == NULL)
			fatal("list_create malloc failure");
	}
	for (i=0; ((i < gres_context_cnt) && (rc == SLURM_SUCCESS)); i++) {
		rc = (*(gres_context[i].ops.node_config_validate))
			(node_name, configured_gres, gres_list, fast_schedule,
			 reason_down);
	}
	slurm_mutex_unlock(&gres_context_lock);

	return rc;
}
