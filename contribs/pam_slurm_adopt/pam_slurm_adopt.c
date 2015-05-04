/*****************************************************************************\
 *  pam_slurm_adopt.c - Adopt incoming connections into jobs
 *****************************************************************************
 *  Copyright (C) 2015, Brigham Young University
 *  Author:  Ryan Cox <ryan_cox@byu.edu>
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

#ifndef PAM_MODULE_NAME
#  define PAM_MODULE_NAME "pam_slurm_adopt"
#endif

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <security/_pam_macros.h>
#include <security/pam_ext.h>
#define PAM_SM_ACCOUNT
#include <security/pam_modules.h>
#include <security/pam_modutil.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>
#include <pwd.h>
#include <stddef.h>
#include <stdint.h>
#include <arpa/inet.h>

#include "helper.h"
#include "slurm/slurm.h"
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_protocol_api.h"

/* This definition would probably be good to centralize somewhere */
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN    64
#endif

typedef enum {
	CALLERID_ACTION_ANY,
	CALLERID_ACTION_IGNORE,
	CALLERID_ACTION_DENY
} callerid_action_t;

/* module options */
static struct {
	int single_job_skip_rpc;
	int ignore_root;
	int action_no_jobs;
	callerid_action_t action_unknown;
	callerid_action_t action_adopt_failure;
	callerid_action_t action_generic_failure;
	log_level_t log_level;
} opts;

static void _init_opts(void)
{
	opts.single_job_skip_rpc = 1;
	opts.ignore_root = 1;
	opts.action_no_jobs = CALLERID_ACTION_DENY;
	opts.action_unknown = CALLERID_ACTION_ANY;
	opts.action_adopt_failure = CALLERID_ACTION_IGNORE;
	opts.action_generic_failure = CALLERID_ACTION_IGNORE;
	opts.log_level = LOG_LEVEL_INFO;
}

static int _adopt_process(pid_t pid, uint32_t job_id)
{
	/* TODO:  add this pid to plugins for task, container, accounting, etc
	*  need more code here ... */
	info("_adopt_process(%d, %u): UNIMPLEMENTED", pid, job_id);

	/* TODO:  change my primary gid to the job's group, if possible */
	return SLURM_SUCCESS;
}

/* Returns negative number on failure. Failures are likely to occur if a step
 * exits; this is not a problem. */
static uid_t _get_job_uid(step_loc_t *stepd)
{
	/* BUG: uid_t on Linux is unsigned but stepd_get_uid can return -1 */
	uid_t uid = -1;
	int fd;
	uint16_t protocol_version;

	fd = stepd_connect(stepd->directory, stepd->nodename,
			stepd->jobid, stepd->stepid, &protocol_version);
	if (fd < 0) {
		/* It's normal for a step to exit */
		debug3("unable to connect to step %u.%u on %s: %m",
				stepd->jobid, stepd->stepid, stepd->nodename);
		return -1;
	}

	uid = stepd_get_uid(fd, stepd->protocol_version);
	close(fd);

	/* The step may have exited. Not a big concern. */
	/* BUG: uid_t on Linux is unsigned but stepd_get_uid can return -1 */
	if ((int32_t)uid == -1)
		debug3("unable to determine uid of step %u.%u on %s",
				stepd->jobid, stepd->stepid, stepd->nodename);

	return uid;
}

static int _indeterminate_multiple(pam_handle_t *pamh, List steps, uid_t uid,
		uint32_t *job_id)
{
	ListIterator itr = NULL;
	int rc = SLURM_FAILURE;
	step_loc_t *stepd = NULL;

	if (opts.action_unknown == CALLERID_ACTION_DENY) {
		debug("Denying due to action_unknown=deny");
		send_user_msg(pamh,
			      "Access denied by "
			      PAM_MODULE_NAME
			      ": unable to determine source job");
		return PAM_PERM_DENIED;
	} else if (opts.action_unknown == CALLERID_ACTION_IGNORE) {
		debug("Allowing due to action_unknown=ignore");
		return PAM_SUCCESS;
	}

	itr = list_iterator_create(steps);
	while ((stepd = list_next(itr))) {
		if (uid == _get_job_uid(stepd)) {
			*job_id = stepd->jobid;
			rc = SLURM_SUCCESS;
			break;
		}
	}

	/* No jobs from this user exist on this node */
	if (rc != SLURM_SUCCESS) {
		if (opts.action_no_jobs == CALLERID_ACTION_DENY) {
			debug("uid %u owns no jobs => deny", uid);
			send_user_msg(pamh,
				      "Access denied by "
				      PAM_MODULE_NAME
				      ": you have no active jobs on this node");
			rc = PAM_PERM_DENIED;
		} else {
			debug("uid %u owns no jobs but action_no_jobs=ignore",
					uid);
			rc = PAM_IGNORE;
		}
	}

	list_iterator_destroy(itr);
	return rc;
}

static int _single_job_check(List steps, uid_t uid, uint32_t *job_id)
{
	ListIterator itr = NULL;
	int user_job_cnt = 0, rc = SLURM_FAILURE;
	step_loc_t *stepd = NULL;

	*job_id = (uint32_t)NO_VAL;

	itr = list_iterator_create(steps);
	while ((stepd = list_next(itr))) {
		if (uid == _get_job_uid(stepd)) {
			/* We found a job from the user but we want to ignore
			 * duplicates due to multiple steps from the same job */
			if (*job_id != stepd->jobid) {
				user_job_cnt++;
				*job_id = stepd->jobid;
				rc = SLURM_SUCCESS;
			}
		}
		if(user_job_cnt > 1) {
			debug3("_single_job_check: uid %u has multiple jobs on this node",
					uid);
			rc = SLURM_FAILURE;
			break;
		}
	}
	list_iterator_destroy(itr);

	return rc;
}

static int _rpc_network_callerid(struct callerid_conn *conn, char *user_name,
		uint32_t *job_id)
{
	network_callerid_msg_t req;
	char ip_src_str[INET6_ADDRSTRLEN];
	char node_name[MAXHOSTNAMELEN];

	memcpy((void *)&req.ip_src, (void *)&conn->ip_src, 16);
	memcpy((void *)&req.ip_dst, (void *)&conn->ip_dst, 16);
	req.port_src = conn->port_src;
	req.port_dst = conn->port_dst;
	req.af = conn->af;

	inet_ntop(req.af, &conn->ip_src, ip_src_str, INET6_ADDRSTRLEN);
	if (slurm_network_callerid(req, job_id, node_name, MAXHOSTNAMELEN)
			!= SLURM_SUCCESS) {
		debug("From %s port %d as %s: unable to retrieve callerid data from remote slurmd",
		     ip_src_str,
		     req.port_src,
		     user_name);
		return SLURM_FAILURE;
	} else if (*job_id == (uint32_t)NO_VAL) {
		debug("From %s port %d as %s: job indeterminate",
		     ip_src_str,
		     req.port_src,
		     user_name);
		return SLURM_FAILURE;
	} else {
		info("From %s port %d as %s: member of job %u",
		      ip_src_str,
		      req.port_src,
		      user_name,
		      *job_id);
		return SLURM_SUCCESS;
	}
}

/* Use the pam logging function for now since normal logging is not yet
 * initialized */
log_level_t _parse_log_level(pam_handle_t *pamh, const char *log_level_str)
{
	unsigned int u;
	char *endptr;

	u = (unsigned int)strtoul(log_level_str, &endptr, 0);
	if (endptr && endptr[0]) {
		/* not an integer */
		if (!strcasecmp(log_level_str, "quiet"))
			u = LOG_LEVEL_QUIET;
		else if(!strcasecmp(log_level_str, "fatal"))
			u = LOG_LEVEL_FATAL;
		else if(!strcasecmp(log_level_str, "error"))
			u = LOG_LEVEL_ERROR;
		else if(!strcasecmp(log_level_str, "info"))
			u = LOG_LEVEL_INFO;
		else if(!strcasecmp(log_level_str, "verbose"))
			u = LOG_LEVEL_VERBOSE;
		else if(!strcasecmp(log_level_str, "debug"))
			u = LOG_LEVEL_DEBUG;
		else if(!strcasecmp(log_level_str, "debug2"))
			u = LOG_LEVEL_DEBUG2;
		else if(!strcasecmp(log_level_str, "debug3"))
			u = LOG_LEVEL_DEBUG3;
		else if(!strcasecmp(log_level_str, "debug4"))
			u = LOG_LEVEL_DEBUG4;
		else if(!strcasecmp(log_level_str, "debug5"))
			u = LOG_LEVEL_DEBUG5;
		else if(!strcasecmp(log_level_str, "sched"))
			u = LOG_LEVEL_SCHED;
		else {
			pam_syslog(pamh,
				   LOG_ERR,
				   "unrecognized log level %s, setting to max",
				   log_level_str);
			/* We'll set it to the highest logging
			 * level, just to be sure */
			u = (unsigned int)LOG_LEVEL_END - 1;
		}
	} else {
		/* An integer was specified */
		if (u >= LOG_LEVEL_END) {
			pam_syslog(pamh,
				   LOG_ERR,
				   "log level %u too high, lowering to max", u);
			u = (unsigned int)LOG_LEVEL_END - 1;
		}
	}
	return u;
}

/* Use the pam logging function for now, so we need pamh */
static void _parse_opts(pam_handle_t *pamh, int argc, const char **argv)
{
	char *v;

	for (; argc-- > 0; ++argv) {
		if (!strncasecmp(*argv, "single_job_skip_rpc=0", 18))
			opts.single_job_skip_rpc = 0;
		else if (!strncasecmp(*argv, "ignore_root=0", 13))
			opts.ignore_root = 0;
		else if (!strncasecmp(*argv,"action_unknown=",15)) {
			v = (char *)(15 + *argv);
			if (!strncasecmp(v, "ignore", 6))
				opts.action_unknown = CALLERID_ACTION_IGNORE;
			else if (!strncasecmp(v, "any", 3))
				opts.action_unknown = CALLERID_ACTION_ANY;
			else if (!strncasecmp(v, "deny", 4))
				opts.action_unknown = CALLERID_ACTION_DENY;
			else {
				pam_syslog(pamh,
					   LOG_ERR,
					   "unrecognized action_unknown=%s, setting to 'any'",
					   v);
			}
		} else if (!strncasecmp(*argv, "log_level=", 10)) {
			v = (char *)(10 + *argv);
			opts.log_level = _parse_log_level(pamh, v);
		}
	}

}

static void _log_init(log_level_t level)
{
	log_options_t logopts = LOG_OPTS_INITIALIZER;

	logopts.stderr_level  = LOG_LEVEL_FATAL;
	logopts.syslog_level  = level;
	log_init(PAM_MODULE_NAME, logopts, LOG_AUTHPRIV, NULL);
}

/* Parse arguments, etc then get my socket address/port information. Attempt to
 * adopt this process into a job in the following order:
 * 	1) If the user has only one job on the node, pick that one
 * 	2) Send RPC to source IP of socket. If there is a slurmd at the IP
 * 		address, ask it which job I belong to. On success, pick that one
 *	3) Pick a job semi-randomly (default) or skip the adoption (if
 *		configured)
*/
PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags
		__attribute__((unused)), int argc, const char **argv)
{
	int retval = PAM_IGNORE, rc = PAM_IGNORE;
	char *user_name;
	struct callerid_conn conn;
	uint32_t job_id;
	char ip_src_str[INET6_ADDRSTRLEN];
	List steps = NULL;
	struct passwd pwd, *pwd_result;
	char *buf = NULL;
	int bufsize;

	_init_opts();
	_parse_opts(pamh, argc, argv);
	_log_init(opts.log_level);

	retval = pam_get_item(pamh, PAM_USER, (void *) &user_name);
	if (user_name == NULL || retval != PAM_SUCCESS)  {
		pam_syslog(pamh, LOG_ERR, "No username in PAM_USER? Fail!");
		return PAM_SESSION_ERR;
	}

	/* Check for an unsafe config that might lock out root. This is a very
	 * basic check that shouldn't be 100% relied on */
	if (!opts.ignore_root &&
			(opts.action_unknown == CALLERID_ACTION_DENY ||
			opts.action_no_jobs != CALLERID_ACTION_IGNORE ||
			opts.action_adopt_failure != CALLERID_ACTION_IGNORE ||
			opts.action_generic_failure != CALLERID_ACTION_IGNORE
			)) {
		/* Let's get verbose */
		info("===============================");
		info("Danger!!!");
		info("A crazy admin set ignore_root=0 and some unsafe actions");
		info("You might lock out root!");
		info("If this is desirable, modify the source code");
		info("Setting ignore_root=1 and continuing");
		opts.ignore_root = 1;
	}

	/* Ignoring root is probably best but the admin can allow it*/
	if (!strcmp(user_name, "root")) {
		if (opts.ignore_root) {
			info("Ignoring root user");
			return PAM_IGNORE;
		} else {
			/* This administrator is crazy */
			info("Danger!!! This is a connection attempt by root and ignore_root=0 is set! Hope for the best!");
		}
	}

	/* Calculate buffer size for getpwnam_r */
	bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (bufsize == -1)
		bufsize = 16384; /* take a large guess */

	buf = xmalloc(bufsize);
	retval = getpwnam_r(user_name, &pwd, buf, bufsize, &pwd_result);
	if (pwd_result == NULL) {
		if (retval == 0) {
			error("getpwnam_r could not locate %s", user_name);
		} else {
			errno = retval;
			error("getpwnam_r: %m");
		}

		xfree(buf);
		return PAM_SESSION_ERR;
	}

	/* Check my fds for a network socket */
	if (callerid_get_own_netinfo(&conn) != SLURM_SUCCESS) {
		/* We could press on for the purposes of the single- or
		 * multi-job checks, but the RPC will surely fail. If we
		 * continued we'd have to fill in junk for lots of variables */
		error("Unable to find network socket");
		rc = PAM_IGNORE;
		goto cleanup;
	}

	if (inet_ntop(conn.af, &conn.ip_src, ip_src_str, INET6_ADDRSTRLEN)
			== NULL) {
		/* This is really odd. If this failed, other functions are so
		 * likely to fail that we might as well exit */
		error("inet_ntop failed");
		rc = PAM_IGNORE;
		goto cleanup;
	}

	/* Get a list of steps on the node. A failure here likely means failures
	 * everywhere so exit on failure or if no local jobs exist */
	steps = stepd_available(NULL, NULL);
	if (!steps) {
		error("Error obtaining local step information. Fail.");
		rc = PAM_IGNORE;
		goto cleanup;
	} else if (list_count(steps) == 0) {
		info("No steps on this node from any user");
		if (opts.action_no_jobs == CALLERID_ACTION_DENY) {
			send_user_msg(pamh,
				      "Access denied by "
				      PAM_MODULE_NAME
				      ": you have no active jobs on this node");
			rc = PAM_PERM_DENIED;
		} else {
			info("uid %u owns no jobs but action_no_jobs=ignore",
					pwd.pw_uid);
			rc = PAM_IGNORE;
		}

		goto cleanup;
	}

	/* Check to see if this user has only one job on the node. If so, choose
	 * that job and adopt this process into it (unless configured not to) */
	if (opts.single_job_skip_rpc) {
		if (_single_job_check(steps, pwd.pw_uid, &job_id)
				== SLURM_SUCCESS) {
			debug("From %s port %d as %s: _single_job_check succeeded",
			      ip_src_str,
			      conn.port_src,
			      user_name);

			info("From %s port %d as %s: member of job %u",
			     ip_src_str,
			     conn.port_src,
			     user_name,
			     job_id);
			rc = _adopt_process(getpid(), job_id);
			goto cleanup;
		} else {
			debug("From %s port %d as %s: _single_job_check failed",
			      ip_src_str,
			      conn.port_src,
			      user_name);
		}
	}

	/* Single job check failed or wasn't used. Ask the slurmd (if any) at
	 * the source IP address about this connection */
	rc = _rpc_network_callerid(&conn, user_name, &job_id);
	if (rc == SLURM_SUCCESS) {
		rc = _adopt_process(getpid(), job_id);
		goto cleanup;
	}

	info("From %s port %d as %s: unable to determine source job",
	     ip_src_str,
	     conn.port_src,
	     user_name);

	/* Both the single job check and the RPC call have failed to ascertain
	 * the correct job to adopt this into. Time for drastic measures */
	rc = _indeterminate_multiple(pamh, steps, pwd.pw_uid, &job_id);
	if (rc == SLURM_SUCCESS) {
		info("From %s port %d as %s: picked job %u",
		     ip_src_str,
		     conn.port_src,
		     user_name,
		     job_id);
		rc = _adopt_process(getpid(), job_id);
	} else {
		/* This pam module was worthless, apparently */
		debug("_indeterminate_multiple failed to find a job to adopt this into");
	}

cleanup:
	FREE_NULL_LIST(steps);
	xfree(buf);
	return rc;
}

#ifdef PAM_STATIC
struct pam_module _pam_slurm_adopt_modstruct = {
	 PAM_MODULE_NAME,
	 NULL,
	 NULL,
	 pam_sm_acct_mgmt,
	 NULL,
	 NULL,
	 NULL,
};
#endif
