#ifndef _LIBKOISHI_H
#define _LIBKOISHI_H
#include <stdio.h>
#include <stdint.h>

#define KSH_CONTINUATIONS_PER_HEADER 1
#define KSH_CONTINUATIONS_PER_STRUCT 4

// unicode codepoint type (utf-32)
typedef uint32_t ksh_u32char;

// rule continuation linked list
struct ksh_continuation_t {
	struct ksh_continuation_t *next;
	ksh_u32char character[KSH_CONTINUATIONS_PER_STRUCT];
	uint32_t probability[KSH_CONTINUATIONS_PER_STRUCT];
};
typedef struct ksh_continuation_t ksh_continuation_t;

struct ksh_rule_t {
	struct ksh_rule_t *next;
	ksh_u32char name[4];
	uint64_t probtotal;
    ksh_u32char character[KSH_CONTINUATIONS_PER_HEADER];
    uint32_t probability[KSH_CONTINUATIONS_PER_STRUCT];
    // a few continuations are going already into the rule object, to avoid
    // the memory overhead of allocating an entire ksh_continuation_t
    // in v1 around 60% of rules had only one cont, 80% had only two
	ksh_continuation_t *cont;
};
typedef struct ksh_rule_t ksh_rule_t;

struct ksh_model_t {
    int mapsize; // hashmap[2^mapsize]
	ksh_rule_t **hashmap;
};
typedef struct ksh_model_t ksh_model_t;

ksh_model_t *ksh_createmodel(int mapsize);

void ksh_makeassociation(ksh_model_t *model, ksh_u32char n1, ksh_u32char n2, ksh_u32char n3, ksh_u32char n4, ksh_u32char c);
ksh_u32char ksh_getcontinuation(ksh_model_t *model, ksh_u32char n1, ksh_u32char n2, ksh_u32char n3, ksh_u32char n4);

void ksh_trainmarkov(ksh_model_t *model, const char *str);
void ksh_createstring(ksh_model_t *model, char *buf, size_t bufsize);

void ksh_savemodel(ksh_model_t *model, FILE *f);
void ksh_loadmodel(ksh_model_t *model, FILE *f);

void ksh_freemodel(ksh_model_t *);

#endif