# usage:
#   void *vmemcpy(void *dest, void *src, long long len)
#
# function:
#  Copy len bytes of memory from src to dest by vector intsructions.
#
# restriction:
#  dest and src must be aligned to 8 byte and len must be a multiple of 8
#  otherwise program aborts .
#
	global	_int64_vmemcpy
	text
linkage:
	using	linkage,$s38
	llong   0xffffffffffffffff
	long    code-linkage
	long    0x00000007
	llong   0
	llong   0
	rlong   7,0
	long    7
	str     "vmemcpy"
	rstr    25," "
#
code:
_int64_vmemcpy:
	lea	$s38,linkage-code(,$s33)
	smvl	$s39
	lds	$s40,8(,$s34)	#get args
	lds	$s41,16(,$s34)
	lds	$s42,24(,$s34)
	or	$s123,0,$s40	#set return value
	ble	$s42,0(,$s32) 	#return if len <=0
	and	$s43,7,$s42
	bne	$s43,0		# abort if mod(len,8)<>0
#
	srl	$s42,$s42,3
	add	$s43,-1,$s42
	add	$s44,-1,$s39
	and	$s45,$s43,$s44
	add	$s46,1,$s45 	#mod(len,MAXVL)
	or	$s50,0,(0)1
L1:
	lvl	$s46		#set vector length
	lea	$s47,0($s50,$s41)
	vld	$vs1,8,$s47
	lea	$s48,0($s50,$s40)
	vst	$vs1,8,$s48
	sll	$s51,$s46,3
	sbx	$s42,$s42,$s46
	adx	$s50,$s50,$s51
	or	$s46,0,$s39
	bne	$s42,L1
	be	0,0(,$s32)
