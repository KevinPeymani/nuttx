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
 *   Nothing in this function can report a failure: the console is not up
 *   until zynqr5_lowsetup() returns, and even then syslog is not running.
 *   The 0xA001xxxx markers written to 0x1EEFFFB0 are therefore the only
 *   evidence of how far early boot progressed, and are read from the A53 with
 *   devmem after a silent hang.  Scratchpad address map:
 *   boards/amd/kria-kv260/nuttx-config/scripts/script.ld.
 *
 ****************************************************************************/

void arm_boot(void)
{
  *((volatile uint32_t *)0x1EEFFFB0u) = 0xA0010001u;  /* reached arm_boot */

  /* Enable the FPU before anything else runs.  PX4 uses floating point
   * throughout, including in code that runs before nx_start() sets up
   * per-task FPU contexts; without this those accesses trap. */

  arm_fpuconfig();

  *((volatile uint32_t *)0x1EEFFFB0u) = 0xA0010002u;  /* FPU enabled */

  /* Bring up polled console output.  The FSBL and U-Boot have already
   * programmed the baud-rate divisors, so only the transmitter needs
   * enabling -- reprogramming the divisors here would risk corrupting a UART
   * that Linux is also using. */

  zynqr5_lowsetup();

  zynqr5_board_initialize();

  *((volatile uint32_t *)0x1EEFFFA8u) = 0xBB000001u;  /* entering nx_start */

  nx_start();
}
