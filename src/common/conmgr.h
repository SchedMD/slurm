/*****************************************************************************\
 *  conmgr.h - declarations for connection manager
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#ifndef _CONMGR_H
#define _CONMGR_H

#include <netdb.h>

#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/workq.h"

/*
 * connection manager will do the follow:
 * 	maintain a list of active connections by
 * 		ip/source
 *		user
 *	hand out fd for processing
 *	hold fd until ready for processing
 */

/*
 * Connection tracking pointer
 * Opaque struct - do not access directly
 */
typedef struct conmgr_fd_s conmgr_fd_t;

/*
 * Struct of call backs to call on events
 * of a given connection.
 */
typedef struct {
	/*
	 * Call back for new connection for setup
	 *
	 * IN fd file descriptor of new connection
	 * IN arg - arg ptr handed to fd processing functions
	 * RET arg ptr to hand to events
	 */
	void *(*on_connection)(conmgr_fd_t *con, void *arg);

	/*
	 * Call back when there is data ready in "in" buffer
	 * This may be called several times in the same connection.
	 * Only called when type = CON_TYPE_RAW.
	 *
	 * IN con connection handler
	 * IN arg ptr to be handed return of conmgr_on_new_connection_t().
	 * RET SLURM_SUCCESS or error to kill connection
	 */
	int (*on_data)(conmgr_fd_t *con, void *arg);

	/*
	 * Call back when there is new RPC msg ready
	 * This may be called several times in the same connection.
	 * Only called when type = CON_TYPE_RPC.
	 *
	 * IN con connection handler
	 * IN msg ptr to new msg (call must slurm_free_msg())
	 * IN arg ptr to be handed return of conmgr_on_new_connection_t().
	 * RET SLURM_SUCCESS or error to kill connection
	 */
	int (*on_msg)(conmgr_fd_t *con, slurm_msg_t *msg, void *arg);

	/*
	 * Call back when connection ended.
	 * Called once per connection.
	 *
	 * IN arg ptr to be handed return of conmgr_on_new_connection_t().
	 * 	must free arg as required.
	 */
	void (*on_finish)(void *arg);
} conmgr_events_t;

typedef struct {
	const char *host;
	const char *port; /* port as string for later parsing */
} parsed_host_port_t;

typedef struct {
	/*
	 * Parse a combined host:port string into host and port
	 * IN str host:port string for parsing
	 * OUT parsed will be populated with strings (must xfree())
	 * RET SLURM_SUCCESS or error
	 */
	parsed_host_port_t *(*parse)(const char *str);

	/*
	 * Free parsed_host_port_t returned from parse_host_port_t()
	 */
	void (*free_parse)(parsed_host_port_t *parsed);
} conmgr_callbacks_t;

typedef enum {
	CONMGR_WORK_STATUS_INVALID = 0,
	CONMGR_WORK_STATUS_PENDING,
	CONMGR_WORK_STATUS_RUN,
	CONMGR_WORK_STATUS_CANCELLED,
	CONMGR_WORK_STATUS_MAX /* place holder */
} conmgr_work_status_t;

extern const char *conmgr_work_status_string(conmgr_work_status_t status);

typedef enum {
	CONMGR_WORK_TYPE_INVALID = 0,
	CONMGR_WORK_TYPE_CONNECTION_FIFO, /* connection specific work ordered by FIFO */
	CONMGR_WORK_TYPE_CONNECTION_WRITE_COMPLETE, /* call once all connection writes complete then FIFO */
	CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO, /* call once time delay completes then FIFO */
	CONMGR_WORK_TYPE_FIFO, /* non-connection work ordered by FIFO */
	CONMGR_WORK_TYPE_TIME_DELAY_FIFO, /* call once time delay completes then FIFO */
	CONMGR_WORK_TYPE_MAX /* place holder */
} conmgr_work_type_t;

extern const char *conmgr_work_type_string(conmgr_work_type_t type);

/*
 * Prototype for all conmgr callbacks
 * IN con - ptr to relavent connection (or NULL)
 * IN type - work type
 * IN status - work status
 * IN tag - logging tag for work
 * IN arg - arbitrary pointer
 */
typedef void (*conmgr_work_func_t)(conmgr_fd_t *con, conmgr_work_type_t type,
				   conmgr_work_status_t status,
				   const char *tag, void *arg);

/*
 * conmgr can handle RPC or raw connections
 */
typedef enum {
	CON_TYPE_INVALID = 0,
	CON_TYPE_RAW, /* handle data unprocessed to/from */
	CON_TYPE_RPC, /* handle data Slurm RPCs to/from */
	CON_TYPE_MAX /* place holder - do not use */
} conmgr_con_type_t;

/*
 * Initialise global connection manager
 * IN thread_count - number of threads to create or 0 for default
 * IN max_connections - max number of connections or 0 for default
 * IN callbacks - struct containing function pointers
 */
extern void init_conmgr(int thread_count, int max_connections,
			conmgr_callbacks_t callbacks);
extern void free_conmgr(void);

/*
 * Request kernel provide auth credentials for connection
 * IN con connection to query creds
 * RET SLURM_SUCCESS or error (ESLURM_NOT_SUPPORTED if connection can't query)
 */
extern int conmgr_get_fd_auth_creds(conmgr_fd_t *con, uid_t *cred_uid,
				    gid_t *cred_gid, pid_t *cred_pid);

/*
 * instruct connection manager to process fd (async)
 * IN type connection type for fd
 * IN input_fd file descriptor to have conmgr take ownership and read from
 * IN output_fd file descriptor to have conmgr take ownership and write to
 * IN events call backs on events of fd
 * IN addr socket address (if known or NULL) (will always xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_process_fd(conmgr_con_type_t type, int input_fd,
			     int output_fd, const conmgr_events_t events,
			     const slurm_addr_t *addr, socklen_t addrlen,
			     void *arg);

/*
 * instruct connection manager to listen to fd (async)
 * IN type connection type for fd
 * IN fd file descriptor to have conmgr take ownership of
 * IN events call backs on events of fd
 * IN addr socket listen address (will not xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_process_fd_listen(int fd, conmgr_con_type_t type,
				    const conmgr_events_t events,
				    const slurm_addr_t *addr,
				    socklen_t addrlen, void *arg);
/*
 * instruct connection manager to listen to unix socket fd (async)
 * IN type connection type for fd
 * IN fd file descriptor to have conmgr take ownership of
 * IN events call backs on events of fd
 * IN addr socket listen address (will not xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * IN path path to unix socket on filesystem
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_process_fd_unix_listen(conmgr_con_type_t type, int fd,
					 const conmgr_events_t events,
					 const slurm_addr_t *addr,
					 socklen_t addrlen, const char *path,
					 void *arg);

/*
 * Write binary data to connection (from callback).
 * NOTE: type=CON_TYPE_RAW only
 * IN con connection manager connection struct
 * IN buffer pointer to buffer
 * IN bytes number of bytes in buffer to write
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_queue_write_fd(conmgr_fd_t *con, const void *buffer,
				 const size_t bytes);

/*
 * Write packed msg to connection (from callback).
 * NOTE: type=CON_TYPE_RPC only
 * IN con conmgr connection ptr
 * IN msg message to send
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_queue_write_msg(conmgr_fd_t *con, slurm_msg_t *msg);

/*
 * Request soft close of connection
 * IN con connection manager connection struct
 * RET SLURM_SUCCESS or error
 */
extern void conmgr_queue_close_fd(conmgr_fd_t *con);

/*
 * create sockets based on requested SOCKET_LISTEN
 * to accepted connections.
 * IN type connection type for fd
 * IN  hostports list_t* of cstrings to listen on.
 *	format: host:port
 * IN events function callback on events
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_create_sockets(conmgr_con_type_t type, list_t *hostports,
				 conmgr_events_t events, void *arg);

/*
 * Run connection manager main loop for until all processing is done
 * IN blocking - Run in blocking mode or in background as new thread
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_run(bool blocking);

/*
 * Notify conmgr to shutdown
 */
extern void conmgr_request_shutdown(void);

/*
 * Hold starting any new work and event handling.
 * 	Will cause any active conmgr_run(true) to return.
 * 	Any running work will not be interrupted.
 * 	Quiesce state cleared by next call of conmgr_run().
 * IN wait - wait for all running work to finish before returning
 */
extern void conmgr_quiesce(bool wait);

/*
 * Add work to call on signal received
 * IN signal - Signal number to watch
 * IN func - function pointer to run work
 * 	Will be run after signal is received and not in signal handler itself.
 * IN type - type of work
 * IN arg - arg to hand to function pointer
 * IN tag - tag used in logging this function
 * NOTE: never add a thread that will never return or conmgr_run() will never
 * return either.
 */
extern void conmgr_add_signal_work(int signal, conmgr_work_func_t func,
				   void *arg, const char *tag);

/*
 * Add work for connection manager
 * NOTE: only call from within an conmgr_events_t callback
 * IN con - connection to assign work or NULL for non-connection related work
 * IN func - function pointer to run work
 * IN type - type of work
 * IN arg - arg to hand to function pointer
 * IN tag - tag used in logging this function
 * NOTE: never add a thread that will never return or conmgr_run() will never
 * return either.
 */
extern void conmgr_add_work(conmgr_fd_t *con, conmgr_work_func_t func,
			    conmgr_work_type_t type, void *arg,
			    const char *tag);

/*
 * Add time delayed work for connection manager
 * IN con - connection to assign work or NULL for non-connection related work
 * IN func - function pointer to run work
 * IN type - type of work
 * IN arg - arg to hand to function pointer
 * IN tag - tag used in logging this function
 * NOTE: never add a thread that will never return or conmgr_run() will never
 * return either.
 */
extern void conmgr_add_delayed_work(conmgr_fd_t *con,
				    conmgr_work_func_t func, time_t seconds,
				    long nanoseconds, void *arg,
				    const char *tag);

/*
 * Get number of threads used by conmgr
 */
extern int conmgr_get_thread_count(void);

/*
 * Control if conmgr will exit on any error
 */
extern void conmgr_set_exit_on_error(bool exit_on_error);
extern bool conmgr_get_exit_on_error(void);

/*
 * Get last error code from conmgr
 */
extern int conmgr_get_error(void);

/*
 * Get assigned connection name - stays same for life of connection
 */
extern const char *conmgr_fd_get_name(const conmgr_fd_t *con);

/*
 * Get pointer to data held by input buffer
 * IN con - connection to query data
 * IN data_ptr - pointer to set with pointer to buffer data or NULL
 * IN len_ptr - number of bytes in buffer
 */
extern void conmgr_fd_get_in_buffer(const conmgr_fd_t *con,
				    const void **data_ptr, size_t *bytes_ptr);

/*
 * Get shadow buffer to data held by input buffer
 * IN con - connection to query data
 * RET new shadow buffer
 * 	shadow buffer must FREE_NULL_BUFFER()ed before end of callback function
 * 	completes. Shadow buffer's data pointer will be invalid once the
 * 	callbackup function completes.
 * 	conmgr_fd_mark_consumed_in_buffer() must be called if any register
 * 	if any data is processed from buffer.
 */
extern buf_t *conmgr_fd_shadow_in_buffer(const conmgr_fd_t *con);

/*
 * Mark bytes in input buffer as have been consumed
 * WARNING: will xassert() if bytes > size of buffer
 */
extern void conmgr_fd_mark_consumed_in_buffer(const conmgr_fd_t *con,
					      size_t bytes);

/*
 * Transfer incoming data into a buf_t
 * IN con - connection to query data
 * IN buffer_ptr - pointer to buf_t to add/set with incoming data
 * 	if *buffer_ptr is NULL, then a new buf_t will be created and caller must
 * 	call FREE_NULL_BUFFER()
 * 	if buffer->size is too small, then buffer will be grown to sufficent
 * 	size.
 * 	buffer->processed will not be changed
 * 	if buffer->head is NULL, it will be set with a new xmalloc() buffer.
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_fd_xfer_in_buffer(const conmgr_fd_t *con,
				    buf_t **buffer_ptr);

/*
 * Transfer outgoing data to connection from buf_t
 * NOTE: type=CON_TYPE_RAW only
 * IN con - connection manager connection struct
 * IN output - pointer to buffer to write to connection
 * 	output->{head,size} pointer may be changed
 * 	output->processed will be set to 0 on success
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_fd_xfer_out_buffer(conmgr_fd_t *con, buf_t *output);

/*
 * Get input file descriptor
 * WARNING: fd is only valid until return from callback and may close due to
 * other calls against connection
 * RET -1 if closed or valid number
 */
extern int conmgr_fd_get_input_fd(conmgr_fd_t *con);

/*
 * Get output file descriptor
 * WARNING: fd is only valid until return from callback and may close due to
 * other calls against connection
 * RET -1 if closed or valid number
 */
extern int conmgr_fd_get_output_fd(conmgr_fd_t *con);

typedef struct {
	/* this is a socket fd */
	bool is_socket;
	/* path to unix socket if it is one */
	char *unix_socket;
	/* this is a listen only socket */
	bool is_listen;
	/* has this connection received read EOF */
	bool read_eof;
	/* has this connection called on_connection */
	bool is_connected;
} conmgr_fd_status_t;

extern conmgr_fd_status_t conmgr_fd_get_status(conmgr_fd_t *con);

#endif /* _CONMGR_H */
