/*****************************************************************************\
 **  pmix_dconn.h - direct connection module
 *****************************************************************************
 *  Copyright (C) 2017      Mellanox Technologies. All rights reserved.
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

#ifndef PMIXP_DCONN_H
#define PMIXP_DCONN_H

#include "pmixp_common.h"
#include "pmixp_debug.h"
#include "pmixp_io.h"
#include "pmixp_conn.h"

typedef enum {
	PMIXP_DIRECT_NONE, /* shouldn't be used in this state */
	PMIXP_DIRECT_INIT,
	PMIXP_DIRECT_EP_SENT,
	PMIXP_DIRECT_CONNECTED
} pmixp_dconn_state_t;

typedef enum {
	/* This direct connection implementation needs software-level
	 * progress engine (select/poll/epoll)
	 */
	PMIXP_DCONN_PROGRESS_SW,
	/* This direct connection implementation has hardware-level
	 * progress engine
	 */
	PMIXP_DCONN_PROGRESS_HW
} pmixp_dconn_progress_type_t;

typedef enum {
	/* This direct connection implementation requires
	 * both sides to be involved in the connection
	 * establishment
	 */
	PMIXP_DCONN_CONN_TYPE_TWOSIDE,
	/* This direct connection implementation implements
	 * one-sided connection semantics
	 */
	PMIXP_DCONN_CONN_TYPE_ONESIDE
} pmixp_dconn_conn_type_t;

typedef struct {
	/* element-wise lock */
	pthread_mutex_t lock;

	/* status */
	pmixp_dconn_state_t state;

	/* remote node info */
	int nodeid;
	void *priv;

	/* authenticated uid on remote */
	uid_t uid;
} pmixp_dconn_t;

typedef void *(*pmixp_dconn_p2p_init_t)(int nodeid,
					pmixp_p2p_data_t direct_hdr);
typedef void (*pmixp_dconn_p2p_fini_t)(void *_priv);
typedef int (*pmixp_dconn_p2p_connect_t)(void *_priv, void *ep_data,
					 size_t ep_len, void *init_msg);
typedef int (*pmixp_dconn_p2p_send_nb_t)(void *_priv, void *msg);
typedef pmixp_io_engine_t *(*pmixp_dconn_p2p_getio_t)(void *_priv);
typedef void (*pmixp_dconn_p2p_regio_t)(eio_handle_t *h);

typedef struct {
	pmixp_dconn_p2p_init_t init;
	pmixp_dconn_p2p_fini_t fini;
	pmixp_dconn_p2p_connect_t connect;
	pmixp_dconn_p2p_send_nb_t send;
	pmixp_dconn_p2p_getio_t getio;
	pmixp_dconn_p2p_regio_t regio;
} pmixp_dconn_handlers_t;


/* for internal use only ! */
extern uint32_t _pmixp_dconn_conn_cnt;
extern pmixp_dconn_t *_pmixp_dconn_conns;
extern pmixp_dconn_handlers_t _pmixp_dconn_h;

int pmixp_dconn_init(int node_cnt, pmixp_p2p_data_t direct_hdr);
void pmixp_dconn_fini();
int pmixp_dconn_connect_do(pmixp_dconn_t *dconn, void *ep_data,
			   size_t ep_len, void *init_msg);
pmixp_dconn_progress_type_t pmixp_dconn_progress_type();
pmixp_dconn_conn_type_t pmixp_dconn_connect_type();
int pmixp_dconn_poll_fd();
size_t pmixp_dconn_ep_len();
char *pmixp_dconn_ep_data();

#ifndef NDEBUG
static void pmixp_dconn_verify(pmixp_dconn_t *dconn)
{
	int i = dconn->nodeid;
	xassert((&_pmixp_dconn_conns[i]) == dconn);
}
#else
#define pmixp_dconn_verify(dconn)
#endif

static inline pmixp_dconn_t *pmixp_dconn_lock(int nodeid)
{
	xassert(nodeid < _pmixp_dconn_conn_cnt);
	slurm_mutex_lock(&_pmixp_dconn_conns[nodeid].lock);
	return &_pmixp_dconn_conns[nodeid];
}

static inline void pmixp_dconn_unlock(pmixp_dconn_t *dconn)
{
	pmixp_dconn_verify(dconn);
	slurm_mutex_unlock(&dconn->lock);
}

static inline pmixp_dconn_state_t pmixp_dconn_state(pmixp_dconn_t *dconn)
{
	pmixp_dconn_verify(dconn);
	return dconn->state;
}

static inline void pmixp_dconn_req_sent(pmixp_dconn_t *dconn)
{
	if (PMIXP_DIRECT_INIT != dconn->state) {
		PMIXP_ERROR("State machine violation, when transition "
			    "to PORT_SENT from %d",
			    (int)dconn->state);
		xassert(PMIXP_DIRECT_INIT == dconn->state);
		abort();
	}
	dconn->state = PMIXP_DIRECT_EP_SENT;
}

static inline int pmixp_dconn_send(pmixp_dconn_t *dconn, void *msg)
{
	return _pmixp_dconn_h.send(dconn->priv, msg);
}

static inline void pmixp_dconn_regio(eio_handle_t *h)
{
	return _pmixp_dconn_h.regio(h);
}

/* Returns locked direct connection descriptor */

static inline bool pmixp_dconn_require_connect(
	pmixp_dconn_t *dconn, bool *send_init)
{
	*send_init = false;
	switch( pmixp_dconn_state(dconn) ){
	case PMIXP_DIRECT_INIT:
		*send_init = true;
		return true;
	case PMIXP_DIRECT_EP_SENT:{
		switch (pmixp_dconn_connect_type()) {
		case PMIXP_DCONN_CONN_TYPE_TWOSIDE: {
			if( dconn->nodeid < pmixp_info_nodeid()){
				*send_init = true;
				return true;
			} else {
				/* just ignore this connection,
				 * remote side will come with counter-
				 * connection
				 */
				return false;
			}
		}
		case PMIXP_DCONN_CONN_TYPE_ONESIDE:
			*send_init = false;
			return true;
		default:
			/* shouldn't happen */
			PMIXP_ERROR("Unexpected direct connection "
				    "semantics type: %d",
				    pmixp_dconn_connect_type());
			xassert(0 && pmixp_dconn_connect_type());
			abort();
		}
		break;
	}
	case PMIXP_DIRECT_CONNECTED:
		PMIXP_DEBUG("Trying to re-establish the connection");
		return false;
	default:
		/* shouldn't happen */
		PMIXP_ERROR("Unexpected direct connection state: "
			    "PMIXP_DIRECT_NONE");
		xassert(0 && pmixp_dconn_state(dconn));
		abort();
	}
	return false;
}

static inline int pmixp_dconn_connect(
	pmixp_dconn_t *dconn, void *ep_data, int ep_len, void *init_msg)
{
	int rc;
	/* establish the connection */
	rc = pmixp_dconn_connect_do(dconn, ep_data, ep_len, init_msg);
	if (SLURM_SUCCESS == rc){
		dconn->state = PMIXP_DIRECT_CONNECTED;
	} else {
		/*
		 * Abort the application - we can't do what user requested.
		 * Make sure to provide enough info
		 */
		char *nodename = pmixp_info_job_host(dconn->nodeid);
		xassert(nodename);
		if (NULL == nodename) {
			PMIXP_ERROR("Bad nodeid = %d in the incoming message",
				    dconn->nodeid);
			abort();
		}
		PMIXP_ERROR("Cannot establish direct connection to %s (%d)",
			    nodename, dconn->nodeid);
		xfree(nodename);
		pmixp_debug_hang(0); /* enable hang to debug this! */
		slurm_kill_job_step(pmixp_info_jobid(),
				    pmixp_info_stepid(), SIGKILL);
	}
	return rc;
}

/* POLL-based specific API */
static inline pmixp_io_engine_t *pmixp_dconn_engine(pmixp_dconn_t *dconn)
{
	pmixp_dconn_verify(dconn);
	xassert( PMIXP_DCONN_PROGRESS_SW == pmixp_dconn_progress_type(dconn));
	if( PMIXP_DCONN_PROGRESS_SW == pmixp_dconn_progress_type(dconn) ){
		return _pmixp_dconn_h.getio(dconn->priv);
	}
	return NULL;
}

/* Returns locked direct connection descriptor */
static inline pmixp_dconn_t *pmixp_dconn_accept(int nodeid, int fd)
{
	if( PMIXP_DCONN_PROGRESS_SW != pmixp_dconn_progress_type() ){
		PMIXP_ERROR("Accept is not supported by direct connection "
			    "of type %d",
			    (int)pmixp_dconn_progress_type());
		xassert(PMIXP_DCONN_PROGRESS_SW ==
			pmixp_dconn_progress_type());
		return NULL;
	}
	pmixp_dconn_t *dconn = pmixp_dconn_lock(nodeid);
	xassert(dconn);
	pmixp_io_engine_t *eng = _pmixp_dconn_h.getio(dconn->priv);
	xassert( NULL != eng );

	if( PMIXP_DIRECT_EP_SENT == pmixp_dconn_state(dconn) ){
		/* we request this connection some time ago
		 * and now we finishing it's establishment
		 */
		pmixp_fd_set_nodelay(fd);
		pmixp_io_attach(eng, fd);
		dconn->state = PMIXP_DIRECT_CONNECTED;
	} else {
		/* shouldn't happen */
		PMIXP_ERROR("Unexpected direct connection state: %d",
			    (int)pmixp_dconn_state(dconn));
		xassert(PMIXP_DIRECT_EP_SENT == pmixp_dconn_state(dconn));
		pmixp_dconn_unlock(dconn);
		return NULL;
	}
	return dconn;
}

/* Returns locked direct connection descriptor */
static inline void pmixp_dconn_disconnect(pmixp_dconn_t *dconn)
{
	switch( pmixp_dconn_state(dconn) ){
	case PMIXP_DIRECT_INIT:
	case PMIXP_DIRECT_EP_SENT:
		break;
	case PMIXP_DIRECT_CONNECTED:{
		pmixp_io_engine_t *eng = _pmixp_dconn_h.getio(dconn->priv);
		int fd = pmixp_io_detach(eng);
		close(fd);
		break;
	}
	default:
		/* shouldn't happen */
		PMIXP_ERROR("Unexpected direct connection state: "
			    "PMIXP_DIRECT_NONE");
		xassert(0 && pmixp_dconn_state(dconn));
		abort();
	}

	dconn->state = PMIXP_DIRECT_INIT;
}

#endif // PMIXP_DCONN_H
