/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0 */
/*
 * HymoFS - dentry_operations shadow installation for trap-free d_path spoofing.
 *
 * License: Author's work under Apache-2.0; when used as a kernel module
 * (or linked with the Linux kernel), GPL-2.0 applies for kernel compatibility.
 *
 * Author: Anatdx
 */
#include "hymofs_dop_override.h"
#include "hymofs_runtime.h"

#include <linux/err.h>
#include <linux/hashtable.h>
#include <linux/namei.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define HYMO_DOP_HASH_BITS 10

struct hymo_dop_meta {
	struct dentry *dentry;
	const struct dentry_operations *orig_dop;
	struct dentry_operations shadow_dop;
	char *display_path;
	struct hlist_node node;
	struct rcu_head rcu;
};

static DEFINE_HASHTABLE(hymo_dop_table, HYMO_DOP_HASH_BITS);
static DEFINE_SPINLOCK(hymo_dop_lock);
static bool hymo_dop_ready;

static struct hymo_dop_meta *hymo_dop_lookup_rcu(struct dentry *dentry)
{
	struct hymo_dop_meta *m;

	hash_for_each_possible_rcu(hymo_dop_table, m, node, (unsigned long)dentry) {
		if (m->dentry == dentry)
			return m;
	}
	return NULL;
}

static void hymo_dop_meta_free_rcu(struct rcu_head *rcu)
{
	struct hymo_dop_meta *m = container_of(rcu, struct hymo_dop_meta, rcu);

	kfree(m->display_path);
	kfree(m);
}

static char *hymo_shadow_dname(struct dentry *dentry, char *buf, int buflen)
{
	struct hymo_dop_meta *m;
	const char *display = NULL;
	size_t len;
	char *out;

	atomic64_inc(&hymo_hook_stats.dop_dname_entries);

	rcu_read_lock();
	m = hymo_dop_lookup_rcu(dentry);
	if (m)
		display = READ_ONCE(m->display_path);
	if (!display) {
		rcu_read_unlock();
		return ERR_PTR(-ENOENT);
	}

	len = strlen(display) + 1;
	if (len > (size_t)buflen) {
		rcu_read_unlock();
		return ERR_PTR(-ENAMETOOLONG);
	}
	out = buf + buflen - len;
	memcpy(out, display, len);
	rcu_read_unlock();
	return out;
}

int hymofs_dop_install(struct dentry *dentry, const char *display_path)
{
	struct hymo_dop_meta *m, *existing;
	const struct dentry_operations *orig;
	char *old_display = NULL;

	if (!READ_ONCE(hymo_dop_ready))
		return -EOPNOTSUPP;
	if (!dentry || !display_path || display_path[0] != '/')
		return -EINVAL;

	orig = READ_ONCE(dentry->d_op);
	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->display_path = kstrdup(display_path, GFP_KERNEL);
	if (!m->display_path) {
		kfree(m);
		return -ENOMEM;
	}
	m->dentry = dget(dentry);
	m->orig_dop = orig;
	if (orig)
		memcpy(&m->shadow_dop, orig, sizeof(struct dentry_operations));
	m->shadow_dop.d_dname = hymo_shadow_dname;

	spin_lock(&hymo_dop_lock);
	existing = hymo_dop_lookup_rcu(dentry);
	if (existing) {
		old_display = existing->display_path;
		WRITE_ONCE(existing->display_path, m->display_path);
		spin_unlock(&hymo_dop_lock);
		dput(m->dentry);
		kfree(m);
		synchronize_rcu();
		kfree(old_display);
		return 0;
	}
	if (READ_ONCE(dentry->d_op) != orig) {
		spin_unlock(&hymo_dop_lock);
		dput(m->dentry);
		kfree(m->display_path);
		kfree(m);
		return -EAGAIN;
	}
	hash_add_rcu(hymo_dop_table, &m->node, (unsigned long)dentry);
	smp_wmb();
	WRITE_ONCE(dentry->d_op, &m->shadow_dop);
	spin_unlock(&hymo_dop_lock);

	hymo_log("dop_override: installed on dentry %p -> %s\n", dentry, display_path);
	return 0;
}

static struct hymo_dop_meta *hymo_dop_uninstall_locked(struct dentry *dentry)
{
	struct hymo_dop_meta *m;

	m = hymo_dop_lookup_rcu(dentry);
	if (!m)
		return NULL;

	if (dentry->d_op == &m->shadow_dop)
		WRITE_ONCE(dentry->d_op, m->orig_dop);
	hash_del_rcu(&m->node);
	return m;
}

int hymofs_dop_uninstall_path(const char *path)
{
	struct path p;
	int ret;

	if (!path || !hymo_kern_path)
		return -EINVAL;
	ret = hymo_kern_path(path, LOOKUP_FOLLOW, &p);
	if (ret)
		return ret;
	spin_lock(&hymo_dop_lock);
	{
		struct hymo_dop_meta *m = hymo_dop_uninstall_locked(p.dentry);

		spin_unlock(&hymo_dop_lock);
		if (m) {
			dput(m->dentry);
			call_rcu(&m->rcu, hymo_dop_meta_free_rcu);
		}
	}
	path_put(&p);
	return 0;
}

int hymofs_dop_override_init(void)
{
	hash_init(hymo_dop_table);
	WRITE_ONCE(hymo_dop_ready, true);
	pr_info("HymoFS: dop_override initialized\n");
	return 0;
}

void hymofs_dop_override_exit(void)
{
	struct hymo_dop_meta *m;
	struct hlist_node *tmp;
	HLIST_HEAD(free_list);
	int bkt;

	WRITE_ONCE(hymo_dop_ready, false);

	spin_lock(&hymo_dop_lock);
	hash_for_each_safe(hymo_dop_table, bkt, tmp, m, node) {
		if (m->dentry && m->dentry->d_op == &m->shadow_dop)
			WRITE_ONCE(m->dentry->d_op, m->orig_dop);
		hash_del_rcu(&m->node);
		hlist_add_head(&m->node, &free_list);
	}
	spin_unlock(&hymo_dop_lock);

	hlist_for_each_entry_safe(m, tmp, &free_list, node) {
		hlist_del(&m->node);
		dput(m->dentry);
		call_rcu(&m->rcu, hymo_dop_meta_free_rcu);
	}

	rcu_barrier();
	pr_info("HymoFS: dop_override exited\n");
}
