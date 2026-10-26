/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_timerisr.c
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

#include <stdint.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "arm_internal.h"
#include "hardware/zynqmp_rpu_ttc.h"

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The system tick comes from one counter of one TTC.  Under remoteproc the
 * RPU must use a TTC that Linux does not own; changing these three lines
 * moves the tick to another TTC or counter.
 */

#define TIMER_BASE     ZYNQMP_RPU_TTC2_BASE
#define TIMER_COUNTER  0
#define TIMER_IRQ      ZYNQMP_RPU_IRQ_TTC2_0

/* The counter runs from LPD_LSBUS_CLK (Clock_Control.C_Src = 0), whose
 * rate the board provides.
 */

#ifndef BOARD_TTC_CLK_FREQUENCY
#  error BOARD_TTC_CLK_FREQUENCY not defined
#endif

/* Timing Calculations:
 *
 * In interval mode the counter counts from 0 up to INTERVAL and then
 * restarts, so one period is INTERVAL + 1 counts:
 *
 *   INTERVAL = CONFIG_USEC_PER_TICK * TTCCLK / 1,000,000 - 1
 *
 * For Example:
 *   TTCCLK  = 100,000,000               Hz
 *   CONFIG_USEC_PER_TICK = 10,000       Microseconds
 *   INTERVAL = 10,000 * 100 - 1
 *            = 999,999
 *
 * NOTES:
 *   - Dividing the clock by 1,000,000 first keeps the product in 32 bits;
 *     this needs TTCCLK to be a whole number of MHz.
 *   - The Interval register is 32 bits on ZynqMP, so no prescaler is needed
 *     for any practical tick period.
 */

#if (BOARD_TTC_CLK_FREQUENCY % 1000000) != 0
#  error BOARD_TTC_CLK_FREQUENCY must be a whole number of MHz
#endif

#define TTC_INTERVAL \
  ((uint32_t)(BOARD_TTC_CLK_FREQUENCY / 1000000) * CONFIG_USEC_PER_TICK - 1)

/* Register addresses of the selected counter */

#define TTC_REG(off)   (TIMER_BASE + XTTCPS_CNT_OFFSET(off, TIMER_COUNTER))

#define TTC_CLK_CNTRL  TTC_REG(XTTCPS_CLK_CNTRL_OFFSET)
#define TTC_CNT_CNTRL  TTC_REG(XTTCPS_CNT_CNTRL_OFFSET)
#define TTC_INTERVAL_VAL TTC_REG(XTTCPS_INTERVAL_VAL_OFFSET)
#define TTC_ISR        TTC_REG(XTTCPS_ISR_OFFSET)
#define TTC_IER        TTC_REG(XTTCPS_IER_OFFSET)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  zynqmp_rpu_timerisr
 *
 * Description:
 *   The timer ISR will perform a variety of services for various portions
 *   of the systems.
 *
 ****************************************************************************/

static int zynqmp_rpu_timerisr(int irq, void *context, void *arg)
{
  /* Reading the interrupt register clears it, which deasserts the
   * (level-sensitive) interrupt line.
   */

  getreg32(TTC_ISR);

  /* Process timer interrupt */

  nxsched_process_timer();
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  up_timer_initialize
 *
 * Description:
 *   This function is called during start-up to initialize the timer
 *   interrupt.
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  /* Stop the counter in case a previous owner left it running */

  up_disable_irq(TIMER_IRQ);
  putreg32(XTTCPS_CNT_CNTRL_RESET_VALUE, TTC_CNT_CNTRL);

  /* Mask all of its interrupts, then read the interrupt register to clear
   * anything already latched.
   */

  putreg32(0, TTC_IER);
  getreg32(TTC_ISR);

  /* Count LPD_LSBUS_CLK directly: internal clock, no prescaler */

  putreg32(0, TTC_CLK_CNTRL);

  /* Set the tick period */

  putreg32(TTC_INTERVAL, TTC_INTERVAL_VAL);

  /* Attach the interrupt handler and unmask the interval interrupt
   * (still disabled at the GIC).
   */

  DEBUGVERIFY(irq_attach(TIMER_IRQ, zynqmp_rpu_timerisr, NULL));
  putreg32(XTTCPS_IXR_INTERVAL_MASK, TTC_IER);

  /* Start the counter: interval mode, count up, reset the count to zero,
   * waveform output disabled (EN_WAVE is active low).  Writing DIS = 0
   * enables it.
   */

  putreg32(XTTCPS_CNT_CNTRL_INT_MASK | XTTCPS_CNT_CNTRL_RST_MASK |
           XTTCPS_CNT_CNTRL_EN_WAVE_MASK, TTC_CNT_CNTRL);

  /* Enable the TTC interrupt at the GIC */

  up_enable_irq(TIMER_IRQ);
}
