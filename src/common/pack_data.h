/*****************************************************************************\
 *  pack_data.h - pack and unpack a data_t
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

#ifndef _PACK_DATA_H
#define _PACK_DATA_H

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/pack.h"
#include "src/common/xassert.h"

/*
 * Pack a data_t into a buffer.
 *
 * Writes a one byte tag naming the type, followed by the value. A list or a
 * dictionary is packed recursively, its entry count first. An integer is
 * narrowed to the smallest width that holds it, and the tag says which.
 *
 * Nesting is bounded, and a list or a dictionary deeper than the bound is
 * refused, because unpack_data() would refuse to read it back. The bound is
 * deeper than any payload this packs in practice.
 *
 * IN val - data to pack
 * IN protocol_version - version of the peer this is packed for.
 * IN/OUT buffer - buffer to append the tag and value to
 * RET SLURM_SUCCESS, EINVAL on a NULL argument, ESLURM_DATA_TOO_LARGE when a
 *	string or an entry count is too large to pack, or SLURM_ERROR when the
 *	value is nested too deeply
 */
extern int pack_data(const data_t *val, uint16_t protocol_version,
		     buf_t *buffer);

/*
 * Unpack a data_t from a buffer.
 *
 * On any error valp is left holding whatever was read before the error, so a
 * caller that does not get SLURM_SUCCESS must discard it rather than read it.
 *
 * IN/OUT valp - existing data_t to populate with the unpacked value
 * IN protocol_version - version of the peer this was packed by.
 * IN/OUT buffer - buffer to read from, advanced past what was read
 * RET SLURM_SUCCESS, EINVAL on a NULL argument, or SLURM_ERROR when the buffer
 *	is too short, the tag is not a known one, or the value is nested too
 *	deeply
 */
extern int unpack_data(data_t *valp, uint16_t protocol_version, buf_t *buffer);

/*
 * data_t is opaque, so this cannot check sizeof(*valp) the way the
 * safe_unpack macros in pack.h check their own types.
 */
#define safe_unpack_data(valp, protocol_version, buf) \
	do { \
		xassert(buf->magic == BUF_MAGIC); \
		if (unpack_data(valp, protocol_version, buf)) \
			goto unpack_error; \
	} while (0)

#endif /* _PACK_DATA_H */
