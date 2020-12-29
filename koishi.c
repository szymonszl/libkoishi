#include <stdio.h>
#include <libkoishi.h>

int main(int argc, char **argv) {
    puts("koishi.c - libkoishi example program");
    ksh_model_t *model = ksh_createmodel(16);
    printf("allocated model (%p)\n", model);
    ksh_makeassociation(model, 'a', 'b', 'c', 'd', 'e');
    printf("associated 'e' to 'abcd'\n");
    return 0;
}