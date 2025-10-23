/*****************************************************************************\
 *  config.c - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2021-2023 Hewlett Packard Enterprise Development LP
 *  Written by Jim Nordby <james.nordby@hpe.com>
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

#include "config.h"

#include <sys/stat.h>

#include "switch_hpe_slingshot.h"
#include "rest.h"

#include "src/common/job_record.h"
#include "src/common/node_conf.h"

/* Set this to true if VNI table is re-sized and loses some bits */
static bool lost_vnis = false;
/* Number of free VNIs */
int free_vnis = 0;

/*
 * Set up slingshot_config defaults
 */
static void _config_defaults(void)
{
	memset(&slingshot_config, 0, sizeof(slingshot_config_t));

	slingshot_config.single_node_vni = SLINGSHOT_SN_VNI_NONE;
	slingshot_config.job_vni = SLINGSHOT_JOB_VNI_NONE;
	slingshot_config.tcs = SLINGSHOT_TC_DEFAULT;
	slingshot_config.flags = SLINGSHOT_FLAGS_DEFAULT;

	slingshot_config.limits.txqs.max = SLINGSHOT_TXQ_MAX;
	slingshot_config.limits.tgqs.max = SLINGSHOT_TGQ_MAX;
	slingshot_config.limits.eqs.max = SLINGSHOT_EQ_MAX;
	slingshot_config.limits.cts.max = SLINGSHOT_CT_MAX;
	slingshot_config.limits.tles.max = SLINGSHOT_TLE_MAX;
	slingshot_config.limits.ptes.max = SLINGSHOT_PTE_MAX;
	slingshot_config.limits.les.max = SLINGSHOT_LE_MAX;
	slingshot_config.limits.acs.max = SLINGSHOT_AC_MAX;

	slingshot_config.limits.txqs.def = SLINGSHOT_TXQ_DEF;
	slingshot_config.limits.tgqs.def = SLINGSHOT_TGQ_DEF;
	slingshot_config.limits.eqs.def = SLINGSHOT_EQ_DEF;
	slingshot_config.limits.cts.def = SLINGSHOT_CT_DEF;
	slingshot_config.limits.tles.def = SLINGSHOT_TLE_DEF;
	slingshot_config.limits.ptes.def = SLINGSHOT_PTE_DEF;
	slingshot_config.limits.les.def = SLINGSHOT_LE_DEF;
	slingshot_config.limits.acs.def = SLINGSHOT_AC_DEF;
	slingshot_config.destroy_retries = SLINGSHOT_CXI_DESTROY_RETRIES;
}

/*
 * Parse the VNI min/max token, with format "vni=<min>-<max>";
 * put results in *minp, *maxp
 */
static bool _config_vnis(const char *token, char *arg, uint16_t *min_ptr,
			 uint16_t *max_ptr)
{
	char *end_ptr;
	long min, max;

	if (!arg)
		goto err;
	end_ptr = NULL;
	min = strtol(arg, &end_ptr, 10);
	if (!end_ptr || (end_ptr == arg) || (*end_ptr != '-'))
		goto err;
	if ((min < SLINGSHOT_VNI_MIN) || (min > SLINGSHOT_VNI_MAX))
		goto err;

	arg = end_ptr + 1;
	end_ptr = NULL;
	max = strtol(arg, &end_ptr, 10);
	if (!end_ptr || (end_ptr == arg) || (*end_ptr != '\0'))
		goto err;
	if ((max <= min) || (max > SLINGSHOT_VNI_MAX))
		goto err;

	*min_ptr = min;
	*max_ptr = max;
	log_flag(SWITCH, "[token=%s]: min/max %ld %ld", token, min, max);
	return true;

err:
	error("Invalid vni token '%s' (example: 'vnis=10-100', valid range %d-%d)",
	      token, SLINGSHOT_VNI_MIN, SLINGSHOT_VNI_MAX);
	return false;
}

/*
 * Compare old slingshot_state.vni_{min,max} with passed-in min/max;
 * if old table is incompatible with new min/max, return false;
 * otherwise set up slingshot_state with new vni_table values
 */
static bool _setup_vni_table(uint16_t min, uint16_t max)
{
	int32_t oldbits, newbits;
	size_t oldsize = slingshot_state.vni_max - slingshot_state.vni_min + 1;
	size_t newsize = max - min + 1;
	uint16_t oldmin = slingshot_state.vni_min;
	uint16_t oldmax = slingshot_state.vni_max;
	bitstr_t *table = slingshot_state.vni_table;

	log_flag(SWITCH, "oldmin/max/size %hu %hu %zu min/max/size %hu %hu %zu",
		 oldmin, oldmax, oldsize, min, max, newsize);

	/* If no recovery of vni_table, just set up new one */
	if (!slingshot_state.vni_table) {
		table = bit_alloc(newsize);
		newbits = 0;	/* no VNIs used */
		goto done;
	}

	xassert(oldmin);
	xassert(oldmax);
	xassert(oldsize > 0);
	xassert(newsize > 0);
	xassert(table);
	xassert(bit_size(table) == oldsize);

	if ((oldmin == min) && (oldmax == max)) {
		newbits = bit_set_count(table);
		goto done;
	}

	/* Re-size bitstring if needed */
	oldbits = bit_set_count(table);
	if (oldsize != newsize)
		table = bit_realloc(table, newsize);

	/* Shift bits if vni_min is changing */
	if (oldmin != min)
		bit_rotate(table, min - oldmin);

	newbits = bit_set_count(table);
	/* Go on even if we're losing VNIs */
	if (newbits != oldbits) {
		warning("changing vni_min/max %hu %hu -> %hu %hu; %d VNIs will be lost!",
			oldmin, oldmax, min, max, oldbits - newbits);
		lost_vnis = true;
	}

done:
	free_vnis = newsize - newbits;
	slingshot_state.vni_min = min;
	slingshot_state.vni_max = max;
	if ((slingshot_state.vni_last < min) ||
	    (slingshot_state.vni_last >= max))
		slingshot_state.vni_last = min - 1;
	slingshot_state.vni_table = table;

	log_flag(SWITCH, "version=%d min/max/last=%hu %hu %hu num_vnis=%zu used=%d free_vnis=%d",
		 slingshot_state.version, slingshot_state.vni_min,
		 slingshot_state.vni_max, slingshot_state.vni_last, newsize,
		 newbits, free_vnis);
	return true;
}

/*
 * Update vni table from config
 */
extern int slingshot_update_vni_table(void)
{
	uint16_t min = slingshot_state.vni_min;
	uint16_t max = slingshot_state.vni_max;
	char *arg = NULL;

	if ((arg = conf_get_opt_str(slurm_conf.switch_param, "vnis="))) {
		if (!_config_vnis("vnis", arg, &min, &max)) {
			xfree(arg);
			return SLURM_ERROR;
		}
	}
	xfree(arg);

	if (!_setup_vni_table(min, max))
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

/* Mapping between Slingshot traffic class labels and their bitmasks */
static struct {
	const char *label;
	uint32_t bit;
} classes[] = {
	{ "DEDICATED_ACCESS", SLINGSHOT_TC_DEDICATED_ACCESS },
	{ "LOW_LATENCY", SLINGSHOT_TC_LOW_LATENCY },
	{ "BULK_DATA", SLINGSHOT_TC_BULK_DATA },
	{ "BEST_EFFORT", SLINGSHOT_TC_BEST_EFFORT },
};
const int num_classes = sizeof(classes) / sizeof(classes[0]);

/*
 * Parse the Slingshot traffic classes token, with format
 * "tcs=<class1>:<class2>[:...]
 */
static bool _config_tcs(const char *token, char *arg, uint32_t *tcs_out)
{
	char *save_ptr = NULL, *tcs = NULL, *tc;
	uint32_t tcbits = 0;
	int i;
	const char prefix[] = "TC_";
	const size_t prefix_siz = sizeof(prefix) - 1;

	if (!arg)
		goto err;
	tcs = xstrdup(arg);
	for (tc = strtok_r(tcs, ":", &save_ptr); tc;
		tc = strtok_r(NULL, ":", &save_ptr)) {

		/* Skip optional TC_ prefix */
		if (!xstrncasecmp(tc, prefix, prefix_siz))
			tc += prefix_siz;

		for (i = 0; i < num_classes; i++) {
			if (!xstrcasecmp(tc, classes[i].label)) {
				tcbits |= classes[i].bit;
				break;
			}
		}
		if (i == num_classes)
			goto err;
	}
	if (tcbits == 0)
		goto err;

	*tcs_out = tcbits;
	log_flag(SWITCH, "[token=%s]: tcs %#x", token, tcbits);
	xfree(tcs);
	return true;

err:
	xfree(tcs);
	error("Invalid traffic class token '%s' (example 'tcs=DEDICATED_ACCESS:LOW_LATENCY:BULK_DATA:BEST_EFFORT')",
	      token);
	return false;
}

/*
 * Parse the Slingshot job VNI token, with format "job_vni={all,user,none}"
 */
static bool _config_job_vni(const char *token, char *arg)
{
	/* Backwards compatibility: no argument = SLINGSHOT_JOB_VNI_ALL */
	if (!arg) {
		slingshot_config.job_vni = SLINGSHOT_JOB_VNI_ALL;
		goto out;
	}
	if (!xstrcasecmp(arg, "all"))
		slingshot_config.job_vni = SLINGSHOT_JOB_VNI_ALL;
	else if (!xstrcasecmp(arg, "user"))
		slingshot_config.job_vni = SLINGSHOT_JOB_VNI_USER;
	else if (!xstrcasecmp(arg, "none"))
		slingshot_config.job_vni = SLINGSHOT_JOB_VNI_NONE;
	else {
		error("Invalid job_vni token '%s' (example 'job_vni={all,user,none}')",
		      token);
		return false;
	}

out:
	log_flag(SWITCH, "[token=%s]: job_vni %d",
		 token, slingshot_config.job_vni);
	return true;
}

/*
 * Parse the Slingshot single-node VNI token, with format
 * "single_node_vni={all,user,none}"
 */
static bool _config_single_node_vni(const char *token, char *arg)
{
	/* Backwards compatibility: no argument = SLINGSHOT_SN_VNI_ALL */
	if (!arg) {
		slingshot_config.single_node_vni = SLINGSHOT_SN_VNI_ALL;
		goto out;
	}
	if (!xstrcasecmp(arg, "all"))
		slingshot_config.single_node_vni = SLINGSHOT_SN_VNI_ALL;
	else if (!xstrcasecmp(arg, "user"))
		slingshot_config.single_node_vni = SLINGSHOT_SN_VNI_USER;
	else if (!xstrcasecmp(arg, "none"))
		slingshot_config.single_node_vni = SLINGSHOT_SN_VNI_NONE;
	else {
		error("Invalid single_node_vni token '%s' (example 'single_node_vni={all,user,none}')",
		      token);
		return false;
	}

out:
	log_flag(SWITCH, "[token=%s]: single_node_vni %d",
		 token, slingshot_config.single_node_vni);
	return true;
}

/*
 * Parse the "hwcoll_addrs_per_job" token, with format
 * "hwcoll_addrs_per_job=<number>"
 */
static bool _config_hwcoll_addrs_per_job(const char *token, char *arg)
{
	char *end_ptr = NULL;
        uint32_t num;

	if (!arg)
		goto err;
	num = strtol(arg, &end_ptr, 10);
	if (!end_ptr || (end_ptr == arg))
		goto err;
        if (num <= 0)
            goto err;
	slingshot_config.hwcoll_addrs_per_job = num;

	log_flag(SWITCH, "[token=%s]: hwcoll_addrs_per_job %u",
		 token, slingshot_config.hwcoll_addrs_per_job);
	return true;
err:
	error("Invalid hwcoll_addrs_per_job token '%s' (example 'hwcoll_addrs_per_job=26')",
	      token);
	return false;
}

/*
 * Parse the "hwcoll_num_nodes" token, with format "hwcoll_num_nodes=<number>"
 */
static bool _config_hwcoll_num_nodes(const char *token, char *arg)
{
	char *end_ptr = NULL;
        uint32_t num;

	if (!arg)
		goto err;
	num = strtol(arg, &end_ptr, 10);
	if (!end_ptr || (end_ptr == arg))
		goto err;
        if (num <= 0)
            goto err;
	slingshot_config.hwcoll_num_nodes = num;

	log_flag(SWITCH, "[token=%s]: hwcoll_num_nodes %u",
		 token, slingshot_config.hwcoll_num_nodes);
	return true;
err:
	error("Invalid hwcoll_num_nodes token '%s' (example 'hwcoll_num_nodes=64')",
	      token);
	return false;
}

/*
 * Parse the "fm_url" token, with format "fm_url=<url>"
 */
static bool _config_fm_url(const char *token, char *arg)
{
	if (!arg)
		goto err;
	slingshot_config.fm_url = xstrdup(arg);

	log_flag(SWITCH, "[token=%s]: fm_url %s",
		 token, slingshot_config.fm_url);
	return true;
err:
	error("Invalid fm_url token '%s' (example 'fm_url=https://api-gw-service-nmn.local/apis/fm')",
	      token);
	return false;
}

/*
 * Parse the "fm_auth" token, with format "fm_auth={BASIC,OAUTH}"
 */
static bool _config_fm_auth(const char *token, char *arg)
{
	if (!arg)
		goto err;
	if (!xstrcasecmp(arg, SLINGSHOT_AUTH_BASIC_STR))
		slingshot_config.fm_auth = SLINGSHOT_AUTH_BASIC;
	else if (!xstrcasecmp(arg, SLINGSHOT_AUTH_OAUTH_STR))
		slingshot_config.fm_auth = SLINGSHOT_AUTH_OAUTH;
	else
		goto err;

	log_flag(SWITCH, "[token=%s]: fm_auth %d",
		 token, slingshot_config.fm_auth);
	return true;
err:
	error("Invalid fm_auth token '%s' (example 'fm_auth={BASIC,OAUTH}')",
	      token);
	return false;
}

/*
 * Parse the "fm_authdir" token, with format "fm_authdir=<dirpath>"
 */
static bool _config_fm_authdir(const char *token, char *arg)
{
	struct stat statbuf;

	if (!arg)
		goto err;
	if (stat(arg, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode)) {
		error("fm_authdir directory '%s' is not a directory", arg);
		return false;
	}
	slingshot_config.fm_authdir = xstrdup(arg);

	log_flag(SWITCH, "[token=%s]: fm_authdir %s",
		 token, slingshot_config.fm_authdir);
	return true;
err:
	error("Invalid fm_authdir token '%s' (example 'fm_authdir=/etc/wlm-client-auth')",
	      token);
	return false;
}

/* Verify that the path is a fully qualified and readable */
static bool _is_valid_file(const char *token, char *path, char *option,
			   char *example)
{
	struct stat statbuf;

	if (!path) {
		error("Invalid %s token '%s' (example '%s=%s')",
		      option, token, option, example);
	} else if (path[0] != '/') {
		error("%s's path (%s) is required to be a fully qualified pathname",
		      option, path);
	} else if ((stat(path, &statbuf) != 0)) {
		error("%s's path (%s) can not be accessed or it doesn't exist",
		      option, path);
	} else if (!S_ISREG(statbuf.st_mode)) {
		error("%s's path (%s) is not a regular file", option, path);
	} else if (access(path, R_OK)) {
		error("%s's path (%s) does not have read permissions",
		      option, path);
	} else {
		return true;
	}

	return false;
}

/*
 * Parse the "fm_mtls_ca" token"
 */
static bool _config_fm_mtls_ca(const char *token, char *arg)
{
	if (!_is_valid_file(token, arg, "fm_mtls_ca",
			    "/etc/wlm-client-auth/ca.crt"))
		return false;

	slingshot_config.fm_mtls_ca = xstrdup(arg);

	log_flag(SWITCH, "[token=%s]: fm_mtls_ca %s", token,
		 slingshot_config.fm_mtls_ca);
	return true;
}

/*
 * Parse the "fm_mtls_cert" token"
 */
static bool _config_fm_mtls_cert(const char *token, char *arg)
{
	if (!_is_valid_file(token, arg, "fm_mtls_cert",
			    "/etc/wlm-client-auth/public.crt"))
		return false;

	slingshot_config.fm_mtls_cert = xstrdup(arg);

	log_flag(SWITCH, "[token=%s]: fm_mtls_cert %s", token,
		 slingshot_config.fm_mtls_cert);
	return true;
}

/*
 * Parse the "fm_mtls_key" token"
 */
static bool _config_fm_mtls_key(const char *token, char *arg)
{
	if (!_is_valid_file(token, arg, "fm_mtls_key",
			    "/etc/wlm-client-auth/private.key"))
		return false;

	slingshot_config.fm_mtls_key = xstrdup(arg);

	log_flag(SWITCH, "[token=%s]: fm_mtls_key %s", token,
		 slingshot_config.fm_mtls_key);
	return true;
}

/*
 * Parse the "fm_mtls_url" token"
 */
static bool _config_fm_mtls_url(const char *token, char *arg)
{
	if (!arg)
		goto err;
	slingshot_config.fm_mtls_url = xstrdup(arg);

	log_flag(SWITCH, "[token=%s]: fm_mtls_url %s", token,
		 slingshot_config.fm_mtls_url);
	return true;
err:
	error("Invalid fm_mtls_url token '%s' (example 'fm_mtls_url=https://api-gw-service-nmn.local/apis/fm')",
	      token);
	return false;
}

/*
 * If fm_url is set, set up default values for fm_auth{dir}
 * (if not already set)
 */
static void _config_fm_defaults(void)
{
	if (!slingshot_config.fm_url)
		return;
	if (slingshot_config.fm_auth == SLINGSHOT_AUTH_NONE)
		slingshot_config.fm_auth = SLINGSHOT_AUTH_OAUTH;
	if (!slingshot_config.fm_authdir) {
		if (slingshot_config.fm_auth == SLINGSHOT_AUTH_OAUTH)
			slingshot_config.fm_authdir =
					xstrdup(SLINGSHOT_FM_AUTH_OAUTH_DIR);
		else if (slingshot_config.fm_auth == SLINGSHOT_AUTH_BASIC)
			slingshot_config.fm_authdir =
					xstrdup(SLINGSHOT_FM_AUTH_BASIC_DIR);
	}
	xassert(slingshot_config.fm_authdir);
}

static void _try_enabling_fm_mtls(void)
{
	/* Only enable mTLS if the following were provided */
	if (!(slingshot_config.fm_mtls_cert && slingshot_config.fm_mtls_key &&
	      (slingshot_config.fm_mtls_url || slingshot_config.fm_url))) {
		/* If only partial mTLS config given warn that it is disabled */
		if (slingshot_config.fm_mtls_ca ||
		    slingshot_config.fm_mtls_cert ||
		    slingshot_config.fm_mtls_key ||
		    slingshot_config.fm_mtls_url)
			warning("Fabric Manager mTLS authentication is disabled due to fm_mtls_cert, fm_mtls_key, or a fabric manager url (i.e. fm_mtls_url or fm_url) not being configured.");
		return;
	}
	/* If fm_mtls_url is not provided then default to fm_url */
	if (!slingshot_config.fm_mtls_url)
		slingshot_config.fm_mtls_url = xstrdup(slingshot_config.fm_url);

	slingshot_config.flags |= SLINGSHOT_FLAGS_ENABLE_MTLS;

	if (!slingshot_config.fm_mtls_ca)
		warning("Fabric Manager mTLS authentication is enabled but a certification bundle was not provided. Server identity will not be verified.");
}

static int _config_destroy_retries(const char *token, char *arg)
{
	uint32_t tmp;
	char *end_ptr = NULL;

	if (!arg)
		return SLURM_ERROR;

	errno = 0;
	tmp = strtoul(arg, &end_ptr, 10);
	if ((errno != 0) || *end_ptr) {
		error("Invalid value for %s", token);
		return SLURM_ERROR;
	}

	slingshot_config.destroy_retries = tmp;

	log_flag(SWITCH, "[token=%s]: destroy_retries = %u",
		 token, slingshot_config.destroy_retries);

	return SLURM_SUCCESS;
}

/*
 * Mapping between Slingshot limit names, slingshot_limits_set_t offset, maximum
 * values
 */
typedef struct limits_table {
	const char *name;
	size_t offset;
	int max;
} limits_table_t;
static limits_table_t limits_table[] = {
	{ "txqs", offsetof(slingshot_limits_set_t, txqs), SLINGSHOT_TXQ_MAX },
	{ "tgqs", offsetof(slingshot_limits_set_t, tgqs), SLINGSHOT_TGQ_MAX },
	{ "eqs",  offsetof(slingshot_limits_set_t, eqs),  SLINGSHOT_EQ_MAX },
	{ "cts",  offsetof(slingshot_limits_set_t, cts),  SLINGSHOT_CT_MAX },
	{ "tles", offsetof(slingshot_limits_set_t, tles), SLINGSHOT_TLE_MAX },
	{ "ptes", offsetof(slingshot_limits_set_t, ptes), SLINGSHOT_PTE_MAX },
	{ "les",  offsetof(slingshot_limits_set_t, les),  SLINGSHOT_LE_MAX },
	{ "acs",  offsetof(slingshot_limits_set_t, acs),  SLINGSHOT_AC_MAX },
};
static const int num_limits = sizeof(limits_table) / sizeof(limits_table[0]);
static const char *all_limits = "txqs,tgqs,eqs,cts,tles,ptes,les,acs";

/*
 * Check whether the token is a Slingshot resource limit token,
 * with format "{def,res,max}_{name}=<limit>"; update slingshot_config
 */
static bool _config_limits(const char *token, slingshot_limits_set_t *limits)
{
	char *tok, *arg, *end_ptr;
	const char *name, *typestr;
	enum { DEF = 1, RES, MAX } type;
	int i, limit;
	const char def_str[] = "def_";
	const size_t def_siz = sizeof(def_str) - 1;
	const char res_str[] = "res_";
	const size_t res_siz = sizeof(res_str) - 1;
	const char max_str[] = "max_";
	const size_t max_siz = sizeof(max_str) - 1;
	limits_table_t *entry;
	slingshot_limits_t *limit_ptr;

	tok = xstrdup(token);
	if (!(arg = strchr(tok, '=')))
		goto err;
	*arg = '\0';	/* null-terminate limit name */
	arg++;
	/* Parse "{def,res,max}_" prefix */
	if (!xstrncmp(tok, def_str, def_siz)) {
		type = DEF;
		typestr = "def";
		name = tok + def_siz;
	} else if (!xstrncmp(tok, res_str, res_siz)) {
		type = RES;
		typestr = "res";
		name = tok + res_siz;
	} else if (!xstrncmp(tok, max_str, max_siz)) {
		type = MAX;
		typestr = "max";
		name = tok + max_siz;
	} else {
		goto err;
	}
	/* Now find the limit type and point entry at the limit_table slot */
	entry = NULL;
	for (i = 0; i < num_limits; i++) {
		if (!xstrcmp(name, limits_table[i].name)) {
			entry = &limits_table[i];
			break;
		}
	}
	if (!entry)
		goto err;
	end_ptr = NULL;
	limit = strtol(arg, &end_ptr, 10);
	if (!end_ptr || (end_ptr == arg) || (*end_ptr != '\0'))
		goto err;
	if ((limit < 0) || (limit > entry->max)) {
		error("Invalid limit token '%s': invalid limit %d (valid range 0-%d)",
		      token, limit, entry->max);
		goto out;
	}
	limit_ptr = (slingshot_limits_t *)(((void *) limits) + entry->offset);
	if (type == DEF) {
		limit_ptr->def = limit;
	} else if (type == RES) {
		limit_ptr->res = limit;
	} else if (type == MAX) {
		limit_ptr->max = limit;
	}
	log_flag(SWITCH, "[token=%s]: limits[%d].%s.%s %d",
		 token, i, entry->name, typestr, limit);
	xfree(tok);
	return true;
err:
	error("Invalid limit token '%s' (example {max,res,def}_{%s})",
	      token, all_limits);
out:
	xfree(tok);
	return false;
}

static void _print_limits(slingshot_limits_set_t *limits)
{
#define DEBUG_LIMIT(SET, LIM) \
	debug("%s: max/res/def %hu %hu %hu", \
		#LIM, SET->LIM.max, SET->LIM.res, SET->LIM.def);
	DEBUG_LIMIT(limits, txqs);
	DEBUG_LIMIT(limits, tgqs);
	DEBUG_LIMIT(limits, eqs);
	DEBUG_LIMIT(limits, cts);
	DEBUG_LIMIT(limits, tles);
	DEBUG_LIMIT(limits, ptes);
	DEBUG_LIMIT(limits, les);
	DEBUG_LIMIT(limits, acs);
#undef DEBUG_LIMIT
}

/*
 * Free any configuration memory
 */
extern void slingshot_free_config(void)
{
	xfree(slingshot_config.fm_url);
	xfree(slingshot_config.fm_authdir);
	xfree(slingshot_config.fm_mtls_ca);
	xfree(slingshot_config.fm_mtls_cert);
	xfree(slingshot_config.fm_mtls_key);
	xfree(slingshot_config.fm_mtls_url);
}

extern bool slingshot_stepd_init(const char *switch_params)
{
	char *params = NULL, *token, *arg, *save_ptr = NULL;
	const char destroy_retries[] = "destroy_retries";
	const size_t size_destroy_retries = sizeof(destroy_retries) - 1;

	slingshot_config.destroy_retries = SLINGSHOT_CXI_DESTROY_RETRIES;

	if (!switch_params)
		return true;

	params = xstrdup(switch_params);
	for (token = strtok_r(params, ",", &save_ptr); token;
	     token = strtok_r(NULL, ",", &save_ptr)) {
		if ((arg = strchr(token, '=')))
			arg++; /* points to argument after = if any */
		if (!xstrncasecmp(token, destroy_retries,
				  size_destroy_retries)) {
			if (_config_destroy_retries(token, arg))
				goto err;
		}
	}

	xfree(params);
	return true;
err:
	xfree(params);
	return false;
}

/*
 * Set up passed-in slingshot_config_t based on values in 'SwitchParameters'
 * slurm.conf setting.  Return true on success, false on bad parameters
 */
extern bool slingshot_setup_config(const char *switch_params)
{
	char *params = NULL, *token, *arg, *save_ptr = NULL;
	const char destroy_retries[] = "destroy_retries";
	const size_t size_destroy_retries = sizeof(destroy_retries) - 1;
	const char vnis[] = "vnis";
	const size_t size_vnis = sizeof(vnis) - 1;
	const char tcs[] = "tcs";
	const size_t size_tcs = sizeof(tcs) - 1;
	const char job_vni[] = "job_vni";
	const size_t size_job_vni = sizeof(job_vni) - 1;
	const char single_node_vni[] = "single_node_vni";
	const size_t size_single_node_vni = sizeof(single_node_vni) - 1;
	const char adjust_limits[] = "adjust_limits";
	const size_t size_adjust_limits = sizeof(adjust_limits) - 1;
	const char no_adjust_limits[] = "no_adjust_limits";
	const size_t size_no_adjust_limits = sizeof(no_adjust_limits) - 1;
	const char hwcoll_addrs_per_job[] = "hwcoll_addrs_per_job";
	const size_t size_hwcoll_addrs_per_job =
                                        sizeof(hwcoll_addrs_per_job) - 1;
	const char hwcoll_num_nodes[] = "hwcoll_num_nodes";
	const size_t size_hwcoll_num_nodes = sizeof(hwcoll_num_nodes) - 1;
	const char fm_url[] = "fm_url";
	const size_t size_fm_url = sizeof(fm_url) - 1;
	const char fm_auth[] = "fm_auth";
	const size_t size_fm_auth = sizeof(fm_auth) - 1;
	const char fm_authdir[] = "fm_authdir";
	const size_t size_fm_authdir = sizeof(fm_authdir) - 1;
	const char fm_mtls_ca[] = "fm_mtls_ca";
	const size_t size_fm_mtls_ca = sizeof(fm_mtls_ca) - 1;
	const char fm_mtls_cert[] = "fm_mtls_cert";
	const size_t size_fm_mtls_cert = sizeof(fm_mtls_cert) - 1;
	const char fm_mtls_key[] = "fm_mtls_key";
	const size_t size_fm_mtls_key = sizeof(fm_mtls_key) - 1;
	const char fm_mtls_url[] = "fm_mtls_url";
	const size_t size_fm_mtls_url = sizeof(fm_mtls_url) - 1;
	/* Will be default size when SwitchParameters is not set */
	uint16_t vni_min = slingshot_state.vni_min;
	uint16_t vni_max = slingshot_state.vni_max;
	bool vni_range_set = false;

	/*
	 * Handle SwitchParameters values (separated by commas):
	 *
	 *   vnis=<start>-<end> (e.g. vnis=1-16000)
	 *   tcs=<tc_list> (e.g. tcs=BULK_DATA:BEST_EFFORT)
	 *   single_node_vni=<all,none,user>: allocate VNI for all single-node
	 *     jobs, no single-node jobs, or only on user request
	 *     (via srun --network=single_node_vni)
	 *   job_vni=<all,none,user>: allocate additional VNI per-job for
	 *     all jobs, no jobs, or only on user request
	 *     (via srun --network=job_vni)
	 *   {no_}adjust_limits: {don't} adjust resource reservations
	 *     for each NIC by subtracting resources already
	 *     used/reserved by system services
	 *     (i.e. /etc/jackaloped for BASIC, /etc/wlm-client-auth for OAUTH)
	 *   hwcoll_addrs_per_job=<number>: allocate <number> of Slingshot
         *     hardware collectives multicast addresses per job
         *     (that are larger than <hwcoll_min_nodes> nodes)
	 *   hwcoll_num_nodes=<num_nodes>: minimum number of nodes for a
         *     job to be allocated Slingshot hardware collectives
	 *   fm_url=<url>: use URL for fabric manager REST requests
	 *   fm_auth="BASIC|OAUTH": fabric manager REST API authentication type
	 *   fm_authdir=<dir>: fabric manager authentication info directory
	 *     (i.e. /etc/fmsim for BASIC, /etc/wlm-client-auth for OAUTH)
	 *   fm_mtls_ca=<path to FM certificate bundle>
	 *   fm_mtls_cert=<path to client public certificate>
	 *   fm_mtls_key=<path to client private key>
	 *   fm_mtls_url=<url for mTLS authentication to FM>
	 *
	 *   def_<NIC_resource>: default per-thread value for resource
	 *   res_<NIC_resource>: reserved value for resource
	 *   max_<NIC_resource>: maximum value for resource
	 *
	 * NIC resources are:
	 *   txqs: transmit command queues
	 *   tgqs: target command queues
	 *   eqs:  events queues
	 *   cts:  counters
	 *   tles: trigger list entries
	 *   ptes: portable table entries
	 *   les:  list entries
	 *   acs:  addressing contexts
	 */

	slingshot_free_config();
	_config_defaults();
	if (!switch_params)
		goto out;

	log_flag(SWITCH, "switch_params=%s", switch_params);

	params = xstrdup(switch_params);
	for (token = strtok_r(params, ",", &save_ptr); token;
		token = strtok_r(NULL, ",", &save_ptr)) {
		if ((arg = strchr(token, '=')))
			arg++;	/* points to argument after = if any */
		if (!xstrncasecmp(token, vnis, size_vnis)) {
			if (!_config_vnis(token, arg, &vni_min, &vni_max))
				goto err;
			/* See if any incompatible changes in VNI range */
			if (!_setup_vni_table(vni_min, vni_max))
				goto err;
			vni_range_set = true;
		} else if (!xstrncasecmp(token, tcs, size_tcs)) {
			if (!_config_tcs(token, arg, &slingshot_config.tcs))
				goto err;
		} else if (!xstrncasecmp(token, job_vni, size_job_vni)) {
			if (!_config_job_vni(token, arg))
				goto err;
		} else if (!xstrncasecmp(token, single_node_vni,
					 size_single_node_vni)) {
			if (!_config_single_node_vni(token, arg))
				goto err;
		} else if (!xstrncasecmp(token, adjust_limits,
					 size_adjust_limits)) {
			slingshot_config.flags |= SLINGSHOT_FLAGS_ADJUST_LIMITS;
		} else if (!xstrncasecmp(token, no_adjust_limits,
					 size_no_adjust_limits)) {
			slingshot_config.flags &=
					~(SLINGSHOT_FLAGS_ADJUST_LIMITS);
		} else if (!xstrncasecmp(token, hwcoll_addrs_per_job,
                                         size_hwcoll_addrs_per_job)) {
			if (!_config_hwcoll_addrs_per_job(token, arg))
				goto err;
		} else if (!xstrncasecmp(token, hwcoll_num_nodes,
					 size_hwcoll_num_nodes)) {
			if (!_config_hwcoll_num_nodes(token, arg))
				goto err;
		} else if (!xstrncasecmp(token, fm_url, size_fm_url)) {
			if (!_config_fm_url(token, arg))
				goto err;
		/*
		 * NOTE: fm_authdir needs to come before fm_auth
		 * since fm_auth is a prefix of fm_authdir
		 */
		} else if (!xstrncasecmp(token, fm_authdir, size_fm_authdir)) {
			if (!_config_fm_authdir(token, arg))
				goto err;
		} else if (!xstrncasecmp(token, fm_auth, size_fm_auth)) {
			if (!_config_fm_auth(token, arg))
				goto err;
		} else if (!xstrncasecmp(token, fm_mtls_ca, size_fm_mtls_ca)) {
			if (!_config_fm_mtls_ca(token, arg))
				goto err;
		} else if (!xstrncasecmp(token, fm_mtls_cert,
					 size_fm_mtls_cert)) {
			if (!_config_fm_mtls_cert(token, arg))
				goto err;
		} else if (!xstrncasecmp(token, fm_mtls_key,
					 size_fm_mtls_key)) {
			if (!_config_fm_mtls_key(token, arg))
				goto err;
		} else if (!xstrncasecmp(token, fm_mtls_url,
					 size_fm_mtls_url)) {
			if (!_config_fm_mtls_url(token, arg))
				goto err;
		} else if (!xstrncasecmp(token, destroy_retries,
					 size_destroy_retries)) {
			if (_config_destroy_retries(token, arg))
				goto err;
		} else {
			if (!_config_limits(token, &slingshot_config.limits))
				goto err;
		}
	}
	/* If fm_url is set, set up default values for fm_auth{dir} */
	_config_fm_defaults();

	_try_enabling_fm_mtls();

	/* Set up connection to fabric manager */
	if (!slingshot_init_collectives())
		goto err;

out:
	if (!vni_range_set && !_setup_vni_table(vni_min, vni_max))
		goto err;

	debug("single_node_vni=%d job_vni=%d tcs=%#x flags=%#x",
	      slingshot_config.single_node_vni, slingshot_config.job_vni,
	      slingshot_config.tcs, slingshot_config.flags);
	debug("fm_url=%s fm_auth=%u fm_authdir=%s hwcoll_addrs_per_job=%d hwcoll_num_nodes=%d",
              slingshot_config.fm_url, slingshot_config.fm_auth,
	      slingshot_config.fm_authdir,
	      slingshot_config.hwcoll_addrs_per_job,
	      slingshot_config.hwcoll_num_nodes);
	_print_limits(&slingshot_config.limits);

	xfree(params);
	return true;

err:
	error("SwitchParameters parsing encountered errors, exiting");
	xfree(params);
	return false;
}

/*
 * Allocate a free VNI (range vni_min... vni_max, starting at vni_last + 1)
 * Return true with *vnip filled in on success, false on failure
 */
static bool _alloc_vni(uint16_t *vnip)
{
	bitoff_t start, end, bit;
	uint16_t vni;

	/* Search for clear bit from [vni_last + 1...vni_max] */
	start = (slingshot_state.vni_last + 1) - slingshot_state.vni_min;
	end = slingshot_state.vni_max - slingshot_state.vni_min;
	xassert(start >= 0);
	log_flag(SWITCH, "upper bits: start/end %zu %zu", start, end);
	for (bit = start; bit <= end; bit++) {
		if (!bit_test(slingshot_state.vni_table, bit))
			goto gotvni;
	}
	/* Search for clear bit from [vni_min...vni_last] */
	start = 0;
	end = slingshot_state.vni_last - slingshot_state.vni_min;
	log_flag(SWITCH, "lower bits: start/end %zu %zu", start, end);
	for (bit = start; bit <= end; bit++) {
		if (!bit_test(slingshot_state.vni_table, bit))
			goto gotvni;
	}
	/* TODO: developer's mode: check for no bits set? */
	error("Cannot allocate VNI (min/max/last %hu %hu %hu)",
	      slingshot_state.vni_min, slingshot_state.vni_max,
	      slingshot_state.vni_last);
	return false;

gotvni:
	bit_set(slingshot_state.vni_table, bit);
	xassert(bit + slingshot_state.vni_min <= SLINGSHOT_VNI_MAX);
	vni = bit + slingshot_state.vni_min;
	slingshot_state.vni_last = vni;
	free_vnis--;
	xassert(free_vnis >= 0);
	log_flag(SWITCH, "min/max/last %hu %hu %hu vni=%hu free_vnis=%d",
		 slingshot_state.vni_min, slingshot_state.vni_max,
		 slingshot_state.vni_last, vni, free_vnis);
	*vnip = vni;
	return true;
}

/*
 * Allocate a per-job inter-job-step VNI.
 * If this is the first allocation for this job ID,
 * allocate a new VNI and add it to the job_vnis table;
 * otherwise return the VNI from the table for this job ID
 * Return true with *vnip filled in on success, false on failure
 */
static bool _alloc_job_vni(uint32_t job_id, uint16_t *vnip)
{
	int i, freeslot = -1;
	job_vni_t *jobvni;

	/*
	 * Check if this jobID is in the table already:
	 * if so, fill in *vnip and return true;
	 * otherwise, save the first free index into the table.
	 */
	for (i = 0; i < slingshot_state.num_job_vnis; i++) {
		jobvni = &slingshot_state.job_vnis[i];
		if (jobvni->job_id == job_id) {
			log_flag(SWITCH,
				 "[job_id=%u]: found job_vnis[%d] vni=%hu num_job_vnis=%d",
				 job_id, i, jobvni->vni,
				 slingshot_state.num_job_vnis);
			*vnip = jobvni->vni;
			return true;
		} else if (jobvni->job_id == 0 && freeslot < 0) {
			freeslot = i;
		}
	}

	/* Allocate VNI from bitmap */
	if (!_alloc_vni(vnip))
		return false;

	/* If no free slot, allocate a new slot in the job_vnis table */
	if (freeslot < 0) {
		freeslot = slingshot_state.num_job_vnis;
		slingshot_state.num_job_vnis++;
		xrecalloc(slingshot_state.job_vnis,
			slingshot_state.num_job_vnis, sizeof(job_vni_t));
	}
	slingshot_state.job_vnis[freeslot].job_id = job_id;
	slingshot_state.job_vnis[freeslot].vni = *vnip;
	log_flag(SWITCH, "[job_id=%u]: new vni[%d] vni=%hu num_job_vnis=%d",
		 job_id, freeslot, *vnip, slingshot_state.num_job_vnis);
	return true;
}

/*
 * Free an allocated VNI
 */
static void _free_vni(uint16_t vni)
{
	/*
	 * Range-check VNI, but only if table has been re-sized and VNIs
	 * were lost
	 */
	if (lost_vnis &&
	    ((vni < slingshot_state.vni_min) ||
	     (vni > slingshot_state.vni_max))) {
		info("vni %hu: not in current table min/max %hu-%hu",
			vni, slingshot_state.vni_min, slingshot_state.vni_max);
		return;
	}
	bitoff_t bit = vni - slingshot_state.vni_min;
	if (!bit_test(slingshot_state.vni_table, bit)) {
		log_flag(SWITCH, "vni %hu: bit %zu not set in vni_table!",
			 vni, bit);
		return;
	}
	bit_clear(slingshot_state.vni_table, bit);
	free_vnis++;
	xassert(free_vnis <=
		(slingshot_state.vni_max - slingshot_state.vni_min) + 1);
	log_flag(SWITCH, "[vni=%hu]: bit %zu", vni, bit);
}

/*
 * Free an allocated per-job "user" VNI
 * Return the VNI, or 0 if not found
 */
static uint16_t _free_job_vni(uint32_t job_id)
{
	int i;
	job_vni_t *jobvni = NULL;

	/*
	 * Find the jobID/vni in the job_vnis table;
	 * zero-out the slot in the table
	 */
	for (i = 0; i < slingshot_state.num_job_vnis; i++) {
		jobvni = &slingshot_state.job_vnis[i];
		if (jobvni->job_id == job_id) {
			uint16_t vni = jobvni->vni;
			_free_vni(vni);
			log_flag(SWITCH,
				 "[job_id=%u]: free job_vnis[%d] vni=%hu num_job_vnis=%d free_vnis=%d",
				 jobvni->job_id, i, vni,
				 slingshot_state.num_job_vnis, free_vnis);
			memset(jobvni, 0, sizeof(*jobvni));
			return vni;
		}
	}
	if (slingshot_state.num_job_vnis > 0)
		log_flag(SWITCH, "job_id=%u: not found in job_vnis[%d]",
			 job_id, slingshot_state.num_job_vnis);
	return 0;
}

/*
 * Parse --network 'depth=<value>' token: return value, or 0 on error
 */
static uint32_t _setup_depth(const char *token)
{
	uint32_t ret;
	char *arg = strchr(token, '=');
	if (!arg)
		goto err;
	arg++;	/* point to argument */
	char *end_ptr = NULL;
	ret = strtol(arg, &end_ptr, 10);
	if (*end_ptr || ret < 1 || ret > 1024)
		goto err;
	log_flag(SWITCH, "[token=%s]: depth %u", token, ret);
	return ret;
err:
	error("Invalid depth token '%s' (valid range %d-%d)", token, 1, 1024);
	return 0;
}

/*
 * Parse a single comma-separated part of the --network option
 *
 *   depth: value to be used for threads-per-rank
 *   job_vni: allocate a job VNI for this job
 *   single_node_vni: allocate a VNI for this job even if single-node
 *   no_vni: _don't_ allocate a VNI for this job even if multi-node
 *   {no_}adjust_limits: {don't} adjust resource limit reservations
 *     by subtracting system service reserved/used values
 *   disable_rdzv_get: disable rendezvous gets
 *   tcs: set of traffic classes (job only)
 *   def_<NIC_resource>: default per-thread value for resource
 *   res_<NIC_resource>: reserved value for resource
 *   max_<NIC_resource>: maximum value for resource
 */
static bool _parse_network_token(const char *token, bool is_job,
				 slingshot_stepinfo_t *job,
				 bool *job_vni, bool *single_node_vni,
				 bool *no_vni)
{
	char depth_str[] = "depth";
	size_t depth_siz = sizeof(depth_str) - 1;
	char job_vni_str[] = "job_vni";
	size_t job_vni_siz = sizeof(job_vni_str) - 1;
	char single_node_vni_str[] = "single_node_vni";
	size_t single_node_vni_siz = sizeof(single_node_vni_str) - 1;
	char no_vni_str[] = "no_vni";
	size_t no_vni_siz = sizeof(no_vni_str) - 1;
	char adjust_limits_str[] = "adjust_limits";
	size_t adjust_limits_siz = sizeof(adjust_limits_str) - 1;
	char no_adjust_limits_str[] = "no_adjust_limits";
	size_t no_adjust_limits_siz = sizeof(no_adjust_limits_str) - 1;
	char rdzv_get_str[] = "disable_rdzv_get";
	size_t rdzv_get_siz = sizeof(rdzv_get_str) - 1;
	char hwcoll_str[] = "hwcoll";
	size_t hwcoll_siz = sizeof(hwcoll_siz) - 1;
	char tcs_str[] = "tcs";
	size_t tcs_siz = sizeof(tcs_str) - 1;

	char *arg = xstrchr(token, '=');
	if (arg != NULL)
		arg++;

	if (!xstrncmp(token, depth_str, depth_siz)) {
		if ((job->depth = _setup_depth(token)) == 0)
			return false;
	} else if (!xstrncmp(token, job_vni_str, job_vni_siz)) {
		if (token[job_vni_siz] != '\0') {
			error("Invalid job_vni token '%s'", token);
			return false;
		} else if (slingshot_config.job_vni == SLINGSHOT_JOB_VNI_NONE) {
			error("Job VNI requested by user, but 'job_vni=<all|user>' not set in SwitchParameters");
			return false;
		} else {
			*job_vni = true;
		}
	} else if (!xstrncmp(token, single_node_vni_str, single_node_vni_siz)) {
		if (token[single_node_vni_siz] != '\0') {
			error("Invalid single_node_vni token '%s'", token);
			return false;
		} else if (slingshot_config.single_node_vni ==
			   SLINGSHOT_SN_VNI_NONE) {
			error("Single-node VNI requested by user, but 'single_node_vni=<all|user>' not set in SwitchParameters");
			return false;
		} else {
			*single_node_vni = true;
		}
	} else if (!xstrncmp(token, no_vni_str, no_vni_siz)) {
		if (token[no_vni_siz] != '\0') {
			error("Invalid no_vni token '%s'", token);
			return false;
		} else {
			*no_vni = true;
		}
	} else if (!xstrncmp(token, adjust_limits_str, adjust_limits_siz)) {
		job->flags |= SLINGSHOT_FLAGS_ADJUST_LIMITS;
	} else if (!xstrncmp(token, no_adjust_limits_str,
			     no_adjust_limits_siz)) {
		job->flags &= ~(SLINGSHOT_FLAGS_ADJUST_LIMITS);
	} else if (!xstrncmp(token, rdzv_get_str, rdzv_get_siz)) {
		job->flags |= SLINGSHOT_FLAGS_DISABLE_RDZV_GET;
	} else if (!xstrncmp(token, hwcoll_str, hwcoll_siz)) {
		;
	} else if (!xstrncmp(token, tcs_str, tcs_siz)) {
		if (is_job)
			return _config_tcs(token, arg, &job->tcs);
	} else if (!_config_limits(token, &job->limits)) {
		return false;
	}

	return true;
}

/*
 * Set up passed-in slingshot_stepinfo_t based on values in srun/sbatch/salloc
 * --network parameters.  Return true on successful parsing, false otherwise.
 */
static bool _setup_network_params(const char *network_params,
				  const char *job_network_params,
				  slingshot_stepinfo_t *job,
				  bool *job_vni,
				  bool *single_node_vni,
				  bool *no_vni)
{
	char *params = NULL, *token, *save_ptr = NULL;

	log_flag(SWITCH, "job_network_params=%s network_params=%s",
		 job_network_params, network_params);

	/* First, copy limits, tcs, and flags from slingshot_config to job */
	job->limits = slingshot_config.limits;
	job->tcs = slingshot_config.tcs;
	job->flags = slingshot_config.flags;

	/* no_vni disabled by default */
	*no_vni = false;

	/* Then get configured job VNI setting */
	if (slingshot_config.job_vni == SLINGSHOT_JOB_VNI_ALL)
		*job_vni = true;
	else
		*job_vni = false;

	/* Then get configured single-node VNI setting */
	if (slingshot_config.single_node_vni == SLINGSHOT_SN_VNI_ALL)
		*single_node_vni = true;
	else
		*single_node_vni = false;

	/* Handle sbatch/salloc --network argument values */
	if (job_network_params) {
		params = xstrdup(job_network_params);
		for (token = strtok_r(params, ",", &save_ptr); token;
			token = strtok_r(NULL, ",", &save_ptr)) {
			if (!_parse_network_token(token, true, job, job_vni,
						  single_node_vni, no_vni))
				goto err;
		}
		xfree(params);
	}

	/* Handle srun --network argument values */
	if (network_params) {
		params = xstrdup(network_params);
		for (token = strtok_r(params, ",", &save_ptr); token;
			token = strtok_r(NULL, ",", &save_ptr)) {
			if (!_parse_network_token(token, false, job, job_vni,
						  single_node_vni, no_vni))
				goto err;
		}
		xfree(params);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_SWITCH)
		_print_limits(&job->limits);

	return true;
err:
	xfree(params);
	return false;
}

extern bool slingshot_setup_job_vni_pool(job_record_t *job_ptr)
{
	int alloc_vnis = 0;
	bool alloc_job_vni, alloc_single_node_vni, no_vni;
	slingshot_stepinfo_t stepinfo = {0};
	slingshot_jobinfo_t *jobinfo = NULL;

	if (!job_ptr->switch_jobinfo)
		job_ptr->switch_jobinfo = xmalloc(sizeof(slingshot_jobinfo_t));

	/*
	 * If --network specified, add any depth, limits,
	 * {job,single_node,no}_vni settings
	 * Copy configured Slingshot limits to job, add any --network settings
	 */
	if (!_setup_network_params(NULL, job_ptr->network,
				   &stepinfo, &alloc_job_vni,
				   &alloc_single_node_vni, &no_vni))
		goto err;

	jobinfo = (slingshot_jobinfo_t *) job_ptr->switch_jobinfo;

	/*
	 * VNIs and traffic classes are not allocated if:
	 * --network=no_vni is set, or single-node jobs,
	 * unless 'single_node_vni=all' is set in the configuration,
	 * or 'single_node_vni=user' is set in the configuration and
	 *    'srun --network=single_node_vni' is used
	 */
	jobinfo->num_vnis = 0;
	if (!no_vni &&
	    ((job_ptr->node_cnt > 1) || alloc_single_node_vni)) {
		alloc_vnis = job_ptr->node_cnt;
		if (alloc_job_vni)
			alloc_vnis++;
	}

	jobinfo->vnis = xcalloc(alloc_vnis, sizeof(uint16_t));
	for (int i = 0; i < alloc_vnis; i++) {
		if (!_alloc_vni(&jobinfo->vnis[i]))
			goto err;
		jobinfo->num_vnis++;
	}

	return true;

err:
	if (jobinfo) {
		for (int i = 0; i < jobinfo->num_vnis; i++)
			_free_vni(jobinfo->vnis[i]);
		slingshot_free_jobinfo(jobinfo);
	}
	job_ptr->switch_jobinfo = NULL;

	return false;
}

/*
 * Set up slingshot_stepinfo_t struct with VNIs, and CXI limits,
 * based on configured limits as well as any specified with
 * the --network option
 * Return true on success, false if VNI cannot be allocated,
 * or --network parameters have syntax errors
 */
extern bool slingshot_setup_job_step_vni(slingshot_stepinfo_t *job, int node_cnt,
					 uint32_t job_id,
					 const char *network_params,
					 const char *job_network_params)
{
	int alloc_vnis = 0;
	uint16_t vni = 0, job_vni = 0;
	bool alloc_job_vni, alloc_single_node_vni, no_vni;

	/*
	 * If --network specified, add any depth, limits,
	 * {job,single_node,no}_vni settings
	 * Copy configured Slingshot limits to job, add any --network settings
	 */
	if (!_setup_network_params(network_params, job_network_params, job,
				   &alloc_job_vni, &alloc_single_node_vni,
				   &no_vni))
		goto err;

	/*
	 * VNIs and traffic classes are not allocated if:
	 * --network=no_vni is set, or single-node jobs,
	 * unless 'single_node_vni=all' is set in the configuration,
	 * or 'single_node_vni=user' is set in the configuration and
	 *    'srun --network=single_node_vni' is used
	 */
	job->num_vnis = 0;
	if (!no_vni && ((node_cnt > 1) || alloc_single_node_vni)) {
		alloc_vnis++;
	}
	if (alloc_vnis > 0 && alloc_job_vni)
		alloc_vnis++;

	job->vnis = xcalloc(alloc_vnis, sizeof(uint16_t));
	if (alloc_vnis >= 1) {
		if (!_alloc_vni(&job->vnis[0]))
			goto err;
		vni = job->vnis[0];
		job->num_vnis++;
	}
	/* Allocate (first step in job) or get job VNI */
	if (alloc_vnis == 2) {
		if (!_alloc_job_vni(job_id, &job->vnis[1]))
			goto err;
		job_vni = job->vnis[1];
		job->num_vnis++;
	}
	debug("allocate vni=%hu job_vni=%hu free_vnis=%d",
	      vni, job_vni, free_vnis);

	/* profiles are allocated in slurmstepd */
	job->num_profiles = 0;
	job->profiles = NULL;

	return true;

err:
	if (job->vnis) {
		if (job->num_vnis > 0)
			_free_vni(job->vnis[0]);
		if (job->num_vnis > 1)
			(void)_free_job_vni(job_id);
		xfree(job->vnis);
	}

	return false;
}

/*
 * Free job VNI pool (if any)
 */
extern void slingshot_free_job_vni_pool(slingshot_jobinfo_t *job)
{
	/* slingshot_config is only initialized on the ctld and stepmgr */
	if (!running_in_slurmctld() && !active_outside_ctld)
		return;

	if (!job)
		return;

	for (int i = 0; i < job->num_vnis; i++) {
		_free_vni(job->vnis[i]);
		debug("free vni=%hu free_vnis=%d", job->vnis[0], free_vnis);
	}
}

/*
 * Free job-step VNI (if any)
 */
extern void slingshot_free_job_step_vni(slingshot_stepinfo_t *job)
{
	/* slingshot_config is only initialized on the ctld and stepmgr */
	if (!running_in_slurmctld() && !active_outside_ctld)
		return;

	/* Second VNI is a job VNI - don't free until job is complete */
	if (job->vnis && (job->num_vnis > 0)) {
		_free_vni(job->vnis[0]);
		debug("free vni=%hu free_vnis=%d", job->vnis[0], free_vnis);
	}
}

/*
 * Free this job's job-specific VNI; called at end of job
 */
extern void slingshot_free_job_vni(uint32_t job_id)
{
	uint16_t vni = _free_job_vni(job_id);
	debug("free job_vni=%hu free_vnis=%d", vni, free_vnis);
}

extern void slingshot_free_jobinfo(slingshot_jobinfo_t *jobinfo)
{
	if (!jobinfo)
		return;

	xfree(jobinfo->vnis);
	xfree(jobinfo);
}

extern int slingshot_update_config(slingshot_jobinfo_t *jobinfo)
{
	free_vnis = 0;
	bit_set_all(slingshot_state.vni_table);
	for (int i = 0; i < jobinfo->num_vnis; i++) {
		bit_clear(slingshot_state.vni_table,
			  (jobinfo->vnis[i] - slingshot_state.vni_min));
		free_vnis++;
	}
	if (slurm_conf.debug_flags & DEBUG_FLAG_SWITCH)	{
		char *bit_str = bit_fmt_full(slingshot_state.vni_table);
		log_flag(SWITCH, "%s: min/max: %hu/%hu free_vnis: %d bitstr: %s",
			 __func__, slingshot_state.vni_min,
			 slingshot_state.vni_max, free_vnis, bit_str);
		xfree(bit_str);
	}

	return SLURM_SUCCESS;
}
