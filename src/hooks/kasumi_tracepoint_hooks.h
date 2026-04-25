/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * Kasumi - tracepoint-based syscall monitoring header.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module
 * (or linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 *
 * Author: Anatdx
 */
#ifndef _KASUMI_TRACEPOINT_HOOKS_H
#define _KASUMI_TRACEPOINT_HOOKS_H

#include <asm/ptrace.h>

int kasumi_tracepoint_path_init(void);
void kasumi_tracepoint_path_exit(void);
int kasumi_tracepoint_path_registered(void);
int kasumi_tracepoint_getfd_registered(void);

#endif /* _KASUMI_TRACEPOINT_HOOKS_H */
