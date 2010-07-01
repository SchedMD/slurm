#include <slurm/slurm.h>

#include <src/common/list.h>

extern "C" {

int
select_p_block_init(
        List part_list
        )
{
    return SLURM_SUCCESS;
}

int
select_p_update_block(
        update_block_msg_t* block_desc_ptr
        )
{

    return SLURM_SUCCESS;
}

}
