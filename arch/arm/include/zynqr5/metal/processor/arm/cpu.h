/*
 * Copyright (c) 2015, Xilinx Inc. and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Shadow override of libmetal processor/arm/cpu.h for the zynqr5 target.
 *
 * Placed under arch/arm/include/zynqr5/ which is listed first in CFLAGS
 * (-I$(TOPDIR)/arch/arm/include/zynqr5) so this file is found before the
 * downloaded copy in nuttx/include/metal/processor/arm/cpu.h, making the
 * override version-controlled and survive clean builds.
 *
 * When CONFIG_ZYNQR5_RPTUN is set the rptun thread runs at priority 224 and
 * sched_yield() alone never reaches the idle task (priority 0), so IPI
 * checking never happens during the rpmsg_virtio DRIVER_OK busy-spin phase.
 * Polling IPI + timer on every yield lets the spin phase detect A53 kicks.
 */

#ifndef __METAL_ARM_CPU__H__
#define __METAL_ARM_CPU__H__

#ifdef CONFIG_ZYNQR5_RPTUN
#  include <sched.h>
void zynqr5_rptun_ipi_check(void);
void zynqr5_timer_poll(void);
static inline void _zynqr5_metal_yield(void)
{
  zynqr5_rptun_ipi_check();
  zynqr5_timer_poll();
  sched_yield();
}
#  define metal_cpu_yield() _zynqr5_metal_yield()
#else
#  include <sched.h>
#  define metal_cpu_yield() sched_yield()
#endif

#endif /* __METAL_ARM_CPU__H__ */
