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
	case PMIXP_COLL_TYPE_FENCE_TREE:
		ret = pmixp_coll_tree_contrib_local(coll, data,
						    ndata, cbfunc, cbdata);
		break;
	case PMIXP_COLL_TYPE_FENCE_RING:
		ret = pmixp_coll_ring_contrib_local(coll, data,
						    ndata, cbfunc, cbdata);
		break;
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
	hostlist_t hl;

	coll->seq = 0;
#ifndef NDEBUG
	coll->magic = PMIXP_COLL_STATE_MAGIC;
#endif
	coll->type = type;
	coll->pset.procs = xmalloc(sizeof(*procs) * nprocs);
	coll->pset.nprocs = nprocs;
	memcpy(coll->pset.procs, procs, sizeof(*procs) * nprocs);

	if (SLURM_SUCCESS != pmixp_hostset_from_ranges(procs, nprocs, &hl)) {
		/* TODO: provide ranges output routine */
		PMIXP_ERROR("Bad ranges information");
		rc = SLURM_ERROR;
		goto exit;
	}
	coll->peers_cnt = hostlist_count(hl);
	coll->my_peerid = hostlist_find(hl, pmixp_info_hostname());
#ifdef PMIXP_COLL_DEBUG
	/* if we debug collectives - store a copy of a full
	 * hostlist to resolve participant id to the hostname */
	coll->peers_hl = hostlist_copy(hl);
#endif

	switch(type) {
	case PMIXP_COLL_TYPE_FENCE_TREE:
		rc = pmixp_coll_tree_init(coll, &hl);
		break;
	case PMIXP_COLL_TYPE_FENCE_RING:
		rc = pmixp_coll_ring_init(coll);
		break;
	default:
		PMIXP_ERROR("Unknown coll type");
		rc = SLURM_FAILURE;
	}
	hostlist_destroy(hl);
	if (rc) {
		goto exit;
	}

exit:
	return rc;
}

void pmixp_coll_free(pmixp_coll_t *coll)
{
	pmixp_coll_sanity_check(coll);

	if (NULL != coll->pset.procs) {
		xfree(coll->pset.procs);
	}
#ifdef PMIXP_COLL_DEBUG
	hostlist_destroy(coll->peers_hl);
#endif
	switch(coll->type) {
	case PMIXP_COLL_TYPE_FENCE_TREE:

		/*
		 * check for collective in a not-SYNC state - something went
		 * wrong
		 */
		if (PMIXP_COLL_TREE_SYNC != coll->state.tree.state)
			pmixp_coll_log(coll);

		pmixp_coll_tree_free(&coll->state.tree);
		break;
	case PMIXP_COLL_TYPE_FENCE_RING:
		pmixp_coll_ring_free(&coll->state.ring);
		break;
	default:
		PMIXP_ERROR("Unknown coll type");
		break;
	}
	xfree(coll);
}

int pmixp_coll_belong_chk(const pmixp_proc_t *procs, size_t nprocs)
{
	int i;
	pmixp_namespace_t *nsptr = pmixp_nspaces_local();
	/* Find my namespace in the range */
	for (i = 0; i < nprocs; i++) {
		if (0 != xstrcmp(procs[i].nspace, nsptr->name)) {
			continue;
		}
		if (pmixp_lib_is_wildcard(procs[i].rank))
			return 0;
		if (0 <= pmixp_info_taskid2localid(procs[i].rank)) {
			return 0;
		}
	}
	/* we don't participate in this collective! */
	PMIXP_ERROR("Have collective that doesn't include this job's namespace");
	return -1;
}
