/*****************************************************************************\
 *  pam_slurm_adopt.c - Adopt incoming connections into jobs
 *****************************************************************************
 *  Copyright (C) 2015, Brigham Young University
 *  Author:  Ryan Cox <ryan_cox@byu.edu>
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

#define _GNU_SOURCE
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

#include "src/common/callerid.h"
#include "src/interfaces/cgroup.h"
#include "src/common/slurm_protocol_api.h"

typedef enum {
	CALLERID_ACTION_NEWEST,
	CALLERID_ACTION_ALLOW,
	CALLERID_ACTION_IGNORE,
	CALLERID_ACTION_DENY,
} callerid_action_t;

/* module options */
static struct {
	int single_job_skip_rpc; /* Undocumented. If 1 and there is only 1 user
				  * job, adopt it and skip RPC. If 0, *always*
				  * try RPC even in single job situations.
				  * Unlikely to ever be set to 0. */
	int ignore_root;
	callerid_action_t action_no_jobs;
	callerid_action_t action_unknown;
	callerid_action_t action_adopt_failure;
	callerid_action_t action_generic_failure;
	log_level_t log_level;
	char *node_name;
	bool disable_x11;
	char *pam_service;
	bool join_container;
} opts;

static void _init_opts(void)
{
	opts.single_job_skip_rpc = 1;
	opts.ignore_root = 1;
	opts.action_no_jobs = CALLERID_ACTION_DENY;
	opts.action_unknown = CALLERID_ACTION_NEWEST;
	opts.action_adopt_failure = CALLERID_ACTION_ALLOW;
	opts.action_generic_failure = CALLERID_ACTION_IGNORE;
	opts.log_level = LOG_LEVEL_INFO;
	opts.node_name = NULL;
	opts.disable_x11 = false;
	opts.pam_service = NULL;
	opts.join_container = true;
}

/* Adopts a process into the given step. Returns SLURM_SUCCESS if
 * opts.action_adopt_failure == CALLERID_ACTION_ALLOW or if the process was
 * successfully adopted.
 */
static int _adopt_process(pam_handle_t *pamh, pid_t pid, step_loc_t *stepd)
{
	int fd;
	uint16_t protocol_version;
	int rc;

	if (!stepd)
		return -1;
	debug("%s: trying to get %ps to adopt %d",
	      __func__, &stepd->step_id, pid);
	fd = stepd_connect(stepd->directory, stepd->nodename,
			   &stepd->step_id, &protocol_version);
	if (fd < 0) {
		/* It's normal for a step to exit */
		debug3("unable to connect to %ps on %s: %m",
		       &stepd->step_id, stepd->nodename);
		return -1;
	}

	rc = stepd_add_extern_pid(fd, stepd->protocol_version, pid);

	if (rc == SLURM_SUCCESS) {
		char *env;
		env = xstrdup_printf("SLURM_JOB_ID=%u", stepd->step_id.job_id);
		pam_putenv(pamh, env);
		xfree(env);
	}

	if ((rc == SLURM_SUCCESS) && !opts.disable_x11) {
		int display;
		char *xauthority;
		display = stepd_get_x11_display(fd, stepd->protocol_version,
						&xauthority);

		if (display) {
			char *env;
			env = xstrdup_printf("DISPLAY=localhost:%d.0", display);
			pam_putenv(pamh, env);
			xfree(env);
		}

		if (xauthority) {
			char *env;
			env = xstrdup_printf("XAUTHORITY=%s", xauthority);
			pam_putenv(pamh, env);
			xfree(env);
			xfree(xauthority);
		}
	}

	if (opts.join_container) {
		int ns_fd = stepd_get_namespace_fd(fd, protocol_version);
		if (ns_fd == -1) {
			error("stepd_get_ns_fd failed");
			rc = SLURM_ERROR;
		} else if (ns_fd == 0) {
			debug2("No ns_fd given back, expected if not running with a job_container plugin that supports namespace mounting");
		} else {
			/*
			 * No need to specify the type of namespace, rely on
			 * slurm to give us the right one
			 */
			if (setns(ns_fd, 0)) {
				error("setns() failed: %m");
				rc = SLURM_ERROR;
			}
		}
	}

	close(fd);

	if (rc == SLURM_SUCCESS)
		info("Process %d adopted into job %u",
		     pid, stepd->step_id.job_id);
	else
		info("Process %d adoption FAILED for job %u",
		     pid, stepd->step_id.job_id);

	return rc;
}

/* Returns negative number on failure. Failures are likely to occur if a step
 * exits; this is not a problem. */
static uid_t _get_job_uid(step_loc_t *stepd)
{
	uid_t uid = -1;
	int fd;

	fd = stepd_connect(stepd->directory, stepd->nodename,
			   &stepd->step_id, &stepd->protocol_version);
	if (fd < 0) {
		/* It's normal for a step to exit */
		debug3("unable to connect to %ps on %s: %m",
		       &stepd->step_id, stepd->nodename);
		return -1;
	}

	uid = stepd_get_uid(fd, stepd->protocol_version);
	close(fd);

	/* The step may have exited. Not a big concern. */
	if ((int32_t)uid == -1)
		debug3("unable to determine uid of %ps on %s",
		       &stepd->step_id, stepd->nodename);

	return uid;
}

/* Return mtime of a cgroup. If we can't read the right cgroup information,
 * return 0. That results in a (somewhat) random choice of job */
static time_t _cgroup_creation_time(char *uidcg, uint32_t job_id)
{
	char path[PATH_MAX];
	struct stat statbuf;

	if (snprintf(path, PATH_MAX, "%s/job_%u", uidcg, job_id) >= PATH_MAX) {
		info("snprintf: '%s/job_%u' longer than PATH_MAX of %d",
		     uidcg, job_id, PATH_MAX);
		return 0;
	}

	if (stat(path, &statbuf) != 0) {
		info("Couldn't stat path '%s': %m", path);
		return 0;
	}

	return statbuf.st_mtime;
}

static int _check_cg_version()
{
	char *type;
	int cg_ver = 0;

	/* Check cgroup version */
	type = slurm_cgroup_conf.cgroup_plugin;

	/* Default is autodetect */
	if (!type)
		type = "autodetect";

	if (!xstrcmp(type, "autodetect"))
		if (!(type = slurm_autodetect_cgroup_version()))
			return cg_ver;

	if (!xstrcmp("cgroup/v1", type))
		cg_ver = 1;
	else if (!xstrcmp("cgroup/v2", type))
		cg_ver = 2;

	return cg_ver;
}

/*
 * Pick a random job belonging to this user.
 * Unlike when using cgroup/v1, we will pick here the job with the highest JobID
 * instead of getting the job which has the earliest cgroup creation time.
 */
static int _indeterminate_multiple_v2(pam_handle_t *pamh, List steps, uid_t uid,
				      step_loc_t **out_stepd)
{
	int rc = PAM_PERM_DENIED;
	ListIterator itr = NULL;
	step_loc_t *stepd = NULL;
	uint32_t most_recent = 0;

	itr = list_iterator_create(steps);
	while ((stepd = list_next(itr))) {
		if ((stepd->step_id.step_id == SLURM_EXTERN_CONT) &&
		    (uid == _get_job_uid(stepd))) {
			if (stepd->step_id.job_id > most_recent) {
				most_recent = stepd->step_id.job_id;
				*out_stepd = stepd;
				rc = PAM_SUCCESS;
			}
		}
	}

	if (rc != PAM_SUCCESS) {
		if (opts.action_no_jobs == CALLERID_ACTION_DENY) {
			debug("uid %u owns no jobs => deny", uid);
			send_user_msg(pamh, "Access denied by " PAM_MODULE_NAME
				      ": you have no active jobs on this node");
			rc = PAM_PERM_DENIED;
		} else {
			debug("uid %u owns no jobs but action_no_jobs=allow",
			      uid);
			rc = PAM_SUCCESS;
		}
	}

	list_iterator_destroy(itr);
	return rc;
}

static int _indeterminate_multiple(pam_handle_t *pamh, List steps, uid_t uid,
				   step_loc_t **out_stepd)
{
	ListIterator itr = NULL;
	int rc = PAM_PERM_DENIED;
	step_loc_t *stepd = NULL;
	time_t most_recent = 0, cgroup_time = 0;
	char uidcg[PATH_MAX];
	char *cgroup_suffix = "";
	char *cgroup_res = "";
	int cg_ver;

	if (opts.action_unknown == CALLERID_ACTION_DENY) {
		debug("Denying due to action_unknown=deny");
		send_user_msg(pamh,
			      "Access denied by "
			      PAM_MODULE_NAME
			      ": unable to determine source job");
		return PAM_PERM_DENIED;
	}

	cg_ver = _check_cg_version();
	debug("Detected cgroup version %d", cg_ver);

	if (cg_ver != 1 && cg_ver != 2)
		return PAM_SESSION_ERR;

	if (cg_ver == 2)
		return _indeterminate_multiple_v2(pamh, steps, uid, out_stepd);

	if (opts.node_name)
		cgroup_suffix = xstrdup_printf("_%s", opts.node_name);

	/* pick a cgroup that is likely to exist */
	if (slurm_cgroup_conf.constrain_ram_space ||
	    slurm_cgroup_conf.constrain_swap_space) {
		cgroup_res = "memory";
	} else if (slurm_cgroup_conf.constrain_cores) {
		cgroup_res = "cpuset";
	} else if (slurm_cgroup_conf.constrain_devices) {
		cgroup_res = "devices";
	} else {
		/* last resort, from proctrack/cgroup */
		cgroup_res = "freezer";
	}

	if (snprintf(uidcg, PATH_MAX, "%s/%s/slurm%s/uid_%u",
		     slurm_cgroup_conf.cgroup_mountpoint, cgroup_res,
		     cgroup_suffix, uid)
	    >= PATH_MAX) {
		info("snprintf: '%s/%s/slurm%s/uid_%u' longer than PATH_MAX of %d",
		     slurm_cgroup_conf.cgroup_mountpoint, cgroup_res,
		     cgroup_suffix, uid, PATH_MAX);
		/* Make the uidcg an empty string. This will effectively switch
		 * to a (somewhat) random selection of job rather than picking
		 * the latest, but how did you overflow PATH_MAX chars anyway?
		 */
		uidcg[0] = '\0';
	}

	if (opts.node_name)
		xfree(cgroup_suffix);

	itr = list_iterator_create(steps);
	while ((stepd = list_next(itr))) {
		/*
		 * Only use container steps from this user
		 */
		if ((stepd->step_id.step_id == SLURM_EXTERN_CONT) &&
		    (uid == _get_job_uid(stepd))) {
			cgroup_time = _cgroup_creation_time(
				uidcg, stepd->step_id.job_id);
			/* Return the newest job_id, according to cgroup
			 * creation. Hopefully this is a good way to do this */
			if (cgroup_time >= most_recent) {
				most_recent = cgroup_time;
				*out_stepd = stepd;
				rc = PAM_SUCCESS;
			}
		}
	}

	/* No jobs from this user exist on this node. This should have been
	 * caught earlier but wasn't for some reason. */
	if (rc != PAM_SUCCESS) {
		if (opts.action_no_jobs == CALLERID_ACTION_DENY) {
			debug("uid %u owns no jobs => deny", uid);
			send_user_msg(pamh, "Access denied by " PAM_MODULE_NAME
				      ": you have no active jobs on this node");
			rc = PAM_PERM_DENIED;
		} else {
			debug("uid %u owns no jobs but action_no_jobs=allow",
			      uid);
			rc = PAM_SUCCESS;
		}
	}

	list_iterator_destroy(itr);
	return rc;
}

/* This is the action of last resort. If action_unknown=allow, allow it through
 * without adoption. Otherwise, call _indeterminate_multiple to pick a job. If
 * successful, adopt it into a process and use a return code based on success of
 * the adoption and the action_adopt_failure setting. */
static int _action_unknown(pam_handle_t *pamh, struct passwd *pwd, List steps)
{
	int rc;
	step_loc_t *stepd = NULL;

	if (opts.action_unknown == CALLERID_ACTION_ALLOW) {
		debug("Allowing due to action_unknown=allow");
		return PAM_SUCCESS;
	}

	/* Both the single job check and the RPC call have failed to ascertain
	 * the correct job to adopt this into. Time for drastic measures */
	rc = _indeterminate_multiple(pamh, steps, pwd->pw_uid, &stepd);
	if (rc == PAM_SUCCESS) {
		info("action_unknown: Picked job %u", stepd->step_id.job_id);
		if (_adopt_process(pamh, getpid(), stepd) == SLURM_SUCCESS) {
			return PAM_SUCCESS;
		}
		if (opts.action_adopt_failure == CALLERID_ACTION_ALLOW)
			return PAM_SUCCESS;
		else
			return PAM_PERM_DENIED;
	} else {
		/* This pam module was worthless, apparently */
		debug("_indeterminate_multiple failed to find a job to adopt this into");
		return rc;
	}
}

/* _user_job_count returns the count of jobs owned by the user AND sets job_id
 * to the last job from the user that is found */
static int _user_job_count(List steps, uid_t uid, step_loc_t **out_stepd)
{
	ListIterator itr = NULL;
	int user_job_cnt = 0;
	step_loc_t *stepd = NULL;
	*out_stepd = NULL;

	itr = list_iterator_create(steps);
	while ((stepd = list_next(itr))) {
		/*
		 * Only count container steps from this user
		 */
		if ((stepd->step_id.step_id == SLURM_EXTERN_CONT) &&
		    (uid == _get_job_uid(stepd))) {
			user_job_cnt++;
			*out_stepd = stepd;
		}
	}
	list_iterator_destroy(itr);

	return user_job_cnt;
}

static int _rpc_network_callerid(callerid_conn_t *conn, char *user_name,
				 uint32_t *job_id)
{
	network_callerid_msg_t req;
	char ip_src_str[INET6_ADDRSTRLEN];
	char node_name[HOST_NAME_MAX];

	memset(&req, 0, sizeof(req));
	memcpy((void *)&req.ip_src, (void *)&conn->ip_src, 16);
	memcpy((void *)&req.ip_dst, (void *)&conn->ip_dst, 16);
	req.port_src = conn->port_src;
	req.port_dst = conn->port_dst;
	req.af = conn->af;

	inet_ntop(req.af, &conn->ip_src, ip_src_str, sizeof(ip_src_str));
	if (slurm_network_callerid(req, job_id, node_name, sizeof(node_name))
	    != SLURM_SUCCESS) {
		debug("From %s port %d as %s: unable to retrieve callerid data from remote slurmd",
		      ip_src_str, req.port_src, user_name);
		return SLURM_ERROR;
	} else if (*job_id == NO_VAL) {
		debug("From %s port %d as %s: job indeterminate",
		      ip_src_str, req.port_src, user_name);
		return SLURM_ERROR;
	} else {
		info("From %s port %d as %s: member of job %u",
		     ip_src_str, req.port_src, user_name, *job_id);
		return SLURM_SUCCESS;
	}
}

/* Ask the slurmd at the source IP address of the network connection if it knows
 * what job initiated this connection. If it can be determined, the process is
 * adopted into that job's step_extern. In the event of any failure, it returns
 * PAM_IGNORE so that it will fall through to the next action */
static int _try_rpc(pam_handle_t *pamh, struct passwd *pwd)
{
	uint32_t job_id;
	int rc;
	char ip_src_str[INET6_ADDRSTRLEN];
	callerid_conn_t conn;

	/* Gather network information for RPC call. */
	debug("Checking file descriptors for network socket");

	/* Check my fds for a network socket */
	if (callerid_get_own_netinfo(&conn) != SLURM_SUCCESS) {
		/* If this failed, the RPC will surely fail. If we continued
		 * we'd have to fill in junk for lots of variables. Fall
		 * through to next action. This is really odd and likely means
		 * that the kernel doesn't provide the necessary mechanisms to
		 * view this process' network info or that sshd did something
		 * different with the arrangement of file descriptors */
		error("callerid_get_own_netinfo unable to find network socket");
		return PAM_IGNORE;
	}

	if (inet_ntop(conn.af, &conn.ip_src, ip_src_str, sizeof(ip_src_str))
	    == NULL) {
		/* Somehow we successfully grabbed bad data. Fall through to
		 * next action. */
		error("inet_ntop failed");
		return PAM_IGNORE;
	}

	/* Ask the slurmd at the source IP address about this connection */
	rc = _rpc_network_callerid(&conn, pwd->pw_name, &job_id);
	if (rc == SLURM_SUCCESS) {
		step_loc_t stepd;
		memset(&stepd, 0, sizeof(stepd));
		/* We only need the step_id struct needed to be filled in here
		   all the rest isn't needed for the adopt.
		*/
		stepd.step_id.job_id = job_id;
		stepd.step_id.step_id = SLURM_EXTERN_CONT;
		stepd.step_id.step_het_comp = NO_VAL;

		/* Adopt the process. If the adoption succeeds, return SUCCESS.
		 * If not, maybe the adoption failed because the user hopped
		 * into one node and was adopted into a job there that isn't on
		 * our node here. In that case we got a bad jobid so we'll fall
		 * through to the next action */
		if (_adopt_process(pamh, getpid(), &stepd) == SLURM_SUCCESS)
			return PAM_SUCCESS;
		else
			return PAM_IGNORE;
	}

	info("From %s port %d as %s: unable to determine source job",
	     ip_src_str, conn.port_src, pwd->pw_name);

	return PAM_IGNORE;
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
		else {
			pam_syslog(pamh, LOG_ERR,
				   "unrecognized log level %s, setting to max",
				   log_level_str);
			/* We'll set it to the highest logging
			 * level, just to be sure */
			u = (unsigned int)LOG_LEVEL_END - 1;
		}
	} else {
		/* An integer was specified */
		if (u >= LOG_LEVEL_END) {
			pam_syslog(pamh, LOG_ERR,
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
		if (!xstrncasecmp(*argv, "single_job_skip_rpc=0", 21))
			opts.single_job_skip_rpc = 0;
		else if (!xstrncasecmp(*argv, "ignore_root=0", 13))
			opts.ignore_root = 0;
		else if (!xstrncasecmp(*argv,"action_no_jobs=",15)) {
			v = (char *)(15 + *argv);
			if (!xstrncasecmp(v, "deny", 4))
				opts.action_no_jobs = CALLERID_ACTION_DENY;
			else if (!xstrncasecmp(v, "ignore", 6))
				opts.action_no_jobs = CALLERID_ACTION_IGNORE;
			else {
				pam_syslog(pamh, LOG_ERR,
					   "unrecognized action_no_jobs=%s, setting to 'deny'",
					   v);
			}
		} else if (!xstrncasecmp(*argv,"action_unknown=",15)) {
			v = (char *)(15 + *argv);
			if (!xstrncasecmp(v, "allow", 5))
				opts.action_unknown = CALLERID_ACTION_ALLOW;
			else if (!xstrncasecmp(v, "newest", 6))
				opts.action_unknown = CALLERID_ACTION_NEWEST;
			else if (!xstrncasecmp(v, "deny", 4))
				opts.action_unknown = CALLERID_ACTION_DENY;
			else {
				pam_syslog(pamh, LOG_ERR,
					   "unrecognized action_unknown=%s, setting to 'newest'",
					   v);
			}
		} else if (!xstrncasecmp(*argv,"action_generic_failure=",23)) {
			v = (char *)(23 + *argv);
			if (!xstrncasecmp(v, "allow", 5))
				opts.action_generic_failure =
					CALLERID_ACTION_ALLOW;
			else if (!xstrncasecmp(v, "ignore", 6))
				opts.action_generic_failure =
					CALLERID_ACTION_IGNORE;
			else if (!xstrncasecmp(v, "deny", 4))
				opts.action_generic_failure =
					CALLERID_ACTION_DENY;
			else {
				pam_syslog(pamh, LOG_ERR,
					   "unrecognized action_generic_failure=%s, setting to 'allow'",
					   v);
			}
		} else if (!xstrncasecmp(*argv, "action_adopt_failure=", 21)) {
			v = (char *)(21 + *argv);
			if (!xstrncasecmp(v, "allow", 5))
				opts.action_adopt_failure =
					CALLERID_ACTION_ALLOW;
			else if (!xstrncasecmp(v, "deny", 4))
				opts.action_adopt_failure =
					CALLERID_ACTION_DENY;
			else {
				pam_syslog(pamh, LOG_ERR,
					   "unrecognized action_adopt_failure=%s, setting to 'allow'",
					   v);
			}
		} else if (!xstrncasecmp(*argv, "log_level=", 10)) {
			v = (char *)(10 + *argv);
			opts.log_level = _parse_log_level(pamh, v);
		} else if (!xstrncasecmp(*argv, "nodename=", 9)) {
			v = (char *)(9 + *argv);
			opts.node_name = xstrdup(v);
		} else if (!xstrncasecmp(*argv, "disable_x11=1", 13)) {
			opts.disable_x11 = true;
		} else if (!xstrncasecmp(*argv, "service=", 8)) {
			v = (char *)(8 + *argv);
			opts.pam_service = xstrdup(v);
		} else if (!xstrncasecmp(*argv, "join_container=false", 19)) {
			opts.join_container = false;
		} else {
			pam_syslog(pamh, LOG_ERR,
				   "ignoring unrecognized option '%s'", *argv);
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

/* Make sure to only continue if we're running in the sshd context
 *
 * If this module is used locally e.g. via sudo then unexpected things might
 * happen (e.g. passing environment variables interpreted by slurm code like
 * SLURM_CONF or inheriting file descriptors that are used by _try_rpc()).
 */
static int check_pam_service(pam_handle_t *pamh)
{
	const char *allowed = opts.pam_service ? opts.pam_service : "sshd";
	char *service = NULL;
	int rc;

	if (!xstrcmp(allowed, "*"))
		// any service name is allowed
		return PAM_SUCCESS;

	rc = pam_get_item(pamh, PAM_SERVICE, (void*)&service);

	if (rc != PAM_SUCCESS) {
		pam_syslog(pamh, LOG_ERR, "failed to obtain PAM_SERVICE name");
		return rc;
	} else if (!service) {
		// this shouldn't actually happen
		return PAM_BAD_ITEM;
	}

	if (!xstrcmp(service, allowed)) {
		return PAM_SUCCESS;
	}

	pam_syslog(pamh, LOG_INFO,
		   "Not adopting process since this is not an allowed pam service");
	return PAM_IGNORE;
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
	int retval = PAM_IGNORE, rc = PAM_IGNORE, slurmrc, bufsize, user_jobs;
	char *user_name;
	List steps = NULL;
	step_loc_t *stepd = NULL;
	struct passwd pwd, *pwd_result;
	char *buf = NULL;

	_init_opts();
	_parse_opts(pamh, argc, argv);

	retval = check_pam_service(pamh);
	if (retval != PAM_SUCCESS) {
		return retval;
	}

	_log_init(opts.log_level);

	switch (opts.action_generic_failure) {
	case CALLERID_ACTION_DENY:
		rc = PAM_PERM_DENIED;
		break;
	case CALLERID_ACTION_ALLOW:
		rc = PAM_SUCCESS;
		break;
	case CALLERID_ACTION_IGNORE:
		rc = PAM_IGNORE;
		break;
		/* Newer gcc versions warn if enum cases are missing */
	default:
		error("The code is broken!!!!");
	}

	retval = pam_get_item(pamh, PAM_USER, (void *) &user_name);
	if (user_name == NULL || retval != PAM_SUCCESS)  {
		pam_syslog(pamh, LOG_ERR, "No username in PAM_USER? Fail!");
		return PAM_SESSION_ERR;
	}

	/* Check for an unsafe config that might lock out root. This is a very
	 * basic check that shouldn't be 100% relied on */
	if (!opts.ignore_root &&
	    (opts.action_unknown == CALLERID_ACTION_DENY ||
	     opts.action_no_jobs != CALLERID_ACTION_ALLOW ||
	     opts.action_adopt_failure != CALLERID_ACTION_ALLOW ||
	     opts.action_generic_failure != CALLERID_ACTION_ALLOW
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

	/* Ignoring root is probably best but the admin can allow it */
	if (pwd.pw_uid == 0) {
		if (opts.ignore_root) {
			info("Ignoring root user");
			return PAM_IGNORE;
		} else {
			/* This administrator is crazy */
			info("Danger!!! This is a connection attempt by root (user id 0) and ignore_root=0 is set! Hope for the best!");
		}
	}

	/*
	 * Initialize after root has been permitted access, which is critical
	 * in case the config file won't load on this node for some reason.
	 */
	slurm_conf_init(NULL);
	slurm_cgroup_conf_init();

	/*
	 * Check if there are any steps on the node from any user. A failure here
	 * likely means failures everywhere so exit on failure or if no local jobs
	 * exist. This can also happen if SlurmdSpoolDir cannot be found, or if
	 * the NodeName cannot be established for some reason.
	 */
	steps = stepd_available(NULL, opts.node_name);
	if (!steps) {
		send_user_msg(pamh, "No Slurm jobs found on node.");
		goto cleanup;
	}

	/* Check to see if this user has only one job on the node. If so, choose
	 * that job and adopt this process into it (unless configured not to) */
	user_jobs = _user_job_count(steps, pwd.pw_uid, &stepd);
	if (user_jobs == 0) {
		if (opts.action_no_jobs == CALLERID_ACTION_DENY) {
			debug("uid %u owns no jobs => deny", pwd.pw_uid);
			send_user_msg(pamh, "Access denied by " PAM_MODULE_NAME
				      ": you have no active jobs on this node");
			rc = PAM_PERM_DENIED;
		} else {
			debug("uid %u owns no jobs but action_no_jobs=ignore",
			      pwd.pw_uid);
			rc = PAM_IGNORE;
		}
		goto cleanup;
	} else if (user_jobs == 1) {
		if (opts.single_job_skip_rpc) {
			info("Connection by user %s: user has only one job %u",
			     user_name, stepd->step_id.job_id);
			slurmrc = _adopt_process(pamh, getpid(), stepd);
			/* If adoption into the only job fails, it is time to
			 * exit. Return code is based on the
			 * action_adopt_failure setting */
			if (slurmrc == SLURM_SUCCESS ||
			    (opts.action_adopt_failure ==
			     CALLERID_ACTION_ALLOW))
				rc = PAM_SUCCESS;
			else {
				send_user_msg(pamh, "Access denied by "
					      PAM_MODULE_NAME
					      ": failed to adopt process into cgroup, denying access because action_adopt_failure=deny");
				rc = PAM_PERM_DENIED;
			}
			goto cleanup;
		}
	} else {
		debug("uid %u has %d jobs", pwd.pw_uid, user_jobs);
	}

	/* Single job check turned up nothing (or we skipped it). Make RPC call
	 * to slurmd at source IP. If it can tell us the job, the function calls
	 * _adopt_process */
	rc = _try_rpc(pamh, &pwd);
	if (rc == PAM_SUCCESS)
		goto cleanup;

	/* The source of the connection either didn't reply or couldn't
	 * determine the job ID at the source. Proceed to action_unknown */
	rc = _action_unknown(pamh, &pwd, steps);

cleanup:
	slurm_cgroup_conf_destroy();
	FREE_NULL_LIST(steps);
	xfree(buf);
	xfree(opts.node_name);
	xfree(opts.pam_service);
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
