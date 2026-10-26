/****************************************************************************
 * arch/arm/src/zynqmp-rpu/hardware/zynqmp_rpu_ttc.h
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
 * UG1085 (Triple-timer Counter chapter) & UG1087 (TTC Module)
 * Register names follow AMD's xttcps_hw.h
 */

#ifndef __ARCH_ARM_SRC_ZYNQMP_RPU_HARDWARE_ZYNQMP_RPU_TTC_H
#define __ARCH_ARM_SRC_ZYNQMP_RPU_HARDWARE_ZYNQMP_RPU_TTC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include "hardware/zynqmp_rpu_memorymap.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * ZYNQMP_RPU TTC Registers and Bit Definitions
 *
 * Register Map
 *
 * Each TTC module holds three counters.  Every per-counter register is an
 * array of three words, so counter n (0-2) is at offset + 4 * n.  UG1087
 * numbers the counters 1-3; these macros number them 0-2.
 ****************************************************************************/

#define XTTCPS_CNT_OFFSET(off, n)  ((off) + ((n) << 2))

#define XTTCPS_CLK_CNTRL_OFFSET    0x0000U /* Clock Control [6:0] */
#define XTTCPS_CNT_CNTRL_OFFSET    0x000CU /* Counter Control [6:0] */
#define XTTCPS_COUNT_VALUE_OFFSET  0x0018U /* Current Counter Value [31:0] */
#define XTTCPS_INTERVAL_VAL_OFFSET 0x0024U /* Interval Value [31:0] */
#define XTTCPS_MATCH_0_OFFSET      0x0030U /* Match 1 value [31:0] */
#define XTTCPS_MATCH_1_OFFSET      0x003CU /* Match 2 value [31:0] */
#define XTTCPS_MATCH_2_OFFSET      0x0048U /* Match 3 value [31:0] */
#define XTTCPS_ISR_OFFSET          0x0054U /* Interrupt Status [5:0] */
#define XTTCPS_IER_OFFSET          0x0060U /* Interrupt Enable [5:0] */
#define XTTCPS_EVCTRL_OFFSET       0x006CU /* Event Control [3:0] */
#define XTTCPS_EVENT_OFFSET        0x0078U /* Event count [31:0] */

/****************************************************************************
 * Clock Control Register
 *
 * Selects the clock source and prescaler.  With C_SRC clear, the counter
 * runs from LPD_LSBUS_CLK.
 ****************************************************************************/

#define XTTCPS_CLK_CNTRL_EXT_EDGE_MASK  0x00000040U /* External clock edge */
#define XTTCPS_CLK_CNTRL_SRC_MASK       0x00000020U /* Clock source */
#define XTTCPS_CLK_CNTRL_PS_VAL_MASK    0x0000001EU /* Prescale value */
#define XTTCPS_CLK_CNTRL_PS_VAL_SHIFT   1U          /* Prescale shift */
#define XTTCPS_CLK_CNTRL_PS_EN_MASK     0x00000001U /* Prescale enable */

/* If prescale is enabled, the count rate is divided by 2^(N+1) */

#define XTTCPS_CLK_CNTRL_PS_VAL(n)      (((n) << XTTCPS_CLK_CNTRL_PS_VAL_SHIFT) & \
                                         XTTCPS_CLK_CNTRL_PS_VAL_MASK)

/****************************************************************************
 * Counter Control Register
 *
 * Operational mode and reset.  The reset value (0x21) leaves the counter
 * disabled with the waveform output off.
 ****************************************************************************/

#define XTTCPS_CNT_CNTRL_POL_WAVE_MASK  0x00000040U /* Waveform polarity */
#define XTTCPS_CNT_CNTRL_EN_WAVE_MASK   0x00000020U /* Waveform output
                                                     * disable (active
                                                     * low enable) */
#define XTTCPS_CNT_CNTRL_RST_MASK       0x00000010U /* Reset counter,
                                                     * self-clearing */
#define XTTCPS_CNT_CNTRL_MATCH_MASK     0x00000008U /* Match mode */
#define XTTCPS_CNT_CNTRL_DECR_MASK      0x00000004U /* Count down */
#define XTTCPS_CNT_CNTRL_INT_MASK       0x00000002U /* Interval mode */
#define XTTCPS_CNT_CNTRL_DIS_MASK       0x00000001U /* Disable counter */
#define XTTCPS_CNT_CNTRL_RESET_VALUE    0x00000021U /* Reset value */

/****************************************************************************
 * Interrupt Registers
 *
 * ISR and IER share the same bit layout.  ISR is clear-on-read: reading it
 * acknowledges every pending source.  IER is a plain read/write mask that
 * is ANDed with ISR to drive the interrupt line.
 ****************************************************************************/

#define XTTCPS_IXR_EVTOVR_MASK   0x00000020U /* Event timer overflow */
#define XTTCPS_IXR_CNT_OVR_MASK  0x00000010U /* Counter overflow */
#define XTTCPS_IXR_MATCH_2_MASK  0x00000008U /* Match 3 */
#define XTTCPS_IXR_MATCH_1_MASK  0x00000004U /* Match 2 */
#define XTTCPS_IXR_MATCH_0_MASK  0x00000002U /* Match 1 */
#define XTTCPS_IXR_INTERVAL_MASK 0x00000001U /* Interval */
#define XTTCPS_IXR_ALL_MASK      0x0000003FU /* All valid interrupts */

/****************************************************************************
 * Event Control Register
 ****************************************************************************/

#define XTTCPS_EVCTRL_OVR_MASK   0x00000004U /* Continue on overflow */
#define XTTCPS_EVCTRL_LEVEL_MASK 0x00000002U /* Count on low level */
#define XTTCPS_EVCTRL_EN_MASK    0x00000001U /* Enable event timer */

#endif /* __ARCH_ARM_SRC_ZYNQMP_RPU_HARDWARE_ZYNQMP_RPU_TTC_H */
