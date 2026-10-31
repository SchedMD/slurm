/*****************************************************************************\
 *  relay_io_common.h - record schema shared by relay_io plugins
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

#ifndef _RELAY_IO_COMMON_H
#define _RELAY_IO_COMMON_H

#include "src/common/data.h"

#include "src/interfaces/relay_io.h"

/*
 * Render a record as a data_t in the schema every relay_io plugin emits, with
 * the body sanitized so that it always serializes.
 * IN record - event to render
 * IN/OUT dst - data to populate, set to a dictionary
 * IN body_limit - most bytes of sanitized body to include, SIZE_MAX for all
 * OUT consumed_ptr - bytes of record->body the result represents, never zero
 *	for a non-empty body, NULL if the caller does not care
 */
extern void relay_io_record_pack_data(const relay_io_record_t *record,
				      data_t *dst, size_t body_limit,
				      size_t *consumed_ptr);

#endif
