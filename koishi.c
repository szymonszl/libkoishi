#include <stdio.h>
#include <libkoishi.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#define BREAK() if(debug)raise(SIGTRAP);

int main(int argc, char **argv) {
	puts("koishi.c - libkoishi example program");

	int debug = 0;
	if (getenv("KSH_DEBUG") && !strcmp(getenv("KSH_DEBUG"), "1")) {
		debug = 1;
		printf("Warning: debug tracing enabled, will raise SIGTRAP\n");
	}

	ksh_model_t *model = ksh_createmodel(16, NULL, 0xb00b);
	printf("allocated model (%p)\n", model);

	BREAK();

	ksh_u32char name[] = {'a', 'b', 'c', 'd'};
	ksh_u32char name2[] = {'b', 'c', 'd', 'e'};
	ksh_makeassociation(model, name, 'e');
	printf("associated 'e' to 'abcd'\n");

	ksh_u32char c = ksh_getcontinuation(model, name);
	printf("Got the character '%c' as continuation to 'abcd'\n", c);

	ksh_makeassociation(model, name, 'e');
	ksh_makeassociation(model, name, 'f');
	ksh_makeassociation(model, name, 'g');
	ksh_makeassociation(model, name2, 'f');
	printf("Made associations for 'abcdef', 'abcdf', 'abcdg'\n");

	BREAK();

	for (int i = 0; i < 5; i++) {
		c = ksh_getcontinuation(model, name);
		printf("Got the character '%c' as continuation to 'abcd'\n", c);
	}
	c = ksh_getcontinuation(model, name2);
	printf("Got the character '%c' as continuation to 'bcde'\n", c);

	BREAK();

	// simple long string with some random polish in it
	ksh_trainmarkov(model, "Grzegorz Brzęczyszczykiewicz, Chrząszczyżewoszyce, powiat Łękołody.");
	// a similar long string, just to cause the chain to jump around a bit
	ksh_trainmarkov(model, "Grzegorz Brzęczyszczykiewicz, Chrząszczyżewoszyce Małe, województwo Łękołodzkie.");
	// alternative timeline where Franciszek Dolas was captured by the Japanese and unicode wasnt a thing yet so he was interested to see how their systems handle both japanese and polish in the same string
	ksh_trainmarkov(model, "Brzęczyszczykiewicz Grzegorz です.");
	// invalid unicode sequences - only start byte, stray continuation, wrongly encoded NUL (should be stripped on read)
	ksh_trainmarkov(model, "abc""\xF0""def""\x80""ghi""\xC0""\x80""jkl");
	printf("Trained some strings with Unicode\n");

	BREAK();


	char buf[128];
	for (int i = 0; i < 10; i++) {
		ksh_createstring(model, buf, 128);
		printf("Generated string: '\033[97m%s\033[0m'\n", buf);
	}

	FILE *f = fopen("test.ksh", "w");
	ksh_savemodel(model, f);
	fclose(f);
	printf("Saved model\n");

	ksh_freemodel(model);
	printf("Deallocated model\n");

	BREAK();

	model = ksh_createmodel(8, NULL, 0x51d3b00b);
	f = fopen("test.ksh", "r");
	int r = ksh_loadmodel(model, f);
	if (r < 0) {
		printf("Loading model failed (%d), file left at byte 0x%x\n", r, ftell(f));
		fclose(f);
		return 1;
	}
	fclose(f);
	BREAK();
	printf("Loaded model again from file\n");
	for (int i = 0; i < 10; i++) {
		ksh_createstring(model, buf, 128);
		printf("Generated string: '\033[97m%s\033[0m'\n", buf);
	}

	return 0;
}