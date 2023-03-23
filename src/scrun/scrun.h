/*****************************************************************************\
 *  scrun.h - Slurm OCI runtime headers
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
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

#ifndef _SCRUN_H
#define _SCRUN_H

#include "src/common/conmgr.h"
#include "src/common/data.h"
#include "src/common/oci_config.h"

#ifndef OCI_VERSION
extern const char *OCI_VERSION;
#endif

typedef struct {
	int magic; /* STATE_MAGIC */

	/*
	 * Only use lock_state() and unlock_state() instead of directly locking
	 * this field.
	 */
	pthread_rwlock_t lock; /* lock must be used for access */
	bool needs_lock;
	int locked;

	/* every field required by OCI runtime-spec v1.0.2 state query. */
	char *oci_version;
	char *id; /* container-id */
	container_state_msg_status_t status; /* current status */
	pid_t pid; /* pid of anchor process */
	char *bundle; /* path to OCI container bundle */
	char *orig_bundle; /* original bundle path before lua override */
	list_t *annotations; /* List of config_key_pair_t */
	/* Internal state tracking */
	char *console_socket;
	bool requested_terminal; /* /process/terminal/ value */
	int ptm; /* pseudoterminal master (PTM) */
	int pts; /* pseudoterminal master (PTS) */
	struct winsize tty_size; /* size props of calling terminal */
	char *pid_file; /* full path for pid file */
	int pid_file_fd; /* file descriptor for pid file */
	bool existing_allocation; /* running an existing job allocation */
	uint32_t jobid; /* assigned jobID */
	bool job_completed; /* has job been completed */
	bool staged_out; /* stage out done */
	bool staged_in; /* true if stage_in() called */
	int srun_pid; /* pid of the srun running job */
	int srun_rc; /* exit_code from srun exit (only valid if srun_exited) */
	bool srun_exited; /* true if we reaped the srun pid already */
	char *anchor_socket; /* path to anchor msg socket */
	char *spool_dir; /* container specific work space */
	char **job_env; /* env to hand to srun */
	char *config_file; /* path to config.json */
	uint32_t user_id; /* user job is running as */
	uint32_t group_id; /* group job is running as */
	char *root_dir; /* path to root directory to hold working files */
	char *root_path; /* contents of /root/path where container files are */
	char *orig_root_path; /* original root path before lua override */
	int requested_signal; /* signal to send or zero if not requested */
	bool force; /* user requested force argument */
	data_t *config; /* container's config.json */
	list_t *start_requests; /* list of (blocking_req_t *) */
	list_t *delete_requests; /* list of (blocking_req_t *) */
	con_mgr_fd_t *startup_con; /* conmgr connection for the SIGCHILD handler */
} state_t;

extern state_t state;
extern oci_conf_t *oci_conf;

/* logging options */
extern log_options_t log_opt;
extern log_facility_t log_fac;
extern char *log_file;
extern char *log_format;
extern void update_logging(void);

extern void check_state(void);
extern void init_state(void);
extern void destroy_state(void);

#define read_lock_state()                                                    \
	do {                                                                 \
		slurm_rwlock_rdlock(&state.lock);                            \
		xassert(state.needs_lock);                                   \
		xassert(state.locked >= 0);                                  \
		state.locked++;                                              \
		check_state();                                               \
		debug3("%s: taking state read lock needs_lock=%c locked=%d", \
		       __func__, (state.needs_lock ? 'T' : 'F'),             \
		       state.locked);                                        \
	} while (false)

#define write_lock_state()                                                    \
	do {                                                                  \
		slurm_rwlock_wrlock(&state.lock);                             \
		xassert(state.needs_lock);                                    \
		xassert(state.locked >= 0);                                   \
		state.locked++;                                               \
		check_state();                                                \
		debug3("%s: taking state write lock needs_lock=%c locked=%d", \
		       __func__, (state.needs_lock ? 'T' : 'F'),              \
		       state.locked);                                         \
	} while (false)

#define unlock_state()                                                       \
	do {                                                                 \
		debug3("%s: unlock state needs_lock=%c locked=%d",           \
		       __func__, (state.needs_lock ? 'T' : 'F'),             \
		       state.locked);                                        \
		check_state();                                               \
		xassert(state.needs_lock);                                   \
		xassert(state.locked > 0);                                   \
		state.locked--;                                              \
		slurm_rwlock_unlock(&state.lock);                            \
	} while (false)

extern int command_create(void);
extern int command_start(void);
extern int command_state(void);
extern int command_kill(void);
extern int command_delete(void);
extern int command_version(void);

/*
 * fork and start running anchor process
 */
extern int spawn_anchor(void);

/*
 * send rpc to anchor and get reply msg
 * WARNING: will not free original msg
 * WARNING: must cleanup returned msg with slurm_free_msg();
 * IN conn_fd - use this if not -1 and set if not NULL
 * reply message
 */
extern int send_rpc(slurm_msg_t *msg, slurm_msg_t **ptr_resp, const char *id,
		    int *conn_fd);

/*
 * Attempt to connect to anchor and get current state
 * RET - SLURM_SUCCESS or error
 */
extern int get_anchor_state(void);

/*
 * Request allocation for job
 * IN arg - ptr to conmgr
 */
extern void get_allocation(con_mgr_t *mgr, con_mgr_fd_t *con,
			   con_mgr_work_type_t type,
			   con_mgr_work_status_t status, const char *tag,
			   void *arg);

/* callback after allocation success */
extern void on_allocation(con_mgr_t *mgr, con_mgr_fd_t *con,
			  con_mgr_work_type_t type,
			  con_mgr_work_status_t status, const char *tag,
			  void *arg);

/*
 * Stop and (eventually) cleanup anchor
 * IN status - error code for why we are stopping or SLURM_SUCCESS
 */
extern void stop_anchor(int status);

/* run srun container command against job */
extern void exec_srun_container();

/* convert data array of arguments into argv[] for execv() */
extern char **create_argv(data_t *args);

/*
 * Init the staging - aka load the lua script
 */
extern void init_lua(void);

/*
 * release lua script
 */
extern void destroy_lua(void);

/*
 * Call lua stage in script
 * RET SLURM_SUCCESS or error
 */
extern int stage_in(void);

/*
 * Call lua stage out script
 * RET SLURM_SUCCESS or error
 */
extern int stage_out(void);

extern void change_status_funcname(container_state_msg_status_t status,
				   bool force, const char *src, bool locked);
#define change_status(status) \
	change_status_funcname(status, false, __func__, false)
#define change_status_locked(status) \
	change_status_funcname(status, false, __func__, true)
/* ignore reverse status change */
#define change_status_force(status) \
	change_status_funcname(status, true, __func__, false)

#endif
