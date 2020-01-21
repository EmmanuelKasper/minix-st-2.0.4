#
! sections

.sect .text; .sect .rom; .sect .data; .sect .bss

#include <minix/config.h>
#include <minix/const.h>
#include "const.h"
#include "sconst.h"
#include "protect.h"

! This file contains a number of assembly code utility routines needed by the
! kernel.  They are:

.define	_monitor	! exit Minix and return to the monitor
.define	_int86		! let the monitor make an 8086 interrupt call
.define	_cp_mess	! copies messages from source to destination
.define	_exit		! dummy for library routines
.define	__exit		! dummy for library routines
.define	___exit		! dummy for library routines
.define	___main		! dummy for GCC
.define	_phys_insw	! transfer data from (disk controller) port to memory
.define	_phys_insb	! likewise byte by byte
.define	_phys_outsw	! transfer data from memory to (disk controller) port
.define	_phys_outsb	! likewise byte by byte
.define	_enable_irq	! enable an irq at the 8259 controller
.define	_disable_irq	! disable an irq
.define	_phys_copy	! copy data from anywhere to anywhere in memory
.define	_mem_rdw	! copy one word from [segment:offset]
.define	_reset		! reset the system
.define	_mem_vid_copy	! copy data to video ram
.define	_vid_vid_copy	! move data in video ram
.define	_level0		! call a function at level 0

! The routines only guarantee to preserve the registers the C compiler
! expects to be preserved (ebx, esi, edi, ebp, esp, segment registers, and
! direction bit in the flags).

.sect .text
!*===========================================================================*
!*				monitor					     *
!*===========================================================================*
! PUBLIC void monitor();
! Return to the monitor.

_monitor:
	mov	esp, (_mon_sp)		! restore monitor stack pointer
    o16 mov	dx, SS_SELECTOR		! monitor data segment
	mov	ds, dx
	mov	es, dx
	mov	fs, dx
	mov	gs, dx
	mov	ss, dx
	pop	edi
	pop	esi
	pop	ebp
    o16 retf				! return to the monitor


!*===========================================================================*
!*				int86					     *
!*===========================================================================*
! PUBLIC void int86();
_int86:
	cmpb	(_mon_return), 0	! is the monitor there?
	jnz	0f
	movb	ah, 0x01		! an int 13 error seems appropriate
	movb	(_reg86+ 0), ah		! reg86.w.f = 1 (set carry flag)
	movb	(_reg86+13), ah		! reg86.b.ah = 0x01 = "invalid command"
	ret
0:	push	ebp			! save C registers
	push	esi
	push	edi
	push	ebx
	pushf				! save flags
	cli				! no interruptions

	inb	INT2_CTLMASK
	movb	ah, al
	inb	INT_CTLMASK
	push	eax			! save interrupt masks
	mov	eax, (_irq_use)		! map of in-use IRQ`s
	and	eax, ~[1<<CLOCK_IRQ]	! keep the clock ticking
	outb	INT_CTLMASK		! enable all unused IRQ`s and vv.
	movb	al, ah
	outb	INT2_CTLMASK

	mov	eax, SS_SELECTOR	! monitor data segment
	mov	ss, ax
	xchg	esp, (_mon_sp)		! switch stacks
	push	(_reg86+36)		! parameters used in INT call
	push	(_reg86+32)
	push	(_reg86+28)
	push	(_reg86+24)
	push	(_reg86+20)
	push	(_reg86+16)
	push	(_reg86+12)
	push	(_reg86+ 8)
	push	(_reg86+ 4)
	push	(_reg86+ 0)
	mov	ds, ax			! remaining data selectors
	mov	es, ax
	mov	fs, ax
	mov	gs, ax
	push	cs
	push	return			! kernel return address and selector
    o16	jmpf	20+2*4+10*4+2*4(esp)	! make the call
return:
	pop	(_reg86+ 0)
	pop	(_reg86+ 4)
	pop	(_reg86+ 8)
	pop	(_reg86+12)
	pop	(_reg86+16)
	pop	(_reg86+20)
	pop	(_reg86+24)
	pop	(_reg86+28)
	pop	(_reg86+32)
	pop	(_reg86+36)
	lgdt	(_gdt+GDT_SELECTOR)	! reload global descriptor table
	jmpf	CS_SELECTOR:csinit	! restore everything
csinit:	mov	eax, DS_SELECTOR
	mov	ds, ax
	mov	es, ax
	mov	fs, ax
	mov	gs, ax
	mov	ss, ax
	xchg	esp, (_mon_sp)		! unswitch stacks
	lidt	(_gdt+IDT_SELECTOR)	! reload interrupt descriptor table
	andb	(_gdt+TSS_SELECTOR+DESC_ACCESS), ~0x02  ! clear TSS busy bit
	mov	eax, TSS_SELECTOR
	ltr	ax			! set TSS register

	pop	eax
	outb	INT_CTLMASK		! restore interrupt masks
	movb	al, ah
	outb	INT2_CTLMASK

	add	(_lost_ticks), ecx	! record lost clock ticks

	popf				! restore flags
	pop	ebx			! restore C registers
	pop	edi
	pop	esi
	pop	ebp
	ret


!*===========================================================================*
!*				cp_mess					     *
!*===========================================================================*
! PUBLIC void cp_mess(int src, phys_clicks src_clicks, vir_bytes src_offset,
!		      phys_clicks dst_clicks, vir_bytes dst_offset);
! This routine makes a fast copy of a message from anywhere in the address
! space to anywhere else.  It also copies the source address provided as a
! parameter to the call into the first word of the destination message.
!
! Note that the message size, "Msize" is in DWORDS (not bytes) and must be set
! correctly.  Changing the definition of message in the type file and not
! changing it here will lead to total disaster.

CM_ARGS	=	4 + 4 + 4 + 4 + 4	! 4 + 4 + 4 + 4 + 4
!		es  ds edi esi eip	proc scl sof dcl dof

	.align	16
_cp_mess:
	cld
	push	esi
	push	edi
	push	ds
	push	es

	mov	eax, FLAT_DS_SELECTOR
	mov	ds, ax
	mov	es, ax

	mov	esi, CM_ARGS+4(esp)		! src clicks
	shl	esi, CLICK_SHIFT
	add	esi, CM_ARGS+4+4(esp)		! src offset
	mov	edi, CM_ARGS+4+4+4(esp)		! dst clicks
	shl	edi, CLICK_SHIFT
	add	edi, CM_ARGS+4+4+4+4(esp)	! dst offset

	mov	eax, CM_ARGS(esp)	! process number of sender
	stos				! copy number of sender to dest message
	add	esi, 4			! do not copy first word
	mov	ecx, Msize - 1		! remember, first word does not count
	rep
	movs				! copy the message

	pop	es
	pop	ds
	pop	edi
	pop	esi
	ret				! that is all folks!


!*===========================================================================*
!*				exit					     *
!*===========================================================================*
! PUBLIC void exit();
! Some library routines use exit, so provide a dummy version.
! Actual calls to exit cannot occur in the kernel.
! GNU CC likes to call ___main from main() for nonobvious reasons.

_exit:
__exit:
___exit:
	sti
	jmp	___exit

___main:
	ret


!*===========================================================================*
!*				phys_insw				     *
!*===========================================================================*
! PUBLIC void phys_insw(Port_t port, phys_bytes buf, size_t count);
! Input an array from an I/O port.  Absolute address version of insw().

_phys_insw:
	push	ebp
	mov	ebp, esp
	cld
	push	edi
	push	es
	mov	ecx, FLAT_DS_SELECTOR
	mov	es, cx
	mov	edx, 8(ebp)		! port to read from
	mov	edi, 12(ebp)		! destination addr
	mov	ecx, 16(ebp)		! byte count
	shr	ecx, 1			! word count
rep o16	ins				! input many words
	pop	es
	pop	edi
	pop	ebp
	ret


!*===========================================================================*
!*				phys_insb				     *
!*===========================================================================*
! PUBLIC void phys_insb(Port_t port, phys_bytes buf, size_t count);
! Input an array from an I/O port.  Absolute address version of insb().

_phys_insb:
	push	ebp
	mov	ebp, esp
	cld
	push	edi
	push	es
	mov	ecx, FLAT_DS_SELECTOR
	mov	es, cx
	mov	edx, 8(ebp)		! port to read from
	mov	edi, 12(ebp)		! destination addr
	mov	ecx, 16(ebp)		! byte count
	shr	ecx, 1			! word count
   rep	insb				! input many bytes
	pop	es
	pop	edi
	pop	ebp
	ret


!*===========================================================================*
!*				phys_outsw				     *
!*===========================================================================*
! PUBLIC void phys_outsw(Port_t port, phys_bytes buf, size_t count);
! Output an array to an I/O port.  Absolute address version of outsw().

	.align	16
_phys_outsw:
	push	ebp
	mov	ebp, esp
	cld
	push	esi
	push	ds
	mov	ecx, FLAT_DS_SELECTOR
	mov	ds, cx
	mov	edx, 8(ebp)		! port to write to
	mov	esi, 12(ebp)		! source addr
	mov	ecx, 16(ebp)		! byte count
	shr	ecx, 1			! word count
rep o16	outs				! output many words
	pop	ds
	pop	esi
	pop	ebp
	ret


!*===========================================================================*
!*				phys_outsb				     *
!*===========================================================================*
! PUBLIC void phys_outsb(Port_t port, phys_bytes buf, size_t count);
! Output an array to an I/O port.  Absolute address version of outsb().

	.align	16
_phys_outsb:
	push	ebp
	mov	ebp, esp
	cld
	push	esi
	push	ds
	mov	ecx, FLAT_DS_SELECTOR
	mov	ds, cx
	mov	edx, 8(ebp)		! port to write to
	mov	esi, 12(ebp)		! source addr
	mov	ecx, 16(ebp)		! byte count
   rep	outsb				! output many bytes
	pop	ds
	pop	esi
	pop	ebp
	ret


!*==========================================================================*
!*				enable_irq				    *
!*==========================================================================*/
! PUBLIC void enable_irq(irq_hook_t *hook)
! Enable an interrupt request line by clearing an 8259 bit.
! Equivalent C code for hook->irq < 8:
!   if ((irq_actids[hook->irq] &= ~hook->id) == 0)
!	outb(INT_CTLMASK, inb(INT_CTLMASK) & ~(1 << irq));

	.align	16
_enable_irq:
	push	ebp
	mov	ebp, esp
	pushf
	cli
	mov	eax, 8(ebp)		! hook
	mov	ecx, 8(eax)		! irq
	mov	eax, 12(eax)		! id bit
	not	eax
	and	_irq_actids(ecx*4), eax	! clear this id bit
	jnz	en_done			! still masked by other handlers?
	movb	ah, ~1
	rolb	ah, cl			! ah = ~(1 << (irq % 8))
	mov	edx, INT_CTLMASK	! enable irq < 8 at the master 8259
	cmpb	cl, 8
	jb	0f
	mov	edx, INT2_CTLMASK	! enable irq >= 8 at the slave 8259
0:	inb	dx
	andb	al, ah
	outb	dx			! clear bit at the 8259
en_done:popf
	leave
	ret


!*==========================================================================*
!*				disable_irq				    *
!*==========================================================================*/
! PUBLIC int disable_irq(irq_hook_t *hook)
! Disable an interrupt request line by setting an 8259 bit.
! Equivalent C code for irq < 8:
!   irq_actids[hook->irq] |= hook->id;
!   outb(INT_CTLMASK, inb(INT_CTLMASK) | (1 << irq));
! Returns true iff the interrupt was not already disabled.

	.align	16
_disable_irq:
	push	ebp
	mov	ebp, esp
	pushf
	cli
	mov	eax, 8(ebp)		! hook
	mov	ecx, 8(eax)		! irq
	mov	eax, 12(eax)		! id bit
	or	_irq_actids(ecx*4), eax	! set this id bit
	movb	ah, 1
	rolb	ah, cl			! ah = (1 << (irq % 8))
	mov	edx, INT_CTLMASK	! disable irq < 8 at the master 8259
	cmpb	cl, 8
	jb	0f
	mov	edx, INT2_CTLMASK	! disable irq >= 8 at the slave 8259
0:	inb	dx
	testb	al, ah
	jnz	dis_already		! already disabled?
	orb	al, ah
	outb	dx			! set bit at the 8259
	mov	eax, 1			! disabled by this function
	popf
	leave
	ret
dis_already:
	xor	eax, eax		! already disabled
	popf
	leave
	ret


!*===========================================================================*
!*				phys_copy				     *
!*===========================================================================*
! PUBLIC void phys_copy(phys_bytes source, phys_bytes destination,
!			phys_bytes bytecount);
! Copy a block of physical memory.

PC_ARGS	=	4 + 4 + 4 + 4	! 4 + 4 + 4
!		es edi esi eip	 src dst len

	.align	16
_phys_copy:
	cld
	push	esi
	push	edi
	push	es

	mov	eax, FLAT_DS_SELECTOR
	mov	es, ax

	mov	esi, PC_ARGS(esp)
	mov	edi, PC_ARGS+4(esp)
	mov	eax, PC_ARGS+4+4(esp)

	cmp	eax, 10			! avoid align overhead for small counts
	jb	pc_small
	mov	ecx, esi		! align source, hope target is too
	neg	ecx
	and	ecx, 3			! count for alignment
	sub	eax, ecx
	rep
   eseg	movsb
	mov	ecx, eax
	shr	ecx, 2			! count of dwords
	rep
   eseg	movs
	and	eax, 3
pc_small:
	xchg	ecx, eax		! remainder
	rep
   eseg	movsb

	pop	es
	pop	edi
	pop	esi
	ret


!*===========================================================================*
!*				mem_rdw					     *
!*===========================================================================*
! PUBLIC u16_t mem_rdw(U16_t segment, u16_t *offset);
! Load and return word at far pointer segment:offset.

	.align	16
_mem_rdw:
	mov	cx, ds
	mov	ds, 4(esp)		! segment
	mov	eax, 4+4(esp)		! offset
	movzx	eax, (eax)		! word to return
	mov	ds, cx
	ret


!*===========================================================================*
!*				reset					     *
!*===========================================================================*
! PUBLIC void reset();
! Reset the system by loading IDT with offset 0 and interrupting.

_reset:
	lidt	(idt_zero)
	int	3		! anything goes, the 386 will not like it
.sect .data
idt_zero:	.data4	0, 0
.sect .text


!*===========================================================================*
!*				mem_vid_copy				     *
!*===========================================================================*
! PUBLIC void mem_vid_copy(u16 *src, unsigned dst, unsigned count);
!
! Copy count characters from kernel memory to video memory.  Src is an ordinary
! pointer to a word, but dst and count are character (word) based video offset
! and count.  If src is null then screen memory is blanked by filling it with
! blank_color.

_mem_vid_copy:
	push	ebp
	mov	ebp, esp
	push	esi
	push	edi
	push	es
	mov	esi, 8(ebp)		! source
	mov	edi, 12(ebp)		! destination
	mov	edx, 16(ebp)		! count
	mov	es, (_vid_seg)		! segment containing video memory
	cld				! make sure direction is up
mvc_loop:
	and	edi, (_vid_mask)	! wrap address
	mov	ecx, edx		! one chunk to copy
	mov	eax, (_vid_size)
	sub	eax, edi
	cmp	ecx, eax
	jbe	0f
	mov	ecx, eax		! ecx = min(ecx, vid_size - edi)
0:	sub	edx, ecx		! count -= ecx
	shl	edi, 1			! byte address
	add	edi, (_vid_off)		! in video memory
	test	esi, esi		! source == 0 means blank the screen
	jz	mvc_blank
mvc_copy:
	rep				! copy words to video memory
    o16	movs
	jmp	mvc_test
mvc_blank:
	mov	eax, (_blank_color)	! ax = blanking character
	rep
    o16	stos				! copy blanks to video memory
	!jmp	mvc_test
mvc_test:
	sub	edi, (_vid_off)
	shr	edi, 1			! back to a word address
	test	edx, edx
	jnz	mvc_loop
mvc_done:
	pop	es
	pop	edi
	pop	esi
	pop	ebp
	ret


!*===========================================================================*
!*				vid_vid_copy				     *
!*===========================================================================*
! PUBLIC void vid_vid_copy(unsigned src, unsigned dst, unsigned count);
!
! Copy count characters from video memory to video memory.  Handle overlap.
! Used for scrolling, line or character insertion and deletion.  Src, dst
! and count are character (word) based video offsets and count.

_vid_vid_copy:
	push	ebp
	mov	ebp, esp
	push	esi
	push	edi
	push	es
	mov	esi, 8(ebp)		! source
	mov	edi, 12(ebp)		! destination
	mov	edx, 16(ebp)		! count
	mov	es, (_vid_seg)		! segment containing video memory
	cmp	esi, edi		! copy up or down?
	jb	vvc_down
vvc_up:
	cld				! direction is up
vvc_uploop:
	and	esi, (_vid_mask)	! wrap addresses
	and	edi, (_vid_mask)
	mov	ecx, edx		! one chunk to copy
	mov	eax, (_vid_size)
	sub	eax, esi
	cmp	ecx, eax
	jbe	0f
	mov	ecx, eax		! ecx = min(ecx, vid_size - esi)
0:	mov	eax, (_vid_size)
	sub	eax, edi
	cmp	ecx, eax
	jbe	0f
	mov	ecx, eax		! ecx = min(ecx, vid_size - edi)
0:	sub	edx, ecx		! count -= ecx
	call	vvc_copy		! copy video words
	test	edx, edx
	jnz	vvc_uploop		! again?
	jmp	vvc_done
vvc_down:
	std				! direction is down
	lea	esi, -1(esi)(edx*1)	! start copying at the top
	lea	edi, -1(edi)(edx*1)
vvc_downloop:
	and	esi, (_vid_mask)	! wrap addresses
	and	edi, (_vid_mask)
	mov	ecx, edx		! one chunk to copy
	lea	eax, 1(esi)
	cmp	ecx, eax
	jbe	0f
	mov	ecx, eax		! ecx = min(ecx, esi + 1)
0:	lea	eax, 1(edi)
	cmp	ecx, eax
	jbe	0f
	mov	ecx, eax		! ecx = min(ecx, edi + 1)
0:	sub	edx, ecx		! count -= ecx
	call	vvc_copy
	test	edx, edx
	jnz	vvc_downloop		! again?
	cld				! C compiler expects up
	!jmp	vvc_done
vvc_done:
	pop	es
	pop	edi
	pop	esi
	pop	ebp
	ret

! Copy video words.  (Inner code of both the up and downcopying loop.)
vvc_copy:
	shl	esi, 1
	shl	edi, 1			! byte addresses
	add	esi, (_vid_off)
	add	edi, (_vid_off)		! in video memory
	rep
eseg o16 movs				! copy video words
	sub	esi, (_vid_off)
	sub	edi, (_vid_off)
	shr	esi, 1
	shr	edi, 1			! back to word addresses
	ret


!*===========================================================================*
!*			      level0					     *
!*===========================================================================*
! PUBLIC void level0(void (*func)(void))
! Call a function at permission level 0.  This allows kernel tasks to do
! things that are only possible at the most privileged CPU level.
!
_level0:
	mov	eax, 4(esp)
	mov	(_level0_func), eax
	int	LEVEL0_VECTOR
	ret
