/****************************************************************************
 * arch/arm/src/zynqr5/zynqr5_start.c
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
#include <stdbool.h>

#include <nuttx/init.h>
#include <arch/board/board.h>

#include "arm_internal.h"
#include "zynqr5_start.h"

#ifdef CONFIG_ZYNQR5_ICACHE
#  include "mpu.h"
#  include "cp15_cacheops.h"
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_ZYNQR5_ICACHE

/* Exported by the linker script so the resource table's region does not have
 * to be a hardcoded address: it moves whenever .data changes size. */

extern uint8_t _sresource_table[];
extern uint8_t _eresource_table[];

/****************************************************************************
 * Name: zynqr5_mpu_region
 *
 * Description:
 *   Program one MPU region of exactly (1 << l2size) bytes at base.
 *
 *   base must be aligned to the region size: ARMV7-R leaves an unaligned
 *   region UNPREDICTABLE, and no subregion arithmetic can repair it.  Every
 *   caller here satisfies that by construction, so no subregions are disabled.
 *
 *   cacheable=true  -> Normal, Write-Back, non-shareable (TEX=000 C=1 B=1 S=0)
 *   cacheable=false -> Strongly-Ordered (TEX=000 C=0 B=0 S=0)
 *
 *   The S bit is left clear deliberately.  NuttX's mpu_priv_intsram() sets it,
 *   and this RPU runs non-coherent (no ACP, Coherent bit unset), so on
 *   Cortex-R5 a shareable Normal region is treated as non-cacheable -- it
 *   would enable the cache and change nothing.
 *
 ****************************************************************************/

static void zynqr5_mpu_region(uintptr_t base, uint8_t l2size, bool cacheable)
{
  uint32_t attr = MPU_RACR_AP_RWNO;    /* P:RW U:None, XN clear (executable) */

  if (cacheable)
    {
      attr |= MPU_RACR_C | MPU_RACR_B;
    }

  mpu_set_rgnr(mpu_allocregion());
  mpu_set_drbar(base & MPU_RBAR_ADDR_MASK);
  mpu_set_dracr(attr);
  mpu_set_drsr(MPU_RASR_ENABLE | MPU_RASR_RSIZE_LOG2((uint32_t)l2size));
}

/****************************************************************************
 * Name: zynqr5_cache_enable
 *
 * Description:
 *   Enable the MPU and the L1 caches for the DDR carveout.
 *
 *   This R5F runs its whole ~1.4 MB .text and all of its data from external
 *   DDR.  Until this ran, every access went there uncached:
 *   zynqr5_tcmstart.S clears SCTLR.M to drop the FSBL's MPU regions, and on
 *   ARMv7-R a disabled MPU selects the default memory map, under which the
 *   caches cannot be used at all.
 *
 *   Measured effect, same perf counters before and after:
 *
 *     ekf2 EKF update      2533 us -> 500 us avg,  1280 us -> 307 us min
 *     simulator_sih cycle  8520 us -> 1231 us avg,  518 us ->  89 us min
 *     mavlink tx elapsed   2035 us ->  448 us avg
 *     CPU load              92-98% -> 40%,  commander check now reports OK
 *
 *   Note which half did that.  Enabling the MPU and the I-cache ALONE changed
 *   nothing measurable (ekf2 min 1280 -> 1278 us): instruction fetch was never
 *   the bottleneck.  The entire gain is the D-cache, because the hot working
 *   set is data -- the EKF's 24x24 float covariance matrix, which lives in a
 *   heap allocation (EKF2 is `new`ed, and Ekf is a member) and so could never
 *   have been relocated into TCM by the linker either.  If a future change
 *   appears to buy nothing, measure the minima rather than the load: load
 *   varies ~8 pp between boots of identical firmware and will mislead.
 *
 *   Regions are NON-shareable on purpose.  NuttX's mpu_priv_intsram() sets the
 *   S bit, and this RPU is non-coherent (no ACP, Coherent bit unset), so on
 *   Cortex-R5 a shareable Normal region is treated as non-cacheable -- it would
 *   enable the cache and change nothing.
 *
 *   Everything the A53 also touches is carved back out as Strongly-Ordered
 *   below.  Nothing in the rptun/rpmsg path performs any cache maintenance, so
 *   that list is the only thing keeping shared memory coherent: anything new
 *   shared with Linux must be added to it.  The failure signature when a region
 *   is missed is an R5 that loads, reports "running", and never announces its
 *   rpmsg channels, because rptun is spinning on a stale vdev status.
 *
 ****************************************************************************/

static void zynqr5_cache_enable(void)
{
  uint32_t regval;
  uint8_t  l2size;
  uintptr_t base;
  size_t remaining;
  unsigned int nregions;
  unsigned int region;
  unsigned int regions_done = 0;

  /* Disable every region the FSBL left programmed before enabling the MPU.
   *
   * zynqr5_tcmstart.S only cleared SCTLR.M, which stops the MPU being
   * consulted but leaves the region registers loaded.  Turning the MPU back
   * on would reactivate all of them, and on ARMV7-R a higher-numbered region
   * wins where regions overlap -- so a stale FSBL region could quietly
   * override the carveout mapping below and undo the whole change.
   *
   * NuttX's mpu_reset() is not usable here: arch/arm/src/armv7-r/arm_mpu.c
   * implements it against the memory-mapped MPU_RNR/MPU_RASR/MPU_CTRL
   * registers of a Cortex-M, which are not defined for armv7-r at all, so
   * CONFIG_ARM_MPU_RESET does not even compile on this architecture.  The R5
   * MPU is behind CP15, so do it with the accessors that port does provide.
   */

  nregions = (mpu_get_mpuir() & MPUIR_DREGION_MASK) >> MPUIR_DREGION_SHIFT;

  for (region = 0; region < nregions; region++)
    {
      mpu_set_rgnr(region);
      mpu_set_drsr(0);            /* Clearing the enable bit is sufficient */
    }

  /* Cover the carveout with naturally-aligned power-of-two regions.
   *
   * mpu_control() also sets SCTLR_BR, so every address outside these regions
   * -- ATCM, the vrings, the GIC at 0xF9000000, all MMIO at 0xFF000000+ --
   * keeps the default memory map and therefore exactly the behaviour it had
   * before this function existed.  The TCMs lose nothing by being left out:
   * they are zero-wait-state regardless.
   *
   * Do NOT hand the whole carveout to mpu_log2regionceil()/mpu_subregion().
   * CONFIG_RAM_SIZE is 2 MB, so that asks for a 2 MB region based at
   * 0x1ED00000, which is only 1 MB aligned -- and ARMV7-R requires a region
   * base aligned to the region size or the behaviour is UNPREDICTABLE.
   * mpu_subregion() cannot rescue it either: it computes
   * size + (base & (2 MB - 1)) = 3 MB against a 2 MB region, outside the range
   * its own subregion arithmetic is defined for.  The first attempt at this
   * function did exactly that and measured no change at all, which is what a
   * silently malformed region looks like.
   *
   * So split greedily instead: at each step take the largest power-of-two
   * block that both starts on its own alignment and fits in what is left.
   * For 0x1ED00000 + 2 MB that is exactly two 1 MB regions, each naturally
   * aligned, with no subregion trickery anywhere.
   */

  base      = (uintptr_t)CONFIG_RAM_START;
  remaining = (size_t)CONFIG_RAM_SIZE;

  while (remaining > 0)
    {
      l2size = 5;
      while (l2size < 31 &&
             (base & ((uintptr_t)(1u << (l2size + 1)) - 1)) == 0 &&
             (size_t)(1u << (l2size + 1)) <= remaining)
        {
          l2size++;
        }

      zynqr5_mpu_region(base, l2size, true);

      /* Record the geometry actually programmed.  CONFIG_RAM_SIZE reached this
       * code as 0 once already -- arch/Kconfig declares RAM_SIZE as an int, so
       * the 0x200000 this board's defconfig used parsed to zero and the region
       * silently became the 32-byte minimum.  Nothing about that was visible
       * in a load measurement, so publish the numbers instead of trusting
       * them: 0x1EEFFF80 must read back as 0x0002_0014, two regions of
       * 1 << 20 bytes. */

      base      += (uintptr_t)(1u << l2size);
      remaining -= (size_t)(1u << l2size);
      regions_done++;

      *((volatile uint32_t *)0x1EEFFF80u) =
        ((uint32_t)regions_done << 16) | (uint32_t)l2size;
    }

  /* Carve the memory Linux touches back out as Strongly-Ordered.
   *
   * These come AFTER the cacheable regions on purpose: on ARMV7-R the
   * highest-numbered region wins where regions overlap, and mpu_allocregion()
   * hands out ascending indices, so these override the carveout mapping above
   * rather than being shadowed by it.
   *
   * Do NOT assume that being outside the carveout is enough.  SCTLR_BR leaves
   * unmapped addresses on the ARMV7-R default memory map, and that map marks
   * the whole low 2 GB as Normal Write-Back -- so with SCTLR.C set the vrings
   * would become CACHEABLE by default, not uncached.  Nothing in
   * zynqr5_rptun.c or drivers/rptun/rptun.c performs any cache maintenance
   * (no flush, no invalidate, no libmetal cache hooks are wired up), so there
   * is nothing to make that safe: a dirty vring descriptor would simply never
   * reach the A53.  It happens to work at the time of writing, which is
   * exactly why it gets an explicit region rather than being left to luck.
   */

  /* 1. The resource table.  Linux writes the RSC_VDEV status field while this
   *    core polls it.  Exactly 1 KB, aligned and padded by the linker script.
   */

  zynqr5_mpu_region((uintptr_t)_sresource_table, 10, false);

  /* 2. The RSC_TRACE ring (0x1EEF7E00, 32 KB, read by Linux through
   *    remoteproc's trace0) and the debug scratchpad (0x1EEFFE00-0x1EEFFFFF,
   *    read from the A53 with devmem -- including the SCTLR readback below,
   *    which would otherwise be able to sit dirty in the cache and report a
   *    stale value).  They are contiguous, so one 64 KB region at 0x1EEF0000
   *    covers both and is naturally aligned.
   *
   *    This also makes the top 64 KB of the heap uncacheable, since the heap
   *    runs up to the scratchpad at 0x1EEFFE00.  That is accepted: being
   *    exact would need a second, smaller region and the heap's last 32 KB is
   *    not where the hot data lives.
   */

  zynqr5_mpu_region(0x1EEF0000u, 16, false);

  /* 3. The virtio rings and the rpmsg buffer pool, which the A53 reads and
   *    writes continuously:
   *      0x1EF00000  vring0  16 KB   R5F -> A53
   *      0x1EF04000  vring1  16 KB   A53 -> R5F
   *      0x1EF08000  buffer pool  1 MB
   *    That spans [0x1EF00000, 0x1F008000).  Two naturally-aligned regions
   *    cover it exactly: 1 MB at 0x1EF00000, then 32 KB at 0x1F000000 for the
   *    pool's tail. */

  zynqr5_mpu_region(0x1EF00000u, 20, false);
  zynqr5_mpu_region(0x1F000000u, 15, false);

  mpu_control(true);

  /* Only after the MPU says the region is cacheable is it meaningful to turn
   * the I-cache on.  Invalidate first: nothing guarantees what the FSBL, or a
   * previous remoteproc load of a different image, left in it. */

  cp15_invalidate_icache();
  regval = cp15_rdsctlr();
  cp15_wrsctlr(regval | SCTLR_I);

#ifdef CONFIG_ZYNQR5_DCACHE
  /* Invalidate rather than flush: nothing in the D-cache can be worth keeping
   * at this point (every write so far went straight to DDR with the cache off)
   * and a clean of unknown FSBL-era lines could write garbage back over
   * .data.  Invalidate-all discards them instead. */

  cp15_invalidate_dcache_all();
  regval = cp15_rdsctlr();
  cp15_wrsctlr(regval | SCTLR_C);
#endif

  /* Publish the SCTLR we actually ended up with, read back from the register
   * rather than the value written, so "the cache is on" is a measurement and
   * not an assumption.  Read from the A53 with
   *   devmem 0x1EEFFF7C 32
   * and check: bit 0 (M) = MPU on, bit 2 (C) = D-cache -- must be CLEAR here,
   * bit 12 (I) = I-cache on, bit 17 (BR) = background region on.
   * 0x1EEFFF78 is a fixed magic so a stale scratchpad value from an older boot
   * cannot be mistaken for a live one. */

  *((volatile uint32_t *)0x1EEFFF78u) = 0x5C71A5A5u;
  *((volatile uint32_t *)0x1EEFFF7Cu) = cp15_rdsctlr();
}
#endif /* CONFIG_ZYNQR5_ICACHE */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_boot
 *
 * Description:
 *   Entry point called from arm_head.S after CPU / stack setup.
 *
 *   Boot sequence (CONFIG_ARMV7R_MEMINIT is not set):
 *     arm_head.S → arm_data_initialize() → arm_boot() → nx_start()
 *
 *   arm_head.S already called arm_data_initialize(), so .bss/.data are
 *   valid here.
 *
 *   Nothing in this function can report a failure: the console is not up
 *   until zynqr5_lowsetup() returns, and even then syslog is not running.
 *   The 0xA001xxxx markers written to 0x1EEFFFB0 are therefore the only
 *   evidence of how far early boot progressed, and are read from the A53 with
 *   devmem after a silent hang.  Scratchpad address map:
 *   boards/amd/kria-kv260/nuttx-config/scripts/script.ld.
 *
 ****************************************************************************/

void arm_boot(void)
{
  *((volatile uint32_t *)0x1EEFFFB0u) = 0xA0010001u;  /* reached arm_boot */

  /* Enable the FPU before anything else runs.  PX4 uses floating point
   * throughout, including in code that runs before nx_start() sets up
   * per-task FPU contexts; without this those accesses trap. */

  arm_fpuconfig();

  *((volatile uint32_t *)0x1EEFFFB0u) = 0xA0010002u;  /* FPU enabled */

#ifdef CONFIG_ZYNQR5_ICACHE
  /* Before the console exists, so a hang inside it is only visible through
   * these markers.  Distinct values on both sides: the scratchpad is raw DDR
   * that is never zeroed, so a plausible-looking value may be from an earlier
   * boot -- only a CHANGE between the two proves this code ran. */

  *((volatile uint32_t *)0x1EEFFFB4u) = 0xA0020001u;  /* entering MPU setup */
  zynqr5_cache_enable();
  *((volatile uint32_t *)0x1EEFFFB4u) = 0xA0020002u;  /* MPU + caches on */
#endif

  /* Bring up polled console output.  The FSBL and U-Boot have already
   * programmed the baud-rate divisors, so only the transmitter needs
   * enabling -- reprogramming the divisors here would risk corrupting a UART
   * that Linux is also using. */

  zynqr5_lowsetup();

  zynqr5_board_initialize();

  *((volatile uint32_t *)0x1EEFFFA8u) = 0xBB000001u;  /* entering nx_start */

  nx_start();
}
