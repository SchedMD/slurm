/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  UCRL-CODE-2002-040.
 *  
 *  Written by Chris Dunlap <cdunlap@llnl.gov>
 *         and Jim Garlick  <garlick@llnl.gov>
 *         modified for SLURM by Moe Jette <jette@llnl.gov>.
 *  
 *  This file is part of pam_slurm, a PAM module for restricting access to
 *  the compute nodes within a cluster based on information obtained from
 *  Simple Linux Utility for Resource Managment (SLURM).  For details, see
 *  <http://www.llnl.gov/linux/slurm/>.
 *  
 *  pam_slurm is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *  
 *  pam_slurm is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with pam_slurm; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/


#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include <dlfcn.h>

#include "slurm/slurm.h"
#include "hostlist.h"

/*  Define the externally visible functions in this file.
 */
#define PAM_SM_ACCOUNT
#include <security/pam_modules.h>
#include <security/_pam_macros.h>


struct _options {
	int disable_sys_info;
	int enable_debug;
	int enable_silence;
	const char *msg_prefix;
	const char *msg_suffix;
};

/* Define the functions to be called before and after load since _init
 * and _fini are obsolete, and their use can lead to unpredicatable
 * results.
 */
void __attribute__ ((constructor)) libpam_slurm_init(void);
void __attribute__ ((destructor)) libpam_slurm_fini(void);

/*
 *  Handle for libslurm.so
 *
 *  We open libslurm.so via dlopen () in order to pass the
 *   flag RTDL_GLOBAL so that subsequently loaded modules have
 *   access to libslurm symbols. This is pretty much only needed
 *   for dynamically loaded modules that would otherwise be
 *   linked against libslurm.
 *
 */
static void * slurm_h = NULL;
static int    debug   = 0;

static void _log_msg(int level, const char *format, ...);
static void _parse_args(struct _options *opts, int argc, const char **argv);
static int  _hostrange_member(char *hostname, char *str);
static int  _slurm_match_allocation(uid_t uid);
static void _send_denial_msg(pam_handle_t *pamh, struct _options *opts,
			     const char *user, uid_t uid);

#define DBG(msg,args...)					\
	do {							\
		if (debug)					\
			_log_msg(LOG_INFO, msg, ##args);	\
	} while (0);

/**********************************\
 *  Account Management Functions  *
\**********************************/

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	struct _options opts;
	int retval;
	char *user;
	void *dummy;  /* needed to eliminate warning:
		       * dereferencing type-punned pointer will break
		       * strict-aliasing rules */
	struct passwd *pw;
	uid_t uid;
	int auth = PAM_PERM_DENIED;

	_parse_args(&opts, argc, argv);
	if (flags & PAM_SILENT)
		opts.enable_silence = 1;

	retval = pam_get_item(pamh, PAM_USER, (const void **) &dummy);
	user = (char *) dummy;
	if ((retval != PAM_SUCCESS) || (user == NULL) || (*user == '\0')) {
		_log_msg(LOG_ERR, "unable to identify user: %s",
			 pam_strerror(pamh, retval));
		return(PAM_USER_UNKNOWN);
	}
	if (!(pw = getpwnam(user))) {
		_log_msg(LOG_ERR, "user %s does not exist", user);
		return(PAM_USER_UNKNOWN);
	}
	uid = pw->pw_uid;

	if (uid == 0)
		auth = PAM_SUCCESS;
	else if (_slurm_match_allocation(uid))
		auth = PAM_SUCCESS;

	if ((auth != PAM_SUCCESS) && (!opts.enable_silence))
		_send_denial_msg(pamh, &opts, user, uid);
	if ((auth != PAM_SUCCESS) || (!opts.disable_sys_info)) {
		_log_msg(LOG_INFO, "access %s for user %s (uid=%d)",
			 (auth == PAM_SUCCESS) ? "granted" : "denied", 
			 user, uid);
	}
	_log_msg(LOG_INFO, "access %s for user %s (uid=%d)",
		 (auth == PAM_SUCCESS) ? "granted" : "denied", user, uid);

	return(auth);
}


/************************\
 *  Internal Functions  *
\************************/

/*
 *  Writes message described by the 'format' string to syslog.
 */
static void
_log_msg(int level, const char *format, ...)
{
	va_list args;

	openlog("pam_slurm", LOG_CONS | LOG_PID, LOG_AUTHPRIV);
	va_start(args, format);
	vsyslog(level, format, args);
	va_end(args);
	closelog();
	return;
}

/*
 *  Parses module args passed via PAM's config.
 */
static void
_parse_args(struct _options *opts, int argc, const char **argv)
{
	int i;

	opts->disable_sys_info = 0;
	opts->enable_debug = 0;
	opts->enable_silence = 0;
	opts->msg_prefix = "";
	opts->msg_suffix = "";

	/*  rsh_kludge:
	 *  The rsh service under RH71 (rsh-0.17-2.5) truncates the first char
	 *  of this msg.  The rsh client sends 3 NUL-terminated ASCII strings:
	 *  client-user-name, server-user-name, and command string.  The server
	 *  then validates the user.  If the user is valid, it responds with a
	 *  1-byte zero; o/w, it responds with a 1-byte one followed by an ASCII
	 *  error message and a newline.  RH's server is using the default PAM
	 *  conversation function which doesn't prepend the message with a
	 *  single-byte error code.  As a result, the client receives a string,
	 *  interprets the first byte as a non-zero status, and treats the
	 *  remaining string as an error message.  The rsh_kludge prepends a
	 *  newline which will be interpreted by the rsh client as an
	 *  error status.
	 *
	 *  rlogin_kludge:
	 *  The rlogin service under RH71 (rsh-0.17-2.5) does not perform a
	 *  carriage-return after the PAM error message is displayed
	 *  which results
	 *  in the "staircase-effect" of the next message. The rlogin_kludge
	 *  appends a carriage-return to prevent this.
	 */
	for (i=0; i<argc; i++) {
		if (!strcmp(argv[i], "debug"))
			opts->enable_debug = debug = 1;
		else if (!strcmp(argv[i], "no_sys_info"))
			opts->disable_sys_info = 1;
		else if (!strcmp(argv[i], "no_warn"))
			opts->enable_silence = 1;
		else if (!strcmp(argv[i], "rsh_kludge"))
			opts->msg_prefix = "\n";
		else if (!strcmp(argv[i], "rlogin_kludge"))
			opts->msg_suffix = "\r";
		else
			_log_msg(LOG_ERR, "unknown option [%s]", argv[i]);
	}
	return;
}

/* 
 *  Return 1 if 'hostname' is a member of 'str', a SLURM-style host list as 
 *  returned by SLURM datatbase queries, else 0.  The 'str' argument is
 *  truncated to the base prefix as a side-effect.
 */
static int
_hostrange_member(char *hostname, char *str)
{
	hostlist_t hl;
	int found_host;

	if (!*hostname || !*str)
		return 0;

	if ((hl = hostlist_create(str)) == NULL)
		return 0;
	found_host = hostlist_find(hl, hostname);
	hostlist_destroy(hl);

	if (found_host == -1)
		return 0;
	else
		return 1;
}

/*
 *  Wrapper for SLURM API function slurm_load_jobs ()
 */
int _slurm_load_jobs (job_info_msg_t **msgp)
{
	static int (*load_jobs) (time_t, job_info_msg_t **, uint16_t);

	if (!(load_jobs = dlsym (slurm_h, "slurm_load_jobs"))) {
		_log_msg (LOG_ERR, "Unable to resolve slurm_load_jobs\n");
		return -1;
	}
    
	return load_jobs ((time_t) NULL, msgp, (uint16_t) SHOW_ALL);
}

/*
 *  Wrapper for SLURM API function slurm_strerror ()
 */
char * _slurm_strerror (int errnum)
{
	static char * (*f) (int);

	if (!(f = dlsym (slurm_h, "slurm_strerror"))) {
		_log_msg (LOG_ERR, "Unable to resolve slurm_strerror\n");
		return "unknown error";
	}

	return f (errnum);
}

/*
 *  Wrapper for slurm_free_job_info_msg ()
 */
void _free_msg (job_info_msg_t *msg)
{
	static void (*free_msg) (job_info_msg_t *);

	if (!(free_msg = dlsym (slurm_h, "slurm_free_job_info_msg"))) {
		_log_msg (LOG_ERR, "Unable to resolve slurm_free_job...\n");
		return;
	}

	free_msg (msg);

	return;
}

/*
 *  Query the SLURM database to find out if 'uid' has been allocated 
 *  this node. If so, return 1 indicating that 'uid' is authorized to 
 *  this node else return 0.  
 */
static int 
_slurm_match_allocation(uid_t uid)
{
	int authorized = 0, i;
	char hostname[MAXHOSTNAMELEN], *p;
	job_info_msg_t * msg;

	if (gethostname(hostname, sizeof(hostname)) < 0) {
		_log_msg(LOG_ERR, "gethostname: %m");
		return 0;
	}
	if ((p = strchr(hostname, '.')))
		*p = '\0';

	DBG ("does uid %ld have \"%s\" allocated", uid, hostname);

	if (_slurm_load_jobs(&msg) < 0) {
		_log_msg(LOG_ERR, "slurm_load_jobs: %s",
			 _slurm_strerror(errno));
		return 0;
	}

	DBG ("slurm_load_jobs returned %d records", msg->record_count);

	for (i = 0; i < msg->record_count; i++) {
		job_info_t *j = &msg->job_array[i];

		if (  (j->user_id == uid) && (j->job_state == JOB_RUNNING)) {

			DBG ("jobid %ld: nodes=\"%s\"", j->job_id, j->nodes);

			if (_hostrange_member(hostname, j->nodes) ) {
				DBG ("user %ld allocated node %s in job %ld", 
				     uid, hostname, j->job_id);
				authorized = 1;
				break;
			}
		}
	}

	_free_msg (msg);

	return authorized;
}

/*
 *  Sends a message to the application informing the user
 *  that access was denied due to SLURM.
 */
static void
_send_denial_msg(pam_handle_t *pamh, struct _options *opts,
                 const char *user, uid_t uid)
{
	int retval;
	struct pam_conv *conv;
	void *dummy;    /* needed to eliminate warning:
			 * dereferencing type-punned pointer will
			 * break strict-aliasing rules */
	int n;
	char str[PAM_MAX_MSG_SIZE];
	struct pam_message msg[1];
	const struct pam_message *pmsg[1];
	struct pam_response *prsp;

	/*  Get conversation function to talk with app.
	 */
	retval = pam_get_item(pamh, PAM_CONV, (const void **) &dummy);
	conv = (struct pam_conv *) dummy;
	if (retval != PAM_SUCCESS) {
		_log_msg(LOG_ERR, "unable to get pam_conv: %s",
			 pam_strerror(pamh, retval));
		return;
	}

	/*  Construct msg to send to app.
	 */
	n = snprintf(str, sizeof(str),
		     "%sAccess denied: user %s (uid=%d) has no active jobs.%s",
		     opts->msg_prefix, user, uid, opts->msg_suffix);
	if ((n < 0) || (n >= sizeof(str)))
		_log_msg(LOG_ERR, "exceeded buffer for pam_conv message");
	msg[0].msg_style = PAM_ERROR_MSG;
	msg[0].msg = str;
	pmsg[0] = &msg[0];
	prsp = NULL;

	/*  Send msg to app and free the (meaningless) rsp.
	 */
	retval = conv->conv(1, pmsg, &prsp, conv->appdata_ptr);
	if (retval != PAM_SUCCESS)
		_log_msg(LOG_ERR, "unable to converse with app: %s",
			 pam_strerror(pamh, retval));
	if (prsp != NULL)
		_pam_drop_reply(prsp, 1);

	return;
}

/* 
 * Dynamically open system's libslurm.so with RTLD_GLOBAL flag.
 *  This allows subsequently loaded modules access to libslurm symbols.
 */
extern void libpam_slurm_init (void)
{
	if (slurm_h)
		return;
   
	if (!(slurm_h = dlopen("libslurm.so", RTLD_NOW|RTLD_GLOBAL))) 
		_log_msg (LOG_ERR, "Unable to dlopen libslurm: %s\n",
			  dlerror ());

	return;
}

extern void libpam_slurm_fini (void)
{
 	if (slurm_h)
		dlclose (slurm_h);
	return;
}


/*************************************\
 *  Statically Loaded Module Struct  *
\*************************************/

#ifdef PAM_STATIC
struct pam_module _pam_rms_modstruct = {
	"pam_slurm",
	NULL,
	NULL,
	pam_sm_acct_mgmt,
	NULL,
	NULL,
	NULL,
};
#endif /* PAM_STATIC */

