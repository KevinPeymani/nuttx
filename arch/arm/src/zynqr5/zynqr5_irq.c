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
 *     0xBEEF0012 = up_irqinitialize() completed (GIC init skipped — XMPU)
 *   GIC init (arm_gic0_initialize / arm_gic_initialize) is skipped until BOOT.BIN
 *   XMPU config is updated.  rptun uses spin-poll instead of GIC ISR delivery.
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  putreg32(0xBEEF0010u, 0x1ED3FFCCu);
  uint32_t iidr = getreg32(0xF9000008u);  /* GICD_IIDR — read-only probe */
  putreg32(0xBEEF0011u, 0x1ED3FFCCu);
  putreg32(iidr,        0x1ED3FFD0u);     /* store IIDR for inspection */

  /* XMPU blocks RPU0 writes to GICD (0xF9000000) — arm_gic0_initialize()
   * either stalls the AXI bus or corrupts GIC state, leading to an unhandled
   * IPI interrupt panic when up_irq_enable() unmasks IRQs before the IPI ISR
   * is registered in zynqr5_rptun_initialize().
   *
   * rptun uses the spin-poll path (zynqr5_rptun_ipi_check) instead of GIC SPI
   * delivery.  Re-enable once BOOT.BIN XMPU config grants RPU0 access to
   * 0xF9000000–0xF9001FFF and the IPI ISR is registered before up_irq_enable(). */
#ifdef CONFIG_ARMV7R_HAVE_GICv2
  /* arm_gic0_initialize();  — blocked by XMPU until BOOT.BIN is fixed */
  /* arm_gic_initialize();   — blocked by XMPU until BOOT.BIN is fixed */
  /* up_irq_enable();        — unsafe: premature IPI fires before ISR registered */
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
