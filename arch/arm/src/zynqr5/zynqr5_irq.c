/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_irq.c
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

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <arch/irq.h>

#include "arm_internal.h"
#include "gic.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* g_current_regs[] holds a reference to the current interrupt-level
 * register storage structure.  Non-NULL only during interrupt processing.
 */

volatile uint32_t *g_current_regs[1];

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 *
 * Description:
 *   Probe RPU GIC GICD_IIDR (read-only, safe regardless of XMPU).
 *   Breadcrumbs at 0x1ED3FFCC are readable from A53 via devmem:
 *     0xBEEF0010 = about to read GICD_IIDR
 *     0xBEEF0011 = GICD_IIDR read completed (value stored at 0x1ED3FFD0)
 *     0xBEEF0014 = about to write/read back GICD_ISENABLER[2] test bit
 *                  (isolated probe — runs before GICD_CTLR is ever touched
 *                  and before arm_gic0_initialize()/arm_gic_initialize())
 *     0xBEEF0015 = isolated probe completed (readback stored at
 *                  0x1ED3FFB4; devmem 0x1ED3FFB4 32 — expect 0x00000002
 *                  if the write stuck, 0x00000000 if it was discarded)
 *     0xBEEF0012 = GIC init + up_irq_enable() completed
 *
 *   Part 0 of boards/amd/kria-kv260/boot-xmpu-fix-plan.md: this isolated
 *   probe exists to tell apart two failure theories.  GICD_ISENABLER[2]
 *   (0xF9000108) and GICD_CTLR (0xF9000000) are different registers, and
 *   ICDICER (cleared by arm_gic0_initialize() right after this probe) may
 *   live in a different security/group banking than CTLR's global-enable
 *   bit.  If ISENABLER's bit sticks here but GICD_CTLR still reads 0 after
 *   arm_gic_initialize() below, that points at secure/non-secure register
 *   banking rather than a protection unit blocking the whole 0xF9000000
 *   aperture uniformly.
 *   rptun spin-poll fallback in up_idle() remains active as a safety net.
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  putreg32(0xBEEF0010u, 0x1ED3FFCCu);
  uint32_t iidr = getreg32(0xF9000008u);  /* GICD_IIDR — read-only probe */
  putreg32(0xBEEF0011u, 0x1ED3FFCCu);
  putreg32(iidr,        0x1ED3FFD0u);     /* store IIDR for inspection */

  /* Clear stale active interrupts left by a previous firmware run that was
   * stopped before writing GICC_EOIR.  The GIC retains "active" state across
   * remoteproc stop/start, leaving GICC_RPR stuck at the stale priority and
   * blocking all new interrupts at the same or lower (numerically higher)
   * priority level.
   *
   * Fix: for each set bit in GICD_ISACTIVER write GICC_EOIR (drops RPR),
   * then write GICD_ICACTIVER to clear the distributor active state. */
  {
    uint32_t i;
    for (i = 0u; i < 5u; i++)
      {
        uint32_t activer = getreg32(0xF9000300u + i * 4u); /* GICD_ISACTIVER[i] */
        uint32_t tmp = activer;
        while (tmp)
          {
            uint32_t bit = (uint32_t)__builtin_ctz(tmp);
            putreg32(i * 32u + bit, 0xF9001010u);          /* GICC_EOIR */
            tmp &= tmp - 1u;
          }
        if (activer)
          {
            putreg32(0xFFFFFFFFu, 0xF9000380u + i * 4u);   /* GICD_ICACTIVER[i] */
          }
      }
  }

  /* Snapshot GICC_RPR after active-interrupt clear — should now be 0xFF (idle).
   * Readable from A53: devmem 0x1ED3FFA8 32 */
  putreg32(getreg32(0xF9001014u), 0x1ED3FFA8u);  /* GICC_RPR post-clear */
  __asm__ volatile("dsb sy" ::: "memory");

  /* Attempt to initialize the RPU GIC (PL390 at 0xF9000000).
   *
   * NOTE: As of 2026-07-09, GICD_CTLR, GICD_ISENABLER, and GICC_CTLR all
   * read back as 0 after arm_gic*_initialize() — writes to at least some
   * registers in this range don't stick.  arm_gic0/gic_initialize() and
   * up_enable_irq() therefore run without faulting but appear to have no
   * effect on those specific registers.
   *
   * root cause UNCONFIRMED as of 2026-07-20 — do not assume LPD_XPPU/XMPU_FPD.
   * This board's own Vivado hardware design configures no XPPU or XMPU
   * regions at all (psu_lpd_xppu_data()/psu_fpd_xmpu_data() in
   * psu_init_gpl.c are empty stubs), and the LPD_XPPU aperture list doesn't
   * even include an entry near 0xF9000000. A more likely explanation being
   * investigated: GIC secure/non-secure register-group banking (writes to a
   * Secure-gated field from a Non-secure context are architecturally RAZ/WI,
   * with no protection unit involved) — this would also explain why IRQ 65
   * apparently *did* get enabled and fire elsewhere (see zynqr5_rptun.c),
   * which is hard to reconcile with "every write to this range is ignored".
   * See boards/amd/kria-kv260/boot-xmpu-fix-plan.md ("Reassessment" section)
   * for the current investigation status before changing BOOT.BIN or Vivado.
   *
   * Until resolved: rptun uses the spin-poll path in up_idle() (fallback in
   * zynqr5_rptun_ipi_check).  up_irq_enable() is still called here so
   * that the ARM CPSR.I bit is cleared — needed for WFI to be interruptible
   * by whatever signals the processor does receive. */

  /* Part 0 isolated probe (boot-xmpu-fix-plan.md "Part 0 - Rule out GIC
   * secure/non-secure banking"): write a single bit to GICD_ISENABLER[2]
   * and read it straight back, deliberately kept separate from GICD_CTLR
   * and from arm_gic0_initialize()/arm_gic_initialize() below (both of
   * which touch overlapping registers and would make it impossible to
   * tell which write caused which effect).  Bit 1 of register [2] is IRQ
   * 65 (65 / 32 = 2, 65 % 32 = 1) - the IPI SPI relevant to the
   * reassessment's contradiction.  This bit gets legitimately cleared by
   * arm_gic0_initialize()'s GIC_ICDICER(64) call right after this probe;
   * that's expected and does not invalidate the readback captured here. */
#ifdef CONFIG_ARMV7R_HAVE_GICv2
  {
    putreg32(0xBEEF0014u, 0x1ED3FFCCu);
    putreg32(0x00000002u, 0xF9000108u);          /* GICD_ISENABLER[2] |= bit 1 (IRQ 65) */
    __asm__ volatile("dsb sy" ::: "memory");
    uint32_t isenabler2 = getreg32(0xF9000108u); /* read back in isolation */
    putreg32(isenabler2, 0x1ED3FFB4u);           /* devmem 0x1ED3FFB4 32 */
    putreg32(0xBEEF0015u, 0x1ED3FFCCu);
  }

  arm_gic0_initialize();
  arm_gic_initialize();
  up_irq_enable();

  /* GIC post-init snapshot — written once, never zeroed by rptun_init.
   * Readable from A53: devmem 0x1ED3FFA0 32 (GICD_TYPER)
   *                    devmem 0x1ED3FFA4 32 (GICD_CTLR)
   *                    devmem 0x1ED3FFAC 32 (GICC_PMR) */
  putreg32(getreg32(0xF9000004u), 0x1ED3FFA0u);  /* GICD_TYPER  */
  putreg32(getreg32(0xF9000000u), 0x1ED3FFA4u);  /* GICD_CTLR   */
  putreg32(getreg32(0xF9001004u), 0x1ED3FFACu);  /* GICC_PMR    */
  __asm__ volatile("dsb sy" ::: "memory");
#endif

  putreg32(0xBEEF0012u, 0x1ED3FFCCu);
}

/****************************************************************************
 * Name: arm_ack_irq
 *
 * Description:
 *   arm_decodeirq() in arm_gicv2.c writes GICC_EOIR before calling the ISR,
 *   so end-of-interrupt is already handled.  Keep this as a no-op to avoid
 *   a double-EOI.
 *
 ****************************************************************************/

void arm_ack_irq(int irq)
{
  (void)irq;
}
