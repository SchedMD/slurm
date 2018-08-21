/*****************************************************************************\
 *  xcgroup_read_config.c - functions for reading cgroup.conf
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/parse_config.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/xcgroup_read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define DEFAULT_CGROUP_BASEDIR "/sys/fs/cgroup"

static pthread_mutex_t cgroup_config_read_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Local functions */
static void _clear_slurm_cgroup_conf(slurm_cgroup_conf_t *slurm_cgroup_conf);

/*
 * free_slurm_cgroup_conf - free storage associated with the global variable
 *	slurm_cgroup_conf
 */
extern void free_slurm_cgroup_conf(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	_clear_slurm_cgroup_conf(slurm_cgroup_conf);
}

static void _clear_slurm_cgroup_conf(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	if (slurm_cgroup_conf) {
		slurm_cgroup_conf->cgroup_automount = false ;
		xfree(slurm_cgroup_conf->cgroup_mountpoint);
		xfree(slurm_cgroup_conf->cgroup_prepend);
		slurm_cgroup_conf->constrain_cores = false ;
		slurm_cgroup_conf->task_affinity = false ;
		slurm_cgroup_conf->constrain_ram_space = false ;
		slurm_cgroup_conf->allowed_ram_space = 100 ;
		slurm_cgroup_conf->max_ram_percent = 100 ;
		slurm_cgroup_conf->min_ram_space = XCGROUP_DEFAULT_MIN_RAM;
		slurm_cgroup_conf->constrain_swap_space = false ;
		slurm_cgroup_conf->constrain_kmem_space = false ;
		slurm_cgroup_conf->allowed_kmem_space = -1;
		slurm_cgroup_conf->max_kmem_percent = 100;
		slurm_cgroup_conf->min_kmem_space = XCGROUP_DEFAULT_MIN_RAM;
		slurm_cgroup_conf->allowed_swap_space = 0 ;
		slurm_cgroup_conf->max_swap_percent = 100 ;
		slurm_cgroup_conf->memlimit_enforcement = 0 ;
		slurm_cgroup_conf->memlimit_threshold = 100 ;
		slurm_cgroup_conf->constrain_devices = false ;
		slurm_cgroup_conf->memory_swappiness = NO_VAL64;
		xfree(slurm_cgroup_conf->allowed_devices_file);
	}
}

/*
 *   Parse a floating point value in s and return in val
 *    Return -1 on error and leave *val unchanged.
 */
static int str_to_float (char *s, float *val)
{
	float f;
	char *p;

	errno = 0;
	f = strtof (s, &p);

	if ((*p != '\0') || (errno != 0))
		return (-1);

	*val = f;
	return (0);
}

static void conf_get_float (s_p_hashtbl_t *t, char *name, float *fp)
{
	char *str;
	if (!s_p_get_string(&str, name, t))
		return;
	if (str_to_float (str, fp) < 0)
		fatal ("cgroup.conf: Invalid value '%s' for %s", str, name);
	xfree(str);
}

/*
 * read_slurm_cgroup_conf - load the Slurm cgroup configuration from the
 *	cgroup.conf file.
 * RET SLURM_SUCCESS if no error, otherwise an error code
 */
static int _read_slurm_cgroup_conf_int(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	s_p_options_t options[] = {
		{"CgroupAutomount", S_P_BOOLEAN},
		{"CgroupMountpoint", S_P_STRING},
		{"CgroupReleaseAgentDir", S_P_STRING},
		{"ConstrainCores", S_P_BOOLEAN},
		{"TaskAffinity", S_P_BOOLEAN},
		{"ConstrainRAMSpace", S_P_BOOLEAN},
		{"AllowedRAMSpace", S_P_STRING},
		{"MaxRAMPercent", S_P_STRING},
		{"MinRAMSpace", S_P_UINT64},
		{"ConstrainSwapSpace", S_P_BOOLEAN},
		{"ConstrainKmemSpace", S_P_BOOLEAN},
		{"AllowedKmemSpace", S_P_STRING},
		{"MaxKmemPercent", S_P_STRING},
		{"MinKmemSpace", S_P_UINT64},
		{"AllowedSwapSpace", S_P_STRING},
		{"MaxSwapPercent", S_P_STRING},
		{"MemoryLimitEnforcement", S_P_BOOLEAN},
		{"MemoryLimitThreshold", S_P_STRING},
		{"ConstrainDevices", S_P_BOOLEAN},
		{"AllowedDevicesFile", S_P_STRING},
		{"MemorySwappiness", S_P_UINT64},
		{NULL} };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL, *tmp_str;
	struct stat buf;

	/* Set initial values */
	if (slurm_cgroup_conf == NULL) {
		return SLURM_ERROR;
	}
	_clear_slurm_cgroup_conf(slurm_cgroup_conf);

	/* Get the cgroup.conf path and validate the file */
	conf_path = get_extra_conf_path("cgroup.conf");
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		debug2("%s: No cgroup.conf file (%s)", __func__, conf_path);
	} else {
		debug("Reading cgroup.conf file %s", conf_path);

		tbl = s_p_hashtbl_create(options);
		if (s_p_parse_file(tbl, NULL, conf_path, false) ==
		    SLURM_ERROR) {
			fatal("Could not open/read/parse cgroup.conf file %s",
			      conf_path);
		}

		/* cgroup initialisation parameters */
		if (!s_p_get_boolean(&slurm_cgroup_conf->cgroup_automount,
				     "CgroupAutomount", tbl))
			slurm_cgroup_conf->cgroup_automount = false;

		if (!s_p_get_string(&slurm_cgroup_conf->cgroup_mountpoint,
				    "CgroupMountpoint", tbl))
			slurm_cgroup_conf->cgroup_mountpoint =
				xstrdup(DEFAULT_CGROUP_BASEDIR);

		if (s_p_get_string(&tmp_str, "CgroupReleaseAgentDir", tbl)) {
			xfree(tmp_str);
			debug("Ignoring obsolete CgroupReleaseAgentDir option.");
		}

		/* cgroup prepend directory */
#ifndef MULTIPLE_SLURMD
		slurm_cgroup_conf->cgroup_prepend = xstrdup("/slurm");
#else
		slurm_cgroup_conf->cgroup_prepend = xstrdup("/slurm_%n");
#endif

		/* Cores constraints related conf items */
		if (!s_p_get_boolean(&slurm_cgroup_conf->constrain_cores,
				     "ConstrainCores", tbl))
			slurm_cgroup_conf->constrain_cores = false;
		if (!s_p_get_boolean(&slurm_cgroup_conf->task_affinity,
				     "TaskAffinity", tbl))
			slurm_cgroup_conf->task_affinity = false;

		/* RAM and Swap constraints related conf items */
		if (!s_p_get_boolean(&slurm_cgroup_conf->constrain_ram_space,
				     "ConstrainRAMSpace", tbl))
			slurm_cgroup_conf->constrain_ram_space = false;

		conf_get_float (tbl,
				"AllowedRAMSpace",
				&slurm_cgroup_conf->allowed_ram_space);

		conf_get_float (tbl,
				"MaxRAMPercent",
				&slurm_cgroup_conf->max_ram_percent);

		if (!s_p_get_boolean(&slurm_cgroup_conf->constrain_swap_space,
				     "ConstrainSwapSpace", tbl))
			slurm_cgroup_conf->constrain_swap_space = false;

		/*
		 * Disable constrain_kmem_space by default because of a known
		 * bug in Linux kernel version 3, early versions of kernel
		 * version 4, and RedHat/CentOS 6 and 7, which leaks slab
		 * caches, eventually causing the machine to be unable to create
		 * new cgroups.
		 */
		if (!s_p_get_boolean(&slurm_cgroup_conf->constrain_kmem_space,
				     "ConstrainKmemSpace", tbl))
			slurm_cgroup_conf->constrain_kmem_space = false;

		conf_get_float (tbl,
				"AllowedKmemSpace",
				&slurm_cgroup_conf->allowed_kmem_space);

		conf_get_float (tbl,
				"MaxKmemPercent",
				&slurm_cgroup_conf->max_kmem_percent);

		(void) s_p_get_uint64 (&slurm_cgroup_conf->min_kmem_space,
				       "MinKmemSpace", tbl);

		conf_get_float (tbl,
				"AllowedSwapSpace",
				&slurm_cgroup_conf->allowed_swap_space);

		conf_get_float (tbl,
				"MaxSwapPercent",
				&slurm_cgroup_conf->max_swap_percent);

		(void) s_p_get_uint64 (&slurm_cgroup_conf->min_ram_space,
				      "MinRAMSpace", tbl);

		if (s_p_get_uint64(&slurm_cgroup_conf->memory_swappiness,
				     "MemorySwappiness", tbl)) {
			if (slurm_cgroup_conf->memory_swappiness > 100) {
				error("Value for MemorySwappiness is too high,"
				      " rounding down to 100.");
				slurm_cgroup_conf->memory_swappiness = 100;
			}
		}

		/* Memory limits */
		if (!s_p_get_boolean(&slurm_cgroup_conf->memlimit_enforcement,
				     "MemoryLimitEnforcement", tbl))
			slurm_cgroup_conf->memlimit_enforcement = false;

		conf_get_float (tbl,
				"MemoryLimitThreshold",
				&slurm_cgroup_conf->memlimit_threshold);

		/* Devices constraint related conf items */
		if (!s_p_get_boolean(&slurm_cgroup_conf->constrain_devices,
				     "ConstrainDevices", tbl))
			slurm_cgroup_conf->constrain_devices = false;

		s_p_get_string(&slurm_cgroup_conf->allowed_devices_file,
                               "AllowedDevicesFile", tbl);
                if (! slurm_cgroup_conf->allowed_devices_file)
                        slurm_cgroup_conf->allowed_devices_file =
				get_extra_conf_path(
					"cgroup_allowed_devices_file.conf");

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);

	return SLURM_SUCCESS;
}

/*
 * get_slurm_cgroup_conf - load the Slurm cgroup configuration from the
 *      cgroup.conf  file and return a key pair <name,value> ordered list.
 * RET List with cgroup.conf <name,value> pairs if no error, NULL otherwise.
 */
extern List get_slurm_cgroup_conf(void)
{
	slurm_cgroup_conf_t cg_conf;
	config_key_pair_t *key_pair;
	char *conf_path = NULL;
	struct stat buf;
	List cgroup_conf_l;

	/* Check for cgroup.conf access */
	conf_path = get_extra_conf_path("cgroup.conf");
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		xfree(conf_path);
		return NULL;
	}
	xfree(conf_path);

	/* Read and parse cgroup.conf */
	memset(&cg_conf, 0, sizeof(slurm_cgroup_conf_t));

	if (read_slurm_cgroup_conf(&cg_conf) != SLURM_SUCCESS)
		return NULL;

	/* Fill list with cgroup config key pairs */
	cgroup_conf_l = list_create(destroy_config_key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CgroupAutomount");
	key_pair->value = xstrdup_printf("%s", cg_conf.cgroup_automount ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CgroupMountpoint");
	key_pair->value = xstrdup(cg_conf.cgroup_mountpoint);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ConstrainCores");
	key_pair->value = xstrdup_printf("%s", cg_conf.constrain_cores ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TaskAffinity");
	key_pair->value = xstrdup_printf("%s", cg_conf.task_affinity ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ConstrainRAMSpace");
	key_pair->value = xstrdup_printf("%s", cg_conf.constrain_ram_space ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowedRAMSpace");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf.allowed_ram_space);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxRAMPercent");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf.max_ram_percent);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MinRAMSpace");
	key_pair->value = xstrdup_printf("%"PRIu64" MB", cg_conf.min_ram_space);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ConstrainSwapSpace");
	key_pair->value = xstrdup_printf("%s", cg_conf.constrain_swap_space ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ConstrainKmemSpace");
	key_pair->value = xstrdup_printf("%s", cg_conf.constrain_kmem_space ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowedKmemSpace");
	if (cg_conf.allowed_kmem_space >= 0)
		key_pair->value = xstrdup_printf("%.0f Bytes",
						 cg_conf.allowed_kmem_space);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxKmemPercent");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf.max_kmem_percent);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MinKmemSpace");
	key_pair->value = xstrdup_printf("%"PRIu64" MB",
					 cg_conf.min_kmem_space);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowedSwapSpace");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf.allowed_swap_space);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxSwapPercent");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf.max_swap_percent);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MemoryLimitEnforcement");
	key_pair->value = xstrdup_printf("%s", cg_conf.memlimit_enforcement ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MemLimitThreshold");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf.memlimit_threshold);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ConstrainDevices");
	key_pair->value = xstrdup_printf("%s", cg_conf.constrain_devices ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowedDevicesFile");
	key_pair->value = xstrdup(cg_conf.allowed_devices_file);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MemorySwappiness");
	if (cg_conf.memory_swappiness != NO_VAL64)
		key_pair->value = xstrdup_printf("%"PRIu64,
						 cg_conf.memory_swappiness);
	list_append(cgroup_conf_l, key_pair);

	list_sort(cgroup_conf_l, (ListCmpF) sort_key_pairs);
	free_slurm_cgroup_conf(&cg_conf);

	return cgroup_conf_l;
}

extern int read_slurm_cgroup_conf(slurm_cgroup_conf_t *slurm_cgroup_conf)
{
	int rc;

	slurm_mutex_lock(&cgroup_config_read_mutex);
	rc = _read_slurm_cgroup_conf_int(slurm_cgroup_conf);
	slurm_mutex_unlock(&cgroup_config_read_mutex);

	return rc;
}
