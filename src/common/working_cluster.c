/*****************************************************************************\
 *  working_cluster.c - definitions dealing with the working cluster
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
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

#include <string.h>

#include "src/common/slurm_strcasestr.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"

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
		working_cluster_rec->dimensions : SYSTEM_DIMENSIONS;
}

extern int *slurmdb_setup_cluster_dim_size(void)
{
	if (working_cluster_rec)
		return working_cluster_rec->dim_size;

	return select_g_ba_get_dims();
}

extern bool is_cray_system(void)
{
	if (working_cluster_rec)
		return working_cluster_rec->flags & CLUSTER_FLAG_CRAY;
#if defined HAVE_ALPS_CRAY || defined HAVE_NATIVE_CRAY
	return true;
#endif
	return false;
}

extern bool is_alps_cray_system(void)
{
	if (working_cluster_rec)
		return working_cluster_rec->flags & CLUSTER_FLAG_CRAY_A;
#ifdef HAVE_ALPS_CRAY
	return true;
#endif
	return false;
}

extern uint16_t slurmdb_setup_cluster_name_dims(void)
{
	if (is_cray_system())
		return 1;	/* Cray uses 1-dimensional hostlists */

	return slurmdb_setup_cluster_dims();
}

extern uint32_t slurmdb_setup_cluster_flags(void)
{
	static uint32_t cluster_flags = NO_VAL;

	if (working_cluster_rec)
		return working_cluster_rec->flags;
	else if (cluster_flags != NO_VAL)
		return cluster_flags;

	cluster_flags = 0;
#ifdef HAVE_BG
	cluster_flags |= CLUSTER_FLAG_BG;
#endif
#ifdef HAVE_BGL
	cluster_flags |= CLUSTER_FLAG_BGL;
#endif
#ifdef HAVE_BGP
	cluster_flags |= CLUSTER_FLAG_BGP;
#endif
#ifdef HAVE_BGQ
	cluster_flags |= CLUSTER_FLAG_BGQ;
#endif
#ifdef HAVE_SUN_CONST
	cluster_flags |= CLUSTER_FLAG_SC;
#endif
#ifdef HAVE_XCPU
	cluster_flags |= CLUSTER_FLAG_XCPU;
#endif
#ifdef HAVE_AIX
	cluster_flags |= CLUSTER_FLAG_AIX;
#endif
#ifdef MULTIPLE_SLURMD
	cluster_flags |= CLUSTER_FLAG_MULTSD;
#endif
#ifdef HAVE_ALPS_CRAY
	cluster_flags |= CLUSTER_FLAG_CRAY_A;
#endif
#ifdef HAVE_FRONT_END
	cluster_flags |= CLUSTER_FLAG_FE;
#endif
#ifdef HAVE_NATIVE_CRAY
	cluster_flags |= CLUSTER_FLAG_CRAY_N;
#endif
	return cluster_flags;
}

static uint32_t _str_2_cluster_flags(char *flags_in)
{
	if (slurm_strcasestr(flags_in, "AIX"))
		return CLUSTER_FLAG_AIX;

	if (slurm_strcasestr(flags_in, "BGL"))
		return CLUSTER_FLAG_BGL;

	if (slurm_strcasestr(flags_in, "BGP"))
		return CLUSTER_FLAG_BGP;

	if (slurm_strcasestr(flags_in, "BGQ"))
		return CLUSTER_FLAG_BGQ;

	if (slurm_strcasestr(flags_in, "Bluegene"))
		return CLUSTER_FLAG_BG;

	if (slurm_strcasestr(flags_in, "AlpsCray")
	    || slurm_strcasestr(flags_in, "CrayXT"))
		return CLUSTER_FLAG_CRAY_A;

	if (slurm_strcasestr(flags_in, "FrontEnd"))
		return CLUSTER_FLAG_FE;

	if (slurm_strcasestr(flags_in, "MultipleSlurmd"))
		return CLUSTER_FLAG_MULTSD;

	if (slurm_strcasestr(flags_in, "SunConstellation"))
		return CLUSTER_FLAG_SC;

	if (slurm_strcasestr(flags_in, "XCPU"))
		return CLUSTER_FLAG_XCPU;

	if (slurm_strcasestr(flags_in, "Cray"))
		return CLUSTER_FLAG_CRAY_N;

	return (uint32_t) 0;
}


extern uint32_t slurmdb_str_2_cluster_flags(char *flags_in)
{
	uint32_t cluster_flags = 0;
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

/*needs to be xfreed */
extern char *slurmdb_cluster_flags_2_str(uint32_t flags_in)
{
	char *cluster_flags = NULL;

	if (flags_in & CLUSTER_FLAG_AIX) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "AIX");
	}

	if (flags_in & CLUSTER_FLAG_BG) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "Bluegene");
	}

	if (flags_in & CLUSTER_FLAG_BGL) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "BGL");
	}

	if (flags_in & CLUSTER_FLAG_BGP) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "BGP");
	}

	if (flags_in & CLUSTER_FLAG_BGQ) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "BGQ");
	}

	if (flags_in & CLUSTER_FLAG_CRAY_A) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "AlpsCray");
	}

	if (flags_in & CLUSTER_FLAG_FE) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "FrontEnd");
	}

	if (flags_in & CLUSTER_FLAG_MULTSD) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "MultipleSlurmd");
	}

	if (flags_in & CLUSTER_FLAG_SC) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "SunConstellation");
	}

	if (flags_in & CLUSTER_FLAG_XCPU) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "XCPU");
	}

	if (flags_in & CLUSTER_FLAG_CRAY_N) {
		if (cluster_flags)
			xstrcat(cluster_flags, ",");
		xstrcat(cluster_flags, "Cray");
	}

	if (!cluster_flags)
		cluster_flags = xstrdup("None");

	return cluster_flags;
}

