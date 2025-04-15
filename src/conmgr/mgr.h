/*****************************************************************************\
 *  mgr.h - Internal declarations for connection manager
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

/*
 * Note: Only src/conmgr/(*).c should include this header. Everything else should
 * only include src/conmgr/conmgr.h for the exported functions and structs.
 */

#ifndef _CONMGR_MGR_H
#define _CONMGR_MGR_H

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#include "slurm/slurm.h"

#include "src/common/pack.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/events.h"
#include "src/conmgr/polling.h"

/* Default buffer to 1 page */
#define BUFFER_START_SIZE 4096

typedef struct {
#define MAGIC_EXTRACT_FD 0xabf8e2a3
	int magic; /* MAGIC_EXTRACT_FD */
	int input_fd;
	int output_fd;
	conmgr_extract_fd_func_t func;
	const char *func_name;
	void *func_arg;
} extract_fd_t;

typedef struct {
#define MAGIC_WORK 0xD231444A
	int magic; /* MAGIC_WORK */
	conmgr_work_status_t status;
	conmgr_fd_t *con;
	conmgr_callback_t callback;
	conmgr_work_control_t control;
} work_t;

/*
 * WARNING: flags overlap with with conmgr_con_flags_t with con_flags_t being
 * used to avoid exporting conmgr private flags outside of conmgr.
 */
typedef enum {
	FLAG_NONE = CON_FLAG_NONE,
	/* has on_data() already tried to parse data */
	FLAG_ON_DATA_TRIED = SLURM_BIT(0),
	/* connection is a socket file descriptor */
	FLAG_IS_SOCKET = SLURM_BIT(1),
	/* connection is a listening only socket */
	FLAG_IS_LISTEN = SLURM_BIT(2),
	/* connection is waiting for on_finish() to complete */
	FLAG_WAIT_ON_FINISH = SLURM_BIT(3),
	/* poll has indicated write is possible */
	FLAG_CAN_WRITE = SLURM_BIT(4),
	/* poll has indicated read is possible */
	FLAG_CAN_READ = SLURM_BIT(5),
	/* connection received read EOF for input_fd */
	FLAG_READ_EOF = SLURM_BIT(6),
	/* is connection established and enqueued on_connection() */
	FLAG_IS_CONNECTED = SLURM_BIT(7),
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
	 *	FLAG_ON_DATA_TRIED
	 *
	 */
	FLAG_WORK_ACTIVE = SLURM_BIT(8),
	/* @see CON_FLAG_RPC_KEEP_BUFFER */
	FLAG_RPC_KEEP_BUFFER = CON_FLAG_RPC_KEEP_BUFFER,
	/* @see CON_FLAG_QUIESCE */
	FLAG_QUIESCE = CON_FLAG_QUIESCE,
	/* True if fd_get_buffered_output_bytes() work on output_fd */
	FLAG_CAN_QUERY_OUTPUT_BUFFER = SLURM_BIT(11),
	/* connection is a pipe() */
	FLAG_IS_FIFO = SLURM_BIT(12),
	/* connection is a character special device */
	FLAG_IS_CHR = SLURM_BIT(13),
	/* @see CON_FLAG_TCP_NODELAY */
	FLAG_TCP_NODELAY = CON_FLAG_TCP_NODELAY,
	/* @see CON_FLAG_WATCH_WRITE_TIMEOUT */
	FLAG_WATCH_WRITE_TIMEOUT = CON_FLAG_WATCH_WRITE_TIMEOUT,
	/* @see CON_FLAG_WATCH_READ_TIMEOUT */
	FLAG_WATCH_READ_TIMEOUT = CON_FLAG_WATCH_READ_TIMEOUT,
	/* @see CON_FLAG_WATCH_CONNECT_TIMEOUT */
	FLAG_WATCH_CONNECT_TIMEOUT = CON_FLAG_WATCH_CONNECT_TIMEOUT,
} con_flags_t;

/* Mask over flags that track connection state */
#define FLAGS_MASK_STATE \
	( FLAG_ON_DATA_TRIED | FLAG_IS_SOCKET | FLAG_IS_LISTEN | \
	  FLAG_WAIT_ON_FINISH | FLAG_CAN_WRITE | FLAG_CAN_READ | \
	  FLAG_READ_EOF | FLAG_IS_CONNECTED | FLAG_WORK_ACTIVE | \
	  FLAG_CAN_QUERY_OUTPUT_BUFFER | FLAG_IS_FIFO | FLAG_IS_CHR )

/* con_flags_t macro helpers to test, set, and unset flags */
#define con_flag(con, flag) ((con)->flags & (flag))
#define con_set_flag(con, flag) ((con)->flags |= (flag))
#define con_unset_flag(con, flag) ((con)->flags &= ~(flag))
#define con_assign_flag(con, flag, value) \
	((con)->flags = ((con)->flags & ~(flag)) | ((!!value) * (flag)))


/*
 * Convert flags to printable string
 * IN flags - connection flags
 * RET string of flags (must xfree())
 */
extern char *con_flags_string(const con_flags_t flags);

typedef struct conmgr_fd_ref_s {
#define MAGIC_CON_MGR_FD_REF 0xA2F4B4EF
	int magic; /* MAGIC_CON_MGR_FD_REF */
	conmgr_fd_t *con;
} conmgr_fd_ref_t;

/*
 * Connection tracking structure
 */
struct conmgr_fd_s {
#define MAGIC_CON_MGR_FD 0xD23444EF
	int magic; /* MAGIC_CON_MGR_FD */
	conmgr_con_type_t type;
	/* input and output may be a different fd to inet mode */
	int input_fd;
	int output_fd;
	/* arg handed to on_connection */
	void *new_arg;
	/* arg returned from on_connection */
	void *arg;
	/* name of connection for logging */
	char *name;
	/* address for connection */
	slurm_addr_t address;
	/* call backs for events */
	const conmgr_events_t *events;
	/* buffer holding incoming already read data */
	buf_t *in;
	/* timestamp when last read() got >0 bytes or when connect() called */
	timespec_t last_read;
	/* list of buf_t to write (in order) */
	list_t *out;
	/* timestamp when last write() wrote >0 bytes */
	timespec_t last_write;
	/* socket maximum segment size (MSS) or NO_VAL if not known */
	int mss;

	/* queued extraction of input_fd/output_fd request */
	extract_fd_t *extract;

	/*
	 * Current active polling (if any).
	 * Only set by con_set_polling()
	 */
	pollctl_fd_type_t polling_input_fd;
	pollctl_fd_type_t polling_output_fd;
	/*
	 * list of non-IO work pending
	 * type: work_t*
	 */
	list_t *work;
	/*
	 * list of non-IO work pending out buffer being full sent
	 * type: work_t*
	 */
	list_t *write_complete_work;

	/* Flags set for connection */
	con_flags_t flags;

	/* Number of active references of this connection */
	int refs;
};

typedef struct {
#define MAGIC_WORKER 0xD2342412
	int magic; /* MAGIC_WORKER */
	/* thread id of worker */
	pthread_t tid;
	/* unique id for tracking */
	int id;
} worker_t;

/*
 * Global instance of conmgr
 */
typedef struct {
	/* Configured value for max connections */
	int conf_max_connections;

	/*
	 * Configured number of seconds to wait for recheck of output_fd for
	 * write_complete work
	 */
	uint32_t conf_delay_write_complete;

	/* Time delay requires to trigger a read timeout */
	timespec_t conf_read_timeout;
	/* Time delay requires to trigger a write timeout */
	timespec_t conf_write_timeout;
	/* Time delay requires to trigger a connect timeout */
	timespec_t conf_connect_timeout;

	/* Max number of connections at any one time allowed */
	int max_connections;
	/*
	 * list of all connections to process
	 * type: conmgr_fd_t
	 */
	list_t *connections;
	/*
	 * list of connections that only listen
	 * type: conmgr_fd_t
	 */
	list_t *listen_conns;
	/*
	 * list of complete connections pending cleanup
	 * type: conmgr_fd_t
	 */
	list_t *complete_conns;
	/*
	 * True after conmgr_init() is called, false after conmgr_fini() is
	 * called.
	 */
	bool initialized;
	/* One time per process tasks initialized */
	bool one_time_initialized;
	/*
	 * Thread id of thread running watch()
	 */
	pthread_t watch_thread;

	/*
	 * Max abs time watch can sleep due to pending timeout
	 */
	timespec_t watch_max_sleep;

	/*
	 * True if there is a thread for poll queued or running
	 */
	bool poll_active;
	/*
	 * Is trying to shutdown?
	 */
	bool shutdown_requested;
	/* will inspect connections (not listeners */
	bool inspecting;
	/* True if watch() is only waiting on work to complete */
	bool waiting_on_work;

	/* Caller requests finish on error */
	bool exit_on_error;
	/* First observed error */
	int error;
	/* list of work_t */
	list_t *delayed_work;
	/* list of work_t* */
	list_t *work;

	/* functions to handle host/port parsing */
	conmgr_callbacks_t callbacks;

	pthread_mutex_t mutex;

	struct {
		/* Configured value of threads */
		int conf_threads;

		/* list of worker_t */
		list_t *workers;

		/* track simple stats for logging */
		int active;
		int total;

		/*
		 * track shutdown of workers after other work is done or there
		 * may be no workers to do the work
		 */
		bool shutdown_requested;

		/* number of threads */
		int threads;
	} workers;

	/* Global quiesce state */
	struct {
		/* Has a thread requested conmgr to quiesce? */
		bool requested;
		/* Has conmgr quiesced */
		bool active;
		/* Event to broadcast when conmgr enters quiesced state */
		event_signal_t on_start_quiesced;
		/* Event to broadcast when conmgr exits quiesced state */
		event_signal_t on_stop_quiesced;
	} quiesce;

	event_signal_t watch_sleep;
	event_signal_t watch_return;
	event_signal_t worker_sleep;
	event_signal_t worker_return;
} conmgr_t;

#define CONMGR_DEFAULT \
	(conmgr_t) {\
		.conf_max_connections = -1,\
		.mutex = PTHREAD_MUTEX_INITIALIZER,\
		.max_connections = -1,\
		.error = SLURM_SUCCESS,\
		.shutdown_requested = true,\
		.workers.conf_threads = -1,\
		.quiesce = { \
			.on_start_quiesced = \
				EVENT_INITIALIZER("START_QUIESCED"), \
			.on_stop_quiesced = \
				EVENT_INITIALIZER("STOP_QUIESCED"), \
		}, \
		.watch_sleep = EVENT_INITIALIZER("WATCH_SLEEP"), \
		.watch_return = EVENT_INITIALIZER("WATCH_RETURN"), \
		.worker_sleep = EVENT_INITIALIZER("WORKER_SLEEP"), \
		.worker_return = EVENT_INITIALIZER("WORKER_RETURN"), \
	}

extern conmgr_t mgr;

/*
 * Create new work to run
 * IN locked - true if calling thread has mgr.mutex already locked
 * IN callback - callback function details
 * IN control - controls on when work is run
 * IN depend_mask - Apply mask against control.depend_type.
 * 	Mask is intended for work that generates new work (such as signal work)
 * 	to make it relatively clean to remove a now fullfilled dependency.
 * 	Ignored if depend_mask=0.
 * IN caller - __func__ from caller
 */
extern void add_work(bool locked, conmgr_fd_t *con, conmgr_callback_t callback,
		     conmgr_work_control_t control,
		     conmgr_work_depend_t depend_mask, const char *caller);

#define add_work_fifo(locked, _func, func_arg) \
	add_work(locked, NULL, (conmgr_callback_t) { \
			.func = _func, \
			.arg = func_arg, \
			.func_name = #_func, \
		}, (conmgr_work_control_t) { \
			.depend_type = CONMGR_WORK_DEP_NONE, \
			.schedule_type = CONMGR_WORK_SCHED_FIFO, \
		}, 0, __func__)

#define add_work_con_fifo(locked, con, _func, func_arg) \
	add_work(locked, con, (conmgr_callback_t) { \
			.func = _func, \
			.arg = func_arg, \
			.func_name = #_func, \
		}, (conmgr_work_control_t) { \
			.depend_type = CONMGR_WORK_DEP_NONE, \
			.schedule_type = CONMGR_WORK_SCHED_FIFO, \
		}, 0, __func__)

#define add_work_con_delayed_fifo(locked, con, _func, func_arg, delay_seconds, \
				  delay_nanoseconds) \
	add_work(locked, con, (conmgr_callback_t) { \
			.func = _func, \
			.arg = func_arg, \
			.func_name = #_func, \
		}, (conmgr_work_control_t) { \
			.depend_type = CONMGR_WORK_DEP_TIME_DELAY, \
			.schedule_type = CONMGR_WORK_SCHED_FIFO, \
			.time_begin = \
				conmgr_calc_work_time_delay(delay_seconds, \
							    delay_nanoseconds),\
		}, 0, __func__)

extern void work_mask_depend(work_t *work, conmgr_work_depend_t depend_mask);
extern void handle_work(bool locked, work_t *work);

/*
 * Poll all connections and handle any events
 */
extern void *watch(void *arg);
extern void *watch_thread(void *arg);

/*
 * Wait for _watch() to finish
 * WARNING: caller must not hold mgr.mutex
 */
extern void wait_for_watch(void);

/*
 * Stop reading from connection but write out the remaining buffer and finish
 * any queued work
 */
extern void close_con(bool locked, conmgr_fd_t *con);

/*
 * Stop writting to connection and drop remaining outbound buffer(s)
 */
extern void close_con_output(bool locked, conmgr_fd_t *con);

/*
 * Wrap close_con() as work
 */
extern void work_close_con(conmgr_callback_args_t conmgr_args, void *arg);

/*
 * Close connection due to poll error
 *
 * Note: Removal of fd from poll() will already be handled before calling this
 * Note: Caller must lock mgr.mutex
 * IN con - connection that owns fd that had error
 * IN fd - file descriptor that had an error (probably from poll)
 * IN rc - error if known
 */
extern void con_close_on_poll_error(conmgr_fd_t *con, int fd);

/*
 * Set connection polling state
 * NOTE: Caller must hold mgr.mutex lock.
 * IN type - Set type of polling for connection or PCTL_TYPE_INVALID to disable
 *	polling this connection
 * IN caller - __func__ from caller
 */
extern void con_set_polling(conmgr_fd_t *con, pollctl_fd_type_t type,
			    const char *caller);

extern void handle_write(conmgr_callback_args_t conmgr_args, void *arg);

extern void handle_read(conmgr_callback_args_t conmgr_args, void *arg);

/*
 * Resize con->in if needed
 * IN arg - (ssize_t) number of bytes need in con->in
 */
extern void resize_input_buffer(conmgr_callback_args_t conmgr_args, void *arg);

extern void wrap_on_data(conmgr_callback_args_t conmgr_args, void *arg);

/*
 * Add new connection from file descriptor(s)
 *
 * IN type - Initial connection type
 * IN source - connection that created this fd (listeners only)
 * IN input_fd - file descriptor for incoming data (or -1)
 * IN output_fd - file descriptor for outgoing data (or -1)
 * IN events - callbacks for this connections
 * IN flags - flags to apply to connection
 * IN addr - address for this connection or NULL
 * IN addrlen - number of bytes in *addr or 0 if addr==NULL
 * IN is_listen - True if this is a listening socket
 * IN unix_socket_path - Named Unix Socket path in filesystem or NULL
 * IN arg - arbitrary pointer to hand to events
 * RET SLURM_SUCCESS or errror
 */
extern int add_connection(conmgr_con_type_t type,
			  conmgr_fd_t *source, int input_fd,
			  int output_fd,
			  const conmgr_events_t *events,
			  conmgr_con_flags_t flags,
			  const slurm_addr_t *addr,
			  socklen_t addrlen, bool is_listen,
			  const char *unix_socket_path, void *arg);

extern void close_all_connections(void);

extern int on_rpc_connection_data(conmgr_fd_t *con, void *arg);

/*
 * Find connection by a given file descriptor
 * NOTE: Caller must hold mgr.mutex lock
 * IN fd - file descriptor to use to search
 * RET ptr or NULL if not found
 */
extern conmgr_fd_t *con_find_by_fd(int fd);

/*
 * Wrap work requested to notify mgr when that work is complete
 */
extern void wrap_work(work_t *work);

/*
 * Wait for all workers to finish their work
 * WARNING: caller must hold mgr.mutex
 * WARNING: never call from work or call will never return
 */
extern void wait_for_workers_idle(const char *caller);

/*
 * Notify all worker thread to shutdown.
 * Wait until all work and workers have completed their work (and exited).
 * Note: Caller MUST hold conmgr lock
 */
extern void workers_shutdown(void);

/*
 * Initialize worker threads
 * IN count - number of workers to add
 * Note: Caller must hold conmgr lock
 */
extern void workers_init(int count);

/*
 * Release worker threads
 * Will stop all workers (eventually).
 * Note: Caller must hold conmgr lock
 */
extern void workers_fini(void);

/*
 * Change con->type
 * NOTE: caller must hold mgr.mutex lock
 * IN con - connection to change
 * IN type - type to change to
 * RET SLURM_SUCESS or error
 */
extern int fd_change_mode(conmgr_fd_t *con, conmgr_con_type_t type);

/*
 * Wraps on_connection() callback
 */
extern void wrap_on_connection(conmgr_callback_args_t conmgr_args, void *arg);

/*
 * Extract connection file descriptors
 */
extern void extract_con_fd(conmgr_fd_t *con);

#endif /* _CONMGR_MGR_H */
