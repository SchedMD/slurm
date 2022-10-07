/*****************************************************************************\
 **  pmix_db.c - PMIx KVS database
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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

#include "pmixp_common.h"
#include "pmixp_nspaces.h"

pmixp_db_t _pmixp_nspaces;

static void _xfree_nspace(void *n)
{
	pmixp_namespace_t *nsptr = n;
	xfree(nsptr->task_cnts);
	xfree(nsptr->task_map);
	xfree(nsptr->task_map_packed);
	xfree(nsptr);
}

int pmixp_nspaces_init(void)
{
	char *mynspace, *task_map;
	uint32_t nnodes, ntasks, *task_cnts;
	int nodeid, rc;
	hostlist_t hl;

#ifndef NDEBUG
	_pmixp_nspaces.magic = PMIXP_NSPACE_DB_MAGIC;
#endif
	_pmixp_nspaces.nspaces = list_create(_xfree_nspace);
	mynspace = pmixp_info_namespace();
	nnodes = pmixp_info_nodes();
	nodeid = pmixp_info_nodeid();
	ntasks = pmixp_info_tasks();
	task_cnts = pmixp_info_tasks_cnts();
	task_map = pmixp_info_task_map();
	hl = pmixp_info_step_hostlist();
	/* Initialize local namespace */
	rc = pmixp_nspaces_add(mynspace, nnodes, nodeid, ntasks, task_cnts,
			       task_map, hostlist_copy(hl));
	_pmixp_nspaces.local = pmixp_nspaces_find(mynspace);
	return rc;
}

int pmixp_nspaces_finalize(void)
{
	FREE_NULL_LIST(_pmixp_nspaces.nspaces);
	return 0;
}

int pmixp_nspaces_add(char *name, uint32_t nnodes, int node_id,
		      uint32_t ntasks, uint32_t *task_cnts,
		      char *task_map_packed, hostlist_t hl)
{
	pmixp_namespace_t *nsptr = xmalloc(sizeof(pmixp_namespace_t));
	int i;

	xassert(_pmixp_nspaces.magic == PMIXP_NSPACE_DB_MAGIC);

	/* fill up informational part */
#ifndef NDEBUG
	nsptr->magic = PMIXP_NSPACE_MAGIC;
#endif
	strlcpy(nsptr->name, name, sizeof(nsptr->name));
	nsptr->nnodes = nnodes;
	nsptr->node_id = node_id;
	nsptr->ntasks = ntasks;
	nsptr->task_cnts = xmalloc(sizeof(uint32_t) * nnodes);
	/* Cannot use memcpy here because of different types */
	for (i = 0; i < nnodes; i++) {
		nsptr->task_cnts[i] = task_cnts[i];
	}
	nsptr->task_map_packed = xstrdup(task_map_packed);
	nsptr->task_map = unpack_process_mapping_flat(task_map_packed, nnodes,
						      ntasks, NULL);
	if (nsptr->task_map == NULL) {
		xfree(nsptr->task_cnts);
		xfree(nsptr->task_map_packed);
		return SLURM_ERROR;
	}
	nsptr->hl = hl;
	list_append(_pmixp_nspaces.nspaces, nsptr);
	return SLURM_SUCCESS;
}

pmixp_namespace_t *pmixp_nspaces_local(void)
{
	xassert(_pmixp_nspaces.magic == PMIXP_NSPACE_DB_MAGIC);
	return _pmixp_nspaces.local;
}

pmixp_namespace_t *pmixp_nspaces_find(const char *name)
{
	xassert(_pmixp_nspaces.magic == PMIXP_NSPACE_DB_MAGIC);

	ListIterator it = list_iterator_create(_pmixp_nspaces.nspaces);
	pmixp_namespace_t *nsptr = NULL;
	while ((nsptr = list_next(it))) {
		xassert(nsptr->magic == PMIXP_NSPACE_MAGIC);
		if (0 == xstrcmp(nsptr->name, name)) {
			goto exit;
		}
	}
	/* Didn't found one! */
	nsptr = NULL;
exit:
	return nsptr;
}

hostlist_t pmixp_nspace_rankhosts(pmixp_namespace_t *nsptr, const uint32_t *ranks,
				  size_t nranks)
{
	hostlist_t hl = hostlist_create("");
	int i;
	for (i = 0; i < nranks; i++) {
		int rank = ranks[i];
		int node = nsptr->task_map[rank];
		char *node_s = hostlist_nth(nsptr->hl, node);
		hostlist_push(hl, node_s);
		free(node_s);
	}
	hostlist_uniq(hl);
	return hl;
}

int pmixp_nspace_resolve(const char *name, int rank)
{
	pmixp_namespace_t *nsptr;

	xassert(_pmixp_nspaces.magic == PMIXP_NSPACE_DB_MAGIC);

	ListIterator it = list_iterator_create(_pmixp_nspaces.nspaces);
	while ((nsptr = list_next(it))) {
		xassert(nsptr->magic == PMIXP_NSPACE_MAGIC);
		if (0 == xstrcmp(nsptr->name, name)) {
			break;
		}
	}

	if (NULL == nsptr) {
		return SLURM_ERROR;
	}
	xassert(rank < nsptr->ntasks);

	return nsptr->task_map[rank];
}
