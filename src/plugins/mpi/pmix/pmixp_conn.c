/*****************************************************************************\
 **  pmix_conn.c - module for fd-based connections progress
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

#include "pmixp_conn.h"

/* temporal engines local API */
static void _tmp_engines_init();
static void _tmp_engines_fini();
static inline pmixp_io_engine_t *_tmp_engines_get_slurm();
static inline void _tmp_engines_return_slurm(pmixp_io_engine_t *eng);
static inline pmixp_io_engine_t *_tmp_engines_get_direct();
static inline void _tmp_engines_return_direct(pmixp_io_engine_t *eng);

static void _msg_handler_destruct(void *obj);
static List _conn_list, _empty_hndl_list;
static pmixp_p2p_data_t _slurm_hdr, _direct_hdr;

void pmixp_conn_init(pmixp_p2p_data_t slurm_hdr,
		     pmixp_p2p_data_t direct_hdr)
{
	_conn_list = list_create(_msg_handler_destruct);
	_empty_hndl_list = list_create(_msg_handler_destruct);
	_slurm_hdr = slurm_hdr;
	_direct_hdr = direct_hdr;
	_tmp_engines_init();
}

void pmixp_conn_fini(void)
{
	list_destroy(_conn_list);
	list_destroy(_empty_hndl_list);
	_tmp_engines_fini();
}

static void _msg_handler_destruct(void *obj)
{
	pmixp_conn_t *conn = (pmixp_conn_t*)obj;
	switch (conn->type) {
	case PMIXP_CONN_TEMP:
		pmixp_io_finalize(conn->eng, 0);
		xfree(conn->eng);
		conn->eng = NULL;
		break;
	case PMIXP_CONN_EMPTY:
	case PMIXP_CONN_PERSIST:
		/* - Persistent handlers have their engines allocated
		 * somewhere else. Don't release it here
		 * - empty handlers don't carry any data.
		 */
		break;
	default:
		/* this shouldn't happen! */
		PMIXP_ERROR("Bad message handler connection type: %d",
			    (int)conn->type);
		abort();
	}
	xfree(conn);
}

void pmixp_conn_cleanup(void)
{
	ListIterator it = list_iterator_create(_conn_list);
	pmixp_conn_t *hndl = NULL;
	while (NULL != (hndl = list_next(it))) {
		if (PMIXP_CONN_EMPTY == hndl->type) {
			/* move this handler to the empty list */
			list_remove(it);
			list_append(_empty_hndl_list, hndl);

		}
	}
}

pmixp_conn_t *pmixp_conn_new_temp(pmixp_conn_proto_t proto, int fd,
				  pmixp_conn_new_msg_cb_t nmsg_cb)
{
	xassert(proto == PMIXP_PROTO_SLURM || proto == PMIXP_PROTO_DIRECT);

	pmixp_conn_t *conn = list_pop(_empty_hndl_list);
	if (NULL == conn) {
		conn = xmalloc(sizeof(*conn));
	}

	xassert(PMIXP_PROTO_NONE == conn->proto);
	xassert(NULL == conn->eng);
	xassert(NULL == conn->rcv_progress_cb);

	conn->type = PMIXP_CONN_TEMP;
	conn->proto = proto;
	/* grab the temp I/O engine of the corresponding type */
	switch (proto) {
	case PMIXP_PROTO_SLURM:
		conn->eng = _tmp_engines_get_slurm();
		break;
	case PMIXP_PROTO_DIRECT:
		conn->eng = _tmp_engines_get_direct();
		break;
	default:
		/* should not happen */
		PMIXP_ERROR("Bad protocol type: %d", proto);
		abort();
	}
	pmixp_io_attach(conn->eng, fd);
	conn->rcv_progress_cb = nmsg_cb;
	conn->ret_cb = NULL;
	conn->ret_data = NULL;
	conn->hdr = NULL;
	return conn;
}

pmixp_conn_t *pmixp_conn_new_persist(pmixp_conn_proto_t proto,
				     pmixp_io_engine_t *eng,
				     pmixp_conn_new_msg_cb_t nmsg_cb,
				     pmixp_conn_ret_cb_t ret_cb, void *ret_data)
{
	xassert(proto == PMIXP_PROTO_SLURM || proto == PMIXP_PROTO_DIRECT);
	xassert(NULL != eng);

	pmixp_conn_t *conn = list_pop(_empty_hndl_list);
	if (NULL == conn) {
		conn = xmalloc(sizeof(*conn));
	}

	xassert(PMIXP_PROTO_NONE == conn->proto);
	xassert(NULL == conn->eng);
	xassert(NULL == conn->rcv_progress_cb);

	conn->type = PMIXP_CONN_PERSIST;
	conn->proto = proto;
	conn->eng = eng;
	conn->rcv_progress_cb = nmsg_cb;
	conn->ret_cb = ret_cb;
	conn->ret_data = ret_data;
	conn->hdr = NULL;
	return conn;
}

void pmixp_conn_return(pmixp_conn_t *conn)
{
	/* if this is a temp connection - return I/O engine */
	if (NULL != conn->ret_cb) {
		conn->ret_cb(conn);
	}
	if (NULL != conn->hdr) {
		xfree(conn->hdr);
	}
	switch (conn->type){
	case PMIXP_CONN_PERSIST:
		/* corresponding I/O engine was allocated somewhere else */
		break;
	case PMIXP_CONN_TEMP: {
		if (pmixp_io_conn_closed(conn->eng)) {
			int fd = pmixp_io_detach(conn->eng);
			close(fd);
		}

		/* grab the temp I/O engine of the corresponding type */
		switch (conn->proto) {
		case PMIXP_PROTO_SLURM:
			_tmp_engines_return_slurm(conn->eng);
			break;
		case PMIXP_PROTO_DIRECT:
			_tmp_engines_return_direct(conn->eng);
			break;
		default:
			/* should not happen */
			PMIXP_ERROR("Bad protocol type: %d", conn->proto);
			abort();
		}
		break;
	}
	default:
		/* should not happen */
		PMIXP_ERROR("Bad connection type: %d", conn->type);
		abort();
	}
	/* this handler will soon be garbage-collected */
	memset(conn, 0, sizeof(*conn));
	conn->type = PMIXP_CONN_EMPTY;
}



/*
 * --------------------- temporal I/O engines -------------------
 */

static void _temp_engine_destruct(void *obj);
static List _slurm_engines, _direct_engines;

static void _tmp_engines_init()
{
	_slurm_engines  = list_create(_temp_engine_destruct);
	_direct_engines = list_create(_temp_engine_destruct);
}

static void _tmp_engines_fini()
{
	list_destroy(_slurm_engines);
	list_destroy(_direct_engines);
}

static void _temp_engine_destruct(void *obj)
{
	pmixp_io_engine_t *eng = (pmixp_io_engine_t*)obj;
	pmixp_io_finalize(eng, 0);
	xfree(eng);
}

static inline pmixp_io_engine_t *_tmp_engines_get_slurm()
{
	pmixp_io_engine_t *eng = list_pop(_slurm_engines);
	if (NULL == eng){
		eng = xmalloc(sizeof(*eng));
		pmixp_io_init(eng, _slurm_hdr);
	}
	return eng;
}

static inline void _tmp_engines_return_slurm(pmixp_io_engine_t *eng)
{
	xassert(NULL != eng);
	list_push(_slurm_engines, eng);
}

static inline pmixp_io_engine_t *_tmp_engines_get_direct()
{
	pmixp_io_engine_t *eng = list_pop(_direct_engines);
	if (NULL == eng){
		eng = xmalloc(sizeof(*eng));
		pmixp_io_init(eng, _direct_hdr);
	}
	return eng;
}

static inline void _tmp_engines_return_direct(pmixp_io_engine_t *eng)
{
	xassert(NULL != eng);
	list_push(_direct_engines, eng);
}
