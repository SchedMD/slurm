extern "C" {

int
select_p_state_save(
        char *dir_name
        )
{
	return SLURM_SUCCESS;
}

int
select_p_state_restore(
        char *dir_name
        )
{
	return SLURM_SUCCESS;
}

int
select_p_reconfigure(void)
{

    return SLURM_SUCCESS;
}

} // extern "C"

