/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * Kasumi - SELinux inode SID overrides for trap-free security.selinux spoofing.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module
 * (or linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 *
 * Author: Anatdx
 */
#ifndef _KASUMI_XATTR_SID_OVERRIDE_H
#define _KASUMI_XATTR_SID_OVERRIDE_H

#include <linux/fs.h>

int kasumi_xattr_sid_override_init(void);
void kasumi_xattr_sid_override_exit(void);
int kasumi_xattr_sid_install(struct inode *target_inode, const char *source_path);
int kasumi_xattr_sid_uninstall_path(const char *path);

#endif /* _KASUMI_XATTR_SID_OVERRIDE_H */
