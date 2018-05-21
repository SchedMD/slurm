/*****************************************************************************\
 *  cray_config.c
 *
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD LLC <https://www.schedmd.com>.
 *  Supported by the Oak Ridge National Laboratory Extreme Scale Systems Center
 *  Written by Danny Auble <da@schedmd.com>
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

#include "cray_config.h"

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/common/read_config.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

cray_config_t *cray_conf = NULL;

s_p_options_t cray_conf_file_options[] = {
	{"AlpsDir",        S_P_STRING},	/* Vestigial option */
	{"apbasil",        S_P_STRING},
	{"ApbasilTimeout", S_P_UINT16},
	{"apkill",         S_P_STRING},
	{"AlpsEngine",     S_P_STRING},
	{"NoAPIDSignalOnKill", S_P_BOOLEAN},
	{"SDBdb",          S_P_STRING},
	{"SDBhost",        S_P_STRING},
	{"SDBpass",        S_P_STRING},
	{"SDBport",        S_P_UINT32},
	{"SDBuser",        S_P_STRING},
	{"SubAllocate",    S_P_BOOLEAN},
	{"SyncTimeout",    S_P_UINT32},
	{NULL}
};

extern int create_config(void)
{
	int rc = SLURM_SUCCESS;
	char* cray_conf_file = NULL;
	static time_t last_config_update = (time_t) 0;
	struct stat config_stat;
	s_p_hashtbl_t *tbl = NULL;

	if (cray_conf)
		return SLURM_ERROR;

	cray_conf = xmalloc(sizeof(cray_config_t));

	cray_conf_file = get_extra_conf_path("cray.conf");

	if (stat(cray_conf_file, &config_stat) < 0) {
		cray_conf->apbasil          = xstrdup(DEFAULT_APBASIL);
		cray_conf->apbasil_timeout  = DEFAULT_APBASIL_TIMEOUT;
		cray_conf->apkill           = xstrdup(DEFAULT_APKILL);
		cray_conf->sdb_db           = xstrdup(DEFAULT_CRAY_SDB_DB);
		cray_conf->sdb_host         = xstrdup(DEFAULT_CRAY_SDB_HOST);
		cray_conf->sdb_pass         = xstrdup(DEFAULT_CRAY_SDB_PASS);
		cray_conf->sdb_port         = DEFAULT_CRAY_SDB_PORT;
		cray_conf->sdb_user         = xstrdup(DEFAULT_CRAY_SDB_USER);
		cray_conf->sync_timeout     = DEFAULT_CRAY_SYNC_TIMEOUT;
		xfree(cray_conf_file);
		goto end_it;
	}
	if (cray_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("Reading the cray.conf file %s", cray_conf_file);

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
		      "conf file %s", cray_conf_file);
	xfree(cray_conf_file);

	if (!s_p_get_string(&cray_conf->apbasil, "apbasil", tbl))
		cray_conf->apbasil = xstrdup(DEFAULT_APBASIL);
	if (!s_p_get_uint16(&cray_conf->apbasil_timeout, "ApbasilTimeout", tbl))
		cray_conf->apbasil_timeout = DEFAULT_APBASIL_TIMEOUT;
	if (!s_p_get_string(&cray_conf->apkill, "apkill", tbl))
		cray_conf->apkill = xstrdup(DEFAULT_APKILL);

	(void) s_p_get_string(&cray_conf->alps_engine, "AlpsEngine", tbl);

	(void) s_p_get_boolean(&cray_conf->no_apid_signal_on_kill,
			       "NoAPIDSignalOnKill", tbl);

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

	(void) s_p_get_boolean(&cray_conf->sub_alloc, "SubAllocate", tbl);

	if (!s_p_get_uint32(&cray_conf->sync_timeout, "SyncTimeout", tbl))
		cray_conf->sync_timeout = DEFAULT_CRAY_SYNC_TIMEOUT;

	s_p_hashtbl_destroy(tbl);
end_it:
	cray_conf->slurm_debug_flags = slurmctld_conf.debug_flags;

#if 0
	info("Cray conf is...");
	info("\tapbasil=\t%s", cray_conf->apbasil);
	info("\ApbasilTimeout=\t%s", cray_conf->apbasil);
	info("\tapkill=\t\t%s", cray_conf->apkill);
	info("\tAlpsEngine=\t\t%u", cray_conf->apbasil_timeout);
	info("\tSDBdb=\t\t%s", cray_conf->sdb_db);
	info("\tSDBhost=\t%s", cray_conf->sdb_host);
	info("\tSDBpass=\t%s", cray_conf->sdb_pass);
	info("\tSDBport=\t%u", cray_conf->sdb_port);
	info("\tSDBuser=\t%s", cray_conf->sdb_user);
	info("\tSubAllocate=\t%u", cray_conf->sub_alloc);
	info("\tSyncTimeout=\t%u", cray_conf->sync_timeout);
#endif
	return rc;
}

extern int destroy_config(void)
{
	int rc = SLURM_SUCCESS;

	if (cray_conf) {
		xfree(cray_conf->apbasil);
		xfree(cray_conf->apkill);
		xfree(cray_conf->alps_engine);
		xfree(cray_conf->sdb_db);
		xfree(cray_conf->sdb_host);
		xfree(cray_conf->sdb_pass);
		xfree(cray_conf->sdb_user);
		xfree(cray_conf);
	}

	return rc;
}
