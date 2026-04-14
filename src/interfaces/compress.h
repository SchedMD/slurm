/*****************************************************************************\
 *  compress.h - Compression plugin interface
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

#ifndef _INTERFACES_COMPRESS_H
#define _INTERFACES_COMPRESS_H

extern int compress_g_init(void);
extern void compress_g_fini(void);

/*
 * Purpose:
 *	Check if the requested plugin type is available
 *
 * Arguments:
 * 	int type - one of the compress_plugin_type enum values
 *
 * Returns:
 * 	int - SLURM_SUCCESS if available, SLURM_ERROR if not available
 */
extern int compress_g_type_available(const int type);

/*
 * Arguments:
 * 	int type - one of the compress_plugin_type enum values
 * 	in_buf - buffer to read from - Note that position in the buffer is
 * 		 updated
 * 	input_size - full size of the input
 * 	out_buf - location to output data, must be allocated by the caller
 * 	out_size - size of the output buffer
 * 	remaining - how much data remains to be read from the input buffer
 */
extern ssize_t compress_g_comp_block(const int type, char **in_buf,
				     const ssize_t input_size, char **out_buf,
				     const ssize_t out_size,
				     ssize_t *remaining);

/*
 * Arguments:
 * 	int type: one of the compress_plugin_type enum values
 * 	in_buf: buffer to read from
 * 	in_size: size of input buffer
 * 	out_size: expected decompressed size
 *
 * Returns:
 * 	char *: location of decompressed data
 */
extern char *compress_g_decompress(const int type, char *in_buf,
				   const ssize_t in_size,
				   const ssize_t out_size);

#endif
