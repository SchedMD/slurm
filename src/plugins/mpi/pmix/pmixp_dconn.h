/*****************************************************************************\
 **  pmix_dconn.h - direct connection module
 *****************************************************************************
 *  Copyright (C) 2017      Mellanox Technologies. All rights reserved.
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

#ifndef PMIXP_DCONN_H
#define PMIXP_DCONN_H

#include "pmixp_common.h"
#include "pmixp_debug.h"
#include "pmixp_io.h"

typedef enum {
	PMIXP_DIRECT_NONE, /* shouldn't be used in this state */
	PMIXP_DIRECT_INIT,
	PMIXP_DIRECT_EP_SENT,
	PMIXP_DIRECT_CONNECTED
} pmixp_dconn_state_t;

typedef struct {
	/* element-wise lock */
	pthread_mutex_t lock;

	/* status */
	pmixp_dconn_state_t state;

	/* remote node info */
	int nodeid;
	int fd;
	pmixp_io_engine_t eng;
} pmixp_dconn_t;

void pmixp_dconn_init(int node_cnt,
		      pmixp_io_engine_header_t direct_hdr);
void pmixp_dconn_fini();

/* for internal use only ! */
extern uint32_t _pmixp_dconn_conn_cnt;
extern pmixp_dconn_t *_pmixp_dconn_conns;

static inline pmixp_dconn_t *
pmixp_dconn_lock(int nodeid)
{
	xassert(nodeid < _pmixp_dconn_conn_cnt);
	slurm_mutex_lock(&_pmixp_dconn_conns[nodeid].lock);
	return &_pmixp_dconn_conns[nodeid];
}

static inline pmixp_dconn_t *
pmixp_dconn_lock(int nodeid);

#ifndef NDEBUG
static void pmixp_dconn_verify(pmixp_dconn_t *dconn)
{
	int i = dconn->nodeid;
	xassert((&_pmixp_dconn_conns[i]) == dconn);
}
#else
#define pmixp_dconn_verify(dconn)
#endif

static inline void
pmixp_dconn_unlock(pmixp_dconn_t *dconn)
{
	pmixp_dconn_verify(dconn);
	slurm_mutex_unlock(&dconn->lock);
}

static inline pmixp_dconn_state_t
pmixp_dconn_state(pmixp_dconn_t *dconn)
{
	pmixp_dconn_verify(dconn);
	return dconn->state;
}

static inline pmixp_io_engine_t*
pmixp_dconn_engine(pmixp_dconn_t *dconn)
{
	pmixp_dconn_verify(dconn);
	return &dconn->eng;
}

static inline int
pmixp_dconn_fd(pmixp_dconn_t *dconn)
{
	pmixp_dconn_verify(dconn);
	return dconn->fd;
}

static inline void
pmixp_dconn_req_sent(pmixp_dconn_t *dconn)
{
	if (PMIXP_DIRECT_INIT != dconn->state) {
		PMIXP_ERROR("State machine violation, when transition to PORT_SENT from %d",
			    (int)dconn->state);
		xassert(PMIXP_DIRECT_INIT == dconn->state);
		abort();
	}
	dconn->state = PMIXP_DIRECT_EP_SENT;
}

static inline int
pmixp_dconn_send(pmixp_dconn_t *dconn, void *msg)
{

	int rc = pmixp_io_send_enqueue(&dconn->eng, msg);
	if( SLURM_SUCCESS != rc) {
		char *nodename = pmixp_info_job_host(dconn->nodeid);
		xassert(NULL != nodename);
		PMIXP_ERROR("Fail to enqueue to engine, node: %s (%d)",
			    nodename, dconn->nodeid);
		xassert(pmixp_io_enqueue_ok(&dconn->eng));
		xfree(nodename);
	}
	return rc;
}

/* Returns locked direct connection descriptor */
static inline pmixp_dconn_t *
pmixp_dconn_accept(int nodeid, int fd)
{
	pmixp_dconn_t *dconn = pmixp_dconn_lock(nodeid);
	if( PMIXP_DIRECT_EP_SENT == pmixp_dconn_state(dconn) ){
		/* we request this connection some time ago
		 * and now we finishing it's establishment
		 */
		pmixp_fd_set_nodelay(fd);
		pmixp_io_attach(&dconn->eng, fd);
		dconn->state = PMIXP_DIRECT_CONNECTED;
	} else {
		/* shouldn't happen */
		PMIXP_ERROR("Unexpected direct connection state: PMIXP_DIRECT_NONE");
		xassert(0 && pmixp_dconn_state(dconn));
		abort();
	}
	return dconn;
}

int pmixp_dconn_connect_do(pmixp_dconn_t *dconn, uint16_t port, void *init_msg);

/* Returns locked direct connection descriptor */
static inline pmixp_dconn_t *
pmixp_dconn_connect(int nodeid, uint16_t port, void *init_msg)
{
	int rc;
	pmixp_dconn_t *dconn = pmixp_dconn_lock(nodeid);
	switch( pmixp_dconn_state(dconn) ){
	case PMIXP_DIRECT_INIT:
		break;
	case PMIXP_DIRECT_EP_SENT:
		if( nodeid < pmixp_info_nodeid()){
			break;
		} else {
			/* just ignore this connection,
			 * remote side will come with counter-connection
			 */
		}
		goto unlock;
	case PMIXP_DIRECT_CONNECTED:
		PMIXP_DEBUG("Trying to re-establish the connection");
		goto unlock;
	default:
		/* shouldn't happen */
		PMIXP_ERROR("Unexpected direct connection state: PMIXP_DIRECT_NONE");
		xassert(0 && pmixp_dconn_state(dconn));
		abort();
	}

	/* establish the connection */
	rc = pmixp_dconn_connect_do(dconn, port, init_msg);
	if( SLURM_SUCCESS == rc ){
		dconn->state = PMIXP_DIRECT_CONNECTED;
		/* return locked structure */
		return dconn;
	} else {
		/* drop the state to INIT so we will try again later
		 * if it will always be failing - we will always use
		 * SLURM's protocol
		 */
		char *nodename = pmixp_info_job_host(nodeid);
		xassert(NULL != nodename);
		if (NULL == nodename) {
			PMIXP_ERROR("Bad nodeid = %d in the incoming message", nodeid);
			abort();
		}
		dconn->state = PMIXP_DIRECT_INIT;
		PMIXP_ERROR("Cannot establish direct connection to %s (%d)",
			   nodename, nodeid);
		xfree(nodename);
	}
unlock:
	pmixp_dconn_unlock(dconn);
	return NULL;
}

/* Returns locked direct connection descriptor */
static inline void
pmixp_dconn_disconnect(pmixp_dconn_t *dconn)
{
	switch( pmixp_dconn_state(dconn) ){
	case PMIXP_DIRECT_INIT:
	case PMIXP_DIRECT_EP_SENT:
		break;
	case PMIXP_DIRECT_CONNECTED:{
		int fd = pmixp_io_detach(&dconn->eng);
		close(fd);
		break;
	}
	default:
		/* shouldn't happen */
		PMIXP_ERROR("Unexpected direct connection state: PMIXP_DIRECT_NONE");
		xassert(0 && pmixp_dconn_state(dconn));
		abort();
	}

	dconn->state = PMIXP_DIRECT_INIT;
}

#endif // PMIXP_DCONN_H
