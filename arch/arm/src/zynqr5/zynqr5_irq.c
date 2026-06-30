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
 *   Initialize the GIC distributor (arm_gic0_initialize) and the per-CPU
 *   CPU interface (arm_gic_initialize).  GICD (0xF9010000) and GICC
 *   (0xF9020000) must be accessible from R5F — ensured by the XMPU_FPD
 *   configuration in the custom FSBL (BOOT.BIN).
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  /* XMPU blocks RPU0 access to GICD (0xF9010000) / GICC (0xF9020000).
   * Calling arm_gic0_initialize() causes a data abort and halts R5F.
   *
   * TODO: fix the Vitis BOOT.BIN XMPU config to allow RPU0 read/write of
   * GICD and GICC, then re-enable arm_gic0_initialize() + arm_gic_initialize()
   * so IPI IRQ 97 can be delivered to R5F for incoming rpmsg messages.
   *
   * Until then: TTC tick runs polled; outgoing IPI writes (IPI_TRIG) work
   * without GIC; the first rptun_dev_start() (autostart) runs and sends
   * the NS announcement without needing an incoming IRQ from APU. */

#ifdef CONFIG_ARMV7R_HAVE_GICv2
  /* arm_gic0_initialize();  — blocked by XMPU until BOOT.BIN is fixed */
  /* arm_gic_initialize();   — blocked by XMPU until BOOT.BIN is fixed */
#endif
}

/****************************************************************************
 * Name: arm_ack_irq
 *
 * Description:
 *   Polled TTC timer — no GIC interrupt acknowledgement needed.
 *   up_enable_irq / up_disable_irq are provided by arm_gicv2.c (always
 *   compiled for armv7-r) and are safe no-ops when the GIC is uninitialised.
 *
 ****************************************************************************/

void arm_ack_irq(int irq)
{
  (void)irq;
}
