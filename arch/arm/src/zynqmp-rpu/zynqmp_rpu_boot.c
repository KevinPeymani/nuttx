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
#include <sys/param.h>
#include <arch/board/board.h>
#include "arm_internal.h"
#include "arm.h"

#include "zynqmp_rpu_boot.h"
#include "zynqmp_rpu_lowputc.h"
#include "zynqmp_rpu_timebase.h"

/* MPU regions are needed for code and data in DDR and for the RPMsg
 * shared memory.
 */

#if defined(CONFIG_ARM_MPU) && \
    (defined(CONFIG_BOOT_RUNFROMSDRAM) || defined(CONFIG_RPTUN))
#  define ZYNQMP_RPU_MPU_REGIONS 1
#  include "mpu.h"
#endif

#if defined(ZYNQMP_RPU_MPU_REGIONS) && defined(CONFIG_RPTUN)
#  include "zynqmp_rpu_rsctable.h"
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef ZYNQMP_RPU_MPU_REGIONS
/* Regions are numbered in table order and a higher number wins where two
 * regions overlap, so the more specific regions come last.
 */

static const struct mpu_region_s g_zynqmp_rpu_mpu_regions[] =
{
#ifdef CONFIG_BOOT_RUNFROMSDRAM
  /* The DDR carve-out that holds the code, data, stacks and heap: Normal
   * memory, write-back write-allocate (TEX=001, C=1, B=1), executable.
   * Non-shareable, for the same LDREX/STREX reason as below.  Only this
   * core uses it, so caching it needs no maintenance.
   */

  {
    CONFIG_RAM_START, CONFIG_RAM_SIZE,
    MPU_RACR_TEX(1) | MPU_RACR_C | MPU_RACR_B | MPU_RACR_AP_RWRW
  },
#endif

#ifdef CONFIG_RPTUN
  /* The R5 is not cache-coherent with the A53s, so the RPMsg vrings and
   * buffers shared with Linux must not be cached.  Normal memory (TEX=001,
   * C=0, B=0) rather than Device, so memcpy and unaligned accesses work.
   * Non-shareable, because LDREX/STREX to shareable memory can abort on
   * the R5.  The resource table is in TCM, which is never cached.
   */

  {
    ZYNQMP_RPU_SHM_BASE, ZYNQMP_RPU_SHM_SIZE,
    MPU_RACR_TEX(1) | MPU_RACR_AP_RWRW | MPU_RACR_XN
  },
#endif
};
#endif

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

  /* Start the timebase before anything can call up_udelay() */

  zynqmp_rpu_timebase_init();

  /* __start enables the MPU with the background region, so privileged code
   * uses the default memory map.  Only DDR code and data and the RPMsg
   * shared memory get their own regions; mpu_initialize() keeps the
   * background region enabled.
   *
   * TODO (milestone 2): configure MPU regions for TCM and device memory.
   */

#ifdef ZYNQMP_RPU_MPU_REGIONS
  mpu_initialize(g_zynqmp_rpu_mpu_regions,
                 nitems(g_zynqmp_rpu_mpu_regions));
#endif

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
