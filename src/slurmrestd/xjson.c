/*****************************************************************************\
 *  json.c - definitions for json messages
 *****************************************************************************
 *  Copyright (C) 2019-2021 SchedMD LLC.
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

#include "config.h"

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/xjson.h"

extern data_t *parse_json(const char *buf, size_t len)
{
	int rc;
	data_t *d = NULL;

	if ((rc = data_g_deserialize(&d, buf, len, MIME_TYPE_JSON)))
		return NULL;

	return d;
}

extern char *dump_json(const data_t *data, dump_json_flags_t flags)
{
	int rc;
	char *d = NULL;
	data_serializer_flags_t f = DATA_SER_FLAGS_NONE;

	if (flags == DUMP_JSON_FLAGS_COMPACT)
		f = DATA_SER_FLAGS_COMPACT;
	else if (flags == DUMP_JSON_FLAGS_PRETTY)
		f = DATA_SER_FLAGS_PRETTY;

	if ((rc = data_g_serialize(&d, data, MIME_TYPE_JSON, f)))
		return NULL;

	return d;
}
