/* This file is dedicated to the public domain. */

// We get most of the libc functions from ucrtbase.dll, which comes with
// Windows, but for some reason a few of the intrinsic-y things are part of
// vcruntime, which does *not* come with Windows!!! We can statically link just
// that part but it adds ~12KiB of random useless bloat to our binary. So, let's
// just implement the handful of required things here instead. This is only for
// release/non-debug builds; we want the extra checks in Microsoft's CRT when
// debugging.
//
// Is it actually reasonable to have to do any of this? Of course not.

// TODO(opt): this feels like a sad implementation, can we do marginally better?
int memcmp(const void *x_, const void *y_, unsigned int sz) {
	const char *x = x_, *y = y_;
	for (unsigned int i = 0; i < sz; ++i) {
		if (x[i] > y[i]) return 1;
		if (x[i] < y[i]) return -1;
	}
	return 0;
}

void *memcpy(void *restrict x, const void *restrict y, unsigned int sz) {
#ifdef __clang__
	__asm__ volatile (
		"rep movsb\n" :
		"+D" (x), "+S" (y), "+c" (sz) :
		:
		"memory"
	);
#else // terrible fallback just in case someone wants to use this with MSVC
	char *restrict xb = x; const char *restrict yb = y;
	for (unsigned int i = 0; i < sz; ++i) xb[i] = yb[i];
#endif
	return x;
}

void *memmove(void *x, const void *y, unsigned int sz) {
	char *xb = x; const char *yb = y;
#ifdef __clang__
	// specify direction for rep movsb and adjust input accordingly
	// FIXME: should probably write this if statement in asm too and potentially
	// set the flag direction onto the register instead of using cld/std
	if (y < x) {
		xb += sz - 1; yb += sz - 1;
		__asm__ volatile ("std\n" ::: "cc");
	}
	else {
		__asm__ volatile ("cld\n" ::: "cc");
	}
	__asm__ volatile (
		"rep movsb\n" :
		"=D" (xb), "=S" (yb), "=c" (sz) :
		"0" (xb), "1" (yb), "2" (sz) :
		"memory"
	);
#else
	if (y < x)
		for (unsigned int i = sz - 1; i >= 0; --i) xb[i] = yb[i];
	else
		for (unsigned int i = 0; i < sz; ++i) xb[i] = yb[i];
#endif
	return x;
}

int __stdcall _DllMainCRTStartup(void *inst, unsigned int reason,
		void *reserved) {
	return 1;
}

#ifdef __clang__
__attribute__((used))
#endif
int _fltused = 1;

// vi: sw=4 ts=4 noet tw=80 cc=80
