/****************************************************************************
 * arch/arm/include/zynqmp-rpu/chip.h
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

#ifndef __ARCH_ARM_INCLUDE_ZYNQMP_RPU_CHIP_H
#define __ARCH_ARM_INCLUDE_ZYNQMP_RPU_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

/*                 Kria K26 / KV260 (XCZU5EV)
 * Package         FCBGA784
 * CPU             2x ARM Cortex-R5F
 * Frequency (MHz) 533
 * Flash (KB)      External QSPI
 * RAM (KB)        256 OCM + 256 RPU TCM
 * Data Flash (KB) -
 * EMAC            2 GEM
 * FlexRay         -
 * CAN             2 CAN FD
 * MibADC (CH)     1 XADC
 * N2HET (Ch)      -
 * ePWM Channels   -
 * eCAP Channels   -
 * eQEP Channels   -
 * MibSPI (CS)     -
 * SPI (CS)        2
 * SCI (LIN)       2 UART
 * I2C             2
 * GPIO (INT)      78 MIO
 * EMIF            64-bit DDR4
 * ETM (Trace)     2 R5F ETM
 * RTP/DMM         -
 */

/* Peripheral counts */
#define ZYNQMP_RPU_NUART        2   /* UART0, UART1 */
#define ZYNQMP_RPU_NSPI         2   /* SPI0, SPI1 */
#define ZYNQMP_RPU_SPI_NCS      3   /* Chip selects per SPI */
#define ZYNQMP_RPU_NI2C         2   /* I2C0, I2C1 */
#define ZYNQMP_RPU_NCAN         2   /* CAN 2.0B */
#define ZYNQMP_RPU_NEMAC        4   /* GEM0-3 */
#define ZYNQMP_RPU_NTTC         4   /* TTC0-3, 3 counters each */
#define ZYNQMP_RPU_NGPIO_MIO    78
#define ZYNQMP_RPU_NGPIO_EMIO   96

/* Cache */
#define ZYNQMP_RPU_DCACHE_LINESIZE  32
#define ZYNQMP_RPU_ICACHE_LINESIZE  32

/* TCM, R5 local view, split mode (lockstep: 128 KB each) */
#define ZYNQMP_RPU_ATCM_BASE    0x00000000
#define ZYNQMP_RPU_ATCM_SIZE    (64 * 1024)
#define ZYNQMP_RPU_BTCM_BASE    0x00020000
#define ZYNQMP_RPU_BTCM_SIZE    (64 * 1024)
#endif /* __ARCH_ARM_INCLUDE_ZYNQMP_RPU_CHIP_H */