/*****************************************************************************\
 *  http_url.h - definitions for handling http urls
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

#ifndef SLURMRESTD_HTTP_URL_H
#define SLURMRESTD_HTTP_URL_H

#include "src/common/data.h"
#include "src/slurmrestd/http.h"

/*
 * Parses url path into a data struct.
 * IN query rfc3986&rfc1866 query string
 * 	application/x-www-form-urlencoded
 * 	breaks /path/to/url/ -> [path,to,url]
 * 	into a data_t sequence
 * IN convert_types if true, call data_convert_type() on each value
 * IN allow_templates - allow sections to be template variables e.g.: "{name}"
 * RET data ptr or NULL on error
 */
extern data_t *parse_url_path(const char *path, bool convert_types,
			      bool allow_templates);

/*
 * Parses url query into a data struct.
 * IN query rfc3986&rfc1866 query string
 * 	application/x-www-form-urlencoded
 * 	breaks key=value&key2=value2&...
 * 	into a data_t dictionary
 * 	dup keys will override existing keys
 * IN convert_types if true, call data_convert_type() on each value
 * RET data ptr or NULL on error
 */
extern data_t *parse_url_query(const char *query, bool convert_types);

#endif /* SLURMRESTD_HTTP_URL_H */
