/*****************************************************************************\
 *  extra_constraints.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Marshall Garey <marshall@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <string.h>

#include "src/common/extra_constraints.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"

#define _DEBUG 0 /* Set this to non-zero to see detailed debugging */

#define OP_BEGIN OP_CHILD_AND

typedef enum {
	CMP_INVALID = -2,
	CMP_LT = -1,
	CMP_EQ = 0,
	CMP_GT = 1
} cmp_t;

typedef struct {
	op_t op;
	char *op_str;
} op_tbl_t;

#define CHILDREN_LEN 2 /* Starting length of the children array */

static op_tbl_t op_table[] = {
	{ OP_NONE, NULL },
	{ OP_CHILD_AND, "&" },
	{ OP_CHILD_AND_COMMA, "," },
	{ OP_CHILD_OR, "|" },
	{ OP_LEAF_EQ, "=" },
	{ OP_LEAF_NE, "!=" },
	{ OP_LEAF_GT, ">" },
	{ OP_LEAF_GTE, ">=" },
	{ OP_LEAF_LT, "<" },
	{ OP_LEAF_LTE, "<=" },
};
static const int op_table_len = ARRAY_SIZE(op_table);

static const char *child_op_chars = ",&|";
static const char *leaf_op_chars = "<>=!";
static const char *op_chars = ",&|<>=!";

static bool extra_constraints_parsing = false;

static char *_op2str(op_t op)
{
	return op_table[op].op_str;
}

static void _element2str(elem_t *el, int indent, char **str, char **pos)
{
	char *newline = *pos ? "\n" : "";

	if (el->children) {
		xstrfmtcatat(*str, pos, "%s%*s{key:\"%s\", value:\"%s\", operator:\"%s\"(%d), num_children:%d, children:%p}",
			     newline, indent, "", el->key, el->value,
			     op_table[el->operator].op_str, el->operator,
			     el->num_children, el->children);
	} else {
		xstrfmtcatat(*str, pos, "%s%*s{key:\"%s\", value:\"%s\", operator:\"%s\"(%d)}",
			     newline, indent, "", el->key, el->value,
			     op_table[el->operator].op_str, el->operator);
	}
}

static void _tree2str_recursive(elem_t *el, int indent, char **str, char **pos)
{
	if (!el)
		return;
	if (!el->num_children) {
		/* leaf */
		_element2str(el, indent, str, pos);
		return;
	}
	xassert(el->children);
	_element2str(el, indent, str, pos);
	for (int i = 0; i < el->num_children; i++) {
		_tree2str_recursive(el->children[i], indent + 4, str, pos);
	}
}

extern char *extra_constraints_2str(elem_t *el)
{
	char *pos = NULL;
	char *output_str = NULL;

	_tree2str_recursive(el, 0, &output_str, &pos);
	return output_str;
}

#if _DEBUG
static void _log_element(elem_t *el)
{
	char *str = NULL;
	char *pos = NULL;

	_element2str(el, 0, &str, &pos);
	info("%s", str);
	xfree(str);
}
#endif

static void _free_null_elem(elem_t **el)
{
	if (*el) {
		xfree((*el)->children);
		xfree((*el)->key);
		xfree((*el)->value);
		xfree((*el));
	}
}
#define _free_element(el) _free_null_elem(&el)

extern bool extra_constraints_enabled(void)
{
	return extra_constraints_parsing;
}

extern void extra_constraints_free_null(elem_t **el)
{
	if (!(*el))
		return;
	if (!(*el)->num_children) {
		/* leaf */
		_free_element(*el);
		return;
	} else {
		xassert((*el)->children);
		for (int i = 0; i < (*el)->num_children; i++) {
			_free_element((*el)->children[i]);
		}
		_free_element((*el));
	}
	xfree((*el));
}

static elem_t *_alloc_tree(void)
{
	return xmalloc(sizeof(elem_t));
}

static void _add_child(elem_t *parent, elem_t *child)
{
	int num_children;
	int curr_max;

	xassert(parent);
	xassert(child);

	num_children = parent->num_children;
	curr_max = parent->curr_max_children;
	if (!parent->children) {
		parent->children =
			xcalloc(CHILDREN_LEN,
				sizeof(*parent->children));
		curr_max = CHILDREN_LEN;
	} else if (num_children == curr_max) {
		curr_max = num_children * 2;
		xrecalloc(parent->children, curr_max,
			  sizeof(*parent->children));
	}
	parent->children[num_children] = child;
	parent->num_children++;
	parent->curr_max_children = curr_max;
}

/*
 * Given a string and a set of valid operator characters, return the matching
 * operator. Also set *end_out to point to the first invalid character.
 * Return OP_NONE if the operator was not found.
 */
static op_t _str2op(char *str, const char *valid_chars, char **end_out)
{
	op_t op = OP_NONE;
	char save_char;
	char *end = str;

	xassert(strchr(valid_chars, *str));

	while (*end) {
		if (!strchr(valid_chars, *end))
			break;
		end++;
	}
	save_char = *end;
	*end = '\0';

	for (int i = OP_BEGIN; i < op_table_len; i++) {
		op_t tmp = op_table[i].op;
		if (!xstrcmp(str, _op2str(tmp))) {
			op = tmp;
			break;
		}
	}
	/* Automatically convert ',' to '&' */
	if (op == OP_CHILD_AND_COMMA)
		op = OP_CHILD_AND;

	*end = save_char;
	*end_out = end;
	return op;
}

static char *_find_op_in_string(char *str)
{
	char *p;

	if (!str)
		return NULL;

	while (*str) {
		if ((p = strchr(op_chars, *str)))
			return p;
		str++;
	}
	return NULL;
}

/*
 * Leaf:
 * <key><op><value>
 *
 * Return: SLURM_SUCCESS or SLURM_ERROR
 */
static elem_t *_parse_leaf(char *str)
{
	char *key;
	char *val = NULL;
	char *op_ptr = NULL;
	op_t op;
	elem_t *leaf;

	if (!str)
		return NULL;

	/* This is not a leaf if there are paren */
	xassert(!strchr(str, '(') && !strchr(str, ')'));

	if (*str == '\0') {
#if _DEBUG
		error("Leaf is empty");
#endif
		return NULL;
	}

	key = xstrdup(str);

	/* Find the first leaf operator character */
	op_ptr = key;
	while (*op_ptr) {
		if (strchr(leaf_op_chars, *op_ptr))
			break;
		op_ptr++;
	}
	if (*op_ptr == '\0') {
#if _DEBUG
		error("Could not find a leaf operator \"%s\" in \"%s\"",
		      leaf_op_chars, str);
#endif
		xfree(key);
		return NULL;
	}

	/* Get the operator from the string and a pointer to value */
	op = _str2op(op_ptr, leaf_op_chars, &val);

	if (op == OP_NONE) {
		/*
		 * The strchr check verified that an operator
		 * character exists, but not that the whole
		 * operator string is valid. For example, there
		 * could be repeating operator characters:
		 * strchr would return a pointer to the first
		 * one, but _str2op would correctly identify
		 * that the operator is invalid.
		 */
#if _DEBUG
		{
			char save_char;
			save_char = *val;
			*val = '\0';
			error("Invalid operator string: \"%s\"", op_ptr);
			*val = save_char;
		}
#endif
		xfree(key);
		return NULL;
	}

	/* NULL-terminate key */
	*op_ptr = '\0';

	/* Check for invalid characters in key and value: operators */
	if (_find_op_in_string(key) || (_find_op_in_string(val))) {
#if _DEBUG
		error("Invalid key-op-value: %s", str);
#endif
		xfree(key);
		return NULL;
	}

	/* Build an element */
	leaf = xmalloc(sizeof(*leaf));
	leaf->operator = op;
	leaf->key = key; /* Already malloc'd */
	leaf->value = xstrdup(val);

#if _DEBUG
	_log_element(leaf);
#endif

	return leaf;
}

static char *_find_leaf_end(char *str)
{
	char *ptr = str;

	xassert(str);

	/* None of the following characters are allowed in a leaf */
	while (*ptr) {
		if (strchr(child_op_chars, *ptr) ||
		    (*ptr == '(') || (*ptr == ')'))
			break;
		ptr++;
	}
	return ptr;
}

/*
 * Make sure that all children have an operator between them.
 */
static bool _valid_parent_child_op(elem_t *parent)
{
	if (parent->num_children && (parent->operator == OP_NONE)) {
#if _DEBUG
		error("No child operator between children");
#endif
		return false;
	}
	return true;
}

/*
 * Parse a string like the following:
 *   (a=23&(b<=42|c=foo)&d>50)&e=bar
 *
 * Parentheses denote a level of the tree.
 * There are two kinds of operators: child and leaf operators. They are defined
 * in op_table.
 * Any particular level of the tree has only one child operator.
 * Leaves are:
 *   <key><leaf_op><value>
 *
 * Operators are not allowed in a key or value.
 *
 * The following should succeed:
 *
 * a=1
 * a=1,b=2
 * a=3&(b=asdf|c<24)
 * (a=1|(b>=2))
 * zed<yam,(a=23&(b<=42|c=foo)&d>50)&e=bar
 *
 * Spaces are allowed and are considered part of the string:
 * a=   b
 *
 *
 * The following should fail:
 *
 * Invalid leaf operator (',')
 * a,<=6
 *
 * Trailing operator:
 * a<=6<=
 *
 * Multiple child operators in a row:
 * a=5&&&b=5
 * a=5|||b=5
 *
 * Multiple leaf operators in a row:
 * a====5
 * b<=<=5
 *
 * Paren without anything inside
 * a=5&()
 *
 * Different operators at a single level
 * a=5&b=5|c=5
 * (a=1)&(b=2)|(c=3)
 *
 * No operator given:
 * a=1(b=2)
 * (a=1)(b=2)
 * (((a=1)b=2))
 */
static void _recurse(char **str_ptr, int *level, elem_t *parent, int *rc)
{
	elem_t *child;
	char *save_ptr;

	xassert(str_ptr);
	xassert(level);
	xassert(parent);
	xassert(rc);

	/* Save a pointer to the beginning of the string */
	save_ptr = *str_ptr;

	while (**str_ptr && (*rc == SLURM_SUCCESS)) {
		char save_char;
		char *next;

#if _DEBUG
		info("level=%d, string=\"%s\"", *level, *str_ptr);
#endif

		/*
		 * These first two checks go deeper or shallower in the tree.
		 * Make sure to update str_ptr.
		 *
		 * We can have multiple '(' or ')' in a row.
		 */
		if (**str_ptr == '(') {
			elem_t *child;

			if (!_valid_parent_child_op(parent)) {
				*rc = SLURM_ERROR;
				break;
			}

			/* Create a child for this new level and recurse */
			child = xmalloc(sizeof(*child));
			_add_child(parent, child);
			*level = *level + 1;
			(*str_ptr) = *str_ptr + 1;
			_recurse(str_ptr, level, child, rc);
			continue;
		} else if (**str_ptr == ')') {
			(*str_ptr) = *str_ptr + 1;
			if (*level) {
				*level = *level - 1;
			} else {
#if _DEBUG
				error("Unbalanced parentheses");
#endif
				*rc = SLURM_ERROR;
			}
			if (!parent->num_children) {
#if _DEBUG
				error("No children at this level");
#endif
				*rc = SLURM_ERROR;
			}

			return;
		}

		/* Check if we are at a child operator. */
		if (**str_ptr == '\0') {
			/*
			 * End of string. strchr will find the '\0' character
			 * in a string, so we need to avoid that when looking
			 * for child operator characters.
			 */
		} else if (strchr(child_op_chars, **str_ptr)) {
			char *tmp_end = NULL;
			op_t op = _str2op(*str_ptr, child_op_chars, &tmp_end);

			if (op == OP_NONE) {
				/*
				 * The strchr check verified that an operator
				 * character exists, but not that the whole
				 * operator string is valid. For example, there
				 * could be repeating operator characters:
				 * strchr would return a pointer to the first
				 * one, but _str2op would correctly identify
				 * that the operator is invalid.
				 */
#if _DEBUG
				save_char = *tmp_end;
				*tmp_end = '\0';
				error("Invalid operator string: \"%s\"",
				      *str_ptr);
				*tmp_end = save_char;
#endif
				*rc = SLURM_ERROR;
				break;
			}

			/* All operators in a single level must be the same. */
			if ((parent->operator != OP_NONE) &&
			    (parent->operator != op)) {
#if _DEBUG
				error("Operators at a single level must be the same. Got \"%s\" but parent op is \"%s\"",
				      _op2str(op),
				      _op2str(parent->operator));
#endif
				*rc = SLURM_ERROR;
				break;
			} else {
				parent->operator = op;
			}
			*str_ptr = tmp_end;
			continue;
		}

		if (!_valid_parent_child_op(parent)) {
			*rc = SLURM_ERROR;
			break;
		}

		/*
		 * This is a leaf.
		 * NULL-terminate the leaf string and create the leaf at the
		 * the next paren or child operator, or end of string.
		 * Then continue parsing at the next paren or child operator.
		 */
		next = _find_leaf_end(*str_ptr);
		xassert(next);

		save_char = *next;
		*next = '\0';

		if (!(child = _parse_leaf(*str_ptr))) {
			xfree(child);
			*rc = SLURM_ERROR;
			break;
		} else
			_add_child(parent, child);

		*next = save_char;
		*str_ptr = next;
	}

	/*
	 * Underflow should not happen - we should be catching potential
	 * underflow conditions and return an error instead.
	 */
	xassert(*level >= 0);

	/*
	 * Restore the pointer to the beginning so it can be free'd by the
	 * caller if it was malloc'd.
	 */
	*str_ptr = save_ptr;

	if (*level) {
		/* Unbalanced parentheses or parsing error */
#if _DEBUG
		if (*rc != SLURM_ERROR)
			error("Unbalanced parentheses");
#endif
		*rc = SLURM_ERROR;
	}
}

#define NUMBER_COMPARE(a,b,fuzzy,result) \
do {\
	if (fuzzy && fuzzy_equal(a, b))\
		result = CMP_EQ;\
	else if (!fuzzy && (a == b))\
		result = CMP_EQ;\
	else if (a < b)\
		result = CMP_LT;\
	else\
		result = CMP_GT;\
} while(0);


/*
 * Test if "data" equals, is less than, or is greater than "value"
 * data.c already has data_check_match(); however, that only checks for
 * equality and is stricter than we want to be here.
 */
static cmp_t _compare(data_t *data, char *value)
{
	cmp_t comparison;
	data_type_t data_type;
	data_t *value_d;

	xassert(value);
	xassert(data);

	value_d = data_new();
	if (!data_set_string(value_d, value)) {
		data_free(value_d);
#if _DEBUG
		error("%s: Couldn't convert %s to data_t", __func__, value);
#endif
		return CMP_INVALID;
	}
	data_type = data_get_type(data);

	switch (data_type) {
	case DATA_TYPE_INT_64:
	{
		/*
		 * We always do floating point comparison to be less strict on
		 * the user, and if the node data sometimes swaps between
		 * integer and floating point on node updates.
		 */
		double tmp1 = (double) data_get_int(data);
		double tmp2;

		if (data_convert_type(value_d, DATA_TYPE_FLOAT) !=
		    DATA_TYPE_FLOAT) {
			comparison = CMP_INVALID;
		} else {
			tmp2 = data_get_float(value_d);
			NUMBER_COMPARE(tmp1, tmp2, true, comparison);
		}
		break;
	}
	case DATA_TYPE_STRING:
		/*
		 * NOTE: strcmp is not guaranteed to return -1, 0, or 1. It
		 * is guaranteed to return a negative number, zero, or a
		 * positive number. Convert those to our CMP_* values.
		 */
		comparison = xstrcmp(data_get_string(data), value);
		if (comparison < 0)
			comparison = CMP_LT;
		else if (comparison > 0)
			comparison = CMP_GT;
		else
			comparison = CMP_EQ;
		break;
	case DATA_TYPE_FLOAT:
	{
		double tmp1 = data_get_float(data);
		double tmp2;

		if (data_convert_type(value_d, DATA_TYPE_FLOAT) !=
		    DATA_TYPE_FLOAT) {
			comparison = CMP_INVALID;
		} else {
			tmp2 = data_get_float(value_d);
			NUMBER_COMPARE(tmp1, tmp2, true, comparison);
		}
		break;
	}
	case DATA_TYPE_BOOL:
	{
		bool tmp1 = data_get_bool(data);
		bool tmp2;

		if (data_convert_type(value_d, DATA_TYPE_BOOL) !=
		    DATA_TYPE_BOOL) {
			comparison = CMP_INVALID;
		} else {
			tmp2 = data_get_bool(value_d);
			NUMBER_COMPARE(tmp1, tmp2, false, comparison);
		}
		break;
	}
	default:
		comparison = CMP_INVALID;
#if _DEBUG
		info("%s: Data type: %s is invalid",
		     __func__, data_type_to_string(data_type));
#endif
		break;
	}
	FREE_NULL_DATA(value_d);
	return comparison;
}

static bool _test(cmp_t comparison, op_t op)
{
	bool rc;

	if (op == OP_LEAF_EQ) {
		rc = (comparison == CMP_EQ);
	} else if (op == OP_LEAF_NE) {
		rc = (comparison != CMP_EQ);
	} else if (op == OP_LEAF_GT) {
		rc = (comparison == CMP_GT);
	} else if (op == OP_LEAF_GTE) {
		rc = (comparison >= CMP_EQ);
	} else if (op == OP_LEAF_LT) {
		rc = (comparison == CMP_LT);
	} else if (op == OP_LEAF_LTE) {
		rc = (comparison <= CMP_EQ);
	} else {
		error("%s: Undefined leaf operator %d", __func__, op);
		rc = false;
	}

	return rc;
}

/*
 * Test each leaf: the test is true if <data_value> <leaf_op> <leaf_value>
 * For each test, the key needs to exist in the data_t structure.
 */
static bool _test_extra_constraints(elem_t *parent, elem_t *el, data_t *data)
{
	bool test_result = false;

	if (!el)
		return false;
	if (!el->num_children) {
		/* leaf */
		data_t *data_ptr = NULL;
		cmp_t comparison = CMP_INVALID;
#if _DEBUG
		char *data_str = NULL;
#endif

		/* Check that key is in data_t */
		data_ptr = data_key_get(data, el->key);
		if (!data_ptr) {
#if _DEBUG
			info("%s: Key %s not found", __func__, el->key);
#endif
			return false;
		}
#if _DEBUG
		if (data_get_string_converted(data_ptr, &data_str) !=
		    SLURM_SUCCESS) {
			/* Couldn't convert to string. */
			data_str = xstrdup_printf("<Couldn't convert data to string>");
		}
#endif
		comparison = _compare(data_ptr, el->value);
		if (comparison == CMP_INVALID) {
#if _DEBUG
			info("%s: Invalid comparison: \"%s\" %s \"%s\"",
			     __func__, el->value, _op2str(el->operator),
			     data_str);
			xfree(data_str);
#endif
			return false;
		}
		test_result = _test(comparison, el->operator);
#if _DEBUG
		info("%s: Comparison result=%s: \"%s\" %s \"%s\"",
		     __func__, test_result ? "true" : "false",
		     data_str, _op2str(el->operator), el->value);
		xfree(data_str);
#endif

		return test_result;
	}

	xassert(el->children);
	for (int i = 0; i < el->num_children; i++) {
		test_result = _test_extra_constraints(el, el->children[i],
						      data);
		if (el->operator == OP_CHILD_OR) {
			/* OR: At least one child must pass. */
			if (test_result)
				break;
			else
				continue;
		} else {
			/*
			 * OP_CHILD_AND or OP_CHILD_NONE which is treated the
			 * same as OP_CHILD_AND.
			 * AND: All children must pass.
			 */
			if (test_result)
				continue;
			else
				break;
		}
	}
	return test_result;
}

/*
 * Parse a string into a tree
 */
extern int extra_constraints_parse(char *extra, elem_t **head)
{
	int rc = SLURM_SUCCESS;
	int level = 0;
	char *copy;
	elem_t *tree_head;

	xassert(head);

	if (!extra)
		return SLURM_SUCCESS;
	if (!extra_constraints_parsing)
		return SLURM_SUCCESS;

#if _DEBUG
	info("%s: parse %s", __func__, extra);
#endif

	copy = xstrdup(extra);
	tree_head = _alloc_tree();
	/*
	 * _recurse is currently not destructive of the string.
	 * However, just in case this changes in the future, operate on a copy
	 * of the string.
	 */
	_recurse(&copy, &level, tree_head, &rc);
	if (rc != SLURM_SUCCESS) {
		error("%s: Parsing %s failed", __func__, extra);
		FREE_NULL_EXTRA_CONSTRAINTS(tree_head);
		rc = ESLURM_INVALID_EXTRA;
	} else {
		if (tree_head->operator == OP_NONE) {
			/*
			 * This should only happen if the request was
			 * structured such that the parent has only one child.
			 * In that case, set the operator to AND as the
			 * default.
			 */
			xassert(tree_head->num_children == 1);
			tree_head->operator = OP_CHILD_AND;
		}
#if _DEBUG
		info("%s: Succeeded parsing %s", __func__, extra);
#endif
	}

#if _DEBUG
	{
		char *str = extra_constraints_2str(tree_head);
		info("\n%s", str);
		xfree(str);
	}
#endif

	*head = tree_head;
	xfree(copy);
	return rc;
}

extern void extra_constraints_set_parsing(bool set)
{
	extra_constraints_parsing = set;
}

extern bool extra_constraints_test(elem_t *head, data_t *data)
{
	if (!extra_constraints_parsing)
		return true;
	if (!head)
		return true;
	if (!data) {
		return false;
	}

	return _test_extra_constraints(NULL, head, data);
}
