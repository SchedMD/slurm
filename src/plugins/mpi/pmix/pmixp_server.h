/*****************************************************************************\
 **  pmix_server.h - PMIx server side functionality
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
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

#ifndef PMIXP_SERVER_H
#define PMIXP_SERVER_H

#include "pmixp_common.h"

typedef enum {
	PMIXP_MSG_NONE,
	PMIXP_MSG_FAN_IN,
	PMIXP_MSG_FAN_OUT,
	PMIXP_MSG_DMDX,
	PMIXP_MSG_INIT_DIRECT,
#ifndef NDEBUG
	PMIXP_MSG_PINGPONG
#endif
} pmixp_srv_cmd_t;

typedef enum {
	/* use non as to check non-init case */
	PMIXP_EP_NONE = 0,
	PMIXP_EP_HLIST,
	PMIXP_EP_NOIDEID
} pmixp_ep_type_t;

typedef struct {
	pmixp_ep_type_t type;
	union {
		char *hostlist;
		int nodeid;
	} ep;
} pmixp_ep_t;

typedef void (*pmixp_server_sent_cb_t)(int rc, pmixp_p2p_ctx_t ctx,
				       void *cb_data);
/* convenience callback to just release sent buffer
 * expects an object of type `Buf` to be passed as `cb_data`
 */
void pmixp_server_sent_buf_cb(int rc, pmixp_p2p_ctx_t ctx, void *data);

int pmixp_stepd_init(const stepd_step_rec_t *job, char ***env);
int pmixp_stepd_finalize(void);
void pmixp_server_cleanup(void);
int pmix_srun_init(const mpi_plugin_client_info_t *job, char ***env);
void pmixp_server_slurm_conn(int fd);
void pmixp_server_direct_conn(int fd);
int pmixp_server_send_nb(pmixp_ep_t *ep, pmixp_srv_cmd_t type,
			 uint32_t seq, Buf buf,
			 pmixp_server_sent_cb_t complete_cb,
			 void *cb_data);
Buf pmixp_server_buf_new(void);
size_t pmixp_server_buf_reset(Buf buf);

#ifndef NDEBUG
/* Debug tools used only if debug was enabled */
void pmixp_server_init_pp(char ***env);
bool pmixp_server_want_pp(void);
void pmixp_server_run_pp(void);
int pmixp_server_pp_send(int nodeid, int size);
int pmixp_server_pp_count(void);
void pmixp_server_pp_inc(void);
void pmixp_server_pp_start(void);
int pmixp_server_pp_warmups(void);
int pmixp_server_pp_same_thread(void);
bool pmixp_server_pp_check_fini(int size);

void pmixp_server_init_cperf(char ***env);
bool pmixp_server_want_cperf();
void pmixp_server_run_cperf();

#else
/* Stubs for the initialization code */
#define pmixp_server_want_pp() (0)
#define pmixp_server_run_pp()
#define pmixp_server_init_pp(env)

#define pmixp_server_init_cperf(env)
#define pmixp_server_want_cperf() (0)
#define pmixp_server_run_cperf();
#endif

static inline void pmixp_server_buf_reserve(Buf buf, uint32_t size)
{
	if (remaining_buf(buf) < size) {
		uint32_t to_reserve = size - remaining_buf(buf);
		grow_buf(buf, to_reserve);
	}
}

#endif /* PMIXP_SERVER_H */
