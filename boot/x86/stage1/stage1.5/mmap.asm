;
;    Get the system memory map from the bios and store it in a global constant.
;    Copyright (C) 2011 Michel Megens
;
;    This program is free software: you can redistribute it and/or modify
;    it under the terms of the GNU General Public License as published by
;    the Free Software Foundation, either version 3 of the License, or
;    (at your option) any later version.
;
;    This program is distributed in the hope that it will be useful,
;    but WITHOUT ANY WARRANTY; without even the implied warranty of
;    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;    GNU General Public License for more details.
;
;    You should have received a copy of the GNU General Public License
;    along with this program.  If not, see <http://www.gnu.org/licenses/>.
;

[GLOBAL getmmr]
getmemorymap:
	mov ax, 0x50
	mov es, ax
	xor di, di
	mov word [mmr], ax
	mov word [mmr+2], di

; 
; The memory map returned from bios int 0xe820 is a complete system map, it will be given to the bootloader kernel for little editing
; 
mm_e820:
	push bp
	xor bp, bp ; entry counter
	mov eax, 0xE820
	xor ebx, ebx
	mov ecx, 0x18
	mov edx, 0x534D4150
	push edx
	mov [es:di+20], dword 1 ; acpi 3.x compatibility
	int 0x15
	pop edx	; restore magic number

	jc .failed
	cmp eax, edx ; magic word should also be in eax after interrupt
	jne .failed
	test ebx, ebx ; ebx = 0 means the list is only 1 entry long = worthless
	jz .failed
	jmp .addentry

.getentry:
	mov eax, 0xE820
	mov ecx, 0x18
	mov edx, 0x534D4150
	push edx
	mov [es:di+20], dword 1
	int 0x15
	pop edx

	jc .done ; carriage means end of list
	cmp eax, edx
	jne .failed

.addentry:
	jcxz .skipentry ; entries with length 0 are compleet bullshit
	add di, 24
	inc bp

.skipentry:
	test ebx, ebx ;
	jnz .getentry

.done:
	mov [mmr+4], word bp
	pop bp
	clc	; clear carry flag
	ret
.failed:
	pop bp
	jmp mm_e801
	stc
	ret

;
; This memory map will contain 4 entries. See doc/mmap.txt for more information.
;
mm_e801:
	xor di, di
	mov ax, 0x50
	mov es, ax
	call lowmmap
	jc .failed
	push .midmem
	jmp .next

.midmem:
	mov ax, 0xe801
	int 0x15
	jc .failed
	push bx		; save bx and dx for the high mem entry
	push dx

	and ecx, 0xffff	; clear upper 16 bits
	shl ecx, 10
	mov [es:di], dword 0x100000
	mov [es:di+8], ecx		;dword (0x3c00<<10)
	mov [es:di+16], byte 0x1
	mov [es:di+20], byte 0x1
	jecxz .useax
	push .highmem
	jmp .next
.useax:
	and eax, 0xffff
	shl eax, 10
	mov [es:di+8], eax
	push .highmem
	jmp .next

.highmem:
	pop dx
	pop bx

	and edx, 0xffff
	shl edx, 16	; edx * 1024 * 64
	mov [es:di], dword 0x1000000
	mov [es:di+8], edx
	mov [es:di+16],	byte 0x1	; type -> usable
	mov [es:di+20], byte 0x1	; acpi 3.0 compatible

	test edx, edx	
	jz .usebx
	jmp .done

.usebx:
	and ebx, 0xffff
	shl ebx, 16
	test ebx, ebx
	jz .failed
	mov [es:di+8], ebx
	jmp .done
.next:
	add di, 0x18
	jmp copy_empty_entry

.failed:
	jmp mm_88
	stc
	ret

.done:
	mov [mmr+4], word 0x4	; 2x low mem + mid mem + high mem = 4 entries
	clc
	ret

; 
; This is a function from the dinosaur time, it it used on verry old PC's when all other methods to get a memory map fail. If this
; fails to, the bootloader will cry to the user, and tell him to buy a new pc.
; 
mm_88:
	xor di, di	; set segment:offset of the buffer, just to be sure
	mov ax, 0x50
	mov es, ax

	call lowmmap	; get a lowmmap
	jc .failed
	push .highmem
	jmp .nxtentry
.highmem:
	mov ax, 0x8800
	int 0x15
	jc .failed
	and eax, 0xffff
	shl eax, 10

	mov [es:di], dword 0x00100000	; base
	mov [es:di+8], eax
	mov [es:di+16], dword 0x1	; free memory
	mov [es:di+20], dword 0x1	; acpi 3.0
	jmp .done

.nxtentry:
	add di, 0x18
	jmp copy_empty_entry

.failed:
	stc
	ret
.done:
	mov [mmr+4], word 0x3
	clc
	ret

;
; This routine makes a memory map of the low memory (memory < 1M)
;
lowmmap:
; low available memory
	call copy_empty_entry
	xor ax, ax
	int 0x12	; get low memory size

	jc .failed	; if interrupt 0x12 is not support, its really really over..
	push ax
	and eax, 0xffff	; clear upper 16  bits
	shl eax, 10	; convert to bytes
	mov [es:di], dword 0x0
	mov [es:di+8], eax
	mov [es:di+16], dword 0x1
	mov [es:di+20], dword 0x1	; acpi 3.0 compatible entry
	push .lowres
	add di, 0x18
	jmp copy_empty_entry

.lowres:
; low reserver memory
	pop ax
	mov [es:di], ax
	mov dx, (1 << 10)
	sub dx, ax
	and edx, 0xffff
	shl edx, 10		; convert to bytes
	mov [es:di+8], edx	; length (in bytes)
	mov [es:di+16], dword 0x2	; reserverd memory
	mov [es:di+20], dword 0x1		; also this entry is acpi 3.0 compatible
	jmp .done

.failed:
	stc
	ret
.done:
	clc
	ret

; 
; This routine copies an empty memory map to the location specified by es:di
; 
copy_empty_entry:	; this subroutine copies an emty memory map to the location specified by es:di
	cld	; just to be sure that di gets incremented
	mov si, mmap_entry
	mov cx, 0xc
	rep movsw	; copy copy copy!
	sub di, 0x18	; just to make addressing esier
	ret
; now there is an empty entry at [es:di]

; 
; This will return the mmr in eax. Used in mmap.c to get the mmr.
; 
getmmr:
	mov eax, mmr
	ret

mmap_entry:	; 0x18-byte mmap entry
	base dq 0	; base address
	length dq 0	; length (top_addr - base_addr)
	type dd 0	; entry type
	acpi3 dd 0	; acpi 3.0 compatibility => should be 1
	mmap_size equ $ - mmap_entry

mmr:
	dd 0 		; dw 0 -> segment
			; dw 0 -> offset
	dw 0 ;entry count
	db 24 ; entry size