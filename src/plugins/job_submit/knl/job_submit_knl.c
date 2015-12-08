/*****************************************************************************\
 *  job_submit_knl.c - Infrastructure for Intel Knights Landing processor
 *****************************************************************************
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#if HAVE_CONFIG_H
#  include "config.h"
#  if STDC_HEADERS
#    include <string.h>
#  endif
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif /* HAVE_SYS_TYPES_H */
#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else /* ! HAVE_INTTYPES_H */
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif /* HAVE_INTTYPES_H */
#else /* ! HAVE_CONFIG_H */
#  include <sys/types.h>
#  include <unistd.h>
#  include <stdint.h>
#  include <string.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/knl.h"
#include "src/common/parse_config.h"
#include "src/slurmctld/slurmctld.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for SLURM authentication) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Job submit KNL plugin";
const char plugin_type[]       	= "job_submit/knl";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

static s_p_options_t knl_conf_file_options[] = {
	{"AvailNUMA", S_P_STRING},
	{"DefaultNUMA", S_P_STRING},
	{"AvailMCDRAM", S_P_STRING},
	{"DefaultMCDRAM", S_P_STRING},
	{NULL}
};

static uint16_t avail_mcdram;
static uint16_t avail_numa;
static uint16_t default_mcdram;
static uint16_t default_numa;

static s_p_hashtbl_t *_config_make_tbl(char *filename)
{
	s_p_hashtbl_t *tbl = NULL;

	xassert(filename);

	if (!(tbl = s_p_hashtbl_create(knl_conf_file_options))) {
		error("%s: %s: s_p_hashtbl_create error: %m",
		      plugin_name, __func__);
		return tbl;
	}

	if (s_p_parse_file(tbl, NULL, filename, false) == SLURM_ERROR) {
		error("%s: %s: s_p_parse_file error: %m",
		      plugin_name, __func__);
		s_p_hashtbl_destroy(tbl);
		tbl = NULL;
	}

	return tbl;
}

int init (void)
{
	char *avail_mcdram_str, *avail_numa_str;
	char *default_mcdram_str, *default_numa_str;
	char *knl_conf_file, *tmp_str = NULL;
	s_p_hashtbl_t *tbl;

	/* Set default values */
	avail_mcdram = KNL_MCDRAM_FLAG;
	avail_numa = KNL_NUMA_FLAG;
	default_mcdram = KNL_CACHE;
	default_numa = KNL_ALL2ALL;

	knl_conf_file = get_extra_conf_path("knl.conf");
	if ((tbl = _config_make_tbl(knl_conf_file))) {
		if (s_p_get_string(&tmp_str, "AvailMCDRAM", tbl)) {
			avail_mcdram = knl_mcdram_parse(tmp_str);
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "AvailNUMA", tbl)) {
			avail_numa = knl_numa_parse(tmp_str);
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "DefaultMCDRAM", tbl)) {
			default_mcdram = knl_mcdram_parse(tmp_str);
			if (knl_mcdram_bits_cnt(default_mcdram) != 1) {
				fatal("%s: Invalid DefaultMCDRAM=%s",
				      plugin_name, tmp_str);
			}
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "DefaultNUMA", tbl)) {
			default_numa = knl_numa_parse(tmp_str);
			if (knl_numa_bits_cnt(default_numa) != 1) {
				fatal("%s: Invalid DefaultNUMA=%s",
				      plugin_name, tmp_str);
			}
			xfree(tmp_str);
		}
	} else {
		error("something wrong with opening/reading knl.conf");
	}
	xfree(knl_conf_file);
	s_p_hashtbl_destroy(tbl);

	avail_mcdram_str = knl_mcdram_str(avail_mcdram);
	avail_numa_str = knl_numa_str(avail_numa);
	default_mcdram_str = knl_mcdram_str(default_mcdram);
	default_numa_str = knl_numa_str(default_numa);
	if ((default_mcdram & avail_mcdram) == 0) {
		fatal("%s: DefaultMCDRAM(%s) not within AvailMCDRAM(%s)",
		      plugin_name, default_mcdram_str, avail_mcdram_str);
	}
	if ((default_numa & avail_numa) == 0) {
		fatal("%s: DefaultNUMA(%s) not within AvailNUMA(%s)",
		      plugin_name, default_numa_str, avail_numa_str);
	}
	if (slurm_get_debug_flags() & DEBUG_FLAG_KNL) {
		info("AvailMCDRAM=%s DefaultMCDRAM=%s",
		     avail_mcdram_str, default_mcdram_str);
		info("AvailNUMA=%s DefaultNUMA=%s",
		     avail_numa_str, default_numa_str);
	}
	xfree(avail_mcdram_str);
	xfree(avail_numa_str);
	xfree(default_mcdram_str);
	xfree(default_numa_str);

	return SLURM_SUCCESS;
}

int fini (void)
{
	return SLURM_SUCCESS;
}

extern int job_submit(struct job_descriptor *job_desc, uint32_t submit_uid)
{
	return SLURM_SUCCESS;
}

extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr, uint32_t submit_uid)
{
	return SLURM_SUCCESS;
}
