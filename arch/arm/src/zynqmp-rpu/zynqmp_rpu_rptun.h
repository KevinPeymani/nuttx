/****************************************************************************
 * arch/arm/src/zynqmp-rpu/zynqmp_rpu_rptun.h
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

#ifndef __ARCH_ARM_SRC_ZYNQMP_RPU_ZYNQMP_RPU_RPTUN_H
#define __ARCH_ARM_SRC_ZYNQMP_RPU_ZYNQMP_RPU_RPTUN_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

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
 * Name: zynqmp_rpu_rptun_initialize
 *
 * Description:
 *   Register the RPMsg link to Linux with rptun.  Linux is the remoteproc
 *   master: it loads and starts the R5 and owns the vrings.  The link uses
 *   IPI channel 1 (RPU0) to receive from and kick Linux on channel 7.
 *
 *   Must be called after the work queues have started, for example from
 *   board_late_initialize().
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

#ifdef CONFIG_RPTUN
int zynqmp_rpu_rptun_initialize(void);
#endif

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_ARM_SRC_ZYNQMP_RPU_ZYNQMP_RPU_RPTUN_H */
