/* Patch functions are recompiled to C, so this address only has to be a
   valid one that does not overlap the game's own layout. */
SECTIONS {
    . = 0x80800000;

    /* The recompiler identifies replacements by the section a function lives
       in, and IDO has no way to place a function in one -- section attributes
       are a gcc extension. Do it here instead: every function in this build
       replaces a game function of the same name, so all of their code goes
       into the patch section. A helper that is not a replacement would have to
       be kept out of this and given its own section. */
    .recomp_patch : {
        *(.text*)
        *(.recomp_patch*)
    }

    .data : {
        *(.data*)
        *(.rodata*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.MIPS.abiflags)
        *(.pdr)
        *(.comment)
        *(.note*)
        *(.gnu.attributes)
    }
}
