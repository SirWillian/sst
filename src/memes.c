/*
 * Copyright © 2021 Willian Henrique <wsimanbrazil@yahoo.com.br>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdbool.h>

#include "con_.h"
#include "factory.h"
#include "hook.h"
#include "os.h"
#include "vcall.h"

typedef void *(*VCALLCONV f_GetNetChannelInfo)(void *);
typedef bool (*VCALLCONV f_RegisterMessage)(void *, void *);
typedef char *(*VCALLCONV f_GetMsgName)(void *);
typedef int (*VCALLCONV f_GetMsgType)(void *);

void *engine_iface;
static f_RegisterMessage orig_RegisterMessage;
static f_GetNetChannelInfo get_net_chan;

void **net_chan_vtable = 0;

static bool VCALLCONV hook_RegisterMessage(void *this, void *msg) {
	f_GetMsgType type_func = (f_GetMsgType)(*(*((void ***)msg) + 7));
	f_GetMsgName name_func = (f_GetMsgName)(*(*((void ***)msg) + 9));
	int m_type = type_func(msg);
	char *m_name = name_func(msg);

	con_msg("%d %s\n", m_type, m_name);

	return orig_RegisterMessage(this, msg);
}

DEF_CCMD_HERE(sst_hook_register_message, "Try to hook CNetChan::RegisterMessage", 0) {
	void *net_chan = get_net_chan(engine_iface);
    con_msg("net_chan: %p\n", net_chan);
	net_chan_vtable = *(void ***)net_chan;
    con_msg("net_chan_vtable: %p\n", net_chan_vtable);

    //return;
	// CNetChan::RegisterMessage
	// vtable offset 29
	if (!os_mprot(net_chan_vtable, 32 * sizeof(void *),
				  PAGE_EXECUTE_READWRITE)) {
#ifdef _WIN32
		char err[128];
		OS_WINDOWS_ERROR(err);
#else
		const char *err = strerror(errno);
#endif
		con_warn("memes: couldn't unprotect CNetChan vtable: %s\n", err);
		return;
	}
    // 56 57 8b 7c 24 0c 8b 07 8b 50 1c 8b f1
    static const uchar bytes[] = {0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x8b, 0x07, 0x8b, 0x50, 0x1c, 0x8b, 0xf1};
    if (memcmp((uchar*)(net_chan_vtable[29]), bytes, sizeof(bytes))) {
        con_warn("memes: Couldn't find RegisterMessage\n");
        con_warn("Expected: ");
        for(int i=0; i<sizeof(bytes); i++)
            con_warn("0x%x ", bytes[i]);
        con_warn("\nGot: ");
        uchar *func_pointer = (uchar *)(net_chan_vtable[29]);
        for(int i=0; i<sizeof(bytes); i++)
            con_warn("0x%x ", func_pointer[i]);
        con_warn("\n");
        return;
    }
	orig_RegisterMessage = (f_RegisterMessage)hook_vtable(
		net_chan_vtable, 29, (void *)&hook_RegisterMessage);
    con_msg("orig_RegisterMessage: %p\n", orig_RegisterMessage);
}

bool memes_init(void) {
	engine_iface = factory_engine("VEngineClient013", 0);
	// GetNetChannelInfo
	// engine vtable offset 74 for l4d1 / l4d2
	int get_net_chan_offset = 74;
	if (!engine_iface) {
		// l4d2 2.0.0.0 uses a different interface version
		engine_iface = factory_engine("VEngineClient014", 0);
		get_net_chan_offset = 38;
		if (!engine_iface) {
	        con_warn("memes: couldn't find VEngineClient interface\n");
        	return false;
		}
    }
    con_msg("engine_iface: %p vtable: %p\n", engine_iface, *(void ***)engine_iface);
    con_msg("get_net_chan: %p\n", (*(void ***)engine_iface)[get_net_chan_offset]);

	get_net_chan = (f_GetNetChannelInfo)((*(void ***)engine_iface)[get_net_chan_offset]);
	return true;
}

void memes_end(void) {
	unhook_vtable(net_chan_vtable, 29, (void *)orig_RegisterMessage);
}
