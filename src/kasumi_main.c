/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * Kasumi - module metadata and the thin entrypoint that forwards into bootstrap.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module
 * (or linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 *
 * Author: Anatdx
 */
#include <linux/module.h>

#include "kasumi_bootstrap.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anatdx");
MODULE_DESCRIPTION("Kasumi kernel module");
#ifndef KASUMI_VERSION
#define KASUMI_VERSION "0.1.0-dev"
#endif
MODULE_VERSION(KASUMI_VERSION);
MODULE_SOFTDEP("pre: kernelsu");

static int __init kasumi_lkm_init(void)
{
	return kasumi_bootstrap_init();
}

static void __exit kasumi_lkm_exit(void)
{
	kasumi_bootstrap_exit();
}

module_init(kasumi_lkm_init);
module_exit(kasumi_lkm_exit);
