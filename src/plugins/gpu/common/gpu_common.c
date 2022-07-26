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

#include <ctype.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

static unsigned int _xlate_freq_code(char *gpu_freq)
{
	if (!gpu_freq || !gpu_freq[0])
		return 0;
	if ((gpu_freq[0] >= '0') && (gpu_freq[0] <= '9'))
		return 0;	/* Pure numeric value */
	if (!xstrcasecmp(gpu_freq, "low"))
		return GPU_LOW;
	else if (!xstrcasecmp(gpu_freq, "medium"))
		return GPU_MEDIUM;
	else if (!xstrcasecmp(gpu_freq, "highm1"))
		return GPU_HIGH_M1;
	else if (!xstrcasecmp(gpu_freq, "high"))
		return GPU_HIGH;

	debug("%s: %s: Invalid job GPU frequency (%s)",
	      plugin_type, __func__, gpu_freq);
	return 0;	/* Bad user input */
}

static unsigned int _xlate_freq_value(char *gpu_freq)
{
	unsigned int value;

	if (!gpu_freq || ((gpu_freq[0] < '0') && (gpu_freq[0] > '9')))
		return 0;	/* Not a numeric value */
	value = strtoul(gpu_freq, NULL, 10);
	return value;
}

static void _parse_gpu_freq2(char *gpu_freq, unsigned int *gpu_freq_code,
			     unsigned int *gpu_freq_value,
			     unsigned int *mem_freq_code,
			     unsigned int *mem_freq_value, bool *verbose_flag)
{
	char *tmp, *tok, *sep, *save_ptr = NULL;

	if (!gpu_freq || !gpu_freq[0])
		return;

	tmp = xstrdup(gpu_freq);
	tok = strtok_r(tmp, ",", &save_ptr);

	while (tok) {
		sep = strchr(tok, '=');
		if (sep) {
			sep[0] = '\0';
			sep++;
			if (!xstrcasecmp(tok, "memory")) {
				if (!(*mem_freq_code = _xlate_freq_code(sep)) &&
				    !(*mem_freq_value =_xlate_freq_value(sep))){
					debug("Invalid job GPU memory frequency: %s",
					      tok);
				}
			} else {
				debug("%s: %s: Invalid job device frequency type: %s",
				      plugin_type, __func__, tok);
			}
		} else if (!xstrcasecmp(tok, "verbose")) {
			*verbose_flag = true;
		} else {
			if (!(*gpu_freq_code = _xlate_freq_code(tok)) &&
			    !(*gpu_freq_value = _xlate_freq_value(tok))) {
				debug("Invalid job GPU frequency: %s", tok);
			}
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);
}

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
	for (i = 0; i < freqs_size;) {
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

/*
 * Print out an array of possible frequencies (in MHz).
 *
 * freqs	(IN) The array of frequencies to print, in MHz.
 * size		(IN) The size of the freqs array.
 * l		(IN) The log level to print the frequencies at.
 * freq_type	(IN) (Optional) A short description of the frequencies to print.
 * 		E.g., a value of "GPU Graphics" would print a header of
 * 		"Possible GPU Graphics Frequencies". Set to "" or NULL to just
 * 		print "Possible Frequencies".
 * indent	(IN) (Optional) Whitespace to precede each print line. Set to
 * 		0 for no additional indentation.
 */
extern void gpu_common_print_freqs(unsigned int freqs[], unsigned int size,
				   log_level_t l, char *freq_type,
				   int indent)
{
	bool concise = false;
	unsigned int middle;
	unsigned int penult;
	unsigned int last;

	if (size > FREQS_CONCISE)
		concise = true;

	log_var(l, "%*sPossible %s%sFrequencies (%u):",
		indent, "",
		freq_type ? freq_type : "",
		freq_type ? " ": "",
		size);
	log_var(l, "%*s---------------------------------", indent, "");

	if (!concise) {
		for (int i = 0; i < size; ++i)
			log_var(l, "%*s  *%u MHz [%u]",
				indent, "", freqs[i], i);
		return;
	}

	penult = size - 2;
	last = size - 1;
	middle = last / 2;

	/* First, next, ..., middle, ..., penultimate, last */
	log_var(l, "%*s  *%u MHz [0]", indent, "", freqs[0]);
	log_var(l, "%*s  *%u MHz [1]", indent, "", freqs[1]);
	log_var(l, "%*s  ...", indent, "");
	log_var(l, "%*s  *%u MHz [%u]", indent, "", freqs[middle], middle);
	log_var(l, "%*s  ...", indent, "");
	log_var(l, "%*s  *%u MHz [%u]", indent, "", freqs[penult], penult);
	log_var(l, "%*s  *%u MHz [%u]", indent, "", freqs[last], last);
}

extern void gpu_common_underscorify_tolower(char *str)
{
	for (int i = 0; str[i]; i++) {
		str[i] = tolower(str[i]);
		if (str[i] == ' ')
			str[i] = '_';
	}
}

extern void gpu_common_parse_gpu_freq(char *gpu_freq,
				      unsigned int *gpu_freq_num,
				      unsigned int *mem_freq_num,
				      bool *verbose_flag)
{
	unsigned int def_gpu_freq_code = 0, def_gpu_freq_value = 0;
	unsigned int def_mem_freq_code = 0, def_mem_freq_value = 0;
	unsigned int job_gpu_freq_code = 0, job_gpu_freq_value = 0;
	unsigned int job_mem_freq_code = 0, job_mem_freq_value = 0;
	char *def_freq;

	_parse_gpu_freq2(gpu_freq, &job_gpu_freq_code, &job_gpu_freq_value,
			 &job_mem_freq_code, &job_mem_freq_value, verbose_flag);

	/* Defaults to high for both mem and gfx */
	def_freq = slurm_get_gpu_freq_def();
	_parse_gpu_freq2(def_freq, &def_gpu_freq_code, &def_gpu_freq_value,
			 &def_mem_freq_code, &def_mem_freq_value, verbose_flag);
	xfree(def_freq);

	if (job_gpu_freq_code)
		*gpu_freq_num = job_gpu_freq_code;
	else if (job_gpu_freq_value)
		*gpu_freq_num = job_gpu_freq_value;
	else if (def_gpu_freq_code)
		*gpu_freq_num = def_gpu_freq_code;
	else if (def_gpu_freq_value)
		*gpu_freq_num = def_gpu_freq_value;

	if (job_mem_freq_code)
		*mem_freq_num = job_mem_freq_code;
	else if (job_mem_freq_value)
		*mem_freq_num = job_mem_freq_value;
	else if (def_mem_freq_code)
		*mem_freq_num = def_mem_freq_code;
	else if (def_mem_freq_value)
		*mem_freq_num = def_mem_freq_value;
}

extern int gpu_common_sort_freq_descending(const void *a, const void *b)
{
	return (*(unsigned long*)b - *(unsigned long*)a);
}
