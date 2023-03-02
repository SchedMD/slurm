/*****************************************************************************\
 *  cgroup.c - driver for cgroup plugin
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Felip Moll <felip.moll@schedmd.com>
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

#include "src/interfaces/cgroup.h"

/* Define slurm-specific aliases for use by plugins, see slurm_xlator.h. */
strong_alias(cgroup_conf_init, slurm_cgroup_conf_init);
strong_alias(cgroup_conf_destroy, slurm_cgroup_conf_destroy);
strong_alias(autodetect_cgroup_version, slurm_autodetect_cgroup_version);

#define DEFAULT_CGROUP_BASEDIR "/sys/fs/cgroup"
#define DEFAULT_CGROUP_PLUGIN "autodetect"

/* Symbols provided by the plugin */
typedef struct {
	int	(*initialize)		(cgroup_ctl_type_t sub);
	int	(*system_create)	(cgroup_ctl_type_t sub);
	int	(*system_addto)		(cgroup_ctl_type_t sub, pid_t *pids,
					 int npids);
	int	(*system_destroy)	(cgroup_ctl_type_t sub);
	int	(*step_create)		(cgroup_ctl_type_t sub,
					 stepd_step_rec_t *step);
	int	(*step_addto)		(cgroup_ctl_type_t sub, pid_t *pids,
					 int npids);
	int	(*step_get_pids)	(pid_t **pids, int *npids);
	int	(*step_suspend)		(void);
	int	(*step_resume)		(void);
	int	(*step_destroy)		(cgroup_ctl_type_t sub);
	bool	(*has_pid)		(pid_t pid);
	cgroup_limits_t *(*constrain_get) (cgroup_ctl_type_t sub,
					   cgroup_level_t level);
	int	(*constrain_set)	(cgroup_ctl_type_t sub,
					 cgroup_level_t level,
					 cgroup_limits_t *limits);
        int	(*constrain_apply)	(cgroup_ctl_type_t sub,
                                         cgroup_level_t level,
                                         uint32_t task_id);
	int	(*step_start_oom_mgr)	(void);
	cgroup_oom_t *(*step_stop_oom_mgr) (stepd_step_rec_t *step);
	int	(*task_addto)		(cgroup_ctl_type_t sub,
					 stepd_step_rec_t *step, pid_t pid,
					 uint32_t task_id);
	cgroup_acct_t *(*task_get_acct_data) (uint32_t taskid);
	long int (*get_acct_units)	(void);
	bool (*has_feature) (cgroup_ctl_feature_t f);
} slurm_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_ops_t.
 */
static const char *syms[] = {
	"cgroup_p_initialize",
	"cgroup_p_system_create",
	"cgroup_p_system_addto",
	"cgroup_p_system_destroy",
	"cgroup_p_step_create",
	"cgroup_p_step_addto",
	"cgroup_p_step_get_pids",
	"cgroup_p_step_suspend",
	"cgroup_p_step_resume",
	"cgroup_p_step_destroy",
	"cgroup_p_has_pid",
	"cgroup_p_constrain_get",
	"cgroup_p_constrain_set",
        "cgroup_p_constrain_apply",
	"cgroup_p_step_start_oom_mgr",
	"cgroup_p_step_stop_oom_mgr",
	"cgroup_p_task_addto",
	"cgroup_p_task_get_acct_data",
	"cgroup_p_get_acct_units",
	"cgroup_p_has_feature",
};

/* Local variables */
static slurm_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock =	PTHREAD_MUTEX_INITIALIZER;

cgroup_conf_t slurm_cgroup_conf;

static pthread_rwlock_t cg_conf_lock = PTHREAD_RWLOCK_INITIALIZER;
static buf_t *cg_conf_buf = NULL;
static bool cg_conf_inited = false;
static bool cg_conf_exist = true;

/* local functions */
static void _cgroup_conf_fini();
static void _clear_slurm_cgroup_conf();
static void _pack_cgroup_conf(buf_t *buffer);
static int _unpack_cgroup_conf(buf_t *buffer);
static void _read_slurm_cgroup_conf(void);

/* Local functions */
static void _cgroup_conf_fini()
{
	slurm_rwlock_wrlock(&cg_conf_lock);

	_clear_slurm_cgroup_conf();
	cg_conf_inited = false;
	FREE_NULL_BUFFER(cg_conf_buf);

	slurm_rwlock_unlock(&cg_conf_lock);
}

static void _clear_slurm_cgroup_conf(void)
{
	xfree(slurm_cgroup_conf.cgroup_mountpoint);
	xfree(slurm_cgroup_conf.cgroup_plugin);
	xfree(slurm_cgroup_conf.cgroup_prepend);

	memset(&slurm_cgroup_conf, 0, sizeof(slurm_cgroup_conf));
}

static void _init_slurm_cgroup_conf(void)
{
	_clear_slurm_cgroup_conf();

	slurm_cgroup_conf.allowed_kmem_space = -1;
	slurm_cgroup_conf.allowed_ram_space = 100;
	slurm_cgroup_conf.allowed_swap_space = 0;
	slurm_cgroup_conf.cgroup_automount = false;
	slurm_cgroup_conf.cgroup_mountpoint = xstrdup(DEFAULT_CGROUP_BASEDIR);
	slurm_cgroup_conf.cgroup_plugin = xstrdup(DEFAULT_CGROUP_PLUGIN);
#ifndef MULTIPLE_SLURMD
	slurm_cgroup_conf.cgroup_prepend = xstrdup("/slurm");
#else
	slurm_cgroup_conf.cgroup_prepend = xstrdup("/slurm_%n");
#endif
	slurm_cgroup_conf.constrain_cores = false;
	slurm_cgroup_conf.constrain_devices = false;
	slurm_cgroup_conf.constrain_kmem_space = false;
	slurm_cgroup_conf.constrain_ram_space = false;
	slurm_cgroup_conf.constrain_swap_space = false;
	slurm_cgroup_conf.enable_controllers = false;
	slurm_cgroup_conf.ignore_systemd = false;
	slurm_cgroup_conf.ignore_systemd_on_failure = false;
	slurm_cgroup_conf.max_kmem_percent = 100;
	slurm_cgroup_conf.max_ram_percent = 100;
	slurm_cgroup_conf.max_swap_percent = 100;
	slurm_cgroup_conf.memory_swappiness = NO_VAL64;
	slurm_cgroup_conf.min_kmem_space = XCGROUP_DEFAULT_MIN_RAM;
	slurm_cgroup_conf.min_ram_space = XCGROUP_DEFAULT_MIN_RAM;
	slurm_cgroup_conf.root_owned_cgroups = true;
}

static void _pack_cgroup_conf(buf_t *buffer)
{
	/*
	 * No protocol version needed, at the time of writing we are only
	 * sending at slurmstepd startup.
	 */

	if (!cg_conf_exist) {
		packbool(0, buffer);
		return;
	}
	packbool(1, buffer);
	packbool(slurm_cgroup_conf.cgroup_automount, buffer);
	packstr(slurm_cgroup_conf.cgroup_mountpoint, buffer);

	packstr(slurm_cgroup_conf.cgroup_prepend, buffer);

	packbool(slurm_cgroup_conf.constrain_cores, buffer);

	packbool(slurm_cgroup_conf.constrain_ram_space, buffer);
	packfloat(slurm_cgroup_conf.allowed_ram_space, buffer);
	packfloat(slurm_cgroup_conf.max_ram_percent, buffer);

	pack64(slurm_cgroup_conf.min_ram_space, buffer);

	packbool(slurm_cgroup_conf.constrain_kmem_space, buffer);
	packfloat(slurm_cgroup_conf.allowed_kmem_space, buffer);
	packfloat(slurm_cgroup_conf.max_kmem_percent, buffer);
	pack64(slurm_cgroup_conf.min_kmem_space, buffer);

	packbool(slurm_cgroup_conf.constrain_swap_space, buffer);
	packfloat(slurm_cgroup_conf.allowed_swap_space, buffer);
	packfloat(slurm_cgroup_conf.max_swap_percent, buffer);
	pack64(slurm_cgroup_conf.memory_swappiness, buffer);

	packbool(slurm_cgroup_conf.constrain_devices, buffer);
	packstr(slurm_cgroup_conf.cgroup_plugin, buffer);

	packbool(slurm_cgroup_conf.ignore_systemd, buffer);
	packbool(slurm_cgroup_conf.ignore_systemd_on_failure, buffer);

	packbool(slurm_cgroup_conf.root_owned_cgroups, buffer);
	packbool(slurm_cgroup_conf.enable_controllers, buffer);
}

static int _unpack_cgroup_conf(buf_t *buffer)
{
	uint32_t uint32_tmp = 0;
	bool tmpbool = false;
	/*
	 * No protocol version needed, at the time of writing we are only
	 * reading on slurmstepd startup.
	 */
	safe_unpackbool(&tmpbool, buffer);
	if (!tmpbool) {
		cg_conf_exist = false;
		return SLURM_SUCCESS;
	}

	safe_unpackbool(&slurm_cgroup_conf.cgroup_automount, buffer);
	safe_unpackstr_xmalloc(&slurm_cgroup_conf.cgroup_mountpoint,
			       &uint32_tmp, buffer);

	safe_unpackstr_xmalloc(&slurm_cgroup_conf.cgroup_prepend,
			       &uint32_tmp, buffer);

	safe_unpackbool(&slurm_cgroup_conf.constrain_cores, buffer);

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
	safe_unpackstr_xmalloc(&slurm_cgroup_conf.cgroup_plugin,
			       &uint32_tmp, buffer);

	safe_unpackbool(&slurm_cgroup_conf.ignore_systemd, buffer);
	safe_unpackbool(&slurm_cgroup_conf.ignore_systemd_on_failure, buffer);

	safe_unpackbool(&slurm_cgroup_conf.root_owned_cgroups, buffer);
	safe_unpackbool(&slurm_cgroup_conf.enable_controllers, buffer);
	return SLURM_SUCCESS;

unpack_error:
	_clear_slurm_cgroup_conf();

	return SLURM_ERROR;
}

/*
 * read_slurm_cgroup_conf - load the Slurm cgroup configuration from the
 *	cgroup.conf file.
 */
static void _read_slurm_cgroup_conf(void)
{
	s_p_options_t options[] = {
		{"CgroupAutomount", S_P_BOOLEAN},
		{"CgroupMountpoint", S_P_STRING},
		{"CgroupReleaseAgentDir", S_P_STRING},
		{"ConstrainCores", S_P_BOOLEAN},
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
		{"CgroupPlugin", S_P_STRING},
		{"IgnoreSystemd", S_P_BOOLEAN},
		{"IgnoreSystemdOnFailure", S_P_BOOLEAN},
		{"RootOwnedCgroups", S_P_BOOLEAN},
		{"EnableControllers", S_P_BOOLEAN},
		{NULL} };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL, *tmp_str;
	struct stat buf;
	size_t sz;
	bool kmem_deprecate_msg = false;

	/* Get the cgroup.conf path and validate the file */
	conf_path = get_extra_conf_path("cgroup.conf");
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		log_flag(CGROUP, "%s: No cgroup.conf file (%s)", __func__,
			 conf_path);
		cg_conf_exist = false;
	} else {
		debug("Reading cgroup.conf file %s", conf_path);

		tbl = s_p_hashtbl_create(options);
		if (s_p_parse_file(tbl, NULL, conf_path, false, NULL) ==
		    SLURM_ERROR) {
			fatal("Could not open/read/parse cgroup.conf file %s",
			      conf_path);
		}

		/* cgroup initialization parameters */
		(void) s_p_get_boolean(&slurm_cgroup_conf.cgroup_automount,
				       "CgroupAutomount", tbl);

		if (s_p_get_string(&tmp_str, "CgroupMountpoint", tbl)) {
			/* Remove the trailing / if any. */
			sz = strlen(tmp_str);
			if (*(tmp_str + sz - 1) == '/')
				*(tmp_str + sz - 1) = '\0';
			xfree(slurm_cgroup_conf.cgroup_mountpoint);
			slurm_cgroup_conf.cgroup_mountpoint = tmp_str;
			tmp_str = NULL;
		}
		if (s_p_get_string(&tmp_str, "CgroupReleaseAgentDir", tbl)) {
			xfree(tmp_str);
			fatal("Support for CgroupReleaseAgentDir option has been removed.");
		}

		/* Cores constraints related conf items */
		(void) s_p_get_boolean(&slurm_cgroup_conf.constrain_cores,
				       "ConstrainCores", tbl);

		/* RAM and Swap constraints related conf items */
		(void) s_p_get_boolean(&slurm_cgroup_conf.constrain_ram_space,
				       "ConstrainRAMSpace", tbl);

		(void) s_p_get_float(&slurm_cgroup_conf.allowed_ram_space,
				     "AllowedRAMSpace", tbl);

		(void) s_p_get_float(&slurm_cgroup_conf.max_ram_percent,
				     "MaxRAMPercent", tbl);

		(void) s_p_get_boolean(&slurm_cgroup_conf.constrain_swap_space,
				       "ConstrainSwapSpace", tbl);

		/*
		 * Disable constrain_kmem_space by default because of a known
		 * bug in Linux kernel version 3, early versions of kernel
		 * version 4, and RedHat/CentOS 6 and 7, which leaks slab
		 * caches, eventually causing the machine to be unable to create
		 * new cgroups.
		 *
		 * kmem.limit_in_bytes is deprecated in the linux kernel
		 * and is not used in cgroup v2, so we are deprecating
		 * ConstrainKmemSpace and related parameters.
		 */
		if (s_p_get_boolean(&slurm_cgroup_conf.constrain_kmem_space,
				    "ConstrainKmemSpace", tbl))
			kmem_deprecate_msg = true;

		if (s_p_get_float(&slurm_cgroup_conf.allowed_kmem_space,
				  "AllowedKmemSpace", tbl))
			kmem_deprecate_msg = true;

		if (s_p_get_float(&slurm_cgroup_conf.max_kmem_percent,
				  "MaxKmemPercent", tbl))
			kmem_deprecate_msg = true;

		if (s_p_get_uint64(&slurm_cgroup_conf.min_kmem_space,
				   "MinKmemSpace", tbl))
			kmem_deprecate_msg = true;

		if (kmem_deprecate_msg && running_in_daemon())
			error("AllowedKmemSpace, ConstrainKmemSpace, MaxKmemPercent, and MinKmemSpace are deprecated, and will be removed in a future release");

		(void) s_p_get_float(&slurm_cgroup_conf.allowed_swap_space,
				     "AllowedSwapSpace", tbl);

		(void) s_p_get_float(&slurm_cgroup_conf.max_swap_percent,
				     "MaxSwapPercent", tbl);

		(void) s_p_get_uint64 (&slurm_cgroup_conf.min_ram_space,
				      "MinRAMSpace", tbl);

		if (s_p_get_uint64(&slurm_cgroup_conf.memory_swappiness,
				     "MemorySwappiness", tbl)) {
			if (slurm_cgroup_conf.memory_swappiness > 100) {
				error("Value for MemorySwappiness is too high, rounding down to 100.");
				slurm_cgroup_conf.memory_swappiness = 100;
			}
		}

		/* Devices constraint related conf items */
		(void) s_p_get_boolean(&slurm_cgroup_conf.constrain_devices,
				       "ConstrainDevices", tbl);

		if (s_p_get_string(&tmp_str, "AllowedDevicesFile", tbl)) {
			xfree(tmp_str);
			warning("AllowedDevicesFile option is obsolete, please remove it from your configuration.");
		}

		if (s_p_get_string(&tmp_str, "CgroupPlugin", tbl)) {
			xfree(slurm_cgroup_conf.cgroup_plugin);
			slurm_cgroup_conf.cgroup_plugin = tmp_str;
			tmp_str = NULL;
		}

		if (s_p_get_boolean(&slurm_cgroup_conf.ignore_systemd,
				    "IgnoreSystemd", tbl)) {
			/* Implicitly set these other one. */
			slurm_cgroup_conf.ignore_systemd_on_failure = true;
		}

		if (!slurm_cgroup_conf.ignore_systemd &&
		    (!s_p_get_boolean(
			    &slurm_cgroup_conf.ignore_systemd_on_failure,
			    "IgnoreSystemdOnFailure", tbl)))
			slurm_cgroup_conf.ignore_systemd_on_failure = false;

		(void) s_p_get_boolean(&slurm_cgroup_conf.root_owned_cgroups,
				       "RootOwnedCgroups", tbl);

		(void) s_p_get_boolean(&slurm_cgroup_conf.enable_controllers,
				       "EnableControllers", tbl);

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);

	return;
}

/* Autodetect logic inspired from systemd source code */
extern char *autodetect_cgroup_version(void)
{
	struct statfs fs;
	int cgroup_ver = -1;

	if (statfs("/sys/fs/cgroup/", &fs) < 0) {
		error("cgroup filesystem not mounted in /sys/fs/cgroup/");
		return NULL;
	}

	if (F_TYPE_EQUAL(fs.f_type, CGROUP2_SUPER_MAGIC))
		cgroup_ver = 2;
	else if (F_TYPE_EQUAL(fs.f_type, TMPFS_MAGIC)) {
		if (statfs("/sys/fs/cgroup/systemd/", &fs) != 0) {
			error("can't stat /sys/fs/cgroup/systemd/: %m");
			return NULL;
		}

		if (F_TYPE_EQUAL(fs.f_type, CGROUP2_SUPER_MAGIC)) {
			if (statfs("/sys/fs/cgroup/unified/", &fs) != 0) {
				error("can't stat /sys/fs/cgroup/unified/: %m");
				return NULL;
			}
			cgroup_ver = 2;
		} else if (F_TYPE_EQUAL(fs.f_type, CGROUP_SUPER_MAGIC)) {
			cgroup_ver = 1;
		} else {
			error("Unexpected fs type on /sys/fs/cgroup/systemd");
			return NULL;
		}
	} else if (F_TYPE_EQUAL(fs.f_type, SYSFS_MAGIC)) {
		error("No filesystem mounted on /sys/fs/cgroup");
		return NULL;
	} else {
		error("Unknown filesystem type mounted on /sys/fs/cgroup");
		return NULL;
	}

	log_flag(CGROUP, "%s: using cgroup version %d", __func__, cgroup_ver);

	switch (cgroup_ver) {
	case 1:
		return "cgroup/v1";
		break;
	case 2:
		return "cgroup/v2";
		break;
	default:
		error("unsupported cgroup version %d", cgroup_ver);
		break;
	}
	return NULL;
}

/*
 * cgroup_conf_init - load the cgroup.conf configuration.
 *
 * RET SLURM_SUCCESS if conf file is initialized. If the cgroup conf was
 *     already initialized, return SLURM_ERROR.
 */
extern int cgroup_conf_init(void)
{
	int rc = SLURM_SUCCESS;

	slurm_rwlock_wrlock(&cg_conf_lock);

	if (!cg_conf_inited) {
		_init_slurm_cgroup_conf();
		_read_slurm_cgroup_conf();
		/*
		 * Initialize and pack cgroup.conf info into a buffer that can
		 * be used by slurmd to send to stepd every time, instead of
		 * re-packing every time we want to send to slurmstepd
		 */
		cg_conf_buf = init_buf(0);
		_pack_cgroup_conf(cg_conf_buf);
		cg_conf_inited = true;
	} else
		rc = SLURM_ERROR;

	slurm_rwlock_unlock(&cg_conf_lock);
	return rc;
}

extern void cgroup_conf_destroy(void)
{
	xassert(cg_conf_inited);
	_cgroup_conf_fini();
}

extern void cgroup_conf_reinit(void)
{
	cgroup_conf_destroy();
	cgroup_conf_init();
}

extern void cgroup_free_limits(cgroup_limits_t *limits)
{
	if (!limits)
		return;

	xfree(limits->allow_cores);
	xfree(limits->allow_mems);
	xfree(limits);
}

extern void cgroup_init_limits(cgroup_limits_t *limits)
{
	if (!limits)
		return;

	memset(limits, 0, sizeof(*limits));

	limits->taskid = NO_VAL;
	limits->device.type = DEV_TYPE_NONE;
	limits->device.major = NO_VAL;
	limits->device.minor = NO_VAL;
	limits->limit_in_bytes = NO_VAL64;
	limits->soft_limit_in_bytes = NO_VAL64;
	limits->kmem_limit_in_bytes = NO_VAL64;
	limits->memsw_limit_in_bytes = NO_VAL64;
	limits->swappiness = NO_VAL64;
}

/*
 * get_slurm_cgroup_conf - load the Slurm cgroup configuration from the
 *      cgroup.conf  file and return a key pair <name,value> ordered list.
 * RET List with cgroup.conf <name,value> pairs if no error, NULL otherwise.
 */
extern List cgroup_get_conf_list(void)
{
	config_key_pair_t *key_pair;
	List cgroup_conf_l;
	cgroup_conf_t *cg_conf = &slurm_cgroup_conf;

	xassert(cg_conf_inited);

	slurm_rwlock_rdlock(&cg_conf_lock);

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
	key_pair->name = xstrdup("MemorySwappiness");
	if (cg_conf->memory_swappiness != NO_VAL64)
		key_pair->value = xstrdup_printf("%"PRIu64,
						 cg_conf->memory_swappiness);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CgroupPlugin");
	key_pair->value = xstrdup(cg_conf->cgroup_plugin);
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("IgnoreSystemd");
	key_pair->value = xstrdup_printf("%s",
					 cg_conf->ignore_systemd ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("IgnoreSystemdOnFailure");
	key_pair->value = xstrdup_printf("%s",
					 cg_conf->ignore_systemd_on_failure ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EnableControllers");
	key_pair->value = xstrdup_printf("%s",
					 cg_conf->enable_controllers ?
					 "yes" : "no");
	list_append(cgroup_conf_l, key_pair);

	list_sort(cgroup_conf_l, (ListCmpF) sort_key_pairs);

	slurm_rwlock_unlock(&cg_conf_lock);

	return cgroup_conf_l;
}

extern int cgroup_write_conf(int fd)
{
	int len;

	xassert(cg_conf_inited);

	slurm_rwlock_rdlock(&cg_conf_lock);
	len = get_buf_offset(cg_conf_buf);
	safe_write(fd, &len, sizeof(int));
	safe_write(fd, get_buf_data(cg_conf_buf), len);
	slurm_rwlock_unlock(&cg_conf_lock);

	return SLURM_SUCCESS;
rwfail:
	slurm_rwlock_unlock(&cg_conf_lock);
	return SLURM_ERROR;
}

extern int cgroup_read_conf(int fd)
{
	int len, rc;
	buf_t *buffer = NULL;

	slurm_rwlock_wrlock(&cg_conf_lock);

	if (cg_conf_inited)
		_clear_slurm_cgroup_conf();

	safe_read(fd, &len, sizeof(int));
	buffer = init_buf(len);
	safe_read(fd, buffer->head, len);

	rc = _unpack_cgroup_conf(buffer);

	if (rc == SLURM_ERROR)
		fatal("%s: problem with unpack of cgroup.conf", __func__);

	FREE_NULL_BUFFER(buffer);

	cg_conf_inited = true;
	slurm_rwlock_unlock(&cg_conf_lock);

	return SLURM_SUCCESS;
rwfail:
	slurm_rwlock_unlock(&cg_conf_lock);
	FREE_NULL_BUFFER(buffer);

	return SLURM_ERROR;
}

extern bool cgroup_memcg_job_confinement(void)
{
	bool status = false;

	xassert(cg_conf_inited);

	/* read cgroup configuration */
	slurm_rwlock_rdlock(&cg_conf_lock);

	if ((slurm_cgroup_conf.constrain_ram_space ||
	     slurm_cgroup_conf.constrain_swap_space) &&
	    xstrstr(slurm_conf.task_plugin, "cgroup"))
		status = true;

	slurm_rwlock_unlock(&cg_conf_lock);

	return status;
}

/*
 * Initialize Cgroup plugins.
 *
 * Returns a Slurm errno.
 */
extern int cgroup_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = "cgroup";
	char *type = NULL;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	cgroup_conf_init();
	type = slurm_cgroup_conf.cgroup_plugin;

	if (!xstrcmp(type, "autodetect")) {
		if (!(type = autodetect_cgroup_version())) {
			rc = SLURM_ERROR;
			goto done;
		}
	}

	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		rc = SLURM_ERROR;
		goto done;
	}

done:
	slurm_mutex_unlock(&g_context_lock);

	return rc;
}

extern int cgroup_g_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	slurm_mutex_unlock(&g_context_lock);

	cgroup_conf_destroy();

	return rc;
}

extern int cgroup_g_initialize(cgroup_ctl_type_t sub)
{
	xassert(g_context);

	return (*(ops.initialize))(sub);
}

extern int cgroup_g_system_create(cgroup_ctl_type_t sub)
{
	xassert(g_context);

	return (*(ops.system_create))(sub);
}

extern int cgroup_g_system_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids)
{
	xassert(g_context);

	return (*(ops.system_addto))(sub, pids, npids);
}

extern int cgroup_g_system_destroy(cgroup_ctl_type_t sub)
{
	xassert(g_context);

	return (*(ops.system_destroy))(sub);
}

extern int cgroup_g_step_create(cgroup_ctl_type_t sub, stepd_step_rec_t *step)
{
	xassert(g_context);

	return (*(ops.step_create))(sub, step);
}

extern int cgroup_g_step_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids)
{
	xassert(g_context);

	return (*(ops.step_addto))(sub, pids, npids);
}

extern int cgroup_g_step_get_pids(pid_t **pids, int *npids)
{
	xassert(g_context);

	return (*(ops.step_get_pids))(pids, npids);
}

extern int cgroup_g_step_suspend(void)
{
	xassert(g_context);

	return (*(ops.step_suspend))();
}

extern int cgroup_g_step_resume(void)
{
	xassert(g_context);

	return (*(ops.step_resume))();
}

extern int cgroup_g_step_destroy(cgroup_ctl_type_t sub)
{
	xassert(g_context);

	return (*(ops.step_destroy))(sub);
}

extern bool cgroup_g_has_pid(pid_t pid)
{
	xassert(g_context);

	return (*(ops.has_pid))(pid);
}

extern cgroup_limits_t *cgroup_g_constrain_get(cgroup_ctl_type_t sub,
					       cgroup_level_t level)
{
	xassert(g_context);

	return (*(ops.constrain_get))(sub, level);
}

extern int cgroup_g_constrain_set(cgroup_ctl_type_t sub, cgroup_level_t level,
				  cgroup_limits_t *limits)
{
	xassert(g_context);

	return (*(ops.constrain_set))(sub, level, limits);
}

extern int cgroup_g_constrain_apply(cgroup_ctl_type_t sub, cgroup_level_t level,
                                    uint32_t task_id)
{
	xassert(g_context);

	return (*(ops.constrain_apply))(sub, level, task_id);
}

extern int cgroup_g_step_start_oom_mgr()
{
	xassert(g_context);

	return (*(ops.step_start_oom_mgr))();
}

extern cgroup_oom_t *cgroup_g_step_stop_oom_mgr(stepd_step_rec_t *step)
{
	xassert(g_context);

	return (*(ops.step_stop_oom_mgr))(step);
}

extern int cgroup_g_task_addto(cgroup_ctl_type_t sub, stepd_step_rec_t *step,
			       pid_t pid, uint32_t task_id)
{
	xassert(g_context);

	return (*(ops.task_addto))(sub, step, pid, task_id);
}

extern cgroup_acct_t *cgroup_g_task_get_acct_data(uint32_t taskid)
{
	xassert(g_context);

	return (*(ops.task_get_acct_data))(taskid);
}

extern long int cgroup_g_get_acct_units()
{
	xassert(g_context);

	return (*(ops.get_acct_units))();
}

extern bool cgroup_g_has_feature(cgroup_ctl_feature_t f)
{
	xassert(g_context);

	return (*(ops.has_feature))(f);
}
