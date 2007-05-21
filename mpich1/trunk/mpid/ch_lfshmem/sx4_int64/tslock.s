# usage:
#  void tslock(long long *lock)
#
# function:
#   L1: if ((w = *(char *)lock) == 0)  *(char *)lock = 1; /* atomic op */
#       if (w != 0 ) goto L1;
#
	global	_int64_tslock
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
	str     "tslock"
	rstr    26," "
#
code:
_int64_tslock:
	lea	$s38,linkage-code(,$s33)
	lds	$s70,8(,$s34)	#get arg
	or	$s80,0,(8)1
	or	$s81,0,(7)0
	and	$s71,$s80,$s81  # s71 = 00000001 0 0 0
	or	$s82,0,(56)0
	or	$s83,0,(57)1
	and	$s78,$s82,$s83  # s78 = 0 0 0 10000000
L1:
	or	$s72,0,$s71
	ts2am	$s72,$s78,$s70
	bne	$s72,L2		# return if lock is locked
	rcr	1		# clear cache
	be	0,0(,$s32)
L2:
	lea	$s74,10
	stusrcc	$s73		# wait a little
	add	$s73,$s73,$s74
L3:
	stusrcc	$s75
	sub	$s76,$s75,$s73
	bl	$s76,L3
	be	0,L1
