/*****************************************************************************\
 **  pmix_coll.c - PMIx collective primitives
 *****************************************************************************
 *  Copyright (C) 2018      Mellanox Technologies. All rights reserved.
 *  Written by Boris Karasev <karasev.b@gmail.com, boriska@mellanox.com>.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "pmixp_common.h"
#include "pmixp_coll.h"
#include "pmixp_nspaces.h"
#include "pmixp_client.h"
#include "pmixp_server.h"

int pmixp_hostset_from_ranges(const pmixp_proc_t *procs, size_t nprocs,
			      hostlist_t *hl_out)
{
	int i;
	hostlist_t hl = hostlist_create("");
	pmixp_namespace_t *nsptr = NULL;
	for (i = 0; i < nprocs; i++) {
		char *node = NULL;
		hostlist_t tmp;
		nsptr = pmixp_nspaces_find(procs[i].nspace);
		if (NULL == nsptr) {
			goto err_exit;
		}
		if (pmixp_lib_is_wildcard(procs[i].rank)) {
			tmp = hostlist_copy(nsptr->hl);
		} else {
			tmp = pmixp_nspace_rankhosts(nsptr, &procs[i].rank, 1);
		}
		while (NULL != (node = hostlist_pop(tmp))) {
			hostlist_push(hl, node);
			free(node);
		}
		hostlist_destroy(tmp);
	}
	hostlist_uniq(hl);
	*hl_out = hl;
	return SLURM_SUCCESS;
err_exit:
	hostlist_destroy(hl);
	return SLURM_ERROR;
}

int pmixp_coll_contrib_local(pmixp_coll_t *coll, pmixp_coll_type_t type,
			     char *data, size_t ndata,
			     void *cbfunc, void *cbdata) {
	int ret = SLURM_SUCCESS;

	PMIXP_DEBUG("%p: %s seq=%d, size=%lu", coll, pmixp_coll_type2str(type),
		    coll->seq, ndata);
	switch (type) {
	case PMIXP_COLL_TYPE_FENCE:
		ret = pmixp_coll_tree_contrib_local(coll, data,
						    ndata, cbfunc, cbdata);
		break;
	/*
	case PMIXP_COLL_TYPE_FENCE_RING:
		ret = pmixp_coll_ring_contrib_local(coll, data,
						    ndata, cbfunc, cbdata);
		break;
	*/
	default:
		ret = SLURM_FAILURE;
		break;
	}

	return ret;
}

int pmixp_coll_init(pmixp_coll_t *coll, pmixp_coll_type_t type,
		    const pmixp_proc_t *procs, size_t nprocs)
{
	int rc = SLURM_SUCCESS;

	coll->type = type;



	return rc;
}
