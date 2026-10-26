/****************************************************************************
 * arch/arm/src/zynqmp-rpu/hardware/zynqmp_rpu_ipi.h
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

/* References:
 * UG1085 (Interrupts chapter, Inter-Processor Interrupts) & UG1087 (IPI
 * Module)
 * Register offsets follow AMD's xipipsu_hw.h
 */

#ifndef __ARCH_ARM_SRC_ZYNQMP_RPU_HARDWARE_ZYNQMP_RPU_IPI_H
#define __ARCH_ARM_SRC_ZYNQMP_RPU_HARDWARE_ZYNQMP_RPU_IPI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * ZYNQMP_RPU IPI Channel Base Addresses
 *
 * Each IPI channel has its own register block.  A processor uses the
 * channel assigned to it: it triggers other channels through its own TRIG
 * register and receives from them through its own ISR.  Channels 3-6
 * belong to the PMU and are not listed.
 ****************************************************************************/

#define ZYNQMP_RPU_IPI_CH0_BASE    0xff300000 /* Ch 0: APU */
#define ZYNQMP_RPU_IPI_CH1_BASE    0xff310000 /* Ch 1: RPU0 */
#define ZYNQMP_RPU_IPI_CH2_BASE    0xff320000 /* Ch 2: RPU1 */
#define ZYNQMP_RPU_IPI_CH7_BASE    0xff340000 /* Ch 7: PL IPI0 */
#define ZYNQMP_RPU_IPI_CH8_BASE    0xff350000 /* Ch 8: PL IPI1 */
#define ZYNQMP_RPU_IPI_CH9_BASE    0xff360000 /* Ch 9: PL IPI2 */
#define ZYNQMP_RPU_IPI_CH10_BASE   0xff370000 /* Ch 10: PL IPI3 */

/****************************************************************************
 * ZYNQMP_RPU IPI Registers and Bit Definitions
 *
 * Register Map
 *
 * Every channel has the same layout.  All six registers share one bit
 * layout, with one bit per channel (see the channel masks below).
 ****************************************************************************/

#define XIPIPSU_TRIG_OFFSET        0x0000U /* Trigger, write-only */
#define XIPIPSU_OBS_OFFSET         0x0004U /* Observation, read-only */
#define XIPIPSU_ISR_OFFSET         0x0010U /* Interrupt Status, W1C */
#define XIPIPSU_IMR_OFFSET         0x0014U /* Interrupt Mask, read-only */
#define XIPIPSU_IER_OFFSET         0x0018U /* Interrupt Enable, write-only */
#define XIPIPSU_IDR_OFFSET         0x001CU /* Interrupt Disable, write-only */

/****************************************************************************
 * Channel Masks
 *
 * TRIG:    writing a channel's bit raises an interrupt on that channel.
 * OBS:     a bit stays set while the matching channel has not yet cleared
 *          the interrupt this channel sent it.
 * ISR:     a bit is set when that channel triggered this one.  Writing 1
 *          clears it, which also clears the sender's OBS bit.
 * IMR:     a set bit means that source is masked.  Every source is masked
 *          at reset.
 * IER/IDR: writing 1 unmasks/masks that source.
 ****************************************************************************/

#define XIPIPSU_CH0_MASK           0x00000001U /* Ch 0: APU */
#define XIPIPSU_CH1_MASK           0x00000100U /* Ch 1: RPU0 */
#define XIPIPSU_CH2_MASK           0x00000200U /* Ch 2: RPU1 */
#define XIPIPSU_CH3_MASK           0x00010000U /* Ch 3: PMU IPI0 */
#define XIPIPSU_CH4_MASK           0x00020000U /* Ch 4: PMU IPI1 */
#define XIPIPSU_CH5_MASK           0x00040000U /* Ch 5: PMU IPI2 */
#define XIPIPSU_CH6_MASK           0x00080000U /* Ch 6: PMU IPI3 */
#define XIPIPSU_CH7_MASK           0x01000000U /* Ch 7: PL IPI0 */
#define XIPIPSU_CH8_MASK           0x02000000U /* Ch 8: PL IPI1 */
#define XIPIPSU_CH9_MASK           0x04000000U /* Ch 9: PL IPI2 */
#define XIPIPSU_CH10_MASK          0x08000000U /* Ch 10: PL IPI3 */
#define XIPIPSU_ALL_MASK           0x0f0f0301U

#endif /* __ARCH_ARM_SRC_ZYNQMP_RPU_HARDWARE_ZYNQMP_RPU_IPI_H */
