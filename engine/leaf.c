/*
 * Copyright (c) 2012-2014 The nessDB Project Developers. All rights reserved.
 * Code is licensed with GPL. See COPYING.GPL file.
 *
 */

#include "tcursor.h"
#include "node.h"

/*
 * apply a msg to leaf basement
 * neither the leaf type is LE_CLEAN or LE_MVCC
 * basement will always keep multi-snapshot
 * the difference between LE_CLEAN and LE_MVCC is gc affects.
 *
 */
int leaf_apply_msg(struct node *leaf, struct bt_cmd *cmd)
{
	write_lock(&leaf->u.l.le->rwlock);
	switch (cmd->type & 0xff) {
	case MSG_INSERT:
	case MSG_DELETE:
	case MSG_UPDATE: {
			/* TODO: do msg update, node->node_op->update */
		}
	case MSG_COMMIT:
	case MSG_ABORT:
	default:
		basement_put(leaf->u.l.le->bsm,
		             cmd->msn,
		             cmd->type,
		             cmd->key,
		             cmd->val,
		             &cmd->xidpair);
	}
	leaf->msn = cmd->msn > leaf->msn ? cmd->msn : leaf->msn;
	node_set_dirty(leaf);
	write_unlock(&leaf->u.l.le->rwlock);

	return NESS_OK;
}

/*
 * TODO: (BohuTANG) to do gc on MVCC
 * a) if a commit txid(with the same key) is smaller than other, gc it
 */
int leaf_do_gc(struct node *leaf)
{
	(void)leaf;

	return NESS_OK;
}

/*
 * apply parent's [leaf, right] messages to child node
 */
void _apply_msg_to_child(struct node *parent,
                         int child_num,
                         struct node *child,
                         struct msg *left,
                         struct msg *right)
{
	int height;
	struct basement *bsm;
	struct basement_iter iter;

	nassert(child != NULL);
	nassert(parent->height > 0);

	height = child->height;
	if (height == 0)
		bsm = child->u.l.le->bsm;
	else
		bsm = child->u.n.parts[child_num].buffer;

	basement_iter_init(&iter, bsm);
	basement_iter_seek(&iter, left);

	while (basement_iter_valid_lessorequal(&iter, right)) {
		struct bt_cmd cmd = {
			.msn = iter.msn,
			.type = iter.type,
			.key = &iter.key,
			.val = &iter.val,
			.xidpair = iter.xidpair
		};

		if (nessunlikely(height == 0))
			leaf_put_cmd(child, &cmd);
		else
			nonleaf_put_cmd(child, &cmd);
	}
}

/*
 * apply msgs from ances to leaf basement which are between(include) left and right
 * REQUIRES:
 *  1) leaf write-lock
 *  2) ances all write-lock
 */
int leaf_apply_ancestors(struct node *leaf, struct ancestors *ances)
{
	struct ancestors *ance;
	struct msg *left = NULL;
	struct msg *right = NULL;
	struct basement_iter iter;
	struct basement *bsm = leaf->u.l.le->bsm;

	basement_iter_init(&iter, bsm);
	basement_iter_seektofirst(&iter);
	if (basement_iter_valid(&iter))
		left = msgdup(&iter.key);

	basement_iter_seektolast(&iter);
	if (basement_iter_valid(&iter))
		right = msgdup(&iter.key);

	ance = ances;
	while (ance && ance->next) {
		/* apply [leaf, right] to leaf */
		_apply_msg_to_child(ance->v,
		                    ance->childnum,
		                    ance->next->v,
		                    left,
		                    right);
		ance = ances->next;
	}

	msgfree(left);
	msgfree(right);

	return NESS_OK;
}
