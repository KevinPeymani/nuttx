/****************************************************************************
 * arch/arm/include/zynqr5/chip.h
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

#ifndef __ARCH_ARM_INCLUDE_ZYNQR5_CHIP_H
#define __ARCH_ARM_INCLUDE_ZYNQR5_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* AMD/Xilinx Zynq UltraScale+ MPSoC Cortex-R5F (RPU) */

#define ZYNQR5_CORTEX_R5F       1           /* Cortex-R5F family */

/* TCM (Tightly Coupled Memory) */

#define ZYNQR5_ATCM_BASE        0x00000000  /* ATCM base (RPU0 split mode) */
#define ZYNQR5_ATCM_SIZE        (128*1024)  /* 128 KB ATCM */
#define ZYNQR5_BTCM_BASE        0x00020000  /* BTCM base */
#define ZYNQR5_BTCM_SIZE        (64*1024)   /* 64 KB BTCM */

/* OCM (On-Chip Memory) */

#define ZYNQR5_OCM_BASE         0xFFFC0000  /* OCM base */
#define ZYNQR5_OCM_SIZE         (256*1024)  /* 256 KB OCM */

/* UART0 base address */

#define ZYNQR5_UART0_BASE       0xFF000000  /* UART0 (PS UART) */
#define ZYNQR5_UART1_BASE       0xFF010000  /* UART1 (PS UART) */

/* RPU GIC (ARM PL390 GICv1) — the R5F RPU has its own dedicated GIC,
 * completely separate from the APU GIC-400 at 0xF9010000/0xF9020000.
 * RPU GICD = 0xF9000000, RPU GICC = 0xF9001000. */

#define ZYNQR5_GIC_BASE         0xF9000000  /* RPU GIC base */
#define ZYNQR5_GIC_DIST_BASE    ZYNQR5_GIC_BASE              /* GICD 0xF9000000 */
#define ZYNQR5_GIC_CPU_BASE     (ZYNQR5_GIC_BASE + 0x1000)  /* GICC 0xF9001000 */

/* Override standard MPCore offsets before mpcore.h defines them.
 * RPU GICv1 PL390: GICD at base+0, GICC at base+0x1000. */

#define MPCORE_ICD_OFFSET       0x00000  /* GICD at GIC_BASE + 0 */
#define MPCORE_ICC_OFFSET       0x01000  /* GICC at GIC_BASE + 0x1000 */

#define CHIP_MPCORE_VBASE       ZYNQR5_GIC_BASE

/* Number of IRQs */

#define ZYNQR5_IRQ_NINT         148         /* PS GIC: 148 interrupts */

#endif /* __ARCH_ARM_INCLUDE_ZYNQR5_CHIP_H */
