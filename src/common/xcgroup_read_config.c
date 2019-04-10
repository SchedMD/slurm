/*****************************************************************************\
 *  xcgroup_read_config.c - functions for reading cgroup.conf
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *  Copyright (C) 2018 SchedMD LLC.
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
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/xcgroup_read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_api.h"

#define DEFAULT_CGROUP_BASEDIR "/sys/fs/cgroup"

pthread_mutex_t xcgroup_config_read_mutex = PTHREAD_MUTEX_INITIALIZER;

static slurm_cgroup_conf_t slurm_cgroup_conf;
static Buf cg_conf_buf = NULL;
static bool slurm_cgroup_conf_inited = false;
static bool slurm_cgroup_conf_exist = true;

strong_alias(xcgroup_config_read_mutex, slurm_xcgroup_config_read_mutex);
strong_alias(xcgroup_get_slurm_cgroup_conf,
	     slurm_xcgroup_get_slurm_cgroup_conf);
strong_alias(xcgroup_fini_slurm_cgroup_conf,
	     slurm_xcgroup_fini_slurm_cgroup_conf);

/* Local functions */
static void _clear_slurm_cgroup_conf(slurm_cgroup_conf_t *cg_conf)
{
	if (!cg_conf)
		return;

	cg_conf->cgroup_automount = false;
	xfree(cg_conf->cgroup_mountpoint);
	xfree(cg_conf->cgroup_prepend);
	cg_conf->constrain_cores = false;
	cg_conf->task_affinity = false;
	cg_conf->constrain_ram_space = false;
	cg_conf->allowed_ram_space = 100;
	cg_conf->max_ram_percent = 100;
	cg_conf->min_ram_space = XCGROUP_DEFAULT_MIN_RAM;
	cg_conf->constrain_swap_space = false;
	cg_conf->constrain_kmem_space = false;
	cg_conf->allowed_kmem_space = -1;
	cg_conf->max_kmem_percent = 100;
	cg_conf->min_kmem_space = XCGROUP_DEFAULT_MIN_RAM;
	cg_conf->allowed_swap_space = 0;
	cg_conf->max_swap_percent = 100;
	cg_conf->constrain_devices = false;
	cg_conf->memory_swappiness = NO_VAL64;
	xfree(cg_conf->allowed_devices_file);
}

static void _pack_cgroup_conf(slurm_cgroup_conf_t *cg_conf, Buf buffer)
{
	/*
	 * No protocol version needed, at the time of writing we are only
	 * sending at slurmstepd startup.
	 */

	if (!slurm_cgroup_conf_exist) {
		packbool(0, buffer);
		return;
	}
	packbool(1, buffer);
	packbool(cg_conf->cgroup_automount, buffer);
	packstr(cg_conf->cgroup_mountpoint, buffer);

	packstr(cg_conf->cgroup_prepend, buffer);

	packbool(cg_conf->constrain_cores, buffer);
	packbool(cg_conf->task_affinity, buffer);

	packbool(cg_conf->constrain_ram_space, buffer);
	packfloat(cg_conf->allowed_ram_space, buffer);
	packfloat(cg_conf->max_ram_percent, buffer);

	pack64(cg_conf->min_ram_space, buffer);

	packbool(cg_conf->constrain_kmem_space, buffer);
	packfloat(cg_conf->allowed_kmem_space, buffer);
	packfloat(cg_conf->max_kmem_percent, buffer);
	pack64(cg_conf->min_kmem_space, buffer);

	packbool(cg_conf->constrain_swap_space, buffer);
	packfloat(cg_conf->allowed_swap_space, buffer);
	packfloat(cg_conf->max_swap_percent, buffer);
	pack64(cg_conf->memory_swappiness, buffer);

	packbool(cg_conf->constrain_devices, buffer);
	packstr(cg_conf->allowed_devices_file, buffer);
}

static int _unpack_cgroup_conf(Buf buffer)
{
	uint32_t uint32_tmp = 0;
	bool tmpbool = false;
	/*
	 * No protocol version needed, at the time of writing we are only
	 * reading on slurmstepd startup.
	 */
	safe_unpackbool(&tmpbool, buffer);
	if (!tmpbool) {
		slurm_cgroup_conf_exist = false;
		return SLURM_SUCCESS;
	}

	safe_unpackbool(&slurm_cgroup_conf.cgroup_automount, buffer);
	safe_unpackstr_xmalloc(&slurm_cgroup_conf.cgroup_mountpoint,
			       &uint32_tmp, buffer);

	safe_unpackstr_xmalloc(&slurm_cgroup_conf.cgroup_prepend,
			       &uint32_tmp, buffer);

	safe_unpackbool(&slurm_cgroup_conf.constrain_cores, buffer);
	safe_unpackbool(&slurm_cgroup_conf.task_affinity, buffer);

	safe_unpackbool(&slurm_cgroup_conf.constrain_ram_space, buffer);
	safe_unpackfloat(&slurm_cgroup_conf.allowed_ram_space, buffer);
	safe_unpackfloat(&slurm_cgroup_conf.max_ram_percent, buffer);

	safe_unpack64(&slurm_cgroup_conf.min_ram_space, buffer);

	safe_unpackbool(&slurm_cgroup_conf.constrain_kmem_space, buffer);
	safe_unpackfloat(&slurm_cgroup_conf.allowed_kmem_space, buffer);
	safe_unpackfloat(&slurm_cgroup_conf.max_kmem_percent, buffer);
	safe_unpack64(&slurm_cgroup_conf.min_kmem_space, buffer);

	safe_unpackbool(&slurm_cgroup_conf.constrain_swap_space, buffer);
	safe_unpackfloat(&slurm_cgroup_conf.allowed_swap_space, buffer);
	safe_unpackfloat(&slurm_cgroup_conf.max_swap_percent, buffer);
	safe_unpack64(&slurm_cgroup_conf.memory_swappiness, buffer);

	safe_unpackbool(&slurm_cgroup_conf.constrain_devices, buffer);
	safe_unpackstr_xmalloc(&slurm_cgroup_conf.allowed_devices_file,
			       &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	_clear_slurm_cgroup_conf(&slurm_cgroup_conf);

	return SLURM_ERROR;
}

/*
 * read_slurm_cgroup_conf - load the Slurm cgroup configuration from the
 *	cgroup.conf file.
 */
static void _read_slurm_cgroup_conf_int(void)
{
	s_p_options_t options[] = {
		{"CgroupAutomount", S_P_BOOLEAN},
		{"CgroupMountpoint", S_P_STRING},
		{"CgroupReleaseAgentDir", S_P_STRING},
		{"ConstrainCores", S_P_BOOLEAN},
		{"TaskAffinity", S_P_BOOLEAN},
		{"ConstrainRAMSpace", S_P_BOOLEAN},
		{"AllowedRAMSpace", S_P_FLOAT},
		{"MaxRAMPercent", S_P_FLOAT},
		{"MinRAMSpace", S_P_UINT64},
		{"ConstrainSwapSpace", S_P_BOOLEAN},
		{"ConstrainKmemSpace", S_P_BOOLEAN},
		{"AllowedKmemSpace", S_P_FLOAT},
		{"MaxKmemPercent", S_P_FLOAT},
		{"MinKmemSpace", S_P_UINT64},
		{"AllowedSwapSpace", S_P_FLOAT},
		{"MaxSwapPercent", S_P_FLOAT},
		{"MemoryLimitEnforcement", S_P_BOOLEAN},
		{"MemoryLimitThreshold", S_P_FLOAT},
		{"ConstrainDevices", S_P_BOOLEAN},
		{"AllowedDevicesFile", S_P_STRING},
		{"MemorySwappiness", S_P_UINT64},
		{NULL} };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL, *tmp_str;
	struct stat buf;

	_clear_slurm_cgroup_conf(&slurm_cgroup_conf);

	/* Get the cgroup.conf path and validate the file */
	conf_path = get_extra_conf_path("cgroup.conf");
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		debug2("%s: No cgroup.conf file (%s)", __func__, conf_path);
		slurm_cgroup_conf_exist = false;
	} else {
		debug("Reading cgroup.conf file %s", conf_path);

		tbl = s_p_hashtbl_create(options);
		if (s_p_parse_file(tbl, NULL, conf_path, false) ==
		    SLURM_ERROR) {
			fatal("Could not open/read/parse cgroup.conf file %s",
			      conf_path);
		}

		/* cgroup initialisation parameters */
		if (!s_p_get_boolean(&slurm_cgroup_conf.cgroup_automount,
				     "CgroupAutomount", tbl))
			slurm_cgroup_conf.cgroup_automount = false;

		if (!s_p_get_string(&slurm_cgroup_conf.cgroup_mountpoint,
				    "CgroupMountpoint", tbl))
			slurm_cgroup_conf.cgroup_mountpoint =
				xstrdup(DEFAULT_CGROUP_BASEDIR);

		if (s_p_get_string(&tmp_str, "CgroupReleaseAgentDir", tbl)) {
			xfree(tmp_str);
			debug("Ignoring obsolete CgroupReleaseAgentDir option.");
		}

		/* cgroup prepend directory */
#ifndef MULTIPLE_SLURMD
		slurm_cgroup_conf.cgroup_prepend = xstrdup("/slurm");
#else
		slurm_cgroup_conf.cgroup_prepend = xstrdup("/slurm_%n");
#endif

		/* Cores constraints related conf items */
		if (!s_p_get_boolean(&slurm_cgroup_conf.constrain_cores,
				     "ConstrainCores", tbl))
			slurm_cgroup_conf.constrain_cores = false;
		if (!s_p_get_boolean(&slurm_cgroup_conf.task_affinity,
				     "TaskAffinity", tbl))
			slurm_cgroup_conf.task_affinity = false;

		/* RAM and Swap constraints related conf items */
		if (!s_p_get_boolean(&slurm_cgroup_conf.constrain_ram_space,
				     "ConstrainRAMSpace", tbl))
			slurm_cgroup_conf.constrain_ram_space = false;

		(void) s_p_get_float(&slurm_cgroup_conf.allowed_ram_space,
				     "AllowedRAMSpace", tbl);

		(void) s_p_get_float(&slurm_cgroup_conf.max_ram_percent,
				     "MaxRAMPercent", tbl);

		if (!s_p_get_boolean(&slurm_cgroup_conf.constrain_swap_space,
				     "ConstrainSwapSpace", tbl))
			slurm_cgroup_conf.constrain_swap_space = false;

		/*
		 * Disable constrain_kmem_space by default because of a known
		 * bug in Linux kernel version 3, early versions of kernel
		 * version 4, and RedHat/CentOS 6 and 7, which leaks slab
		 * caches, eventually causing the machine to be unable to create
		 * new cgroups.
		 */
		if (!s_p_get_boolean(&slurm_cgroup_conf.constrain_kmem_space,
				     "ConstrainKmemSpace", tbl))
			slurm_cgroup_conf.constrain_kmem_space = false;

		(void) s_p_get_float(&slurm_cgroup_conf.allowed_kmem_space,
				     "AllowedKmemSpace", tbl);

		(void) s_p_get_float(&slurm_cgroup_conf.max_kmem_percent,
				     "MaxKmemPercent", tbl);

		(void) s_p_get_uint64 (&slurm_cgroup_conf.min_kmem_space,
				       "MinKmemSpace", tbl);

		(void) s_p_get_float(&slurm_cgroup_conf.allowed_swap_space,
				     "AllowedSwapSpace", tbl);

		(void) s_p_get_float(&slurm_cgroup_conf.max_swap_percent,
				     "MaxSwapPercent", tbl);

		(void) s_p_get_uint64 (&slurm_cgroup_conf.min_ram_space,
				      "MinRAMSpace", tbl);

		if (s_p_get_uint64(&slurm_cgroup_conf.memory_swappiness,
				     "MemorySwappiness", tbl)) {
			if (slurm_cgroup_conf.memory_swappiness > 100) {
				error("Value for MemorySwappiness is too high,"
				      " rounding down to 100.");
				slurm_cgroup_conf.memory_swappiness = 100;
			}
		}

		/* Devices constraint related conf items */
		if (!s_p_get_boolean(&slurm_cgroup_conf.constrain_devices,
				     "ConstrainDevices", tbl))
			slurm_cgroup_conf.constrain_devices = false;

		s_p_get_string(&slurm_cgroup_conf.allowed_devices_file,
                               "AllowedDevicesFile", tbl);
                if (! slurm_cgroup_conf.allowed_devices_file)
                        slurm_cgroup_conf.allowed_devices_file =
				get_extra_conf_path(
					"cgroup_allowed_devices_file.conf");

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);

	return;
}

extern slurm_cgroup_conf_t *xcgroup_get_slurm_cgroup_conf(void)
{
	if (!slurm_cgroup_conf_inited) {
		memset(&slurm_cgroup_conf, 0, sizeof(slurm_cgroup_conf_t));
		_read_slurm_cgroup_conf_int();
		/*
		 * Initialize and pack cgroup.conf info into a buffer that can
		 * be used by slurmd to send to stepd every time, instead of
		 * re-packing every time we want to send to slurmsetpd
		 */
		cg_conf_buf = init_buf(0);
		_pack_cgroup_conf(&slurm_cgroup_conf, cg_conf_buf);
		slurm_cgroup_conf_inited = true;
	}

	return &slurm_cgroup_conf;
}


/*
 * get_slurm_cgroup_conf - load the Slurm cgroup configuration from the
 *      cgroup.conf  file and return a key pair <name,value> ordered list.
 * RET List with cgroup.conf <name,value> pairs if no error, NULL otherwise.
 */
extern List xcgroup_get_conf_list(void)
{
	slurm_cgroup_conf_t *cg_conf;
	config_key_pair_t *key_pair;
	List cgroup_conf_l;

	slurm_mutex_lock(&xcgroup_config_read_mutex);
	cg_conf = xcgroup_get_slurm_cgroup_conf();

	/* Fill list with cgroup config key pairs */
	cgroup_conf_l = list_create(destroy_config_key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CgroupAutomount");
	key_pair->value = xstrdup_printf("%s", cg_conf->cgroup_automount ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CgroupMountpoint");
	key_pair->value = xstrdup(cg_conf->cgroup_mountpoint);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ConstrainCores");
	key_pair->value = xstrdup_printf("%s", cg_conf->constrain_cores ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TaskAffinity");
	key_pair->value = xstrdup_printf("%s", cg_conf->task_affinity ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ConstrainRAMSpace");
	key_pair->value = xstrdup_printf("%s", cg_conf->constrain_ram_space ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowedRAMSpace");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf->allowed_ram_space);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxRAMPercent");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf->max_ram_percent);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MinRAMSpace");
	key_pair->value = xstrdup_printf("%"PRIu64" MB",
					 cg_conf->min_ram_space);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ConstrainSwapSpace");
	key_pair->value = xstrdup_printf("%s", cg_conf->constrain_swap_space ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ConstrainKmemSpace");
	key_pair->value = xstrdup_printf("%s", cg_conf->constrain_kmem_space ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowedKmemSpace");
	if (cg_conf->allowed_kmem_space >= 0)
		key_pair->value = xstrdup_printf("%.0f Bytes",
						 cg_conf->allowed_kmem_space);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxKmemPercent");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf->max_kmem_percent);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MinKmemSpace");
	key_pair->value = xstrdup_printf("%"PRIu64" MB",
					 cg_conf->min_kmem_space);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowedSwapSpace");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf->allowed_swap_space);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxSwapPercent");
	key_pair->value = xstrdup_printf("%.1f%%", cg_conf->max_swap_percent);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ConstrainDevices");
	key_pair->value = xstrdup_printf("%s", cg_conf->constrain_devices ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AllowedDevicesFile");
	key_pair->value = xstrdup(cg_conf->allowed_devices_file);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MemorySwappiness");
	if (cg_conf->memory_swappiness != NO_VAL64)
		key_pair->value = xstrdup_printf("%"PRIu64,
						 cg_conf->memory_swappiness);
	list_append(cgroup_conf_l, key_pair);

	list_sort(cgroup_conf_l, (ListCmpF) sort_key_pairs);

	slurm_mutex_unlock(&xcgroup_config_read_mutex);

	return cgroup_conf_l;
}

extern void xcgroup_reconfig_slurm_cgroup_conf(void)
{
	slurm_mutex_lock(&xcgroup_config_read_mutex);

	if (slurm_cgroup_conf_inited) {
		_clear_slurm_cgroup_conf(&slurm_cgroup_conf);
		FREE_NULL_BUFFER(cg_conf_buf);
		slurm_cgroup_conf_inited = false;
	}
	(void)xcgroup_get_slurm_cgroup_conf();

	slurm_mutex_unlock(&xcgroup_config_read_mutex);

}

extern int xcgroup_write_conf(int fd)
{
	int len;

	slurm_mutex_lock(&xcgroup_config_read_mutex);
	if (!slurm_cgroup_conf_inited)
		(void)xcgroup_get_slurm_cgroup_conf();

	len = get_buf_offset(cg_conf_buf);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, get_buf_data(cg_conf_buf), len);

	slurm_mutex_unlock(&xcgroup_config_read_mutex);
	return 0;

rwfail:
	slurm_mutex_unlock(&xcgroup_config_read_mutex);
	return -1;
}

extern int xcgroup_read_conf(int fd)
{
	int len, rc;
	Buf buffer = NULL;

	xcgroup_fini_slurm_cgroup_conf();

	slurm_mutex_lock(&xcgroup_config_read_mutex);
	memset(&slurm_cgroup_conf, 0, sizeof(slurm_cgroup_conf_t));

	safe_read(fd, &len, sizeof(int));

	buffer = init_buf(len);
	safe_read(fd, buffer->head, len);

	rc = _unpack_cgroup_conf(buffer);

	if (rc == SLURM_ERROR)
		fatal("%s: problem with unpack of cgroup.conf", __func__);

	FREE_NULL_BUFFER(buffer);

	slurm_cgroup_conf_inited = true;
	slurm_mutex_unlock(&xcgroup_config_read_mutex);

	return SLURM_SUCCESS;
rwfail:
	slurm_mutex_unlock(&xcgroup_config_read_mutex);
	FREE_NULL_BUFFER(buffer);

	return SLURM_ERROR;
}

extern void xcgroup_fini_slurm_cgroup_conf(void)
{
	slurm_mutex_lock(&xcgroup_config_read_mutex);

	if (slurm_cgroup_conf_inited) {
		_clear_slurm_cgroup_conf(&slurm_cgroup_conf);
		slurm_cgroup_conf_inited = false;
		FREE_NULL_BUFFER(cg_conf_buf);
	}

	slurm_mutex_unlock(&xcgroup_config_read_mutex);
}

extern bool xcgroup_mem_cgroup_job_confinement(void)
{
	slurm_cgroup_conf_t *cg_conf;
	char *task_plugin_type = NULL;
	bool status = false;

	/* read cgroup configuration */
	slurm_mutex_lock(&xcgroup_config_read_mutex);
	cg_conf = xcgroup_get_slurm_cgroup_conf();

	task_plugin_type = slurm_get_task_plugin();

	if ((cg_conf->constrain_ram_space ||
	     cg_conf->constrain_swap_space) &&
	    xstrstr(task_plugin_type, "cgroup"))
		status = true;

	slurm_mutex_unlock(&xcgroup_config_read_mutex);

	xfree(task_plugin_type);
	return status;
}
