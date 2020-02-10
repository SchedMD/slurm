/*****************************************************************************\
 *  http_content_type.h - definitions for handling http content types
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

#ifndef SLURMRESTD_HTTP_CONTENT_TYPE_H
#define SLURMRESTD_HTTP_CONTENT_TYPE_H

#include "src/common/data.h"
#include "src/common/list.h"

#include "src/slurmrestd/http.h"

typedef enum {
	MIME_UNKNOWN = 0,
	/*
	 * YAML doesn't have an IANA registered mime type yet
	 * so we are gonna match ruby on rails
	 */
	MIME_YAML, /* application/x-yaml,text/yaml */
	MIME_JSON, /* application/json,application/jsonrequest */
	MIME_URL_ENCODED, /* application/x-www-form-urlencoded */
} mime_types_t;

typedef struct {
	char *type; /* mime type and sub type unchanged */
	float q; /* quality factor (priority) */
} http_header_accept_t;

/*
 * Parses rfc7231 accept header of accepted content types.
 * IN accept rfc7231 accept header string */
// 	example: Accept: text/html, application/xhtml+xml, application/xml;q=0.9, */*;q=0.8
/* RET list of http_header_accept_t or NULL on error (must call list_destroy)
 * list will be ordered by q value (highest being first)
 */
extern List parse_http_accept(const char *accept);

/*
 * Detect if a mime matches b mime.
 * This allows matching even if mime types have * in them.
 * IN a first mime type to check
 * IN b second mime to type to check
 * RET true if they match, false otherwise
 */
extern bool is_mime_matching_type(const char *a, const char *b);

/*
 * Matches mime type exactly or not at all.
 * IN type mime type
 * RET mime type or unknown
 */
extern mime_types_t get_mime_type(const char *type);

/*
 * Get string of mime type
 * will always return the preferred type since they can be non-unique types
 * IN type mime type
 * RET mime type as string or NULL for unknown
 */
extern const char *get_mime_type_str(mime_types_t type);

/*
 * Find closest matching mime type
 * IN type mime type
 * RET mime type or MIME_UNKNOWN
 */
extern mime_types_t find_matching_mime_type(const char *type);

#endif /* SLURMRESTD_HTTP_CONTENT_TYPE_H */
