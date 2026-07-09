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
 * With GIC-based IPI handling (CONFIG_ARMV7R_HAVE_GICv2), the GIC ISR
 * delivers A53 kicks asynchronously — no manual IPI polling needed in the
 * metal_cpu_yield() spin loops.  sched_yield() is sufficient: it gives the
 * scheduler a chance to run and briefly re-enables interrupts between
 * iterations, allowing the GIC ISR to fire and update shared-memory state.
 */

#ifndef __METAL_ARM_CPU__H__
#define __METAL_ARM_CPU__H__

#include <sched.h>
#define metal_cpu_yield() sched_yield()

#endif /* __METAL_ARM_CPU__H__ */
