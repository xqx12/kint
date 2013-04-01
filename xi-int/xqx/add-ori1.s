	.file	"add-ori1.bc"
	.text
	.globl	adds32
	.align	16, 0x90
	.type	adds32,@function
adds32:                                 # @adds32
	.cfi_startproc
# BB#0:                                 # %entry
	movl	%edi, -4(%rsp)
	movl	%esi, -8(%rsp)
	addl	-4(%rsp), %esi
	movl	%esi, %eax
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
	movq	%rdi, -8(%rsp)
	movq	%rsi, -16(%rsp)
	addq	-8(%rsp), %rsi
	movq	%rsi, %rax
	ret
.Ltmp1:
	.size	addu64, .Ltmp1-addu64
	.cfi_endproc


	.section	".note.GNU-stack","",@progbits
