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
#include "src/common/knl.h"
#include "src/common/macros.h"
#include "src/common/parse_config.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, KNL plugins will stop
 * working.  If you need to add fields, add them to the end of the structure.
 */
typedef struct slurm_knl_ops {
	int	(*status) (char *node_list);
	int	(*boot)	  (char *node_list, char *mcdram_type, char *numa_type);
} slurm_knl_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_knl_ops_t.
 */
static const char *syms[] = {
	"slurm_knl_g_status",
	"slurm_knl_g_boot"
};

static s_p_options_t knl_conf_file_options[] = {
	{"AvailNUMA", S_P_STRING},
	{"DefaultNUMA", S_P_STRING},
	{"AvailMCDRAM", S_P_STRING},
	{"DefaultMCDRAM", S_P_STRING},
	{NULL}
};

static int g_context_cnt = -1;
static slurm_knl_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static char *knl_plugin_list = NULL;
static bool init_run = false;

static s_p_hashtbl_t *_config_make_tbl(char *filename)
{
	s_p_hashtbl_t *tbl = NULL;

	xassert(filename);

	if (!(tbl = s_p_hashtbl_create(knl_conf_file_options))) {
		error("knl.conf: %s: s_p_hashtbl_create error: %m", __func__);
		return tbl;
	}

	if (s_p_parse_file(tbl, NULL, filename, false) == SLURM_ERROR) {
		error("knl.conf: %s: s_p_parse_file error: %m", __func__);
		s_p_hashtbl_destroy(tbl);
		tbl = NULL;
	}

	return tbl;
}

/*
 * Parse knl.conf file and return available and default modes
 * avail_mcdram IN - available MCDRAM modes
 * avail_numa IN - available NUMA modes
 * default_mcdram IN - default MCDRAM mode
 * default_numa IN - default NUMA mode
 * RET - Slurm error code
 */
extern int knl_conf_read(uint16_t *avail_mcdram, uint16_t *avail_numa,
			 uint16_t *default_mcdram, uint16_t *default_numa)
{
	char *avail_mcdram_str, *avail_numa_str;
	char *default_mcdram_str, *default_numa_str;
	char *knl_conf_file, *tmp_str = NULL;
	s_p_hashtbl_t *tbl;

	/* Set default values */
	*avail_mcdram = KNL_MCDRAM_FLAG;
	*avail_numa = KNL_NUMA_FLAG;
	*default_mcdram = KNL_CACHE;
	*default_numa = KNL_ALL2ALL;

	knl_conf_file = get_extra_conf_path("knl.conf");
	if ((tbl = _config_make_tbl(knl_conf_file))) {
		if (s_p_get_string(&tmp_str, "AvailMCDRAM", tbl)) {
			*avail_mcdram = knl_mcdram_parse(tmp_str, ",");
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "AvailNUMA", tbl)) {
			*avail_numa = knl_numa_parse(tmp_str, ",");
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "DefaultMCDRAM", tbl)) {
			*default_mcdram = knl_mcdram_parse(tmp_str, ",");
			if (knl_mcdram_bits_cnt(*default_mcdram) != 1) {
				fatal("knl.conf: Invalid DefaultMCDRAM=%s",
				      tmp_str);
			}
			xfree(tmp_str);
		}
		if (s_p_get_string(&tmp_str, "DefaultNUMA", tbl)) {
			*default_numa = knl_numa_parse(tmp_str, ",");
			if (knl_numa_bits_cnt(*default_numa) != 1) {
				fatal("knl.conf: Invalid DefaultNUMA=%s",
				      tmp_str);
			}
			xfree(tmp_str);
		}
	} else {
		error("something wrong with opening/reading knl.conf");
	}
	xfree(knl_conf_file);
	s_p_hashtbl_destroy(tbl);

	avail_mcdram_str = knl_mcdram_str(*avail_mcdram);
	avail_numa_str = knl_numa_str(*avail_numa);
	default_mcdram_str = knl_mcdram_str(*default_mcdram);
	default_numa_str = knl_numa_str(*default_numa);
	if ((*default_mcdram & *avail_mcdram) == 0) {
		fatal("knl.conf: DefaultMCDRAM(%s) not within AvailMCDRAM(%s)",
		      default_mcdram_str, avail_mcdram_str);
	}
	if ((*default_numa & *avail_numa) == 0) {
		fatal("knl.conf: DefaultNUMA(%s) not within AvailNUMA(%s)",
		      default_numa_str, avail_numa_str);
	}
	debug("AvailMCDRAM=%s DefaultMCDRAM=%s",
	     avail_mcdram_str, default_mcdram_str);
	debug("AvailNUMA=%s DefaultNUMA=%s",
	     avail_numa_str, default_numa_str);
	xfree(avail_mcdram_str);
	xfree(avail_numa_str);
	xfree(default_mcdram_str);
	xfree(default_numa_str);

	return SLURM_SUCCESS;
}

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
 * Given a KNL MCDRAM token, return its equivalent numeric value
 * token IN - String to scan
 * RET MCDRAM numeric value
 */
extern uint16_t knl_mcdram_token(char *token)
{
	uint16_t mcdram_num = 0;

	if (!strcasecmp(token, "cache"))
		mcdram_num = KNL_CACHE;
	else if (!strcasecmp(token, "flat"))
		mcdram_num = KNL_FLAT;
	else if (!strcasecmp(token, "hybrid"))
		mcdram_num = KNL_HYBRID;

	return mcdram_num;
}

/*
 * Given a KNL NUMA token, return its equivalent numeric value
 * token IN - String to scan
 * RET NUMA numeric value
 */
extern uint16_t knl_numa_token(char *token)
{
	uint16_t numa_num = 0;

	if (!strcasecmp(token, "all2all"))
		numa_num |= KNL_ALL2ALL;
	else if (!strcasecmp(token, "snc2"))
		numa_num |= KNL_SNC2;
	else if (!strcasecmp(token, "snc4"))
		numa_num |= KNL_SNC4;
	else if (!strcasecmp(token, "hemi"))
		numa_num |= KNL_HEMI;
	else if (!strcasecmp(token, "quad"))
		numa_num |= KNL_QUAD;

	return numa_num;
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
		mcdram_num |= knl_mcdram_token(tok);
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
		numa_num |= knl_numa_token(tok);
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
//		sep = ",";	/* Remove to avoid CLANG error */
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
//		sep = ",";	/* Remove to avoid CLANG error */
	}

	return numa_str;

}

extern int slurm_knl_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *names;
	char *plugin_type = "knl";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	knl_plugin_list = slurm_get_knl_plugins();
	g_context_cnt = 0;
	if ((knl_plugin_list == NULL) || (knl_plugin_list[0] == '\0'))
		goto fini;

	names = knl_plugin_list;
	while ((type = strtok_r(names, ",", &last))) {
		xrealloc(ops, (sizeof(slurm_knl_ops_t) * (g_context_cnt + 1)));
		xrealloc(g_context,
			 (sizeof(plugin_context_t *) * (g_context_cnt + 1)));
		if (strncmp(type, "knl/", 4) == 0)
			type += 4; /* backward compatibility */
		type = xstrdup_printf("knl/%s", type);
		g_context[g_context_cnt] = plugin_context_create(
			plugin_type, type, (void **)&ops[g_context_cnt],
			syms, sizeof(syms));
		if (!g_context[g_context_cnt]) {
			error("cannot create %s context for %s",
			      plugin_type, type);
			rc = SLURM_ERROR;
			xfree(type);
			break;
		}

		xfree(type);
		g_context_cnt++;
		names = NULL; /* for next strtok_r() iteration */
	}
	init_run = true;

fini:
	slurm_mutex_unlock(&g_context_lock);

	if (rc != SLURM_SUCCESS)
		slurm_knl_g_fini();

	return rc;
}

extern int slurm_knl_g_fini(void)
{
	int i, j, rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt < 0)
		goto fini;

	init_run = false;
	for (i = 0; i < g_context_cnt; i++) {
		if (g_context[i]) {
			j = plugin_context_destroy(g_context[i]);
			if (j != SLURM_SUCCESS)
				rc = j;
		}
	}
	xfree(ops);
	xfree(g_context);
	xfree(knl_plugin_list);
	g_context_cnt = -1;

fini:	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

extern int slurm_knl_g_status(char *node_list)
{
	DEF_TIMERS;
	int i, rc;

	START_TIMER;
	rc = slurm_knl_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].status))(node_list);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2("slurm_knl_g_status");

	return rc;
}

extern int slurm_knl_g_boot(char *node_list, char *mcdram_type, char *numa_type)
{
	DEF_TIMERS;
	int i, rc;

	START_TIMER;
	rc = slurm_knl_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; ((i < g_context_cnt) && (rc == SLURM_SUCCESS)); i++)
		rc = (*(ops[i].boot))(node_list, mcdram_type, numa_type);
	slurm_mutex_unlock(&g_context_lock);
	END_TIMER2("slurm_knl_g_boot");

	return rc;
}
