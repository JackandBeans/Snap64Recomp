/* Patch functions are recompiled to C, but the .data section below them --
   where IDO keeps every float literal and table -- is read at run time, so
   the ELF's bytes are copied into memory at start-up (src/main.cpp,
   snap_register_patches) by librecomp, which places them at its
   patch_rdram_start (librecomp/addresses.hpp). The link address has to be
   that same address; below it librecomp keeps its PI handles. */
SECTIONS {
    . = 0x80801000;

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
