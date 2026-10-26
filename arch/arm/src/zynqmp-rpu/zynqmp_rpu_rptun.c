/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_rptun.c
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

/* RPMsg transport to Linux on the A53s.
 *
 * Linux (zynqmp_r5_remoteproc) is the remoteproc master.  It loads the
 * ELF, reads the resource table, sets up the vrings and releases the R5.
 * The two sides notify each other through the IPI block:
 *
 *   Linux -> R5: Linux triggers channel 1 (RPU0) from channel 7, which
 *                raises ZYNQMP_RPU_IRQ_IPI_CH1.  Clearing the ISR bit
 *                clears Linux's OBS bit, which its mailbox polls as the
 *                acknowledgement before it sends the next kick.
 *   R5 -> Linux: the R5 triggers channel 7 from channel 1.  Linux scans
 *                every vring on each kick, so no message is written to
 *                the IPI buffers and the vring id is not needed.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/wqueue.h>
#include <nuttx/rptun/rptun.h>

#include <arch/barriers.h>

#include "arm_internal.h"
#include "hardware/zynqmp_rpu_ipi.h"
#include "zynqmp_rpu_rsctable.h"
#include "zynqmp_rpu_rptun.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The R5 uses its own channel (RPU0) and talks to Linux's channel 7, as set
 * by the zynqmp_ipi1 node in the Linux device tree.
 */

#define RPTUN_IPI_BASE   ZYNQMP_RPU_IPI_CH1_BASE
#define RPTUN_IPI_LINUX  XIPIPSU_CH7_MASK
#define RPTUN_IPI_IRQ    ZYNQMP_RPU_IRQ_IPI_CH1

#define RPTUN_IPI_TRIG   (RPTUN_IPI_BASE + XIPIPSU_TRIG_OFFSET)
#define RPTUN_IPI_ISR    (RPTUN_IPI_BASE + XIPIPSU_ISR_OFFSET)
#define RPTUN_IPI_IER    (RPTUN_IPI_BASE + XIPIPSU_IER_OFFSET)
#define RPTUN_IPI_IDR    (RPTUN_IPI_BASE + XIPIPSU_IDR_OFFSET)

/* Name of the peer.  rptun registers /dev/rptun/<name>, and RPMsg services
 * such as uart_rpmsg_raw use it to pick the remote they talk to.
 */

#define RPTUN_CPUNAME    "linux"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct zynqmp_rpu_rptun_dev_s
{
  struct rptun_dev_s rptun;    /* Must be first */
  rptun_callback_t   callback; /* rptun's handler for incoming kicks */
  void              *arg;      /* Argument for callback */
  struct work_s      work;     /* Runs callback outside the interrupt */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static const char *zynqmp_rpu_rptun_get_cpuname(struct rptun_dev_s *dev);
static struct rptun_rsc_s *
zynqmp_rpu_rptun_get_resource(struct rptun_dev_s *dev);
static bool zynqmp_rpu_rptun_is_autostart(struct rptun_dev_s *dev);
static bool zynqmp_rpu_rptun_is_master(struct rptun_dev_s *dev);
static int zynqmp_rpu_rptun_notify(struct rptun_dev_s *dev, uint32_t vqid);
static int zynqmp_rpu_rptun_register_callback(struct rptun_dev_s *dev,
                                              rptun_callback_t callback,
                                              void *arg);
static void zynqmp_rpu_rptun_panic(struct rptun_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* get_firmware, get_addrenv, start and stop are left out: Linux loads the
 * firmware and controls the R5's power, and addresses are identity-mapped.
 */

static const struct rptun_ops_s g_zynqmp_rpu_rptun_ops =
{
  .get_cpuname       = zynqmp_rpu_rptun_get_cpuname,
  .get_resource      = zynqmp_rpu_rptun_get_resource,
  .is_autostart      = zynqmp_rpu_rptun_is_autostart,
  .is_master         = zynqmp_rpu_rptun_is_master,
  .notify            = zynqmp_rpu_rptun_notify,
  .register_callback = zynqmp_rpu_rptun_register_callback,
  .panic             = zynqmp_rpu_rptun_panic,
};

static struct zynqmp_rpu_rptun_dev_s g_zynqmp_rpu_rptun =
{
  .rptun =
  {
    .ops = &g_zynqmp_rpu_rptun_ops,
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static const char *zynqmp_rpu_rptun_get_cpuname(struct rptun_dev_s *dev)
{
  return RPTUN_CPUNAME;
}

static struct rptun_rsc_s *
zynqmp_rpu_rptun_get_resource(struct rptun_dev_s *dev)
{
  /* The table is const so that it stays in its own ELF section, but it is
   * in TCM, and Linux and rptun both write to it.
   */

  return (struct rptun_rsc_s *)&g_zynqmp_rpu_rsc_table;
}

static bool zynqmp_rpu_rptun_is_autostart(struct rptun_dev_s *dev)
{
  return true;
}

static bool zynqmp_rpu_rptun_is_master(struct rptun_dev_s *dev)
{
  return false;
}

static int zynqmp_rpu_rptun_notify(struct rptun_dev_s *dev, uint32_t vqid)
{
  /* Make the vring updates visible before Linux is told about them */

  UP_DSB();
  putreg32(RPTUN_IPI_LINUX, RPTUN_IPI_TRIG);
  return OK;
}

static int zynqmp_rpu_rptun_register_callback(struct rptun_dev_s *dev,
                                              rptun_callback_t callback,
                                              void *arg)
{
  struct zynqmp_rpu_rptun_dev_s *priv =
    (struct zynqmp_rpu_rptun_dev_s *)dev;

  if (callback != NULL)
    {
      priv->arg      = arg;
      priv->callback = callback;

      putreg32(RPTUN_IPI_LINUX, RPTUN_IPI_IER);
      up_enable_irq(RPTUN_IPI_IRQ);
    }
  else
    {
      up_disable_irq(RPTUN_IPI_IRQ);
      putreg32(RPTUN_IPI_LINUX, RPTUN_IPI_IDR);

      priv->callback = NULL;
      priv->arg      = NULL;
    }

  return OK;
}

/****************************************************************************
 * Name: zynqmp_rpu_rptun_panic
 *
 * Description:
 *   Without this op, rptun sends Linux a panic command on a NuttX panic and
 *   waits RPTUN_CMD_TIMEOUT_MS for an acknowledgement that Linux never
 *   sends.  Linux has no use for the command, so do nothing.
 *
 ****************************************************************************/

static void zynqmp_rpu_rptun_panic(struct rptun_dev_s *dev)
{
}

/****************************************************************************
 * Name: zynqmp_rpu_rptun_work
 *
 * Description:
 *   Pass a kick from Linux to rptun.  This runs on the high-priority work
 *   queue because the OpenAMP receive path takes mutexes.  RPTUN_NOTIFY_ALL
 *   scans every vring, so kicks that arrive before the work runs are all
 *   handled by one pass.
 *
 ****************************************************************************/

static void zynqmp_rpu_rptun_work(void *arg)
{
  struct zynqmp_rpu_rptun_dev_s *priv = arg;

  if (priv->callback != NULL)
    {
      priv->callback(priv->arg, RPTUN_NOTIFY_ALL);
    }
}

/****************************************************************************
 * Name: zynqmp_rpu_rptun_interrupt
 *
 * Description:
 *   IPI channel 1 interrupt.  Acknowledge every pending source and queue
 *   the work if Linux sent it.
 *
 ****************************************************************************/

static int zynqmp_rpu_rptun_interrupt(int irq, void *context, void *arg)
{
  struct zynqmp_rpu_rptun_dev_s *priv = arg;
  uint32_t isr;

  /* Writing the bits back clears them, which deasserts the (level-
   * sensitive) interrupt line and lets Linux send its next kick.
   */

  isr = getreg32(RPTUN_IPI_ISR);
  putreg32(isr, RPTUN_IPI_ISR);

  if ((isr & RPTUN_IPI_LINUX) != 0 && work_available(&priv->work))
    {
      work_queue(HPWORK, &priv->work, zynqmp_rpu_rptun_work, priv, 0);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zynqmp_rpu_rptun_initialize
 *
 * Description:
 *   Register the RPMsg link to Linux with rptun.  rptun_initialize()
 *   returns after it starts the rptun thread, which sets up the vrings,
 *   enables the IPI interrupt through register_callback and creates the
 *   RPMsg device once Linux has set the vdev status to DRIVER_OK.
 *
 ****************************************************************************/

int zynqmp_rpu_rptun_initialize(void)
{
  struct zynqmp_rpu_rptun_dev_s *priv = &g_zynqmp_rpu_rptun;
  int ret;

  /* Mask every source and drop anything left pending from a previous run
   * (Linux can stop and restart the R5 without a full reset of the IPI).
   */

  putreg32(XIPIPSU_ALL_MASK, RPTUN_IPI_IDR);
  putreg32(XIPIPSU_ALL_MASK, RPTUN_IPI_ISR);

  ret = irq_attach(RPTUN_IPI_IRQ, zynqmp_rpu_rptun_interrupt, priv);
  if (ret < 0)
    {
      return ret;
    }

  ret = rptun_initialize(&priv->rptun);
  if (ret < 0)
    {
      irq_detach(RPTUN_IPI_IRQ);
    }

  return ret;
}
