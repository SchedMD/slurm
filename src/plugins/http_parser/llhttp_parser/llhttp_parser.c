/*****************************************************************************\
 *  llhttp_parser.c - llhttp_parser handler
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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"

#include "src/interfaces/http_parser.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "Slurm http_parser llhttp_parser plugin";
const char plugin_type[] = HTTP_PARSER_PREFIX LLHTTP_PARSER_PLUGIN;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

extern int init(void)
{
	debug("loaded");
	return SLURM_SUCCESS;
}

extern void fini(void)
{
	debug("unloaded");
}

extern int http_parser_p_new_parse_request(const char *name,
					   const http_parser_callbacks_t
						   *callbacks,
					   void *callback_arg,
					   http_parser_state_t **state_ptr)
{
	return SLURM_SUCCESS;
}

extern void http_parser_p_free_parse_request(http_parser_state_t **state_ptr)
{
	return;
}

extern int http_parser_p_parse_request(http_parser_state_t *state,
				       const buf_t *buffer,
				       ssize_t *bytes_parsed_ptr)
{
	return SLURM_SUCCESS;
}
