/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_tcmstart.S
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

/****************************************************************************
 * TCM trampoline vector table.
 *
 * The R5F in split mode has ATCM at 0x00000000 (CPU view).  The remoteproc
 * driver maps ELF PT_LOAD entries with p_paddr=0 to APU physical 0xFFE00000
 * so the Linux kernel can write the TCM image before releasing the R5F.
 *
 * On R5F reset the CPU fetches from ATCM[0x00].  Each entry performs an
 * indirect branch (LDR PC, literal) to the corresponding entry in the
 * NuttX vector table located in DDR at _vector_start (0x3ED00000).
 *
 * The reference to _vector_start ensures arm_vectortab.o is pulled from
 * libarch.a so the DDR vector table section (.vectors at 0x3ED00000) is
 * populated.  Do NOT define _vector_start as a linker-script symbol —
 * that would satisfy the reference without linking arm_vectortab.o.
 *
 ****************************************************************************/

	.file	"zynqr5_tcmstart.S"

	.globl	_tcm_start
	.globl	_vector_start		/* external — defined in arm_vectortab.o */

	.section	.tcm_vectors, "ax"
	.arm

/****************************************************************************
 * Name: _tcm_start
 *
 * Eight ARM exception vector entries that redirect to the corresponding
 * entry in the DDR NuttX vector table (_vector_start).
 *
 *  Offset  Exception
 *  0x00    Reset
 *  0x04    Undefined instruction
 *  0x08    Supervisor call (SVC)
 *  0x0C    Prefetch abort
 *  0x10    Data abort
 *  0x14    Address exception (reserved)
 *  0x18    IRQ
 *  0x1C    FIQ
 *
 ****************************************************************************/

_tcm_start:
	ldr		pc, .Lv0		/* 0x00: Reset */
	ldr		pc, .Lv1		/* 0x04: Undefined instruction */
	ldr		pc, .Lv2		/* 0x08: SVC */
	ldr		pc, .Lv3		/* 0x0C: Prefetch abort */
	ldr		pc, .Lv4		/* 0x10: Data abort */
	ldr		pc, .Lv5		/* 0x14: Address exception (reserved) */
	ldr		pc, .Lv6		/* 0x18: IRQ */
	ldr		pc, .Lv7		/* 0x1C: FIQ */

/* Literal pool — absolute DDR addresses computed at link time.
 * Each entry holds _vector_start + N*4, i.e. the address of the
 * Nth ARM exception entry in the DDR vector table.  The CPU loads this
 * address into PC and executes the DDR entry, which then dispatches to
 * the real handler via its own LDR PC literal.
 */

.Lv0:	.long	_vector_start + 0x00	/* Reset    → __start */
.Lv1:	.long	_vector_start + 0x04	/* Undef    → arm_vectorundefinsn */
.Lv2:	.long	_vector_start + 0x08	/* SVC      → arm_vectorsvc */
.Lv3:	.long	_vector_start + 0x0C	/* Prefetch → arm_vectorprefetch */
.Lv4:	.long	_vector_start + 0x10	/* Data     → arm_vectordata */
.Lv5:	.long	_vector_start + 0x14	/* Reserved → arm_vectoraddrexcptn */
.Lv6:	.long	_vector_start + 0x18	/* IRQ      → arm_vectorirq */
.Lv7:	.long	_vector_start + 0x1C	/* FIQ      → arm_vectorfiq */

	.size	_tcm_start, . - _tcm_start
	.end
