/*****************************************************************************\
 *  slurmd_plugstack.c - driver for slurmd plugstack plugin
 *****************************************************************************
 *  Copyright (C) 2013 Intel Inc.
 *  Written by Ralph H Castain <ralph.h.castain@intel.com>
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
#else /* ! HAVE_CONFIG_H */
#  include <sys/types.h>
#  include <unistd.h>
#  include <stdint.h>
#  include <string.h>
#endif /* HAVE_CONFIG_H */
#include <stdio.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmd/slurmd_plugstack.h"

slurm_nonstop_ops_t nonstop_ops = { NULL, NULL, NULL };

typedef struct slurmd_plugstack_ops {
	/* NO FUNCTIONS */
} slurmd_plugstack_ops_t;

/*
 * Must be synchronized with slurmd_plugstack_t above.
 */
static const char *syms[] = {
	/* NO FUNCTIONS */
};

static int g_context_cnt = -1;
static slurmd_plugstack_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static char *slurmd_plugstack_list = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * Initialize the slurmd plugstack plugin.
 *
 * Returns a SLURM errno.
 */
extern int slurmd_plugstack_init(void)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *names;
	char *plugin_type = "slurmd_plugstack";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	slurmd_plugstack_list = slurm_get_slurmd_plugstack();
	g_context_cnt = 0;
	if ((slurmd_plugstack_list == NULL) ||
	    (slurmd_plugstack_list[0] == '\0'))
		goto fini;

	names = slurmd_plugstack_list;
	while ((type = strtok_r(names, ",", &last))) {
		xrealloc(ops, (sizeof(slurmd_plugstack_ops_t) *
			      (g_context_cnt + 1)));
		xrealloc(g_context,
			 (sizeof(plugin_context_t *) * (g_context_cnt + 1)));
		if (strncmp(type, "slurmd/", 10) == 0)
			type += 10; /* backward compatibility */
		type = xstrdup_printf("slurmd/%s", type);
		g_context[g_context_cnt] = plugin_context_create(
			plugin_type, type, (void **)&ops[g_context_cnt],
			syms, sizeof(syms));
		if (!g_context[g_context_cnt]) {
			error("cannot create %s context for %s",
			      plugin_type, type);
			rc = SLURM_ERROR;
			xfree(type);
			break;
		}

		xfree(type);
		g_context_cnt++;
		names = NULL; /* for next iteration */
	}
	init_run = true;

fini:
	slurm_mutex_unlock(&g_context_lock);

	if (rc != SLURM_SUCCESS)
		slurmd_plugstack_fini();

	return rc;
}

/*
 * Terminate the slurmd plugstack plugin. Free memory.
 *
 * Returns a SLURM errno.
 */
extern int slurmd_plugstack_fini(void)
{
	int i, j, rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt < 0)
		goto fini;

	init_run = false;
	for (i=0; i<g_context_cnt; i++) {
		if (g_context[i]) {
			j = plugin_context_destroy(g_context[i]);
			if (j != SLURM_SUCCESS)
				rc = j;
		}
	}
	xfree(ops);
	xfree(g_context);
	xfree(slurmd_plugstack_list);
	g_context_cnt = -1;

fini:	slurm_mutex_unlock(&g_context_lock);
	return rc;
}
