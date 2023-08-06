/*
 * Copyright © 2022 Michael Smith <mikesmiffy128@gmail.com>
 * Copyright © 2022 Willian Henrique <wsimanbrazil@yahoo.com.br>
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

#ifndef INC_CMETA_H
#define INC_CMETA_H

#include "../os.h"
#include "vec.h"

struct cmeta;

const struct cmeta *cmeta_loadfile(const os_char *f);

/*
 * Iterates through all the #include directives in a file, passing each one in
 * turn to the callback cb.
 */
void cmeta_includes(const struct cmeta *cm,
		void (*cb)(const char *f, bool issys, void *ctxt), void *ctxt);

/*
 * Iterates through all commands and variables declared using the macros in
 * con_.h, passing each one in turn to the callback cb.
 */
void cmeta_conmacros(const struct cmeta *cm,
		void (*cb)(const char *name, bool isvar, bool unreg));

/*
 * Looks for a feature description macro in file, returning the description
 * string if it exists, an empty string if the feature is defined without a
 * user-facing description, and null if source file does not define a feature.
 */
const char *cmeta_findfeatmacro(const struct cmeta *cm);

/*
 * the various kinds of feature specficiation macros, besides the feature
 * declaration macro itself
 */
enum cmeta_featmacro {
	CMETA_FEAT_REQUIRE,
	CMETA_FEAT_REQUIREGD,
	CMETA_FEAT_REQUIREGLOBAL,
	CMETA_FEAT_REQUEST,
	CMETA_FEAT_PREINIT,
	CMETA_FEAT_INIT,
	CMETA_FEAT_END
};

/*
 * Iterates through all feature dependency macros and init/end/preinit
 * indicators, passing each bit of information to the callback cb.
 *
 * PREINT, INIT and END macros don't pass anything to param.
 *
 * This one takes a context pointer, while the others don't, because this is all
 * cobbled together without much consistent abstraction.
 */
void cmeta_featinfomacros(const struct cmeta *cm, void (*cb)(
		enum cmeta_featmacro type, const char *param, void *ctxt), void *ctxt);

/*
 * Iterates through all event-related macros and takes note of which events are
 * defined, giving a callback for each.
 */
void cmeta_evdefmacros(const struct cmeta *cm, void (*cb)(const char *name,
		const char *const *params, int nparams, bool predicate));
/*
 * Iterates through all event-related macros and gives a callback for each event
 * that is handled by the given module.
 */
void cmeta_evhandlermacros(const struct cmeta *cm, const char *modname,
		void (*cb)(const char *evname, const char *modname));

struct msg_member {
	const char* name;
	const char* len_offset;
	const char* members;
	int members_offset;
	char key[16];
	int token_type;
	int item_type;
	int deref;
	int arr_len;
};
struct vec_member VEC(struct msg_member);

/*
 * Iterates through all demo custom messages declared using macros, passing
 * message metadata to a callback
 */ 
void cmeta_msgmacros(const struct cmeta *cm, void (*cb)(char *msgdef,
		int msgdeflen, const char *msgname, const char *msgtype,
		struct vec_member *msgmembers));
#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
