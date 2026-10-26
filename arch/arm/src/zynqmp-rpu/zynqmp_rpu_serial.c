/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_serial.c
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

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#ifdef CONFIG_SERIAL_TERMIOS
#  include <termios.h>
#endif

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/serial/serial.h>

#include <arch/board/board.h>

#include "arm_internal.h"
#include "hardware/zynqmp_rpu_uart.h"
#include "zynqmp_rpu_lowputc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef USE_SERIALDRIVER

/* Which UART with be tty0/console and which tty1? */

/* First pick the console and ttys0.  This could be either of UART0-1 */

#if defined(CONFIG_UART0_SERIAL_CONSOLE)
#    define CONSOLE_DEV         g_uart0port /* UART0 is console */
#    define TTYS0_DEV           g_uart0port /* UART0 is ttyS0 */
#    define UART0_ASSIGNED      1
#elif defined(CONFIG_UART1_SERIAL_CONSOLE)
#    define CONSOLE_DEV         g_uart1port /* UART1 is console */
#    define TTYS0_DEV           g_uart1port /* UART1 is ttyS0 */
#    define UART1_ASSIGNED      1
#else
#  undef CONSOLE_DEV                        /* No console */
#  if defined(CONFIG_ZYNQMP_RPU_UART0)
#    define TTYS0_DEV           g_uart0port /* UART0 is ttyS0 */
#    define UART0_ASSIGNED      1
#  elif defined(CONFIG_ZYNQMP_RPU_UART1)
#    define TTYS0_DEV           g_uart1port /* UART1 is ttyS0 */
#    define UART1_ASSIGNED      1
#  endif
#endif

/* Pick ttys1.  This could be either of UART0-1, excluding the console
 * UART.
 */

#if defined(CONFIG_ZYNQMP_RPU_UART0) && !defined(UART0_ASSIGNED)
#  define TTYS1_DEV             g_uart0port /* UART0 is ttyS1 */
#  define UART0_ASSIGNED        1
#elif defined(CONFIG_ZYNQMP_RPU_UART1) && !defined(UART1_ASSIGNED)
#  define TTYS1_DEV             g_uart1port /* UART1 is ttyS1 */
#  define UART1_ASSIGNED        1
#endif

/* Interrupt sources used by this driver */

#define UART_RXINTS  (XUARTPS_IXR_RXOVR | XUARTPS_IXR_TOUT)
#define UART_TXINTS  XUARTPS_IXR_TXEMPTY
#define UART_ERRINTS (XUARTPS_IXR_FRAMING | XUARTPS_IXR_PARITY | \
                      XUARTPS_IXR_OVER)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct zynqmp_rpu_dev_s
{
  const uint32_t uartbase;       /* Base address of UART registers */
  struct uart_config_s config;   /* UART configuration */
  uint8_t irq;                   /* IRQ associated with this UART */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  zynqmp_rpu_setup(struct uart_dev_s *dev);
static void zynqmp_rpu_shutdown(struct uart_dev_s *dev);
static int  zynqmp_rpu_attach(struct uart_dev_s *dev);
static void zynqmp_rpu_detach(struct uart_dev_s *dev);
static int  zynqmp_rpu_interrupt(int irq, void *context, void *arg);
static int  zynqmp_rpu_ioctl(struct file *filep, int cmd,
                             unsigned long arg);
static int  zynqmp_rpu_receive(struct uart_dev_s *dev,
                               unsigned int *status);
static void zynqmp_rpu_rxint(struct uart_dev_s *dev, bool enable);
static bool zynqmp_rpu_rxavailable(struct uart_dev_s *dev);
static void zynqmp_rpu_send(struct uart_dev_s *dev, int ch);
static void zynqmp_rpu_txint(struct uart_dev_s *dev, bool enable);
static bool zynqmp_rpu_txready(struct uart_dev_s *dev);
static bool zynqmp_rpu_txempty(struct uart_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_uart_ops =
{
  .setup          = zynqmp_rpu_setup,
  .shutdown       = zynqmp_rpu_shutdown,
  .attach         = zynqmp_rpu_attach,
  .detach         = zynqmp_rpu_detach,
  .ioctl          = zynqmp_rpu_ioctl,
  .receive        = zynqmp_rpu_receive,
  .rxint          = zynqmp_rpu_rxint,
  .rxavailable    = zynqmp_rpu_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  .rxflowcontrol  = NULL,
#endif
  .send           = zynqmp_rpu_send,
  .txint          = zynqmp_rpu_txint,
  .txready        = zynqmp_rpu_txready,
  .txempty        = zynqmp_rpu_txempty,
};

/* I/O buffers */

#ifdef CONFIG_ZYNQMP_RPU_UART0
static char g_uart0rxbuffer[CONFIG_UART0_RXBUFSIZE];
static char g_uart0txbuffer[CONFIG_UART0_TXBUFSIZE];
#endif
#ifdef CONFIG_ZYNQMP_RPU_UART1
static char g_uart1rxbuffer[CONFIG_UART1_RXBUFSIZE];
static char g_uart1txbuffer[CONFIG_UART1_TXBUFSIZE];
#endif

/* This describes the state of the UART0 port. */

#ifdef CONFIG_ZYNQMP_RPU_UART0
static struct zynqmp_rpu_dev_s g_uart0priv =
{
  .uartbase       = ZYNQMP_RPU_UART0_BASE,
  .config         =
  {
    .baud         = CONFIG_UART0_BAUD,
    .parity       = CONFIG_UART0_PARITY,
    .bits         = CONFIG_UART0_BITS,
    .stopbits2    = CONFIG_UART0_2STOP,
  },
  .irq            = ZYNQMP_RPU_IRQ_UART0,
};

static uart_dev_t g_uart0port =
{
  .recv     =
  {
    .size   = CONFIG_UART0_RXBUFSIZE,
    .buffer = g_uart0rxbuffer,
  },
  .xmit     =
  {
    .size   = CONFIG_UART0_TXBUFSIZE,
    .buffer = g_uart0txbuffer,
  },
  .ops      = &g_uart_ops,
  .priv     = &g_uart0priv,
};
#endif

/* This describes the state of the UART1 port. */

#ifdef CONFIG_ZYNQMP_RPU_UART1
static struct zynqmp_rpu_dev_s g_uart1priv =
{
  .uartbase       = ZYNQMP_RPU_UART1_BASE,
  .config         =
  {
    .baud         = CONFIG_UART1_BAUD,
    .parity       = CONFIG_UART1_PARITY,
    .bits         = CONFIG_UART1_BITS,
    .stopbits2    = CONFIG_UART1_2STOP,
  },
  .irq            = ZYNQMP_RPU_IRQ_UART1,
};

static uart_dev_t g_uart1port =
{
  .recv     =
  {
    .size   = CONFIG_UART1_RXBUFSIZE,
    .buffer = g_uart1rxbuffer,
  },
  .xmit     =
  {
    .size   = CONFIG_UART1_TXBUFSIZE,
    .buffer = g_uart1txbuffer,
  },
  .ops      = &g_uart_ops,
  .priv     = &g_uart1priv,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zynqmp_rpu_serialin
 ****************************************************************************/

static inline uint32_t zynqmp_rpu_serialin(struct zynqmp_rpu_dev_s *priv,
                                           int offset)
{
  return getreg32(priv->uartbase + offset);
}

/****************************************************************************
 * Name: zynqmp_rpu_serialout
 ****************************************************************************/

static inline void zynqmp_rpu_serialout(struct zynqmp_rpu_dev_s *priv,
                                        int offset, uint32_t value)
{
  putreg32(value, priv->uartbase + offset);
}

/****************************************************************************
 * Name: zynqmp_rpu_restoreuartint
 ****************************************************************************/

static inline void zynqmp_rpu_restoreuartint(struct zynqmp_rpu_dev_s *priv,
                                             uint32_t ints)
{
  /* Restore the previous interrupt state (assuming all interrupts
   * disabled).  IER only sets mask bits, so this re-enables exactly the
   * sources that were saved.
   */

  zynqmp_rpu_serialout(priv, XUARTPS_IER_OFFSET, ints);
}

/****************************************************************************
 * Name: zynqmp_rpu_disableallints
 ****************************************************************************/

static void zynqmp_rpu_disableallints(struct zynqmp_rpu_dev_s *priv,
                                      uint32_t *ints)
{
  irqstate_t flags;

  /* The following must be atomic */

  flags = enter_critical_section();
  if (ints)
    {
      /* Return the current enable bits */

      *ints = zynqmp_rpu_serialin(priv, XUARTPS_IMR_OFFSET);
    }

  /* Disable all interrupts */

  zynqmp_rpu_serialout(priv, XUARTPS_IDR_OFFSET, XUARTPS_IXR_MASK);
  leave_critical_section(flags);
}

/****************************************************************************
 * Name: zynqmp_rpu_setup
 *
 * Description:
 *   Configure the UART baud, bits, parity, etc. This method is called the
 *   first time that the serial port is opened.
 *
 ****************************************************************************/

static int zynqmp_rpu_setup(struct uart_dev_s *dev)
{
#ifndef CONFIG_SUPPRESS_UART_CONFIG
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;

  /* Configure baud, number of bits, stop bits, and parity */

  return zynqmp_rpu_uart_configure(priv->uartbase, &priv->config);
#else
  return OK;
#endif
}

/****************************************************************************
 * Name: zynqmp_rpu_shutdown
 *
 * Description:
 *   Disable the UART.  This method is called when the serial
 *   port is closed
 *
 ****************************************************************************/

static void zynqmp_rpu_shutdown(struct uart_dev_s *dev)
{
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;
  uint32_t regval;

  /* Disable receiver and transmitter */

  regval  = zynqmp_rpu_serialin(priv, XUARTPS_CR_OFFSET);
  regval &= ~XUARTPS_CR_EN_DIS_MASK;
  regval |= XUARTPS_CR_RX_DIS | XUARTPS_CR_TX_DIS;
  zynqmp_rpu_serialout(priv, XUARTPS_CR_OFFSET, regval);

  /* Disable all interrupts */

  zynqmp_rpu_disableallints(priv, NULL);
}

/****************************************************************************
 * Name: zynqmp_rpu_attach
 *
 * Description:
 *   Configure the UART to operation in interrupt driven mode.  This method
 *   is called when the serial port is opened.  Normally, this is just after
 *   the setup() method is called, however, the serial console may operate
 *   in a non-interrupt driven mode during the boot phase.
 *
 *   RX and TX interrupts are not enabled when by the attach method (unless
 *   the hardware supports multiple levels of interrupt enabling).  The RX
 *   and TX interrupts are not enabled until the txint() and rxint() methods
 *   are called.
 *
 ****************************************************************************/

static int zynqmp_rpu_attach(struct uart_dev_s *dev)
{
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;
  int ret;

  /* Attach and enable the IRQ */

  ret = irq_attach(priv->irq, zynqmp_rpu_interrupt, dev);
  if (ret == OK)
    {
      /* Enable the interrupt (RX and TX interrupts are still disabled
       * in the UART
       */

      up_enable_irq(priv->irq);
    }

  return ret;
}

/****************************************************************************
 * Name: zynqmp_rpu_detach
 *
 * Description:
 *   Detach UART interrupts.  This method is called when the serial port is
 *   closed normally just before the shutdown method is called.  The
 *   exception is the serial console which is never shutdown.
 *
 ****************************************************************************/

static void zynqmp_rpu_detach(struct uart_dev_s *dev)
{
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;

  up_disable_irq(priv->irq);
  irq_detach(priv->irq);
}

/****************************************************************************
 * Name: zynqmp_rpu_interrupt
 *
 * Description:
 *   This is the common UART interrupt handler.  It will be invoked
 *   when an interrupt received on the device.  It should call
 *   uart_xmitchars or uart_recvchars to perform the appropriate data
 *   transfers.
 *
 ****************************************************************************/

static int zynqmp_rpu_interrupt(int irq, void *context, void *arg)
{
  struct uart_dev_s *dev = (struct uart_dev_s *)arg;
  struct zynqmp_rpu_dev_s *priv;
  uint32_t status;

  DEBUGASSERT(dev != NULL && dev->priv != NULL);
  priv = (struct zynqmp_rpu_dev_s *)dev->priv;

  /* Loop until there are no further pending interrupts */

  for (; ; )
    {
      /* ISR latches events whether or not they are enabled, so only look
       * at the sources that are currently unmasked.
       */

      status = zynqmp_rpu_serialin(priv, XUARTPS_ISR_OFFSET) &
               zynqmp_rpu_serialin(priv, XUARTPS_IMR_OFFSET);
      if (status == 0)
        {
          break;
        }

      /* Acknowledge (write-1-to-clear) before servicing, so that an event
       * arriving while we service this one is latched again and caught on
       * the next pass.
       */

      zynqmp_rpu_serialout(priv, XUARTPS_ISR_OFFSET, status);

      if ((status & (UART_RXINTS | XUARTPS_IXR_RXFULL)) != 0)
        {
          /* Receive data ready... process incoming bytes */

          uart_recvchars(dev);
        }

      if ((status & UART_TXINTS) != 0)
        {
          /* TX FIFO empty... process outgoing bytes */

          uart_xmitchars(dev);
        }
    }

  return OK;
}

/****************************************************************************
 * Name: zynqmp_rpu_ioctl
 *
 * Description:
 *   All ioctl calls will be routed through this method
 *
 ****************************************************************************/

static int zynqmp_rpu_ioctl(struct file *filep, int cmd, unsigned long arg)
{
#if defined(CONFIG_SERIAL_TERMIOS) || defined(CONFIG_SERIAL_TIOCSERGSTRUCT)
  struct inode      *inode = filep->f_inode;
  struct uart_dev_s *dev   = inode->i_private;
#endif
  int                ret   = OK;

  switch (cmd)
    {
#ifdef CONFIG_SERIAL_TIOCSERGSTRUCT
      case TIOCSERGSTRUCT:
        {
          struct zynqmp_rpu_dev_s *user = (struct zynqmp_rpu_dev_s *)arg;

          if (!user)
            {
              ret = -EINVAL;
            }
          else
            {
              memcpy(user, dev->priv, sizeof(struct zynqmp_rpu_dev_s));
            }
        }
        break;
#endif

#ifdef CONFIG_SERIAL_TERMIOS
      case TCGETS:
        {
          struct termios *termiosp = (struct termios *)arg;
          struct zynqmp_rpu_dev_s *priv =
            (struct zynqmp_rpu_dev_s *)dev->priv;

          if (!termiosp)
            {
              ret = -EINVAL;
              break;
            }

          /* Return parity */

          termiosp->c_cflag = ((priv->config.parity != 0) ? PARENB : 0) |
                              ((priv->config.parity == 1) ? PARODD : 0);

          /* Return stop bits */

          termiosp->c_cflag |= (priv->config.stopbits2) ? CSTOPB : 0;

          /* Return baud */

          cfsetispeed(termiosp, priv->config.baud);

          /* Return number of bits */

          switch (priv->config.bits)
            {
              case 6:
                termiosp->c_cflag |= CS6;
                break;

              case 7:
                termiosp->c_cflag |= CS7;
                break;

              default:
              case 8:
                termiosp->c_cflag |= CS8;
                break;
            }
        }
        break;

      case TCSETS:
        {
          struct termios *termiosp = (struct termios *)arg;
          struct zynqmp_rpu_dev_s *priv =
            (struct zynqmp_rpu_dev_s *)dev->priv;
          uint32_t baud;
          uint32_t ints;
          uint8_t parity;
          uint8_t nbits;
          bool stop2;

          if (!termiosp)
            {
              ret = -EINVAL;
              break;
            }

          /* Decode baud. */

          ret = OK;
          baud = cfgetispeed(termiosp);

          /* Decode number of bits.  The Cadence UART supports 6-8 bits. */

          switch (termiosp->c_cflag & CSIZE)
            {
              case CS6:
                nbits = 6;
                break;

              case CS7:
                nbits = 7;
                break;

              case CS8:
                nbits = 8;
                break;

              default:
                ret = -EINVAL;
                break;
            }

          /* Decode parity */

          if ((termiosp->c_cflag & PARENB) != 0)
            {
              parity = (termiosp->c_cflag & PARODD) ? 1 : 2;
            }
          else
            {
              parity = 0;
            }

          /* Decode stop bits */

          stop2 = (termiosp->c_cflag & CSTOPB) != 0;

          /* Verify that all settings are valid before committing */

          if (ret == OK)
            {
              /* Commit */

              priv->config.baud      = baud;
              priv->config.parity    = parity;
              priv->config.bits      = nbits;
              priv->config.stopbits2 = stop2;

              /* effect the changes immediately - note that we do not
               * implement TCSADRAIN / TCSAFLUSH.  Reconfiguring resets both
               * FIFOs, so any bytes still queued in hardware are lost.
               */

              zynqmp_rpu_disableallints(priv, &ints);
              ret = zynqmp_rpu_uart_configure(priv->uartbase, &priv->config);

              /* Restore the interrupt state */

              zynqmp_rpu_restoreuartint(priv, ints);
            }
        }
        break;
#endif /* CONFIG_SERIAL_TERMIOS */

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: zynqmp_rpu_receive
 *
 * Description:
 *   Called (usually) from the interrupt level to receive one
 *   character from the UART.  Error bits associated with the
 *   receipt are provided in the return 'status'.
 *
 ****************************************************************************/

static int zynqmp_rpu_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;

  /* Return the latched error information.  REVISIT: these bits are not
   * tied to a particular byte; RXBS gives per-byte status if needed.
   */

  *status = zynqmp_rpu_serialin(priv, XUARTPS_ISR_OFFSET) & UART_ERRINTS;

  /* Then return the actual received byte */

  return (int)(zynqmp_rpu_serialin(priv, XUARTPS_FIFO_OFFSET) & 0xff);
}

/****************************************************************************
 * Name: zynqmp_rpu_rxint
 *
 * Description:
 *   Call to enable or disable RX interrupts
 *
 ****************************************************************************/

static void zynqmp_rpu_rxint(struct uart_dev_s *dev, bool enable)
{
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;

  if (enable)
    {
      /* Receive an interrupt when the RX FIFO reaches its trigger level,
       * or when the line goes idle with fewer bytes than that waiting.
       */

#ifndef CONFIG_SUPPRESS_SERIAL_INTS
      zynqmp_rpu_serialout(priv, XUARTPS_IER_OFFSET, UART_RXINTS);
#endif
    }
  else
    {
      zynqmp_rpu_serialout(priv, XUARTPS_IDR_OFFSET, UART_RXINTS);
    }
}

/****************************************************************************
 * Name: zynqmp_rpu_rxavailable
 *
 * Description:
 *   Return true if the RX FIFO is not empty
 *
 ****************************************************************************/

static bool zynqmp_rpu_rxavailable(struct uart_dev_s *dev)
{
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;

  return ((zynqmp_rpu_serialin(priv, XUARTPS_SR_OFFSET) &
          XUARTPS_SR_RXEMPTY) == 0);
}

/****************************************************************************
 * Name: zynqmp_rpu_send
 *
 * Description:
 *   This method will send one byte on the UART
 *
 ****************************************************************************/

static void zynqmp_rpu_send(struct uart_dev_s *dev, int ch)
{
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;

  zynqmp_rpu_serialout(priv, XUARTPS_FIFO_OFFSET, (uint32_t)ch);
}

/****************************************************************************
 * Name: zynqmp_rpu_txint
 *
 * Description:
 *   Call to enable or disable TX interrupts
 *
 ****************************************************************************/

static void zynqmp_rpu_txint(struct uart_dev_s *dev, bool enable)
{
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;
  irqstate_t flags;

  flags = enter_critical_section();
  if (enable)
    {
      /* Set to receive an interrupt when the TX FIFO is empty */

#ifndef CONFIG_SUPPRESS_SERIAL_INTS
      zynqmp_rpu_serialout(priv, XUARTPS_IER_OFFSET, UART_TXINTS);

      /* Fake a TX interrupt here by just calling uart_xmitchars() with
       * interrupts disabled (note this may recurse).
       */

      uart_xmitchars(dev);
#endif
    }
  else
    {
      /* Disable the TX interrupt */

      zynqmp_rpu_serialout(priv, XUARTPS_IDR_OFFSET, UART_TXINTS);
    }

  leave_critical_section(flags);
}

/****************************************************************************
 * Name: zynqmp_rpu_txready
 *
 * Description:
 *   Return true if the TX FIFO has room for another byte
 *
 ****************************************************************************/

static bool zynqmp_rpu_txready(struct uart_dev_s *dev)
{
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;

  return ((zynqmp_rpu_serialin(priv, XUARTPS_SR_OFFSET) &
          XUARTPS_SR_TXFULL) == 0);
}

/****************************************************************************
 * Name: zynqmp_rpu_txempty
 *
 * Description:
 *   Return true if the TX FIFO and shift register are both empty
 *
 ****************************************************************************/

static bool zynqmp_rpu_txempty(struct uart_dev_s *dev)
{
  struct zynqmp_rpu_dev_s *priv = (struct zynqmp_rpu_dev_s *)dev->priv;
  uint32_t sr = zynqmp_rpu_serialin(priv, XUARTPS_SR_OFFSET);

  return (sr & XUARTPS_SR_TXEMPTY) != 0 && (sr & XUARTPS_SR_TACTIVE) == 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_serialinit
 *
 * Description:
 *   Register serial console and serial ports.
 *
 ****************************************************************************/

void arm_serialinit(void)
{
  /* Disable all UART interrupts */

  zynqmp_rpu_disableallints(TTYS0_DEV.priv, NULL);
#ifdef TTYS1_DEV
  zynqmp_rpu_disableallints(TTYS1_DEV.priv, NULL);
#endif

#ifdef CONSOLE_DEV
  /* Configure whichever one is the console.  NOTE: This was already done
   * in zynqmp_rpu_lowsetup().
   */

  CONSOLE_DEV.isconsole = true;
  zynqmp_rpu_setup(&CONSOLE_DEV);

  /* Register the console */

  uart_register("/dev/console", &CONSOLE_DEV);
#endif

  /* Register all UARTs */

  uart_register("/dev/ttyS0", &TTYS0_DEV);
#ifdef TTYS1_DEV
  uart_register("/dev/ttyS1", &TTYS1_DEV);
#endif
}

#endif /* USE_SERIALDRIVER */
