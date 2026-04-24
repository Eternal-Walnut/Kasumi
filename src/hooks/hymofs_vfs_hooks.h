/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * HymoFS - VFS hook lifecycle interfaces for path, stat, xattr, and iterate flows.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module
 * (or linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 *
 * Author: Anatdx
 */
#ifndef _HYMOFS_VFS_HOOKS_H
#define _HYMOFS_VFS_HOOKS_H

#include <linux/types.h>

struct dir_context;
struct file;
struct hymofs_filldir_wrapper;

int hymofs_vfs_hooks_init(bool skip_vfs);
void hymofs_vfs_hooks_exit(bool skip_vfs);
struct hymofs_filldir_wrapper *hymofs_iterate_prepare_wrapper(struct file *file,
							      struct dir_context *orig_ctx);
void hymofs_iterate_finish_wrapper(struct hymofs_filldir_wrapper *wrapper);

#endif /* _HYMOFS_VFS_HOOKS_H */
