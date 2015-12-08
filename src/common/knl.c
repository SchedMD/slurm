/*****************************************************************************\
 *  knl.c - Infrastructure for Intel Knights Landing processor
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

#include "slurm/slurm.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * Return the count of MCDRAM bits set
 */
extern int knl_mcdram_bits_cnt(uint16_t mcdram_num)
{
	int cnt = 0, i;
	uint16_t tmp = 1;

	for (i = 0; i < 16; i++) {
		if ((mcdram_num & KNL_MCDRAM_FLAG) & tmp)
			cnt++;
		tmp = tmp << 1;
	}
	return cnt;
}

/*
 * Return the count of NUMA bits set
 */
extern int knl_numa_bits_cnt(uint16_t numa_num)
{
	int cnt = 0, i;
	uint16_t tmp = 1;

	for (i = 0; i < 16; i++) {
		if ((numa_num & KNL_NUMA_FLAG) & tmp)
			cnt++;
		tmp = tmp << 1;
	}
	return cnt;
}

/*
 * Translate KNL MCDRAM string to equivalent numeric value
 * mcdram_str IN - String to scan
 * sep IN - token separator to search for
 * RET MCDRAM numeric value
 */
extern uint16_t knl_mcdram_parse(char *mcdram_str, char *sep)
{
	char *save_ptr = NULL, *tmp, *tok;
	uint16_t mcdram_num = 0;

	if (!mcdram_str)
		return mcdram_num;

	tmp = xstrdup(mcdram_str);
	tok = strtok_r(tmp, sep, &save_ptr);
	while (tok) {
		if (!strcasecmp(tok, "cache"))
			mcdram_num |= KNL_CACHE;
		else if (!strcasecmp(tok, "flat"))
			mcdram_num |= KNL_FLAT;
		else if (!strcasecmp(tok, "hybrid"))
			mcdram_num |= KNL_HYBRID;
		tok = strtok_r(NULL, sep, &save_ptr);
	}
	xfree(tmp);

	return mcdram_num;
}

/*
 * Translate KNL NUMA string to equivalent numeric value
 * numa_str IN - String to scan
 * sep IN - token separator to search for
 * RET NUMA numeric value
 */
extern uint16_t knl_numa_parse(char *numa_str, char *sep)
{
	char *save_ptr = NULL, *tmp, *tok;
	uint16_t numa_num = 0;

	if (!numa_str)
		return numa_num;

	tmp = xstrdup(numa_str);
	tok = strtok_r(tmp, sep, &save_ptr);
	while (tok) {
		if (!strcasecmp(tok, "all2all"))
			numa_num |= KNL_ALL2ALL;
		else if (!strcasecmp(tok, "snc2"))
			numa_num |= KNL_SNC2;
		else if (!strcasecmp(tok, "snc4"))
			numa_num |= KNL_SNC4;
		else if (!strcasecmp(tok, "hemi"))
			numa_num |= KNL_HEMI;
		else if (!strcasecmp(tok, "quad"))
			numa_num |= KNL_QUAD;
		tok = strtok_r(NULL, sep, &save_ptr);
	}
	xfree(tmp);

	return numa_num;
}

/*
 * Translate KNL MCDRAM number to equivalent string value
 * Caller must free return value
 */
extern char *knl_mcdram_str(uint16_t mcdram_num)
{
	char *mcdram_str = NULL, *sep = "";

	if (mcdram_num & KNL_CACHE) {
		xstrfmtcat(mcdram_str, "%scache", sep);
		sep = ",";
	}
	if (mcdram_num & KNL_FLAT) {
		xstrfmtcat(mcdram_str, "%sflat", sep);
		sep = ",";
	}
	if (mcdram_num & KNL_HYBRID) {
		xstrfmtcat(mcdram_str, "%shybrid", sep);
		sep = ",";
	}

	return mcdram_str;
}

/*
 * Translate KNL NUMA number to equivalent string value
 * Caller must free return value
 */
extern char *knl_numa_str(uint16_t numa_num)
{
	char *numa_str = NULL, *sep = "";

	if (numa_num & KNL_ALL2ALL) {
		xstrfmtcat(numa_str, "%sall2all", sep);
		sep = ",";
	}
	if (numa_num & KNL_SNC2) {
		xstrfmtcat(numa_str, "%ssnc2", sep);
		sep = ",";
	}
	if (numa_num & KNL_SNC4) {
		xstrfmtcat(numa_str, "%ssnc4", sep);
		sep = ",";
	}
	if (numa_num & KNL_HEMI) {
		xstrfmtcat(numa_str, "%shemi", sep);
		sep = ",";
	}
	if (numa_num & KNL_QUAD) {
		xstrfmtcat(numa_str, "%squad", sep);
		sep = ",";
	}

	return numa_str;

}
