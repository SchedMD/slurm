/*****************************************************************************\
 **  pmix_coll.h - PMIx collective primitives
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

#ifndef PMIXP_COLL_H
#define PMIXP_COLL_H
#include "pmixp_common.h"
#include "pmixp_debug.h"

#define PMIXP_COLL_DEBUG 1

typedef enum {
	PMIXP_COLL_SYNC,
	PMIXP_COLL_COLLECT,
	PMIXP_COLL_UPFWD,
	PMIXP_COLL_UPFWD_WSC, /* Wait for the upward Send Complete */
	PMIXP_COLL_UPFWD_WPC, /* Wait for Parent Contrib */
	PMIXP_COLL_DOWNFWD,
} pmixp_coll_state_t;

inline static char *
pmixp_coll_state2str(pmixp_coll_state_t state)
{
	switch (state) {
	case PMIXP_COLL_SYNC:
		return "COLL_SYNC";
	case PMIXP_COLL_COLLECT:
		return "COLL_COLLECT";
	case PMIXP_COLL_UPFWD:
		return "COLL_UPFWD";
	case PMIXP_COLL_UPFWD_WSC:
		return "COLL_UPFWD_WAITSND";
	case PMIXP_COLL_UPFWD_WPC:
		return "COLL_UPFWD_WAITPRNT";
	case PMIXP_COLL_DOWNFWD:
		return "COLL_DOWNFWD";
	default:
		return "COLL_UNKNOWN";
	}
}

typedef enum {
	PMIXP_COLL_SND_NONE,
	PMIXP_COLL_SND_ACTIVE,
	PMIXP_COLL_SND_DONE,
	PMIXP_COLL_SND_FAILED,
} pmixp_coll_sndstate_t;

inline static char *
pmixp_coll_sndstatus2str(pmixp_coll_sndstate_t state)
{
	switch (state) {
	case PMIXP_COLL_SND_NONE:
		return "COLL_SND_NONE";
	case PMIXP_COLL_SND_ACTIVE:
		return "COLL_SND_ACTIVE";
	case PMIXP_COLL_SND_DONE:
		return "COLL_SND_DONE";
	case PMIXP_COLL_SND_FAILED:
		return "COLL_SND_FAILED";
	default:
		return "COLL_UNKNOWN";
	}
}

typedef enum {
	PMIXP_COLL_TYPE_FENCE,
	PMIXP_COLL_TYPE_CONNECT,
	PMIXP_COLL_TYPE_DISCONNECT
} pmixp_coll_type_t;

typedef enum {
	PMIXP_COLL_REQ_PROGRESS,
	PMIXP_COLL_REQ_SKIP,
	PMIXP_COLL_REQ_FAILURE
} pmixp_coll_req_state_t;

typedef struct {
#ifndef NDEBUG
#define PMIXP_COLL_STATE_MAGIC 0xC011CAFE
	int magic;
#endif
	/* element-wise lock */
	pthread_mutex_t lock;

	/* general information */
	pmixp_coll_state_t state;
	pmixp_coll_type_t type;
	/* PMIx collective id */
	struct {
		pmix_proc_t *procs;
		size_t nprocs;
	} pset;
	int my_peerid;
	int peers_cnt;
#ifdef PMIXP_COLL_DEBUG
	hostlist_t peers_hl;
#endif

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
	uint32_t seq;
	bool contrib_local;
	uint32_t contrib_children;
	bool *contrib_chld;
	pmixp_coll_sndstate_t ufwd_status;
	bool contrib_prnt;
	uint32_t dfwd_cb_cnt, dfwd_cb_wait;
	pmixp_coll_sndstate_t dfwd_status;


	/* collective data */
	Buf ufwd_buf, dfwd_buf;
	size_t serv_offs, dfwd_offset, ufwd_offset;


	/* libpmix callback data */
	pmix_modex_cbfunc_t cbfunc;
	void *cbdata;

	/* timestamp for stale collectives detection */
	time_t ts, ts_next;
} pmixp_coll_t;

static inline void pmixp_coll_sanity_check(pmixp_coll_t *coll)
{
	xassert(NULL != coll);
	xassert(coll->magic == PMIXP_COLL_STATE_MAGIC);
}

int pmixp_coll_init(pmixp_coll_t *coll, const pmix_proc_t *procs,
		    size_t nprocs, pmixp_coll_type_t type);
void pmixp_coll_free(pmixp_coll_t *coll);

pmixp_coll_t *pmixp_coll_from_cbdata(void *cbdata);

/*
 * This is important routine that takes responsibility to decide
 * what messages may appear and what may not. In absence of errors
 * we won't need this routine. Unfortunately they are exist.
 * There can be 3 general types of communication errors:
 * 1. We are trying to send our contribution to a parent and it fails.
 *    In this case we will be blocked in send function. At some point
 *    we either succeed or fail after predefined number of trials.
 *
 *    If we succeed - we are OK. Otherwise we will abort the whole job step.
 *
 * 2. A child of us sends us the message and gets the error, however we receive
 *    this message (false negative). Child will try again while we might be:
 *    (a) at FAN-IN step waiting for other contributions.
 *    (b) at FAN-OUT since we get all we need.
 *    (c) 2 step forward (SYNC) with coll->seq = (child_seq+1) if root of the
 *        tree successfuly broadcasted the whole database to us.
 *    (d) 3 step forward (next FAN-IN) with coll->seq = (child_seq+1)
 *        if somebody initiated next collective.
 *    (e) we won't move further because the child with problem won't send us
 *        next contribution.
 *
 *    Cases (a) and (b) can't be noticed here since child and we have the
 *    same seq number. They will later be detected  in pmixp_coll_contrib_node()
 *    based on collective contribution accounting vector.
 *
 *    Cases (c) and (d) would be visible here and should be treated as possible
 *    errors that should be ignored discarding the contribution.
 *
 *    Other cases are obvious error, we can abort in this case or ignore with
 *    error.
 *
 * 3. Root of the tree broadcasts the data and we get it, however root gets
 *    false negative. In this case root will try again. We might be:
 *    (a) at SYNC since we just got the DB and we are fine
 *        (coll->seq == root_seq+1)
 *    (b) at FAN-IN if somebody initiated next collective
 *        (coll->seq == root_seq+1)
 *    (c) at FAN-OUT if we will collect all necessary contributions and send
 *        it to our parent.
 *    (d) we won't be able to switch to SYNC since root will be busy dealing
 *        with previous DB broadcast.
 *    (e) at FAN-OUT waiting for the fan-out msg while receiving next fan-in
 *        message from one of our children (coll->seq + 1 == child_seq).
 */
static inline int pmixp_coll_check_seq(pmixp_coll_t *coll, uint32_t seq)
{
	if (coll->seq == seq) {
		/* accept this message */
		return PMIXP_COLL_REQ_PROGRESS;
	} else if ((coll->seq+1) == seq) {
		/* practice shows that because of SLURM communication
		 * infrastructure our child can switch to the next Fence
		 * and send us the message before the current fan-out message
		 * arrived. This is accounted in current state machine, so we
		 * allow if we receive message with seq number grater by one */
		return PMIXP_COLL_REQ_PROGRESS;
	} else if ((coll->seq - 1) == seq) {
		/* his may be our child OR root of the tree that
		 * had false negatives from SLURM protocol.
		 * It's normal situation, return error because we
		 * want to discard this message */
		return PMIXP_COLL_REQ_SKIP;
	}
	/* maybe need more sophisticated handling in presence of
	 * several steps. However maybe it's enough to just ignore */
	return PMIXP_COLL_REQ_FAILURE;
}

int pmixp_coll_contrib_local(pmixp_coll_t *coll, char *data, size_t size,
			     pmix_modex_cbfunc_t cbfunc, void *cbdata);
int pmixp_coll_contrib_child(pmixp_coll_t *coll, uint32_t nodeid,
			     uint32_t seq, Buf buf);
int pmixp_coll_contrib_parent(pmixp_coll_t *coll, uint32_t nodeid,
			     uint32_t seq, Buf buf);
void pmixp_coll_bcast(pmixp_coll_t *coll);
bool pmixp_coll_progress(pmixp_coll_t *coll, char *fwd_node,
			 void **data, uint64_t size);
int pmixp_coll_unpack_info(Buf buf, pmixp_coll_type_t *type,
			   int *nodeid, pmix_proc_t **r,
			   size_t *nr);
int pmixp_coll_belong_chk(pmixp_coll_type_t type,
			  const pmix_proc_t *procs, size_t nprocs);
void pmixp_coll_reset_if_to(pmixp_coll_t *coll, time_t ts);

#endif /* PMIXP_COLL_H */
