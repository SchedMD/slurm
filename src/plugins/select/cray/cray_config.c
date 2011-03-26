/*****************************************************************************\
 *  cray_config.c
 *
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include "cray_config.h"
#include "src/common/read_config.h"
#include "src/common/parse_spec.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

cray_config_t *cray_conf = NULL;

s_p_options_t cray_conf_file_options[] = {
	{"AlpsDir", S_P_STRING},
	{"apbasil", S_P_STRING},
	{"apkill", S_P_STRING},
	{"SDBdb", S_P_STRING},
	{"SDBhost", S_P_STRING},
	{"SDBpass", S_P_STRING},
	{"SDBport", S_P_UINT32},
	{"SDBuser", S_P_STRING},
	{NULL}
};

static char *_get_cray_conf(void)
{
	char *val = getenv("SLURM_CONF");
	char *rc = NULL;
	int i;

	if (!val)
		return xstrdup(CRAY_CONFIG_FILE);

	/* Replace file name on end of path */
	i = strlen(val) - strlen("slurm.conf") + strlen("cray.conf") + 1;
	rc = xmalloc(i);
	strcpy(rc, val);
	val = strrchr(rc, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = rc;
	strcpy(val, "cray.conf");
	return rc;
}

extern int create_config()
{
	int rc = SLURM_SUCCESS;
	char* cray_conf_file = NULL;
	static time_t last_config_update = (time_t) 0;
	struct stat config_stat;
	s_p_hashtbl_t *tbl = NULL;

	if (cray_conf)
		return SLURM_ERROR;

	cray_conf = xmalloc(sizeof(cray_config_t));

	cray_conf_file = _get_cray_conf();

	if (stat(cray_conf_file, &config_stat) < 0) {
		cray_conf->alps_dir = xstrdup(DEFAULT_ALPS_DIR);
		cray_conf->apbasil = xstrdup(DEFAULT_APBASIL);
		cray_conf->apkill = xstrdup(DEFAULT_APKILL);
		cray_conf->sdb_db = xstrdup(DEFAULT_CRAY_SDB_DB);
		cray_conf->sdb_host = xstrdup(DEFAULT_CRAY_SDB_HOST);
		cray_conf->sdb_pass = xstrdup(DEFAULT_CRAY_SDB_PASS);
		cray_conf->sdb_port = DEFAULT_CRAY_SDB_PORT;
		cray_conf->sdb_user = xstrdup(DEFAULT_CRAY_SDB_USER);
		goto end_it;
	}
	if (cray_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("Reading the cray.conf file");
	
	if (last_config_update) {
		if (last_config_update == config_stat.st_mtime) {
			if (cray_conf->slurm_debug_flags
			    & DEBUG_FLAG_SELECT_TYPE)
				info("%s unchanged", cray_conf_file);
		} else {
			info("Restart slurmctld for %s changes "
			     "to take effect",
			     cray_conf_file);
		}
		last_config_update = config_stat.st_mtime;
		xfree(cray_conf_file);
		return SLURM_SUCCESS;
	}

	tbl = s_p_hashtbl_create(cray_conf_file_options);

	if (s_p_parse_file(tbl, NULL, cray_conf_file, false) == SLURM_ERROR)
		fatal("something wrong with opening/reading cray "
		      "conf file");
	xfree(cray_conf_file);

	if (!s_p_get_string(&cray_conf->alps_dir, "AlpsDir", tbl))
		cray_conf->alps_dir = xstrdup(DEFAULT_ALPS_DIR);
	if (!s_p_get_string(&cray_conf->apbasil, "apbasil", tbl))
		cray_conf->apbasil = xstrdup_printf("%s/bin/apbasil",
						    cray_conf->alps_dir);
#ifndef HAVE_ALPS_EMULATION 
	if (!s_p_get_string(&cray_conf->apkill, "apkill", tbl))
		cray_conf->apkill = xstrdup_printf("%s/bin/apkill",
						   cray_conf->alps_dir);
#endif
	if (!s_p_get_string(&cray_conf->sdb_db, "SDBdb", tbl))
		cray_conf->sdb_db = xstrdup(DEFAULT_CRAY_SDB_DB);
	if (!s_p_get_string(&cray_conf->sdb_host, "SDBhost", tbl))
		cray_conf->sdb_host = xstrdup(DEFAULT_CRAY_SDB_HOST);
	if (!s_p_get_string(&cray_conf->sdb_pass, "SDBpass", tbl))
		cray_conf->sdb_pass = xstrdup(DEFAULT_CRAY_SDB_PASS);
	if (!s_p_get_uint32(&cray_conf->sdb_port, "SDBport", tbl))
		cray_conf->sdb_port = DEFAULT_CRAY_SDB_PORT;
	if (!s_p_get_string(&cray_conf->sdb_user, "SDBuser", tbl))
		cray_conf->sdb_user = xstrdup(DEFAULT_CRAY_SDB_USER);

end_it:
	cray_conf->slurm_debug_flags = slurmctld_conf.debug_flags;

#if 0
	info("Cray conf is...");
	info("\tAlpsDir=\t%s", cray_conf->alps_dir);
	info("\tapbasil=\t%s", cray_conf->apbasil);
	info("\tapkill=\t\t%s", cray_conf->apkill);
	info("\tSDBdb=\t\t%s", cray_conf->sdb_db);
	info("\tSDBhost=\t%s", cray_conf->sdb_host);
	info("\tSDBpass=\t%s", cray_conf->sdb_pass);
	info("\tSDBport=\t%u", cray_conf->sdb_port);
	info("\tSDBuser=\t%s", cray_conf->sdb_user);
#endif
	return rc;
}

extern int destroy_config()
{
	int rc = SLURM_SUCCESS;

	if (cray_conf) {
		xfree(cray_conf->alps_dir);
		xfree(cray_conf->apbasil);
		xfree(cray_conf->apkill);
		xfree(cray_conf->sdb_db);
		xfree(cray_conf->sdb_host);
		xfree(cray_conf->sdb_pass);
		xfree(cray_conf->sdb_user);
		xfree(cray_conf->slurm_debug_flags);
		xfree(cray_conf);
	}

	return rc;
}
