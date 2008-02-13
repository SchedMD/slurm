void print_header(void)
{
	int	i,j;
	for (i=0; i<nprintfields; i++) {
		if (i)
			printf(" ");
		j=printfields[i];
		(fields[j].print_routine)(HEADLINE, 0);
	}
	printf("\n");
	for (i=0; i<nprintfields; i++) {
		if (i)
			printf(" ");
		j=printfields[i];
		(fields[j].print_routine)(UNDERSCORE, 0);
	}
	printf("\n");
}
