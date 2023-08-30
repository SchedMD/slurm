/*****************************************************************************\
 *  Copyright (C) 2019 SchedMD LLC
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include <check.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_opt.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static void _help(void)
{
}

static void _usage(void)
{
}

START_TEST(test_data_job_macros)
{
	char *opt_string = NULL;
	sbatch_opt_t sbopt = { 0 };
	srun_opt_t sropt = { 0 };
	slurm_opt_t opt = { .sbatch_opt = &sbopt,
			    .srun_opt = &sropt,
			    .help_func = _help,
			    .usage_func = _usage };
	struct option *spanked = slurm_option_table_create(&opt, &opt_string);
	data_t *errors = data_set_list(data_new());
	data_t *arg = data_new();
	slurm_reset_all_options(&opt, true);

	/* COMMON_STRING_OPTION */
	data_set_string(arg, "wckey");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_WCKEY, arg,
						errors) == 0, "LONG_OPT_WCKEY");
	ck_assert_msg(!xstrcmp(opt.wckey, "wckey"), "wckey");
	/* COMMON_SBATCH_STRING_OPTION */
	data_set_bool(arg, true);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_BATCH, arg,
						errors) == 0, "LONG_OPT_BATCH");
	ck_assert_msg(!xstrcmp(opt.sbatch_opt->batch_features, "true"),
		      "batch_features");
	/* COMMON_BOOL_OPTION */
	data_set_bool(arg, true);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_CONTIGUOUS, arg,
						errors) == 0,
		      "LONG_OPT_CONTIGUOUS=true");
	ck_assert_msg(opt.contiguous == true, "contiguous=true");
	data_set_bool(arg, false);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_CONTIGUOUS, arg,
						errors) == 0,
		      "LONG_OPT_CONTIGUOUS=false");
	ck_assert_msg(opt.contiguous == false, "contiguous=false");
	/* COMMON_INT_OPTION */
	data_set_string(arg, "12345");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_CPUS_PER_GPU,
						arg, errors) == 0,
		      "LONG_OPT_CPUS_PER_GPU");
	ck_assert_msg(opt.cpus_per_gpu == 12345, "cpus_per_gpu");
	data_set_string(arg, "0");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_CPUS_PER_GPU,
						arg, errors) == 0,
		      "LONG_OPT_CPUS_PER_GPU");
	ck_assert_msg(opt.cpus_per_gpu == 0, "cpus_per_gpu");
	/* COMMON_MBYTES_OPTION */
	data_set_string(arg, "1");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_TMP, arg,
						errors) == 0, "LONG_OPT_TMP");
	ck_assert_msg(opt.pn_min_tmp_disk == 1, "pn_min_tmp_disk");
	data_set_string(arg, "1k");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_TMP, arg,
						errors) == 0, "LONG_OPT_TMP");
	ck_assert_msg(opt.pn_min_tmp_disk == 1, "pn_min_tmp_disk");
	data_set_string(arg, "10M");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_TMP, arg,
						errors) == 0, "LONG_OPT_TMP");
	ck_assert_msg(opt.pn_min_tmp_disk == 10, "pn_min_tmp_disk");

	slurm_option_table_destroy(spanked);
	xfree(opt_string);
	FREE_NULL_DATA(arg);
	FREE_NULL_DATA(errors);
	slurm_free_options_members(&opt);
}
END_TEST

/* test non-macro options */
START_TEST(test_data_job)
{
	char *opt_string = NULL;
	sbatch_opt_t sbopt = { 0 };
	srun_opt_t sropt = { 0 };
	salloc_opt_t saopt = { 0 };
	slurm_opt_t opt = { .sbatch_opt = &sbopt,
			    .srun_opt = &sropt,
			    .salloc_opt = &saopt,
			    .help_func = _help,
			    .usage_func = _usage };
	struct option *spanked = slurm_option_table_create(&opt, &opt_string);
	data_t *errors = data_set_list(data_new());
	data_t *arg = data_new();
	slurm_reset_all_options(&opt, true);

	data_set_string(arg, "2000-01-01");
	ck_assert_msg(slurm_process_option_data(&opt, 'b', arg, errors) == 0,
		      "begin");
	ck_assert_msg(opt.begin == parse_time("2000-01-01", 0), "begin value");

	data_set_string(arg, "invalid time");
	ck_assert_msg(slurm_process_option_data(&opt, 'b', arg, errors) != 0,
		      "begin");
	ck_assert_msg(opt.begin == 0, "begin invalid");

	data_set_string(arg, "2");
	ck_assert_msg(slurm_process_option_data(&opt, 'S', arg, errors) == 0,
		      "core spec");
	ck_assert_msg(opt.core_spec == 2, "core spec 2");
	ck_assert_msg(opt.srun_opt->core_spec_set == true, "core spec set");

	data_set_string(arg, "0");
	ck_assert_msg(slurm_process_option_data(&opt, 'S', arg, errors) == 0,
		      "core spec");
	ck_assert_msg(opt.core_spec == 0, "core spec 0");
	ck_assert_msg(opt.srun_opt->core_spec_set == false, "core spec unset");

	opt.core_spec = 1234;
	opt.srun_opt->core_spec_set = true;

	data_set_string(arg, "taco");
	ck_assert_msg(slurm_process_option_data(&opt, 'S', arg, errors) != 0,
		      "core spec");
	ck_assert_msg(opt.core_spec == 1234, "core spec nochange");
	ck_assert_msg(opt.srun_opt->core_spec_set == true,
		      "core spec nochange");

	/* force enable all governors */
	slurm_conf.cpu_freq_govs = 0xffffffff;
	data_set_string(arg, "10-100:PowerSave");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_CPU_FREQ, arg,
						errors) == 0, "cpu freq");
	ck_assert_msg(opt.cpu_freq_min == 10, "cpu min freq");
	ck_assert_msg(opt.cpu_freq_max == 100, "cpu max freq");
	ck_assert_msg(opt.cpu_freq_gov == (CPU_FREQ_POWERSAVE |
					   CPU_FREQ_RANGE_FLAG),
		      "cpu freq gov");

	data_set_string(arg, "low-highm1:Performance");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_CPU_FREQ, arg,
						errors) == 0, "cpu freq");
	ck_assert_msg(opt.cpu_freq_min == CPU_FREQ_LOW, "cpu min freq");
	ck_assert_msg(opt.cpu_freq_max == CPU_FREQ_HIGHM1, "cpu max freq");
	ck_assert_msg(opt.cpu_freq_gov == (CPU_FREQ_PERFORMANCE |
					   CPU_FREQ_RANGE_FLAG),
		      "cpu freq gov");

	opt.cpu_freq_min = 12345;
	opt.cpu_freq_max = 12345;
	opt.cpu_freq_gov = 12345;
	data_set_string(arg, "Performance");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_CPU_FREQ, arg,
						errors) == 0, "cpu freq");
	ck_assert_msg(opt.cpu_freq_min == NO_VAL, "cpu min freq");
	ck_assert_msg(opt.cpu_freq_max == NO_VAL, "cpu max freq");
	ck_assert_msg(opt.cpu_freq_gov == (CPU_FREQ_PERFORMANCE |
					   CPU_FREQ_RANGE_FLAG),
		      "cpu freq gov");

	opt.cpu_freq_min = 12345;
	opt.cpu_freq_max = 12345;
	opt.cpu_freq_gov = 12345;
	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_CPU_FREQ, arg,
						errors) != 0, "cpu freq");
	ck_assert_msg(opt.cpu_freq_min == NO_VAL, "cpu min freq");
	ck_assert_msg(opt.cpu_freq_max == NO_VAL, "cpu max freq");
	ck_assert_msg(opt.cpu_freq_gov == NO_VAL, "cpu freq gov");

	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, 'c', arg, errors) != 0,
		      "cpus per task");
	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, 'c', arg, errors) != 0,
		      "cpus per task");
	data_set_string(
		arg,
		"99999999999999999999999999999999999999999999999999999999");
	ck_assert_msg(slurm_process_option_data(&opt, 'c', arg, errors) != 0,
		      "cpus per task");
	data_set_string(arg, "-1");
	ck_assert_msg(slurm_process_option_data(&opt, 'c', arg, errors) != 0,
		      "cpus per task");
	data_set_string(arg, "0");
	ck_assert_msg(slurm_process_option_data(&opt, 'c', arg, errors) != 0,
		      "cpus per task");
	data_set_int(arg, 0);
	ck_assert_msg(slurm_process_option_data(&opt, 'c', arg, errors) != 0,
		      "cpus per task");
	data_set_int(arg, 10);
	ck_assert_msg(slurm_process_option_data(&opt, 'c', arg, errors) == 0,
		      "cpus per task");
	ck_assert_msg(opt.cpus_per_task == 10, "cpus per task 10");
	ck_assert_msg(opt.cpus_set == true, "cpus set");

	data_set_string(arg, "2000-01-01");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_DEADLINE, arg,
						errors) == 0, "deadline");
	ck_assert_msg(opt.deadline == parse_time("2000-01-01", 0),
		      "deadline value");
	data_set_string(arg, "invalid time");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_DEADLINE, arg,
						errors) != 0, "deadline");
	ck_assert_msg(opt.deadline == 0, "deadline invalid");

	data_set_string(arg, "60");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_DELAY_BOOT, arg,
						errors) == 0, "delay boot");
	ck_assert_msg(opt.delay_boot == (60 * 60), "delay boot value");
	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_DELAY_BOOT, arg,
						errors) != 0, "delay boot");

	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, 'm', arg, errors) != 0,
		      "distribution");
	ck_assert_msg(opt.distribution == SLURM_ERROR, "distribution value");
	ck_assert_msg(opt.plane_size == NO_VAL, "distribution value");

	data_set_string(arg, "cyclic:block:fcyclic");
	ck_assert_msg(slurm_process_option_data(&opt, 'm', arg, errors) == 0,
		      "distribution");
	ck_assert_msg(opt.distribution == SLURM_DIST_CYCLIC_BLOCK_CFULL,
		      "distribution value");
	ck_assert_msg(opt.plane_size == NO_VAL, "distribution value");

	data_set_string(arg, "plane=10");
	ck_assert_msg(slurm_process_option_data(&opt, 'm', arg, errors) == 0,
		      "distribution");
	ck_assert_msg(opt.distribution == SLURM_DIST_PLANE,
		      "distribution value");
	ck_assert_msg(opt.plane_size == 10, "distribution value");

	data_set_string(arg, "/dev/stderr");
	ck_assert_msg(slurm_process_option_data(&opt, 'e', arg, errors) == 0,
		      "stderr");
	ck_assert_msg(!xstrcmp(opt.efname, "/dev/stderr"), "stderr value");
	data_set_string(arg, "none");
	ck_assert_msg(slurm_process_option_data(&opt, 'e', arg, errors) == 0,
		      "stderr");
	ck_assert_msg(!xstrcmp(opt.efname, "/dev/null"), "stderr value");

	data_set_string(arg, "exclusive");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_EXCLUSIVE, arg,
						errors) == 0, "exclusive");
	ck_assert_msg(opt.shared == JOB_SHARED_NONE, "exclusive value");
	ck_assert_msg(opt.srun_opt->exclusive == true, "srun excl");

	data_set_string(arg, "tacos");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_EXCLUSIVE, arg,
						errors) != 0, "exclusive");

	data_set_string(arg, "tacos");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GET_USER_ENV,
						arg, errors) != 0,
		      "get user env");

	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GET_USER_ENV,
						arg, errors) == 0,
		      "get user env");
	ck_assert_msg(opt.get_user_env_time == 0, "get user env timeout");
	ck_assert_msg(opt.get_user_env_mode == -1, "get user mode");

	data_set_string(arg, "10l");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GET_USER_ENV,
						arg, errors) == 0,
		      "get user env");
	ck_assert_msg(opt.get_user_env_time == 10, "get user env timeout");
	ck_assert_msg(opt.get_user_env_mode == 2, "get user mode");

	opt.gid = NO_VAL;
	data_set_string(arg, "invalid-group-tacos");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GID, arg, errors)
		      != 0, "gid");
	ck_assert_msg(opt.gid == NO_VAL, "gid value");
	/* verify that group of slurmuser can be used */
	data_set_string(arg, slurm_conf.slurm_user_name);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GID, arg, errors)
		      == 0, "gid");
	ck_assert_msg(opt.gid == gid_from_uid(slurm_conf.slurm_user_id),
		      "gid value");

	data_set_string(arg, "help");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GRES, arg,
						errors) != 0, "gres");
	data_set_string(arg, "list");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GRES, arg,
						errors) != 0, "gres");
	data_set_string(arg, "gpu:10");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GRES, arg,
						errors) == 0, "gres");
	ck_assert_msg(!xstrcmp(opt.gres, "gres:gpu:10"), "gres value");

	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GRES_FLAGS, arg,
						errors) != 0, "gres flags");
	opt.job_flags = 0;
	data_set_string(arg, "disable-binding");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GRES_FLAGS, arg,
						errors) == 0, "gres flags");
	ck_assert_msg(opt.job_flags == GRES_DISABLE_BIND, "gres flags value");
	opt.job_flags = 0;
	data_set_string(arg, "enforce-binding");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_GRES_FLAGS, arg,
						errors) == 0, "gres flags");
	ck_assert_msg(opt.job_flags == GRES_ENFORCE_BIND, "gres flags value");

	data_set_string(arg, "/dev/stdin");
	ck_assert_msg(slurm_process_option_data(&opt, 'i', arg, errors) == 0,
		      "stdin");
	ck_assert_msg(!xstrcmp(opt.ifname, "/dev/stdin"), "stdin value");
	data_set_string(arg, "none");
	ck_assert_msg(slurm_process_option_data(&opt, 'i', arg, errors) == 0,
		      "stdin");
	ck_assert_msg(!xstrcmp(opt.ifname, "/dev/null"), "stdin value");

	opt.job_flags = 0;
	data_set_string(arg, "true");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_KILL_INV_DEP,
						arg, errors) == 0,
		      "kill on invalid dep");
	ck_assert_msg(opt.job_flags == KILL_INV_DEP,
		      "kill on invalid dep value");
	opt.job_flags = 0;
	data_set_string(arg, "false");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_KILL_INV_DEP,
						arg, errors) == 0,
		      "kill on invalid dep");
	ck_assert_msg(opt.job_flags == NO_KILL_INV_DEP,
		      "kill on invalid dep value");
	opt.job_flags = 0;
	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_KILL_INV_DEP,
						arg, errors) == 0,
		      "kill on invalid dep");
	ck_assert_msg(opt.job_flags == NO_KILL_INV_DEP,
		      "kill on invalid dep value");

	opt.mail_type = 0;
	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_MAIL_TYPE, arg,
						errors) != 0,
		      "kill on invalid dep");
	ck_assert_msg(opt.mail_type == INFINITE16, "kill on invalid dep value");
	opt.mail_type = 0;
	data_set_string(arg, "BEGIN,END");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_MAIL_TYPE, arg,
						errors) == 0, "mail type");
	ck_assert_msg(opt.mail_type == (MAIL_JOB_BEGIN | MAIL_JOB_END),
		      "mail type value");
	opt.mail_type = 0;
	data_set_string(arg, "none");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_MAIL_TYPE, arg,
						errors) == 0, "mail type");
	ck_assert_msg(opt.mail_type == 0, "mail type value");

	data_set_string(arg, "-1");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_MEM, arg, errors)
		      != 0, "memory");
	data_set_string(arg, "10M");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_MEM, arg, errors)
		      == 0, "memory");
	ck_assert_msg(opt.pn_min_memory == 10, "memory value");

	data_set_string(arg, "-1");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_MEM_BIND, arg,
						errors) != 0, "memory bind");
	data_set_string(arg, "help");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_MEM_BIND, arg,
						errors) != 0, "memory bind");
	opt.mem_bind_type = 0;
	data_set_string(arg, "sort,verbose");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_MEM_BIND, arg,
						errors) == 0, "memory bind");
	ck_assert_msg(opt.mem_bind == 0, "memory bind value");
	ck_assert_msg(opt.mem_bind_type == (MEM_BIND_SORT | MEM_BIND_VERBOSE),
		      "memory bind type value");
	opt.mem_bind_type = 0;
	xfree(opt.mem_bind);
	data_set_string(arg, "rank");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_MEM_BIND, arg,
						errors) == 0, "memory bind");
	ck_assert_msg(opt.mem_bind == 0, "memory bind value");
	ck_assert_msg(opt.mem_bind_type == MEM_BIND_RANK,
		      "memory bind type value");
	opt.mem_bind_type = 0;
	xfree(opt.mem_bind);
	data_set_string(arg, "MAP_MEM:0,1");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_MEM_BIND, arg,
						errors) == 0, "memory bind");
	ck_assert_msg(!xstrcmp(opt.mem_bind, "0,1"), "memory bind value");
	ck_assert_msg(opt.mem_bind_type == MEM_BIND_MAP,
		      "memory bind type value");

	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_NICE, arg,
						errors) == 0, "nice");
	ck_assert_msg(opt.nice == 100, "nice value");
	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_NICE, arg,
						errors) != 0, "nice");
	data_set_string(arg, "900000000000000000000000");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_NICE, arg,
						errors) != 0, "nice");

	data_set_string(arg, "set");
	ck_assert_msg(slurm_process_option_data(&opt, 'k', arg, errors) == 0,
		      "no kill");
	ck_assert_msg(opt.no_kill == true, "no kill value");
	data_set_string(arg, "off");
	ck_assert_msg(slurm_process_option_data(&opt, 'k', arg, errors) == 0,
		      "no kill");
	ck_assert_msg(opt.no_kill == false, "no kill value");
	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, 'k', arg, errors) == 0,
		      "no kill");
	ck_assert_msg(opt.no_kill == true, "no kill value");
	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, 'k', arg, errors) != 0,
		      "no kill");

	opt.sbatch_opt->requeue = true;
	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_NO_REQUEUE, arg,
						errors) == 0, "no requeue");
	ck_assert_msg(opt.sbatch_opt->requeue == false, "no requeue value");

	data_set_string(arg, "hostlist");
	ck_assert_msg(slurm_process_option_data(&opt, 'w', arg, errors) == 0,
		      "nodelist");
	ck_assert_msg(!xstrcmp(opt.nodelist, "hostlist"), "nodelist check");
	ck_assert_msg(opt.nodefile == 0, "verify no nodefile");

	data_set_string(arg, "1-2");
	ck_assert_msg(slurm_process_option_data(&opt, 'N', arg, errors) == 0,
		      "nodes");
	ck_assert_msg(opt.min_nodes == 1, "min nodes count");
	ck_assert_msg(opt.max_nodes == 2, "mxn nodes count");
	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, 'N', arg, errors) != 0,
		      "nodes");
	data_set_list(arg);
	data_set_string(data_list_append(arg), "10");
	data_set_string(data_list_append(arg), "100");
	ck_assert_msg(slurm_process_option_data(&opt, 'N', arg, errors) == 0,
		      "nodes");
	ck_assert_msg(opt.min_nodes == 10, "min nodes count");
	ck_assert_msg(opt.max_nodes == 100, "mxn nodes count");
	data_set_string(data_list_append(arg), "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, 'N', arg, errors) != 0,
		      "nodes");

	data_set_string(arg, "100");
	ck_assert_msg(slurm_process_option_data(&opt, 'n', arg, errors) == 0,
		      "ntasks");
	ck_assert_msg(opt.ntasks == 100, "ntasks value");
	ck_assert_msg(opt.ntasks_set == true, "ntasks value");
	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, 'n', arg, errors) != 0,
		      "ntasks");
	data_set_string(arg, "-1");
	ck_assert_msg(slurm_process_option_data(&opt, 'n', arg, errors) != 0,
		      "ntasks");

	data_set_string(arg, "append");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_OPEN_MODE, arg,
						errors) == 0, "open mode");
	ck_assert_msg(opt.open_mode == OPEN_MODE_APPEND, "open mode value");
	data_set_string(arg, "truncate");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_OPEN_MODE, arg,
						errors) == 0, "open mode");
	ck_assert_msg(opt.open_mode == OPEN_MODE_TRUNCATE, "open mode value");
	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_OPEN_MODE, arg,
						errors) != 0, "open mode");

	data_set_string(arg, "/dev/stdout");
	ck_assert_msg(slurm_process_option_data(&opt, 'o', arg, errors) == 0,
		      "stdout");
	ck_assert_msg(!xstrcmp(opt.ofname, "/dev/stdout"), "stdout value");
	data_set_string(arg, "none");
	ck_assert_msg(slurm_process_option_data(&opt, 'o', arg, errors) == 0,
		      "stdout");
	ck_assert_msg(!xstrcmp(opt.ofname, "/dev/null"), "stdout value");

	opt.srun_opt->exclusive = true;
	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, 's', arg, errors) == 0,
		      "oversubscribe");
	ck_assert_msg(opt.srun_opt->exclusive == false, "oversubscribe");

	data_set_string(arg, "top");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_PRIORITY, arg,
						errors) == 0, "priority");
	ck_assert_msg(opt.priority == (NO_VAL - 1), "priority value");
	data_set_string(arg, "100");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_PRIORITY, arg,
						errors) == 0, "priority");
	ck_assert_msg(opt.priority == 100, "priority value");
	data_set_string(arg, "-100");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_PRIORITY, arg,
						errors) != 0, "priority");
	data_set_string(arg, "8832828382838283892839823928392");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_PRIORITY, arg,
						errors) != 0, "priority");

	opt.sbatch_opt->requeue = 12345;
	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_REQUEUE, arg,
						errors) == 0, "requeue");
	ck_assert_msg(opt.sbatch_opt->requeue == 1, "requeue value");

	opt.job_flags = 0;
	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_SPREAD_JOB, arg,
						errors) == 0, "spread value");
	ck_assert_msg(opt.job_flags == SPREAD_JOB, "spread job value");

	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_SWITCH_WAIT, arg,
						errors) == 0, "switch wait");
	ck_assert_msg(opt.wait4switch == NO_VAL, "switch wait value");
	data_set_string(arg, "-1");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_SWITCH_WAIT, arg,
						errors) == 0, "switch wait");
	ck_assert_msg(opt.wait4switch == INFINITE, "switch wait value");
	data_set_string(arg, "60");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_SWITCH_WAIT, arg,
						errors) == 0, "switch wait");
	ck_assert_msg(opt.wait4switch == (60 * 60), "switch wait value");

	opt.wait4switch = 12345;
	opt.req_switch = 1;
	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_SWITCHES, arg,
						errors) == 0, "switches");
	ck_assert_msg(opt.req_switch == 0, "switches value");
	ck_assert_msg(opt.wait4switch == 12345, "wait 4 switches value");
	opt.wait4switch = 12345;
	opt.req_switch = 1;
	data_set_string(arg, "10@16");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_SWITCHES, arg,
						errors) == 0, "switches");
	ck_assert_msg(opt.req_switch == 10, "switches value");
	ck_assert_msg(opt.wait4switch == (16 * 60), "wait 4 switches value");
	opt.wait4switch = 12345;
	opt.req_switch = 1;
	data_set_string(arg, "10");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_SWITCHES, arg,
						errors) == 0, "switches");
	ck_assert_msg(opt.req_switch == 10, "switches value");
	ck_assert_msg(opt.wait4switch == 12345, "wait 4 switches value");
	data_set_dict(arg);
	data_set_string(data_key_set(arg, "count"), "10");
	data_set_string(data_key_set(arg, "timeout"), "16");
	opt.wait4switch = 12345;
	opt.req_switch = 1;
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_SWITCHES, arg,
						errors) == 0, "switches");
	ck_assert_msg(opt.req_switch == 10, "switches value");
	ck_assert_msg(opt.wait4switch == (16 * 60), "wait 4 switches value");
	data_set_dict(arg);
	data_set_string(data_key_set(arg, "count"), "10");
	opt.wait4switch = 12345;
	opt.req_switch = 1;
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_SWITCHES, arg,
						errors) == 0, "switches");
	ck_assert_msg(opt.req_switch == 10, "switches value");
	ck_assert_msg(opt.wait4switch == 12345, "wait 4 switches value");
	data_set_dict(arg);
	data_set_string(data_key_set(arg, "timeout"), "16");
	opt.wait4switch = 12345;
	opt.req_switch = 1;
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_SWITCHES, arg,
						errors) == 0, "switches");
	ck_assert_msg(opt.req_switch == 1, "switches value");
	ck_assert_msg(opt.wait4switch == (16 * 60), "wait 4 switches value");

	opt.sbatch_opt->test_only = 0;
	opt.srun_opt->test_only = 0;
	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_TEST_ONLY, arg,
						errors) == 0, "test-only");
	ck_assert_msg(opt.sbatch_opt->test_only == 1, "test-only value");
	ck_assert_msg(opt.srun_opt->test_only == 1, "test-only value");

	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_THREAD_SPEC, arg,
						errors) != 0, "thread-spec");
	data_set_string(arg, "1245");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_THREAD_SPEC, arg,
						errors) == 0, "thread-spec");
	ck_assert_msg(opt.core_spec == (1245 | CORE_SPEC_THREAD),
		      "thread-spec value");
	data_set_int(arg, CORE_SPEC_THREAD);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_THREAD_SPEC, arg,
						errors) != 0, "thread-spec");
	data_set_int(arg, 0);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_THREAD_SPEC, arg,
						errors) != 0, "thread-spec");

	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, 't', arg, errors) != 0,
		      "time-limit");
	data_set_string(arg, "0");
	ck_assert_msg(slurm_process_option_data(&opt, 't', arg, errors) == 0,
		      "time-limit");
	ck_assert_msg(opt.time_limit == INFINITE, "time-limit value");
	data_set_string(arg, "60");
	ck_assert_msg(slurm_process_option_data(&opt, 't', arg, errors) == 0,
		      "time-limit");
	ck_assert_msg(opt.time_limit == 60, "time-limit value");

	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_TIME_MIN, arg,
						errors) != 0, "time-min");
	data_set_string(arg, "0");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_TIME_MIN, arg,
						errors) == 0, "time-min");
	ck_assert_msg(opt.time_min == INFINITE, "time-min value");
	data_set_string(arg, "60");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_TIME_MIN, arg,
						errors) == 0, "time-min");
	ck_assert_msg(opt.time_min == 60, "time_min value");

	opt.uid = NO_VAL;
	data_set_string(arg, "invalid-group-tacos");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_UID, arg, errors)
		      != 0, "gid");
	ck_assert_msg(opt.uid == NO_VAL, "uid value");
	/* verify that slurmuser can be used */
	data_set_string(arg, slurm_conf.slurm_user_name);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_UID, arg, errors)
		      == 0, "uid");
	ck_assert_msg(opt.uid == slurm_conf.slurm_user_id, "uid value");

	data_set_string(arg, "invalid");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_UMASK, arg,
						errors) != 0, "umask");
	data_set_string(arg, "0770");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_UMASK, arg,
						errors) == 0, "umask");
	ck_assert_msg(opt.sbatch_opt->umask == 00770, "umask value");
	data_set_string(arg, "0");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_UMASK, arg,
						errors) == 0, "umask");
	ck_assert_msg(opt.sbatch_opt->umask == 0, "umask value");

	opt.job_flags = 0;
	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_USE_MIN_NODES,
						arg, errors) == 0,
		      "use min nodes");
	ck_assert_msg(opt.job_flags == USE_MIN_NODES, "use min nodes value");

	data_set_null(arg);
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_WAIT_ALL_NODES,
						arg, errors) != 0,
		      "wait-all-nodes");
	data_set_string(arg, "0");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_WAIT_ALL_NODES,
						arg, errors) == 0,
		      "wait-all-nodes");
	ck_assert_msg(opt.salloc_opt->wait_all_nodes == 0,
		      "wait-all-nodes value");
	ck_assert_msg(opt.sbatch_opt->wait_all_nodes == 0,
		      "wait-all-nodes value");
	data_set_string(arg, "1");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_WAIT_ALL_NODES,
						arg, errors) == 0,
		      "wait-all-nodes");
	ck_assert_msg(opt.salloc_opt->wait_all_nodes == 1,
		      "wait-all-nodes value");
	ck_assert_msg(opt.sbatch_opt->wait_all_nodes == 1,
		      "wait-all-nodes value");
	data_set_string(arg, "988328328");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_WAIT_ALL_NODES,
						arg, errors) != 0,
		      "wait-all-nodes");
	data_set_string(arg, "-1");
	ck_assert_msg(slurm_process_option_data(&opt, LONG_OPT_WAIT_ALL_NODES,
						arg, errors) != 0,
		      "wait-all-nodes");

	slurm_option_table_destroy(spanked);
	xfree(opt_string);
	FREE_NULL_DATA(arg);
	FREE_NULL_DATA(errors);
	slurm_free_options_members(&opt);
}
END_TEST

Suite *slurm_opt_suite(void)
{
	Suite *s = suite_create("slurm_opt");
	TCase *tc_core = tcase_create("Core");
	tcase_add_test(tc_core, test_data_job);
	tcase_add_test(tc_core, test_data_job_macros);
	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	/* Set up Slurm logging */
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("slurm_opt-test", log_opts, 0, NULL);

	/* Call slurm_init() with a mock slurm.conf*/
	int fd;
	char *slurm_unit_conf_filename = xstrdup("slurm_unit.conf-XXXXXX");
	if ((fd = mkstemp(slurm_unit_conf_filename)) == -1) {
		error("error creating slurm_unit.conf (%s)",
		      slurm_unit_conf_filename);
		return EXIT_FAILURE;
	} else
		debug("fake slurm.conf created: %s", slurm_unit_conf_filename);

	/*
	 * PluginDir=. is needed as loading the slurm.conf will check for the
	 * existence of the dir. As 'make check' doesn't install anything the
	 * normal PluginDir might not exist. As we don't load any plugins for
	 * these test this should be ok.
	 */
	char slurm_unit_conf_content[] = "ClusterName=slurm_unit\n"
		                         "PluginDir=.\n"
					 "SlurmctldHost=slurm_unit\n";
	size_t csize = sizeof(slurm_unit_conf_content);
	ssize_t rc = write(fd, slurm_unit_conf_content, csize);
	if (rc < csize) {
		error("error writing slurm_unit.conf (%s)",
		      slurm_unit_conf_filename);
		return EXIT_FAILURE;
	}

	/* Do not load any plugins, we are only testing slurm_opt */
	if (slurm_conf_init_load(slurm_unit_conf_filename, false) !=
	    SLURM_SUCCESS) {
		error("slurm_conf_init_load() failed");
		return EXIT_FAILURE;
	}

	unlink(slurm_unit_conf_filename);
	xfree(slurm_unit_conf_filename);
	close(fd);

	/* data_init() is necessary on this test */
	if (data_init()) {
		error("data_init_static() failed");
		return EXIT_FAILURE;
	}

	/* Start the actual libcheck code */
	int number_failed;
	SRunner *sr = srunner_create(slurm_opt_suite());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	/* Cleanup */
	data_fini();

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
