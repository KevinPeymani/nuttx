/****************************************************************************
 * arch/arm/include/zynqr5/irq.h
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

#ifndef __ARCH_ARM_INCLUDE_ZYNQR5_IRQ_H
#define __ARCH_ARM_INCLUDE_ZYNQR5_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/zynqr5/chip.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ARM Cortex-R5F Software Generated Interrupts (SGI): 0-15 */

#define ZYNQR5_IRQ_SGI0         0
#define ZYNQR5_IRQ_SGI1         1
#define ZYNQR5_IRQ_SGI2         2
#define ZYNQR5_IRQ_SGI3         3
#define ZYNQR5_IRQ_SGI4         4
#define ZYNQR5_IRQ_SGI5         5
#define ZYNQR5_IRQ_SGI6         6
#define ZYNQR5_IRQ_SGI7         7
#define ZYNQR5_IRQ_SGI8         8
#define ZYNQR5_IRQ_SGI9         9
#define ZYNQR5_IRQ_SGI10        10
#define ZYNQR5_IRQ_SGI11        11
#define ZYNQR5_IRQ_SGI12        12
#define ZYNQR5_IRQ_SGI13        13
#define ZYNQR5_IRQ_SGI14        14
#define ZYNQR5_IRQ_SGI15        15

/* Private Peripheral Interrupts (PPI): 16-31 */

#define ZYNQR5_IRQ_GTIMER       27  /* Global Timer */
#define ZYNQR5_IRQ_NFIQ         28  /* nFIQ */
#define ZYNQR5_IRQ_PTIMER       29  /* Private Timer */
#define ZYNQR5_IRQ_WDOG         30  /* Watchdog */
#define ZYNQR5_IRQ_NIRQ         31  /* nIRQ */

/* Shared Peripheral Interrupts (SPI): 32+ */

#define ZYNQR5_IRQ_SPI_OFFSET   32

/* PS UART0 interrupt */
#define ZYNQR5_IRQ_UART0        (ZYNQR5_IRQ_SPI_OFFSET + 21)   /* SPI #21 = IRQ 53 */
#define ZYNQR5_IRQ_UART1        (ZYNQR5_IRQ_SPI_OFFSET + 22)   /* SPI #22 = IRQ 54 */

/* TTC0 (Triple Timer Counter 0) */
#define ZYNQR5_IRQ_TTC0_0       (ZYNQR5_IRQ_SPI_OFFSET + 36)   /* IRQ 68 */
#define ZYNQR5_IRQ_TTC0_1       (ZYNQR5_IRQ_SPI_OFFSET + 37)   /* IRQ 69 */
#define ZYNQR5_IRQ_TTC0_2       (ZYNQR5_IRQ_SPI_OFFSET + 38)   /* IRQ 70 */

/* TTC1 */
#define ZYNQR5_IRQ_TTC1_0       (ZYNQR5_IRQ_SPI_OFFSET + 39)   /* IRQ 71 */
#define ZYNQR5_IRQ_TTC1_1       (ZYNQR5_IRQ_SPI_OFFSET + 40)   /* IRQ 72 */
#define ZYNQR5_IRQ_TTC1_2       (ZYNQR5_IRQ_SPI_OFFSET + 41)   /* IRQ 73 */

/* Total number of IRQs */

#define NR_IRQS                 ZYNQR5_IRQ_NINT

#endif /* __ARCH_ARM_INCLUDE_ZYNQR5_IRQ_H */
