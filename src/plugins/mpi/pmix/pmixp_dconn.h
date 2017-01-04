#ifndef PMIXP_DCONN_H
#define PMIXP_DCONN_H

#include "pmixp_common.h"
#include "pmixp_debug.h"
#include "pmixp_io.h"

typedef enum {
	PMIXP_DIRECT_NONE, /* shouldn't be used in this state */
	PMIXP_DIRECT_INIT,
	PMIXP_DIRECT_PORT_SENT,
	PMIXP_DIRECT_CONNECTED
} pmixp_dconn_state_t;

typedef struct {
	/* element-wise lock */
	pthread_mutex_t lock;

	/* status */
	pmixp_dconn_state_t state;
	List pending_msg;

	/* remote node info */
	int nodeid;
	int fd;
	pmixp_io_engine_t eng;
	struct timeval send_ts;

} pmixp_dconn_t;

void pmixp_dconn_init(int node_cnt,
		      pmixp_io_engine_header_t direct_hdr);
void pmixp_dconn_fini();

/* for internal use only ! */
extern uint32_t __pmixp_dconn_conn_cnt;
extern pmixp_dconn_t *__pmixp_dconn_conns;

static inline pmixp_dconn_t *
pmixp_dconn_lock(int nodeid)
{

#ifdef DEBUG
	if( nodeid >= node_conns_cnt ){
		PMIXP_ERROR("Access unexisting node id %d, max is %d",
			    nodeid, node_conns_cnt - 1);
		return NULL;
	}
#endif
	slurm_mutex_lock(&__pmixp_dconn_conns[nodeid].lock);
	return &__pmixp_dconn_conns[nodeid];
}

static inline pmixp_dconn_t *
pmixp_dconn_lock(int nodeid);

static inline void
pmixp_dconn_unlock(pmixp_dconn_t *conn)
{
	slurm_mutex_unlock(&conn->lock);
}

static inline pmixp_dconn_state_t
pmixp_dconn_state(pmixp_dconn_t *conn)
{
	return conn->state;
}

static inline pmixp_io_engine_t*
pmixp_dconn_engine(pmixp_dconn_t *conn)
{
	return &conn->eng;
}

/* Returns locked direct connection descriptor */
static inline pmixp_dconn_t *
pmixp_dconn_establish(int nodeid, int fd)
{
	pmixp_dconn_t *dconn = pmixp_dconn_lock(nodeid);
	switch( pmixp_dconn_state(dconn) ){
	case PMIXP_DIRECT_INIT:
		/* we haven't requested the connection, however
		 * remote side acquired our port in some way
		 * (reserved for possible future improvements
		 *
		 * <fall-thru>
		 */
	case PMIXP_DIRECT_PORT_SENT:
		/* we request this connection some time ago
		 * and now we finishing it's establishment
		 */
		pmixp_io_attach(&dconn->eng, fd);
		dconn->state = PMIXP_DIRECT_CONNECTED;
		return dconn;
	case PMIXP_DIRECT_CONNECTED:
		PMIXP_DEBUG("Trying to re-establish the connection");
		break;
	case PMIXP_DIRECT_NONE:
		/* shouldn't happen */
		PMIXP_ERROR("Unexpected direct connection state: PMIXP_DIRECT_NONE");
		abort();
	}
	pmixp_dconn_unlock(dconn);
	return NULL;
}


#endif // PMIXP_DCONN_H
