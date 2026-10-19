/*****************************************************************************\
 *  persist_conn-test.c - Persistent connection server tests.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/persist_conn.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurmdbd_pack.h"
#include "src/common/workerpool.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/hash.h"

#define READ_TIMEOUT_SEC 1
/* Outlast an accidentally enabled timeout. */
#define IDLE_WAIT_SEC (READ_TIMEOUT_SEC * 3)

#define PAYLOAD_SIZE 64

/* conmgr owns the other end. */
static int peer_fd = -1;

/* Non-NULL callback argument prevents connection rejection. */
static int conn_arg = 0;

static int frames_seen = 0;
static int read_timeouts = 0;
static int write_timeouts = 0;
static int parse_rc = SLURM_SUCCESS;
static bool finished = false;
static bool send_reply = false;
static uint32_t reply_bytes = 0;
static conmgr_timeouts_t timeouts;

/* Set by main(); restored after capturing errors. */
static log_options_t log_opts = LOG_OPTS_INITIALIZER;

/* Protocol test observations. */
static struct {
	int inits, msgs, finishes;
	uint16_t last_type;
	uid_t init_uid;
	uint16_t init_version;
	char init_cluster[32];
	uint16_t init_persist_type;
	bool init_rem_host;
	int init_rc; /* on_init() result */
} ps;

static int _on_frame(conmgr_fd_ref_t *ref, buf_t *frame, void *arg)
{
	frames_seen++;

	ck_assert_int_eq(size_buf(frame), PAYLOAD_SIZE);
	ck_assert_int_eq(get_buf_offset(frame), 0);

	if (send_reply) {
		uint32_t nw_size = htonl(reply_bytes);
		char *reply = xmalloc(reply_bytes);

		/* Recognizable pattern to catch reordered or dropped bytes. */
		for (uint32_t i = 0; i < reply_bytes; i++)
			reply[i] = (char) (i % 251);
		ck_assert(!conmgr_con_queue_write_data(ref, &nw_size,
						       sizeof(nw_size)));
		ck_assert(!conmgr_con_queue_write_data(ref, reply,
						       reply_bytes));
		xfree(reply);
	}

	return SLURM_SUCCESS;
}

static void *_on_connection(conmgr_callback_args_t conmgr_args, void *arg)
{
	return arg;
}

static int _on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	parse_rc = persist_conn_parse(conmgr_args.ref, _on_frame, arg);
	return parse_rc;
}

static int _on_read_timeout(conmgr_callback_args_t conmgr_args, void *arg)
{
	read_timeouts++;

	return SLURM_COMMUNICATIONS_READ_TIMEOUT;
}

static int _on_write_timeout(conmgr_callback_args_t conmgr_args, void *arg)
{
	write_timeouts++;
	return SLURM_COMMUNICATIONS_WRITE_TIMEOUT;
}

static void _on_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	finished = true;

	conmgr_request_shutdown();
}

static const conmgr_events_t events = {
	.on_connection = _on_connection,
	.on_data = _on_data,
	.on_read_timeout = _on_read_timeout,
	.on_write_timeout = _on_write_timeout,
	.on_finish = _on_finish,
};

/* Announce PAYLOAD_SIZE; send only send_bytes. */
static void _write_frame(uint32_t send_bytes)
{
	uint32_t nw_size = htonl(PAYLOAD_SIZE);
	char payload[PAYLOAD_SIZE] = { 0 };

	ck_assert(send_bytes <= PAYLOAD_SIZE);

	ck_assert_int_eq(write(peer_fd, &nw_size, sizeof(nw_size)),
			 sizeof(nw_size));

	if (send_bytes)
		ck_assert_int_eq(write(peer_fd, payload, send_bytes),
				 send_bytes);
}

static void _add_connection(void)
{
	int sv[2] = { -1, -1 };
	int send_buffer = 4096;

	ck_assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
	ck_assert(!setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &send_buffer,
			      sizeof(send_buffer)));

	peer_fd = sv[1];

	ck_assert(!conmgr_process_fd(CON_TYPE_RAW, &timeouts, sv[0], sv[0],
				     &events, CON_FLAG_NONE, NULL, 0, NULL,
				     &conn_arg));
}

static void _close_peer(conmgr_callback_args_t conmgr_args, void *arg)
{
	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED)
		return;

	ck_assert(!close(peer_fd));
	peer_fd = -1;
}

START_TEST(test_idle_connection_never_times_out)
{
	_add_connection();

	_write_frame(PAYLOAD_SIZE);

	/* End the observation window with EOF. */
	conmgr_add_work_delayed_fifo(_close_peer, NULL, IDLE_WAIT_SEC, 0);

	ck_assert(!conmgr_run(true));

	ck_assert_int_eq(frames_seen, 1);
	ck_assert_int_eq(read_timeouts, 0);
	ck_assert(finished);
}

END_TEST

START_TEST(test_partial_frame_never_times_out)
{
	_add_connection();

	_write_frame(PAYLOAD_SIZE / 2);
	conmgr_add_work_delayed_fifo(_close_peer, NULL, IDLE_WAIT_SEC, 0);

	ck_assert(!conmgr_run(true));

	ck_assert_int_eq(frames_seen, 0);
	ck_assert_int_eq(read_timeouts, 0);
	ck_assert(finished);
}

END_TEST

START_TEST(test_header_only_frame_never_times_out)
{
	_add_connection();

	_write_frame(0);
	conmgr_add_work_delayed_fifo(_close_peer, NULL, IDLE_WAIT_SEC, 0);

	ck_assert(!conmgr_run(true));

	ck_assert_int_eq(frames_seen, 0);
	ck_assert_int_eq(read_timeouts, 0);
	ck_assert(finished);
}

END_TEST

START_TEST(test_trailing_partial_frame_never_times_out)
{
	_add_connection();

	_write_frame(PAYLOAD_SIZE);
	_write_frame(PAYLOAD_SIZE / 2);
	conmgr_add_work_delayed_fifo(_close_peer, NULL, IDLE_WAIT_SEC, 0);

	ck_assert(!conmgr_run(true));

	ck_assert_int_eq(frames_seen, 1);
	ck_assert_int_eq(read_timeouts, 0);
	ck_assert(finished);
}

END_TEST

START_TEST(test_caller_read_timeout_preserved)
{
	timeouts.read.tv_sec = READ_TIMEOUT_SEC;
	timeouts.read.tv_nsec = 0;
	_add_connection();
	_write_frame(PAYLOAD_SIZE);
	conmgr_add_work_delayed_fifo(_close_peer, NULL, IDLE_WAIT_SEC, 0);

	ck_assert(!conmgr_run(true));

	ck_assert_int_eq(frames_seen, 1);
	ck_assert_int_eq(read_timeouts, 1);
	ck_assert(finished);
}

END_TEST

/* Draining input must not disable write timeouts. */
START_TEST(test_caller_write_timeout_preserved)
{
	send_reply = true;
	timeouts.write.tv_sec = READ_TIMEOUT_SEC;
	timeouts.write.tv_nsec = 0;
	_add_connection();
	_write_frame(PAYLOAD_SIZE);
	conmgr_add_work_delayed_fifo(_close_peer, NULL, IDLE_WAIT_SEC, 0);

	ck_assert(!conmgr_run(true));

	ck_assert_int_eq(frames_seen, 1);
	ck_assert_int_eq(read_timeouts, 0);
	ck_assert_int_eq(write_timeouts, 1);
	ck_assert(finished);
}

END_TEST

static const uint32_t reply_sizes[] = {
	2, 64, 4096, (64 * 1024) - 1, 64 * 1024, (64 * 1024) + 1, 1024 * 1024,
};

/* Read one reply frame, then close to end the test. */
static void *_read_reply(void *arg)
{
	uint32_t *got = arg, nw_size = 0, off = 0;
	char *data = NULL;

	ck_assert_int_eq(read(peer_fd, &nw_size, sizeof(nw_size)),
			 sizeof(nw_size));
	got[0] = ntohl(nw_size);
	data = xmalloc(got[0]);

	while (off < got[0]) {
		ssize_t rc = read(peer_fd, data + off, got[0] - off);

		ck_assert(rc > 0);
		off += rc;
	}

	/* got[1] counts pattern mismatches. */
	for (uint32_t i = 0; i < got[0]; i++)
		if (data[i] != (char) (i % 251))
			got[1]++;

	xfree(data);
	ck_assert(!close(peer_fd));
	peer_fd = -1;
	return NULL;
}

/* Replies must arrive complete and in order across writev() boundaries. */
START_TEST(test_reply_bytes_exact)
{
	pthread_t reader;
	uint32_t got[2] = { 0, 0 };

	send_reply = true;
	reply_bytes = reply_sizes[_i];
	_add_connection();
	_write_frame(PAYLOAD_SIZE);
	ck_assert(!pthread_create(&reader, NULL, _read_reply, got));

	ck_assert(!conmgr_run(true));
	ck_assert(!pthread_join(reader, NULL));

	ck_assert_int_eq(frames_seen, 1);
	ck_assert_int_eq(got[0], reply_bytes);
	ck_assert_int_eq(got[1], 0);
	ck_assert(finished);
}

END_TEST

/* Send the rest of a frame whose first arg header bytes were sent. */
static void *_finish_split_frame(void *arg)
{
	uint32_t header = htonl(PAYLOAD_SIZE);
	size_t sent = (size_t) arg;
	char payload[PAYLOAD_SIZE] = { 0 };

	/* Let conmgr read the partial header first. */
	sleep(READ_TIMEOUT_SEC);
	ck_assert_int_eq(write(peer_fd, (char *) &header + sent,
			       sizeof(header) - sent),
			 sizeof(header) - sent);
	ck_assert_int_eq(write(peer_fd, payload, sizeof(payload)),
			 sizeof(payload));
	return NULL;
}

/* A length split across reads is buffered, not rejected. */
START_TEST(test_split_header_completes)
{
	uint32_t header = htonl(PAYLOAD_SIZE);
	pthread_t writer;

	_add_connection();
	ck_assert_int_eq(write(peer_fd, &header, _i), _i);
	ck_assert(!pthread_create(&writer, NULL, _finish_split_frame,
				  (void *) (size_t) _i));
	conmgr_add_work_delayed_fifo(_close_peer, NULL, IDLE_WAIT_SEC, 0);

	ck_assert(!conmgr_run(true));
	ck_assert(!pthread_join(writer, NULL));

	ck_assert_int_eq(parse_rc, SLURM_SUCCESS);
	ck_assert_int_eq(frames_seen, 1);
	ck_assert_int_eq(read_timeouts, 0);
	ck_assert(finished);
}

END_TEST

static void _setup(void)
{
	frames_seen = 0;
	read_timeouts = 0;
	write_timeouts = 0;
	parse_rc = SLURM_SUCCESS;
	finished = false;
	send_reply = false;
	reply_bytes = 1024 * 1024;
	peer_fd = -1;
	timeouts = conmgr_timeouts_disabled;
	memset(&ps, 0, sizeof(ps));

	/* Socket pairs do not support TCP keepalive. */
	slurm_conf.keepalive_time = NO_VAL;

	/* Expose accidental use of default timeouts. */
	slurm_conf.msg_timeout = READ_TIMEOUT_SEC;

	workerpool_init(0, 0, NULL);
	conmgr_init(0);
	ck_assert(!auth_g_init());
	ck_assert(!hash_g_init());
}

static void _teardown(void)
{
	conmgr_fini();
	workerpool_fini();

	if (peer_fd >= 0) {
		(void) close(peer_fd);
		peer_fd = -1;
	}
}

/*
 * Handshake and dispatch on a connection given to persist_conn_assign(),
 * talking to it the way a DBD client does. Uses auth/none.
 */

#define TEST_VERSION SLURM_PROTOCOL_VERSION

static int ps_arg = 0;

static int _ps_on_init(persist_conn_ctx_t *pcon_ctx, slurm_msg_t *msg,
		       uint16_t *reply_flags)
{
	ps.inits++;
	ps.init_uid = pcon_ctx->auth.uid;
	ps.init_version = pcon_ctx->version;
	strlcpy(ps.init_cluster, pcon_ctx->cluster_name,
		sizeof(ps.init_cluster));
	ps.init_persist_type = pcon_ctx->persist_type;
	ps.init_rem_host = (pcon_ctx->rem_host && pcon_ctx->rem_host[0]);
	ck_assert(pcon_ctx->auth.ids_set);
	ck_assert(pcon_ctx->arg == &ps_arg);

	ck_assert(msg->flags & SLURM_PERSIST_MSG);
	*reply_flags = PERSIST_FLAG_P_USER_CASE;
	return ps.init_rc;
}

static int _ps_on_msg(persist_conn_ctx_t *pcon_ctx, slurm_msg_t *msg)
{
	uint16_t msg_type = msg->msg_type;

	ps.msgs++;
	ps.last_type = msg_type;
	ck_assert(pcon_ctx->auth.ids_set);

	/* A request carries the session identity, version and connection. */
	ck_assert(msg->auth.ids_set);
	ck_assert_int_eq(msg->auth.uid, pcon_ctx->auth.uid);
	ck_assert_int_eq(msg->protocol_version, pcon_ctx->version);
	ck_assert(msg->conmgr_con != NULL);
	ck_assert(!msg->auth_cred);
	ck_assert(msg->pcon_ctx == pcon_ctx);

	/* Access denied closes the connection after the reply. */
	if (msg_type == DBD_SHUTDOWN) {
		ck_assert(!slurm_send_rc_err_msg(msg, ESLURM_ACCESS_DENIED,
						 "denied"));
		return ESLURM_ACCESS_DENIED;
	}

	/* Typed replies go through send_msg_response(), as in slurmctld. */
	if (msg_type == DBD_GET_TRES) {
		dbd_list_msg_t list = { 0 };

		list.my_list = list_create(NULL);
		ck_assert(!send_msg_response(msg, DBD_GOT_TRES, &list));
		FREE_NULL_LIST(list.my_list);
		return SLURM_SUCCESS;
	}

	ck_assert(!slurm_send_rc_msg(msg, SLURM_SUCCESS));
	return SLURM_SUCCESS;
}

/* The connection is gone; end the test run. */
static void _ps_on_fini(persist_conn_ctx_t *pcon_ctx)
{
	ps.finishes++;
	ck_assert(pcon_ctx->arg == &ps_arg);
	finished = true;
	conmgr_request_shutdown();
}

static const persist_conn_ops_t ps_ops = {
	.flags = PERSIST_FLAG_DBD,
	.on_init = _ps_on_init,
	.on_msg = _ps_on_msg,
	.on_fini = _ps_on_fini,
};

/*
 * Assign on the first data, as slurmdbd's listener does for a framed
 * REQUEST_PERSIST_INIT, then let persist_conn parse that data.
 */
static int _ps_on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_ref_t *ref = conmgr_fd_new_ref(conmgr_args.con);
	int rc = persist_conn_assign(ref, &ps_ops, NULL, &ps_arg, NULL);

	CONMGR_CON_UNLINK(ref);
	ck_assert(!rc);
	return rc;
}

static void *_ps_on_connection(conmgr_callback_args_t conmgr_args, void *arg)
{
	return arg;
}

static void _ps_add_connection(void)
{
	static const conmgr_events_t events = {
		.on_connection = _ps_on_connection,
		.on_data = _ps_on_data,
	};
	int sv[2] = { -1, -1 };

	ck_assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
	peer_fd = sv[1];

	ck_assert(!conmgr_process_fd(CON_TYPE_RAW, &timeouts, sv[0], sv[0],
				     &events, CON_FLAG_NONE, NULL, 0, NULL,
				     &ps_arg));
}

static void _ps_write(buf_t *payload)
{
	uint32_t nw_size = htonl(get_buf_offset(payload));

	ck_assert_int_eq(write(peer_fd, &nw_size, sizeof(nw_size)),
			 sizeof(nw_size));
	ck_assert_int_eq(write(peer_fd, get_buf_data(payload),
			       get_buf_offset(payload)),
			 get_buf_offset(payload));
	FREE_NULL_BUFFER(payload);
}

/*
 * Pack a DBD REQUEST_PERSIST_INIT as persist_conn does. inner_type lets the
 * authenticated type differ from the outer, unauthenticated one.
 */
static buf_t *_ps_init(uint16_t inner_type, bool no_auth)
{
	persist_init_req_msg_t req = {
		.cluster_name = "test",
		.persist_type = PERSIST_TYPE_DBD,
		.version = TEST_VERSION,
	};
	slurm_msg_t msg;
	msg_bufs_t bufs = { 0 };
	buf_t *payload = init_buf(BUF_SIZE);

	slurm_msg_t_init(&msg);
	msg.protocol_version = TEST_VERSION;
	msg.msg_type = REQUEST_PERSIST_INIT;
	msg.flags = SLURMDBD_CONNECTION | (no_auth ? SLURM_NO_AUTH_CRED : 0);
	msg.data = &req;
	slurm_msg_set_r_uid(&msg, SLURM_AUTH_UID_ANY);
	ck_assert(!slurm_buffers_pack_msg(&msg, &bufs, false));

	/* Outer type comes from the header's leading copy; override it. */
	if (inner_type != REQUEST_PERSIST_INIT) {
		uint32_t offset = get_buf_offset(bufs.header);

		set_buf_offset(bufs.header, 0);
		pack16(REQUEST_PERSIST_INIT, bufs.header);
		/* Skip version and flags to the authenticated type. */
		set_buf_offset(bufs.header, 6);
		pack16(inner_type, bufs.header);
		set_buf_offset(bufs.header, offset);
	}

	for (int i = 0; i < 3; i++) {
		buf_t *b = (i == 0) ? bufs.header :
			   (i == 1) ? bufs.auth :
				      bufs.body;

		if (!b)
			continue;
		grow_buf(payload, get_buf_offset(b));
		memcpy(get_buf_data(payload) + get_buf_offset(payload),
		       get_buf_data(b), get_buf_offset(b));
		set_buf_offset(payload,
			       get_buf_offset(payload) + get_buf_offset(b));
	}

	FREE_NULL_BUFFER(bufs.header);
	FREE_NULL_BUFFER(bufs.auth);
	FREE_NULL_BUFFER(bufs.body);
	return payload;
}

/* A DBD message with no body: [msg_type]. */
static buf_t *_ps_dbd(uint16_t msg_type)
{
	slurmdbd_msg_t msg = { .msg_type = msg_type };

	return pack_slurmdbd_msg(&msg, TEST_VERSION);
}

/* Read one DBD reply into msg; false if the peer closed. */
static bool _ps_read_msg(slurmdbd_msg_t *msg)
{
	uint32_t nw_size = 0, off = 0, size;
	buf_t *buffer;
	/* A missing reply fails the read instead of hanging the test. */
	struct timeval tv = { .tv_sec = 10 };

	ck_assert(!setsockopt(peer_fd, SOL_SOCKET, SO_RCVTIMEO, &tv,
			      sizeof(tv)));
	if (read(peer_fd, &nw_size, sizeof(nw_size)) != sizeof(nw_size))
		return false;
	size = ntohl(nw_size);
	buffer = init_buf(size);
	while (off < size) {
		ssize_t n =
			read(peer_fd, get_buf_data(buffer) + off, size - off);

		ck_assert(n > 0);
		off += n;
	}

	ck_assert(!unpack_slurmdbd_msg(msg, TEST_VERSION, buffer));
	FREE_NULL_BUFFER(buffer);
	return true;
}

/* Read a PERSIST_RC reply; return its rc, or -1 if the peer closed. */
static int _ps_read_rc_flags(uint16_t *ret_info, uint16_t *flags)
{
	slurmdbd_msg_t msg = { 0 };
	persist_rc_msg_t *rc_msg;
	int rc;

	if (!_ps_read_msg(&msg))
		return -1;
	ck_assert_int_eq(msg.msg_type, PERSIST_RC);
	rc_msg = msg.data;
	rc = rc_msg->rc;
	if (ret_info)
		*ret_info = rc_msg->ret_info;
	if (flags)
		*flags = rc_msg->flags;
	slurm_free_persist_rc_msg(rc_msg);
	return rc;
}

static int _ps_read_rc(uint16_t *ret_info)
{
	return _ps_read_rc_flags(ret_info, NULL);
}

static bool _ps_peer_closed(void)
{
	char c;

	return (read(peer_fd, &c, 1) == 0);
}

/* Steps run on a thread while conmgr serves the connection. */
static void *_ps_client(void *arg)
{
	void (*steps)(void) = arg;

	steps();
	ck_assert(!close(peer_fd));
	peer_fd = -1;
	return NULL;
}

static void _ps_run(void (*steps)(void))
{
	pthread_t client;

	_ps_add_connection();
	ck_assert(!pthread_create(&client, NULL, _ps_client, steps));
	ck_assert(!conmgr_run(true));
	ck_assert(!pthread_join(client, NULL));
	ck_assert(finished);
}

static void _steps_init_and_msgs(void)
{
	uint16_t ret_info = 0, flags = 0;

	/* The init reply has the version and on_init()'s flags. */
	_ps_write(_ps_init(REQUEST_PERSIST_INIT, false));
	ck_assert_int_eq(_ps_read_rc_flags(&ret_info, &flags), SLURM_SUCCESS);
	ck_assert_int_eq(ret_info, TEST_VERSION);
	ck_assert_int_eq(flags, PERSIST_FLAG_P_USER_CASE);

	for (int i = 0; i < 3; i++) {
		_ps_write(_ps_dbd(DBD_GET_STATS));
		ck_assert_int_eq(_ps_read_rc(&ret_info), SLURM_SUCCESS);
		ck_assert_int_eq(ret_info, DBD_GET_STATS);
	}
}

START_TEST(test_init_then_messages)
{
	_ps_run(_steps_init_and_msgs);

	ck_assert_int_eq(ps.inits, 1);
	ck_assert_int_eq(ps.init_uid, geteuid());
	ck_assert_int_eq(ps.init_version, TEST_VERSION);
	ck_assert_str_eq(ps.init_cluster, "test");
	ck_assert_int_eq(ps.init_persist_type, PERSIST_TYPE_DBD);
	ck_assert_int_eq(ps.msgs, 3);
	ck_assert_int_eq(ps.last_type, DBD_GET_STATS);
	ck_assert_int_eq(ps.finishes, 1);
}

END_TEST

static void _steps_msg_before_init(void)
{
	uint16_t ret_info = 0;

	_ps_write(_ps_dbd(DBD_GET_STATS));
	ck_assert_int_eq(_ps_read_rc(&ret_info),
			 SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	ck_assert_int_eq(ret_info, REQUEST_PERSIST_INIT);
	ck_assert(_ps_peer_closed());
}

START_TEST(test_message_before_init_rejected)
{
	_ps_run(_steps_msg_before_init);

	ck_assert_int_eq(ps.inits, 0);
	ck_assert_int_eq(ps.msgs, 0);
	ck_assert_int_eq(ps.finishes, 1);
}

END_TEST

static void _steps_second_init(void)
{
	_ps_write(_ps_init(REQUEST_PERSIST_INIT, false));
	ck_assert_int_eq(_ps_read_rc(NULL), SLURM_SUCCESS);
	_ps_write(_ps_init(REQUEST_PERSIST_INIT, false));
	ck_assert_int_eq(_ps_read_rc(NULL),
			 SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	ck_assert(_ps_peer_closed());
}

START_TEST(test_second_init_rejected)
{
	_ps_run(_steps_second_init);

	ck_assert_int_eq(ps.inits, 1);
	ck_assert_int_eq(ps.finishes, 1);
}

END_TEST

static void _steps_mismatched_init(void)
{
	_ps_write(_ps_init(REQUEST_PING, false));
	ck_assert_int_eq(_ps_read_rc(NULL),
			 SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	ck_assert(_ps_peer_closed());
}

/* The outer type is not covered by the credential hash. */
START_TEST(test_mismatched_init_type_rejected)
{
	_ps_run(_steps_mismatched_init);

	ck_assert_int_eq(ps.inits, 0);
	ck_assert_int_eq(ps.finishes, 1);
}

END_TEST

static void _steps_unauthenticated_init(void)
{
	_ps_write(_ps_init(REQUEST_PERSIST_INIT, true));
	ck_assert_int_eq(_ps_read_rc(NULL),
			 SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	ck_assert(_ps_peer_closed());
}

START_TEST(test_unauthenticated_init_rejected)
{
	_ps_run(_steps_unauthenticated_init);

	ck_assert_int_eq(ps.inits, 0);
	ck_assert_int_eq(ps.finishes, 1);
}

END_TEST

static void _steps_init_refused(void)
{
	_ps_write(_ps_init(REQUEST_PERSIST_INIT, false));
	ck_assert_int_eq(_ps_read_rc(NULL), ESLURM_DB_CONNECTION);
	ck_assert(_ps_peer_closed());
}

/* Any on_init() failure closes after its reply. */
START_TEST(test_init_refused_closes)
{
	ps.init_rc = ESLURM_DB_CONNECTION;
	_ps_run(_steps_init_refused);

	ck_assert_int_eq(ps.inits, 1);
	ck_assert_int_eq(ps.msgs, 0);
	ck_assert_int_eq(ps.finishes, 1);
}

END_TEST

static void _steps_access_denied(void)
{
	_ps_write(_ps_init(REQUEST_PERSIST_INIT, false));
	ck_assert_int_eq(_ps_read_rc(NULL), SLURM_SUCCESS);
	_ps_write(_ps_dbd(DBD_SHUTDOWN));
	ck_assert_int_eq(_ps_read_rc(NULL), ESLURM_ACCESS_DENIED);
	ck_assert(_ps_peer_closed());
}

START_TEST(test_access_denied_closes)
{
	_ps_run(_steps_access_denied);

	ck_assert_int_eq(ps.msgs, 1);
	ck_assert_int_eq(ps.finishes, 1);
}

END_TEST

static void _steps_bad_body(void)
{
	buf_t *bad = init_buf(16);

	_ps_write(_ps_init(REQUEST_PERSIST_INIT, false));
	ck_assert_int_eq(_ps_read_rc(NULL), SLURM_SUCCESS);

	/* DBD_GET_JOBS_COND with a truncated condition. */
	pack16(DBD_GET_JOBS_COND, bad);
	pack8(1, bad);
	_ps_write(bad);
	ck_assert(_ps_read_rc(NULL) != SLURM_SUCCESS);

	/* Ordinary RPC failures keep the connection open. */
	_ps_write(_ps_dbd(DBD_GET_STATS));
	ck_assert_int_eq(_ps_read_rc(NULL), SLURM_SUCCESS);
}

START_TEST(test_bad_body_replies_and_continues)
{
	_ps_run(_steps_bad_body);

	ck_assert_int_eq(ps.msgs, 1);
	ck_assert_int_eq(ps.finishes, 1);
}

END_TEST

/*
 * Assign with init: REQUEST_PERSIST_INIT arrives as an ordinary RPC on a
 * CON_TYPE_RPC connection, as in slurmctld. Later frames use the non-DBD
 * body format.
 */

static int _ps_ctld_on_msg(persist_conn_ctx_t *pcon_ctx, slurm_msg_t *msg)
{
	ps.msgs++;
	ps.last_type = msg->msg_type;
	ck_assert(msg->auth.ids_set);
	ck_assert(!slurm_send_rc_msg(msg, SLURM_SUCCESS));
	return SLURM_SUCCESS;
}

static const persist_conn_ops_t ps_ctld_ops = {
	.on_init = _ps_on_init,
	.on_msg = _ps_ctld_on_msg,
	.on_fini = _ps_on_fini,
};

static int _ps_rpc_on_msg(conmgr_callback_args_t conmgr_args, slurm_msg_t *msg,
			  int unpack_rc, void *arg)
{
	int rc;

	ck_assert(!unpack_rc);
	ck_assert_int_eq(msg->msg_type, REQUEST_PERSIST_INIT);
	rc = persist_conn_assign(conmgr_args.ref, &ps_ctld_ops, NULL, &ps_arg,
				 msg);
	slurm_free_msg(msg);
	return rc;
}

/* Only reached if the connection closes before assign. */
static void _ps_rpc_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	finished = true;
	conmgr_request_shutdown();
}

/* Read a non-DBD PERSIST_RC reply; return its rc, or -1 if closed. */
static int _ps_read_ctld_rc(uint16_t *ret_info)
{
	uint32_t nw_size = 0, off = 0, size;
	slurm_msg_t msg;
	persist_rc_msg_t *rc_msg;
	uint16_t type;
	buf_t *buffer;
	int rc;

	if (read(peer_fd, &nw_size, sizeof(nw_size)) != sizeof(nw_size))
		return -1;
	size = ntohl(nw_size);
	buffer = init_buf(size);
	while (off < size) {
		ssize_t n =
			read(peer_fd, get_buf_data(buffer) + off, size - off);

		ck_assert(n > 0);
		off += n;
	}

	ck_assert(!unpack16(&type, buffer));
	ck_assert_int_eq(type, PERSIST_RC);
	slurm_msg_t_init(&msg);
	msg.msg_type = type;
	msg.protocol_version = TEST_VERSION;
	ck_assert(!unpack_msg(&msg, buffer));
	rc_msg = msg.data;
	rc = rc_msg->rc;
	if (ret_info)
		*ret_info = rc_msg->ret_info;
	slurm_free_persist_rc_msg(rc_msg);
	FREE_NULL_BUFFER(buffer);
	return rc;
}

static void _steps_assign(void)
{
	persist_init_req_msg_t req = {
		.cluster_name = "sibling",
		.persist_type = PERSIST_TYPE_FED,
		.version = TEST_VERSION,
	};
	slurm_msg_t msg;
	msg_bufs_t bufs = { 0 };
	uint16_t ret_info = 0;
	uint32_t len;
	buf_t *ping = init_buf(16);

	/* REQUEST_PERSIST_INIT as an ordinary RPC: full header first. */
	slurm_msg_t_init(&msg);
	msg.protocol_version = TEST_VERSION;
	msg.msg_type = REQUEST_PERSIST_INIT;
	msg.data = &req;
	slurm_msg_set_r_uid(&msg, SLURM_AUTH_UID_ANY);
	ck_assert(!slurm_buffers_pack_msg(&msg, &bufs, false));
	len = htonl(get_buf_offset(bufs.header) + get_buf_offset(bufs.auth) +
		    get_buf_offset(bufs.body));
	ck_assert_int_eq(write(peer_fd, &len, sizeof(len)), sizeof(len));
	ck_assert(write(peer_fd, get_buf_data(bufs.header),
			get_buf_offset(bufs.header)) > 0);
	ck_assert(write(peer_fd, get_buf_data(bufs.auth),
			get_buf_offset(bufs.auth)) > 0);
	ck_assert(write(peer_fd, get_buf_data(bufs.body),
			get_buf_offset(bufs.body)) > 0);
	FREE_NULL_BUFFER(bufs.header);
	FREE_NULL_BUFFER(bufs.auth);
	FREE_NULL_BUFFER(bufs.body);

	ck_assert_int_eq(_ps_read_ctld_rc(&ret_info), SLURM_SUCCESS);
	ck_assert_int_eq(ret_info, TEST_VERSION);

	/* Then a persistent frame: [REQUEST_PING] with no body. */
	pack16(REQUEST_PING, ping);
	_ps_write(ping);
	ck_assert_int_eq(_ps_read_ctld_rc(&ret_info), SLURM_SUCCESS);
	ck_assert_int_eq(ret_info, REQUEST_PING);
}

/* Connected TCP sockets over loopback. */
static void _tcp_pair(int sv[2])
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	socklen_t len = sizeof(addr);
	int lfd = socket(AF_INET, SOCK_STREAM, 0), one = 1;

	ck_assert(lfd >= 0);
	ck_assert(!bind(lfd, (struct sockaddr *) &addr, sizeof(addr)));
	ck_assert(!listen(lfd, 1));
	ck_assert(!getsockname(lfd, (struct sockaddr *) &addr, &len));
	ck_assert((sv[1] = socket(AF_INET, SOCK_STREAM, 0)) >= 0);
	ck_assert(!setsockopt(sv[1], IPPROTO_TCP, TCP_NODELAY, &one,
			      sizeof(one)));
	ck_assert(!connect(sv[1], (struct sockaddr *) &addr, sizeof(addr)));
	ck_assert((sv[0] = accept(lfd, NULL, NULL)) >= 0);
	ck_assert(!close(lfd));
}

START_TEST(test_assign_rpc_init)
{
	static conmgr_events_t events = {
		.on_connection = _ps_on_connection,
		.on_msg = _ps_rpc_on_msg,
		.on_finish = _ps_rpc_finish,
	};
	pthread_t client;
	int sv[2] = { -1, -1 };

	/* RPC connections still need on_data() defined. */
	events.on_data = _ps_on_data;

	/* RPC connections are TCP; switching modes sets TCP_NODELAY. */
	_tcp_pair(sv);
	peer_fd = sv[1];
	ck_assert(!conmgr_process_fd(CON_TYPE_RPC, &timeouts, sv[0], sv[0],
				     &events, CON_FLAG_NONE, NULL, 0, NULL,
				     &conn_arg));
	ck_assert(!pthread_create(&client, NULL, _ps_client, _steps_assign));
	ck_assert(!conmgr_run(true));
	ck_assert(!pthread_join(client, NULL));

	ck_assert(finished);
	ck_assert_int_eq(ps.inits, 1);
	ck_assert_str_eq(ps.init_cluster, "sibling");
	ck_assert_int_eq(ps.init_persist_type, PERSIST_TYPE_FED);
	ck_assert_int_eq(ps.msgs, 1);
	ck_assert_int_eq(ps.last_type, REQUEST_PING);
	ck_assert_int_eq(ps.finishes, 1);
}

END_TEST

static buf_t *_ps_tres_query(void)
{
	slurmdb_tres_cond_t cond = { 0 };
	dbd_cond_msg_t get = { .cond = &cond };
	slurmdbd_msg_t msg = { .msg_type = DBD_GET_TRES, .data = &get };

	return pack_slurmdbd_msg(&msg, TEST_VERSION);
}

static void _steps_typed_reply(void)
{
	slurmdbd_msg_t reply = { 0 };

	_ps_write(_ps_init(REQUEST_PERSIST_INIT, false));
	ck_assert_int_eq(_ps_read_rc(NULL), SLURM_SUCCESS);

	_ps_write(_ps_tres_query());
	ck_assert(_ps_read_msg(&reply));
	ck_assert_int_eq(reply.msg_type, DBD_GOT_TRES);
	ck_assert_int_eq(list_count(((dbd_list_msg_t *) reply.data)->my_list),
			 0);
	slurmdbd_free_msg(&reply);
}

/* send_msg_response() frames a typed reply for a persistent session. */
START_TEST(test_typed_reply)
{
	_ps_run(_steps_typed_reply);

	ck_assert_int_eq(ps.msgs, 1);
	ck_assert_int_eq(ps.last_type, DBD_GET_TRES);
}

END_TEST

/*
 * A request with resp_buf set gets its reply as a buffer instead of
 * sent, as slurmdbd does for each request in a DBD_SEND_MULT_MSG.
 */
static void _ps_free_buf(void *x)
{
	buf_t *buffer = x;

	FREE_NULL_BUFFER(buffer);
}

static int _ps_batch_on_msg(persist_conn_ctx_t *pcon_ctx, slurm_msg_t *msg)
{
	dbd_list_msg_t batch = { .my_list = list_create(_ps_free_buf) };

	ps.msgs++;
	for (int i = 0; i < 2; i++) {
		slurm_msg_t sub_req = *msg;
		buf_t *reply = NULL;

		sub_req.resp_buf = &reply;
		if (!i)
			ck_assert(!slurm_send_rc_msg(&sub_req, SLURM_SUCCESS));
		else
			ck_assert(!slurm_send_rc_err_msg(&sub_req,
							 ESLURM_ACCESS_DENIED,
							 "no"));
		ck_assert(reply != NULL);

		/* Same bytes as the slurmdbd packer has always sent. */
		{
			persist_rc_msg_t rc_msg = {
				.comment = (i ? "no" : NULL),
				.rc = (i ? ESLURM_ACCESS_DENIED :
					   SLURM_SUCCESS),
				.ret_info = msg->msg_type,
			};
			slurmdbd_msg_t old = {
				.msg_type = PERSIST_RC,
				.data = &rc_msg,
			};
			buf_t *want =
				pack_slurmdbd_msg(&old, msg->protocol_version);

			ck_assert_int_eq(get_buf_offset(reply),
					 get_buf_offset(want));
			ck_assert(!memcmp(get_buf_data(reply),
					  get_buf_data(want),
					  get_buf_offset(want)));
			FREE_NULL_BUFFER(want);
		}
		list_append(batch.my_list, reply);
	}

	ck_assert_int_eq(list_count(batch.my_list), 2);
	ck_assert(!send_msg_response(msg, DBD_GOT_MULT_MSG, &batch));
	FREE_NULL_LIST(batch.my_list);
	return SLURM_SUCCESS;
}

static const persist_conn_ops_t ps_batch_ops = {
	.flags = PERSIST_FLAG_DBD,
	.on_init = _ps_on_init,
	.on_msg = _ps_batch_on_msg,
	.on_fini = _ps_on_fini,
};

static int _ps_batch_on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_ref_t *ref = conmgr_fd_new_ref(conmgr_args.con);
	int rc = persist_conn_assign(ref, &ps_batch_ops, NULL, &ps_arg, NULL);

	CONMGR_CON_UNLINK(ref);
	return rc;
}

static void _steps_batch(void)
{
	slurmdbd_msg_t reply = { 0 };
	dbd_list_msg_t *batch;
	buf_t *sub;
	list_itr_t *itr;
	int rcs[2] = { -1, -1 }, i = 0;

	_ps_write(_ps_init(REQUEST_PERSIST_INIT, false));
	ck_assert_int_eq(_ps_read_rc(NULL), SLURM_SUCCESS);

	_ps_write(_ps_dbd(DBD_GET_STATS));
	ck_assert(_ps_read_msg(&reply));
	ck_assert_int_eq(reply.msg_type, DBD_GOT_MULT_MSG);
	batch = reply.data;
	ck_assert_int_eq(list_count(batch->my_list), 2);

	/* Batched replies are complete DBD messages, in order. */
	itr = list_iterator_create(batch->my_list);
	while ((sub = list_next(itr))) {
		slurmdbd_msg_t rc_msg = { 0 };

		set_buf_offset(sub, 0);
		ck_assert(!unpack_slurmdbd_msg(&rc_msg, TEST_VERSION, sub));
		ck_assert_int_eq(rc_msg.msg_type, PERSIST_RC);
		rcs[i++] = ((persist_rc_msg_t *) rc_msg.data)->rc;
		slurmdbd_free_msg(&rc_msg);
	}
	list_iterator_destroy(itr);
	ck_assert_int_eq(rcs[0], SLURM_SUCCESS);
	ck_assert_int_eq(rcs[1], ESLURM_ACCESS_DENIED);
	slurmdbd_free_msg(&reply);
}

START_TEST(test_resp_buf)
{
	static const conmgr_events_t events = {
		.on_connection = _ps_on_connection,
		.on_data = _ps_batch_on_data,
	};
	pthread_t client;
	int sv[2] = { -1, -1 };

	ck_assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
	peer_fd = sv[1];
	ck_assert(!conmgr_process_fd(CON_TYPE_RAW, &timeouts, sv[0], sv[0],
				     &events, CON_FLAG_NONE, NULL, 0, NULL,
				     &ps_arg));
	ck_assert(!pthread_create(&client, NULL, _ps_client, _steps_batch));
	ck_assert(!conmgr_run(true));
	ck_assert(!pthread_join(client, NULL));
	ck_assert(finished);
	ck_assert_int_eq(ps.msgs, 1);
}

END_TEST

/*
 * DBD message bodies. Any peer can send them: daemons decode the body of an
 * RPC flagged SLURM_NO_AUTH_CRED without a credential.
 */

static const uint16_t qos_usage_types[] = {
	DBD_GET_QOS_USAGE,
	DBD_GOT_QOS_USAGE,
};

/* Unpack body as an RPC without a credential, as conmgr does for any peer. */
static int _unpack_noauth(uint16_t msg_type, buf_t *body)
{
	header_t header = {
		.version = TEST_VERSION,
		.flags = SLURM_NO_AUTH_CRED,
		.msg_type = msg_type,
		.body_length = get_buf_offset(body),
	};
	buf_t *buffer = init_buf(BUF_SIZE), *rpc;
	slurm_msg_t msg;
	int rc;

	pack_header(&header, buffer);
	packmem_array(get_buf_data(body), get_buf_offset(body), buffer);

	/* Exactly one RPC, as conmgr passes it. */
	rpc = create_shadow_buf(get_buf_data(buffer), get_buf_offset(buffer));
	slurm_msg_t_init(&msg);
	rc = slurm_unpack_received_msg(&msg, -1, rpc);
	slurm_free_msg_members(&msg);

	FREE_NULL_BUFFER(rpc);
	FREE_NULL_BUFFER(buffer);
	return rc;
}

/*
 * Unpack body as an RPC of msg_type, as a DBD message and as a DBD message in
 * a REQUEST_DBD_RELAY RPC. Each must succeed only if valid.
 */
static void _unpack_routes(uint16_t msg_type, buf_t *body, bool valid)
{
	slurmdbd_msg_t dbd_msg = { 0 };
	buf_t *dbd = init_buf(BUF_SIZE), *relay = init_buf(BUF_SIZE), *frame;

	/* RPC of that type */
	ck_assert_int_eq(!_unpack_noauth(msg_type, body), valid);

	/* DBD message, as in a session or a DBD_SEND_MULT_MSG batch */
	pack16(msg_type, dbd);
	packmem_array(get_buf_data(body), get_buf_offset(body), dbd);
	frame = create_shadow_buf(get_buf_data(dbd), get_buf_offset(dbd));
	ck_assert_int_eq(!unpack_slurmdbd_msg(&dbd_msg, TEST_VERSION, frame),
			 valid);
	ck_assert_int_eq(!dbd_msg.data, !valid);
	slurmdbd_free_msg(&dbd_msg);

	/* DBD message in a REQUEST_DBD_RELAY RPC */
	pack16(msg_type, relay);
	packmem_array(get_buf_data(dbd), get_buf_offset(dbd), relay);
	ck_assert_int_eq(!_unpack_noauth(REQUEST_DBD_RELAY, relay), valid);

	FREE_NULL_BUFFER(frame);
	FREE_NULL_BUFFER(relay);
	FREE_NULL_BUFFER(dbd);
}

/*
 * A QOS usage body unpacks on each route. Without its last byte, the record
 * unpacks, the end time does not, and the partial message is freed.
 */
START_TEST(test_truncated_qos_usage_rejected)
{
	uint16_t msg_type = qos_usage_types[_i];
	slurmdb_assoc_rec_t assoc;
	dbd_usage_msg_t usage = { .rec = &assoc };
	buf_t *body = init_buf(BUF_SIZE);

	slurmdb_init_assoc_rec(&assoc, false);
	slurmdbd_pack_usage_msg(&usage, TEST_VERSION, msg_type, body);
	_unpack_routes(msg_type, body, true);

	set_buf_offset(body, get_buf_offset(body) - 1);
	_unpack_routes(msg_type, body, false);

	FREE_NULL_BUFFER(body);
}

END_TEST

/* Bodies any RPC can carry; slurmdbd_free_msg() must free each one. */
static const uint16_t dbd_free_types[] = {
	PERSIST_RC,        DBD_ID_RC,         DBD_GOT_STATS,
	DBD_GET_QOS_USAGE, DBD_GOT_QOS_USAGE,
};

/* Decode a valid body of msg_type, as the DBD unpacker does. */
static void *_dbd_body(uint16_t msg_type)
{
	persist_rc_msg_t rc_msg = { .comment = "comment" };
	dbd_id_rc_msg_t id_rc = { .job_id = 1 };
	slurmdb_rollup_stats_t rollup = { 0 };
	slurmdb_stats_rec_t stats = { .dbd_rollup_stats = &rollup };
	slurmdb_assoc_rec_t assoc;
	dbd_usage_msg_t usage = { .rec = &assoc };
	slurmdbd_msg_t msg = { .msg_type = msg_type, .data = &usage };
	buf_t *buffer;

	slurmdb_init_assoc_rec(&assoc, false);
	if (msg_type == PERSIST_RC)
		msg.data = &rc_msg;
	else if (msg_type == DBD_ID_RC)
		msg.data = &id_rc;
	else if (msg_type == DBD_GOT_STATS)
		msg.data = &stats;

	buffer = pack_slurmdbd_msg(&msg, TEST_VERSION);
	ck_assert(buffer != NULL);
	set_buf_offset(buffer, 0);
	msg.data = NULL;
	ck_assert(!unpack_slurmdbd_msg(&msg, TEST_VERSION, buffer));
	ck_assert_int_eq(msg.msg_type, msg_type);
	ck_assert(msg.data != NULL);
	FREE_NULL_BUFFER(buffer);
	return msg.data;
}

/* Also log errors to a temporary file. */
static FILE *_capture_errors(void)
{
	log_options_t opts = log_opts;
	FILE *fp = tmpfile();

	ck_assert(fp != NULL);
	opts.logfile_level = LOG_LEVEL_ERROR;
	ck_assert(!log_alter_with_fp(opts, 0, fp));
	return fp;
}

/* Restore main()'s logging; fail with whatever was captured. */
static void _assert_no_errors(FILE *fp)
{
	char errors[1024];
	size_t len;

	rewind(fp);
	len = fread(errors, 1, sizeof(errors) - 1, fp);
	errors[len] = '\0';

	/* Closes fp */
	ck_assert(!log_alter_with_fp(log_opts, 0, NULL));
	ck_assert_msg(!len, "logged: %s", errors);
}

/*
 * Free a decoded body both ways without an error. slurmdbd_free_msg() logs
 * "Unknown rec type" for a body it cannot free, and leaks it.
 */
START_TEST(test_dbd_body_freed)
{
	uint16_t msg_type = dbd_free_types[_i];
	void *rpc_data = _dbd_body(msg_type), *dbd_data = _dbd_body(msg_type);
	slurmdbd_msg_t dbd_msg = { .msg_type = msg_type, .data = dbd_data };
	FILE *errors = _capture_errors();

	/* As slurm_free_msg() frees an RPC, such as a rejected one */
	slurm_free_msg_data(msg_type, rpc_data);

	/* As persist_conn, DBD_SEND_MULT_MSG and REQUEST_DBD_RELAY free it */
	slurmdbd_free_msg(&dbd_msg);

	_assert_no_errors(errors);
}

END_TEST

static Suite *suite_frame(void)
{
	Suite *s = suite_create("persist_conn");
	TCase *tc_core = tcase_create("framing");

	tcase_add_checked_fixture(tc_core, _setup, _teardown);

	/* Allow for delayed callbacks. */
	tcase_set_timeout(tc_core, 30);

	tcase_add_test(tc_core, test_idle_connection_never_times_out);
	tcase_add_test(tc_core, test_partial_frame_never_times_out);
	tcase_add_test(tc_core, test_header_only_frame_never_times_out);
	tcase_add_test(tc_core, test_trailing_partial_frame_never_times_out);
	tcase_add_test(tc_core, test_caller_read_timeout_preserved);
	tcase_add_test(tc_core, test_caller_write_timeout_preserved);
	tcase_add_loop_test(tc_core, test_reply_bytes_exact, 0,
			    ARRAY_SIZE(reply_sizes));
	tcase_add_loop_test(tc_core, test_split_header_completes, 1, 4);
	suite_add_tcase(s, tc_core);

	tc_core = tcase_create("protocol");
	tcase_add_checked_fixture(tc_core, _setup, _teardown);
	tcase_set_timeout(tc_core, 30);
	tcase_add_test(tc_core, test_init_then_messages);
	tcase_add_test(tc_core, test_message_before_init_rejected);
	tcase_add_test(tc_core, test_second_init_rejected);
	tcase_add_test(tc_core, test_mismatched_init_type_rejected);
	tcase_add_test(tc_core, test_unauthenticated_init_rejected);
	tcase_add_test(tc_core, test_init_refused_closes);
	tcase_add_test(tc_core, test_access_denied_closes);
	tcase_add_test(tc_core, test_bad_body_replies_and_continues);
	tcase_add_test(tc_core, test_assign_rpc_init);
	tcase_add_test(tc_core, test_typed_reply);
	tcase_add_test(tc_core, test_resp_buf);
	suite_add_tcase(s, tc_core);

	tc_core = tcase_create("dbd_body");
	tcase_add_checked_fixture(tc_core, _setup, _teardown);
	tcase_add_loop_test(tc_core, test_truncated_qos_usage_rejected, 0,
			    ARRAY_SIZE(qos_usage_types));
	tcase_add_loop_test(tc_core, test_dbd_body_freed, 0,
			    ARRAY_SIZE(dbd_free_types));
	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	int number_failed;
	SRunner *sr = NULL;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	log_init("persist_conn-test", log_opts, 0, NULL);

	/* url_parser for conmgr_init(); auth/none and hash for handshakes. */
	slurm_conf.plugindir = xstrdup(TEST_PLUGIN_DIR);
	slurm_conf.authtype = xstrdup("auth/none");
	slurm_conf.hash_plugin = xstrdup("hash/k12");

	sr = srunner_create(suite_frame());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	log_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
