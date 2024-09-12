/*****************************************************************************\
 *  io.c - definitions for connection I/O in connection manager
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

#define _GNU_SOURCE
#include <limits.h>
#include <sys/uio.h>

#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

#define DEFAULT_READ_BYTES 512

/*
 * Default number of write()s to queue up using the stack instead of xmalloc().
 * Avoid the slow down from calling xmalloc() on a majority of the writev()s.
 */
#define IOV_STACK_COUNT 16
#define HANDLE_WRITEV_ARGS_MAGIC 0x1a4afb40
typedef struct {
	int magic; /* HANDLE_WRITEV_ARGS_MAGIC */
	int index;
	const int iov_count;
	conmgr_fd_t *con;
	struct iovec *iov;
	ssize_t wrote;
} handle_writev_args_t;

extern void resize_input_buffer(conmgr_callback_args_t conmgr_args, void *arg)
{
	int rc;
	uint64_t bytes = (uint64_t) arg;
	conmgr_fd_t *con = conmgr_args.con;

	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED)
		return;

	xassert(bytes > 0);
	xassert(bytes < MAX_MSG_SIZE);

	if (!(rc = try_grow_buf_remaining(con->in, bytes)))
		return;

	log_flag(NET, "%s: [%s] unable to increase buffer %"PRIu64" bytes for RPC message: %s",
		 __func__, con->name, bytes, slurm_strerror(rc));

	/* conmgr will be unable to read entire RPC -> close connection now */
	close_con(false, con);
}

static int _get_fd_readable(conmgr_fd_t *con)
{
	int readable = 0;

	if (fd_get_readable_bytes(con->input_fd, &readable, con->name) ||
	    !readable) {
		if (con->mss != NO_VAL)
			readable = con->mss;
		else
			readable = DEFAULT_READ_BYTES;
	}

	/*
	 * Limit read byte count to avoid creating huge buffers from a huge MSS
	 * on a loopback device or a buggy device driver.
	 */
	readable = MIN(readable, MAX_MSG_SIZE);

	/*
	 * Even if there are zero bytes to read, we want to make sure that we
	 * already try to do the read to avoid a shutdown(SHUT_RDWR) file
	 * descriptor never getting the final read()=0.
	 */
	readable = MAX(readable, DEFAULT_READ_BYTES);

	return readable;
}

extern void handle_read(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	ssize_t read_c;
	int rc, readable;

	con_unset_flag(con, FLAG_CAN_READ);
	xassert(con->magic == MAGIC_CON_MGR_FD);

	if (con->input_fd < 0) {
		log_flag(NET, "%s: [%s] called on closed connection",
			 __func__, con->name);
		return;
	}

	readable = _get_fd_readable(con);

	/* Grow buffer as needed to handle the incoming data */
	if ((rc = try_grow_buf_remaining(con->in, readable))) {
		error("%s: [%s] unable to allocate larger input buffer: %s",
		      __func__, con->name, slurm_strerror(rc));
		close_con(false, con);
		return;
	}

	/* check for errors with a NULL read */
	read_c = read(con->input_fd,
		      (get_buf_data(con->in) + get_buf_offset(con->in)),
		      readable);
	if (read_c == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			log_flag(NET, "%s: [%s] socket would block on read",
				 __func__, con->name);
			return;
		}

		log_flag(NET, "%s: [%s] error while reading: %m",
			 __func__, con->name);
		close_con(false, con);
		return;
	} else if (read_c == 0) {
		log_flag(NET, "%s: [%s] read EOF with %u bytes to process already in buffer",
			 __func__, con->name, get_buf_offset(con->in));

		slurm_mutex_lock(&mgr.mutex);
		/* lock to tell mgr that we are done */
		con_set_flag(con, FLAG_READ_EOF);
		slurm_mutex_unlock(&mgr.mutex);
	} else {
		log_flag(NET, "%s: [%s] read %zd bytes with %u bytes to process already in buffer",
			 __func__, con->name, read_c, get_buf_offset(con->in));
		log_flag_hex(NET_RAW,
			     (get_buf_data(con->in) + get_buf_offset(con->in)),
			     read_c, "%s: [%s] read", __func__, con->name);

		set_buf_offset(con->in, (get_buf_offset(con->in) + read_c));

		if (con_flag(con, FLAG_WATCH_READ_TIMEOUT))
			con->last_read = timespec_now();
	}
}

static int _foreach_add_writev_iov(void *x, void *arg)
{
	buf_t *out = x;
	handle_writev_args_t *args = arg;
	struct iovec *iov = &args->iov[args->index];

	xassert(out->magic == BUF_MAGIC);
	xassert(args->magic == HANDLE_WRITEV_ARGS_MAGIC);

	if (args->index >= args->iov_count)
		return -1;

	iov->iov_base = ((void *) get_buf_data(out)) + get_buf_offset(out);
	iov->iov_len = remaining_buf(out);

	log_flag(CONMGR, "%s: [%s] queued writev[%d] %u/%u bytes to outgoing fd %u",
		 __func__, args->con->name, args->index, remaining_buf(out),
		 size_buf(out), args->con->output_fd);

	args->index++;
	return 0;
}

static int _foreach_writev_flush_bytes(void *x, void *arg)
{
	buf_t *out = x;
	handle_writev_args_t *args = arg;

	xassert(out->magic == BUF_MAGIC);
	xassert(args->magic == HANDLE_WRITEV_ARGS_MAGIC);
	xassert(args->wrote >= 0);

	if (!args->wrote)
		return 0;

	if (args->wrote >= remaining_buf(out)) {
		log_flag(NET, "%s: [%s] completed write[%d] of %u/%u bytes to outgoing fd %u",
			 __func__, args->con->name, args->index,
			 remaining_buf(out), size_buf(out),
			 args->con->output_fd);
		log_flag_hex_range(NET_RAW, get_buf_data(out), size_buf(out),
				   get_buf_offset(out), size_buf(out),
				   "%s: [%s] completed write[%d] of %u/%u bytes",
				   __func__, args->con->name, args->index,
				   remaining_buf(out), size_buf(out));

		args->wrote -= remaining_buf(out);
		args->index++;
		return 1;
	} else {
		log_flag(CONMGR, "%s: [%s] partial write[%d] of %zd/%u bytes to outgoing fd %u",
			 __func__, args->con->name, args->index,
			 args->wrote, size_buf(out), args->con->output_fd);
		log_flag_hex_range(NET_RAW, get_buf_data(out), size_buf(out),
				   get_buf_offset(out), args->wrote,
				   "%s: [%s] partial write[%d] of %zd/%u bytes",
				   __func__, args->con->name, args->index,
				   args->wrote, remaining_buf(out));

		set_buf_offset(out, get_buf_offset(out) + args->wrote);
		args->wrote = 0;
		args->index++;
		return 0;
	}
}

static void _handle_writev(conmgr_fd_t *con, const int out_count)
{
	const int iov_count = MIN(IOV_MAX, out_count);
	struct iovec iov_stack[IOV_STACK_COUNT];
	handle_writev_args_t args = {
		.magic = HANDLE_WRITEV_ARGS_MAGIC,
		.iov_count = iov_count,
		.con = con,
		.iov = iov_stack,
	};

	/* Try to use stack for small write counts when possible */
	if (iov_count > ARRAY_SIZE(iov_stack))
		args.iov = xcalloc(iov_count, sizeof(*args.iov));

	(void) list_for_each_ro(con->out, _foreach_add_writev_iov, &args);
	xassert(args.index == iov_count);

	args.wrote = writev(con->output_fd, args.iov, iov_count);

	if (args.wrote == -1) {
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			log_flag(NET, "%s: [%s] retry write: %m",
				 __func__, con->name);
		} else {
			error("%s: [%s] writev(%d) failed: %m",
			      __func__, con->name, con->output_fd);
			/* drop outbound data on the floor */
			list_flush(con->out);
			close_con(false, con);
			close_con_output(false, con);
		}
	} else if (args.wrote == 0) {
		log_flag(NET, "%s: [%s] wrote 0 bytes", __func__, con->name);
	} else {
		log_flag(NET, "%s: [%s] wrote %zd bytes",
			 __func__, con->name, args.wrote);

		args.index = 0;
		(void) list_delete_all(con->out, _foreach_writev_flush_bytes,
				       &args);
		xassert(!args.wrote);

		if (con_flag(con, FLAG_WATCH_WRITE_TIMEOUT))
			con->last_write = timespec_now();
	}

	if (args.iov != iov_stack)
		xfree(args.iov);
}

extern void handle_write(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	int out_count;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	if (!(out_count = list_count(con->out)))
		log_flag(CONMGR, "%s: [%s] skipping attempt with zero writes",
			 __func__, con->name);
	else
		_handle_writev(con, out_count);
}

extern void wrap_on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	int avail = get_buf_offset(con->in);
	int size = size_buf(con->in);
	int rc;
	int (*callback)(conmgr_fd_t *con, void *arg) = NULL;
	const char *callback_string = NULL;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	/* override buffer offset to allow reading */
	set_buf_offset(con->in, 0);
	/* override buffer size to only read upto previous offset */
	con->in->size = avail;

	if (con->type == CON_TYPE_RAW) {
		callback = con->events->on_data;
		callback_string = XSTRINGIFY(con->events->on_data);
	} else if (con->type == CON_TYPE_RPC) {
		callback = on_rpc_connection_data;
		callback_string = XSTRINGIFY(on_rpc_connection_data);
	} else {
		fatal("%s: invalid type", __func__);
	}

	log_flag(CONMGR, "%s: [%s] BEGIN func=%s(arg=0x%"PRIxPTR")@0x%"PRIxPTR,
		 __func__, con->name, callback_string, (uintptr_t) con->arg,
		 (uintptr_t) callback);

	rc = callback(con, con->arg);

	log_flag(CONMGR, "%s: [%s] END func=%s(arg=0x%"PRIxPTR")@0x%"PRIxPTR"=[%d]%s",
		 __func__, con->name, callback_string, (uintptr_t) con->arg,
		 (uintptr_t) callback, rc, slurm_strerror(rc));

	if (rc) {
		error("%s: [%s] on_data returned rc: %s",
		      __func__, con->name, slurm_strerror(rc));

		slurm_mutex_lock(&mgr.mutex);
		if (mgr.exit_on_error)
			mgr.shutdown_requested = true;

		if (!mgr.error)
			mgr.error = rc;
		slurm_mutex_unlock(&mgr.mutex);

		/*
		 * processing data failed so drop any
		 * pending data on the floor
		 */
		log_flag(CONMGR, "%s: [%s] on_data callback failed. Purging the remaining %d bytes of pending input.",
			 __func__, con->name, get_buf_offset(con->in));
		set_buf_offset(con->in, 0);

		close_con(false, con);
		return;
	}

	if (get_buf_offset(con->in) < size_buf(con->in)) {
		if (get_buf_offset(con->in) > 0) {
			log_flag(CONMGR, "%s: [%s] partial read %u/%u bytes.",
				 __func__, con->name, get_buf_offset(con->in),
				 size_buf(con->in));

			/*
			 * not all data read, need to shift it to start of
			 * buffer and fix offset
			 */
			memmove(get_buf_data(con->in),
				(get_buf_data(con->in) +
				 get_buf_offset(con->in)),
				remaining_buf(con->in));

			/* reset start of offset to end of previous data */
			set_buf_offset(con->in, remaining_buf(con->in));
		} else {
			/* need more data for parser to read */
			log_flag(CONMGR, "%s: [%s] parser refused to read %u bytes. Waiting for more data.",
				 __func__, con->name, size_buf(con->in));

			con_set_flag(con, FLAG_ON_DATA_TRIED);

			/* revert offset change */
			set_buf_offset(con->in, avail);
		}
	} else
		/* buffer completely read: reset it */
		set_buf_offset(con->in, 0);

	/* restore original size */
	con->in->size = size;
}

extern int conmgr_queue_write_data(conmgr_fd_t *con, const void *buffer,
				   const size_t bytes)
{
	buf_t *buf;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	buf = init_buf(bytes);

	/* TODO: would be nice to avoid this copy */
	memmove(get_buf_data(buf), buffer, bytes);

	log_flag(NET, "%s: [%s] write of %zu bytes queued",
		 __func__, con->name, bytes);

	log_flag_hex(NET_RAW, get_buf_data(buf), get_buf_offset(buf),
		     "%s: queuing up write", __func__);

	list_append(con->out, buf);

	if (con_flag(con, FLAG_WATCH_WRITE_TIMEOUT))
		con->last_write = timespec_now();

	slurm_mutex_lock(&mgr.mutex);
	EVENT_SIGNAL(&mgr.watch_sleep);
	slurm_mutex_unlock(&mgr.mutex);
	return SLURM_SUCCESS;
}

extern void conmgr_fd_get_in_buffer(const conmgr_fd_t *con,
				    const void **data_ptr, size_t *bytes_ptr)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con_flag(con, FLAG_WORK_ACTIVE));

	if (data_ptr)
		*data_ptr = get_buf_data(con->in) + get_buf_offset(con->in);
	*bytes_ptr = size_buf(con->in);
}

extern buf_t *conmgr_fd_shadow_in_buffer(const conmgr_fd_t *con)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->type == CON_TYPE_RAW);
	xassert(con_flag(con, FLAG_WORK_ACTIVE));

	return create_shadow_buf((get_buf_data(con->in) + con->in->processed),
				 (size_buf(con->in) - con->in->processed));
}

extern void conmgr_fd_mark_consumed_in_buffer(const conmgr_fd_t *con,
					      size_t bytes)
{
	size_t offset;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con_flag(con, FLAG_WORK_ACTIVE));

	offset = get_buf_offset(con->in) + bytes;
	xassert(offset <= size_buf(con->in));
	set_buf_offset(con->in, offset);
}

extern int conmgr_fd_xfer_in_buffer(const conmgr_fd_t *con,
				    buf_t **buffer_ptr)
{
	const void *data = (get_buf_data(con->in) + get_buf_offset(con->in));
	const size_t bytes = (size_buf(con->in) - get_buf_offset(con->in));
	buf_t *buf = NULL;
	int rc;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->type == CON_TYPE_RAW);
	xassert(con_flag(con, FLAG_WORK_ACTIVE));

	xassert(buffer_ptr);
	if (!buffer_ptr)
		return EINVAL;

	/*
	 * Create buffer if needed and size it size of the data to copy or the
	 * minimal buffer size to avoid multiple recalloc()s in the future.
	 */
	if (!*buffer_ptr &&
	    !(*buffer_ptr = init_buf(MAX(bytes, BUFFER_START_SIZE))))
		return ENOMEM;

	buf = *buffer_ptr;

	/* grow buffer to size to hold incoming data (if needed) */
	if ((rc = try_grow_buf_remaining(buf, bytes)))
		return rc;

	/* Append data to existing buffer */
	memcpy((get_buf_data(buf) + get_buf_offset(buf)), data, bytes);
	set_buf_offset(buf, (get_buf_offset(buf) + bytes));

	/* mark connection input buffer as fully consumed */
	set_buf_offset(con->in, size_buf(con->in));
	return SLURM_SUCCESS;
}

extern int conmgr_fd_xfer_out_buffer(conmgr_fd_t *con, buf_t *output)
{
	int rc;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->type == CON_TYPE_RAW);
	xassert(!output || (output->magic == BUF_MAGIC));

	if (!output || !size_buf(output) || !get_buf_offset(output))
		return SLURM_SUCCESS;

	xassert(size_buf(output) <= xsize(get_buf_data(output)));
	xassert(get_buf_offset(output) <= size_buf(output));

	rc = conmgr_queue_write_data(con, get_buf_data(output),
				     get_buf_offset(output));

	if (!rc)
		set_buf_offset(output, 0);

	return rc;
}

extern int conmgr_fd_get_input_fd(conmgr_fd_t *con)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con_flag(con, FLAG_WORK_ACTIVE));
	return con->input_fd;
}

extern int conmgr_fd_get_output_fd(conmgr_fd_t *con)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con_flag(con, FLAG_WORK_ACTIVE));
	return con->output_fd;
}
