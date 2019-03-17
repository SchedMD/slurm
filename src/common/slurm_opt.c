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
	bool reset_each_pass;	/* Reset on all HetJob passes or only first */
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

static slurm_cli_opt_t *common_options[] = {
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

int slurm_process_option(slurm_opt_t *opt, int optval, const char *arg)
{
	int i;
	const char *setarg = arg;
	bool set = true;

	if (!opt)
		fatal("%s: missing slurm_opt_t struct", __func__);

	for (i = 0; common_options[i]; i++) {
		if (common_options[i]->val == optval)
			break;
	}

	if (!common_options[i])
		return SLURM_ERROR;

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

	if (set) {
		if (!(common_options[i]->set_func)(opt, setarg)) {
			common_options[i]->set = true;
			return SLURM_SUCCESS;
		}
	} else {
		(common_options[i]->reset_func)(opt);
		common_options[i]->set = false;
		return SLURM_SUCCESS;
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
