#include "pmixp_dconn.h"

pmixp_dconn_t *__pmixp_dconn_conns = NULL;
uint32_t __pmixp_dconn_conn_cnt = 0;

void pmixp_dconn_init(int node_cnt, pmixp_io_engine_header_t direct_hdr)
{
	int i;
	__pmixp_dconn_conns = xmalloc(sizeof(*__pmixp_dconn_conns) * node_cnt);
	__pmixp_dconn_conn_cnt = node_cnt;
	for(i=0; i<__pmixp_dconn_conn_cnt; i++){
		slurm_mutex_init(&__pmixp_dconn_conns[i].lock);
		__pmixp_dconn_conns[i].nodeid = i;
		__pmixp_dconn_conns[i].state = PMIXP_DIRECT_INIT;
		__pmixp_dconn_conns[i].fd = -1;
		pmixp_io_init(&__pmixp_dconn_conns[i].eng, direct_hdr);
	}
}

void pmixp_dconn_fini()
{
	int i;
	for(i=0; i<__pmixp_dconn_conn_cnt; i++){
		slurm_mutex_destroy(&__pmixp_dconn_conns[i].lock);
		pmixp_io_finalize(&__pmixp_dconn_conns[i].eng, 0);
	}
	xfree(__pmixp_dconn_conns);
	__pmixp_dconn_conn_cnt = 0;
}
