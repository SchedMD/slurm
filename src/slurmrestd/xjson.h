/*****************************************************************************\
 *  json.h - definitions for json messages
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
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

#ifndef _XJSON_H
#define _XJSON_H

#include "src/common/data.h"

/*
 * Read JSON formatted buffer.
 * IN buf string buffer containing json formatted data
 * RET structured data or NULL on error
 */
extern data_t *parse_json(const char *buf, size_t len);

/*
 * Define flags for JSON options.
 *
 * Leaving ability to add more flags later to allow for new formats
 * and different JSON library.
 */
typedef enum {
	DUMP_JSON_FLAGS_NONE = 0, /* defaults to compact currently */
	DUMP_JSON_FLAGS_COMPACT = 1 << 1,
	DUMP_JSON_FLAGS_PRETTY = 1 << 2,
} dump_json_flags_t;

/*
 * Read JSON formatted buffer
 * IN buf string buffer containing JSON formatted data
 * IN flags flags to format the output
 * RET structured data or NULL on error (must call xfree())
 */
extern char *dump_json(const data_t *data, dump_json_flags_t flags);

#endif /* _XJSON_H */
