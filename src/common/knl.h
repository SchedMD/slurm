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

/*****************************************************************************\
 *  KNL configuration file managment functions
\*****************************************************************************/

/*
 * Parse knl.conf file and return available and default modes
 * avail_mcdram IN - available MCDRAM modes
 * avail_numa IN - available NUMA modes
 * default_mcdram IN - default MCDRAM mode
 * default_numa IN - default NUMA mode
 * RET - Slurm error code
 */
extern int knl_conf_read(uint16_t *avail_mcdram, uint16_t *avail_numa,
			 uint16_t *default_mcdram, uint16_t *default_numa);

/*
 * Return the count of MCDRAM bits set
 */
extern int knl_mcdram_bits_cnt(uint16_t mcdram_num);

/*
 * Translate KNL MCDRAM string to equivalent numeric value
 * mcdram_str IN - String to scan
 * sep IN - token separator to search for
 * RET MCDRAM numeric value
 */
extern uint16_t knl_mcdram_parse(char *mcdram_str, char *sep);

/*
 * Given a KNL MCDRAM token, return its equivalent numeric value
 * token IN - String to scan
 * RET MCDRAM numeric value
 */
extern uint16_t knl_mcdram_token(char *token);

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
 * Given a KNL NUMA token, return its equivalent numeric value
 * token IN - String to scan
 * RET NUMA numeric value
 */
extern uint16_t knl_numa_token(char *token);

/*
 * Translate KNL NUMA string to equivalent numeric value
 * numa_str IN - String to scan
 * sep IN - token separator to search for
 * RET NUMA numeric value
 */
extern uint16_t knl_numa_parse(char *numa_str, char *sep);

/*
 * Translate KNL NUMA number to equivalent string value
 * Caller must free return value
 */
extern char *knl_numa_str(uint16_t numa_num);

/*****************************************************************************\
 *  KNL node management functions, uses plugin
\*****************************************************************************/

extern int slurm_knl_g_init(void);

extern int slurm_knl_g_fini(void);

extern int slurm_knl_g_status(char *node_list);

extern int slurm_knl_g_boot(char *node_list, char *mcdram_type,char *numa_type);

#endif /* !_SLURM_COMMON_KNL_H */
