/*****************************************************************************\
 *  configure_api.c
 *
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#include "configure_api.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"

typedef struct {
	void (*ba_init)                (node_info_msg_t *node_info_ptr,
					bool load_bridge);
	void (*ba_fini)                (void);
	void (*reset_ba_system)        (bool track_down_mps);
	void (*destroy_ba_mp)          (void *ptr);
	char *(*ba_passthroughs_string)(uint16_t passthrough);
	void (*ba_update_mp_state)     (ba_mp_t *ba_mp, uint16_t state);
	int (*ba_set_removable_mps)    (bitstr_t *bitmap, bool except);
	int (*ba_reset_all_removed_mps)(void);
	int (*new_ba_request)          (select_ba_request_t* ba_request);
	int (*allocate_block)          (select_ba_request_t* ba_request,
				        List results);
	int (*remove_block)            (List mps, bool is_small);
	ba_mp_t *(*str2ba_mp)          (const char *coords);
	ba_mp_t *(*loc2ba_mp)          (const char *mp_id);
	ba_mp_t *(*coord2ba_mp)        (const uint16_t *coord);
	char *(*give_geo)              (uint16_t *int_geo, int dims,
					bool with_sep);
	s_p_hashtbl_t *(*config_make_tbl)(char *filename);
	void (*set_ba_debug_flags)     (uint32_t debug_flags);
} bg_configure_api_ops_t;

typedef struct bg_configure_context {
	char	       	*type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		bg_configure_errno;
	bg_configure_api_ops_t ops;
} bg_configure_context_t;

static bg_configure_context_t *bg_configure_context = NULL;
static pthread_mutex_t	       bg_configure_context_lock =
	PTHREAD_MUTEX_INITIALIZER;

static bg_configure_api_ops_t *_get_ops(bg_configure_context_t *c)
{
	/*
	 * Must be synchronized with bg_configure_api_ops_t above.
	 */
	static const char *syms[] = {
		"ba_init",
		"ba_fini",
		"reset_ba_system",
		"destroy_ba_mp",
		"ba_passthroughs_string",
		"ba_update_mp_state",
		"ba_set_removable_mps",
		"ba_reset_all_removed_mps",
		"new_ba_request",
		"allocate_block",
		"remove_block",
		"str2ba_mp",
		"loc2ba_mp",
		"coord2ba_mp",
		"give_geo",
		"config_make_tbl",
		"set_ba_debug_flags",
	};
	int n_syms = sizeof(syms) / sizeof(char *);

	/* Find the correct plugin. */
	c->cur_plugin = plugin_load_and_link(c->type, n_syms, syms,
					     (void **) &c->ops);
	if (c->cur_plugin != PLUGIN_INVALID_HANDLE)
		return &c->ops;

	if(errno != EPLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      c->type, plugin_strerror(errno));
		return NULL;
	}

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->type);

	/* Get plugin list. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "cannot create plugin manager" );
			return NULL;
		}
		plugrack_set_major_type(c->plugin_list, "select");
		plugrack_set_paranoia(c->plugin_list,
				      PLUGRACK_PARANOIA_NONE,
				      0);
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir(c->plugin_list, plugin_dir);
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type(c->plugin_list, c->type);
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find accounting_storage plugin for %s",
		       c->type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms(c->cur_plugin,
			     n_syms,
			     syms,
			     (void **) &c->ops ) < n_syms) {
		error("incomplete select plugin detected");
		return NULL;
	}

	return &c->ops;
}

/*
 * Destroy a node selection context
 */
static int _context_destroy(bg_configure_context_t *c)
{
	int rc = SLURM_SUCCESS;
	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if (c->plugin_list) {
		if (plugrack_destroy(c->plugin_list) != SLURM_SUCCESS)
			rc = SLURM_ERROR;
	} else {
		plugin_unload(c->cur_plugin);
	}

	xfree(c->type);

	return rc;
}


extern int bg_configure_init(void)
{
	int rc = SLURM_SUCCESS;
	slurm_mutex_lock(&bg_configure_context_lock);

	if (bg_configure_context)
		goto done;

	bg_configure_context = xmalloc(sizeof(bg_configure_context_t));
	bg_configure_context->type = xstrdup("select/bluegene");
	bg_configure_context->cur_plugin = PLUGIN_INVALID_HANDLE;
	bg_configure_context->bg_configure_errno = SLURM_SUCCESS;

	if (!_get_ops(bg_configure_context)) {
		error("cannot resolve select plugin operations for configure");
		_context_destroy(bg_configure_context);
		bg_configure_context = NULL;
		rc = SLURM_ERROR;
	}

done:
	slurm_mutex_unlock(&bg_configure_context_lock);
	return rc;

}

extern int bg_configure_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&bg_configure_context_lock);
	if (!bg_configure_context)
		goto fini;

	rc = _context_destroy(bg_configure_context);
	bg_configure_context = NULL;
fini:
	slurm_mutex_unlock(&bg_configure_context_lock);
	return rc;
}

extern void bg_configure_ba_init(
	node_info_msg_t *node_info_ptr, bool load_bridge)
{
	if (bg_configure_init() < 0)
		return;

	(*(bg_configure_context->ops.ba_init))(node_info_ptr, load_bridge);
}

extern void bg_configure_ba_fini(void)
{
	if (bg_configure_init() < 0)
		return;

	(*(bg_configure_context->ops.ba_fini))();
}

extern void bg_configure_reset_ba_system(bool track_down_mps)
{
	if (bg_configure_init() < 0)
		return;

	(*(bg_configure_context->ops.reset_ba_system))(track_down_mps);
}

extern void bg_configure_destroy_ba_mp(void *ptr)
{
	if (bg_configure_init() < 0)
		return;

	(*(bg_configure_context->ops.destroy_ba_mp))(ptr);
}

extern char *bg_configure_ba_passthroughs_string(uint16_t passthrough)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(bg_configure_context->ops.ba_passthroughs_string))
		(passthrough);
}

extern void bg_configure_ba_update_mp_state(ba_mp_t *ba_mp, uint16_t state)
{
	if (bg_configure_init() < 0)
		return;

	(*(bg_configure_context->ops.ba_update_mp_state))(ba_mp, state);
}

extern int bg_configure_ba_set_removable_mps(bitstr_t *bitmap, bool except)
{
	if (bg_configure_init() < 0)
		return SLURM_ERROR;

	return (*(bg_configure_context->ops.ba_set_removable_mps))
		(bitmap, except);
}

extern int bg_configure_ba_reset_all_removed_mps(void)
{
	if (bg_configure_init() < 0)
		return SLURM_ERROR;

	return (*(bg_configure_context->ops.ba_reset_all_removed_mps))();
}


extern int bg_configure_new_ba_request(select_ba_request_t* ba_request)
{
	if (bg_configure_init() < 0)
		return SLURM_ERROR;

	return (*(bg_configure_context->ops.new_ba_request))(ba_request);
}

extern int bg_configure_allocate_block(
	select_ba_request_t* ba_request, List results)
{
	if (bg_configure_init() < 0)
		return SLURM_ERROR;

	return (*(bg_configure_context->ops.allocate_block))
		(ba_request, results);
}

extern int bg_configure_remove_block(List mps, bool is_small)
{
	if (bg_configure_init() < 0)
		return SLURM_ERROR;

	return (*(bg_configure_context->ops.remove_block))(mps, is_small);
}

extern ba_mp_t *bg_configure_str2ba_mp(const char *coords)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(bg_configure_context->ops.str2ba_mp))(coords);
}

extern ba_mp_t *bg_configure_loc2ba_mp(const char *mp_id)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(bg_configure_context->ops.loc2ba_mp))(mp_id);
}

extern ba_mp_t *bg_configure_coord2ba_mp(const uint16_t *coord)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(bg_configure_context->ops.coord2ba_mp))(coord);
}

extern char *bg_configure_give_geo(uint16_t *int_geo, int dims, bool with_sep)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(bg_configure_context->ops.give_geo))(int_geo, dims, with_sep);
}

extern s_p_hashtbl_t *bg_configure_config_make_tbl(char *filename)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(bg_configure_context->ops.config_make_tbl))(filename);
}

extern void ba_configure_set_ba_debug_flags(uint32_t debug_flags)
{
	if (bg_configure_init() < 0)
		return;

	(*(bg_configure_context->ops.set_ba_debug_flags))(debug_flags);
}
