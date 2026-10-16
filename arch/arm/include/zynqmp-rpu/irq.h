/****************************************************************************
 * arch/arm/include/zynqmp-rpu/irq.h
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

/* This file should never be included directly but, rather,
 * only indirectly through nuttx/irq.h
 */

#ifndef __ARCH_ARM_INCLUDE_ZYNQMP_RPU_IRQ_H
#define __ARCH_ARM_INCLUDE_ZYNQMP_RPU_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/zynqmp-rpu/chip.h>

/* Total number of IRQ numbers.
 * Includes all channels plus GIO second-level interrupts (if enabled).
 *Excluds the phantom vector.  Zero corresponds to channel 0, vector 1.
 */

#define GIC_IRQ_SPI             32
#define ZYNQMP_RPU_NR_IRQS      187
/* Peripheral IRQs: GIC IDs from UG1085 Table 13-1, all level-high */
#define ZYNQMP_RPU_IRQ_UART0    53
#define ZYNQMP_RPU_IRQ_UART1    54
#define ZYNQMP_RPU_IRQ_TTC0_0   68
#define ZYNQMP_RPU_IRQ_TTC0_1   69
#define ZYNQMP_RPU_IRQ_TTC0_2   70
#define ZYNQMP_RPU_IRQ_IPI_CH1  65   /* RPU0 IPI, needed for OpenAMP/RPMsg */

#define NR_IRQS  (ZYNQMP_RPU_NR_IRQS)

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifndef __ASSEMBLY__

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif

#endif /* __ARCH_ARM_INCLUDE_ZYNQMP_RPU_IRQ_H */
