/*****************************************************************************\
 *  namespace.h - job namespace plugin interface.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Morris Jette
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

#ifndef _INTERFACES_NAMESPACE_H
#define _INTERFACES_NAMESPACE_H

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct ns_conf {
	bool auto_basepath;
	char *basepath;
	char *clonensscript;
	char *clonensflags_str;
	char *clonensepilog;
	uint32_t clonensscript_wait;
	uint32_t clonensflags;
	uint32_t clonensepilog_wait;
	char *dirs;
	char *initscript;
	bool shared;
	char *usernsscript;
} ns_conf_t;

typedef struct ns_node_conf_t {
	hostlist_t *nodes;
	ns_conf_t *ns_conf;
	bool set_auto_basepath;
	bool set_clonensscript_wait;
	bool set_clonensepilog_wait;
	bool set_shared;
} ns_node_conf_t;

typedef struct {
	ns_conf_t *defaults;
	list_t *node_confs; /* list of slurm_ns_node_conf_t* */
} ns_full_conf_t;

/*
 * Initialize the job namespace plugin.
 *
 * RET - slurm error code
 */
extern int namespace_g_init(void);

/*
 * Terminate the job namespace plugin, free memory.
 *
 * RET - slurm error code
 */
extern int namespace_g_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/* Add the calling process's pid to the specified job's namespace. */
extern int namespace_g_join(slurm_step_id_t *step_id, uid_t uid,
			    bool step_create);

/*
 * Allow external processes to join the job namespace
 * (eg. via PAM)
 */
extern int namespace_g_join_external(slurm_step_id_t *step_id, list_t *fd_map);

/* Restore namespace information */
extern int namespace_g_restore(char *dir_name, bool recover);

/* Create a namespace for the specified job, actions run in slurmstepd */
extern int namespace_g_stepd_create(stepd_step_rec_t *step);

/* Delete the namespace for the specified job, actions run in slurmstepd */
extern int namespace_g_stepd_delete(slurm_step_id_t *step_id);

/* Send namespace config to slurmstepd on the provided file descriptor */
extern int namespace_g_send_stepd(int fd);

/* Receive namespace config from slurmd on the provided file descriptor */
extern int namespace_g_recv_stepd(int fd);

/* Checks whether bpf syscalls can be done from the namespace */
extern bool namespace_g_can_bpf(stepd_step_rec_t *step);

/* Setups the bpf token */
extern int namespace_g_setup_bpf_token(stepd_step_rec_t *step);

extern void slurm_free_ns_conf_members(ns_conf_t *ns_conf);
extern void slurm_free_ns_conf(ns_conf_t *ns_conf);
extern void slurm_free_ns_full_conf(ns_full_conf_t *ns_full_conf);
extern void slurm_free_ns_node_conf(ns_node_conf_t *ns_node_conf);

#endif
