/*****************************************************************************\
 *  src/common/job_options.c  - Extra job options
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "slurm/slurm.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/common/job_options.h"

#define JOB_OPTIONS_PACK_TAG "job_options"

#define JOB_OPTIONS_MAGIC 0xa1a2a3a4
struct job_options {
	int magic;
	List options;
	ListIterator iterator;
};


static struct job_option_info *
job_option_info_create (int type, const char *opt, const char *optarg)
{
	struct job_option_info *ji = xmalloc (sizeof (*ji));

	ji->type =   type;
	ji->option = xstrdup (opt);
	ji->optarg = xstrdup(optarg);

	return (ji);
}

static void job_option_info_destroy (struct job_option_info *ji)
{
	xfree (ji->option);
	xfree (ji->optarg);
	ji->type = -1;
	xfree (ji);
	return;
}

static void job_option_info_pack(struct job_option_info *ji, buf_t *buf)
{
	pack32  (ji->type, buf);
	packstr (ji->option, buf);
	packstr (ji->optarg, buf); /* packstr() handles NULL optarg */
	return;
}

static struct job_option_info *job_option_info_unpack(buf_t *buf)
{
	struct job_option_info *ji = xmalloc (sizeof (*ji));
	uint32_t type;
	uint32_t len;

	safe_unpack32 (&type, buf);
	safe_unpackstr_xmalloc (&ji->option, &len, buf);
	safe_unpackstr_xmalloc (&ji->optarg, &len, buf);

	ji->type = (int) type;
	return (ji);

  unpack_error:
	job_option_info_destroy (ji);
	return (NULL);
}


/*
 *  Create generic job options container.
 */
List job_options_create(void)
{
	return list_create((ListDelF) job_option_info_destroy);
}

/*
 *  Append option of type `type' and its argument to job options
 */
int job_options_append(List opts, int type, const char *opt, const char *optarg)
{
	list_append(opts, job_option_info_create(type, opt, optarg));

	return (0);
}

/*
 *  Pack all accumulated options into buf
 */
int job_options_pack(List opts, buf_t *buf)
{
	uint32_t count = 0;
	ListIterator i;
	struct job_option_info *opt;

	packstr (JOB_OPTIONS_PACK_TAG, buf);

	if (opts == NULL) {
		pack32  (0, buf);
		return (0);
	}

	count = list_count(opts);
	pack32  (count, buf);

	i = list_iterator_create (opts);

	while ((opt = list_next (i)))
		job_option_info_pack (opt, buf);
	list_iterator_destroy (i);

	return (count);
}

/*
 *  Unpack options from buffer "buf" into options container opts.
 */
int job_options_unpack(List opts, buf_t *buf)
{
	uint32_t count;
	uint32_t len;
	char *   tag = NULL;
	int      i;

	safe_unpackstr_xmalloc (&tag, &len, buf);

	if (xstrncmp (tag, JOB_OPTIONS_PACK_TAG, len) != 0) {
		xfree(tag);
		return (-1);
	}
	xfree(tag);
	safe_unpack32 (&count, buf);

	for (i = 0; i < count; i++) {
		struct job_option_info *ji;
		if ((ji = job_option_info_unpack (buf)) == NULL)
			return (SLURM_ERROR);
		list_append(opts, ji);
	}

	return (0);

  unpack_error:
	xfree(tag);
	return SLURM_ERROR;
}
