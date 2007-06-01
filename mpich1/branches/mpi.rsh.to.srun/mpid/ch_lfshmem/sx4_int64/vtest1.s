# usage:
#  int vtest1(long long *ready)
#
# function:
#   return(*ready ==1) (by vector instr).
#
	global	_int64_vtest1
#
	text
linkage:
	using	linkage,$s38
	llong   0xffffffffffffffff
	long    code-linkage
	long    0x00000007
	llong   0
	llong   0
	rlong   7,0
	long    6
	str     "vtest1"
	rstr    26," "
#
code:
_int64_vtest1:
	lds	$s40,8(,$s34)	#get arg
        lvl	1
	vld	$vs0,8,$s40	#load *arg by vector instr
	lvs	$s41,$vs0(0)	#move it to a scalar reg.
	or	$s123,0,(0)1
	or	$s42,1,(0)1
	cpx	$s43,$s41,$s42	#compare it to 1
	bne	$s43,0(,$s32)	#if <>1 return 0
	or	$s123,1,(0)1	#if ==1 return 1
	be	0,0(,$s32)
