/*****************************************************************************\
 *  slurm_opt.c - salloc/sbatch/srun option processing functions
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include <getopt.h>

#include "src/common/log.h"
#include "src/common/optz.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/slurm_opt.h"

/*
 * This is ugly. But... less ugly than dozens of identical functions handling
 * variables that are just strings pushed and pulled out of the associated
 * structures.
 *
 * This takes one argument: the desired field in slurm_opt_t.
 * The function name will be automatically generated as arg_set_##field.
 */
#define COMMON_STRING_OPTION(field)	\
COMMON_STRING_OPTION_SET(field)		\
COMMON_STRING_OPTION_GET(field)		\
COMMON_STRING_OPTION_RESET(field)
#define COMMON_STRING_OPTION_GET_AND_RESET(field)	\
COMMON_STRING_OPTION_GET(field)				\
COMMON_STRING_OPTION_RESET(field)
#define COMMON_STRING_OPTION_SET(field)				\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
__attribute__((nonnull (1)));					\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
{								\
	xfree(opt->field);					\
	opt->field = xstrdup(arg);				\
								\
	return SLURM_SUCCESS;					\
}
#define COMMON_STRING_OPTION_GET(field)				\
static char *arg_get_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static char *arg_get_##field(slurm_opt_t *opt)			\
{								\
	return xstrdup(opt->field);				\
}
#define COMMON_STRING_OPTION_RESET(field)			\
static void arg_reset_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static void arg_reset_##field(slurm_opt_t *opt)			\
{								\
	xfree(opt->field);					\
}

#define COMMON_SBATCH_STRING_OPTION(field)	\
COMMON_SBATCH_STRING_OPTION_SET(field)		\
COMMON_SBATCH_STRING_OPTION_GET(field)		\
COMMON_SBATCH_STRING_OPTION_RESET(field)
#define COMMON_SBATCH_STRING_OPTION_SET(field)			\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
__attribute__((nonnull (1)));					\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
{								\
	if (!opt->sbatch_opt)					\
		return SLURM_ERROR;				\
								\
	xfree(opt->sbatch_opt->field);				\
	opt->sbatch_opt->field = xstrdup(arg);			\
								\
	return SLURM_SUCCESS;					\
}
#define COMMON_SBATCH_STRING_OPTION_GET(field)			\
static char *arg_get_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static char *arg_get_##field(slurm_opt_t *opt)			\
{								\
	if (!opt->sbatch_opt)					\
		return xstrdup("invalid-context");		\
	return xstrdup(opt->sbatch_opt->field);			\
}
#define COMMON_SBATCH_STRING_OPTION_RESET(field)		\
static void arg_reset_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static void arg_reset_##field(slurm_opt_t *opt)			\
{								\
	if (opt->sbatch_opt)					\
		xfree(opt->sbatch_opt->field);			\
}

#define COMMON_OPTION_RESET(field, value)			\
static void arg_reset_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static void arg_reset_##field(slurm_opt_t *opt)			\
{								\
	opt->field = value;					\
}

#define COMMON_BOOL_OPTION(field, option)			\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
__attribute__((nonnull (1)));					\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
{								\
	opt->field = true;					\
								\
	return SLURM_SUCCESS;					\
}								\
static char *arg_get_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static char *arg_get_##field(slurm_opt_t *opt)			\
{								\
	return xstrdup(opt->field ? "set" : "unset");		\
}								\
COMMON_OPTION_RESET(field, false)

#define COMMON_SRUN_BOOL_OPTION(field)				\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
__attribute__((nonnull (1)));					\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
{								\
	if (!opt->srun_opt)					\
		return SLURM_ERROR;				\
								\
	opt->srun_opt->field = true;				\
								\
	return SLURM_SUCCESS;					\
}								\
static char *arg_get_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static char *arg_get_##field(slurm_opt_t *opt)			\
{								\
	if (!opt->srun_opt)					\
		return xstrdup("invalid-context");		\
								\
	return xstrdup(opt->srun_opt->field ? "set" : "unset");	\
}								\
static void arg_reset_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static void arg_reset_##field(slurm_opt_t *opt)			\
{								\
	if (opt->srun_opt)					\
		opt->srun_opt->field = false;			\
}

#define COMMON_INT_OPTION(field, option)			\
COMMON_INT_OPTION_SET(field, option)				\
COMMON_INT_OPTION_GET(field)					\
COMMON_OPTION_RESET(field, 0)
#define COMMON_INT_OPTION_GET_AND_RESET(field)			\
COMMON_INT_OPTION_GET(field)					\
COMMON_OPTION_RESET(field, 0)
#define COMMON_INT_OPTION_SET(field, option)			\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
__attribute__((nonnull (1)));					\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
{								\
	opt->field = parse_int(option, arg, true);		\
								\
	return SLURM_SUCCESS;					\
}
#define COMMON_INT_OPTION_GET(field)				\
static char *arg_get_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static char *arg_get_##field(slurm_opt_t *opt)			\
{								\
	return xstrdup_printf("%d", opt->field);		\
}

#define COMMON_MBYTES_OPTION(field, option)			\
COMMON_MBYTES_OPTION_SET(field, option)				\
COMMON_MBYTES_OPTION_GET(field)					\
COMMON_OPTION_RESET(field, NO_VAL64)
#define COMMON_MBYTES_OPTION_SET(field, option)			\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
{								\
	if ((opt->field = str_to_mbytes2(arg)) == NO_VAL64) {	\
		error("Invalid ##field specification");		\
		exit(-1);					\
	}							\
								\
        return SLURM_SUCCESS;					\
}
#define COMMON_MBYTES_OPTION_GET_AND_RESET(field)		\
COMMON_MBYTES_OPTION_GET(field)					\
COMMON_OPTION_RESET(field, NO_VAL64)
#define COMMON_MBYTES_OPTION_GET(field)				\
static char *arg_get_##field(slurm_opt_t *opt)			\
{								\
	return mbytes2_to_str(opt->field);			\
}

#define COMMON_TIME_DURATION_OPTION_GET_AND_RESET(field)	\
static char *arg_get_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static char *arg_get_##field(slurm_opt_t *opt)			\
{								\
	char time_str[32];					\
	mins2time_str(opt->field, time_str, sizeof(time_str));	\
	return xstrdup(time_str);				\
}								\
COMMON_OPTION_RESET(field, NO_VAL)

typedef struct {
	/*
	 * DO NOT ALTER THESE FIRST FOUR ARGUMENTS
	 * They must match 'struct option', so that some
	 * casting abuse is nice and trivial.
	 */
	const char *name;	/* Long option name. */
	int has_arg;		/* no_argument, required_argument,
				 * or optional_argument */
	int *flag;		/* Always NULL in our usage. */
	int val;		/* Single character, or LONG_OPT_* */
	/*
	 * Add new members below here:
	 */
	bool set;		/* Has the option been set */
	bool set_by_env;	/* Has the option been set by env var */
	bool reset_each_pass;	/* Reset on all HetJob passes or only first */
	bool sbatch_early_pass;	/* For sbatch - run in the early pass. */
				/* For salloc/srun - this is ignored, and will
				 * run alongside all other options. */
	/*
	 * If set_func is set, it will be used, and the command
	 * specific versions must not be set.
	 * Otherwise, command specific versions will be used.
	 */
	int (*set_func)(slurm_opt_t *, const char *);
	int (*set_func_salloc)(slurm_opt_t *, const char *);
	int (*set_func_sbatch)(slurm_opt_t *, const char *);
	int (*set_func_srun)(slurm_opt_t *, const char *);
	/* Return must be xfree()'d */
	char *(*get_func)(slurm_opt_t *);
	void (*reset_func)(slurm_opt_t *);
} slurm_cli_opt_t;

/*
 * Function names should be directly correlated with the slurm_opt_t field
 * they manipulate. But the slurm_cli_opt_t name should always match that
 * of the long-form name for the argument itself.
 *
 * These should be alphabetized by the slurm_cli_opt_t name.
 */

COMMON_STRING_OPTION(account);
static slurm_cli_opt_t slurm_opt_account = {
	.name = "account",
	.has_arg = required_argument,
	.val = 'A',
	.set_func = arg_set_account,
	.get_func = arg_get_account,
	.reset_func = arg_reset_account,
};

static int arg_set_begin(slurm_opt_t *opt, const char *arg)
{
	if (!(opt->begin = parse_time(arg, 0))) {
		error("Invalid --begin specification");
		exit(-1);
	}

	return SLURM_SUCCESS;
}
static char *arg_get_begin(slurm_opt_t *opt)
{
	char time_str[32];
	slurm_make_time_str(&opt->begin, time_str, sizeof(time_str));
	return xstrdup(time_str);
}
COMMON_OPTION_RESET(begin, 0);
static slurm_cli_opt_t slurm_opt_begin = {
	.name = "begin",
	.has_arg = required_argument,
	.val = 'b',
	.set_func = arg_set_begin,
	.get_func = arg_get_begin,
	.reset_func = arg_reset_begin,
};

COMMON_STRING_OPTION(c_constraint);
static slurm_cli_opt_t slurm_opt_c_constraint = {
	.name = "cluster-constraint",
	.has_arg = required_argument,
	.val = LONG_OPT_CLUSTER_CONSTRAINT,
	.set_func = arg_set_c_constraint,
	.get_func = arg_get_c_constraint,
	.reset_func = arg_reset_c_constraint,
};

/* --clusters and --cluster are equivalent */
COMMON_STRING_OPTION(clusters);
static slurm_cli_opt_t slurm_opt_clusters = {
	.name = "clusters",
	.has_arg = required_argument,
	.val = 'M',
	.set_func = arg_set_clusters,
	.get_func = arg_get_clusters,
	.reset_func = arg_reset_clusters,
};
static slurm_cli_opt_t slurm_opt_cluster = {
	.name = "cluster",
	.has_arg = required_argument,
	.val = LONG_OPT_CLUSTER,
	.set_func = arg_set_clusters,
	.get_func = arg_get_clusters,
	.reset_func = arg_reset_clusters,
};

COMMON_STRING_OPTION(comment);
static slurm_cli_opt_t slurm_opt_comment = {
	.name = "comment",
	.has_arg = required_argument,
	.val = LONG_OPT_COMMENT,
	.set_func = arg_set_comment,
	.get_func = arg_get_comment,
	.reset_func = arg_reset_comment,
};

COMMON_STRING_OPTION(constraint);
static slurm_cli_opt_t slurm_opt_constraint = {
	.name = "constraint",
	.has_arg = required_argument,
	.val = 'C',
	.set_func = arg_set_constraint,
	.get_func = arg_get_constraint,
	.reset_func = arg_reset_constraint,
	.reset_each_pass = true,
};

COMMON_BOOL_OPTION(contiguous, "contiguous");
static slurm_cli_opt_t slurm_opt_contiguous = {
	.name = "contiguous",
	.has_arg = no_argument,
	.val = LONG_OPT_CONTIGUOUS,
	.set_func = arg_set_contiguous,
	.get_func = arg_get_contiguous,
	.reset_func = arg_reset_contiguous,
	.reset_each_pass = true,
};

static int arg_set_deadline(slurm_opt_t *opt, const char *arg)
{
	if (!(opt->deadline = parse_time(arg, 0))) {
		error("Invalid --deadline specification");
		exit(-1);
	}

	return SLURM_SUCCESS;
}
static char *arg_get_deadline(slurm_opt_t *opt)
{
	char time_str[32];
	slurm_make_time_str(&opt->deadline, time_str, sizeof(time_str));
	return xstrdup(time_str);
}
COMMON_OPTION_RESET(deadline, 0);
static slurm_cli_opt_t slurm_opt_deadline = {
	.name = "deadline",
	.has_arg = required_argument,
	.val = LONG_OPT_DEADLINE,
	.set_func = arg_set_deadline,
	.get_func = arg_get_deadline,
	.reset_func = arg_reset_deadline,
};

COMMON_STRING_OPTION(dependency);
static slurm_cli_opt_t slurm_opt_dependency = {
	.name = "dependency",
	.has_arg = required_argument,
	.val = 'd',
	.set_func = arg_set_dependency,
	.get_func = arg_get_dependency,
	.reset_func = arg_reset_dependency,
};

COMMON_STRING_OPTION(gpus);
static slurm_cli_opt_t slurm_opt_gpus = {
	.name = "gpus",
	.has_arg = required_argument,
	.val = 'G',
	.set_func = arg_set_gpus,
	.get_func = arg_get_gpus,
	.reset_func = arg_reset_gpus,
};

static int arg_set_gres(slurm_opt_t *opt, const char *arg)
{
	if (!xstrcasecmp(arg, "help") || !xstrcasecmp(arg, "list")) {
		print_gres_help();
		exit(0);
	}

	xfree(opt->gres);
	opt->gres = xstrdup(arg);

	return SLURM_SUCCESS;
}
COMMON_STRING_OPTION_GET_AND_RESET(gres);
static slurm_cli_opt_t slurm_opt_gres = {
	.name = "gres",
	.has_arg = required_argument,
	.val = LONG_OPT_GRES,
	.set_func = arg_set_gres,
	.get_func = arg_get_gres,
	.reset_func = arg_reset_gres,
	.reset_each_pass = true,
};

COMMON_BOOL_OPTION(hold, "hold");
static slurm_cli_opt_t slurm_opt_hold = {
	.name = "hold",
	.has_arg = no_argument,
	.val = 'H',
	.set_func = arg_set_hold,
	.get_func = arg_get_hold,
	.reset_func = arg_reset_hold,
};

COMMON_STRING_OPTION(licenses);
static slurm_cli_opt_t slurm_opt_licenses = {
	.name = "licenses",
	.has_arg = required_argument,
	.val = 'L',
	.set_func = arg_set_licenses,
	.get_func = arg_get_licenses,
	.reset_func = arg_reset_licenses,
	.reset_each_pass = true,
};

COMMON_STRING_OPTION(mcs_label);
static slurm_cli_opt_t slurm_opt_mcs_label = {
	.name = "mcs-label",
	.has_arg = required_argument,
	.val = LONG_OPT_MCS_LABEL,
	.set_func = arg_set_mcs_label,
	.get_func = arg_get_mcs_label,
	.reset_func = arg_reset_mcs_label,
};

COMMON_BOOL_OPTION(overcommit, "overcommit");
static slurm_cli_opt_t slurm_opt_overcommit = {
	.name = "overcommit",
	.has_arg = no_argument,
	.val = 'O',
	.set_func = arg_set_overcommit,
	.get_func = arg_get_overcommit,
	.reset_func = arg_reset_overcommit,
	.reset_each_pass = true,
};

static int arg_set_priority(slurm_opt_t *opt, const char *arg)
{
	if (!xstrcasecmp(arg, "TOP")) {
		opt->priority = NO_VAL - 1;
	} else {
		long long priority = strtoll(arg, NULL, 10);
		if (priority < 0) {
			error("Priority must be >= 0");
			exit(-1);
		}
		if (priority >= NO_VAL) {
			error("Priority must be < %u", NO_VAL);
			exit(-1);
		}
		opt->priority = priority;
	}

	return SLURM_SUCCESS;
}
COMMON_INT_OPTION_GET_AND_RESET(priority);
static slurm_cli_opt_t slurm_opt_priority = {
	.name = "priority",
	.has_arg = required_argument,
	.val = LONG_OPT_PRIORITY,
	.set_func = arg_set_priority,
	.get_func = arg_get_priority,
	.reset_func = arg_reset_priority,
};

COMMON_STRING_OPTION(qos);
static slurm_cli_opt_t slurm_opt_qos = {
	.name = "qos",
	.has_arg = required_argument,
	.val = 'q',
	.set_func = arg_set_qos,
	.get_func = arg_get_qos,
	.reset_func = arg_reset_qos,
};

COMMON_BOOL_OPTION(reboot, "reboot");
static slurm_cli_opt_t slurm_opt_reboot = {
	.name = "reboot",
	.has_arg = no_argument,
	.val = LONG_OPT_REBOOT,
	.set_func = arg_set_reboot,
	.get_func = arg_get_reboot,
	.reset_func = arg_reset_reboot,
};

COMMON_STRING_OPTION(reservation);
static slurm_cli_opt_t slurm_opt_reservation = {
	.name = "reservation",
	.has_arg = required_argument,
	.val = LONG_OPT_RESERVATION,
	.set_func = arg_set_reservation,
	.get_func = arg_get_reservation,
	.reset_func = arg_reset_reservation,
};

static int arg_set_time_limit(slurm_opt_t *opt, const char *arg)
{
	int time_limit;

	time_limit = time_str2mins(arg);
	if (time_limit == NO_VAL) {
		error("Invalid --time specification");
		exit(-1);
	} else if (time_limit == 0) {
		time_limit = INFINITE;
	}

	opt->time_limit = time_limit;
	return SLURM_SUCCESS;
}
COMMON_TIME_DURATION_OPTION_GET_AND_RESET(time_limit);
static slurm_cli_opt_t slurm_opt_time_limit = {
	.name = "time",
	.has_arg = required_argument,
	.val = 't',
	.set_func = arg_set_time_limit,
	.get_func = arg_get_time_limit,
	.reset_func = arg_reset_time_limit,
};

static int arg_set_time_min(slurm_opt_t *opt, const char *arg)
{
	int time_min;

	time_min = time_str2mins(arg);
	if (time_min == NO_VAL) {
		error("Invalid --time-min specification");
		exit(-1);
	} else if (time_min == 0) {
		time_min = INFINITE;
	}

	opt->time_min = time_min;
	return SLURM_SUCCESS;
}
COMMON_TIME_DURATION_OPTION_GET_AND_RESET(time_min);
static slurm_cli_opt_t slurm_opt_time_min = {
	.name = "time-min",
	.has_arg = required_argument,
	.val = LONG_OPT_TIME_MIN,
	.set_func = arg_set_time_min,
	.get_func = arg_get_time_min,
	.reset_func = arg_reset_time_min,
};

COMMON_STRING_OPTION(wckey);
static slurm_cli_opt_t slurm_opt_wckey = {
	.name = "wckey",
	.has_arg = required_argument,
	.val = LONG_OPT_WCKEY,
	.set_func = arg_set_wckey,
	.get_func = arg_get_wckey,
	.reset_func = arg_reset_wckey,
};

static slurm_cli_opt_t *common_options[] = {
	&slurm_opt_account,
	&slurm_opt_begin,
	&slurm_opt_c_constraint,
	&slurm_opt_cluster,
	&slurm_opt_clusters,
	&slurm_opt_comment,
	&slurm_opt_contiguous,
	&slurm_opt_constraint,
	&slurm_opt_deadline,
	&slurm_opt_dependency,
	&slurm_opt_gpus,
	&slurm_opt_gres,
	&slurm_opt_hold,
	&slurm_opt_licenses,
	&slurm_opt_mcs_label,
	&slurm_opt_overcommit,
	&slurm_opt_priority,
	&slurm_opt_qos,
	&slurm_opt_reboot,
	&slurm_opt_reservation,
	&slurm_opt_time_limit,
	&slurm_opt_time_min,
	&slurm_opt_wckey,
	NULL /* END */
};

struct option *slurm_option_table_create(struct option *options,
					 slurm_opt_t *opt)
{
	struct option *merged = optz_create();

	optz_append(&merged, options);
	/*
	 * Since the initial elements of slurm_cli_opt_t match
	 * the layout of struct option, we can use this cast to
	 * avoid needing to make a temporary structure.
	 */
	for (int i = 0; common_options[i]; i++) {
		bool set = true;
		/*
		 * Runtime sanity checking for development builds,
		 * as I cannot find a convenient way to instruct the
		 * compiler to handle this. So if you make a mistake,
		 * you'll hit an assertion failure in salloc/srun/sbatch.
		 *
		 * If set_func is set, the others must not be:
		 */
		xassert((common_options[i]->set_func
			 && !common_options[i]->set_func_salloc
			 && !common_options[i]->set_func_sbatch
			 && !common_options[i]->set_func_srun) ||
			!common_options[i]->set_func);
		/*
		 * These two must always be set:
		 */
		xassert(common_options[i]->get_func);
		xassert(common_options[i]->reset_func);

		/*
		 * A few options only exist as environment variables, and
		 * should not be added to the table. They should be marked
		 * with a NULL name field.
		 */
		if (!common_options[i]->name)
			continue;

		if (common_options[i]->set_func)
			optz_add(&merged, (struct option *) common_options[i]);
		else if (opt->salloc_opt && common_options[i]->set_func_salloc)
			optz_add(&merged, (struct option *) common_options[i]);
		else if (opt->sbatch_opt && common_options[i]->set_func_sbatch)
			optz_add(&merged, (struct option *) common_options[i]);
		else if (opt->srun_opt && common_options[i]->set_func_srun)
			optz_add(&merged, (struct option *) common_options[i]);
		else
			set = false;

		if (set) {
			/* FIXME: append appropriate characters to optstring */
		}
	}
	return merged;
}

void slurm_option_table_destroy(struct option *optz)
{
	optz_destroy(optz);
}

int slurm_process_option(slurm_opt_t *opt, int optval, const char *arg,
			 bool set_by_env, bool early_pass)
{
	int i;
	const char *setarg = arg;
	bool set = true;

	if (!opt)
		fatal("%s: missing slurm_opt_t struct", __func__);

	for (i = 0; common_options[i]; i++) {
		if (common_options[i]->val != optval)
			continue;

		/* Check that this is a valid match. */
		if (!common_options[i]->set_func &&
		    !(opt->salloc_opt && common_options[i]->set_func_salloc) &&
		    !(opt->sbatch_opt && common_options[i]->set_func_sbatch) &&
		    !(opt->srun_opt && common_options[i]->set_func_srun))
			continue;

		/* Match found */
		break;
	}

	if (!common_options[i])
		return SLURM_ERROR;

	/*
	 * Special handling for the early pass in sbatch.
	 *
	 * Some options are handled in the early pass, but most are deferred
	 * to a later pass, in which case those options are not re-evaluated.
	 * Environment variables are always evaluated by this though - there
	 * is no distinction for them of early vs normal passes.
	 */
	if (!set_by_env && opt->sbatch_opt) {
		if (!early_pass && common_options[i]->sbatch_early_pass)
			return SLURM_SUCCESS;
		if (early_pass && !common_options[i]->sbatch_early_pass)
			return SLURM_SUCCESS;
	}

	if (arg) {
		if (common_options[i]->has_arg == no_argument) {
			char *end;
			/*
			 * Treat these "flag" arguments specially.
			 * For normal getopt_long() handling, arg is null.
			 * But for envvars, arg may be set, and will be
			 * processed by these rules:
			 * arg == '\0', flag is set
			 * arg == "yes", flag is set
			 * arg is a non-zero number, flag is set
			 * arg is anything else, call reset instead
			 */
			if (arg[0] == '\0') {
				set = true;
			} else if (!xstrcasecmp(arg, "yes")) {
				set = true;
			} else if (strtol(arg, &end, 10) && (*end == '\0')) {
				set = true;
			} else {
				set = false;
			}
		} else if (common_options[i]->has_arg == required_argument) {
			/* no special processing required */
		} else if (common_options[i]->has_arg == optional_argument) {
			/*
			 * If an empty string, convert to null,
			 * as this will let the envvar processing
			 * match the normal getopt_long() behavior.
			 */
			if (arg[0] == '\0')
				setarg = NULL;
		}
	}

	if (!set) {
		(common_options[i]->reset_func)(opt);
		common_options[i]->set = false;
		common_options[i]->set_by_env = false;
		return SLURM_SUCCESS;
	}

	if (common_options[i]->set_func) {
		if (!(common_options[i]->set_func)(opt, setarg)) {
			common_options[i]->set = true;
			common_options[i]->set_by_env = set_by_env;
			return SLURM_SUCCESS;
		}
	} else if (opt->salloc_opt && common_options[i]->set_func_salloc) {
		if (!(common_options[i]->set_func_salloc)(opt, setarg)) {
			common_options[i]->set = true;
			common_options[i]->set_by_env = set_by_env;
			return SLURM_SUCCESS;
		}
	} else if (opt->sbatch_opt && common_options[i]->set_func_sbatch) {
		if (!(common_options[i]->set_func_sbatch)(opt, setarg)) {
			common_options[i]->set = true;
			common_options[i]->set_by_env = set_by_env;
			return SLURM_SUCCESS;
		}
	} else if (opt->srun_opt && common_options[i]->set_func_srun) {
		if (!(common_options[i]->set_func_srun)(opt, setarg)) {
			common_options[i]->set = true;
			common_options[i]->set_by_env = set_by_env;
			return SLURM_SUCCESS;
		}
	}
	return SLURM_ERROR;
}

void slurm_print_set_options(slurm_opt_t *opt)
{
	if (!opt)
		fatal("%s: missing slurm_opt_t struct", __func__);

	info("defined options");
	info("-------------------- --------------------");

	for (int i = 0; common_options[i]; i++) {
		char *val = NULL;

		if (!common_options[i]->set)
			continue;

		if (common_options[i]->get_func)
			val = (common_options[i]->get_func)(opt);
		info("%-20s: %s", common_options[i]->name, val);
		xfree(val);
	}
	info("-------------------- --------------------");
	info("end of defined options");
}

extern void slurm_reset_all_options(slurm_opt_t *opt, bool first_pass)
{
	for (int i = 0; common_options[i]; i++) {
		if (!first_pass && !common_options[i]->reset_each_pass)
			continue;
		if (common_options[i]->reset_func) {
			(common_options[i]->reset_func)(opt);
			common_options[i]->set = false;
		}
	}
}

/*
 * Was the option set by a cli argument?
 */
extern bool slurm_option_set_by_cli(int optval)
{
	int i;

	for (i = 0; common_options[i]; i++) {
		if (common_options[i]->val == optval)
			break;
	}

	/* This should not happen... */
	if (!common_options[i])
		return false;

	/*
	 * set is true if the option is set at all. If both set and set_by_env
	 * are true, then the argument was set through the environment not the
	 * cli, and we must return false.
	 */

	return (common_options[i]->set && !common_options[i]->set_by_env);
}

/*
 * Was the option set by an env var?
 */
extern bool slurm_option_set_by_env(int optval)
{
	int i;

	for (i = 0; common_options[i]; i++) {
		if (common_options[i]->val == optval)
			break;
	}

	/* This should not happen... */
	if (!common_options[i])
		return false;

	return common_options[i]->set_by_env;
}
