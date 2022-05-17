/*****************************************************************************\
 *  read_config.c - Read configuration file for slurmwmwd
 *****************************************************************************
 *  Copyright (C) 2017 Regents of the University of California
 *  Written by Douglas Jacobsen <dmjacobsen@lbl.gov>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"	/* Must be first */

#include "src/common/parse_config.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "read_config.h"

/* Global variables */
uint16_t slurmsmwd_cabinets_per_row = 0;
uint16_t slurmsmwd_debug_level = LOG_LEVEL_INFO;
char *slurmsmwd_log_file = NULL;

static s_p_options_t slurmsmwd_options[] = {
	{"CabinetsPerRow", S_P_UINT16},
	{"DebugLevel", S_P_STRING},
	{"LogFile", S_P_STRING},
	{NULL}
};

static void _validate_config(void)
{
	if (slurmsmwd_cabinets_per_row == 0)
		fatal("slurmsmwd.conf: CabinetsPerRow must not be zero");
}

extern void slurmsmwd_print_config(void)
{
	debug2("slurmsmwd configuration");
	debug2("CabinetsPerRow = %u", slurmsmwd_cabinets_per_row);
	debug2("DebugLevel     = %u", slurmsmwd_debug_level);
	debug2("LogFile        = %s", slurmsmwd_log_file);
}

/* Load configuration file contents into global variables.
 * Call slurmsmwd_free_config to free memory. */
extern void slurmsmwd_read_config(void)
{
	char *config_file = NULL;
	char *temp_str = NULL;
	s_p_hashtbl_t *tbl = NULL;
	struct stat config_stat;

	config_file = get_extra_conf_path("slurmsmwd.conf");
	if (stat(config_file, &config_stat) < 0)
		fatal("Can't stat slurmsmwd.conf %s: %m", config_file);
	tbl = s_p_hashtbl_create(slurmsmwd_options);
	if (s_p_parse_file(tbl, NULL, config_file, false, NULL) == SLURM_ERROR)
		fatal("Can't parse slurmsmwd.conf %s: %m", config_file);

	s_p_get_uint16(&slurmsmwd_cabinets_per_row, "CabinetsPerRow", tbl);
	s_p_get_string(&slurmsmwd_log_file, "LogFile", tbl);
	if (s_p_get_string(&temp_str, "DebugLevel", tbl)) {
		slurmsmwd_debug_level = log_string2num(temp_str);
		if (slurmsmwd_debug_level == NO_VAL16)
			fatal("Invalid DebugLevel %s", temp_str);
		xfree(temp_str);
	}

	_validate_config();

	s_p_hashtbl_destroy(tbl);
	xfree(config_file);
}
