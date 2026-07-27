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
 *   Bring up the RPU GIC (ARM PL390: GICD at 0xF9000000, GICC at
 *   0xF9001000) and unmask interrupts at the CPU.
 *
 *   Two things happen here that an ordinary GIC port does not need:
 *
 *   1. Stale *active* interrupt state is cleared before initialisation.
 *      The RPU GIC is not reset when Linux remoteproc stops and restarts
 *      this core, so an image killed between GICC_IAR and GICC_EOIR leaves
 *      its interrupt permanently active.  GICC_RPR then stays pinned at
 *      that interrupt's priority and the GIC refuses to signal anything at
 *      an equal or lower priority: the core boots and runs normally but
 *      never receives another interrupt.  The loop below EOIs every bit set
 *      in GICD_ISACTIVER (which drops RPR) and then clears the
 *      distributor's copy of the active state via GICD_ICACTIVER.
 *
 *   2. GIC register state is mirrored into the debug scratchpad.  Writes to
 *      parts of this aperture have been observed not to stick on this part
 *      (see the note at the isolated read-back probe below), and the RPU has
 *      no debugger attached in normal operation.  Mirroring the registers to
 *      DDR is the only way for the A53 to tell "the write was discarded"
 *      apart from "the write worked but the interrupt never arrived".
 *      Scratchpad address map:
 *      boards/amd/kria-kv260/nuttx-config/scripts/script.ld.
 *
 *   Progress markers are written to 0x1EEFFFCC so a hang inside this
 *   function can be localised from the A53:
 *     0xBEEF0010  about to read GICD_IIDR
 *     0xBEEF0011  GICD_IIDR read returned (value at 0x1EEFFFD0)
 *     0xBEEF0014  about to run the isolated GICD_ISENABLER read-back
 *     0xBEEF0015  read-back complete (result at 0x1EEFFFB4)
 *     0xBEEF0012  GIC init and up_irq_enable() complete
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  /* GICD_IIDR is read-only, so this read is safe whatever state a previous
   * firmware image left the GIC in, and it establishes that the 0xF9000000
   * aperture is reachable at all before any write is attempted. */

  putreg32(0xBEEF0010u, 0x1EEFFFCCu);
  uint32_t iidr = getreg32(0xF9000008u);  /* GICD_IIDR */
  putreg32(0xBEEF0011u, 0x1EEFFFCCu);
  putreg32(iidr,        0x1EEFFFD0u);     /* mirror for A53 inspection */

  /* Clear interrupts left active by a previous firmware image (see item 1
   * of the function comment).  ISACTIVER[0..4] covers IRQs 0-159, which
   * spans the whole 148-interrupt RPU GIC.  EOI must be written per set bit
   * because each EOI pops one entry off the CPU interface's active priority
   * stack, whereas the ICACTIVER write clears the whole word at once. */
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

  /* Mirror GICC_RPR so the clear above can be confirmed: a healthy idle CPU
   * interface reads 0xFF (no priority active).  Any other value means an
   * interrupt is still active and interrupt delivery will remain wedged. */

  putreg32(getreg32(0xF9001014u), 0x1EEFFFA8u);  /* GICC_RPR */
  __asm__ volatile("dsb sy" ::: "memory");

  /* Write one bit to GICD_ISENABLER[2] and read it straight back.
   *
   * This exists because GICD_CTLR, GICD_ISENABLER and GICC_CTLR have all
   * been observed reading back as 0 after arm_gic*_initialize() on this
   * part: the writes do not fault, they are simply discarded.  The root
   * cause is not established.  It is *not* the LPD_XPPU or FPD_XMPU --
   * this board's Vivado design programs neither (psu_lpd_xppu_data() and
   * psu_fpd_xmpu_data() in psu_init_gpl.c are empty stubs) and the XPPU
   * aperture list has no entry covering 0xF9000000.  The leading candidate
   * is GIC secure/non-secure register banking, where a Secure-gated field
   * written from a Non-secure context is architecturally RAZ/WI with no
   * protection unit involved.
   *
   * The read-back is kept separate from GICD_CTLR and from the
   * arm_gic*_initialize() calls below because those touch overlapping
   * registers; interleaving them makes it impossible to attribute a failed
   * read-back to one specific register.  Bit 1 of word [2] is IRQ 65
   * (65 / 32 = 2, 65 % 32 = 1), the IPI SPI, chosen because rptun's
   * behaviour suggests that particular enable does take effect.
   *
   * arm_gic0_initialize() clears this bit again via its GIC_ICDICER(64)
   * write immediately afterwards.  That is expected -- the value captured
   * at 0x1EEFFFB4 is sampled before that happens and stays valid.
   */

#ifdef CONFIG_ARMV7R_HAVE_GICv2
  {
    putreg32(0xBEEF0014u, 0x1EEFFFCCu);
    putreg32(0x00000002u, 0xF9000108u);          /* GICD_ISENABLER[2] bit 1 */
    __asm__ volatile("dsb sy" ::: "memory");
    uint32_t isenabler2 = getreg32(0xF9000108u);
    putreg32(isenabler2, 0x1EEFFFB4u);           /* 2 = stuck, 0 = discarded */
    putreg32(0xBEEF0015u, 0x1EEFFFCCu);
  }

  /* up_irq_enable() is called even though the GIC may be ignoring the enable
   * writes: it also clears CPSR.I, without which WFI in up_idle() could never
   * be woken by any interrupt the core does manage to receive.  While the
   * discarded-write behaviour stands, rptun does not depend on GIC delivery
   * at all -- zynqr5_rptun_ipi_check() polls the IPI from up_idle() instead. */

  arm_gic0_initialize();
  arm_gic_initialize();
  up_irq_enable();

  /* Post-init mirror of the registers whose writes are suspect.  GICD_CTLR
   * reading 0 here is the primary symptom described above; GICD_TYPER (which
   * is read-only, so always trustworthy) and GICC_PMR bound the diagnosis. */

  putreg32(getreg32(0xF9000004u), 0x1EEFFFA0u);  /* GICD_TYPER */
  putreg32(getreg32(0xF9000000u), 0x1EEFFFA4u);  /* GICD_CTLR  */
  putreg32(getreg32(0xF9001004u), 0x1EEFFFACu);  /* GICC_PMR   */
  __asm__ volatile("dsb sy" ::: "memory");
#endif

  putreg32(0xBEEF0012u, 0x1EEFFFCCu);
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
