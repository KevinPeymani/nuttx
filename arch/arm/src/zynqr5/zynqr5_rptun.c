/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_rptun.c
 *
 * NuttX rptun platform driver for the Kria KV260 R5F.
 *
 * Implements the rptun_ops_s interface over the ZynqMPSoC IPI hardware
 * (RPU0 channel).  The R5F is the virtio slave; Linux remoteproc on the
 * A53 is the master.
 *
 * IPI interrupt (RPU GIC SPI 33 = IRQ 65) delivered via RPU GIC (PL390 GICv1).
 * GIC init in up_irqinitialize() must succeed before this driver is used.
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
#include "arch/zynqr5/irq.h"
#include "zynqr5_rptun.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* IPI registers — RPU0 channel (UG1085 TRM §14.4) */

#define IPI_RPU0_BASE  0xFF310000u
#define IPI_TRIG       (IPI_RPU0_BASE + 0x00u) /* write IPI_APU_BIT → kick zynqmp_ipi1 */
#define IPI_ISR        (IPI_RPU0_BASE + 0x10u) /* write IPI_APU_BIT → clear pending */
#define IPI_IER        (IPI_RPU0_BASE + 0x18u) /* write IPI_APU_BIT → enable RX IRQ */
#define IPI_IMR        (IPI_RPU0_BASE + 0x20u) /* read: bit set = masked, 0 = enabled */

/* zynqmp_ipi1 bit mask in ZynqMPSoC IPI registers (UG1085 §14):
 *   zynqmp_ipi1 (xlnx,ipi-id=7, GIC SPI 29 = IRQ 61): bit 24 (0x01000000)
 *   RPU0 bit: bit 8 (0x00000100)
 *
 * Receive path: A53 (via zynqmp_ipi1 TF-A SMC) kicks R5F →
 *   RPU0_ISR bit 24 set → GIC SPI 29 fires on R5F → zynqr5_ipi_isr().
 * Kick path: R5F notifies A53 → write IPI_APU_BIT (bit 24) to RPU0_TRIG →
 *   zynqmp_ipi1 ISR bit 8 set → A53 GIC SPI 29 fires → rproc_vq_interrupt().
 * Empirically confirmed: devmem write of 0x01000000 to 0xFF310000 increments
 *   /proc/interrupts IRQ 15 (zynqmp_ipi1). */

#define IPI_APU_BIT    (1u << 24)  /* zynqmp_ipi1 agent bit — kick AND receive */

/* RPU GIC SPI 33 = GIC interrupt ID 65 (32 + 33), RPU0 IPI channel.
 * Source: echo-vitis xparameters XPAR_XIPIPSU_0_INTR=0x41=65, base 0xFF310000.
 * (A53's APU GIC uses SPI 29 = ID 61 for the same IPI, different GIC instance.) */

#define IPI_IRQ        ZYNQR5_IRQ_IPI1

/* Debug breadcrumb area — last 32 bytes of the 256 KB ELF carveout.
 * Readable from A53 via devmem.  Writes survive cache since R5F D-cache is
 * disabled; A53 must use cached reads or flush before reading to see updates.
 *   RPTUN_DBG_STEP   : rptun lifecycle step (see values below)
 *   RPTUN_DBG_NOTIFY : number of IPI kick calls made by R5F
 *   RPTUN_DBG_ISR    : number of IPI interrupts received from APU
 */
#define RPTUN_DBG_BASE    0x1ED3FFE0u
#define RPTUN_DBG_STEP    (RPTUN_DBG_BASE + 0x00u)
#define RPTUN_DBG_NOTIFY  (RPTUN_DBG_BASE + 0x04u)
#define RPTUN_DBG_ISR     (RPTUN_DBG_BASE + 0x08u)
#define RPTUN_DBG_IDLE    (RPTUN_DBG_BASE + 0x0Cu) /* ipi_check call count */
/* 0x1ED3FFD0: rptun_dev_start phase — reuses DEAD-marker slot (already past by START case) */
#define RPTUN_DBG_PHASE   0x1ED3FFD0u

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
    {
      rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_NOTIFY);
      /* Save vqid so A53 can distinguish set_status(0) (vqid=0) from
       * virtqueue kick (vqid=1 or 2) when diagnosing STEP=0x30 stalls. */
      rptun_dbg_write(RPTUN_DBG_IDLE, vqid);
    }
  rptun_dbg_write(RPTUN_DBG_NOTIFY, n + 1);

  /* Kick A53: write IPI_APU_BIT to RPU0_TRIG → sets zynqmp_ipi1 ISR bit 8 →
   * fires A53 GIC SPI 29 → rproc_vq_interrupt processes the NS / virtqueue.
   * Do NOT write IPI_ISR here — that would erase any pending A53→R5F kick.
   * DSB ensures the TRIG write reaches the IPI hardware before returning. */
  putreg32(IPI_APU_BIT, IPI_TRIG);
  __asm__ volatile("dsb sy" ::: "memory");

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

  /* Sub-breadcrumbs at RPTUN_DBG_PHASE (0x1ED3FFD0), 0xCB prefix */

  rptun_dbg_write(RPTUN_DBG_PHASE, 0xCB000001u); /* before up_enable_irq */

  /* Enable GIC interrupt now that callback is registered.  Deferring
   * up_enable_irq() to here (from zynqr5_rptun_initialize) ensures the
   * ISR never runs with callback == NULL, which was causing the board to
   * hang (AppBringUp's context was unrestorable after the early IPI). */

  up_enable_irq(IPI_IRQ);

  rptun_dbg_write(RPTUN_DBG_PHASE, 0xCB000002u); /* after up_enable_irq */

  /* A53 sent an IPI kick before R5F booted; GICD_ISPENDR[2] bit 1 may
   * still be set.  If the GIC delivered it early (before this callback
   * was registered), the ISR already cleared IPI_ISR — so the IPI_ISR
   * check below would miss it.  Call callback unconditionally to break
   * the rpmsg_virtio_wait_remote_ready ↔ notify() deadlock regardless
   * of whether the GIC beat us. */

  putreg32(IPI_APU_BIT, IPI_ISR);   /* clear any residual IPI bit (W1C) */
  __asm__ volatile("dsb sy" ::: "memory");

  rptun_dbg_write(RPTUN_DBG_PHASE, 0xCB000003u); /* before callback() */
  callback(arg, RPTUN_NOTIFY_ALL);
  rptun_dbg_write(RPTUN_DBG_PHASE, 0xCB000004u); /* callback returned */

  return OK;
}

/* Debug snapshot area at end of carveout, above g_idle_topstack:
 *   +0x00 IPI_ISR value on entry  (0xFF310010)
 *   +0x04 IPI_IMR value on entry  (0xFF310020, 0=enabled)
 *   +0x08 isr_n (counter before this call)
 *   +0x0C IPI_ISR value after clear (should be 0 if clear worked)
 */
#define DBG_IPI_SNAP    (RPTUN_DBG_BASE - 0x20u)  /* 0x1ED3FFC0 */

static int zynqr5_ipi_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct zynqr5_rptun_dev_s *priv = (FAR struct zynqr5_rptun_dev_s *)arg;
  uint32_t n = getreg32(RPTUN_DBG_ISR);
  if (n == 0)
    rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_ISR);
  rptun_dbg_write(RPTUN_DBG_ISR, n + 1);

  /* Snapshot IPI register state — safe DDR-region reads (no XMPU issue). */
  putreg32(getreg32(IPI_ISR), DBG_IPI_SNAP + 0);
  putreg32(getreg32(IPI_IMR), DBG_IPI_SNAP + 4);
  putreg32(n,                 DBG_IPI_SNAP + 8);
  __asm__ volatile("dsb sy" ::: "memory");

  putreg32(IPI_APU_BIT, IPI_ISR);   /* clear hardware ISR bit before callback */
  __asm__ volatile("dsb sy" ::: "memory");
  putreg32(getreg32(IPI_ISR), DBG_IPI_SNAP + 12);  /* should be 0 after clear */

  if (priv->callback != NULL)
    {
      priv->callback(priv->arg, RPTUN_NOTIFY_ALL);
    }

  return OK;
}

/* Idle-loop IPI check: called from up_idle() when the GIC misses a kick.
 * R5F cannot reliably read IPI_ISR (XMPU returns 0xFFFF), so we write
 * unconditionally.  The IPI_ISR W1C clear is a no-op when no kick is
 * pending; the TRIG write fires A53's IRQ#15 which runs STATUS_ENQUIRY →
 * TF-A re-enables PL0_IER[8].  callback is idempotent (posts semrx only
 * when rvq buffers are actually available). */

void zynqr5_rptun_ipi_check(void)
{
  FAR struct zynqr5_rptun_dev_s *priv = &g_zynqr5_rptun_priv;

  if (priv->callback == NULL)
    return;

  putreg32(IPI_APU_BIT, IPI_ISR);   /* clear any pending kick (W1C, safe if not set) */
  __asm__ volatile("dsb sy" ::: "memory");
  putreg32(IPI_APU_BIT, IPI_TRIG);  /* ACK to A53 — keeps PL0_IER[8] enabled */
  __asm__ volatile("dsb sy" ::: "memory");
  priv->callback(priv->arg, RPTUN_NOTIFY_ALL);
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

  *((volatile uint32_t *)0x1ED3FF34u) = 0xDB000001u; /* zynqr5_rptun_initialize entered */
  __asm__ volatile("dsb sy" ::: "memory");

  /* Zero the debug area so A53 can detect fresh writes. */

  putreg32(0, RPTUN_DBG_STEP);
  putreg32(0, RPTUN_DBG_NOTIFY);
  putreg32(0, RPTUN_DBG_ISR);
  putreg32(0, RPTUN_DBG_IDLE);
  *((volatile uint32_t *)0x1ED3FFDCu) = 0; /* up_idle heartbeat */
  *((volatile uint32_t *)0x1ED3FFD4u) = 0; /* WRKR count */
  *((volatile uint32_t *)0x1ED3FFD8u) = 0; /* STATE */
  /* IPI snapshot area (4 words at 0x1ED3FFC0) */
  *((volatile uint32_t *)(DBG_IPI_SNAP + 0)) = 0;
  *((volatile uint32_t *)(DBG_IPI_SNAP + 4)) = 0;
  *((volatile uint32_t *)(DBG_IPI_SNAP + 8)) = 0;
  *((volatile uint32_t *)(DBG_IPI_SNAP + 12)) = 0;
  putreg32(0, RPTUN_DBG_PHASE); /* zero rptun_dev_start phase (0x1ED3FFD0) */
  __asm__ volatile("dsb sy" ::: "memory");

  rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_INIT_ENTER);  /* 0x01 */

  *((volatile uint32_t *)0x1ED3FF30u) = 0xDB000002u; /* after STEP=0x01 */
  __asm__ volatile("dsb sy" ::: "memory");

  priv->rptun.ops = &g_zynqr5_rptun_ops;

  putreg32(IPI_APU_BIT, IPI_IER);

  *((volatile uint32_t *)0x1ED3FF2Cu) = 0xDB000003u; /* after IPI_IER enable */
  __asm__ volatile("dsb sy" ::: "memory");

  irq_attach(IPI_IRQ, zynqr5_ipi_isr, priv);

  *((volatile uint32_t *)0x1ED3FF28u) = 0xDB000004u; /* after irq_attach */
  __asm__ volatile("dsb sy" ::: "memory");

  /* up_enable_irq(IPI_IRQ) is deferred to register_callback.
   * Enabling GIC here fires the IPI immediately (A53 kicked R5F at boot,
   * so GICD_ISPENDR[2] bit 1 is set).  The ISR would run with callback=NULL
   * (register_callback hasn't been called yet), clear IPI_ISR, and leave
   * AppBringUp's context unrestorable — the board hangs.
   * Deferring ensures callback is set before the GIC can deliver the IRQ. */

  *((volatile uint32_t *)0x1ED3FF24u) = 0xDB000005u; /* after deferred enable */
  __asm__ volatile("dsb sy" ::: "memory");

  ret = rptun_initialize(&priv->rptun);
  if (ret == OK)
    {
      rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_INIT_DONE);  /* 0x02 */
    }

  return ret;
}
