/*
 * tmem.h
 *
 * Transcendent memory
 *
 * Copyright (c) 2009-2011, Dan Magenheimer, Oracle Corp.
 * Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 */

#ifndef _TMEM_H_
#define _TMEM_H_

#include <linux/types.h>
#include <linux/highmem.h>
#include <linux/hash.h>
#include <linux/atomic.h>
#include <linux/fmem.h>

#define TMEM_PUT_PAGE			4
#define TMEM_GET_PAGE			5
#define TMEM_FLUSH_PAGE			6
#define TMEM_FLUSH_OBJECT		7
#define TMEM_POOL_PERSIST		1
#define TMEM_POOL_SHARED		2
#define TMEM_POOL_PRECOMPRESSED		4
#define TMEM_POOL_PAGESIZE_SHIFT	4
#define TMEM_POOL_PAGESIZE_MASK		0xf
#define TMEM_POOL_RESERVED_BITS		0x00ffff00

#define SENTINELS
#ifdef SENTINELS
#define DECL_SENTINEL uint32_t sentinel;
#define SET_SENTINEL(_x, _y) (_x->sentinel = _y##_SENTINEL)
#define INVERT_SENTINEL(_x, _y) (_x->sentinel = ~_y##_SENTINEL)
#define ASSERT_SENTINEL(_x, _y) WARN_ON(_x->sentinel != _y##_SENTINEL)
#define ASSERT_INVERTED_SENTINEL(_x, _y) WARN_ON(_x->sentinel != ~_y##_SENTINEL)
#else
#define DECL_SENTINEL
#define SET_SENTINEL(_x, _y) do { } while (0)
#define INVERT_SENTINEL(_x, _y) do { } while (0)
#define ASSERT_SENTINEL(_x, _y) do { } while (0)
#define ASSERT_INVERTED_SENTINEL(_x, _y) do { } while (0)
#endif


#define TMEM_HASH_BUCKET_BITS	8
#define TMEM_HASH_BUCKETS	(1<<TMEM_HASH_BUCKET_BITS)

struct tmem_hashbucket {
	struct rb_root obj_rb_root;
	spinlock_t lock;
};

struct tmem_pool {
	void *client; 
	struct list_head pool_list;
	uint32_t pool_id;
	bool persistent;
	bool shared;
	atomic_t obj_count;
	atomic_t refcount;
	struct tmem_hashbucket hashbucket[TMEM_HASH_BUCKETS];
	DECL_SENTINEL
};

#define is_persistent(_p)  (_p->persistent)
#define is_ephemeral(_p)   (!(_p->persistent))


struct tmem_oid {
	uint64_t oid[3];
};

static inline void tmem_oid_set_invalid(struct tmem_oid *oidp)
{
	oidp->oid[0] = oidp->oid[1] = oidp->oid[2] = -1UL;
}

static inline bool tmem_oid_valid(struct tmem_oid *oidp)
{
	return oidp->oid[0] != -1UL || oidp->oid[1] != -1UL ||
		oidp->oid[2] != -1UL;
}

static inline int tmem_oid_compare(struct tmem_oid *left,
					struct tmem_oid *right)
{
	int ret;

	if (left->oid[2] == right->oid[2]) {
		if (left->oid[1] == right->oid[1]) {
			if (left->oid[0] == right->oid[0])
				ret = 0;
			else if (left->oid[0] < right->oid[0])
				ret = -1;
			else
				return 1;
		} else if (left->oid[1] < right->oid[1])
			ret = -1;
		else
			ret = 1;
	} else if (left->oid[2] < right->oid[2])
		ret = -1;
	else
		ret = 1;
	return ret;
}

static inline unsigned tmem_oid_hash(struct tmem_oid *oidp)
{
	return hash_long(oidp->oid[0] ^ oidp->oid[1] ^ oidp->oid[2],
				TMEM_HASH_BUCKET_BITS);
}


struct tmem_objnode;

struct tmem_obj {
	struct tmem_oid oid;
	struct tmem_pool *pool;
	struct rb_node rb_tree_node;
	struct tmem_objnode *objnode_tree_root;
	unsigned int objnode_tree_height;
	unsigned long objnode_count;
	long pampd_count;
	void *extra; 
	DECL_SENTINEL
};

#define OBJNODE_TREE_MAP_SHIFT 6
#define OBJNODE_TREE_MAP_SIZE (1UL << OBJNODE_TREE_MAP_SHIFT)
#define OBJNODE_TREE_MAP_MASK (OBJNODE_TREE_MAP_SIZE-1)
#define OBJNODE_TREE_INDEX_BITS (8  * sizeof(unsigned long))
#define OBJNODE_TREE_MAX_PATH \
		(OBJNODE_TREE_INDEX_BITS/OBJNODE_TREE_MAP_SHIFT + 2)

struct tmem_objnode {
	struct tmem_obj *obj;
	DECL_SENTINEL
	void *slots[OBJNODE_TREE_MAP_SIZE];
	unsigned int slots_in_use;
};

struct tmem_pamops {
	void *(*create)(char *, size_t, bool, int,
			struct tmem_pool *, struct tmem_oid *, uint32_t);
	int (*get_data)(char *, size_t *, bool, void *, struct tmem_pool *,
				struct tmem_oid *, uint32_t);
	int (*get_data_and_free)(char *, size_t *, bool, void *,
				struct tmem_pool *, struct tmem_oid *,
				uint32_t);
	void (*free)(void *, struct tmem_pool *, struct tmem_oid *, uint32_t);
	void (*free_obj)(struct tmem_pool *, struct tmem_obj *);
	bool (*is_remote)(void *);
	void (*new_obj)(struct tmem_obj *);
	int (*replace_in_obj)(void *, struct tmem_obj *);
};
extern void tmem_register_pamops(struct tmem_pamops *m);

struct tmem_hostops {
	struct tmem_obj *(*obj_alloc)(struct tmem_pool *);
	void (*obj_free)(struct tmem_obj *, struct tmem_pool *);
	struct tmem_objnode *(*objnode_alloc)(struct tmem_pool *);
	void (*objnode_free)(struct tmem_objnode *, struct tmem_pool *);
	void (*flush_all_obj)(void);
	void (*control)(bool);
};
extern void tmem_register_hostops(struct tmem_hostops *m);

extern int tmem_put(struct tmem_pool *, struct tmem_oid *, uint32_t index,
			char *, size_t, bool, bool);
extern int tmem_get(struct tmem_pool *, struct tmem_oid *, uint32_t index,
			char *, size_t *, bool, int);
extern int tmem_replace(struct tmem_pool *, struct tmem_oid *, uint32_t index,
			void *);
extern int tmem_flush_page(struct tmem_pool *, struct tmem_oid *,
			uint32_t index);
extern int tmem_flush_object(struct tmem_pool *, struct tmem_oid *);
extern int tmem_destroy_pool(struct tmem_pool *);
extern int tmem_flush_pool(struct tmem_pool *);
extern void tmem_new_pool(struct tmem_pool *, uint32_t);

extern void tmem_enable(void);
extern void tmem_disable(void);
#endif 
