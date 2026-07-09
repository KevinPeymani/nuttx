/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_start.c
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

#include <nuttx/init.h>
#include <arch/board/board.h>

#include "arm_internal.h"
#include "zynqr5_start.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_boot
 *
 * Description:
 *   Entry point called from arm_head.S after CPU / stack setup.
 *
 *   Boot sequence (CONFIG_ARMV7R_MEMINIT is not set):
 *     arm_head.S → arm_data_initialize() → arm_boot() → nx_start()
 *
 *   arm_head.S already called arm_data_initialize(), so .bss/.data are
 *   valid here.
 *
 ****************************************************************************/

void arm_boot(void)
{
  /* Breadcrumb: arm_boot() entered (after arm_head.S + arm_data_initialize) */
  *((volatile uint32_t *)0x1ED3FFB0u) = 0xA0010001u;

  /* Enable the FPU so floating-point operations can be used before nx_start
   * configures the FPU context for each task.
   */

  arm_fpuconfig();

  /* Breadcrumb: arm_fpuconfig() survived */
  *((volatile uint32_t *)0x1ED3FFB0u) = 0xA0010002u;

  /* Configure the early debug UART console (polls TX FIFO, no interrupt).
   * The FSBL/U-Boot has already set the baud-rate divisors; we just ensure
   * TX is enabled.
   */

  zynqr5_lowsetup();

  /* Board-specific early initialisation (stub in Phase 1). */

  zynqr5_board_initialize();

  *((volatile uint32_t *)0x1ED3FFA8u) = 0xBB000001u;  /* arm_boot: before nx_start */

  /* Hand control to the NuttX kernel. */

  nx_start();
}
