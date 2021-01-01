#include "libkoishi.h"
#include <stdlib.h>
#include <string.h>

#define RND_IMPLEMENTATION
#define RND_U32 uint32_t
#define RND_U64 uint64_t
#include "rnd.h"

#ifdef KSH_DEBUG
#define D(__FMT) printf("\033[92m[K] " __FMT "\033[0m\n")
#define Df(__FMT, ...) printf("\033[92m[K] " __FMT "\033[0m\n", __VA_ARGS__)
#else
#define D(__FMT)
#define Df(__FMT, ...)
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

void
ksh_freemodel(ksh_model_t *model)
{
	if (model->rng = defaultrng) {
		free(model->rngdata);
	}
	for (uint64_t i = 0; i < (1<<model->mapsize); i++) {
		ksh_rule_t *rule, *nextrule;
		ksh_continuations_t *cont, *nextcont;
		rule = model->hashmap[i];
		while (rule != NULL) {
			cont = rule->cont;
			while (cont != NULL) {
				nextcont = cont->next;
				free(cont);
				cont = nextcont;
			}
			nextrule = rule->next;
			free(rule);
			rule = nextrule;
		}
	}
	free(model);
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
	uint32_t hval = 0x811c9dc5;
	unsigned char *bp = (unsigned char *)buf;
	unsigned char *be = bp + len;
	while (bp < be) {
		hval ^= (uint32_t)*bp++;
		#if defined(NO_FNV_GCC_OPTIMIZATION)
		hval *= 0x01000193;
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
	uint32_t hash = fnv_32a_folded(name, 4*sizeof(ksh_u32char), model->mapsize);
	Df("Resolving rule %4x%4x%4x%4x, hash: %8x", name[0], name[1], name[2], name[3], hash);
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

ksh_rule_t*
create_rule(ksh_model_t *model, ksh_u32char *name, uint32_t *hashptr) {
	uint32_t hash;
	if (hashptr == NULL) {
		hash = fnv_32a_folded(name, 4*sizeof(ksh_u32char), model->mapsize);
	} else {
		hash = *hashptr;
	}
	ksh_rule_t *rule = calloc(1, sizeof(ksh_rule_t));
	memcpy(rule->name, name, 4*sizeof(ksh_u32char));
	rule->next = model->hashmap[hash];
	model->hashmap[hash] = rule;
	return rule;
}

ksh_rule_t*
resolve_create_rule(ksh_model_t *model, ksh_u32char *name) {
	uint32_t hash;
	ksh_rule_t *rule = resolve_rule(model, name, &hash);
	if (!rule)
		rule = create_rule(model, name, &hash);
	return rule;
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
			if (lastobj->probability[i] == 0) {
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
			if (rule->probability[i] == 0) {
				rule->character[i] = ch;
				rule->probability[i] = 0;
				ret.ptr = 0;
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
append_cont(ksh_rule_t *rule, struct cont *ctx)
{
	ctx->i++;
	if (!ctx->ptr) {
		if (ctx->i >= KSH_CONTINUATIONS_PER_HEADER) {
			ksh_continuations_t *new = calloc(1, sizeof(ksh_continuations_t));
			rule->cont = new;
			ctx->ptr = new;
			ctx->i = 0;
		}
	} else {
		if (ctx->i >= KSH_CONTINUATIONS_PER_STRUCT) {
			ksh_continuations_t *new = calloc(1, sizeof(ksh_continuations_t));
			ctx->ptr->next = new;
			ctx->ptr = new;
			ctx->i = 0;
		}
	}
}

void
ksh_makeassociation(
	ksh_model_t *model,
	ksh_u32char *name,
	ksh_u32char ch
)
{
	ksh_rule_t *rule = resolve_create_rule(model, name);
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
	ksh_u32char *name
)
{
	ksh_rule_t *rule = resolve_rule(model, name, NULL);
	if (!rule)
		return 0;
	int64_t r = model->rng(model->rngdata, rule->probtotal+1);
	for (int i = 0; i < KSH_CONTINUATIONS_PER_HEADER; i++) {
		Df("[get] Rrng%ld/%ld rx%02x(%c) p%u", r, rule->probtotal, rule->character[i], rule->character[i], rule->probability[i]);
		r -= rule->probability[i];
		if (r <= 0)
			return rule->character[i];
	}
	for(ksh_continuations_t *c = rule->cont; c != NULL; c = c->next) {
		for (int i = 0; i < KSH_CONTINUATIONS_PER_STRUCT; i++) {
			Df("[get] Crng%ld/%ld rx%02x(%c) p%u", r, rule->probtotal, c->character[i], c->character[i], c->probability[i]);
			r -= c->probability[i];
			if (r <= 0)
				return c->character[i];
		}
	}
	return 0;
}

/*
 * rfc3629 for reference:
 * Char. number range  |        UTF-8 octet sequence
 *    (hexadecimal)    |              (binary)
 * --------------------+--------------------------------------
 * 0000 0000-0000 007F | 0xxxxxxx
 * 0000 0080-0000 07FF | 110xxxxx 10xxxxxx
 * 0000 0800-0000 FFFF | 1110xxxx 10xxxxxx 10xxxxxx
 * 0001 0000-0010 FFFF | 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
 */

int
utf8_readcharacter(ksh_u32char *out, const char *str)
{
	#define UTF8_VERIFY_TOP_BITS(UP_TO) \
		for (int _VERIFY_I = 1; _VERIFY_I < UP_TO; _VERIFY_I++) \
			if ((str[_VERIFY_I] & 0xC0) != 0x80) return -1;
	ksh_u32char ch;
	int bytesread = 0;
	if (!(str[0] & 0x80)) { // high bit not set, ascii
		bytesread = 1;
		ch = str[0];
	} else if ((str[0] & 0xE0) == 0xC0) {
		UTF8_VERIFY_TOP_BITS(2)
		bytesread = 2;
		ch = (str[0] & 0x1F) << 6;
		ch |= (str[1] & 0x3F);
		if (ch < 0x80)
			return -1;
	} else if ((str[0] & 0xF0) == 0xE0) {
		UTF8_VERIFY_TOP_BITS(3)
		bytesread = 3;
		ch = (str[0] & 0x0F) << 12;
		ch |= (str[1] & 0x3F) << 6;
		ch |= (str[2] & 0x3F);
		if (ch < 0x800)
			return -1;
	} else if ((str[0] & 0xF8) == 0xF0) {
		UTF8_VERIFY_TOP_BITS(4)
		bytesread = 4;
		ch = (str[0] & 0x07) << 18;
		ch |= (str[1] & 0x3F) << 12;
		ch |= (str[2] & 0x3F) << 6;
		ch |= (str[3] & 0x3F);
		if (ch < 0x10000)
			return -1;
	} else {
		// invalid character
		return -1;
	}
	*out = ch;
	return bytesread;
}

int
utf8_writecharacter(ksh_u32char ch, char *buf) {
	if (ch < 0x80) {
		buf[0] = ch;
		return 1;
	} else if (ch < 0x800) {
		buf[0] = 0xC0 | (ch >> 6);
		buf[1] = 0x80 | (ch & 0x3F);
		return 2;
	} else if (ch < 0x10000) {
		buf[0] = 0xE0 | (ch >> 12);
		buf[1] = 0x80 | (ch >> 6 & 0x3F);
		buf[2] = 0x80 | (ch & 0x3F);
		return 3;
	} else {
		buf[0] = 0xF0 | (ch >> 18 & 0x07);
		buf[1] = 0x80 | (ch >> 12 & 0x3F);
		buf[2] = 0x80 | (ch >> 6 & 0x3F);
		buf[3] = 0x80 | (ch & 0x3F);
		return 4;
	}
}

void
ksh_trainmarkov(ksh_model_t *model, const char *str)
{
	ksh_u32char buf[4] = {0};
	ksh_u32char ch = 0;
	int i = 0;
	while (str[i] != 0) {
		int len = utf8_readcharacter(&ch, &str[i]);
		if (len < 0) { // skip over invalid characters i dont care
			i++;
			continue;
		}
		i += len;
		// teach buffer->ch
		ksh_makeassociation(model, buf, ch);
		// push ch to buffer for next loop
		memmove(&buf[0], &buf[1], 3*sizeof(ksh_u32char));
		buf[3] = ch;
	}
	// after the string has been studied, teach to end on it
	ksh_makeassociation(model, buf, 0);
}

void
ksh_createstring(ksh_model_t *model, char *buf, size_t bufsize)
{
	ksh_u32char name[4] = {0};
	ksh_u32char ch = 0;
	int i = 0;
	while (i < (bufsize-1)) {
		ch = ksh_getcontinuation(model, name);
		if (ch == 0)
			break;
		// write character as utf-8
		char encoded[4];
		int len = utf8_writecharacter(ch, encoded);
		if ((i+len+1) >= bufsize)
			break;
		memcpy(&buf[i], encoded, len);
		i += len;
		memmove(&name[0], &name[1], 3*sizeof(ksh_u32char));
		name[3] = ch;
	}
	buf[i] = 0;
}

/* unsigned leb128:
 * split the number into groups of 7 bits, starting from the lsb.
 * then for every 7-bit group, starting from the lsb, set the eighth bit
 * if there's another group after it, then send that as a byte.
 * 1234 -> 10011010010 -> 1001 1010010 -> (11010010, 00001001)
 * dec     bin            4321 7654321     8         8
 *                       byte2   byte1      [byte1]   [byte2]
 */

int
leb128_encode(uint64_t n, unsigned char *buf)
{
	int i = 0;
	do {
		buf[i] = n & 0x7f;
		n >>= 7;
		if (n > 0)
			buf[i] |= 0x80;
		i++;
	} while (n > 0);
	return i;
}

int
leb128_decode(uint64_t *n, unsigned char *buf)
{
	int i = 0;
	*n = 0;
	while (1) {
		*n |= (buf[i] & 0x7F) << (i*7);
		i++;
		if (!(buf[i-1] & 0x80))
			break;
	}
	return i;
}

/*
 * FILE FORMAT
 * +- HEADER <l\x05\x01\x04> -> l is lib, 514 is koishi
 * +- VERSION <\x02> -> starting from 2 because yes, also let's make it leb128
 * |                    for no reason because no chance this has over 127 versions anyway
 * +- for each RULE
 * |  +- RULE.NAME -> 4 or whatever utf-8 chars
 * |  +- for each CONT in RULE
 * |  |  +- CONT.CHAR -> utf-8 char
 * |  |  +- CONT.PROP -> leb128-encoded probability
 * |  +- RULE END MARKER <\x00\x00> -> looks like a continuation with 0 prop,
 * |                                   which can't happen naturally and can thus be a marker
 * +- EOF MARKER <\xFF> -> is not valid utf-8, and can be differentiated from RULE.NAME
 * Note: RULES and CONTS do not have a specified order
 */
void
ksh_savemodel(ksh_model_t *model, FILE *f)
{
	fwrite("l\x05\x01\x04\x02", sizeof(char), 5, f); // HEADER + VERSION
	char buf[10]; // longest possible leb128 repr is 10 bytes for 64 bits
	int l;
	for (uint64_t i = 0; i < (1<<model->mapsize); i++) {
		for(ksh_rule_t *rule = model->hashmap[i]; rule != NULL; rule = rule->next) { // for each RULE
			for (int i = 0; i < 4; i++) { // RULE.NAME
				l = utf8_writecharacter(rule->name[i], buf);
				fwrite(buf, sizeof(char), l, f);
			}
			for (int i = 0; i < KSH_CONTINUATIONS_PER_HEADER; i++) { // for each CONT in RULE (1)
				if (rule->probability[i]) {
					l = utf8_writecharacter(rule->character[i], buf); // CONT.CHAR
					fwrite(buf, sizeof(char), l, f);
					l = leb128_encode(rule->probability[i], buf); // CONT.PROP
					fwrite(buf, sizeof(char), l, f);
				}
			}
			for(ksh_continuations_t *c = rule->cont; c != NULL; c = c->next) {
				for (int i = 0; i < KSH_CONTINUATIONS_PER_STRUCT; i++) { // for each CONT in RULE (2)
					if (c->probability[i]) {
						l = utf8_writecharacter(c->character[i], buf); // CONT.CHAR
						fwrite(buf, sizeof(char), l, f);
						l = leb128_encode(c->probability[i], buf); // CONT.PROP
						fwrite(buf, sizeof(char), l, f);
					}
				}
			}
			fwrite("\x00\x00", sizeof(char), 2, f); // RULE END MARKER
		}
	}
	fwrite("\xFF", sizeof(char), 1, f); // EOF MARKER
}

int
ksh_loadmodel(ksh_model_t *model, FILE *f)
{
#define READ_WITH_SEEK(_RWS_BUF, _RWS_SIZE, _RWS_N, _RWS_F) \
			do { \
				long _RWS_oldpos = ftell(_RWS_F); \
				if (fread(_RWS_BUF, _RWS_SIZE,  _RWS_N, _RWS_F) == 0) \
					return -1; \
				fseek(f, _RWS_oldpos, SEEK_SET); \
			} while (0);
	char buf[10];
	int l;
	fread(buf, sizeof(char), 4, f);
	if (0 != memcmp("l\x05\x01\x04", buf, 4)) // check for header
		return -1; // todo: actual error types, maybe errno?

	uint64_t version;
	if (fread(buf, sizeof(char), 10, f) == 0)
			return -1;
	l = leb128_decode(&version, buf);
	if (l < 0)
		return -1; // unexpected EOF
	fseek(f, l-10, SEEK_CUR);
	if (version != 2)
		return -1;

	while (1) {
		ksh_u32char name[4];
		for (int i = 0; i < 4; i++) {
			READ_WITH_SEEK(buf, sizeof(char), 4, f); // warning: exits on unexpected eof.
			l = utf8_readcharacter(&name[i], buf);
			if (l < 0) {
				int tmp = fgetc(f);
				if (tmp == 0xFF) { // eof marker
					goto loadmodel_eof; // exiting nested loop
				}
				return -1; // invalid character
			}
			fseek(f, l, SEEK_CUR);
		}
		Df("[ldr] rn%4x%4x%4x%4x", name[0], name[1], name[2], name[3]);
		ksh_rule_t *rule = create_rule(model, name, NULL);
		struct cont c = {.ptr=0, .i=-1};
		while (1) {
			// read character
			ksh_u32char ch;
			READ_WITH_SEEK(buf, sizeof(char), 4, f);
			l = utf8_readcharacter(&ch, buf);
			if (l < 0)
				return -1; // invalid character
			fseek(f, l, SEEK_CUR);

			// read probability
			uint64_t prop;
			READ_WITH_SEEK(buf, sizeof(char), 10, f);
			l = leb128_decode(&prop, buf);
			if (l < 0)
				return -1; // unexpected EOF
			fseek(f, l, SEEK_CUR);

			if (prop == 0) {
				if (ch == 0) {
					break; // RULE END MARKER
				}
				return -10; // prop cannot be 0
			}
			rule->probtotal += prop;
			append_cont(rule, &c);
			if (c.ptr) {
				c.ptr->character[c.i] = ch;
				c.ptr->probability[c.i] = prop;
			} else {
				rule->character[c.i] = ch;
				rule->probability[c.i] = prop;
			}
		}
	}
	loadmodel_eof:
	return 0;
}