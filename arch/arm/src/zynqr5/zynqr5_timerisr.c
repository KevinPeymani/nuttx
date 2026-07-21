/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_timerisr.c
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
#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "arm_internal.h"
#include "arch/zynqr5/irq.h"

/****************************************************************************
 * Pre-processor Definitions
 *
 * Zynq UltraScale+ Triple Timer Counter (TTC) register map.
 *
 * TTC0: 0xFF110000, TTC1: 0xFF120000, TTC2: 0xFF130000, TTC3: 0xFF140000
 * Each TTC has three counters; offsets below are for counter N = 0, 1, 2:
 *   CLK_CTRL_N    = base + 0x00 + N*4
 *   CNT_CTRL_N    = base + 0x0C + N*4
 *   CNT_VALUE_N   = base + 0x18 + N*4
 *   INTERVAL_N    = base + 0x24 + N*4
 *   INT_STATUS_N  = base + 0x54 + N*4
 *   IEN_N         = base + 0x60 + N*4
 *
 ****************************************************************************/

/* TTC selection from Kconfig */

#if defined(CONFIG_ZYNQR5_TTC1)
#  define TTC_BASE    0xFF120000U
#elif defined(CONFIG_ZYNQR5_TTC2)
#  define TTC_BASE    0xFF130000U
#elif defined(CONFIG_ZYNQR5_TTC3)
#  define TTC_BASE    0xFF140000U
#else
#  define TTC_BASE    0xFF110000U   /* Default: TTC0 */
#endif

/* Counter selection (0, 1, or 2) */

#if defined(CONFIG_ZYNQR5_TTC_COUNTER0)
#  define TTC_CNUM    0
#elif defined(CONFIG_ZYNQR5_TTC_COUNTER1)
#  define TTC_CNUM    1
#elif defined(CONFIG_ZYNQR5_TTC_COUNTER2)
#  define TTC_CNUM    2
#else
#  define TTC_CNUM    0
#endif

#define TTC_CLK_CTRL    (TTC_BASE + 0x00U + (TTC_CNUM) * 4)
#define TTC_CNT_CTRL    (TTC_BASE + 0x0CU + (TTC_CNUM) * 4)
#define TTC_CNT_VALUE   (TTC_BASE + 0x18U + (TTC_CNUM) * 4)
#define TTC_INTERVAL    (TTC_BASE + 0x24U + (TTC_CNUM) * 4)
#define TTC_INT_STATUS  (TTC_BASE + 0x54U + (TTC_CNUM) * 4)
#define TTC_INT_ENABLE  (TTC_BASE + 0x60U + (TTC_CNUM) * 4)

/* Counter control bits */

#define TTC_CTRL_DIS    (1U << 0)   /* Disable counter (1=off, 0=running) */
#define TTC_CTRL_INT    (1U << 1)   /* Interval mode (1=interval, 0=free-run) */
#define TTC_CTRL_DEC    (1U << 2)   /* Decrement (0=increment) */
#define TTC_CTRL_MATCH  (1U << 3)   /* Match mode */
#define TTC_CTRL_RST    (1U << 4)   /* Reset counter */
#define TTC_CTRL_WAVE   (1U << 5)   /* Waveform output enable */

/* Clock control bits */

#define TTC_CLK_PS_EN   (1U << 0)   /* Prescaler enable */
#define TTC_CLK_PS_MASK (0x1eU)     /* PS_VAL bits [4:1] */
#define TTC_CLK_PS_VAL(v) (((v) & 0xfU) << 1)

/* Interrupt status bits (counter 0 = bit 0, etc.) */

#define TTC_INT_INTERVAL (1U << 0)  /* Interval interrupt */

/* Timing:
 *   Input clock: CONFIG_ZYNQR5_TTC_FREQ (default 100 MHz)
 *   Prescale: PS_VAL=3 → divide by 2^(3+1)=16 → 6.25 MHz
 *   Tick period: CONFIG_USEC_PER_TICK µs (default 10 000 µs = 10 ms)
 *   Interval count = 6.25 MHz × 10 ms = 62 500
 *
 * For other clock frequencies or tick periods adjust PS_VAL accordingly.
 */

#ifndef CONFIG_ZYNQR5_TTC_FREQ
#  define CONFIG_ZYNQR5_TTC_FREQ   100000000UL
#endif

#define TTC_PS_VAL       3U
#define TTC_CLK_DIV      (1UL << (TTC_PS_VAL + 1))     /* 16 */
#define TTC_TICK_CLK     (CONFIG_ZYNQR5_TTC_FREQ / TTC_CLK_DIV)

/* NOTE: the intermediate product (TTC_TICK_CLK * CONFIG_USEC_PER_TICK)
 * overflows a 32-bit unsigned long for common configs -- e.g. the
 * default 6 250 000 * 10 000 = 62 500 000 000, versus a UINT32_MAX of
 * ~4.29e9.  That silently wrapped to 2 370 457 856, which /1000000
 * truncated to 2370 instead of the intended 62500: a ~26x-too-small
 * TTC_INTERVAL, i.e. a ~26x-too-fast system tick.  Do the multiply in
 * 64 bits to avoid this.
 */

#define TTC_INTERVAL_VAL \
  ((uint32_t)(((uint64_t)TTC_TICK_CLK * CONFIG_USEC_PER_TICK) / 1000000ULL))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int zynqr5_timer_interrupt(int irq, FAR void *context, FAR void *arg)
{
  /* Checkpoint A: ISR entered */
  *((volatile uint32_t *)0x1ED3FF70u) += 1;
  __asm__ volatile("dsb sy" ::: "memory");

  /* Reading TTC_INT_STATUS clears the interval flag. */
  (void)getreg32(TTC_INT_STATUS);

  /* Alive counter at end of DDR carveout — readable from A53 via devmem. */
  *((volatile uint32_t *)0x1ED3FFDCu) += 1;

  /* Checkpoint B: before nxsched_process_timer */
  *((volatile uint32_t *)0x1ED3FF6Cu) += 1;
  __asm__ volatile("dsb sy" ::: "memory");

  nxsched_process_timer();

  /* Checkpoint C: after nxsched_process_timer */
  *((volatile uint32_t *)0x1ED3FF68u) += 1;
  __asm__ volatile("dsb sy" ::: "memory");

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_timer_initialize
 *
 * Description:
 *   Set up TTC in interval mode, attach ISR to GIC IRQ 71 (TTC1 counter 0).
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  /* Stop the counter */

  putreg32(TTC_CTRL_DIS, TTC_CNT_CTRL);

  /* Configure prescaler: PS_EN=1, PS_VAL=TTC_PS_VAL */

  putreg32(TTC_CLK_PS_EN | TTC_CLK_PS_VAL(TTC_PS_VAL), TTC_CLK_CTRL);

  /* Set interval value */

  putreg32((uint32_t)TTC_INTERVAL_VAL, TTC_INTERVAL);

  /* Enable interval mode, reset counter, no waveform output */

  putreg32(TTC_CTRL_INT | TTC_CTRL_RST, TTC_CNT_CTRL);

  /* Enable interval interrupt */

  putreg32(TTC_INT_INTERVAL, TTC_INT_ENABLE);

  /* Start the counter (clear DIS bit) */

  putreg32(TTC_CTRL_INT, TTC_CNT_CTRL);

  irq_attach(ZYNQR5_IRQ_TTC1_0, zynqr5_timer_interrupt, NULL);
  up_enable_irq(ZYNQR5_IRQ_TTC1_0);

  /* Post-enable snapshot: GICD_ISENABLER[2] and GICD_ITARGETSR[17].
   * Readable from A53: devmem 0x1ED3FF98 32 (ISENABLER[2], bit 7 = TTC)
   *                    devmem 0x1ED3FF9C 32 (ITARGETSR[17], byte 3 = IRQ 71) */
  putreg32(getreg32(0xF9000108u), 0x1ED3FF98u);  /* GICD_ISENABLER[2] */
  putreg32(getreg32(0xF9000844u), 0x1ED3FF9Cu);  /* GICD_ITARGETSR[17] */
  __asm__ volatile("dsb sy" ::: "memory");
}

/****************************************************************************
 * Name: up_idle
 *
 * Description:
 *   Wait for interrupt.  Timer and IPI are now GIC-driven; idle can sleep.
 *   This replaces the common arm_idle.c (filtered out in Make.defs).
 *
 ****************************************************************************/

/* Polled timer tick — called from rptun spin-poll when GIC is disabled.
 * TTC is now interrupt-driven; this is a no-op kept for link compatibility. */

void zynqr5_timer_poll(void)
{
}

void up_idle(void)
{
  __asm__ volatile("wfi" ::: "memory");

  /* Fallback IPI check: fire every ~10 ms (every 20th TTC wake at ~2 kHz).
   * Catches A53 kicks that the GIC failed to deliver as SPI 33. */

  static uint32_t s_ipi_tick;
  s_ipi_tick++;
  if ((s_ipi_tick & 0x1fu) == 0)
    {
      void zynqr5_rptun_ipi_check(void);
      zynqr5_rptun_ipi_check();
    }
}
