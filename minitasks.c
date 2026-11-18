
#include "minitasks.h"


// tasklang.S stores stack pointer in here so we can examine it
uint16_t _sp = 0;

// TIMER0_OVF_vect increments this
uint16_t _tick = 0;

#define __ALWAYS_INLINE__ inline __attribute__((always_inline))


///////////////////////////////////////////////////////////////////////////////
// TASK FUNCTIONS

task_id_t current_task_id = 0;

void tasks_init(void) {
    for (task_id_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = &TASKS[i];
        task->free = task->mem;
    }
}

static __ALWAYS_INLINE__ uint16_t get_heap_size(task_t *task) {
    if (task->free < task->mem) die("FREE below start of CODE");
    return (task->free - task->mem) - task->code_size;
}

void init_task(task_t *task) {
    char *code = task->mem;
    char *heap = task->free;

    // stack pointer should point at last byte of task's memory
    char *stack = task->mem + TASK_MEM_SIZE - 1;
    task->stack = stack;

    // implemented in assembly, pushes registers onto task's stack to be
    // picked up next time there's a context switch to it
    _init_task(code, heap, stack);
}

void start_task(task_t *task) {
    if (task->state == TASK_STATE_STOPPED) {
        task->state = TASK_STATE_READY;
    }
}

void stop_task(task_t *task) {
    // TODO: if we add more states, like WAITING, then setting directly to
    // STOPPED might mess things up, because it would allow start_task to
    // set to READY, which would allow context switch to set to RUNNING,
    // which might mess up if the code was expecting something to be pushed
    // onto stack!..
    task->state = TASK_STATE_STOPPED;
}

void __attribute__((noreturn)) switch_task(char *old_stack, char *old_free) {

    // Update old task
    task_t *old_task = &TASKS[current_task_id];
    if (old_task->state == TASK_STATE_RUNNING) {
        old_task->state = TASK_STATE_READY;
    }
    old_task->stack = old_stack;
    old_task->free = old_free;

    // Choose next task to run a time slice of
    task_id_t task_id = current_task_id + 1;
    if (task_id >= MAX_TASKS) task_id = 0;
    current_task_id = task_id;
    task_t *task = &TASKS[task_id];

    if (task->state == TASK_STATE_READY) {
        task->state = TASK_STATE_RUNNING;
        resume_task(task->stack); // ...and we will never return...
    } else {
        // Task isn't ready to run, so just sleep until the next time slice...
        sei(); // enable global interrupts
        for (;;) sleep_mode();
    }
}


///////////////////////////////////////////////////////////////////////////////
// UART FUNCTIONS

void uart_init(void) {
    // These values are defined in setbaud.h, which makes use of BAUD
    // "USART BAUD RATE REGISTER"
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;

    // "USART CONTROL & STATUS REGISTER"
    #if USE_2X
    UCSR0A |= (1 << U2X0);
    #else
    UCSR0A &= ~(1 << U2X0);
    #endif
    // Enable UART0 transmit, receive, and receive interrupt
    UCSR0B = (1 << TXEN0) | (1 << RXEN0) | (1 << RXCIE0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // 8 data bits, no parity, 1 stop bit
}

static __ALWAYS_INLINE__ char uart_get(void) {
    loop_until_bit_is_set(UCSR0A, RXC0);
    // "USART DATA REGISTER"
    return UDR0;
}

static __ALWAYS_INLINE__ uint16_t uart_get_uint16(void) {
    // NOTE: little-endian
    int i = (uint8_t) uart_get();
    i |= ((uint8_t) uart_get()) << 8;
    return i;
}

static __ALWAYS_INLINE__ void uart_put(char c) {
    loop_until_bit_is_set(UCSR0A, UDRE0);
    // "USART DATA REGISTER"
    UDR0 = c;
}

static __ALWAYS_INLINE__ void uart_put_uint16(uint16_t i) {
    // NOTE: little-endian
    uart_put(i & 0xff);
    uart_put(i >> 8);
}

static __ALWAYS_INLINE__ void uart_put_uint32(uint32_t i) {
    // NOTE: little-endian
    uart_put(i & 0xff);
    uart_put((i >> 8) & 0xff);
    uart_put((i >> 16) & 0xff);
    uart_put(i >> 24);
}

static __ALWAYS_INLINE__ char to_hex(uint8_t c) {
    return c < 10? '0' + c: c < 16? 'A' + c - 10: '?';
}

static __ALWAYS_INLINE__ void uart_put_hex(uint8_t c) {
    uart_put('0');
    uart_put('x');
    char a = to_hex(c >> 4);
    char b = to_hex(c & 0x0f);
    uart_put(a);
    uart_put(b);
}

static __ALWAYS_INLINE__ void uart_put_newline(void) {
    uart_put('\r');
    uart_put('\n');
}

static __ALWAYS_INLINE__ void uart_puts(const char *msg) {
    char c;
    while ((c = *msg++)) uart_put(c);
}

static __ALWAYS_INLINE__ void uart_put_message(char msg_type) {
    uart_put(MESSAGE_ESCAPE);
    uart_put(msg_type);
}


///////////////////////////////////////////////////////////////////////////////
// LED FUNCTIONS

void led_init(void) {
    DDRB |= (1 << PB5);
}

static __ALWAYS_INLINE__ void led_on(void) {
    PORTB |= (1 << PB5);
}

static __ALWAYS_INLINE__ void led_off(void) {
    PORTB &= ~(1 << PB5);
}

static __ALWAYS_INLINE__ void led_toggle(void) {
    PORTB ^= (1 << PB5);
}


///////////////////////////////////////////////////////////////////////////////
// TIMER FUNCTIONS

void timer_init(void) {

    // https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf
    /*
        14.7.2 Clear Timer on Compare Match (CTC) Mode
        In clear timer on compare or CTC mode (WGM02:0 = 2), the OCR0A
        register is used to manipulate the counter resolution.
        In CTC mode the counter is cleared to zero when the counter value
        (TCNT0) matches the OCR0A.
    */

    // TCCB = Timer/Counter Control Register
    // CS = Clock Source (just CS00: no prescaling)
    // WGM = Wave Generation Mode (just WGM01: CTC mode, i.e. Clear Timer on
    // Compare)
    TCCR0A = (1 << CS00) | (1 << WGM01);

    // TIFR = Timer Interrupt Flag Register
    // OCF0A = Output Compare Flag
    // NOTE: writing 1 to an interrupt flag *clears it*, weird
    TIFR0 = 1 << OCF0A;

    // TIMSK = Timer Interrupt Mask Register
    // OCIE = Output Compare Interrupt Enable
    TIMSK0 = 1 << OCIE0A;

    // OCR = Output Compare Register
    OCR0A = CYCLES_PER_TICK;
}


///////////////////////////////////////////////////////////////////////////////
// OS/CLIENT MESSAGES

static __ALWAYS_INLINE__ task_id_t get_task_id(void) {
    task_id_t task_id = uart_get();
    if (task_id >= MAX_TASKS) die("Bad task ID");
    return task_id;
}

static __ALWAYS_INLINE__ void handle_ping(void) {
    char c = uart_get();
    uart_put(c);
}

static __ALWAYS_INLINE__ void handle_get_offsets(void) {
    uart_put_uint16(RAMSTART);

    // NOTE: RAMEND is 2303, which looks to me like the last valid RAM
    // address, whereas in the Python client, I want ram_end - ram_start to
    // be RAM size, thus the "+ 1" here
    uart_put_uint16(RAMEND + 1);

    uart_put_uint16((uint16_t) TASKS);
    uart_put_uint16(MAX_TASKS);
    uart_put_uint16(TASK_SIZE);
    uart_put_uint16(offsetof(task_t, mem));

    uart_put_uint32(TICKS_PER_SECOND);
    uart_put_uint16(CYCLES_PER_TICK);
}

static __ALWAYS_INLINE__ void handle_get_builtin_locations(void) {
    uart_put_uint16((uint16_t) N_BUILTINS);
    for (uint16_t i = 0; i < N_BUILTINS; i++) {
        uart_put_uint16(pgm_read_word(&BUILTINS[i]));
    }
}

static __ALWAYS_INLINE__ void handle_load_task(void) {
    task_id_t task_id = get_task_id();
    task_t *task = &TASKS[task_id];
    stop_task(task);
    uint16_t code_size = uart_get_uint16();
    uint16_t heap_size = uart_get_uint16();
    uint16_t data_size = code_size + heap_size;
    task->code_size = code_size;
    task->free = task->mem + data_size;
    char *data = (char*) task->mem;
    char *data_end = data + data_size;
    while (data < data_end) *data++ = uart_get();
    data_end += TASK_MEM_SIZE - data_size;
    while (data < data_end) *data++ = '\0';
    init_task(task);
}

static __ALWAYS_INLINE__ void handle_start_task(void) {
    task_id_t task_id = get_task_id();
    task_t *task = &TASKS[task_id];
    start_task(task);
}

static __ALWAYS_INLINE__ void handle_stop_task(void) {
    task_id_t task_id = get_task_id();
    task_t *task = &TASKS[task_id];
    stop_task(task);
}

static __ALWAYS_INLINE__ void handle_inspect_task(void) {
    task_id_t task_id = get_task_id();
    task_t *task = &TASKS[task_id];
    uart_put_uint16(task->code_size);
    uart_put_uint16(get_heap_size(task));
    char *data = (char*) task;
    char *data_end = data + TASK_SIZE;
    while (data < data_end) uart_put(*data++);
}

static __ALWAYS_INLINE__ void handle_get_memory(void) {
    char *p = (char*)uart_get_uint16();
    char *end = p + uart_get_uint16();
    while (p < end) uart_put(*p++);
}

static __ALWAYS_INLINE__ void handle_set_memory(void) {
    char *p = (char*)uart_get_uint16();
    char *end = p + uart_get_uint16();
    while (p < end) *p++ = uart_get();
}

static __ALWAYS_INLINE__ void handle_get_timer(void) {
    uart_put_uint16(_tick);
}

static __ALWAYS_INLINE__ void handle_message(char msg_type) {
    switch (msg_type) {
        case MESSAGE_PING: handle_ping(); break;
        case MESSAGE_GET_OFFSETS: handle_get_offsets(); break;
        case MESSAGE_GET_BUILTIN_LOCATIONS: handle_get_builtin_locations(); break;
        case MESSAGE_LOAD_TASK: handle_load_task(); break;
        case MESSAGE_START_TASK: handle_start_task(); break;
        case MESSAGE_STOP_TASK: handle_stop_task(); break;
        case MESSAGE_INSPECT_TASK: handle_inspect_task(); break;
        case MESSAGE_GET_MEMORY: handle_get_memory(); break;
        case MESSAGE_SET_MEMORY: handle_set_memory(); break;
        case MESSAGE_GET_TIMER: handle_get_timer(); break;
        default: die("Bad input message type");
    }
}

void send_ping(char c) {
    uart_put_message(MESSAGE_PING);
    uart_put(c);
    char pong = uart_get();
    if (pong != c) die("Bad pong");
}

void send_kernel_log(void) {
    uart_put_message(MESSAGE_KERNEL_LOG);
    // Caller should now use e.g. uart_put_string to send the log message
}

void send_task_log(task_id_t task_id) {
    uart_put_message(MESSAGE_TASK_LOG);
    uart_put(task_id);
    // Caller should now use e.g. uart_put_string to send the log message
}

void send_task_stopped(task_id_t task_id, char reason) {
    uart_put_message(MESSAGE_TASK_STOPPED);
    uart_put(task_id);
    uart_put(reason);
}


///////////////////////////////////////////////////////////////////////////////
// INTERRUPTS
//
// NOTE: just about every function we call from these interrupts is marked with
// static __ALWAYS_INLINE__ so that we don't have to put as much stuff on the
// stack, because each of our tasks has its own stack, and interrupts end up
// using the stack of whichever task is currently running.
// This means that the more stack our interrupts might need to use, the more
// memory we need to allocate for each task (i.e. the bigger TASK_SIZE must
// be), which decreases the maximum number of tasks we can have (i.e.
// MAX_TASKS).
// NOTE: marking the individual functions __ALWAYS_INLINE__ is probably
// equivalent to just passing ISR_FLATTEN to ISR()...
//
// A different (better?..) way to handle all this might be to allocate a
// separate stack space for interrupts to use, and have all interrupts begin
// by setting the stack pointer to it, and end by restoring the old stack
// pointer. Or something.
// But for now, we're keeping things relatively simple (and perhaps therefore
// inefficient)...
//

ISR (USART_RX_vect) {
    // USART received data
    led_toggle();
    char c0 = UDR0;
    if (c0 == MESSAGE_ESCAPE) {
        char c1 = uart_get();
        if (c1 != MESSAGE_ESCAPE) {
            handle_message(c1);
            return;
        }
    }

    // TODO: implement stdin queue
    uart_puts("Got: ");
    uart_put_hex(c0);
    uart_put_newline();
}

ISR (TIMER0_COMPA_vect, ISR_NAKED) {
    // The timer interrupt, represents a "tick", runs every CYCLES_PER_TICK
    // cycles
    _tick++;

    // DEBUGGING PURPOSES: flash the LED so we can tell that this interrupt
    // is working...
    if (_tick == 0) led_toggle();
    sei();
    for (;;) ;

    // Initiates a context switch, doesn't return.
    pause_task();
}


///////////////////////////////////////////////////////////////////////////////
// MAIN

const char *death_message = NULL;
void __attribute__((noreturn)) die(const char *msg) {
    cli(); // disable global interrupts, we're dead now
    death_message = msg;
    led_off();
    while (1) {
        for (uint8_t i = 6; i--;) {
            led_toggle();
            _delay_ms(100);
        }
        _delay_ms(500);
    }
}

void __attribute__((noreturn)) blink_forever(void) {
    led_off();
    while (1) {
        led_toggle();
        _delay_ms(300);
        led_toggle();
        _delay_ms(100);
        led_toggle();
        _delay_ms(100);
        led_toggle();
        _delay_ms(100);
    }
}

int main(void) {
    led_init();
    uart_init();
    timer_init();
    tasks_init();
    set_sleep_mode(SLEEP_MODE_IDLE);

    // Indicate that we've started, by flashing the LED a few times!
    for (uint8_t i = 8; i--;) {
        led_toggle();
        _delay_ms(50);
    }

    sei(); // enable global interrupts
    for (;;) ; // churn a bit until the timer interrupt fires... we will never return!
    return 0;
}
