/*****************************************************************************\
 **  pmix_conn.h - module for fd-based connections progress
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

#ifndef PMIXP_DIRECT_CONN_H
#define PMIXP_DIRECT_CONN_H

#include "pmixp_common.h"
#include "pmixp_debug.h"
#include "pmixp_io.h"


typedef enum {
	PMIXP_PROTO_NONE = 0,
	PMIXP_PROTO_SLURM,
	PMIXP_PROTO_DIRECT
} pmixp_conn_proto_t;

typedef enum {
	PMIXP_CONN_NONE = 0,
	PMIXP_CONN_PERSIST,
	PMIXP_CONN_TEMP,
	PMIXP_CONN_EMPTY,
} pmixp_conn_type_t;

/* this routine tries to complete message processing on message
 * engine (eng). Return value:
 * - false: no progress was observed on the descriptor
 * - true: one more message was successfuly processed
  */

typedef struct  pmixp_conn_struct{
	pmixp_io_engine_t *eng;
	void *hdr;
	void (*rcv_progress_cb)(struct pmixp_conn_struct *conn,
				void *hdr, void *msg);
	pmixp_conn_proto_t proto;
	pmixp_conn_type_t type;
	void (*ret_cb)(struct  pmixp_conn_struct *conn);
	void *ret_data;
} pmixp_conn_t;

typedef void (*pmixp_conn_new_msg_cb_t)(pmixp_conn_t *conn,
					void *hdr, void *msg);
typedef void (*pmixp_conn_ret_cb_t)(pmixp_conn_t *conn);

void pmixp_conn_init(pmixp_p2p_data_t slurm_hdr,
		     pmixp_p2p_data_t direct_hdr);
void pmixp_conn_fini(void);
void pmixp_conn_cleanup(void);

pmixp_conn_t *pmixp_conn_new_temp(pmixp_conn_proto_t proto, int fd,
				  pmixp_conn_new_msg_cb_t msg_cb);
pmixp_conn_t *pmixp_conn_new_persist(pmixp_conn_proto_t proto,
				     pmixp_io_engine_t *eng,
				     pmixp_conn_new_msg_cb_t msg_cb,
				     pmixp_conn_ret_cb_t ret_cb,
				     void *conn_data);
void pmixp_conn_return(pmixp_conn_t *hndl);

static inline bool pmixp_conn_is_alive(pmixp_conn_t *conn)
{
	return pmixp_io_operating(conn->eng);
}

static inline bool pmixp_conn_progress_rcv(pmixp_conn_t *conn)
{
	bool ret = false;
	if (NULL == conn->hdr) {
		/* allocate at the first use */
		conn->hdr = pmixp_io_recv_hdr_alloc_host(conn->eng);
	}
	/* slurm */
	pmixp_io_rcvd_progress(conn->eng);
	if (pmixp_io_rcvd_ready(conn->eng)) {
		void *msg = pmixp_io_rcvd_extract(conn->eng, conn->hdr);
		conn->rcv_progress_cb(conn, conn->hdr, msg);
		ret = true;
	}

	return ret;
}

static inline void pmixp_conn_progress_snd(pmixp_conn_t *conn)
{
	pmixp_io_send_progress(conn->eng);
}

static inline pmixp_io_engine_t *pmixp_conn_get_eng(pmixp_conn_t *conn)
{
	return conn->eng;
}

static inline void *pmixp_conn_get_data(pmixp_conn_t *conn)
{
	return conn->ret_data;
}

#endif // PMIXP_DIRECT_CONN_H
