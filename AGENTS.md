# Code Review Rules — Zephyr RTOS firmware (C)

## General
REJECT if:
- Hardcoded secrets or credentials
- Code duplication (violates DRY)
- Commented-out code blocks left in place
- Magic numbers for timing, pins, or priorities → use named `#define`/`enum` or Kconfig
- Return values of Zephyr APIs ignored where they can fail (`device_is_ready`, `gpio_pin_configure_dt`, `k_sem_take` with timeout, etc.)

## ISR context
REJECT if:
- Blocking calls inside an ISR (`k_sleep`, `k_msleep`, `k_mutex_lock`, `k_sem_take` with non-`K_NO_WAIT` timeout, `k_msgq_put`/`k_msgq_get` with timeout)
- `printk`/`LOG_*` or other heavy work inside an ISR → defer to a thread or work queue
- Dynamic allocation (`k_malloc`, `malloc`) inside an ISR

## Concurrency
REJECT if:
- Variables shared between an ISR and a thread are not `volatile` or protected (atomic, `irq_lock`, spinlock)
- Shared resources between threads accessed without a mutex, semaphore, or message queue
- Mutex used from ISR context (Zephyr mutexes are thread-only)
- Thread priorities or stack sizes defined without a named constant

## Real-time behavior
REJECT if:
- Busy-wait loops (`k_busy_wait`, empty `while`) in periodic tasks without justification
- Periodic tasks scheduled with relative `k_sleep` where drift matters → use `k_timer` or absolute deadlines
- Instrumentation GPIO toggles removed or reordered in a way that breaks logic-analyzer measurements

## Configuration (`prj.conf`, `*.conf`, `*.overlay`)
REJECT if:
- Kconfig symbols enabled that the code does not use
- Devicetree nodes referenced in C without a matching overlay entry for the target board (`esp32s3_devkitc/esp32s3/procpu`)

## Style
PREFER:
- `static` for file-local functions and variables
- `*_dt` GPIO/ADC APIs with `DT_ALIAS`/`DT_NODELABEL` over raw pin numbers
- Fixed-width types (`uint32_t`, `int16_t`) for hardware and timing values
