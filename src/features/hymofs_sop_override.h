/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * HymoFS - super_operations shadow overrides for per-inode hook cleanup.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module
 * (or linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 *
 * Author: Anatdx
 */
#ifndef _HYMOFS_SOP_OVERRIDE_H
#define _HYMOFS_SOP_OVERRIDE_H

#include <linux/fs.h>

int hymofs_sop_override_init(void);
void hymofs_sop_override_exit(void);
int hymofs_sop_install(struct super_block *sb);

#endif /* _HYMOFS_SOP_OVERRIDE_H */
