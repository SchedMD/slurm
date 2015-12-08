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

static uint16_t avail_mcdram, avail_numa;
static uint16_t default_mcdram, default_numa;

int init (void)
{
	char *avail_mcdram_str, *avail_numa_str;
	char *default_mcdram_str, *default_numa_str;
	int rc;

	rc = knl_conf_read(&avail_mcdram, &avail_numa,
			   &default_mcdram, &default_numa);

	if (slurm_get_debug_flags() & DEBUG_FLAG_KNL) {
		avail_mcdram_str = knl_mcdram_str(avail_mcdram);
		avail_numa_str = knl_numa_str(avail_numa);
		default_mcdram_str = knl_mcdram_str(default_mcdram);
		default_numa_str = knl_numa_str(default_numa);
		info("AvailMCDRAM=%s DefaultMCDRAM=%s",
		     avail_mcdram_str, default_mcdram_str);
		info("AvailNUMA=%s DefaultNUMA=%s",
		     avail_numa_str, default_numa_str);
		xfree(avail_mcdram_str);
		xfree(avail_numa_str);
		xfree(default_mcdram_str);
		xfree(default_numa_str);
	}

	return rc;
}

int fini (void)
{
	return SLURM_SUCCESS;
}

extern int job_submit(struct job_descriptor *job_desc, uint32_t submit_uid)
{
	uint16_t job_mcdram, job_numa;
	int mcdram_cnt, numa_cnt;
	char *tmp_str;

	job_mcdram = knl_mcdram_parse(job_desc->features, "&");
	mcdram_cnt = knl_mcdram_bits_cnt(job_mcdram);
	if (mcdram_cnt > 1) {			/* Multiple MCDRAM options */
		return ESLURM_INVALID_KNL;
	} else if (mcdram_cnt == 0) {
		if (job_desc->features && job_desc->features[0])
			xstrcat(job_desc->features, "&");
		tmp_str = knl_mcdram_str(default_mcdram);
		xstrcat(job_desc->features, tmp_str);
		xfree(tmp_str);
	} else if ((job_mcdram & avail_mcdram) == 0) { /* Unavailable option */
		return ESLURM_INVALID_KNL;
	}

	job_numa = knl_numa_parse(job_desc->features, "&");
	numa_cnt = knl_numa_bits_cnt(job_numa);
	if (numa_cnt > 1) {			/* Multiple NUMA options */
		return ESLURM_INVALID_KNL;
	} else if (numa_cnt == 0) {
		if (job_desc->features && job_desc->features[0])
			xstrcat(job_desc->features, "&");
		tmp_str = knl_numa_str(default_numa);
		xstrcat(job_desc->features, tmp_str);
		xfree(tmp_str);
	} else if ((job_numa & avail_numa) == 0) { /* Unavailable NUMA option */
		return ESLURM_INVALID_KNL;
	}

	return SLURM_SUCCESS;
}

extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr, uint32_t submit_uid)
{
	uint16_t job_mcdram, job_numa;
	int mcdram_cnt, numa_cnt;
	char *tmp_str;

	if (!job_desc->features)
		return SLURM_SUCCESS;
	if (!IS_JOB_PENDING(job_ptr))
		return ESLURM_JOB_NOT_PENDING;

	job_mcdram = knl_mcdram_parse(job_desc->features, "&");
	mcdram_cnt = knl_mcdram_bits_cnt(job_mcdram);
	if (mcdram_cnt > 1) {			/* Multiple MCDRAM options */
		return ESLURM_INVALID_KNL;
	} else if (mcdram_cnt == 0) {
		if (job_desc->features && job_desc->features[0])
			xstrcat(job_desc->features, "&");
		tmp_str = knl_mcdram_str(default_mcdram);
		xstrcat(job_desc->features, tmp_str);
		xfree(tmp_str);
	} else if ((job_mcdram & avail_mcdram) == 0) { /* Unavailable option */
		return ESLURM_INVALID_KNL;
	}

	job_numa = knl_numa_parse(job_desc->features, "&");
	numa_cnt = knl_numa_bits_cnt(job_numa);
	if (numa_cnt > 1) {			/* Multiple NUMA options */
		return ESLURM_INVALID_KNL;
	} else if (numa_cnt == 0) {
		if (job_desc->features && job_desc->features[0])
			xstrcat(job_desc->features, "&");
		tmp_str = knl_numa_str(default_numa);
		xstrcat(job_desc->features, tmp_str);
		xfree(tmp_str);
	} else if ((job_numa & avail_numa) == 0) { /* Unavailable NUMA option */
		return ESLURM_INVALID_KNL;
	}

	return SLURM_SUCCESS;
}
