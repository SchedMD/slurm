/*****************************************************************************\
 *  auth_common.h - Common authentication utilities for Slurm auth plugins
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Copyright Amazon.com Inc. or its affiliates.
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

#ifndef _AUTH_COMMON_H
#define _AUTH_COMMON_H

#include "src/common/data.h"
#include "src/common/identity.h"

/*
 * These functions provide conversion between identity_t structures
 * and various data formats (JSON, data_t) for serialization.
 */

/*
 * Convert identity_t structure to JSON string
 * IN id - Identity structure to convert (can be NULL)
 * IN uid - UID to use if id is NULL
 * IN gid - GID to use if id is NULL
 * RETURNS: JSON string (caller must free with xfree)
 */
extern char *auth_common_get_identity_string(identity_t *id, uid_t uid,
					     gid_t gid);

/*
 * Convert identity_t structure to data_t for serialization
 * IN id - Identity structure to convert
 * RETURNS: data_t structure (caller must free with FREE_NULL_DATA)
 */
extern data_t *auth_common_identity_to_data(identity_t *id);

/*
 * Extract identity_t structure from JSON string
 * IN json - JSON string containing identity data
 * IN uid - UID to set in identity structure
 * IN gid - GID to set in identity structure
 * RETURNS: identity_t structure (caller must free with FREE_NULL_IDENTITY)
 */
extern identity_t *auth_common_extract_identity(char *json, uid_t uid,
						gid_t gid);

#endif
