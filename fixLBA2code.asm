; code.asm - patch code for fixLBA2 very loosely based on FunnyFrog's patch

; Compile with nasm...
; nasm -f bin code.asm -o code.bin

bits 32

; Code loads at 0052f000 in section .text2

; CARE in the code that follows FunnyFrog repurposes an operand at 0x45bb2b + 6 ... this works because the relocations
; table will fix it up should the program to be loaded at a different address. We need to be very careful when making
; changes to account for these relocatable operands. It is NOT possible to replace code at random as the loader may
; make unexpected changes to our code!

; NB In the main body of the patch to the code which calls DirectDrawSurface.Lock, FunnyFrog has already performed
; 0045BB20: inc dword [004D11C8h]			; this replaces "mov eax, [ebp-58h]"
; Then calls the patch area code via...
; 0045BB26: call 004735E0					; this replaces "mov [00476D00h], eax"
; We retain the "inc dword [004D11C8h]", but replace the call with one to our larger patch area in .text2
; 0045BB26: call 0052f000

mov         edx, [esp]			; get return address from stack into edx so edx=0x45bb2b

; NB eax will contains the return value of the function which is actually lpSurface from DirectDrawSurface.Lock
; We will set this later (to our new temp buffer) so these lines are commented out normally.
; DEBUG uncomment the next 3 lines to use normal lpSurface instead of temp buffer
;mov         ecx, [edx]			; get value at address [edx] so ecx=0x476d00 - this address was originally the operand
								; of the mov eax instr, but is repurposed here (for the same effect, as follows)
;mov         eax, [ebp-58h]		; this is lpSurface as returned by Lock
;mov         [ecx],eax			; same effect as original mov [0x476d00], eax

; This is where I insert my changes, we won't use FunnyFrog's pitch mod as we want LBA2 to continue to use lPitch=640
; as this matches the width of our temporary buffer in .bss2

;mov         ecx, [edx+6]		; This is FunnyFrog's change, get an address from 0x45bb2b + 6 which in the original code
								; was an operand 0x4d11c8, but FunnyFrog replaces it with 0x476d18
;mov         ebx, [ebp-6Ch]		; This is the lPitch value (typically 720 / 0x300 on Windows Vista, the cause of all our woe)
;mov         [ecx],ebx			; Save to 0x476d18

; Instead we save the height, width, pitch and lpSurface values returned by Lock, then replace lpSurface
; with a pointer to a temporary buffer allocated in .bss2. LBA2 will write to this buffer as normal, using the default
; pitch which is the same as width (640). There is no need to copy lpSurface data first, as the buffer is persistent
; and retains the prior data (written by LBA2) between calls. If we do copy data here, it slows the frame rate very
; considerably (lpSurface is in video memory, and read access is very slow, at least on my machine).

mov         ecx, [edx]			; get value at address [edx] so ecx=0x476d00 - this address was originally the operand
								; of the mov eax instr, but is repurposed here
add			ecx, 4e3000h - 476d00h ; Adjust ecx to point to the start of my new .bss2 section at 004e3000
mov         ebx, [ebp-78h]		; Get the flags
and			ebx, 0xe			; Mask for DDSD_HEIGHT|DDSD_WIDTH|DDSD_PITCH
cmp			ebx, 0xe			; Check we have correct flags
jne			bad
; As our temp buffer is fixed at 640 by 480, be sure height/width are correct
; OPTIONALLY could allow smaller values, but probably unneccessary
mov         ebx, [ebp-74h]		; Get the height
cmp			ebx, 480
jne			bad
mov         [ecx],ebx			; Save to 004e3000
mov         ebx, [ebp-70h]		; Get the width
cmp			ebx, 640
jne			bad
mov         [ecx+4],ebx			; Save to 004e3004
mov         ebx, [ebp-6Ch]		; Get the pitch
mov         [ecx+8],ebx			; Save to 004e3008, we will use it when copying data back to lpSurface
mov         ebx, [ebp-58h]		; Get lpSurface
mov         [ecx+12],ebx		; Save to 004e300C, for use prior to Unlock
add			ecx, 0x10			; This is our temp buffer, immediately following the above variables
mov			[ebp-58h], ecx		; Replace lpSurface with our buffer
jmp			alldone
bad:
mov			ebx, 0				; Set these to zero to indicate an error
mov         [ecx],ebx			; Save to 004e3000 height
mov         [ecx+4],ebx			; Save to 004e3004 width
mov         [ecx+8],ebx			; Save to 004e3008 pitch
mov         [ecx+12],ebx		; Save to 004e300C lpSurface
alldone:
; Now get lpSurface (the MODIFIED ONE, or original if badflags)
mov         ecx, [edx]			; get value at address [edx] so ecx=0x476d00 - this address was originally the operand
								; of the mov eax instr, but is repurposed here (for the same effect, as follows)
mov         eax, [ebp-58h]		; this is lpSurface as returned by Lock, and then replaced by our temp buffer above
mov         [ecx],eax			; same effect as original mov [0x476d00], eax

								; The next instruction adds 0x0A to the return address on the stack, so it no
								; longer returns to 0x45bb2b instead to 0x45bb35 thus skipping the repurposed operands
; add         dword [esp],0Ah	; NB nasm generates "81 04 24 0A 00 00 00" not the "83 04 24 0A" of FunnyFrog's patch
; Not sure how to make asm generate the short version of this instruction (dword add, but with byte immediate value)
; so hard-code it for now
db	83h
db	04h
db	24h
db	0Ah
ret								; return, to 0045BB35, where esi, edx, ecx, ebx, ebp are popped.

; Patch for DirectDrawSurface.Unlock

; We copy the data from our temporary buffer into the lpSurface, correcting for pitch

; This is currently 0052f000 + 6D = 0052f06D (use dump code.bin to locate, just after C3 for ret)
; TODO ALWAYS Ensure correct displacement is used in fixLBA2.cpp, vis
; The detour for Surface.Unlock returns to 0045BB69, which is called from...
; 0045BB63: FF 92 80 00 00 00  call        dword ptr [edx+00000080h]
; This is safe to modify as there is no relocation associated with this instruction, so replace it with a jump
; 0045BB63: E9 xx xx xx xx 90  jmp displacement to HERE; nop

; Push the registers we want to use
pushf							; flags since we cld later
push		ebx
push		ecx
push		edx
push		esi
push		edi

; Get the value of the program counter
call		here				; Dummy call, do NOT ret
here:

; This is currently 0052f000 + 78 = 52f078 (use dump code.bin to locate, just after E8 00 00 00 00)
; TODO ALWAYS insert value 2 lines below...

pop			edx					; here is now in edx
add			edx, 4e3000h - 52f078h			; Offset back to my new .bss2 section at 004e3000
; Sanity checks - the Lock patch will set these to zero if bad and use lpSurface instead of temp
cmp			dword [edx], 480	; Check if height is valid
jne			skipon				; Skip the copy
cmp			dword [edx+4], 640	; Check if width is valid
jne			skipon				; Skip the copy

; All is good, so copy the data
cld
mov			esi, edx			; set esi to start of .bss2
add			esi, 0x10			; and adjust to point to the temp buffer which immediately follows 4 dword variables
mov			edi, [edx+12]		; get saved lpSurface
cmp			edi, 0
je			skipon				; sanity check for null pointer
mov 		ebx, [edx]			; ebx is outer loop counter for height
lineloop:
mov			ecx, [edx+4]		; ecx is inner loop counter for width 
shr			ecx, 2				; divide by 4 for dword move (ASSUME width is 640)
rep 		movsd  				; mov dword [edi], [esi]
; adjust for pitch
add			edi, [edx+8]		; add pitch
sub			edi, [edx+4]		; subtract width
dec			ebx
jne			lineloop

skipon:

; Pop the registers we saved
pop			edi
pop			esi
pop			edx
pop			ecx
pop			ebx
popf

; Finally we can call DirectDrawSurface.Unlock (parameters are already on stack)
call [edx+00000080h]

; This is currently 0052f000 + C3 = 0052f0C3 (use dump code.bin to locate)
; To calc displacement use 0045BB69 - 0052f0C3 - 5 = FFF2CAA1
; TODO ALWAYS insert value below...
db 0E9h							; Jump back to 0045BB69
db 0A1h							; TODO ALWAYS calc displacement (NB will change if above code is modified)
db 0CAh
db 0F2h
db 0FFh
