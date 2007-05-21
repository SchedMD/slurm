# $Id: rs6000.s,v 1.1.1.1 1997/09/17 20:39:43 gropp Exp $
#
# $Source: /home/MPI/cvsMaster/mpich/mpid/ch_p4/rs6000.s,v $
#
# $Log: rs6000.s,v $
# Revision 1.1.1.1  1997/09/17 20:39:43  gropp
# MPICH
#
# Revision 1.1  1994/10/25  20:03:06  kohr
# Initial revision
#
#
# Hand-edited compiler output for UTP_readTime-rs6000.c

# This assembler file was generated using:
#	xlc -O2 -qlanglvl=ansi -S UTP_readTime-rs6000.c

# References to _rtcu and _rtcl have been replaced by hand by the
# appropriate "mfspr" instructions for accessing the corresponding halves
# of the RTC.

# I've developed an RS6000 assembly language routine which reads the on-chip
# real-time clock directly.  The resulting time value has a resolution of 256
# nanoseconds (so the clock runs at about 4 Megahertz); it works identically
# on the SP.  This is the same resolution as you get from the standard
# routine gettimer(), but this assembly language routine has the advantage
# that the execution time is only about 450 nanoseconds, as opposed to about
# 9 microseconds for gettimer().  Also the variation in execution time is
# lower because the cache is disturbed less.
# 
# The ANSI C function prototype for the function is this:
# 
# 	extern void UTP_readTime(struct timestruc_t *tv);
# 
# where "struct timestruc_t" is defined in the header file <sys/time.h>, and
# looks like this:
# 
# 	struct timestruc_t {
# 		unsigned long tv_sec;	/* seconds		*/
# 		long          tv_nsec;	/* and nanoseconds	*/
# 	};
# 
# An example of using the routine is this:
# 
# 	#include <stdio.h>
# 	#include <sys/time.h>
# 
# 	struct timestruc_t tv;
# 
# 	UTP_readTime(&tv);
# 
# 	printf("seconds: %lu,  nanoseconds: %l\n", tv.tv_sec, tv.tv_nsec);
# 
# 	
.set r0,0; .set SP,1; .set RTOC,2; .set r3,3; .set r4,4
.set r5,5; .set r6,6; .set r7,7; .set r8,8; .set r9,9
.set r10,10; .set r11,11; .set r12,12; .set r13,13; .set r14,14
.set r15,15; .set r16,16; .set r17,17; .set r18,18; .set r19,19
.set r20,20; .set r21,21; .set r22,22; .set r23,23; .set r24,24
.set r25,25; .set r26,26; .set r27,27; .set r28,28; .set r29,29
.set r30,30; .set r31,31
.set fp0,0; .set fp1,1; .set fp2,2; .set fp3,3; .set fp4,4
.set fp5,5; .set fp6,6; .set fp7,7; .set fp8,8; .set fp9,9
.set fp10,10; .set fp11,11; .set fp12,12; .set fp13,13; .set fp14,14
.set fp15,15; .set fp16,16; .set fp17,17; .set fp18,18; .set fp19,19
.set fp20,20; .set fp21,21; .set fp22,22; .set fp23,23; .set fp24,24
.set fp25,25; .set fp26,26; .set fp27,27; .set fp28,28; .set fp29,29
.set fp30,30; .set fp31,31
.set MQ,0; .set XER,1; .set FROM_RTCU,4; .set FROM_RTCL,5; .set FROM_DEC,6
.set LR,8; .set CTR,9; .set TID,17; .set DSISR,18; .set DAR,19; .set TO_RTCU,20
.set TO_RTCL,21; .set TO_DEC,22; .set SDR_0,24; .set SDR_1,25; .set SRR_0,26
.set SRR_1,27
.set BO_dCTR_NZERO_AND_NOT,0; .set BO_dCTR_NZERO_AND_NOT_1,1
.set BO_dCTR_ZERO_AND_NOT,2; .set BO_dCTR_ZERO_AND_NOT_1,3
.set BO_IF_NOT,4; .set BO_IF_NOT_1,5; .set BO_IF_NOT_2,6
.set BO_IF_NOT_3,7; .set BO_dCTR_NZERO_AND,8; .set BO_dCTR_NZERO_AND_1,9
.set BO_dCTR_ZERO_AND,10; .set BO_dCTR_ZERO_AND_1,11; .set BO_IF,12
.set BO_IF_1,13; .set BO_IF_2,14; .set BO_IF_3,15; .set BO_dCTR_NZERO,16
.set BO_dCTR_NZERO_1,17; .set BO_dCTR_ZERO,18; .set BO_dCTR_ZERO_1,19
.set BO_ALWAYS,20; .set BO_ALWAYS_1,21; .set BO_ALWAYS_2,22
.set BO_ALWAYS_3,23; .set BO_dCTR_NZERO_8,24; .set BO_dCTR_NZERO_9,25
.set BO_dCTR_ZERO_8,26; .set BO_dCTR_ZERO_9,27; .set BO_ALWAYS_8,28
.set BO_ALWAYS_9,29; .set BO_ALWAYS_10,30; .set BO_ALWAYS_11,31
.set CR0_LT,0; .set CR0_GT,1; .set CR0_EQ,2; .set CR0_SO,3
.set CR1_FX,4; .set CR1_FEX,5; .set CR1_VX,6; .set CR1_OX,7
.set CR2_LT,8; .set CR2_GT,9; .set CR2_EQ,10; .set CR2_SO,11
.set CR3_LT,12; .set CR3_GT,13; .set CR3_EQ,14; .set CR3_SO,15
.set CR4_LT,16; .set CR4_GT,17; .set CR4_EQ,18; .set CR4_SO,19
.set CR5_LT,20; .set CR5_GT,21; .set CR5_EQ,22; .set CR5_SO,23
.set CR6_LT,24; .set CR6_GT,25; .set CR6_EQ,26; .set CR6_SO,27
.set CR7_LT,28; .set CR7_GT,29; .set CR7_EQ,30; .set CR7_SO,31
.set TO_LT,16; .set TO_GT,8; .set TO_EQ,4; .set TO_LLT,2; .set TO_LGT,1

	.rename	H.10.NO_SYMBOL{PR},""
	.rename	H.18.UTP_readTime{TC},"UTP_readTime"
	.rename	H.22._rtcu{TC},"_rtcu"
	.rename	H.26._rtcl{TC},"_rtcl"

	.lglobl	H.10.NO_SYMBOL{PR}      
	.globl	.UTP_readTime           
	.globl	UTP_readTime{DS}        
	.extern	_rtcu{UA}               
	.extern	_rtcl{UA}               


# .text section


	.csect	H.10.NO_SYMBOL{PR}      
.UTP_readTime:                          # 0x00000000 (H.10.NO_SYMBOL)
	.file	"UTP_readTime-rs6000.c" 
#	l	r4,T.22._rtcu(RTOC)
#	l	r0,0(r4)
	mfspr	r0,4		# copy RTCU to r0.
				# r0 is rtcu_s in original C routine.
#	l	r7,T.26._rtcl(RTOC)
#	l	r5,0(r7)
	mfspr	r5,5		# copy RTCL to r5.
				# r5 is rtcl in original C routine.
#	l	r6,0(r4)
	mfspr	r6,4		# copy RTCU to r6.
				# r6 is rtcu_f in original C routine.
	cmpl	0,r6,r0
	bc	BO_IF,CR0_EQ,__L44
	cal	r0,0(r6)
#	l	r5,0(r7)
	mfspr	r5,5		# copy RTCL to r5.
#	l	r6,0(r4)
	mfspr	r6,4		# copy RTCU to r6.
	cmpl	1,r6,r0
	bc	BO_IF,CR1_VX,__L44
__L30:                                  # 0x00000030 (H.10.NO_SYMBOL+0x30)
	cal	r0,0(r6)
#	l	r5,0(r7)
	mfspr	r5,5		# copy RTCL to r5.
#	l	r6,0(r4)
	mfspr	r6,4		# copy RTCU to r6.
	cmpl	1,r6,r0
	bc	BO_IF_NOT,CR1_VX,__L30
__L44:                                  # 0x00000044 (H.10.NO_SYMBOL+0x44)
	st	r5,4(r3)
	st	r0,0(r3)
	bcr	BO_ALWAYS,CR0_LT
# traceback table
	.long	0x00000000
	.byte	0x00			# VERSION=0
	.byte	0x00			# LANG=TB_C
	.byte	0x20			# IS_GL=0,IS_EPROL=0,HAS_TBOFF=1
					# INT_PROC=0,HAS_CTL=0,TOCLESS=0
					# FP_PRESENT=0,LOG_ABORT=0
	.byte	0x40			# INT_HNDL=0,NAME_PRESENT=1
					# USES_ALLOCA=0,CL_DIS_INV=WALK_ONCOND
					# SAVES_CR=0,SAVES_LR=0
	.byte	0x00			# STORES_BC=0,FPR_SAVED=0
	.byte	0x00			# GPR_SAVED=0
	.byte	0x01			# FIXEDPARMS=1
	.byte	0x00			# FLOATPARMS=0,PARMSONSTK=0
	.long	0x00000000		# 
	.long	0x00000050		# TB_OFFSET
	.short	12			# NAME_LEN
	.byte	"UTP_readTime"
	.byte	0			# padding
	.byte	0			# padding
# End of traceback table
	.long	0x00000000              # "\0\0\0\0"
# End	csect	H.10.NO_SYMBOL{PR}

# .data section


	.toc	                        # 0x00000078 
T.18.UTP_readTime:
	.tc	H.18.UTP_readTime{TC},UTP_readTime{DS}
T.22._rtcu:
	.tc	H.22._rtcu{TC},_rtcu{UA}
T.26._rtcl:
	.tc	H.26._rtcl{TC},_rtcl{UA}


	.csect	UTP_readTime{DS}        
	.long	.UTP_readTime           # "\0\0\0\0"
	.long	TOC{TC0}                # "\0\0\0x"
	.long	0x00000000              # "\0\0\0\0"
# End	csect	UTP_readTime{DS}



# .bss section
