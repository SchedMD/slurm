/*****************************************************************************\
 *  compress.c - Compression plugin interface
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <pthread.h>

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/compress.h"

typedef struct compress_ops {
	uint32_t *plugin_id;
	ssize_t (*compress_p_comp_block)(char **in_buf,
					 const ssize_t input_size,
					 char **out_buf, const ssize_t out_size,
					 ssize_t *remaining);
	char *(*compress_p_decompress)(char *in_buf, const ssize_t in_size,
				       const ssize_t out_size);
} compress_ops_t;

/*
 * Must be synchronized with compress_ops_t above.
 */
static const char *syms[] = {
	"plugin_id",
	"compress_p_comp_block",
	"compress_p_decompress",
};

static compress_ops_t *ops = NULL;
static plugin_context_t **compress_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

static int compress_context_cnt = -1;

typedef struct _plugin_args {
	char *plugin_type;
} _plugin_args_t;

static int _load_plugins(void *x, void *arg)
{
	char *plugin_name = (char *) x;
	_plugin_args_t *pargs = (_plugin_args_t *) arg;

	compress_context[compress_context_cnt] =
		plugin_context_create(pargs->plugin_type, plugin_name,
				      (void **) &ops[compress_context_cnt],
				      syms, sizeof(syms));

	if (compress_context[compress_context_cnt]) {
		compress_context_cnt++;
	}

	return 0;
}

static int _get_plugin_index(const int type)
{
	for (int i = 0; i < compress_context_cnt; i++) {
		if (*(ops[i].plugin_id) == type)
			return i;
	}
	return -1;
}

extern int compress_g_init(void)
{
	char *plugin_type = "compress";
	list_t *plugin_names = NULL;
	int rc = SLURM_SUCCESS, plugin_cnt = 0;
	_plugin_args_t plugin_args = { 0 };

	slurm_rwlock_wrlock(&context_lock);

	if (plugin_inited != PLUGIN_NOT_INITED)
		goto done;

	plugin_args.plugin_type = plugin_type;
	compress_context_cnt = 0;

	plugin_names = plugin_get_plugins_of_type(plugin_type);

	if (plugin_names && (plugin_cnt = list_count(plugin_names))) {
		ops = xcalloc(plugin_cnt, sizeof(compress_ops_t));
		compress_context =
			xcalloc(plugin_cnt, sizeof(plugin_context_t *));
		list_for_each(plugin_names, _load_plugins, &plugin_args);
	}

	if (compress_context_cnt == 0)
		fatal("Unable to locate valid compression plugin");

	plugin_inited = PLUGIN_INITED;

done:
	slurm_rwlock_unlock(&context_lock);
	FREE_NULL_LIST(plugin_names);
	return rc;
}

extern void compress_g_fini(void)
{
	slurm_rwlock_wrlock(&context_lock);

	for (int i = 0; i < compress_context_cnt; i++) {
		plugin_context_destroy(compress_context[i]);
	}
	xfree(compress_context);
	xfree(ops);
	compress_context_cnt = -1;

	plugin_inited = PLUGIN_NOT_INITED;

	slurm_rwlock_unlock(&context_lock);
}

extern ssize_t compress_g_comp_block(const int type, char **in_buf,
				     const ssize_t input_size, char **out_buf,
				     const ssize_t out_size, ssize_t *remaining)
{
	int plugin_index = -1;
	ssize_t rc = -1;
	slurm_rwlock_rdlock(&context_lock);

	if (compress_context_cnt <= 0) {
		rc = SLURM_ERROR;
		goto done;
	}

	if ((plugin_index = _get_plugin_index(type)) == -1) {
		error("Unable to find requested compression plugin");
		rc = SLURM_ERROR;
		goto done;
	}

	rc = (*(ops[plugin_index].compress_p_comp_block))(in_buf, input_size,
							  out_buf, out_size,
							  remaining);

done:
	slurm_rwlock_unlock(&context_lock);
	return rc;
}

extern char *compress_g_decompress(const int type, char *in_buf,
				   const ssize_t in_size,
				   const ssize_t out_size)
{
	int plugin_index = -1;
	char *rc = NULL;
	slurm_rwlock_rdlock(&context_lock);

	if (compress_context_cnt <= 0) {
		goto done;
	}

	if ((plugin_index = _get_plugin_index(type)) == -1) {
		error("Unable to find requested compression plugin");
		goto done;
	}

	rc = (*(ops[plugin_index].compress_p_decompress))(in_buf, in_size,
							  out_size);

done:
	slurm_rwlock_unlock(&context_lock);
	return rc;
}
