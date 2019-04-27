/*****************************************************************************\
 *  nss_slurm.c - Slurm NSS Plugin
 *****************************************************************************
 *  Copyright (C) 2018-2019 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include <fcntl.h>
#include <nss.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "src/common/slurm_xlator.h"

#include "src/common/parse_config.h"
#include "src/common/stepd_api.h"

/*
 * One important design note: we cannot load the slurm.conf file using the
 * usual API calls, as it will internally trigger various UID/GID lookups,
 * which will then in turn end up back inside this library. At which point,
 * we'll end up deadlocked on an internal Slurm mutex.
 */

static char node[HOST_NAME_MAX] = "";
static char spool[PATH_MAX] = DEFAULT_SPOOLDIR;

static int _load_config(void)
{
	static int config_loaded = -1;
	struct stat statbuf;
	s_p_options_t options[] = {
		{"SlurmdSpoolDir", S_P_STRING},
		{"NodeName", S_P_STRING},
		{NULL}
	};
	s_p_hashtbl_t *tbl = NULL;
	char *conf = "/etc/nss_slurm.conf";
	char *tmp = NULL;

	if (config_loaded != -1)
		return config_loaded;

	tbl = s_p_hashtbl_create(options);
	if (stat(conf, &statbuf) || !statbuf.st_size) {
		/* No file, continue below to set defaults */
	} else if (s_p_parse_file(tbl, NULL, conf, false)) {
		/* could not load or parse file */
		return (config_loaded = SLURM_ERROR);
	}

	/*
	 * For the node name:
	 * 1) Use the config value, or
	 * 2) Use SLURMD_NODENAME env var,
	 * 3) Use gethostname(), and chop off the domain.
	 */
	if (s_p_get_string(&tmp, "NodeName", tbl)) {
		strlcpy(node, tmp, HOST_NAME_MAX);
		xfree(tmp);
	} else if ((tmp = getenv("SLURMD_NODENAME"))) {
		strlcpy(node, tmp, HOST_NAME_MAX);
	} else {
		char *dot;

		if (gethostname(node, HOST_NAME_MAX))
			goto error;
		if ((dot = strchr(node, '.')))
			*dot = '\0';
	}

	if (s_p_get_string(&tmp, "SlurmdSpoolDir", tbl)) {
		strlcpy(spool, tmp, PATH_MAX);
		xfree(tmp);
	}

	s_p_hashtbl_destroy(tbl);

	return (config_loaded = SLURM_SUCCESS);

error:
	s_p_hashtbl_destroy(tbl);
	return (config_loaded = SLURM_ERROR);
}

static struct passwd *_pw_internal(int mode, uid_t uid, const char *name)
{
	List steps = NULL;
	ListIterator itr = NULL;
	step_loc_t *stepd;
	int fd;
	struct passwd *pwd = NULL;

	if (_load_config())
		return NULL;
	/*
	 * Both arguments to stepd_available() must be provided, otherwise
	 * it will internally try to load the Slurm config to sort it out.
	 */
	if (!(steps = stepd_available(spool, node))) {
		fprintf(stderr, "error retrieving Slurm step info\n");
		return NULL;
	}

	itr = list_iterator_create(steps);
        while ((stepd = list_next(itr))) {
		fd = stepd_connect(stepd->directory, stepd->nodename,
				   stepd->jobid, stepd->stepid,
				   &stepd->protocol_version);

		if (fd < 0)
			continue;

		if ((pwd = stepd_getpw(fd, stepd->protocol_version,
				       mode, uid, name)))
			break;
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(steps);

	return pwd;
}

static int _internal_getpw(int mode, uid_t uid, const char *name,
			   struct passwd *pwd, char *buf, size_t buflen,
			   struct passwd **result)
{
	int len_name, len_passwd, len_gecos, len_dir, len_shell;
	struct passwd *rpc_result = NULL;

	if (!(rpc_result = _pw_internal(mode, uid, name)))
		return NSS_STATUS_NOTFOUND;

	len_name = strlen(rpc_result->pw_name);
	len_passwd = strlen(rpc_result->pw_passwd);
	len_gecos = strlen(rpc_result->pw_gecos);
	len_dir = strlen(rpc_result->pw_dir);
	len_shell = strlen(rpc_result->pw_shell);

	/* need space for an extra five NUL characters */
	if ((len_name + len_passwd + len_gecos + len_dir + len_shell + 5)
	    > buflen) {
		xfree_struct_passwd(rpc_result);
		return ERANGE;
	}

	strncpy(buf, rpc_result->pw_name, len_name + 1);
	pwd->pw_name = buf;
	buf += len_name + 1;

	strncpy(buf, rpc_result->pw_passwd, len_passwd + 1);
	pwd->pw_passwd = buf;
	buf += len_passwd + 1;

	pwd->pw_uid = rpc_result->pw_uid;
	pwd->pw_gid = rpc_result->pw_gid;

	strncpy(buf, rpc_result->pw_gecos, len_gecos + 1);
	pwd->pw_gecos = buf;
	buf += len_gecos + 1;

	strncpy(buf, rpc_result->pw_dir, len_dir + 1);
	pwd->pw_dir = buf;
	buf += len_dir + 1;

	strncpy(buf, rpc_result->pw_shell, len_shell + 1);
	pwd->pw_shell = buf;

	*result = pwd;
	xfree_struct_passwd(rpc_result);
	return NSS_STATUS_SUCCESS;
}

int _nss_slurm_getpwnam_r(const char *name, struct passwd *pwd,
			  char *buf, size_t buflen, struct passwd **result)
{
	return _internal_getpw(GETPW_MATCH_USER_AND_PID, NO_VAL, name, pwd,
			       buf, buflen, result);

}

int _nss_slurm_getpwuid_r(uid_t uid, struct passwd *pwd,
			  char *buf, size_t buflen, struct passwd **result)
{
	return _internal_getpw(GETPW_MATCH_USER_AND_PID, uid, NULL, pwd,
			       buf, buflen, result);
}

static int entry_fetched = 1;

int _nss_slurm_setpwent(void)
{
	entry_fetched = 0;
	return NSS_STATUS_SUCCESS;
}

int _nss_slurm_getpwent_r(struct passwd *pwd, char *buf, size_t buflen,
			  struct passwd **result)
{
	/*
	 * There is only ever one entry here. The docs indicate we should
	 * return NSS_STATUS_NOTFOUND on successive queries.
	 */
	if (entry_fetched)
		return NSS_STATUS_NOTFOUND;
	entry_fetched = 1;

	return _internal_getpw(GETPW_MATCH_PID, NO_VAL, NULL, pwd,
			     buf, buflen, result);
}

int _nss_slurm_endpwent(void)
{
	return NSS_STATUS_SUCCESS;
}
