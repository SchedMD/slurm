# usage:
#   void vreadtest1(int *mask, int *ready, int n)
#
# function:
#   for (i=0; i<n; i++)
#    mask[i] = (read[i] == 1)
#
	global	vreadtest1
	text
linkage:
	using	linkage,$s38
	llong   0xffffffffffffffff
	long    code-linkage
	long    0x00000007
	llong   0
	llong   0
	rlong   7,0
	long    10
	str     "vreadtest1"
	rstr    22," "
#
code:
vreadtest1:
	lea	$s38,linkage-code(,$s33)
	smvl	$s39
	lds	$s40,8(,$s34)	#get mask
	lds	$s41,16(,$s34)	#get ready
	lds	$s42,24(,$s34)	#get n
	ble	$s42,0(,$s32) 	#return if n <=0
#
	add	$s43,-1,$s42
	add	$s44,-1,$s39
	and	$s45,$s43,$s44
	add	$s46,1,$s45 	#mod(n,MAXVL)
	or	$s50,0,(0)1
L1:
	lvl	$s46		#set vector length
	vbrd	$vs1,0
	lea	$s47,0($s50,$s41)
	vldl	$va1,4,$s47
	or	$s52,1,(0)1
	vcpx	$vl0,$s52,$va1
	vfmk	$vm1,12,$vl0
	vbrd*	$vs1,1
	lea	$s48,0($s50,$s40)
	vstl	$vs1,4,$s48
	sll	$s51,$s46,2
	sbx	$s42,$s42,$s46
	adx	$s50,$s50,$s51
	or	$s46,0,$s39
	bne	$s42,L1
	be	0,0(,$s32)
