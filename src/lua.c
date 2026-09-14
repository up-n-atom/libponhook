#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lua.h"
#include "hook.h"
#include "util.h"

typedef struct lua_State lua_State;
typedef long lua_Long;
typedef int (*lua_CFunction)(lua_State *);

static lua_State *(*L_newstate)(void);
static void  (*L_openlibs)(lua_State *);
static int   (*L_loadfile)(lua_State *, const char *);
static int   (*L_loadbuffer)(lua_State *, const char *, size_t, const char *);
static int   (*L_pcall)(lua_State *, int, int, int);
static int   (*L_type)(lua_State *, int);
static const char *(*L_tolstring)(lua_State *, int, size_t *);
static size_t (*L_objlen)(lua_State *, int);
static int   (*L_toboolean)(lua_State *, int);
static void  (*L_pushlstring)(lua_State *, const char *, size_t);
static void  (*L_pushinteger)(lua_State *, lua_Long);
static void  (*L_pushboolean)(lua_State *, int);
static void  (*L_pushcclosure)(lua_State *, lua_CFunction, int);
static void  (*L_getfield)(lua_State *, int, const char *);
static void  (*L_setfield)(lua_State *, int, const char *);
static void  (*L_settop)(lua_State *, int);
static void  (*L_close)(lua_State *);

/* Lua 5.1 pseudo-index / helper macros */
#define LUA_GLOBALS51	(-10002)
static void L_getglobal(lua_State *L, const char *n)
{ L_getfield(L, LUA_GLOBALS51, n); }
static void L_setglobal(lua_State *L, const char *n)
{ L_setfield(L, LUA_GLOBALS51, n); }
static const char *L_tostring(lua_State *L, int i)
{ return L_tolstring(L, i, NULL); }
/* binary-safe: Lua strings may embed NULs */
static const char *L_tobin(lua_State *L, int i, size_t *len)
{ return L_tolstring(L, i, len); }
static void L_pop(lua_State *L, int n)
{ L_settop(L, -(n)-1); }
static void L_clear(lua_State *L)
{ L_settop(L, 0); }

#define LUA_TNONE	(-1)
#define LUA_TNIL	0
#define LUA_TBOOLEAN	1
#define LUA_TSTRING	4
#define LUA_TFUNCTION	6

static lua_State *script_state;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static bool lua_active;

#define SCRIPT_PATH "/etc/omci_hook/"
#define SCRIPT_FILE "omci_hook.lua"
#define SCRIPT_BOOTSTRAP "package.path='" SCRIPT_PATH "?.lua;'..package.path"

#define OMCI_HDR_LEN 8

typedef struct {
	pthread_mutex_t *mutex;
	bool locked;
} lock_t;

static void lock_release(lock_t *lk)
{
	if (lk->locked) {
		pthread_mutex_unlock(lk->mutex);
	}
}

#define LOCK(name, m) \
	lock_t name __attribute__((cleanup(lock_release))) = { .mutex = (m) }; \
	(name).locked = (pthread_mutex_lock((name).mutex) == 0)

static void resolve_lua_symbols(void *h)
{
	DLSYM(h, L_newstate,     "luaL_newstate");
	DLSYM(h, L_openlibs,     "luaL_openlibs");
	DLSYM(h, L_loadfile,     "luaL_loadfile");
	DLSYM(h, L_loadbuffer,   "luaL_loadbuffer");
	DLSYM(h, L_pcall,        "lua_pcall");
	DLSYM(h, L_type,         "lua_type");
	DLSYM(h, L_tolstring,    "lua_tolstring");
	DLSYM(h, L_objlen,       "lua_objlen");
	DLSYM(h, L_toboolean,    "lua_toboolean");
	DLSYM(h, L_pushlstring,  "lua_pushlstring");
	DLSYM(h, L_pushinteger,  "lua_pushinteger");
	DLSYM(h, L_pushboolean,  "lua_pushboolean");
	DLSYM(h, L_pushcclosure, "lua_pushcclosure");
	DLSYM(h, L_getfield,     "lua_getfield");
	DLSYM(h, L_setfield,     "lua_setfield");
	DLSYM(h, L_settop,       "lua_settop");
	DLSYM(h, L_close,        "lua_close");
	if (!L_newstate || !L_loadfile || !L_pcall || !L_getfield ||
	    !L_pushlstring || !L_pushinteger || !L_tolstring || !L_type) {
		L_newstate = NULL;
		L_openlibs = NULL;
		L_loadfile = NULL;
		L_loadbuffer = NULL;
		L_pcall = NULL;
		L_type = NULL;
		L_tolstring = NULL;
		L_objlen = NULL;
		L_toboolean = NULL;
		L_pushlstring = NULL;
		L_pushinteger = NULL;
		L_pushboolean = NULL;
		L_pushcclosure = NULL;
		L_getfield = NULL;
		L_setfield = NULL;
		L_settop = NULL;
		L_close = NULL;
	}
}

static int cb_send(lua_State *L)
{
	size_t n = 0;
	const char *f = L_tobin(L, 1, &n);

	if (!f || n == 0 || n > OMCI_FRAME_MAX) {
		L_pushboolean(L, 0);
		return 1;
	}
	L_pushboolean(L, hook_send((const uint8_t *)f, (uint16_t)n) == 0);
	return 1;
}

static int cb_log(lua_State *L)
{
	const char *s = L_tostring(L, 1);
	if (s) {
		LOG("lua: %s\n", s);
	}
	return 0;
}

static bool pending_on_ready;
static void call(const lock_t *lk, const char *fn);

static int load_script(const lock_t *lk)
{
	lua_State *new_state;

	if (!lk->locked) {
		LOGE("%s called without state lock\n", __func__);
		return -1;
	}

	new_state = L_newstate();
	if (!new_state) {
		return -1;
	}
	L_openlibs(new_state);

	if (L_loadbuffer(new_state, SCRIPT_BOOTSTRAP, sizeof(SCRIPT_BOOTSTRAP) - 1,
			 "=(omci_hook)") == 0) {
		L_pcall(new_state, 0, 0, 0);
	}
	L_clear(new_state);

	L_pushcclosure(new_state, cb_send, 0); L_setglobal(new_state, "omci_hook_send");
	L_pushcclosure(new_state, cb_log, 0);  L_setglobal(new_state, "omci_hook_log");

	if (L_loadfile(new_state, SCRIPT_PATH SCRIPT_FILE) != 0 ||
	    L_pcall(new_state, 0, 0, 0) != 0) {
		LOGE("%s error: %s\n", SCRIPT_FILE, L_tostring(new_state, -1));
		if (L_close) {
			L_close(new_state);
		}
		return -1;
	}

	script_state = new_state;
	call(lk, "on_load");
	pending_on_ready = true;
	return 0;
}

enum fn_stack { FN_POP, FN_KEEP };

static bool fn_exists(const lock_t *lk, const char *name, enum fn_stack mode)
{
	if (!lk->locked) {
		LOGE("%s(%s) without state lock\n", __func__, name);
		return false;
	}
	L_getglobal(script_state, name); /* pushes the global's value */
	if (L_type(script_state, -1) != LUA_TFUNCTION) {
		L_pop(script_state, 1); /* wasn't a function: undo that push */
		return false;
	}
	if (mode == FN_POP) {
		L_pop(script_state, 1); /* only bool */
	}
	return true;
}

#define LUA_SO "liblua.so.5.1.5"

static bool check_script(const lock_t *lk)
{
	return fn_exists(lk, "on_rx", FN_POP)     ||
	       fn_exists(lk, "on_tx", FN_POP)     ||
	       fn_exists(lk, "on_load", FN_POP)   ||
	       fn_exists(lk, "on_unload", FN_POP) ||
	       fn_exists(lk, "on_reset", FN_POP)  ||
	       fn_exists(lk, "on_reboot", FN_POP) ||
	       fn_exists(lk, "on_ready", FN_POP);
}

static void *lua_handle;

void lua_attach(void)
{
	lua_handle = dlopen(LUA_SO, RTLD_NOW | RTLD_GLOBAL);
	if (!lua_handle) {
		LOGW("lua runtime not found (" LUA_SO "), scripting disabled\n");
		return;
	}
	resolve_lua_symbols(lua_handle);
	if (!L_newstate) {
		LOGE("lua runtime lacks expected symbols, scripting disabled\n");
		dlclose(lua_handle);
		lua_handle = NULL;
		return;
	}

	{
		LOCK(lk, &state_lock);
		if (!lk.locked) {
			LOGE("state lock acquisition failed, scripting disabled\n");
		} else if (access(SCRIPT_PATH SCRIPT_FILE, R_OK) == 0 && load_script(&lk) == 0 &&
			   check_script(&lk)) {
			lua_active = true;
			LOG("lua scripting active: %s\n", SCRIPT_PATH SCRIPT_FILE);
		} else {
			LOG("%s is incompatible (%s), scripting disabled\n", SCRIPT_FILE, SCRIPT_PATH SCRIPT_FILE);
		}
	} /* release lock */

	if (!lua_active) {
		dlclose(lua_handle);
		lua_handle = NULL;
	}
}

#define MT_MASK  0x1F /* Message Type Mask */
#define MT_RESET 0xF  /* MIB Reset */

enum msg_result lua_call_on_rx(const uint8_t *msg, uint16_t len,
				uint8_t *out_msg, uint16_t *out_len)
{
	enum msg_result res = MSG_PASS;

	if (!lua_active || len < OMCI_HDR_LEN) {
		return MSG_PASS;
	}

	LOCK(lk, &state_lock);
	if (!lk.locked) {
		return MSG_PASS;
	}

	if (pending_on_ready && !((msg[2] & MT_MASK) == MT_RESET)) {
		pending_on_ready = false;
		call(&lk, "on_ready");
	}

	if (fn_exists(&lk, "on_rx", FN_KEEP)) {
		int err;

		res = MSG_DROP;
		L_pushlstring(script_state, (const char *)msg, len);
		err = L_pcall(script_state, 1, 1, 0);
		if (err) {
			LOGE("on_rx error: %s\n", L_tostring(script_state, -1));
			L_clear(script_state);
			return MSG_PASS;
		}
		switch (L_type(script_state, -1)) {
		case LUA_TNIL:
			res = MSG_DROP;
			break;
		case LUA_TSTRING: {
			size_t n = 0;
			const char *f = L_tobin(script_state, -1, &n);

			if (f && n == len && memcmp(f, msg, len) == 0) {
				res = MSG_PASS;
			} else if (f && n > 0 && n <= OMCI_FRAME_MAX) {
				memcpy(out_msg, f, n);
				*out_len = (uint16_t)n;
				res = MSG_EDIT;
			}
			break;
		}
		default:
			break;
		}
		L_clear(script_state);
	}
	return res;
}

static void call(const lock_t *lk, const char *fn)
{
	int err;

	if (!fn_exists(lk, fn, FN_KEEP)) {
		return;
	}

	err = L_pcall(script_state, 0, 0, 0);
	if (err) {
		LOGE("%s error: %s\n", fn, L_tostring(script_state, -1));
	}
	L_clear(script_state);
}

void lua_call_on_reset(void)
{
	if (!lua_active) {
		return;
	}
	LOCK(lk, &state_lock);
	if (!lk.locked) {
		return;
	}
	call(&lk, "on_reset");
	pending_on_ready = true;
}

void lua_call_on_reboot(void)
{
	if (!lua_active) {
		return;
	}
	LOCK(lk, &state_lock);
	if (!lk.locked) {
		return;
	}
	call(&lk, "on_reboot");
}

enum msg_result lua_call_on_tx(const uint8_t *msg, uint16_t len,
				uint8_t *out_msg, uint16_t *out_len)
{
	enum msg_result res = MSG_PASS;

	if (!lua_active || len < OMCI_HDR_LEN) {
		return MSG_PASS;
	}

	LOCK(lk, &state_lock);
	if (!lk.locked) {
		return MSG_PASS;
	}

	if (fn_exists(&lk, "on_tx", FN_KEEP)) {
		int err;

		res = MSG_DROP;
		L_pushlstring(script_state, (const char *)msg, len);
		err = L_pcall(script_state, 1, 1, 0);
		if (err) {
			LOGE("on_tx error: %s\n", L_tostring(script_state, -1));
			L_clear(script_state);
			return MSG_PASS;
		}
		switch (L_type(script_state, -1)) {
		case LUA_TNIL:
			res = MSG_DROP;
			break;
		case LUA_TSTRING: {
			size_t n = 0;
			const char *f = L_tobin(script_state, -1, &n);
			if (f && n == len && memcmp(f, msg, len) == 0) {
				res = MSG_PASS;
			} else if (f && n > 0 && n <= OMCI_FRAME_MAX) {
				memcpy(out_msg, f, n);
				*out_len = (uint16_t)n;
				res = MSG_EDIT;
			}
			break;
		}
		default:
			break;
		}
		L_clear(script_state);
	}
	return res;
}

void lua_detach(void)
{
	LOCK(lk, &state_lock);
	if (!lk.locked) {
		LOGE("state lock acquisition failed during teardown\n");
	} else {
		pending_on_ready = false;
		if (script_state) {
			call(&lk, "on_unload");
		}
		if (script_state && L_close) {
			L_close(script_state);
		}
		script_state = NULL;
		lua_active = false;

		if (lua_handle) {
			dlclose(lua_handle);
			lua_handle = NULL;
		}
	}
}
