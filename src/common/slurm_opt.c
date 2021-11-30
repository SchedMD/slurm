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

#include "config.h"

#include <getopt.h>
#include <sys/param.h>

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
#endif

#include "src/common/cpu_frequency.h"
#include "src/common/gres.h"
#include "src/common/log.h"
#include "src/common/optz.h"
#include "src/common/parse_time.h"
#include "src/common/plugstack.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/tres_bind.h"
#include "src/common/tres_frequency.h"
#include "src/common/uid.h"
#include "src/common/util-net.h"
#include "src/common/x11_util.h"
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
#define ADD_DATA_ERROR(str, rc)					\
	do {							\
		data_t *err = data_set_dict(			\
			data_list_append(errors));		\
		data_set_string(				\
			data_key_set(err, "error"), str);	\
		data_set_int(					\
			data_key_set(err, "error_code"), rc);	\
	} while (0)

#define COMMON_STRING_OPTION(field)	\
COMMON_STRING_OPTION_SET(field)		\
COMMON_STRING_OPTION_SET_DATA(field)	\
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
#define COMMON_STRING_OPTION_SET_DATA(field)			\
static int arg_set_data_##field(slurm_opt_t *opt,		\
				const data_t *arg,		\
				data_t *errors)			\
__attribute__((nonnull (1, 2)));				\
static int arg_set_data_##field(slurm_opt_t *opt,		\
				const data_t *arg,		\
				data_t *errors)			\
{								\
	xfree(opt->field);					\
	return data_get_string_converted(arg, &opt->field);	\
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
COMMON_SBATCH_STRING_OPTION_SET_DATA(field)	\
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
#define COMMON_SBATCH_STRING_OPTION_SET_DATA(field)		\
static int arg_set_data_##field(slurm_opt_t *opt,		\
				const data_t *arg,		\
				data_t *errors)			\
__attribute__((nonnull (1, 2)));				\
static int arg_set_data_##field(slurm_opt_t *opt,		\
				const data_t *arg,		\
				data_t *errors)			\
{								\
	if (!opt->sbatch_opt)					\
		return SLURM_ERROR;				\
								\
	xfree(opt->sbatch_opt->field);				\
	return data_get_string_converted(arg,			\
		&opt->sbatch_opt->field);			\
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

#define COMMON_SRUN_STRING_OPTION(field)	\
COMMON_SRUN_STRING_OPTION_SET(field)		\
COMMON_SRUN_STRING_OPTION_GET(field)		\
COMMON_SRUN_STRING_OPTION_RESET(field)
#define COMMON_SRUN_STRING_OPTION_SET(field)			\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
__attribute__((nonnull (1)));					\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
{								\
	if (!opt->srun_opt)					\
		return SLURM_ERROR;				\
								\
	xfree(opt->srun_opt->field);				\
	opt->srun_opt->field = xstrdup(arg);			\
								\
	return SLURM_SUCCESS;					\
}
#define COMMON_SRUN_STRING_OPTION_GET(field)			\
static char *arg_get_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static char *arg_get_##field(slurm_opt_t *opt)			\
{								\
	if (!opt->srun_opt)					\
		return xstrdup("invalid-context");		\
	return xstrdup(opt->srun_opt->field);			\
}
#define COMMON_SRUN_STRING_OPTION_RESET(field)			\
static void arg_reset_##field(slurm_opt_t *opt)			\
__attribute__((nonnull));					\
static void arg_reset_##field(slurm_opt_t *opt)			\
{								\
	if (opt->srun_opt)					\
		xfree(opt->srun_opt->field);			\
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
static int arg_set_data_##field(slurm_opt_t *opt,		\
				const data_t *arg,		\
				data_t *errors)			\
__attribute__((nonnull (1, 2)));				\
static int arg_set_data_##field(slurm_opt_t *opt,		\
				const data_t *arg,		\
				data_t *errors)			\
{								\
	return data_copy_bool_converted(arg, &opt->field);	\
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
COMMON_INT_OPTION_SET_DATA(field)				\
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

#define COMMON_INT_OPTION_SET_DATA(field)			\
static int arg_set_data_##field(slurm_opt_t *opt,		\
			   const data_t *arg,			\
			   data_t *errors)			\
__attribute__((nonnull (1, 2)));				\
static int arg_set_data_##field(slurm_opt_t *opt,		\
			   const data_t *arg,			\
			   data_t *errors)			\
{								\
	int64_t val;						\
	int rc = data_get_int_converted(arg, &val);		\
	if (rc)							\
		ADD_DATA_ERROR("Unable to read integer value",	\
			       rc);				\
	else if (val >= INT_MAX) {				\
		rc = SLURM_ERROR;				\
		ADD_DATA_ERROR("Integer too large", rc);	\
	} else if (val <= INT_MIN) {				\
		rc = SLURM_ERROR;				\
		ADD_DATA_ERROR("Integer too small", rc);	\
	} else							\
		opt->field = (int) val;				\
	return rc;						\
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
COMMON_MBYTES_OPTION_SET_DATA(field, option)			\
COMMON_MBYTES_OPTION_GET(field)					\
COMMON_OPTION_RESET(field, NO_VAL64)
#define COMMON_MBYTES_OPTION_SET(field, option)			\
static int arg_set_##field(slurm_opt_t *opt, const char *arg)	\
{								\
	if ((opt->field = str_to_mbytes(arg)) == NO_VAL64) {	\
		error("Invalid " #option " specification");	\
		return SLURM_ERROR;				\
	}							\
								\
	return SLURM_SUCCESS;					\
}
#define COMMON_MBYTES_OPTION_SET_DATA(field, option)			\
static int arg_set_data_##field(slurm_opt_t *opt, const data_t *arg,	\
				data_t *errors)				\
{									\
	char *str = NULL;						\
	int rc;								\
	if ((rc = data_get_string_converted(arg, &str)))		\
		ADD_DATA_ERROR("Invalid " #option " specification string", \
			       rc);					\
	else if ((opt->field = str_to_mbytes(str)) == NO_VAL64)		\
		ADD_DATA_ERROR("Invalid " #option " specification",	\
			       (rc = SLURM_ERROR));			\
	xfree(str);							\
	return rc;							\
}
#define COMMON_MBYTES_OPTION_GET_AND_RESET(field)		\
COMMON_MBYTES_OPTION_GET(field)					\
COMMON_OPTION_RESET(field, NO_VAL64)
#define COMMON_MBYTES_OPTION_GET(field)				\
static char *arg_get_##field(slurm_opt_t *opt)			\
{								\
	return mbytes_to_str(opt->field);			\
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
	bool reset_each_pass;	/* Reset on all HetJob passes or only first */
	bool sbatch_early_pass;	/* For sbatch - run in the early pass. */
				/* For salloc/srun - this is ignored, and will
				 * run alongside all other options. */
	bool srun_early_pass;	/* For srun - run in the early pass. */
	/*
	 * If set_func is set, it will be used, and the command
	 * specific versions must not be set.
	 * Otherwise, command specific versions will be used.
	 */
	int (*set_func)(slurm_opt_t *, const char *);
	int (*set_func_salloc)(slurm_opt_t *, const char *);
	int (*set_func_sbatch)(slurm_opt_t *, const char *);
	int (*set_func_scron)(slurm_opt_t *, const char *);
	int (*set_func_srun)(slurm_opt_t *, const char *);

	/*
	 * data_t handlers
	 * IN opt - job component options
	 * IN arg - job component entry
	 * IN/OUT errors - appends new dictionary to list of error details
	 */
	int (*set_func_data)(slurm_opt_t *opt, const data_t *arg,
			     data_t *errors);

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

static int arg_set__unknown_salloc(slurm_opt_t *opt, const char *arg)
{
	fprintf(stderr, "Try \"salloc --help\" for more information\n");

	return SLURM_ERROR;
}
static int arg_set__unknown_sbatch(slurm_opt_t *opt, const char *arg)
{
	fprintf(stderr,	"Try \"sbatch --help\" for more information\n");

	return SLURM_ERROR;
}
static int arg_set__unknown_srun(slurm_opt_t *opt, const char *arg)
{
	fprintf(stderr,	"Try \"srun --help\" for more information\n");

	return SLURM_ERROR;
}
static char *arg_get__unknown_(slurm_opt_t *opt)
{
	return NULL; /* no op */
}
static void arg_reset__unknown_(slurm_opt_t *opt)
{
	/* no op */
}
static slurm_cli_opt_t slurm_opt__unknown_ = {
	.name = NULL,
	.has_arg = no_argument,
	.val = '?',
	.set_func_salloc = arg_set__unknown_salloc,
	.set_func_sbatch = arg_set__unknown_sbatch,
	.set_func_srun = arg_set__unknown_srun,
	.get_func = arg_get__unknown_,
	.reset_func = arg_reset__unknown_,
};

static int arg_set_accel_bind_type(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	if (strchr(arg, 'v'))
		opt->srun_opt->accel_bind_type |= ACCEL_BIND_VERBOSE;
	if (strchr(arg, 'g'))
		opt->srun_opt->accel_bind_type |= ACCEL_BIND_CLOSEST_GPU;
	if (strchr(arg, 'n'))
		opt->srun_opt->accel_bind_type |= ACCEL_BIND_CLOSEST_NIC;

	if (!opt->srun_opt->accel_bind_type) {
		error("Invalid --accel-bind specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static char *arg_get_accel_bind_type(slurm_opt_t *opt)
{
	char *tmp = NULL;

	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	if (opt->srun_opt->accel_bind_type & ACCEL_BIND_VERBOSE)
		xstrcat(tmp, "v");
	if (opt->srun_opt->accel_bind_type & ACCEL_BIND_CLOSEST_GPU)
		xstrcat(tmp, "g");
	if (opt->srun_opt->accel_bind_type & ACCEL_BIND_CLOSEST_NIC)
		xstrcat(tmp, "n");

	return tmp;
}
static void arg_reset_accel_bind_type(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->accel_bind_type = 0;
}
static slurm_cli_opt_t slurm_opt_accel_bind = {
	.name = "accel-bind",
	.has_arg = required_argument,
	.val = LONG_OPT_ACCEL_BIND,
	.set_func_srun = arg_set_accel_bind_type,
	.get_func = arg_get_accel_bind_type,
	.reset_func = arg_reset_accel_bind_type,
	.reset_each_pass = true,
};

COMMON_STRING_OPTION(account);
static slurm_cli_opt_t slurm_opt_account = {
	.name = "account",
	.has_arg = required_argument,
	.val = 'A',
	.set_func = arg_set_account,
	.set_func_data = arg_set_data_account,
	.get_func = arg_get_account,
	.reset_func = arg_reset_account,
};

static int arg_set_acctg_freq(slurm_opt_t *opt, const char *arg)
{
	xfree(opt->acctg_freq);
	opt->acctg_freq = xstrdup(arg);
	if (validate_acctg_freq(opt->acctg_freq))
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}
COMMON_STRING_OPTION_GET_AND_RESET(acctg_freq);
COMMON_STRING_OPTION_SET_DATA(acctg_freq);
static slurm_cli_opt_t slurm_opt_acctg_freq = {
	.name = "acctg-freq",
	.has_arg = required_argument,
	.val = LONG_OPT_ACCTG_FREQ,
	.set_func = arg_set_acctg_freq,
	.set_func_data = arg_set_data_acctg_freq,
	.get_func = arg_get_acctg_freq,
	.reset_func = arg_reset_acctg_freq,
};

static int arg_set_alloc_nodelist(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	xfree(opt->srun_opt->alloc_nodelist);
	opt->srun_opt->alloc_nodelist = xstrdup(arg);

	return SLURM_SUCCESS;
}
static char *arg_get_alloc_nodelist(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	return xstrdup(opt->srun_opt->alloc_nodelist);
}
static void arg_reset_alloc_nodelist(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		xfree(opt->srun_opt->alloc_nodelist);
}
static slurm_cli_opt_t slurm_opt_alloc_nodelist = {
	.name = NULL, /* envvar only */
	.has_arg = required_argument,
	.val = LONG_OPT_ALLOC_NODELIST,
	.set_func = arg_set_alloc_nodelist,
	.get_func = arg_get_alloc_nodelist,
	.reset_func = arg_reset_alloc_nodelist,
	.reset_each_pass = true,
};

COMMON_SBATCH_STRING_OPTION(array_inx);
static slurm_cli_opt_t slurm_opt_array = {
	.name = "array",
	.has_arg = required_argument,
	.val = 'a',
	.set_func_sbatch = arg_set_array_inx,
	.set_func_data = arg_set_data_array_inx,
	.get_func = arg_get_array_inx,
	.reset_func = arg_reset_array_inx,
};


static data_for_each_cmd_t _parse_argv(const data_t *data, void *arg)
{
	char ***argv = arg;
	(**argv) = xstrdup(data_get_string_const(data));
	(*argv)++;
	return DATA_FOR_EACH_CONT;
}

static int arg_set_data_argv(slurm_opt_t *opt, const data_t *arg,
			     data_t *errors)
{
	int argc = data_get_list_length(arg);
	char **argv = xcalloc(argc, sizeof(char *));
	opt->sbatch_opt->script_argc = argc;
	opt->sbatch_opt->script_argv = argv;
	/* argv will be advanced by _parse_argv */
	data_list_for_each_const(arg, _parse_argv, &argv);
	return SLURM_SUCCESS;
}
static char *arg_get_argv(slurm_opt_t *opt)
{
	char *argv_string = NULL;
	for (int i = 0; i < opt->sbatch_opt->script_argc; i++)
		xstrfmtcat(argv_string, " %s",
			   opt->sbatch_opt->script_argv[i]);
	return argv_string;
}
static void arg_reset_argv(slurm_opt_t *opt)
{
	if (opt->sbatch_opt) {
		xfree(opt->sbatch_opt->script_argv);
		opt->sbatch_opt->script_argc = 0;
	}
}
static slurm_cli_opt_t slurm_opt_argv = {
	.name = "argv",
	.has_arg = required_argument,
	.val = LONG_OPT_ARGV,
	.set_func_data = arg_set_data_argv,
	.get_func = arg_get_argv,
	.reset_func = arg_reset_argv,
};


COMMON_SBATCH_STRING_OPTION(batch_features);
static slurm_cli_opt_t slurm_opt_batch = {
	.name = "batch",
	.has_arg = required_argument,
	.val = LONG_OPT_BATCH,
	.set_func_sbatch = arg_set_batch_features,
	.set_func_data = arg_set_data_batch_features,
	.get_func = arg_get_batch_features,
	.reset_func = arg_reset_batch_features,
};

COMMON_STRING_OPTION(burst_buffer_file);
static slurm_cli_opt_t slurm_opt_bbf = {
	.name = "bbf",
	.has_arg = required_argument,
	.val = LONG_OPT_BURST_BUFFER_FILE,
	.set_func_salloc = arg_set_burst_buffer_file,
	.set_func_sbatch = arg_set_burst_buffer_file,
	.set_func_srun = arg_set_burst_buffer_file,
	.set_func_data = arg_set_data_burst_buffer_file,
	.get_func = arg_get_burst_buffer_file,
	.reset_func = arg_reset_burst_buffer_file,
};

static int arg_set_bcast(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	opt->srun_opt->bcast_flag = true;
	opt->srun_opt->bcast_file = xstrdup(arg);

	return SLURM_SUCCESS;
}
static char *arg_get_bcast(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	if (opt->srun_opt->bcast_flag && !opt->srun_opt->bcast_file)
		return xstrdup("set");
	else if (opt->srun_opt->bcast_flag)
		return xstrdup(opt->srun_opt->bcast_file);
	return NULL;
}
static void arg_reset_bcast(slurm_opt_t *opt)
{
	if (opt->srun_opt) {
		opt->srun_opt->bcast_flag = false;
		xfree(opt->srun_opt->bcast_file);
	}
}
static slurm_cli_opt_t slurm_opt_bcast = {
	.name = "bcast",
	.has_arg = optional_argument,
	.val = LONG_OPT_BCAST,
	.set_func_srun = arg_set_bcast,
	.get_func = arg_get_bcast,
	.reset_func = arg_reset_bcast,
	.reset_each_pass = true,
};

static int arg_set_bcast_exclude(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	xfree(opt->srun_opt->bcast_exclude);
	opt->srun_opt->bcast_exclude = xstrdup(arg);

	return SLURM_SUCCESS;
}
static char *arg_get_bcast_exclude(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	if (opt->srun_opt->bcast_exclude)
		return xstrdup(opt->srun_opt->bcast_exclude);

	return NULL;
}
static void arg_reset_bcast_exclude(slurm_opt_t *opt)
{
	if (opt->srun_opt) {
		xfree(opt->srun_opt->bcast_exclude);
		opt->srun_opt->bcast_exclude =
			xstrdup(slurm_conf.bcast_exclude);
	}
}
static slurm_cli_opt_t slurm_opt_bcast_exclude = {
	.name = "bcast-exclude",
	.has_arg = required_argument,
	.val = LONG_OPT_BCAST_EXCLUDE,
	.set_func_srun = arg_set_bcast_exclude,
	.get_func = arg_get_bcast_exclude,
	.reset_func = arg_reset_bcast_exclude,
	.reset_each_pass = true,
};

static int arg_set_begin(slurm_opt_t *opt, const char *arg)
{
	if (!(opt->begin = parse_time(arg, 0))) {
		error("Invalid --begin specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_begin(slurm_opt_t *opt, const data_t *arg,
			      data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if (!(opt->begin = parse_time(str, 0))) {
		rc = ESLURM_INVALID_TIME_VALUE;
		ADD_DATA_ERROR("Unable to parse time", rc);
	}

	xfree(str);
	return rc;
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
	.set_func_salloc = arg_set_begin,
	.set_func_sbatch = arg_set_begin,
	.set_func_srun = arg_set_begin,
	.set_func_data = arg_set_data_begin,
	.get_func = arg_get_begin,
	.reset_func = arg_reset_begin,
};

/* Also see --no-bell below */
static int arg_set_bell(slurm_opt_t *opt, const char *arg)
{
	if (opt->salloc_opt)
		opt->salloc_opt->bell = BELL_ALWAYS;

	return SLURM_SUCCESS;
}
static char *arg_get_bell(slurm_opt_t *opt)
{
	if (!opt->salloc_opt)
		return xstrdup("invalid-context");

	if (opt->salloc_opt->bell == BELL_ALWAYS)
		return xstrdup("bell-always");
	else if (opt->salloc_opt->bell == BELL_AFTER_DELAY)
		return xstrdup("bell-after-delay");
	else if (opt->salloc_opt->bell == BELL_NEVER)
		return xstrdup("bell-never");
	return NULL;
}
static void arg_reset_bell(slurm_opt_t *opt)
{
	if (opt->salloc_opt)
		opt->salloc_opt->bell = BELL_AFTER_DELAY;
}
static slurm_cli_opt_t slurm_opt_bell = {
	.name = "bell",
	.has_arg = no_argument,
	.val = LONG_OPT_BELL,
	.set_func_salloc = arg_set_bell,
	.get_func = arg_get_bell,
	.reset_func = arg_reset_bell,
};

COMMON_STRING_OPTION(burst_buffer);
static slurm_cli_opt_t slurm_opt_bb = {
	.name = "bb",
	.has_arg = required_argument,
	.val = LONG_OPT_BURST_BUFFER_SPEC,
	.set_func_salloc = arg_set_burst_buffer,
	.set_func_sbatch = arg_set_burst_buffer,
	.set_func_srun = arg_set_burst_buffer,
	.set_func_data = arg_set_data_burst_buffer,
	.get_func = arg_get_burst_buffer,
	.reset_func = arg_reset_burst_buffer,
	.reset_each_pass = true,
};

COMMON_STRING_OPTION(c_constraint);
static slurm_cli_opt_t slurm_opt_c_constraint = {
	.name = "cluster-constraint",
	.has_arg = required_argument,
	.val = LONG_OPT_CLUSTER_CONSTRAINT,
	.set_func_salloc = arg_set_c_constraint,
	.set_func_sbatch = arg_set_c_constraint,
	.set_func_srun = arg_set_c_constraint,
	.set_func_data = arg_set_data_c_constraint,
	.get_func = arg_get_c_constraint,
	.reset_func = arg_reset_c_constraint,
};

static int arg_set_chdir(slurm_opt_t *opt, const char *arg)
{
	if (is_full_path(arg))
		opt->chdir = xstrdup(arg);
	else
		opt->chdir = make_full_path(arg);

	return SLURM_SUCCESS;
}
static int arg_set_data_chdir(slurm_opt_t *opt, const data_t *arg,
			      data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if (is_full_path(str)) {
		opt->chdir = str;
		str = NULL;
	} else
		opt->chdir = make_full_path(str);

	xfree(str);

	return SLURM_SUCCESS;
}
COMMON_STRING_OPTION_GET(chdir);
static void arg_reset_chdir(slurm_opt_t *opt)
{
	char buf[MAXPATHLEN + 1];
	xfree(opt->chdir);
	if (opt->salloc_opt || opt->scron_opt)
		return;

	if (!(getcwd(buf, MAXPATHLEN))) {
		error("getcwd failed: %m");
		exit(-1);
	}
	opt->chdir = xstrdup(buf);
}
static slurm_cli_opt_t slurm_opt_chdir = {
	.name = "chdir",
	.has_arg = required_argument,
	.val = 'D',
	.set_func = arg_set_chdir,
	.set_func_data = arg_set_data_chdir,
	.get_func = arg_get_chdir,
	.reset_func = arg_reset_chdir,
};

/* --clusters and --cluster are equivalent */
COMMON_STRING_OPTION(clusters);
static slurm_cli_opt_t slurm_opt_clusters = {
	.name = "clusters",
	.has_arg = required_argument,
	.val = 'M',
	.set_func_salloc = arg_set_clusters,
	.set_func_sbatch = arg_set_clusters,
	.set_func_srun = arg_set_clusters,
	.set_func_data = arg_set_data_clusters,
	.get_func = arg_get_clusters,
	.reset_func = arg_reset_clusters,
};
static slurm_cli_opt_t slurm_opt_cluster = {
	.name = "cluster",
	.has_arg = required_argument,
	.val = LONG_OPT_CLUSTER,
	.set_func_salloc = arg_set_clusters,
	.set_func_sbatch = arg_set_clusters,
	.set_func_srun = arg_set_clusters,
	.set_func_data = arg_set_data_clusters,
	.get_func = arg_get_clusters,
	.reset_func = arg_reset_clusters,
};

COMMON_STRING_OPTION(comment);
static slurm_cli_opt_t slurm_opt_comment = {
	.name = "comment",
	.has_arg = required_argument,
	.val = LONG_OPT_COMMENT,
	.set_func = arg_set_comment,
	.set_func_data = arg_set_data_comment,
	.get_func = arg_get_comment,
	.reset_func = arg_reset_comment,
};

static int arg_set_compress(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	opt->srun_opt->compress = parse_compress_type(arg);

	return SLURM_SUCCESS;
}
static char *arg_get_compress(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	if (opt->srun_opt->compress == COMPRESS_LZ4)
		return xstrdup("lz4");
	return xstrdup("none");
}
static void arg_reset_compress(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->compress = COMPRESS_OFF;
}
static slurm_cli_opt_t slurm_opt_compress = {
	.name = "compress",
	.has_arg = optional_argument,
	.val = LONG_OPT_COMPRESS,
	.set_func_srun = arg_set_compress,
	.get_func = arg_get_compress,
	.reset_func = arg_reset_compress,
	.reset_each_pass = true,
};

COMMON_STRING_OPTION(constraint);
static slurm_cli_opt_t slurm_opt_constraint = {
	.name = "constraint",
	.has_arg = required_argument,
	.val = 'C',
	.set_func = arg_set_constraint,
	.set_func_data = arg_set_data_constraint,
	.get_func = arg_get_constraint,
	.reset_func = arg_reset_constraint,
	.reset_each_pass = true,
};

COMMON_STRING_OPTION(container);
static slurm_cli_opt_t slurm_opt_container = {
	.name = "container",
	.has_arg = required_argument,
	.val = LONG_OPT_CONTAINER,
	.set_func = arg_set_container,
	.set_func_data = arg_set_data_container,
	.get_func = arg_get_container,
	.reset_func = arg_reset_container,
};

COMMON_STRING_OPTION_SET(context);
COMMON_STRING_OPTION_SET_DATA(context);
COMMON_STRING_OPTION_GET(context);
static void arg_reset_context(slurm_opt_t *opt)
{
	xfree(opt->context);

#ifdef WITH_SELINUX
	if (is_selinux_enabled() == 1) {
		char *context;
		getcon(&context);
		opt->context = xstrdup(context);
		freecon(context);
	}
#endif
}
static slurm_cli_opt_t slurm_opt_context = {
	.name = "context",
	.has_arg = required_argument,
	.val = LONG_OPT_CONTEXT,
	.set_func = arg_set_context,
	.set_func_data = arg_set_data_context,
	.get_func = arg_get_context,
	.reset_func = arg_reset_context,
};

COMMON_BOOL_OPTION(contiguous, "contiguous");
static slurm_cli_opt_t slurm_opt_contiguous = {
	.name = "contiguous",
	.has_arg = no_argument,
	.val = LONG_OPT_CONTIGUOUS,
	.set_func = arg_set_contiguous,
	.set_func_data = arg_set_data_contiguous,
	.get_func = arg_get_contiguous,
	.reset_func = arg_reset_contiguous,
	.reset_each_pass = true,
};

static int arg_set_core_spec(slurm_opt_t *opt, const char *arg)
{
	if (opt->srun_opt)
		opt->srun_opt->core_spec_set = true;

	opt->core_spec = parse_int("--core-spec", arg, false);

	return SLURM_SUCCESS;
}
static int arg_set_data_core_spec(slurm_opt_t *opt, const data_t *arg,
				  data_t *errors)
{
	int rc;
	int64_t val;

	if ((rc = data_get_int_converted(arg, &val)))
		ADD_DATA_ERROR("Unable to read int", rc);
	else if (val < 0)
		ADD_DATA_ERROR("Invalid core specification", rc);
	else {
		if (opt->srun_opt)
			opt->srun_opt->core_spec_set = (val > 0);
		opt->core_spec = val;
	}

	return rc;
}
static char *arg_get_core_spec(slurm_opt_t *opt)
{
	if ((opt->core_spec == NO_VAL16) ||
	    (opt->core_spec & CORE_SPEC_THREAD))
		return xstrdup("unset");
	return xstrdup_printf("%d", opt->core_spec);
}
static void arg_reset_core_spec(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->core_spec_set = false;

	opt->core_spec = NO_VAL16;
}
static slurm_cli_opt_t slurm_opt_core_spec = {
	.name = "core-spec",
	.has_arg = required_argument,
	.val = 'S',
	.set_func = arg_set_core_spec,
	.set_func_data = arg_set_data_core_spec,
	.get_func = arg_get_core_spec,
	.reset_func = arg_reset_core_spec,
	.reset_each_pass = true,
};

COMMON_INT_OPTION_SET(cores_per_socket, "--cores-per-socket");
COMMON_INT_OPTION_GET(cores_per_socket);
COMMON_INT_OPTION_SET_DATA(cores_per_socket);
COMMON_OPTION_RESET(cores_per_socket, NO_VAL);
static slurm_cli_opt_t slurm_opt_cores_per_socket = {
	.name = "cores-per-socket",
	.has_arg = required_argument,
	.val = LONG_OPT_CORESPERSOCKET,
	.set_func = arg_set_cores_per_socket,
	.set_func_data = arg_set_data_cores_per_socket,
	.get_func = arg_get_cores_per_socket,
	.reset_func = arg_reset_cores_per_socket,
	.reset_each_pass = true,
};

static int arg_set_cpu_bind(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	if (slurm_verify_cpu_bind(arg, &opt->srun_opt->cpu_bind,
				  &opt->srun_opt->cpu_bind_type))
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}
static char *arg_get_cpu_bind(slurm_opt_t *opt)
{
	char tmp[100];

	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	slurm_sprint_cpu_bind_type(tmp, opt->srun_opt->cpu_bind_type);

	return xstrdup(tmp);
}
static void arg_reset_cpu_bind(slurm_opt_t *opt)
{
	if (opt->srun_opt) {
		bool cpu_bind_verbose = false;
		if (opt->srun_opt->cpu_bind_type & CPU_BIND_VERBOSE)
			cpu_bind_verbose = true;

		xfree(opt->srun_opt->cpu_bind);
		opt->srun_opt->cpu_bind_type = 0;
		if (cpu_bind_verbose)
			slurm_verify_cpu_bind("verbose",
					      &opt->srun_opt->cpu_bind,
					      &opt->srun_opt->cpu_bind_type);
	}
}
static slurm_cli_opt_t slurm_opt_cpu_bind = {
	.name = "cpu-bind",
	.has_arg = required_argument,
	.val = LONG_OPT_CPU_BIND,
	.set_func_srun = arg_set_cpu_bind,
	.get_func = arg_get_cpu_bind,
	.reset_func = arg_reset_cpu_bind,
	.reset_each_pass = true,
};
/*
 * OpenMPI hard-coded --cpu_bind as part of their mpirun/mpiexec launch
 * scripting for a long time, and thus we're stuck supporting this deprecated
 * version indefinitely.
 *
 * Keep this after the preferred --cpu-bind handling so cli_filter sees that
 * and not this form.
 */
static slurm_cli_opt_t slurm_opt_cpu_underscore_bind = {
	.name = "cpu_bind",
	.has_arg = required_argument,
	.val = LONG_OPT_CPU_BIND,
	.set_func_srun = arg_set_cpu_bind,
	.get_func = arg_get_cpu_bind,
	.reset_func = arg_reset_cpu_bind,
	.reset_each_pass = true,
};

static int arg_set_cpu_freq(slurm_opt_t *opt, const char *arg)
{
	if (cpu_freq_verify_cmdline(arg, &opt->cpu_freq_min,
				    &opt->cpu_freq_max, &opt->cpu_freq_gov)) {
		error("Invalid --cpu-freq argument");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_cpu_freq(slurm_opt_t *opt, const data_t *arg,
				 data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if ((rc = cpu_freq_verify_cmdline(str, &opt->cpu_freq_min,
					       &opt->cpu_freq_max,
					       &opt->cpu_freq_gov)))
		ADD_DATA_ERROR("Unable to parse CPU frequency", rc);
	xfree(str);

	return rc;
}
static char *arg_get_cpu_freq(slurm_opt_t *opt)
{
	return cpu_freq_to_cmdline(opt->cpu_freq_min,
				   opt->cpu_freq_max,
				   opt->cpu_freq_gov);
}
static void arg_reset_cpu_freq(slurm_opt_t *opt)
{
	opt->cpu_freq_min = NO_VAL;
	opt->cpu_freq_max = NO_VAL;
	opt->cpu_freq_gov = NO_VAL;
}
static slurm_cli_opt_t slurm_opt_cpu_freq = {
	.name = "cpu-freq",
	.has_arg = required_argument,
	.val = LONG_OPT_CPU_FREQ,
	.set_func = arg_set_cpu_freq,
	.set_func_data = arg_set_data_cpu_freq,
	.get_func = arg_get_cpu_freq,
	.reset_func = arg_reset_cpu_freq,
	.reset_each_pass = true,
};

COMMON_INT_OPTION(cpus_per_gpu, "--cpus-per-gpu");
static slurm_cli_opt_t slurm_opt_cpus_per_gpu = {
	.name = "cpus-per-gpu",
	.has_arg = required_argument,
	.val = LONG_OPT_CPUS_PER_GPU,
	.set_func = arg_set_cpus_per_gpu,
	.set_func_data = arg_set_data_cpus_per_gpu,
	.get_func = arg_get_cpus_per_gpu,
	.reset_func = arg_reset_cpus_per_gpu,
	.reset_each_pass = true,
};

static int arg_set_cpus_per_task(slurm_opt_t *opt, const char *arg)
{
	int old_cpus_per_task = opt->cpus_per_task;
	opt->cpus_per_task = parse_int("--cpus-per-task", arg, true);

	if (opt->cpus_set && opt->srun_opt &&
	    (old_cpus_per_task < opt->cpus_per_task))
		info("Job step's --cpus-per-task value exceeds that of job (%d > %d). Job step may never run.",
		     opt->cpus_per_task, old_cpus_per_task);

	opt->cpus_set = true;
	return SLURM_SUCCESS;
}
static int arg_set_data_cpus_per_task(slurm_opt_t *opt, const data_t *arg,
				      data_t *errors)
{
	int64_t val;
	int rc = data_get_int_converted(arg, &val);
	if (rc)
		ADD_DATA_ERROR("Unable to read integer value", rc);
	else if (val >= INT_MAX) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Integer too large", SLURM_ERROR);
	} else if (val < 1) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("cpus per task much be greater than 0", SLURM_ERROR);
	} else {
		int old_cpus_per_task = opt->cpus_per_task;
		opt->cpus_per_task = (int) val;

		if (opt->cpus_set && opt->srun_opt &&
		    (old_cpus_per_task < opt->cpus_per_task)) {
			char str[1024];

			snprintf(str, sizeof(str),
				"Job step's --cpus-per-task value exceeds that of job (%d > %d). Job step may never run.",
				opt->cpus_per_task, old_cpus_per_task);

			rc = SLURM_ERROR;
			ADD_DATA_ERROR(str, rc);
		}

		opt->cpus_set = true;
	}
	return rc;
}
COMMON_INT_OPTION_GET(cpus_per_task);
static void arg_reset_cpus_per_task(slurm_opt_t *opt)
{
	opt->cpus_per_task = 0;
	opt->cpus_set = false;
}
static slurm_cli_opt_t slurm_opt_cpus_per_task = {
	.name = "cpus-per-task",
	.has_arg = required_argument,
	.val = 'c',
	.set_func = arg_set_cpus_per_task,
	.set_func_data = arg_set_data_cpus_per_task,
	.get_func = arg_get_cpus_per_task,
	.reset_func = arg_reset_cpus_per_task,
	.reset_each_pass = true,
};

static int arg_set_deadline(slurm_opt_t *opt, const char *arg)
{
	if (!(opt->deadline = parse_time(arg, 0))) {
		error("Invalid --deadline specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_deadline(slurm_opt_t *opt, const data_t *arg,
				 data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if (!(opt->deadline = parse_time(str, 0))) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Invalid deadline time", rc);
	}

	xfree(str);

	return rc;
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
	.set_func_data = arg_set_data_deadline,
	.get_func = arg_get_deadline,
	.reset_func = arg_reset_deadline,
};

static int arg_set_debugger_test(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	opt->srun_opt->debugger_test = true;

	return SLURM_SUCCESS;
}
static char *arg_get_debugger_test(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return NULL;

	return xstrdup(opt->srun_opt->debugger_test ? "set" : "unset");
}
static void arg_reset_debugger_test(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->debugger_test = false;
}
static slurm_cli_opt_t slurm_opt_debugger_test = {
	.name = "debugger-test",
	.has_arg = no_argument,
	.val = LONG_OPT_DEBUGGER_TEST,
	.set_func_srun = arg_set_debugger_test,
	.get_func = arg_get_debugger_test,
	.reset_func = arg_reset_debugger_test,
};

static int arg_set_delay_boot(slurm_opt_t *opt, const char *arg)
{
	if ((opt->delay_boot = time_str2secs(arg)) == NO_VAL) {
		error("Invalid --delay-boot specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_delay_boot(slurm_opt_t *opt, const data_t *arg,
				   data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if ((opt->delay_boot = time_str2secs(str)) == NO_VAL) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Invalid delay boot specification", rc);
	}

	xfree(str);
	return rc;
}
static char *arg_get_delay_boot(slurm_opt_t *opt)
{
	char time_str[32];

	secs2time_str(opt->delay_boot, time_str, sizeof(time_str));

	return xstrdup(time_str);
}
COMMON_OPTION_RESET(delay_boot, NO_VAL);
static slurm_cli_opt_t slurm_opt_delay_boot = {
	.name = "delay-boot",
	.has_arg = required_argument,
	.val = LONG_OPT_DELAY_BOOT,
	.set_func = arg_set_delay_boot,
	.set_func_data = arg_set_data_delay_boot,
	.get_func = arg_get_delay_boot,
	.reset_func = arg_reset_delay_boot,
};

static data_for_each_cmd_t _parse_env(const char *key, const data_t *data, void *arg)
{
	int rc = DATA_FOR_EACH_FAIL;
	char ***env = arg;
	char *ebuf = NULL;

	if (!data_get_string_converted(data, &ebuf)) {
		env_array_append(env, key, ebuf);
		rc = DATA_FOR_EACH_CONT;
	}
	xfree(ebuf);

	return rc;
}

static int arg_set_data_environment(slurm_opt_t *opt, const data_t *arg,
				    data_t *errors)
{
	if (data_get_type(arg) != DATA_TYPE_DICT) {
		ADD_DATA_ERROR("environment must be a dictionary", SLURM_ERROR);
		return SLURM_ERROR;
	}

	/*
	 * always start with a fresh environment if client
	 * provides one explicitly
	 */
	if (opt->environment)
		env_array_free(opt->environment);
	opt->environment = env_array_create();

	if (data_dict_for_each_const(arg, _parse_env, &opt->environment) < 0) {
		ADD_DATA_ERROR("failure parsing environment", SLURM_ERROR);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static void arg_reset_environment(slurm_opt_t *opt)
{
	env_array_free(opt->environment);
	opt->environment = NULL;
}
static char *arg_get_environment(slurm_opt_t *opt)
{
	return NULL;
}
static slurm_cli_opt_t slurm_opt_environment = {
	.name = "environment",
	.val = LONG_OPT_ENVIRONMENT,
	.has_arg = required_argument,
	.set_func_data = arg_set_data_environment,
	.get_func = arg_get_environment,
	.reset_func = arg_reset_environment,
};

COMMON_STRING_OPTION(dependency);
static slurm_cli_opt_t slurm_opt_dependency = {
	.name = "dependency",
	.has_arg = required_argument,
	.val = 'd',
	.set_func = arg_set_dependency,
	.set_func_data = arg_set_data_dependency,
	.get_func = arg_get_dependency,
	.reset_func = arg_reset_dependency,
};

COMMON_SRUN_BOOL_OPTION(disable_status);
static slurm_cli_opt_t slurm_opt_disable_status = {
	.name = "disable-status",
	.has_arg = no_argument,
	.val = 'X',
	.set_func_srun = arg_set_disable_status,
	.get_func = arg_get_disable_status,
	.reset_func = arg_reset_disable_status,
};

static int arg_set_distribution(slurm_opt_t *opt, const char *arg)
{
	opt->distribution = verify_dist_type(arg, &opt->plane_size);
	if (opt->distribution == SLURM_ERROR) {
		error("Invalid --distribution specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_distribution(slurm_opt_t *opt, const data_t *arg,
				     data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		/* FIXME: ignore SLURM_DIST_PLANESIZE envvar for slurmrestd */
		opt->distribution = verify_dist_type(str, &opt->plane_size);

		if (opt->distribution == SLURM_ERROR) {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid distribution", rc);
		}
	}

	xfree(str);
	return rc;
}
static char *arg_get_distribution(slurm_opt_t *opt)
{
	char *dist = NULL;
	set_distribution(opt->distribution, &dist);
	if (opt->distribution == SLURM_DIST_PLANE)
		xstrfmtcat(dist, "=%u", opt->plane_size);
	return dist;
}
static void arg_reset_distribution(slurm_opt_t *opt)
{
	opt->distribution = SLURM_DIST_UNKNOWN;
	opt->plane_size = NO_VAL;
}
static slurm_cli_opt_t slurm_opt_distribution = {
	.name = "distribution",
	.has_arg = required_argument,
	.val = 'm',
	.set_func = arg_set_distribution,
	.set_func_data = arg_set_data_distribution,
	.get_func = arg_get_distribution,
	.reset_func = arg_reset_distribution,
	.reset_each_pass = true,
};

COMMON_SRUN_STRING_OPTION(epilog);
static slurm_cli_opt_t slurm_opt_epilog = {
	.name = "epilog",
	.has_arg = required_argument,
	.val = LONG_OPT_EPILOG,
	.set_func_srun = arg_set_epilog,
	.get_func = arg_get_epilog,
	.reset_func = arg_reset_epilog,
};

static int arg_set_efname(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt && !opt->scron_opt && !opt->srun_opt)
		return SLURM_ERROR;

	xfree(opt->efname);
	if (!xstrcasecmp(arg, "none"))
		opt->efname = xstrdup("/dev/null");
	else
		opt->efname = xstrdup(arg);

	return SLURM_SUCCESS;
}
static int arg_set_data_efname(slurm_opt_t *opt, const data_t *arg,
			       data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		xfree(opt->efname);
		if (!xstrcasecmp(str, "none"))
			opt->efname = xstrdup("/dev/null");
		else {
			opt->efname = str;
			str = NULL;
		}
	}

	xfree(str);
	return rc;
}
COMMON_STRING_OPTION_GET(efname);
COMMON_STRING_OPTION_RESET(efname);
static slurm_cli_opt_t slurm_opt_error = {
	.name = "error",
	.has_arg = required_argument,
	.val = 'e',
	.set_func_sbatch = arg_set_efname,
	.set_func_scron = arg_set_efname,
	.set_func_srun = arg_set_efname,
	.set_func_data = arg_set_data_efname,
	.get_func = arg_get_efname,
	.reset_func = arg_reset_efname,
};

COMMON_STRING_OPTION(exclude);
static slurm_cli_opt_t slurm_opt_exclude = {
	.name = "exclude",
	.has_arg = required_argument,
	.val = 'x',
	.set_func = arg_set_exclude,
	.set_func_data = arg_set_data_exclude,
	.get_func = arg_get_exclude,
	.reset_func = arg_reset_exclude,
};

static int arg_set_exclusive(slurm_opt_t *opt, const char *arg)
{
	if (!arg || !xstrcasecmp(arg, "exclusive")) {
		if (opt->srun_opt) {
			opt->srun_opt->exclusive = true;
			opt->srun_opt->exact = true;
		}
		opt->shared = JOB_SHARED_NONE;
	} else if (!xstrcasecmp(arg, "oversubscribe")) {
		opt->shared = JOB_SHARED_OK;
	} else if (!xstrcasecmp(arg, "user")) {
		opt->shared = JOB_SHARED_USER;
	} else if (!xstrcasecmp(arg, "mcs")) {
		opt->shared = JOB_SHARED_MCS;
	} else {
		error("Invalid --exclusive specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_exclusive(slurm_opt_t *opt, const data_t *arg,
				  data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		if (!str || !xstrcasecmp(str, "exclusive")) {
			if (opt->srun_opt) {
				opt->srun_opt->exclusive = true;
				opt->srun_opt->exact = true;
			}
			opt->shared = JOB_SHARED_NONE;
		} else if (!xstrcasecmp(str, "oversubscribe")) {
			opt->shared = JOB_SHARED_OK;
		} else if (!xstrcasecmp(str, "user")) {
			opt->shared = JOB_SHARED_USER;
		} else if (!xstrcasecmp(str, "mcs")) {
			opt->shared = JOB_SHARED_MCS;
		} else {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid exclusive specification", rc);
		}
	}

	xfree(str);
	return rc;
}
static char *arg_get_exclusive(slurm_opt_t *opt)
{
	if (opt->shared == JOB_SHARED_NONE)
		return xstrdup("exclusive");
	if (opt->shared == JOB_SHARED_OK)
		return xstrdup("oversubscribe");
	if (opt->shared == JOB_SHARED_USER)
		return xstrdup("user");
	if (opt->shared == JOB_SHARED_MCS)
		return xstrdup("mcs");
	if (opt->shared == NO_VAL16)
		return xstrdup("unset");
	return NULL;
}
/* warning: shared with --oversubscribe below */
static void arg_reset_shared(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->exclusive = true;
	opt->shared = NO_VAL16;
}
static slurm_cli_opt_t slurm_opt_exclusive = {
	.name = "exclusive",
	.has_arg = optional_argument,
	.val = LONG_OPT_EXCLUSIVE,
	.set_func = arg_set_exclusive,
	.set_func_data = arg_set_data_exclusive,
	.get_func = arg_get_exclusive,
	.reset_func = arg_reset_shared,
	.reset_each_pass = true,
};

COMMON_SRUN_BOOL_OPTION(exact);
static slurm_cli_opt_t slurm_opt_exact = {
	.name = "exact",
	.has_arg = no_argument,
	.val = LONG_OPT_EXACT,
	.set_func_srun = arg_set_exact,
	.get_func = arg_get_exact,
	.reset_func = arg_reset_exact,
};

static int arg_set_export(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt && !opt->scron_opt && !opt->srun_opt)
		return SLURM_ERROR;

	opt->export_env = xstrdup(arg);

	return SLURM_SUCCESS;
}
static char *arg_get_export(slurm_opt_t *opt)
{
	if (!opt->sbatch_opt && !opt->scron_opt && !opt->srun_opt)
		return xstrdup("invalid-context");

	return xstrdup(opt->export_env);

	return NULL;
}
static void arg_reset_export(slurm_opt_t *opt)
{
	xfree(opt->export_env);
}
static slurm_cli_opt_t slurm_opt_export = {
	.name = "export",
	.has_arg = required_argument,
	.val = LONG_OPT_EXPORT,
	.set_func_sbatch = arg_set_export,
	.set_func_scron = arg_set_export,
	.set_func_srun = arg_set_export,
	.get_func = arg_get_export,
	.reset_func = arg_reset_export,
};

COMMON_SBATCH_STRING_OPTION(export_file);
static slurm_cli_opt_t slurm_opt_export_file = {
	.name = "export-file",
	.has_arg = required_argument,
	.val = LONG_OPT_EXPORT_FILE,
	.set_func_sbatch = arg_set_export_file,
	.set_func_data = arg_set_data_export_file,
	.get_func = arg_get_export_file,
	.reset_func = arg_reset_export_file,
};

static int arg_set_extra_node_info(slurm_opt_t *opt, const char *arg)
{
	cpu_bind_type_t *cpu_bind_type = NULL;

	if (opt->srun_opt)
		cpu_bind_type = &opt->srun_opt->cpu_bind_type;
	opt->extra_set = verify_socket_core_thread_count(arg,
							 &opt->sockets_per_node,
							 &opt->cores_per_socket,
							 &opt->threads_per_core,
							 cpu_bind_type);

	if (!opt->extra_set) {
		error("Invalid --extra-node-info specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static char *arg_get_extra_node_info(slurm_opt_t *opt)
{
	char *tmp = NULL;
	if (opt->sockets_per_node != NO_VAL)
		xstrfmtcat(tmp, "%d", opt->sockets_per_node);
	if (opt->cores_per_socket != NO_VAL)
		xstrfmtcat(tmp, ":%d", opt->cores_per_socket);
	if (opt->threads_per_core != NO_VAL)
		xstrfmtcat(tmp, ":%d", opt->threads_per_core);

	if (!tmp)
		return xstrdup("unset");
	return tmp;
}
static void arg_reset_extra_node_info(slurm_opt_t *opt)
{
	opt->extra_set = false;
	opt->sockets_per_node = NO_VAL;
	opt->cores_per_socket = NO_VAL;
	opt->threads_per_core = NO_VAL;
}
static slurm_cli_opt_t slurm_opt_extra_node_info = {
	.name = "extra-node-info",
	.has_arg = required_argument,
	.val = 'B',
	.set_func = arg_set_extra_node_info,
	.get_func = arg_get_extra_node_info,
	.reset_func = arg_reset_extra_node_info,
	.reset_each_pass = true,
};

static int arg_set_get_user_env(slurm_opt_t *opt, const char *arg)
{
	char *end_ptr;

	if (!arg) {
		opt->get_user_env_time = 0;
		return SLURM_SUCCESS;
	}

	opt->get_user_env_time = strtol(arg, &end_ptr, 10);

	if (!end_ptr || (end_ptr[0] == '\0'))
		return SLURM_SUCCESS;

	if ((end_ptr[0] == 's') || (end_ptr[0] == 'S'))
		opt->get_user_env_mode = 1;
	else if ((end_ptr[0] == 'l') || (end_ptr[0] == 'L'))
		opt->get_user_env_mode = 2;
	else {
		error("Invalid --get-user-env specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_get_user_env(slurm_opt_t *opt, const data_t *arg,
				     data_t *errors)
{
	int rc = SLURM_SUCCESS;
	char *str = NULL;

	if ((data_get_type(arg) == DATA_TYPE_NULL))
		opt->get_user_env_time = 0;
	else if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		char *end_ptr;

		opt->get_user_env_time = strtol(str, &end_ptr, 10);

		if (!end_ptr || (end_ptr[0] == '\0'))
			opt->get_user_env_mode = -1; /* not set */
		else if ((end_ptr[0] == 's') || (end_ptr[0] == 'S'))
			opt->get_user_env_mode = 1;
		else if ((end_ptr[0] == 'l') || (end_ptr[0] == 'L'))
			opt->get_user_env_mode = 2;
		else {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid get user environment specification", rc);
		}
	}

	xfree(str);
	return rc;
}
static char *arg_get_get_user_env(slurm_opt_t *opt)
{
	if (opt->get_user_env_mode == 1)
		return xstrdup_printf("%dS", opt->get_user_env_time);
	else if (opt->get_user_env_mode == 2)
		return xstrdup_printf("%dL", opt->get_user_env_time);
	else if (opt->get_user_env_time != -1)
		return xstrdup_printf("%d", opt->get_user_env_time);
	return NULL;
}
static void arg_reset_get_user_env(slurm_opt_t *opt)
{
	opt->get_user_env_mode = -1;
	opt->get_user_env_time = -1;
}
static slurm_cli_opt_t slurm_opt_get_user_env = {
	.name = "get-user-env",
	.has_arg = optional_argument,
	.val = LONG_OPT_GET_USER_ENV,
	.set_func_salloc = arg_set_get_user_env,
	.set_func_sbatch = arg_set_get_user_env,
	.set_func_data = arg_set_data_get_user_env,
	.get_func = arg_get_get_user_env,
	.reset_func = arg_reset_get_user_env,
};

static int arg_set_gid(slurm_opt_t *opt, const char *arg)
{
	if (getuid() != 0) {
		error("--gid only permitted by root user");
		return SLURM_ERROR;
	}

	if (gid_from_string(arg, &opt->gid) < 0) {
		error("Invalid --gid specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_gid(slurm_opt_t *opt, const data_t *arg,
			    data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if (gid_from_string(str, &opt->gid) < 0) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Invalid or unknown gid", rc);
	}

	xfree(str);
	return rc;
}
COMMON_INT_OPTION_GET(gid);
COMMON_OPTION_RESET(gid, getgid());
static slurm_cli_opt_t slurm_opt_gid = {
	.name = "gid",
	.has_arg = required_argument,
	.val = LONG_OPT_GID,
	.set_func = arg_set_gid,
	.set_func_data = arg_set_data_gid,
	.get_func = arg_get_gid,
	.reset_func = arg_reset_gid,
};

static int arg_set_gpu_bind(slurm_opt_t *opt, const char *arg)
{
	xfree(opt->gpu_bind);
	xfree(opt->tres_bind);
	opt->gpu_bind = xstrdup(arg);
	xstrfmtcat(opt->tres_bind, "gpu:%s", opt->gpu_bind);
	if (tres_bind_verify_cmdline(opt->tres_bind)) {
		error("Invalid --gpu-bind argument: %s", opt->tres_bind);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_gpu_bind(slurm_opt_t *opt, const data_t *arg,
				 data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		xfree(opt->gpu_bind);
		xfree(opt->tres_bind);
		opt->gpu_bind = xstrdup(str);
		xstrfmtcat(opt->tres_bind, "gpu:%s", opt->gpu_bind);
		if (tres_bind_verify_cmdline(opt->tres_bind)) {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid --gpu-bind argument", rc);
			xfree(opt->gpu_bind);
			xfree(opt->tres_bind);
		}
	}

	xfree(str);
	return rc;
}
static void arg_reset_gpu_bind(slurm_opt_t *opt)
{
	xfree(opt->gpu_bind);
	xfree(opt->tres_bind);
}
COMMON_STRING_OPTION_GET(gpu_bind);
static slurm_cli_opt_t slurm_opt_gpu_bind = {
	.name = "gpu-bind",
	.has_arg = required_argument,
	.val = LONG_OPT_GPU_BIND,
	.set_func = arg_set_gpu_bind,
	.set_func_data = arg_set_data_gpu_bind,
	.get_func = arg_get_gpu_bind,
	.reset_func = arg_reset_gpu_bind,
	.reset_each_pass = true,
};

static int arg_set_gpu_freq(slurm_opt_t *opt, const char *arg)
{
	xfree(opt->gpu_freq);
	xfree(opt->tres_freq);
	opt->gpu_freq = xstrdup(arg);
	xstrfmtcat(opt->tres_freq, "gpu:%s", opt->gpu_freq);
	if (tres_freq_verify_cmdline(opt->tres_freq)) {
		error("Invalid --gpu-freq argument: %s", opt->tres_freq);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_gpu_freq(slurm_opt_t *opt, const data_t *arg,
				 data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		xfree(opt->gpu_freq);
		xfree(opt->tres_freq);
		opt->gpu_freq = xstrdup(str);
		xstrfmtcat(opt->tres_freq, "gpu:%s", opt->gpu_freq);
		if (tres_freq_verify_cmdline(opt->tres_freq)) {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid --gpu-freq argument", rc);
			xfree(opt->gpu_freq);
			xfree(opt->tres_freq);
		}
	}

	xfree(str);
	return rc;
}
static void arg_reset_gpu_freq(slurm_opt_t *opt)
{
	xfree(opt->gpu_freq);
	xfree(opt->tres_freq);
}
COMMON_STRING_OPTION_GET(gpu_freq);
static slurm_cli_opt_t slurm_opt_gpu_freq = {
	.name = "gpu-freq",
	.has_arg = required_argument,
	.val = LONG_OPT_GPU_FREQ,
	.set_func = arg_set_gpu_freq,
	.set_func_data = arg_set_data_gpu_freq,
	.get_func = arg_get_gpu_freq,
	.reset_func = arg_reset_gpu_freq,
	.reset_each_pass = true,
};

COMMON_STRING_OPTION(gpus);
static slurm_cli_opt_t slurm_opt_gpus = {
	.name = "gpus",
	.has_arg = required_argument,
	.val = 'G',
	.set_func = arg_set_gpus,
	.set_func_data = arg_set_data_gpus,
	.get_func = arg_get_gpus,
	.reset_func = arg_reset_gpus,
	.reset_each_pass = true,
};

COMMON_STRING_OPTION(gpus_per_node);
static slurm_cli_opt_t slurm_opt_gpus_per_node = {
	.name = "gpus-per-node",
	.has_arg = required_argument,
	.val = LONG_OPT_GPUS_PER_NODE,
	.set_func = arg_set_gpus_per_node,
	.set_func_data = arg_set_data_gpus_per_node,
	.get_func = arg_get_gpus_per_node,
	.reset_func = arg_reset_gpus_per_node,
	.reset_each_pass = true,
};

COMMON_STRING_OPTION(gpus_per_socket);
static slurm_cli_opt_t slurm_opt_gpus_per_socket = {
	.name = "gpus-per-socket",
	.has_arg = required_argument,
	.val = LONG_OPT_GPUS_PER_SOCKET,
	.set_func = arg_set_gpus_per_socket,
	.set_func_data = arg_set_data_gpus_per_socket,
	.get_func = arg_get_gpus_per_socket,
	.reset_func = arg_reset_gpus_per_socket,
	.reset_each_pass = true,
};

COMMON_STRING_OPTION(gpus_per_task);
static slurm_cli_opt_t slurm_opt_gpus_per_task = {
	.name = "gpus-per-task",
	.has_arg = required_argument,
	.val = LONG_OPT_GPUS_PER_TASK,
	.set_func = arg_set_gpus_per_task,
	.set_func_data = arg_set_data_gpus_per_task,
	.get_func = arg_get_gpus_per_task,
	.reset_func = arg_reset_gpus_per_task,
	.reset_each_pass = true,
};

static int arg_set_gres(slurm_opt_t *opt, const char *arg)
{
	if (!xstrcasecmp(arg, "help") || !xstrcasecmp(arg, "list")) {
		if (opt->scron_opt)
			return SLURM_ERROR;
		print_gres_help();
		exit(0);
	}

	xfree(opt->gres);
	/*
	 * Do not prepend "gres:" to none; none is handled specially by
	 * slurmctld to mean "do not copy the job's GRES to the step" -
	 * see _copy_job_tres_to_step()
	 */
	if (!xstrcasecmp(arg, "none"))
		opt->gres = xstrdup(arg);
	else
		opt->gres = gres_prepend_tres_type(arg);

	return SLURM_SUCCESS;
}
static int arg_set_data_gres(slurm_opt_t *opt, const data_t *arg,
			     data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if (!xstrcasecmp(str, "help") || !xstrcasecmp(str, "list")) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("GRES \"help\" not supported", rc);
	} else {
		xfree(opt->gres);
		/*
		 * Do not prepend "gres:" to none; none is handled specially by
		 * slurmctld to mean "do not copy the job's GRES to the step" -
		 * see _copy_job_tres_to_step()
		 */
		if (!xstrcasecmp(str, "none")) {
			opt->gres = str;
			str = NULL;
		} else
			opt->gres = gres_prepend_tres_type(str);
	}

	xfree(str);
	return rc;
}
COMMON_STRING_OPTION_GET_AND_RESET(gres);
static slurm_cli_opt_t slurm_opt_gres = {
	.name = "gres",
	.has_arg = required_argument,
	.val = LONG_OPT_GRES,
	.set_func = arg_set_gres,
	.set_func_data = arg_set_data_gres,
	.get_func = arg_get_gres,
	.reset_func = arg_reset_gres,
	.reset_each_pass = true,
};

static int arg_set_gres_flags(slurm_opt_t *opt, const char *arg)
{
	/* clear both flag options first */
	opt->job_flags &= ~(GRES_DISABLE_BIND|GRES_ENFORCE_BIND);
	if (!xstrcasecmp(arg, "disable-binding")) {
		opt->job_flags |= GRES_DISABLE_BIND;
	} else if (!xstrcasecmp(arg, "enforce-binding")) {
		opt->job_flags |= GRES_ENFORCE_BIND;
	} else {
		error("Invalid --gres-flags specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_gres_flags(slurm_opt_t *opt, const data_t *arg,
				   data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		/* clear both flag options first */
		opt->job_flags &= ~(GRES_DISABLE_BIND|GRES_ENFORCE_BIND);
		if (!xstrcasecmp(str, "disable-binding")) {
			opt->job_flags |= GRES_DISABLE_BIND;
		} else if (!xstrcasecmp(str, "enforce-binding")) {
			opt->job_flags |= GRES_ENFORCE_BIND;
		} else {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid GRES flags", rc);
		}
	}

	xfree(str);
	return rc;
}
static char *arg_get_gres_flags(slurm_opt_t *opt)
{
	if (opt->job_flags & GRES_DISABLE_BIND)
		return xstrdup("disable-binding");
	else if (opt->job_flags & GRES_ENFORCE_BIND)
		return xstrdup("enforce-binding");
	return xstrdup("unset");
}
static void arg_reset_gres_flags(slurm_opt_t *opt)
{
	opt->job_flags &= ~(GRES_DISABLE_BIND);
	opt->job_flags &= ~(GRES_ENFORCE_BIND);
}
static slurm_cli_opt_t slurm_opt_gres_flags = {
	.name = "gres-flags",
	.has_arg = required_argument,
	.val = LONG_OPT_GRES_FLAGS,
	.set_func = arg_set_gres_flags,
	.set_func_data = arg_set_data_gres_flags,
	.get_func = arg_get_gres_flags,
	.reset_func = arg_reset_gres_flags,
	.reset_each_pass = true,
};

static int arg_set_help(slurm_opt_t *opt, const char *arg)
{
	if (opt->scron_opt)
		return SLURM_ERROR;

	if (opt->help_func)
		(opt->help_func)();
	else
		error("Could not find --help message");

	exit(0);
	return SLURM_SUCCESS;
}
static char *arg_get_help(slurm_opt_t *opt)
{
	return NULL; /* no op */
}
static void arg_reset_help(slurm_opt_t *opt)
{
	/* no op */
}
static slurm_cli_opt_t slurm_opt_help = {
	.name = "help",
	.has_arg = no_argument,
	.val = 'h',
	.sbatch_early_pass = true,
	.set_func = arg_set_help,
	.get_func = arg_get_help,
	.reset_func = arg_reset_help,
};

COMMON_STRING_OPTION(hint);
static slurm_cli_opt_t slurm_opt_hint = {
	.name = "hint",
	.has_arg = required_argument,
	.val = LONG_OPT_HINT,
	.set_func = arg_set_hint,
	.set_func_data = arg_set_data_hint,
	.get_func = arg_get_hint,
	.reset_func = arg_reset_hint,
	.reset_each_pass = true,
};

COMMON_BOOL_OPTION(hold, "hold");
static slurm_cli_opt_t slurm_opt_hold = {
	.name = "hold",
	.has_arg = no_argument,
	.val = 'H',
	.set_func_salloc = arg_set_hold,
	.set_func_sbatch = arg_set_hold,
	.set_func_srun = arg_set_hold,
	.set_func_data = arg_set_data_hold,
	.get_func = arg_get_hold,
	.reset_func = arg_reset_hold,
};

static int arg_set_ignore_pbs(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt)
		return SLURM_ERROR;

	opt->sbatch_opt->ignore_pbs = true;

	return SLURM_SUCCESS;
}
static char *arg_get_ignore_pbs(slurm_opt_t *opt)
{
	if (!opt->sbatch_opt)
		return xstrdup("invalid-context");

	return xstrdup(opt->sbatch_opt->ignore_pbs ? "set" : "unset");
}
static void arg_reset_ignore_pbs(slurm_opt_t *opt)
{
	if (opt->sbatch_opt)
		opt->sbatch_opt->ignore_pbs = false;
}
static slurm_cli_opt_t slurm_opt_ignore_pbs = {
	.name = "ignore-pbs",
	.has_arg = no_argument,
	.val = LONG_OPT_IGNORE_PBS,
	.set_func_sbatch = arg_set_ignore_pbs,
	.get_func = arg_get_ignore_pbs,
	.reset_func = arg_reset_ignore_pbs,
};

static int arg_set_immediate(slurm_opt_t *opt, const char *arg)
{
	if (opt->sbatch_opt)
		return SLURM_ERROR;

	if (arg)
		opt->immediate = parse_int("immediate", arg, false);
	else
		opt->immediate = DEFAULT_IMMEDIATE;

	return SLURM_SUCCESS;
}
COMMON_INT_OPTION_GET_AND_RESET(immediate);
static slurm_cli_opt_t slurm_opt_immediate = {
	.name = "immediate",
	.has_arg = optional_argument,
	.val = 'I',
	.set_func_salloc = arg_set_immediate,
	.set_func_srun = arg_set_immediate,
	.get_func = arg_get_immediate,
	.reset_func = arg_reset_immediate,
};

static int arg_set_ifname(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt && !opt->srun_opt)
		return SLURM_ERROR;

	xfree(opt->ifname);
	if (!xstrcasecmp(arg, "none"))
		opt->ifname = xstrdup("/dev/null");
	else
		opt->ifname = xstrdup(arg);

	return SLURM_SUCCESS;
}
static int arg_set_data_ifname(slurm_opt_t *opt, const data_t *arg,
			       data_t *errors)
{
	int rc;
	char *str = NULL;

	if (!opt->sbatch_opt && !opt->scron_opt && !opt->srun_opt)
		return SLURM_ERROR;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		xfree(opt->ifname);
		if (!xstrcasecmp(str, "none"))
			opt->ifname = xstrdup("/dev/null");
		else {
			opt->ifname = str;
			str = NULL;
		}
	}

	xfree(str);
	return rc;
}
COMMON_STRING_OPTION_GET(ifname);
COMMON_STRING_OPTION_RESET(ifname);
static slurm_cli_opt_t slurm_opt_input = {
	.name = "input",
	.has_arg = required_argument,
	.val = 'i',
	.set_func_sbatch = arg_set_ifname,
	.set_func_scron = arg_set_ifname,
	.set_func_srun = arg_set_ifname,
	.set_func_data = arg_set_data_ifname,
	.get_func = arg_get_ifname,
	.reset_func = arg_reset_ifname,
};

COMMON_SRUN_BOOL_OPTION(interactive);
static slurm_cli_opt_t slurm_opt_interactive = {
	.name = "interactive",
	.has_arg = no_argument,
	.val = LONG_OPT_INTERACTIVE,
	.set_func_srun = arg_set_interactive,
	.get_func = arg_get_interactive,
	.reset_func = arg_reset_interactive,
};

static int arg_set_jobid(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	opt->srun_opt->jobid = parse_int("--jobid", arg, true);

	return SLURM_SUCCESS;
}
static char *arg_get_jobid(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return NULL;

	if (opt->srun_opt->jobid == NO_VAL)
		return xstrdup("unset");

	return xstrdup_printf("%d", opt->srun_opt->jobid);
}
static void arg_reset_jobid(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->jobid = NO_VAL;
}
static slurm_cli_opt_t slurm_opt_jobid = {
	.name = "jobid",
	.has_arg = required_argument,
	.val = LONG_OPT_JOBID,
	.set_func_srun = arg_set_jobid,
	.get_func = arg_get_jobid,
	.reset_func = arg_reset_jobid,
};

COMMON_STRING_OPTION(job_name);
static slurm_cli_opt_t slurm_opt_job_name = {
	.name = "job-name",
	.has_arg = required_argument,
	.val = 'J',
	.set_func = arg_set_job_name,
	.set_func_data = arg_set_data_job_name,
	.get_func = arg_get_job_name,
	.reset_func = arg_reset_job_name,
};

static int arg_set_kill_command(slurm_opt_t *opt, const char *arg)
{
	if (!opt->salloc_opt)
		return SLURM_ERROR;

	/* Optional argument, enables default of SIGTERM if not given. */
	if (!arg) {
		opt->salloc_opt->kill_command_signal = SIGTERM;
		return SLURM_SUCCESS;
	}

	if (!(opt->salloc_opt->kill_command_signal = sig_name2num(arg))) {
		error("Invalid --kill-command specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static char *arg_get_kill_command(slurm_opt_t *opt)
{
	if (!opt->salloc_opt)
		return NULL;

	return sig_num2name(opt->salloc_opt->kill_command_signal);
}
static void arg_reset_kill_command(slurm_opt_t *opt)
{
	if (opt->salloc_opt)
		opt->salloc_opt->kill_command_signal = 0;
}
static slurm_cli_opt_t slurm_opt_kill_command = {
	.name = "kill-command",
	.has_arg = optional_argument,
	.val = 'K',
	.set_func_salloc = arg_set_kill_command,
	.get_func = arg_get_kill_command,
	.reset_func = arg_reset_kill_command,
};

static int arg_set_kill_on_bad_exit(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	if (!arg) {
		opt->srun_opt->kill_bad_exit = 1;
		return SLURM_SUCCESS;
	}

	opt->srun_opt->kill_bad_exit = parse_int("--kill-on-bad-exit",
						 arg, false);

	return SLURM_SUCCESS;
}
static char *arg_get_kill_on_bad_exit(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return NULL;

	return xstrdup_printf("%d", opt->srun_opt->kill_bad_exit);
}
static void arg_reset_kill_on_bad_exit(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->kill_bad_exit = NO_VAL;
}
static slurm_cli_opt_t slurm_opt_kill_on_bad_exit = {
	.name = "kill-on-bad-exit",
	.has_arg = optional_argument,
	.val = 'K',
	.set_func_srun = arg_set_kill_on_bad_exit,
	.get_func = arg_get_kill_on_bad_exit,
	.reset_func = arg_reset_kill_on_bad_exit,
};

static int arg_set_kill_on_invalid_dep(slurm_opt_t *opt, const char *arg)
{
	if (!xstrcasecmp(arg, "yes"))
		opt->job_flags |= KILL_INV_DEP;
	else if (!xstrcasecmp(arg, "no"))
		opt->job_flags |= NO_KILL_INV_DEP;
	else {
		error("Invalid --kill-on-invalid-dep specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_kill_on_invalid_dep(slurm_opt_t *opt, const data_t *arg,
					    data_t *errors)
{
	int rc;
	bool kill;

	if ((rc = data_copy_bool_converted(arg, &kill)))
		ADD_DATA_ERROR("Unable to read boolean", rc);
	else if (kill)
		opt->job_flags |= KILL_INV_DEP;
	else
		opt->job_flags |= NO_KILL_INV_DEP;

	return rc;
}
static char *arg_get_kill_on_invalid_dep(slurm_opt_t *opt)
{
	if (opt->job_flags & KILL_INV_DEP)
		return xstrdup("yes");
	else if (opt->job_flags & NO_KILL_INV_DEP)
		return xstrdup("no");
	return xstrdup("unset");
}
static void arg_reset_kill_on_invalid_dep(slurm_opt_t *opt)
{
	opt->job_flags &= ~KILL_INV_DEP;
	opt->job_flags &= ~NO_KILL_INV_DEP;
}
static slurm_cli_opt_t slurm_opt_kill_on_invalid_dep = {
	.name = "kill-on-invalid-dep",
	.has_arg = required_argument,
	.val = LONG_OPT_KILL_INV_DEP,
	.set_func_sbatch = arg_set_kill_on_invalid_dep,
	.set_func_data = arg_set_data_kill_on_invalid_dep,
	.get_func = arg_get_kill_on_invalid_dep,
	.reset_func = arg_reset_kill_on_invalid_dep,
};

COMMON_SRUN_BOOL_OPTION(labelio);
static slurm_cli_opt_t slurm_opt_label = {
	.name = "label",
	.has_arg = no_argument,
	.val = 'l',
	.set_func_srun = arg_set_labelio,
	.get_func = arg_get_labelio,
	.reset_func = arg_reset_labelio,
};

COMMON_STRING_OPTION(licenses);
static slurm_cli_opt_t slurm_opt_licenses = {
	.name = "licenses",
	.has_arg = required_argument,
	.val = 'L',
	.set_func = arg_set_licenses,
	.set_func_data = arg_set_data_licenses,
	.get_func = arg_get_licenses,
	.reset_func = arg_reset_licenses,
	.reset_each_pass = true,
};

static int arg_set_mail_type(slurm_opt_t *opt, const char *arg)
{
	opt->mail_type |= parse_mail_type(arg);
	if (opt->mail_type == INFINITE16) {
		error("Invalid --mail-type specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_mail_type(slurm_opt_t *opt, const data_t *arg,
				  data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if ((opt->mail_type |= parse_mail_type(str)) == INFINITE16) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Invalid mail type specification", rc);
	}

	xfree(str);
	return rc;
}
static char *arg_get_mail_type(slurm_opt_t *opt)
{
	return xstrdup(print_mail_type(opt->mail_type));
}
COMMON_OPTION_RESET(mail_type, 0);
static slurm_cli_opt_t slurm_opt_mail_type = {
	.name = "mail-type",
	.has_arg = required_argument,
	.val = LONG_OPT_MAIL_TYPE,
	.set_func = arg_set_mail_type,
	.set_func_data = arg_set_data_mail_type,
	.get_func = arg_get_mail_type,
	.reset_func = arg_reset_mail_type,
	.reset_each_pass = true,
};

COMMON_STRING_OPTION(mail_user);
static slurm_cli_opt_t slurm_opt_mail_user = {
	.name = "mail-user",
	.has_arg = required_argument,
	.val = LONG_OPT_MAIL_USER,
	.set_func = arg_set_mail_user,
	.set_func_data = arg_set_data_mail_user,
	.get_func = arg_get_mail_user,
	.reset_func = arg_reset_mail_user,
	.reset_each_pass = true,
};

static int arg_set_max_threads(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	opt->srun_opt->max_threads = parse_int("--threads", arg, true);

	if (opt->srun_opt->max_threads > SRUN_MAX_THREADS)
		error("Thread value --threads=%d exceeds recommended limit of %d",
		      opt->srun_opt->max_threads, SRUN_MAX_THREADS);

	return SLURM_SUCCESS;
}
static char *arg_get_max_threads(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	return xstrdup_printf("%d", opt->srun_opt->max_threads);
}
static void arg_reset_max_threads(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->max_threads = SRUN_MAX_THREADS;
}
static slurm_cli_opt_t slurm_opt_max_threads = {
	.name = "threads",
	.has_arg = required_argument,
	.val = 'T',
	.set_func_srun = arg_set_max_threads,
	.get_func = arg_get_max_threads,
	.reset_func = arg_reset_max_threads,
};

COMMON_STRING_OPTION(mcs_label);
static slurm_cli_opt_t slurm_opt_mcs_label = {
	.name = "mcs-label",
	.has_arg = required_argument,
	.val = LONG_OPT_MCS_LABEL,
	.set_func = arg_set_mcs_label,
	.set_func_data = arg_set_data_mcs_label,
	.get_func = arg_get_mcs_label,
	.reset_func = arg_reset_mcs_label,
};

static int arg_set_mem(slurm_opt_t *opt, const char *arg)
{
	if ((opt->pn_min_memory = str_to_mbytes(arg)) == NO_VAL64) {
		error("Invalid --mem specification");
		return SLURM_ERROR;
	}

	/*
	 * FIXME: the srun command silently stomps on any --mem-per-cpu
	 * setting, as it was likely inherited from the env var.
	 */
	if (opt->srun_opt)
		opt->mem_per_cpu = NO_VAL64;

	return SLURM_SUCCESS;
}
static int arg_set_data_mem(slurm_opt_t *opt, const data_t *arg, data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if ((opt->pn_min_memory = str_to_mbytes(str)) == NO_VAL64) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Invalid memory specification", rc);
	}

	xfree(str);
	return rc;
}
COMMON_MBYTES_OPTION_GET_AND_RESET(pn_min_memory);
static slurm_cli_opt_t slurm_opt_mem = {
	.name = "mem",
	.has_arg = required_argument,
	.val = LONG_OPT_MEM,
	.set_func = arg_set_mem,
	.set_func_data = arg_set_data_mem,
	.get_func = arg_get_pn_min_memory,
	.reset_func = arg_reset_pn_min_memory,
};

static int arg_set_mem_bind(slurm_opt_t *opt, const char *arg)
{
	xfree(opt->mem_bind);
	if (slurm_verify_mem_bind(arg, &opt->mem_bind, &opt->mem_bind_type))
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}
static int arg_set_data_mem_bind(slurm_opt_t *opt, const data_t *arg,
				 data_t *errors)
{
	int rc;
	char *str = NULL;

	xfree(opt->mem_bind);

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if (xstrcasestr(str, "help")) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("memory binding help not supported", rc);
	} else if ((rc = slurm_verify_mem_bind(str, &opt->mem_bind,
					       &opt->mem_bind_type)))
		ADD_DATA_ERROR("Invalid memory binding specification", rc);

	xfree(str);
	return rc;
}
static char *arg_get_mem_bind(slurm_opt_t *opt)
{
	char *tmp;
	if (!opt->mem_bind_type)
		return xstrdup("unset");
	tmp = slurm_xstr_mem_bind_type(opt->mem_bind_type);
	if (opt->mem_bind)
		xstrfmtcat(tmp, ":%s", opt->mem_bind);
	return tmp;
}
static void arg_reset_mem_bind(slurm_opt_t *opt)
{
	xfree(opt->mem_bind);
	opt->mem_bind_type = 0;

	if (opt->srun_opt) {
		if (xstrstr(slurm_conf.launch_params, "mem_sort"))
			opt->mem_bind_type |= MEM_BIND_SORT;
	}
}
static slurm_cli_opt_t slurm_opt_mem_bind = {
	.name = "mem-bind",
	.has_arg = required_argument,
	.val = LONG_OPT_MEM_BIND,
	.set_func = arg_set_mem_bind,
	.set_func_data = arg_set_data_mem_bind,
	.get_func = arg_get_mem_bind,
	.reset_func = arg_reset_mem_bind,
	.reset_each_pass = true,
};

COMMON_MBYTES_OPTION(mem_per_cpu, --mem-per-cpu);
static slurm_cli_opt_t slurm_opt_mem_per_cpu = {
	.name = "mem-per-cpu",
	.has_arg = required_argument,
	.val = LONG_OPT_MEM_PER_CPU,
	.set_func = arg_set_mem_per_cpu,
	.set_func_data = arg_set_data_mem_per_cpu,
	.get_func = arg_get_mem_per_cpu,
	.reset_func = arg_reset_mem_per_cpu,
	.reset_each_pass = true,
};

COMMON_MBYTES_OPTION(mem_per_gpu, --mem-per-gpu);
static slurm_cli_opt_t slurm_opt_mem_per_gpu = {
	.name = "mem-per-gpu",
	.has_arg = required_argument,
	.val = LONG_OPT_MEM_PER_GPU,
	.set_func = arg_set_mem_per_gpu,
	.set_func_data = arg_set_data_mem_per_gpu,
	.get_func = arg_get_mem_per_gpu,
	.reset_func = arg_reset_mem_per_gpu,
	.reset_each_pass = true,
};

COMMON_INT_OPTION_SET(pn_min_cpus, "--mincpus");
COMMON_INT_OPTION_SET_DATA(pn_min_cpus);
COMMON_INT_OPTION_GET(pn_min_cpus);
COMMON_OPTION_RESET(pn_min_cpus, -1);
static slurm_cli_opt_t slurm_opt_mincpus = {
	.name = "mincpus",
	.has_arg = required_argument,
	.val = LONG_OPT_MINCPUS,
	.set_func = arg_set_pn_min_cpus,
	.set_func_data = arg_set_data_pn_min_cpus,
	.get_func = arg_get_pn_min_cpus,
	.reset_func = arg_reset_pn_min_cpus,
	.reset_each_pass = true,
};

COMMON_SRUN_STRING_OPTION(mpi_type);
static slurm_cli_opt_t slurm_opt_mpi = {
	.name = "mpi",
	.has_arg = required_argument,
	.val = LONG_OPT_MPI,
	.set_func_srun = arg_set_mpi_type,
	.get_func = arg_get_mpi_type,
	.reset_func = arg_reset_mpi_type,
};

static int arg_set_msg_timeout(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	opt->srun_opt->msg_timeout = parse_int("--msg-timeout", arg, true);

	return SLURM_SUCCESS;
}
static char *arg_get_msg_timeout(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	return xstrdup_printf("%d", opt->srun_opt->msg_timeout);
}
static void arg_reset_msg_timeout(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->msg_timeout = slurm_conf.msg_timeout;
}
static slurm_cli_opt_t slurm_opt_msg_timeout = {
	.name = "msg-timeout",
	.has_arg = required_argument,
	.val = LONG_OPT_MSG_TIMEOUT,
	.set_func_srun = arg_set_msg_timeout,
	.get_func = arg_get_msg_timeout,
	.reset_func = arg_reset_msg_timeout,
};

COMMON_SRUN_BOOL_OPTION(multi_prog);
static slurm_cli_opt_t slurm_opt_multi_prog = {
	.name = "multi-prog",
	.has_arg = no_argument,
	.val = LONG_OPT_MULTI,
	.set_func_srun = arg_set_multi_prog,
	.get_func = arg_get_multi_prog,
	.reset_func = arg_reset_multi_prog,
};

COMMON_STRING_OPTION(network);
static slurm_cli_opt_t slurm_opt_network = {
	.name = "network",
	.has_arg = required_argument,
	.val = LONG_OPT_NETWORK,
	.set_func = arg_set_network,
	.set_func_data = arg_set_data_network,
	.get_func = arg_get_network,
	.reset_func = arg_reset_network,
	.reset_each_pass = true,
};

static int arg_set_nice(slurm_opt_t *opt, const char *arg)
{
	long long tmp_nice;

	if (arg)
		tmp_nice = strtoll(arg, NULL, 10);
	else
		tmp_nice = 100;

	if (llabs(tmp_nice) > (NICE_OFFSET - 3)) {
		error("Invalid --nice value, out of range (+/- %u)",
		      NICE_OFFSET - 3);
		return SLURM_ERROR;
	}

	opt->nice = (int) tmp_nice;

	return SLURM_SUCCESS;
}
static int arg_set_data_nice(slurm_opt_t *opt, const data_t *arg,
			     data_t *errors)
{
	int64_t val;
	int rc = SLURM_SUCCESS;

	if (data_get_type(arg) == DATA_TYPE_NULL)
		opt->nice = 100;
	else if ((rc = data_get_int_converted(arg, &val)))
		ADD_DATA_ERROR("Unable to read integer value", rc);
	else if (llabs(val) >= (NICE_OFFSET - 3)) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Nice too large", rc);
	} else
		opt->nice = (int) val;
	return rc;
}
static char *arg_get_nice(slurm_opt_t *opt)
{
	return xstrdup_printf("%d", opt->nice);
}
COMMON_OPTION_RESET(nice, NO_VAL);
static slurm_cli_opt_t slurm_opt_nice = {
	.name = "nice",
	.has_arg = optional_argument,
	.val = LONG_OPT_NICE,
	.set_func = arg_set_nice,
	.set_func_data = arg_set_data_nice,
	.get_func = arg_get_nice,
	.reset_func = arg_reset_nice,
};

COMMON_SRUN_BOOL_OPTION(no_alloc);
static slurm_cli_opt_t slurm_opt_no_allocate = {
	.name = "no-allocate",
	.has_arg = no_argument,
	.val = 'Z',
	.set_func_srun = arg_set_no_alloc,
	.get_func = arg_get_no_alloc,
	.reset_func = arg_reset_no_alloc,
};

/* See --bell above as well */
static int arg_set_no_bell(slurm_opt_t *opt, const char *arg)
{
	if (opt->salloc_opt)
		opt->salloc_opt->bell = BELL_NEVER;

	return SLURM_SUCCESS;
}
static slurm_cli_opt_t slurm_opt_no_bell = {
	.name = "no-bell",
	.has_arg = no_argument,
	.val = LONG_OPT_NO_BELL,
	.set_func_salloc = arg_set_no_bell,
	.get_func = arg_get_bell,
	.reset_func = arg_reset_bell,
};

static int arg_set_no_kill(slurm_opt_t *opt, const char *arg)
{
	if (!arg || !xstrcasecmp(arg, "set"))
		opt->no_kill = true;
	else if (!xstrcasecmp(arg, "off") || !xstrcasecmp(arg, "no"))
		opt->no_kill = false;
	else {
		error("Invalid --no-kill specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_no_kill(slurm_opt_t *opt, const data_t *arg,
			       data_t *errors)
{
	int rc = SLURM_SUCCESS;
	char *str = NULL;

	if (data_get_type(arg) == DATA_TYPE_NULL)
		opt->no_kill = true;
	else if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if (!xstrcasecmp(str, "set"))
		opt->no_kill = true;
	else if (!xstrcasecmp(str, "off") || !xstrcasecmp(str, "no"))
		opt->no_kill = false;
	else {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Invalid no kill specification", rc);
	}
	xfree(str);
	return rc;
}
static char *arg_get_no_kill(slurm_opt_t *opt)
{
	return xstrdup(opt->no_kill ? "set" : "unset");
}
COMMON_OPTION_RESET(no_kill, false);
static slurm_cli_opt_t slurm_opt_no_kill = {
	.name = "no-kill",
	.has_arg = optional_argument,
	.val = 'k',
	.set_func = arg_set_no_kill,
	.set_func_data = arg_set_data_no_kill,
	.get_func = arg_get_no_kill,
	.reset_func = arg_reset_no_kill,
};

/* see --requeue below as well */
static int arg_set_no_requeue(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt)
		return SLURM_ERROR;

	opt->sbatch_opt->requeue = 0;

	return SLURM_SUCCESS;
}
static int arg_set_data_no_requeue(slurm_opt_t *opt, const data_t *arg,
				   data_t *errors)
{
	if (!opt->sbatch_opt)
		return SLURM_ERROR;

	opt->sbatch_opt->requeue = 0;

	return SLURM_SUCCESS;
}
static char *arg_get_requeue(slurm_opt_t *opt)
{
	if (!opt->sbatch_opt)
		return xstrdup("invalid-context");

	if (opt->sbatch_opt->requeue == NO_VAL)
		return xstrdup("unset");
	else if (opt->sbatch_opt->requeue == 0)
		return xstrdup("no-requeue");
	return xstrdup("requeue");
}
static void arg_reset_requeue(slurm_opt_t *opt)
{
	if (opt->sbatch_opt)
		opt->sbatch_opt->requeue = NO_VAL;
}
static slurm_cli_opt_t slurm_opt_no_requeue = {
	.name = "no-requeue",
	.has_arg = no_argument,
	.val = LONG_OPT_NO_REQUEUE,
	.set_func_sbatch = arg_set_no_requeue,
	.set_func_data = arg_set_data_no_requeue,
	.get_func = arg_get_requeue,
	.reset_func = arg_reset_requeue,
};

static int arg_set_no_shell(slurm_opt_t *opt, const char *arg)
{
	if (opt->salloc_opt)
		opt->salloc_opt->no_shell = true;

	return SLURM_SUCCESS;
}
static char *arg_get_no_shell(slurm_opt_t *opt)
{
	if (!opt->salloc_opt)
		return xstrdup("invalid-context");

	return xstrdup(opt->salloc_opt->no_shell ? "set" : "unset");
}
static void arg_reset_no_shell(slurm_opt_t *opt)
{
	if (opt->salloc_opt)
		opt->salloc_opt->no_shell = false;
}
static slurm_cli_opt_t slurm_opt_no_shell = {
	.name = "no-shell",
	.has_arg = no_argument,
	.val = LONG_OPT_NO_SHELL,
	.set_func_salloc = arg_set_no_shell,
	.get_func = arg_get_no_shell,
	.reset_func = arg_reset_no_shell,
};

/*
 * FIXME: --nodefile and --nodelist options should be mutually exclusive.
 * Right now they'll overwrite one another; the last to run wins.
 */
static int arg_set_nodefile(slurm_opt_t *opt, const char *arg)
{
	xfree(opt->nodefile);
	xfree(opt->nodelist);
	opt->nodefile = xstrdup(arg);

	return SLURM_SUCCESS;
}
COMMON_STRING_OPTION_GET_AND_RESET(nodefile);
static slurm_cli_opt_t slurm_opt_nodefile = {
	.name = "nodefile",
	.has_arg = required_argument,
	.val = 'F',
	.set_func = arg_set_nodefile,
	.set_func_data = NULL, /* avoid security issues of reading user files */
	.get_func = arg_get_nodefile,
	.reset_func = arg_reset_nodefile,
	.reset_each_pass = true,
};

static int arg_set_nodelist(slurm_opt_t *opt, const char *arg)
{
	xfree(opt->nodefile);
	xfree(opt->nodelist);
	opt->nodelist = xstrdup(arg);

	return SLURM_SUCCESS;
}
static int arg_set_data_nodelist(slurm_opt_t *opt, const data_t *arg,
				 data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		xfree(opt->nodefile);
		xfree(opt->nodelist);
		opt->nodelist = str;
		str = NULL;
	}

	xfree(str);
	return rc;
}
COMMON_STRING_OPTION_GET_AND_RESET(nodelist);
static slurm_cli_opt_t slurm_opt_nodelist = {
	.name = "nodelist",
	.has_arg = required_argument,
	.val = 'w',
	.set_func = arg_set_nodelist,
	.set_func_data = arg_set_data_nodelist,
	.get_func = arg_get_nodelist,
	.reset_func = arg_reset_nodelist,
	.reset_each_pass = true,
};

static int arg_set_nodes(slurm_opt_t *opt, const char *arg)
{
	if (!(opt->nodes_set = verify_node_count(arg, &opt->min_nodes,
					   &opt->max_nodes)))
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

typedef struct {
	int min;
	int max;
	data_t *errors;
} node_cnt_t;

static data_for_each_cmd_t _parse_nodes_counts(const data_t *data, void *arg)
{
	node_cnt_t *nodes = arg;
	data_t *errors = nodes->errors;
	int64_t val;
	int rc;

	if ((rc = data_get_int_converted(data, &val))) {
		ADD_DATA_ERROR("Invalid node count", rc);
		return DATA_FOR_EACH_FAIL;
	}

	nodes->min = nodes->max;
	nodes->max = (int) val;

	return DATA_FOR_EACH_CONT;
}

static int arg_set_data_nodes(slurm_opt_t *opt, const data_t *arg,
			       data_t *errors)
{
	int rc = SLURM_SUCCESS;
	char *str = NULL;

	if (data_get_type(arg) == DATA_TYPE_LIST) {
		node_cnt_t counts =
			{ .min = NO_VAL, .max = NO_VAL, .errors = errors };

		if (data_get_list_length(arg) != 2) {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid node count list size", rc);
		} else if (data_list_for_each_const(arg, _parse_nodes_counts,
						    &counts) < 0) {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid node count specification", rc);
		} else {
			opt->min_nodes = counts.min;
			opt->max_nodes = counts.max;
		}
	} else if ((rc = data_get_string_converted(arg, &str))) {
		ADD_DATA_ERROR("Unable to read string", rc);
	} else if (!(opt->nodes_set = verify_node_count(str, &opt->min_nodes,
						      &opt->max_nodes))) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Invalid node count string", rc);
	}

	xfree(str);
	return rc;
}
static char *arg_get_nodes(slurm_opt_t *opt)
{
	if (opt->min_nodes != opt->max_nodes)
		return xstrdup_printf("%d-%d", opt->min_nodes, opt->max_nodes);
	return xstrdup_printf("%d", opt->min_nodes);
}
static void arg_reset_nodes(slurm_opt_t *opt)
{
	opt->min_nodes = 1;
	opt->max_nodes = 0;
	opt->nodes_set = false;
}
static slurm_cli_opt_t slurm_opt_nodes = {
	.name = "nodes",
	.has_arg = required_argument,
	.val = 'N',
	.set_func = arg_set_nodes,
	.set_func_data = arg_set_data_nodes,
	.get_func = arg_get_nodes,
	.reset_func = arg_reset_nodes,
	.reset_each_pass = true,
};

static int arg_set_ntasks(slurm_opt_t *opt, const char *arg)
{
	opt->ntasks = parse_int("--ntasks", arg, true);
	opt->ntasks_set = true;
	return SLURM_SUCCESS;
}
static int arg_set_data_ntasks(slurm_opt_t *opt, const data_t *arg,
			       data_t *errors)
{
	int64_t val;
	int rc = data_get_int_converted(arg, &val);
	if (rc)
		ADD_DATA_ERROR("Unable to read integer value", rc);
	else if (val >= INT_MAX) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("ntasks too large", rc);
	} else if (val <= 0) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("ntasks too small", rc);
	} else {
		opt->ntasks = (int) val;
		opt->ntasks_set = true;
	}
	return rc;
}
COMMON_INT_OPTION_GET(ntasks);
static void arg_reset_ntasks(slurm_opt_t *opt)
{
	opt->ntasks = 1;
	opt->ntasks_set = false;
}
static slurm_cli_opt_t slurm_opt_ntasks = {
	.name = "ntasks",
	.has_arg = required_argument,
	.val = 'n',
	.set_func = arg_set_ntasks,
	.set_func_data = arg_set_data_ntasks,
	.get_func = arg_get_ntasks,
	.reset_func = arg_reset_ntasks,
	.reset_each_pass = true,
};

COMMON_INT_OPTION_SET(ntasks_per_core, "--ntasks-per-core");
COMMON_INT_OPTION_SET_DATA(ntasks_per_core);
COMMON_INT_OPTION_GET(ntasks_per_core);
COMMON_OPTION_RESET(ntasks_per_core, NO_VAL);
static slurm_cli_opt_t slurm_opt_ntasks_per_core = {
	.name = "ntasks-per-core",
	.has_arg = required_argument,
	.val = LONG_OPT_NTASKSPERCORE,
	.set_func = arg_set_ntasks_per_core,
	.set_func_data = arg_set_data_ntasks_per_core,
	.get_func = arg_get_ntasks_per_core,
	.reset_func = arg_reset_ntasks_per_core,
	.reset_each_pass = true,
};

COMMON_INT_OPTION_SET(ntasks_per_node, "--ntasks-per-node");
COMMON_INT_OPTION_SET_DATA(ntasks_per_node);
COMMON_INT_OPTION_GET(ntasks_per_node);
COMMON_OPTION_RESET(ntasks_per_node, NO_VAL);
static slurm_cli_opt_t slurm_opt_ntasks_per_node = {
	.name = "ntasks-per-node",
	.has_arg = required_argument,
	.val = LONG_OPT_NTASKSPERNODE,
	.set_func = arg_set_ntasks_per_node,
	.set_func_data = arg_set_data_ntasks_per_node,
	.get_func = arg_get_ntasks_per_node,
	.reset_func = arg_reset_ntasks_per_node,
	.reset_each_pass = true,
};

COMMON_INT_OPTION_SET(ntasks_per_socket, "--ntasks-per-socket");
COMMON_INT_OPTION_SET_DATA(ntasks_per_socket);
COMMON_INT_OPTION_GET(ntasks_per_socket);
COMMON_OPTION_RESET(ntasks_per_socket, NO_VAL);
static slurm_cli_opt_t slurm_opt_ntasks_per_socket = {
	.name = "ntasks-per-socket",
	.has_arg = required_argument,
	.val = LONG_OPT_NTASKSPERSOCKET,
	.set_func = arg_set_ntasks_per_socket,
	.set_func_data = arg_set_data_ntasks_per_socket,
	.get_func = arg_get_ntasks_per_socket,
	.reset_func = arg_reset_ntasks_per_socket,
	.reset_each_pass = true,
};

COMMON_INT_OPTION_SET(ntasks_per_tres, "--ntasks-per-tres");
COMMON_INT_OPTION_SET_DATA(ntasks_per_tres);
COMMON_INT_OPTION_GET(ntasks_per_tres);
COMMON_OPTION_RESET(ntasks_per_tres, NO_VAL);
static slurm_cli_opt_t slurm_opt_ntasks_per_tres = {
	.name = "ntasks-per-tres",
	.has_arg = required_argument,
	.val = LONG_OPT_NTASKSPERTRES,
	.set_func = arg_set_ntasks_per_tres,
	.set_func_data = arg_set_data_ntasks_per_tres,
	.get_func = arg_get_ntasks_per_tres,
	.reset_func = arg_reset_ntasks_per_tres,
	.reset_each_pass = true,
};

COMMON_INT_OPTION_SET(ntasks_per_gpu, "--ntasks-per-gpu");
COMMON_INT_OPTION_SET_DATA(ntasks_per_gpu);
COMMON_INT_OPTION_GET(ntasks_per_gpu);
COMMON_OPTION_RESET(ntasks_per_gpu, NO_VAL);
static slurm_cli_opt_t slurm_opt_ntasks_per_gpu = {
	.name = "ntasks-per-gpu",
	.has_arg = required_argument,
	.val = LONG_OPT_NTASKSPERGPU,
	.set_func = arg_set_ntasks_per_gpu,
	.set_func_data = arg_set_data_ntasks_per_gpu,
	.get_func = arg_get_ntasks_per_gpu,
	.reset_func = arg_reset_ntasks_per_gpu,
	.reset_each_pass = true,
};

static int arg_set_open_mode(slurm_opt_t *opt, const char *arg)
{
	if (arg && (arg[0] == 'a' || arg[0] == 'A'))
		opt->open_mode = OPEN_MODE_APPEND;
	else if (arg && (arg[0] == 't' || arg[0] == 'T'))
		opt->open_mode = OPEN_MODE_TRUNCATE;
	else {
		error("Invalid --open-mode specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_open_mode(slurm_opt_t *opt, const data_t *arg,
				  data_t *errors)
{
	int rc = SLURM_SUCCESS;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		if (str && (str[0] == 'a' || str[0] == 'A'))
			opt->open_mode = OPEN_MODE_APPEND;
		else if (str && (str[0] == 't' || str[0] == 'T'))
			opt->open_mode = OPEN_MODE_TRUNCATE;
		else {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid open mode specification", rc);
		}
	}

	xfree(str);
	return rc;
}
static char *arg_get_open_mode(slurm_opt_t *opt)
{
	if (opt->open_mode == OPEN_MODE_APPEND)
		return xstrdup("a");
	if (opt->open_mode == OPEN_MODE_TRUNCATE)
		return xstrdup("t");

	return NULL;
}
COMMON_OPTION_RESET(open_mode, 0);
static slurm_cli_opt_t slurm_opt_open_mode = {
	.name = "open-mode",
	.has_arg = required_argument,
	.val = LONG_OPT_OPEN_MODE,
	.set_func_sbatch = arg_set_open_mode,
	.set_func_scron = arg_set_open_mode,
	.set_func_srun = arg_set_open_mode,
	.set_func_data = arg_set_data_open_mode,
	.get_func = arg_get_open_mode,
	.reset_func = arg_reset_open_mode,
};

static int arg_set_ofname(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt && !opt->scron_opt && !opt->srun_opt)
		return SLURM_ERROR;

	xfree(opt->ofname);
	if (!xstrcasecmp(arg, "none"))
		opt->ofname = xstrdup("/dev/null");
	else
		opt->ofname = xstrdup(arg);

	return SLURM_SUCCESS;
}
static int arg_set_data_ofname(slurm_opt_t *opt, const data_t *arg,
			       data_t *errors)
{
	int rc;
	char *str = NULL;

	if (!opt->sbatch_opt && !opt->scron_opt && !opt->srun_opt)
		return SLURM_ERROR;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		xfree(opt->ofname);
		if (!xstrcasecmp(str, "none"))
			opt->ofname = xstrdup("/dev/null");
		else {
			opt->ofname = str;
			str = NULL;
		}
	}

	xfree(str);
	return rc;
}
COMMON_STRING_OPTION_GET(ofname);
COMMON_STRING_OPTION_RESET(ofname);
static slurm_cli_opt_t slurm_opt_output = {
	.name = "output",
	.has_arg = required_argument,
	.val = 'o',
	.set_func_sbatch = arg_set_ofname,
	.set_func_scron = arg_set_ofname,
	.set_func_srun = arg_set_ofname,
	.set_func_data = arg_set_data_ofname,
	.get_func = arg_get_ofname,
	.reset_func = arg_reset_ofname,
};

COMMON_BOOL_OPTION(overcommit, "overcommit");
static slurm_cli_opt_t slurm_opt_overcommit = {
	.name = "overcommit",
	.has_arg = no_argument,
	.val = 'O',
	.set_func = arg_set_overcommit,
	.set_func_data = arg_set_data_overcommit,
	.get_func = arg_get_overcommit,
	.reset_func = arg_reset_overcommit,
	.reset_each_pass = true,
};

static int arg_set_overlap(slurm_opt_t *opt, const char *arg)
{
	if (opt->srun_opt)
		opt->srun_opt->exclusive = false;

	return SLURM_SUCCESS;
}
static char *arg_get_overlap(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	return xstrdup(opt->srun_opt->exclusive ? "unset" : "set");
}
static void arg_reset_overlap(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->exclusive = true;
}

static slurm_cli_opt_t slurm_opt_overlap = {
	.name = "overlap",
	.has_arg = no_argument,
	.val = LONG_OPT_OVERLAP,
	.set_func_srun = arg_set_overlap,
	.get_func = arg_get_overlap,
	.reset_func = arg_reset_overlap,
};

/*
 * This option is directly tied to --exclusive. Both use the same output
 * function, and the string arguments are designed to mirror one another.
 */
static int arg_set_oversubscribe(slurm_opt_t *opt, const char *arg)
{
	if (opt->srun_opt)
		opt->srun_opt->exclusive = false;

	opt->shared = JOB_SHARED_OK;

	return SLURM_SUCCESS;
}
static int arg_set_data_oversubscribe(slurm_opt_t *opt, const data_t *arg,
				      data_t *errors)
{
	if (opt->srun_opt)
		opt->srun_opt->exclusive = false;

	opt->shared = JOB_SHARED_OK;

	return SLURM_SUCCESS;
}

static slurm_cli_opt_t slurm_opt_oversubscribe = {
	.name = "oversubscribe",
	.has_arg = no_argument,
	.val = 's',
	.set_func = arg_set_oversubscribe,
	.set_func_data = arg_set_data_oversubscribe,
	.get_func = arg_get_exclusive,
	.reset_func = arg_reset_shared,
	.reset_each_pass = true,
};

static int arg_set_het_group(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	xfree(opt->srun_opt->het_group);
	opt->srun_opt->het_group = xstrdup(arg);

	return SLURM_SUCCESS;
}
static char *arg_get_het_group(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	return xstrdup(opt->srun_opt->het_group);
}
static void arg_reset_het_group(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		xfree(opt->srun_opt->het_group);
}

/* Continue support for pack-group */
static slurm_cli_opt_t slurm_opt_pack_group = {
	.name = "pack-group",
	.has_arg = required_argument,
	.val = LONG_OPT_HET_GROUP,
	.srun_early_pass = true,
	.set_func_srun = arg_set_het_group,
	.get_func = arg_get_het_group,
	.reset_func = arg_reset_het_group,
};

static slurm_cli_opt_t slurm_opt_het_group = {
	.name = "het-group",
	.has_arg = required_argument,
	.val = LONG_OPT_HET_GROUP,
	.srun_early_pass = true,
	.set_func_srun = arg_set_het_group,
	.get_func = arg_get_het_group,
	.reset_func = arg_reset_het_group,
};

static int arg_set_parsable(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt)
		return SLURM_ERROR;

	opt->sbatch_opt->parsable = true;

	return SLURM_SUCCESS;
}
static char *arg_get_parsable(slurm_opt_t *opt)
{
	if (!opt->sbatch_opt)
		return xstrdup("invalid-context");

	return xstrdup(opt->sbatch_opt->parsable ? "set" : "unset");
}
static void arg_reset_parsable(slurm_opt_t *opt)
{
	if (opt->sbatch_opt)
		opt->sbatch_opt->parsable = false;
}
static slurm_cli_opt_t slurm_opt_parsable = {
	.name = "parsable",
	.has_arg = no_argument,
	.val = LONG_OPT_PARSABLE,
	.set_func_sbatch = arg_set_parsable,
	.get_func = arg_get_parsable,
	.reset_func = arg_reset_parsable,
};

COMMON_STRING_OPTION(partition);
static slurm_cli_opt_t slurm_opt_partition = {
	.name = "partition",
	.has_arg = required_argument,
	.val = 'p',
	.set_func = arg_set_partition,
	.set_func_data = arg_set_data_partition,
	.get_func = arg_get_partition,
	.reset_func = arg_reset_partition,
	.reset_each_pass = true,
};

static int arg_set_power(slurm_opt_t *opt, const char *arg)
{
	opt->power = power_flags_id(arg);

	return SLURM_SUCCESS;
}
static int arg_set_data_power(slurm_opt_t *opt, const data_t *arg,
			      data_t *errors)
{
	int rc;
	char *str = NULL;

	if (!opt->sbatch_opt && !opt->srun_opt)
		return SLURM_ERROR;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else
		opt->power = power_flags_id(str);

	xfree(str);
	return rc;
}
static char *arg_get_power(slurm_opt_t *opt)
{
	if (opt->power)
		return xstrdup(power_flags_str(opt->power));
	return xstrdup("unset");
}
COMMON_OPTION_RESET(power, 0);
static slurm_cli_opt_t slurm_opt_power = {
	.name = "power",
	.has_arg = required_argument,
	.val = LONG_OPT_POWER,
	.set_func = arg_set_power,
	.set_func_data = arg_set_data_power,
	.get_func = arg_get_power,
	.reset_func = arg_reset_power,
	.reset_each_pass = true,
};

COMMON_SRUN_BOOL_OPTION(preserve_env);
static slurm_cli_opt_t slurm_opt_preserve_env = {
	.name = "preserve-env",
	.has_arg = no_argument,
	.val = 'E',
	.set_func_srun = arg_set_preserve_env,
	.get_func = arg_get_preserve_env,
	.reset_func = arg_reset_preserve_env,
};

static int arg_set_priority(slurm_opt_t *opt, const char *arg)
{
	if (!xstrcasecmp(arg, "TOP")) {
		opt->priority = NO_VAL - 1;
	} else {
		long long priority = strtoll(arg, NULL, 10);
		if (priority < 0) {
			error("Priority must be >= 0");
			return SLURM_ERROR;
		}
		if (priority >= NO_VAL) {
			error("Priority must be < %u", NO_VAL);
			return SLURM_ERROR;
		}
		opt->priority = priority;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_priority(slurm_opt_t *opt, const data_t *arg,
				 data_t *errors)
{
	int rc;
	int64_t val;
	char *str = NULL;

	if ((rc = data_get_int_converted(arg, &val))) {
		if ((rc = data_get_string_converted(arg, &str)))
			ADD_DATA_ERROR("Unable to read string", rc);
		else if (!xstrcasecmp(str, "TOP"))
			opt->priority = NO_VAL - 1;
		else {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid priority", rc);
		}
	} else if (val >= NO_VAL) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Priority too large", rc);
	} else if (val <= 0) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Priority must be >0", rc);
	} else
		opt->priority = (int) val;

	xfree(str);

	return rc;
}
COMMON_INT_OPTION_GET_AND_RESET(priority);
static slurm_cli_opt_t slurm_opt_priority = {
	.name = "priority",
	.has_arg = required_argument,
	.val = LONG_OPT_PRIORITY,
	.set_func = arg_set_priority,
	.set_func_data = arg_set_data_priority,
	.get_func = arg_get_priority,
	.reset_func = arg_reset_priority,
};

static int arg_set_profile(slurm_opt_t *opt, const char *arg)
{
	opt->profile = acct_gather_profile_from_string(arg);

	if (opt->profile == ACCT_GATHER_PROFILE_NOT_SET) {
		error("invalid --profile=%s option", arg);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_profile(slurm_opt_t *opt, const data_t *arg,
				data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else
		opt->profile = acct_gather_profile_from_string(str);

	xfree(str);
	return rc;
}
static char *arg_get_profile(slurm_opt_t *opt)
{
	return xstrdup(acct_gather_profile_to_string(opt->profile));
}
COMMON_OPTION_RESET(profile, ACCT_GATHER_PROFILE_NOT_SET);
static slurm_cli_opt_t slurm_opt_profile = {
	.name = "profile",
	.has_arg = required_argument,
	.val = LONG_OPT_PROFILE,
	.set_func = arg_set_profile,
	.set_func_data = arg_set_data_profile,
	.get_func = arg_get_profile,
	.reset_func = arg_reset_profile,
};

COMMON_SRUN_STRING_OPTION(prolog);
static slurm_cli_opt_t slurm_opt_prolog = {
	.name = "prolog",
	.has_arg = required_argument,
	.val = LONG_OPT_PROLOG,
	.set_func_srun = arg_set_prolog,
	.get_func = arg_get_prolog,
	.reset_func = arg_reset_prolog,
};

static int arg_set_propagate(slurm_opt_t *opt, const char *arg)
{
	const char *tmp = arg;
	if (!opt->sbatch_opt && !opt->srun_opt)
		return SLURM_ERROR;

	if (!tmp)
		tmp = "ALL";

	if (opt->sbatch_opt)
		opt->sbatch_opt->propagate = xstrdup(tmp);
	if (opt->srun_opt)
		opt->srun_opt->propagate = xstrdup(tmp);

	return SLURM_SUCCESS;
}
static char *arg_get_propagate(slurm_opt_t *opt)
{
	if (!opt->sbatch_opt && !opt->srun_opt)
		return xstrdup("invalid-context");

	if (opt->sbatch_opt)
		return xstrdup(opt->sbatch_opt->propagate);
	if (opt->srun_opt)
		return xstrdup(opt->srun_opt->propagate);

	return NULL;
}
static void arg_reset_propagate(slurm_opt_t *opt)
{
	if (opt->sbatch_opt)
		xfree(opt->sbatch_opt->propagate);
	if (opt->srun_opt)
		xfree(opt->srun_opt->propagate);
}
static slurm_cli_opt_t slurm_opt_propagate = {
	.name = "propagate",
	.has_arg = optional_argument,
	.val = LONG_OPT_PROPAGATE,
	.set_func_sbatch = arg_set_propagate,
	.set_func_srun = arg_set_propagate,
	.get_func = arg_get_propagate,
	.reset_func = arg_reset_propagate,
};

COMMON_SRUN_BOOL_OPTION(pty);
static slurm_cli_opt_t slurm_opt_pty = {
	.name = "pty",
	.has_arg = no_argument,
	.val = LONG_OPT_PTY,
	.set_func_srun = arg_set_pty,
	.get_func = arg_get_pty,
	.reset_func = arg_reset_pty,
};

COMMON_STRING_OPTION(qos);
static slurm_cli_opt_t slurm_opt_qos = {
	.name = "qos",
	.has_arg = required_argument,
	.val = 'q',
	.set_func = arg_set_qos,
	.set_func_data = arg_set_data_qos,
	.get_func = arg_get_qos,
	.reset_func = arg_reset_qos,
};

static int arg_set_quiet(slurm_opt_t *opt, const char *arg)
{
	opt->quiet++;

	return SLURM_SUCCESS;
}
COMMON_INT_OPTION_SET_DATA(quiet);
COMMON_INT_OPTION_GET_AND_RESET(quiet);
static slurm_cli_opt_t slurm_opt_quiet = {
	.name = "quiet",
	.has_arg = no_argument,
	.val = 'Q',
	.sbatch_early_pass = true,
	.set_func = arg_set_quiet,
	.set_func_data = arg_set_data_quiet,
	.get_func = arg_get_quiet,
	.reset_func = arg_reset_quiet,
};

COMMON_SRUN_BOOL_OPTION(quit_on_intr);
static slurm_cli_opt_t slurm_opt_quit_on_interrupt = {
	.name = "quit-on-interrupt",
	.has_arg = no_argument,
	.val = LONG_OPT_QUIT_ON_INTR,
	.set_func_srun = arg_set_quit_on_intr,
	.get_func = arg_get_quit_on_intr,
	.reset_func = arg_reset_quit_on_intr,
};

COMMON_BOOL_OPTION(reboot, "reboot");
static slurm_cli_opt_t slurm_opt_reboot = {
	.name = "reboot",
	.has_arg = no_argument,
	.val = LONG_OPT_REBOOT,
	.set_func = arg_set_reboot,
	.set_func_data = arg_set_data_reboot,
	.get_func = arg_get_reboot,
	.reset_func = arg_reset_reboot,
};

static int arg_set_relative(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	opt->srun_opt->relative = parse_int("--relative", arg, false);

	return SLURM_SUCCESS;
}
static char *arg_get_relative(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	return xstrdup_printf("%d", opt->srun_opt->relative);
}
static void arg_reset_relative(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->relative = NO_VAL;
}
static slurm_cli_opt_t slurm_opt_relative = {
	.name = "relative",
	.has_arg = required_argument,
	.val = 'r',
	.set_func_srun = arg_set_relative,
	.get_func = arg_get_relative,
	.reset_func = arg_reset_relative,
	.reset_each_pass = true,
};

static int arg_set_requeue(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt)
		return SLURM_ERROR;

	opt->sbatch_opt->requeue = 1;

	return SLURM_SUCCESS;
}
static int arg_set_data_requeue(slurm_opt_t *opt, const data_t *arg,
				data_t *errors)
{
	if (!opt->sbatch_opt)
		return SLURM_ERROR;

	opt->sbatch_opt->requeue = 1;

	return SLURM_SUCCESS;
}
/* arg_get_requeue and arg_reset_requeue defined before with --no-requeue */
static slurm_cli_opt_t slurm_opt_requeue = {
	.name = "requeue",
	.has_arg = no_argument,
	.val = LONG_OPT_REQUEUE,
	.set_func_sbatch = arg_set_requeue,
	.set_func_data = arg_set_data_requeue,
	.get_func = arg_get_requeue,
	.reset_func = arg_reset_requeue,
};

COMMON_STRING_OPTION(reservation);
static slurm_cli_opt_t slurm_opt_reservation = {
	.name = "reservation",
	.has_arg = required_argument,
	.val = LONG_OPT_RESERVATION,
	.set_func = arg_set_reservation,
	.set_func_data = arg_set_data_reservation,
	.get_func = arg_get_reservation,
	.reset_func = arg_reset_reservation,
};

static int arg_set_resv_port_cnt(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	if (!arg)
		opt->srun_opt->resv_port_cnt = 0;
	else
		opt->srun_opt->resv_port_cnt = parse_int("--resv-port",
							 arg, false);

	return SLURM_SUCCESS;
}
static char *arg_get_resv_port_cnt(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	if (opt->srun_opt->resv_port_cnt == NO_VAL)
		return xstrdup("unset");

	return xstrdup_printf("%d", opt->srun_opt->resv_port_cnt);
}
static void arg_reset_resv_port_cnt(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->resv_port_cnt = NO_VAL;
}
static slurm_cli_opt_t slurm_opt_resv_ports = {
	.name = "resv-ports",
	.has_arg = optional_argument,
	.val = LONG_OPT_RESV_PORTS,
	.set_func_srun = arg_set_resv_port_cnt,
	.get_func = arg_get_resv_port_cnt,
	.reset_func = arg_reset_resv_port_cnt,
	.reset_each_pass = true,
};

static int arg_set_send_libs(slurm_opt_t *opt, const char *arg)
{
	int rc;

	if (!opt->srun_opt)
		return SLURM_ERROR;

	if ((rc = parse_send_libs(arg)) == -1) {
		error("Invalid --send-libs specification");
		exit(-1);
	}

	opt->srun_opt->send_libs = rc ? true : false;

	return SLURM_SUCCESS;
}
static char *arg_get_send_libs(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	if (opt->srun_opt->send_libs)
		return xstrdup("set");

	return NULL;
}
static void arg_reset_send_libs(slurm_opt_t *opt)
{
	char *tmp = NULL;

	if (opt->srun_opt) {
		tmp = xstrcasestr(slurm_conf.bcast_parameters, "send_libs");
		opt->srun_opt->send_libs = tmp ? true : false;
	}
}
static slurm_cli_opt_t slurm_opt_send_libs = {
	.name = "send-libs",
	.has_arg = optional_argument,
	.val = LONG_OPT_SEND_LIBS,
	.set_func_srun = arg_set_send_libs,
	.get_func = arg_get_send_libs,
	.reset_func = arg_reset_send_libs,
	.reset_each_pass = true,
};

static int arg_set_signal(slurm_opt_t *opt, const char *arg)
{
	if (get_signal_opts((char *) arg, &opt->warn_signal,
			    &opt->warn_time, &opt->warn_flags)) {
		error("Invalid --signal specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_signal(slurm_opt_t *opt, const data_t *arg,
			       data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if (get_signal_opts(str, &opt->warn_signal, &opt->warn_time,
				 &opt->warn_flags)) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Invalid SIGNAL specification", rc);
	}
	xfree(str);
	return rc;
}
static char *arg_get_signal(slurm_opt_t *opt)
{
	return signal_opts_to_cmdline(opt->warn_signal, opt->warn_time,
				      opt->warn_flags);
}
static void arg_reset_signal(slurm_opt_t *opt)
{
	opt->warn_flags = 0;
	opt->warn_signal = 0;
	opt->warn_time = 0;
}
static slurm_cli_opt_t slurm_opt_signal = {
	.name = "signal",
	.has_arg = required_argument,
	.val = LONG_OPT_SIGNAL,
	.set_func = arg_set_signal,
	.set_func_data = arg_set_data_signal,
	.get_func = arg_get_signal,
	.reset_func = arg_reset_signal,
};

static int arg_set_slurmd_debug(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	opt->srun_opt->slurmd_debug = log_string2num(arg);

	return SLURM_SUCCESS;
}
static char *arg_get_slurmd_debug(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	return xstrdup(log_num2string(opt->srun_opt->slurmd_debug));
}
static void arg_reset_slurmd_debug(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->slurmd_debug = LOG_LEVEL_QUIET;
}
static slurm_cli_opt_t slurm_opt_slurmd_debug = {
	.name = "slurmd-debug",
	.has_arg = required_argument,
	.val = LONG_OPT_SLURMD_DEBUG,
	.set_func_srun = arg_set_slurmd_debug,
	.get_func = arg_get_slurmd_debug,
	.reset_func = arg_reset_slurmd_debug,
};

COMMON_INT_OPTION_SET(sockets_per_node, "--sockets-per-node");
COMMON_INT_OPTION_SET_DATA(sockets_per_node);
COMMON_INT_OPTION_GET(sockets_per_node);
COMMON_OPTION_RESET(sockets_per_node, NO_VAL);
static slurm_cli_opt_t slurm_opt_sockets_per_node = {
	.name = "sockets-per-node",
	.has_arg = required_argument,
	.val = LONG_OPT_SOCKETSPERNODE,
	.set_func = arg_set_sockets_per_node,
	.set_func_data = arg_set_data_sockets_per_node,
	.get_func = arg_get_sockets_per_node,
	.reset_func = arg_reset_sockets_per_node,
	.reset_each_pass = true,
};

static int arg_set_spread_job(slurm_opt_t *opt, const char *arg)
{
	opt->job_flags |= SPREAD_JOB;

	return SLURM_SUCCESS;
}
static int arg_set_data_spread_job(slurm_opt_t *opt, const data_t *arg,
				   data_t *errors)
{
	opt->job_flags |= SPREAD_JOB;

	return SLURM_SUCCESS;
}
static char *arg_get_spread_job(slurm_opt_t *opt)
{
	if (opt->job_flags & SPREAD_JOB)
		return xstrdup("set");
	return xstrdup("unset");
}
static void arg_reset_spread_job(slurm_opt_t *opt)
{
	opt->job_flags &= ~SPREAD_JOB;
}
static slurm_cli_opt_t slurm_opt_spread_job = {
	.name = "spread-job",
	.has_arg = no_argument,
	.val = LONG_OPT_SPREAD_JOB,
	.set_func = arg_set_spread_job,
	.set_func_data = arg_set_data_spread_job,
	.get_func = arg_get_spread_job,
	.reset_func = arg_reset_spread_job,
	.reset_each_pass = true,
};

static int arg_set_switch_req(slurm_opt_t *opt, const char *arg)
{
	opt->req_switch = parse_int("--switches", arg, true);

	return SLURM_SUCCESS;
}
static char *arg_get_switch_req(slurm_opt_t *opt)
{
	if (opt->req_switch != -1)
		return xstrdup_printf("%d", opt->req_switch);
	return xstrdup("unset");
}
static void arg_reset_switch_req(slurm_opt_t *opt)
{
	opt->req_switch = -1;
}
COMMON_INT_OPTION_SET_DATA(req_switch);
static slurm_cli_opt_t slurm_opt_switch_req = {
	.name = NULL, /* envvar only */
	.has_arg = required_argument,
	.val = LONG_OPT_SWITCH_REQ,
	.set_func = arg_set_switch_req,
	.set_func_data = arg_set_data_req_switch,
	.get_func = arg_get_switch_req,
	.reset_func = arg_reset_switch_req,
	.reset_each_pass = true,
};

static int arg_set_switch_wait(slurm_opt_t *opt, const char *arg)
{
	opt->wait4switch = time_str2secs(arg);

	return SLURM_SUCCESS;
}
static int arg_set_data_switch_wait(slurm_opt_t *opt, const data_t *arg,
				    data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else
		opt->wait4switch = time_str2secs(str);

	xfree(str);
	return rc;
}
static char *arg_get_switch_wait(slurm_opt_t *opt)
{
	char time_str[32];
	secs2time_str(opt->wait4switch, time_str, sizeof(time_str));
	return xstrdup_printf("%s", time_str);
}
static void arg_reset_switch_wait(slurm_opt_t *opt)
{
	opt->req_switch = -1;
	opt->wait4switch = -1;
}
static slurm_cli_opt_t slurm_opt_switch_wait = {
	.name = NULL, /* envvar only */
	.has_arg = required_argument,
	.val = LONG_OPT_SWITCH_WAIT,
	.set_func = arg_set_switch_wait,
	.set_func_data = arg_set_data_switch_wait,
	.get_func = arg_get_switch_wait,
	.reset_func = arg_reset_switch_wait,
	.reset_each_pass = true,
};

static int arg_set_switches(slurm_opt_t *opt, const char *arg)
{
	char *tmparg = xstrdup(arg);
	char *split = xstrchr(tmparg, '@');

	if (split) {
		split[0] = '\0';
		split++;
		opt->wait4switch = time_str2secs(split);
	}

	opt->req_switch = parse_int("--switches", tmparg, true);

	xfree(tmparg);

	return SLURM_SUCCESS;
}

static int _handle_data_switches_str(slurm_opt_t *opt, char *arg,
				     data_t *errors)
{
	int rc = SLURM_SUCCESS;
	char *split = xstrchr(arg, '@');

	if (split) {
		split[0] = '\0';
		split++;
		opt->wait4switch = time_str2secs(split);

		rc = _handle_data_switches_str(opt, arg, errors);
	} else
		opt->req_switch = atoi(arg);

	return rc;
}

static int _handle_data_switches_data(slurm_opt_t *opt, const data_t *arg,
				      data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else
		rc = _handle_data_switches_str(opt, str, errors);

	xfree(str);

	return rc;
}

typedef struct {
	slurm_opt_t *opt;
	data_t *errors;
} data_foreach_switches_t;

data_for_each_cmd_t
	_foreach_data_switches(const char *key, const data_t *data, void *arg)
{
	data_foreach_switches_t *args = arg;
	data_t *errors = args->errors;

	if (!xstrcasecmp("count", key)) {
		int64_t val;

		if (data_get_int_converted(data, &val)) {
			ADD_DATA_ERROR("Invalid count specification",
				       SLURM_ERROR);
			return DATA_FOR_EACH_FAIL;
		}

		args->opt->req_switch = (int) val;
	} else if (!xstrcasecmp("timeout", key)) {
		char *str = NULL;

		if (data_get_string_converted(data, &str)) {
			return DATA_FOR_EACH_FAIL;
			ADD_DATA_ERROR("Invalid timeout specification",
				       SLURM_ERROR);
		}

		args->opt->wait4switch = time_str2secs(str);
		xfree(str);
	} else {
		ADD_DATA_ERROR("unknown key in switches specification",
				       SLURM_ERROR);
		return DATA_FOR_EACH_FAIL;
	}

	return DATA_FOR_EACH_CONT;
}

static int arg_set_data_switches(slurm_opt_t *opt, const data_t *arg,
				 data_t *errors)
{
	int rc = SLURM_SUCCESS;
	int64_t val;

	if (data_get_type(arg) == DATA_TYPE_DICT) {
		data_foreach_switches_t args = {
			.opt = opt,
			.errors = errors,
		};
		if (data_dict_for_each_const(arg, _foreach_data_switches,
					     &args) < 0) {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid switch specification", rc);
		}
	} else if ((rc = data_get_int_converted(arg, &val)))
		return _handle_data_switches_data(opt, arg, errors);
	else if (val >= INT_MAX) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Integer too large", rc);
	} else if (val <= 0) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Must request at least 1 switch", rc);
	} else
		opt->req_switch = (int) val;

	return rc;
}

static char *arg_get_switches(slurm_opt_t *opt)
{
	if (opt->wait4switch != -1) {
		char time_str[32];
		secs2time_str(opt->wait4switch, time_str, sizeof(time_str));
		return xstrdup_printf("%d@%s", opt->req_switch, time_str);
	}
	if (opt->req_switch != -1)
		return xstrdup_printf("%d", opt->req_switch);
	return xstrdup("unset");
}
static void arg_reset_switches(slurm_opt_t *opt)
{
	opt->req_switch = -1;
	opt->wait4switch = -1;
}
static slurm_cli_opt_t slurm_opt_switches = {
	.name = "switches",
	.has_arg = required_argument,
	.val = LONG_OPT_SWITCHES,
	.set_func = arg_set_switches,
	.set_func_data = arg_set_data_switches,
	.get_func = arg_get_switches,
	.reset_func = arg_reset_switches,
	.reset_each_pass = true,
};

COMMON_SRUN_STRING_OPTION(task_epilog);
static slurm_cli_opt_t slurm_opt_task_epilog = {
	.name = "task-epilog",
	.has_arg = required_argument,
	.val = LONG_OPT_TASK_EPILOG,
	.set_func_srun = arg_set_task_epilog,
	.get_func = arg_get_task_epilog,
	.reset_func = arg_reset_task_epilog,
};

COMMON_SRUN_STRING_OPTION(task_prolog);
static slurm_cli_opt_t slurm_opt_task_prolog = {
	.name = "task-prolog",
	.has_arg = required_argument,
	.val = LONG_OPT_TASK_PROLOG,
	.set_func_srun = arg_set_task_prolog,
	.get_func = arg_get_task_prolog,
	.reset_func = arg_reset_task_prolog,
};

/* Deprecated form of --ntasks-per-node */
static slurm_cli_opt_t slurm_opt_tasks_per_node = {
	.name = "tasks-per-node",
	.has_arg = required_argument,
	.val = LONG_OPT_NTASKSPERNODE,
	.set_func = arg_set_ntasks_per_node,
	.get_func = arg_get_ntasks_per_node,
	.reset_func = arg_reset_ntasks_per_node,
	.reset_each_pass = true,
};

static int arg_set_test_only(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt && !opt->srun_opt)
		return SLURM_ERROR;

	if (opt->sbatch_opt)
		opt->sbatch_opt->test_only = true;
	if (opt->srun_opt)
		opt->srun_opt->test_only = true;

	return SLURM_SUCCESS;
}
static int arg_set_data_test_only(slurm_opt_t *opt, const data_t *arg,
				  data_t *errors)
{
	if (!opt->sbatch_opt && !opt->srun_opt)
		return SLURM_ERROR;

	if (opt->sbatch_opt)
		opt->sbatch_opt->test_only = true;
	if (opt->srun_opt)
		opt->srun_opt->test_only = true;

	return SLURM_SUCCESS;
}
static char *arg_get_test_only(slurm_opt_t *opt)
{
	bool tmp = false;

	if (!opt->sbatch_opt && !opt->srun_opt)
		return xstrdup("invalid-context");

	if (opt->sbatch_opt)
		tmp = opt->sbatch_opt->test_only;
	if (opt->srun_opt)
		tmp = opt->srun_opt->test_only;

	return xstrdup(tmp ? "set" : "unset");
}
static void arg_reset_test_only(slurm_opt_t *opt)
{
	if (opt->sbatch_opt)
		opt->sbatch_opt->test_only = false;
	if (opt->srun_opt)
		opt->srun_opt->test_only = false;
}
static slurm_cli_opt_t slurm_opt_test_only = {
	.name = "test-only",
	.has_arg = no_argument,
	.val = LONG_OPT_TEST_ONLY,
	.set_func_sbatch = arg_set_test_only,
	.set_func_srun = arg_set_test_only,
	.set_func_data = arg_set_data_test_only,
	.get_func = arg_get_test_only,
	.reset_func = arg_reset_test_only,
};

/* note this is mutually exclusive with --core-spec above */
static int arg_set_thread_spec(slurm_opt_t *opt, const char *arg)
{
	opt->core_spec = parse_int("--thread-spec", arg, true);
	opt->core_spec |= CORE_SPEC_THREAD;

	return SLURM_SUCCESS;
}
static int arg_set_data_thread_spec(slurm_opt_t *opt, const data_t *arg,
				    data_t *errors)
{
	int rc;
	int64_t val;

	if ((rc = data_get_int_converted(arg, &val)))
		ADD_DATA_ERROR("Unable to read integer", rc);
	else if (val >= CORE_SPEC_THREAD) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("core_spec is too large", rc);
	} else if (val <= 0) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("core_spec must be >0", rc);
	} else {
		opt->core_spec = val;
		opt->core_spec |= CORE_SPEC_THREAD;
	}

	return rc;
}
static char *arg_get_thread_spec(slurm_opt_t *opt)
{
	if ((opt->core_spec == NO_VAL16) ||
	    !(opt->core_spec & CORE_SPEC_THREAD))
		return xstrdup("unset");
	return xstrdup_printf("%d", (opt->core_spec & ~CORE_SPEC_THREAD));
}
static slurm_cli_opt_t slurm_opt_thread_spec = {
	.name = "thread-spec",
	.has_arg = required_argument,
	.val = LONG_OPT_THREAD_SPEC,
	.set_func = arg_set_thread_spec,
	.set_func_data = arg_set_data_thread_spec,
	.get_func = arg_get_thread_spec,
	.reset_func = arg_reset_core_spec,
	.reset_each_pass = true,
};

static int arg_set_threads_per_core(slurm_opt_t *opt, const char *arg)
{
	opt->threads_per_core = parse_int("--threads-per-core", arg, true);

	return SLURM_SUCCESS;
}
COMMON_INT_OPTION_SET_DATA(threads_per_core);
COMMON_INT_OPTION_GET(threads_per_core);
COMMON_OPTION_RESET(threads_per_core, NO_VAL);
static slurm_cli_opt_t slurm_opt_threads_per_core = {
	.name = "threads-per-core",
	.has_arg = required_argument,
	.val = LONG_OPT_THREADSPERCORE,
	.set_func = arg_set_threads_per_core,
	.set_func_data = arg_set_data_threads_per_core,
	.get_func = arg_get_threads_per_core,
	.reset_func = arg_reset_threads_per_core,
	.reset_each_pass = true,
};

static int arg_set_time_limit(slurm_opt_t *opt, const char *arg)
{
	int time_limit;

	time_limit = time_str2mins(arg);
	if (time_limit == NO_VAL) {
		error("Invalid --time specification");
		return SLURM_ERROR;
	} else if (time_limit == 0) {
		time_limit = INFINITE;
	}

	opt->time_limit = time_limit;
	return SLURM_SUCCESS;
}
static int arg_set_data_time_limit(slurm_opt_t *opt, const data_t *arg,
			       data_t *errors)
{
	int rc;
	char *str = NULL;

	if (!opt->sbatch_opt && !opt->srun_opt)
		return SLURM_ERROR;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		int time_limit = time_str2mins(str);
		if (time_limit == NO_VAL) {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid time specification", rc);
		} else if (time_limit == 0) {
			opt->time_limit = INFINITE;
		} else
			opt->time_limit = time_limit;
	}

	xfree(str);
	return rc;
}
COMMON_TIME_DURATION_OPTION_GET_AND_RESET(time_limit);
static slurm_cli_opt_t slurm_opt_time_limit = {
	.name = "time",
	.has_arg = required_argument,
	.val = 't',
	.set_func = arg_set_time_limit,
	.set_func_data = arg_set_data_time_limit,
	.get_func = arg_get_time_limit,
	.reset_func = arg_reset_time_limit,
};

static int arg_set_time_min(slurm_opt_t *opt, const char *arg)
{
	int time_min;

	time_min = time_str2mins(arg);
	if (time_min == NO_VAL) {
		error("Invalid --time-min specification");
		return SLURM_ERROR;
	} else if (time_min == 0) {
		time_min = INFINITE;
	}

	opt->time_min = time_min;
	return SLURM_SUCCESS;
}
static int arg_set_data_time_min(slurm_opt_t *opt, const data_t *arg,
				 data_t *errors)
{
	int rc;
	char *str = NULL;

	if (!opt->sbatch_opt && !opt->srun_opt)
		return SLURM_ERROR;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else {
		int time_limit = time_str2mins(str);
		if (time_limit == NO_VAL) {
			rc = SLURM_ERROR;
			ADD_DATA_ERROR("Invalid time specification", rc);
		} else if (time_limit == 0) {
			opt->time_min = INFINITE;
		} else
			opt->time_min = time_limit;
	}

	xfree(str);
	return rc;
}
COMMON_TIME_DURATION_OPTION_GET_AND_RESET(time_min);
static slurm_cli_opt_t slurm_opt_time_min = {
	.name = "time-min",
	.has_arg = required_argument,
	.val = LONG_OPT_TIME_MIN,
	.set_func = arg_set_time_min,
	.set_func_data = arg_set_data_time_min,
	.get_func = arg_get_time_min,
	.reset_func = arg_reset_time_min,
};

COMMON_MBYTES_OPTION(pn_min_tmp_disk, --tmp);
static slurm_cli_opt_t slurm_opt_tmp = {
	.name = "tmp",
	.has_arg = required_argument,
	.val = LONG_OPT_TMP,
	.set_func = arg_set_pn_min_tmp_disk,
	.set_func_data = arg_set_data_pn_min_tmp_disk,
	.get_func = arg_get_pn_min_tmp_disk,
	.reset_func = arg_reset_pn_min_tmp_disk,
	.reset_each_pass = true,
};

static int arg_set_uid(slurm_opt_t *opt, const char *arg)
{
	if (getuid() != 0) {
		error("--uid only permitted by root user");
		return SLURM_ERROR;
	}

	if (uid_from_string(arg, &opt->uid) < 0) {
		error("Invalid --uid specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_uid(slurm_opt_t *opt, const data_t *arg,
			    data_t *errors)
{
	int rc;
	char *str = NULL;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if (uid_from_string(str, &opt->uid) < 0) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Invalid user id specification", rc);
	}

	xfree(str);
	return rc;
}
COMMON_INT_OPTION_GET(uid);
COMMON_OPTION_RESET(uid, getuid());
static slurm_cli_opt_t slurm_opt_uid = {
	.name = "uid",
	.has_arg = required_argument,
	.val = LONG_OPT_UID,
	.set_func = arg_set_uid,
	.set_func_data = arg_set_data_uid,
	.get_func = arg_get_uid,
	.reset_func = arg_reset_uid,
};

/*
 * This is not exposed as an argument in sbatch, but is used
 * in xlate.c to translate a PBS option.
 */
static int arg_set_umask(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt)
		return SLURM_ERROR;

	opt->sbatch_opt->umask = strtol(arg, NULL, 0);

	if ((opt->sbatch_opt->umask < 0) || (opt->sbatch_opt->umask > 0777)) {
		error("Invalid -W umask= specification");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
static int arg_set_data_umask(slurm_opt_t *opt, const data_t *arg,
			      data_t *errors)
{
	int rc;
	char *str = NULL;
	int32_t umask;

	if ((rc = data_get_string_converted(arg, &str)))
		ADD_DATA_ERROR("Unable to read string", rc);
	else if (sscanf(str, "%"SCNo32, &umask) != 1) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Invalid octal umask", rc);
	} else if (umask < 0) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("umask too small", rc);
	} else if (umask < 0 || umask > 07777) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("umask too large", rc);
	} else
		opt->sbatch_opt->umask = umask;

	xfree(str);
	return rc;
}
static char *arg_get_umask(slurm_opt_t *opt)
{
	if (!opt->sbatch_opt)
		return xstrdup("invalid-context");

	return xstrdup_printf("0%o", opt->sbatch_opt->umask);
}
static void arg_reset_umask(slurm_opt_t *opt)
{
	if (opt->sbatch_opt)
		opt->sbatch_opt->umask = -1;
}
static slurm_cli_opt_t slurm_opt_umask = {
	.name = NULL, /* only for use through xlate.c */
	.has_arg = no_argument,
	.val = LONG_OPT_UMASK,
	.set_func_sbatch = arg_set_umask,
	.set_func_data = arg_set_data_umask,
	.get_func = arg_get_umask,
	.reset_func = arg_reset_umask,
	.reset_each_pass = true,
};

COMMON_SRUN_BOOL_OPTION(unbuffered);
static slurm_cli_opt_t slurm_opt_unbuffered = {
	.name = "unbuffered",
	.has_arg = no_argument,
	.val = 'u',
	.set_func_srun = arg_set_unbuffered,
	.get_func = arg_get_unbuffered,
	.reset_func = arg_reset_unbuffered,
};

static int arg_set_use_min_nodes(slurm_opt_t *opt, const char *arg)
{
	opt->job_flags |= USE_MIN_NODES;

	return SLURM_SUCCESS;
}
static int arg_set_data_use_min_nodes(slurm_opt_t *opt, const data_t *arg,
				      data_t *errors)
{
	opt->job_flags |= USE_MIN_NODES;

	return SLURM_SUCCESS;
}
static char *arg_get_use_min_nodes(slurm_opt_t *opt)
{
	if (opt->job_flags & USE_MIN_NODES)
		return xstrdup("set");
	return xstrdup("unset");
}
static void arg_reset_use_min_nodes(slurm_opt_t *opt)
{
	opt->job_flags &= ~(USE_MIN_NODES);
}
static slurm_cli_opt_t slurm_opt_use_min_nodes = {
	.name = "use-min-nodes",
	.has_arg = no_argument,
	.val = LONG_OPT_USE_MIN_NODES,
	.set_func = arg_set_use_min_nodes,
	.set_func_data = arg_set_data_use_min_nodes,
	.get_func = arg_get_use_min_nodes,
	.reset_func = arg_reset_use_min_nodes,
	.reset_each_pass = true,
};

static int arg_set_usage(slurm_opt_t *opt, const char *arg)
{
	if(opt->scron_opt)
		return SLURM_ERROR;

	if (opt->usage_func)
		(opt->usage_func)();
	else
		error("Could not find --usage message");

	exit(0);
	return SLURM_SUCCESS;
}
static char *arg_get_usage(slurm_opt_t *opt)
{
	return NULL; /* no op */
}
static void arg_reset_usage(slurm_opt_t *opt)
{
	/* no op */
}
static slurm_cli_opt_t slurm_opt_usage = {
	.name = "usage",
	.has_arg = no_argument,
	.val = LONG_OPT_USAGE,
	.sbatch_early_pass = true,
	.set_func = arg_set_usage,
	.get_func = arg_get_usage,
	.reset_func = arg_reset_usage,
};

static int arg_set_verbose(slurm_opt_t *opt, const char *arg)
{
	/*
	 * Note that verbose is handled a bit differently. As a cli argument,
	 * it has no_argument set so repeated 'v' characters can be used.
	 * As an environment variable though, it will have a numeric value.
	 * The boolean treatment from slurm_process_option() will still pass
	 * the string form along to us, which we can parse here into the
	 * correct value.
	 */
	if (!arg)
		opt->verbose++;
	else
		opt->verbose = parse_int("--verbose", arg, false);

	return SLURM_SUCCESS;
}
COMMON_INT_OPTION_GET_AND_RESET(verbose);
static slurm_cli_opt_t slurm_opt_verbose = {
	.name = "verbose",
	.has_arg = no_argument,	/* sort of */
	.val = 'v',
	.sbatch_early_pass = true,
	.set_func = arg_set_verbose,
	.get_func = arg_get_verbose,
	.reset_func = arg_reset_verbose,
};

static int arg_set_version(slurm_opt_t *opt, const char *arg)
{
	if (opt->scron_opt)
		return SLURM_ERROR;

	print_slurm_version();
	exit(0);
}
static char *arg_get_version(slurm_opt_t *opt)
{
	return NULL; /* no op */
}
static void arg_reset_version(slurm_opt_t *opt)
{
	/* no op */
}
static slurm_cli_opt_t slurm_opt_version = {
	.name = "version",
	.has_arg = no_argument,
	.val = 'V',
	.sbatch_early_pass = true,
	.set_func = arg_set_version,
	.get_func = arg_get_version,
	.reset_func = arg_reset_version,
};

static int arg_set_wait(slurm_opt_t *opt, const char *arg)
{
	if (!opt->sbatch_opt)
		return SLURM_ERROR;

	opt->sbatch_opt->wait = true;

	return SLURM_SUCCESS;
}
static char *arg_get_wait(slurm_opt_t *opt)
{
	if (!opt->sbatch_opt)
		return xstrdup("invalid-context");

	return xstrdup(opt->sbatch_opt->wait ? "set" : "unset");
}
static void arg_reset_wait(slurm_opt_t *opt)
{
	if (opt->sbatch_opt)
		opt->sbatch_opt->wait = false;
}
static slurm_cli_opt_t slurm_opt_wait = {
	.name = "wait",
	.has_arg = no_argument,
	.val = 'W',
	.set_func_sbatch = arg_set_wait,
	.get_func = arg_get_wait,
	.reset_func = arg_reset_wait,
};

static int arg_set_wait_srun(slurm_opt_t *opt, const char *arg)
{
	if (!opt->srun_opt)
		return SLURM_ERROR;

	opt->srun_opt->max_wait = parse_int("--wait", arg, false);

	return SLURM_SUCCESS;
}
static char *arg_get_wait_srun(slurm_opt_t *opt)
{
	if (!opt->srun_opt)
		return xstrdup("invalid-context");

	return xstrdup_printf("%d", opt->srun_opt->max_wait);
}
static void arg_reset_wait_srun(slurm_opt_t *opt)
{
	if (opt->srun_opt)
		opt->srun_opt->max_wait = slurm_conf.wait_time;
}
static slurm_cli_opt_t slurm_opt_wait_srun = {
	.name = "wait",
	.has_arg = required_argument,
	.val = 'W',
	.set_func_srun = arg_set_wait_srun,
	.get_func = arg_get_wait_srun,
	.reset_func = arg_reset_wait_srun,
};

static int arg_set_wait_all_nodes(slurm_opt_t *opt, const char *arg)
{
	uint16_t tmp;

	if (!opt->salloc_opt && !opt->sbatch_opt)
		return SLURM_ERROR;

	tmp = parse_int("--wait-all-nodes", arg, false);

	if (tmp > 1) {
		error("Invalid --wait-all-nodes specification");
		return SLURM_ERROR;
	}

	if (opt->salloc_opt)
		opt->salloc_opt->wait_all_nodes = tmp;
	if (opt->sbatch_opt)
		opt->sbatch_opt->wait_all_nodes = tmp;

	return SLURM_SUCCESS;
}
static int arg_set_data_wait_all_nodes(slurm_opt_t *opt, const data_t *arg,
				       data_t *errors)
{
	int64_t val;
	int rc = data_get_int_converted(arg, &val);
	if (rc)
		ADD_DATA_ERROR("Unable to read integer value", rc);
	else if (val > 1) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Wait all nodes too large", rc);
	} else if (val < 0) {
		rc = SLURM_ERROR;
		ADD_DATA_ERROR("Wait all nodes too small", rc);
	} else {
		if (opt->salloc_opt)
			opt->salloc_opt->wait_all_nodes = val;
		if (opt->sbatch_opt)
			opt->sbatch_opt->wait_all_nodes = val;
	}
	return rc;
}
static char *arg_get_wait_all_nodes(slurm_opt_t *opt)
{
	uint16_t tmp = NO_VAL16;

	if (!opt->salloc_opt && !opt->sbatch_opt)
		return xstrdup("invalid-context");

	if (opt->salloc_opt)
		tmp = opt->salloc_opt->wait_all_nodes;
	if (opt->sbatch_opt)
		tmp = opt->sbatch_opt->wait_all_nodes;

	return xstrdup_printf("%u", tmp);
}
static void arg_reset_wait_all_nodes(slurm_opt_t *opt)
{
	if (opt->salloc_opt)
		opt->salloc_opt->wait_all_nodes = NO_VAL16;
	if (opt->sbatch_opt)
		opt->sbatch_opt->wait_all_nodes = NO_VAL16;
}
static slurm_cli_opt_t slurm_opt_wait_all_nodes = {
	.name = "wait-all-nodes",
	.has_arg = required_argument,
	.val = LONG_OPT_WAIT_ALL_NODES,
	.set_func_salloc = arg_set_wait_all_nodes,
	.set_func_sbatch = arg_set_wait_all_nodes,
	.set_func_data = arg_set_data_wait_all_nodes,
	.get_func = arg_get_wait_all_nodes,
	.reset_func = arg_reset_wait_all_nodes,
};

COMMON_STRING_OPTION(wckey);
static slurm_cli_opt_t slurm_opt_wckey = {
	.name = "wckey",
	.has_arg = required_argument,
	.val = LONG_OPT_WCKEY,
	.set_func = arg_set_wckey,
	.set_func_data = arg_set_data_wckey,
	.get_func = arg_get_wckey,
	.reset_func = arg_reset_wckey,
};

COMMON_SRUN_BOOL_OPTION(whole);
static slurm_cli_opt_t slurm_opt_whole = {
	.name = "whole",
	.has_arg = no_argument,
	.val = LONG_OPT_WHOLE,
	.set_func_srun = arg_set_whole,
	.get_func = arg_get_whole,
	.reset_func = arg_reset_whole,
};

COMMON_SBATCH_STRING_OPTION(wrap);
static slurm_cli_opt_t slurm_opt_wrap = {
	.name = "wrap",
	.has_arg = required_argument,
	.val = LONG_OPT_WRAP,
	.sbatch_early_pass = true,
	.set_func_sbatch = arg_set_wrap,
	.set_func_data = arg_set_data_wrap,
	.get_func = arg_get_wrap,
	.reset_func = arg_reset_wrap,
};

static int arg_set_x11(slurm_opt_t *opt, const char *arg)
{
	if (arg)
		opt->x11 = x11_str2flags(arg);
	else
		opt->x11 = X11_FORWARD_ALL;

	return SLURM_SUCCESS;
}
static char *arg_get_x11(slurm_opt_t *opt)
{
	return xstrdup(x11_flags2str(opt->x11));
}
COMMON_OPTION_RESET(x11, 0);
static slurm_cli_opt_t slurm_opt_x11 = {
#ifdef WITH_SLURM_X11
	.name = "x11",
#else
	/*
	 * Keep the code paths active, but disables the option name itself
	 * so the SPANK plugin can claim it.
	 */
	.name = NULL,
#endif
	.has_arg = optional_argument,
	.val = LONG_OPT_X11,
	.set_func_salloc = arg_set_x11,
	.set_func_srun = arg_set_x11,
	.get_func = arg_get_x11,
	.reset_func = arg_reset_x11,
};

static const slurm_cli_opt_t *common_options[] = {
	&slurm_opt__unknown_,
	&slurm_opt_accel_bind,
	&slurm_opt_account,
	&slurm_opt_acctg_freq,
	&slurm_opt_alloc_nodelist,
	&slurm_opt_array,
	&slurm_opt_argv,
	&slurm_opt_batch,
	&slurm_opt_bcast,
	&slurm_opt_bcast_exclude,
	&slurm_opt_begin,
	&slurm_opt_bell,
	&slurm_opt_bb,
	&slurm_opt_bbf,
	&slurm_opt_c_constraint,
	&slurm_opt_chdir,
	&slurm_opt_cluster,
	&slurm_opt_clusters,
	&slurm_opt_comment,
	&slurm_opt_compress,
	&slurm_opt_container,
	&slurm_opt_context,
	&slurm_opt_contiguous,
	&slurm_opt_constraint,
	&slurm_opt_core_spec,
	&slurm_opt_cores_per_socket,
	&slurm_opt_cpu_bind,
	&slurm_opt_cpu_underscore_bind,
	&slurm_opt_cpu_freq,
	&slurm_opt_cpus_per_gpu,
	&slurm_opt_cpus_per_task,
	&slurm_opt_deadline,
	&slurm_opt_debugger_test,
	&slurm_opt_delay_boot,
	&slurm_opt_environment,
	&slurm_opt_dependency,
	&slurm_opt_disable_status,
	&slurm_opt_distribution,
	&slurm_opt_epilog,
	&slurm_opt_error,
	&slurm_opt_exact,
	&slurm_opt_exclude,
	&slurm_opt_exclusive,
	&slurm_opt_export,
	&slurm_opt_export_file,
	&slurm_opt_extra_node_info,
	&slurm_opt_get_user_env,
	&slurm_opt_gid,
	&slurm_opt_gpu_bind,
	&slurm_opt_gpu_freq,
	&slurm_opt_gpus,
	&slurm_opt_gpus_per_node,
	&slurm_opt_gpus_per_socket,
	&slurm_opt_gpus_per_task,
	&slurm_opt_gres,
	&slurm_opt_gres_flags,
	&slurm_opt_help,
	&slurm_opt_het_group,
	&slurm_opt_hint,
	&slurm_opt_hold,
	&slurm_opt_ignore_pbs,
	&slurm_opt_immediate,
	&slurm_opt_input,
	&slurm_opt_interactive,
	&slurm_opt_jobid,
	&slurm_opt_job_name,
	&slurm_opt_kill_command,
	&slurm_opt_kill_on_bad_exit,
	&slurm_opt_kill_on_invalid_dep,
	&slurm_opt_label,
	&slurm_opt_licenses,
	&slurm_opt_mail_type,
	&slurm_opt_mail_user,
	&slurm_opt_max_threads,
	&slurm_opt_mcs_label,
	&slurm_opt_mem,
	&slurm_opt_mem_bind,
	&slurm_opt_mem_per_cpu,
	&slurm_opt_mem_per_gpu,
	&slurm_opt_mincpus,
	&slurm_opt_mpi,
	&slurm_opt_msg_timeout,
	&slurm_opt_multi_prog,
	&slurm_opt_network,
	&slurm_opt_nice,
	&slurm_opt_no_allocate,
	&slurm_opt_no_bell,
	&slurm_opt_no_kill,
	&slurm_opt_no_shell,
	&slurm_opt_no_requeue,
	&slurm_opt_nodefile,
	&slurm_opt_nodelist,
	&slurm_opt_nodes,
	&slurm_opt_ntasks,
	&slurm_opt_ntasks_per_core,
	&slurm_opt_ntasks_per_gpu,
	&slurm_opt_ntasks_per_node,
	&slurm_opt_ntasks_per_socket,
	&slurm_opt_ntasks_per_tres,
	&slurm_opt_open_mode,
	&slurm_opt_output,
	&slurm_opt_overcommit,
	&slurm_opt_overlap,
	&slurm_opt_oversubscribe,
	&slurm_opt_pack_group,
	&slurm_opt_parsable,
	&slurm_opt_partition,
	&slurm_opt_power,
	&slurm_opt_preserve_env,
	&slurm_opt_priority,
	&slurm_opt_profile,
	&slurm_opt_prolog,
	&slurm_opt_propagate,
	&slurm_opt_pty,
	&slurm_opt_qos,
	&slurm_opt_quiet,
	&slurm_opt_quit_on_interrupt,
	&slurm_opt_reboot,
	&slurm_opt_relative,
	&slurm_opt_requeue,
	&slurm_opt_reservation,
	&slurm_opt_resv_ports,
	&slurm_opt_send_libs,
	&slurm_opt_signal,
	&slurm_opt_slurmd_debug,
	&slurm_opt_sockets_per_node,
	&slurm_opt_spread_job,
	&slurm_opt_switch_req,
	&slurm_opt_switch_wait,
	&slurm_opt_switches,
	&slurm_opt_task_epilog,
	&slurm_opt_task_prolog,
	&slurm_opt_tasks_per_node,
	&slurm_opt_test_only,
	&slurm_opt_thread_spec,
	&slurm_opt_threads_per_core,
	&slurm_opt_time_limit,
	&slurm_opt_time_min,
	&slurm_opt_tmp,
	&slurm_opt_uid,
	&slurm_opt_unbuffered,
	&slurm_opt_use_min_nodes,
	&slurm_opt_verbose,
	&slurm_opt_version,
	&slurm_opt_umask,
	&slurm_opt_usage,
	&slurm_opt_wait,
	&slurm_opt_wait_all_nodes,
	&slurm_opt_wait_srun,
	&slurm_opt_wckey,
	&slurm_opt_whole,
	&slurm_opt_wrap,
	&slurm_opt_x11,
	NULL /* END */
};

struct option *slurm_option_table_create(slurm_opt_t *opt,
					 char **opt_string)
{
	struct option *optz = optz_create(), *spanked;

	*opt_string = xstrdup("+");

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
			 && !common_options[i]->set_func_scron
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
			optz_add(&optz, (struct option *) common_options[i]);
		else if (opt->salloc_opt && common_options[i]->set_func_salloc)
			optz_add(&optz, (struct option *) common_options[i]);
		else if (opt->sbatch_opt && common_options[i]->set_func_sbatch)
			optz_add(&optz, (struct option *) common_options[i]);
		else if (opt->scron_opt && common_options[i]->set_func_scron)
			optz_add(&optz, (struct option *) common_options[i]);
		else if (opt->srun_opt && common_options[i]->set_func_srun)
			optz_add(&optz, (struct option *) common_options[i]);
		else
			set = false;

		if (set && (common_options[i]->val < LONG_OPT_ENUM_START)) {
			xstrfmtcat(*opt_string, "%c", common_options[i]->val);
			if (common_options[i]->has_arg == required_argument)
				xstrcat(*opt_string, ":");
			if (common_options[i]->has_arg == optional_argument)
				xstrcat(*opt_string, "::");
		}
	}

	spanked = spank_option_table_create(optz);
	optz_destroy(optz);

	return spanked;
}

void slurm_option_table_destroy(struct option *optz)
{
	optz_destroy(optz);
}

extern void slurm_free_options_members(slurm_opt_t *opt)
{
	if (!opt)
		return;

	slurm_reset_all_options(opt, true);

	xfree(opt->chdir);
	xfree(opt->state);
	xfree(opt->submit_line);
}

static void _init_state(slurm_opt_t *opt)
{
	if (opt->state)
		return;

	opt->state = xcalloc(sizeof(common_options),
			     sizeof(slurm_opt_state_t));
}

extern int slurm_process_option_data(slurm_opt_t *opt, int optval,
				     const data_t *arg, data_t *errors)
{
	int i;

	if (!opt)
		fatal("%s: missing slurm_opt_t struct", __func__);

	for (i = 0; common_options[i]; i++) {
		if (common_options[i]->val != optval)
			continue;

		/* Check that this is a valid match. */
		if (!common_options[i]->set_func_data)
			continue;

		/* Match found */
		break;
	}

	if (!common_options[i]) {
		char str[1024];
		snprintf(str, sizeof(str), "Unknown option: %u", optval);
		ADD_DATA_ERROR(str, SLURM_ERROR);
		return SLURM_ERROR;
	}

	// TODO: implement data aware spank parsing

	_init_state(opt);

	if (!(common_options[i]->set_func_data)(opt, arg, errors)) {
		opt->state[i].set = true;
		opt->state[i].set_by_data = true;
		opt->state[i].set_by_env = false;
		return SLURM_SUCCESS;
	}

	return SLURM_ERROR;
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
		    !(opt->scron_opt && common_options[i]->set_func_scron) &&
		    !(opt->srun_opt && common_options[i]->set_func_srun))
			continue;

		/* Match found */
		break;
	}

	/*
	 * Not a Slurm internal option, so hopefully it's a SPANK option.
	 * Skip this for early pass handling - SPANK options should only be
	 * processed once during the main pass.
	 */
	if (!common_options[i] && !early_pass) {
		if (spank_process_option(optval, arg))
			return SLURM_ERROR;
		return SLURM_SUCCESS;
	} else if (!common_options[i]) {
		/* early pass, assume it is a SPANK option and skip */
		return SLURM_SUCCESS;
	}

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
	} else if (!set_by_env && opt->srun_opt) {
		if (!early_pass && common_options[i]->srun_early_pass)
			return SLURM_SUCCESS;
		if (early_pass && !common_options[i]->srun_early_pass)
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

	_init_state(opt);

	if (!set) {
		(common_options[i]->reset_func)(opt);
		opt->state[i].set = false;
		opt->state[i].set_by_data = false;
		opt->state[i].set_by_env = false;
		return SLURM_SUCCESS;
	}

	if (common_options[i]->set_func) {
		if (!(common_options[i]->set_func)(opt, setarg)) {
			opt->state[i].set = true;
			opt->state[i].set_by_data = false;
			opt->state[i].set_by_env = set_by_env;
			return SLURM_SUCCESS;
		}
	} else if (opt->salloc_opt && common_options[i]->set_func_salloc) {
		if (!(common_options[i]->set_func_salloc)(opt, setarg)) {
			opt->state[i].set = true;
			opt->state[i].set_by_data = false;
			opt->state[i].set_by_env = set_by_env;
			return SLURM_SUCCESS;
		}
	} else if (opt->sbatch_opt && common_options[i]->set_func_sbatch) {
		if (!(common_options[i]->set_func_sbatch)(opt, setarg)) {
			opt->state[i].set = true;
			opt->state[i].set_by_data = false;
			opt->state[i].set_by_env = set_by_env;
			return SLURM_SUCCESS;
		}
	} else if (opt->scron_opt && common_options[i]->set_func_scron) {
		if (!(common_options[i]->set_func_scron)(opt, setarg)) {
			opt->state[i].set = true;
			opt->state[i].set_by_data = false;
			opt->state[i].set_by_env = set_by_env;
			return SLURM_SUCCESS;
		}
	} else if (opt->srun_opt && common_options[i]->set_func_srun) {
		if (!(common_options[i]->set_func_srun)(opt, setarg)) {
			opt->state[i].set = true;
			opt->state[i].set_by_data = false;
			opt->state[i].set_by_env = set_by_env;
			return SLURM_SUCCESS;
		}
	}

	return SLURM_ERROR;
}

void slurm_process_option_or_exit(slurm_opt_t *opt, int optval, const char *arg,
				  bool set_by_env, bool early_pass)
{
	if (slurm_process_option(opt, optval, arg, set_by_env, early_pass))
		exit(-1);
}

void slurm_print_set_options(slurm_opt_t *opt)
{
	if (!opt)
		fatal("%s: missing slurm_opt_t struct", __func__);

	info("defined options");
	info("-------------------- --------------------");

	for (int i = 0; common_options[i]; i++) {
		char *val = NULL;

		if (!opt->state || !opt->state[i].set)
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
			if (opt->state)
				opt->state[i].set = false;
		}
	}
}

/*
 * Was the option set by a cli argument?
 */
extern bool slurm_option_set_by_cli(slurm_opt_t *opt, int optval)
{
	int i;

	if (!opt) {
		debug3("%s: opt=NULL optval=%u", __func__, optval);
		return false;
	}

	for (i = 0; common_options[i]; i++) {
		if (common_options[i]->val == optval)
			break;
	}

	/* This should not happen... */
	if (!common_options[i])
		return false;

	if (!opt->state)
		return false;

	/*
	 * set is true if the option is set at all. If both set and set_by_env
	 * are true, then the argument was set through the environment not the
	 * cli, and we must return false.
	 */

	return (opt->state[i].set && !opt->state[i].set_by_env);
}

/*
 * Was the option set by an data_t value?
 */
extern bool slurm_option_set_by_data(slurm_opt_t *opt, int optval)
{
	int i;

	if (!opt) {
		debug3("%s: opt=NULL optval=%u", __func__, optval);
		return false;
	}

	for (i = 0; common_options[i]; i++) {
		if (common_options[i]->val == optval)
			break;
	}

	/* This should not happen... */
	if (!common_options[i])
		return false;

	if (!opt->state)
		return false;

	return opt->state[i].set_by_data;
}

/*
 * Was the option set by an env var?
 */
extern bool slurm_option_set_by_env(slurm_opt_t *opt, int optval)
{
	int i;

	if (!opt) {
		debug3("%s: opt=NULL optval=%u", __func__, optval);
		return false;
	}

	for (i = 0; common_options[i]; i++) {
		if (common_options[i]->val == optval)
			break;
	}

	/* This should not happen... */
	if (!common_options[i])
		return false;

	if (!opt->state)
		return false;

	return opt->state[i].set_by_env;
}

/*
 * Find the index into common_options for a given option name.
 */
static int _find_option_idx(const char *name)
{
	for (int i = 0; common_options[i]; i++)
		if (!xstrcmp(name, common_options[i]->name))
			return i;
	return -1;
}

/*
 * Get option value by common option name.
 */
extern char *slurm_option_get(slurm_opt_t *opt, const char *name)
{
	int i = _find_option_idx(name);
	if (i < 0)
		return NULL;
	return common_options[i]->get_func(opt);
}

/*
 * Is option set? Discover by common option name.
 */
extern bool slurm_option_isset(slurm_opt_t *opt, const char *name)
{
	int i = _find_option_idx(name);
	if (i < 0 || !opt->state)
		return false;
	return opt->state[i].set;
}

/*
 * Replace option value by common option name.
 */
extern int slurm_option_set(slurm_opt_t *opt, const char *name,
                             const char *value, bool early)
{
	int rc = SLURM_ERROR;
	int i = _find_option_idx(name);
	if (i < 0)
		return rc;

	/* Don't set early options if it is not early. */
	if (opt->sbatch_opt && common_options[i]->sbatch_early_pass && !early)
		return SLURM_SUCCESS;
	if (opt->srun_opt && common_options[i]->srun_early_pass && !early)
		return SLURM_SUCCESS;

	/* Run the appropriate set function. */
	if (common_options[i]->set_func)
		rc = common_options[i]->set_func(opt, value);
	else if (common_options[i]->set_func_salloc && opt->salloc_opt)
		rc = common_options[i]->set_func_salloc(opt, value);
	else if (common_options[i]->set_func_sbatch && opt->sbatch_opt)
		rc = common_options[i]->set_func_sbatch(opt, value);
	else if (common_options[i]->set_func_scron && opt->scron_opt)
		rc = common_options[i]->set_func_scron(opt, value);
	else if (common_options[i]->set_func_srun && opt->srun_opt)
		rc = common_options[i]->set_func_srun(opt, value);

	/* Ensure that the option shows up as "set". */
	if (rc == SLURM_SUCCESS) {
		_init_state(opt);
		opt->state[i].set = true;
	}

	return rc;
}

/*
 * Reset option by common option name.
 */
extern bool slurm_option_reset(slurm_opt_t *opt, const char *name)
{
	int i = _find_option_idx(name);
	if (i < 0)
		return false;
	common_options[i]->reset_func(opt);
	if (opt->state)
		opt->state[i].set = false;
	return true;
}

/*
 * Function for iterating through all the common option data structure
 * and returning (via parameter arguments) the name and value of each
 * set slurm option.
 *
 * IN opt	- option data structure being interpreted
 * OUT name	- xmalloc()'d string with the option name
 * OUT value	- xmalloc()'d string with the option value
 * IN/OUT state	- internal state, should be set to 0 for the first call
 * RETURNS      - true if name/value set; false if no more options
 */
extern bool slurm_option_get_next_set(slurm_opt_t *opt, char **name,
				      char **value, size_t *state)
{
	size_t limit = sizeof(common_options) / sizeof(slurm_cli_opt_t *);
	if (*state >= limit)
		return false;

	while (common_options[*state] && (*state < limit) &&
	       (!(opt->state && opt->state[*state].set) ||
		!common_options[*state]->name))
		(*state)++;

	if (*state < limit && common_options[*state]) {
		*name = xstrdup(common_options[*state]->name);
		*value = common_options[*state]->get_func(opt);
		(*state)++;
		return true;
	}
	return false;
}

/*
 * Validate that the three memory options (--mem, --mem-per-cpu, --mem-per-gpu)
 * and their associated environment variables are set mutually exclusively.
 *
 * This will fatal() if multiple CLI options are specified simultaneously.
 * If any of the CLI options are specified, the other options are reset to
 * clear anything that may have been set through the environment.
 * Otherwise, if multiple environment variables are set simultaneously,
 * this will fatal().
 */
static void _validate_memory_options(slurm_opt_t *opt)
{
	if ((slurm_option_set_by_cli(opt, LONG_OPT_MEM) +
	     slurm_option_set_by_cli(opt, LONG_OPT_MEM_PER_CPU) +
	     slurm_option_set_by_cli(opt, LONG_OPT_MEM_PER_GPU)) > 1) {
		fatal("--mem, --mem-per-cpu, and --mem-per-gpu are mutually exclusive.");
	} else if (slurm_option_set_by_cli(opt, LONG_OPT_MEM)) {
		slurm_option_reset(opt, "mem-per-cpu");
		slurm_option_reset(opt, "mem-per-gpu");
	} else if (slurm_option_set_by_cli(opt, LONG_OPT_MEM_PER_CPU)) {
		slurm_option_reset(opt, "mem");
		slurm_option_reset(opt, "mem-per-gpu");
	} else if (slurm_option_set_by_cli(opt, LONG_OPT_MEM_PER_GPU)) {
		slurm_option_reset(opt, "mem");
		slurm_option_reset(opt, "mem-per-cpu");
	} else if ((slurm_option_set_by_env(opt, LONG_OPT_MEM) +
		    slurm_option_set_by_env(opt, LONG_OPT_MEM_PER_CPU) +
		    slurm_option_set_by_env(opt, LONG_OPT_MEM_PER_GPU)) > 1) {
		fatal("SLURM_MEM_PER_CPU, SLURM_MEM_PER_GPU, and SLURM_MEM_PER_NODE are mutually exclusive.");
	}
}

static void _validate_threads_per_core_option(slurm_opt_t *opt)
{
	if (!slurm_option_isset(opt, "threads-per-core"))
		return;

	if (!slurm_option_isset(opt, "cpu-bind")) {
		verbose("Setting --cpu-bind=threads as a default of --threads-per-core use");
		if (opt->srun_opt)
			slurm_verify_cpu_bind("threads",
					      &opt->srun_opt->cpu_bind,
					      &opt->srun_opt->cpu_bind_type);
	} else if (opt->srun_opt &&
		   (opt->srun_opt->cpu_bind_type == CPU_BIND_VERBOSE)) {
		verbose("Setting --cpu-bind=threads,verbose as a default of --threads-per-core use");
		if (opt->srun_opt)
			slurm_verify_cpu_bind("threads,verbose",
					      &opt->srun_opt->cpu_bind,
					      &opt->srun_opt->cpu_bind_type);
	} else {
		debug3("Not setting --cpu-bind=threads because of --threads-per-core since --cpu-bind already set by cli option or environment variable");
	}
}

extern int validate_hint_option(slurm_opt_t *opt)
{
	if (slurm_option_set_by_cli(opt, LONG_OPT_HINT) &&
	    ((slurm_option_set_by_cli(opt, LONG_OPT_NTASKSPERCORE) ||
	      slurm_option_set_by_cli(opt, LONG_OPT_THREADSPERCORE) ||
	      slurm_option_set_by_cli(opt, 'B') ||
	      (slurm_option_set_by_cli(opt, LONG_OPT_CPU_BIND) &&
	       (opt->srun_opt->cpu_bind_type & ~CPU_BIND_VERBOSE))))) {
		if (opt->verbose)
			info("Following options are mutually exclusive with --hint: --ntasks-per-core, --threads-per-core, -B and --cpu-bind (other then --cpu-bind=verbose). Ignoring --hint.");
		slurm_option_reset(opt, "hint");
		return SLURM_ERROR;
	} else if (slurm_option_set_by_cli(opt, LONG_OPT_HINT)) {
		slurm_option_reset(opt, "ntasks-per-core");
		slurm_option_reset(opt, "threads-per-core");
		slurm_option_reset(opt, "extra-node-info");
		slurm_option_reset(opt, "cpu-bind");
	} else if (slurm_option_set_by_cli(opt, LONG_OPT_NTASKSPERCORE) ||
		   slurm_option_set_by_cli(opt, LONG_OPT_THREADSPERCORE) ||
		   slurm_option_set_by_cli(opt, 'B') ||
		   slurm_option_set_by_cli(opt, LONG_OPT_CPU_BIND)) {
		slurm_option_reset(opt, "hint");
		return SLURM_ERROR;
	} else if (slurm_option_set_by_env(opt, LONG_OPT_HINT) &&
		   (slurm_option_set_by_env(opt, LONG_OPT_NTASKSPERCORE) ||
		    slurm_option_set_by_env(opt, LONG_OPT_THREADSPERCORE) ||
		    slurm_option_set_by_env(opt, 'B') ||
		    slurm_option_set_by_env(opt, LONG_OPT_CPU_BIND))) {
		if (opt->verbose)
			info("Following options are mutually exclusive with --hint: --ntasks-per-core, --threads-per-core, -B and --cpu-bind, but more than one set by environment variables. Ignoring SLURM_HINT.");
		slurm_option_reset(opt, "hint");
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

static void _validate_ntasks_per_gpu(slurm_opt_t *opt)
{
	bool tres = slurm_option_set_by_cli(opt, LONG_OPT_NTASKSPERTRES);
	bool gpu = slurm_option_set_by_cli(opt, LONG_OPT_NTASKSPERGPU);
	bool tres_env = slurm_option_set_by_env(opt, LONG_OPT_NTASKSPERTRES);
	bool gpu_env = slurm_option_set_by_env(opt, LONG_OPT_NTASKSPERGPU);
	bool any = (tres || gpu || tres_env || gpu_env);

	if (!any)
		return;

	/* Validate --ntasks-per-gpu and --ntasks-per-gpu */
	if (gpu && tres) {
		if (opt->ntasks_per_gpu != opt->ntasks_per_tres)
			fatal("Inconsistent values set to --ntasks-per-gpu=%d and --ntasks-per-tres=%d ",
			      opt->ntasks_per_gpu,
			      opt->ntasks_per_tres);
	} else if (gpu && tres_env) {
		if (opt->verbose)
			info("Ignoring SLURM_NTASKS_PER_TRES since --ntasks-per-gpu given as command line option");
		slurm_option_reset(opt, "ntasks-per-tres");
	} else if (tres && gpu_env) {
		if (opt->verbose)
			info("Ignoring SLURM_NTASKS_PER_GPU since --ntasks-per-tres given as command line option");
		slurm_option_reset(opt, "ntasks-per-gpu");
	} else if (gpu_env && tres_env) {
		if (opt->ntasks_per_gpu != opt->ntasks_per_tres)
			fatal("Inconsistent values set by environment variables SLURM_NTASKS_PER_GPU=%d and SLURM_NTASKS_PER_TRES=%d ",
			      opt->ntasks_per_gpu,
			      opt->ntasks_per_tres);
	}

	if (slurm_option_set_by_cli(opt, LONG_OPT_GPUS_PER_TASK))
		fatal("--gpus-per-task is mutually exclusive with --ntasks-per-gpu and SLURM_NTASKS_PER_GPU");

	if (slurm_option_set_by_env(opt, LONG_OPT_GPUS_PER_TASK))
		fatal("SLURM_GPUS_PER_TASK is mutually exclusive with --ntasks-per-gpu and SLURM_NTASKS_PER_GPU");

	if (slurm_option_set_by_cli(opt, LONG_OPT_GPUS_PER_SOCKET))
		fatal("--gpus-per-socket is mutually exclusive with --ntasks-per-gpu and SLURM_NTASKS_PER_GPU");

	if (slurm_option_set_by_env(opt, LONG_OPT_GPUS_PER_SOCKET))
		fatal("SLURM_GPUS_PER_SOCKET is mutually exclusive with --ntasks-per-gpu and SLURM_NTASKS_PER_GPU");

	if (slurm_option_set_by_cli(opt, LONG_OPT_NTASKSPERNODE))
		fatal("--ntasks-per-node is mutually exclusive with --ntasks-per-gpu and SLURM_NTASKS_PER_GPU");

	if (slurm_option_set_by_env(opt, LONG_OPT_NTASKSPERNODE))
		fatal("SLURM_NTASKS_PER_NODE is mutually exclusive with --ntasks-per-gpu and SLURM_NTASKS_PER_GPU");
}

static void _validate_spec_cores_options(slurm_opt_t *opt)
{
	if (!slurm_option_isset(opt, "thread-spec") &&
	    !slurm_option_isset(opt, "core-spec"))
		return;

	if ((slurm_option_set_by_cli(opt, 'S') +
	     slurm_option_set_by_cli(opt, LONG_OPT_THREAD_SPEC)) > 1)
		fatal("-S/--core-spec and --thred-spec options are mutually exclusive");
	else if (((slurm_option_set_by_env(opt, 'S') +
		   slurm_option_set_by_env(opt, LONG_OPT_THREAD_SPEC)) > 1) &&
		 (((slurm_option_set_by_cli(opt, 'S') +
		    slurm_option_set_by_cli(opt, LONG_OPT_THREAD_SPEC))) == 0))
		fatal("Both --core-spec and --thread-spec set using environment variables. Those options are mutually exclusive.");

	if (!(slurm_conf.conf_flags & CTL_CONF_ASRU)) {
		error("Ignoring %s since it's not allowed by configuration (AllowSpecResourcesUsage = No)",
		      (opt->core_spec & CORE_SPEC_THREAD) ?
		      "--thread-spec":"-S");
	}
}

/* Validate shared options between srun, salloc, and sbatch */
extern void validate_options_salloc_sbatch_srun(slurm_opt_t *opt)
{
	_validate_ntasks_per_gpu(opt);
	_validate_spec_cores_options(opt);
	_validate_threads_per_core_option(opt);
	_validate_memory_options(opt);
}

extern char *slurm_option_get_argv_str(const int argc, char **argv)
{
	char *submit_line;

	if (!argv || !argv[0])
		fatal("%s: no argv given", __func__);

	submit_line = xstrdup(argv[0]);

	for (int i = 1; i < argc; i++)
		xstrfmtcat(submit_line, " %s", argv[i]);

	return submit_line;
}

extern job_desc_msg_t *slurm_opt_create_job_desc(slurm_opt_t *opt_local,
						 bool set_defaults)
{
	job_desc_msg_t *job_desc = xmalloc_nz(sizeof(*job_desc));
	List tmp_gres_list = NULL;
	int rc;

	slurm_init_job_desc_msg(job_desc);

	job_desc->account = xstrdup(opt_local->account);
	job_desc->acctg_freq = xstrdup(opt_local->acctg_freq);

	/* admin_comment not filled in here */
	/* alloc_node not filled in here */
	/* alloc_resp_port not filled in here */
	/* alloc_sid not filled in here */
	/* arg[c|v] not filled in here */
	/* array_inx not filled in here */
	/* array_bitmap not filled in here */
	/* batch_features not filled in here */

	job_desc->begin_time = opt_local->begin;
	job_desc->bitflags |= opt_local->job_flags;
	job_desc->burst_buffer = xstrdup(opt_local->burst_buffer);
	job_desc->clusters = xstrdup(opt_local->clusters);
	job_desc->cluster_features = xstrdup(opt_local->c_constraint);
	job_desc->comment = xstrdup(opt_local->comment);
	job_desc->req_context = xstrdup(opt_local->context);

	if (set_defaults || slurm_option_isset(opt_local, "contiguous"))
		job_desc->contiguous = opt_local->contiguous;
	else
		job_desc->contiguous = NO_VAL16;

	if (opt_local->core_spec != NO_VAL16)
		job_desc->core_spec = opt_local->core_spec;

	/* cpu_bind not filled in here */
	/* cpu_bind_type not filled in here */

	job_desc->cpu_freq_min = opt_local->cpu_freq_min;
	job_desc->cpu_freq_max = opt_local->cpu_freq_max;
	job_desc->cpu_freq_gov = opt_local->cpu_freq_gov;

	if (opt_local->cpus_per_gpu)
		xstrfmtcat(job_desc->cpus_per_tres, "gres:gpu:%d",
			   opt_local->cpus_per_gpu);

	/* crontab_entry not filled in here */

	job_desc->deadline = opt_local->deadline;

	if (opt_local->delay_boot != NO_VAL)
		job_desc->delay_boot = opt_local->delay_boot;

	job_desc->dependency = xstrdup(opt_local->dependency);

	/* end_time not filled in here */
	/* environment not filled in here */
	/* env_size not filled in here */

	job_desc->extra = xstrdup(opt_local->extra);
	job_desc->exc_nodes = xstrdup(opt_local->exclude);
	job_desc->features = xstrdup(opt_local->constraint);

	/* fed_siblings_active not filled in here */
	/* fed_siblings_viable not filled in here */

	job_desc->group_id = opt_local->gid;

	/* het_job_offset not filled in here */

	if (opt_local->immediate == 1)
		job_desc->immediate = 1;

	/* job_id not filled in here */
	/* job_id_str not filled in here */

	if (opt_local->no_kill)
		job_desc->kill_on_node_fail = 0;

	job_desc->licenses = xstrdup(opt_local->licenses);

	if (set_defaults || slurm_option_isset(opt_local, "mail_type"))
		job_desc->mail_type = opt_local->mail_type;

	job_desc->mail_user = xstrdup(opt_local->mail_user);

	job_desc->mcs_label = xstrdup(opt_local->mcs_label);

	job_desc->mem_bind = xstrdup(opt_local->mem_bind);
	job_desc->mem_bind_type = opt_local->mem_bind_type;

	if (opt_local->mem_per_gpu != NO_VAL64)
		xstrfmtcat(job_desc->mem_per_tres, "gres:gpu:%"PRIu64,
			   opt_local->mem_per_gpu);

	if (set_defaults || slurm_option_isset(opt_local, "name"))
		job_desc->name = xstrdup(opt_local->job_name);

	job_desc->network = xstrdup(opt_local->network);

	if (opt_local->nice != NO_VAL)
		job_desc->nice = NICE_OFFSET + opt_local->nice;

	if (opt_local->ntasks_set) {
		job_desc->bitflags |= JOB_NTASKS_SET;
		job_desc->num_tasks = opt_local->ntasks;
	}

	if (opt_local->open_mode)
		job_desc->open_mode = opt_local->open_mode;

	/* origin_cluster is not filled in here */
	/* other_port not filled in here */

	if (opt_local->overcommit) {
		if (set_defaults || (opt_local->min_nodes > 0))
			job_desc->min_cpus = MAX(opt_local->min_nodes, 1);
		job_desc->overcommit = opt_local->overcommit;
	} else if (opt_local->cpus_set)
		job_desc->min_cpus =
			opt_local->ntasks * opt_local->cpus_per_task;
	else if (opt_local->nodes_set && (opt_local->min_nodes == 0))
		job_desc->min_cpus = 0;
	else if (set_defaults)
		job_desc->min_cpus = opt_local->ntasks;

	job_desc->partition = xstrdup(opt_local->partition);

	if (opt_local->plane_size != NO_VAL)
		job_desc->plane_size = opt_local->plane_size;

	job_desc->power_flags = opt_local->power;

	if (slurm_option_isset(opt_local, "hold")) {
		if (opt_local->hold)
			job_desc->priority = 0;
		else
			job_desc->priority = INFINITE;
	} else if (opt_local->priority)
		job_desc->priority = opt_local->priority;

	job_desc->profile = opt_local->profile;

	job_desc->qos = xstrdup(opt_local->qos);

	if (opt_local->reboot)
		job_desc->reboot = 1;

	/* resp_host not filled in here */
	/* restart_cnt not filled in here */

	/*
	 * simplify the job allocation nodelist, not laying out tasks until step
	 */
	if (opt_local->nodelist) {
		hostlist_t hl = hostlist_create(opt_local->nodelist);
		xfree(opt_local->nodelist);
		opt_local->nodelist = hostlist_ranged_string_xmalloc(hl);
		hostlist_uniq(hl);
		job_desc->req_nodes = hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);
	}

	if (((opt_local->distribution & SLURM_DIST_STATE_BASE) ==
	     SLURM_DIST_ARBITRARY) && !job_desc->req_nodes) {
		error("With Arbitrary distribution you need to "
		      "specify a nodelist or hostfile with the -w option");
		return NULL;
	}

	/* requeue not filled in here */

	job_desc->reservation = xstrdup(opt_local->reservation);

	/* script not filled in here */
	/* script_buf not filled in here */

	if (opt_local->shared != NO_VAL16)
		job_desc->shared = opt_local->shared;

	/* site_factor not filled in here */

	if (opt_local->spank_job_env_size) {
		job_desc->spank_job_env =
			xcalloc(opt_local->spank_job_env_size,
				sizeof(*job_desc->spank_job_env));
		for (int i = 0; i < opt_local->spank_job_env_size; i++)
			job_desc->spank_job_env[i] =
				xstrdup(opt_local->spank_job_env[i]);
		job_desc->spank_job_env_size = opt_local->spank_job_env_size;
	}

	job_desc->submit_line = opt_local->submit_line;
	job_desc->task_dist = opt_local->distribution;

	if (opt_local->time_limit != NO_VAL)
		job_desc->time_limit = opt_local->time_limit;
	if (opt_local->time_min != NO_VAL)
		job_desc->time_min = opt_local->time_min;

	job_desc->tres_bind = xstrdup(opt_local->tres_bind);
	job_desc->tres_freq = xstrdup(opt_local->tres_freq);
	xfmt_tres(&job_desc->tres_per_job, "gres:gpu", opt_local->gpus);
	xfmt_tres(&job_desc->tres_per_node, "gres:gpu",
		  opt_local->gpus_per_node);
	/* --gres=none for jobs means no GRES, so don't send it to slurmctld */
	if (opt_local->gres && xstrcasecmp(opt_local->gres, "NONE")) {
		if (job_desc->tres_per_node)
			xstrfmtcat(job_desc->tres_per_node, ",%s",
				   opt_local->gres);
		else
			job_desc->tres_per_node = xstrdup(opt_local->gres);
	}
	xfmt_tres(&job_desc->tres_per_socket, "gres:gpu",
		  opt_local->gpus_per_socket);
	xfmt_tres(&job_desc->tres_per_task, "gres:gpu",
		  opt_local->gpus_per_task);

	job_desc->user_id = opt_local->uid;

	/* wait_all_nodes not filled in here */

	job_desc->warn_flags = opt_local->warn_flags;
	job_desc->warn_signal = opt_local->warn_signal;
	job_desc->warn_time = opt_local->warn_time;

	if (set_defaults || slurm_option_isset(opt_local, "chdir"))
		job_desc->work_dir = xstrdup(opt_local->chdir);

	if (opt_local->cpus_set) {
		job_desc->bitflags |= JOB_CPUS_SET;
		job_desc->cpus_per_task = opt_local->cpus_per_task;
	}

	/* max_cpus not filled in here */

	if (opt_local->nodes_set) {
		job_desc->min_nodes = opt_local->min_nodes;
		if (opt_local->max_nodes)
			job_desc->max_nodes = opt_local->max_nodes;
	} else if (opt_local->ntasks_set && (opt_local->ntasks == 0))
		job_desc->min_nodes = 0;

	/* boards_per_node not filled in here */
	/* sockets_per_board not filled in here */

	if (opt_local->sockets_per_node != NO_VAL)
		job_desc->sockets_per_node = opt_local->sockets_per_node;
	if (opt_local->cores_per_socket != NO_VAL)
		job_desc->cores_per_socket = opt_local->cores_per_socket;
	if (opt_local->threads_per_core != NO_VAL)
		job_desc->threads_per_core = opt_local->threads_per_core;

	if (opt_local->ntasks_per_node != NO_VAL)
		job_desc->ntasks_per_node = opt_local->ntasks_per_node;
	if (opt_local->ntasks_per_socket != NO_VAL)
		job_desc->ntasks_per_socket = opt_local->ntasks_per_socket;
	if (opt_local->ntasks_per_core != NO_VAL)
		job_desc->ntasks_per_core = opt_local->ntasks_per_core;

	/* ntasks_per_board not filled in here */

	if (opt_local->ntasks_per_tres != NO_VAL)
		job_desc->ntasks_per_tres = opt_local->ntasks_per_tres;
	else if (opt_local->ntasks_per_gpu != NO_VAL)
		job_desc->ntasks_per_tres = opt_local->ntasks_per_gpu;

	if (opt_local->pn_min_cpus > -1)
		job_desc->pn_min_cpus = opt_local->pn_min_cpus;

	if (opt_local->pn_min_memory != NO_VAL64)
		job_desc->pn_min_memory = opt_local->pn_min_memory;
	else if (opt_local->mem_per_cpu != NO_VAL64)
		job_desc->pn_min_memory = opt_local->mem_per_cpu | MEM_PER_CPU;

	if (opt_local->pn_min_tmp_disk != NO_VAL64)
		job_desc->pn_min_tmp_disk = opt_local->pn_min_tmp_disk;

	if (opt_local->req_switch >= 0)
		job_desc->req_switch = opt_local->req_switch;

	/* select_jobinfo not filled in here */
	/* desc->std_[err|in|out] not filled in here */
	/* tres_req_cnt not filled in here */

	if (opt_local->wait4switch >= 0)
		job_desc->wait4switch = opt_local->wait4switch;

	job_desc->wckey = xstrdup(opt_local->wckey);

	job_desc->x11 = opt_local->x11;
	if (job_desc->x11) {
		job_desc->x11_magic_cookie =
			xstrdup(opt_local->x11_magic_cookie);
		job_desc->x11_target = xstrdup(opt_local->x11_target);
		job_desc->x11_target_port = opt_local->x11_target_port;
	}

	rc = gres_job_state_validate(job_desc->cpus_per_tres,
				     job_desc->tres_freq,
				     job_desc->tres_per_job,
				     job_desc->tres_per_node,
				     job_desc->tres_per_socket,
				     job_desc->tres_per_task,
				     job_desc->mem_per_tres,
				     &job_desc->num_tasks,
				     &job_desc->min_nodes,
				     &job_desc->max_nodes,
				     &job_desc->ntasks_per_node,
				     &job_desc->ntasks_per_socket,
				     &job_desc->sockets_per_node,
				     &job_desc->cpus_per_task,
				     &job_desc->ntasks_per_tres,
				     &tmp_gres_list);
	FREE_NULL_LIST(tmp_gres_list);
	if (rc) {
		error("%s", slurm_strerror(rc));
		return NULL;
	}

	return job_desc;
}
