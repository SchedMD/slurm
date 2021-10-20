/*****************************************************************************\
 *  gpu_common.c - GPU plugin common functions
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Michael Hinton <hinton@schedmd.com>
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

#include "gpu_common.h"

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xstring.h"

/*
 * Convert a frequency value to a string
 * Returned string must be xfree()'ed
 */
extern char *gpu_common_freq_value_to_string(unsigned int freq)
{
	switch (freq) {
	case GPU_LOW:
		return xstrdup("low");
	case GPU_MEDIUM:
		return xstrdup("medium");
	case GPU_HIGH:
		return xstrdup("high");
	case GPU_HIGH_M1:
		return xstrdup("highm1");
	default:
		return xstrdup_printf("%u", freq);
	}
}

/*
 * Convert frequency to nearest valid frequency found in frequency array
 *
 * freq		(IN/OUT) The frequency to check, in MHz. Also the output, if
 * 		it needs to be changed.
 * freqs_size	(IN) The size of the freqs array
 * freqs	(IN) An array of frequency values in MHz, sorted highest to
 * 		lowest
 *
 * Inspired by src/common/cpu_frequency#_cpu_freq_freqspec_num()
 */
extern void gpu_common_get_nearest_freq(unsigned int *freq,
					unsigned int freqs_size,
					unsigned int *freqs)
{
	unsigned int i;

	if (!freq || !(*freq)) {
		log_flag(GRES, "%s: No frequency supplied", __func__);
		return;
	}
	if (!freqs || !(*freqs)) {
		log_flag(GRES, "%s: No frequency list supplied", __func__);
		return;
	}
	if (freqs_size <= 0) {
		log_flag(GRES, "%s: Frequency list is empty", __func__);
		return;
	}

	/* Check for special case values; freqs is sorted in descending order */
	switch ((*freq)) {
	case GPU_LOW:
		*freq = freqs[freqs_size - 1];
		debug2("Frequency GPU_LOW: %u MHz", *freq);
		return;

	case GPU_MEDIUM:
		*freq = freqs[(freqs_size - 1) / 2];
		debug2("Frequency GPU_MEDIUM: %u MHz", *freq);
		return;

	case GPU_HIGH_M1:
		if (freqs_size == 1)
			*freq = freqs[0];
		else
			*freq = freqs[1];
		debug2("Frequency GPU_HIGH_M1: %u MHz", *freq);
		return;

	case GPU_HIGH:
		*freq = freqs[0];
		debug2("Frequency GPU_HIGH: %u MHz", *freq);
		return;

	default:
		debug2("Freq is not a special case. Continue...");
		break;
	}

	/* check if freq is out of bounds of freqs */
	if (*freq > freqs[0]) {
		log_flag(GRES, "Rounding requested frequency %u MHz down to %u MHz (highest available)",
		         *freq, freqs[0]);
		*freq = freqs[0];
		return;
	} else if (*freq < freqs[freqs_size - 1]) {
		log_flag(GRES, "Rounding requested frequency %u MHz up to %u MHz (lowest available)",
		         *freq, freqs[freqs_size - 1]);
		*freq = freqs[freqs_size - 1];
		return;
	}

	/* check for frequency, and round up if no exact match */
	for (i = 0; i < freqs_size - 1;) {
		if (*freq == freqs[i]) {
			/* No change necessary */
			debug2("No change necessary. Freq: %u MHz", *freq);
			return;
		}
		i++;
		/*
		 * Step down to next element to round up.
		 * Safe to advance due to bounds checks above here
		 */
		if (*freq > freqs[i]) {
			log_flag(GRES, "Rounding requested frequency %u MHz up to %u MHz (next available)",
			         *freq, freqs[i - 1]);
			*freq = freqs[i - 1];
			return;
		}
	}
	error("%s: Got to the end of the function. This shouldn't happen. Freq: %u MHz",
	      __func__, *freq);
}
