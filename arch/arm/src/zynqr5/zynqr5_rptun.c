/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_rptun.c
 *
 * NuttX rptun platform driver for the Kria KV260 R5F.
 *
 * Implements the rptun_ops_s interface over the ZynqMPSoC IPI hardware
 * (RPU0 channel).  The R5F is the virtio slave; Linux remoteproc on the
 * A53 is the master.
 *
 * The doorbell in both directions is the IPI block: outbound kicks are
 * IPI_TRIG writes, inbound kicks arrive as RPU GIC SPI 33 (IRQ 65) and are
 * taken by zynqr5_ipi_isr().  zynqr5_rptun_ipi_check() remains as a polled
 * fallback on a timeout, for the case where that interrupt stops arriving;
 * see zynqr5_rptun_initialize() for the history behind both.
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
#include <nuttx/clock.h>

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
#define IPI_OBS        (IPI_RPU0_BASE + 0x04u) /* read: kick sent, not yet acked */
#define IPI_ISR        (IPI_RPU0_BASE + 0x10u) /* write IPI_APU_BIT → clear pending */
#define IPI_IMR        (IPI_RPU0_BASE + 0x14u) /* read: bit set = masked, 0 = enabled */
#define IPI_IER        (IPI_RPU0_BASE + 0x18u) /* write IPI_APU_BIT → enable RX IRQ */
#define IPI_IDR        (IPI_RPU0_BASE + 0x1Cu) /* write IPI_APU_BIT → disable RX IRQ */

/* Agent bit for the peer, in this core's IPI registers (UG1085 §14).  The
 * ZynqMPSoC IPI registers are per-channel bitmaps of the *other* agents, so the
 * same bit identifies the A53 whether it is being kicked or has kicked us:
 *
 *   kick     write bit 24 to RPU0_TRIG -> sets bit 8 in zynqmp_ipi1's ISR ->
 *            A53 GIC SPI 29 -> rproc_vq_interrupt()
 *   receive  A53 (through the zynqmp_ipi1 TF-A SMC) sets bit 24 in RPU0_ISR ->
 *            RPU GIC SPI 33
 *
 * The kick direction can be verified from Linux without any firmware running:
 * writing 0x01000000 to 0xFF310000 with devmem increments the zynqmp_ipi1
 * count in /proc/interrupts.
 *
 * Note the name is loose: in the IPI agent bitmap bit 24 is the *PL0* slot
 * (the APU's own slot is bit 0, per XPAR_XIPIPS_TARGET_PSU_* in Xilinx's
 * xipipsu_hw.h).  The value is nonetheless correct here, because Linux does
 * not talk to us from its own APU channel -- Xilinx's OpenAMP devicetree
 * gives remoteproc the PL0 channel at 0xFF340000 as its mailbox, which is
 * why the Linux-side interrupt is zynqmp_ipi1 on SPI 29 rather than the
 * APU channel's SPI 35.  Keep the value, distrust the name. */

#define IPI_APU_BIT    (1u << 24)

/* GIC ID 65 = SPI 33 + 32, per XPAR_XIPIPSU_0_INTR for the channel at
 * 0xFF310000.  Note this is the RPU GIC's numbering; the A53 sees the same
 * physical IPI as SPI 29 on its own separate GIC instance, so the two sides
 * legitimately quote different interrupt numbers for one doorbell. */

#define IPI_IRQ        ZYNQR5_IRQ_IPI1

/* Debug breadcrumbs in the top 32 bytes of the debug scratchpad.  The rpmsg
 * handshake spans two processors, so a stall gives no console output on either
 * side: these counters are how the A53 tells "the R5F never reached
 * init_vdev" from "the R5F is kicking but the A53 is not answering".  Writes
 * are visible to the A53 without a cache maintenance operation because the
 * R5F D-cache is disabled.
 *
 * Scratchpad address map:
 * boards/amd/kria-kv260/nuttx-config/scripts/script.ld.
 */

#define RPTUN_DBG_BASE    0x1EEFFFE0u
#define RPTUN_DBG_STEP    (RPTUN_DBG_BASE + 0x00u) /* last lifecycle step */
#define RPTUN_DBG_NOTIFY  (RPTUN_DBG_BASE + 0x04u) /* kicks sent to A53 */
#define RPTUN_DBG_ISR     (RPTUN_DBG_BASE + 0x08u) /* IPI IRQs taken */
#define RPTUN_DBG_IDLE    (RPTUN_DBG_BASE + 0x0Cu) /* first notify()'s vqid */

/* Shared with drivers/rptun/rptun.c, which writes its own rptun_dev_start()
 * progress markers here.  The slot is safe to reuse only because rptun.c is
 * past its 0xDEAD markers by the time this driver's callbacks run. */

#define RPTUN_DBG_PHASE   0x1EEFFFD0u

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

      /* Record the first vqid only.  If the handshake stalls at STEP 0x30,
       * this says which kick it was: vqid 0 is the virtio set_status()
       * notification, 1 and 2 are the TX and RX virtqueues.  A stall on 0
       * means the A53 never acknowledged the status change; a stall on 1 or 2
       * means status negotiation completed and the vring itself is wedged. */

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

  /* 0xCB-prefixed markers bracket the callback so a hang inside it is
   * distinguishable from never having reached it. */

  rptun_dbg_write(RPTUN_DBG_PHASE, 0xCB000001u); /* callback stored */

  /* Discard any IPI raised during boot before Linux and this driver were both
   * ready; it carries no information and would otherwise look like a kick.
   * The register is write-1-to-clear, so this is harmless when nothing is
   * pending. */

  putreg32(IPI_APU_BIT, IPI_ISR);
  __asm__ volatile("dsb sy" ::: "memory");

  /* Drive the callback once, unconditionally.  Normally the first inbound IPI
   * would do this, but that interrupt is masked on this port (see
   * zynqr5_rptun_initialize()), and rpmsg_virtio_wait_remote_ready() blocks
   * until something signals it -- so the handshake has to be primed by hand
   * or it never starts. */

  rptun_dbg_write(RPTUN_DBG_PHASE, 0xCB000002u); /* before callback() */
  callback(arg, RPTUN_NOTIFY_ALL);
  rptun_dbg_write(RPTUN_DBG_PHASE, 0xCB000003u); /* callback returned */

  return OK;
}

/* IPI register snapshot taken inside the ISR.  Reading these registers here
 * rather than from the A53 is the point: it captures the hardware state at the
 * instant the interrupt was taken, which is the only way to confirm the W1C
 * clear actually landed.
 *   +0x00  IPI_ISR on entry
 *   +0x04  IPI_IMR on entry (0 = unmasked)
 *   +0x08  ISR count before this entry
 *   +0x0C  IPI_ISR after the clear; non-zero means the clear was discarded
 */

#define DBG_IPI_SNAP    (RPTUN_DBG_BASE - 0x20u)  /* 0x1EEFFFC0 */

static int zynqr5_ipi_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct zynqr5_rptun_dev_s *priv = (FAR struct zynqr5_rptun_dev_s *)arg;
  uint32_t n = getreg32(RPTUN_DBG_ISR);

  if (n == 0)
    {
      rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_ISR);

      /* One-shot snapshot on the very first interrupt.  Kept because it is
       * the only way to confirm from the A53 that the W1C clear landed at
       * the instant the interrupt was taken; doing it on every entry would
       * add four uncached MMIO accesses to the hot path. */

      putreg32(getreg32(IPI_ISR), DBG_IPI_SNAP + 0);
      putreg32(getreg32(IPI_IMR), DBG_IPI_SNAP + 4);
    }

  putreg32(n + 1, RPTUN_DBG_ISR);

  /* Clear the pending bit before running the callback, so a kick that lands
   * while the callback is executing is not lost. */

  putreg32(IPI_APU_BIT, IPI_ISR);
  __asm__ volatile("dsb sy" ::: "memory");

  if (n == 0)
    {
      putreg32(getreg32(IPI_ISR), DBG_IPI_SNAP + 12);  /* 0 if the clear took */
    }

  /* Kick back.  This is the same load-bearing ACK the polled path sends (see
   * zynqr5_rptun_ipi_check()): the A53's handler runs a STATUS_ENQUIRY that
   * makes TF-A re-enable PL0_IER[8], without which the A53 stops delivering
   * after the first exchange. */

  putreg32(IPI_APU_BIT, IPI_TRIG);
  __asm__ volatile("dsb sy" ::: "memory");

  if (priv->callback != NULL)
    {
      priv->callback(priv->arg, RPTUN_NOTIFY_ALL);
    }

  return OK;
}

/* Diagnostics for the polled receive path, in the free low half of the debug
 * scratchpad (allocation map: boards/amd/kria-kv260/nuttx-config/scripts/
 * script.ld).  These exist to settle whether the R5F can read IPI_ISR back,
 * which decides whether the poll can be made edge-triggered -- the old code
 * asserted it could not (see the removed comment: "it returns 0xFFFF") and
 * therefore did everything unconditionally.
 */

#define DBG_IPI_LAST_ISR  0x1EEFFE10u  /* IPI_ISR as last read by the poll */
#define DBG_IPI_SEEN      0x1EEFFE14u  /* polls that found bit 24 set */
#define DBG_IPI_POLLS     0x1EEFFE18u  /* poll calls */
#define DBG_IPI_KICKS     0x1EEFFE1Cu  /* TRIG writes issued by the poll */
#define DBG_IPI_LAST_IMR  0x1EEFFE20u  /* IPI_IMR (correct offset 0x14) */

/* Lower bound on the interval between the poll's unsolicited TRIG writes,
 * in system ticks.  See the discussion of the keep-alive in
 * zynqr5_rptun_ipi_check().  10 ms is deliberately the same wall-clock rate
 * the poll produced before CONFIG_USEC_PER_TICK dropped to 1000, so raising
 * the tick does not multiply the interrupt load on the A53.
 */

#define IPI_KEEPALIVE_TICKS  MSEC2TICK(10)

/****************************************************************************
 * Name: zynqr5_rptun_ipi_check
 *
 * Description:
 *   Substitute for the inbound IPI interrupt, called periodically from
 *   up_idle() and from rptun's receive loop.  This is the receive path on
 *   this port whenever IRQ 65 is masked (see zynqr5_rptun_initialize()), as
 *   nothing else would notice an A53 kick.
 *
 *   The two halves of this function have very different natural rates, and
 *   the previous version ran both at the poll rate:
 *
 *     - the callback only inspects the vring in DDR.  That is local, cheap,
 *       and costs the A53 nothing, so it should run as fast as the poll can.
 *     - the TRIG write raises a real interrupt on the A53, whose handler runs
 *       a STATUS_ENQUIRY that causes TF-A to re-enable PL0_IER[8].  Without
 *       it the A53 side latches disabled after an exchange and stops
 *       delivering, so it is load-bearing rather than a bare ACK -- but it is
 *       also the one thing here that the other processor pays for.
 *
 *   Coupling them meant the poll rate was capped by what Linux would tolerate
 *   in spurious IPIs.  Decoupled, the callback runs every poll while TRIG is
 *   issued only when there is a reason to: either a kick was actually
 *   observed pending, or IPI_KEEPALIVE_TICKS have passed since the last one.
 *
 *   Reading IPI_ISR to decide "was a kick pending" degrades safely if the
 *   readback turns out to be unusable: garbage that keeps bit 24 set reduces
 *   this to the old kick-every-poll behaviour, and garbage that keeps it
 *   clear still leaves the keep-alive.  DBG_IPI_LAST_ISR records what the
 *   readback actually produces so this can be checked from the A53.
 *
 *   All of the register operations remain idempotent, so extra calls are
 *   harmless: the IPI_ISR write is write-1-to-clear, and the rptun callback
 *   posts its receive semaphore only when the RX virtqueue really holds
 *   buffers.
 *
 ****************************************************************************/

void zynqr5_rptun_ipi_check(void)
{
  FAR struct zynqr5_rptun_dev_s *priv = &g_zynqr5_rptun_priv;
  static clock_t s_last_kick;
  clock_t now;
  uint32_t isr;
  bool pending;

  if (priv->callback == NULL)
    return;

  isr = getreg32(IPI_ISR);
  putreg32(isr, DBG_IPI_LAST_ISR);
  putreg32(getreg32(IPI_IMR), DBG_IPI_LAST_IMR);
  putreg32(getreg32(DBG_IPI_POLLS) + 1, DBG_IPI_POLLS);

  pending = (isr & IPI_APU_BIT) != 0;
  if (pending)
    {
      putreg32(getreg32(DBG_IPI_SEEN) + 1, DBG_IPI_SEEN);
    }

  putreg32(IPI_APU_BIT, IPI_ISR);   /* clear the kick (W1C, safe if not set) */
  __asm__ volatile("dsb sy" ::: "memory");

  /* Keep-alive/ACK to the A53.  Unsigned tick subtraction is wrap-safe. */

  now = clock_systime_ticks();
  if (pending || (clock_t)(now - s_last_kick) >= IPI_KEEPALIVE_TICKS)
    {
      s_last_kick = now;
      putreg32(IPI_APU_BIT, IPI_TRIG);
      __asm__ volatile("dsb sy" ::: "memory");
      putreg32(getreg32(DBG_IPI_KICKS) + 1, DBG_IPI_KICKS);
    }

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
 *   Register the rptun device.  Must be called from
 *   board_late_initialize(), i.e. after the scheduler is running:
 *   rptun_initialize() spawns a kernel thread and takes semaphores.
 *
 ****************************************************************************/

int zynqr5_rptun_initialize(void)
{
  FAR struct zynqr5_rptun_dev_s *priv = &g_zynqr5_rptun_priv;
  int ret;

  /* Entry counter, deliberately incremented before the zeroing below so it
   * survives it.  A value above 1 means this function ran more than once,
   * which on a single-shot init path can only mean the firmware crashed and
   * was restarted -- the distinguishing symptom of a boot crash-loop, which
   * otherwise looks identical to a clean boot from the A53's point of view. */

  *((volatile uint32_t *)0x1EEFFF20u) += 1;
  __asm__ volatile("dsb sy" ::: "memory");

  /* 0xDB-prefixed markers step through this function.  Each is a distinct
   * scratchpad word rather than one shared slot so that the last marker
   * reached is still readable after a later step faults. */

  *((volatile uint32_t *)0x1EEFFF34u) = 0xDB000001u; /* entered */
  __asm__ volatile("dsb sy" ::: "memory");

  /* Zero the counters.  The scratchpad lies outside every ELF segment, so it
   * is never cleared by C startup and retains values from the previous run
   * across a remoteproc restart.  Without this, stale counters read as
   * activity that never happened. */

  putreg32(0, RPTUN_DBG_STEP);
  putreg32(0, RPTUN_DBG_NOTIFY);
  putreg32(0, RPTUN_DBG_ISR);
  putreg32(0, RPTUN_DBG_IDLE);
  *((volatile uint32_t *)0x1EEFFFDCu) = 0; /* up_idle heartbeat */
  *((volatile uint32_t *)0x1EEFFFD4u) = 0; /* WRKR count */
  *((volatile uint32_t *)0x1EEFFFD8u) = 0; /* STATE */
  /* IPI snapshot area (4 words at 0x1EEFFFC0) */
  *((volatile uint32_t *)(DBG_IPI_SNAP + 0)) = 0;
  *((volatile uint32_t *)(DBG_IPI_SNAP + 4)) = 0;
  *((volatile uint32_t *)(DBG_IPI_SNAP + 8)) = 0;
  *((volatile uint32_t *)(DBG_IPI_SNAP + 12)) = 0;
  /* Polled-receive-path diagnostics (5 words at 0x1EEFFE10) */
  putreg32(0, DBG_IPI_LAST_ISR);
  putreg32(0, DBG_IPI_SEEN);
  putreg32(0, DBG_IPI_POLLS);
  putreg32(0, DBG_IPI_KICKS);
  putreg32(0, DBG_IPI_LAST_IMR);
  putreg32(0, RPTUN_DBG_PHASE); /* zero rptun_dev_start phase (0x1EEFFFD0) */
  __asm__ volatile("dsb sy" ::: "memory");

  rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_INIT_ENTER);

  *((volatile uint32_t *)0x1EEFFF30u) = 0xDB000002u; /* counters zeroed */
  __asm__ volatile("dsb sy" ::: "memory");

  priv->rptun.ops = &g_zynqr5_rptun_ops;

  /* Unmask the IPI at the IPI block itself.  This is separate from unmasking
   * it at the GIC, which is not done -- see the comment below.  Enabling it
   * here is what allows the pending bit to be observed and cleared by the
   * polled path. */

  putreg32(IPI_APU_BIT, IPI_IER);

  *((volatile uint32_t *)0x1EEFFF2Cu) = 0xDB000003u; /* IPI_IER written */
  __asm__ volatile("dsb sy" ::: "memory");

  irq_attach(IPI_IRQ, zynqr5_ipi_isr, priv);

  *((volatile uint32_t *)0x1EEFFF28u) = 0xDB000004u; /* handler attached */
  __asm__ volatile("dsb sy" ::: "memory");

  /* Enable the inbound doorbell.
   *
   * This call was removed on 2026-07-10 and left out for a fortnight on the
   * theory that the LPD_XPPU blocks R5F writes to IPI_ISR (0xFF310010) -- the
   * register zynqr5_ipi_isr() must write to dismiss the interrupt -- so that
   * enabling IRQ 65 would fault in interrupt context and halt the firmware.
   * That diagnosis does not survive contact with the hardware:
   *
   *   - IPI_TRIG (+0x00) and IPI_ISR (+0x10) sit in the *same* 64 KB XPPU
   *     aperture.  No XPPU configuration can permit one and block the other,
   *     yet TRIG writes demonstrably work.
   *   - this design never programs the XPPU at all; psu_lpd_xppu_data() in
   *     the Vivado-generated psu_init_gpl.c is an empty stub, and the XPPU is
   *     permissive until a design opts in.
   *   - zynqr5_rptun_ipi_check() has been writing IPI_ISR on every poll for
   *     weeks without faulting, and IPI_ISR reads back 0 immediately after,
   *     which is only possible if the W1C write is landing.
   *   - the 2026-07-10 abort is fully explained by a bug fixed since: at that
   *     time chip.h still put GICC at 0xF9020000 (the APU GIC), and
   *     arm_decodeirq() reads GICC_IAR on every interrupt entry.  Enabling
   *     *any* SPI would have aborted the same way.  Nobody retested IRQ 65
   *     after the address was corrected.
   *
   * The earlier claim that the R5F "cannot reliably read IPI_ISR -- it
   * returns 0xFFFF" came from reading offset 0x20, which is reserved.  The
   * real IMR is at 0x14 and reads 0x0E0F0301, matching the A53's view
   * exactly.
   *
   * zynqr5_rptun_ipi_check() is deliberately kept as a fallback rather than
   * deleted: rptun's receive loop waits on a timeout, so if this interrupt
   * ever stops arriving the link degrades to the old polled behaviour
   * instead of hanging.  RPTUN_DBG_ISR counts real interrupts, so the two
   * paths can be told apart from the A53. */

  up_enable_irq(IPI_IRQ);

  *((volatile uint32_t *)0x1EEFFF1Cu) = 0xDB000006u; /* survived enable_irq */
  __asm__ volatile("dsb sy" ::: "memory");

  *((volatile uint32_t *)0x1EEFFF24u) = 0xDB000005u; /* entering rptun_init */
  __asm__ volatile("dsb sy" ::: "memory");

  ret = rptun_initialize(&priv->rptun);

  if (ret == OK)
    {
      rptun_dbg_write(RPTUN_DBG_STEP, DBG_STEP_INIT_DONE);
    }

  /* The CONFIG_RPMSG_UART endpoint is registered from board_app_initialize()
   * in boards/amd/kria-kv260/src/init.c, not here.  Registering it at this
   * point races with the "rpmsg-ping" endpoint over RPMSG_ADDR_ANY
   * assignment, and whichever loses gets an address the A53 is not
   * expecting. */

  return ret;
}
