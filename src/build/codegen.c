/*
 * Copyright © 2023 Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © 2023 Willian Henrique <wsimanbrazil@yahoo.com.br>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED “AS IS” AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../intdefs.h"
#include "../os.h"
#include "cmeta.h"
#include "skiplist.h"
#include "vec.h"

#ifdef _WIN32
#define fS "S"
#include "../3p/openbsd/asprintf.c" // missing from libc; plonked here for now
#else
#define fS "s"
#endif

static void die(const char *s) {
	fprintf(stderr, "codegen: fatal: %s\n", s);
	exit(100);
}

static void die2(const char *s1, char *s2) {
	fprintf(stderr, "codegen: ");
	fprintf(stderr, s1, s2);
	fprintf(stderr, "\n");
	exit(100);
}

#define MAXENT 65536 // arbitrary limit!
static struct conent {
	const char *name;
	bool unreg;
	bool isvar; // false for cmd
} conents[MAXENT];
static int nconents;

#define PUT(name_, isvar_, unreg_) do { \
	if (nconents == sizeof(conents) / sizeof(*conents)) { \
		fprintf(stderr, "codegen: out of space; make ents bigger!\n"); \
		exit(1); \
	} \
	conents[nconents].name = name_; \
	conents[nconents].isvar = isvar_; conents[nconents++].unreg = unreg_; \
} while (0)

static void oncondef(const char *name, bool isvar, bool unreg) {
	PUT(name, isvar, unreg);
}

struct vec_str VEC(const char *);
struct vec_usize VEC(usize);
struct vec_featp VEC(struct feature *);

enum { UNSEEN, SEEING, SEEN };

DECL_SKIPLIST(static, feature, struct feature, const char *, 4)
DECL_SKIPLIST(static, feature_bydesc, struct feature, const char *, 4)
struct feature {
	const char *modname;
	const char *desc;
	const struct cmeta *cm; // backref for subsequent options pass
	struct vec_featp needs;
	// keep optionals in a separate array mainly so we have separate counts
	struct vec_featp wants;
	uint dfsstate : 2; // used for sorting and cycle checking
	bool has_preinit : 1, /*has_init : 1, <- required anyway! */ has_end : 1;
	bool has_evhandlers : 1;
	bool is_requested : 1; // determines if has_ variable needs to be extern
	//char pad : 2;
	//char pad[3];
	struct vec_str need_gamedata;
	struct vec_str need_globals;
	struct skiplist_hdr_feature hdr; // by id/modname
	struct skiplist_hdr_feature_bydesc hdr_bydesc;
};
static inline int cmp_feature(struct feature *e, const char *s) {
	return strcmp(e->modname, s);
}
static inline int cmp_feature_bydesc(struct feature *e, const char *s) {
	for (const char *p = e->desc; ; ++p, ++s) {
		// shortest string first
		if (!*p) return !!*s; if (!*s) return -1;
		// case insensitive sort where possible
		if (tolower((uchar)*p) > tolower((uchar)*s)) return 1;
		if (tolower((uchar)*p) < tolower((uchar)*s)) return -1;
		// prioritise upper-case if same letter
		if (isupper((uchar)*p) && islower((uchar)*s)) return 1;
		if (islower((uchar)*p) && isupper((uchar)*s)) return -1;
	}
	return 0;
}
static inline struct skiplist_hdr_feature *hdr_feature(struct feature *e) {
	return &e->hdr;
}
static inline struct skiplist_hdr_feature_bydesc *hdr_feature_bydesc(
		struct feature *e) {
	return &e->hdr_bydesc;
}
DEF_SKIPLIST(static, feature, cmp_feature, hdr_feature)
DEF_SKIPLIST(static, feature_bydesc, cmp_feature_bydesc, hdr_feature_bydesc)
static struct skiplist_hdr_feature features = {0};
// sort in two different ways, so we can alphabetise the user-facing display
// NOTE: not all features will show up in this second list!
static struct skiplist_hdr_feature_bydesc features_bydesc = {0};

static void onfeatinfo(enum cmeta_featmacro type, const char *param,
		void *ctxt) {
	struct feature *f = ctxt;
	switch (type) {
		case CMETA_FEAT_REQUIRE:; bool optional = false; goto dep;
		case CMETA_FEAT_REQUEST: optional = true;
dep:		struct feature *dep = skiplist_get_feature(&features, param);
			if (optional) dep->is_requested = true;
			if (!dep) {
				fprintf(stderr, "codegen: error: feature `%s` tried to depend "
						"on non-existent feature `%s`\n", f->modname, param);
				exit(1);
			}
			if (!vec_push(optional ? &f->wants : &f->needs, dep)) {
				die("couldn't allocate memory");
			}
			break;
		case CMETA_FEAT_REQUIREGD:;
			struct vec_str *vecp = &f->need_gamedata;
			goto push;
		case CMETA_FEAT_REQUIREGLOBAL:
			vecp = &f->need_globals;
push:		if (!vec_push(vecp, param)) die("couldn't allocate memory");
			break;
		case CMETA_FEAT_PREINIT: f->has_preinit = true; break;
		case CMETA_FEAT_END: f->has_end = true; break;
		case CMETA_FEAT_INIT:; // nop for now, I guess
	}
}

DECL_SKIPLIST(static, event, struct event, const char *, 4)
	struct event {
	usize name; // string, but tagged pointer - see below
	const char *const *params;
	int nparams;
	//char pad[4];
	struct vec_usize handlers; // strings, but with tagged pointers - see below
	struct skiplist_hdr_event hdr;
};
static inline int cmp_event(struct event *e, const char *s) {
	return strcmp((const char *)(e->name & ~1ull), s);
}
static inline struct skiplist_hdr_event *hdr_event(struct event *e) {
	return &e->hdr;
}
DEF_SKIPLIST(static, event, cmp_event, hdr_event)
static struct skiplist_hdr_event events = {0};

static void onevdef(const char *name, const char *const *params, int nparams,
		bool predicate) {
	struct event *e = skiplist_get_event(&events, name);
	if (!e) {
		struct event *e = malloc(sizeof(*e));
		if (!e) die("couldn't allocate memory");
		// hack: using unused pointer bit to distinguish the two types of event
		e->name = (usize)name | predicate;
		e->params = params; e->nparams = nparams;
		e->handlers = (struct vec_usize){0};
		e->hdr = (struct skiplist_hdr_event){0};
		skiplist_insert_event(&events, name, e);
	}
	else {
		fprintf(stderr, "codegen: error: duplicate event definition `%s`\n",
				name);
		exit(2);
	}
}

static void onevhandler(const char *evname, const char *modname) {
	struct event *e = skiplist_get_event(&events, evname);
	if (!e) {
		fprintf(stderr, "codegen: error: module `%s` trying to handle "
				"non-existent event `%s`\n", modname, evname);
		exit(2);
	}
	usize taggedptr = (usize)modname;
	struct feature *f = skiplist_get_feature(&features, modname);
	f->has_evhandlers = true;
	// hack: using unused pointer bit to determine whether a handler is tied to
	// a feature and thus conditional. relies on malloc alignment!
	if (f) taggedptr |= 1ull;
	// NOTE: not bothering to check for more than one handler in a file.
	// compiler will get that anyway.
	if (!vec_push(&e->handlers, taggedptr)) die("couldn't allocate memory");
}

#define MAXDATA 32 // arbitrary limit!
static struct dem_data {
	const char *name;
	struct vec_member msg_members;
	bool ismsg, dynlen;
} demdata[MAXDATA] = {{0}};
static int ndem;

static void onmsgdef(const char *n, bool msg, bool dyn, struct vec_member *m) {
	if (ndem == sizeof(demdata) / sizeof(*demdata)) {
		fprintf(stderr, "codegen: out of space; make msgs bigger!\n");
		exit(1);
	}
	demdata[ndem].name = n;
	demdata[ndem].ismsg = msg;
	demdata[ndem].dynlen = dyn;
	demdata[ndem++].msg_members = *m;
}

struct passinfo {
	const struct cmeta *cm;
	const os_char *path;
};
static struct vec_passinfo VEC(struct passinfo) pass2 = {0};

#define _(x) \
	if (fprintf(out, "%s\n", x) < 0) die("couldn't write to file");
#define F(f, ...) \
	if (fprintf(out, f "\n", __VA_ARGS__) < 0) die("couldn't write to file");
#define H_() \
	_( "/* This file is autogenerated by "__FILE__". DO NOT EDIT! */")
#define H() H_() _( "")

static struct vec_featp endstack = {0}; // stack for reversing order

static void featdfs(FILE *out, struct feature *f) {
	if (f->dfsstate == SEEN) return;
	if (f->dfsstate == SEEING) {
		// XXX: could unwind for full cycle listing like in build.
		// purely being lazy by not doing that here, and assuming there won't
		// actually be cycles anyway, because this is not a general purpose tool
		// and people working on this codebase are very smart.
		fprintf(stderr, "codegen: error: dependency cycle found at feature `%s`\n",
				f->modname);
		exit(2);
	}
	f->dfsstate = SEEING;
	// easier to do wants first, then we can do the conditional counter nonsense
	// without worrying about how that fits in...
	for (struct feature *const *pp = f->wants.data;
			pp - f->wants.data < f->wants.sz; ++pp) {
		featdfs(out, *pp);
	}
F( "	char status_%s = FEAT_OK;", f->modname);
	const char *else_ = "";
	if (f->needs.sz == 1) {
		featdfs(out, f->needs.data[0]);
F( "	if (status_%s != FEAT_OK) status_%s = FEAT_REQFAIL;",
		f->needs.data[0]->modname, f->modname)
		else_ = "else ";
	}
	else if (f->needs.sz > 1) {
		for (struct feature *const *pp = f->needs.data;
				pp - f->needs.data < f->needs.sz; ++pp) {
			featdfs(out, *pp);
		}
F( "	bool metdeps_%s =", f->modname)
		for (struct feature *const *pp = f->needs.data;
				pp - f->needs.data < f->needs.sz; ++pp) {
F( "		status_%s == FEAT_OK%s", (*pp)->modname,
		pp - f->needs.data == f->needs.sz - 1 ? ";" : " &&") // dumb but oh well
		}
F( "	if (!metdeps_%s) status_%s = FEAT_REQFAIL;", f->modname, f->modname)
		else_ = "else ";
	}
	if (f->has_preinit) {
F( "	%sif (!_feature_preinit_%s()) status_%s = FEAT_PREFAIL;", else_,
		f->modname, f->modname);
		else_ = "else ";
	}
	for (const char **pp = f->need_gamedata.data;
			pp - f->need_gamedata.data < f->need_gamedata.sz; ++pp) {
F( "	%sif (!has_%s) status_%s = FEAT_NOGD;", else_, *pp, f->modname)
		else_ = "else "; // blegh
	}
	for (const char **pp = f->need_globals.data;
			pp - f->need_globals.data < f->need_globals.sz; ++pp) {
F( "	%sif (!%s) status_%s = FEAT_NOGLOBAL;", else_, *pp, f->modname)
		else_ = "else "; // blegh 2
	}
F( "	%sif (!_feature_init_%s()) status_%s = FEAT_FAIL;", else_, f->modname,
		f->modname)
	if (f->has_end || f->has_evhandlers || f->is_requested) {
F( "	has_%s = status_%s == FEAT_OK;", f->modname, f->modname)
	}
	if (!vec_push(&endstack, f)) die("couldn't allocate memory");
	f->dfsstate = SEEN;
}

static void cmdinit(FILE *out) {
	for (const struct conent *p = conents; p - conents < nconents; ++p) {
F( "extern struct con_%s *%s;", p->isvar ? "var" : "cmd", p->name)
	}
_( "")
_( "static void regcmds(void) {")
	for (const struct conent *p = conents; p - conents < nconents; ++p) {
		if (p->isvar) {
F( "	initval(%s);", p->name)
		}
		if (!p->unreg) {
F( "	con_reg(%s);", p->name)
		}
	}
_( "}")
_( "")
_( "static void freevars(void) {")
	for (const struct conent *p = conents; p - conents < nconents; ++p) {
		if (p->isvar) {
F( "	extfree(%s->strval);", p->name)
		}
	}
_( "}")
}

static void featureinit(FILE *out) {
	// XXX: I dunno whether this should just be defined in sst.c. It's sort of
	// internal to the generated stuff hence tucking it away here, but that's at
	// the cost of extra string-spaghettiness
_( "enum {")
_( "	FEAT_OK,")
_( "	FEAT_REQFAIL,")
_( "	FEAT_PREFAIL,")
_( "	FEAT_NOGD,")
_( "	FEAT_NOGLOBAL,")
_( "	FEAT_FAIL")
_( "};")
_( "")
_( "static const char *const featmsgs[] = {")
_( "	\" [     OK!     ] %s\\n\",")
_( "	\" [   skipped   ] %s (requires another feature)\\n\",")
_( "	\" [   skipped   ] %s (not applicable or useful)\\n\",")
_( "	\" [ unsupported ] %s (missing gamedata)\\n\",")
_( "	\" [   FAILED!   ] %s (failed to access engine)\\n\",")
_( "	\" [   FAILED!   ] %s (error in initialisation)\\n\"")
_( "};")
_( "")
	for (struct feature *f = features.x[0]; f; f = f->hdr.x[0]) {
		if (f->has_preinit) {
F( "extern bool _feature_preinit_%s(void);", f->modname)
		}
F( "extern bool _feature_init_%s(void);", f->modname)
		if (f->has_end) {
F( "extern bool _feature_end_%s(void);", f->modname)
		}
		if (f->is_requested) {
F( "bool has_%s = false;", f->modname)
		}
		else if (f->has_end || f->has_evhandlers) {
F( "static bool has_%s = false;", f->modname)
		}
	}
_( "")
_( "static void initfeatures(void) {")
	for (struct feature *f = features.x[0]; f; f = f->hdr.x[0]) featdfs(out, f);
_( "")
	// note: old success message is moved in here, to get the ordering right
_( "	con_colourmsg(&(struct rgba){64, 255, 64, 255},")
_( "			LONGNAME \" v\" VERSION \" successfully loaded\");")
_( "	con_colourmsg(&(struct rgba){255, 255, 255, 255}, \" for game \");")
_( "	con_colourmsg(&(struct rgba){0, 255, 255, 255}, \"%s\\n\", ")
_( "		gameinfo_title);")
_( "	struct rgba white = {255, 255, 255, 255};")
_( "	struct rgba green = {128, 255, 128, 255};")
_( "	struct rgba red   = {255, 128, 128, 255};")
_( "	con_colourmsg(&white, \"---- List of plugin features ---\\n\");");
	for (const struct feature *f = features_bydesc.x[0]; f;
			f = f->hdr_bydesc.x[0]) {
F( "	con_colourmsg(status_%s == FEAT_OK ? &green : &red,", f->modname)
F( "			featmsgs[(int)status_%s], \"%s\");", f->modname, f->desc)
	}
_( "}")
_( "")
_( "static void endfeatures(void) {")
	for (struct feature **pp = endstack.data + endstack.sz - 1;
			pp - endstack.data >= 0; --pp) {
		if ((*pp)->has_end) {
F( "	if (has_%s) _feature_end_%s();", (*pp)->modname, (*pp)->modname)
		}
	}
_( "}")
_( "")
}

static void evglue(FILE *out) {
	for (const struct event *e = events.x[0]; e; e = e->hdr.x[0]) {
_( "")
		// gotta break from the string emit macros for a sec in order to do the
		// somewhat more complicated task sometimes referred to as a "for loop"
		fprintf(out, "%s_%s(", e->name & 1 ? "bool CHECK" : "void EMIT",
				(const char *)(e->name & ~1ull));
		for (int n = 0; n < (int)e->nparams - 1; ++n) {
			fprintf(out, "typeof(%s) a%d, ", e->params[n], n + 1);
		}
		if (e->nparams && strcmp(e->params[0], "void")) {
			fprintf(out, "typeof(%s) a%d", e->params[e->nparams -  1],
					e->nparams);
		}
		else {
			// just unilaterally doing void for now. when we're fully on C23
			// eventually we can unilaterally do nothing instead
			fputs("void", out);
		}
_( ") {")
		for (usize *pp = e->handlers.data;
				pp - e->handlers.data < e->handlers.sz; ++pp) {
			const char *modname = (const char *)(*pp & ~1ull);
			fprintf(out, "\t%s _evhandler_%s_%s(", e->name & 1 ? "bool" : "void",
					modname, (const char *)(e->name & ~1ull));
			for (int n = 0; n < (int)e->nparams - 1; ++n) {
				fprintf(out, "typeof(%s) a%d, ", e->params[n], n + 1);
			}
			if (e->nparams && strcmp(e->params[0], "void")) {
				fprintf(out, "typeof(%s) a%d", e->params[e->nparams -  1],
						e->nparams);
			}
			else {
				fputs("void", out);
			}
			fputs(");\n\t", out);
			// conditional and non-conditional cases - in theory could be
			// unified a bit but this is easier to make output relatively pretty
			// note: has_* variables are already included by this point (above)
			if (e->name & 1) {
				if (*pp & 1) fprintf(out, "if (has_%s && !", modname);
				else fprintf(out, "if (!");
				fprintf(out, "_evhandler_%s_%s(", modname,
						(const char *)(e->name & ~1ull));
				// XXX: much repetitive drivel here
				for (int n = 0; n < (int)e->nparams - 1; ++n) {
					fprintf(out, "a%d,", n + 1);
				}
				if (e->nparams && strcmp(e->params[0], "void")) {
					fprintf(out, "a%d", e->nparams);
				}
				fputs(")) return false;\n", out);
			}
			else {
				if (*pp & 1) fprintf(out, "if (has_%s) ", modname);
				fprintf(out, "_evhandler_%s_%s(", modname,
						(const char *)(e->name & ~1ull));
				for (int n = 0; n < (int)e->nparams - 1; ++n) {
					fprintf(out, "a%d,", n + 1);
				}
				if (e->nparams && strcmp(e->params[0], "void")) {
					fprintf(out, "a%d", e->nparams);
				}
				fputs(");\n", out);
			}
		}
		if (e->name & 1) fputs("\treturn true;\n", out);
_( "}")
	}
}

/**
 * Steps to rewrite this:
 * X Generate functions that do token pasting for numbers and fixed size strings (no alloc)
 * X Store chain of macros per member
 * X Generate functions that can write pointers
 * X Store size information for arrays
 * X Generate functions that can write arrays
 * X Implement struct members
 * X Handle DEMO_MSG vs DEMO_STRUCT
 * - Implement length checking and allocations
 *  X Just non-deref types (int, uint, float, double)
 *  - Strings
 *  - Arrays
 *  - Maps
 * - Decide how to handle nulls
 * - Try to optimize which functions are called
 */


// state variables shared between the demomsg functions below
static int tabdepth = 0;
static char tabs[MAXTYPES];
static const u8 typesize[] = {0, 1, 9, 9, 5, 9, 5, 5, 5, 0, 5, 5};

#define T(f, ...) F("%.*s" f, tabdepth, tabs, __VA_ARGS__)

static inline void writetype(FILE *out, const struct msg_member *m,
		const char *varname, const char *nextvar, int typedepth) {
	switch(m->type_chain[typedepth]) {
		case MSG_BOOLEAN:
T( "	msg_putbool(buf++, %s);", varname) break;
		case MSG_INT:
T( "	buf += msg_puts(buf, %s);", varname) break;
		case MSG_ULONG:
T( " 	buf += msg_putu(buf, %s);", varname) break;
		case MSG_FLOAT:
T( " 	msg_putf(buf, %s); buf += 5;", varname) break;
		case MSG_DOUBLE:
T( " 	buf += msg_putd(buf, %s);", varname) break;
		case MSG_STR:
		case MSG_DYN_STR:
T( "	int %s_len = strlen(%s);", m->key, varname)
T( "	buf += msg_putssz(buf, %s_len);", m->key)
T( "	memcpy(buf, %s, %s_len); buf += %s_len;", varname, m->key, m->key)
			break;
		case MSG_MAP:
T( "	buf += _msg_write_%s(buf, &%s);", m->map_type, varname) break;
		case MSG_PTR:
T( "	typeof(*%s) %s = *%s;", varname, nextvar, varname) break;
		case MSG_ARRAY: {
			const char *size_str = m->member_len[typedepth];
T( "	buf += msg_putasz(buf, %s);", size_str)
T( "	for (typeof(&*%s) x = %s; x - %s < %s; x++) {", varname, varname,
		varname, size_str)
			bool arrnext = m->type_chain[typedepth+1] == MSG_ARRAY;
T( "		typeof(%s*%s) %s = *x;", arrnext ? "&*" : "", varname, nextvar)
			tabdepth++;
		} break;
		case MSG_DYN_ARRAY: {
			const char *size_str, *memblen = m->member_len[typedepth];
			if (asprintf((char **)&size_str, "msg->%s", memblen) == -1)
				die("failed to allocate array size str");
T( "	buf += msg_putasz(buf, %s);", size_str)
T( "	for (typeof(%s) x = %s; x - %s < %s; x++) {", varname, varname,
		varname, size_str)
T( "		typeof(*%s) %s = *x;", varname, nextvar)
			tabdepth++;
		} break;
		default:
			fprintf(stderr, "codegen: failed to set type for msg member"
				" %s (type %d)\n", m->name, typedepth);
			exit(1);
	}
}

static inline void msgwrite(FILE *out, const struct dem_data *d) {
F( "static int _msg_write_%s(unsigned char *buf, struct %s *msg) {",
		d->name, d->name)
_( "	unsigned char *start = buf;\n")
	if (d->ismsg) {
_( "	msg_putasz4(buf++, 2);")
F( "	msg_puti7(buf++, _demomsg_%s);", d->name)
	}
F( "	buf += msg_putmsz(buf, %u);", d->msg_members.sz)
	for (const struct msg_member *m = d->msg_members.data;
			m - d->msg_members.data < d->msg_members.sz; ++m) {
F( "\n	msg_putssz5(buf++, %u);", m->key_len)
F( "	memcpy(buf, \"%s\", %u); buf += %d;", m->key, m->key_len, m->key_len)
		printf("%s", m->name);
		for(int i=0; m->type_chain[i]!=0; i++) printf(" %d", m->type_chain[i]);
		printf("\n");

		char *varname, *nextvar;
		if (asprintf(&varname, "msg->%s", m->name) == -1 ||
				asprintf(&nextvar, "%s_0", m->key) == -1)
			die("couldn't allocate variable name");
		// unroll first iteration to handle variable name
		writetype(out, m, varname, nextvar, 0);
		sprintf(varname, "%s_/", m->key);
		for(int typedepth = 1; typedepth < m->type_depth; typedepth++) {
			varname[m->key_len + 1]++; nextvar[m->key_len + 1]++;
			writetype(out, m, varname, nextvar, typedepth);
		}
		while(tabdepth > 0) {
			tabdepth--;
T( "	}")
		}
	}
_( "\n	return buf-start;")
_( "}")
}

static inline void msglen(FILE *out, const struct dem_data *d) {
F( "static int _msg_len_%s(struct %s *msg) {", d->name, d->name)
	int fixedlen = 2*d->ismsg + 5; // (msg type + map) header
	if (d->dynlen)
_( "	int dynlen = 0;")
	for (const struct msg_member *m = d->msg_members.data;
			m - d->msg_members.data < d->msg_members.sz; ++m) {
		char *varname, *nextvar;
		bool finaltypedyn = m->dynamic_len[m->type_depth - 1];
		if (finaltypedyn) {
			if (asprintf(&varname, "%s_0", m->key) == -1 ||
					asprintf(&nextvar, "%s_1", m->key) == -1)
				die("couldn't allocate variable name");
F(" 	typeof(msg->%s) %s = msg->%s;", m->name, varname, m->name)
			int i = 0;
			for (; i < m->type_depth - 1; i++) {
				if (m->type_chain[i] == MSG_ARRAY) {
T( "	for (typeof(*%s) %s = *%s; %s - *%s < %s; %s++) {", varname, nextvar,
		varname, nextvar, varname, m->member_len[i], nextvar)
					tabdepth++;
				}
				else if (m->type_chain[i] == MSG_DYN_ARRAY) {
T( "	for (typeof(*%s) %s = *%s; %s - *%s < msg->%s; %s++) {", varname,
		nextvar, varname, nextvar, varname, m->member_len[i], nextvar)
					tabdepth++;
				}
				else /*if (m->type_chain[i] == MSG_PTR)*/ {
T( "	typeof(*%s) %s = *%s;", varname, nextvar, varname)
				}
				varname[m->key_len + 1]++; nextvar[m->key_len + 1]++;
			}
			if (m->type_chain[i] == MSG_MAP) {
T( "	dynlen += _msg_len_%s(%s);", m->map_type, varname)
			}
			else /* if (m->type_chain[i] == MSG_DYN_STR)*/ {
T( "	dynlen += strlen(%s) + 5;", varname)
			}
			while(tabdepth > 0) {
				tabdepth--;
T( "	}")
			}
		}
		else if (m->has_dyn_array) {
			int parens = 0;
			fprintf(out, "\tint %s_len = ", m->key);
			for (int i = 0; i < m->type_depth - 1; i++) {
				if (m->type_chain[i] != MSG_PTR) {
					parens++;
					if (m->type_chain[i] == MSG_DYN_ARRAY)
						fprintf(out, "(5 + msg->%s * ", m->member_len[i]);
					else /* if (m->type_chain[i] == MSG_ARRAY) */
						fprintf(out, "(5 + %s * ", m->member_len[i]);
				}
				int valuelen = typesize[i];
				if (m->type_chain[i] == MSG_STR)
					valuelen += atoi(m->member_len[i]) - 1;
				fprintf(out, "%d", valuelen);
				while(parens--) fputc(')', out);
				fputs(";\n", out);
			}
		}
		else {
			int i = m->type_depth - 1;
			int valuelen = typesize[i];
			// only relevant on the first iteration
			if (m->type_chain[i] == MSG_STR)
				valuelen += atoi(m->member_len[i]) - 1;
			while (i--) {
				if (m->type_chain[i] == MSG_ARRAY)
					valuelen *= atoi(m->member_len[i]);
				valuelen += typesize[i];
			}
			fixedlen += valuelen;
		}
		fixedlen += m->key_len + 1;
		// u8 finaltype = m->type_chain[m->type_depth - 1];
		// int valuelen = finaltype != MSG_STR ? typesize[finaltype] :
		// 		atoi(m->member_len[m->type_depth - 1]) - 1 + 5;
		// fixedlen += valuelen + m->key_len + 1;
	}
F( "	return %d%s;", fixedlen, d->dynlen ? " + dynlen" : "")
_( "}")
}
#undef T

#define _GENFILE(outfile, filepath, func, ...) \
	outfile = fopen(filepath, "wb"); \
	if (!outfile) die2("couldn't open %s", filepath); \
	H() \
	func(outfile, __VA_ARGS__); \
	if (fclose(outfile) == EOF) die2("couldn't fully write %s", filepath)

#define GENFILE(outfile, filename) \
	_GENFILE(outfile, ".build/include/"#filename".gen.h", filename)

static void _customdata(FILE * out, const struct dem_data *d) {
F( "#ifndef INC_MSG_%s_H", d->name)
F( "#define INC_MSG_%s_H\n", d->name)
	// include functions for members with map type
	for (const struct msg_member *m = d->msg_members.data;
			m - d->msg_members.data < d->msg_members.sz; ++m) {
		// XXX: this will include the same file multiple times
		// the include guards prevent this from failing spectacularly
		if (m->type_chain[m->type_depth-1] == MSG_MAP) {
F( "#include <msg/%s.gen.h>", m->map_type);
		}
	}

	// forward declare the function that msglen will generate
F( "static int _msg_len_%s(struct %s *msg);", d->name, d->name)
	msgwrite(out, d);
	//msglen(out, d);
_( "\n#endif")
}

static void customdata(void) {
	FILE *out;
	char *outname;
	memset(tabs, '\t', MAXTYPES);
	for (const struct dem_data *d = demdata; d - demdata < ndem; ++d) {
		if (asprintf(&outname, ".build/include/msg/%s.gen.h", d->name) == -1)
			die("couldn't allocate file name");
		_GENFILE(out, outname, _customdata, d);
	}
}

int OS_MAIN(int argc, os_char *argv[]) {
	for (++argv; *argv; ++argv) {
		const struct cmeta *cm = cmeta_loadfile(*argv);
		if (!cm) {
			fprintf(stderr, "codegen: fatal: couldn't load file %" fS "\n",
					*argv);
			exit(100);
		}
		cmeta_conmacros(cm, &oncondef);
		cmeta_evdefmacros(cm, &onevdef);
		if (!vec_push(&pass2, ((struct passinfo){cm, *argv}))) {
			die("couldn't allocate memory");
		}
	}

	// we have to do a second pass for features and event handlers. also,
	// there's a bunch of terrible garbage here. don't stare for too long...
	for (struct passinfo *pi = pass2.data; pi - pass2.data < pass2.sz; ++pi) {
		// XXX: I guess we should cache these by name or something!
		const struct cmeta *cm = pi->cm;
#ifdef _WIN32
		int arglen = wcslen(pi->path);
		char *p = malloc(arglen + 1);
		if (!p) die("couldn't allocate string");
		// XXX: Unicode isn't real, it can't hurt you.
		for (const ushort *q = pi->path; q - pi->path < arglen; ++q) {
			p[q - pi->path] = *q; // ugh this is stupid
		}
		p[arglen] = '\0';
#else
		const char *p = p->path;
#endif
		const char *lastslash = p - 1;
		for (; *p; ++p) {
#ifdef _WIN32
			if (*p == '/' || *p == '\\') {
#else
			if (*p == '/') {
#endif
				lastslash = p;
			}
		}
		int len = strlen(lastslash + 1);
		if (len <= 3 || lastslash[len - 1] != '.' || lastslash[len] != 'c') {
			fprintf(stderr, "filenames should end in .c probably\n");
			exit(2);
		}
		char *modname = malloc(len - 1);
		if (!modname) die("couldn't allocate string");
		memcpy(modname, lastslash + 1, len - 2);
		modname[len - 2] = '\0';
		// ugh. same dumb hacks from compile scripts
		if (!strcmp(modname, "con_")) {
			free(modname); // might as well
			modname = "con";
		}
		else if (!strcmp(modname, "sst")) {
			continue; // I guess???
		}
		const char *featdesc = cmeta_findfeatmacro(cm);
		if (featdesc) {
			struct feature *f = malloc(sizeof(*f));
			if (!f) die("couldn't allocate memory");
			*f = (struct feature){
				.modname = modname,
				.desc = featdesc[0] ? featdesc : 0,
				.cm = cm
			};
			skiplist_insert_feature(&features, modname, f);
			if (f->desc) {
				skiplist_insert_feature_bydesc(&features_bydesc, f->desc, f);
			}
		}
		cmeta_evhandlermacros(cm, modname, &onevhandler);
	}
	// yet another pass because I am stupid and don't want to think harder :)
	for (struct feature *f = features.x[0]; f; f = f->hdr.x[0]) {
		cmeta_featinfomacros(f->cm, &onfeatinfo, f);
	}

	FILE *out;
	GENFILE(out, cmdinit);
	GENFILE(out, featureinit);
	GENFILE(out, evglue);
	customdata();

	return 0;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
