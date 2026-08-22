/* Normalize Clang input sections to the names expected by NuttX modlib. */
SECTIONS
{
    .text : ALIGN(4)
    {
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(4)
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4)
    {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(4)
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    .init_array : ALIGN(4)
    {
        KEEP(*(.init_array))
        KEEP(*(.init_array.*))
    }

    .fini_array : ALIGN(4)
    {
        KEEP(*(.fini_array))
        KEEP(*(.fini_array.*))
    }

    /DISCARD/ :
    {
        /* The native module neither throws nor unwinds. Keeping Clang's
         * per-function EHABI index only consumes the loader's scarce
         * SHF_ALLOC budget; no runtime code reads it. */
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.llvm_addrsig)
        *(.note.GNU-stack)
    }
}
