/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_serial.c
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

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/serial/serial.h>

#include "arm_internal.h"
#include "arch/zynqr5/irq.h"

/****************************************************************************
 * Pre-processor Definitions
 *
 * Cadence PS UART register layout (same for UART0 and UART1).
 *
 ****************************************************************************/

#include <arch/zynqr5/chip.h>

#define UART_CR_OFFSET      0x00U
#define UART_MR_OFFSET      0x04U
#define UART_IER_OFFSET     0x08U
#define UART_IDR_OFFSET     0x0CU
#define UART_IMR_OFFSET     0x10U
#define UART_ISR_OFFSET     0x14U
#define UART_BRGR_OFFSET    0x18U
#define UART_RXTOUT_OFFSET  0x1CU
#define UART_RXWM_OFFSET    0x20U
#define UART_SR_OFFSET      0x2CU
#define UART_FIFO_OFFSET    0x30U
#define UART_BDIV_OFFSET    0x34U

/* Control register */

#define UART_CR_RXRST       (1U << 0)
#define UART_CR_TXRST       (1U << 1)
#define UART_CR_RXEN        (1U << 2)
#define UART_CR_RXDIS       (1U << 3)
#define UART_CR_TXEN        (1U << 4)
#define UART_CR_TXDIS       (1U << 5)
#define UART_CR_STPBRK      (1U << 8)

/* Status register */

#define UART_SR_RXOVR       (1U << 0)
#define UART_SR_RXEMPTY     (1U << 1)
#define UART_SR_RXFULL      (1U << 2)
#define UART_SR_TXEMPTY     (1U << 3)
#define UART_SR_TXFULL      (1U << 4)
#define UART_SR_RXTRIG      (1U << 10)

/* Interrupt bits (ISR, IER, IDR, IMR) */

#define UART_INT_RTRIG      (1U << 0)   /* RX trigger */
#define UART_INT_REMPTY     (1U << 1)   /* RX FIFO empty */
#define UART_INT_RFUL       (1U << 2)   /* RX FIFO full */
#define UART_INT_TEMPTY     (1U << 3)   /* TX FIFO empty */
#define UART_INT_TFUL       (1U << 4)   /* TX FIFO full */
#define UART_INT_ROVR       (1U << 5)   /* RX overflow */
#define UART_INT_RFRAME     (1U << 6)   /* Frame error */
#define UART_INT_RPARITY    (1U << 7)   /* Parity error */
#define UART_INT_TOUT       (1U << 8)   /* RX timeout */
#define UART_INT_DMSI       (1U << 9)   /* Delta modem status */

/* Debug breadcrumbs — readable from A53 via devmem, free scratch words
 * between the timer-ISR checkpoints (0x1ED3FF70) and the GIC post-init
 * snapshot (0x1ED3FF98).  Used only to confirm the UART RX/TX ISR path is
 * actually reached now that GIC writes are known to stick (see
 * boards/amd/kria-kv260/boot-xmpu-fix-plan.md). */

#define UART_DBG_BASE       0x1ED3FF40u
#define UART_DBG_RXCNT      (UART_DBG_BASE + 0x00u)  /* RX/TOUT ISR hits */
#define UART_DBG_TXCNT      (UART_DBG_BASE + 0x04u)  /* TEMPTY ISR hits */
#define UART_DBG_ISR        (UART_DBG_BASE + 0x08u)  /* last ISR reg value */
#define UART_DBG_RXBYTE     (UART_DBG_BASE + 0x0Cu)  /* last byte received */
#define UART_DBG_IRQ        (UART_DBG_BASE + 0x10u)  /* IRQ number of last ISR */

/* 0x1ED3FF00-0x1ED3FF1C: separate free block (0x1ED3FF20 onward collides
 * with drivers/rptun/rptun.c's own breadcrumbs — verified by grepping both
 * files before picking addresses, do the same before adding more).
 *
 * CAVEAT (2026-07-21): 0x1ED3FF00/04/08 (used below) read back stable on
 * hardware, but 0x1ED3FF10 — nominally free by this same claim — read back
 * rapidly-changing garbage in an unrelated timer debug session despite no
 * code writing to it. This "free block" may not actually be safely
 * reserved DDR; prefer picking new addresses from the RPTUN block
 * (0x1ED3FF20-0x1ED3FFFF in drivers/rptun/rptun.c / zynqr5_rptun.c) instead,
 * which has tested stable, until this is understood. */
#define UART_DBG_SERIALINIT 0x1ED3FF00u  /* arm_serialinit() reached */
#define UART_DBG_SETUP_BASE 0x1ED3FF04u  /* last uartbase passed to setup() */
#define UART_DBG_SETUP_CNT  0x1ED3FF08u  /* zynqr5_setup() call count */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct zynqr5_uart_s
{
  uint32_t uartbase;    /* UART base address */
  uint32_t baud;        /* Configured baud rate */
  bool     rxint;       /* RX interrupt enabled */
  bool     txint;       /* TX interrupt enabled */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  zynqr5_setup(struct uart_dev_s *dev);
static void zynqr5_shutdown(struct uart_dev_s *dev);
static int  zynqr5_attach(struct uart_dev_s *dev);
static void zynqr5_detach(struct uart_dev_s *dev);
static int  zynqr5_interrupt(int irq, FAR void *context, FAR void *arg);
static int  zynqr5_ioctl(struct file *filep, int cmd, unsigned long arg);
static int  zynqr5_receive(struct uart_dev_s *dev, unsigned int *status);
static void zynqr5_rxint(struct uart_dev_s *dev, bool enable);
static bool zynqr5_rxavailable(struct uart_dev_s *dev);
static void zynqr5_send(struct uart_dev_s *dev, int ch);
static void zynqr5_txint(struct uart_dev_s *dev, bool enable);
static bool zynqr5_txready(struct uart_dev_s *dev);
static bool zynqr5_txempty(struct uart_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_uart_ops __attribute__((used)) =
{
  .setup       = zynqr5_setup,
  .shutdown    = zynqr5_shutdown,
  .attach      = zynqr5_attach,
  .detach      = zynqr5_detach,
  .ioctl       = zynqr5_ioctl,
  .receive     = zynqr5_receive,
  .rxint       = zynqr5_rxint,
  .rxavailable = zynqr5_rxavailable,
  .send        = zynqr5_send,
  .txint       = zynqr5_txint,
  .txready     = zynqr5_txready,
  .txempty     = zynqr5_txempty,
};

#ifdef CONFIG_ZYNQR5_UART0
static struct zynqr5_uart_s g_uart0priv =
{
  .uartbase = ZYNQR5_UART0_BASE,
  .baud     = CONFIG_UART0_BAUD,
};

static char g_uart0rxbuf[CONFIG_UART0_RXBUFSIZE];
static char g_uart0txbuf[CONFIG_UART0_TXBUFSIZE];

static struct uart_dev_s g_uart0dev =
{
  .recv =
    {
      .size   = CONFIG_UART0_RXBUFSIZE,
      .buffer = g_uart0rxbuf,
    },
  .xmit =
    {
      .size   = CONFIG_UART0_TXBUFSIZE,
      .buffer = g_uart0txbuf,
    },
  .ops  = &g_uart_ops,
  .priv = &g_uart0priv,
};
#endif /* CONFIG_ZYNQR5_UART0 */

#ifdef CONFIG_ZYNQR5_UART1
static struct zynqr5_uart_s g_uart1priv =
{
  .uartbase = ZYNQR5_UART1_BASE,
  .baud     = CONFIG_UART1_BAUD,
};

static char g_uart1rxbuf[CONFIG_UART1_RXBUFSIZE];
static char g_uart1txbuf[CONFIG_UART1_TXBUFSIZE];

static struct uart_dev_s g_uart1dev =
{
  .recv =
    {
      .size   = CONFIG_UART1_RXBUFSIZE,
      .buffer = g_uart1rxbuf,
    },
  .xmit =
    {
      .size   = CONFIG_UART1_TXBUFSIZE,
      .buffer = g_uart1txbuf,
    },
  .ops  = &g_uart_ops,
  .priv = &g_uart1priv,
};
#endif /* CONFIG_ZYNQR5_UART1 */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t zynqr5_uart_irq(struct zynqr5_uart_s *priv)
{
#ifdef CONFIG_ZYNQR5_UART0
  if (priv->uartbase == ZYNQR5_UART0_BASE)
    return ZYNQR5_IRQ_UART0;
#endif
  return ZYNQR5_IRQ_UART1;
}

static int zynqr5_setup(struct uart_dev_s *dev)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t base = priv->uartbase;
  uint32_t cr;

  putreg32(base, UART_DBG_SETUP_BASE);
  *((volatile uint32_t *)UART_DBG_SETUP_CNT) += 1;
  __asm__ volatile("dsb sy" ::: "memory");

  /* Reset RX and TX FIFOs, then enable RX and TX */

  putreg32(UART_CR_RXRST | UART_CR_TXRST, base + UART_CR_OFFSET);

  cr = UART_CR_RXEN | UART_CR_TXEN;
  putreg32(cr, base + UART_CR_OFFSET);

  /* Disable all UART interrupts; rxint/txint re-enable as needed */

  putreg32(0xffffffff, base + UART_IDR_OFFSET);

  return OK;
}

static void zynqr5_shutdown(struct uart_dev_s *dev)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t base = priv->uartbase;

  putreg32(UART_CR_TXDIS | UART_CR_RXDIS, base + UART_CR_OFFSET);
  putreg32(0xffffffff, base + UART_IDR_OFFSET);
}

static int zynqr5_attach(struct uart_dev_s *dev)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t irq = zynqr5_uart_irq(priv);
  int ret = irq_attach(irq, zynqr5_interrupt, dev);
  if (ret == OK)
    up_enable_irq(irq);
  return ret;
}

static void zynqr5_detach(struct uart_dev_s *dev)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t irq = zynqr5_uart_irq(priv);
  up_disable_irq(irq);
  irq_detach(irq);
}

static int zynqr5_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct uart_dev_s *dev = (FAR struct uart_dev_s *)arg;
  FAR struct zynqr5_uart_s *priv = (FAR struct zynqr5_uart_s *)dev->priv;
  uint32_t base = priv->uartbase;
  uint32_t isr;

  isr = getreg32(base + UART_ISR_OFFSET);
  putreg32(isr, base + UART_ISR_OFFSET);  /* W1C clear */

  putreg32(isr,             UART_DBG_ISR);
  putreg32((uint32_t)irq,   UART_DBG_IRQ);
  __asm__ volatile("dsb sy" ::: "memory");

  if (isr & (UART_INT_RTRIG | UART_INT_TOUT))
    {
      *((volatile uint32_t *)UART_DBG_RXCNT) += 1;
      uart_recvchars(dev);
    }

  if (isr & UART_INT_TEMPTY)
    {
      *((volatile uint32_t *)UART_DBG_TXCNT) += 1;
      uart_xmitchars(dev);
    }

  return OK;
}

static int zynqr5_ioctl(struct file *filep, int cmd, unsigned long arg)
{
  return -ENOTTY;
}

static int zynqr5_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t base = priv->uartbase;
  int ch;

  *status = 0;
  ch = (int)(getreg32(base + UART_FIFO_OFFSET) & 0xff);
  putreg32((uint32_t)ch, UART_DBG_RXBYTE);
  return ch;
}

static void zynqr5_rxint(struct uart_dev_s *dev, bool enable)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t base = priv->uartbase;

  priv->rxint = enable;

  if (enable)
    putreg32(UART_INT_RTRIG | UART_INT_TOUT, base + UART_IER_OFFSET);
  else
    putreg32(UART_INT_RTRIG | UART_INT_TOUT, base + UART_IDR_OFFSET);
}

static bool zynqr5_rxavailable(struct uart_dev_s *dev)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t base = priv->uartbase;

  return (getreg32(base + UART_SR_OFFSET) & UART_SR_RXEMPTY) == 0;
}

static void zynqr5_send(struct uart_dev_s *dev, int ch)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t base = priv->uartbase;

  while (getreg32(base + UART_SR_OFFSET) & UART_SR_TXFULL)
    {
    }

  putreg32((uint32_t)ch, base + UART_FIFO_OFFSET);
}

static void zynqr5_txint(struct uart_dev_s *dev, bool enable)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t base = priv->uartbase;
  irqstate_t flags;

  priv->txint = enable;

  if (enable)
    {
      /* Enable TEMPTY and drain under a critical section so the ISR
       * cannot preempt between the IER write and uart_xmitchars(). */

      flags = enter_critical_section();
      putreg32(UART_INT_TEMPTY, base + UART_IER_OFFSET);
      uart_xmitchars(dev);
      leave_critical_section(flags);
    }
  else
    {
      putreg32(UART_INT_TEMPTY, base + UART_IDR_OFFSET);
    }
}

static bool zynqr5_txready(struct uart_dev_s *dev)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t base = priv->uartbase;

  return (getreg32(base + UART_SR_OFFSET) & UART_SR_TXFULL) == 0;
}

static bool zynqr5_txempty(struct uart_dev_s *dev)
{
  struct zynqr5_uart_s *priv = (struct zynqr5_uart_s *)dev->priv;
  uint32_t base = priv->uartbase;

  return (getreg32(base + UART_SR_OFFSET) & UART_SR_TXEMPTY) != 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_serialinit
 *
 * Description:
 *   Register the PS UART serial drivers with the NuttX VFS layer.
 *   Called from arm_initialize() → up_initialize() during OS boot.
 *
 ****************************************************************************/

void arm_serialinit(void)
{
  putreg32(0xCAFE0001u, UART_DBG_SERIALINIT);
  __asm__ volatile("dsb sy" ::: "memory");

  /* Always register both UARTs under their canonical names first:
   *   UART0 → /dev/ttyS0
   *   UART1 → /dev/ttyS1
   * Then alias the console UART to /dev/console.
   * This ensures CONFIG_SYSLOG_DEVPATH="/dev/ttyS1" always resolves. */

#ifdef CONFIG_ZYNQR5_UART0
  uart_register("/dev/ttyS0", &g_uart0dev);
#endif
#ifdef CONFIG_ZYNQR5_UART1
  uart_register("/dev/ttyS1", &g_uart1dev);
#endif

#ifdef CONFIG_ZYNQR5_UART0_CONSOLE
  uart_register("/dev/console", &g_uart0dev);
#elif defined(CONFIG_ZYNQR5_UART1_CONSOLE)
  uart_register("/dev/console", &g_uart1dev);
#endif
}

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Polled character output — used by early syslog and up_nputs before the
 *   full serial driver is operational.
 *
 ****************************************************************************/

int up_putc(int ch)
{
  if (ch == '\n')
    {
      arm_lowputc('\r');
    }

  arm_lowputc((char)ch);
  return ch;
}
