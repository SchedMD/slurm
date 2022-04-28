/*****************************************************************************\
 *  ebpf.c - library to handle BPF cgroup device constrains
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

#define _GNU_SOURCE
#include "ebpf.h"

#define bpf(cmd, attr, size) (int) syscall(__NR_bpf, cmd, attr, size);

/* Macros inspired from libcrun. */
#define BPF_ALU32_IMM(OP, DST, IMM)					\
	((struct bpf_insn) { .code = BPF_ALU | BPF_OP (OP) | BPF_K,	\
		 .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM })

#define BPF_LDX_MEM(SIZE, DST, SRC, OFF)				\
	((struct bpf_insn) { .code = BPF_LDX | BPF_SIZE (SIZE) | BPF_MEM, \
		 .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0 })

#define BPF_JMP_IMM(OP, DST, IMM, OFF)					\
	((struct bpf_insn) { .code = BPF_JMP | BPF_OP (OP) | BPF_K,	\
		 .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM })

#define BPF_MOV64_IMM(DST, IMM)						\
	((struct bpf_insn) { .code = BPF_ALU64 | BPF_MOV | BPF_K,	\
		 .dst_reg = DST, .src_reg = 0, .off = 0, .imm = IMM })

#define BPF_EXIT_INSN()							\
	((struct bpf_insn) { .code = BPF_JMP | BPF_EXIT, .dst_reg = 0,	\
		 .src_reg = 0, .off = 0, .imm = 0 })

extern void init_ebpf_prog(bpf_program_t *program)
{
	/*
	 * The following bpf program represented by the struct array init_dev
	 * will take care of storing the parameter (a.k.a the request) of the
	 * called function (which is program->program) into different registers,
	 * The request is stored in BPF_REG_1 at the moment of the function call
	 * and is a struct that represents the device that needs to be checked
	 * by the program in order to see whether access is granted or not.
	 *
	 * It looks like:
	 *
	 * struct request {
	 *     int access_type; //int is split into type(low) & access(high)
	 *     int major;
	 *     int minor;
	 * }
	 *
	 * To split type_access the following operation is done:
	 *
	 * int type = access_type & 0x0000FFFF;
	 * int access = access_type >> 16;
	 */
	struct bpf_insn init_dev[] = {
		/* type -> R2.  */
		BPF_LDX_MEM (BPF_W, BPF_REG_2, BPF_REG_1, 0),
		BPF_ALU32_IMM (BPF_AND, BPF_REG_2, 0xFFFF),

		/* access -> R3.  */
		BPF_LDX_MEM (BPF_W, BPF_REG_3, BPF_REG_1, 0),
		BPF_ALU32_IMM (BPF_RSH, BPF_REG_3, 16),

		/* major -> R4.  */
		BPF_LDX_MEM (BPF_W, BPF_REG_4, BPF_REG_1, 4),

		/* minor -> R5.  */
		BPF_LDX_MEM (BPF_W, BPF_REG_5, BPF_REG_1, 8),
	};

	/*
	 * Allocate the size of the init instructions(6) + 2 more instructions
	 * for the ending (close_ebpf_prog). The allocated space is stored in
	 * prog_size so that it can be used in future reallocs.
	 * If the number of init or closing instructions changes remember to
	 * change the define in the header file.
	 */
	program->prog_size = (INIT_INST + CLOSE_INST) * sizeof(struct bpf_insn);
	program->program = xmalloc(program->prog_size);

	/* Copy the init sequence of the program. */
	memcpy(program->program, &init_dev, sizeof(init_dev));
	/*
	 * Save the number of instructions in the program, used when loading the
	 * program.
	 */
	program->n_inst = INIT_INST;
}

extern int add_device_ebpf_prog(bpf_program_t *program, uint32_t dev_type,
				uint32_t major, uint32_t minor, bool accept)
{
	bool has_type = ((dev_type == BPF_DEVCG_DEV_BLOCK)||
			 (dev_type == BPF_DEVCG_DEV_CHAR));
	bool has_major = (major != NO_VAL);
	bool has_minor = (minor != NO_VAL);
	int jump_inst = 1;

	/*
	 * Calculate the needed offset to skip to the next device check.
	 * jump_inst is initialized with 1 to also jump to the "return accept"
	 * block. For example if the device has both major and minor, then the
	 * jump_inst will be 3, we would jump 3 in the major check
	 * (1 instruction for the minor check and 2 for the return accept) and
	 * 2 in the minor check (the return accept block).
	 */
	if (has_type)
		jump_inst++;
	if (has_major)
		jump_inst++;
	if (has_minor)
		jump_inst++;

	/* If none of the conditions is set exit with an error. */
	if (jump_inst == 1) {
		error("%s: At least one parameter needs to not be a wildcard",
		      __func__);
		return SLURM_ERROR;
	}

	/*
	 * Reallocate the space, taking into account that number_instructions
	 * variable is the number of instructions to jump, the number of
	 * instructions added is 1 more, to take into account the first one.
	 */
	program->prog_size += (jump_inst + 1) * sizeof(struct bpf_insn);
	xrealloc(program->program, program->prog_size);

	/*
	 * The remaining logic will insert the C code described in the following
	 * comments into the program as BPF bytecode. Look at the struct request
	 * definition in init_ebpf_prog() to better understand the code. Note
	 * that the access type is not checked here as we allow the devices of
	 * any access type.
	 */

	/*
	 * //R2 = request.type
	 * //if(has_type) == if(dev_type != 'a')
	 * if (dev_type != 'a' && request.type != dev_type)
	 *   goto next_device:
	 */
	if (has_type)
		program->program[program->n_inst++] = BPF_JMP_IMM(BPF_JNE,
								  BPF_REG_2,
								  dev_type,
								  jump_inst--);

	/*
	 * //R4 = request.major
	 * //if(has_major) == if(major != -1)
	 * if (major != -1 && request.major != major)
	 *   goto next_device:
	 */
	if (has_major)
		program->program[program->n_inst++] = BPF_JMP_IMM(BPF_JNE,
								  BPF_REG_4,
								  major,
								  jump_inst--);

	/*
	 * //R5 = request.major
	 * //if(has_minor) == if(minor != -1)
	 * if (minor != -1 && request.minor != minor)
	 *   goto next_device:
	 */
	if (has_minor)
		program->program[program->n_inst++] = BPF_JMP_IMM(BPF_JNE,
								  BPF_REG_5,
								  minor,
								  jump_inst--);

	/*
	 * The "return accept;" piece of code, the return value is stored in R0.
	 * The variable accept is what to do with the device (accept/deny).
	 */
	program->program[program->n_inst++] = BPF_MOV64_IMM(BPF_REG_0, accept);
	program->program[program->n_inst++] = BPF_EXIT_INSN();

	/*
	 * Add future devices below this line
	 * next_device:
	 */

	return SLURM_SUCCESS;
}

extern void close_ebpf_prog(bpf_program_t *program, bool def_action)
{
	/* This is the same code as the return accept block in add_device. */
	program->program[program->n_inst++] = BPF_MOV64_IMM(BPF_REG_0,
							    def_action);
	program->program[program->n_inst++] = BPF_EXIT_INSN();
}

extern int load_ebpf_prog(bpf_program_t *program, const char cgroup_path[],
			  bool override_flag)
{
	int dirfd, ret, fd = -1;
	char log[8192] = "\0";
	union bpf_attr attr;

	/*
	 * Open the cgroup directory to get the fd for later use in the cgroup
	 * attach syscall.
	 */
	dirfd = open(cgroup_path, O_DIRECTORY);
	if (dirfd < 0) {
		error("%s: cannot open cgroup (%s): %m", __func__, cgroup_path);
		return SLURM_ERROR;
	}

	/*
	 * Prepare all the attributes to verify and load the bpf program.
	 * With the fd of the loaded program then we can associate it with the
	 * cgroup.
	 */
	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_CGROUP_DEVICE;
	attr.insns = (size_t) program->program;
	attr.insn_cnt = program->n_inst;
	/* We set the license to GPL to use helper functions marked gpl_only. */
	attr.license = (size_t) "GPL";
	strlcpy(attr.prog_name, "Slurm_Cgroup_v2", BPF_OBJ_NAME_LEN);
	attr.log_level = 1;
	attr.log_buf = (size_t) log;
	attr.log_size = sizeof(log);

	/* Call the load syscall */
	fd = bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
	if (fd < 0) {
		error("%s: BPF load error (%m). Please check your system limits (MEMLOCK).",
		      __func__);
		return SLURM_ERROR;
	}

	/*
	 * Erase the whole attr union so that is can be reused to attach the bpf
	 * program to the cgroup, if override_flag is true then also add the
	 * BPF_F_ALLOW_OVERRIDE flag, what this flag does is that any descendent
	 * cgroups will be able to override effective bpf program that was
	 * inherited from this cgroup, this flag is specified for all "non-leaf"
	 * cgroups.
	 */
	memset(&attr, 0, sizeof(attr));
	attr.attach_type = BPF_CGROUP_DEVICE;
	attr.target_fd = dirfd;
	attr.attach_bpf_fd = fd;
	if (override_flag)
		attr.attach_flags = BPF_F_ALLOW_OVERRIDE;

	/* Call the attach syscall */
	ret = bpf(BPF_PROG_ATTACH, &attr, sizeof(attr));
	if (ret < 0) {
		error("%s: BPF attach: %d: %m", __func__, ret);
		close(dirfd);
		return SLURM_ERROR;
	}

	close(dirfd);
	return SLURM_SUCCESS;
}

extern void free_ebpf_prog(bpf_program_t *program)
{
	xfree(program->program);
}
