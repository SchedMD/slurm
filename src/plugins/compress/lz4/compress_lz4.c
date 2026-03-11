/*****************************************************************************\
 *  compress_lz4.c - lz4 compression plugin
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

#include <inttypes.h>
#include <lz4.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "lz4 compression plugin";
const char plugin_type[] = "compress/lz4";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t plugin_id = COMPRESS_PLUGIN_LZ4;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded,
 * free any global memory allocations here to avoid memory leaks.
 */
extern int fini(void)
{
	verbose("%s unloaded", plugin_name);
	return SLURM_SUCCESS;
}

extern ssize_t compress_p_comp_block(char **in_buf, const ssize_t input_size,
				     char **out_buf, const ssize_t out_size,
				     ssize_t *remaining)
{
	ssize_t compress_size;
	int size_limit = MIN(out_size * 10, *remaining);

	compress_size =
		LZ4_compress_destSize(*in_buf, *out_buf, &size_limit, out_size);

	if (!compress_size)
		fatal("LZ4 compression error");

	*remaining -= size_limit;
	*in_buf += size_limit;

	return compress_size;
}

extern char *compress_p_decompress(char *in_buf, const ssize_t in_size,
				   const ssize_t out_size)
{
	char *out_buf = NULL;
	int uncomp_len;

	xassert(in_buf);

	/* no data to decompress, simply return the input data */
	if (in_size == 0)
		return in_buf;

	out_buf = xmalloc(out_size);
	uncomp_len = LZ4_decompress_safe(in_buf, out_buf, in_size, out_size);

	if (uncomp_len != out_size) {
		error("lz4 decompression error, original block length != decompressed length");
		xfree(out_buf);
	}

	return out_buf;
}
