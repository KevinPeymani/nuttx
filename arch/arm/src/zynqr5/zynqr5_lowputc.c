/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_lowputc.c
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

#include <stdint.h>

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 *
 * Cadence UART (PS UART) register map — same for UART0 and UART1.
 *
 ****************************************************************************/

#include <arch/zynqr5/chip.h>

/* Register offsets */

#define UART_CR_OFFSET      0x00U   /* Control */
#define UART_SR_OFFSET      0x2CU   /* Status */
#define UART_FIFO_OFFSET    0x30U   /* TX/RX FIFO */

/* Control register bits */

#define UART_CR_RXRST       (1U << 0)   /* RX logic reset */
#define UART_CR_TXRST       (1U << 1)   /* TX logic reset */
#define UART_CR_RXEN        (1U << 2)   /* RX enable */
#define UART_CR_RXDIS       (1U << 3)   /* RX disable */
#define UART_CR_TXEN        (1U << 4)   /* TX enable */
#define UART_CR_TXDIS       (1U << 5)   /* TX disable */

/* Status register bits */

#define UART_SR_RXEMPTY     (1U << 1)   /* RX FIFO empty */
#define UART_SR_TXFULL      (1U << 4)   /* TX FIFO full */
#define UART_SR_TXEMPTY     (1U << 3)   /* TX FIFO empty */

#ifdef CONFIG_ZYNQR5_UART0_CONSOLE
#  define UART_CONSOLE_BASE ZYNQR5_UART0_BASE
#elif defined(CONFIG_ZYNQR5_UART1_CONSOLE)
#  define UART_CONSOLE_BASE ZYNQR5_UART1_BASE
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zynqr5_lowsetup
 *
 * Description:
 *   Enable TX on the console UART.  The FSBL / U-Boot has already configured
 *   the baud-rate divisors; we only need to make sure the transmitter is on.
 *
 ****************************************************************************/

void zynqr5_lowsetup(void)
{
#ifdef UART_CONSOLE_BASE
  uint32_t cr;

  /* Reset TX, then enable RX + TX */

  cr = getreg32(UART_CONSOLE_BASE + UART_CR_OFFSET);
  cr &= ~(UART_CR_TXDIS | UART_CR_RXDIS);
  cr |= (UART_CR_TXEN | UART_CR_RXEN);
  putreg32(cr, UART_CONSOLE_BASE + UART_CR_OFFSET);
#endif
}

/****************************************************************************
 * Name: arm_lowputc
 *
 * Description:
 *   Polled UART output — used by early syslog/up_putc before the serial
 *   driver is registered.  Blocks until a slot is free in the TX FIFO.
 *
 ****************************************************************************/

void arm_lowputc(char ch)
{
#ifdef UART_CONSOLE_BASE
  /* Wait for TX FIFO not full */

  while (getreg32(UART_CONSOLE_BASE + UART_SR_OFFSET) & UART_SR_TXFULL)
    {
    }

  putreg32((uint32_t)ch, UART_CONSOLE_BASE + UART_FIFO_OFFSET);
#endif
}
