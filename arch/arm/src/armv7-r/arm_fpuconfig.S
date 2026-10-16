/****************************************************************************
 * arch/arm/src/armv7-r/arm_fpuconfig.S
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/irq.h>

#ifdef CONFIG_ARCH_FPU

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Symbols
 ****************************************************************************/

	.globl	arm_fpuconfig
	.file	"arm_fpuconfig.S"

/****************************************************************************
 * Assembly Macros
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

	.text
	.syntax	unified
	.arm

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_fpuconfig
 *
 * Description:
 *   Configure the FPU.  Enables access to CP10 and CP11
 *
 ****************************************************************************/

	.globl	arm_fpuconfig
	.type	arm_fpuconfig, %function

arm_fpuconfig:

	/* Enable access to CP10 and CP11 in CP15.CACR */

	mrc		CP15_CPACR(r0)
	orr		r0, r0, #0xf00000
	mcr		CP15_CPACR(r0)

	/* Enable access to CP10 and CP11 in CP15.NSACR */

#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
	mrc		CP15_NSACR(r0)
	orr		r0, r0, #0xc00
	mcr		CP15_NSACR(r0)
#endif

	/* REVISIT: Do we need to do this? */

	/* Set FPEXC.EN (B30) */

	fmrx		r0, fpexc
	orr		r0, r0, #0x40000000
	fmxr		fpexc, r0

#ifdef CONFIG_ARMV7R_LOCKSTEP_REGINIT
	/* Zero the VFP registers, which are saved in every task context, so
	 * that both lockstep cores store identical values.
	 */

	mov		r0, #0
	vmov		d0, r0, r0
	vmov		d1, r0, r0
	vmov		d2, r0, r0
	vmov		d3, r0, r0
	vmov		d4, r0, r0
	vmov		d5, r0, r0
	vmov		d6, r0, r0
	vmov		d7, r0, r0
	vmov		d8, r0, r0
	vmov		d9, r0, r0
	vmov		d10, r0, r0
	vmov		d11, r0, r0
	vmov		d12, r0, r0
	vmov		d13, r0, r0
	vmov		d14, r0, r0
	vmov		d15, r0, r0
#ifdef CONFIG_ARM_DPFPU32
	vmov		d16, r0, r0
	vmov		d17, r0, r0
	vmov		d18, r0, r0
	vmov		d19, r0, r0
	vmov		d20, r0, r0
	vmov		d21, r0, r0
	vmov		d22, r0, r0
	vmov		d23, r0, r0
	vmov		d24, r0, r0
	vmov		d25, r0, r0
	vmov		d26, r0, r0
	vmov		d27, r0, r0
	vmov		d28, r0, r0
	vmov		d29, r0, r0
	vmov		d30, r0, r0
	vmov		d31, r0, r0
#endif
	vmsr		fpscr, r0
#endif

	bx		lr
	.size	arm_fpuconfig, . - arm_fpuconfig
#endif
	.end
