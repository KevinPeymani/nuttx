/****************************************************************************
 * boards/arm/zynqmp-rpu/kv260-rpu/src/zynqmp_rpu_rpmsg_echo.c
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

/* RPMsg echo service for testing the link to Linux.
 *
 * When the RPMsg device to Linux comes up, create an endpoint named
 * "rpmsg-raw".  NuttX announces it through the name service, Linux creates
 * a channel for it, and Linux's rpmsg_char driver binds to that name and
 * exposes it as /dev/rpmsgN.  Every message received on the endpoint is
 * sent back to its sender unchanged.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/rpmsg/rpmsg.h>

#include "kv260-rpu.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Peer CPU name, as returned by the rptun driver's get_cpuname */

#define ECHO_CPUNAME   "linux"

/* Linux's rpmsg_char driver binds to channels with this name */

#define ECHO_EPT_NAME  "rpmsg-raw"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct rpmsg_endpoint g_echo_ept;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zynqmp_rpu_rpmsg_echo_ept_cb
 *
 * Description:
 *   Send each received message back to the endpoint it came from.  This
 *   runs on the high-priority work queue (see zynqmp_rpu_rptun.c), so it
 *   may block while it waits for a TX buffer.
 *
 ****************************************************************************/

static int zynqmp_rpu_rpmsg_echo_ept_cb(struct rpmsg_endpoint *ept,
                                        void *data, size_t len,
                                        uint32_t src, void *priv)
{
  int ret;

  ret = rpmsg_sendto(ept, data, len, src);
  if (ret < 0)
    {
      syslog(LOG_ERR, "rpmsg echo: send to 0x%" PRIx32 " failed: %d\n",
             src, ret);
    }

  return 0;
}

/****************************************************************************
 * Name: zynqmp_rpu_rpmsg_echo_device_created
 *
 * Description:
 *   Called by the rpmsg core when an RPMsg device is created, and for any
 *   device that already exists when the callback is registered.
 *
 ****************************************************************************/

static void zynqmp_rpu_rpmsg_echo_device_created(struct rpmsg_device *rdev,
                                                 void *priv)
{
  int ret;

  if (strcmp(rpmsg_get_cpuname(rdev), ECHO_CPUNAME) != 0)
    {
      return;
    }

  ret = rpmsg_create_ept(&g_echo_ept, rdev, ECHO_EPT_NAME,
                         RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                         zynqmp_rpu_rpmsg_echo_ept_cb, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "rpmsg echo: failed to create endpoint: %d\n", ret);
    }
}

/****************************************************************************
 * Name: zynqmp_rpu_rpmsg_echo_device_destroy
 ****************************************************************************/

static void zynqmp_rpu_rpmsg_echo_device_destroy(struct rpmsg_device *rdev,
                                                 void *priv)
{
  if (strcmp(rpmsg_get_cpuname(rdev), ECHO_CPUNAME) != 0)
    {
      return;
    }

  rpmsg_destroy_ept(&g_echo_ept);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zynqmp_rpu_rpmsg_echo_initialize
 *
 * Description:
 *   Register the echo service with the rpmsg core.
 *
 ****************************************************************************/

int zynqmp_rpu_rpmsg_echo_initialize(void)
{
  return rpmsg_register_callback(NULL,
                                 zynqmp_rpu_rpmsg_echo_device_created,
                                 zynqmp_rpu_rpmsg_echo_device_destroy,
                                 NULL, NULL);
}
