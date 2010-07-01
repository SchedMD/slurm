#include <slurm/slurm.h>

extern "C" {

int
select_p_node_init(
        struct node_record *node_ptr,
        int node_cnt
        )
{
    return SLURM_SUCCESS;
}

int
select_p_update_sub_node(
        update_part_msg_t *part_desc_ptr
        )
{
    return SLURM_SUCCESS;
}

int
select_p_update_node_config(
        int index
        )
{
    return SLURM_SUCCESS;
}

int
select_p_update_node_state(
        int index,
        uint16_t state
        )
{
    return SLURM_SUCCESS;
}

int
select_p_alter_node_cnt(
        enum select_node_cnt type,
        void *data
        )
{
    return SLURM_SUCCESS;
}

} // extern "C"

