/*****************************************************************************\
 *  gpu_common.h - GPU plugin common header file
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

#ifndef _GPU_COMMON_H
#define _GPU_COMMON_H

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/cgroup.h"
#include "src/common/gpu.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"

#define FREQS_CONCISE   5 /* This must never be smaller than 5, or error */

#define GPU_LOW         ((unsigned int) -1)
#define GPU_MEDIUM      ((unsigned int) -2)
#define GPU_HIGH_M1     ((unsigned int) -3)
#define GPU_HIGH        ((unsigned int) -4)

/*
 * Convert a frequency value to a string
 * Returned string must be xfree()'ed
 */
extern char *gpu_common_freq_value_to_string(unsigned int freq);

/*
 * Convert frequency to nearest valid frequency found in frequency array
 *
 * freq         (IN/OUT) The frequency to check, in MHz. Also the output, if
 *              it needs to be changed.
 * freqs_size   (IN) The size of the freqs array
 * freqs        (IN) An array of frequency values in MHz, sorted highest to
 *              lowest
 *
 * Inspired by src/common/cpu_frequency#_cpu_freq_freqspec_num()
 */
extern void gpu_common_get_nearest_freq(unsigned int *freq,
					unsigned int freqs_size,
					unsigned int *freqs);

extern void gpu_common_parse_gpu_freq(char *gpu_freq,
				      unsigned int *gpu_freq_num,
				      unsigned int *mem_freq_num,
				      bool *verbose_flag);

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
				   int indent);

/*
 * Replace all space characters in a string with underscores, and make all
 * characters lower case.
 */
extern void gpu_common_underscorify_tolower(char *str);

extern int gpu_common_sort_freq_descending(const void *a, const void *b);

#endif /* !_GPU_COMMON_H */
