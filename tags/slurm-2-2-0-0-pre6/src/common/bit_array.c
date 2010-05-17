/*****************************************************************************\
 *  bit_array.c - Functions to manupulate arrays of bitstrings. This permits
 *  one to maintain separate bitmaps for each node, but also work with a
 *  single bitmap for system-wide scheduling operations (e.g. determining
 *  which jobs are allocated overlapping resources and thus can not be
 *  concurrently scheduled).
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#  if STDC_HEADERS
#    include <string.h>
#  endif
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif /* HAVE_SYS_TYPES_H */
#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else /* ! HAVE_INTTYPES_H */
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif /* HAVE_INTTYPES_H */
#else /* ! HAVE_CONFIG_H */
#  include <sys/types.h>
#  include <unistd.h>
#  include <stdint.h>
#  include <string.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/bit_array.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

/*
 * Convert an array of bitstrings into a simple structure containing one
 *	bitstring. Use 
 * IN strings - a NULL termininated array of pointers to bitstrings to be
 *		converted
 * OUT bitstruct - a struct containing all of the information from all
 *		bitstrings in a single bitstring
 * RET: SLURM_SUCCESS or error code
 *
 * NOTE: User is responsible to free bitstruct using bitstruct_free()
 * NOTE: Use bitstruct2strings() to recreate original bitstrings
 */
int bitstrings2struct(bitstr_t **strings, bit_array_struct_t **bitstruct)
{
	bit_array_struct_t *bs;
	uint32_t cnt = 0, i, j, sizes_len = 0, total_len = 0;
	uint32_t *sizes = NULL;

	bs = (bit_array_struct_t *) xmalloc(sizeof(bit_array_struct_t));
	if (strings == NULL)
		goto fini;

	for (i=0; strings[i]; i++) {
		if (sizes_len < (i + 1)) {
			sizes_len += 200;
			sizes = xrealloc(sizes, (sizeof(uint32_t) * sizes_len));
		}
		sizes[i] = bit_size(strings[i]);
		total_len += sizes[i];
	}
	if (i == 0) {
		xfree(sizes);
		goto fini;
	}
	cnt = i;

	bs->bitstr   = bit_alloc(total_len);
	bs->rec_size = sizes;
	bs->rec_reps = xmalloc(sizeof(uint32_t) * cnt);
	for (i=0, total_len=0; i<cnt; i++) {
		for (j=0; j<sizes[i]; j++) {
			if (bit_test(strings[i], j))
				bit_set(bs->bitstr, total_len+j);
		}
		total_len += sizes[i];
		if (sizes[i] == sizes[bs->rec_cnt])
			bs->rec_reps[bs->rec_cnt]++;
		else {
			bs->rec_cnt++;
			sizes[bs->rec_cnt] = sizes[i];
			bs->rec_reps[bs->rec_cnt]++;
		}
	}
	bs->rec_cnt++;

fini:	*bitstruct = bs;
	return SLURM_SUCCESS;
}

/* Log the contents of a bitstruct */
void bitstruct_log(bit_array_struct_t *bitstruct)
{
	int i, j, k;
	int bit_offset = 0, cnt = 0, str_len, ret;
	char str[128], *sep;

	if (bitstruct == NULL) {
		error("log_bitstruct: struct pointer is NULL");
		return;
	}

	xassert(bitstruct->rec_size);
	xassert(bitstruct->rec_reps);
	info("rec_cnt=%u", bitstruct->rec_cnt);
	for (i=0; i<bitstruct->rec_cnt; i++) {
		info("rec_size[%d]=%u rec_reps[%d]=%u",
		     i, bitstruct->rec_size[i], i, bitstruct->rec_reps[i]);
		for (j=0; j<bitstruct->rec_reps[i]; j++) {
			str_len = 0;
			ret = snprintf(str + str_len,
				       sizeof(str) - str_len,
				       "bitstr[%u] len=%u bits:",
				       cnt++, bitstruct->rec_size[i]);
			if (ret >= 0)
				str_len += ret;
			sep = "";
			for (k=0; k<bitstruct->rec_size[i]; k++) {
				if (!bit_test(bitstruct->bitstr, bit_offset+k))
					continue;
				ret = snprintf(str + str_len,
					       sizeof(str) - str_len, 
					       "%s%u", sep, k);
				sep = ",";
				if (ret >= 0)
					str_len += ret;
			}
			info("%s", str);
			bit_offset += bitstruct->rec_size[i];
		}
	}
}

/*
 * Convert a simple structure containing one bitstring into an array of
 *	bitstrings
 * IN bitstruct - a struct containing all of the information from all bitstrings
 *	in a single bitstrings
 * OUT strings - a NULL termininated array of pointers to bitstrings originally   *	passed as input to bitstrings2struct()
 * RET: SLURM_SUCCESS or error code
 *
 * NOTE: User is responsible to free strings
 */
int bitstruct2strings(bit_array_struct_t *bitstruct, bitstr_t ***strings)
{
	int i, j, k;
	int bit_offset = 0, cnt = 0;
	bitstr_t **local_bitstr;

	if (bitstruct == NULL) {
		error("bitstruct2strings: struct pointer is NULL");
		return EINVAL;
	}

	xassert(bitstruct->rec_size);
	xassert(bitstruct->rec_reps);

	for (i=0; i<bitstruct->rec_cnt; i++) {
		cnt += bitstruct->rec_reps[i];
		bit_offset += bitstruct->rec_size[i] * bitstruct->rec_reps[i];
	}
	i = bit_size(bitstruct->bitstr);
	if (i != bit_offset) {
		error("bitstruct2strings: bit_size mismatch (%d != %d)",
		      i, bit_offset);
		return EINVAL;
	}

	local_bitstr = xmalloc(sizeof(bitstr_t *) * cnt);
	*strings = local_bitstr;

	bit_offset = 0;
	cnt = 0;
	for (i=0; i<bitstruct->rec_cnt; i++) {
		for (j=0; j<bitstruct->rec_reps[i]; j++) {
			local_bitstr[cnt] = bit_alloc(bitstruct->rec_size[i]);
			if (!local_bitstr[cnt])
				fatal("bit_alloc malloc failure");
			for (k=0; k<bitstruct->rec_size[i]; k++) {
				if (bit_test(bitstruct->bitstr, bit_offset + k))
					bit_set(local_bitstr[cnt], k);
			}
			bit_offset += bitstruct->rec_size[i];
			cnt++;
		}
	}

	return SLURM_SUCCESS;
}

/*
 * Extract a specific bitstring from the array by specifying its index
 * IN bitstruct - a struct containing all of the information from all bitstrings
 *	in a single bitstrings
 * IN index - zero-origin index of the 
 * RET: a single bitstrings originally passed as input to bitstrings2struct()
 *	NULL on failure
 *
 * NOTE: User is responsible to free the return value
 */
bitstr_t *bitstruct2string(bit_array_struct_t *bitstruct, int index)
{
	int i, j;
	int bit_offset = 0, cnt = 0;
	bitstr_t *local_bitstr;

	if (bitstruct == NULL) {
		error("bitstruct2string: struct pointer is NULL");
		return NULL;
	}
	if (index < 0) {
		error("bitstruct2string: index is invalid (%d)", index);
		return NULL;
	}

	xassert(bitstruct->rec_size);
	xassert(bitstruct->rec_reps);

	for (i=0; i<bitstruct->rec_cnt; i++) {
		if (index >= (cnt + bitstruct->rec_reps[i])) {
			bit_offset += bitstruct->rec_size[i] *
				      bitstruct->rec_reps[i];
			cnt += bitstruct->rec_reps[i];
			continue;
		}

		bit_offset += bitstruct->rec_size[i] * (index - cnt);
		break;
	}
	if (i >= bitstruct->rec_cnt) {
		error("bitstruct2string: index is invalid (%d > %d)",
		      index, (cnt - 1));
		return NULL;
	}
	j = bit_size(bitstruct->bitstr);
	if (j < (bit_offset+bitstruct->rec_size[i])) {
		error("bitstruct2string: bit_size mismatch (%d < %d)",
		      j, bit_offset);
		return NULL;
	}

	local_bitstr = bit_alloc(bitstruct->rec_size[i]);
	if (!local_bitstr)
		fatal("bit_alloc malloc failure");
	for (j=0; j<bitstruct->rec_size[i]; j++) {
		if (bit_test(bitstruct->bitstr, bit_offset + j))
			bit_set(local_bitstr, j);
	}

	return local_bitstr;
}

/*
 * free bitstruct generated by bitstrings2struct()
 */
void bitstruct_free(bit_array_struct_t *bitstruct)
{
	if (!bitstruct)
		return;

	if (bitstruct->bitstr)
		bit_free(bitstruct->bitstr);
	xfree(bitstruct->rec_size);
	xfree(bitstruct->rec_reps);
	xfree(bitstruct);
}

/*
 * pack bitstruct generated by bitstrings2struct() into a buffer
 */
void bitstruct_pack(bit_array_struct_t *bitstruct, Buf buffer,
		    uint16_t protocol_version)
{
	if (!bitstruct) {
		uint32_t rec_cnt = NO_VAL;
		pack32(rec_cnt, buffer);
		return;
	}

	pack32(bitstruct->rec_cnt, buffer);
	pack32_array(bitstruct->rec_size, bitstruct->rec_cnt, buffer);
	pack32_array(bitstruct->rec_reps, bitstruct->rec_cnt, buffer);
	pack_bit_str(bitstruct->bitstr, buffer);
}

/*
 * unpack bitstruct from a buffer as packed by pack_bitstruct()
 * RET: SLURM_SUCCESS or error code
 *
 * NOTE: User is responsible to free bitstruct using bitstruct_free()
 */
int bitstruct_unpack(bit_array_struct_t **bitstruct, Buf buffer,
		     uint16_t protocol_version)
{
	bit_array_struct_t *bs = NULL;
	uint32_t rec_cnt, uint32_tmp;

	*bitstruct = NULL;
	safe_unpack32(&rec_cnt, buffer);
	if (rec_cnt == NO_VAL)
		return SLURM_SUCCESS;

	bs = xmalloc(sizeof(bit_array_struct_t));
	bs->rec_cnt = rec_cnt;

	safe_unpack32_array(&bs->rec_size, &uint32_tmp, buffer);
	if (uint32_tmp != rec_cnt)
		goto unpack_error;
	safe_unpack32_array(&bs->rec_reps, &uint32_tmp, buffer);
	if (uint32_tmp != rec_cnt)
		goto unpack_error;
	unpack_bit_str(&bs->bitstr, buffer);

	*bitstruct = bs;
	return SLURM_SUCCESS;

unpack_error:
	bitstruct_free(bs);
	return SLURM_ERROR;
}
