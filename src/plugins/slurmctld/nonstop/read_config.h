/*****************************************************************************\
 *  read_config.h - Define symbols used to read configuration file for
 *  slurmctld/nonstop plugin
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#ifndef _HAVE_NONSTOP_READ_CONFIG_H
#define _HAVE_NONSTOP_READ_CONFIG_H

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDBOOL_H
#    include <stdbool.h>
#  else
     typedef enum {false, true} bool;
#  endif			/* !HAVE_STDBOOL_H */
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#endif

#define GPL_LICENSED 1
#include <munge.h>
#include <unistd.h>
#include <sys/types.h>

#define DEFAULT_NONSTOP_PORT		6820

/* Configuration data types */
typedef struct spare_node_resv {
	uint32_t node_cnt;	/* count of hot spare nodes		*/
	char *partition;	/* name of partition to be used		*/
	struct part_record *part_ptr;	/* pointer to partition used	*/
} spare_node_resv_t;

extern char *nonstop_control_addr;
extern char *nonstop_backup_addr;
extern uint16_t nonstop_comm_port;

extern int hot_spare_info_cnt;
extern spare_node_resv_t *hot_spare_info;
extern char *hot_spare_count_str;
extern uint32_t max_spare_node_count;
extern uint16_t nonstop_debug;
extern uint16_t time_limit_delay;
extern uint16_t time_limit_drop;
extern uint16_t time_limit_extend;

extern int user_drain_allow_cnt;
extern uid_t *user_drain_allow;
extern char *user_drain_allow_str;
extern int user_drain_deny_cnt;
extern uid_t *user_drain_deny;
extern char *user_drain_deny_str;

extern munge_ctx_t ctx;

/* Configuration functions */

/* Load configuration file contents into global variables.
 * Call nonstop_free_config to free memory. */
extern void nonstop_read_config(void);
extern void nonstop_free_config(void);

/* Create reservations to contain hot-spare nodes
 * and purge vestigial reservations */
extern void create_hot_spare_resv(void);

#endif	/* _HAVE_NONSTOP_READ_CONFIG_H */
