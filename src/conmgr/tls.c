/*****************************************************************************\
 *  tls.c - definitions for TLS work in connection manager
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/tls.h"
#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

#include "src/interfaces/tls.h"

static void _post_wait_close_fds(bool locked, conmgr_fd_t *con)
{
	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	xassert(con_flag(con, FLAG_TLS_WAIT_ON_CLOSE));

	close_con(true, con);
	close_con_output(true, con);

	con_unset_flag(con, FLAG_TLS_WAIT_ON_CLOSE);

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}

static void _delayed_close(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;

	log_flag(CONMGR, "%s: [%s] close wait complete", __func__, con->name);

	_post_wait_close_fds(false, con);
}

extern void tls_wait_close(bool locked, conmgr_fd_t *con)
{
	timespec_t delay = { 0 };

	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	xassert(!con_flag(con, FLAG_TLS_WAIT_ON_CLOSE));

	/* Soft close the connection to stop any more activity */
	con_set_polling(con, PCTL_TYPE_NONE, __func__);
	con_set_flag(con, FLAG_READ_EOF);
	con_set_flag(con, FLAG_TLS_WAIT_ON_CLOSE);
	con_unset_flag(con, FLAG_CAN_WRITE);
	con_unset_flag(con, FLAG_CAN_READ);

	xassert(con->tls);
	delay = tls_g_get_delay(con->tls);

	if (delay.tv_sec) {
		log_flag(CONMGR, "%s: [%s] deferring close",
			 __func__, con->name);

		add_work_con_delayed_abs_fifo(true, con, _delayed_close, NULL,
					      delay);
	} else {
		log_flag(CONMGR, "%s: [%s] closing now",
			 __func__, con->name);

		_post_wait_close_fds(true, con);
	}

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}

extern void tls_close(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	void *tls = NULL;
	int rc = EINVAL;
	buf_t *tls_in = NULL;
	list_t *tls_out = NULL;

	slurm_mutex_lock(&mgr.mutex);

	xassert(con->tls);
	xassert(con_flag(con, FLAG_TLS_CLIENT) ^
		con_flag(con, FLAG_TLS_SERVER));
	xassert(con->input_fd == -1);
	xassert(con_flag(con, FLAG_READ_EOF));
	xassert(!con_flag(con, FLAG_TLS_WAIT_ON_CLOSE));

	tls = con->tls;

	slurm_mutex_unlock(&mgr.mutex);

	if (!tls) {
		log_flag(CONMGR, "%s: [%s] closing TLS state skipped",
			 __func__, con->name);
		return;
	}

	log_flag(CONMGR, "%s: [%s] closing via tls_g_destroy_conn()",
		 __func__, con->name);

	errno = SLURM_SUCCESS;
	tls_g_destroy_conn(tls);
	if ((rc = errno))
		log_flag(CONMGR, "%s: [%s] tls_g_destroy_conn() failed: %s",
			 __func__, con->name, slurm_strerror(rc));


	slurm_mutex_lock(&mgr.mutex);
	xassert(tls == con->tls);
	con->tls = NULL;

	SWAP(tls_in, con->tls_in);
	SWAP(tls_out, con->tls_out);
	slurm_mutex_unlock(&mgr.mutex);

	FREE_NULL_BUFFER(tls_in);
	FREE_NULL_LIST(tls_out);
}

extern void tls_create(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	tls_conn_args_t tls_args = {
		.defer_blinding = true,
	};
	int rc = SLURM_ERROR;
	void *tls = NULL;
	buf_t *tls_in = NULL;
	list_t *tls_out = NULL;

	if (!tls_enabled()) {
		log_flag(CONMGR, "%s: [%s] TLS disabled: Unable to secure connection. Closing connection.",
			 __func__, con->name);

		close_con(true, con);
		close_con_output(true, con);
		return;
	}

	slurm_mutex_lock(&mgr.mutex);

	xassert(!con->tls);
	xassert(con_flag(con, FLAG_TLS_CLIENT) ^
		con_flag(con, FLAG_TLS_SERVER));

	if (con_flag(con, FLAG_TLS_CLIENT))
		tls_args.mode = TLS_CONN_CLIENT;
	else if (con_flag(con, FLAG_TLS_SERVER))
		tls_args.mode = TLS_CONN_SERVER;

	xassert(tls_args.mode != TLS_CONN_NULL);
	xassert(con->input_fd >= 0);
	xassert(con->output_fd >= 0);
	xassert(!con->tls_in);
	xassert(!con->tls_out);
	/* Should not be any outgoing data yet */
	xassert(list_is_empty(con->out));

	tls_args.input_fd = con->input_fd;
	tls_args.output_fd = con->output_fd;

	slurm_mutex_unlock(&mgr.mutex);

	tls_in = create_buf(xmalloc(BUFFER_START_SIZE), BUFFER_START_SIZE);
	tls_out = list_create((ListDelF) free_buf);

	/* TLS operations must have a blocking FD */
	fd_set_blocking(tls_args.input_fd);
	if (tls_args.input_fd != tls_args.output_fd)
		fd_set_blocking(tls_args.output_fd);

	errno = SLURM_SUCCESS;
	tls = tls_g_create_conn(&tls_args);
	/* Capture errno before it can be clobbered */
	rc = errno;

	/* Revert back to non-blocking */
	fd_set_nonblocking(tls_args.input_fd);
	if (tls_args.input_fd != tls_args.output_fd)
		fd_set_nonblocking(tls_args.output_fd);

	if (rc || !tls) {
		log_flag(CONMGR, "%s: [%s] tls_g_create_conn() failed: %s",
			 __func__, con->name, slurm_strerror(rc));

		slurm_mutex_lock(&mgr.mutex);

		xassert(!con->tls);
		con->tls = tls;
		xassert(!con->tls_in);
		con->tls_in = tls_in;
		xassert(!con->tls_out);
		con->tls_out = tls_out;

		tls_wait_close(true, con);

		slurm_mutex_unlock(&mgr.mutex);
	} else {
		log_flag(CONMGR, "%s: [%s] TLS handshake completed successfully",
			 __func__, con->name);

		slurm_mutex_lock(&mgr.mutex);

		con_set_flag(con, FLAG_IS_TLS_CONNECTED);

		xassert(!con->tls);
		con->tls = tls;
		xassert(!con->tls_in);
		con->tls_in = tls_in;
		xassert(!con->tls_out);
		con->tls_out = tls_out;

		xassert(con->input_fd == tls_args.input_fd);
		xassert(con->output_fd == tls_args.output_fd);

		slurm_mutex_unlock(&mgr.mutex);
	}
}
