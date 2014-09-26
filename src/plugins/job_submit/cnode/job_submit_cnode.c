/*****************************************************************************\
 *  job_submit_cnode.c - Set a job's cnode license count equal to the number
 *  of cnode required (BlueGene compute nodes). This mechanism can be used to
 *  manage resource reservations of less than a full midplane.
 *
 *  NOTE: In order to use this, configure licenses on the computer named
 *  "cnode" and having a count equal to all cnodes on the system.
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmctld/slurmctld.h"

/* Define the minimum number of cnodes which can be allocated on a system.
 * This is hardware and configuration dependent. More work is needed here. */
#ifndef MIN_CNODES
#define MIN_CNODES 32
#endif

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
 * plugin_version   - specifies the version number of the plugin.
 * min_plug_version - specifies the minumum version number of incoming
 *                    messages that this plugin can accept
 */
const char plugin_name[]       	= "Job submit cnode plugin";
const char plugin_type[]       	= "job_submit/cnode";
const uint32_t plugin_version   = 110;
const uint32_t min_plug_version = 100;

static void _rebuild_licenses(char **license_ptr, uint32_t cnode_cnt)
{
	char *sep = "", *save_ptr, *tok;
	char *orig_licenses, *new_licenses = NULL;
	int i;
	bool put_cnode = false;

	/* Reset job cnode count to a value supported on this hardware
	 * with this SLURM configuration. The job specification might
	 * also have a CPU count or geometry that might alter the cnode
	 * count specified in the job request. */
	for (i = 1; i < (1024 * 1024); i *= 2) {
		if (cnode_cnt <= (MIN_CNODES * i)) {
			cnode_cnt = (MIN_CNODES * i);
			break;
		}
	}

	if (*license_ptr == NULL) {
		xstrfmtcat(*license_ptr, "cnode*%u", cnode_cnt);
		return;
	}

	orig_licenses = *license_ptr;
	tok = strtok_r(orig_licenses, ",", &save_ptr);
	while (tok) {
		if (!strcmp(tok, "cnode") || !strncmp(tok, "cnode*", 6)) {
			xstrfmtcat(new_licenses, "%scnode*%u", sep, cnode_cnt);
			put_cnode = true;
		} else {
			xstrfmtcat(new_licenses, "%s%s", sep, tok);
		}
		tok = strtok_r(NULL, ",", &save_ptr);
		sep = ",";
	}
	if (!put_cnode)
		xstrfmtcat(new_licenses, "%scnode*%u", sep, cnode_cnt);
	xfree(orig_licenses);
	*license_ptr = new_licenses;
}

/* Set the job's license specification to include its cnodes requirement */
extern int job_submit(struct job_descriptor *job_desc, uint32_t submit_uid,
		      char **err_msg)
{
	uint32_t cnode_cnt = job_desc->min_nodes;
	static bool printed = false;

	xassert(job_desc);
	if (cnode_cnt == NO_VAL)
		cnode_cnt = MIN_CNODES;

	if (!printed) {
		error("job_submit/cnode is deprecated.  Reservations can now "
		      "be done on a cnode level.  Please start doing it "
		      "this way instead of using licenses as this plugin "
		      "will go away in the next version of the code.");
		printed = true;
	}
	_rebuild_licenses(&job_desc->licenses, cnode_cnt);
	return SLURM_SUCCESS;
}

extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr, uint32_t submit_uid)
{
	char **license_ptr;
	uint32_t cnode_cnt = job_desc->min_nodes;

	xassert(job_desc);
	xassert(job_ptr);
	if ((job_desc->licenses == NULL) && job_ptr->licenses)
		job_desc->licenses = xstrdup(job_ptr->licenses);
	license_ptr = &job_desc->licenses;

	if ((cnode_cnt == NO_VAL) && job_ptr->details)
		cnode_cnt = job_ptr->details->min_nodes;

	_rebuild_licenses(license_ptr, cnode_cnt);

	return SLURM_SUCCESS;
}
