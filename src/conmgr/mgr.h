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
#include "src/conmgr/poll.h"

/* Default buffer to 1 page */
#define BUFFER_START_SIZE 4096

typedef struct {
#define MAGIC_WORK 0xD231444A
	int magic; /* MAGIC_WORK */
	conmgr_fd_t *con;
	conmgr_work_func_t func;
	void *arg;
	const char *tag;
	conmgr_work_status_t status;
	conmgr_work_type_t type;
	struct {
		/* absolute time when to work can begin */
		time_t seconds;
		long nanoseconds; /* offset from seconds */
	} begin;
} work_t;

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
	/* call backs on events */
	conmgr_events_t events;
	/* buffer holding incoming already read data */
	buf_t *in;
	/* has on_data already tried to parse data */
	bool on_data_tried;
	/* list of buf_t to write (in order) */
	list_t *out;
	/* socket maximum segment size (MSS) or NO_VAL if not known */
	int mss;
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
	/*
	 * Current active polling (if any).
	 * Only set by con_set_polling()
	 */
	pollctl_fd_type_t polling_input_fd;
	pollctl_fd_type_t polling_output_fd;
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
	 * type: work_t*
	 */
	list_t *work;
	/*
	 * list of non-IO work pending out buffer being full sent
	 * type: work_t*
	 */
	list_t *write_complete_work;
};

typedef struct {
#define MAGIC_WORKER 0xD2342412
	int magic; /* MAGIC_WORKER */
	/* thread id of worker */
	pthread_t tid;
	/* unique id for tracking */
	int id;
} worker_t;

typedef struct {
#define MAGIC_WATCH_REQUEST 0xD204f412
	int magic; /* MAGIC_WATCH_REQUEST */
	bool running_on_worker; /* true if request issues via work */
	bool blocking; /* true to block if another thread already watching */
} watch_request_t;

/*
 * Global instance of conmgr
 */
typedef struct {
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
	/*
	 * True if _watch() is running
	 */
	bool watching;
	/*
	 * Number of watch() instances running on a worker thread (as work)
	 */
	int watch_on_worker;
	/*
	 * True if there is a thread for poll queued or running
	 */
	bool poll_active;
	/*
	 * Is trying to shutdown?
	 */
	bool shutdown_requested;
	/*
	 * Is mgr currently quiesced?
	 * Defers all new work to while true
	 */
	bool quiesced;
	/* at fork handler installed */
	bool at_fork_installed;
	/* will inspect connections (not listeners */
	bool inspecting;

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
	/* list of work_t* */
	list_t *work;

	/* functions to handle host/port parsing */
	conmgr_callbacks_t callbacks;

	pthread_mutex_t mutex;

	struct {
		/* list of worker_t */
		list_t *workers;

		/* track simple stats for logging */
		int active;
		int total;

		/* number of threads */
		int threads;
	} workers;

	event_signal_t watch_sleep;
	event_signal_t watch_return;
	event_signal_t worker_sleep;
	event_signal_t worker_return;
} conmgr_t;

#define CONMGR_DEFAULT \
	(conmgr_t) {\
		.mutex = PTHREAD_MUTEX_INITIALIZER,\
		.max_connections = -1,\
		.error = SLURM_SUCCESS,\
		.quiesced = true,\
		.shutdown_requested = true,\
		.watch_sleep = EVENT_INITIALIZER("WATCH_SLEEP"), \
		.watch_return = EVENT_INITIALIZER("WATCH_RETURN"), \
		.worker_sleep = EVENT_INITIALIZER("WORKER_SLEEP"), \
		.worker_return = EVENT_INITIALIZER("WORKER_RETURN"), \
	}

extern conmgr_t mgr;

extern void add_work(bool locked, conmgr_fd_t *con, conmgr_work_func_t func,
		     conmgr_work_type_t type, void *arg, const char *tag);
extern void cancel_delayed_work(void);
extern void free_delayed_work(void);
extern void update_timer(bool locked);
extern void on_signal_alarm(conmgr_fd_t *con, conmgr_work_type_t type,
			    conmgr_work_status_t status, const char *tag,
			    void *arg);
extern void handle_work(bool locked, work_t *work);
extern void update_last_time(bool locked);

/*
 * Poll all connections and handle any events
 * IN arg - cast to bool blocking - non-zero if blocking
 */
extern void watch(conmgr_fd_t *con, conmgr_work_type_t type,
		  conmgr_work_status_t status, const char *tag, void *arg);

/*
 * Wait for _watch() to finish
 *
 * WARNING: caller must NOT hold mgr.mutex
 */
extern void wait_for_watch(void);

/*
 * Stop reading from connection but write out the remaining buffer and finish
 * any queued work
 */
extern void close_con(bool locked, conmgr_fd_t *con);

/*
 * Close connection due to poll error
 *
 * Note: Removal of fd from poll() will already be handled before calling this
 * IN locked - if mgr.mutex is already locked or not
 * IN con - connection that owns fd that had error
 * IN fd - file descriptor that had an error (probably from poll)
 * IN rc - error if known
 */
extern void con_close_on_poll_error(bool locked, conmgr_fd_t *con, int fd);

/*
 * Set connection polling state
 * IN locked - true if caller hold mgr.mutex lock
 * IN type - Set type of polling for connection or PCTL_TYPE_INVALID to disable
 *	polling this connection
 * IN caller - __func__ from caller
 */
extern void con_set_polling(bool locked, conmgr_fd_t *con,
			    pollctl_fd_type_t type, const char *caller);

extern void handle_write(conmgr_fd_t *con, conmgr_work_type_t type,
			 conmgr_work_status_t status, const char *tag,
			 void *arg);

extern void handle_read(conmgr_fd_t *con, conmgr_work_type_t type,
			conmgr_work_status_t status, const char *tag,
			void *arg);

extern void wrap_on_data(conmgr_fd_t *con, conmgr_work_type_t type,
			 conmgr_work_status_t status, const char *tag,
			 void *arg);

extern conmgr_fd_t *add_connection(conmgr_con_type_t type,
				   conmgr_fd_t *source, int input_fd,
				   int output_fd,
				   const conmgr_events_t events,
				   const slurm_addr_t *addr,
				   socklen_t addrlen, bool is_listen,
				   const char *unix_socket_path, void *arg);

extern void close_all_connections(void);

extern int on_rpc_connection_data(conmgr_fd_t *con, void *arg);
extern void wrap_con_work(work_t *work, conmgr_fd_t *con);

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
 * Wait until all work and workers have completed their work (and exited)
 * Note: Caller must NOT hold conmgr lock
 * Note: mgr.shutdown_requested must be true
 */
extern void workers_wait_complete(void);

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

#endif /* _CONMGR_MGR_H */
