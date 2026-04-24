/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * HymoFS - super_operations shadow installation for trap-free inode cleanup.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module
 * (or linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 *
 * Author: Anatdx
 */
#include "hymofs_fop_override.h"
#include "hymofs_iop_override.h"
#include "hymofs_runtime.h"
#include "hymofs_sop_override.h"

#include <linux/hashtable.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define HYMO_SOP_HASH_BITS 8

struct hymo_sop_meta {
	struct super_block *sb;
	const struct super_operations *orig_sop;
	struct super_operations shadow_sop;
	struct hlist_node node;
	struct rcu_head rcu;
};

static DEFINE_HASHTABLE(hymo_sop_table, HYMO_SOP_HASH_BITS);
static DEFINE_SPINLOCK(hymo_sop_lock);
static bool hymo_sop_ready;

static struct hymo_sop_meta *hymo_sop_lookup_rcu(struct super_block *sb)
{
	struct hymo_sop_meta *m;

	hash_for_each_possible_rcu(hymo_sop_table, m, node, (unsigned long)sb) {
		if (m->sb == sb)
			return m;
	}
	return NULL;
}

static void hymo_sop_meta_free_rcu(struct rcu_head *rcu)
{
	struct hymo_sop_meta *m = container_of(rcu, struct hymo_sop_meta, rcu);

	kfree(m);
}

static void hymo_shadow_destroy_inode(struct inode *inode)
{
	struct hymo_sop_meta *m;
	const struct super_operations *orig = NULL;

	atomic64_inc(&hymo_hook_stats.sop_destroy_inode);
	hymofs_iop_cleanup_inode(inode);
	hymofs_fop_cleanup_inode(inode);

	if (!inode || !inode->i_sb)
		return;

	rcu_read_lock();
	m = hymo_sop_lookup_rcu(inode->i_sb);
	if (m)
		orig = m->orig_sop;
	rcu_read_unlock();

	if (orig && orig->destroy_inode)
		orig->destroy_inode(inode);
}

int hymofs_sop_install(struct super_block *sb)
{
	struct hymo_sop_meta *m, *existing;
	const struct super_operations *orig;

	if (!READ_ONCE(hymo_sop_ready))
		return -EOPNOTSUPP;
	if (!sb || !sb->s_op)
		return -EINVAL;

	orig = READ_ONCE(sb->s_op);

	m = kzalloc(sizeof(*m), GFP_ATOMIC);
	if (!m)
		return -ENOMEM;

	m->sb = sb;
	m->orig_sop = orig;
	memcpy(&m->shadow_sop, orig, sizeof(struct super_operations));
	m->shadow_sop.destroy_inode = hymo_shadow_destroy_inode;
	if (!orig->destroy_inode && !orig->free_inode)
		m->shadow_sop.free_inode = free_inode_nonrcu;

	spin_lock(&hymo_sop_lock);
	existing = hymo_sop_lookup_rcu(sb);
	if (existing) {
		spin_unlock(&hymo_sop_lock);
		kfree(m);
		return 0;
	}
	if (READ_ONCE(sb->s_op) != orig) {
		spin_unlock(&hymo_sop_lock);
		kfree(m);
		return -EAGAIN;
	}

	hash_add_rcu(hymo_sop_table, &m->node, (unsigned long)sb);
	smp_wmb();
	WRITE_ONCE(sb->s_op, &m->shadow_sop);
	spin_unlock(&hymo_sop_lock);

	hymo_log("sop_override: installed on sb %p (orig=%p)\n", sb, orig);
	return 0;
}

int hymofs_sop_override_init(void)
{
	hash_init(hymo_sop_table);
	WRITE_ONCE(hymo_sop_ready, true);
	pr_info("HymoFS: sop_override initialized\n");
	return 0;
}

void hymofs_sop_override_exit(void)
{
	struct hymo_sop_meta *m;
	struct hlist_node *tmp;
	int bkt;

	WRITE_ONCE(hymo_sop_ready, false);

	spin_lock(&hymo_sop_lock);
	hash_for_each_safe(hymo_sop_table, bkt, tmp, m, node) {
		if (m->sb && m->sb->s_op == &m->shadow_sop)
			WRITE_ONCE(m->sb->s_op, m->orig_sop);
		hash_del_rcu(&m->node);
		call_rcu(&m->rcu, hymo_sop_meta_free_rcu);
	}
	spin_unlock(&hymo_sop_lock);

	rcu_barrier();
	pr_info("HymoFS: sop_override exited\n");
}
