/*
 * Memory de-allocation
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "memory_handling.h"

static void _free_basil_processor(struct basil_node_processor *p)
{
	if (p) {
		_free_basil_processor(p->next);
		p->rsvn_id = 0; /* just to be safe */
		xfree(p);
	}
}

static void _free_basil_mem_alloc(struct basil_mem_alloc *m)
{
	if (m) {
		_free_basil_mem_alloc(m->next);
		xfree(m);
	}
}

static void _free_basil_memory(struct basil_node_memory *m)
{
	if (m) {
		_free_basil_memory(m->next);
		_free_basil_mem_alloc(m->a_head);
		xfree(m);
	}
}

static void _free_basil_label(struct basil_label *l)
{
	if (l) {
		_free_basil_label(l->next);
		xfree(l);
	}
}

static void _free_basil_accel(struct basil_node_accelerator *a)
{
	if (a) {
		_free_basil_accel(a->next);
		xfree(a->allocation);
		xfree(a);
	}
}
static void _free_basil_segment(struct basil_segment *s)
{
	if (s) {
		_free_basil_segment(s->next);
		_free_basil_processor(s->proc_head);
		_free_basil_memory(s->mem_head);
		_free_basil_label(s->lbl_head);
		xfree(s);
	}
}

static void _free_basil_node(struct basil_node *n)
{
	if (n) {
		_free_basil_node(n->next);
		_free_basil_accel(n->accel_head);
		_free_basil_segment(n->seg_head);
		xfree(n);
	}
}

static void _free_basil_rsvn_cmd(struct basil_rsvn_app_cmd *c)
{
	if (c) {
		_free_basil_rsvn_cmd(c->next);
		xfree(c);
	}
}

static void _free_basil_rsvn_app(struct basil_rsvn_app *a)
{
	if (a) {
		_free_basil_rsvn_app(a->next);
		_free_basil_rsvn_cmd(a->cmd_head);
		xfree(a);
	}
}

static void _free_basil_rsvn(struct basil_rsvn *r)
{
	if (r) {
		_free_basil_rsvn(r->next);
		_free_basil_rsvn_app(r->app_head);
		xfree(r);
	}
}

/*
 * Reservation parameters
 */
static void _rsvn_free_param_mem(struct basil_memory_param *m)
{
	if (m) {
		_rsvn_free_param_mem(m->next);
		xfree(m);
	}
}

static void _rsvn_free_param_accel(struct basil_accel_param *a)
{
	if (a) {
		_rsvn_free_param_accel(a->next);
		xfree(a);
	}
}

void free_inv(struct basil_inventory *inv)
{
	if (inv) {
		if (inv->f) {
			_free_basil_node(inv->f->node_head);
			_free_basil_rsvn(inv->f->rsvn_head);
			xfree(inv->f);
		}
		xfree(inv);
	}
}

/*
 * Node-specifier lists
 */
void free_nodespec(struct nodespec *ns)
{
	if (ns) {
		free_nodespec(ns->next);
		xfree(ns);
	}
}

void rsvn_free_param(struct basil_rsvn_param *p)
{
	if (p) {
		rsvn_free_param(p->next);
		_rsvn_free_param_mem(p->memory);
		_rsvn_free_param_accel(p->accel);
		_free_basil_label(p->labels);
		xfree(p->nodes);
		xfree(p);
	}
}

void free_rsvn(struct basil_reservation *r)
{
	if (r) {
		rsvn_free_param(r->params);
		free_nodespec(r->rsvd_nodes);
		xfree(r);
	}
}
