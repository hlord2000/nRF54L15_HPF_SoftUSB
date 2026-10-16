	.file	"hrt.c"
	.option nopic
	.attribute arch, "rv32e2p0_m2p0_c2p0_zicsr2p0"
	.attribute unaligned_access, 0
	.attribute stack_align, 4
	.text
	.section	.text.su_now,"ax",@progbits
	.align	1
	.globl	su_now
	.type	su_now, @function
su_now:
 #APP
	csrr a5, (0x000007D7ul)
 #NO_APP
	lui	a4,%hi(time_last16)
	lw	a0,%lo(time_last16)(a4)
	slli	a5,a5,16
	srli	a5,a5,16
	lui	a3,%hi(time_acc)
	lw	a2,%lo(time_acc)(a3)
	sub	a0,a0,a5
	slli	a0,a0,16
	srli	a0,a0,16
	add	a0,a0,a2
	sw	a0,%lo(time_acc)(a3)
	sw	a5,%lo(time_last16)(a4)
	ret
	.size	su_now, .-su_now
	.section	.text.su_time_init,"ax",@progbits
	.align	1
	.globl	su_time_init
	.type	su_time_init, @function
su_time_init:
	li	a5,1
 #APP
	csrw (0x000007D1ul), a5
 #NO_APP
	li	a5,65536
	addi	a5,a5,-1
 #APP
	csrw (0x000007D7ul), a5
	csrr a5, (0x000007D7ul)
 #NO_APP
	slli	a5,a5,16
	srli	a5,a5,16
	lui	a4,%hi(time_last16)
	sw	a5,%lo(time_last16)(a4)
	lui	a5,%hi(time_acc)
	sw	zero,%lo(time_acc)(a5)
	ret
	.size	su_time_init, .-su_time_init
	.section	.text.su_rx,"ax",@progbits
	.align	1
	.globl	su_rx
	.type	su_rx, @function
su_rx:
	addi	sp,sp,-28
	sw	s0,20(sp)
	sw	s1,16(sp)
	sw	ra,24(sp)
	sw	a0,0(sp)
	mv	s0,a1
	mv	s1,a2
	sw	a3,4(sp)
	call	su_now
	mv	a4,a0
.L8:
	li	a2,8
	li	a3,0
.L5:
 #APP
	csrr a5, (0x00000BC2ul)
 #NO_APP
	andi	a1,a5,16
	bne	a1,zero,.L19
	andi	a5,a5,1
	addi	a2,a2,-1
	or	a3,a3,a5
	bne	a2,zero,.L5
	sw	a4,12(sp)
	sw	a3,8(sp)
	call	su_now
	lw	a3,8(sp)
	lw	a4,12(sp)
	beq	a3,zero,.L6
	lui	a5,%hi(su_last_activity)
	sw	a0,%lo(su_last_activity)(a5)
.L6:
	sub	a5,a0,a4
	bleu	a5,s1,.L8
	li	s0,0
	j	.L3
.L9:
	addi	a4,a4,-1
	bne	a4,zero,.L4
	call	su_now
	lui	a5,%hi(su_last_activity)
	sw	a0,%lo(su_last_activity)(a5)
	li	s0,-4
.L3:
	lw	ra,24(sp)
	mv	a0,s0
	lw	s0,20(sp)
	lw	s1,16(sp)
	addi	sp,sp,28
	jr	ra
.L19:
	li	a4,100
.L4:
 #APP
	csrr a5, (0x00000BC2ul)
 #NO_APP
	andi	a5,a5,16
	bne	a5,zero,.L9
	li	a5,84
 #APP
	csrw (0x000007D3ul), a5
 #NO_APP
	li	a5,36
 #APP
	csrw (0x000007D5ul), a5
 #NO_APP
	li	a3,1
	li	a0,0
	li	a2,0
	li	a1,0
	li	t1,2
	mv	a5,a3
	li	t2,6
	li	s1,8
.L10:
 #APP
	csrw (0x000007DAul), zero
	csrr a4, (0x00000BC2ul)
 #NO_APP
	beq	t1,zero,.L21
	addi	t1,t1,-1
	li	t0,84
.L11:
 #APP
	csrw (0x000007D3ul), t0
 #NO_APP
	andi	t0,a4,17
	beq	t0,zero,.L12
	srli	a4,a4,4
	andi	a4,a4,1
	bne	a1,t2,.L13
	beq	a5,a4,.L22
	li	a1,0
.L15:
	mv	a5,a4
	j	.L10
.L21:
	li	t1,2
	li	t0,85
	j	.L11
.L13:
	addi	a1,a1,1
	beq	a5,a4,.L16
	li	a1,0
.L16:
	sub	a5,a5,a4
	seqz	a5,a5
	srli	a2,a2,1
	slli	a5,a5,7
	addi	a3,a3,1
	or	a2,a5,a2
	bne	a3,s1,.L15
	beq	a0,s0,.L24
	lw	a5,0(sp)
	li	a3,0
	add	a5,a5,a0
	sb	a2,0(a5)
	addi	a0,a0,1
	j	.L15
.L12:
	mv	s0,a0
.L14:
	li	a5,150
	li	a3,1
.L18:
 #APP
	csrr a4, (0x00000BC2ul)
 #NO_APP
	andi	a4,a4,17
	beq	a4,a3,.L17
	addi	a5,a5,-1
	bne	a5,zero,.L18
.L17:
	call	su_now
	lw	a5,4(sp)
	sw	a0,0(a5)
	lui	a5,%hi(su_last_activity)
	sw	a0,%lo(su_last_activity)(a5)
	j	.L3
.L22:
	li	s0,-2
	j	.L14
.L24:
	li	s0,-3
	j	.L14
	.size	su_rx, .-su_rx
	.section	.text.su_tx,"ax",@progbits
	.align	1
	.globl	su_tx
	.type	su_tx, @function
su_tx:
	addi	sp,sp,-16
	sw	s0,8(sp)
	sw	s1,4(sp)
	sw	ra,12(sp)
	mv	s1,a0
	mv	s0,a1
.L33:
	sw	a2,0(sp)
	call	su_now
	lw	a2,0(sp)
	sub	a0,a0,a2
	blt	a0,zero,.L33
	li	a4,1
 #APP
	csrw (0x00000BC0ul), a4
 #NO_APP
	li	a5,17
 #APP
	csrw (0x00000BC1ul), a5
 #NO_APP
	li	a5,84
 #APP
	csrw (0x000007D3ul), a5
 #NO_APP
	li	a3,8
 #APP
	csrw (0x000007D5ul), a3
 #NO_APP
	li	a5,2
	li	a0,128
	li	a1,0
	li	t1,6
.L34:
	andi	a2,a0,1
	beq	a2,zero,.L35
	addi	a2,a1,1
.L36:
 #APP
	csrw (0x000007DAul), zero
	csrw (0x00000BC0ul), a4
 #NO_APP
	beq	a5,zero,.L46
	addi	a5,a5,-1
	li	a1,84
.L37:
 #APP
	csrw (0x000007D3ul), a1
 #NO_APP
	bne	a2,t1,.L38
	xori	a4,a4,17
 #APP
	csrw (0x000007DAul), zero
	csrw (0x00000BC0ul), a4
 #NO_APP
	beq	a5,zero,.L47
	addi	a5,a5,-1
	li	a2,84
.L39:
 #APP
	csrw (0x000007D3ul), a2
 #NO_APP
	li	a2,0
.L38:
	addi	a3,a3,-1
	bne	a3,zero,.L40
	beq	s0,zero,.L41
	lbu	a0,0(s1)
	addi	s0,s0,-1
	addi	s1,s1,1
	li	a3,8
	j	.L42
.L40:
	srli	a0,a0,1
.L42:
	mv	a1,a2
	j	.L34
.L35:
	xori	a4,a4,17
	j	.L36
.L46:
	li	a5,2
	li	a1,85
	j	.L37
.L47:
	li	a5,2
	li	a2,85
	j	.L39
.L41:
 #APP
	csrw (0x000007DAul), zero
	csrw (0x00000BC0ul), s0
 #NO_APP
	beq	a5,zero,.L48
	addi	a5,a5,-1
	li	a4,84
.L43:
 #APP
	csrw (0x000007D3ul), a4
	csrw (0x000007DAul), zero
 #NO_APP
	beq	a5,zero,.L49
	addi	a5,a5,-1
	li	a4,84
.L44:
 #APP
	csrw (0x000007D3ul), a4
 #NO_APP
	li	a4,1
 #APP
	csrw (0x000007DAul), zero
	csrw (0x00000BC0ul), a4
 #NO_APP
	seqz	a5,a5
	addi	a5,a5,84
 #APP
	csrw (0x000007D3ul), a5
	csrw (0x000007DAul), zero
 #NO_APP
	li	a5,0
 #APP
	csrw (0x00000BC1ul), a5
 #NO_APP
	lw	ra,12(sp)
	lw	s0,8(sp)
	lw	s1,4(sp)
	addi	sp,sp,16
	jr	ra
.L48:
	li	a5,2
	li	a4,85
	j	.L43
.L49:
	li	a5,2
	li	a4,85
	j	.L44
	.size	su_tx, .-su_tx
	.section	.sbss.time_last16,"aw",@nobits
	.align	2
	.type	time_last16, @object
	.size	time_last16, 4
time_last16:
	.zero	4
	.section	.sbss.time_acc,"aw",@nobits
	.align	2
	.type	time_acc, @object
	.size	time_acc, 4
time_acc:
	.zero	4
	.globl	su_last_activity
	.section	.sbss.su_last_activity,"aw",@nobits
	.align	2
	.type	su_last_activity, @object
	.size	su_last_activity, 4
su_last_activity:
	.zero	4
	.section	.note.GNU-stack,"",@progbits
