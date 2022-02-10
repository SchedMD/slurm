/*****************************************************************************\
 **  pmix_dconn_tcp.c - PMIx direct TCP connection
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
#include "pmixp_dconn.h"
#include "pmixp_dconn_tcp.h"

static int _server_fd;
static in_port_t _server_port;

typedef struct {
	int fd;
	int nodeid;
	pmixp_io_engine_t eng;
} pmixp_dconn_tcp_t;

static void *_tcp_init(int nodeid, pmixp_p2p_data_t direct_hdr);
static void _tcp_fini(void *_priv);
static int _tcp_connect(void *_priv, void *ep_data, size_t ep_len,
			void *init_msg);
static int _tcp_send(void *_priv, void *msg);
static pmixp_io_engine_t *_tcp_getio(void *_priv);
static void _tcp_regio(eio_handle_t *h);

int pmixp_dconn_tcp_prepare(pmixp_dconn_handlers_t *handlers,
			    char **ep_data, size_t *ep_len)
{
	memset(handlers, 0, sizeof(*handlers));
	handlers->init = _tcp_init;
	handlers->fini = _tcp_fini;
	handlers->connect = _tcp_connect;
	handlers->send = _tcp_send;
	handlers->getio = _tcp_getio;
	handlers->regio = _tcp_regio;

	/* Create TCP socket for slurmd communication */
	if (0 > net_stream_listen(&_server_fd, &_server_port)) {
		PMIXP_ERROR("net_stream_listen");
		return SLURM_ERROR;
	}

	*ep_len = sizeof(_server_port);
	*ep_data = xmalloc(*ep_len);
	memcpy(*ep_data, &_server_port, *ep_len);

	return _server_fd;
}

void pmixp_dconn_tcp_finalize()
{
	close(_server_fd);
}

static void *_tcp_init(int nodeid, pmixp_p2p_data_t direct_hdr)
{
	pmixp_dconn_tcp_t *priv = xmalloc(sizeof(pmixp_dconn_tcp_t));
	pmixp_io_init(&priv->eng, direct_hdr);
	priv->fd = -1;
	priv->nodeid = nodeid;
	return (void*)priv;
}

static void _tcp_fini(void *_priv)
{
	pmixp_dconn_tcp_t *priv = (pmixp_dconn_tcp_t *)_priv;
	pmixp_io_finalize(&priv->eng, 0);
	xfree(priv);
}

static int _tcp_connect(void *_priv, void *ep_data, size_t ep_len,
			void *init_msg)
{
	pmixp_dconn_tcp_t *priv = (pmixp_dconn_tcp_t *)_priv;
	slurm_addr_t address;
	int fd, i, conn_timeout = 5;
	char *nodename = pmixp_info_job_host(priv->nodeid);
	uint16_t port;

	if (slurm_conf_get_addr(nodename, &address, 0) == SLURM_ERROR) {
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
	xassert(sizeof(port) == ep_len);
	memcpy(&port, ep_data, ep_len);
	slurm_set_port(&address, port);

	for (i = 0; i <= conn_timeout; i++) {
		if (i) {
			usleep(i * 1000);
		}
		fd = slurm_open_msg_conn(&address);
		if ((fd >= 0) || (errno != ECONNREFUSED)) {
			log_flag(NET, "%s: slurm_open_msg_conn(%pA): %m",
				 __func__, &address);
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
	priv->fd = fd;
	pmixp_fd_set_nodelay(fd);
	fd_set_nonblocking(fd);

	/* Send initialization message if requested */
	if (init_msg) {
		/* Init message has to be first in the line */
		pmixp_io_send_urgent(&priv->eng, init_msg);
	}

	/* enable send */
	pmixp_io_attach(&priv->eng, fd);

	return SLURM_SUCCESS;
}

static int _tcp_send(void *_priv, void *msg)
{
	pmixp_dconn_tcp_t *priv = (pmixp_dconn_tcp_t *)_priv;
	int rc = pmixp_io_send_enqueue(&priv->eng, msg);
	if (SLURM_SUCCESS != rc) {
		char *nodename = pmixp_info_job_host(priv->nodeid);
		xassert(NULL != nodename);
		PMIXP_ERROR("Fail to enqueue to engine, node: %s (%d)",
			    nodename, priv->nodeid);
		xassert(pmixp_io_enqueue_ok(&priv->eng));
		xfree(nodename);
	}
	eio_signal_wakeup(pmixp_info_io());
	return rc;
}


static pmixp_io_engine_t *_tcp_getio(void *_priv)
{
	pmixp_dconn_tcp_t *priv = (pmixp_dconn_tcp_t *)_priv;
	return &priv->eng;
}

/* don't need to do anything */
static void _tcp_regio(eio_handle_t *h)
{

}
