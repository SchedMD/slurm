/*****************************************************************************\
 *  slurm_route.c - route plugin functions.
 *****************************************************************************
 *  Copyright (C) 2014 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
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

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/param.h>		/* MAXPATHLEN */

#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/forward.h"
#include "src/common/node_conf.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/interfaces/route.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

strong_alias(route_split_hostlist_treewidth,
	     slurm_route_split_hostlist_treewidth);

typedef struct slurm_route_ops {
	int  (*split_hostlist)    (hostlist_t hl,
				   hostlist_t** sp_hl,
				   int* count, uint16_t tree_width);
	int  (*reconfigure)       (void);
} slurm_route_ops_t;

/*
 * Must be synchronized with slurm_route_ops_t above.
 */
static const char *syms[] = {
	"route_p_split_hostlist",
	"route_p_reconfigure",
};

static slurm_route_ops_t ops;
static plugin_context_t	*g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;

extern int route_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "route";

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	g_context = plugin_context_create(plugin_type,
					  slurm_conf.route_plugin,
					  (void **) &ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s",
		      plugin_type, slurm_conf.route_plugin);
		retval = SLURM_ERROR;
		goto done;
	}

done:
	slurm_mutex_unlock(&g_context_lock);
	return retval;
}

extern int route_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	rc = plugin_context_destroy(g_context);
	g_context = NULL;

	return rc;
}


/*
 * route_g_split_hostlist - logic to split an input hostlist into
 *                          a set of hostlists to forward to.
 *
 * IN: hl        - hostlist_t   - list of every node to send message to
 *                                will be empty on return which is same behavior
 *                                as similar code replaced in forward.c
 * OUT: sp_hl    - hostlist_t** - the array of hostlists that will be malloced
 * OUT: count    - int*         - the count of created hostlists
 * RET: SLURM_SUCCESS - int
 *
 * Note: created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: the hostlist_t array will have to be xfree.
 */
extern int route_g_split_hostlist(hostlist_t hl,
				  hostlist_t** sp_hl,
				  int* count, uint16_t tree_width)
{
	int rc;
	int j, nnodes, nnodex;
	char *buf;

	nnodes = nnodex = 0;
	xassert(g_context);

	if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
		/* nnodes has to be set here as the hl is empty after the
		 * split_hostlise call.  */
		nnodes = hostlist_count(hl);
		buf = hostlist_ranged_string_xmalloc(hl);
		info("ROUTE: split_hostlist: hl=%s tree_width %u",
		     buf, tree_width);
		xfree(buf);
	}

	if (!tree_width)
		tree_width = slurm_conf.tree_width;

	rc = (*(ops.split_hostlist))(hl, sp_hl, count, tree_width);
	if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
		/* Sanity check to make sure all nodes in msg list are in
		 * a child list */
		nnodex = 0;
		for (j = 0; j < *count; j++) {
			nnodex += hostlist_count((*sp_hl)[j]);
		}
		if (nnodex != nnodes) {	/* CLANG false positive */
			info("ROUTE: number of nodes in split lists (%d)"
			     " is not equal to number in input list (%d)",
			     nnodex, nnodes);
		}
	}
	return rc;
}

/*
 * route_g_reconfigure - reset during reconfigure
 *
 * RET: SLURM_SUCCESS - int
 */
extern int route_g_reconfigure(void)
{
	xassert(g_context);

	return (*(ops.reconfigure))();
}

/*
 * route_split_hostlist_treewidth - logic to split an input hostlist into
 *                                  a set of hostlists to forward to.
 *
 * This is the default behavior. It is implemented here as there are cases
 * where the topology version also needs to split the message list based
 * on TreeWidth.
 *
 * IN: hl        - hostlist_t   - list of every node to send message to
 *                                will be empty on return which is same behavior
 *                                as similar code replaced in forward.c
 * OUT: sp_hl    - hostlist_t** - the array of hostlists that will be malloced
 * OUT: count    - int*         - the count of created hostlists
 * RET: SLURM_SUCCESS - int
 *
 * Note: created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: the hostlist_t array will have to be xfree.
 */
extern int route_split_hostlist_treewidth(hostlist_t hl,
					  hostlist_t** sp_hl,
					  int* count, uint16_t tree_width)
{
	int host_count;
	int *span = NULL;
	char *name = NULL;
	char *buf;
	int nhl = 0;
	int j;

	if (!tree_width)
		tree_width = slurm_conf.tree_width;

	host_count = hostlist_count(hl);
	span = set_span(host_count, tree_width);
	*sp_hl = xmalloc(tree_width * sizeof(hostlist_t));

	while ((name = hostlist_shift(hl))) {
		(*sp_hl)[nhl] = hostlist_create(name);
		free(name);
		for (j = 0; j < span[nhl]; j++) {
			name = hostlist_shift(hl);
			if (!name) {
				break;
			}
			hostlist_push_host((*sp_hl)[nhl], name);
			free(name);
		}
		if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
			buf = hostlist_ranged_string_xmalloc((*sp_hl)[nhl]);
			debug("ROUTE: ... sublist[%d] %s", nhl, buf);
			xfree(buf);
		}
		nhl++;
	}
	xfree(span);
	*count = nhl;

	return SLURM_SUCCESS;
}
