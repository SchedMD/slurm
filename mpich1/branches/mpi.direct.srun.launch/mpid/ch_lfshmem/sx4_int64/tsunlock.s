# usage:
#  void tsunlock(long long *lock)
#
# function:
#       *(char *)lock = 0
#
	global	_int64_tsunlock
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
	long    8
	str     "tsunlock"
	rstr    24," "
#
code:
_int64_tsunlock:
	lds	$s70,8(,$s34)	#get arg
	or	$s71,0,(0)1	# s71 = 00000000 0 0 0
	or	$s82,0,(56)0
	or	$s83,0,(57)1
	and	$s78,$s82,$s83  # s78 = 0 0 0 10000000
	ts1am	$s71,$s78,$s70
	be	0,0(,$s32)
