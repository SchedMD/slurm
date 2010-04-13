/*****************************************************************************\
 *  job_submit_partition.c - Set default partition in job submit request
 *  specifications.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
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
 * plugin_version   - specifies the version number of the plugin.
 * min_plug_version - specifies the minumum version number of incoming
 *                    messages that this plugin can accept
 */
const char plugin_name[]       	= "Job submit partition plugin";
const char plugin_type[]       	= "job_submit/partition";
const uint32_t plugin_version   = 100;
const uint32_t min_plug_version = 100;

/* Test if this user can run jobs in the selected partition based upon
 * the partition's AllowGroups parameter. */
static bool _user_access(uid_t run_uid, struct part_record *part_ptr)
{
	int i;

	if ((run_uid == 0) || (run_uid == getuid()))
		return true;	/* Superuser can run anywhere */

	if (!part_ptr->allow_uids)
		return true;	/* AllowGroups=ALL */

	for (i=0; part_ptr->allow_uids[i]; i++) {
		if (part_ptr->allow_uids[i] == run_uid)
			return true;	/* User in AllowGroups */
	}
	return false;		/* User not in AllowGroups */
}

/* This exampe code will set a job's default partition to the highest
 * priority partition that is available to this user. This is only an
 * example and tremendous flexibility is available. */
extern int job_submit(struct job_descriptor *job_desc)
{
	ListIterator part_iterator;
	struct part_record *part_ptr;
	struct part_record *top_prio_part = NULL;

	if (job_desc->partition)	/* job already specified partition */
		return SLURM_SUCCESS;

	part_iterator = list_iterator_create(part_list);
	if (!part_iterator)
		fatal("list_iterator_create malloc");
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		if (!(part_ptr->state_up & PARTITION_SUBMIT))
			continue;	/* nobody can submit jobs here */
		if (!_user_access(job_desc->user_id, part_ptr))
			continue;	/* AllowGroups prevents use */
		if (!top_prio_part ||
		    (top_prio_part->priority < part_ptr->priority)) {
			/* Found higher priority partition */
			top_prio_part = part_ptr;
		}
	}
	list_iterator_destroy(part_iterator);

	if (top_prio_part) {
		info("Setting partition of submitted job to %s",
		     top_prio_part->name);
		job_desc->partition = xstrdup(top_prio_part->name);
	}

	return SLURM_SUCCESS;
}

extern int job_modify(struct job_descriptor *job_desc, 
		      struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}
