/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_start.h
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

#ifndef __ARCH_ARM_SRC_ZYNQR5_ZYNQR5_START_H
#define __ARCH_ARM_SRC_ZYNQR5_ZYNQR5_START_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Early UART console setup — called from arm_boot() before nx_start() */

void zynqr5_lowsetup(void);

/* Board-specific early init — called from arm_boot() before nx_start() */

void zynqr5_board_initialize(void);

#endif /* __ARCH_ARM_SRC_ZYNQR5_ZYNQR5_START_H */
