/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_boot.c
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
#include <nuttx/init.h>
#include <stdint.h>
#include <arch/board/board.h>
#include "arm_internal.h"
#include "arm.h"

#include "zynqmp_rpu_boot.h"
#include "zynqmp_rpu_lowputc.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_boot
 *
 * Description:
 *   Complete boot operations started in arm_head.S
 *
 * Boot Sequence
 *
 *   1.  The __start entry point in armv7-r/arm_head.S is invoked upon power-
 *       on reset.
 *   2.  __start prepares CPU for code execution.
 *   3.  __start prepares memory resources by calling arm_data_initialize()
 *       and will then branch to this function.
 *   4.  This function will then branch to nx_start() to start the operating
 *       system.
 *
 ****************************************************************************/

void arm_boot(void)
{
  /* Initialize the FPU */

  arm_fpuconfig();
  zynqmp_rpu_lowsetup();

  /* TODO (milestone 2): configure MPU regions for TCM and device memory.
   * Until then, __start enables the MPU with the background region, so
   * privileged code uses the default memory map.
   */

  /* Early board setup */

  zynqmp_rpu_board_initialize();

  /* Then start NuttX */

  nx_start();
}

/****************************************************************************
 * Name: up_addrenv_pa_to_va / up_addrenv_va_to_pa
 *
 * The R5F has no MMU (only MPU), so physical == virtual.
 * OpenAMP / libmetal call these unconditionally; provide trivial stubs.
 ****************************************************************************/

#ifdef CONFIG_RPTUN
FAR void *up_addrenv_pa_to_va(uintptr_t pa)
{
  return (FAR void *)pa;
}

uintptr_t up_addrenv_va_to_pa(FAR void *va)
{
  return (uintptr_t)va;
}
#endif
