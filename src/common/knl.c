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
 */
extern uint16_t knl_mcdram_parse(char *mcdram_str)
{
	char *save_ptr = NULL, *tmp, *tok;
	uint16_t mcdram = 0;

	tmp = xstrdup(mcdram_str);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		if (!strncasecmp(tok, "cache", 2))
			mcdram |= KNL_CACHE;
		else if (!strncasecmp(tok, "flat", 2))
			mcdram |= KNL_FLAT;
		else if (!strncasecmp(tok, "hybrid", 2))
			mcdram |= KNL_HYBRID;
		else
			error("Invalid KNL MCDRAM value: %s", tok);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	return mcdram;
}

/*
 * Translate KNL NUMA string to equivalent numeric value
 */
extern uint16_t knl_numa_parse(char *numa_str)
{
	char *save_ptr = NULL, *tmp, *tok;
	uint16_t numa = 0;

	tmp = xstrdup(numa_str);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		if (!strncasecmp(tok, "all2all", 3) ||
		    !strcasecmp(tok, "a2a"))
			numa |= KNL_ALL2ALL;
		else if (!strcasecmp(tok, "snc2") ||
			 !strcasecmp(tok, "snc-2"))
			numa |= KNL_SNC2;
		else if (!strcasecmp(tok, "snc4") ||
			 !strcasecmp(tok, "snc-4"))
			numa |= KNL_SNC4;
		else if (!strncasecmp(tok, "hemi", 2))
			numa |= KNL_HEMI;
		else if (!strncasecmp(tok, "quad", 2))
			numa |= KNL_QUAD;
		else
			error("Invalid KNL NUMA value: %s", tok);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	return numa;
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
