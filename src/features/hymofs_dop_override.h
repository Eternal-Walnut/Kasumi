/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * HymoFS - dentry_operations shadow overrides for d_path presentation.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module
 * (or linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 *
 * Author: Anatdx
 */
#ifndef _HYMOFS_DOP_OVERRIDE_H
#define _HYMOFS_DOP_OVERRIDE_H

#include <linux/dcache.h>

int hymofs_dop_override_init(void);
void hymofs_dop_override_exit(void);
int hymofs_dop_install(struct dentry *dentry, const char *display_path);
int hymofs_dop_uninstall_path(const char *path);

#endif /* _HYMOFS_DOP_OVERRIDE_H */
