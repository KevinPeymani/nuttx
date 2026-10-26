/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_rsctable.c
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

#include <stddef.h>

#include <nuttx/rptun/rptun.h>

#include "zynqmp_rpu_rsctable.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define RSC_TABLE_VERSION       1
#define NUM_RESOURCE_ENTRIES    1   /* Only the RPMsg vdev */

#define NUM_VRINGS              2
#define VRING_ALIGN             0x1000
#define VRING_NUM_BUFS          256 /* Per vring, must be a power of 2 */

/* Name service lets Linux create the rpmsg-tty channel when NuttX
 * announces it.  VIRTIO_RPMSG_F_CPUNAME is a NuttX-only extension that
 * Linux does not acknowledge, so it is left out.
 */

#define RSC_VDEV_FEATURES       (1 << VIRTIO_RPMSG_F_NS)

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Linux remoteproc finds this table by the name of its ELF section.  The
 * vring addresses are fixed rather than FW_RSC_ADDR_ANY, so Linux uses the
 * reserved-memory regions at those exact addresses.  Linux writes the
 * negotiated features and the vdev status into the table before it
 * releases the R5 from reset.
 */

__attribute__((section(".resource_table")))
const struct rptun_rsc_s g_zynqmp_rpu_rsc_table =
{
  .rsc_tbl_hdr =
  {
    .ver = RSC_TABLE_VERSION,
    .num = NUM_RESOURCE_ENTRIES,
  },

  .offset =
  {
    offsetof(struct rptun_rsc_s, rpmsg_vdev),
  },

  /* Not listed in offset[], so Linux ignores it */

  .log_trace =
  {
    .type = RSC_TRACE,
  },

  .rpmsg_vdev =
  {
    .type          = RSC_VDEV,
    .id            = VIRTIO_ID_RPMSG,
    .notifyid      = 0,
    .dfeatures     = RSC_VDEV_FEATURES,
    .gfeatures     = 0,   /* Written by Linux */
    .config_len    = 0,
    .status        = 0,   /* Written by Linux */
    .num_of_vrings = NUM_VRINGS,
  },

  .rpmsg_vring0 =
  {
    .da       = ZYNQMP_RPU_VRING0_BASE,
    .align    = VRING_ALIGN,
    .num      = VRING_NUM_BUFS,
    .notifyid = 0,
  },

  .rpmsg_vring1 =
  {
    .da       = ZYNQMP_RPU_VRING1_BASE,
    .align    = VRING_ALIGN,
    .num      = VRING_NUM_BUFS,
    .notifyid = 1,
  },
};
