# usage:
#  void syncvset0(int *ready)
#
# function:
#   *ready = 0 (by vector instr) after sync all memory write
#
	global	syncvset0
#
	data
	bss	bssarea,8
	set	dummy,0
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
	long    9
	str     "syncvset0"
	rstr    23," "
abssarea:	llong	bssarea
#
code:
syncvset0:
	lea	$s38,linkage-code(,$s33)
	lds	$s43,abssarea-linkage(,$s38)
	lea	$s42,dummy(,$s43)
	ts1am	$s41,0,$s42
	lds	$s40,8(,$s34)	#get arg
        lvl	1
	vbrd	$vs1,0
	vstl	$vs1,4,$s40	
	be	0,0(,$s32)
