/*****************************************************************************\
 *  serializer plugin interface
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
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

#ifndef _SERIALIZER_H
#define _SERIALIZER_H

#include "src/common/data.h"

typedef enum {
	SER_FLAGS_NONE = 0, /* defaults to compact currently */
	SER_FLAGS_COMPACT = 1 << 1,
	SER_FLAGS_PRETTY = 1 << 2,
} serializer_flags_t;

/*
 * Define common MIME types to make it easier for serializer callers.
 *
 * WARNING: There is no guarantee that plugins for these types
 * will be loaded at any given time.
 */
#define MIME_TYPE_YAML "application/x-yaml"
#define MIME_TYPE_YAML_PLUGIN "serializer/yaml"
#define MIME_TYPE_JSON "application/json"
#define MIME_TYPE_JSON_PLUGIN "serializer/json"
#define MIME_TYPE_URL_ENCODED "application/x-www-form-urlencoded"
#define MIME_TYPE_URL_ENCODED_PLUGIN "serializer/url-encoded"

/*
 * Serialize data in src into string dest
 * IN/OUT dest - ptr to NULL string ptr to set with output data.
 * 	caller must xfree(dest) if set. Pointer is not changed on failure.
 * IN/OUT length - set with number of bytes written to *dest (including '\0')
 * IN src - populated data ptr to serialize
 * IN mime_type - serialize data into the given mime_type
 * IN flags - optional flags to specify to serilzier to change presentation of
 * 	data
 * RET SLURM_SUCCESS or error
 */
extern int serialize_g_data_to_string(char **dest, size_t *length,
				      const data_t *src, const char *mime_type,
				      serializer_flags_t flags);

/*
 * serialize string in src into data dest
 * IN/OUT dest - ptr to NULL data ptr to set with output data.
 * 	caller must FREE_NULL_DATA(dest) if set.
 * IN src - string to deserialize
 * IN length - number of bytes in src
 * IN mime_type - deserialize data using given mime_type
 * RET SLURM_SUCCESS or error
 */
extern int serialize_g_string_to_data(data_t **dest, const char *src,
				      size_t length, const char *mime_type);

/*
 * Check if there is a plugin loaded that can handle the requested mime type
 * RET ptr to best matching mime type or NULL if none can match
 */
extern const char *resolve_mime_type(const char *mime_type);

/*
 * Load and initialize serializer plugins
 *
 * IN plugins - comma delimited list of plugins or "list"
 * 	pass NULL to load all found or "" to load none of them
 *
 * IN listf - function to call if plugins="list" (may be NULL)
 * RET SLURM_SUCCESS or error
 */
extern int serializer_g_init(const char *plugin_list, plugrack_foreach_t listf);

/*
 * Unload all serializer plugins
 */
extern void serializer_g_fini(void);

#endif
