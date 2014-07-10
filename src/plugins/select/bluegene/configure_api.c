/*****************************************************************************\
 *  configure_api.c
 *
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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

#include "configure_api.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"

typedef struct {
	void (*ba_init)                (node_info_msg_t *node_info_ptr,
					bool load_bridge);
	void (*ba_fini)                (void);
	void (*ba_setup_wires)         (void);
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
	void (*set_ba_debug_flags)     (uint64_t debug_flags);
} bg_configure_api_ops_t;

/*
 * Must be synchronized with bg_configure_api_ops_t above.
 */
static const char *syms[] = {
	"ba_init",
	"ba_fini",
	"ba_setup_wires",
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

static bg_configure_api_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

extern int bg_configure_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = "select", *type="select/bluegene";
	if (init_run && g_context)
		return rc;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		rc = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock(&g_context_lock);
	return rc;

}

extern int bg_configure_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	if (!g_context)
		goto fini;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
fini:
	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

extern void bg_configure_ba_init(
	node_info_msg_t *node_info_ptr, bool load_bridge)
{
	if (bg_configure_init() < 0)
		return;

	(*(ops.ba_init))(node_info_ptr, load_bridge);
}

extern void bg_configure_ba_fini(void)
{
	if (bg_configure_init() < 0)
		return;

	(*(ops.ba_fini))();
}

extern void bg_configure_ba_setup_wires(void)
{
	if (bg_configure_init() < 0)
		return;

	(*(ops.ba_setup_wires))();
}

extern void bg_configure_reset_ba_system(bool track_down_mps)
{
	if (bg_configure_init() < 0)
		return;

	(*(ops.reset_ba_system))(track_down_mps);
}

extern void bg_configure_destroy_ba_mp(void *ptr)
{
	if (bg_configure_init() < 0)
		return;

	(*(ops.destroy_ba_mp))(ptr);
}

extern char *bg_configure_ba_passthroughs_string(uint16_t passthrough)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(ops.ba_passthroughs_string))
		(passthrough);
}

extern void bg_configure_ba_update_mp_state(ba_mp_t *ba_mp, uint16_t state)
{
	if (bg_configure_init() < 0)
		return;

	(*(ops.ba_update_mp_state))(ba_mp, state);
}

extern int bg_configure_ba_set_removable_mps(bitstr_t *bitmap, bool except)
{
	if (bg_configure_init() < 0)
		return SLURM_ERROR;

	return (*(ops.ba_set_removable_mps))
		(bitmap, except);
}

extern int bg_configure_ba_reset_all_removed_mps(void)
{
	if (bg_configure_init() < 0)
		return SLURM_ERROR;

	return (*(ops.ba_reset_all_removed_mps))();
}


extern int bg_configure_new_ba_request(select_ba_request_t* ba_request)
{
	if (bg_configure_init() < 0)
		return SLURM_ERROR;

	return (*(ops.new_ba_request))(ba_request);
}

extern int bg_configure_allocate_block(
	select_ba_request_t* ba_request, List results)
{
	if (bg_configure_init() < 0)
		return SLURM_ERROR;

	return (*(ops.allocate_block))
		(ba_request, results);
}

extern int bg_configure_remove_block(List mps, bool is_small)
{
	if (bg_configure_init() < 0)
		return SLURM_ERROR;

	return (*(ops.remove_block))(mps, is_small);
}

extern ba_mp_t *bg_configure_str2ba_mp(const char *coords)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(ops.str2ba_mp))(coords);
}

extern ba_mp_t *bg_configure_loc2ba_mp(const char *mp_id)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(ops.loc2ba_mp))(mp_id);
}

extern ba_mp_t *bg_configure_coord2ba_mp(const uint16_t *coord)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(ops.coord2ba_mp))(coord);
}

extern char *bg_configure_give_geo(uint16_t *int_geo, int dims, bool with_sep)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(ops.give_geo))(int_geo, dims, with_sep);
}

extern s_p_hashtbl_t *bg_configure_config_make_tbl(char *filename)
{
	if (bg_configure_init() < 0)
		return NULL;

	return (*(ops.config_make_tbl))(filename);
}

extern void ba_configure_set_ba_debug_flags(uint64_t debug_flags)
{
	if (bg_configure_init() < 0)
		return;

	(*(ops.set_ba_debug_flags))(debug_flags);
}
