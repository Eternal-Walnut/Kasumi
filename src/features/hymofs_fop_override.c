/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * HymoFS - directory file_operations shadow installation for fast readdir hooks.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module
 * (or linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 *
 * Author: Anatdx
 */
#include "hymofs_entrypoints.h"
#include "hymofs_fop_override.h"
#include "hymofs_runtime.h"
#include "hymofs_sop_override.h"
#include "hymofs_vfs_hooks.h"

#include <linux/hashtable.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define HYMO_FOP_HASH_BITS 10

struct hymo_fop_meta {
	struct inode *inode;
	const struct file_operations *orig_fop;
	struct file_operations shadow_fop;
	struct hlist_node node;
	struct rcu_head rcu;
};

static DEFINE_HASHTABLE(hymo_fop_table, HYMO_FOP_HASH_BITS);
static DEFINE_SPINLOCK(hymo_fop_lock);
static bool hymo_fop_override_ready;

static struct hymo_fop_meta *hymo_fop_lookup_rcu(struct inode *inode)
{
	struct hymo_fop_meta *m;

	hash_for_each_possible_rcu(hymo_fop_table, m, node, (unsigned long)inode) {
		if (m->inode == inode)
			return m;
	}
	return NULL;
}

static void hymo_fop_meta_free_rcu(struct rcu_head *rcu)
{
	struct hymo_fop_meta *m = container_of(rcu, struct hymo_fop_meta, rcu);

	kfree(m);
}

static void hymo_fop_put_orig(const struct file_operations *fop)
{
	if (fop && fop->owner)
		module_put(fop->owner);
}

HYMO_NOCFI static int hymo_shadow_iterate_shared(struct file *file,
						 struct dir_context *ctx)
{
	struct hymofs_filldir_wrapper *wrapper;
	struct hymo_fop_meta *m;
	const struct file_operations *orig = NULL;
	int ret;

	if (!file)
		return -EINVAL;

	rcu_read_lock();
	m = hymo_fop_lookup_rcu(file_inode(file));
	if (m)
		orig = m->orig_fop;
	rcu_read_unlock();

	if (!orig || !orig->iterate_shared)
		return -ENOTDIR;

	atomic64_inc(&hymo_hook_stats.iterate_fop_entries);

	wrapper = hymofs_iterate_prepare_wrapper(file, ctx);
	if (!wrapper)
		return orig->iterate_shared(file, ctx);

	atomic64_inc(&hymo_hook_stats.iterate_fop_wrapped);
	ret = orig->iterate_shared(file, &wrapper->wrap_ctx);
	hymofs_iterate_finish_wrapper(wrapper);
	return ret;
}

bool hymofs_fop_file_is_shadowed(const struct file *file)
{
	struct hymo_fop_meta *m;
	bool shadowed = false;

	if (!file)
		return false;

	rcu_read_lock();
	m = hymo_fop_lookup_rcu(file_inode(file));
	if (m && READ_ONCE(file->f_op) == &m->shadow_fop)
		shadowed = true;
	rcu_read_unlock();

	return shadowed;
}

int hymofs_fop_install(struct inode *inode)
{
	struct hymo_fop_meta *m, *existing;
	const struct file_operations *orig;

	if (!READ_ONCE(hymo_fop_override_ready))
		return -EOPNOTSUPP;
	if (!inode || !inode->i_mapping || !S_ISDIR(inode->i_mode))
		return -EINVAL;
	if (!inode->i_sb)
		return -EINVAL;
	if (test_bit(AS_FLAGS_HYMO_FOP_INSTALLED, &inode->i_mapping->flags))
		return 0;
	if (hymofs_sop_install(inode->i_sb))
		return -EOPNOTSUPP;

	orig = READ_ONCE(inode->i_fop);
	if (!orig || !orig->iterate_shared)
		return -EOPNOTSUPP;
	if (orig->owner && !try_module_get(orig->owner))
		return -ENODEV;

	m = kzalloc(sizeof(*m), GFP_ATOMIC);
	if (!m) {
		hymo_fop_put_orig(orig);
		return -ENOMEM;
	}

	m->inode = inode;
	m->orig_fop = orig;
	memcpy(&m->shadow_fop, orig, sizeof(struct file_operations));
	m->shadow_fop.owner = THIS_MODULE;
	m->shadow_fop.iterate_shared = hymo_shadow_iterate_shared;

	spin_lock(&hymo_fop_lock);
	existing = hymo_fop_lookup_rcu(inode);
	if (existing) {
		spin_unlock(&hymo_fop_lock);
		hymo_fop_put_orig(orig);
		kfree(m);
		return 0;
	}
	if (READ_ONCE(inode->i_fop) != orig) {
		spin_unlock(&hymo_fop_lock);
		hymo_fop_put_orig(orig);
		kfree(m);
		return -EAGAIN;
	}

	hash_add_rcu(hymo_fop_table, &m->node, (unsigned long)inode);
	set_bit(AS_FLAGS_HYMO_FOP_INSTALLED, &inode->i_mapping->flags);
	smp_wmb();
	WRITE_ONCE(inode->i_fop, &m->shadow_fop);
	spin_unlock(&hymo_fop_lock);

	hymo_log("fop_override: installed on inode %p (orig=%p)\n", inode, orig);
	return 0;
}

static void hymo_fop_uninstall_locked(struct inode *inode)
{
	struct hymo_fop_meta *m;

	m = hymo_fop_lookup_rcu(inode);
	if (!m)
		return;

	if (inode->i_fop == &m->shadow_fop)
		WRITE_ONCE(inode->i_fop, m->orig_fop);

	hash_del_rcu(&m->node);
	if (inode->i_mapping)
		clear_bit(AS_FLAGS_HYMO_FOP_INSTALLED, &inode->i_mapping->flags);
	hymo_fop_put_orig(m->orig_fop);
	call_rcu(&m->rcu, hymo_fop_meta_free_rcu);
}

void hymofs_fop_cleanup_inode(struct inode *inode)
{
	if (!inode)
		return;

	spin_lock(&hymo_fop_lock);
	hymo_fop_uninstall_locked(inode);
	spin_unlock(&hymo_fop_lock);
}

int hymofs_fop_override_init(void)
{
	hash_init(hymo_fop_table);
	WRITE_ONCE(hymo_fop_override_ready, true);
	pr_info("HymoFS: fop_override initialized (cleanup via sop_override)\n");
	return 0;
}

void hymofs_fop_override_exit(void)
{
	struct hymo_fop_meta *m;
	struct hlist_node *tmp;
	int bkt;

	WRITE_ONCE(hymo_fop_override_ready, false);

	spin_lock(&hymo_fop_lock);
	hash_for_each_safe(hymo_fop_table, bkt, tmp, m, node) {
		if (m->inode && m->inode->i_fop == &m->shadow_fop)
			WRITE_ONCE(m->inode->i_fop, m->orig_fop);
		if (m->inode && m->inode->i_mapping)
			clear_bit(AS_FLAGS_HYMO_FOP_INSTALLED,
				  &m->inode->i_mapping->flags);
		hash_del_rcu(&m->node);
		hymo_fop_put_orig(m->orig_fop);
		call_rcu(&m->rcu, hymo_fop_meta_free_rcu);
	}
	spin_unlock(&hymo_fop_lock);

	rcu_barrier();
	pr_info("HymoFS: fop_override exited\n");
}
