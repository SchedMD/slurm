/*****************************************************************************\
 *  xbase64.c
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

#include <stdint.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/xbase64.h"
#include "src/common/xmalloc.h"

strong_alias(xbase64_encode, slurm_xbase64_encode);
strong_alias(xbase64_decode, slurm_xbase64_decode);

static const char encode[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

extern char *xbase64_encode(const uint8_t *plain, int len)
{
	size_t i = 0, j = 0;
	size_t output_len = ((len * 4) / 3) + 5;
	char *output = xmalloc(output_len);

	for (; (i + 2) < len; i += 3) {
		output[j++] = encode[plain[i] >> 2];
		output[j++] =
			encode[((plain[i] & 0x03) << 4) | (plain[i + 1] >> 4)];
		output[j++] = encode[((plain[i + 1] & 0x0f) << 2) |
				     (plain[i + 2] >> 6)];
		output[j++] = encode[plain[i + 2] & 0x3f];
	}

	if ((len % 3) == 1) {
		output[j++] = encode[plain[i] >> 2];
		output[j++] = encode[((plain[i] & 0x03) << 4)];
		output[j++] = '=';
		output[j++] = '=';
	} else if ((len % 3) == 2) {
		output[j++] = encode[plain[i] >> 2];
		output[j++] =
			encode[((plain[i] & 0x03) << 4) | (plain[i + 1] >> 4)];
		output[j++] = encode[((plain[i + 1] & 0x0f) << 2)];
		output[j++] = '=';
	}

	return output;
}

extern int xbase64_decode(uint8_t **decoded, const char *encoded)
{
	uint8_t *output = NULL;
	size_t len = strlen(encoded), output_len;
	int i = 0, j = 0;
	const char *pos0, *pos1, *pos2, *pos3;
	char val0, val1, val2, val3;

	*decoded = NULL;

	if (!len || (len % 4))
		goto fail;

	output_len = (len / 4) * 3;
	output = xmalloc(output_len + 1);

	for (; (i + 7) < len; i += 4) {
		pos0 = strchr(encode, encoded[i]);
		pos1 = strchr(encode, encoded[i + 1]);
		pos2 = strchr(encode, encoded[i + 2]);
		pos3 = strchr(encode, encoded[i + 3]);

		/* invalid characters */
		if (!pos0 || !pos1 || !pos2 || !pos3)
			goto fail;

		val0 = pos0 - encode;
		val1 = pos1 - encode;
		val2 = pos2 - encode;
		val3 = pos3 - encode;

		output[j++] = (val0 << 2) | (val1 >> 4);
		output[j++] = ((val1 & 0x0f) << 4) | (val2 >> 2);
		output[j++] = ((val2 & 0x03) << 6) | val3;
	}

	/* last four characters are handled differently: */
	pos0 = strchr(encode, encoded[i]);
	pos1 = strchr(encode, encoded[i + 1]);
	pos2 = pos3 = encode;
	if ((encoded[i + 2] == '=') && (encoded[i + 3] == '=')) {
		output_len -= 2;
	} else if (encoded[i + 3] == '=') {
		pos2 = strchr(encode, encoded[i + 2]);
		output_len -= 1;
	} else {
		pos2 = strchr(encode, encoded[i + 2]);
		pos3 = strchr(encode, encoded[i + 3]);
	}

	/* invalid characters */
	if (!pos0 || !pos1 || !pos2 || !pos3)
		goto fail;

	val0 = pos0 - encode;
	val1 = pos1 - encode;
	val2 = pos2 - encode;
	val3 = pos3 - encode;

	output[j++] = (val0 << 2) | (val1 >> 4);
	output[j++] = ((val1 & 0x0f) << 4) | (val2 >> 2);
	output[j++] = ((val2 & 0x03) << 6) | val3;

	*decoded = output;
	return output_len;

fail:
	xfree(output);
	return -1;
}
