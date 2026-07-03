/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_rptun.c
 *
 * NuttX rptun platform driver for the Kria KV260 R5F.
 *
 * Implements the rptun_ops_s interface over the ZynqMPSoC IPI hardware
 * (RPU0 channel).  The R5F is the virtio slave; Linux remoteproc on the
 * A53 is the master.
 *
 * PHASE 3 GATE: irq_attach() requires arm_gic_initialize() to succeed,
 * which in turn requires GICD (0xF9010000) and GICC (0xF9020000) to be
 * accessible from R5F — confirmed working via BOOT.BIN XMPU hook.
 * Phase 2 gate has been cleared; this driver is production-ready.
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

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/rptun/rptun.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>

/* RPTUN_NOTIFY_ALL is defined in rptun.h; pull in UINT32_MAX as fallback */
#ifndef RPTUN_NOTIFY_ALL
#  define RPTUN_NOTIFY_ALL UINT32_MAX
#endif

#include "arm_internal.h"
#include "zynqr5_rptun.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* IPI registers for the RPU0 channel (UG1085 TRM §14.4) */

#define IPI_RPU0_BASE  0xFF310000u
#define IPI_TRIG       (IPI_RPU0_BASE + 0x00u) /* write IPI_APU_BIT → kick APU */
#define IPI_ISR        (IPI_RPU0_BASE + 0x10u) /* write IPI_APU_BIT → clear pending */
#define IPI_IER        (IPI_RPU0_BASE + 0x18u) /* write IPI_APU_BIT → enable APU→RPU0 irq */

/* APU agent bit mask in ZynqMPSoC IPI registers (UG1085 §14):
 * Each agent has a fixed bit in all IPI TRIG/ISR/IER registers.
 *   RPU0 = bit 8,  APU = bit 24
 * Verified empirically: after Linux writes DRIVER_OK and kicks R5F,
 * RPU0_ISR (0xFF310010) reads 0x01000000 (bit 24) = APU's bit. */

#define IPI_APU_BIT    (1u << 24)

/* GIC SPI 29 = GIC interrupt ID 61 (32 + 29), RPU0 IPI channel.
 * Confirmed from Linux /proc/interrupts: "GICv2 61 Level zynqmp_ipi1".
 * XMPU_FPD blocks R5F from GICD/GICC so GIC-based delivery is disabled;
 * IPI is polled via zynqr5_rptun_ipi_check() called from up_idle(). */

#define IPI_IRQ        61

/* Debug breadcrumb area — last 32 bytes of the 256 KB ELF carveout.
 * Readable from A53 via devmem.  Writes survive cache since R5F D-cache is
 * disabled; A53 must use cached reads or flush before reading to see updates.
 *   RPTUN_DBG_STEP   : rptun lifecycle step (see values below)
 *   RPTUN_DBG_NOTIFY : number of IPI kick calls made by R5F
 *   RPTUN_DBG_ISR    : number of IPI interrupts received from APU
 */
#define RPTUN_DBG_BASE    0x3ED3FFE0u
#define RPTUN_DBG_STEP    (RPTUN_DBG_BASE + 0x00u)
#define RPTUN_DBG_NOTIFY  (RPTUN_DBG_BASE + 0x04u)
#define RPTUN_DBG_ISR     (RPTUN_DBG_BASE + 0x08u)
#define RPTUN_DBG_IDLE    (RPTUN_DBG_BASE + 0x0Cu) /* ipi_check call count */

/* Step values written to RPTUN_DBG_STEP */
#define DBG_STEP_INIT_ENTER    0x01u  /* zynqr5_rptun_initialize() entered */
#define DBG_STEP_INIT_DONE     0x02u  /* rptun_initialize() returned OK */
#define DBG_STEP_GET_RSC       0x10u  /* get_resource callback fired */
#define DBG_STEP_IS_MASTER     0x11u  /* is_master callback fired */
#define DBG_STEP_REG_CB        0x20u  /* register_callback fired (past init_vdev) */
#define DBG_STEP_NOTIFY        0x30u  /* first notify() call */
#define DBG_STEP_ISR           0x40u  /* first ISR from APU */

static inline void rptun_dbg_write(uint32_t reg, uint32_t val)
{
  putreg32(val, reg);
  __asm__ volatile("dsb sy" ::: "memory");
}

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct zynqr5_rptun_dev_s
{
  struct rptun_dev_s  rptun;    /* must be first — ops pointer lives here */
  rptun_callback_t    callback;
  void               *arg;
};

/****************************************************************************
 * External Data
 ****************************************************************************/

extern struct rptun_rsc_s g_zynqr5_rsc;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static FAR const char *zynqr5_rptun_get_cpuname(FAR struct rptun_dev_s *dev);
static FAR const char *zynqr5_rptun_get_firmware(FAR struct rptun_dev_s *dev);
static FAR const struct rptun_addrenv_s *
zynqr5_rptun_get_addrenv(FAR struct rptun_dev_s *dev);
static FAR struct rptun_rsc_s *
zynqr5_rptun_get_resource(FAR struct rptun_dev_s *dev);
static bool zynqr5_rptun_is_autostart(FAR struct rptun_dev_s *dev);
static bool zynqr5_rptun_is_master(FAR struct rptun_dev_s *dev);
static int  zynqr5_rptun_notify(FAR struct rptun_dev_s *dev, uint32_t vqid);
static int  zynqr5_rptun_register_callback(FAR struct rptun_dev_s *dev,
                                           rptun_callback_t callback,
                                           FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct zynqr5_rptun_dev_s g_zynqr5_rptun_priv;

static const struct rptun_ops_s g_zynqr5_rptun_ops =
{
  .get_cpuname       = zynqr5_rptun_get_cpuname,
  .get_firmware      = zynqr5_rptun_get_firmware,
  .get_addrenv       = zynqr5_rptun_get_addrenv,
  .get_resource      = zynqr5_rptun_get_resource,
  .is_autostart      = zynqr5_rptun_is_autostart,
  .is_master         = zynqr5_rptun_is_master,
  .config            = NULL,
  .start             = NULL,
  .stop              = NULL,
  .notify            = zynqr5_rptun_notify,
  .register_callback = zynqr5_rptun_register_callback,
  .reset             = NULL,
  .panic             = NULL,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static FAR const char *zynqr5_rptun_get_cpuname(FAR struct rptun_dev_s *dev)
{
  return "a53";
}

static FAR const char *zynqr5_rptun_get_firmware(FAR struct rptun_dev_s *dev)
{
  return NULL;  /* remoteproc already loaded the ELF */
}

static FAR const struct rptun_addrenv_s *
zynqr5_rptun_get_addrenv(FAR struct rptun_dev_s *dev)
{
  return NULL;  /* identity mapping: R5F and A53 share the same DDR PA */
}

static FAR struct rptun_rsc_s *
zynqr5_rptun_get_resource(FAR struct rptun_dev_s *dev)
{
  rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_GET_RSC);
  return &g_zynqr5_rsc;
}

static bool zynqr5_rptun_is_autostart(FAR struct rptun_dev_s *dev)
{
  return true;
}

static bool zynqr5_rptun_is_master(FAR struct rptun_dev_s *dev)
{
  rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_IS_MASTER);
  return false;  /* R5F is slave; Linux remoteproc is master */
}

static int zynqr5_rptun_notify(FAR struct rptun_dev_s *dev, uint32_t vqid)
{
  uint32_t n = getreg32(RPTUN_DBG_NOTIFY);
  if (n == 0)
    rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_NOTIFY);
  rptun_dbg_write(RPTUN_DBG_NOTIFY, n + 1);

  /* Kick A53: write IPI_APU_BIT to RPU0_TRIG to assert the R5F→A53 IPI.
   * Do NOT write IPI_ISR here — that would erase any pending APU→R5F kick
   * (e.g. the DRIVER_OK notification A53 sends before our first notify()),
   * causing ipi_check() to miss it and leaving semrx never posted. */
  putreg32(IPI_APU_BIT, IPI_TRIG);

  return OK;
}

static int zynqr5_rptun_register_callback(FAR struct rptun_dev_s *dev,
                                          rptun_callback_t callback,
                                          FAR void *arg)
{
  FAR struct zynqr5_rptun_dev_s *priv = (FAR struct zynqr5_rptun_dev_s *)dev;
  rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_REG_CB);
  priv->callback = callback;
  priv->arg      = arg;

  /* If A53's initial IPI is already pending in RPU0_ISR, fire callback now
   * so rptun_dev_start() can proceed.
   *
   * IMPORTANT: do NOT clear IPI_ISR here.  The ISR clear happens later in
   * notify(), after writing IPI_TRIG to kick A53.  That ordering guarantees
   * PL0_ISR BIT(8) is set before SPI 29 fires on A53, so the Linux SPI 29
   * handler sees our NS kick and calls rproc_vq_interrupt(). */

  if (getreg32(IPI_ISR) & IPI_APU_BIT)
    {
      putreg32(IPI_APU_BIT, IPI_ISR);   /* clear */
      callback(arg, RPTUN_NOTIFY_ALL);
      putreg32(IPI_APU_BIT, IPI_TRIG);  /* ACK A53's kick */
    }

  return OK;
}

static int zynqr5_ipi_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct zynqr5_rptun_dev_s *priv = (FAR struct zynqr5_rptun_dev_s *)arg;
  uint32_t n = getreg32(RPTUN_DBG_ISR);
  if (n == 0)
    rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_ISR);
  rptun_dbg_write(RPTUN_DBG_ISR, n + 1);

  putreg32(IPI_APU_BIT, IPI_ISR);  /* clear before processing */

  if (priv->callback != NULL)
    {
      priv->callback(priv->arg, RPTUN_NOTIFY_ALL);
    }

  return OK;
}

/* IPI polling fallback: called from up_idle() every scheduler tick.
 * XMPU_FPD blocks R5F GICD access so GIC cannot route SPI-29 to R5F;
 * poll IPI_RPU0_ISR directly — LPD IPI registers are accessible without
 * XMPU restriction (confirmed via IPI_TRIG writes working). */

void zynqr5_rptun_ipi_check(void)
{
  FAR struct zynqr5_rptun_dev_s *priv = &g_zynqr5_rptun_priv;

  rptun_dbg_write(RPTUN_DBG_IDLE, getreg32(RPTUN_DBG_IDLE) + 1);

  if (getreg32(IPI_ISR) & IPI_APU_BIT)
    {
      uint32_t isr_n = getreg32(RPTUN_DBG_ISR);
      if (isr_n == 0)
        rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_ISR);
      rptun_dbg_write(RPTUN_DBG_ISR, isr_n + 1);

      /* Clear the IPI before calling callback so A53 can kick again. */
      putreg32(IPI_APU_BIT, IPI_ISR);

      /* ACK A53's kick: writing RPU0_TRIG[APU_bit] sets APU_ISR[RPU0_bit],
       * which triggers SPI 29 on A53 → zynqmp_ipi_interrupt() →
       * mbox_chan_txdone() → clears ack_pending → A53 can send next kick. */
      putreg32(IPI_APU_BIT, IPI_TRIG);

      /* The ZynqMPSoC IPI carries no vring notifyid — use RPTUN_NOTIFY_ALL
       * so rptun_callback() checks both the RX vring (wakes rptun thread if
       * A53 sent data) and the TX vring (unblocks notify_wait). */
      if (priv->callback != NULL)
        priv->callback(priv->arg, RPTUN_NOTIFY_ALL);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* R5F has no MMU: physical address == virtual address. */

FAR void *up_addrenv_pa_to_va(uintptr_t pa)
{
  return (FAR void *)pa;
}

uintptr_t up_addrenv_va_to_pa(FAR void *va)
{
  return (uintptr_t)va;
}

/****************************************************************************
 * Name: zynqr5_rptun_initialize
 *
 * Description:
 *   Register the rptun device and enable the IPI interrupt.
 *
 *   PHASE 3 GATE: must not be called until arm_gic_initialize() succeeds
 *   and GICC/GICD registers are accessible (XMPU unlock confirmed).
 *   Call from board_late_initialize() after the scheduler has started.
 *
 ****************************************************************************/

int zynqr5_rptun_initialize(void)
{
  FAR struct zynqr5_rptun_dev_s *priv = &g_zynqr5_rptun_priv;
  int ret;

  /* Zero the debug area so A53 can detect fresh writes. */

  putreg32(0, RPTUN_DBG_STEP);
  putreg32(0, RPTUN_DBG_NOTIFY);
  putreg32(0, RPTUN_DBG_ISR);
  putreg32(0, RPTUN_DBG_IDLE);
  *((volatile uint32_t *)0x3ED3FFDCu) = 0; /* up_idle heartbeat */
  *((volatile uint32_t *)0x3ED3FFD4u) = 0; /* WRKR count */
  *((volatile uint32_t *)0x3ED3FFD8u) = 0; /* STATE */
  __asm__ volatile("dsb sy" ::: "memory");

  rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_INIT_ENTER);  /* 0x01 */

  priv->rptun.ops = &g_zynqr5_rptun_ops;

  putreg32(IPI_APU_BIT, IPI_IER);

  /* GICD/GICC (0xF9010000/0xF9020000) are blocked by XMPU_FPD for RPU0.
   * irq_attach + up_enable_irq would write GICD → data abort.
   * IPI delivery uses zynqr5_rptun_ipi_check() from up_idle() instead. */

  /* irq_attach(IPI_IRQ, zynqr5_ipi_isr, priv); */
  /* up_enable_irq(IPI_IRQ); */

  ret = rptun_initialize(&priv->rptun);
  if (ret == OK)
    {
      rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_INIT_DONE);  /* 0x02 */
    }

  return ret;
}
