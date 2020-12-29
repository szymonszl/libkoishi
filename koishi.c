#include <stdio.h>
#include <libkoishi.h>

int main(int argc, char **argv) {
	puts("koishi.c - libkoishi example program");

	ksh_model_t *model = ksh_createmodel(16, NULL, 0xb00b);
	printf("allocated model (%p)\n", model);

	ksh_makeassociation(model, 'a', 'b', 'c', 'd', 'e');
	printf("associated 'e' to 'abcd'\n");

	ksh_u32char c = ksh_getcontinuation(model, 'a', 'b', 'c', 'd');
	printf("Got the character '%c' as continuation to 'abcd'\n", c);

	ksh_makeassociation(model, 'a', 'b', 'c', 'd', 'e');
	ksh_makeassociation(model, 'b', 'c', 'd', 'e', 'f');
	ksh_makeassociation(model, 'a', 'b', 'c', 'd', 'f');
	ksh_makeassociation(model, 'a', 'b', 'c', 'd', 'g');
	printf("Made associations for 'abcdef', 'abcdf', 'abcdg'\n");

	for (int i = 0; i < 10; i++) {
		c = ksh_getcontinuation(model, 'a', 'b', 'c', 'd');
		printf("Got the character '%c' as continuation to 'abcd'\n", c);
	}
	c = ksh_getcontinuation(model, 'b', 'c', 'd', 'e');
	printf("Got the character '%c' as continuation to 'bcde'\n", c);

	return 0;
}