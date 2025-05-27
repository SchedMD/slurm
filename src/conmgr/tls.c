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
#include "src/conmgr/tls_fingerprint.h"
#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

#include "src/interfaces/tls.h"

#define HANDLE_ENC_ARGS_MAGIC 0x2a4afb43
typedef struct {
	int magic; /* HANDLE_ENC_ARGS_MAGIC */
	int index;
	conmgr_fd_t *con;
	ssize_t wrote;
} handle_enc_args_t;

static void _shift_buf_bytes(buf_t *buf, const size_t bytes)
{
	void *start = NULL;
	size_t remain = 0;

	xassert(get_buf_offset(buf) >= bytes);

	if (get_buf_offset(buf) == bytes) {
		set_buf_offset(buf, 0);
		return;
	}

	start = (((void *) get_buf_data(buf)) + bytes);
	remain = (get_buf_offset(buf) - bytes);

	xassert(remain > 0);
	xassert(remain < size_buf(buf));

	(void) memcpy(get_buf_data(buf), start, remain);

	set_buf_offset(buf, remain);
}

static void _post_wait_close_fds(bool locked, conmgr_fd_t *con)
{
	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	xassert(con_flag(con, FLAG_TLS_WAIT_ON_CLOSE));

	close_con(true, con);

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

/*
 * Check and enforce if TLS has requested wait on operations and then close
 * connection
 */
static void _wait_close(bool locked, conmgr_fd_t *con)
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

	log_flag(CONMGR, "%s: [%s] closing via tls_g_destroy()",
		 __func__, con->name);

	errno = SLURM_SUCCESS;
	tls_g_destroy_conn(tls, false);
	if ((rc = errno))
		log_flag(CONMGR, "%s: [%s] tls_g_destroy() failed: %s",
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

static int _recv(void *io_context, uint8_t *buf, uint32_t len)
{
	conmgr_fd_t *con = io_context;
	size_t bytes = 0;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->tls);
	xassert(con->tls_in);
	xassert(con_flag(con, FLAG_WORK_ACTIVE));

	if (!(bytes = get_buf_offset(con->tls_in))) {
		if (con_flag(con, FLAG_READ_EOF)) {
			log_flag(CONMGR, "%s: [%s] recv() returning EOF",
				 __func__, con->name);
			return 0;
		}

		log_flag(CONMGR, "%s: [%s] recv() returning EWOULDBLOCK",
			 __func__, con->name);
		errno = EWOULDBLOCK;
		return -1;
	}

	if (bytes > len)
		bytes = len;

	log_flag_hex_range(NET_RAW, get_buf_data(con->tls_in),
			   get_buf_offset(con->tls_in), 0, bytes,
			   "[%s] TLS recv() %u/%u bytes encrypted",
			   con->name, bytes, len);

	(void) memcpy(buf, get_buf_data(con->tls_in), bytes);
	_shift_buf_bytes(con->tls_in, bytes);

	return bytes;
}

extern void tls_handle_decrypt(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	buf_t *buf = con->in;
	void *start = NULL;
	size_t need = -1, readable = -1;
	ssize_t read_c = -1;
	int rc = EINVAL;
	int try = 0;

again:
	slurm_mutex_lock(&mgr.mutex);
	if (con_flag(con, FLAG_ON_DATA_TRIED) ||
	    con_flag(con, FLAG_TLS_WAIT_ON_CLOSE)) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char *flags = con_flags_string(con->flags);
			log_flag(NET, "%s: [%s] skipping with flags=%s",
				 __func__, con->name, flags);
			xfree(flags);
		}
		slurm_mutex_unlock(&mgr.mutex);
		return;
	}
	slurm_mutex_unlock(&mgr.mutex);

	if (try > 1) {
		log_flag(NET, "%s: [%s] need >%d bytes of incoming data to decrypted TLS",
				 __func__, con->name,
				 get_buf_offset(con->tls_in));

		slurm_mutex_lock(&mgr.mutex);
		/* lock to tell mgr that we are done for now */
		con_set_flag(con, FLAG_ON_DATA_TRIED);
		slurm_mutex_unlock(&mgr.mutex);
		return;
	}

	if ((need = get_buf_offset(con->tls_in)) <= 0) {
		log_flag(NET, "%s: [%s] already decrypted all incoming TLS data",
			 __func__, con->name);
		return;
	}

	if ((rc = try_grow_buf_remaining(buf, need))) {
		error("%s: [%s] unable to allocate larger input buffer for TLS data: %s",
		      __func__, con->name, slurm_strerror(rc));
		_wait_close(false, con);
		return;
	}

	readable = remaining_buf(buf);
	start = ((void *) get_buf_data(buf)) + get_buf_offset(buf);

	xassert(readable >= need);
	xassert(con->tls);
	xassert(con->magic == MAGIC_CON_MGR_FD);

	/* TLS will callback to _recv() to read from con->tls_in*/
	read_c = tls_g_recv(con->tls, start, readable);

	if (read_c < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			log_flag(NET, "%s: [%s] TLS would block on tls_g_recv()",
				 __func__, con->name);
			return;
		}

		log_flag(NET, "%s: [%s] error while decrypting TLS: %m",
			 __func__, con->name);

		_wait_close(false, con);
		return;
	} else if (read_c == 0) {
		log_flag(NET, "%s: [%s] read EOF with %u bytes previously decrypted",
			 __func__, con->name, get_buf_offset(buf));

		slurm_mutex_lock(&mgr.mutex);
		/* lock to tell mgr that we are done */
		con_set_flag(con, FLAG_READ_EOF);
		slurm_mutex_unlock(&mgr.mutex);

		return;
	} else {
		log_flag(NET, "%s: [%s] decrypted TLS %zd/%zd bytes with %u bytes previously decrypted",
			 __func__, con->name, read_c, readable,
			 get_buf_offset(buf));
		log_flag_hex_range(NET_RAW, get_buf_data(buf),
				   (get_buf_offset(buf) + read_c),
				   get_buf_offset(buf),
				   (get_buf_offset(buf) + read_c),
				   "%s: [%s] decrypted", __func__, con->name);

		set_buf_offset(buf, (get_buf_offset(buf) + read_c));

		if (get_buf_offset(con->tls_in) > 0) {
			try++;
			goto again;
		}
	}
}

static int _send(void *io_context, const uint8_t *src, uint32_t len)
{
	conmgr_fd_t *con = io_context;
	buf_t *buf = NULL;
	void *dst = NULL;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->tls);
	xassert(con_flag(con, FLAG_WORK_ACTIVE));

	if (!(dst = try_xmalloc(len)) || !(buf = create_buf(dst, len))) {
		xfree(dst);
		errno = ENOMEM;
		return -1;
	}

	log_flag_hex(NET_RAW, src, len, "[%s] TLS send encrypted", con->name);

	(void) memcpy(dst, src, len);

	slurm_mutex_lock(&mgr.mutex);

	list_append(con->tls_out, buf);

	if (con_flag(con, FLAG_WATCH_WRITE_TIMEOUT))
		con->last_write = timespec_now();

	slurm_mutex_unlock(&mgr.mutex);

	return len;
}

/* WARNING: caller must not hold mgr->mutex lock */
static void _negotiate(conmgr_fd_t *con, void *tls)
{
	int rc = tls_g_negotiate_conn(tls);

	if (rc == EWOULDBLOCK) {
		log_flag(CONMGR, "%s: [%s] tls_g_negotiate_conn() requires more incoming data",
				 __func__, con->name);

		slurm_mutex_lock(&mgr.mutex);

		xassert(!con_flag(con, FLAG_IS_TLS_CONNECTED));
		xassert(con_flag(con, FLAG_TLS_SERVER) ||
			con_flag(con, FLAG_TLS_CLIENT));
		xassert(!con_flag(con, FLAG_WAIT_ON_FINGERPRINT));
		xassert(con_flag(con, FLAG_WORK_ACTIVE));
		xassert(!con_flag(con, FLAG_TLS_WAIT_ON_CLOSE));

		/* Wait for more incoming data before trying again */
		con_set_flag(con, FLAG_ON_DATA_TRIED);

		slurm_mutex_unlock(&mgr.mutex);
		return;
	} else if (rc) {
		log_flag(CONMGR, "%s: [%s] tls_g_negotiate_tls() failed: %s",
				 __func__, con->name, slurm_strerror(rc));
		_wait_close(false, con);
		return;
	} else {
		slurm_mutex_lock(&mgr.mutex);

		xassert(!con_flag(con, FLAG_IS_TLS_CONNECTED));
		xassert(con_flag(con, FLAG_TLS_SERVER) ||
			con_flag(con, FLAG_TLS_CLIENT));
		xassert(!con_flag(con, FLAG_WAIT_ON_FINGERPRINT));
		xassert(con_flag(con, FLAG_WORK_ACTIVE));
		xassert(!con_flag(con, FLAG_TLS_WAIT_ON_CLOSE));
		xassert(con->tls == tls);

		con_set_flag(con, FLAG_IS_TLS_CONNECTED);

		if (con->events->on_connection)
			queue_on_connection(con);

		slurm_mutex_unlock(&mgr.mutex);

		log_flag(CONMGR, "%s: [%s] TLS connected", __func__, con->name);
	}
}

extern void tls_create(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	conn_args_t tls_args = {
		.input_fd = -1,
		.output_fd = -1,
		.defer_blinding = true,
		.callbacks = {
			.recv = _recv,
			.send = _send,
			.io_context = con,
		},
		.defer_negotiation = true,
	};
	int rc = SLURM_ERROR;
	void *tls = NULL;
	buf_t *tls_in = NULL;
	list_t *tls_out = NULL;

	if (tls_g_init() || !tls_available()) {
		log_flag(CONMGR, "%s: [%s] TLS disabled: Unable to secure connection. Closing connection.",
			 __func__, con->name);

		close_con(true, con);
		close_con_output(true, con);
		return;
	}

	slurm_mutex_lock(&mgr.mutex);

	xassert(con_flag(con, FLAG_TLS_CLIENT) ^
		con_flag(con, FLAG_TLS_SERVER));
	xassert(!con_flag(con, FLAG_IS_TLS_CONNECTED));
	xassert(con_flag(con, FLAG_IS_CONNECTED));
	xassert(!con_flag(con, FLAG_WAIT_ON_FINGERPRINT));

	if ((con->input_fd < 0) || (con->output_fd < 0)) {
		xassert(con_flag(con, FLAG_READ_EOF));
		slurm_mutex_unlock(&mgr.mutex);

		log_flag(CONMGR, "%s: [%s] skip TLS create due to partial connection",
			 __func__, con->name);
		return;
	}

	if ((tls = con->tls)) {
		slurm_mutex_unlock(&mgr.mutex);

		log_flag(CONMGR, "%s: [%s] attempting TLS negotiation again",
			 __func__, con->name);

		_negotiate(con, tls);
		return;
	}

	xassert(con->input_fd >= 0);
	xassert(con->output_fd >= 0);
	xassert(!con->tls_in);
	xassert(!con->tls_out);
	/* Should not be any outgoing data yet */
	xassert(list_is_empty(con->out));

	slurm_mutex_unlock(&mgr.mutex);

	tls_in = create_buf(xmalloc(BUFFER_START_SIZE), BUFFER_START_SIZE);
	tls_out = list_create((ListDelF) free_buf);

	if (get_buf_offset(con->in)) {
		/*
		 * Need to move the TLS handshake to con->tls_in to allow tls
		 * plugin to read the handshake
		 */
		const size_t bytes = get_buf_offset(con->in);

		if ((rc = try_grow_buf_remaining(tls_in, bytes))) {
			FREE_NULL_BUFFER(tls_in);
			FREE_NULL_LIST(tls_out);

			log_flag(CONMGR, "%s: [%s] out of memory for TLS handshake: %s",
				 __func__, con->name, slurm_strerror(rc));

			close_con(false, con);
			return;
		}

		log_flag_hex(NET_RAW, get_buf_data(con->in), bytes,
			     "[%s] transferring for decryption", con->name);

		(void) memcpy(get_buf_data(tls_in), get_buf_data(con->in),
			      bytes);

		set_buf_offset(con->in, 0);
		set_buf_offset(tls_in, bytes);

		xassert(!con_flag(con, FLAG_ON_DATA_TRIED));
	}

	slurm_mutex_lock(&mgr.mutex);

	xassert(!con->tls);
	xassert(!con->tls_in);
	xassert(!con->tls_out);
	xassert(con_flag(con, FLAG_TLS_CLIENT) ^
		con_flag(con, FLAG_TLS_SERVER));

	if (con_flag(con, FLAG_TLS_CLIENT))
		tls_args.mode = TLS_CONN_CLIENT;
	else if (con_flag(con, FLAG_TLS_SERVER))
		tls_args.mode = TLS_CONN_SERVER;

	xassert(tls_args.mode != TLS_CONN_NULL);
	xassert(con->input_fd >= 0);
	xassert(con->output_fd >= 0);

	con->tls_in = tls_in;
	con->tls_out = tls_out;

	slurm_mutex_unlock(&mgr.mutex);

	if (!(tls = tls_g_create_conn(&tls_args))) {
		rc = errno;
		log_flag(CONMGR, "%s: [%s] tls_g_create() failed: %s",
			 __func__, con->name, slurm_strerror(rc));

		slurm_mutex_lock(&mgr.mutex);

		xassert(!con_flag(con, FLAG_IS_TLS_CONNECTED));
		xassert(!con->tls);

		close_con(true, con);
		con->tls_in = NULL;
		con->tls_out = NULL;

		slurm_mutex_unlock(&mgr.mutex);

		FREE_NULL_BUFFER(tls_in);
		FREE_NULL_LIST(tls_out);
	} else {
		log_flag(CONMGR, "%s: [%s] tls_g_create() success",
			 __func__, con->name);

		slurm_mutex_lock(&mgr.mutex);

		xassert(!con_flag(con, FLAG_IS_TLS_CONNECTED));

		xassert(!con->tls);
		con->tls = tls;
		xassert(con->tls_in == tls_in);
		xassert(con->tls_out == tls_out);

		slurm_mutex_unlock(&mgr.mutex);

		_negotiate(con, tls);
	}
}

extern void tls_adopt(conmgr_fd_t *con, void *tls_conn)
{
	conn_callbacks_t callbacks = {
		.recv = _recv,
		.send = _send,
		.io_context = con,
	};
	int rc;

	xassert(tls_conn);
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con_flag(con, FLAG_TLS_CLIENT) ||
		con_flag(con, FLAG_TLS_SERVER));

	con->tls = tls_conn;
	con->tls_in = create_buf(xmalloc(BUFFER_START_SIZE), BUFFER_START_SIZE);
	con->tls_out = list_create((ListDelF) free_buf);

	/* Can't finger print existing TLS connections */
	con_unset_flag(con, FLAG_WAIT_ON_FINGERPRINT);

	if ((rc = tls_g_set_conn_callbacks(tls_conn, &callbacks))) {
		log_flag(CONMGR, "%s: [%s] adopting TLS state failed: %s",
			 __func__, con->name, slurm_strerror(rc));

		con_set_flag(con, FLAG_READ_EOF);
	} else {
		log_flag(CONMGR, "%s: [%s] adopted TLS state",
			 __func__, con->name);

		/* TLS state must already be connected */
		con_set_flag(con, FLAG_IS_TLS_CONNECTED);
	}
}

extern void tls_handle_read(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;

	xassert(!con_flag(con, FLAG_WAIT_ON_FINGERPRINT));
	xassert(con->tls);
	xassert(con_flag(con, FLAG_TLS_CLIENT) ||
		con_flag(con, FLAG_TLS_SERVER));

	read_input(con, con->tls_in, "input TLS buffer");
}

extern void tls_handle_write(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	const size_t count = list_count(con->tls_out);

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con_flag(con, FLAG_TLS_CLIENT) ||
		con_flag(con, FLAG_TLS_SERVER));
	xassert(!con_flag(con, FLAG_WAIT_ON_FINGERPRINT));

	if (count)
		write_output(con, count, con->tls_out);
}

static int _foreach_write_tls(void *x, void *key)
{
	buf_t *out = x;
	handle_enc_args_t *args = key;
	conmgr_fd_t *con = args->con;
	void *start = ((void *) get_buf_data(out)) + get_buf_offset(out);

	xassert(out->magic == BUF_MAGIC);
	xassert(args->magic == HANDLE_ENC_ARGS_MAGIC);
	xassert(con->magic == MAGIC_CON_MGR_FD);

	args->wrote = tls_g_send(con->tls, start, remaining_buf(out));
	if (args->wrote < 0) {
		error("%s: [%s] tls_g_send() failed: %m", __func__, con->name);
		return SLURM_ERROR;
	} else if (!args->wrote) {
		log_flag(NET, "%s: [%s] encrypt[%d] of 0/%u bytes to outgoing fd %u",
			 __func__, args->con->name, args->index,
			 remaining_buf(out), args->con->output_fd);
		return 0;
	}

	if (args->wrote >= remaining_buf(out)) {
		log_flag(NET, "%s: [%s] completed encrypt[%d] of %u/%u bytes to outgoing fd %u",
			 __func__, args->con->name, args->index,
			 remaining_buf(out), size_buf(out),
			 args->con->output_fd);
		log_flag_hex_range(NET_RAW, get_buf_data(out), size_buf(out),
				   get_buf_offset(out),
				   (get_buf_offset(out) + args->wrote),
				   "%s: [%s] completed encrypt[%d] of %u/%u bytes",
				   __func__, args->con->name, args->index,
				   remaining_buf(out), size_buf(out));

		args->wrote -= remaining_buf(out);
		args->index++;
		return 1;
	} else {
		log_flag(CONMGR, "%s: [%s] partial encrypt[%d] of %zd/%u bytes to outgoing fd %u",
			 __func__, args->con->name, args->index,
			 args->wrote, size_buf(out), args->con->output_fd);
		log_flag_hex_range(NET_RAW, get_buf_data(out), size_buf(out),
				   get_buf_offset(out),
				   (get_buf_offset(out) + args->wrote),
				   "%s: [%s] partial encrypt[%d] of %zd/%u bytes",
				   __func__, args->con->name, args->index,
				   args->wrote, size_buf(out));

		set_buf_offset(out, get_buf_offset(out) + args->wrote);
		args->wrote = 0;
		args->index++;
		return 0;
	}
}

extern void tls_handle_encrypt(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	handle_enc_args_t args = {
		.magic = HANDLE_ENC_ARGS_MAGIC,
		.con = con,
	};

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->tls);
	xassert(con_flag(con, FLAG_TLS_CLIENT) ||
		con_flag(con, FLAG_TLS_SERVER));

	if (list_delete_all(con->out, _foreach_write_tls, &args) < 0) {
		error("%s: [%s] _foreach_write_tls() failed",
		      __func__, con->name);
		/* drop outbound data on the floor */
		list_flush(con->out);
		_wait_close(false, con);
	}
}

extern int on_fingerprint_tls(conmgr_fd_t *con, const void *buffer,
			      const size_t bytes, void *arg)
{
	int match = EINVAL;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	slurm_mutex_lock(&mgr.mutex);

	if (con_flag(con, FLAG_TLS_CLIENT) || con_flag(con, FLAG_TLS_SERVER)) {
		slurm_mutex_unlock(&mgr.mutex);

		log_flag(CONMGR, "%s: [%s] skipping TLS fingerprinting as TLS already activated",
				 __func__, con->name);

		return SLURM_SUCCESS;
	}

	xassert(!con->tls);
	xassert(!con->tls_in);
	xassert(!con->tls_out);
	xassert(!con_flag(con, FLAG_TLS_CLIENT));
	xassert(!con_flag(con, FLAG_TLS_SERVER));
	xassert(!con_flag(con, FLAG_IS_TLS_CONNECTED));
	xassert(!con_flag(con, FLAG_READ_EOF));
	xassert(!con_flag(con, FLAG_IS_LISTEN));
	xassert(con_flag(con, FLAG_IS_CONNECTED));
	xassert(con_flag(con, FLAG_WAIT_ON_FINGERPRINT));

	slurm_mutex_unlock(&mgr.mutex);

	if (!(match = tls_is_handshake(get_buf_data(con->in),
				       get_buf_offset(con->in), con->name))) {
		log_flag(CONMGR, "%s: [%s] TLS matched",
			 __func__, con->name);

		slurm_mutex_lock(&mgr.mutex);

		/* Only servers can accept an incoming TLS handshake requests */
		con_set_flag(con, FLAG_TLS_SERVER);

		slurm_mutex_unlock(&mgr.mutex);
		return SLURM_SUCCESS;
	} else if (match == EWOULDBLOCK) {
		log_flag(CONMGR, "%s: [%s] waiting for more bytes for TLS match",
				 __func__, con->name);

		slurm_mutex_lock(&mgr.mutex);
		con_set_flag(con, FLAG_ON_DATA_TRIED);
		slurm_mutex_unlock(&mgr.mutex);

		return EWOULDBLOCK;
	} else if (match == ENOENT) {
		log_flag(CONMGR, "%s: [%s] TLS not detected",
			 __func__, con->name);
		return SLURM_SUCCESS;
	}

	fatal_abort("should never happen");
}

extern int tls_extract(conmgr_fd_t *con, extract_fd_t *extract)
{
	int rc;

	if (con->input_fd < 0) {
		log_flag(CONMGR, "%s: [%s] invalid input_fd",
			 __func__, con->name);
		close_con(true, con);
		return EBADF;
	}

	if (con->output_fd < 0) {
		log_flag(CONMGR, "%s: [%s] invalid output_fd",
			 __func__, con->name);
		close_con(true, con);
		return EBADF;
	}

	if ((rc = tls_g_set_conn_fds(con->tls, con->input_fd,
				     con->output_fd))) {
		log_flag(CONMGR, "%s: [%s] tls_g_set_fds() failed: %s",
			 __func__, con->name, slurm_strerror(rc));
		close_con(true, con);
		return rc;
	}

	/* Take the TLS state for extraction */
	SWAP(extract->tls_conn, con->tls);

	return rc;
}
