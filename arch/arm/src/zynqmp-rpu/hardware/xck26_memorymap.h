/****************************************************************************
 * arch/arm/src/zynqmp-rpu/hardware/xck26_memorymap.h
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

#ifndef __ARCH_ARM_SRC_ZYNQMP_RPU_HARDWARE_XCK26_MEMORYMAP_H
#define __ARCH_ARM_SRC_ZYNQMP_RPU_HARDWARE_XCK26_MEMORYMAP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Memory Map Overview (TCM sizes assume lockstep mode) */

#define ZYNQMP_RPU_ATCM_BASE  0x00000000 /* 0x00000000-0x0001ffff: ATCM */
#define ZYNQMP_RPU_BTCM_BASE  0x00020000 /* 0x00020000-0x0003ffff: BTCM */
#define ZYNQMP_RPU_OCM_BASE   0xfffc0000 /* 0xfffc0000-0xffffffff: OCM */
#define ZYNQMP_RPU_DDR_BASE   0x00000000 /* DDR carveout TBD */

/* Private CPU Registers */

#define ZYNQMP_RPU_GICD_BASE  0xf9000000 /* 0xf9000000-0xf9000fff: GICD */
#define ZYNQMP_RPU_GICC_BASE  0xf9001000 /* 0xf9001000-0xf9001fff: GICC */

/* I/O LPD Peripheral Registers */

#define ZYNQMP_RPU_UART0_BASE 0xff000000 /* 0xff000000-0xff00ffff: UART0 */
#define ZYNQMP_RPU_UART1_BASE 0xff010000 /* 0xff010000-0xff01ffff: UART1 */
#define ZYNQMP_RPU_GPIO_BASE  0xff0a0000 /* 0xff0a0000-0xff0affff: GPIO */
#define ZYNQMP_RPU_TTC0_BASE  0xff110000 /* 0xff110000-0xff11ffff: TTC0 */
#define ZYNQMP_RPU_TTC1_BASE  0xff120000 /* 0xff120000-0xff12ffff: TTC1 */
#define ZYNQMP_RPU_TTC2_BASE  0xff130000 /* 0xff130000-0xff13ffff: TTC2 */
#define ZYNQMP_RPU_TTC3_BASE  0xff140000 /* 0xff140000-0xff14ffff: TTC3 */

#endif /* __ARCH_ARM_SRC_ZYNQMP_RPU_HARDWARE_XCK26_MEMORYMAP_H */
