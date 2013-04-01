	.file	"add-ori.bc"
	.text
	.globl	adds32
	.align	16, 0x90
	.type	adds32,@function
adds32:                                 # @adds32
	.cfi_startproc
# BB#0:                                 # %entry
	addl	%esi, %edi
	movl	%edi, %eax
	ret
.Ltmp0:
	.size	adds32, .Ltmp0-adds32
	.cfi_endproc

	.globl	addu64
	.align	16, 0x90
	.type	addu64,@function
addu64:                                 # @addu64
	.cfi_startproc
# BB#0:                                 # %entry
	addq	%rsi, %rdi
	movq	%rdi, %rax
	ret
.Ltmp1:
	.size	addu64, .Ltmp1-addu64
	.cfi_endproc


	.section	".note.GNU-stack","",@progbits
