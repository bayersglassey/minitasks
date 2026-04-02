#ifndef _MINITASKS_H_
#define _MINITASKS_H_

#include <stdlib.h>
#include <stddef.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>

#include <util/setbaud.h>
#include <util/delay.h>
#include <util/atomic.h> // ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { ... }

#include "avrstuff.h"


// Maximum number of tasks in the system
#define MAX_TASKS 5

#define TASK_SIZE (sizeof(task_t))

// The size of CODE + HEAP + FREE + STACK
#define TASK_MEM_SIZE (256 + 128)

// A tick is like a "time slice" for one task; we need to run at least
// one time slace per task per UART operation, i.e. BAUD.
// So that every time we read a character from UART, at least one task
// gets a chance to handle it before the next character, you know?
#define TICKS_PER_SECOND (BAUD * MAX_TASKS)

#define CYCLES_PER_TICK (F_CPU / TICKS_PER_SECOND)

typedef uint8_t task_id_t;

// The "escape character" over USART communications, indicating the start
// of a message
#define MESSAGE_ESCAPE ((char) 0xFF)

typedef uint8_t task_state_t;
enum task_state {
    TASK_STATE_STOPPED   = 0,
    TASK_STATE_STARTED   = 1,
    TASK_STATE_SLEEPING  = 2,
    TASK_STATE_QUEUED    = 3,
};

enum message_type {
    MESSAGE_PING                   = 0x00,
    MESSAGE_GET_OFFSETS            = 0x01,
    MESSAGE_GET_BUILTIN_LOCATIONS  = 0x02,
    MESSAGE_LOAD_TASK              = 0x03,
    MESSAGE_START_TASK             = 0x04,
    MESSAGE_STOP_TASK              = 0x05,
    MESSAGE_INSPECT_TASK           = 0x06,
    MESSAGE_KERNEL_LOG             = 0x07,
    MESSAGE_TASK_LOG               = 0x08,
    MESSAGE_TASK_STOPPED           = 0x09,
    MESSAGE_GET_MEMORY             = 0x0a,
    MESSAGE_SET_MEMORY             = 0x0b,
    MESSAGE_GET_TIMER              = 0x0c,
};

typedef struct task {
    // TODO...
    task_state_t state; // enum task_state
    uint16_t code_size;
    char *free; // pointer to FREE
    char *stack; // pointer to STACK - 1, i.e. last byte of FREE
    char mem[TASK_MEM_SIZE];
} task_t;

task_t TASKS[MAX_TASKS] = {0};

// A "builtin" is an assembly routine implementing a tasklang instruction
typedef void builtin_t(void);

void __attribute__((noreturn)) die(const char *msg);

void __attribute__((noreturn)) blink_forever(void);

// Functions implemented in tasklang.S
extern void _init_task(char *code, char *free, char *stack);
extern void __attribute__((noreturn)) resume_task(char *stack);
extern void __attribute__((noreturn)) pause_task(void);

// A listing of all tasklang "builtins".
// NOTE: this list of builtins must be in the same order as that of BUILTINS
// in tasklang.py!.. the names don't need to match up exactly, but the order
// must be the same.
#define _BUILTINS \
    BUILTIN(builtin_add) \
    BUILTIN(builtin_sub) \
    BUILTIN(builtin_mul) \
    BUILTIN(builtin_div) \
    BUILTIN(builtin_mod) \
    BUILTIN(builtin_lshift) \
    BUILTIN(builtin_rshift) \
    BUILTIN(builtin_eq) \
    BUILTIN(builtin_ne) \
    BUILTIN(builtin_lt) \
    BUILTIN(builtin_gt) \
    BUILTIN(builtin_le) \
    BUILTIN(builtin_ge) \
    BUILTIN(builtin_not) \
    BUILTIN(builtin_bitnot) \
    BUILTIN(builtin_bitand) \
    BUILTIN(builtin_bitor) \
    BUILTIN(builtin_bitxor) \
    BUILTIN(builtin_getptr) \
    BUILTIN(builtin_getptrc) \
    BUILTIN(builtin_setptr) \
    BUILTIN(builtin_setptrc) \
    BUILTIN(builtin_literal) \
    BUILTIN(builtin_store) \
    BUILTIN(builtin_load) \
    BUILTIN(builtin_jump) \
    BUILTIN(builtin_jumpif) \
    BUILTIN(builtin_jumpifnot) \
    BUILTIN(builtin_atomic) \
    BUILTIN(builtin_end_atomic) \
    BUILTIN(builtin_neg) \
    BUILTIN(builtin_dup) \
    BUILTIN(builtin_swap) \
    BUILTIN(builtin_drop) \
    BUILTIN(builtin_heap) \
    BUILTIN(builtin_hpush) \
    BUILTIN(builtin_hpushc) \
    BUILTIN(builtin_assert) \
    BUILTIN(builtin_error) \
    BUILTIN(builtin_set_led) \
    BUILTIN(builtin_sleep) \
    BUILTIN(builtin_getc) \
    BUILTIN(builtin_putc) \
    BUILTIN(builtin_logs) \
    BUILTIN(builtin_logc) \
    BUILTIN(builtin_logi) \
    BUILTIN(builtin_halt) \
    BUILTIN(builtin_pause)

// Function prototypes for assembly routines defined in tasklang.S
#define BUILTIN(name) extern __attribute__((noreturn)) builtin_t name;
_BUILTINS
#undef BUILTIN

// Table of assembly routines
#define BUILTIN(name) &name,
builtin_t * const BUILTINS[] PROGMEM = { _BUILTINS };
#undef BUILTIN

#define N_BUILTINS (sizeof(BUILTINS) / sizeof(builtin_t*))

#endif
