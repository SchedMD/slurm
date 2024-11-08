/*****************************************************************************\
 *  conmgr.h - declarations for connection manager
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

#ifndef _CONMGR_H
#define _CONMGR_H

#include <netdb.h>
#include <sys/socket.h>

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"

#define CONMGR_THREAD_COUNT_DEFAULT 10
#define CONMGR_THREAD_COUNT_MIN 2
#define CONMGR_THREAD_COUNT_MAX 1024

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
	 * Call back for new listener for setup
	 *
	 * IN con - connection handler
	 * IN arg - arg ptr handed to fd processing functions
	 * RET arg ptr to hand to events
	 */
	void *(*on_listen_connect)(conmgr_fd_t *con, void *arg);

	/*
	 * Call back when listener ended.
	 * Called once per connection right before connection is xfree()ed.
	 *
	 * IN con - connection handler
	 * IN arg - ptr to be handed return of on_connection().
	 * 	Ownership of arg pointer returned to caller as it will not be
	 * 	used anymore.
	 */
	void (*on_listen_finish)(conmgr_fd_t *con, void *arg);

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
	 * IN arg ptr to be handed return of on_connection() callback.
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
	 * IN unpack_rc return code from unpacking RPC
	 * WARNING: always check unpack_rc and msg->auth_ids_set before
	 *	considering msg to be valid!
	 * IN arg ptr to be handed return of on_connection() callback.
	 * RET SLURM_SUCCESS or error to kill connection
	 */
	int (*on_msg)(conmgr_fd_t *con, slurm_msg_t *msg, int unpack_rc,
		      void *arg);

	/*
	 * Call back when connection ended.
	 * Called once per connection right before connection is xfree()ed.
	 *
	 * IN con - connection handler
	 * IN arg - ptr to be handed return of on_connection().
	 * 	Ownership of arg pointer returned to caller as it will not be
	 * 	used anymore.
	 */
	void (*on_finish)(conmgr_fd_t *con, void *arg);

	/*
	 * Call back when read timeout occurs
	 * Called once per timeout triggering or being detected.
	 *
	 * If on_read_timeout=NULL is treated same as returning
	 *	SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT
	 *
	 * IN con - connection handler
	 * IN arg ptr to be handed return of on_connection() callback.
	 * RET SLURM_SUCCESS to wait timeout again or error to kill connection
	 */
	int (*on_read_timeout)(conmgr_fd_t *con, void *arg);

	/*
	 * Call back when write timeout occurs
	 * Called once per timeout triggering or being detected.
	 *
	 * If on_read_timeout=NULL is treated same as returning
	 *	SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT
	 *
	 * IN con - connection handler
	 * IN arg ptr to be handed return of on_connection() callback.
	 * RET SLURM_SUCCESS to wait timeout again or error to kill connection
	 */
	int (*on_write_timeout)(conmgr_fd_t *con, void *arg);

	/*
	 * Call back when connect timeout occurs
	 * Called once per timeout triggering or being detected.
	 *
	 * If on_read_timeout=NULL is treated same as returning
	 *	SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT
	 *
	 * IN con - connection handler
	 * IN arg ptr to be handed return of on_connection() callback.
	 * RET SLURM_SUCCESS to wait timeout again or error to kill connection
	 */
	int (*on_connect_timeout)(conmgr_fd_t *con, void *arg);
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
	CONMGR_WORK_SCHED_INVALID = 0,
	/* work scheduled by FIFO */
	CONMGR_WORK_SCHED_FIFO = SLURM_BIT(0),
} conmgr_work_sched_t;

/* RET caller must xfree() */
extern char *conmgr_work_sched_string(conmgr_work_sched_t type);

typedef enum {
	CONMGR_WORK_DEP_INVALID = 0,
	/* specify work has no dependencies */
	CONMGR_WORK_DEP_NONE = SLURM_BIT(1),
	/* call once all connection writes complete */
	CONMGR_WORK_DEP_CON_WRITE_COMPLETE = SLURM_BIT(2),
	/* call once time delay completes */
	CONMGR_WORK_DEP_TIME_DELAY = SLURM_BIT(3),
	/* call every time signal is received */
	CONMGR_WORK_DEP_SIGNAL = SLURM_BIT(4),
} conmgr_work_depend_t;

/* RET caller must xfree() */
extern char *conmgr_work_depend_string(conmgr_work_depend_t type);

/*
 * Calculate the absolute start time from delayed time
 * IN delay_seconds - Number of seconds to delay from current time
 * IN delay_nanoseconds - Number of additional nanoseconds to delay from
 *	delay_seconds
 */
extern timespec_t conmgr_calc_work_time_delay(time_t delay_seconds,
					      long delay_nanoseconds);

typedef struct {
	/* ptr to relevant connection (or NULL) */
	conmgr_fd_t *con;
	/*
	 * Work status
	 * Note: Always check status for CONMGR_WORK_STATUS_CANCELLED to know
	 *	when a shutdown has been triggered and to just cleanup instead
	 *	of doing the work.
	 */
	conmgr_work_status_t status;
} conmgr_callback_args_t;

/*
 * Prototype for all conmgr callbacks
 * IN conmgr_args - Args relaying conmgr callback state
 * IN func_arg - arbitrary pointer passed directly
 */
typedef void (*conmgr_work_func_t)(conmgr_callback_args_t conmgr_args,
				   void *arg);

typedef struct {
	conmgr_work_func_t func;
	void *arg;
	const char *func_name;
} conmgr_callback_t;

typedef struct {
	/* Bitflags to control how work is priority scheduled */
	conmgr_work_sched_t schedule_type;

	/* Bitflags to activate work Dependencies */
	conmgr_work_depend_t depend_type;

	/* set if (depend_type & CONMGR_WORK_DEP_TIME_DELAY) */
	timespec_t time_begin;

	/* set if (depend_type & CONMGR_WORK_DEP_SIGNAL) */
	int on_signal_number;
} conmgr_work_control_t;

/*
 * conmgr can handle RPC or raw connections
 */
typedef enum {
	CON_TYPE_INVALID = 0,
	CON_TYPE_NONE, /* Initialized state */
	CON_TYPE_RAW, /* handle data unprocessed to/from */
	CON_TYPE_RPC, /* handle data Slurm RPCs to/from */
	CON_TYPE_MAX /* place holder - do not use */
} conmgr_con_type_t;
extern const char *conmgr_con_type_string(conmgr_con_type_t type);

/* WARNING: flags overlap with con_flags_t */
typedef enum {
	CON_FLAG_NONE = 0,
	/*
	 * Copy entire message into slurm_msg_t after parsing.
	 * Allocate buffer and copy entire message into msg->buffer.
	 * Sets SLURM_MSG_KEEP_BUFFER in msg->flags.
	 * Only applies to CON_TYPE_RPC connections.
	 */
	CON_FLAG_RPC_KEEP_BUFFER = SLURM_BIT(9),
	/*
	 * Connection will not be poll()'ed for changes and all pending work
	 * will remained queued until unset. New work can still be added. If the
	 * connection is requested to be closed, then the flag will be removed
	 * automatically.
	 */
	CON_FLAG_QUIESCE = SLURM_BIT(10),
	/* output_fd is a socket with TCP_NODELAY set */
	CON_FLAG_TCP_NODELAY = SLURM_BIT(14),
	/*
	 * Trigger on_write_timeout() callback when write of at least 1 byte
	 * takes longer than conf_write_timeout when connection is otherwise
	 * idle.
	 */
	CON_FLAG_WATCH_WRITE_TIMEOUT = SLURM_BIT(15),
	/*
	 * Trigger on_read_timeout() callback when read of at least 1 byte takes
	 * longer than conf_read_timeout when connection is otherwise idle.
	 */
	CON_FLAG_WATCH_READ_TIMEOUT = SLURM_BIT(16),
	/*
	 * Trigger on_connect_timeout() callback when read of at least 1 byte
	 * takes longer than timeout when connection is otherwise idle.
	 */
	CON_FLAG_WATCH_CONNECT_TIMEOUT = SLURM_BIT(17),
} conmgr_con_flags_t;

/*
 * Initialise global connection manager
 * IN thread_count - number of thread workers to run
 * IN max_connections - max number of connections or 0 for default
 * IN callbacks - struct containing function pointers
 * WARNING: Never queue as work for conmgr or call from work run by conmgr.
 */
extern void conmgr_init(int thread_count, int max_connections,
			conmgr_callbacks_t callbacks);
/* WARNING: Never queue as work for conmgr or call from work run by conmgr. */
extern void conmgr_fini(void);

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
 * IN flags bit-or'ed flags to apply to connection
 * IN addr socket address (if known or NULL) (will always xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_process_fd(conmgr_con_type_t type, int input_fd,
			     int output_fd, const conmgr_events_t *events,
			     conmgr_con_flags_t flags,
			     const slurm_addr_t *addr, socklen_t addrlen,
			     void *arg);

/*
 * instruct connection manager to listen to fd (async)
 * IN type connection type for fd
 * IN fd file descriptor to have conmgr take ownership of
 * IN events call backs on events of fd
 * IN flags bit-or'ed flags to apply to connection
 * IN addr socket listen address (will not xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_process_fd_listen(int fd, conmgr_con_type_t type,
				    const conmgr_events_t *events,
				    conmgr_con_flags_t flags, void *arg);

/*
 * Queue up work to receive new connection (file descriptor via socket)
 * IN src - source connection to receive file descriptor
 * IN type connection type for fd
 * IN events call backs on events of fd
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_queue_receive_fd(conmgr_fd_t *src, conmgr_con_type_t type,
				   const conmgr_events_t *events, void *arg);

/*
 * Queue send file descriptor over connection
 * IN con - connection to send file descriptor over
 * IN fd - file descriptor to send (must not be managed by conmgr)
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_queue_send_fd(conmgr_fd_t *con, int fd);

/*
 * Write binary data to connection (from callback).
 * NOTE: type=CON_TYPE_RAW only
 * IN con connection manager connection struct
 * IN buffer pointer to buffer
 * IN bytes number of bytes in buffer to write
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_queue_write_data(conmgr_fd_t *con, const void *buffer,
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
 * Change connection mode
 * IN con - conmgr connection ptr
 * IN type - change connection to new type
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_fd_change_mode(conmgr_fd_t *con, conmgr_con_type_t type);

/*
 * Create listening socket
 * IN type - connection type for new sockets
 * IN listen_on - cstrings to listen on:
 *	formats:
 *		host:port
 *		unix:/path/to/socket
 * IN events - ptr to function callback on events
 * IN arg - arbitrary ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_create_listen_socket(conmgr_con_type_t type,
					const char *listen_on,
					const conmgr_events_t *events,
					void *arg);

/*
 * Create listening sockets from list of host:port pairs
 * IN type - connection type for new sockets
 * IN hostports - list_t* of cstrings to listen on:
 *	formats:
 *		host:port
 *		unix:/path/to/socket
 * IN events - ptr to function callback on events
 * IN arg - arbitrary ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_create_listen_sockets(conmgr_con_type_t type,
					list_t *hostports,
					const conmgr_events_t *events,
					void *arg);

/*
 * Instruct conmgr to create new socket and connect to addr
 * IN type - connection for new socket
 * IN addr - destination address to connect() socket
 * IN addrlen - sizeof(*addr)
 * IN events - ptr to function callback on events
 * IN arg - arbitrary ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_create_connect_socket(conmgr_con_type_t type,
					slurm_addr_t *addr, socklen_t addrlen,
					const conmgr_events_t *events,
					void *arg);

/*
 * Run connection manager main loop for until shutdown
 * IN blocking - Run in blocking mode or in background as new thread
 * RET SLURM_SUCCESS or error
 * WARNING: Never call from work function (directly or indirectly)
 */
extern int conmgr_run(bool blocking);

/*
 * Notify conmgr to shutdown
 */
extern void conmgr_request_shutdown(void);

/*
 * Add work to run
 * IN con - connection to run work or NULL
 * IN callback - callback function details
 * IN control - work controls to determine when work is run
 * IN caller - __func__ from caller for logging
 * NOTE: never add a thread that will never return or conmgr_run() will never
 * return either.
 */
extern void conmgr_add_work(conmgr_fd_t *con, conmgr_callback_t callback,
			    conmgr_work_control_t control, const char *caller);

/*
 * Add work to run
 * IN _func - function pointer to run work
 * IN func_arg - arg to hand to function pointer
 * NOTE: never add a thread that will never return or conmgr_run() will never
 * return either.
 */
#define conmgr_add_work_fifo(_func, func_arg) \
	conmgr_add_work(NULL, (conmgr_callback_t) { \
			.func = _func, \
			.arg = func_arg, \
			.func_name = #_func, \
		}, (conmgr_work_control_t) { \
			.depend_type = CONMGR_WORK_DEP_NONE, \
			.schedule_type = CONMGR_WORK_SCHED_FIFO, \
		}, __func__)

/*
 * Add work to run for connection
 * IN con - connection to assign work
 * IN _func - function pointer to run work
 * IN func_arg - arg to hand to function pointer
 * NOTE: never add a thread that will never return or conmgr_run() will never
 * return either.
 */
#define conmgr_add_work_con_fifo(con, _func, func_arg) \
	conmgr_add_work(con, (conmgr_callback_t) { \
			.func = _func, \
			.arg = func_arg, \
			.func_name = #_func, \
		}, (conmgr_work_control_t) { \
			.depend_type = CONMGR_WORK_DEP_NONE, \
			.schedule_type = CONMGR_WORK_SCHED_FIFO, \
		}, __func__)

/*
 * Add work to run when all pendings writes are complete for connection
 * IN con - connection to assign work
 * IN _func - function pointer to run work
 * IN func_arg - arg to hand to function pointer
 * IN delay_second - number of seconds to delay running work
 * IN delay_nanosecond - number of nanoseconds to delay running work
 * NOTE: never add a thread that will never return or conmgr_run() will never
 * return either.
 */
#define conmgr_add_work_con_write_complete_fifo(con, _func, func_arg) \
	conmgr_add_work(con, (conmgr_callback_t) { \
			.func = _func, \
			.arg = func_arg, \
			.func_name = #_func, \
		}, (conmgr_work_control_t) { \
			.depend_type = \
				CONMGR_WORK_DEP_CON_WRITE_COMPLETE, \
			.schedule_type = CONMGR_WORK_SCHED_FIFO, \
		}, __func__)

/*
 * Add time delayed work
 * IN _func - function pointer to run work
 * IN func_arg - arg to hand to function pointer
 * IN delay_second - number of seconds to delay running work
 * IN delay_nanosecond - number of nanoseconds to delay running work
 * NOTE: never add a thread that will never return or conmgr_run() will never
 * return either.
 */
#define conmgr_add_work_delayed_fifo(_func, func_arg, delay_seconds, \
				     delay_nanoseconds) \
	conmgr_add_work(NULL, (conmgr_callback_t) { \
			.func = _func, \
			.arg = func_arg, \
			.func_name = #_func, \
		}, (conmgr_work_control_t) { \
			.depend_type = CONMGR_WORK_DEP_TIME_DELAY, \
			.time_begin = \
				conmgr_calc_work_time_delay(delay_seconds, \
							    delay_nanoseconds),\
			.schedule_type = CONMGR_WORK_SCHED_FIFO, \
		}, __func__)

/*
 * Add time delayed work for connection
 * IN con - connection to assign work
 * IN _func - function pointer to run work
 * IN func_arg - arg to hand to function pointer
 * IN delay_second - number of seconds to delay running work
 * IN delay_nanosecond - number of nanoseconds to delay running work
 * NOTE: never add a thread that will never return or conmgr_run() will never
 * return either.
 */
#define conmgr_add_work_con_delayed_fifo(con, _func, func_arg, delay_seconds, \
					 delay_nanoseconds) \
	conmgr_add_work(con, (conmgr_callback_t) { \
			.func = _func, \
			.arg = func_arg, \
			.func_name = #_func, \
		}, (conmgr_work_control_t) { \
			.depend_type = CONMGR_WORK_DEP_TIME_DELAY, \
			.time_begin = \
				conmgr_calc_work_time_delay(delay_seconds, \
							    delay_nanoseconds),\
			.schedule_type = CONMGR_WORK_SCHED_FIFO, \
		}, __func__)

/*
 * Add work to call on signal received
 * IN signal - Signal number to watch
 * IN func - function pointer to run work
 * 	Will be run after signal is received and not in signal handler itself.
 * IN type - type of work
 * IN arg - arg to hand to function pointer
 * NOTE: never add a thread that will never return or conmgr_run() will never
 * return either.
 */
#define conmgr_add_work_signal(signal_number, _func, func_arg) \
	conmgr_add_work(NULL, (conmgr_callback_t) { \
			.func = _func, \
			.arg = func_arg, \
			.func_name = #_func, \
		}, (conmgr_work_control_t) { \
			.depend_type = CONMGR_WORK_DEP_SIGNAL, \
			.on_signal_number = signal_number, \
			.schedule_type = CONMGR_WORK_SCHED_FIFO, \
		}, __func__)

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
 * 	if buffer->size is too small, then buffer will be grown to sufficient
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
	/* has this connection been fully established with remote */
	bool is_connected;
} conmgr_fd_status_t;

extern conmgr_fd_status_t conmgr_fd_get_status(conmgr_fd_t *con);

/*
 * Check to see if the con->output_fd is currently open and can (in theory)
 * accept more write()s.
 *
 * WARNING: This check is inherently a race condition and should only be used to
 * verify a connection is still valid before an expensive operation. The
 * connection output could close or fail at anytime after this check which will
 * be relayed via callbacks on the connection.
 *
 * RET true if output is still open or false if otherwise
 */
extern bool conmgr_fd_is_output_open(conmgr_fd_t *con);

/*
 * Check if conmgr is enabled in this process
 * RET true if conmgr is enabled or running in this process
 */
extern bool conmgr_enabled(void);

/*
 * Callback function for when connection file descriptors extracted
 * IN conmgr_args - Args relaying conmgr callback state
 * IN input_fd - input file descriptor or -1 - Ownership is transferred.
 * IN output_fd - output file descriptor or -1 - Ownership is transferred.
 */
typedef void (*conmgr_extract_fd_func_t)(conmgr_callback_args_t conmgr_args,
					 int input_fd, int output_fd,
					 void *arg);

/*
 * Queue up extraction of file descriptors from a connection.
 * NOTE: Extraction may need to wait for any running work to be completed on
 *	connection.
 * WARNING: Only to be used for conversion to conmgr where file descriptors must
 *	be controlled by non-conmgr code.
 *
 * IN con - connection to extract file descriptors from
 * IN func - callback function when extraction is complete to take ownership of
 *	file descriptors
 * IN func_name - XSTRINGIFY(func) for logging
 * IN func_arg - arbitrary pointer passed directly to func
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_queue_extract_con_fd(conmgr_fd_t *con,
				       conmgr_extract_fd_func_t func,
				       const char *func_name,
				       void *func_arg);

#define CONMGR_PARAM_POLL_ONLY "CONMGR_USE_POLL"
#define CONMGR_PARAM_THREADS "CONMGR_THREADS="
#define CONMGR_PARAM_MAX_CONN "CONMGR_MAX_CONNECTIONS="
#define CONMGR_PARAM_WAIT_WRITE_DELAY "CONMGR_WAIT_WRITE_DELAY="
#define CONMGR_PARAM_READ_TIMEOUT "CONMGR_READ_TIMEOUT="
#define CONMGR_PARAM_WRITE_TIMEOUT "CONMGR_WRITE_TIMEOUT="
#define CONMGR_PARAM_CONNECT_TIMEOUT "CONMGR_CONNECT_TIMEOUT="

/*
 * Set configuration parameters to be applied when conmgr_init() is called.
 * IN params - CSV string with parameters for conmgr.
 *	See CONMGR_PARAM_* for possible parameters.
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_set_params(const char *params);

/*
 * Mark connection as quiesced
 * @see CON_FLAG_QUIESCE for details
 * IN con - connection to set CON_FLAG_QUIESCE flag
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_quiesce_fd(conmgr_fd_t *con);

/*
 * Remove queisced flag from connection
 * @see CON_FLAG_QUIESCE for details
 * IN con - connection to unset CON_FLAG_QUIESCE flag
 * RET SLURM_SUCCESS or error
 */
extern int conmgr_unquiesce_fd(conmgr_fd_t *con);

/*
 * Block until conmgr is quiesced
 * IN caller - __func__ from caller for logging
 */
extern void conmgr_quiesce(const char *caller);

/*
 * Unquiesce conmgr
 * IN caller - __func__ from caller for logging
 */
extern void conmgr_unquiesce(const char *caller);

/*
 * Connection reference.
 * Opaque struct - do not access directly.
 * While exists: the conmgr_fd_t ptr will remain valid.
 */
typedef struct conmgr_fd_ref_s conmgr_fd_ref_t;

/*
 * Create new reference to conmgr connection
 * Will ensure that conmgr_fd_t will remain valid until released.
 * IN con - connection to create reference
 * RET ptr to new reference (must be released by conmgr_fd_free_ref())
 */
extern conmgr_fd_ref_t *conmgr_fd_new_ref(conmgr_fd_t *con);
/*
 * Release reference to conmgr connection
 * WARNING: Connection may not exist after this called
 * IN ref_ptr - ptr to reference to release (will be set to NULL)
 */
extern void conmgr_fd_free_ref(conmgr_fd_ref_t **ref_ptr);
/*
 * Get conmgr_fd_t pointer from reference
 */
extern conmgr_fd_t *conmgr_fd_get_ref(conmgr_fd_ref_t *ref);

/* Get connection name from reference */
#define conmgr_ref_get_name(ref) \
	conmgr_fd_get_name(conmgr_fd_get_ref(ref))

#endif /* _CONMGR_H */
