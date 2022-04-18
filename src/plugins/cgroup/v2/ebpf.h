/*****************************************************************************\
 *  ebpf.h - header file, library to handle BPF cgroup device constrains
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Oriol Vilarrubi <jvilarru@schedmd.com>
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

#ifndef _EBPF_H
#define _EBPF_H

#include <fcntl.h>
#include <linux/bpf.h>
#include <sys/syscall.h>

#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"

typedef struct bpf_program {
	size_t n_inst;
	size_t prog_size;
	struct bpf_insn *program;
} bpf_program_t;

#define EBPF_ACCEPT true
#define EBPF_DENY false
#define INIT_INST 6
#define CLOSE_INST 2

/*
 * init_ebpf_prog - Initialize the bpf_program struct and include the INIT_INST
 * instructions to it.
 * OUT program - Pointer to the bpf_program_t to be initialized
 */
extern void init_ebpf_prog(bpf_program_t *program);

/*
 * add_device_ebpf_prog - Add the instructions to accept or deny (based on the
 * parameter accept) the device specified with dev_type, major and minor to the
 * program.
 * OUT program - Pointer to the bpf_program_t to add the device rule to.
 * IN dev_type - can be BPF_DEVCG_DEV_BLOCK, BPF_DEVCG_DEV_CHAR or 0, if 0 is
 *               passed then the device type check is skipped.
 * IN major - The major id of the device, if this parameter is NO_VAL then the
 *            major check is skipped.
 * IN minor - The minor id of the device, if this parameter is NO_VAL then the
 *            minor check is skipped.
 * RET SLURM_SUCCESS or SLURM_ERROR if more than 2 checks are skipped.
 */
extern int add_device_ebpf_prog(bpf_program_t *program, uint32_t dev_type,
				uint32_t major, uint32_t minor, bool accept);
/*
 * close_ebpf_prog - Adds the closing instructions to the bpf_program, this is
 * the action that the program will do if none of the rules (added using
 * add_device_ebpf_prog) are met.
 * OUT close_ebpf_prog - Pointer to the bpf_program_t to be closed.
 * IN def_action - What to set as default action allow any device(True) or
 *                 deny(False)
 */
extern void close_ebpf_prog(bpf_program_t *close_ebpf_prog, bool def_action);

/*
 * load_ebpf_prog - Loads the program and attaches it to a cgroup.
 * OUT program - Pointer to the bpf_program_t to be loaded
 * IN cgroup_path - Path to the cgroup the program needs to be attached to.
 * IN override_flag - true sets BPF_F_ALLOW_OVERRIDE flag to the program, this
 *		      indicates that any descendent cgroups bpf program will
 *		      override this bpf program
 * RET SLURM_SUCCESS on successfull load, SLURM_ERROR otherwise.
 */
extern int load_ebpf_prog(bpf_program_t *program, const char cgroup_path[],
			  bool override_flag);

/*
 * free_ebpf_prog -Frees the memory allocated by the program
 * OUT program - Pointer to the bpf_program_t to free
 */
extern void free_ebpf_prog(bpf_program_t *program);

#endif
