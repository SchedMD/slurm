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

/*
 * Reverse of encode[]. Values are biased by one so that zero, the default for
 * every character not listed, means the character is not in the alphabet.
 */
static const uint8_t decode[256] = {
	['A'] = 1,  ['B'] = 2,  ['C'] = 3,  ['D'] = 4,  ['E'] = 5,  ['F'] = 6,
	['G'] = 7,  ['H'] = 8,  ['I'] = 9,  ['J'] = 10, ['K'] = 11, ['L'] = 12,
	['M'] = 13, ['N'] = 14, ['O'] = 15, ['P'] = 16, ['Q'] = 17, ['R'] = 18,
	['S'] = 19, ['T'] = 20, ['U'] = 21, ['V'] = 22, ['W'] = 23, ['X'] = 24,
	['Y'] = 25, ['Z'] = 26, ['a'] = 27, ['b'] = 28, ['c'] = 29, ['d'] = 30,
	['e'] = 31, ['f'] = 32, ['g'] = 33, ['h'] = 34, ['i'] = 35, ['j'] = 36,
	['k'] = 37, ['l'] = 38, ['m'] = 39, ['n'] = 40, ['o'] = 41, ['p'] = 42,
	['q'] = 43, ['r'] = 44, ['s'] = 45, ['t'] = 46, ['u'] = 47, ['v'] = 48,
	['w'] = 49, ['x'] = 50, ['y'] = 51, ['z'] = 52, ['0'] = 53, ['1'] = 54,
	['2'] = 55, ['3'] = 56, ['4'] = 57, ['5'] = 58, ['6'] = 59, ['7'] = 60,
	['8'] = 61, ['9'] = 62, ['+'] = 63, ['/'] = 64,
};

static int _decode_value(char c)
{
	return decode[(uint8_t) c] - 1;
}

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
	size_t i = 0, j = 0;
	int v0, v1, v2, v3;

	*decoded = NULL;

	if (!len || (len % 4))
		goto fail;

	output_len = (len / 4) * 3;
	output = xmalloc(output_len + 1);

	/* Every quad but the last, which alone may carry padding. */
	for (; i < (len - 4); i += 4) {
		if (((v0 = _decode_value(encoded[i])) < 0) ||
		    ((v1 = _decode_value(encoded[i + 1])) < 0) ||
		    ((v2 = _decode_value(encoded[i + 2])) < 0) ||
		    ((v3 = _decode_value(encoded[i + 3])) < 0))
			goto fail;

		output[j++] = (v0 << 2) | (v1 >> 4);
		output[j++] = ((v1 & 0x0f) << 4) | (v2 >> 2);
		output[j++] = ((v2 & 0x03) << 6) | v3;
	}

	if (((v0 = _decode_value(encoded[i])) < 0) ||
	    ((v1 = _decode_value(encoded[i + 1])) < 0))
		goto fail;

	if ((encoded[i + 2] == '=') && (encoded[i + 3] == '=')) {
		v2 = 0;
		v3 = 0;
		output_len -= 2;
	} else if (encoded[i + 3] == '=') {
		if ((v2 = _decode_value(encoded[i + 2])) < 0)
			goto fail;
		v3 = 0;
		output_len -= 1;
	} else {
		if (((v2 = _decode_value(encoded[i + 2])) < 0) ||
		    ((v3 = _decode_value(encoded[i + 3])) < 0))
			goto fail;
	}

	output[j++] = (v0 << 2) | (v1 >> 4);
	output[j++] = ((v1 & 0x0f) << 4) | (v2 >> 2);
	output[j++] = ((v2 & 0x03) << 6) | v3;

	*decoded = output;
	return output_len;

fail:
	xfree(output);
	return -1;
}
