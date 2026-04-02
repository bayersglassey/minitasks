
#include <avr/common.h>
#include <avr/io.h>

#include "avrstuff.h"


.macro builtin name
.global builtin_\name
builtin_\name:
.endm

.macro _GET_STACK R0 R1
    ; load stack pointer into given register pair
    in \R1, SPH_IO_ADDR
    in \R0, SPL_IO_ADDR
.endm

.macro _SET_STACK R0 R1
    ; set stack pointer to value in given register pair
    ; NOTE: it seems like we must set SPH before SPL; if we set SPL first, it
    ; seems that SPH is always set to 0x08. No idea why. O_o
    out SPH_IO_ADDR, \R1
    out SPL_IO_ADDR, \R0
.endm

.macro _BINARYPOP
    ; pop 2 values off the stack, for implementing binary operators.
    ; if stack looks like: x y
    ; ...then this will result in: r25:r24 = x, r23:r22 = y
    pop r23
    pop r22
    pop r25
    pop r24
.endm

.macro NEXT
    ; Jump to the next threaded code instruction, and also increment the
    ; threaded code instruction pointer.
    ld r30, Y+   ; load whatever Y is pointing at into Z, and increment Y
    ld r31, Y+   ; ...
    ijmp         ; jump to the code pointed to by Z
.endm

.macro _SAVE_CONTEXT
    ; save registers used by tasklang instructions, for context switching
    push r22
    push r23
    push r24
    push r25
    push r26 ; X
    push r27 ; ...
    push r28 ; Y
    push r29 ; ...
    push r30 ; Z
    push r31 ; ...
.endm

.macro _LOAD_CONTEXT
    ; load registers used by tasklang instructions, for context switching
    pop r31 ; Z
    pop r30 ; ...
    pop r29 ; Y
    pop r28 ; ...
    pop r27 ; X
    pop r26 ; ...
    pop r25
    pop r24
    pop r23
    pop r22
.endm

.global _init_task
_init_task:
    ; NOTE: intended to be called by C code, with arguments:
    ;  code, free, stack

    ; Save registers which we'll be overwriting in a moment, and must restore
    ; later in order to return to calling C code
    ; See: https://gcc.gnu.org/wiki/avr-gcc#Register_Layout
    push r28
    push r29

    movw r28, r24 ; load code pointer into Y
    movw r26, r22 ; load heap pointer into X
                  ;   ...which points to FREE, i.e. just past top of heap

    ; Save the stack pointer, so we can return to it later
    _GET_STACK r18 r19

    ; Set the stack pointer to that of the task's to be initialized
    _SET_STACK r20 r21

    ; Save the "return address" to be used the first time there's a context
    ; switch to this task
    ldi r20, lo8(task_start)
    ldi r21, hi8(task_start)
    push r20
    push r21

    ; Save registers, in particular Y and X, to the new stack, to be picked
    ; up next time there's a context switch to this task
    _SAVE_CONTEXT

    ; Restore the stack & registers, and return to whoever called us!
    _SET_STACK r18 r19
    pop r29
    pop r28
    ret

task_start:
    ; The place tasks first jump to when started
    NEXT

loop_forever:
    rjmp loop_forever

.global pause_task
pause_task:
    ; The timer interrupt, represents a "tick", runs every CYCLES_PER_TICK
    ; cycles.

    ; Assume we're being called from an interrupt while a task was running.
    ; So, a return address has been pushed for the task's interrupted code.
    ; Also, global interrupts are disabled.

    ; save registers
    _SAVE_CONTEXT

    ; toggle LED
    ldi r24, lo8(_tick)
    brne 1f               ; only toggle every 256th tick
    in	r24, 0x05
    ldi	r25, 0x20
    eor	r24, r25
    out	0x05, r24
    1:

    ; save stack pointer in first C function argument
    _GET_STACK r24 r25

    ; save X (i.e. current task's top-of-heap pointer) in second C function argument
    movw r22, r26

    ; call a C function which switches to next runnable task
    ; (and never returns to us)
    ; with arguments:
    ;   stack, free
    rjmp switch_task

.global resume_task
resume_task:
    ; NOTE: intended to be called by C code, with arguments:
    ;  stack
    ; ...but does not return to the calling C code!

    ; Set the stack pointer to that of the task's
    _SET_STACK r24 r25

    ; load registers & resume from wherever this task left off!..
    _LOAD_CONTEXT
    reti


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; OPERATORS

BUILTIN add
    ; x y + -> x+y

    ; pop two 16-bit values (x, y) from the stack
    ; x = r25:r24, y = r23:r22
    _BINARYPOP

    ; add the two values together (x += y)
    add r24, r22 ; add the lower bytes
    adc r25, r23 ; add the higher bytes with carry

    ; push the resulting 16-bit value
    push r24
    push r25

    ; jump to the next threaded code instruction
    NEXT

BUILTIN sub
    ; x y + -> x-y
    ; see the comments for "add"
    _BINARYPOP
    sub r24, r22 ; x -= y
    sbc r25, r23 ; ...
    push r24
    push r25
    NEXT

; TODO...
BUILTIN mul
    rjmp builtin_halt
BUILTIN div
    rjmp builtin_halt
BUILTIN mod
    rjmp builtin_halt
BUILTIN lshift
    rjmp builtin_halt
BUILTIN rshift
    rjmp builtin_halt

_return_true:
    ; return true from an instruction, i.e. push 1 onto stack and jump to
    ; next instruction
    ; NOTE: THIS LABEL NEEDS TO LIVE HERE, directly before all the comparison
    ; operators, in order to be within 64 bytes of their close-range jumps!..
    ldi r24, 1
    push r24
    ldi r24, 0
    push r24
    NEXT

; NOTE: the binary logical operator implementations below were tested using
; the Python client as follows:
;
;   code = ' 0xeeee '.join(f'1 1 {op} 1 2 {op} 2 1 {op}' for op in ('==', '!=', '<', '<=', '>', '>='))
;   c.run_task(0, code)
;   _bytemap = {' ':b'\xee\xee', '0':b'\x00\x00', '1':b'\x00\x01'}
;   expected = b''.join(_bytemap[c] for c in ''.join(reversed('100 011 010 110 001 101')))
;   assert c.inspect_task(0).rest.data[-len(expected):] == expected
;

BUILTIN eq
    ; x y + -> x==y
    _BINARYPOP
    cp r24, r22  ; compare x - y
    cpc r25, r23 ; ...
    breq _return_true
    rjmp _return_false

BUILTIN ne
    ; x y + -> x!=y
    _BINARYPOP
    cp r24, r22  ; compare x - y
    cpc r25, r23 ; ...
    brne _return_true
    rjmp _return_false

BUILTIN lt
    ; x y + -> x<y
    _BINARYPOP
    cp r24, r22  ; compare x - y
    cpc r25, r23 ; ...
    brcs _return_true ; branch if carry set, i.e. if x < y
    rjmp _return_false

BUILTIN gt
    ; x y + -> x>y
    _BINARYPOP
    cp r22, r24  ; compare y - x
    cpc r23, r25 ; ...
    brcs _return_true ; branch if carry set, i.e. if y < x
    rjmp _return_false

BUILTIN le
    ; x y + -> x<=y
    _BINARYPOP
    cp r22, r24  ; compare y - x
    cpc r23, r25 ; ...
    brcc _return_true ; branch if carry clear (not set), i.e. if not y < x
    rjmp _return_false

BUILTIN ge
    ; x y + -> x>=y
    _BINARYPOP
    cp r24, r22  ; compare x - y
    cpc r25, r23 ; ...
    brcc _return_true ; branch if carry clear (not set), i.e. if not x < y
    rjmp _return_false

BUILTIN not
    pop r25
    pop r24
    or r24, r25 ; we want to know if any of the bits are 1
    breq _return_true   ; all bits were 0, i.e. it was false
    rjmp _return_false  ; at least one bit was 1, i.e. it was true

_return_false:
    ; return false from an instruction, i.e. push 0 onto stack and jump to
    ; next instruction
    ; NOTE: THIS LABEL NEEDS TO LIVE HERE, directly after all the comparison
    ; operators, in order to be within 64 bytes of their RJMP instructions!..
    ldi r24, 0
    push r24
    push r24
    NEXT

BUILTIN bitnot
    pop r25
    pop r24
    com r24
    com r25
    push r24
    push r25
    NEXT

BUILTIN bitand
    _BINARYPOP
    and r24, r22
    and r25, r23
    push r24
    push r25
    NEXT

BUILTIN bitor
    _BINARYPOP
    or r24, r22
    or r25, r23
    push r24
    push r25
    NEXT

BUILTIN bitxor
    _BINARYPOP
    eor r24, r22
    eor r25, r23
    push r24
    push r25
    NEXT

BUILTIN getptr
    pop r31    ; pop a pointer from the stack into Z
    pop r30    ; ...
    ld r24, Z+ ; get the 16-bit value pointed at by Z
    ld r25, Z+ ; ...
    push r24   ; push the value onto stack
    push r25   ; ...
    NEXT

BUILTIN getptrc
    pop r31    ; pop a pointer from the stack into Z
    pop r30    ; ...
    ld r24, Z+ ; get the 8-bit value pointed at by Z
    push r24   ; push the value onto stack
    ldi r24, 0 ; push 0 for the high byte
    push r24   ; ...
    NEXT

BUILTIN setptr
    pop r31    ; pop a pointer from the stack into Z
    pop r30    ; ...
    pop r25    ; pop a 16-bit value from the stack
    pop r24    ; ...
    st Z+, r24 ; store the value at the location pointed at by Z
    st Z+, r25 ; ...
    NEXT

BUILTIN setptrc
    pop r31    ; pop a pointer from the stack into Z
    pop r30    ; ...
    pop r25    ; pop a 16-bit value from the stack (but we will "coerce" it
               ; to 8-bit)
    pop r24    ; ...
    st Z+, r24 ; store the lower byte of the value at the location pointed
               ; at by Z
    NEXT


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PRIVATE BUILTINS

BUILTIN literal
    ; load the integer argument from threaded code into a register pair
    ; (and increment Y so instead of pointing at the argument, it now points
    ; at next threaded code instruction)
    ld r24, Y+
    ld r25, Y+

    ; push the integer argument onto the stack
    push r24
    push r25

    ; jump to the next threaded code instruction
    NEXT

BUILTIN store
    pop r25
    pop r24
    ld r30, Y+     ; load the argument following this instruction into Z
                   ; (and increment Y to next instruction)
    ld r31, Y+     ; ...
    st Z+, r24     ; store into the memory pointed to by Z
    st Z+, r25     ; ...
    NEXT

BUILTIN load
    ld r30, Y+     ; load the argument following this instruction into Z
                   ; (and increment Y to next instruction)
    ld r31, Y+     ; ...
    ld r24, Z+     ; load the word pointed to by Z
    ld r25, Z+     ; ...
    push r24
    push r25
    NEXT

BUILTIN jump
    ; load the argument (which is a pointer to another instruction within
    ; the threaded code)
    ld r24, Y+
    ld r25, Y+

    ; copy the argument back into Y (i.e. into r29:r28)
    movw r28, r24

    ; jump to the indicated instruction within the threaded code
    NEXT

BUILTIN jumpif
    pop r25
    pop r24
    or r24, r25          ; we want to know if any of the bits are 1
    breq _jumpif_false   ; jump if all bits were 0
    _jumpif_true:
        ; TOS was 1
        ld r24, Y+     ; load the argument
        ld r25, Y+     ; ...
        movw r28, r24  ; set Y to the argument
        NEXT     ; jump to the argument
    _jumpif_false:
        ; TOS was 0
        adiw r28, 2  ; add 2 bytes, skipping over the argument so we point
                     ; at next instruction
        NEXT   ; jump to next instruction

BUILTIN jumpifnot
    pop r25
    pop r24
    or r24, r25          ; we want to know if any of the bits are 1
    breq _jumpif_true    ; jump if any bits were 1
    rjmp _jumpif_false   ; else...

BUILTIN atomic
    ; start an atomic "block" by disabling interrupts
    ; NOTE: THIS INSTRUCTION IS SUPER HACKY!..
    cli
    NEXT

BUILTIN end_atomic
    ; end an atomic "block" by enabling interrupts
    ; NOTE: THIS INSTRUCTION IS SUPER HACKY!..
    sei
    NEXT


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PUBLIC BUILTINS

BUILTIN neg
    pop r25
    pop r24
    ; TODO: understand the following!.. I got it from avr-gcc generated asm
    neg	r25 ; two's compliment
    neg	r24 ; ...
    ldi r23, 0
    sbc	r25, r23
    NEXT

BUILTIN dup
    ; TODO: see whether this can improved with LDS from stack pointer
    pop r25
    pop r24
    push r24
    push r25
    push r24
    push r25
    NEXT

BUILTIN swap
    ; TODO: see whether this can improved with LDS from stack pointer
    _BINARYPOP ; if stack look like: x y ...then x=r25:r24, y=r23:r22
    push r22 ; push y
    push r23 ; ...
    push r24 ; push x
    push r25 ; ...
    NEXT

BUILTIN drop
    pop r25
    pop r24
    NEXT

BUILTIN heap
    ; push X (address of top of heap) onto stack
    push r26
    push r27
    NEXT

BUILTIN hpush
    ; pop a value from stack, push it onto the heap
    pop r25
    pop r24
    st X+, r24
    st X+, r25
    NEXT

BUILTIN hpushc
    ; pop a value from stack, push its lower byte onto the heap
    pop r25
    pop r24
    st X+, r24
    NEXT

BUILTIN assert
    pop r25
    pop r24
    or r24, r25 ; we want to know if any of the bits are 1
    breq 1f     ; all bits were 0
        ; TOS was 1: assertion passed!
        NEXT
    1:
        ; TOS was 0: assertion failed!
        ; TODO: send a TASK_STOPPED message
        rjmp loop_forever

BUILTIN error
    ; TODO: send a TASK_STOPPED message
    rjmp loop_forever

BUILTIN set_led
    pop r25
    pop r24
    or r24, r25 ; we want to know if any of the bits are 1
    breq 1f     ; all bits were 0
        sbi	0x05, 5 ; turn LED on
        NEXT
    1:
        cbi	0x05, 5 ; turn LED off
        NEXT

BUILTIN sleep
    ; poor man's sleep: just churn CPU for a while...
    ; we could be fancy and convert from ms to cycles, but... we don't
    pop r25
    pop r24
    1:
        sbiw r24, 1
        brne 1b       ; loop until we hit 0
    NEXT

BUILTIN getc
    rjmp builtin_halt
BUILTIN putc
    rjmp builtin_halt
BUILTIN logs
    rjmp builtin_halt
BUILTIN logc
    rjmp builtin_halt
BUILTIN logi
    rjmp builtin_halt

BUILTIN halt
    ; TODO: send a TASK_STOPPED message
    rjmp loop_forever

BUILTIN pause
    ; TODO: send a TASK_STOPPED message
    rjmp loop_forever
