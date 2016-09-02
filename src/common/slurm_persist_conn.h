/*****************************************************************************\
 *  slurm_persist_conn.h - Definitions for communicating over a persistant
 *                         connection within Slurm.
 ******************************************************************************
 *  Copyright (C) 2016 SchedMD LLC
 *  Written by Danny Auble da@schedmd.com, et. al.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#ifndef _SLURM_PERSIST_CONN_H
#define _SLURM_PERSIST_CONN_H

#include "slurm/slurm.h"

#define PERSIST_FLAG_NONE      0x0000
#define PERSIST_FLAG_DBD       0x0001
#define PERSIST_FLAG_RECONNECT 0x0002

typedef struct {
	void *auth_cred;
	char *cluster_name;
	int fd;
	uint16_t flags;
	bool inited;
	char *rem_host;
	int rem_port;
	time_t *shutdown;
	int timeout;
	slurm_trigger_callbacks_t trigger_callbacks;
	uint16_t version;
} slurm_persist_conn_t;

typedef struct {
	uint16_t msg_type;	/* see slurmdbd_msg_type_t or
				 * slurm_msg_type_t */
	void * data;		/* pointer to a message type below */
} persist_msg_t;

typedef struct {
	char *cluster_name;     /* cluster this message is coming from */
	uint16_t version;	/* protocol version */
	uint32_t uid;		/* UID originating connection,
				 * filled by authtentication plugin*/
} persist_init_req_msg_t;

typedef struct {
	char *comment;
	uint32_t rc;
	uint16_t ret_info; /* protocol version we are connecting to since we
			    * sent the lowest one to begin with, or the return
			    * of a message type sent. */
} persist_rc_msg_t;

/* Open a persistant socket connection
 * IN/OUT - persistant connection needing host and port filled in.  Returned
 * mostly filled in without the version to use to communicate.
 * Returns SLURM_SUCCESS on success or SLURM_ERROR on failure */
extern int slurm_persist_conn_open_without_init(
	slurm_persist_conn_t *persist_conn);

/* Open a persistant socket connection and sends an init message to establish
 * the connection.
 * IN/OUT - persistant connection needing host and port filled in.  Returned
 * completely filled in.
 * Returns SLURM_SUCCESS on success or SLURM_ERROR on failure */
extern int slurm_persist_conn_open(slurm_persist_conn_t *persist_conn);

/* Close the persistant connection don't free structure or members */
extern void slurm_persist_conn_close(slurm_persist_conn_t *persist_conn);

extern int slurm_persist_conn_reopen(slurm_persist_conn_t *persist_conn,
				     bool with_init);

/* Close the persistant connection members, but don't free structure */
extern void slurm_persist_conn_members_destroy(
	slurm_persist_conn_t *persist_conn);

/* Close the persistant connection and free structure */
extern void slurm_persist_conn_destroy(slurm_persist_conn_t *persist_conn);

extern int slurm_persist_conn_process_msg(slurm_persist_conn_t *persist_conn,
					  persist_msg_t *persist_msg,
					  char *msg_char, uint32_t msg_size,
					  Buf *out_buffer, bool first);

extern int slurm_persist_conn_writeable(slurm_persist_conn_t *persist_conn);

extern int slurm_persist_send_msg(
	slurm_persist_conn_t *persist_conn, Buf buffer);
extern Buf slurm_persist_recv_msg(slurm_persist_conn_t *persist_conn);


extern Buf slurm_persist_msg_pack(slurm_persist_conn_t *persist_conn,
				  persist_msg_t *req_msg);
extern int slurm_persist_msg_unpack(slurm_persist_conn_t *persist_conn,
				    persist_msg_t *resp_msg, Buf buffer);

extern void slurm_persist_pack_init_req_msg(
	persist_init_req_msg_t *msg, Buf buffer);
extern int slurm_persist_unpack_init_req_msg(
	persist_init_req_msg_t **msg, Buf buffer);
extern void slurm_persist_free_init_req_msg(persist_init_req_msg_t *msg);

extern void slurm_persist_pack_rc_msg(
	persist_rc_msg_t *msg, Buf buffer, uint16_t protocol_version);
extern int slurm_persist_unpack_rc_msg(
	persist_rc_msg_t **msg, Buf buffer, uint16_t protocol_version);
extern void slurm_persist_free_rc_msg(persist_rc_msg_t *msg);

extern int slurm_persist_send_recv_msg(slurm_persist_conn_t *persist_conn,
				       persist_msg_t *req, persist_msg_t *resp);

extern Buf slurm_persist_make_rc_msg(slurm_persist_conn_t *persist_conn,
				     uint32_t rc, char *comment,
				     uint16_t ret_info);

#endif
