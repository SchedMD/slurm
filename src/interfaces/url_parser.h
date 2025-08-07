/*****************************************************************************\
 *  URL Parser plugin interface
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

#ifndef _INTERFACES_URL_PARSER_H
#define _INTERFACES_URL_PARSER_H

#include "src/common/http.h"
#include "src/common/pack.h"

#define URL_PARSER_MAJOR_TYPE "url_parser"
#define URL_PARSER_PREFIX URL_PARSER_MAJOR_TYPE "/"

/*
 * Load and initialize URL parser plugin
 * RET SLURM_SUCCESS or error
 */
extern int url_parser_g_init(void);

/* Unload URL plugin */
extern void url_parser_g_fini(void);

/*
 * Parse URL
 * IN name - name used for logging
 * IN buffer - buffer containing string to parse
 * IN/OUT url - URL to populate with parsed components of URL
 * RET SLURM_SUCCESS or error
 */
extern int url_parser_g_parse(const char *name, const buf_t *buffer,
			      url_t *url);

#endif
