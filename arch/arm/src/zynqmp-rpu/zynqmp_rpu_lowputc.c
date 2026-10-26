/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_lowputc.c
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * SPDX-FileCopyrightText: 2015,2018 Gregory Nutt. All rights reserved.
 * SPDX-FileCopyrightText: 2012 Texas Instruments Incorporated
 * SPDX-FileContributor: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <assert.h>
#include <errno.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <arch/board/board.h>
#include <nuttx/spinlock.h>

#include "arm_internal.h"
#include "hardware/zynqmp_rpu_uart.h"
#include "zynqmp_rpu_lowputc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

/* Select UART parameters for the selected console */

#if defined(CONFIG_UART0_SERIAL_CONSOLE) && defined(CONFIG_ZYNQMP_RPU_UART0)
#  define ZYNQMP_RPU_CONSOLE_BASE     ZYNQMP_RPU_UART0_BASE
#  define ZYNQMP_RPU_CONSOLE_BAUD     CONFIG_UART0_BAUD
#  define ZYNQMP_RPU_CONSOLE_BITS     CONFIG_UART0_BITS
#  define ZYNQMP_RPU_CONSOLE_PARITY   CONFIG_UART0_PARITY
#  define ZYNQMP_RPU_CONSOLE_2STOP    CONFIG_UART0_2STOP
#  define HAVE_SERIAL_CONSOLE         1
#elif defined(CONFIG_UART1_SERIAL_CONSOLE) && defined(CONFIG_ZYNQMP_RPU_UART1)
#  define ZYNQMP_RPU_CONSOLE_BASE     ZYNQMP_RPU_UART1_BASE
#  define ZYNQMP_RPU_CONSOLE_BAUD     CONFIG_UART1_BAUD
#  define ZYNQMP_RPU_CONSOLE_BITS     CONFIG_UART1_BITS
#  define ZYNQMP_RPU_CONSOLE_PARITY   CONFIG_UART1_PARITY
#  define ZYNQMP_RPU_CONSOLE_2STOP    CONFIG_UART1_2STOP
#  define HAVE_SERIAL_CONSOLE         1

/* No UART is the console (for example, the console is on RPMsg): use the
 * first enabled UART for up_putc (syslog and panics).
 */

#elif defined(CONFIG_ZYNQMP_RPU_UART0)
#  define ZYNQMP_RPU_CONSOLE_BASE     ZYNQMP_RPU_UART0_BASE
#  define ZYNQMP_RPU_CONSOLE_BAUD     CONFIG_UART0_BAUD
#  define ZYNQMP_RPU_CONSOLE_BITS     CONFIG_UART0_BITS
#  define ZYNQMP_RPU_CONSOLE_PARITY   CONFIG_UART0_PARITY
#  define ZYNQMP_RPU_CONSOLE_2STOP    CONFIG_UART0_2STOP
#  define HAVE_SERIAL_CONSOLE         1
#elif defined(CONFIG_ZYNQMP_RPU_UART1)
#  define ZYNQMP_RPU_CONSOLE_BASE     ZYNQMP_RPU_UART1_BASE
#  define ZYNQMP_RPU_CONSOLE_BAUD     CONFIG_UART1_BAUD
#  define ZYNQMP_RPU_CONSOLE_BITS     CONFIG_UART1_BITS
#  define ZYNQMP_RPU_CONSOLE_PARITY   CONFIG_UART1_PARITY
#  define ZYNQMP_RPU_CONSOLE_2STOP    CONFIG_UART1_2STOP
#  define HAVE_SERIAL_CONSOLE         1
#else
#  warning "No CONFIG_ZYNQMP_RPU_UARTn Setting"
#  undef HAVE_SERIAL_CONSOLE
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/
#ifdef HAVE_SERIAL_CONSOLE
static spinlock_t g_zynqmp_rpu_lowputc_lock = SP_UNLOCKED;
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef HAVE_SERIAL_CONSOLE
static const struct uart_config_s g_console_config =
{
  .baud       = ZYNQMP_RPU_CONSOLE_BAUD,
  .parity     = ZYNQMP_RPU_CONSOLE_PARITY,
  .bits       = ZYNQMP_RPU_CONSOLE_BITS,
  .stopbits2  = ZYNQMP_RPU_CONSOLE_2STOP,
};
#endif /* HAVE_SERIAL_CONSOLE */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_lowputc
 *
 * Description:
 *   Output one byte on the serial console
 *
 ****************************************************************************/

void arm_lowputc(char ch)
{
#ifdef HAVE_SERIAL_CONSOLE
  irqstate_t flags;

  /* Wait for the transmitter to be available */

  flags = spin_lock_irqsave(&g_zynqmp_rpu_lowputc_lock);

  while ((getreg32(ZYNQMP_RPU_CONSOLE_BASE + XUARTPS_SR_OFFSET) &
    XUARTPS_SR_TXFULL) != 0);

  /* Send the character */

  putreg32((uint32_t)ch, ZYNQMP_RPU_CONSOLE_BASE + XUARTPS_FIFO_OFFSET);

  spin_unlock_irqrestore(&g_zynqmp_rpu_lowputc_lock, flags);
#endif
}

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Provide priority, low-level access to support OS debug writes
 *
 ****************************************************************************/

void up_putc(int ch)
{
#ifdef HAVE_SERIAL_CONSOLE
  arm_lowputc(ch);
#endif
}

/****************************************************************************
 * Name: zynqmp_rpu_lowsetup
 *
 * Description:
 *   This performs basic initialization of the UART used for the serial
 *   console.  Its purpose is to get the console output available as soon
 *   as possible.
 *
 ****************************************************************************/

void zynqmp_rpu_lowsetup(void)
{
#if defined(HAVE_SERIAL_CONSOLE) && !defined(CONFIG_SUPPRESS_UART_CONFIG)
  /* Configure the console (only) */

  zynqmp_rpu_uart_configure(ZYNQMP_RPU_CONSOLE_BASE, &g_console_config);
#endif
}

/****************************************************************************
 * Name: zynqmp_rpu_uart_configure
 *
 * Description:
 *   Configure a UART for non-interrupt driven operation
 *
 ****************************************************************************/

int zynqmp_rpu_uart_configure(uint32_t base,
                              const struct uart_config_s *config)
{
  uint32_t refclk = BOARD_UART_REFCLK_FREQUENCY;
  uint32_t best_cd = 0;
  uint32_t best_bdiv = 0;
  uint32_t best_err = UINT32_MAX;
  uint32_t bdiv;
  uint32_t cd;
  uint32_t actual;
  uint32_t err;
  uint32_t regval;

  DEBUGASSERT(config->baud >= XUARTPS_MIN_RATE &&
              config->baud <= XUARTPS_MAX_RATE);

  /* 1. Disable the transmitter and receiver */

  regval  = getreg32(base + XUARTPS_CR_OFFSET);
  regval &= ~XUARTPS_CR_EN_DIS_MASK;
  regval |= XUARTPS_CR_RX_DIS | XUARTPS_CR_TX_DIS;
  putreg32(regval, base + XUARTPS_CR_OFFSET);

  /* 2. Mask every interrupt source, then clear any latched status */

  putreg32(XUARTPS_IXR_MASK, base + XUARTPS_IDR_OFFSET);
  putreg32(getreg32(base + XUARTPS_ISR_OFFSET), base + XUARTPS_ISR_OFFSET);

  /* 3. Frame format: normal mode, reference clock not divided by 8 */

  regval = XUARTPS_MR_CHMODE_NORM;

  switch (config->bits)
    {
      case 6:
        regval |= XUARTPS_MR_CHARLEN_6_BIT;
        break;

      case 7:
        regval |= XUARTPS_MR_CHARLEN_7_BIT;
        break;

      default:
        DEBUGASSERT(config->bits == 8);
        regval |= XUARTPS_MR_CHARLEN_8_BIT;
        break;
    }

  /* Note that the even-parity encoding is zero; "none" is a set bit */

  switch (config->parity)
    {
      case 1:
        regval |= XUARTPS_MR_PARITY_ODD;
        break;

      case 2:
        regval |= XUARTPS_MR_PARITY_EVEN;
        break;

      default:
        DEBUGASSERT(config->parity == 0);
        regval |= XUARTPS_MR_PARITY_NONE;
        break;
    }

  regval |= config->stopbits2 ? XUARTPS_MR_STOPMODE_2_BIT :
                                XUARTPS_MR_STOPMODE_1_BIT;
  putreg32(regval, base + XUARTPS_MR_OFFSET);

  /* 4. Baud rate = refclk / (CD * (BDIV + 1)).  Search every legal BDIV
   *    (4-254) for the CD (BAUDGEN, 1-65535) giving the smallest error.
   */

  for (bdiv = 4; bdiv < 255 && best_err != 0; bdiv++)
    {
      cd = refclk / (config->baud * (bdiv + 1));
      if (cd == 0 || cd > XUARTPS_BAUDGEN_MASK)
        {
          continue;
        }

      actual = refclk / (cd * (bdiv + 1));
      err    = actual > config->baud ? actual - config->baud :
                                       config->baud - actual;

      if (err < best_err)
        {
          best_err  = err;
          best_cd   = cd;
          best_bdiv = bdiv;
        }
    }

  if (best_cd == 0 ||
      best_err * 100 / config->baud > XUARTPS_MAX_BAUD_ERROR_RATE)
    {
      return -EINVAL;
    }

  putreg32(best_cd, base + XUARTPS_BAUDGEN_OFFSET);
  putreg32(best_bdiv, base + XUARTPS_BAUDDIV_OFFSET);

  /* 5. Reset the TX and RX data paths (flushes both FIFOs).  The reset
   *    bits clear themselves when the reset completes.
   */

  putreg32(XUARTPS_CR_TXRST | XUARTPS_CR_RXRST, base + XUARTPS_CR_OFFSET);
  while ((getreg32(base + XUARTPS_CR_OFFSET) &
          (XUARTPS_CR_TXRST | XUARTPS_CR_RXRST)) != 0)
    {
    }

  /* 6. RX interrupt policy for the serial driver: trigger at 8 bytes in
   *    the FIFO, or after an idle timeout (RXTOUT is in units of 4 bit
   *    periods) so that a short burst is not stranded below the trigger.
   */

  putreg32(8, base + XUARTPS_RXWM_OFFSET);
  putreg32(10, base + XUARTPS_RXTOUT_OFFSET);

  /* 7. Enable TX and RX, restart the RX timeout counter and make sure no
   *    break is being transmitted.
   */

  putreg32(XUARTPS_CR_RX_EN | XUARTPS_CR_TX_EN | XUARTPS_CR_TORST |
           XUARTPS_CR_STOPBRK, base + XUARTPS_CR_OFFSET);

  return OK;
}
