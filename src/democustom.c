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

#include <stdlib.h>
#include <string.h>

#include "bitbuf.h"
#include "con_.h"
#include "demorec.h"
#include "engineapi.h"
#include "errmsg.h"
#include "feature.h"
#include "gamedata.h"
#include "intdefs.h"
#include "mem.h"
#include "mpack.h"
#include "ppmagic.h"
#include "vcall.h"
#include "x86.h"
#include "x86util.h"

FEATURE()
REQUIRE(demorec)
REQUIRE_GAMEDATA(vtidx_GetEngineBuildNumber)
REQUIRE_GAMEDATA(vtidx_IsRecording)
REQUIRE_GAMEDATA(vtidx_RecordPacket)

static int nbits_msgtype, nbits_datalen;

// engine limit is 255, we use 2 bytes for header + round the bitstream to the
// next whole byte, which gives 3 bytes overhead hence 252 here.
#define CHUNKSZ 252

static union {
	char x[CHUNKSZ + /*7*/ 8]; // needs to be multiple of of 4!
	bitbuf_cell _align; // just in case...
} bb_buf;
static struct bitbuf bb = {
	{bb_buf.x}, sizeof(bb_buf), sizeof(bb_buf) * 8, 0, false, false, "SST"
};

struct member_meta {
	char key[16];
	size_t offsetof;
	mpack_token_type_t token_type;
	mpack_token_type_t item_type; // for arrays
	size_t size; // size in bytes for numbers and array elements
	int length; // for static arrays
	size_t length_offsetof; // for dynamic arrays
	int member_count; // for maps
	struct member_meta *members; // for maps
	u8 deref;
};

struct struct_packer {
	void *object;
	int member_count;
	int msg_type;
	struct member_meta *members;
	struct struct_packer *prev;
};
#define _PACKER_MEMBERS(obj_type) _demomsg_##obj_type##_members
#define _PACKER_COUNT(obj_type) \
	sizeof(_PACKER_MEMBERS(obj_type)) / sizeof(struct member_meta)
#define MSG_PACKER(obj, obj_type, msgtype) \
	(struct struct_packer) { \
		.object = obj, \
		.msg_type = msgtype, \
		.member_count = _PACKER_COUNT(obj_type), \
		.members = _PACKER_MEMBERS(obj_type), \
		.prev = NULL \
	}

static inline void *get_struct_member(mpack_node_t *parent, void *object,
		struct member_meta *meta) {
	char *member = (char *)object + meta->offsetof;
	for (int i = 0; i < meta->deref; i++) {
		member = *(char **)member;
		if (member == NULL) return NULL;
	}
	if (parent->tok.type == MPACK_TOKEN_ARRAY)
		member += parent->pos * meta->size;
	return (void *)member;
}

static inline mpack_uint32_t get_token_length(mpack_token_type_t tok_type,
		struct struct_packer *packer, struct member_meta *meta, void *member) {
	if (meta->length_offsetof)
		return *(u32 *)((char *)packer->object + meta->length_offsetof);
	if (tok_type == MPACK_TOKEN_STR) return strlen(member);
	return meta->length;
}

// TODO: do serialization that isn't stack based
static void struct_pack_enter(mpack_parser_t *parser, mpack_node_t *node) {
	mpack_node_t *parent = MPACK_PARENT_NODE(node);
	struct struct_packer *packer;
	packer = (struct struct_packer *)parser->data.p;
	// just started packing
	// pack an array for msg type and content
	if (!parent) {
		node->tok = mpack_pack_array(2);
		return;
	}
	// we've packed the container array
	// pack the msg type and then the content
	if (parent->tok.type == MPACK_TOKEN_ARRAY && !MPACK_PARENT_NODE(parent)) {
		if (!parent->pos) node->tok = mpack_pack_number(packer->msg_type);
		else node->tok = mpack_pack_map(packer->member_count);
		return;
	}
	// process the message content
	int metadata_pos = parent->data[0].u;
	struct member_meta meta = packer->members[metadata_pos];

	if (!parent->key_visited) {
		// keys are always strings
		size_t key_size = strlen(meta.key);
		if (parent->tok.type == MPACK_TOKEN_STR) {
			node->tok = mpack_pack_chunk(meta.key, key_size);
		}
		else {
			node->tok = mpack_pack_str(key_size);
			node->data[0].u = metadata_pos;
		}
		return;
	}

	void *member = get_struct_member(parent, packer->object, &meta);
	if (member == NULL) {
		node->tok = mpack_pack_nil();
		return;
	}
	if (parent->tok.type >= MPACK_TOKEN_STR) {
		u32 length = get_token_length(parent->tok.type, packer, &meta, member);
		node->tok = mpack_pack_chunk(member, length);
		return;
	}

	mpack_token_type_t pack_type = parent->tok.type == MPACK_TOKEN_ARRAY ?
		meta.item_type : meta.token_type;
	switch (pack_type) {
		case MPACK_TOKEN_BOOLEAN:
			node->tok = mpack_pack_boolean(*(bool *)member);
			break;
		// XXX: consider packing all number types with their own pack functions
		case MPACK_TOKEN_UINT:
		case MPACK_TOKEN_SINT:
		case MPACK_TOKEN_FLOAT:
			u64 mask = (1ULL << 8 * meta.size) - 1;
			double number = (double)((*(u64 *)member) & mask);
			node->tok = mpack_pack_number(number);
			break;
		case MPACK_TOKEN_ARRAY:
		case MPACK_TOKEN_STR:
		case MPACK_TOKEN_BIN: {
			// XXX: handle EXT types some day here if needed
			u32 length = get_token_length(pack_type, packer, &meta, member);
			if (pack_type == MPACK_TOKEN_ARRAY)
				node->tok = mpack_pack_array(length);
			else if (pack_type == MPACK_TOKEN_STR)
				node->tok = mpack_pack_str(length);
			else
				node->tok = mpack_pack_bin(length);
			// setting flag to represent the parent map's key_visited state to
			// simplify the logic for packing map keys above, since these types
			// become parents. idk if i should be touching this flag but the
			// library doesn't seem to touch it for anything but maps so this
			// shouldn't cause bugs in the library
			node->key_visited = true;
			node->data[0].u = metadata_pos;
			break;
		}
		case MPACK_TOKEN_MAP:
			packer = malloc(sizeof(struct struct_packer));
			packer->object = member;
			packer->member_count = meta.member_count;
			packer->members = meta.members;
			packer->prev = parser->data.p;
			parser->data.p = packer;
			node->tok = mpack_pack_map(packer->member_count);
			break;
		default:
			break;
	}
}

static void struct_pack_exit(mpack_parser_t *parser, mpack_node_t *node) {
	mpack_node_t *parent = MPACK_PARENT_NODE(node);
	struct struct_packer *packer = parser->data.p;
	if (parent && MPACK_PARENT_NODE(parent)) {
		if (node->tok.type == MPACK_TOKEN_MAP) {
			parser->data.p = packer->prev;
			free(packer);
		}
		// this simplifies retrieving metadata in the enter cb
		if (parent->tok.type == MPACK_TOKEN_MAP)
			parent->data[0].u = parent->pos;
	}
}

static const void *createhdr(struct bitbuf *msg, int len, bool last) {
	// We pack custom data into user message packets of type "HudText," with a
	// leading null byte which the engine treats as an empty string. On demo
	// playback, the client does a text lookup which fails silently on invalid
	// keys, giving us the rest of the packet to stick in whatever data we want.
	//
	// Big thanks to our resident demo expert, Uncrafted, for explaining what to
	// do here way back when this was first being figured out!
	bitbuf_appendbits(msg, 23, nbits_msgtype); // type: 23 is user message
	bitbuf_appendbyte(msg, 2); // user message type: 2 is HudText
	bitbuf_appendbits(msg, (len + 3) * 8, nbits_datalen); // our data length in bits
	bitbuf_appendbyte(msg, 0); // aforementionied null byte
	bitbuf_appendbyte(msg, 0xAC + last); // arbitrary marker byte to aid parsing
	// store the data itself byte-aligned so there's no need to bitshift the
	// universe (which would be both slower and more annoying to do)
	bitbuf_roundup(msg);
	return msg->buf + (msg->nbits >> 3);
}

typedef void (*VCALLCONV WriteMessages_func)(void *this, struct bitbuf *msg);
static WriteMessages_func WriteMessages = 0;
DECL_VFUNC_DYN(bool, IsRecording)

void democustom_write(const void *buf, int len) {
	if (!VCALL(demorecorder, IsRecording)) return;
	for (; len > CHUNKSZ; len -= CHUNKSZ) {
		createhdr(&bb, CHUNKSZ, false);
		memcpy(bb.buf + (bb.nbits >> 3), buf, CHUNKSZ);
		bb.nbits += CHUNKSZ << 3;
		WriteMessages(demorecorder, &bb);
		bitbuf_reset(&bb);
	}
	createhdr(&bb, len, true);
	memcpy(bb.buf + (bb.nbits >> 3), buf, len);
	bb.nbits += len << 3;
	WriteMessages(demorecorder, &bb);
	bitbuf_reset(&bb);
}

#include <demomsginit.gen.h>

static bool find_WriteMessages(void) {
	const uchar *insns = (*(uchar ***)demorecorder)[vtidx_RecordPacket];
	// RecordPacket calls WriteMessages right away, so just look for a call
	for (const uchar *p = insns; p - insns < 32;) {
		if (*p == X86_CALL) {
			WriteMessages = (WriteMessages_func)(p + 5 + mem_loadoffset(p + 1));
			return true;
		}
		NEXT_INSN(p, "WriteMessages function");
	}
	return false;
}

DECL_VFUNC_DYN(int, GetEngineBuildNumber)

INIT {
	// More UncraftedkNowledge:
	// - usermessage length is:
	//   - 11 bits in protocol 11, or l4d2 protocol 2042
	//   - otherwise 12 bits
	// So here we have to figure out the network protocol version!
	// NOTE: assuming engclient != null as GEBN index relies on client version
	int buildnum = GetEngineBuildNumber(engclient);
	//if (GAMETYPE_MATCHES(L4D2)) { // redundant until we add more GEBN offsets!
		nbits_msgtype = 6;
		// based on Some Code I Read, buildnum *should* be the protocol version,
		// however L4D2 returns the actual game version instead, because sure
		// why not. The only practical difference though is that the network
		// protocol froze after 2042, so we just have to do a >=. Fair enough!
		// TODO(compat): how does TLS affect this? no idea yet
		if (buildnum >= 2042) nbits_datalen = 11; else nbits_datalen = 12;
	//}

	return find_WriteMessages();
}

// vi: sw=4 ts=4 noet tw=80 cc=80
