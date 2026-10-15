	.file	"hrt.c"
	.option nopic
	.attribute arch, "rv32e2p0_m2p0_c2p0_zicsr2p0"
	.attribute unaligned_access, 0
	.attribute stack_align, 4
	.text
	.section	.text.bench_hrt_tx,"ax",@progbits
	.align	1
	.globl	bench_hrt_tx
	.type	bench_hrt_tx, @function
bench_hrt_tx:
 #APP
	.option push
.option norvc
lw t0, 0(a0)
srli t1, t0, 16
csrw 2003, t1
csrw 2005, t1
csrw 3008, t0
1:
addi a1, a1, -1
beqz a1, 2f
addi a0, a0, 4
lw t0, 0(a0)
srli t1, t0, 16
csrw 2003, t1
csrw 2010, zero
csrw 3008, t0
j 1b
2: csrw 2010, zero
csrw 3009, zero
csrw 2005, zero
.option pop

 #NO_APP
	ret
	.size	bench_hrt_tx, .-bench_hrt_tx
	.section	.text.bench_hrt_tx_sample,"ax",@progbits
	.align	1
	.globl	bench_hrt_tx_sample
	.type	bench_hrt_tx_sample, @function
bench_hrt_tx_sample:
 #APP
	.option push
.option norvc
lw t0, 0(a0)
srli t1, t0, 16
csrw 2003, t1
csrw 2005, t1
csrw 3008, t0
1: .rept 16
nop
.endr
csrr t1, 3010
sh t1, 0(a2)
addi a2, a2, 2
addi a1, a1, -1
beqz a1, 2f
addi a0, a0, 4
lw t0, 0(a0)
srli t1, t0, 16
csrw 2003, t1
csrw 2010, zero
csrw 3008, t0
j 1b
2: csrw 2010, zero
csrw 3009, zero
csrw 2005, zero
.option pop

 #NO_APP
	ret
	.size	bench_hrt_tx_sample, .-bench_hrt_tx_sample
	.section	.note.GNU-stack,"",@progbits
