#include "pmixp_dconn.h"

pmixp_dconn_t *_pmixp_dconn_conns = NULL;
uint32_t _pmixp_dconn_conn_cnt = 0;

void pmixp_dconn_init(int node_cnt, pmixp_io_engine_header_t direct_hdr)
{
	int i;
	_pmixp_dconn_conns = xmalloc(sizeof(*_pmixp_dconn_conns) * node_cnt);
	_pmixp_dconn_conn_cnt = node_cnt;
	for(i=0; i<_pmixp_dconn_conn_cnt; i++){
		slurm_mutex_init(&_pmixp_dconn_conns[i].lock);
		_pmixp_dconn_conns[i].nodeid = i;
		_pmixp_dconn_conns[i].state = PMIXP_DIRECT_INIT;
		_pmixp_dconn_conns[i].fd = -1;
		pmixp_io_init(&_pmixp_dconn_conns[i].eng, direct_hdr);
	}
}

void pmixp_dconn_fini()
{
	int i;
	for(i=0; i<_pmixp_dconn_conn_cnt; i++){
		slurm_mutex_destroy(&_pmixp_dconn_conns[i].lock);
		pmixp_io_finalize(&_pmixp_dconn_conns[i].eng, 0);
	}
	xfree(_pmixp_dconn_conns);
	_pmixp_dconn_conn_cnt = 0;
}

int pmixp_dconn_connect_do(pmixp_dconn_t *dconn, uint16_t port, void *init_msg)
{
	char *nodename = pmixp_info_job_host(dconn->nodeid);
	slurm_addr_t address;
	int fd, i, conn_timeout = 5;

	if (slurm_conf_get_addr(nodename, &address) == SLURM_ERROR) {
		PMIXP_ERROR("Can't find address for host "
			    "%s, check slurm.conf", nodename);
		return SLURM_ERROR;
	}

	/* need to change the port # from the slurmd's to
	 * the provided stepd's
	 * TODO: check carefully if there is an appropriate api for that
	 */
	address.sin_port = htons(port);

	for (i = 0; i <= conn_timeout; i++) {
		if (i) {
			usleep(i * 1000);
		}
		fd = slurm_open_msg_conn(&address);
		if ((fd >= 0) || (errno != ECONNREFUSED)) {
			break;
		}
		if (i == 0){
			PMIXP_DEBUG("connect refused, retrying");
		}
	}
	if (fd < 0) {
		PMIXP_ERROR("Cannot establish the connection");
		return SLURM_ERROR;
	}
	dconn->fd = fd;
	pmixp_fd_set_nodelay(fd);
	fd_set_nonblocking(fd);

	/* Init message has to be first in the line */
	pmixp_io_send_urgent(&dconn->eng, init_msg);

	/* enable send */
	pmixp_io_attach(&dconn->eng, fd);
	return SLURM_SUCCESS;
}
