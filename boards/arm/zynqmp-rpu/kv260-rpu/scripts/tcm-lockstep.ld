/****************************************************************************
 * boards/arm/zynqmp-rpu/kv260-rpu/scripts/tcm-lockstep.ld
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/* The Cortex-R5F inside the Kria KV260 has 256 KB of TCM split between ATCM
 * and BTCM on two cores. Each core has its own ATCM and BTCM (64 KB) but they
 * can be combined in lock-step mode. This script uses lockstep mode.
 *
 * In lockstep, ATCM (0x00000000-0x0001ffff) and BTCM (0x00020000-0x0003ffff)
 * are contiguous, so they are used as one 256 KB region: code fills from
 * ATCM and may spill into BTCM, with data, bss and the heap after it.
 */

MEMORY
{
  tcm (rwx) : ORIGIN = 0x00000000, LENGTH = 256K
}

EXTERN(_vector_start)
ENTRY(_vector_start)

SECTIONS
{
    .text : {
        _stext = ABSOLUTE(.);
        KEEP(*(.vectors))
        *(.text .text.*)
        *(.fixup)
        *(.gnu.warning)
        *(.rodata .rodata.*)
        *(.gnu.linkonce.t.*)
        *(.glue_7)
        *(.glue_7t)
        *(.got)
        *(.gcc_except_table)
        *(.gnu.linkonce.r.*)
        _etext = ABSOLUTE(.);
    } > tcm

    .init_section : {
        _sinit = ABSOLUTE(.);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .ctors))
        _einit = ABSOLUTE(.);
    } > tcm

    .ARM.extab : {
        *(.ARM.extab*)
    } > tcm

    __exidx_start = ABSOLUTE(.);
    .ARM.exidx : {
        *(.ARM.exidx*)
    } > tcm
    __exidx_end = ABSOLUTE(.);

    _eronly = ABSOLUTE(.); // Might be unecessary since we boot from RAM and not flash

    .data : {
        _sdata = ABSOLUTE(.);
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        CONSTRUCTORS
        . = ALIGN(4);
        _edata = ABSOLUTE(.);
    } > tcm

    .bss : {
        _sbss = ABSOLUTE(.);
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = ABSOLUTE(.);
    } > tcm

}
