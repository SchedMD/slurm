/*****************************************************************************\
 *  http_parser_common.h - common http_parser plugin code
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef _HTTP_PARSER_COMMON_H
#define _HTTP_PARSER_COMMON_H

#include <stddef.h>

#include "src/common/pack.h"

#include "src/interfaces/http_parser.h"

extern void log_parse(const buf_t *buffer, const char *name, const void *at,
		      const size_t at_bytes, const char *caller,
		      const char *fmt, ...);

/*
 * Notify caller that parsing failed
 * NOTE: use plugin's PARSE_ERROR()/PARSE_ERROR_AT() instead of calling directly
 * IN error_number - Slurm error encountered
 * IN total_bytes - Bytes already parsed (cumulative)
 * IN buffer - Current buffer getting parsed
 * IN callbacks - callback to call on events
 * IN callback_arg - pointer to hand to callbacks
 * IN name - Name of connection for logging
 * IN state - state pointer
 * IN at - pointer to where failure happened or NULL if N/A
 * IN caller - function that caught error
 * OUT rc - error code
 * RET 1 - always 1 to return to http_parser to stop parsing
 */
extern int on_parse_error(slurm_err_t error_number, ssize_t total_bytes,
			  const buf_t *buffer,
			  const http_parser_callbacks_t *callbacks,
			  void *callback_arg, const char *name, const void *at,
			  const size_t at_bytes, const char *caller, int *rc);

#endif
