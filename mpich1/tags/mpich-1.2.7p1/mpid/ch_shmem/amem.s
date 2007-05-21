; $CHeader: amem.s 1.2 1995/11/08 15:08:10 $
; Copyright 1992 Convex Computer Corp.
;	int MPID_SHMEM__ldcws(void *addr)
;	int MPID_SHMEM__read32(void *addr)
;
;	Note: addr must be on a 16 byte boundary.

        .space  $TEXT$,sort=8
        .subspa $CODE$,quad=0,align=8,access=44,code_only,sort=24
        .export MPID_SHMEM__ldcws32,entry,priv_lev=3,argw0=gr,rtnval=gr
MPID_SHMEM__ldcws32
        .proc
        .callinfo caller,frame=0
        .entry
        sync
	ldcws	0(0,%arg0),%ret0
	nop
	bv	%r0(%rp)
	.exit
	nop
        .procend

        .space  $TEXT$
        .subspa $CODE$
        .export MPID_SHMEM__read32,entry,priv_lev=3,argw0=gr,rtnval=gr
MPID_SHMEM__read32
        .proc
        .callinfo caller,frame=0
        .entry
	bv	%r0(%rp)
	.exit
	ldws	0(0,%arg0),%ret0
        .procend

        .space  $TEXT$
        .subspa $CODE$
        .export MPID_SHMEM__release_lock,entry,priv_lev=3,argw0=gr,rtnval=gr
MPID_SHMEM__release_lock
        .proc
        .callinfo caller,frame=0
        .entry
        sync
        ldi     1,%r31  ;offset 0x0
        bv      %r0(%r2)        ;offset 0x4
        .exit
        stws    %r31,0(%r26)    ;offset 0x8
        .procend

        .space  $TEXT$
        .subspa $CODE$
        .export MPID_SHMEM_post1,entry,priv_lev=3,argw0=gr,rtnval=gr
MPID_SHMEM_post1
        .proc
        .callinfo caller,frame=0
        .entry
        sync
        ldi     1,%r31  ;offset 0x0
        bv      %r0(%r2)        ;offset 0x4
        .exit
        stws    %r31,0(%r26)    ;offset 0x8
        .procend

        .space  $TEXT$
        .subspa $CODE$
        .export MPID_SHMEM_post0,entry,priv_lev=3,argw0=gr,rtnval=gr
MPID_SHMEM_post0
        .proc
        .callinfo caller,frame=0
        .entry
        sync
        ldi     0,%r31  ;offset 0x0
        bv      %r0(%r2)        ;offset 0x4
        .exit
        stws    %r31,0(%r26)    ;offset 0x8
        .procend

	.space	$TEXT$
	.subspa	$CODE$
	.export __getspace,entry,priv_lev=3,argw0=gr,rtnval=gr
__getspace
	.proc
	.callinfo caller,frame=0
	.entry
	bv	%r0(%rp)
	.exit
	mfsp	%sr4,%ret0
	.procend
	.end
