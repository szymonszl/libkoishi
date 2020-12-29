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

struct ksh_markovdict_t {
    int mapsize; // hashmap[2^mapsize]
	ksh_rule_t** hashmap;
};
typedef struct ksh_markovdict_t ksh_markovdict_t;

ksh_markovdict_t* ksh_createdict(int);

void ksh_makeassociation(ksh_markovdict_t*, ksh_u32char, ksh_u32char, ksh_u32char, ksh_u32char, ksh_u32char);
ksh_u32char ksh_getcontinuation(ksh_markovdict_t*, ksh_u32char, ksh_u32char, ksh_u32char, ksh_u32char);

void ksh_trainmarkov(ksh_markovdict_t*, const char*);
void ksh_createstring(ksh_markovdict_t*, char*, size_t);

void ksh_savedict(ksh_markovdict_t*, FILE*);
void ksh_loaddict(ksh_markovdict_t*, FILE*);

void ksh_freedict(ksh_markovdict_t*);

#endif