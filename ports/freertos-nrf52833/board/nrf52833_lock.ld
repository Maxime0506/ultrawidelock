/*
 * Flash and RAM map for the DWM3001CDK lock.
 *
 * The one thing this script does that a stock nRF52833 script does not is stop
 * the image at 0x7e000. The two pages above that are the persistent key-value
 * store (storage/kv_flash_freertos.c), which holds the credential provisioning
 * record and OpenThread's settings including the SRP client key. Leaving them
 * inside the FLASH region would let a large image grow into them and be
 * discovered as a lock that forgets its identity after a firmware update, which
 * is a failure worth spending 8 KB of link-time headroom to make impossible.
 *
 * MCUboot is not in this map yet. When it arrives it takes the bottom of flash
 * and this origin moves; the store stays where it is, which is why it was put
 * at the top rather than immediately after the application.
 */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 0x7e000
  STORAGE (r) : ORIGIN = 0x0007e000, LENGTH = 0x02000
  RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 0x20000
}

__ultrawidelock_storage_start = ORIGIN(STORAGE);
__ultrawidelock_storage_end = ORIGIN(STORAGE) + LENGTH(STORAGE);

/*
 * The main stack, used by every interrupt handler and by main() before the
 * scheduler starts. Task stacks are separate and come from their own storage.
 * MPSL's handlers run here at priority 0, so this is not a place to economise.
 */
__stack_size = DEFINED(__stack_size) ? __stack_size : 4096;

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        *(.text*)

        KEEP(*(.init))
        KEEP(*(.fini))

        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)

        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)

        *(.rodata*)

        KEEP(*(.eh_frame*))
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;

    . = ALIGN(4);
    __etext = .;

    .data : AT (__etext)
    {
        __data_start__ = .;
        *(vtable)
        *(.data*)

        . = ALIGN(4);
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);

        . = ALIGN(4);
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);

        KEEP(*(.jcr*))
        . = ALIGN(4);
        __data_end__ = .;
    } > RAM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /*
     * Deliberately not loaded and not zeroed. SEGGER RTT's control block lives
     * here so a debugger attached after a reset can still find a buffer that
     * survived it, and the fault path can leave a breadcrumb for the next boot.
     */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit*)
        . = ALIGN(4);
    } > RAM

    .heap (COPY) :
    {
        . = ALIGN(8);
        __HeapBase = .;
        __end__ = .;
        PROVIDE(end = .);
        KEEP(*(.heap*))
        __HeapLimit = .;
    } > RAM

    .stack (COPY) :
    {
        . = ALIGN(8);
        __StackLimit = .;
        . = . + __stack_size;
        . = ALIGN(8);
        __StackTop = .;
    } > RAM
    PROVIDE(__stack = __StackTop);

    /*
     * The stack is placed explicitly rather than floated to the top of RAM, so
     * that overflowing it runs into a linker error at build time instead of
     * into whatever happens to be below it at run time.
     */
    ASSERT(__StackTop <= ORIGIN(RAM) + LENGTH(RAM), "RAM overflowed: stack does not fit")
    ASSERT(__StackLimit >= __HeapLimit, "RAM overflowed: heap and stack overlap")
    ASSERT(__etext + (__data_end__ - __data_start__) <= ORIGIN(FLASH) + LENGTH(FLASH),
           "FLASH overflowed: image would reach into the key-value store at 0x7e000")
}
