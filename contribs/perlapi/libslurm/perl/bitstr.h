/* 
 * slurm_bit_*() functions are exported in libslurm. But the prototypes are not listed in slurm.h
 */


/* copied and modified from src/common/bitstring.h */

/* compat with Vixie macros */
bitstr_t *slurm_bit_alloc(bitoff_t nbits);
int slurm_bit_test(bitstr_t *b, bitoff_t bit);
void slurm_bit_set(bitstr_t *b, bitoff_t bit);
void slurm_bit_clear(bitstr_t *b, bitoff_t bit);
void slurm_bit_nclear(bitstr_t *b, bitoff_t start, bitoff_t stop);
void slurm_bit_nset(bitstr_t *b, bitoff_t start, bitoff_t stop);

/* changed interface from Vixie macros */
bitoff_t slurm_bit_ffc(bitstr_t *b);
bitoff_t slurm_bit_ffs(bitstr_t *b);

/* new */
bitoff_t slurm_bit_nffs(bitstr_t *b, int n);
bitoff_t slurm_bit_nffc(bitstr_t *b, int n);
bitoff_t slurm_bit_noc(bitstr_t *b, int n, int seed);
void    slurm_bit_free(bitstr_t *b);
bitstr_t *slurm_bit_realloc(bitstr_t *b, bitoff_t nbits);
bitoff_t slurm_bit_size(bitstr_t *b);
void    slurm_bit_and(bitstr_t *b1, bitstr_t *b2);
void    slurm_bit_not(bitstr_t *b);
void    slurm_bit_or(bitstr_t *b1, bitstr_t *b2);
int     slurm_bit_set_count(bitstr_t *b);
int	slurm_bit_set_count_range(bitstr_t *b, int start, int end);
int     slurm_bit_clear_count(bitstr_t *b);
int     slurm_bit_nset_max_count(bitstr_t *b);
bitstr_t *slurm_bit_rotate_copy(bitstr_t *b1, int n, bitoff_t nbits);
void    slurm_bit_rotate(bitstr_t *b1, int n);
char    *slurm_bit_fmt(char *str, int len, bitstr_t *b);
int     slurm_bit_unfmt(bitstr_t *b, char *str);
int     *slurm_bitfmt2int (char *bit_str_ptr);
char    *slurm_bit_fmt_hexmask(bitstr_t *b);
int     slurm_bit_unfmt_hexmask(bitstr_t *b, const char *str);
char    *slurm_bit_fmt_binmask(bitstr_t *b);
int     slurm_bit_unfmt_binmask(bitstr_t *b, const char *str);
bitoff_t slurm_bit_fls(bitstr_t *b);
void    slurm_bit_fill_gaps(bitstr_t *b);
int     slurm_bit_super_set(bitstr_t *b1, bitstr_t *b2);
int     slurm_bit_overlap(bitstr_t *b1, bitstr_t *b2);
int     slurm_bit_equal(bitstr_t *b1, bitstr_t *b2);
void    slurm_bit_copybits(bitstr_t *dest, bitstr_t *src);
bitstr_t *slurm_bit_copy(bitstr_t *b);
bitstr_t *slurm_bit_pick_cnt(bitstr_t *b, bitoff_t nbits);
bitoff_t slurm_bit_get_bit_num(bitstr_t *b, int pos);
int      slurm_bit_get_pos_num(bitstr_t *b, bitoff_t pos);

#define FREE_NULL_BITMAP(_X)            \
	 do {                            \
		if (_X) slurm_bit_free (_X);  \
			_X      = NULL;         \
	 } while (0)

