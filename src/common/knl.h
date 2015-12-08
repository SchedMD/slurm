/*****************************************************************************\
 *  knl.h - Infrastructure for Intel Knights Landing processor
 *****************************************************************************
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _SLURM_COMMON_KNL_H
#define _SLURM_COMMON_KNL_H

#include "slurm/slurm.h"

/*
 * Return the count of MCDRAM bits set
 */
extern int knl_mcdram_bits_cnt(uint16_t mcdram_num);

/*
 * Translate KNL MCDRAM string to equivalent numeric value
 */
extern uint16_t knl_mcdram_parse(char *mcdram_str);

/*
 * Translate KNL MCDRAM number to equivalent string value
 * Caller must free return value
 */
extern char *knl_mcdram_str(uint16_t mcdram_num);

/*
 * Return the count of NUMA bits set
 */
extern int knl_numa_bits_cnt(uint16_t numa_num);

/*
 * Translate KNL NUMA string to equivalent numeric value
 */
extern uint16_t knl_numa_parse(char *numa_str);

/*
 * Translate KNL NUMA number to equivalent string value
 * Caller must free return value
 */
extern char *knl_numa_str(uint16_t numa_num);

#endif /* !_SLURM_COMMON_KNL_H */
