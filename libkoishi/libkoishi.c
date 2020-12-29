#include "libkoishi.h"
#include <stdlib.h>
#include <string.h>

#define RND_IMPLEMENTATION
#include "rnd.h"

#ifdef KSH_DEBUG
#define D(__FMT, ...) printf("\033[92m[K] " __FMT "\033[0m\n", __VA_ARGS__)
#else
#define D(__FMT, ...)
#endif
// TODO: error checking everywhere (esp malloc), error-code return values

struct cont {
	ksh_continuations_t* ptr; // pointer to the struct holding the cont, null if it's in the header
	int i; // the index within ksh_continuations_t or ksh_rule_t, -1 if not found
};

int64_t
defaultrng(void* rngdata, int64_t max) {
	return (int64_t)(rnd_pcg_nextf((rnd_pcg_t*)rngdata) * max);
}

ksh_model_t*
ksh_createmodel(int mapsize, int64_t (*rng)(void*, int64_t), uint32_t seed)
{
	ksh_model_t *model = malloc(sizeof(ksh_model_t));
	if (!model)
		return NULL;
	if (!rng) {
		model->rng = defaultrng;
		model->rngdata = malloc(sizeof(rnd_pcg_t));
		rnd_pcg_seed(model->rngdata, seed);
	} else {
		model->rng = rng;
	}

	model->mapsize = mapsize;
	model->hashmap = calloc(sizeof(ksh_rule_t*), 1<<mapsize);
	if (!model->hashmap)
		return NULL;
	return model;
}

uint32_t
fnv_32a(void *buf, size_t len)
{
	/* 
	 * Fowler/Noll/Vo FNV-1a (32-bit)
	 * http://www.isthe.com/chongo/tech/comp/fnv/
	 * Code adapted from http://www.isthe.com/chongo/src/fnv/hash_32a.c
	 * (public domain)
	 */
	uint32_t hval = 0;
	unsigned char *bp = (unsigned char *)buf;
	unsigned char *be = bp + len;
	while (bp < be) {
		hval ^= (uint32_t)*bp++;
		#if defined(NO_FNV_GCC_OPTIMIZATION)
		hval *= FNV_32_PRIME;
		#else
		hval += (hval<<1) + (hval<<4) + (hval<<7) + (hval<<8) + (hval<<24);
		#endif
	}
	return hval;
}

uint32_t
fnv_32a_folded(void *buf, size_t len, int foldto) {
	// xor-folds the fnv-32a hash to the desired width
	uint32_t hash = fnv_32a(buf, len);
	int width = 32;
	uint32_t mask = 0xFFFFFFFF << foldto;
	while (width > foldto) {
		uint32_t lower = hash & ~mask;
		uint32_t higher = hash & mask;
		hash = (higher >> foldto) ^ lower;
		width -= foldto;
	}
	return hash;
}

ksh_rule_t*
resolve_rule(ksh_model_t *model, ksh_u32char *name, uint32_t *hashptr) {
	D("Resolving rule %4x%4x%4x%4x", name[0], name[1], name[1], name[3]);
	uint32_t hash = fnv_32a_folded(name, 4*sizeof(ksh_u32char), model->mapsize);
	D("hash: %8x", hash);
	if (hashptr)
		*hashptr = hash;
	// optionally return the hash to the caller, for example to create a new rule under it
	ksh_rule_t *rule = model->hashmap[hash];
	if (!rule)
		return NULL;
	for(; rule != NULL; rule = rule->next) {
		if (0 == memcmp(name, rule->name, 4*sizeof(ksh_u32char))) {
			return rule;
		}
	}
	return NULL;
}

struct cont
resolve_create_cont(ksh_rule_t *rule, ksh_u32char ch) {
	// oh wow this function is horrible
	struct cont ret;
	ksh_continuations_t *lastobj;
	for (int i = 0; i < KSH_CONTINUATIONS_PER_HEADER; i++) {
		if (rule->character[i] == ch) {
			ret.ptr = NULL;
			ret.i = i;
			return ret;
		}
	}
	for(ksh_continuations_t *c = rule->cont; c != NULL; c = c->next) {
		lastobj = c;
		for (int i = 0; i < KSH_CONTINUATIONS_PER_STRUCT; i++) {
			if (c->character[i] == ch) {
				ret.ptr = c;
				ret.i = i;
				return ret;
			}
		}
	}
	// not found, create
	if (rule->cont) {
		for (int i = 0; i < KSH_CONTINUATIONS_PER_STRUCT; i++) {
			if (lastobj->character[i] == 0) {
				lastobj->character[i] = ch;
				lastobj->probability[i] = 0;
				ret.ptr = lastobj;
				ret.i = i;
				return ret;
			}
		}
		// no empty space in object, create new
		ksh_continuations_t *new = calloc(1, sizeof(ksh_continuations_t));
		new->character[0] = ch;
		lastobj->next = new;
		ret.ptr = new;
		ret.i = 0;
		return ret;
	} else {
		for (int i = 0; i < KSH_CONTINUATIONS_PER_HEADER; i++) {
			if (rule->character[i] == 0) {
				rule->character[i] = ch;
				rule->probability[i] = 0;
				ret.ptr = lastobj;
				ret.i = i;
				return ret;
			}
		}
		// no empty space in object, create new
		ksh_continuations_t *new = calloc(1, sizeof(ksh_continuations_t));
		new->character[0] = ch;
		rule->cont = new;
		ret.ptr = new;
		ret.i = 0;
		return ret;
	}
}

void
ksh_makeassociation(
	ksh_model_t *model,
	ksh_u32char n1,
	ksh_u32char n2,
	ksh_u32char n3,
	ksh_u32char n4,
	ksh_u32char ch
)
{
	ksh_u32char name[4];
	name[0] = n1;
	name[1] = n2;
	name[2] = n3;
	name[3] = n4;
	uint32_t hash;
	ksh_rule_t *rule = resolve_rule(model, name, &hash);
	if (!rule) {
		rule = malloc(sizeof(ksh_rule_t));
		memcpy(rule->name, name, sizeof(name));
		rule->cont = NULL;
		rule->probtotal = 0;
		for (int i = 0; i < KSH_CONTINUATIONS_PER_HEADER; i++) {
			rule->character[i] = 0;
			rule->probability[i] = 0;
		}
		rule->next = model->hashmap[hash];
		model->hashmap[hash] = rule;
	}
	rule->probtotal++;
	struct cont c = resolve_create_cont(rule, ch);
	if (c.ptr) {
		c.ptr->probability[c.i]++;
	} else {
		rule->probability[c.i]++;
	}
}

ksh_u32char
ksh_getcontinuation(
	ksh_model_t *model,
	ksh_u32char n1,
	ksh_u32char n2,
	ksh_u32char n3,
	ksh_u32char n4
)
{
	ksh_u32char name[4];
	name[0] = n1;
	name[1] = n2;
	name[2] = n3;
	name[3] = n4;
	ksh_rule_t *rule = resolve_rule(model, name, NULL);
	D("Resolved rule %2x%2x%2x%2x to %p", n1, n2, n3, n4, rule);
	if (!rule)
		return 0;
	D("... with pt%ld", rule->probtotal);
	int64_t r = model->rng(model->rngdata, rule->probtotal);
	for (int i = 0; i < KSH_CONTINUATIONS_PER_HEADER; i++) {
		D("Rrng%ld r%2x(%c) p%u", r, rule->character[i], rule->character[i], rule->probability[i]);
		r -= rule->probability[i];
		if (r <= 0)
			return rule->character[i];
	}
	for(ksh_continuations_t *c = rule->cont; c != NULL; c = c->next) {
		for (int i = 0; i < KSH_CONTINUATIONS_PER_STRUCT; i++) {
			D("Crng%ld r%2x(%c) p%u", r, c->character[i], c->character[i], c->probability[i]);
			r -= c->probability[i];
			if (r <= 0)
				return c->character[i];
		}
	}
	return 0;
}