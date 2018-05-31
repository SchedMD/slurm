/*****************************************************************************\
 **  pmix_coll_ring.c - PMIx collective primitives
 *****************************************************************************
 *  Copyright (C) 2018      Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>,
 *             Boris Karasev <karasev.b@gmail.com, boriska@mellanox.com>.
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
#include "src/common/slurm_protocol_api.h"
#include "pmixp_coll.h"
#include "pmixp_nspaces.h"
#include "pmixp_server.h"
#include "pmixp_client.h"

typedef struct {
	pmixp_coll_t *coll;
	pmixp_coll_ring_ctx_t *coll_ctx;
	Buf buf;
} pmixp_coll_ring_cbdata_t;

static inline int _ring_prev_id(pmixp_coll_t *coll)
{
	return (coll->my_peerid + coll->peers_cnt - 1) % coll->peers_cnt;
}

static inline int _ring_next_id(pmixp_coll_t *coll)
{
	return (coll->my_peerid + 1) % coll->peers_cnt;
}

static inline pmixp_coll_t *_ctx_get_coll(pmixp_coll_ring_ctx_t *coll_ctx)
{
	return coll_ctx->coll;
}

static inline pmixp_coll_ring_t *_ctx_get_coll_ring(
		pmixp_coll_ring_ctx_t *coll_ctx)
{
	return &coll_ctx->coll->state.ring;
}

static inline uint32_t _ring_remain_contrib(pmixp_coll_ring_ctx_t *coll_ctx)
{
	return coll_ctx->coll->peers_cnt -
			(coll_ctx->contrib_prev + coll_ctx->contrib_local);
}

static inline uint32_t _ring_fwd_done(pmixp_coll_ring_ctx_t *coll_ctx)
{
	return !(coll_ctx->coll->peers_cnt - coll_ctx->forward_cnt - 1);
}

static void _ring_sent_cb(int rc, pmixp_p2p_ctx_t ctx, void *_cbdata)
{
	pmixp_coll_ring_cbdata_t *cbdata = (pmixp_coll_ring_cbdata_t*)_cbdata;
	pmixp_coll_ring_ctx_t *coll_ctx = cbdata->coll_ctx;
	pmixp_coll_ring_t *ring = _ctx_get_coll_ring(coll_ctx);

	if (PMIXP_P2P_REGULAR == ctx) {
		/* lock the collective */
		slurm_mutex_lock(&_ctx_get_coll(coll_ctx)->lock);
	}

	PMIXP_DEBUG("called %d", coll_ctx->seq);

	pmixp_coll_sanity_check(coll_ctx->coll);
	pmixp_server_buf_reset(cbdata->buf);
	list_push(ring->fwrd_buf_pool, cbdata->buf);
	coll_ctx->forward_cnt++;

	pmixp_coll_ring_progress(coll_ctx);

	if (PMIXP_P2P_REGULAR == ctx) {
		/* unlock the collective */
		slurm_mutex_unlock(&_ctx_get_coll(coll_ctx)->lock);
	}

	xfree(cbdata);
}

static inline void pmixp_coll_ring_ctx_sanity_check(pmixp_coll_ring_ctx_t *coll_ctx)
{
	xassert(NULL != coll_ctx);
	xassert(coll_ctx->in_use);
	pmixp_coll_sanity_check(coll_ctx->coll);
}

/*
 * use it for internal collective
 * performance evaluation tool.
 */
pmixp_coll_t *pmixp_coll_ring_from_cbdata(void *cbdata)
{
	pmixp_coll_ring_cbdata_t *ptr = (pmixp_coll_ring_cbdata_t*)cbdata;
	pmixp_coll_sanity_check(ptr->coll_ctx->coll);
	return ptr->coll_ctx->coll;
}

int pmixp_coll_ring_unpack_info(Buf buf, pmixp_coll_type_t *type,
				pmixp_coll_ring_msg_hdr_t *ring_hdr,
				pmixp_proc_t **r, size_t *nr)
{
	pmixp_proc_t *procs = NULL;
	uint32_t nprocs = 0;
	uint32_t tmp;
	int rc, i;

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

	procs = xmalloc(sizeof(pmixp_proc_t) * nprocs);
	*r = procs;

	/* 3. get namespace/rank of particular process */
	for (i = 0; i < (int)nprocs; i++) {
		rc = unpackmem(procs[i].nspace, &tmp, buf);
		if (SLURM_SUCCESS != rc) {
			PMIXP_ERROR("Cannot unpack namespace for process #%d", i);
			return rc;
		}
		procs[i].nspace[tmp] = '\0';

		rc = unpack32(&tmp, buf);
		procs[i].rank = tmp;
		if (SLURM_SUCCESS != rc) {
			PMIXP_ERROR("Cannot unpack ranks for process #%d, nsp=%s",
				    i, procs[i].nspace);
			return rc;
		}
	}

	/* 4. extract the ring info */
	if (SLURM_SUCCESS != (rc = unpackmem((char *)ring_hdr, &tmp, buf))) {
		PMIXP_ERROR("Cannot unpack ring info");
		return rc;
	}

	return SLURM_SUCCESS;
}

static int _pack_coll_ring_info(pmixp_coll_t *coll,
				pmixp_coll_ring_msg_hdr_t *ring_hdr,
				Buf buf)
{
	pmixp_proc_t *procs = coll->pset.procs;
	size_t nprocs = coll->pset.nprocs;
	uint32_t type = PMIXP_COLL_TYPE_FENCE_RING;
	int i;

	/* 1. store the type of collective */
	pack32(type, buf);

	/* 2. Put the number of ranges */
	pack32(nprocs, buf);
	for (i = 0; i < (int)nprocs; i++) {
		/* Pack namespace */
		packmem(procs->nspace, strlen(procs->nspace) + 1, buf);
		pack32(procs->rank, buf);
	}

	/* 3. pack the ring header info */
	packmem((char*)ring_hdr, sizeof(pmixp_coll_ring_msg_hdr_t), buf);

	return SLURM_SUCCESS;
}

static Buf _get_fwd_buf(pmixp_coll_ring_ctx_t *coll_ctx)
{
	pmixp_coll_ring_t *ring = _ctx_get_coll_ring(coll_ctx);
	Buf buf = list_pop(ring->fwrd_buf_pool);
	if (!buf) {
		buf = pmixp_server_buf_new();
	}
	return buf;
}

static Buf _get_contrib_buf(pmixp_coll_ring_ctx_t *coll_ctx)
{
	pmixp_coll_ring_t *ring = _ctx_get_coll_ring(coll_ctx);
	Buf ring_buf = list_pop(ring->ring_buf_pool);
	if (!ring_buf) {
		ring_buf = pmixp_server_buf_new();
	}
	return ring_buf;
}

static int _ring_forward_data(pmixp_coll_ring_ctx_t *coll_ctx, uint32_t contrib_id,
			       uint32_t hop_seq, void *data, size_t size)
{
	pmixp_coll_ring_msg_hdr_t hdr;
	pmixp_coll_t *coll = _ctx_get_coll(coll_ctx);
	int next_nodeid = _ring_next_id(coll);
	hdr.nodeid = coll->my_peerid;
	hdr.msgsize = size;
	hdr.seq = coll_ctx->seq;
	hdr.hop_seq = hop_seq;
	hdr.contrib_id = contrib_id;
	pmixp_ep_t *ep = (pmixp_ep_t*)xmalloc(sizeof(*ep));
	pmixp_coll_ring_cbdata_t *cbdata = NULL;
	uint32_t offset = 0;
	Buf buf = _get_fwd_buf(coll_ctx);
	int rc = SLURM_SUCCESS;

	pmixp_coll_ring_ctx_sanity_check(coll_ctx);

#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: transit data to nodeid=%d, seq=%d, hop=%d, size=%lu, contrib=%d",
		    coll_ctx, next_nodeid, hdr.seq, hdr.hop_seq, hdr.msgsize, hdr.contrib_id);
#endif
	if (!buf) {
		rc = SLURM_ERROR;
		goto exit;
	}
	ep->type = PMIXP_EP_NOIDEID;
	ep->ep.nodeid = next_nodeid;

	/* pack ring info */
	_pack_coll_ring_info(coll, &hdr, buf);

	/* insert payload to buf */
	offset = get_buf_offset(buf);
	pmixp_server_buf_reserve(buf, size);
	memcpy(get_buf_data(buf) + offset, data, size);
	set_buf_offset(buf, offset + size);

	cbdata = xmalloc(sizeof(pmixp_coll_ring_cbdata_t));
	cbdata->buf = buf;
	cbdata->coll_ctx = coll_ctx;
	rc = pmixp_server_send_nb(ep, PMIXP_MSG_RING, coll_ctx->seq, buf,
				  _ring_sent_cb, cbdata);
exit:
	return rc;
}

static void _reset_coll_ring(pmixp_coll_ring_ctx_t *coll_ctx)
{
	pmixp_coll_t *coll = _ctx_get_coll(coll_ctx);
#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: called", coll_ctx);
#endif
	pmixp_coll_ring_ctx_sanity_check(coll_ctx);
	coll_ctx->in_use = false;
	coll_ctx->state = PMIXP_COLL_RING_SYNC;
	coll_ctx->contrib_local = false;
	coll_ctx->contrib_prev = 0;
	coll_ctx->forward_cnt = 0;
	coll->ts = time(NULL);
	memset(coll_ctx->contrib_map, 0, sizeof(bool) * coll->peers_cnt);
	coll_ctx->ring_buf = NULL;
}

static void _libpmix_cb(void *_vcbdata)
{
	pmixp_coll_ring_cbdata_t *cbdata = (pmixp_coll_ring_cbdata_t*)_vcbdata;
	pmixp_coll_t *coll = cbdata->coll;
	Buf buf = cbdata->buf;

	/* lock the structure */
	slurm_mutex_lock(&coll->lock);

	list_push(coll->state.ring.ring_buf_pool, buf);

	/* unlock the structure */
	slurm_mutex_unlock(&coll->lock);

	xfree(cbdata);
}

void pmixp_coll_ring_progress(pmixp_coll_ring_ctx_t *coll_ctx)
{
	int ret = 0;
	pmixp_coll_t *coll = _ctx_get_coll(coll_ctx);

	pmixp_coll_ring_ctx_sanity_check(coll_ctx);
	assert(coll_ctx->in_use);

	do {
		ret = false;
		switch(coll_ctx->state) {
		case PMIXP_COLL_RING_SYNC:
			if (coll_ctx->contrib_local || coll_ctx->contrib_prev) {
				coll_ctx->state = PMIXP_COLL_RING_PROGRESS;
				ret = true;
			}
			break;
		case PMIXP_COLL_RING_PROGRESS:
			/* check for all data is collected and forwarded */
			if (!_ring_remain_contrib(coll_ctx) && _ring_fwd_done(coll_ctx)) {
				coll_ctx->state = PMIXP_COLL_RING_FINILIZE;
				ret = true;
			}
			break;
		case PMIXP_COLL_RING_FINILIZE:
			if (coll->cbfunc) {
				pmixp_coll_ring_cbdata_t *cbdata;

				cbdata = xmalloc(sizeof(pmixp_coll_ring_cbdata_t));
				cbdata->coll = coll;
				cbdata->coll_ctx = coll_ctx;
				cbdata->buf = coll_ctx->ring_buf;
				pmixp_lib_modex_invoke(coll->cbfunc, SLURM_SUCCESS,
						       get_buf_data(coll_ctx->ring_buf),
						       get_buf_offset(coll_ctx->ring_buf),
						       coll->cbdata, _libpmix_cb, (void *)cbdata);
			}
#ifdef PMIXP_COLL_DEBUG
			PMIXP_ERROR("%p: %s seq=%d is DONE", coll,
				    pmixp_coll_type2str(coll->type), coll_ctx->seq);
#endif
			coll->seq++;
			_reset_coll_ring(coll_ctx);
			break;
		default:
			PMIXP_ERROR("%p: unknown state = %d",
				    coll_ctx, (int)coll_ctx->state);
		}
	} while(ret);
}

static void _free_from_buf_pool(void *p)
{
	Buf buf = (Buf)p;
	free_buf(buf);
}

pmixp_coll_ring_ctx_t *pmixp_coll_ring_ctx_select(pmixp_coll_t *coll,
						 const uint32_t seq)
{
	int i;
	pmixp_coll_ring_ctx_t *coll_ctx = NULL, *ret = NULL;
	pmixp_coll_ring_t *ring = &coll->state.ring;

	/* finding the appropriate ring context */
	for (i = 0; i < PMIXP_COLL_RING_CTX_NUM; i++) {
		coll_ctx = &ring->ctx_array[i];
		if (coll_ctx->in_use && coll_ctx->seq == seq) {
			return coll_ctx;
		} else if (!coll_ctx->in_use) {
			ret = coll_ctx;
			continue;
		}
	}
	/* add this context to use */
	if (ret && !ret->in_use) {
		ret->in_use = true;
		ret->seq = seq;
		ret->ring_buf = _get_contrib_buf(ret);
	}
	return ret;
}

int pmixp_coll_ring_init(pmixp_coll_t *coll)
{
	PMIXP_DEBUG("called");
	int i;
	pmixp_coll_ring_ctx_t *coll_ctx = NULL;
	pmixp_coll_ring_t *ring = &coll->state.ring;

	ring->fwrd_buf_pool = list_create(_free_from_buf_pool);
	ring->ring_buf_pool = list_create(_free_from_buf_pool);

	for (i = 0; i < PMIXP_COLL_RING_CTX_NUM; i++) {
		coll_ctx = &ring->ctx_array[i];
		coll_ctx->coll = coll;
		coll_ctx->in_use = false;
		coll_ctx->seq = coll->seq;
		coll_ctx->contrib_local = false;
		coll_ctx->contrib_prev = 0;
		//coll_ctx->ring_buf = create_buf(NULL, 0);
		coll_ctx->state = PMIXP_COLL_RING_SYNC;
		coll_ctx->contrib_map = xmalloc(sizeof(bool) * coll->peers_cnt); // TODO bit vector
		memset(coll_ctx->contrib_map, 0, sizeof(bool) * coll->peers_cnt);
	}

	return SLURM_SUCCESS;
}

void pmixp_coll_ring_free(pmixp_coll_ring_t *ring)
{
	int i;

	pmixp_coll_ring_ctx_t *coll_ctx;
	for (i = 0; i < PMIXP_COLL_RING_CTX_NUM; i++) {
		coll_ctx = &ring->ctx_array[i];
		free_buf(coll_ctx->ring_buf);
		xfree(coll_ctx->contrib_map);
	}
	list_destroy(ring->fwrd_buf_pool);
	list_destroy(ring->ring_buf_pool);
}

int pmixp_coll_ring_contrib_local(pmixp_coll_t *coll, char *data, size_t size,
				  void *cbfunc, void *cbdata)
{
	int ret = SLURM_SUCCESS;
	pmixp_coll_ring_ctx_t *coll_ctx = NULL;

	PMIXP_DEBUG("called");

	/* lock the structure */
	slurm_mutex_lock(&coll->lock);

	/* sanity check */
	pmixp_coll_sanity_check(coll);

	/* setup callback info */
	coll->cbfunc = cbfunc;
	coll->cbdata = cbdata;

	coll_ctx = pmixp_coll_ring_ctx_select(coll, coll->seq);
	if (!coll_ctx) {
		PMIXP_ERROR("Can not get ring collective context, "
			    "seq=%u", coll->seq);
		ret = SLURM_ERROR;
		goto exit;
	}

#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: contrib/loc: seqnum=%u, state=%d, size=%lu",
		    coll_ctx, coll_ctx->seq, coll_ctx->state, size);
#endif
	/* change the state */
	coll->ts = time(NULL);

	/* save local contribution */
	if (!size_buf(coll_ctx->ring_buf)) {
		pmixp_server_buf_reserve(coll_ctx->ring_buf, size * coll->peers_cnt);
	} else if(remaining_buf(coll_ctx->ring_buf) < size) {
		int remaining_peers = coll->peers_cnt + 1 -
				(coll_ctx->contrib_local + coll_ctx->contrib_prev);
		size_t new_size = size_buf(coll_ctx->ring_buf) + size * remaining_peers;
		pmixp_server_buf_reserve(coll_ctx->ring_buf, new_size);
	}
	memcpy(get_buf_data(coll_ctx->ring_buf) + get_buf_offset(coll_ctx->ring_buf),
	       data, size);
	set_buf_offset(coll_ctx->ring_buf, get_buf_offset(coll_ctx->ring_buf) + size);

	/* mark local contribution */
	coll_ctx->contrib_local = true;

	/* check for a single-node case */
	if (coll->my_peerid != _ring_next_id(coll)) {
		/* forward data to the next node */
		ret = _ring_forward_data(coll_ctx, coll->my_peerid, 0,
					 data, size);
	}
	if (ret) {
		PMIXP_ERROR("Can not forward ring data, seq=%u", coll_ctx->seq);
		goto exit;
	}
	pmixp_coll_ring_progress(coll_ctx);

exit:
	/* unlock the structure */
	slurm_mutex_unlock(&coll->lock);

	return ret;
}

int pmixp_coll_ring_hdr_sanity_check(pmixp_coll_t *coll, pmixp_coll_ring_msg_hdr_t *hdr)
{
	if (hdr->nodeid != _ring_prev_id(coll)) {
		return SLURM_ERROR;
	}
	if (hdr->seq < coll->seq) {
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

int pmixp_coll_ring_contrib_nbr(pmixp_coll_t *coll, pmixp_coll_ring_msg_hdr_t *hdr,
				 Buf buf)
{
	int ret = SLURM_SUCCESS;
	size_t size = hdr->msgsize;
	char *data_src = NULL, *data_dst = NULL;
	pmixp_coll_ring_ctx_t *coll_ctx = NULL;
	uint32_t hop_seq;

	/* lock the structure */
	slurm_mutex_lock(&coll->lock);

	coll_ctx = pmixp_coll_ring_ctx_select(coll, hdr->seq);
	if (!coll_ctx) {
		PMIXP_ERROR("Can not get ring collective context, "
			    "seq=%u", hdr->seq);
		ret = SLURM_ERROR;
		goto exit;
	}
#ifdef PMIXP_COLL_DEBUG
	PMIXP_DEBUG("%p: contrib/prev: seqnum=%u, state=%d, nodeid=%d, contrib=%d, "
		    "seq=%d, size=%lu",
		    coll_ctx, coll_ctx->seq, coll_ctx->state, hdr->nodeid,
		    hdr->contrib_id, hdr->hop_seq, size);
#endif

	/* change the state */
	coll->ts = time(NULL);

	/* verify msg size */
	if (hdr->msgsize != remaining_buf(buf)) {
		PMIXP_DEBUG("%p: unexpected message size=%d, expect=%zd",
			    coll, remaining_buf(buf), hdr->msgsize);
		goto exit;
	}

	/* compute the actual hops of ring: (src - dst + size) % size */
	hop_seq = (coll->my_peerid + coll->peers_cnt - hdr->contrib_id) % coll->peers_cnt - 1;
	if (hdr->hop_seq != hop_seq) {
		PMIXP_DEBUG("%p: unexpected ring seq number=%d, expect=%d, coll seq=%d",
			    coll, hdr->hop_seq, hop_seq, coll->seq);
		goto exit;
	}

	if (hdr->contrib_id >= coll->peers_cnt) {
		goto exit;
	}

	if (coll_ctx->contrib_map[hdr->contrib_id]) {
#ifdef PMIXP_COLL_DEBUG
		PMIXP_DEBUG("%p: double receiving was detected from %d, "
			    "local seq=%d, seq=%d, rejected",
			    coll, hdr->contrib_id, coll->seq, hdr->seq);
#endif
		goto exit;
	}

	/* mark number of individual contributions */
	coll_ctx->contrib_map[hdr->contrib_id] = true;

	/* save contribution */
	if (!size_buf(coll_ctx->ring_buf)) {
		pmixp_server_buf_reserve(coll_ctx->ring_buf, size * coll->peers_cnt);
	} else if(remaining_buf(coll_ctx->ring_buf) < size) {
		/* grow sbuf size to 15% */
		size_t new_size = size_buf(coll_ctx->ring_buf) * 0.15 + size_buf(coll_ctx->ring_buf);
		pmixp_server_buf_reserve(coll_ctx->ring_buf, new_size);
	}
	data_src = get_buf_data(buf) + get_buf_offset(buf);
	pmixp_server_buf_reserve(coll_ctx->ring_buf, size);
	data_dst = get_buf_data(coll_ctx->ring_buf) +
			get_buf_offset(coll_ctx->ring_buf);
	memcpy(data_dst, data_src, size);
	set_buf_offset(coll_ctx->ring_buf, get_buf_offset(coll_ctx->ring_buf) + size);

	/* check for ring is complete */
	if (hdr->contrib_id != _ring_next_id(coll)) {
		/* forward data to the next node */
		ret = _ring_forward_data(coll_ctx, hdr->contrib_id, hdr->hop_seq +1,
					 data_dst, size);
		if (ret) {
			PMIXP_ERROR("Cannot forward ring data");
			goto exit;
		}
	}
	/* increase number of ring contributions */
	coll_ctx->contrib_prev++;

	/* ring coll progress */
	pmixp_coll_ring_progress(coll_ctx);
exit:
	/* unlock the structure */
	slurm_mutex_unlock(&coll->lock);
	return ret;
}

void pmixp_coll_ring_reset_if_to(pmixp_coll_t *coll, time_t ts) {
	pmixp_coll_ring_ctx_t *coll_ctx;
	int i;

	/* lock the structure */
	slurm_mutex_lock(&coll->lock);
	for (i = 0; i < PMIXP_COLL_RING_CTX_NUM; i++) {
		coll_ctx = &coll->state.ring.ctx_array[i];
		if (!coll_ctx->in_use || (PMIXP_COLL_RING_SYNC == coll_ctx->state)) {
			continue;
		}
		if (ts - coll->ts > pmixp_info_timeout()) {
			/* respond to the libpmix */
			if (coll_ctx->contrib_local && coll->cbfunc) {
				pmixp_lib_modex_invoke(coll->cbfunc, PMIXP_ERR_TIMEOUT, NULL,
						       0, coll->cbdata, NULL, NULL);
			}
			/* drop the collective */
			_reset_coll_ring(coll_ctx);
			/* report the timeout event */
			PMIXP_ERROR("Collective timeout!");
		}
	}
	/* unlock the structure */
	slurm_mutex_unlock(&coll->lock);
}
