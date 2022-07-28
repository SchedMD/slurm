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
 * Call back for new connection for setup
 *
 * IN fd file descriptor of new connection
 * RET arg ptr to hand to
 */
typedef void *(*con_mgr_on_new_connection_t)(con_mgr_fd_t *con);

/*
 * Call back when there is data ready in "in" buffer
 * This may be called several times in the same connection.
 *
 * IN con connection handler
 * IN arg ptr to be handed return of con_mgr_on_new_connection_t().
 * RET SLURM_SUCCESS or error to kill connection
 */
typedef int (*con_mgr_on_connection_data_t)(con_mgr_fd_t *con, void *arg);

/*
 * Call back when connection ended.
 * Called once per connection.
 *
 * IN arg ptr to be handed return of con_mgr_on_new_connection_t().
 * 	must free arg as required.
 */
typedef void (*con_mgr_on_connection_finish)(void *arg);

/*
 * Struct of call backs to call on events
 * of a given connection.
 */
typedef struct {
	con_mgr_on_new_connection_t on_connection;
	con_mgr_on_connection_data_t on_data;
	con_mgr_on_connection_finish on_finish;
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

/*
 * Connection tracking structure
 *
 * Opaque struct - do not access directly
 */
struct con_mgr_fd_s {
	int magic;
	/* input and output may be a different fd to inet mode */
	int input_fd;
	int output_fd;
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
	/* buffer holding out going to be written data */
	buf_t *out;
	/* this is a socket fd */
	bool is_socket;
	/* path to unix socket if it is one */
	char *unix_socket;
	/* this is a listen only socket */
	bool is_listen;
	/* poll has indicated write is possible */
	bool can_write;
	/* poll has indicated read is possible */
	bool can_read;
	/* has this connection received read EOF */
	bool read_eof;
	/* has this connection called on_connection */
	bool is_connected;
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
	 *
	 */
	bool has_work;
	/*
	 * list of non-IO work pending
	 * type: wrap_work_arg_t
	 */
	List work;
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
	List connections;
	/*
	 * list of connections that only listen
	 * type: con_mgr_fd_t
	 * */
	List listen;
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
	/* Signal PIPE to catch SIGINT */
	int sigint_fd[2];
	/* Caller requests finish on error */
	bool exit_on_error;
	/* First observed error */
	int error;

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
 * IN input_fd file descriptor to have mgr take ownership and read from
 * IN output_fd file descriptor to have mgr take ownership and write to
 * IN events call backs on events of fd
 * IN addr socket address (if known or NULL) (will always xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_process_fd(con_mgr_t *mgr, int input_fd, int output_fd,
			      const con_mgr_events_t events,
			      const slurm_addr_t *addr, socklen_t addrlen);

/*
 * instruct connection manager to listen to fd (async)
 * IN mgr connection manager to update
 * IN fd file descriptor to have mgr take ownership of
 * IN events call backs on events of fd
 * IN addr socket listen address (will not xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_process_fd_listen(con_mgr_t *mgr, int fd,
				     const con_mgr_events_t events,
				     const slurm_addr_t *addr,
				     socklen_t addrlen);

/*
 * instruct connection manager to listen to unix socket fd (async)
 * IN mgr connection manager to update
 * IN fd file descriptor to have mgr take ownership of
 * IN events call backs on events of fd
 * IN addr socket listen address (will not xfree())
 * IN addrlen sizeof addr or 0 if addr is NULL
 * IN path path to unix socket on filesystem
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_process_fd_unix_listen(con_mgr_t *mgr, int fd,
					  const con_mgr_events_t events,
					  const slurm_addr_t *addr,
					  socklen_t addrlen, const char *path);

/*
 * Write binary data to connection (from callback).
 * NOTE: only call from within a callback
 * IN con connection manager connection struct
 * IN buffer pointer to buffer
 * IN bytes number of bytes in buffer to write
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_queue_write_fd(con_mgr_fd_t *con, const void *buffer,
				  const size_t bytes);

/*
 * Request soft close of connection
 * NOTE: only call from within a callback
 * IN con connection manager connection struct
 * RET SLURM_SUCCESS or error
 */
extern void con_mgr_queue_close_fd(con_mgr_fd_t *con);

/*
 * create sockets based on requested SOCKET_LISTEN
 * IN  mgr assigned connection manager
 * to accepted connections.
 * IN  hostports List of cstrings to listen on.
 *	format: host:port
 * IN events function callback on events
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_create_sockets(con_mgr_t *mgr, List hostports,
				  con_mgr_events_t events);

/*
 * Run connection manager main loop for until all processing is done
 * IN mgr assigned connection mgr to run
 * RET SLURM_SUCCESS or error
 */
extern int con_mgr_run(con_mgr_t *mgr);

/*
 * Notify conmgr to shutdown
 * IN con connection manager connection struct
 */
extern void con_mgr_request_shutdown(con_mgr_fd_t *con);

#endif /* SLURMRESTD_CONMGR_H */
