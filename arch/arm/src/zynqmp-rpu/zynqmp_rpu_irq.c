/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_irq.c
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
#include <errno.h>
#include <assert.h>

#include <debug.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <arch/irq.h>
#include <sched.h>

#include "arm_internal.h"
#include "gic.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zynqmp_rpu_error_handler
 ****************************************************************************/

static void zynqmp_rpu_error_handler(void)
{
  PANIC();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 *
 * As the ZynqMP RPU has a PL390 (GICv1), up_irqinitialize essentially
 * calls the arm_gic_initialize() function.
 *
 ****************************************************************************/

#define ZYNQMP_RPU_VIM_FIRQPR3 (ZYNQMP_RPU_VIM_BASE+0x001c)
#define ZYNQMP_RPU_VIM_REQENASET3 (ZYNQMP_RPU_VIM_BASE+0x003c)
#define ZYNQMP_RPU_VIM_REQENACLR3 (ZYNQMP_RPU_VIM_BASE+0x004c)

void up_irqinitialize(void)
{
  /* The following operations need to be atomic, but since this function is
   * called early in the initialization sequence, we expect to have exclusive
   * access to the GIC.
   */

  /* Initialize the Generic Interrupt Controller (GIC) for CPU0.
   * In AMP mode, we want arm_gic0_initialize to be called only once.
   */

  if (sched_getcpu() == 0)
    {
      arm_gic0_initialize();  /* Initialization unique to CPU0 */
    }

  arm_gic_initialize();   /* Initialization common to all CPUs */

#ifndef CONFIG_SUPPRESS_INTERRUPTS
  /* And finally, enable interrupts */

  arm_color_intstack();
  up_irq_enable();
#endif
}
