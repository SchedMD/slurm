/*****************************************************************************\
 **  pmix_coll.h - PMIx collective primitives
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>,
 *             Boris Karasev <karasev.b@gmail.com, boriska@mellanox.com>.
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

#ifndef PMIXP_COLL_H
#define PMIXP_COLL_H
#include "pmixp_common.h"
#include "pmixp_debug.h"

#define PMIXP_COLL_DEBUG 1
#define PMIXP_COLL_RING_CTX_NUM 3

typedef enum {
	PMIXP_COLL_TYPE_FENCE_TREE = 0,
	PMIXP_COLL_TYPE_FENCE_RING,
	/* reserve coll fence ids up to 15 */
	PMIXP_COLL_TYPE_FENCE_MAX = 15,

	PMIXP_COLL_TYPE_CONNECT,
	PMIXP_COLL_TYPE_DISCONNECT
} pmixp_coll_type_t;

inline static char *
pmixp_coll_type2str(pmixp_coll_type_t type) {
	switch(type) {
	case PMIXP_COLL_TYPE_FENCE_TREE:
		return "COLL_FENCE_TREE";
	case PMIXP_COLL_TYPE_FENCE_RING:
		return "COLL_FENCE_RING";
	case PMIXP_COLL_TYPE_FENCE_MAX:
		return "COLL_FENCE_MAX";
	default:
		return "COLL_FENCE_UNK";
	}
}

typedef enum {
	PMIXP_COLL_CPERF_TREE = PMIXP_COLL_TYPE_FENCE_TREE,
	PMIXP_COLL_CPERF_RING = PMIXP_COLL_TYPE_FENCE_RING,
	PMIXP_COLL_CPERF_MIXED = PMIXP_COLL_TYPE_FENCE_MAX,
	PMIXP_COLL_CPERF_BARRIER
} pmixp_coll_cperf_mode_t;

inline static char *
pmixp_coll_cperf_mode2str(pmixp_coll_cperf_mode_t mode) {
	switch(mode) {
	case PMIXP_COLL_CPERF_RING:
		return "PMIXP_COLL_CPERF_RING";
	case PMIXP_COLL_CPERF_TREE:
		return "PMIXP_COLL_CPERF_TREE";
	case PMIXP_COLL_CPERF_MIXED:
		return "PMIXP_COLL_CPERF_MIXED";
	case PMIXP_COLL_CPERF_BARRIER:
		return "PMIXP_COLL_CPERF_BARRIER";
	default:
		return "PMIXP_COLL_CPERF_UNK";
	}
}

int pmixp_hostset_from_ranges(const pmixp_proc_t *procs, size_t nprocs,
			      hostlist_t *hl_out);

/* PMIx Tree collective */
typedef enum {
	PMIXP_COLL_TREE_SYNC,
	PMIXP_COLL_TREE_COLLECT,
	PMIXP_COLL_TREE_UPFWD,
	PMIXP_COLL_TREE_UPFWD_WSC, /* Wait for the upward Send Complete */
	PMIXP_COLL_TREE_UPFWD_WPC, /* Wait for Parent Contrib */
	PMIXP_COLL_TREE_DOWNFWD,
} pmixp_coll_tree_state_t;

inline static char *
pmixp_coll_tree_state2str(pmixp_coll_tree_state_t state)
{
	switch (state) {
	case PMIXP_COLL_TREE_SYNC:
		return "COLL_SYNC";
	case PMIXP_COLL_TREE_COLLECT:
		return "COLL_COLLECT";
	case PMIXP_COLL_TREE_UPFWD:
		return "COLL_UPFWD";
	case PMIXP_COLL_TREE_UPFWD_WSC:
		return "COLL_UPFWD_WAITSND";
	case PMIXP_COLL_TREE_UPFWD_WPC:
		return "COLL_UPFWD_WAITPRNT";
	case PMIXP_COLL_TREE_DOWNFWD:
		return "COLL_DOWNFWD";
	default:
		return "COLL_UNKNOWN";
	}
}

typedef enum {
	PMIXP_COLL_TREE_SND_NONE,
	PMIXP_COLL_TREE_SND_ACTIVE,
	PMIXP_COLL_TREE_SND_DONE,
	PMIXP_COLL_TREE_SND_FAILED,
} pmixp_coll_tree_sndstate_t;

inline static char *
pmixp_coll_tree_sndstatus2str(pmixp_coll_tree_sndstate_t state)
{
	switch (state) {
	case PMIXP_COLL_TREE_SND_NONE:
		return "COLL_SND_NONE";
	case PMIXP_COLL_TREE_SND_ACTIVE:
		return "COLL_SND_ACTIVE";
	case PMIXP_COLL_TREE_SND_DONE:
		return "COLL_SND_DONE";
	case PMIXP_COLL_TREE_SND_FAILED:
		return "COLL_SND_FAILED";
	default:
		return "COLL_UNKNOWN";
	}
}

typedef enum {
	PMIXP_COLL_REQ_PROGRESS,
	PMIXP_COLL_REQ_SKIP,
	PMIXP_COLL_REQ_FAILURE
} pmixp_coll_req_state_t;

/* tree coll struct */
typedef struct {
	/* general information */
	pmixp_coll_tree_state_t state;

	/* tree topology */
	char *prnt_host;
	int prnt_peerid;
	char *root_host;
	int root_peerid;
	int chldrn_cnt;
	hostlist_t all_chldrn_hl;
	char *chldrn_str;
	int *chldrn_ids;

	/* collective state */
	bool contrib_local;
	uint32_t contrib_children;
	bool *contrib_chld;
	pmixp_coll_tree_sndstate_t ufwd_status;
	bool contrib_prnt;
	uint32_t dfwd_cb_cnt, dfwd_cb_wait;
	pmixp_coll_tree_sndstate_t dfwd_status;

	/* collective data */
	Buf ufwd_buf, dfwd_buf;
	size_t serv_offs, dfwd_offset, ufwd_offset;
} pmixp_coll_tree_t;

/* PMIx Ring collective */
typedef enum {
	PMIXP_COLL_RING_SYNC,
	PMIXP_COLL_RING_PROGRESS,
	PMIXP_COLL_RING_FINALIZE,
} pmixp_ring_state_t;

struct pmixp_coll_s;

typedef struct {
	/* ptr to coll data */
	struct pmixp_coll_s *coll;

	/* context data */
	bool in_use;
	uint32_t seq;
	bool contrib_local;
	uint32_t contrib_prev;
	uint32_t forward_cnt;
	bool *contrib_map;
	pmixp_ring_state_t state;
	Buf ring_buf;
} pmixp_coll_ring_ctx_t;

/* coll ring struct */
typedef struct {
	/* next node id */
	int next_peerid;
	/* coll contexts data */
	pmixp_coll_ring_ctx_t ctx_array[PMIXP_COLL_RING_CTX_NUM];
	/* buffer pool to ensure parallel sends of ring data */
	List fwrd_buf_pool;
	List ring_buf_pool;
} pmixp_coll_ring_t;

typedef struct {
	uint32_t type;
	uint32_t contrib_id;
	uint32_t seq;
	uint32_t hop_seq;
	uint32_t nodeid;
	size_t msgsize;
} pmixp_coll_ring_msg_hdr_t;

typedef struct {
	size_t size;
	char *ptr;
	uint32_t contrib_id;
	uint32_t hop_seq;
} pmixp_coll_msg_ring_data_t;

inline static char *
pmixp_coll_ring_state2str(pmixp_ring_state_t state)
{
	switch (state) {
	case PMIXP_COLL_RING_SYNC:
		return "COLL_RING_SYNC";
	case PMIXP_COLL_RING_PROGRESS:
		return "PMIXP_COLL_RING_PROGRESS";
	case PMIXP_COLL_RING_FINALIZE:
		return "PMIXP_COLL_RING_FINILIZE";
	default:
		return "COLL_RING_UNKNOWN";
	}
}

/* General coll struct */
typedef struct pmixp_coll_s {
#ifndef NDEBUG
#define PMIXP_COLL_STATE_MAGIC 0xC011CAFE
	int magic;
#endif
	/* element-wise lock */
	pthread_mutex_t lock;

	/* collective state */
	uint32_t seq;

	/* general information */
	pmixp_coll_type_t type;

	/* PMIx collective id */
	struct {
		pmixp_proc_t *procs;
		size_t nprocs;
	} pset;
	int my_peerid;
	int peers_cnt;
#ifdef PMIXP_COLL_DEBUG
	hostlist_t peers_hl;
#endif

	/* libpmix callback data */
	void *cbfunc;
	void *cbdata;

	/* timestamp for stale collectives detection */
	time_t ts, ts_next;

	/* coll states */
	union {
		pmixp_coll_tree_t tree;
		pmixp_coll_ring_t ring;
	} state;
} pmixp_coll_t;

/* tree coll functions*/
int pmixp_coll_tree_init(pmixp_coll_t *coll, hostlist_t *hl);
void pmixp_coll_tree_free(pmixp_coll_tree_t *tree);

pmixp_coll_t *pmixp_coll_tree_from_cbdata(void *cbdata);

int pmixp_coll_tree_local(pmixp_coll_t *coll, char *data, size_t size,
			  void *cbfunc, void *cbdata);
int pmixp_coll_tree_child(pmixp_coll_t *coll, uint32_t nodeid,
			  uint32_t seq, Buf buf);
int pmixp_coll_tree_parent(pmixp_coll_t *coll, uint32_t nodeid,
			   uint32_t seq, Buf buf);
void pmixp_coll_tree_bcast(pmixp_coll_t *coll);
bool pmixp_coll_tree_progress(pmixp_coll_t *coll, char *fwd_node,
			      void **data, uint64_t size);
int pmixp_coll_tree_unpack(Buf buf, pmixp_coll_type_t *type,
			   int *nodeid, pmixp_proc_t **r,
			   size_t *nr);
void pmixp_coll_tree_reset_if_to(pmixp_coll_t *coll, time_t ts);
int pmixp_coll_check(pmixp_coll_t *coll, uint32_t seq);

/* ring coll functions */
int pmixp_coll_ring_init(pmixp_coll_t *coll, hostlist_t *hl);
void pmixp_coll_ring_free(pmixp_coll_ring_t *coll_ring);
int pmixp_coll_ring_check(pmixp_coll_t  *coll, pmixp_coll_ring_msg_hdr_t *hdr);
int pmixp_coll_ring_local(pmixp_coll_t  *coll, char *data, size_t size,
			  void *cbfunc, void *cbdata);
int pmixp_coll_ring_neighbor(pmixp_coll_t *coll, pmixp_coll_ring_msg_hdr_t *hdr,
			     Buf buf);
void pmixp_coll_ring_reset(pmixp_coll_ring_ctx_t *coll);
int pmixp_coll_ring_unpack(Buf buf, pmixp_coll_type_t *type,
			   pmixp_coll_ring_msg_hdr_t *ring_hdr,
			   pmixp_proc_t **r, size_t *nr);
void pmixp_coll_ring_reset_if_to(pmixp_coll_t  *coll, time_t ts);
pmixp_coll_ring_ctx_t *pmixp_coll_ring_ctx_select(pmixp_coll_t *coll,
						  const uint32_t seq);
pmixp_coll_t *pmixp_coll_ring_from_cbdata(void *cbdata);
void pmixp_coll_ring_free(pmixp_coll_ring_t *ring);



/* common coll func */
static inline void pmixp_coll_sanity_check(pmixp_coll_t *coll)
{
	xassert(NULL != coll);
	xassert(coll->magic == PMIXP_COLL_STATE_MAGIC);
}
int pmixp_coll_init(pmixp_coll_t *coll, pmixp_coll_type_t type,
		    const pmixp_proc_t *procs, size_t nprocs);
int pmixp_coll_contrib_local(pmixp_coll_t *coll, pmixp_coll_type_t type,
			     char *data, size_t ndata,
			     void *cbfunc, void *cbdata);
void pmixp_coll_free(pmixp_coll_t *coll);
void pmixp_coll_localcb_nodata(pmixp_coll_t *coll, int status);
int pmixp_coll_belong_chk(const pmixp_proc_t *procs, size_t nprocs);
void pmixp_coll_log(pmixp_coll_t *coll);
void pmixp_coll_ring_log(pmixp_coll_t *coll);
void pmixp_coll_tree_log(pmixp_coll_t *coll);

#endif /* PMIXP_COLL_RING_H */
