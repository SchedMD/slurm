/*
 * pam_job.so module to create SGI PAGG container on user login.
 * Needed on Cray systems to enable PAGG support in interactive salloc sessions.
 *
 * 1. install the pam-devel-xxx.rpm corresponding to your pam-xxx.rpm
 * 2. compile with gcc -fPIC -DPIC -shared pam_job.c -o pam_job.so
 * 3. install on boot:/rr/current/lib64/security/pam_job.so
 * 4. in xtopview -c login, add the following line to /etc/pam.d/common-session:
 *    session    optional    pam_job.so
 */
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 * Copyright (c) 2011 Centro Svizzero di Calcolo Scientifico
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 */
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pwd.h>
#include <errno.h>

#include <sys/syslog.h>
#define error(fmt, args...) syslog(LOG_CRIT, "pam_job: " fmt, ##args);

#define PAM_SM_ACCOUNT
#define PAM_SM_SESSION
#include <security/_pam_macros.h>
#include <security/pam_modules.h>

/*
 * Unroll job.h/jobctl.h header declarations. The rationale is that not all
 * systems will have the required kernel header (job.h, jobctl.h, paggctl.h).
 * On early 2.4/2.5 kernels there was a paggctl() system call which was then
 * replaced by the /proc/job ioctl, which this implementation tests for. All
 * patches from ftp://oss.sgi.com/projects/pagg/download that use /proc/job
 * for ioctl have the same ioctl declarations and identical ioctl parameters.
 * Comparing these patches shows that, when using a 2.6 kernel, there are no
 * differences at all in the 23 ioctl calls (last patch was for 2.6.16.21).
 */
#define JOB_CREATE	_IOWR('A', 1, void *)
struct job_create {
	uint64_t	r_jid;		/* Return value of JID */
	uint64_t	jid;		/* Jid value requested */
	int		user;		/* UID of user associated with job */
	int		options;	/* creation options - unused */
};

PAM_EXTERN int pam_sm_open_session(pam_handle_t * pamh, int flags,
				   int argc, const char **argv)
{
	struct job_create jcreate = {0};
	struct passwd *passwd;
	char *username;
	int job_ioctl_fd;

	if (pam_get_item(pamh, PAM_USER, (void *)&username) != PAM_SUCCESS
	    || username == NULL) {
		error("error recovering username");
		return PAM_SESSION_ERR;
	}

	passwd = getpwnam(username);
	if (!passwd) {
		error("error getting passwd entry for %s", username);
		return PAM_SESSION_ERR;
	}
	jcreate.user = passwd->pw_uid;	/* uid associated with job */

	if ((job_ioctl_fd = open("/proc/job", 0)) < 0) {
		error("can not open /proc/job: %s", strerror(errno));
		return PAM_SESSION_ERR;
	} else if (ioctl(job_ioctl_fd, JOB_CREATE, (void *)&jcreate) != 0) {
		error("job_create failed (no container): %s", strerror(errno));
		close(job_ioctl_fd);
		return PAM_SESSION_ERR;
	}
	close(job_ioctl_fd);

	if (jcreate.r_jid == 0)
		error("WARNING - job containers disabled, no PAGG IDs created");
	return PAM_SUCCESS;
}

/*
 * Not all PAMified apps invoke session management modules.  So, we supply
 * this account management function for such cases.  Whenever possible, it
 * is still better to use the session management version.
 */
PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags,
				int argc, const char **argv)
{
	if (pam_sm_open_session(pamh, flags, argc, argv) != PAM_SUCCESS)
		return PAM_AUTH_ERR;
	return PAM_SUCCESS;
}

PAM_EXTERN int pam_sm_close_session(pam_handle_t *pamh, int flags,
				    int argc, const char **argv)
{
	return PAM_SUCCESS;
}
