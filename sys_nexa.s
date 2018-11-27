//
// mathnext.s
// NextSTEP x86 assembly-language math routines.

#include "asm_i386.h"
#include "quakeasm.h"

#if id386

	.data

	.align	4

.globl	ceil_cw, single_cw, full_cw, cw
ceil_cw:	.long	0
single_cw:	.long	0
full_cw:	.long	0
cw:			.long	0

	.text

.globl C(Sys_DoSetFPCW)
C(Sys_DoSetFPCW):
	fnstcw	cw
	movl	cw,%eax
	movl	%eax,full_cw

	andb	$0xF3,%ah
	orb		$0x0C,%ah	// chop mode
	movl	%eax,single_cw

	andb	$0xF3,%ah
	orb		$0x08,%ah	// ceil mode
	movl	%eax,ceil_cw

	ret

.globl C(Sys_LowFPPrecision)
C(Sys_LowFPPrecision):
	fldcw	single_cw

	ret

.globl C(Sys_HighFPPrecision)
C(Sys_HighFPPrecision):
	fldcw	full_cw

	ret

#endif	// id386

