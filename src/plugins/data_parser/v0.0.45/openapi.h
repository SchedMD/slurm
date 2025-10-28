/*****************************************************************************\
 *  openapi.h - Slurm data parser openapi specifier
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

#ifndef _DATA_PARSER_OPENAPI_H
#define _DATA_PARSER_OPENAPI_H

#include "parsers.h"
#include "src/common/openapi.h"
#include "src/common/slurm_protocol_common.h"
#include "src/interfaces/data_parser.h"

#if !defined(PLUGIN_RELEASED)
#error PLUGIN_RELEASED not defined
#elif PLUGIN_RELEASED > SLURM_MIN_PROTOCOL_VERSION
#define IS_PLUGIN_DEPRECATED false
#else
#define IS_PLUGIN_DEPRECATED true
#endif

/*
 * Populate dst with OpenAPI specification schema
 * IN dst - data_t ptr to populate with schema
 * IN parser - schema parser to specify
 * IN args - parser args
 */
extern void set_openapi_schema(data_t *dst, const parser_t *parser,
			       args_t *args);

#endif
