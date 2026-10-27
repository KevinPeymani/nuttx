/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_timebase.h
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

#ifndef __ARCH_ARM_SRC_ZYNQMP_RPU_ZYNQMP_RPU_TIMEBASE_H
#define __ARCH_ARM_SRC_ZYNQMP_RPU_ZYNQMP_RPU_TIMEBASE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

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

/****************************************************************************
 * Name: zynqmp_rpu_timebase_init
 *
 * Description:
 *   Start the timebase counter from zero.  Called once from arm_boot().
 *
 ****************************************************************************/

void zynqmp_rpu_timebase_init(void);

/****************************************************************************
 * Name: zynqmp_rpu_timebase
 *
 * Description:
 *   Return the current count of the timebase counter.  It advances at
 *   BOARD_TTC_CLK_FREQUENCY and wraps at 2^32; take differences between
 *   two reads as uint32_t.  Safe from interrupt handlers and with
 *   interrupts disabled.
 *
 ****************************************************************************/

uint32_t zynqmp_rpu_timebase(void);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_ARM_SRC_ZYNQMP_RPU_ZYNQMP_RPU_TIMEBASE_H */
