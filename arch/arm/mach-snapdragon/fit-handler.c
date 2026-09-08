// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <atf_common.h>
#include <image.h>
#include <log.h>
#include <spl.h>
#include <linux/err.h>
#include <mach/qclib.h>
#include <mach/spl.h>

#define TOC_FDT				"toc_fdt"

/*
 * Global TOC FDT load address populated by qcom_spl_get_toc_fdt_address
 * Placed in .data section to ensure it persists
 */
static u64 g_toc_fdt_address __section(".data");

/**
 * qcom_spl_get_fit_img_entry_point() - Get entry point from FIT image node.
 * @fit:	 Pointer to the FIT image blob.
 * @node:	 Node ID within the FIT image.
 * @entry_point: Pointer to store the retrieved entry point.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int qcom_spl_get_fit_img_entry_point(void *fit, int node,
				     u64 *entry_point)
{
	int ret;

	if (!fit) {
		pr_err("FIT image blob is NULL\n");
		return -EINVAL;
	}
	if (node <= 0) {
		pr_err("Invalid FIT node ID %d\n", node);
		return -EINVAL;
	}
	if (!entry_point) {
		pr_err("Entry point pointer is NULL\n");
		return -EINVAL;
	}

	ret = fit_image_get_entry(fit, node, (ulong *)entry_point);
	if (ret) {
		pr_debug("No entry point for node %d, trying load address\n",
			 node);
		ret = fit_image_get_load(fit, node, (ulong *)entry_point);
		if (ret)
			pr_err("No load address for node %d (%d)\n", node, ret);
	}

	return ret;
}

/**
 * qcom_spl_get_iftbl_entry_by_name() - Get an interface table entry by name.
 * @if_tbl:	Pointer to the QCLIB interface table.
 * @name:	Name of the entry to find.
 * @entry:	Pointer to a buffer where the found entry will be copied.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int qcom_spl_get_iftbl_entry_by_name(struct interface_table *if_tbl,
				     char *name,
				     struct interface_table_entry *entry)
{
	uint uc_index;

	if (!if_tbl) {
		pr_err("Invalid interface table\n");
		return -EINVAL;
	}
	if (!name) {
		pr_err("Invalid name\n");
		return -EINVAL;
	}
	if (!entry) {
		pr_err("Invalid entry pointer\n");
		return -EINVAL;
	}

	for (uc_index = 0; uc_index < if_tbl->num_entries; uc_index++) {
		if (!strcmp(if_tbl->if_table_entries[uc_index].entry_name, name)) {
			memcpy(entry,
			       &if_tbl->if_table_entries[uc_index],
			       sizeof(struct interface_table_entry));
			return 0;
		}
	}
	pr_err("Interface table entry '%s' not found\n", name);

	return -ENOENT;
}

/**
 * qcom_spl_get_toc_fdt_address() - Look up the TOC FDT load address in the FIT
 *
 * Finds the optional "toc_fdt" image node in the FIT at
 * CONFIG_SPL_LOAD_FIT_ADDRESS and records its load address in
 * g_toc_fdt_address, for later use by bl2_plat_get_bl31_params_v2().
 *
 * This is independent of qclib_post_process_from_spl(): the TOC FDT is
 * already loaded into memory by the generic FIT loadables path, so its
 * address only needs to be looked up, not computed by QCLIB.
 */
static void qcom_spl_get_toc_fdt_address(void)
{
	int ret;
	int images_node;
	int toc_fdt_node;
	const void *fit = (const void *)CONFIG_SPL_LOAD_FIT_ADDRESS;

	images_node = fdt_subnode_offset(fit, 0, "images");
	if (images_node < 0) {
		pr_err("Failed to find images node in FIT\n");
		return;
	}

	/*
	 * This node is optional: boards whose FIT image carries no TOC FDT
	 * for BL31 are unaffected and g_toc_fdt_address remains 0.
	 */
	toc_fdt_node = fdt_subnode_offset(fit, images_node, TOC_FDT);
	if (toc_fdt_node < 0) {
		pr_debug("No '%s' node in FIT, BL31 will not receive a TOC FDT address\n",
			 TOC_FDT);
		return;
	}

	ret = qcom_spl_get_fit_img_entry_point((void *)fit, toc_fdt_node,
					       &g_toc_fdt_address);
	if (ret)
		pr_warn("Failed to get '%s' load address (%d)\n", TOC_FDT, ret);
	else
		printf("TOC FDT address: 0x%lx\n", (unsigned long)g_toc_fdt_address);
}

/**
 * bl2_plat_get_bl31_params_v2() - Retrieve and fixup BL31 parameters.
 * @bl32_entry:	Entry point for BL32 (OP-TEE).
 * @bl33_entry:	Entry point for BL33 (U-Boot/kernel).
 * @fdt_addr:	Address of the Device Tree Blob (FDT).
 *
 * Return: Pointer to the populated BL31 parameters structure.
 */
struct bl_params *bl2_plat_get_bl31_params_v2(uintptr_t bl32_entry,
					      uintptr_t bl33_entry,
					      uintptr_t fdt_addr)
{
	struct bl_params *bl_params;
	struct bl_params_node *node;
	u64 qcsdi_address = qclib_get_qcsdi_address();

	/*
	 * Populate the bl31 params with default values.
	 */
	bl_params = bl2_plat_get_bl31_params_v2_default(bl32_entry, bl33_entry,
							fdt_addr);

	/*
	 * The TOC FDT is loaded into memory by the generic FIT loadables
	 * path regardless of QCOM_BOOT_FROM_PBL, so its load address is
	 * looked up here rather than in qclib_post_process_from_spl().
	 */
	qcom_spl_get_toc_fdt_address();

	/*
	 * Fixup the bl31 params based on platform requirements.
	 */
	for_each_bl_params_node(bl_params, node) {
		if (node->image_id == ATF_BL31_IMAGE_ID) {
			/*
			 * Pass QCSDI address to BL31 via arg0
			 * This address was populated by qcom_spl_invoke_qclib()
			 */
			if (qcsdi_address == 0)
				pr_warn("QCSDI address not set, BL31 may not function correctly\n");

			node->ep_info->args.arg0 = qcsdi_address;
			pr_debug("Setting BL31 arg0 to QCSDI address: 0x%llx\n", qcsdi_address);
		} else if (node->image_id == ATF_BL32_IMAGE_ID) {
			/*
			 * Pass TOC FDT load address to BL32 via arg0
			 */
			if (g_toc_fdt_address == 0)
				pr_debug("TOC FDT address not set, BL32 will get arg0=0\n");

			node->ep_info->args.arg0 = g_toc_fdt_address;
			printf("TOC FDT address passed to BL32 (arg0): 0x%lx\n",
			       (unsigned long)node->ep_info->args.arg0);
		}
	}

	return bl_params;
}
