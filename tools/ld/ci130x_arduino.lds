/*
 * Arduino wrapper around the unmodified vendor RAM-mode linker script.
 *
 * The CI130X host image is copied from Flash to SRAM at boot.  The vendor
 * script already gives the sketch's static sections/BSS the memory before a
 * 3 KiB stack and a fixed 100 KiB FreeRTOS heap; heap_start..heap_end is the
 * remaining C/newlib malloc arena.  Keep that allocation dynamic, but reject
 * images that leave less runtime heap than the selected Arduino board option.
 */
PROVIDE(CI_MIN_C_HEAP_SIZE = 0x4000);

INCLUDE ci130x_alg_pro_null.lds

ASSERT(CI_MIN_C_HEAP_SIZE >= 0x4000,
       "CI13XX C/newlib heap minimum must be at least 16 KiB")
ASSERT((heap_end - heap_start) >= CI_MIN_C_HEAP_SIZE,
       "CI13XX static image leaves less than the selected C/newlib heap reserve")
