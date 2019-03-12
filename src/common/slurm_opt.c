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

#include "src/common/log.h"
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
	&slurm_opt_qos,
	NULL /* END */
};

int slurm_process_option(slurm_opt_t *opt, int optval, const char *arg)
{
	int i, rc = SLURM_ERROR;

	if (!opt)
		fatal("%s: missing slurm_opt_t struct", __func__);

	for (i = 0; common_options[i]; i++) {
		if (common_options[i]->val == optval) {
			rc = (common_options[i]->set_func)(opt, arg);
			break;
		}
	}

	if (!rc) {
		common_options[i]->set = true;
		return SLURM_SUCCESS;
	}

	return SLURM_ERROR;
}
