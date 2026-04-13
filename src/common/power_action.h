/*****************************************************************************\
 *  power_action.h - Configuration and helpers for PowerAction entries used to
 *  run node power up, power down, and reboot scripts on either the slurmctld
 *  or slurmd host.
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

#ifndef _POWER_ACTION_H
#define _POWER_ACTION_H

#include <stdbool.h>
#include <stdint.h>

#include "src/common/list.h"

#define POWER_ACTION_NAME_SUSPEND "__suspendprog__"
#define POWER_ACTION_NAME_RESUME "__resumeprog__"
#define POWER_ACTION_NAME_RESUME_FAIL "__resumefailprog__"
#define POWER_ACTION_NAME_REBOOT "__rebootprog__"

typedef enum power_action_type {
	POWER_ACTION_RESUME,
	POWER_ACTION_SUSPEND,
	POWER_ACTION_RESUME_FAIL,
	POWER_ACTION_REBOOT,
} power_action_type_t;

typedef struct power_action {
	uint32_t argc;
	char **argv;
	char *name;
	bool on_slurmctld;
	char *program;
} power_action_t;

/*
 * Find a power action by name
 *
 * IN name - name of the power action to find
 * IN power_action_list - list of power actions to search
 *
 * RET pointer to the power action if found, NULL otherwise
 */
extern power_action_t *power_action_find(list_t *power_action_list, char *name);

/*
 * Find or create a power action by name. Used to generate an action for
 * SuspendProgram, ResumeProgram, ResumeFailProgram, and RebootProgram
 * if they're not set to an existing action.
 *
 * IN power_action_list - list of power actions to search
 * IN field - name of the power action to find or create
 * IN type - type of the power action to find or create
 *
 * RET pointer to the power action if found or created, NULL otherwise
 */
extern power_action_t *power_action_find_or_create(list_t *power_action_list,
						   char *field,
						   power_action_type_t type);

/*
 * Check if a power action and its program are valid.
 *
 * IN action - pointer to the power action to check
 *
 * RET true if the power action is valid, false otherwise
 */
extern bool power_action_valid_prog(power_action_t *action);

/*
 * Get the auto-generated name of a power action type.
 *
 * IN type - power action type
 *
 * RET name to use for the power action
 */
extern const char *power_action_type_name(power_action_type_t type);

/*
 * Copy a power action
 *
 * IN action - pointer to the power action to copy
 *
 * RET pointer to the copied power action
 */
extern power_action_t *power_action_copy(power_action_t *action);

/*
 * Create a power action
 *
 * IN name - name of the power action
 * IN program - program to run
 * IN on_slurmctld - true if the power action is to be run on the slurmctld host
 * IN action_ptr - pointer to the power action to create
 */
extern int power_action_create(const char *name, char *program,
			       bool on_slurmctld, power_action_t **action_ptr);

/*
 * Free memory allocated for a power action.
 */
extern void power_action_destroy(void *ptr);

#endif /* _POWER_ACTION_H */
