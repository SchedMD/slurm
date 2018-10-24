/****************************************************************************\
 *  slurmdbd_agent.c - functions to manage the connection to the SlurmDBD
 *****************************************************************************
 *  Copyright (C) 2011-2018 SchedMD LLC.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "src/common/slurm_xlator.h"
#include "src/common/slurmdbd_pack.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "slurmdbd_agent.h"

#define DBD_MAGIC		0xDEAD3219
#define MAX_AGENT_QUEUE		10000
#define SLURMDBD_TIMEOUT	900	/* Seconds SlurmDBD for response */

static pthread_mutex_t agent_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  agent_cond = PTHREAD_COND_INITIALIZER;
static List      agent_list     = (List) NULL;
static pthread_t agent_tid      = 0;

static bool      halt_agent          = 0;
static time_t    slurmdbd_shutdown   = 0;
static char *    slurmdbd_cluster    = NULL;

static slurm_persist_conn_t *slurmdbd_conn = NULL;
static pthread_mutex_t slurmdbd_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  slurmdbd_cond = PTHREAD_COND_INITIALIZER;


static int _send_fini_msg(void)
{
	int rc;
	Buf buffer;
	dbd_fini_msg_t req;

	/* If the connection is already gone, we don't need to send a
	   fini. */
	if (slurm_persist_conn_writeable(slurmdbd_conn) == -1)
		return SLURM_SUCCESS;

	buffer = init_buf(1024);
	pack16((uint16_t) DBD_FINI, buffer);
	req.commit  = 0;
	req.close_conn   = 1;
	slurmdbd_pack_fini_msg(&req, SLURM_PROTOCOL_VERSION, buffer);

	rc = slurm_persist_send_msg(slurmdbd_conn, buffer);
	free_buf(buffer);

	return rc;
}

static int _unpack_return_code(uint16_t rpc_version, Buf buffer)
{
	uint16_t msg_type = -1;
	persist_rc_msg_t *msg;
	dbd_id_rc_msg_t *id_msg;
	slurmdbd_msg_t resp;
	int rc = SLURM_ERROR;

	memset(&resp, 0, sizeof(slurmdbd_msg_t));
	if ((rc = unpack_slurmdbd_msg(&resp, slurmdbd_conn->version, buffer))
	    != SLURM_SUCCESS) {
		error("%s: unpack message error", __func__);
		return rc;
	}

	switch (resp.msg_type) {
	case DBD_ID_RC:
		id_msg = resp.data;
		rc = id_msg->return_code;
		slurmdbd_free_id_rc_msg(id_msg);
		if (rc != SLURM_SUCCESS)
			error("slurmdbd: DBD_ID_RC is %d", rc);
		break;
	case PERSIST_RC:
		msg = resp.data;
		rc = msg->rc;
		if (rc != SLURM_SUCCESS) {
			if (msg->ret_info == DBD_REGISTER_CTLD &&
			    slurm_get_accounting_storage_enforce()) {
				error("slurmdbd: PERSIST_RC is %d from "
				      "%s(%u): %s",
				      rc,
				      slurmdbd_msg_type_2_str(
					      msg->ret_info, 1),
				      msg->ret_info,
				      msg->comment);
				fatal("You need to add this cluster "
				      "to accounting if you want to "
				      "enforce associations, or no "
				      "jobs will ever run.");
			} else
				debug("slurmdbd: PERSIST_RC is %d from "
				      "%s(%u): %s",
				      rc,
				      slurmdbd_msg_type_2_str(
					      msg->ret_info, 1),
				      msg->ret_info,
				      msg->comment);
		}
		slurm_persist_free_rc_msg(msg);
		break;
	default:
		error("slurmdbd: bad message type %d != PERSIST_RC", msg_type);
	}

	return rc;
}

static int _get_return_code(void)
{
	int rc = SLURM_ERROR;
	Buf buffer = slurm_persist_recv_msg(slurmdbd_conn);
	if (buffer == NULL)
		return rc;

	rc = _unpack_return_code(slurmdbd_conn->version, buffer);

	free_buf(buffer);
	return rc;
}

static int _handle_mult_rc_ret(void)
{
	Buf buffer;
	uint16_t msg_type;
	persist_rc_msg_t *msg = NULL;
	dbd_list_msg_t *list_msg = NULL;
	int rc = SLURM_ERROR;
	Buf out_buf = NULL;

	buffer = slurm_persist_recv_msg(slurmdbd_conn);
	if (buffer == NULL)
		return rc;

	safe_unpack16(&msg_type, buffer);
	switch (msg_type) {
	case DBD_GOT_MULT_MSG:
		if (slurmdbd_unpack_list_msg(
			    &list_msg, slurmdbd_conn->version,
			    DBD_GOT_MULT_MSG, buffer)
		    != SLURM_SUCCESS) {
			error("slurmdbd: unpack message error");
			break;
		}

		slurm_mutex_lock(&agent_lock);
		if (agent_list) {
			ListIterator itr =
				list_iterator_create(list_msg->my_list);
			while ((out_buf = list_next(itr))) {
				Buf b;
				if ((rc = _unpack_return_code(
					     slurmdbd_conn->version, out_buf))
				    != SLURM_SUCCESS)
					break;

				if ((b = list_dequeue(agent_list))) {
					free_buf(b);
				} else {
					error("slurmdbd: DBD_GOT_MULT_MSG "
					      "unpack message error");
				}
			}
			list_iterator_destroy(itr);
		}
		slurm_mutex_unlock(&agent_lock);
		slurmdbd_free_list_msg(list_msg);
		break;
	case PERSIST_RC:
		if (slurm_persist_unpack_rc_msg(
			    &msg, buffer, slurmdbd_conn->version)
		    == SLURM_SUCCESS) {
			rc = msg->rc;
			if (rc != SLURM_SUCCESS) {
				if (msg->ret_info == DBD_REGISTER_CTLD &&
				    slurm_get_accounting_storage_enforce()) {
					error("slurmdbd: PERSIST_RC is %d from "
					      "%s(%u): %s",
					      rc,
					      slurmdbd_msg_type_2_str(
						      msg->ret_info, 1),
					      msg->ret_info,
					      msg->comment);
					fatal("You need to add this cluster "
					      "to accounting if you want to "
					      "enforce associations, or no "
					      "jobs will ever run.");
				} else
					debug("slurmdbd: PERSIST_RC is %d from "
					      "%s(%u): %s",
					      rc,
					      slurmdbd_msg_type_2_str(
						      msg->ret_info, 1),
					      msg->ret_info,
					      msg->comment);
			}
			slurm_persist_free_rc_msg(msg);
		} else
			error("slurmdbd: unpack message error");
		break;
	default:
		error("slurmdbd: bad message type %d != PERSIST_RC", msg_type);
	}

unpack_error:
	free_buf(buffer);
	return rc;
}

/****************************************************************************
 * Functions for agent to manage queue of pending message for the Slurm DBD
 ****************************************************************************/
static Buf _load_dbd_rec(int fd)
{
	ssize_t size, rd_size;
	uint32_t msg_size, magic;
	char *msg;
	Buf buffer;

	size = sizeof(msg_size);
	rd_size = read(fd, &msg_size, size);
	if (rd_size == 0)
		return (Buf) NULL;
	if (rd_size != size) {
		error("slurmdbd: state recover error: %m");
		return (Buf) NULL;
	}
	if (msg_size > MAX_DBD_MSG_LEN) {
		error("slurmdbd: state recover error, msg_size=%u", msg_size);
		return (Buf) NULL;
	}

	buffer = init_buf((int) msg_size);
	set_buf_offset(buffer, msg_size);
	msg = get_buf_data(buffer);
	size = msg_size;
	while (size) {
		rd_size = read(fd, msg, size);
		if ((rd_size > 0) && (rd_size <= size)) {
			msg += rd_size;
			size -= rd_size;
		} else if ((rd_size == -1) && (errno == EINTR))
			continue;
		else {
			error("slurmdbd: state recover error: %m");
			free_buf(buffer);
			return (Buf) NULL;
		}
	}

	size = sizeof(magic);
	rd_size = read(fd, &magic, size);
	if ((rd_size != size) || (magic != DBD_MAGIC)) {
		error("slurmdbd: state recover error");
		free_buf(buffer);
		return (Buf) NULL;
	}

	return buffer;
}

static void _load_dbd_state(void)
{
	char *dbd_fname;
	Buf buffer;
	int fd, recovered = 0;
	uint16_t rpc_version = 0;

	dbd_fname = slurm_get_state_save_location();
	xstrcat(dbd_fname, "/dbd.messages");
	fd = open(dbd_fname, O_RDONLY);
	if (fd < 0) {
		/* don't print an error message if there is no file */
		if (errno == ENOENT)
			debug4("slurmdbd: There is no state save file to "
			       "open by name %s", dbd_fname);
		else
			error("slurmdbd: Opening state save file %s: %m",
			      dbd_fname);
	} else {
		char *ver_str = NULL;
		uint32_t ver_str_len;

		buffer = _load_dbd_rec(fd);
		if (buffer == NULL)
			goto end_it;
		/* This is set to the end of the buffer for send so we
		   need to set it back to 0 */
		set_buf_offset(buffer, 0);
		safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
		debug3("Version string in dbd_state header is %s", ver_str);
	unpack_error:
		free_buf(buffer);
		buffer = NULL;
		if (ver_str) {
			/* get the version after VER */
			rpc_version = slurm_atoul(ver_str + 3);
			xfree(ver_str);
		}

		while (1) {
			/* If the buffer was not the VER%d string it
			   was an actual message so we don't want to
			   skip it.
			*/
			if (!buffer)
				buffer = _load_dbd_rec(fd);
			if (buffer == NULL)
				break;
			if (rpc_version != SLURM_PROTOCOL_VERSION) {
				/* unpack and repack with new
				 * PROTOCOL_VERSION just so we keep
				 * things up to date.
				 */
				slurmdbd_msg_t msg;
				int rc;
				set_buf_offset(buffer, 0);
				rc = unpack_slurmdbd_msg(
					&msg, rpc_version, buffer);
				free_buf(buffer);
				if (rc == SLURM_SUCCESS)
					buffer = pack_slurmdbd_msg(
						&msg, SLURM_PROTOCOL_VERSION);
				else
					buffer = NULL;
			}
			if (!buffer) {
				error("no buffer given");
				continue;
			}
			if (!list_enqueue(agent_list, buffer))
				fatal("slurmdbd: list_enqueue, no memory");
			recovered++;
			buffer = NULL;
		}

	end_it:
		verbose("slurmdbd: recovered %d pending RPCs", recovered);
		(void) close(fd);
	}
	xfree(dbd_fname);
}

static int _save_dbd_rec(int fd, Buf buffer)
{
	ssize_t size, wrote;
	uint32_t msg_size = get_buf_offset(buffer);
	uint32_t magic = DBD_MAGIC;
	char *msg = get_buf_data(buffer);

	size = sizeof(msg_size);
	wrote = write(fd, &msg_size, size);
	if (wrote != size) {
		error("slurmdbd: state save error: %m");
		return SLURM_ERROR;
	}

	wrote = 0;
	while (wrote < msg_size) {
		wrote = write(fd, msg, msg_size);
		if (wrote > 0) {
			msg += wrote;
			msg_size -= wrote;
		} else if ((wrote == -1) && (errno == EINTR))
			continue;
		else {
			error("slurmdbd: state save error: %m");
			return SLURM_ERROR;
		}
	}

	size = sizeof(magic);
	wrote = write(fd, &magic, size);
	if (wrote != size) {
		error("slurmdbd: state save error: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void _save_dbd_state(void)
{
	char *dbd_fname;
	Buf buffer;
	int fd, rc, wrote = 0;
	uint16_t msg_type;
	uint32_t offset;

	dbd_fname = slurm_get_state_save_location();
	xstrcat(dbd_fname, "/dbd.messages");
	(void) unlink(dbd_fname);	/* clear save state */
	fd = open(dbd_fname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		error("slurmdbd: Creating state save file %s", dbd_fname);
	} else if (agent_list && list_count(agent_list)) {
		char curr_ver_str[10];
		snprintf(curr_ver_str, sizeof(curr_ver_str),
			 "VER%d", SLURM_PROTOCOL_VERSION);
		buffer = init_buf(strlen(curr_ver_str));
		packstr(curr_ver_str, buffer);
		rc = _save_dbd_rec(fd, buffer);
		free_buf(buffer);
		if (rc != SLURM_SUCCESS)
			goto end_it;

		while ((buffer = list_dequeue(agent_list))) {
			/*
			 * We do not want to store registration messages. If an
			 * admin puts in an incorrect cluster name we can get a
			 * deadlock unless they add the bogus cluster name to
			 * the accounting system.
			 */
			offset = get_buf_offset(buffer);
			if (offset < 2) {
				free_buf(buffer);
				continue;
			}
			set_buf_offset(buffer, 0);
			(void) unpack16(&msg_type, buffer);  /* checked by offset */
			set_buf_offset(buffer, offset);
			if (msg_type == DBD_REGISTER_CTLD) {
				free_buf(buffer);
				continue;
			}

			rc = _save_dbd_rec(fd, buffer);
			free_buf(buffer);
			if (rc != SLURM_SUCCESS)
				break;
			wrote++;
		}
	}

end_it:
	if (fd >= 0) {
		verbose("slurmdbd: saved %d pending RPCs", wrote);
		(void) close(fd);
	}
	xfree(dbd_fname);
}

/* Open a connection to the Slurm DBD and set slurmdbd_conn */
static void _open_slurmdbd_conn(bool need_db)
{
	bool try_backup = true;
	int rc;

	if (slurmdbd_conn && slurmdbd_conn->fd >= 0) {
		debug("Attempt to re-open slurmdbd socket");
		/* clear errno (checked after this for errors) */
		errno = 0;
		return;
	}

	slurm_persist_conn_close(slurmdbd_conn);
	if (!slurmdbd_conn) {
		slurmdbd_conn = xmalloc(sizeof(slurm_persist_conn_t));
		slurmdbd_conn->flags =
			PERSIST_FLAG_DBD | PERSIST_FLAG_RECONNECT;
		slurmdbd_conn->persist_type = PERSIST_TYPE_DBD;

		if (!slurmdbd_cluster)
			slurmdbd_cluster = slurm_get_cluster_name();

		slurmdbd_conn->cluster_name = xstrdup(slurmdbd_cluster);

		slurmdbd_conn->timeout = (slurm_get_msg_timeout() + 35) * 1000;

		slurmdbd_conn->rem_port = slurm_get_accounting_storage_port();

		if (!slurmdbd_conn->rem_port) {
			slurmdbd_conn->rem_port = SLURMDBD_PORT;
			slurm_set_accounting_storage_port(
				slurmdbd_conn->rem_port);
		}
	}
	slurmdbd_shutdown = 0;
	slurmdbd_conn->shutdown = &slurmdbd_shutdown;
	slurmdbd_conn->version  = SLURM_PROTOCOL_VERSION;

	xfree(slurmdbd_conn->rem_host);
	slurmdbd_conn->rem_host = slurm_get_accounting_storage_host();
	if (!slurmdbd_conn->rem_host) {
		slurmdbd_conn->rem_host = xstrdup(DEFAULT_STORAGE_HOST);
		slurm_set_accounting_storage_host(
			slurmdbd_conn->rem_host);
	}

again:

	if (((rc = slurm_persist_conn_open(slurmdbd_conn)) != SLURM_SUCCESS) &&
	    try_backup) {
		xfree(slurmdbd_conn->rem_host);
		try_backup = false;
		if ((slurmdbd_conn->rem_host =
		     slurm_get_accounting_storage_backup_host()))
			goto again;
	}

	if (rc == SLURM_SUCCESS) {
		/* set the timeout to the timeout to be used for all other
		 * messages */
		slurmdbd_conn->timeout = SLURMDBD_TIMEOUT * 1000;
		if (slurmdbd_conn->trigger_callbacks.dbd_resumed)
			(slurmdbd_conn->trigger_callbacks.dbd_resumed)();
		if (slurmdbd_conn->trigger_callbacks.db_resumed)
			(slurmdbd_conn->trigger_callbacks.db_resumed)();
	}

	if ((!need_db && (rc == ESLURM_DB_CONNECTION)) ||
	    (rc == SLURM_SUCCESS)) {
		debug("slurmdbd: Sent PersistInit msg");
		/* clear errno (checked after this for
		   errors)
		*/
		errno = 0;
	} else {
		if ((rc == ESLURM_DB_CONNECTION) &&
		    slurmdbd_conn->trigger_callbacks.db_fail)
			(slurmdbd_conn->trigger_callbacks.db_fail)();

		error("slurmdbd: Sending PersistInit msg: %m");
		slurm_persist_conn_close(slurmdbd_conn);
	}
}

static void _sig_handler(int signal)
{
}

static void *_agent(void *x)
{
	int cnt, rc;
	Buf buffer;
	struct timespec abs_time;
	static time_t fail_time = 0;
	int sigarray[] = {SIGUSR1, 0};
	slurmdbd_msg_t list_req;
	dbd_list_msg_t list_msg;

	list_req.msg_type = DBD_SEND_MULT_MSG;
	list_req.data = &list_msg;
	memset(&list_msg, 0, sizeof(dbd_list_msg_t));
	/* DEF_TIMERS; */

	/* Prepare to catch SIGUSR1 to interrupt pending
	 * I/O and terminate in a timely fashion. */
	xsignal(SIGUSR1, _sig_handler);
	xsignal_unblock(sigarray);

	while (*slurmdbd_conn->shutdown == 0) {
		/* START_TIMER; */
		slurm_mutex_lock(&slurmdbd_lock);
		if (halt_agent)
			slurm_cond_wait(&slurmdbd_cond, &slurmdbd_lock);

		if ((slurmdbd_conn->fd < 0) &&
		    (difftime(time(NULL), fail_time) >= 10)) {
			/* The connection to Slurm DBD is not open */
			_open_slurmdbd_conn(1);
			if (slurmdbd_conn->fd < 0)
				fail_time = time(NULL);
		}

		slurm_mutex_lock(&agent_lock);
		if (agent_list && slurmdbd_conn->fd)
			cnt = list_count(agent_list);
		else
			cnt = 0;
		if ((cnt == 0) || (slurmdbd_conn->fd < 0) ||
		    (fail_time && (difftime(time(NULL), fail_time) < 10))) {
			slurm_mutex_unlock(&slurmdbd_lock);
			abs_time.tv_sec  = time(NULL) + 10;
			abs_time.tv_nsec = 0;
			slurm_cond_timedwait(&agent_cond, &agent_lock,
					     &abs_time);
			slurm_mutex_unlock(&agent_lock);
			continue;
		} else if ((cnt > 0) && ((cnt % 100) == 0))
			info("slurmdbd: agent queue size %u", cnt);
		/* Leave item on the queue until processing complete */
		if (agent_list) {
			int handle_agent_count = 1000;
			if (cnt > handle_agent_count) {
				int agent_count = 0;
				ListIterator agent_itr =
					list_iterator_create(agent_list);
				list_msg.my_list = list_create(NULL);
				while ((buffer = list_next(agent_itr))) {
					list_enqueue(list_msg.my_list, buffer);
					agent_count++;
					if (agent_count > handle_agent_count)
						break;
				}
				list_iterator_destroy(agent_itr);
				buffer = pack_slurmdbd_msg(
					&list_req, SLURM_PROTOCOL_VERSION);
			} else if (cnt > 1) {
				list_msg.my_list = agent_list;
				buffer = pack_slurmdbd_msg(
					&list_req, SLURM_PROTOCOL_VERSION);
			} else
				buffer = (Buf) list_peek(agent_list);
		} else
			buffer = NULL;
		slurm_mutex_unlock(&agent_lock);
		if (buffer == NULL) {
			slurm_mutex_unlock(&slurmdbd_lock);

			slurm_mutex_lock(&assoc_cache_mutex);
			if (slurmdbd_conn->fd >= 0 && running_cache)
				slurm_cond_signal(&assoc_cache_cond);
			slurm_mutex_unlock(&assoc_cache_mutex);

			continue;
		}

		/* NOTE: agent_lock is clear here, so we can add more
		 * requests to the queue while waiting for this RPC to
		 * complete. */
		rc = slurm_persist_send_msg(slurmdbd_conn, buffer);
		if (rc != SLURM_SUCCESS) {
			if (*slurmdbd_conn->shutdown) {
				slurm_mutex_unlock(&slurmdbd_lock);
				break;
			}
			error("slurmdbd: Failure sending message: %d: %m", rc);
		} else if (list_msg.my_list) {
			rc = _handle_mult_rc_ret();
		} else {
			rc = _get_return_code();
			if (rc == EAGAIN) {
				if (*slurmdbd_conn->shutdown) {
					slurm_mutex_unlock(&slurmdbd_lock);
					break;
				}
				error("slurmdbd: Failure with "
				      "message need to resend: %d: %m", rc);
			}
		}
		slurm_mutex_unlock(&slurmdbd_lock);
		slurm_mutex_lock(&assoc_cache_mutex);
		if (slurmdbd_conn->fd >= 0 && running_cache)
			slurm_cond_signal(&assoc_cache_cond);
		slurm_mutex_unlock(&assoc_cache_mutex);

		slurm_mutex_lock(&agent_lock);
		if (agent_list && (rc == SLURM_SUCCESS)) {
			/*
			 * If we sent a mult_msg we just need to free buffer,
			 * we don't need to requeue, just mark list_msg.my_list
			 * as NULL as that is the sign we sent a mult_msg.
			 */
			if (list_msg.my_list) {
				if (list_msg.my_list != agent_list)
					FREE_NULL_LIST(list_msg.my_list);
				list_msg.my_list = NULL;
			} else
				buffer = (Buf) list_dequeue(agent_list);

			free_buf(buffer);
			fail_time = 0;
		} else {
			/* We need to free a mult_msg even on failure */
			if (list_msg.my_list) {
				if (list_msg.my_list != agent_list)
					FREE_NULL_LIST(list_msg.my_list);
				list_msg.my_list = NULL;
				free_buf(buffer);
			}

			fail_time = time(NULL);
		}
		slurm_mutex_unlock(&agent_lock);
		/* END_TIMER; */
		/* info("at the end with %s", TIME_STR); */
	}

	slurm_mutex_lock(&agent_lock);
	_save_dbd_state();
	FREE_NULL_LIST(agent_list);
	slurm_mutex_unlock(&agent_lock);
	return NULL;
}

static void _create_agent(void)
{
	/* this needs to be set because the agent thread will do
	   nothing if the connection was closed and then opened again */
	slurmdbd_shutdown = 0;

	if (agent_list == NULL) {
		agent_list = list_create(slurmdbd_free_buffer);
		_load_dbd_state();
	}

	if (agent_tid == 0) {
		slurm_thread_create(&agent_tid, _agent, NULL);
	}
}

/* Purge queued step records from the agent queue
 * RET number of records purged */
static int _purge_step_req(void)
{
	int purged = 0;
	ListIterator iter;
	uint16_t msg_type;
	uint32_t offset;
	Buf buffer;

	iter = list_iterator_create(agent_list);
	while ((buffer = list_next(iter))) {
		offset = get_buf_offset(buffer);
		if (offset < 2)
			continue;
		set_buf_offset(buffer, 0);
		(void) unpack16(&msg_type, buffer);	/* checked by offset */
		set_buf_offset(buffer, offset);
		if ((msg_type == DBD_STEP_START) ||
		    (msg_type == DBD_STEP_COMPLETE)) {
			list_remove(iter);
			purged++;
		}
	}
	list_iterator_destroy(iter);
	info("slurmdbd: purge %d step records", purged);
	return purged;
}

/* Purge queued job start records from the agent queue
 * RET number of records purged */
static int _purge_job_start_req(void)
{
	int purged = 0;
	ListIterator iter;
	uint16_t msg_type;
	uint32_t offset;
	Buf buffer;

	iter = list_iterator_create(agent_list);
	while ((buffer = list_next(iter))) {
		offset = get_buf_offset(buffer);
		if (offset < 2)
			continue;
		set_buf_offset(buffer, 0);
		(void) unpack16(&msg_type, buffer);	/* checked by offset */
		set_buf_offset(buffer, offset);
		if (msg_type == DBD_JOB_START) {
			list_remove(iter);
			purged++;
		}
	}
	list_iterator_destroy(iter);
	info("slurmdbd: purge %d job start records", purged);
	return purged;
}

static void _shutdown_agent(void)
{
	int i;

	if (agent_tid) {
		slurmdbd_shutdown = time(NULL);
		for (i=0; i<50; i++) {	/* up to 5 secs total */
			slurm_cond_broadcast(&agent_cond);
			usleep(100000);	/* 0.1 sec per try */
			if (pthread_kill(agent_tid, SIGUSR1))
				break;

		}
		/* On rare occasions agent thread may not end quickly,
		 * perhaps due to communication problems with slurmdbd.
		 * Cancel it and join before returning or we could remove
		 * and leave the agent without valid data */
		if (pthread_kill(agent_tid, 0) == 0) {
			error("slurmdbd: agent failed to shutdown gracefully");
			error("slurmdbd: unable to save pending requests");
			pthread_cancel(agent_tid);
		}
		pthread_join(agent_tid,  NULL);
		agent_tid = 0;
	}
}

/****************************************************************************
 * Socket open/close/read/write functions
 ****************************************************************************/

/* Open a socket connection to SlurmDbd
 * callbacks IN - make agent to process RPCs and contains callback pointers
 * persist_conn_flags OUT - fill in from response of slurmdbd
 * Returns SLURM_SUCCESS or an error code */
extern int open_slurmdbd_conn(const slurm_trigger_callbacks_t *callbacks,
			      uint16_t *persist_conn_flags)
{
	int tmp_errno = SLURM_SUCCESS;
	/* we need to set this up before we make the agent or we will
	 * get a threading issue. */

	slurm_mutex_lock(&slurmdbd_lock);

	if (!slurmdbd_conn) {
		_open_slurmdbd_conn(1);
		if (persist_conn_flags)
			*persist_conn_flags = slurmdbd_conn->flags;
		tmp_errno = errno;
	}
	slurm_mutex_unlock(&slurmdbd_lock);

	slurm_mutex_lock(&agent_lock);
	/* Initialize the callback pointers */
	if (callbacks != NULL) {
		/* copy the user specified callback pointers */
		memcpy(&(slurmdbd_conn->trigger_callbacks), callbacks,
		       sizeof(slurm_trigger_callbacks_t));
	} else {
		memset(&slurmdbd_conn->trigger_callbacks, 0,
		       sizeof(slurm_trigger_callbacks_t));
	}

	if ((callbacks != NULL) && ((agent_tid == 0) || (agent_list == NULL)))
		_create_agent();
	else if (agent_list)
		_load_dbd_state();

	slurm_mutex_unlock(&agent_lock);
	if (tmp_errno) {
		errno = tmp_errno;
		return tmp_errno;
	} else if (slurmdbd_conn->fd < 0)
		return SLURM_ERROR;
	else
		return SLURM_SUCCESS;
}

/* Close the SlurmDBD socket connection */
extern int close_slurmdbd_conn(void)
{
	/* NOTE: agent_lock not needed for _shutdown_agent() */
	_shutdown_agent();

	/*
	 * Only send the FINI message if we haven't shutdown
	 * (i.e. not slurmctld)
	 */
	if (!slurmdbd_shutdown) {
		if (_send_fini_msg() != SLURM_SUCCESS)
			error("slurmdbd: Sending fini msg: %m");
		else
			debug("slurmdbd: Sent fini msg");
	}

	slurm_mutex_lock(&slurmdbd_lock);
	slurm_persist_conn_destroy(slurmdbd_conn);
	slurmdbd_conn = NULL;
	xfree(slurmdbd_cluster);
	slurm_mutex_unlock(&slurmdbd_lock);

	return SLURM_SUCCESS;
}

/* Send an RPC to the SlurmDBD and wait for an arbitrary reply message.
 * The RPC will not be queued if an error occurs.
 * The "resp" message must be freed by the caller.
 * Returns SLURM_SUCCESS or an error code */
extern int send_recv_slurmdbd_msg(uint16_t rpc_version,
				  slurmdbd_msg_t *req,
				  slurmdbd_msg_t *resp)
{
	int rc = SLURM_SUCCESS;
	Buf buffer;

	xassert(req);
	xassert(resp);

	/* To make sure we can get this to send instead of the agent
	   sending stuff that can happen anytime we set halt_agent and
	   then after we get into the mutex we unset.
	*/
	halt_agent = 1;
	slurm_mutex_lock(&slurmdbd_lock);
	halt_agent = 0;
	if (!slurmdbd_conn || (slurmdbd_conn->fd < 0)) {
		/* Either slurm_open_slurmdbd_conn() was not executed or
		 * the connection to Slurm DBD has been closed */
		if (req->msg_type == DBD_GET_CONFIG)
			_open_slurmdbd_conn(0);
		else
			_open_slurmdbd_conn(1);
		if (!slurmdbd_conn || (slurmdbd_conn->fd < 0)) {
			rc = SLURM_ERROR;
			goto end_it;
		}
	}

	if (!(buffer = pack_slurmdbd_msg(req, rpc_version))) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	rc = slurm_persist_send_msg(slurmdbd_conn, buffer);
	free_buf(buffer);
	if (rc != SLURM_SUCCESS) {
		error("slurmdbd: Sending message type %s: %d: %m",
		      rpc_num2string(req->msg_type), rc);
		goto end_it;
	}

	buffer = slurm_persist_recv_msg(slurmdbd_conn);
	if (buffer == NULL) {
		error("slurmdbd: Getting response to message type %u",
		      req->msg_type);
		rc = SLURM_ERROR;
		goto end_it;
	}

	rc = unpack_slurmdbd_msg(resp, rpc_version, buffer);
	/* check for the rc of the start job message */
	if (rc == SLURM_SUCCESS && resp->msg_type == DBD_ID_RC)
		rc = ((dbd_id_rc_msg_t *)resp->data)->return_code;

	free_buf(buffer);
end_it:
	slurm_cond_signal(&slurmdbd_cond);
	slurm_mutex_unlock(&slurmdbd_lock);

	return rc;
}

/* Send an RPC to the SlurmDBD and wait for the return code reply.
 * The RPC will not be queued if an error occurs.
 * Returns SLURM_SUCCESS or an error code */
extern int send_slurmdbd_recv_rc_msg(uint16_t rpc_version,
				     slurmdbd_msg_t *req,
				     int *resp_code)
{
	int rc;
	slurmdbd_msg_t resp;

	xassert(req);
	xassert(resp_code);

	memset(&resp, 0, sizeof(slurmdbd_msg_t));
	rc = send_recv_slurmdbd_msg(rpc_version, req, &resp);
	if (rc != SLURM_SUCCESS) {
		;	/* error message already sent */
	} else if (resp.msg_type != PERSIST_RC) {
		error("slurmdbd: response is not type PERSIST_RC: %s(%u)",
		      slurmdbd_msg_type_2_str(resp.msg_type, 1),
		      resp.msg_type);
		rc = SLURM_ERROR;
	} else {	/* resp.msg_type == PERSIST_RC */
		persist_rc_msg_t *msg = resp.data;
		*resp_code = msg->rc;
		if (msg->rc != SLURM_SUCCESS &&
		    msg->rc != ACCOUNTING_FIRST_REG &&
		    msg->rc != ACCOUNTING_TRES_CHANGE_DB &&
		    msg->rc != ACCOUNTING_NODES_CHANGE_DB) {
			char *comment = msg->comment;
			if (!comment)
				comment = slurm_strerror(msg->rc);
			if (msg->ret_info == DBD_REGISTER_CTLD &&
			    slurm_get_accounting_storage_enforce()) {
				error("slurmdbd: Issue with call "
				      "%s(%u): %u(%s)",
				      slurmdbd_msg_type_2_str(
					      msg->ret_info, 1),
				      msg->ret_info, msg->rc,
				      comment);
				fatal("You need to add this cluster "
				      "to accounting if you want to "
				      "enforce associations, or no "
				      "jobs will ever run.");
			} else
				debug("slurmdbd: Issue with call "
				      "%s(%u): %u(%s)",
				      slurmdbd_msg_type_2_str(
					      msg->ret_info, 1),
				      msg->ret_info, msg->rc,
				      comment);
		}
		slurm_persist_free_rc_msg(msg);
	}

	return rc;
}

/* Send an RPC to the SlurmDBD. Do not wait for the reply. The RPC
 * will be queued and processed later if the SlurmDBD is not responding.
 * NOTE: slurm_open_slurmdbd_conn() must have been called with callbacks set
 *
 * Returns SLURM_SUCCESS or an error code */
extern int send_slurmdbd_msg(uint16_t rpc_version, slurmdbd_msg_t *req)
{
	Buf buffer;
	int cnt, rc = SLURM_SUCCESS;
	static time_t syslog_time = 0;
	static int max_agent_queue = 0;

	/*
	 * Whatever our max job count is multiplied by 2 plus node count
	 * multiplied by 4 or MAX_AGENT_QUEUE which ever is bigger.
	 */
	if (!max_agent_queue)
		max_agent_queue =
			MAX(MAX_AGENT_QUEUE,
			    ((slurmctld_conf.max_job_cnt * 2) +
			     (node_record_count * 4)));

	buffer = slurm_persist_msg_pack(
		slurmdbd_conn, (persist_msg_t *)req);
	if (!buffer)	/* pack error */
		return SLURM_ERROR;

	slurm_mutex_lock(&agent_lock);
	if ((agent_tid == 0) || (agent_list == NULL)) {
		_create_agent();
		if ((agent_tid == 0) || (agent_list == NULL)) {
			slurm_mutex_unlock(&agent_lock);
			free_buf(buffer);
			return SLURM_ERROR;
		}
	}
	cnt = list_count(agent_list);
	if ((cnt >= (max_agent_queue / 2)) &&
	    (difftime(time(NULL), syslog_time) > 120)) {
		/* Record critical error every 120 seconds */
		syslog_time = time(NULL);
		error("slurmdbd: agent queue filling (%d), RESTART SLURMDBD NOW",
		      cnt);
		syslog(LOG_CRIT, "*** RESTART SLURMDBD NOW ***");
		if (slurmdbd_conn->trigger_callbacks.dbd_fail)
			(slurmdbd_conn->trigger_callbacks.dbd_fail)();
	}
	if (cnt == (max_agent_queue - 1))
		cnt -= _purge_step_req();
	if (cnt == (max_agent_queue - 1))
		cnt -= _purge_job_start_req();
	if (cnt < max_agent_queue) {
		if (list_enqueue(agent_list, buffer) == NULL)
			fatal("list_enqueue: memory allocation failure");
	} else {
		error("slurmdbd: agent queue is full (%u), discarding %s:%u request",
		      cnt,
		      slurmdbd_msg_type_2_str(req->msg_type, 1),
		      req->msg_type);
		if (slurmdbd_conn->trigger_callbacks.acct_full)
			(slurmdbd_conn->trigger_callbacks.acct_full)();
		free_buf(buffer);
		rc = SLURM_ERROR;
	}

	slurm_cond_broadcast(&agent_cond);
	slurm_mutex_unlock(&agent_lock);
	return rc;
}

/* Return true if connection to slurmdbd is active, false otherwise. */
extern bool slurmdbd_conn_active(void)
{
	if (!slurmdbd_conn || (slurmdbd_conn->fd < 0))
		return false;

	return true;
}

extern int slurmdbd_agent_queue_count(void)
{
	if (!agent_list)
		return 0;
	return list_count(agent_list);
}
