/*****************************************************************************\
 *  hash.c - hash plugin driver
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Dominik Bartkiewicz <bart@schedmd.com>
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

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "src/interfaces/hash.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/interfaces/auth.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* Symbols provided by the plugin */
typedef struct slurm_ops {
	uint32_t	(*plugin_id);
	char		(*plugin_type);
	int (*compute)	(char *input, int len, char *custom_str, int cs_len,
			 slurm_hash_t *hash);
} slurm_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"plugin_type",
	"hash_p_compute",
};

/* Local variables */
static slurm_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static int g_context_num = -1;
static pthread_mutex_t g_context_lock =	PTHREAD_MUTEX_INITIALIZER;

static unsigned char hash_id_to_inx[HASH_PLUGIN_CNT];

/*
 * Initialize the hash plugin.
 */
extern int hash_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = "hash";

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	g_context_num = 0;
	memset(hash_id_to_inx, 0xff, HASH_PLUGIN_CNT);

	xrecalloc(ops, g_context_num + 1, sizeof(slurm_ops_t));
	xrecalloc(g_context, g_context_num + 1, sizeof(plugin_context_t *));

	g_context[g_context_num] = plugin_context_create(
		plugin_type, "hash/k12", (void **)&ops[g_context_num],
		syms, sizeof(syms));
	if (!g_context[g_context_num] ||
	    (*(ops[g_context_num].plugin_id) != HASH_PLUGIN_K12)) {
		error("cannot create %s context for K12", plugin_type);
		rc = SLURM_ERROR;
		goto done;
	}
	hash_id_to_inx[*(ops[g_context_num].plugin_id)] = g_context_num;
	g_context_num++;

	hash_id_to_inx[HASH_PLUGIN_DEFAULT] = 0;

done:
	slurm_mutex_unlock(&g_context_lock);

	return rc;
}

extern int hash_g_compute(char *input, int len, char *custom_str, int cs_len,
			  slurm_hash_t *hash)
{
	int index;

	xassert(g_context);

	if ((hash->type >= sizeof(hash_id_to_inx)) ||
	    ((index = hash_id_to_inx[hash->type]) == 0xff)) {
		error("%s: hash plugin with id:%u not exist or is not loaded",
		      __func__, hash->type);
		return -1;
	}

	return (*(ops[index].compute))(input, len, custom_str, cs_len, hash);
}
