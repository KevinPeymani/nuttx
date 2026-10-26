/****************************************************************************
 * boards/arm/zynqmp-rpu/kv260-rpu/include/board.h
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

#ifndef __BOARDS_ARM_ZYNQMP_RPU_KV260_RPU_INCLUDE_BOARD_H
#define __BOARDS_ARM_ZYNQMP_RPU_KV260_RPU_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#  include <stdbool.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Clocking *****************************************************************/

/* PS UART reference clock (UARTx_REF_CTRL, sourced from IOPLL).  The PMU
 * firmware / Linux sets it up; the RPU only reads it.  Verify against
 * uart*_ref in /sys/kernel/debug/clk/clk_summary on the board, and against
 * the QEMU device tree for emulation.
 */

#define BOARD_UART_REFCLK_FREQUENCY 100000000

/* TTC counter clock.  With Clock_Control.C_Src = 0 the counters run from
 * the TTC APB interface clock, which IOU_SLCR.IOU_TTC_APB_CLK selects
 * (reset value: LPD_LSBUS_CLK).  Verify against lpd_lsbus in
 * /sys/kernel/debug/clk/clk_summary on the board.
 *
 * Xilinx QEMU's TTC model ignores the clock in its hardware device tree
 * and counts at a fixed 133 MHz.
 */

#ifdef CONFIG_KV260_RPU_QEMU
#  define BOARD_TTC_CLK_FREQUENCY 133000000
#else
#  define BOARD_TTC_CLK_FREQUENCY 100000000
#endif

/* FLASH wait states */

#define BOARD_ASWAIT           0   /* No address setup wait states */
#define BOARD_RWAIT            1   /* One read access wait state */
#define BOARD_EWAIT            4   /* Four wait states for EEPROM access */

/* PIN Multiplexor Initializer **********************************************/

/* You may specify one alternative from each set (the first is the default
 * and, hence, could probably be omitted):
 *
 *  1. {GIOA0, SPI3nCS3}
 *  2. {GIOA1, SPI3nCS2}
 *  3. {GIOA2, SPI3nCS1}
 *  4. {GIOA3, SPI2nCS3}
 *  5. {GIOA4, SPI2nCS2}
 *  6. {GIOA5, EXTCLKIN}
 *  7. {GIOA6, SPI2nCS1, N2HET31}
 *  8. {GIOA7, N2HET29}
 *  9. {MIBSPI1nCS2, N2HET20, N2HET19}
 * 10. {SPI3CLK, EQEPA}
 * 11. {SPI3nENA, EQEPB}
 * 12. {SPI3nCS0, EQEPI}}
 * 13. {MIBSPI1nCS3, N2HET26}
 * 14. {ADEVT, N2HET28}
 * 15. {MIBSPI1nENA, N2HET23, NHET30}
 * 16. {MIBSPI1nCS1, EQEPS, N2HET17}
 */

#define BOARD_PINMUX_INITIALIZER \
  PINMUX_GIOA0_PIN, \
  PINMUX_GIOA1_PIN, \
  PINMUX_GIOA2_PIN, \
  PINMUX_GIOA3_PIN, \
  PINMUX_GIOA4_PIN, \
  PINMUX_GIOA5_PIN, \
  PINMUX_GIOA6_PIN, \
  PINMUX_GIOA7_PIN, \
  PINMUX_MIBSPI1NCS2_PIN, \
  PINMUX_SPI3CLK_PIN, \
  PINMUX_SPI3NENA_PIN, \
  PINMUX_SPI3NCS0_PIN, \
  PINMUX_MIBSPI1NCS3_PIN, \
  PINMUX_ADEVT_PIN, \
  PINMUX_MIBSPI1NENA_PIN, \
  PINMUX_MIBSPI1NCS1_PIN

/* LED definitions **********************************************************/

/* LEDs
 *
 * The launchpad has several LEDs:
 *
 *   - LEd D1 (white) that connects to the USB +5V supply,
 *   - LED D10 (red) that connects to the TMS570's NERROR pin,
 *   - D5 (blue), D6 (blue), and D8 (blue) connect to the XDS100 FT2322,
 *   - D7 (blue) connects to the XSD100 CPLD, and
 *   - Two white, user LEDs labeled D12 that connects to the NHET08
 *     pin and D11 that connects to GIOA2.
 *
 * NHET08 is one of 32 N2HET pins than can be available to the user if not
 * used by N2HET.
 * This implementation, however, uses only the single LED driven by GIOA2.
 * That LED is tied to ground and illuminated with a high level output value.
 */

/* LED index values for use with board_userled() */

#define BOARD_LED_D11        0
#define BOARD_NLEDS          1

/* LED bits for use with board_userled_all() */

#define BOARD_LED_D11_BIT    (1 << BOARD_LED_D11)

/* This LED is not used by the board port unless CONFIG_ARCH_LEDS is
 * defined.  In that case, the usage by the board port is defined in
 * include/board.h and src/zynqmp_rpu_autoleds.c. The LED is used to encode
 * OS-related events as follows:
 */

/* These LEDs are not used by the board port unless CONFIG_ARCH_LEDS is
 * defined.  In that case, the usage by the board port is defined in
 * include/board.h and src/sam_autoleds.c. The LEDs are used to encode
 * OS-related events as follows:
 *
 *   ---------------------- ---------------------------- ------
 *   SYMBOL                 Meaning                      LED
 *   ---------------------- ---------------------------- ------
 */

#define LED_STARTED         0 /* NuttX has been started  OFF      */
#define LED_HEAPALLOCATE    0 /* Heap has been allocated OFF      */
#define LED_IRQSENABLED     0 /* Interrupts enabled      OFF      */
#define LED_STACKCREATED    1 /* Idle stack created      ON       */
#define LED_INIRQ           2 /* In an interrupt         N/C      */
#define LED_SIGNAL          2 /* In a signal handler     N/C      */
#define LED_ASSERTION       2 /* An assertion failed     N/C      */
#define LED_PANIC           3 /* The system has crashed  FLASH    */
#undef  LED_IDLE              /* MCU is in sleep mode    Not used */

/* Thus if the LED is statically on, NuttX has successfully  booted and is,
 * apparently, running normally.  If the LED is flashing at approximately
 * 2Hz, then a fatal error has been detected and the system has halted.
 */

/* Button definitions *******************************************************/

/* Buttons
 *
 *  The launchpad has three mechanical buttons.
 *  Two of these are reset buttons:
 *  One  button is labeled PORRST performs a power-on reset and one labeled
 *  RST performs an MCU reset.
 *  Only one button is available for general software usage.
 *  That button is labeled GIOA7 and is, obviously, sensed on GIOA7.
 *
 * GIOA7 is tied to ground, but will be pulled high if the GIOA7 button is
 * depressed.
 */

#define BUTTON_GIOA7        0
#define NUM_BUTTONS         1

#define BUTTON_GIOA7_BIT    (1 << BUTTON_GIOA7)

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __BOARDS_ARM_ZYNQMP_RPU_KV260_RPU_INCLUDE_BOARD_H */
