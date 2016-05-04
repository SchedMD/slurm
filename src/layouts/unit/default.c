/** TODO: copyright notice */

#include "config.h"

#include "slurm/slurm.h"

#include "src/common/layouts_mgr.h"
#include "src/common/entity.h"
#include "src/common/log.h"

const char plugin_name[] = "Unit Tests layouts plugin";
const char plugin_type[] = "layouts/unit";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* specific options for unit tests layout */
s_p_options_t entity_options[] = {
	/* base keys */
	{"string", S_P_STRING},
	{"long", S_P_LONG},
	{"uint16", S_P_UINT16},
	{"uint32", S_P_UINT32},
	{"float", S_P_FLOAT},
	{"double", S_P_DOUBLE},
	{"ldouble", S_P_LONG_DOUBLE},
	{"readonly", S_P_BOOLEAN},
	/* parents aggregated keys */
	{"parents_sum_long", S_P_LONG},
	{"parents_fshare_long", S_P_LONG},
	{"parents_sum_uint16", S_P_UINT16},
	{"parents_fshare_uint16", S_P_UINT16},
	{"parents_sum_uint32", S_P_UINT32},
	{"parents_fshare_uint32", S_P_UINT32},
	{"parents_sum_float", S_P_FLOAT},
	{"parents_fshare_float", S_P_FLOAT},
	{"parents_sum_double", S_P_DOUBLE},
	{"parents_fshare_double", S_P_DOUBLE},
	{"parents_sum_ldouble", S_P_LONG_DOUBLE},
	{"parents_fshare_ldouble", S_P_LONG_DOUBLE},
	/* children aggregated keys */
	{"children_count", S_P_UINT32},
	{"children_sum_long", S_P_LONG},
	{"children_avg_long", S_P_LONG},
	{"children_min_long", S_P_LONG},
	{"children_max_long", S_P_LONG},
	{"children_sum_uint16", S_P_UINT16},
	{"children_avg_uint16", S_P_UINT16},
	{"children_min_uint16", S_P_UINT16},
	{"children_max_uint16", S_P_UINT16},
	{"children_sum_uint32", S_P_UINT32},
	{"children_avg_uint32", S_P_UINT32},
	{"children_min_uint32", S_P_UINT32},
	{"children_max_uint32", S_P_UINT32},
	{"children_sum_float", S_P_FLOAT},
	{"children_avg_float", S_P_FLOAT},
	{"children_min_float", S_P_FLOAT},
	{"children_max_float", S_P_FLOAT},
	{"children_sum_double", S_P_DOUBLE},
	{"children_avg_double", S_P_DOUBLE},
	{"children_min_double", S_P_DOUBLE},
	{"children_max_double", S_P_DOUBLE},
	{"children_sum_ldouble", S_P_LONG_DOUBLE},
	{"children_avg_ldouble", S_P_LONG_DOUBLE},
	{"children_min_ldouble", S_P_LONG_DOUBLE},
	{"children_max_ldouble", S_P_LONG_DOUBLE},
	{NULL}
};
s_p_options_t options[] = {
	{"Entity", S_P_EXPLINE, NULL, NULL, entity_options},
	{NULL}
};

const layouts_keyspec_t keyspec[] = {
	/* base keys */
	{"string", L_T_STRING},
	{"long", L_T_LONG},
	{"uint16", L_T_UINT16},
	{"uint32", L_T_UINT32},
	{"float", L_T_FLOAT},
	{"double", L_T_DOUBLE},
	{"ldouble", L_T_LONG_DOUBLE},
	{"readonly", L_T_BOOLEAN, KEYSPEC_RDONLY},
	/* parents aggregated keys */
	{"parents_sum_long", L_T_LONG,
	 KEYSPEC_UPDATE_PARENTS_SUM, "long"},
	{"parents_fshare_long", L_T_LONG,
	 KEYSPEC_UPDATE_PARENTS_FSHARE, "long"},
	{"parents_sum_uint16", L_T_UINT16,
	 KEYSPEC_UPDATE_PARENTS_SUM, "uint16"},
	{"parents_fshare_uint16", L_T_UINT16,
	 KEYSPEC_UPDATE_PARENTS_FSHARE, "uint16"},
	{"parents_sum_uint32", L_T_UINT32,
	 KEYSPEC_UPDATE_PARENTS_SUM, "uint32"},
	{"parents_fshare_uint32", L_T_UINT32,
	 KEYSPEC_UPDATE_PARENTS_FSHARE, "uint32"},
	{"parents_sum_float", L_T_FLOAT,
	 KEYSPEC_UPDATE_PARENTS_SUM, "float"},
	{"parents_fshare_float", L_T_FLOAT,
	 KEYSPEC_UPDATE_PARENTS_FSHARE, "float"},
	{"parents_sum_double", L_T_DOUBLE,
	 KEYSPEC_UPDATE_PARENTS_SUM, "double"},
	{"parents_fshare_double", L_T_DOUBLE,
	 KEYSPEC_UPDATE_PARENTS_FSHARE, "double"},
	{"parents_sum_ldouble", L_T_LONG_DOUBLE,
	 KEYSPEC_UPDATE_PARENTS_SUM, "ldouble"},
	{"parents_fshare_ldouble", L_T_LONG_DOUBLE,
	 KEYSPEC_UPDATE_PARENTS_FSHARE, "ldouble"},
	/* children aggregated keys */
	{"children_count", L_T_UINT32, KEYSPEC_UPDATE_CHILDREN_COUNT},
	{"children_sum_long", L_T_LONG,
	 KEYSPEC_UPDATE_CHILDREN_SUM, "long"},
	{"children_avg_long", L_T_LONG,
	 KEYSPEC_UPDATE_CHILDREN_AVG, "long"},
	{"children_min_long", L_T_LONG,
	 KEYSPEC_UPDATE_CHILDREN_MIN, "long"},
	{"children_max_long", L_T_LONG,
	 KEYSPEC_UPDATE_CHILDREN_MAX, "long"},
	{"children_sum_uint16", L_T_UINT16,
	 KEYSPEC_UPDATE_CHILDREN_SUM, "uint16"},
	{"children_avg_uint16", L_T_UINT16,
	 KEYSPEC_UPDATE_CHILDREN_AVG, "uint16"},
	{"children_min_uint16", L_T_UINT16,
	 KEYSPEC_UPDATE_CHILDREN_MIN, "uint16"},
	{"children_max_uint16", L_T_UINT16,
	 KEYSPEC_UPDATE_CHILDREN_MAX, "uint16"},
	{"children_sum_uint32", L_T_UINT32,
	 KEYSPEC_UPDATE_CHILDREN_SUM, "uint32"},
	{"children_avg_uint32", L_T_UINT32,
	 KEYSPEC_UPDATE_CHILDREN_AVG, "uint32"},
	{"children_min_uint32", L_T_UINT32,
	 KEYSPEC_UPDATE_CHILDREN_MIN, "uint32"},
	{"children_max_uint32", L_T_UINT32,
	 KEYSPEC_UPDATE_CHILDREN_MAX, "uint32"},
	{"children_sum_float", L_T_FLOAT,
	 KEYSPEC_UPDATE_CHILDREN_SUM, "float"},
	{"children_avg_float", L_T_FLOAT,
	 KEYSPEC_UPDATE_CHILDREN_AVG, "float"},
	{"children_min_float", L_T_FLOAT,
	 KEYSPEC_UPDATE_CHILDREN_MIN, "float"},
	{"children_max_float", L_T_FLOAT,
	 KEYSPEC_UPDATE_CHILDREN_MAX, "float"},
	{"children_sum_double", L_T_DOUBLE,
	 KEYSPEC_UPDATE_CHILDREN_SUM, "double"},
	{"children_avg_double", L_T_DOUBLE,
	 KEYSPEC_UPDATE_CHILDREN_AVG, "double"},
	{"children_min_double", L_T_DOUBLE,
	 KEYSPEC_UPDATE_CHILDREN_MIN, "double"},
	{"children_max_double", L_T_DOUBLE,
	 KEYSPEC_UPDATE_CHILDREN_MAX, "double"},
	{"children_sum_ldouble", L_T_LONG_DOUBLE,
	 KEYSPEC_UPDATE_CHILDREN_SUM, "ldouble"},
	{"children_avg_ldouble", L_T_LONG_DOUBLE,
	 KEYSPEC_UPDATE_CHILDREN_AVG, "ldouble"},
	{"children_min_ldouble", L_T_LONG_DOUBLE,
	 KEYSPEC_UPDATE_CHILDREN_MIN, "ldouble"},
	{"children_max_ldouble", L_T_LONG_DOUBLE,
	 KEYSPEC_UPDATE_CHILDREN_MAX, "ldouble"},
	{NULL}
};

/* types allowed in the entity's "type" field */
const char* etypes[] = {
	"UnitTestPass",
	"UnitTest",
	NULL
};

const layouts_plugin_spec_t plugin_spec = {
	options,
	keyspec,
	LAYOUT_STRUCT_TREE,
	etypes,
	true, /* if this evalued to true, keys inside plugin_keyspec present in
	       * plugin_options having corresponding types, are automatically
	       * handled by the layouts manager.
	       */
	true  /* if this evalued to true, keys updates trigger an automatic
	       * update of their entities neighborhoods based on their
	       * KEYSPEC_UPDATE_* set flags
	       */
};

/* manager is lock when this function is called */
/* disable this callback by setting it to NULL, warn: not every callback can
 * be desactivated this way */
int layouts_p_conf_done(
		xhash_t* entities, layout_t* layout, s_p_hashtbl_t* tbl)
{
	return 1;
}


/* disable this callback by setting it to NULL, warn: not every callback can
 * be desactivated this way */
void layouts_p_entity_parsing(
		entity_t* e, s_p_hashtbl_t* etbl, layout_t* layout)
{
}

/* manager is lock then this function is called */
/* disable this callback by setting it to NULL, warn: not every callback can
 * be desactivated this way */
int layouts_p_update_done(layout_t* layout, entity_t** e_array, int e_cnt)
{
	int i;
	debug3("layouts/unit: receiving update callback for %d entities",
	       e_cnt);
	for (i = 0; i < e_cnt; i++) {
		if (e_array[i] == NULL) {
			debug3("layouts/unit: skipping update of nullified"
			       "entity[%d]", i);
		} else {
			debug3("layouts/unit: updating entity[%d]=%s",
			       i, e_array[i]->name);
		}
	}
	return 1;
}

int init(void)
{
	return SLURM_SUCCESS;
}

int fini(void)
{
	return SLURM_SUCCESS;
}
