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

#ifndef SLURMRESTD_CONMGR_H
#define SLURMRESTD_CONMGR_H

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

typedef struct con_mgr_fd_s con_mgr_fd_t;
typedef struct con_mgr_s con_mgr_t;

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
	void *(*on_connection)(con_mgr_fd_t *con, void *arg);

	/*
	 * Call back when there is data ready in "in" buffer
	 * This may be called several times in the same connection.
	 * Only called when type = CON_TYPE_RAW.
	 *
	 * IN con connection handler
	 * IN arg ptr to be handed return of con_mgr_on_new_connection_t().
	 * RET SLURM_SUCCESS or error to kill connection
	 */
	int (*on_data)(con_mgr_fd_t *con, void *arg);

	/*
	 * Call back when there is new RPC msg ready
	 * This may be called several times in the same connection.
	 * Only called when type = CON_TYPE_RPC.
	 *
	 * IN con connection handler
	 * IN msg ptr to new msg (call must slurm_free_msg())
	 * IN arg ptr to be handed return of con_mgr_on_new_connection_t().
	 * RET SLURM_SUCCESS or error to kill connection
	 */
	int (*on_msg)(con_mgr_fd_t *con, slurm_msg_t *msg, void *arg);

	/*
	 * Call back when connection ended.
	 * Called once per connection.
	 *
	 * IN arg ptr to be handed return of con_mgr_on_new_connection_t().
	 * 	must free arg as required.
	 */
	void (*on_finish)(void *arg);
} con_mgr_events_t;

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
} con_mgr_callbacks_t;

typedef enum {
	CONMGR_WORK_STATUS_INVALID = 0,
	CONMGR_WORK_STATUS_PENDING,
	CONMGR_WORK_STATUS_RUN,
	CONMGR_WORK_STATUS_CANCELLED,
	CONMGR_WORK_STATUS_MAX /* place holder */
} con_mgr_work_status_t;

extern const char *con_mgr_work_status_string(con_mgr_work_status_t status);

typedef enum {
	CONMGR_WORK_TYPE_INVALID = 0,
	CONMGR_WORK_TYPE_CONNECTION_FIFO, /* connection specific work ordered by FIFO */
	CONMGR_WORK_TYPE_CONNECTION_WRITE_COMPLETE, /* call once all connection writes complete then FIFO */
	CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO, /* call once time delay completes then FIFO */
	CONMGR_WORK_TYPE_FIFO, /* non-connection work ordered by FIFO */
	CONMGR_WORK_TYPE_TIME_DELAY_FIFO, /* call once time delay completes then FIFO */
	CONMGR_WORK_TYPE_MAX /* place holder */
} con_mgr_work_type_t;

extern const char *con_mgr_work_type_string(con_mgr_work_type_t type);

/*
 * Prototype for all conmgr callbacks
 * IN mgr - ptr to owning conmgr
 * IN con - ptr to relavent connection (or NULL)
 * IN type - work type
 * IN status - work status
 * IN tag - logging tag for work
 * IN arg - arbitrary pointer
 */
typedef void (*con_mgr_work_func_t)(con_mgr_t *mgr, con_mgr_fd_t *con,
				    con_mgr_work_type_t type,
				    con_mgr_work_status_t status,
				    const char *tag, void *arg);

/*
 * conmgr can handle RPC or raw connections
 */
typedef enum {
	CON_TYPE_INVALID = 0,
	CON_TYPE_RAW, /* handle data unprocessed to/from */
	CON_TYPE_RPC, /* handle data Slurm RPCs to/from */
	CON_TYPE_MAX /* place holder - do not use */
} con_mgr_con_type_t;

/*
 * Connection tracking structure
 *
 * Opaque struct - do not access directly
 */
struct con_mgr_fd_s {
	int magic;
	con_mgr_con_type_t type;
	/* input and output may be a different fd to inet mode */
	int input_fd;
	int output_fd;
	/* arg handed to on_connection */
	void *new_arg;
	/* arg returned from on_connection */
	void *arg;
	/* name of connection for logging */
	char *name;
	/* call backs on events */
	con_mgr_events_t events;
	/* buffer holding incoming already read data */
	buf_t *in;
	/* has on_data already tried to parse data */
	bool on_data_tried;
	/* list of buf_t to write (in order) */
	list_t *deferred_out;
	/* buffer holding out going to be written data */
	buf_t *out;
	/* this is a socket fd */
	bool is_socket;
	/* path to unix socket if it is one */
	char *unix_socket;
	/* this is a listen only socket */
	bool is_listen;
	/* connection is waiting for on_finish() to complete */
	bool wait_on_finish;
	/* poll has indicated write is possible */
	bool can_write;
	/* poll has indicated read is possible */
	bool can_read;
	/* has this connection received read EOF */
	bool read_eof;
	/* has this connection called on_connection */
	bool is_connected;
	/* incoming msg length - CON_TYPE_RPC only */
	uint32_t msglen;
	/*
	 * has pending work:
	 * there must only be 1 thread at a time working on this connection
	 * directly.
	 *
	 * While this is true, the following must not be changed except by the
	 * callback thread:
	 * 	in
	 * 	out
	 * 	name (will never change for life of connection)
	 * 	mgr (will not be moved)
	 * 	con (will not be moved)
	 * 	arg
	 *	on_data_tried
	 *	msglen
	 *
	 */
	bool work_active;
	/*
	 * list of non-IO work pending
	 * type: wrap_work_arg_t
	 */
	list_t *work;
	/*
	 * list of non-IO work pending out buffer being full sent
	 * type: wrap_work_arg_t
	 */
	list_t *write_complete_work;
	/* owning connection manager */
	con_mgr_t *mgr;
};

/*
 * Opaque struct - do not access directly
 */
struct con_mgr_s {
	int magic;
	/*
	 * list of all connections to process
	 * type: con_mgr_fd_t
	 */
	list_t *connections;
	/*
	 * list of connections that only listen
	 * type: con_mgr_fd_t
	 * */
	list_t *listen;
	/*
	 * list of complete connections pending cleanup
	 * type: con_mgr_fd_t
	 * */
	list_t *complete;
	/*
	 * True if there is a thread for listen queued or running
	 */
	bool listen_active;
	/*
	 * True if there is a thread for poll queued or running
	 */
	bool poll_active;
	/*
	 * Is trying to shutdown?
	 */
	bool shutdown;
	/* thread pool */
	workq_t *workq;
	/* will inspect connections (not listeners */
	bool inspecting;
	/* if an event signal has already been sent */
	int event_signaled;
	/* Event PIPE used to break out of poll */
	int event_fd[2];
	/* Signal PIPE to catch POSIX signals */
	int signal_fd[2];
	/* track when there is a pending signal to read */
	bool signaled;
	/* Caller requests finish on error */
	bool exit_on_error;
	/* First observed error */
	int error;
	/* list of work_t */
	list_t *delayed_work;
	/* last time clock was queried */
	struct timespec last_time;
	/* monotonic timer */
	timer_t timer;
	/* list of deferred_func_t */
	list_t *deferred_funcs;

	/* functions to handle host/port parsing */
	con_mgr_callbacks_t callbacks;

	pthread_mutex_t mutex;
	/* called after events or changes to wake up _watch */
	pthread_cond_t cond;
};

/*
 * create and init a connection manager
 * only call once!
 * IN thread_count - number of threads to create
 * IN callbacks - struct containing function pointers
 * RET SLURM_SUCCESS or error
 */
extern con_mgr_t *init_con_mgr(int thread_count, con_mgr_callbacks_t callbacks);
extern void free_con_mgr(con_mgr_t *mgr);

/*
 * instruct connection manager to process fd (async)
 * IN mgr connection manager to update
 * IN type connection type for fd
 * IN input_fd file descriptor to have mgr take ownership and read from
 * IN output_fd file descriptor to have mgr take ownership and write to
 * IN events call backs on events of fd
 * IN addr socket address (if known or NULL) (will always xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_process_fd(con_mgr_t *mgr, con_mgr_con_type_t type,
			      int input_fd, int output_fd,
			      const con_mgr_events_t events,
			      const slurm_addr_t *addr, socklen_t addrlen,
			      void *arg);

/*
 * instruct connection manager to listen to fd (async)
 * IN mgr connection manager to update
 * IN type connection type for fd
 * IN fd file descriptor to have mgr take ownership of
 * IN events call backs on events of fd
 * IN addr socket listen address (will not xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_process_fd_listen(con_mgr_t *mgr, int fd,
				     con_mgr_con_type_t type,
				     const con_mgr_events_t events,
				     const slurm_addr_t *addr,
				     socklen_t addrlen, void *arg);

/*
 * instruct connection manager to listen to unix socket fd (async)
 * IN mgr connection manager to update
 * IN type connection type for fd
 * IN fd file descriptor to have mgr take ownership of
 * IN events call backs on events of fd
 * IN addr socket listen address (will not xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * IN path path to unix socket on filesystem
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_process_fd_unix_listen(con_mgr_t *mgr,
					  con_mgr_con_type_t type, int fd,
					  const con_mgr_events_t events,
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
extern int con_mgr_queue_write_fd(con_mgr_fd_t *con, const void *buffer,
				  const size_t bytes);

/*
 * Write packed msg to connection (from callback).
 * NOTE: type=CON_TYPE_RPC only
 * IN con conmgr connection ptr
 * IN msg message to send
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_queue_write_msg(con_mgr_fd_t *con, slurm_msg_t *msg);

/*
 * Request soft close of connection
 * IN con connection manager connection struct
 * RET SLURM_SUCCESS or error
 */
extern void con_mgr_queue_close_fd(con_mgr_fd_t *con);

/*
 * create sockets based on requested SOCKET_LISTEN
 * IN  mgr assigned connection manager
 * to accepted connections.
 * IN type connection type for fd
 * IN  hostports list_t* of cstrings to listen on.
 *	format: host:port
 * IN events function callback on events
 * IN arg ptr handed to on_connection callback
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_create_sockets(con_mgr_t *mgr, con_mgr_con_type_t type,
				  list_t *hostports, con_mgr_events_t events,
				  void *arg);

/*
 * Run connection manager main loop for until all processing is done
 * IN mgr assigned connection mgr to run
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_run(con_mgr_t *mgr);

/*
 * Notify conmgr to shutdown
 * IN mgr connection manager ptr
 */
extern void con_mgr_request_shutdown(con_mgr_t *mgr);

/*
 * Add work for connection manager
 * NOTE: only call from within an con_mgr_events_t callback
 * IN mgr - manager to assign work
 * IN con - connection to assign work or NULL for non-connection related work
 * IN func - function pointer to run work
 * IN type - type of work
 * IN arg - arg to hand to function pointer
 * IN tag - tag used in logging this function
 * NOTE: never add a thread that will never return or con_mgr_run() will never
 * return either.
 */
extern void con_mgr_add_work(con_mgr_t *mgr, con_mgr_fd_t *con,
			     con_mgr_work_func_t func, con_mgr_work_type_t type,
			     void *arg, const char *tag);

/*
 * Add time delayed work for connection manager
 * IN mgr - manager to assign work
 * IN con - connection to assign work or NULL for non-connection related work
 * IN func - function pointer to run work
 * IN type - type of work
 * IN arg - arg to hand to function pointer
 * IN tag - tag used in logging this function
 * NOTE: never add a thread that will never return or con_mgr_run() will never
 * return either.
 */
extern void con_mgr_add_delayed_work(con_mgr_t *mgr, con_mgr_fd_t *con,
				     con_mgr_work_func_t func, time_t seconds,
				     long nanoseconds, void *arg,
				     const char *tag);

#endif /* SLURMRESTD_CONMGR_H */
