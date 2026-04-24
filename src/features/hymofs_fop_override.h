/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * HymoFS - directory file_operations shadow overrides for readdir filtering.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module
 * (or linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 *
 * Author: Anatdx
 */
#ifndef _HYMOFS_FOP_OVERRIDE_H
#define _HYMOFS_FOP_OVERRIDE_H

#include <linux/fs.h>

int hymofs_fop_override_init(void);
void hymofs_fop_override_exit(void);

int hymofs_fop_install(struct inode *inode);
bool hymofs_fop_file_is_shadowed(const struct file *file);
void hymofs_fop_cleanup_inode(struct inode *inode);

#endif /* _HYMOFS_FOP_OVERRIDE_H */
