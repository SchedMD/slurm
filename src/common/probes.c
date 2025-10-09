/*****************************************************************************\
 *  probes.c - Daemon liveness and readiness probes
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include <stdarg.h>
#include <stddef.h>

#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/probes.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#define PROBE_MAGIC 0x3afabfaf

typedef struct {
	int magic; /* PROBE_MAGIC */
	const char *name;
	probe_query_t query;
} probe_t;

#define PROBE_RUN_MAGIC 0xaaa3bfa9

typedef struct {
	int magic; /* PROBE_RUN_MAGIC */
	const char *caller;
	const char *name;
	buf_t *output;
	probe_status_t status; /* Lowest status encountered */
	const bool verbose;
} probe_run_t;

#define PROBE_LOG_MAGIC 0xfff3bfa9

typedef struct probe_log_s {
	int magic; /* PROBE_LOG_MAGIC */
	const probe_t *probe;
	probe_run_t *run;
} probe_log_t;

/* list_t* of probe_t* */
static list_t *probes = NULL;

static void _free_probe(void *ptr)
{
	probe_t *probe = ptr;

	xassert(probe->magic == PROBE_MAGIC);
	probe->magic = ~PROBE_MAGIC;
	xfree(probe);
}

extern void probe_init(void)
{
	xassert(!probes);

	probes = list_create(_free_probe);
}

extern void probe_fini(void)
{
	FREE_NULL_LIST(probes);
}

extern void probe_register(const char *name, probe_query_t query)
{
	probe_t *probe = NULL;

	if (!probes)
		return;

	probe = xmalloc(sizeof(*probe));
	*probe = (probe_t) {
		.magic = PROBE_MAGIC,
		.name = name,
		.query = query,
	};

	xassert(name && name[0]);
	xassert(query);

	list_append(probes, probe);
}

static int _run(void *x, void *arg)
{
	probe_t *probe = x;
	probe_run_t *run = arg;
	probe_status_t status = PROBE_RC_INVALID;

	xassert(probe->magic == PROBE_MAGIC);
	xassert(run->magic == PROBE_RUN_MAGIC);

	if (run->verbose) {
		probe_log_t log = {
			.magic = PROBE_LOG_MAGIC,
			.probe = probe,
			.run = run,
		};

		status = probe->query(&log);
	} else {
		status = probe->query(NULL);
	}

	xassert(status > PROBE_RC_INVALID);
	xassert(status < PROBE_RC_INVALID_MAX);

	if (status < run->status)
		run->status = status;

	return SLURM_SUCCESS;
}

extern probe_status_t probe_run(bool verbose, const char *name, buf_t *output,
				const char *caller)
{
	probe_run_t run = {
		.magic = PROBE_RUN_MAGIC,
		.name = name,
		.output = output,
		.verbose = verbose,
		.status = PROBE_RC_INVALID_MAX,
		.caller = caller,
	};

	(void) list_for_each_ro(probes, _run, &run);

	xassert(run.status > PROBE_RC_INVALID);
	xassert(run.status <= PROBE_RC_INVALID_MAX);

	if ((run.status <= PROBE_RC_INVALID) ||
	    (run.status > PROBE_RC_INVALID_MAX))
		return PROBE_RC_UNKNOWN;

	return run.status;
}

extern void probe_logger(probe_log_t *log, const char *caller, const char *fmt,
			 ...)
{
	va_list ap;
	char *str = NULL;
	const probe_t *probe = log->probe;
	probe_run_t *run = log->run;
	buf_t *output = run->output;
	int bytes = -1;

	xassert(log->magic == PROBE_LOG_MAGIC);

	if (!run->verbose)
		return;

	xassert(fmt && fmt[0]);

	va_start(ap, fmt);
	str = vxstrfmt(fmt, ap);
	va_end(ap);

	if (!(bytes = strlen(str))) {
		/* do nothing when nothing is getting logged */
	} else if (output && !try_grow_buf_remaining(output, (bytes + 2))) {
		uint8_t *dst = (((void *) get_buf_data(output)) +
				get_buf_offset(output));

		xassert(output->magic == BUF_MAGIC);

		memcpy(dst, str, bytes);
		dst[bytes] = '\n';
		dst[bytes + 1] = '\0';

		set_buf_offset(output, (get_buf_offset(output) + bytes + 1));
	} else {
		info("%s->%s->%s: [%s] %s",
		     run->caller, caller, __func__, probe->name, str);
	}

	xfree(str);
}
