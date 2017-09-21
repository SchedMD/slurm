/*****************************************************************************\
 **  pmix_coll.c - PMIx collective primitives
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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
#include "src/slurmd/common/reverse_tree_math.h"
#include "src/common/slurm_protocol_api.h"
#include "pmixp_coll.h"
#include "pmixp_nspaces.h"
#include "pmixp_server.h"

static void _progress_coll(pmixp_coll_t *coll);
static void _reset_coll(pmixp_coll_t *coll);

static int _hostset_from_ranges(const pmix_proc_t *procs, size_t nprocs,
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
		if (procs[i].rank == PMIX_RANK_WILDCARD) {
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

static int _pack_coll_info(pmixp_coll_t *coll, Buf buf)
{
	pmix_proc_t *procs = coll->pset.procs;
	size_t nprocs = coll->pset.nprocs;
	uint32_t size;
	int i;

	/* 1. store the type of collective */
	size = coll->type;
	pack32(size, buf);

	/* 2. Put the number of ranges */
	pack32(nprocs, buf);
	for (i = 0; i < (int)nprocs; i++) {
		/* Pack namespace */
		packmem(procs->nspace, strlen(procs->nspace) + 1, buf);
		pack32(procs->rank, buf);
	}

	return SLURM_SUCCESS;
}

int pmixp_coll_unpack_info(Buf buf, pmixp_coll_type_t *type,
			   int *nodeid, pmix_proc_t **r, size_t *nr)
{
	pmix_proc_t *procs = NULL;
	uint32_t nprocs = 0;
	uint32_t tmp;
	int i, rc;

	/* 1. extract the type of collective */
	if (SLURM_SUCCESS != (rc = unpack32(&tmp, buf))) {
		PMIXP_ERROR("Cannot unpack collective type");
		return rc;
	}
	*type = tmp;

	/* 2. get the number of ranges */
	if (SLURM_SUCCESS != (rc = unpack32(&nprocs, buf))) {
		PMIXP_ERROR("Cannot unpack collective type");
		return rc;
	}
	*nr = nprocs;

	procs = xmalloc(sizeof(pmix_proc_t) * nprocs);
	*r = procs;

	for (i = 0; i < (int)nprocs; i++) {
		/* 3. get namespace/rank of particular process */
		rc = unpackmem(procs[i].nspace, &tmp, buf);
		if (SLURM_SUCCESS != rc) {
			PMIXP_ERROR("Cannot unpack namespace for process #%d",
				    i);
			return rc;
		}
		procs[i].nspace[tmp] = '\0';

		unsigned int tmp;
		rc = unpack32(&tmp, buf);
		procs[i].rank = tmp;
		if (SLURM_SUCCESS != rc) {
			PMIXP_ERROR("Cannot unpack ranks for process #%d, nsp=%s",
				    i, procs[i].nspace);
			return rc;
		}
	}
	return SLURM_SUCCESS;
}

int pmixp_coll_belong_chk(pmixp_coll_type_t type,
			  const pmix_proc_t *procs, size_t nprocs)
{
	int i;
	pmixp_namespace_t *nsptr = pmixp_nspaces_local();
	/* Find my namespace in the range */
	for (i = 0; i < nprocs; i++) {
		if (0 != xstrcmp(procs[i].nspace, nsptr->name)) {
			continue;
		}
		if ((procs[i].rank == PMIX_RANK_WILDCARD))
			return 0;
		if (0 <= pmixp_info_taskid2localid(procs[i].rank)) {
			return 0;
		}
	}
	/* we don't participate in this collective! */
	PMIXP_ERROR("Have collective that doesn't include this job's namespace");
	return -1;
}

static void _reset_coll_ufwd(pmixp_coll_t *coll)
{
	/* upward status */
	coll->contrib_children = 0;
	coll->contrib_local = false;
	memset(coll->contrib_chld, 0,
	       sizeof(coll->contrib_chld[0]) * coll->chldrn_cnt);
	coll->serv_offs = pmixp_server_buf_reset(coll->ufwd_buf);
	if (SLURM_SUCCESS != _pack_coll_info(coll, coll->ufwd_buf)) {
		PMIXP_ERROR("Cannot pack ranges to message header!");
	}
	coll->ufwd_offset = get_buf_offset(coll->ufwd_buf);
	coll->ufwd_status = PMIXP_COLL_SND_NONE;
}

static void _reset_coll_dfwd(pmixp_coll_t *coll)
{
	/* downwards status */
	(void)pmixp_server_buf_reset(coll->dfwd_buf);
	if (SLURM_SUCCESS != _pack_coll_info(coll, coll->dfwd_buf)) {
		PMIXP_ERROR("Cannot pack ranges to message header!");
	}
	coll->dfwd_cb_cnt = 0;
	coll->dfwd_cb_wait = 0;
	coll->dfwd_status = PMIXP_COLL_SND_NONE;
	coll->contrib_prnt = false;
	/* Save the toal service offset */
	coll->dfwd_offset = get_buf_offset(coll->dfwd_buf);
}

static void _reset_coll(pmixp_coll_t *coll)
{
	switch (coll->state) {
	case PMIXP_COLL_SYNC:
		/* already reset */
		xassert(!coll->contrib_local && !coll->contrib_children &&
			!coll->contrib_prnt);
		break;
	case PMIXP_COLL_COLLECT:
	case PMIXP_COLL_UPFWD:
	case PMIXP_COLL_UPFWD_WSC:
		coll->seq++;
		coll->state = PMIXP_COLL_SYNC;
		_reset_coll_ufwd(coll);
		_reset_coll_dfwd(coll);
		coll->cbdata = NULL;
		coll->cbfunc = NULL;
		break;
	case PMIXP_COLL_UPFWD_WPC:
		/* If we were waiting for the parent contrib,
		 * upward portion is already reset, and may contain
		 * next collective's data */
	case PMIXP_COLL_DOWNFWD:
		/* same with downward state */
		coll->seq++;
		_reset_coll_dfwd(coll);
		if (coll->contrib_local || coll->contrib_children) {
			/* next collective was already started */
			coll->state = PMIXP_COLL_COLLECT;
		} else {
			coll->state = PMIXP_COLL_SYNC;
		}

		if (!coll->contrib_local) {
			/* drop the callback info if we haven't started
			 * next collective locally
			 */
			coll->cbdata = NULL;
			coll->cbfunc = NULL;
		}
		break;
	default:
		PMIXP_ERROR("Bad collective state = %d", (int)coll->state);
		abort();
	}
}


/*
 * Based on ideas provided by Hongjia Cao <hjcao@nudt.edu.cn> in PMI2 plugin
 */
int pmixp_coll_init(pmixp_coll_t *coll, const pmix_proc_t *procs,
		    size_t nprocs, pmixp_coll_type_t type)
{
	hostlist_t hl;
	int max_depth, width, depth, i;
	char *p;

#ifndef NDEBUG
	coll->magic = PMIXP_COLL_STATE_MAGIC;
#endif
	coll->type = type;
	coll->state = PMIXP_COLL_SYNC;
	coll->pset.procs = xmalloc(sizeof(*procs) * nprocs);
	coll->pset.nprocs = nprocs;
	memcpy(coll->pset.procs, procs, sizeof(*procs) * nprocs);

	if (SLURM_SUCCESS != _hostset_from_ranges(procs, nprocs, &hl)) {
		/* TODO: provide ranges output routine */
		PMIXP_ERROR("Bad ranges information");
		goto err_exit;
	}
#ifdef PMIXP_COLL_DEBUG
	/* if we debug collectives - store a copy of a full
	 * hostlist to resolve participant id to the hostname */
	coll->peers_hl = hostlist_copy(hl);
#endif

	width = slurm_get_tree_width();
	coll->peers_cnt = hostlist_count(hl);
	coll->my_peerid = hostlist_find(hl, pmixp_info_hostname());
	reverse_tree_info(coll->my_peerid, coll->peers_cnt, width,
			  &coll->prnt_peerid, &coll->chldrn_cnt, &depth,
			  &max_depth);

	/* We interested in amount of direct childs */
	coll->seq = 0;
	coll->contrib_children = 0;
	coll->contrib_local = false;
	coll->chldrn_ids = xmalloc(sizeof(int) * width);
	coll->contrib_chld = xmalloc(sizeof(int) * width);
	coll->chldrn_cnt = reverse_tree_direct_children(coll->my_peerid,
							coll->peers_cnt,
							  width, depth,
							  coll->chldrn_ids);
	if (coll->prnt_peerid == -1) {
		/* if we are the root of the tree:
		 * - we don't have a parent;
		 * - we have large list of all_childrens (we don't want
		 * ourselfs there)
		 */
		coll->prnt_host = NULL;
		coll->all_chldrn_hl = hostlist_copy(hl);
		hostlist_delete_host(coll->all_chldrn_hl,
				     pmixp_info_hostname());
		coll->chldrn_str =
			hostlist_ranged_string_xmalloc(coll->all_chldrn_hl);
	} else {
		/* for all other nodes in the tree we need to know:
		 * - nodename of our parent;
		 * - we don't need a list of all_childrens and hl anymore
		 */

		/*
		 * setup parent id's
		 */
		p = hostlist_nth(hl, coll->prnt_peerid);
		coll->prnt_host = xstrdup(p);
		free(p);
		/* reset prnt_peerid to the global peer */
		coll->prnt_peerid = pmixp_info_job_hostid(coll->prnt_host);

		/*
		 * setup root id's
		 * (we need this for the SLURM API communication case)
		 */
		p = hostlist_nth(hl, 0);
		coll->root_host = xstrdup(p);
		free(p);
		/* reset prnt_peerid to the global peer */
		coll->root_peerid = pmixp_info_job_hostid(coll->root_host);

		/* use empty hostlist here */
		coll->all_chldrn_hl = hostlist_create("");
		coll->chldrn_str = NULL;
	}

	/* fixup children peer ids to te global ones */
	for(i=0; i<coll->chldrn_cnt; i++){
		p = hostlist_nth(hl, coll->chldrn_ids[i]);
		coll->chldrn_ids[i] = pmixp_info_job_hostid(p);
		free(p);
	}
	hostlist_destroy(hl);

	/* Collective state */
	coll->ufwd_buf = pmixp_server_buf_new();
	coll->dfwd_buf = pmixp_server_buf_new();
	_reset_coll_ufwd(coll);
	_reset_coll_dfwd(coll);
	coll->cbdata = NULL;
	coll->cbfunc = NULL;

	/* init fine grained lock */
	slurm_mutex_init(&coll->lock);

	return SLURM_SUCCESS;
err_exit:
	return SLURM_ERROR;
}

void pmixp_coll_free(pmixp_coll_t *coll)
{
	if (NULL != coll->pset.procs) {
		xfree(coll->pset.procs);
	}
	if (NULL != coll->prnt_host) {
		xfree(coll->prnt_host);
	}
	if (NULL != coll->root_host) {
		xfree(coll->root_host);
	}
	hostlist_destroy(coll->all_chldrn_hl);
	if (coll->chldrn_str) {
		xfree(coll->chldrn_str);
	}
#ifdef PMIXP_COLL_DEBUG
	hostlist_destroy(coll->peers_hl);
#endif
	if (NULL != coll->contrib_chld) {
		xfree(coll->contrib_chld);
	}
	free_buf(coll->ufwd_buf);
	free_buf(coll->dfwd_buf);
}

typedef struct {
	pmixp_coll_t *coll;
	uint32_t seq;
	volatile uint32_t refcntr;
} pmixp_coll_cbdata_t;

/*
 * use it for internal collective
 * performance evaluation tool.
 */
pmixp_coll_t *pmixp_coll_from_cbdata(void *cbdata)
{
	pmixp_coll_cbdata_t *ptr = (pmixp_coll_cbdata_t*)cbdata;
	pmixp_coll_sanity_check(ptr->coll);
	return ptr->coll;
}

static void _ufwd_sent_cb(int rc, pmixp_p2p_ctx_t ctx, void *_vcbdata)
{
	pmixp_coll_cbdata_t *cbdata = (pmixp_coll_cbdata_t*)_vcbdata;
	pmixp_coll_t *coll = cbdata->coll;

	if( PMIXP_P2P_REGULAR == ctx ){
		/* lock the collective */
		slurm_mutex_lock(&coll->lock);
	}
	if (cbdata->seq != coll->seq) {
		/* it seems like this collective was reset since the time
		 * we initiated this send.
		 * Just exit to avoid data corruption.
		 */
		PMIXP_DEBUG("Collective was reset!");
		goto exit;
	}

	xassert(PMIXP_COLL_UPFWD == coll->state ||
		PMIXP_COLL_UPFWD_WSC == coll->state);


	/* Change  the status */
	if( SLURM_SUCCESS == rc ){
		coll->ufwd_status = PMIXP_COLL_SND_DONE;
	} else {
		coll->ufwd_status = PMIXP_COLL_SND_FAILED;
	}

#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: state: %s, snd_status=%s",
		    coll, pmixp_coll_state2str(coll->state),
		    pmixp_coll_sndstatus2str(coll->ufwd_status));
#endif

exit:
	xassert(0 < cbdata->refcntr);
	cbdata->refcntr--;
	if (!cbdata->refcntr) {
		xfree(cbdata);
	}

	if( PMIXP_P2P_REGULAR == ctx ){
		/* progress, in the inline case progress
		 * will be invoked by the caller */
		_progress_coll(coll);

		/* unlock the collective */
		slurm_mutex_unlock(&coll->lock);
	}
}

static void _dfwd_sent_cb(int rc, pmixp_p2p_ctx_t ctx, void *_vcbdata)
{
	pmixp_coll_cbdata_t *cbdata = (pmixp_coll_cbdata_t*)_vcbdata;
	pmixp_coll_t *coll = cbdata->coll;


	if( PMIXP_P2P_REGULAR == ctx ){
		/* lock the collective */
		slurm_mutex_lock(&coll->lock);
	}

	if (cbdata->seq != coll->seq) {
		/* it seems like this collective was reset since the time
		 * we initiated this send.
		 * Just exit to avoid data corruption.
		 */
		PMIXP_DEBUG("Collective was reset!");
		goto exit;
	}

	xassert(PMIXP_COLL_DOWNFWD == coll->state);

	/* Change  the status */
	if( SLURM_SUCCESS == rc ){
		coll->dfwd_cb_cnt++;
	} else {
		coll->dfwd_status = PMIXP_COLL_SND_FAILED;
	}

#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: state: %s, snd_status=%s, compl_cnt=%d/%d",
		    coll, pmixp_coll_state2str(coll->state),
		    pmixp_coll_sndstatus2str(coll->dfwd_status),
		    coll->dfwd_cb_cnt, coll->dfwd_cb_wait);
#endif

exit:
	xassert(0 < cbdata->refcntr);
	cbdata->refcntr--;
	if (!cbdata->refcntr) {
		xfree(cbdata);
	}

	if( PMIXP_P2P_REGULAR == ctx ){
		/* progress, in the inline case progress
		 * will be invoked by the caller */
		_progress_coll(coll);

		/* unlock the collective */
		slurm_mutex_unlock(&coll->lock);
	}
}

static void _libpmix_cb(void *_vcbdata)
{
	pmixp_coll_cbdata_t *cbdata = (pmixp_coll_cbdata_t*)_vcbdata;
	pmixp_coll_t *coll = cbdata->coll;

	/* lock the collective */
	slurm_mutex_lock(&coll->lock);

	if (cbdata->seq != coll->seq) {
		/* it seems like this collective was reset since the time
		 * we initiated this send.
		 * Just exit to avoid data corruption.
		 */
		PMIXP_ERROR("%p: collective was reset: myseq=%u, curseq=%u",
			    coll, cbdata->seq, coll->seq);
		goto exit;
	}

	xassert(PMIXP_COLL_DOWNFWD == coll->state);

	coll->dfwd_cb_cnt++;
#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: state: %s, snd_status=%s, compl_cnt=%d/%d",
		    coll, pmixp_coll_state2str(coll->state),
		    pmixp_coll_sndstatus2str(coll->dfwd_status),
		    coll->dfwd_cb_cnt, coll->dfwd_cb_wait);
#endif
	_progress_coll(coll);

exit:
	xassert(0 < cbdata->refcntr);
	cbdata->refcntr--;
	if (!cbdata->refcntr) {
		xfree(cbdata);
	}

	/* unlock the collective */
	slurm_mutex_unlock(&coll->lock);
}

static int _progress_collect(pmixp_coll_t *coll)
{
	pmixp_ep_t ep = {0};
	int rc;

	xassert(PMIXP_COLL_COLLECT == coll->state);

	ep.type = PMIXP_EP_NONE;
#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: state=%s, local=%d, child_cntr=%d",
		    coll, pmixp_coll_state2str(coll->state),
		    (int)coll->contrib_local, coll->contrib_children);
#endif
	/* lock the collective */
	pmixp_coll_sanity_check(coll);

	if (PMIXP_COLL_COLLECT != coll->state) {
		/* In case of race condition between libpmix and
		 * slurm threads we can be called
		 * after we moved to the next step. */
		return 0;
	}

	if (!coll->contrib_local ||
	    coll->contrib_children != coll->chldrn_cnt) {
		/* Not yet ready to go to the next step */
		return 0;
	}

	if (pmixp_info_srv_direct_conn()) {
		/* We will need to forward aggregated
		 * message back to our children */
		coll->state = PMIXP_COLL_UPFWD;
	} else {
		/* If we use SLURM API (SAPI) - intermediate nodes
		 * don't need to forward data as the root will do
		 * SAPI broadcast.
		 * So, only root has to go through the full UPFWD
		 * state and send the message back.
		 * Other procs have to go through other route. The reason for
		 * that is the fact that som of out children can receive bcast
		 * message early and initiate next collective. We need to handle
		 * that properly.
		 */
		if (0 > coll->prnt_peerid) {
			coll->state = PMIXP_COLL_UPFWD;
		} else {
			coll->state = PMIXP_COLL_UPFWD_WSC;
		}
	}

	/* The root of the collective will have parent_host == NULL */
	if (NULL != coll->prnt_host) {
		ep.type = PMIXP_EP_NOIDEID;
		ep.ep.nodeid = coll->prnt_peerid;
		coll->ufwd_status = PMIXP_COLL_SND_ACTIVE;
		PMIXP_DEBUG("%p: send data to %s:%d",
			    coll, coll->prnt_host, coll->prnt_peerid);
	} else {
		/* move data from input buffer to the output */
		char *dst, *src = get_buf_data(coll->ufwd_buf) +
				coll->ufwd_offset;
		size_t size = get_buf_offset(coll->ufwd_buf) -
				coll->ufwd_offset;
		pmixp_server_buf_reserve(coll->dfwd_buf, size);
		dst = get_buf_data(coll->dfwd_buf) + coll->dfwd_offset;
		memcpy(dst, src, size);
		set_buf_offset(coll->dfwd_buf, coll->dfwd_offset + size);
		/* no need to send */
		coll->ufwd_status = PMIXP_COLL_SND_DONE;
		/* this is root */
		coll->contrib_prnt = true;
	}

	if (PMIXP_EP_NONE != ep.type) {
		pmixp_coll_cbdata_t *cbdata;
		cbdata = xmalloc(sizeof(pmixp_coll_cbdata_t));
		cbdata->coll = coll;
		cbdata->seq = coll->seq;
		cbdata->refcntr = 1;
		char *nodename = coll->prnt_host;
		rc = pmixp_server_send_nb(&ep, PMIXP_MSG_FAN_IN, coll->seq,
					  coll->ufwd_buf,
					  _ufwd_sent_cb, cbdata);

		if (SLURM_SUCCESS != rc) {
			PMIXP_ERROR("Cannot send data (size = %lu), "
				    "to %s:%d",
				    (uint64_t) get_buf_offset(coll->ufwd_buf),
				    nodename, ep.ep.nodeid);
			coll->ufwd_status = PMIXP_COLL_SND_FAILED;
		}
#ifdef PMIXP_COLL_DEBUG
		PMIXP_DEBUG("%p: fwd to %s:%d, size = %lu",
			    coll, nodename, ep.ep.nodeid,
			    (uint64_t) get_buf_offset(coll->dfwd_buf));
#endif
	}

	/* events observed - need another iteration */
	return true;
}

static int _progress_ufwd(pmixp_coll_t *coll)
{
	pmixp_ep_t ep[coll->chldrn_cnt];
	int ep_cnt = 0;
	int rc, i;
	char *nodename = NULL;
	pmixp_coll_cbdata_t *cbdata = NULL;

	xassert(PMIXP_COLL_UPFWD == coll->state);

	/* for some reasons doesnt switch to downfwd */

	switch (coll->ufwd_status) {
	case PMIXP_COLL_SND_FAILED:
		/* something went wrong with upward send.
		 * notify libpmix about that and abort
		 * collective */
		if (coll->cbfunc) {
			coll->cbfunc(PMIX_ERROR, NULL, 0, coll->cbdata,
				     NULL, NULL);
		}
		_reset_coll(coll);
		/* Don't need to do anything else */
		return false;
	case PMIXP_COLL_SND_ACTIVE:
		/* still waiting for the send completion */
		return false;
	case PMIXP_COLL_SND_DONE:
		if (coll->contrib_prnt) {
			/* all-set to go to the next stage */
			break;
		}
		return false;
	default:
		/* Should not happen, fatal error */
		abort();
	}

	/* We now can upward part for the next collective */
	_reset_coll_ufwd(coll);

	/* move to the next state */
	coll->state = PMIXP_COLL_DOWNFWD;
	coll->dfwd_status = PMIXP_COLL_SND_ACTIVE;
	if (!pmixp_info_srv_direct_conn()) {
		/* only root of the tree should get here */
		xassert(0 > coll->prnt_peerid);
		if (coll->chldrn_cnt) {
			/* We can run on just one node */
			ep[ep_cnt].type = PMIXP_EP_HLIST;
			ep[ep_cnt].ep.hostlist = coll->chldrn_str;
			ep_cnt++;
		}
	} else {
		for(i=0; i<coll->chldrn_cnt; i++){
			ep[i].type = PMIXP_EP_NOIDEID;
			ep[i].ep.nodeid = coll->chldrn_ids[i];
			ep_cnt++;
		}
	}

	/* We need to wait for ep_cnt send completions + the local callback */
	coll->dfwd_cb_wait = ep_cnt;

	if (ep_cnt || coll->cbfunc) {
		/* allocate the callback data */
		cbdata = xmalloc(sizeof(pmixp_coll_cbdata_t));
		cbdata->coll = coll;
		cbdata->seq = coll->seq;
		cbdata->refcntr = ep_cnt;
		if (coll->cbfunc) {
			cbdata->refcntr++;
		}
	}

	for(i=0; i < ep_cnt; i++){
		rc = pmixp_server_send_nb(&ep[i], PMIXP_MSG_FAN_OUT, coll->seq,
					  coll->dfwd_buf,
					  _dfwd_sent_cb, cbdata);

		if (SLURM_SUCCESS != rc) {
			if (PMIXP_EP_NOIDEID == ep[i].type){
				nodename = pmixp_info_job_host(ep[i].ep.nodeid);
				PMIXP_ERROR("Cannot send data (size = %lu), "
				    "to %s:%d",
				    (uint64_t) get_buf_offset(coll->dfwd_buf),
				    nodename, ep[i].ep.nodeid);
				xfree(nodename);
			} else {
				PMIXP_ERROR("Cannot send data (size = %lu), "
				    "to %s",
				    (uint64_t) get_buf_offset(coll->dfwd_buf),
				    ep[i].ep.hostlist);
			}
			coll->dfwd_status = PMIXP_COLL_SND_FAILED;
		}
#ifdef PMIXP_COLL_DEBUG
		if (PMIXP_EP_NOIDEID == ep[i].type) {
			nodename = pmixp_info_job_host(ep[i].ep.nodeid);
			PMIXP_DEBUG("%p: fwd to %s:%d, size = %lu",
				    coll, nodename, ep[i].ep.nodeid,
				    (uint64_t) get_buf_offset(coll->dfwd_buf));
			xfree(nodename);
		} else {
			PMIXP_DEBUG("%p: fwd to %s, size = %lu",
				    coll, ep[i].ep.hostlist,
				    (uint64_t) get_buf_offset(coll->dfwd_buf));
		}
#endif
	}

	if (coll->cbfunc) {
		char *data = get_buf_data(coll->dfwd_buf) + coll->dfwd_offset;
		size_t size = get_buf_offset(coll->dfwd_buf) -
				coll->dfwd_offset;
		coll->dfwd_cb_wait++;
		coll->cbfunc(PMIX_SUCCESS, data, size, coll->cbdata,
			     _libpmix_cb, (void *)cbdata);
#ifdef PMIXP_COLL_DEBUG
		PMIXP_DEBUG("%p: local delivery, size = %lu",
			    coll, (uint64_t)size);
#endif
	}

	/* events observed - need another iteration */
	return true;
}

static int _progress_ufwd_sc(pmixp_coll_t *coll)
{
	xassert(PMIXP_COLL_UPFWD_WSC == coll->state);

	/* for some reasons doesnt switch to downfwd */
	switch (coll->ufwd_status) {
	case PMIXP_COLL_SND_FAILED:
		/* something went wrong with upward send.
		 * notify libpmix about that and abort
		 * collective */
		if (coll->cbfunc) {
			coll->cbfunc(PMIX_ERROR, NULL, 0, coll->cbdata,
				     NULL, NULL);
		}
		_reset_coll(coll);
		/* Don't need to do anything else */
		return false;
	case PMIXP_COLL_SND_ACTIVE:
		/* still waiting for the send completion */
		return false;
	case PMIXP_COLL_SND_DONE:
		/* move to the next step */
		break;
	default:
		/* Should not happen, fatal error */
		abort();
	}

	/* We now can upward part for the next collective */
	_reset_coll_ufwd(coll);

	/* move to the next state */
	coll->state = PMIXP_COLL_UPFWD_WPC;
	return true;
}

static int _progress_ufwd_wpc(pmixp_coll_t *coll)
{
	xassert(PMIXP_COLL_UPFWD_WPC == coll->state);

	if (!coll->contrib_prnt) {
		return false;
	}

	/* Need to wait only for the local completion callback if installed*/
	coll->dfwd_status = PMIXP_COLL_SND_ACTIVE;
	coll->dfwd_cb_wait = 0;


	/* move to the next state */
	coll->state = PMIXP_COLL_DOWNFWD;

	/* local delivery */
	if (coll->cbfunc) {
		pmixp_coll_cbdata_t *cbdata;
		cbdata = xmalloc(sizeof(pmixp_coll_cbdata_t));
		cbdata->coll = coll;
		cbdata->seq = coll->seq;
		cbdata->refcntr = 1;

		char *data = get_buf_data(coll->dfwd_buf) + coll->dfwd_offset;
		size_t size = get_buf_offset(coll->dfwd_buf) -
				coll->dfwd_offset;
		coll->cbfunc(PMIX_SUCCESS, data, size, coll->cbdata,
			     _libpmix_cb, (void *)cbdata);
		coll->dfwd_cb_wait++;
#ifdef PMIXP_COLL_DEBUG
		PMIXP_DEBUG("%p: local delivery, size = %lu",
			    coll, (uint64_t)size);
#endif
	}

	/* events observed - need another iteration */
	return true;
}

static int _progress_dfwd(pmixp_coll_t *coll)
{
	xassert(PMIXP_COLL_DOWNFWD == coll->state);

	/* if all childrens + local callbacks was invoked */
	if (coll->dfwd_cb_wait == coll->dfwd_cb_cnt) {
		coll->dfwd_status = PMIXP_COLL_SND_DONE;
	}

	switch (coll->dfwd_status) {
	case PMIXP_COLL_SND_ACTIVE:
		return false;
	case PMIXP_COLL_SND_FAILED:
		/* something went wrong with upward send.
		 * notify libpmix about that and abort
		 * collective */
		PMIXP_ERROR("%p: failed to send, abort collective", coll);
		if (coll->cbfunc) {
			coll->cbfunc(PMIX_ERROR, NULL, 0, coll->cbdata,
				     NULL, NULL);
		}
		_reset_coll(coll);
		/* Don't need to do anything else */
		return false;
	case PMIXP_COLL_SND_DONE:
		break;
	default:
		/* Should not happen, fatal error */
		abort();
	}
#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: collective is DONE", coll);
#endif
	_reset_coll(coll);

	return true;
}

static void _progress_coll(pmixp_coll_t *coll)
{
	int ret = 0;
	do {
		switch (coll->state) {
		case PMIXP_COLL_SYNC:
			/* check if any activity was observed */
			if (coll->contrib_local || coll->contrib_children) {
				coll->state = PMIXP_COLL_COLLECT;
				ret = true;
			} else {
				ret = false;
			}
			break;
		case PMIXP_COLL_COLLECT:
			ret = _progress_collect(coll);
			break;
		case PMIXP_COLL_UPFWD:
			ret = _progress_ufwd(coll);
			break;
		case PMIXP_COLL_UPFWD_WSC:
			ret = _progress_ufwd_sc(coll);
			break;
		case PMIXP_COLL_UPFWD_WPC:
			ret = _progress_ufwd_wpc(coll);
			break;
		case PMIXP_COLL_DOWNFWD:
			ret = _progress_dfwd(coll);
			break;
		default:
			PMIXP_ERROR("%p: unknown state = %d",
				    coll, coll->state);
		}
	} while(ret);
}

int pmixp_coll_contrib_local(pmixp_coll_t *coll, char *data, size_t size,
			     pmix_modex_cbfunc_t cbfunc, void *cbdata)
{
	int ret = SLURM_SUCCESS;

	pmixp_debug_hang(0);

	/* sanity check */
	pmixp_coll_sanity_check(coll);

	/* lock the structure */
	slurm_mutex_lock(&coll->lock);

#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: contrib/loc: seqnum=%u, state=%s, size=%zd",
		    coll, coll->seq, pmixp_coll_state2str(coll->state), size);
#endif

	switch (coll->state) {
	case PMIXP_COLL_SYNC:
		/* change the state */
		coll->ts = time(NULL);
		/* fall-thru */
	case PMIXP_COLL_COLLECT:
		/* sanity check */
		break;
	case PMIXP_COLL_DOWNFWD:
		/* We are waiting for some send requests
		 * to be finished, but local node has started
		 * the next contribution.
		 * This is an OK situation, go ahead and store
		 * it, the buffer with the contribution is not used
		 * now.
		 */
#ifdef PMIXP_COLL_DEBUG
		PMIXP_DEBUG("%p: contrib/loc: next coll!", coll);
#endif
		break;
	case PMIXP_COLL_UPFWD:
	case PMIXP_COLL_UPFWD_WSC:
	case PMIXP_COLL_UPFWD_WPC:
		/* this is not a correct behavior, respond with an error. */
#ifdef PMIXP_COLL_DEBUG
		PMIXP_DEBUG("%p: contrib/loc: before prev coll is finished!",
			    coll);
#endif
		ret = SLURM_ERROR;
		goto exit;
	default:
		/* FATAL: should not happen in normal workflow */
		PMIXP_ERROR("%p: local contrib while active collective, "
			    "state = %s",
			    coll, pmixp_coll_state2str(coll->state));
		xassert(0);
		abort();
	}

	if (coll->contrib_local) {
		/* Double contribution - reject */
		ret = SLURM_ERROR;
		goto exit;
	}

	/* save & mark local contribution */
	coll->contrib_local = true;
	pmixp_server_buf_reserve(coll->ufwd_buf, size);
	memcpy(get_buf_data(coll->ufwd_buf) + get_buf_offset(coll->ufwd_buf),
	       data, size);
	set_buf_offset(coll->ufwd_buf, get_buf_offset(coll->ufwd_buf) + size);

	/* setup callback info */
	coll->cbfunc = cbfunc;
	coll->cbdata = cbdata;

	/* check if the collective is ready to progress */
	_progress_coll(coll);

#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: finish, state=%s",
		    coll, pmixp_coll_state2str(coll->state));
#endif

exit:
	/* unlock the structure */
	slurm_mutex_unlock(&coll->lock);
	return ret;
}

static int _chld_id(pmixp_coll_t *coll, uint32_t nodeid)
{
	int i;
	for (i=0; i<coll->chldrn_cnt; i++) {
		if (coll->chldrn_ids[i] == nodeid) {
			return i;
		}
	}
	return -1;
}

static char *_chld_ids_str(pmixp_coll_t *coll)
{
	char *p = NULL;
	int i;

	for (i=0; i<coll->chldrn_cnt; i++) {
		if ((coll->chldrn_cnt-1) > i) {
			xstrfmtcat(p, "%d, ", coll->chldrn_ids[i]);
		} else {
			xstrfmtcat(p, "%d", coll->chldrn_ids[i]);
		}
	}
	return p;
}


int pmixp_coll_contrib_child(pmixp_coll_t *coll, uint32_t peerid,
			     uint32_t seq, Buf buf)
{
	char *data_src = NULL, *data_dst = NULL;
	uint32_t size;
	int chld_id;

	/* lock the structure */
	slurm_mutex_lock(&coll->lock);
	pmixp_coll_sanity_check(coll);
	if (0 > (chld_id = _chld_id(coll, peerid))) {
		char *nodename = pmixp_info_job_host(peerid);
		char *avail_ids = _chld_ids_str(coll);
		PMIXP_DEBUG("%p: contribution from the non-child node "
			    "%s:%d, acceptable ids: %s",
			    coll, nodename, peerid, avail_ids);
		xfree(nodename);
		xfree(avail_ids);
	}

#ifdef PMIXP_COLL_DEBUG
	char *nodename = pmixp_info_job_host(peerid);
	int lpeerid = hostlist_find(coll->peers_hl, nodename);
	PMIXP_DEBUG("%p: contrib/rem from %s:%d(%d:%d):, state=%s, size=%u",
		    coll, nodename, peerid, lpeerid, chld_id,
		    pmixp_coll_state2str(coll->state),
		    remaining_buf(buf));
#endif

	switch (coll->state) {
	case PMIXP_COLL_SYNC:
		/* change the state */
		coll->ts = time(NULL);
		/* fall-thru */
	case PMIXP_COLL_COLLECT:
		/* sanity check */
		if (coll->seq != seq) {
			char *nodename = pmixp_info_job_host(peerid);
			/* FATAL: should not happen in normal workflow */
			PMIXP_ERROR("%p: unexpected contrib from %s:%d "
				    "(child #%d) seq = %d, coll->seq = %d, "
				    "state=%s",
				    coll, nodename, peerid, chld_id,
				    seq, coll->seq,
				    pmixp_coll_state2str(coll->state));
			xassert(coll->seq == seq);
			abort();
		}
		break;
	case PMIXP_COLL_UPFWD:
	case PMIXP_COLL_UPFWD_WSC:
		/* FATAL: should not happen in normal workflow */
		PMIXP_ERROR("%p: unexpected contrib from %s:%d, state = %s",
			    coll, nodename, peerid,
			    pmixp_coll_state2str(coll->state));
		xassert(0);
		abort();
	case PMIXP_COLL_UPFWD_WPC:
	case PMIXP_COLL_DOWNFWD:
#ifdef PMIXP_COLL_DEBUG
		/* It looks like a retransmission attempt when remote side
		 * identified transmission failure, but we actually successfuly
		 * received the message */
		PMIXP_DEBUG("%p: contrib for the next collective "
			    "from=%s:%d(%d:%d) contrib_seq=%u, coll->seq=%u, "
			    "state=%s",
			    coll, nodename, peerid, lpeerid, chld_id,
			    seq, coll->seq, pmixp_coll_state2str(coll->state));
#endif
		if ((coll->seq +1) != seq) {
			char *nodename = pmixp_info_job_host(peerid);
			/* should not happen in normal workflow */
			PMIXP_ERROR("%p: unexpected contrib from %s:%d(x:%d) "
				    "seq = %d, coll->seq = %d, "
				    "state=%s",
				    coll, nodename, peerid, chld_id,
				    seq, coll->seq,
				    pmixp_coll_state2str(coll->state));
			xfree(nodename);
			xassert((coll->seq +1) == seq);
			abort();
		}
		break;
	default:
		/* should not happen in normal workflow */
		PMIXP_ERROR("%p: unknown collective state %s",
			    coll, pmixp_coll_state2str(coll->state));
		abort();
	}

	/* Because of possible timeouts/delays in transmission we
	 * can receive a contribution second time. Avoid duplications
	 * by checking our records. */
	if (coll->contrib_chld[chld_id]) {
		char *nodename = pmixp_info_job_host(peerid);
		/* May be 0 or 1. If grater - transmission skew, ignore.
		 * NOTE: this output is not on the critical path -
		 * don't preprocess it out */
		PMIXP_DEBUG("%p: multiple contribs from %s:%d(x:%d)",
			    coll, nodename, peerid, chld_id);
		/* this is duplication, skip. */
		xfree(nodename);
		goto proceed;
	}

	data_src = get_buf_data(buf) + get_buf_offset(buf);
	size = remaining_buf(buf);
	pmixp_server_buf_reserve(coll->ufwd_buf, size);
	data_dst = get_buf_data(coll->ufwd_buf) +
			get_buf_offset(coll->ufwd_buf);
	memcpy(data_dst, data_src, size);
	set_buf_offset(coll->ufwd_buf, get_buf_offset(coll->ufwd_buf) + size);

	/* increase number of individual contributions */
	coll->contrib_chld[chld_id] = true;
	/* increase number of total contributions */
	coll->contrib_children++;

proceed:
	_progress_coll(coll);

#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: finish: node=%s:%d(%d:%d), state=%s",
		    coll, nodename, peerid, lpeerid, chld_id,
		    pmixp_coll_state2str(coll->state));
	xfree(nodename);
#endif
	/* unlock the structure */
	slurm_mutex_unlock(&coll->lock);

	return SLURM_SUCCESS;
}

int pmixp_coll_contrib_parent(pmixp_coll_t *coll, uint32_t peerid,
			     uint32_t seq, Buf buf)
{
#ifdef PMIXP_COLL_DEBUG
	char *nodename = NULL;
	int lpeerid = -1;
#endif
	char *data_src = NULL, *data_dst = NULL;
	uint32_t size;
	int expected_peerid;

	/* lock the structure */
	slurm_mutex_lock(&coll->lock);

	if (pmixp_info_srv_direct_conn()) {
		expected_peerid = coll->prnt_peerid;
	} else {
		expected_peerid = coll->root_peerid;
	}

	/* Sanity check */
	pmixp_coll_sanity_check(coll);
	if (expected_peerid != peerid) {
		char *nodename = pmixp_info_job_host(peerid);
		/* protect ourselfs if we are running with no asserts */
		PMIXP_ERROR("%p: parent contrib from bad nodeid=%s:%u, "
			    "expect=%d",
			    coll, nodename, peerid, expected_peerid);
		xfree(nodename);
		goto proceed;
	}

#ifdef PMIXP_COLL_DEBUG
	nodename = pmixp_info_job_host(peerid);
	lpeerid = hostlist_find(coll->peers_hl, nodename);
	/* Mark this event */
	PMIXP_DEBUG("%p: contrib/rem from %s:%d(%d): state=%s, size=%u",
		    coll, nodename, peerid, lpeerid,
		    pmixp_coll_state2str(coll->state), remaining_buf(buf));
#endif

	switch (coll->state) {
	case PMIXP_COLL_SYNC:
	case PMIXP_COLL_COLLECT:
		/* It looks like a retransmission attempt when remote side
		 * identified transmission failure, but we actually successfuly
		 * received the message */
#ifdef PMIXP_COLL_DEBUG
		PMIXP_DEBUG("%p: prev contrib from %s:%d(%d): "
			    "seq=%u, cur_seq=%u, state=%s",
			    coll, nodename, peerid, lpeerid,
			    seq, coll->seq,
			    pmixp_coll_state2str(coll->state));
#endif
		/* sanity check */
		if ((coll->seq - 1) != seq) {
			/* FATAL: should not happen in normal workflow */
			char *nodename = pmixp_info_job_host(peerid);
			PMIXP_ERROR("%p: unexpected contrib from %s:%d: "
				    "contrib_seq = %d, coll->seq = %d, "
				    "state=%s",
				    coll, nodename, peerid,
				    seq, coll->seq,
				    pmixp_coll_state2str(coll->state));
			xfree(nodename);
			xassert((coll->seq - 1) == seq);
			abort();
		}
		goto proceed;
	case PMIXP_COLL_UPFWD_WSC:{
		/* we are not actually ready to receive this contribution as
		 * the upward portion of the collective wasn't received yet.
		 * This should not happen as SAPI (SLURM API) is blocking and
		 * we chould transit to PMIXP_COLL_UPFWD_WPC immediately */
		/* FATAL: should not happen in normal workflow */
		char *nodename = pmixp_info_job_host(peerid);
		PMIXP_ERROR("%p: unexpected contrib from %s:%d: "
			    "contrib_seq = %d, coll->seq = %d, "
			    "state=%s",
			    coll, nodename, peerid,
			    seq, coll->seq,
			    pmixp_coll_state2str(coll->state));
		xfree(nodename);
		xassert((coll->seq - 1) == seq);
		abort();
	}
	case PMIXP_COLL_UPFWD:
	case PMIXP_COLL_UPFWD_WPC:
		/* we were waiting for this */
		break;
	case PMIXP_COLL_DOWNFWD:
		/* It looks like a retransmission attempt when remote side
		 * identified transmission failure, but we actually successfuly
		 * received the message */
#ifdef PMIXP_COLL_DEBUG
		PMIXP_DEBUG("%p: double contrib from %s:%d(%d) "
			    "seq=%u, cur_seq=%u, state=%s",
			    coll, nodename, peerid, lpeerid,
			    seq, coll->seq, pmixp_coll_state2str(coll->state));
#endif
		/* sanity check */
		if (coll->seq != seq) {
			char *nodename = pmixp_info_job_host(peerid);
			/* FATAL: should not happen in normal workflow */
			PMIXP_ERROR("%p: unexpected contrib from %s:%d: "
				    "seq = %d, coll->seq = %d, state=%s",
				    coll, nodename, peerid,
				    seq, coll->seq,
				    pmixp_coll_state2str(coll->state));
			xassert((coll->seq - 1) == seq);
			xfree(nodename);
			abort();
		}
		goto proceed;
	default:
		/* should not happen in normal workflow */
		PMIXP_ERROR("%p: unknown collective state %s",
			    coll, pmixp_coll_state2str(coll->state));
		abort();
	}

	/* Because of possible timeouts/delays in transmission we
	 * can receive a contribution second time. Avoid duplications
	 * by checking our records. */
	if (coll->contrib_prnt) {
		char *nodename = pmixp_info_job_host(peerid);
		/* May be 0 or 1. If grater - transmission skew, ignore.
		 * NOTE: this output is not on the critical path -
		 * don't preprocess it out */
		PMIXP_DEBUG("%p: multiple contributions from parent %s:%d",
			    coll, nodename, peerid);
		xfree(nodename);
		/* this is duplication, skip. */
		goto proceed;
	}
	coll->contrib_prnt = true;

	data_src = get_buf_data(buf) + get_buf_offset(buf);
	size = remaining_buf(buf);
	pmixp_server_buf_reserve(coll->dfwd_buf, size);

	data_dst = get_buf_data(coll->dfwd_buf) +
			get_buf_offset(coll->dfwd_buf);
	memcpy(data_dst, data_src, size);
	set_buf_offset(coll->dfwd_buf,
		       get_buf_offset(coll->dfwd_buf) + size);
proceed:
	_progress_coll(coll);

#ifdef PMIXP_COLL_DEBUG
	if (nodename) {
		PMIXP_DEBUG("%p: finish: node=%s:%d(%d), state=%s",
			    coll, nodename, peerid, lpeerid,
			    pmixp_coll_state2str(coll->state));
		xfree(nodename);
	}
#endif
	/* unlock the structure */
	slurm_mutex_unlock(&coll->lock);

	return SLURM_SUCCESS;
}

void pmixp_coll_reset_if_to(pmixp_coll_t *coll, time_t ts)
{
	/* lock the */
	slurm_mutex_lock(&coll->lock);

	if (PMIXP_COLL_SYNC == coll->state) {
		goto unlock;
	}

	if (ts - coll->ts > pmixp_info_timeout()) {
		/* respond to the libpmix */
		if (coll->contrib_local && coll->cbfunc) {
			/* Call the callback only if:
			 * - we were asked to do that (coll->cbfunc != NULL);
			 * - local contribution was received.
			 * TODO: we may want to mark this event to respond with
			 * to the next local request immediately and with the
			 * proper (status == PMIX_ERR_TIMEOUT)
			 */
			coll->cbfunc(PMIX_ERR_TIMEOUT, NULL, 0, coll->cbdata,
				     NULL, NULL);
		}
		/* drop the collective */
		_reset_coll(coll);
		/* report the timeout event */
		PMIXP_ERROR("Collective timeout!");
	}
unlock:
	/* unlock the structure */
	slurm_mutex_unlock(&coll->lock);
}
