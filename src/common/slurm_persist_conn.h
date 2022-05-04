/*****************************************************************************\
 *  slurm_persist_conn.h - Definitions for communicating over a persistent
 *                         connection within Slurm.
 ******************************************************************************
 *  Copyright (C) 2016 SchedMD LLC
 *  Written by Danny Auble da@schedmd.com, et. al.
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
#ifndef _SLURM_PERSIST_CONN_H
#define _SLURM_PERSIST_CONN_H

#include "slurm/slurm.h"
#include "pack.h"

#define PERSIST_FLAG_NONE           0x0000
#define PERSIST_FLAG_DBD            0x0001
#define PERSIST_FLAG_RECONNECT      0x0002
#define PERSIST_FLAG_ALREADY_INITED 0x0004
#define PERSIST_FLAG_P_USER_CASE    0x0008
#define PERSIST_FLAG_SUPPRESS_ERR   0x0010
#define PERSIST_FLAG_EXT_DBD        0x0020

typedef enum {
	PERSIST_TYPE_NONE = 0,
	PERSIST_TYPE_DBD,
	PERSIST_TYPE_FED,
	PERSIST_TYPE_HA_CTL,
	PERSIST_TYPE_HA_DBD,
} persist_conn_type_t;

typedef struct {
	void *conn;		/* slurm_persist_conn_t */
	void *data;		/* pointer to a message type below */
	uint32_t data_size;     /* size of data */
	uint16_t msg_type;	/* slurmdbd_msg_type_t / slurm_msg_type_t */
} persist_msg_t;

typedef struct {
	void *auth_cred;
	int (*callback_proc)(void *arg, persist_msg_t *msg,
			     buf_t **out_buffer, uint32_t *uid);
	void (*callback_fini)(void *arg);
	char *cluster_name;
	time_t comm_fail_time;	/* avoid constant error messages */
	uint16_t my_port;
	int fd;
	uint16_t flags;
	bool inited;
	persist_conn_type_t persist_type;
	uid_t r_uid;
	char *rem_host;
	uint16_t rem_port;
	time_t *shutdown;
	pthread_t thread_id;
	int timeout;
	slurm_trigger_callbacks_t trigger_callbacks;
	uint16_t version;
} slurm_persist_conn_t;

typedef struct {
	char *cluster_name;     /* cluster this message is coming from */
	uint16_t persist_type;	/* really persist_conn_type_t, uint16_t on wire */
	uint16_t port;          /* If you want to open a new connection, this is
				 *  the port to talk to. */
	uint16_t version;	/* protocol version */
	uint32_t uid;		/* UID originating connection,
				 * filled by authtentication plugin*/
} persist_init_req_msg_t;

typedef struct {
	char *comment;
	uint16_t flags;
	uint32_t rc;
	uint16_t ret_info; /* protocol version we are connecting to since we
			    * sent the lowest one to begin with, or the return
			    * of a message type sent. */
} persist_rc_msg_t;

/* setup a daemon to receive incoming persistent connections. */
extern void slurm_persist_conn_recv_server_init(void);

/* finish up any persistent connections we are listening to */
extern void slurm_persist_conn_recv_server_fini(void);

/* Create a thread that will wait listening on the fd in the
 * slurm_persist_conn_t.
 * IN - persist_conn - persistent connection to listen to.  This will be freed
 *                     internally, so forget about once it enters here.
 * IN - thread_loc - location in the persist_conn thread pool.  This number can
 *                   be got from slurm_persist_conn_wait_for_thread_loc or given
 *                   -1 to get one inside the function.
 * IN - arg - arbitrary argument that will be sent to the callback as well as
 *            the callback in the persist_conn.
 */
extern void slurm_persist_conn_recv_thread_init(slurm_persist_conn_t *persist_conn,
						int thread_loc, void *arg);

/* Increment thread_count and don't return until its value is no larger
 *	than MAX_THREAD_COUNT,
 * RET index of free index in persist_pthread_id or -1 to exit */
extern int slurm_persist_conn_wait_for_thread_loc(void);

/* Free the index given from slurm_persist_conn_wait_for_thread_loc */
extern void slurm_persist_conn_free_thread_loc(int thread_loc);


/* Open a persistent socket connection
 * IN/OUT - persistent connection needing host and port filled in.  Returned
 * mostly filled in without the version to use to communicate.
 * Returns SLURM_SUCCESS on success or SLURM_ERROR on failure */
extern int slurm_persist_conn_open_without_init(
	slurm_persist_conn_t *persist_conn);

/* Open a persistent socket connection and sends an init message to establish
 * the connection.
 * IN/OUT - persistent connection needing host and port filled in.  Returned
 * completely filled in.
 * Returns SLURM_SUCCESS on success or SLURM_ERROR on failure */
extern int slurm_persist_conn_open(slurm_persist_conn_t *persist_conn);

/* Close the persistent connection don't free structure or members */
extern void slurm_persist_conn_close(slurm_persist_conn_t *persist_conn);

extern int slurm_persist_conn_reopen(slurm_persist_conn_t *persist_conn,
				     bool with_init);

/* Close the persistent connection members, but don't free structure */
extern void slurm_persist_conn_members_destroy(
	slurm_persist_conn_t *persist_conn);

/* Close the persistent connection and free structure */
extern void slurm_persist_conn_destroy(slurm_persist_conn_t *persist_conn);

extern int slurm_persist_conn_process_msg(slurm_persist_conn_t *persist_conn,
					  persist_msg_t *persist_msg,
					  char *msg_char, uint32_t msg_size,
					  buf_t **out_buffer, bool first);

extern int slurm_persist_conn_writeable(slurm_persist_conn_t *persist_conn);

extern int slurm_persist_send_msg(slurm_persist_conn_t *persist_conn,
				  buf_t *buffer);
extern buf_t *slurm_persist_recv_msg(slurm_persist_conn_t *persist_conn);


extern buf_t *slurm_persist_msg_pack(slurm_persist_conn_t *persist_conn,
				     persist_msg_t *req_msg);
extern int slurm_persist_msg_unpack(slurm_persist_conn_t *persist_conn,
				    persist_msg_t *resp_msg, buf_t *buffer);

extern void slurm_persist_pack_init_req_msg(persist_init_req_msg_t *msg,
					    buf_t *buffer);
extern int slurm_persist_unpack_init_req_msg(persist_init_req_msg_t **msg,
					     buf_t *buffer);
extern void slurm_persist_free_init_req_msg(persist_init_req_msg_t *msg);

extern void slurm_persist_pack_rc_msg(persist_rc_msg_t *msg, buf_t *buffer,
				      uint16_t protocol_version);
extern int slurm_persist_unpack_rc_msg(persist_rc_msg_t **msg, buf_t *buffer,
				       uint16_t protocol_version);
extern void slurm_persist_free_rc_msg(persist_rc_msg_t *msg);

extern buf_t *slurm_persist_make_rc_msg(slurm_persist_conn_t *persist_conn,
					uint32_t rc, char *comment,
					uint16_t ret_info);

extern buf_t *slurm_persist_make_rc_msg_flags(slurm_persist_conn_t *persist_conn,
					      uint32_t rc, char *comment,
					      uint16_t flags,
					      uint16_t ret_info);

#endif
