/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_rsc.c
 *
 * Virtio resource table for the Kria KV260 R5F.
 *
 * Linux remoteproc reads this structure from the ELF ".resource_table"
 * section to locate the two vrings used by the rpmsg virtio bus.
 *
 * Vring addresses must match the Linux DT reserved-memory nodes:
 *   0x1ED40000  rpu0vdev0vring0  16 KB  R5F → A53 TX
 *   0x1ED44000  rpu0vdev0vring1  16 KB  A53 → R5F RX
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
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>

#include <nuttx/rptun/rptun.h>
#include <openamp/remoteproc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Must match Linux DT reserved-memory nodes rpu0vdev0vring0/1 */

#define VRING0_DA    0x1ED40000u  /* R5F→A53 TX; rpu0vdev0vring0@1ed40000 */
#define VRING1_DA    0x1ED44000u  /* A53→R5F RX; rpu0vdev0vring1@1ed44000 */
#define VRING_ALIGN  0x1000u
#define VRING_SIZE   16u

/****************************************************************************
 * Public Data
 ****************************************************************************/

struct rptun_rsc_s g_zynqr5_rsc
    __attribute__((section(".resource_table"), aligned(8))) =
{
  .rsc_tbl_hdr =
  {
    .ver      = 1,
    .num      = 1,       /* one entry: rpmsg_vdev; log_trace is skipped */
    .reserved = {0, 0},
  },
  .offset =
  {
    offsetof(struct rptun_rsc_s, rpmsg_vdev),
    0,
  },

  /* log_trace — zeroed; Linux skips it because num = 1 */

  .rpmsg_vdev =
  {
    .type          = RSC_VDEV,
    .id            = VIRTIO_ID_RPMSG,
    .notifyid      = 0,
    .dfeatures     = (1u << VIRTIO_RPMSG_F_NS),
    .gfeatures     = 0,
    .config_len    = 0,
    .status        = 0,
    .num_of_vrings = 2,
    .reserved      = {0, 0},
  },

  .rpmsg_vring0 =         /* R5F→A53 TX */
  {
    .da       = VRING0_DA,
    .align    = VRING_ALIGN,
    .num      = VRING_SIZE,
    .notifyid = 1,
    .reserved = 0,
  },

  .rpmsg_vring1 =         /* A53→R5F RX */
  {
    .da       = VRING1_DA,
    .align    = VRING_ALIGN,
    .num      = VRING_SIZE,
    .notifyid = 2,
    .reserved = 0,
  },

  /* config — zeroed; NuttX rptun uses its own defaults */
};
