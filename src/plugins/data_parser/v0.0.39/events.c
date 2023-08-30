/*****************************************************************************\
 *  events.c - Slurm data parsing events
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
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

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "api.h"
#include "events.h"
#include "parsers.h"
#include "parsing.h"

extern int on_error(parse_op_t op, data_parser_type_t type, args_t *args,
		    int error_code, const char *source, const char *caller,
		    const char *why, ...)
{
	const parser_t *const parser = find_parser_by_type(type);
	va_list ap;
	char *str;
	bool cont;
	int errno_backup = errno;

	xassert((op == PARSING) || (op == DUMPING) || (op == QUERYING));
	xassert(type > DATA_PARSER_TYPE_INVALID);
	xassert(type < DATA_PARSER_TYPE_MAX);
	xassert(parser && (parser->type == type));
	xassert(args);
	xassert(error_code != SLURM_SUCCESS);
	xassert(caller && caller[0]);
	xassert(why && why[0]);

	va_start(ap, why);
	str = vxstrfmt(why, ap);
	va_end(ap);

	/*
	 * All parsing must be providing the source path with starts with a /.
	 * This makes it sooo much easier for a client to figure out what they
	 * are incorrectly submitting to Slurm.
	 */
	xassert((op != PARSING) ||
		(source && (source[0] == OPENAPI_PATH_REL[0]) &&
		 (source[1] == OPENAPI_PATH_SEP[0])));
	cont = args->on_parse_error(args->error_arg, type, error_code,
				    source, "%s", str);

	debug2("%s->%s->%s continue=%c type=%s return_code[%u]=%s why=%s",
	       caller, source, __func__, (cont ? 'T' : 'F'),
	       parser->type_string, error_code, slurm_strerror(error_code),
	       str);

	/* never clobber errno */
	errno = errno_backup;

	xfree(str);
	return cont ? SLURM_SUCCESS : error_code;
}

extern void on_warn(parse_op_t op, data_parser_type_t type, args_t *args,
		    const char *source, const char *caller, const char *why,
		    ...)
{
	const parser_t *const parser = find_parser_by_type(type);
	va_list ap;
	char *str;
	int errno_backup = errno;

	xassert((op == PARSING) || (op == DUMPING) || (op == QUERYING));
	xassert(type > DATA_PARSER_TYPE_INVALID);
	xassert(type < DATA_PARSER_TYPE_MAX);
	xassert(parser && (parser->type == type));
	xassert(args);
	xassert(caller && caller[0]);
	xassert(why && why[0]);

	va_start(ap, why);
	str = vxstrfmt(why, ap);
	va_end(ap);

	/*
	 * All parsing must be providing the source path with starts with a /.
	 * This makes it sooo much easier for a client to figure out what they
	 * are incorrectly submitting to Slurm.
	 */
	xassert((op != PARSING) ||
		(source && (source[0] == OPENAPI_PATH_REL[0]) &&
		 (source[1] == OPENAPI_PATH_SEP[0])));
	args->on_parse_warn(args->warn_arg, type, source, "%s", str);

	debug2("%s->%s->%s type=%s why=%s", caller, source, __func__,
	       parser->type_string, str);

	/* never clobber errno */
	errno = errno_backup;

	xfree(str);
}
