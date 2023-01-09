/*****************************************************************************\
 *  openapi.h - Slurm data parser openapi specifier
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
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

#ifndef _DATA_PARSER_OPENAPI_H
#define _DATA_PARSER_OPENAPI_H

#include "parsers.h"
#include "src/interfaces/data_parser.h"
#include "src/interfaces/openapi.h"

/*
 * Populate OpenAPI specification field with reference to parser
 * IN obj - data_t ptr to specific field in OpenAPI schema
 * 	Sets "$ref" key in obj to path of parser schema.
 * 	Parser must be an OBJECT or ARRAY OpenAPI type.
 * IN parser - populate field with $ref to parser
 * IN spec - entire OpenAPI specification
 * IN args - parser args
 */
extern void set_openapi_parse_ref(data_t *obj, const parser_t *parser,
				  data_t *spec, args_t *args);

/*
 * Populate OpenAPI specification field
 * IN obj - data_t ptr to specific field in OpenAPI schema
 * IN format - OpenAPI format to use
 * IN desc - Description of field to use
 * RET ptr to "items" for ARRAY or "properties" for OBJECT or NULL
 */
extern data_t *set_openapi_props(data_t *obj, openapi_type_format_t format,
				 const char *desc);

#endif
