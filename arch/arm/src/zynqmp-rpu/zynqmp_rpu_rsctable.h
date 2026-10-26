/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_rsctable.h
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

#ifndef __ARCH_ARM_SRC_ZYNQMP_RPU_ZYNQMP_RPU_RSCTABLE_H
#define __ARCH_ARM_SRC_ZYNQMP_RPU_ZYNQMP_RPU_RSCTABLE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/rptun/rptun.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Shared memory window used by RPMsg.  It must match the reserved-memory
 * nodes (vdev0vring0, vdev0vring1, vdev0buffer) in the Linux device tree.
 */

#define ZYNQMP_RPU_SHM_BASE        0x3e000000
#define ZYNQMP_RPU_SHM_SIZE        0x00100000

#define ZYNQMP_RPU_VRING0_BASE     (ZYNQMP_RPU_SHM_BASE + 0x00000)
#define ZYNQMP_RPU_VRING1_BASE     (ZYNQMP_RPU_SHM_BASE + 0x04000)
#define ZYNQMP_RPU_VDEV_BUF_BASE   (ZYNQMP_RPU_SHM_BASE + 0x80000)

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Placed in the .resource_table ELF section, where Linux remoteproc reads
 * it when loading the firmware and writes back the vdev status.
 */

extern const struct rptun_rsc_s g_zynqmp_rpu_rsc_table;

#endif /* __ARCH_ARM_SRC_ZYNQMP_RPU_ZYNQMP_RPU_RSCTABLE_H */
