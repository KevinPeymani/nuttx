/****************************************************************************
 * boards/arm/zynqmp-rpu/kv260-rpu/src/zynqmp_rpu_bringup.c
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
#include <syslog.h>

#include <nuttx/fs/fs.h>

#include "kv260-rpu.h"

#ifdef CONFIG_RPTUN
#  include "zynqmp_rpu_rptun.h"
#endif

#ifdef CONFIG_RPMSG_UART_RAW
#  include <nuttx/drivers/drivers.h>
#  include <nuttx/serial/uart_rpmsg_raw.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_serialrawinit
 *
 * Description:
 *   Register the RPMsg console, /dev/ttyRPMSG0 and /dev/console.  Called
 *   by drivers_initialize(); the "rpmsg-tty" endpoint is created when the
 *   rpmsg device to Linux comes up.
 *
 ****************************************************************************/

#ifdef CONFIG_RPMSG_UART_RAW
void rpmsg_serialrawinit(void)
{
  uart_rpmsg_raw_init("linux", "RPMSG0", 4096, true);
}
#endif

/****************************************************************************
 * Name: zynqmp_rpu_bringup
 *
 * Description:
 *   Bring up simulated board features
 *
 ****************************************************************************/

int zynqmp_rpu_bringup(void)
{
  int ret = OK;

#ifdef CONFIG_FS_PROCFS
  /* Mount the procfs file system */

  ret = nx_mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount procfs at /proc: %d\n", ret);
    }
#endif

#ifdef CONFIG_KV260_RPU_RPMSG_ECHO
  /* Register the echo service before the link starts */

  ret = zynqmp_rpu_rpmsg_echo_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize rpmsg echo: %d\n", ret);
    }
#endif

#ifdef CONFIG_RPTUN
  /* Start the RPMsg link to Linux */

  ret = zynqmp_rpu_rptun_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize rptun: %d\n", ret);
    }
#endif

  return ret;
}
