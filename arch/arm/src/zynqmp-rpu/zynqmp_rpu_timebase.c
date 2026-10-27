/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_timebase.c
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

#include <sys/types.h>
#include <stdint.h>

#include <nuttx/arch.h>

#include "arm_internal.h"
#include "hardware/zynqmp_rpu_ttc.h"
#include "zynqmp_rpu_timebase.h"

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The counter runs from LPD_LSBUS_CLK, whose rate the board provides.  A
 * whole number of counts per microsecond keeps the conversions exact.
 */

#ifndef BOARD_TTC_CLK_FREQUENCY
#  error BOARD_TTC_CLK_FREQUENCY not defined
#endif

#if (BOARD_TTC_CLK_FREQUENCY % 1000000) != 0
#  error BOARD_TTC_CLK_FREQUENCY must be a whole number of MHz
#endif

#define COUNTS_PER_USEC  (BOARD_TTC_CLK_FREQUENCY / 1000000)

/* The timebase is one counter of one TTC, left running freely: it counts
 * the TTC clock (BOARD_TTC_CLK_FREQUENCY) from 0 to 0xffffffff and wraps.
 * Kconfig selects the TTC and the counter.  Under remoteproc the RPU must
 * use a TTC that Linux does not own.
 */

#if CONFIG_ZYNQMP_RPU_TIMEBASE_TTC == 0
#  define TIMEBASE_BASE    ZYNQMP_RPU_TTC0_BASE
#elif CONFIG_ZYNQMP_RPU_TIMEBASE_TTC == 1
#  define TIMEBASE_BASE    ZYNQMP_RPU_TTC1_BASE
#elif CONFIG_ZYNQMP_RPU_TIMEBASE_TTC == 2
#  define TIMEBASE_BASE    ZYNQMP_RPU_TTC2_BASE
#elif CONFIG_ZYNQMP_RPU_TIMEBASE_TTC == 3
#  define TIMEBASE_BASE    ZYNQMP_RPU_TTC3_BASE
#else
#  error CONFIG_ZYNQMP_RPU_TIMEBASE_TTC must be 0-3
#endif

#define TIMEBASE_COUNTER   CONFIG_ZYNQMP_RPU_TIMEBASE_COUNTER

/* zynqmp_rpu_timerisr.c runs the system tick on TTC2 counter 0 */

#if !defined(CONFIG_SCHED_TICKLESS) && \
    CONFIG_ZYNQMP_RPU_TIMEBASE_TTC == 2 && TIMEBASE_COUNTER == 0
#  error TTC2 counter 0 is the system tick
#endif

/* Register addresses of the selected counter */

#define TTC_REG(off)     (TIMEBASE_BASE + \
                          XTTCPS_CNT_OFFSET(off, TIMEBASE_COUNTER))

#define TTC_CLK_CNTRL    TTC_REG(XTTCPS_CLK_CNTRL_OFFSET)
#define TTC_CNT_CNTRL    TTC_REG(XTTCPS_CNT_CNTRL_OFFSET)
#define TTC_COUNT_VALUE  TTC_REG(XTTCPS_COUNT_VALUE_OFFSET)
#define TTC_ISR          TTC_REG(XTTCPS_ISR_OFFSET)
#define TTC_IER          TTC_REG(XTTCPS_IER_OFFSET)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: timebase_wait
 *
 * Description:
 *   Busy-wait until the timebase has advanced by at least 'counts'.  The
 *   32-bit differences between successive reads are added up, so the wait
 *   may be longer than one wrap of the counter (2^32 counts).
 *
 ****************************************************************************/

static void timebase_wait(uint64_t counts)
{
  uint64_t elapsed = 0;
  uint32_t last = zynqmp_rpu_timebase();
  uint32_t now;

  while (elapsed < counts)
    {
      now      = zynqmp_rpu_timebase();
      elapsed += (uint32_t)(now - last);
      last     = now;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zynqmp_rpu_timebase_init
 *
 * Description:
 *   Start the timebase counter from zero.  It runs without interrupts, so
 *   it can be started before the GIC and read with interrupts disabled.
 *
 ****************************************************************************/

void zynqmp_rpu_timebase_init(void)
{
  /* Stop the counter in case a previous owner left it running */

  putreg32(XTTCPS_CNT_CNTRL_RESET_VALUE, TTC_CNT_CNTRL);

  /* Mask all of its interrupts, then read the interrupt register to clear
   * anything already latched.
   */

  putreg32(0, TTC_IER);
  getreg32(TTC_ISR);

  /* Count the TTC clock directly: internal clock, no prescaler */

  putreg32(0, TTC_CLK_CNTRL);

  /* Start the counter: overflow mode (interval mode off, so it wraps at
   * 0xffffffff), count up, reset the count to zero, waveform output
   * disabled (EN_WAVE is active low).  Writing DIS = 0 enables it.
   */

  putreg32(XTTCPS_CNT_CNTRL_RST_MASK | XTTCPS_CNT_CNTRL_EN_WAVE_MASK,
           TTC_CNT_CNTRL);
}

/****************************************************************************
 * Name: zynqmp_rpu_timebase
 *
 * Description:
 *   Return the current count of the timebase counter.  It advances at
 *   BOARD_TTC_CLK_FREQUENCY and wraps at 2^32; take differences between
 *   two reads as uint32_t.
 *
 ****************************************************************************/

uint32_t zynqmp_rpu_timebase(void)
{
  return getreg32(TTC_COUNT_VALUE);
}

/****************************************************************************
 * Name: up_udelay
 *
 * Description:
 *   Delay inline for the requested number of microseconds, timed on the
 *   timebase counter.  Replaces the calibrated loop of
 *   sched/clock/delay.c, so CONFIG_BOARD_LOOPSPERMSEC is unused.
 *
 *   *** NOT multi-tasking friendly ***
 *
 ****************************************************************************/

void up_udelay(useconds_t microseconds)
{
  timebase_wait((uint64_t)microseconds * COUNTS_PER_USEC);
}

/****************************************************************************
 * Name: up_ndelay
 *
 * Description:
 *   Delay inline for the requested number of nanoseconds, rounded up to
 *   the next count of the timebase.
 *
 *   *** NOT multi-tasking friendly ***
 *
 ****************************************************************************/

void up_ndelay(unsigned long nanoseconds)
{
  timebase_wait(((uint64_t)nanoseconds * COUNTS_PER_USEC + 999) / 1000);
}
