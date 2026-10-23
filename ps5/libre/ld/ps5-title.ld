/*
 * RPCS3 for PS5 - the title's memory layout.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md). A position-independent
 * title in four loadable segments, each 16 KiB aligned (the console's page): its
 * code (read, execute), its read-only data with the unwind tables (read), the
 * data relocated at load with the dynamic linking tables (read, write), then its
 * data and bss (read, write). The code starts at address 0. No interpreter and no
 * program header table are mapped: the console's loader reads the file's.
 *
 * LLVM's libunwind finds the unwind tables through __eh_frame_hdr_start/end and
 * __eh_frame_start/end, as it does without dl_iterate_phdr. The start-up
 * (ps5/libre/crt/start.c) runs the init arrays between the bounds defined here.
 * Sections named like C identifiers (libc++'s __lcxx_override, protobuf's
 * pb_defaults) get their __start_ and __stop_ symbols from the linker.
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
   code     PT_LOAD    FLAGS(5);   /* R X */
   constant PT_LOAD    FLAGS(4);   /* R   */
   relocate PT_LOAD    FLAGS(6);   /* R W */
   data     PT_LOAD    FLAGS(6);   /* R W */
   dynamic  PT_DYNAMIC FLAGS(6);
}

SECTIONS
{
   . = 0;

   .text ALIGN(16384) :
   {
      __libre_image_start = .; /* the title's image, for dladdr (libc/misc.c) */
      *(.text.unlikely .text.unlikely.*)
      *(.text.startup .text.startup.*)
      *(.text.hot .text.hot.*)
      *(.text .text.*)
   } :code
   malloc_hook : { KEEP(*(malloc_hook)) } :code
   __lcxx_override : { KEEP(*(__lcxx_override)) } :code

   . = ALIGN(16384);
   .eh_frame_hdr :
   {
      __eh_frame_hdr_start = .;
      *(.eh_frame_hdr)
      __eh_frame_hdr_end = .;
   } :constant
   .eh_frame :
   {
      __eh_frame_start = .;
      KEEP(*(.eh_frame))
      __eh_frame_end = .;
   } :constant
   .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) } :constant
   .rodata : { *(.rodata .rodata.*) } :constant

   . = ALIGN(16384);
   .data.rel.ro : { *(.data.rel.ro.local .data.rel.ro.local.*) *(.data.rel.ro .data.rel.ro.*) } :relocate
   .got : { *(.got) } :relocate
   .got.plt : { *(.got.plt) } :relocate
   .preinit_array :
   {
      __preinit_array_start = .;
      KEEP(*(.preinit_array))
      __preinit_array_end = .;
   } :relocate
   .init_array :
   {
      __init_array_start = .;
      KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
      KEEP(*(.init_array .ctors))
      __init_array_end = .;
   } :relocate
   .fini_array :
   {
      __fini_array_start = .;
      KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
      KEEP(*(.fini_array .dtors))
      __fini_array_end = .;
   } :relocate
   .dynsym : { *(.dynsym) } :relocate
   .gnu.hash : { *(.gnu.hash) } :relocate
   .hash : { *(.hash) } :relocate
   .dynstr : { *(.dynstr) } :relocate
   .rela.dyn : { *(.rela.init) *(.rela.text .rela.text.*) *(.rela.rodata .rela.rodata.*) *(.rela.data .rela.data.*) *(.rela.got) *(.rela.bss .rela.bss.*) *(.rela.dyn) } :relocate
   .rela.plt : { *(.rela.plt) } :relocate

   . = ALIGN(16384);
   .dynamic : { *(.dynamic) } :data :dynamic
   .data : { *(.data .data.*) } :data
   pb_defaults : { KEEP(*(pb_defaults)) } :data
   .bss ALIGN(4096) : { *(.bss .bss.*) *(COMMON) __libre_image_end = .; } :data

   /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) *(.interp) }
}
