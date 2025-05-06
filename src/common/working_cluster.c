/*****************************************************************************\
 *  working_cluster.c - definitions dealing with the working cluster
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <string.h>

#include "src/common/env.h"
#include "src/interfaces/select.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * This functions technically should go in the slurmdb_defs.c, but
 * because some systems don't deal well with strong_alias and
 * functions defined extern in other headers i.e. slurm.h has the
 * hostlist functions in it, and they are also strong aliased in
 * hostlist.c which needs to know about the working_cluster_rec
 * defined in slurmdb.h which includes slurm.h.  So to avoid having to
 * include the slurm.h we will just define them some place that
 * doesn't need to include the slurm.h in the header.
 */

extern uint16_t slurmdb_setup_cluster_dims(void)
{
	return working_cluster_rec ?
		working_cluster_rec->dimensions : 1;
}

extern uint32_t slurmdb_setup_cluster_flags(void)
{
	static uint32_t cluster_flags = NO_VAL;

	if (working_cluster_rec)
		return working_cluster_rec->flags;
	else if (cluster_flags != NO_VAL)
		return cluster_flags;

	cluster_flags = 0;
#ifdef MULTIPLE_SLURMD
	cluster_flags |= CLUSTER_FLAG_MULTSD;
#endif
	return cluster_flags;
}

#define T(flag, str) { flag, XSTRINGIFY(flag), str }
static const struct {
	slurmdb_cluster_flags_t flag;
	char *flag_str;
	char *str;
} slurmdb_cluster_flags_map[] = {
	T(CLUSTER_FLAG_DELETED, "Deleted"),
	T(CLUSTER_FLAG_EXT, "External"),
	T(CLUSTER_FLAG_FED, "Federation"),
	T(CLUSTER_FLAG_MULTSD, "MultipleSlurmd"),
	T(CLUSTER_FLAG_REGISTER, "Registering"),
	T(CLUSTER_FLAG_INVALID, "INVALID"),
};
#undef T

static slurmdb_cluster_flags_t _str_2_cluster_flags(char *flags_in)
{
	if (!flags_in || !flags_in[0])
		return CLUSTER_FLAG_NONE;

	for (int i = 0; i < ARRAY_SIZE(slurmdb_cluster_flags_map); i++)
		if (!xstrncasecmp(flags_in, slurmdb_cluster_flags_map[i].str,
				  strlen(flags_in)))
			return slurmdb_cluster_flags_map[i].flag;

	debug("%s: Unable to match %s to a slurmdbd_cluster_flags_t flag",
	      __func__, flags_in);
	return CLUSTER_FLAG_INVALID;
}


extern slurmdb_cluster_flags_t slurmdb_str_2_cluster_flags(char *flags_in)
{
	slurmdb_cluster_flags_t cluster_flags = 0;
	char *token, *my_flags, *last = NULL;

	my_flags = xstrdup(flags_in);
	token = strtok_r(my_flags, ",", &last);
	while (token) {
		cluster_flags |= _str_2_cluster_flags(token);
		token = strtok_r(NULL, ",", &last);
	}
	xfree(my_flags);

	return cluster_flags;
}

/* must xfree() returned string */
extern char *slurmdb_cluster_flags_2_str(slurmdb_cluster_flags_t flags_in)
{
	char *cluster_flags = NULL, *at = NULL;

	if (!flags_in)
		return xstrdup("None");

	for (int i = 0; i < ARRAY_SIZE(slurmdb_cluster_flags_map); i++) {
		if ((slurmdb_cluster_flags_map[i].flag & flags_in) ==
		    slurmdb_cluster_flags_map[i].flag)
			xstrfmtcatat(cluster_flags, &at, "%s%s",
				     (cluster_flags ? "," : ""),
				     slurmdb_cluster_flags_map[i].str);
	}

	return cluster_flags;
}

extern void
slurm_setup_remote_working_cluster(resource_allocation_response_msg_t *msg)
{
	xassert(msg);
	xassert(msg->working_cluster_rec);
	xassert(msg->node_list);

	if (working_cluster_rec)
		slurmdb_destroy_cluster_rec(working_cluster_rec);

	working_cluster_rec = (slurmdb_cluster_rec_t *)msg->working_cluster_rec;
	msg->working_cluster_rec = NULL;

	slurm_set_addr(&working_cluster_rec->control_addr,
		       working_cluster_rec->control_port,
		       working_cluster_rec->control_host);

	if (setenvf(NULL, "SLURM_CLUSTER_NAME", "%s",
		    working_cluster_rec->name) < 0)
		error("unable to set SLURM_CLUSTER_NAME in environment");
}
