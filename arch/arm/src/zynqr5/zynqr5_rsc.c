/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_rsc.c
 *
 * Virtio resource table for the Kria KV260 R5F.
 *
 * Linux remoteproc reads this structure from the ELF ".resource_table"
 * section to locate the two vrings used by the rpmsg virtio bus.
 *
 * Vring addresses must match the Linux DT reserved-memory nodes:
 *   0x1EF00000  rpu0vdev0vring0  16 KB  R5F → A53 TX
 *   0x1EF04000  rpu0vdev0vring1  16 KB  A53 → R5F RX
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
#include <string.h>

#include <nuttx/rptun/rptun.h>
#include <nuttx/syslog/syslog.h>
#include <openamp/remoteproc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Must match Linux DT reserved-memory nodes rpu0vdev0vring0/1 */

#define VRING0_DA    0x1EF00000u  /* R5F→A53 TX; rpu0vdev0vring0@1ef00000 */
#define VRING1_DA    0x1EF04000u  /* A53→R5F RX; rpu0vdev0vring1@1ef04000 */
#define VRING_ALIGN  0x1000u
#define VRING_SIZE   16u

/* RSC_TRACE ring.
 *
 * This is the only log sink on this board that needs neither the allocator
 * nor the scheduler: writing is a memcpy into DDR, and Linux reads it
 * out-of-band as a trace file under /sys/kernel/debug/remoteproc.  Both other
 * channels need the rptun thread to run -- the rpmsg tty and rpmsg syslog
 * endpoints -- which makes them useless for exactly the failures worth
 * catching.  Enabling CONFIG_DEBUG_MM hard-hung the board because the heap
 * debug hooks log through a path that allocates while the heap lock is held
 * with interrupts off, and arm_assert() cannot emit its register dump for the
 * same reason.
 *
 * The buffer is carved out of the top of the carveout, immediately below the
 * debug scratchpad, by up_allocate_heap() in the board's src/init.c -- keep
 * TRACE_DA and that ceiling in step.  It must lie inside a region Linux has
 * mapped, because rproc_handle_trace() resolves the address lazily on read;
 * rproc@1ed00000 covers [0x1ED00000, 0x1EF00000) so this qualifies.
 */

#define TRACE_DA     0x1EEFDE00u
#define TRACE_LEN    0x2000u      /* 8 KB */

/****************************************************************************
 * Public Data
 ****************************************************************************/

struct rptun_rsc_s g_zynqr5_rsc
    __attribute__((section(".resource_table"), aligned(8))) =
{
  .rsc_tbl_hdr =
  {
    .ver      = 1,
    .num      = 2,       /* rpmsg_vdev and log_trace */
    .reserved = {0, 0},
  },

  /* rpmsg_vdev is kept at index 0 deliberately: it was the only entry before
   * log_trace was added, and leaving its index alone means nothing about the
   * already-working rpmsg bring-up changes.  Linux walks this array, so the
   * order is otherwise arbitrary.
   */

  .offset =
  {
    offsetof(struct rptun_rsc_s, rpmsg_vdev),
    offsetof(struct rptun_rsc_s, log_trace),
  },

  .log_trace =
  {
    .type     = RSC_TRACE,
    .da       = TRACE_DA,
    .len      = TRACE_LEN,
    .reserved = 0,
    .name     = "nuttx_log",
  },

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

/****************************************************************************
 * Trace ring
 ****************************************************************************/

/* Write index.  Deliberately a plain unsigned int with no locking: this sink
 * has to be callable from an assertion with interrupts disabled, so it must
 * not take a lock, and a torn index in that situation costs a garbled line in
 * a debug log -- an acceptable trade for being able to see the log at all.
 */

static unsigned int g_trace_head;

static inline void zynqr5_trace_putbuf(FAR const char *buf, size_t len)
{
  FAR char *ring = (FAR char *)TRACE_DA;
  size_t i;

  for (i = 0; i < len; i++)
    {
      ring[g_trace_head] = buf[i];

      if (++g_trace_head >= TRACE_LEN)
        {
          /* Wrap.  The buffer is zeroed at init, so on the first pass a
           * reader sees clean text terminated by NUL; after a wrap the oldest
           * line is truncated, which is the usual ring-buffer trade.
           */

          g_trace_head = 0;
        }
    }
}

static int zynqr5_trace_putc(FAR struct syslog_channel_s *channel, int ch)
{
  char c = (char)ch;

  zynqr5_trace_putbuf(&c, 1);
  return ch;
}

static ssize_t zynqr5_trace_write(FAR struct syslog_channel_s *channel,
                                  FAR const char *buf, size_t buflen)
{
  zynqr5_trace_putbuf(buf, buflen);
  return (ssize_t)buflen;
}

static int zynqr5_trace_flush(FAR struct syslog_channel_s *channel)
{
  return OK;
}

static const struct syslog_channel_ops_s g_trace_channel_ops =
{
  .sc_putc  = zynqr5_trace_putc,
  .sc_force = zynqr5_trace_putc,   /* safe from interrupt/assert context */
  .sc_flush = zynqr5_trace_flush,
  .sc_write = zynqr5_trace_write,
  .sc_close = NULL,
};

static struct syslog_channel_s g_trace_channel =
{
  .sc_ops = &g_trace_channel_ops,
};

/****************************************************************************
 * Name: zynqr5_trace_init
 *
 * Description:
 *   Zero the trace ring and register it as a SYSLOG channel.  Called from
 *   zynqr5_board_initialize(), i.e. from arm_boot() before nx_start(), so
 *   that anything syslogged during early boot is captured.  syslog_channel()
 *   only walks an array and assigns, taking no locks, so it is safe there.
 *
 *   The ring lives outside every ELF segment, so C startup never clears it;
 *   zeroing here is what makes a reader able to tell where the text ends on
 *   the first pass, and stops a previous boot's contents being mistaken for
 *   this one's.
 *
 ****************************************************************************/

void zynqr5_trace_init(void)
{
  static const char banner[] =
    "[trace] kria-kv260 R5F RSC_TRACE ring up\n";

  memset((FAR void *)TRACE_DA, 0, TRACE_LEN);
  g_trace_head = 0;

  /* Written directly rather than through syslog(): this proves the ring and
   * the Linux-side trace file independently of whether the SYSLOG channel
   * wiring is correct, which matters because an empty trace0 is otherwise
   * ambiguous between "no output" and "sink not registered". */

  zynqr5_trace_putbuf(banner, sizeof(banner) - 1);

  syslog_channel(&g_trace_channel);
}
