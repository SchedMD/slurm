/*****************************************************************************\
 **  pmix_dconn.c - direct connection module
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
#ifdef HAVE_UCX
#include "pmixp_dconn_ucx.h"
#endif

pmixp_dconn_t *_pmixp_dconn_conns = NULL;
uint32_t _pmixp_dconn_conn_cnt = 0;
pmixp_dconn_handlers_t _pmixp_dconn_h;

static pmixp_dconn_progress_type_t _progress_type;
static pmixp_dconn_conn_type_t _conn_type;
static int _poll_fd = -1;
static char *ep_data = NULL;
static size_t ep_len = 0;

int pmixp_dconn_init(int node_cnt, pmixp_p2p_data_t direct_hdr)
{
	int i;
	memset(&_pmixp_dconn_h, 0, sizeof(_pmixp_dconn_h));

#ifdef HAVE_UCX
	if (pmixp_info_srv_direct_conn_ucx()) {
		_poll_fd = pmixp_dconn_ucx_prepare(&_pmixp_dconn_h,
						   &ep_data, &ep_len);
		_progress_type = PMIXP_DCONN_PROGRESS_HW;
		_conn_type = PMIXP_DCONN_CONN_TYPE_ONESIDE;
	} else
#endif
	{
		_poll_fd = pmixp_dconn_tcp_prepare(&_pmixp_dconn_h,
						   &ep_data, &ep_len);
		_progress_type = PMIXP_DCONN_PROGRESS_SW;
		_conn_type = PMIXP_DCONN_CONN_TYPE_TWOSIDE;
	}

	if (SLURM_ERROR == _poll_fd) {
		PMIXP_ERROR("Cannot get polling fd");
		return SLURM_ERROR;
	}
	_pmixp_dconn_conns = xmalloc(sizeof(*_pmixp_dconn_conns) * node_cnt);
	_pmixp_dconn_conn_cnt = node_cnt;
	for (i=0; i<_pmixp_dconn_conn_cnt; i++) {
		slurm_mutex_init(&_pmixp_dconn_conns[i].lock);
		_pmixp_dconn_conns[i].nodeid = i;
		_pmixp_dconn_conns[i].state = PMIXP_DIRECT_INIT;
		_pmixp_dconn_conns[i].priv = _pmixp_dconn_h.init(i, direct_hdr);
		_pmixp_dconn_conns[i].uid = slurm_conf.slurmd_user_id;
	}
	return SLURM_SUCCESS;
}

void pmixp_dconn_fini()
{
	int i;
#ifdef HAVE_UCX
	if (pmixp_info_srv_direct_conn_ucx()) {
		pmixp_dconn_ucx_stop();
	}
#endif
	for (i=0; i<_pmixp_dconn_conn_cnt; i++) {
		slurm_mutex_destroy(&_pmixp_dconn_conns[i].lock);
		_pmixp_dconn_h.fini(_pmixp_dconn_conns[i].priv);
	}

#ifdef HAVE_UCX
	if (pmixp_info_srv_direct_conn_ucx()) {
		pmixp_dconn_ucx_finalize();
	} else
#endif
	{
		pmixp_dconn_tcp_finalize();
	}

	xfree(_pmixp_dconn_conns);
	_pmixp_dconn_conn_cnt = 0;
}

int pmixp_dconn_connect_do(pmixp_dconn_t *dconn, void *ep_data,
			   size_t ep_len, void *init_msg)
{
	return _pmixp_dconn_h.connect(dconn->priv, ep_data, ep_len, init_msg);
}

pmixp_dconn_progress_type_t pmixp_dconn_progress_type()
{
	return _progress_type;
}

pmixp_dconn_conn_type_t pmixp_dconn_connect_type()
{
	return _conn_type;
}

int pmixp_dconn_poll_fd()
{
	return _poll_fd;
}

size_t pmixp_dconn_ep_len()
{
	return ep_len;
}

char *pmixp_dconn_ep_data()
{
	return ep_data;
}
