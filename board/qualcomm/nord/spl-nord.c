// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <hang.h>
#include <cpu_func.h>
#include <event.h>
#include <init.h>
#include <image.h>
#include <spl.h>
#include <spl_load.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <soc/qcom/smem.h>
#include <atf_common.h>
#include <linux/err.h>
#include <dm/device-internal.h>
#include <part.h>
#include <blk.h>
#include <dm/uclass.h>
#include <mach/spl.h>

DECLARE_GLOBAL_DATA_PTR;

/**
 * spl_boot_device() - Report the boot device for nord-ride.
 *
 * nord-ride always boots the SPL FIT image from a UFS raw partition
 * rather than via PBL, so the boot device is fixed.
 *
 * Return: BOOT_DEVICE_UFS
 */
u32 spl_boot_device(void)
{
	return BOOT_DEVICE_UFS;
}

#if defined(CONFIG_SPL_BUILD)
/**
 * board_init_f() - Main entry point for SPL.
 * @dummy:	Dummy argument (unused).
 */
void board_init_f(ulong dummy)
{
	int ret = 0;

	memset(__bss_start, 0, __bss_end - __bss_start); /* Clear BSS */

	qcom_spl_malloc_init_f();

	ret = spl_early_init();
	if (ret) {
		pr_debug("spl_early_init() failed (%d)\n", ret);
		goto fail;
	}

	event_notify_null(EVT_LAST_STAGE_INIT);

	preloader_console_init();


	board_init_r(NULL, 0);

fail:
	if (ret)
		reset_cpu();
}
#endif /* CONFIG_SPL_BUILD */
