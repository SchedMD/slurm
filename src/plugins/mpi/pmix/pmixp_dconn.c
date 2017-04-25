/*****************************************************************************\
 **  pmix_dconn.c - direct connection module
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
		xfree(nodename);
		return SLURM_ERROR;
	}
	xfree(nodename);

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
