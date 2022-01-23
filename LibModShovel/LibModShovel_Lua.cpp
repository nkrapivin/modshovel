#include "pch.h"
#include "LibModShovel_Lua.h"
#include <iostream>
#include "LibModShovel_Console.h"
#include "LibModShovel_GMAutogen.h"
#include "LibModShovel_MethodAutogen.h"

/*
* LibModShovel by nkrapivindev
* Class: LMS::Lua
* Purpose:: All-things-Lua!
* TODO: refactor code to be compatible with LuaJIT...?
*/

/* ah yes, the lua context */
lua_State* LMS::Lua::luactx{};
CInstance* LMS::Lua::curSelf{};
CInstance* LMS::Lua::curOther{};

/* need for asset init: */
TRoutine F_ObjectGetName{};
TRoutine F_SpriteGetName{};
TRoutine F_RoomGetName{};
TRoutine F_FontGetName{};
TRoutine F_AudioGetName{};
TRoutine F_PathGetName{};
TRoutine F_TimelineGetName{};
TRoutine F_TilesetGetName{};
TRoutine F_ShaderGetName{};
TRoutine F_ScriptGetName{};


TRoutine F_VariableStructSet{};
TRoutine F_VariableStructGet{};
TRoutine F_VariableStructNamesCount{};
TRoutine F_ArraySetOwner{};
TRoutine F_JSGetInstance{}; // instance id to YYObjectBase
TRoutine F_Method{};

TRoutine F_VariableGlobalSet{};
TRoutine F_DsMapCreate{};
TRoutine F_DsMapReplace{};
TRoutine F_DsMapDelete{};
TRoutine F_DsMapFindValue{};
TRoutine F_DsExists{};

TGetVarRoutine GV_EventType{};
TGetVarRoutine GV_EventSubtype{};
TGetVarRoutine GV_EventObject{};
TGetVarRoutine GV_InstanceCount{};
TGetVarRoutine GV_InstanceId{};
TGetVarRoutine GV_ProgramDirectory{};

std::mutex FreeMtMutex{};

struct RBuiltinData {
	CInstance* owner;
	RVariableRoutine* var;
	LMS::GMBuiltinVariable* extra;
};

std::unordered_map<unsigned long long, PFUNC_YYGML> LMS::Lua::eventOriginalsMap{};
std::unordered_map<std::string, std::pair<RVariableRoutine*, LMS::GMBuiltinVariable*>> LMS::Lua::builtinsMap{};
double LMS::Lua::pinDsMap{ -1.0 };

/* called by LMS when a new array or struct wrapper is created and returned to Lua */
void LMS::Lua::pinYYObjectBase(YYObjectBase* pObj) {
	if (!pObj) {
		Global::throwError("YYObjectBase pointer is null. Cannot pin a NULL object.");
	}

	RValue exists{ 0.0 };
	RValue exargs[]{ RValue{pinDsMap}, RValue{1.0/*ds_type_map*/} };
	F_DsExists(exists, curSelf, curOther, 2, exargs);
	if (exists.val == 0.0) {
		/* ds map destroyed? */
		return;
	}

	RValue refcount{ nullptr };
	/* initialize refcount to 1. */
	RValue refcountargs[]{ pinDsMap, RValue{reinterpret_cast<void*>(reinterpret_cast<LPBYTE>(pObj) + 1)}, RValue{1.0} };
	RValue args[]{ pinDsMap, RValue{reinterpret_cast<void*>(pObj)}, RValue{pObj} };
	/* obtain refcount */
	F_DsMapFindValue(refcount, curSelf, curOther, 2, refcountargs);

	/* no value? */
	if ((refcount.kind & MASK_KIND_RVALUE) == VALUE_UNDEFINED) {
		// object:
		F_DsMapReplace(refcount, curSelf, curOther, 3, args);
		// refcount (key is mem address +1):
		F_DsMapReplace(refcount, curSelf, curOther, 3, refcountargs);
	}
	else {
		/* increment refcount: */
		refcountargs[2] = ++(refcount.val);
		/* update refcount: */
		F_DsMapReplace(refcount, curSelf, curOther, 3, refcountargs);
	}
}

/* only called by '__gc' metamethods in struct/array wrappers. */
void LMS::Lua::unpinYYObjectBase(YYObjectBase* pObj) {
	if (!pObj) {
		Global::throwError("YYObjectBase pointer is null. Cannot unpin a NULL object.");
	}

	RValue exists{ 0.0 };
	RValue exargs[]{ RValue{pinDsMap}, RValue{1.0/*ds_type_map*/} };
	F_DsExists(exists, curSelf, curOther, 2, exargs);
	if (exists.val == 0.0) {
		/* ds map destroyed? */
		return;
	}

	RValue refcount{ nullptr };
	/* initialize refcount to 1. */
	RValue refcountargs[]{ pinDsMap, RValue{reinterpret_cast<void*>(reinterpret_cast<LPBYTE>(pObj) + 1)}, RValue{1.0} };
	RValue args[]{ pinDsMap, RValue{reinterpret_cast<void*>(pObj)} };
	/* obtain refcount */
	F_DsMapFindValue(refcount, curSelf, curOther, 2, refcountargs);

	if ((refcount.kind & MASK_KIND_RVALUE) == VALUE_UNDEFINED) {
		Global::throwError("Trying to unpin a non-existing YYObjectBase.");
	}
	else {
		/* decrement refcount and update: */
		refcountargs[2] = --(refcount.val);
		F_DsMapReplace(refcount, curSelf, curOther, 3, refcountargs);
		/* do we need to free? */
		if (refcount.val <= 0.0) {
			F_DsMapDelete(refcount, curSelf, curOther, 2, args);
			F_DsMapDelete(refcount, curSelf, curOther, 2, refcountargs);

#ifdef _DEBUG
			std::cout << "Freed pinned YYObjectBase: 0x" << args[1].ptr << std::endl;
#endif
		}
	}
}

std::string LMS::Lua::getProgramDirectory() {
	RValue res{ nullptr };
	GV_ProgramDirectory(curSelf, ARRAY_INDEX_NO_INDEX, &res);
	std::string sres{ res.pString->get() };
	sres += "\\"; // program_directory does not end with a backslash.
	return sres;
}

std::wstring LMS::Lua::stringToWstring(const std::string& str) {
	std::wstring ws{};

	// ws is already empty, don't bother.
	if (str.size() == 0)
		return ws;

	int siz{ MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), str.size(), nullptr, 0) };
	if (siz <= 0)
		throw std::runtime_error{ "String conversion failed (1)." };

	ws.resize(siz);

	siz = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), str.size(), ws.data(), siz);
	if (siz <= 0)
		throw std::runtime_error{ "String conversion failed (2)." };

	return ws;
}

std::string LMS::Lua::wstringToString(const std::wstring& str) {
	std::string as{};

	// ws is already empty.
	if (str.size() == 0)
		return as;

	int siz{ WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, str.c_str(), str.size(), nullptr, 0, nullptr, nullptr) };
	if (siz <= 0)
		throw std::runtime_error{ "String conversion failed (1)." };

	as.resize(siz);

	siz = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, str.c_str(), str.size(), as.data(), siz, nullptr, nullptr);
	if (siz <= 0)
		throw std::runtime_error{ "String conversion failed (2)." };

	return as;
}

int LMS::Lua::apiDebugEnterRepl(lua_State* pL) {
	std::cout << "Entering REPL loop, type 'quit' to continue..." << std::endl;

	while (true) {
		std::string line{};
		std::cout << "> ";
		std::getline(std::cin, line);

		if (line == "quit") {
			break;
		}

		if (line.length() == 0) {
			continue;
		}

		auto before{ lua_gettop(pL) };
		auto ok{ luaL_dostring(pL, line.c_str()) };
		auto after{ lua_gettop(pL) };
		auto diff{ after - before };
		std::cout << "Code: " << ok << std::endl;
		if (diff != 0) {
			std::cout << "Returns:" << std::endl;
			for (auto i{ 0 }; i < diff; ++i) {
				auto msg{ lua_tostring(pL, -1) };
				auto typname{ lua_typename(pL, lua_type(pL, -1)) };
				if (!msg) msg = "<LMS: nullptr string!>";
				if (!typname) typname = "<LMS: invalid type!>";
				// [index:type] = value
				std::cout << "[" << (i + 1) << ":" << typname << "] = " << msg << std::endl;
				lua_pop(pL, 1);
			}
		}
	}

	std::cout << std::endl << "Leaving REPL loop..." << std::endl;
	return 0;
}

void obtainKey(unsigned long long key, int& objId, int& evType, int evSubtype) {
	objId = static_cast<int>(key & 0xFFFFFFFFull);
	evType = static_cast<int>((key >> 32ull) & 0xFFFFull);
	evSubtype = static_cast<int>((key >> 48ull) & 0xFFFFull);
}

unsigned long long genKey(int objId, int evType, int evSubType) {
	return
		static_cast<unsigned long long>(objId)
		| (static_cast<unsigned long long>(evType & 0xffff) << 32ull)
		| (static_cast<unsigned long long>(evSubType & 0xffff) << 48ull);
}

void LMS::Lua::arraySetOwner() {
	RValue res{ nullptr };
	RValue arg[]{ static_cast<int64>(reinterpret_cast<std::intptr_t>(curSelf)) };
	F_ArraySetOwner(res, curSelf, curOther, 1, arg);
}

void LMS::Lua::pushRBuiltinAccessor(lua_State* pL, CInstance* pOwner, const std::string& name) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(lua_newuserdatauv(pL, sizeof(RBuiltinData), 0)) };
	const auto item{ builtinsMap.find(name) };

	if (item == builtinsMap.end()) {
		Global::throwError("Unknown (array?) builtin variable name: " + name);
	}

	rbptr->owner = pOwner;
	rbptr->var = item->second.first;
	rbptr->extra = item->second.second;
	luaL_setmetatable(pL, "__LMS_metatable_RValue_RBuiltin__");
}

std::string LMS::Lua::getEventName(int evtype) {
	static std::string event2Name[]{ "Create", "Destroy", "Alarm", "Step", "Collision", "Keyboard", "Mouse", "Other", "Draw", "KeyPress",
	"KeyRelease", "Trigger", "CleanUp", "Gesture" };

	if (evtype < 0 || evtype > 13) {
		return "<ERROR>";
	}
	else {
		return event2Name[evtype];
	}
}

void LMS::Lua::pushYYObjectBase(lua_State* pL, YYObjectBase* thing) {
	auto yyptr{ reinterpret_cast<RValue**>(lua_newuserdatauv(pL, sizeof(RValue*), 0)) };
	*yyptr = new RValue{ thing };
	pinYYObjectBase((*yyptr)->pObj);
	luaL_setmetatable(pL, "__LMS_metatable_RValue_Struct__");
}

void LMS::Lua::pushYYObjectBase(lua_State* pL, const RValue& rv) {
	auto yyptr{ reinterpret_cast<RValue**>(lua_newuserdatauv(pL, sizeof(RValue*), 0)) };
	*yyptr = new RValue{ rv };
	pinYYObjectBase((*yyptr)->pObj);
	luaL_setmetatable(pL, "__LMS_metatable_RValue_Struct__");
}

int LMS::Lua::scriptCall(lua_State* pL) {
	double mtind{ lua_tonumber(pL, lua_upvalueindex(1)) };
	RValue mvtret{ nullptr };
	RValue rvmtind{ mtind };

	RValue* args{ nullptr };
	RValue** yyc_args{ nullptr };
	std::size_t argc{ static_cast<std::size_t>(lua_gettop(pL)) };
	if (argc > 0) {
		args = new RValue[argc];
		yyc_args = new RValue*[argc];
		for (std::size_t i{ 0 }; i < argc; ++i) {
			args[i] = luaToRValue(pL, 1 + i);
			yyc_args[i] = &args[i];
		}
	}

	mvtret = YYGML_CallMethod(curSelf, curOther, mvtret, static_cast<int>(argc), rvmtind, yyc_args);

	if (argc > 0) {
		delete[] yyc_args;
		delete[] args;
	}

	rvalueToLua(pL, mvtret);
	return 1;
}

LMS::HookBitFlags LMS::Lua::tmpFlags{ LMS::HookBitFlags::SKIP_NONE };

lua_Integer LMS::Lua::findRealArrayLength(lua_State* pL) {
	auto table{ lua_gettop(pL) };
	auto notempty{ false };
	int isnum{ 0 };

	lua_Integer last{ 0 };

	/* loop through the table until we find the LAST key. */
	for (lua_pushnil(pL); lua_next(pL, table) != 0; lua_pop(pL, 1)) {
		auto key{ static_cast<lua_Integer>(lua_tonumberx(pL, -2, &isnum)) };
		notempty = true;

		if (!isnum) {
			return luaL_argerror(pL, table, "The table has non-numeric keys, not an array?");
		}

		last = std::max(last, key);
	}

	/* found! add 1 to turn it into length. */
	return notempty ? (1 + last) : 0;
}

void LMS::Lua::pushYYCScriptArgs(lua_State* pL, int argc, RValue* args[]) {
	lua_newtable(pL);
	if (argc > 0) {
		for (int i{ 0 }; i < argc; ++i) {
			lua_pushinteger(pL, 1 + i);
			rvalueToLua(pL, *(args[i]));
			lua_settable(pL, -3);
		}
	}
}

void LMS::Lua::doScriptHookCall(bool& callorig, bool& callafter, const std::string& prefix, const std::string& stacktracename, RValue& Result, RValue*& newargarr, RValue**& newargs, int& newargc, RValue**& args) {
	if (lua_type(luactx, -1) == LUA_TNIL) return;

	auto tablen{ findRealArrayLength(luactx) };
	for (lua_Integer i{ 0 }; i < tablen; ++i) {
		// we keep a copy of this table on the stack (for reference stuff...)
		pushYYCScriptArgs(luactx, newargc, newargs);

		// function
		lua_pushinteger(luactx, 1 + i);
		lua_gettable(luactx, -3);
		if (lua_type(luactx, -1) == LUA_TNIL) {
			// ignore nil entries.
			lua_pop(luactx, 1);
			continue;
		}

		// self&other
		pushYYObjectBase(luactx, reinterpret_cast<YYObjectBase*>(curSelf));
		if (curSelf != curOther) {
			pushYYObjectBase(luactx, reinterpret_cast<YYObjectBase*>(curOther));
		}
		else {
			// literally duplicate self.
			lua_pushvalue(luactx, -1);
		}

		// args:
		lua_pushvalue(luactx, -4);

		// result:
		rvalueToLua(luactx, Result);

		tmpFlags = HookBitFlags::SKIP_NONE;

		// prepare stacktrace:
		auto actualstname{ stacktracename + "_" + std::to_string(1 + i)};
		SYYStackTrace __stack{ actualstname.c_str(), 1 };

		auto ok{ lua_pcall(luactx, 4, 1, 0) };
		if (ok != LUA_OK) {
			auto msg{ lua_tostring(luactx, -1) };
			if (!msg) msg = "<LMS: unknown error message!>";
			Global::throwError(prefix + msg);
		}

		if (callorig) callorig = (tmpFlags & HookBitFlags::SKIP_ORIG) == 0;
		if (callafter) callafter = (tmpFlags & HookBitFlags::SKIP_AFTER) == 0;
		if (tmpFlags & HookBitFlags::SKIP_TO) {
			lua_pop(luactx, 2); // pop result and args.
			break;
		}

		if (tmpFlags & HookBitFlags::SKIP_NEXT) {
			lua_pop(luactx, 2); // pop result and args.
			++i;
			continue;
		}

		// fetch result:
		Result = luaToRValue(luactx, -1);
		lua_pop(luactx, 1); // pop result

		// fetch args, they *should* be at the stacktop:
		newargc = static_cast<int>(findRealArrayLength(luactx));

		if (newargs != args && newargs) {
			delete[] newargs;
		}

		if (newargarr) {
			delete[] newargarr;
		}

		newargarr = nullptr;
		newargs = nullptr;

		if (newargc > 0) {
			newargarr = new RValue[newargc];
			newargs = new RValue*[newargc];
			for (int i{ 0 }; i < newargc; ++i) {
				lua_pushinteger(luactx, 1 + i);
				lua_gettable(luactx, -2);
				newargarr[i] = luaToRValue(luactx, -1);
				lua_pop(luactx, 1);
				newargs[i] = &newargarr[i];
			}
		}

		lua_pop(luactx, 1); // pop args table that we kept on the stack.
	}
}

RValue& LMS::Lua::HookScriptRoutine(CInstance* selfinst, CInstance* otherinst, RValue& Result, int argc, RValue* args[], unsigned long long index) {
	auto tmpself{ curSelf }, tmpother{ curOther };
	curSelf = selfinst;
	curOther = otherinst;
	arraySetOwner();

	std::string myname{ ScapegoatScripts[index].n };
	std::string beforekey{ myname + "_Before" };
	std::string afterkey{ myname + "_After" };
	auto trampoline{ ScapegoatScripts[index].t };

	int newargc{ argc };
	RValue* newargarr{ nullptr };
	RValue** newargs{ args };

	auto callorig{ true };
	auto callafter{ true };
	auto skiphooks{ false };

	lua_getglobal(luactx, "LMS");

	lua_pushstring(luactx, "Garbage");
	lua_gettable(luactx, -2);

	/* if Garbage is nil, skip hooks. */
	if (lua_type(luactx, -1) == LUA_TNIL) {
		skiphooks = true;
		// the nil garbage will be poped below:
	}

	/* before hooks first! */
	if (!skiphooks) {
		lua_pushstring(luactx, beforekey.c_str());
		lua_gettable(luactx, -2);
		doScriptHookCall(callorig, callafter, "Before", beforekey, Result, newargarr, newargs, newargc, args);
		lua_pop(luactx, 1); // pop before table.
	}

	if (callorig && trampoline) {
		trampoline(selfinst, otherinst, Result, newargc, newargs);
	}

	/* after hooks */
	if (!skiphooks) {
		lua_pushstring(luactx, afterkey.c_str());
		lua_gettable(luactx, -2);
		doScriptHookCall(callorig, callafter, "After", beforekey, Result, newargarr, newargs, newargc, args);
		lua_pop(luactx, 1); // pop after table.
	}

	/* pop garbage and lms */
	lua_pop(luactx, 1); // garbage
	lua_pop(luactx, 1); // lms

	/* deallocate args: */
	if (newargs && newargs != args) delete[] newargs;
	newargs = nullptr;
	if (newargarr) delete[] newargarr;
	newargarr = nullptr;
	tmpFlags = HookBitFlags::SKIP_NONE;

	/* restore curself and curother */
	curSelf = tmpself;
	curOther = tmpother;

	/* restore previous owner */
	arraySetOwner();

	/* pass the Result to the caller */
	return Result;
}

RValue& LMS::Lua::MethodCallRoutine(CInstance* selfinst, CInstance* otherinst, RValue& Result, int argc, RValue* args[], unsigned long long index) {
	auto mykey{ ScapegoatMethods[index].k };

	/* backup self */
	auto tmpself{ curSelf }, tmpother{ curOther };
	curSelf = selfinst;
	curOther = otherinst;
	arraySetOwner();

	lua_getglobal(luactx, "LMS");
	lua_pushstring(luactx, "Garbage");
	lua_gettable(luactx, -2);
	lua_pushstring(luactx, "Methods");
	lua_gettable(luactx, -2);

	auto before{ lua_gettop(luactx) };

	// function:
	lua_pushinteger(luactx, mykey);
	lua_gettable(luactx, -2);

	// self and other:
	pushYYObjectBase(luactx, reinterpret_cast<YYObjectBase*>(selfinst));
	if (selfinst != otherinst) {
		pushYYObjectBase(luactx, reinterpret_cast<YYObjectBase*>(otherinst));
	}
	else {
		lua_pushvalue(luactx, -1);
	}

	// args:
	if (argc > 0) {
		for (auto i{ 0 }; i < argc; ++i) {
			rvalueToLua(luactx, *(args[i]));
		}
	}

	auto ok{ lua_pcall(luactx, 2 + argc, LUA_MULTRET, 0) };
	if (ok != LUA_OK) {
		auto msg{ lua_tostring(luactx, -1) };
		if (!msg) msg = "<LMS: nullptr string!>";
		Global::throwError("Lua execution error in a method:\r\n" + std::string(msg));
	}

	auto after{ lua_gettop(luactx) };
	auto diff{ after - before };
	if (diff != 0) {
		Result = luaToRValue(luactx, -1);
		lua_pop(luactx, 1);
		--diff;
		if (diff > 0) {
			lua_pop(luactx, diff);
		}
	}

	/* stack balance: */
	lua_pop(luactx, 1); // methods
	lua_pop(luactx, 1); // garbage
	lua_pop(luactx, 1); // lms

	/* restore curself and curother */
	curSelf = tmpself;
	curOther = tmpother;

	/* restore previous owner */
	arraySetOwner();

	return Result;
}

void LMS::Lua::FreeMethodAt(long long index) {
	/* since this should come from a PreFree call, and GC is multithreaded, it's better to guard _somehow_ */
	std::unique_lock<std::mutex> lock{ FreeMtMutex };
	/* cache the funchash and free it */
	auto luakey{ ScapegoatMethods[index].k };
	ScapegoatMethods[index].k = 0;
	
	lua_getglobal(luactx, "LMS");
	lua_pushstring(luactx, "Garbage");
	lua_gettable(luactx, -2);

	lua_pushstring(luactx, "Methods");
	lua_newtable(luactx);
	auto newtab{ lua_gettop(luactx) };

	lua_pushstring(luactx, "Methods");
	lua_gettable(luactx, -4);
	
	/* the 'Methods' table is at the stacktop */
	auto mymttab{ lua_gettop(luactx) };

	/*
	* since Lua doesn't let you to fully delete an item from a Table,
	* I end up recreating the table without the needed element ;-;
	*/
	for (lua_pushnil(luactx); lua_next(luactx, mymttab) != 0; lua_pop(luactx, 1)) {
		if (lua_tointeger(luactx, -2) != luakey) {
			lua_pushvalue(luactx, -2);
			lua_pushvalue(luactx, -2); // not a typo, after previous pushvalue key is now at -2.
			lua_settable(luactx, newtab);
		}
	}
	lua_pop(luactx, 1); // methods table

	lua_settable(luactx, -3); // apply new table to LMS.Methods.

	/* stack balance */
	lua_pop(luactx, 1); // garbage
	lua_pop(luactx, 1); // lms
}

void LMS::Lua::doEventHookCall(bool& callorig, bool& callafter, const std::string& prefix, const std::string& stacktracename) {
	if (lua_type(luactx, -1) == LUA_TNIL) return;

	auto tablen{ findRealArrayLength(luactx) };
	for (lua_Integer i{ 0 }; i < tablen; ++i) {
		auto before{ lua_gettop(luactx) };

		lua_pushinteger(luactx, 1 + i);
		lua_gettable(luactx, -2);
		if (lua_type(luactx, -1) == LUA_TNIL) {
			// ignore nil entries.
			lua_pop(luactx, 1);
			continue;
		}

		pushYYObjectBase(luactx, reinterpret_cast<YYObjectBase*>(curSelf));
		if (curSelf != curOther) {
			pushYYObjectBase(luactx, reinterpret_cast<YYObjectBase*>(curOther));
		}
		else {
			lua_pushvalue(luactx, -1);
		}

		// prepare stacktrace:
		auto actualstname{ stacktracename + "_" + std::to_string(1 + i) };
		SYYStackTrace __stack{ actualstname.c_str(), 1 };

		auto ok{ lua_pcall(luactx, 2, LUA_MULTRET, 0) };
		if (ok != LUA_OK) {
			auto msg{ lua_tostring(luactx, -1) };
			if (!msg) msg = "<LMS: unknown error message>";
			Global::throwError(prefix + msg);
		}

		auto after{ lua_gettop(luactx) };
		auto diff{ after - before };

		if (diff != 0) {
			auto action{ HookBitFlags::SKIP_NONE };
			if (lua_type(luactx, -1) == LUA_TNUMBER) action = static_cast<HookBitFlags>(lua_tonumber(luactx, -1));
			lua_pop(luactx, 1);
			--diff;
			if (diff > 0) {
				lua_pop(luactx, diff);
			}

			if (callorig) callorig = (action & HookBitFlags::SKIP_ORIG) == 0;
			if (callafter) callafter = (action & HookBitFlags::SKIP_AFTER) == 0;
			if (action & HookBitFlags::SKIP_TO) {
				break;
			}

			if (action & HookBitFlags::SKIP_NEXT) {
				++i;
			}
		}
	}
}

void LMS::Lua::hookRoutineEvent(CInstance* selfinst, CInstance* otherinst) {
	auto tmpself{ curSelf }, tmpother{ curOther };
	curSelf = selfinst;
	curOther = otherinst;
	arraySetOwner();

	RValue ev_obj{}, ev_type{}, ev_subtype{}, ev_objname{};
	GV_EventObject(curSelf, ARRAY_INDEX_NO_INDEX, &ev_obj);
	GV_EventType(curSelf, ARRAY_INDEX_NO_INDEX, &ev_type);
	GV_EventSubtype(curSelf, ARRAY_INDEX_NO_INDEX, &ev_subtype);
	F_ObjectGetName(ev_objname, curSelf, curOther, 1, &ev_obj);

	std::string stacktracename{"gml_Object_"};
	stacktracename += ev_objname.pString->get();
	stacktracename += "_";
	stacktracename += getEventName(static_cast<int>(ev_type.val));
	stacktracename += "_";
	if (ev_type.val != 4.0 /*collsion event*/) {
		stacktracename += std::to_string(static_cast<int>(ev_subtype.val));
	}
	else {
		stacktracename += curOther->m_pObject->m_pName;
	}

	std::string beforeKey{ stacktracename + "_Before" };
	std::string afterKey{ stacktracename + "_After" };

	unsigned long long key{
		genKey(
			static_cast<int>(ev_obj.val),
			static_cast<int>(ev_type.val),
			static_cast<int>(ev_subtype.val)
		)
	};

	auto callorig{ true };
	auto callafter{ true };
	auto skiphooks{ false };

	auto func{ eventOriginalsMap[key] };

	/* push LMS.Garbage to the stack: */
	lua_getglobal(luactx, "LMS");

	lua_pushstring(luactx, "Garbage");
	lua_gettable(luactx, -2);

	// if LMS.Garbage is nil, skip to the restore part.
	if (lua_type(luactx, -1) == LUA_TNIL) {
		skiphooks = true;
		// it will be poped below:
	}

	/* before hooks first! */
	if (!skiphooks) {
		lua_pushstring(luactx, beforeKey.c_str());
		lua_gettable(luactx, -2);
		doEventHookCall(callorig, callafter, "Fatal Lua error in Before event hook:\r\n", beforeKey);
		lua_pop(luactx, 1); // pop object table (or nil)
	}

	/* call the original if defined: */
	if (callorig && func) {
		func(selfinst, otherinst);
	}

	/* and now, after hooks! */
	if (!skiphooks) {
		lua_pushstring(luactx, afterKey.c_str());
		lua_gettable(luactx, -2);
		doEventHookCall(callorig, callafter, "Fatal Lua error in After event hook:\r\n", afterKey);
		lua_pop(luactx, 1); // pop object table (or nil)
	}

	/* pop lms and garbage */
	lua_pop(luactx, 1); // pop garbage
	lua_pop(luactx, 1); // pop LMS

	/* restore curself and curother */
	curSelf = tmpself;
	curOther = tmpother;

	/* restore previous owner */
	arraySetOwner();
}

double assetAddLoop(lua_State* pL, TRoutine routine, double start = 0.0) {
	double i{ 0.0 };

	try {
		RValue n{};
		RValue args[]{ 0.0 };

		const char* name{ "<undefined>" };

		for (i = start; true; ++i) {
			args[0] = i;
			routine(n, nullptr, nullptr, 1, args);

			name = n.pString->get();
			// if asset does not exist, it's name will be "<undefined>"
			// but since in GM an asset can't start with '<', it's faster to check for the first character.
			if (name && name[0] != '<') {
				lua_pushstring(pL, name);
				lua_pushnumber(pL, i);
				lua_settable(pL, -3);
			}
			else {
				break;
			}
		}
	}
	catch (const YYGMLException&) {
		// shader_get_name throws a C++ exception
		// but we don't want the game to crash (and still want shader names)
		// so eh... yeah...
		// let's ignore all YYGMLExceptions (just do a constant ref and throw it away, won't trigger any destructors)
	}

	return i;
}

static bool isBuiltinVarArray(const char* name) {
	return (
		strcmp(name, "alarm") == 0 ||
		strcmp(name, "view_xport") == 0 ||
		strcmp(name, "view_yport") == 0 ||
		strcmp(name, "view_wport") == 0 ||
		strcmp(name, "view_hport") == 0
	);
}

int LMS::Lua::mtArrayLen(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Array__")) };

	RValue len{ nullptr };
	RValue args[]{ **rptr };
	F_ArrayLength(len, curSelf, curOther, 1, args);

	rvalueToLua(pL, len);
	return 1;
}

int LMS::Lua::mtArrayIndex(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Array__")) };

	// in Lua arrays start at one, in GML at zero, need to decrement the index...
	auto rind{ lua_tonumber(pL, 2) - 1.0 };

	RValue arrlen{ 0.0 };
	F_ArrayLength(arrlen, curSelf, curOther, 1, *rptr);
	if (rind < 0.0 || rind >= arrlen.val) {
		// fix ipairs()
		lua_pushnil(pL);
		return 1;
	}

	RValue rv{ nullptr };
	RValue args[]{ **rptr, rind };
	F_ArrayGet(rv, curSelf, curOther, 2, args);

	rvalueToLua(pL, rv);
	return 1;
}

int LMS::Lua::mtArrayNewindex(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Array__")) };

	RValue dumdum{ nullptr };
	RValue args[]{ **rptr, lua_tonumber(pL, 2) - 1.0, luaToRValue(pL, 3) };
	F_ArraySet(dumdum, curSelf, curOther, 3, args);

	return 0;
}

int LMS::Lua::mtArrayGc(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Array__")) };

	/* decrement array ref: */
	//std::cout << "Freeing array wrapper at " << reinterpret_cast<void*>(*rptr) << std::endl;
	unpinYYObjectBase((*rptr)->pObj);
	delete (*rptr);

	return 0;
}

int LMS::Lua::mtArrayTostring(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Array__")) };

	RValue res{ nullptr };
	F_String(res, curSelf, curOther, 1, *rptr);

	lua_pushfstring(pL, "array:%s", res.pString->get());
	return 1;
}

int LMS::Lua::mtArrayNext(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Array__")) };
	
	RValue arraylen{ 0.0 };
	F_ArrayLength(arraylen, curSelf, curOther, 1, *rptr);

	// -1 cuz if the argument is `nil`, the index increments and we start at 0.
	lua_Integer arrayind{ -1 };
	if (lua_gettop(pL) > 1 && lua_type(pL, 2) != LUA_TNIL) {
		// convert lua array index to gml index.
		// all further operations will use gml array indexing.
		arrayind = static_cast<lua_Integer>(lua_tonumber(pL, 2) - 1.0);
	}

	// advance to next element.
	// so if an index 0 is passed on a 2 length array, we get the [1]th element
	++arrayind;

	// index out of range?
	if (arrayind < 0 || arrayind >= static_cast<lua_Integer>(arraylen.val)) {
		lua_pushnil(pL);
		return 1;
	}

	// otherwise:
	RValue res{};
	RValue args[]{ **rptr, static_cast<double>(arrayind) };
	F_ArrayGet(res, curSelf, curOther, 2, args);

	lua_pushinteger(pL, 1 + arrayind);
	rvalueToLua(pL, res);
	return 2;
}

int LMS::Lua::mtArrayPairs(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Array__")) };

	lua_pushcfunction(pL, &mtArrayNext);
	lua_pushvalue(pL, 1);
	lua_pushnil(pL);
	return 3;
}

int LMS::Lua::mtArrayIpairs(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Array__")) };

	lua_pushcfunction(pL, &mtArrayNext);
	lua_pushvalue(pL, 1);
	lua_pushinteger(pL, 0);
	return 3;
}

int LMS::Lua::mtArrayEq(lua_State* pL) {
	auto rptr1{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Array__")) };
	auto rptr2{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 2, "__LMS_metatable_RValue_Array__")) };

	lua_pushboolean(pL, ((*rptr1)->pArray) == ((*rptr2)->pArray));
	return 1;
}

int LMS::Lua::mtStructIndex(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	if (lua_type(pL, 2) == LUA_TNIL) {
		lua_pushnil(pL);
		return 1;
	}

	if ((*optr)->pObj->m_kind == YYObjectBaseKind::KIND_CINSTANCE) {
		auto n{ lua_tostring(pL, 2) };
		const auto& it{ builtinsMap.find(n) };
		if (it != builtinsMap.end() && it->second.second->arrayLength > 0) {
			// push accessor...
			pushRBuiltinAccessor(pL, reinterpret_cast<CInstance*>((*optr)->pObj), n);
			return 1;
		}
		// otherwise just pass back to variable_struct_set, it works with built-in non-array vars...
		// oh and newindex assignments will go to newindex method of RBuiltin so no need to implement it there...
	}

	RValue res{ nullptr };
	RValue args[]{ **optr, luaToRValue(pL, 2) };

	F_VariableStructGet(res, curSelf, curOther, 2, args);

	rvalueToLua(pL, res);
	return 1;
}

int LMS::Lua::mtStructNewindex(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	luaL_argcheck(pL, lua_type(pL, 2) != LUA_TNIL, 2, "struct new key cannot be nil.");

	RValue res{ nullptr };
	RValue args[]{ **optr, luaToRValue(pL, 2), luaToRValue(pL, 3) };

	F_VariableStructSet(res, curSelf, curOther, 3, args);
	return 0;
}

int LMS::Lua::mtStructGc(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	//std::cout << "Freeing struct wrapper at " << reinterpret_cast<void*>(*optr) << std::endl;
	unpinYYObjectBase((*optr)->pObj);
	delete (*optr);
	
	return 0;
}

int LMS::Lua::mtStructTostring(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	RValue res{ nullptr };
	RValue args[]{ **rptr };
	F_String(res, curSelf, curOther, 1, args);

	lua_pushfstring(pL, "struct:%s", res.pString->get());
	return 1;
}

int LMS::Lua::mtStructLen(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	RValue res{ 0.0 };
	F_VariableStructNamesCount(res, curSelf, curOther, 1, *rptr);

	rvalueToLua(pL, res);
	return 1;
}

int LMS::Lua::mtStructEq(lua_State* pL) {
	auto rptr1{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };
	auto rptr2{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 2, "__LMS_metatable_RValue_Struct__")) };

	lua_pushboolean(pL, ((*rptr1)->pObj) == ((*rptr2)->pObj));
	return 1;
}

int LMS::Lua::mtBuiltinIndex(lua_State* pL) {
	// ignored
	auto __tb{ luaL_checkudata(pL, 1, "__LMS_metatable_RValue_TBuiltin__") };

	if (lua_type(pL, 2) == LUA_TNIL) {
		lua_pushnil(pL);
		return 1;
	}

	auto name{ std::string(lua_tostring(pL, 2)) };
	auto it{ builtinsMap.find(name) };
	if (it == builtinsMap.end()) {
		Global::throwError("index unknown builtin var name! " + name);
		return 0;
	}

	if (it->second.second->arrayLength > 0) {
		// push array accessor...
		pushRBuiltinAccessor(pL, reinterpret_cast<CInstance*>(curSelf), name);
	}
	else {
		// get the thing
		RValue v{ nullptr };
		if (!it->second.first->f_getroutine(reinterpret_cast<CInstance*>(curSelf), ARRAY_INDEX_NO_INDEX, &v)) {
			Global::throwError("tbuiltin index failed to get builtin variable " + name);
		}

		rvalueToLua(pL, v);
	}

	return 1;
}

int LMS::Lua::mtBuiltinNewindex(lua_State* pL) {
	// ignored
	auto __tb{ luaL_checkudata(pL, 1, "__LMS_metatable_RValue_TBuiltin__") };

	luaL_argcheck(pL, lua_type(pL, 2) != LUA_TNIL, 2, "new key cannot be nil.");

	auto name{ std::string(lua_tostring(pL, 2)) };
	auto theval{ luaToRValue(pL, 3) };
	auto it{ builtinsMap.find(name) };
	if (it == builtinsMap.end()) {
		Global::throwError("newindex unknown builtin var name! " + name);
		return 0;
	}
	
	if (!it->second.first->f_canset || !it->second.first->f_setroutine) {
		Global::throwError("Read only builtin variable " + name);
	}

	if (!it->second.first->f_setroutine(reinterpret_cast<CInstance*>(curSelf), ARRAY_INDEX_NO_INDEX, &theval)) {
		Global::throwError("Failed to set read only builtin variable " + name);
	}

	return 0;
}

int LMS::Lua::mtRBuiltinIndex(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

	if (lua_type(pL, 2) == LUA_TNIL) {
		lua_pushnil(pL);
		return 1;
	}

	double arrlen{ 0.0 };
	auto ind{ static_cast<int>(lua_tonumber(pL, 2) - 1.0) };

	arrlen = rbptr->extra->arrayLength;
	if (strcmp(rbptr->var->f_name, "instance_id") == 0) {
		arrlen = getInstanceLen();
	}

	if (ind < 0 || ind >= arrlen) {
		lua_pushnil(pL);
		return 1;
	}
	
	RValue res{ nullptr };
	if (!rbptr->var->f_getroutine(rbptr->owner, ind, &res)) {
		Global::throwError(
			std::string("Failed to get built-in variable ") + std::string(rbptr->var->f_name) + std::string(":\r\n")
			+ std::string("Lua Array index: ") + std::to_string(ind + 1)
		);
	}

	rvalueToLua(pL, res);
	return 1;
}

int LMS::Lua::mtRBuiltinNewindex(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

	luaL_argcheck(pL, lua_type(pL, 2) != LUA_TNIL, 2, "new index cannot be nil.");

	auto ind{ static_cast<int>(lua_tonumber(pL, 2)) };
	auto theval{ luaToRValue(pL, 3) };

	if (!rbptr->var->f_setroutine(rbptr->owner, ind - 1, &theval)) {
		Global::throwError(
			std::string("Failed to set built-in variable ") + std::string(rbptr->var->f_name) + std::string(":\r\n")
			+ std::string("Lua Array index: ") + std::to_string(ind) + std::string(":\r\n")
			+ std::string("RV type: ") + std::to_string(theval.kind & MASK_KIND_RVALUE)
		);
	}

	return 0;
}

int LMS::Lua::mtRBuiltinLen(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

	RValue res{ 0.0 }; // initialize to real

	if (strcmp(rbptr->var->f_name, "instance_id") == 0) {
		res.val = getInstanceLen();
	}
	else {
		res.val = static_cast<double>(rbptr->extra->arrayLength);
	}

	rvalueToLua(pL, res);
	return 1;
}

int LMS::Lua::mtRBuiltinNext(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

	lua_Integer arrayind{ -1 };
	RValue arraylen{ 0.0 }; // initialize to real

	if (strcmp(rbptr->var->f_name, "instance_id") == 0) {
		arraylen.val = getInstanceLen();
	}
	else {
		arraylen.val = static_cast<double>(rbptr->extra->arrayLength);
	}

	if (lua_gettop(pL) > 1 && lua_type(pL, 2) != LUA_TNIL) {
		arrayind = static_cast<lua_Integer>(lua_tonumber(pL, 2) - 1.0);
	}

	// advance to next array element.
	++arrayind;

	if (arrayind < 0 || arrayind >= static_cast<lua_Integer>(arraylen.val)) {
		lua_pushnil(pL);
		return 1;
	}

	RValue res{ nullptr };
	auto ok{ rbptr->var->f_getroutine(rbptr->owner, static_cast<int>(arrayind), &res) };
	if (!ok) {
		return luaL_error(pL, "Failed to index built-in variable %s[%d].", rbptr->extra->name, static_cast<int>(1 + arrayind));
	}

	lua_pushinteger(pL, 1 + arrayind);
	rvalueToLua(pL, res);
	return 2;
}

int LMS::Lua::mtRBuiltinPairs(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

	lua_pushcfunction(pL, &mtRBuiltinNext);
	lua_pushvalue(pL, 1);
	lua_pushnil(pL);
	return 3;
}

int LMS::Lua::mtRBuiltinIpairs(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

	lua_pushcfunction(pL, &mtRBuiltinNext);
	lua_pushvalue(pL, 1);
	lua_pushinteger(pL, 0);
	return 3;
}

int LMS::Lua::mtRBuiltinEq(lua_State* pL) {
	auto rbptr1{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };
	auto rbptr2{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 2, "__LMS_metatable_RValue_RBuiltin__")) };

	lua_pushboolean(pL, rbptr1->owner == rbptr2->owner && rbptr1->var == rbptr2->var);
	return 1;
}

int LMS::Lua::mtStructCall(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	RValue* args{ nullptr };
	RValue** yyc_args{ nullptr };
	int argc{ lua_gettop(pL) - 1 };
	if (argc > 0) {
		args = new RValue[argc];
		yyc_args = new RValue*[argc];
		for (int i{ 0 }; i < argc; ++i) {
			args[i] = luaToRValue(pL, i + 2);
			yyc_args[i] = &args[i];
		}
	}

	RValue res{ nullptr };
	res = YYGML_CallMethod(curSelf, curOther, res, argc, **rptr, yyc_args);

	/* an argument might be inside those args so we convert first, then free YYC args. */
	rvalueToLua(pL, res);

	if (argc > 0) {
		delete[] yyc_args;
		yyc_args = nullptr;
		delete[] args;
		args = nullptr;
	}

	return 1;
}

int LMS::Lua::apiToInstance(lua_State* pL) {
	luaL_argcheck(pL, lua_type(pL, 1) == LUA_TUSERDATA || lua_type(pL, 1) == LUA_TNUMBER, 1,
		"apiToInstance only expects either userdata (existing wrapper) or number.");

	if (lua_type(pL, 1) == LUA_TUSERDATA) {
		// already a userdata, duplicate the argument and return
		lua_pushvalue(pL, 1);
	}
	else {
		auto num{ lua_tonumber(pL, 1) };
		// return nil for noone (a legal no-op)
		if (num == /*noone*/ -4.0) {
			lua_pushnil(pL);
			return 1;
		}

		RValue inst{ nullptr };
		RValue args[]{ num };
		F_JSGetInstance(inst, curSelf, curOther, 1, args);
		rvalueToLua(pL, inst);
	}

	return 1;
}

int LMS::Lua::apiCreateAsyncEvent(lua_State* pL) {
	auto dsmap{ lua_tonumber(pL, 1) };
	auto evsubtype{ lua_tonumber(pL, 2) };

	Create_Async_Event(static_cast<int>(dsmap), static_cast<int>(evsubtype));
	return 0;
}

int LMS::Lua::apiSignalScriptAction(lua_State* pL) {
	tmpFlags = static_cast<HookBitFlags>(lua_tonumber(pL, 1));
	return 0;
}

int LMS::Lua::apiSetHookFunction(lua_State* pL) {
	luaL_argcheck(pL, lua_type(pL, 1) == LUA_TSTRING, 1, "arg1 must be a unique hook identifier.");
	luaL_argcheck(pL, lua_gettop(pL) > 1, 2, "arg2 is not provided to function, must be either a callable or nil.");

	auto hookid{ lua_tostring(pL, 1) };
	luaL_argcheck(pL, hookid != nullptr, 1, "arg1 is nullptr?!");

	auto realhookid{ std::string(hookid) };
	auto lastpos{ realhookid.find_last_of('_') };
	luaL_argcheck(pL, lastpos != realhookid.npos, 1, "hook id is invalid.");
	auto indxstring{ realhookid.substr(lastpos + 1) };
	auto tablename{ realhookid.substr(0, lastpos) };
	auto indx{ std::stoll(indxstring) };
	luaL_argcheck(pL, indx > 0, 1, "hook index is invalid.");

	lua_getglobal(pL, "LMS");

	lua_pushstring(pL, "Garbage");
	lua_gettable(pL, -2);

	lua_pushstring(pL, tablename.c_str());
	lua_gettable(pL, -2);

	lua_pushinteger(pL, indx);
	lua_pushvalue(pL, 2);
	lua_settable(pL, -3);

	lua_pop(pL, 1); // hook table
	lua_pop(pL, 1); // garbage
	lua_pop(pL, 1); // lms

	// return back the hook id.
	// this may return a new hook id in the future.
	lua_pushvalue(pL, 1);
	return 1;
}

int LMS::Lua::apiHookScript(lua_State* pL) {
	luaL_argcheck(pL, lua_type(pL, 2) == LUA_TSTRING &&
		(strcmp(lua_tostring(pL, 2), "before") == 0 || strcmp(lua_tostring(pL, 2), "after") == 0),
		2, "Hook type must be either 'before' or 'after'.");

	luaL_argcheck(pL, lua_type(pL, 3) != LUA_TNIL,
		3, "Callable object is `nil`.");

	auto hooktype{ lua_tostring(pL, 2) };

	const char* myn{ "<undefined>" };
	std::string realname{};

	if (lua_type(pL, 1) == LUA_TNUMBER) {
		RValue scrname{ nullptr };
		RValue args[]{ lua_tonumber(pL, 1) };
		F_ScriptGetName(scrname, curSelf, curOther, 1, args);
		myn = scrname.pString->get();
		realname = std::string("gml_Script_") + myn;
	}
	else {
		myn = lua_tostring(pL, 1);
		luaL_argcheck(pL, myn != nullptr, 1, "arg1 can either be a full code entry name, or a script index.");
		realname = myn;
	}

	luaL_argcheck(pL, myn && myn[0] != '<', 1, "Script does not exist!");
	bool found{ false };

	for (auto i{ 0 }; i < g_pLLVMVars->nYYCode; ++i) {
		if (realname == g_pLLVMVars->pGMLFuncs[i].pName) {
			auto fun{ g_pLLVMVars->pGMLFuncs[i].pFunc };

			// try to find a scapegoat func.
			auto myind{ 0 };
			for (auto ii{ 0 }; true; ++ii) {
				if (!ScapegoatScripts[ii].f) {
					return luaL_argerror(pL, 1, "Reached the end of the scapegoat script table.");
				}

				if (!ScapegoatScripts[ii].n || realname == ScapegoatScripts[ii].n) {
					myind = ii;
					break;
				}
			}

			if (ScapegoatScripts[myind].n) {
				// already hooked?
				found = true;
				break;
			}
			
			auto ok{ MH_CreateHook(fun, ScapegoatScripts[myind].f, reinterpret_cast<LPVOID*>(&ScapegoatScripts[myind].t)) };
			if (ok != MH_STATUS::MH_OK) {
				Global::throwError("Failed to create a script hook due to minhook error!");
			}
			ok = MH_EnableHook(fun);
			if (ok != MH_STATUS::MH_OK) {
				Global::throwError("Failed to enable script hook due to minhook error!");
			}

			// aaand set the name!
			ScapegoatScripts[myind].n = g_pLLVMVars->pGMLFuncs[i].pName;
			found = true;
			break;
		}
	}

	luaL_argcheck(pL, found == true, 1, "Unable to find a script in the YYGML table.");

	if (strcmp(hooktype, "before") == 0) {
		realname += "_Before";
	}
	else if (strcmp(hooktype, "after") == 0) {
		realname += "_After";
	}

	lua_getglobal(pL, "LMS");
	lua_pushstring(pL, "Garbage");
	lua_gettable(pL, -2);
	lua_pushstring(pL, realname.c_str());
	lua_gettable(pL, -2);
	if (lua_type(pL, -1) == LUA_TNIL) {
		lua_pop(pL, 1); // pop nil, garbage becomes stacktop.
		lua_pushstring(pL, realname.c_str()); // key
		lua_newtable(pL); // value
		lua_settable(pL, -3);
		// get the table again:
		lua_pushstring(pL, realname.c_str());
		lua_gettable(pL, -2);
	}

	auto tablen{ findRealArrayLength(pL) };
	if (tablen == 0) ++tablen;

	lua_pushinteger(pL, tablen);
	lua_pushvalue(pL, 3);
	lua_settable(pL, -3);

	lua_pop(pL, 1); // hook table
	lua_pop(pL, 1); // 'Garbage'
	lua_pop(pL, 1); // 'LMS'

	realname += "_" + std::to_string(tablen);
	lua_pushstring(pL, realname.c_str());
	return 1;
}

VOID WINAPI LMS::Lua::ovCompletionRoutine(
	_In_    DWORD dwErrorCode,
	_In_    DWORD dwNumberOfBytesTransfered,
	_Inout_ LPOVERLAPPED lpOverlapped) {
	
	static std::unordered_map<DWORD, std::string> s_fileNotifStringify{
		{0, "error"},
		{FILE_ACTION_ADDED, "added"},
		{FILE_ACTION_REMOVED, "removed"},
		{FILE_ACTION_MODIFIED, "modified"},
		{FILE_ACTION_RENAMED_OLD_NAME, "renamedOldName"},
		{FILE_ACTION_RENAMED_NEW_NAME, "renamedNewName"},
		{FILE_ACTION_RENAMED_NEW_NAME+1, "outOfBounds"}
	};

	if (dwErrorCode != 0 || dwNumberOfBytesTransfered < 1 || !lpOverlapped) {
		Global::throwError("ovCompletionRoutine Wtf?");
	}

	auto* dat{ reinterpret_cast<std::pair<HANDLE, unsigned char*>*>(lpOverlapped->hEvent) };
	auto hDir{ dat->first };
	auto luakey{ static_cast<lua_Integer>(reinterpret_cast<std::intptr_t>(hDir)) };
	auto buf{ dat->second };

	lua_getglobal(luactx, "LMS");
	lua_pushstring(luactx, "Garbage");
	lua_gettable(luactx, -2);
	lua_pushstring(luactx, "Watcher");
	lua_gettable(luactx, -2);

	for (auto ptr{ buf }, ptrend{ buf + dwNumberOfBytesTransfered };
		ptr < ptrend;
		ptr += reinterpret_cast<PFILE_NOTIFY_INFORMATION>(ptr)->NextEntryOffset) {
		auto windat{ reinterpret_cast<PFILE_NOTIFY_INFORMATION>(ptr) };

		auto before{ lua_gettop(luactx) };

		/* get function: fetch it every time since it may change and we'll need to call the new impl! */
		lua_pushinteger(luactx, luakey);
		lua_gettable(luactx, -2);

		/* if null, ignore: */
		if (lua_type(luactx, -1) == LUA_TNIL) {
			lua_pop(luactx, 1);
			continue;
		}

		/* prepare arguments: */
		lua_pushinteger(luactx, luakey); // unique handle:
		
		lua_newtable(luactx); // the info table:
		lua_pushstring(luactx, "action");
		lua_pushstring(luactx, s_fileNotifStringify[windat->Action].c_str());
		lua_settable(luactx, -3);
		
		lua_pushstring(luactx, "fileName");
		std::string aname{ wstringToString({ windat->FileName, static_cast<std::size_t>(windat->FileNameLength) }) };
		lua_pushstring(luactx, aname.c_str());
		lua_settable(luactx, -3);

		/* call: */
		auto ok{ lua_pcall(luactx, 2, LUA_MULTRET, 0) };

		auto after{ lua_gettop(luactx) };
		auto diff{ after - before };
		if (ok != LUA_OK) {
			/* print the stacktrace: */
			std::cerr << "Live reload failed for file " << aname << ", stacktrace: --[[" << std::endl;
			for (auto i{ 0 }; i < diff; ++i) {
				auto msg{ lua_tostring(luactx, -1) };
				if (!msg) msg = "<LMS: nullptr string, tostring() failed>";

				std::cerr << "[" << (i + 1) << ":" << lua_typename(luactx, lua_type(luactx, -1)) << "] = " << msg << std::endl;
				lua_pop(luactx, 1);
			}
			std::cerr << "stacktrace end. cancelling event... ]]--" << std::endl;

			/* cancel this event entirely and wait for another file change.*/
			break;
		}
		else if (diff != 0) {
			/* TODO: figure out some arguments for the return value here? */
			/* as for now just pop all the return values */
			lua_pop(luactx, diff);
		}

		if (windat->NextEntryOffset == 0) {
			break;
		}
	}

	/* balance stack: */
	lua_pop(luactx, 1); // pop watcher
	lua_pop(luactx, 1); // pop garbage
	lua_pop(luactx, 1); // pop lms

	/* memset the buffer to all bits zero. */
	std::memset(buf, 0, 32768);

	/* restart the watcher */
	BOOL bOk{ ReadDirectoryChangesW(
		hDir,
		dat->second,
		32768,
		FALSE,
		FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
		nullptr,
		lpOverlapped,
		&ovCompletionRoutine) };

	if (!bOk) {
		delete[] buf;
		CloseHandle(hDir);
		delete dat;
		delete lpOverlapped;
		Global::throwError("Failed to restart the watcher.");
	}
}

int LMS::Lua::apiSetFileWatchFunction(lua_State* pL) {
	lua_getglobal(pL, "LMS");
	lua_pushstring(pL, "Garbage");
	lua_gettable(pL, -2);
	lua_pushstring(pL, "Watcher");
	lua_gettable(pL, -2);
	lua_pushvalue(pL, 1);
	lua_pushvalue(pL, 2);
	lua_settable(pL, -3);
	// LMS.Garbage.Watcher[arg1] = arg2

	lua_pop(pL, 1); // watcher
	lua_pop(pL, 1); // garbage
	lua_pop(pL, 1); // lms

	// return arg1 for now.
	lua_pushvalue(pL, 1);
	return 1;
}

int LMS::Lua::apiFileWatch(lua_State* pL) {
	if (lua_gettop(pL) < 1 || lua_type(pL, 1) == LUA_TNIL) {
		auto sleepret{ SleepEx(0, TRUE) };
		// observe sleepret in VS debugger...
		return 0;
	}

	auto lpath{ lua_tostring(pL, 1) };
	luaL_argcheck(pL, lpath != nullptr && strlen(lpath) > 0, 1, "Path is not a valid string.");
	luaL_argcheck(pL, lua_type(pL, 2) != LUA_TNIL, 2, "Callable object cannot be nil.");

	std::wstring wpath{ stringToWstring(lpath) };

	HANDLE hDir{
		CreateFileW(
			wpath.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
			nullptr
		)
	};

	if (!hDir || hDir == INVALID_HANDLE_VALUE) {
		return luaL_argerror(pL, 1, "Failed to open the directory.");
	}

	auto ov{ new OVERLAPPED{ 0 } };
	auto* dat{ new std::pair<HANDLE, unsigned char*>(hDir, new unsigned char[32768]{ '\0' }) };
	ov->hEvent = dat;

	auto ok{ ReadDirectoryChangesW(
		hDir,
		dat->second,
		32768,
		FALSE,
		FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
		nullptr,
		ov,
		&ovCompletionRoutine
	) };

	if (!ok) {
		CloseHandle(dat->first);
		delete[] dat->second;
		delete dat;
		delete ov;
		return luaL_argerror(pL, 1, "Failed to setup the filewatcher.");
	}

	auto luakey{ static_cast<lua_Integer>(reinterpret_cast<std::intptr_t>(hDir)) };

	lua_getglobal(pL, "LMS");
	lua_pushstring(pL, "Garbage");
	lua_gettable(pL, -2);
	lua_pushstring(pL, "Watcher");
	lua_gettable(pL, -2);
	lua_pushinteger(pL, luakey);
	lua_pushvalue(pL, 2);
	lua_settable(pL, -3);
	lua_pop(pL, 1); // pop watcher
	lua_pop(pL, 1); // pop garbage
	lua_pop(pL, 1); // pop lms


	lua_pushinteger(pL, luakey);
	return 1;
}

int LMS::Lua::apiSetConsoleShow(lua_State* pL) {
	// allow numbers as args too.
	auto doShow{ (lua_tonumber(pL, 1) > 0.5) || (lua_toboolean(pL, 1)) };

	auto res{ false };
	if (doShow) {
		res = ShowWindowAsync(GetConsoleWindow(), SW_SHOW) == TRUE;
	}
	else {
		res = ShowWindowAsync(GetConsoleWindow(), SW_HIDE) == TRUE;
	}

	lua_pushboolean(pL, res);
	return 1;
}

int LMS::Lua::apiSetConsoleTitle(lua_State* pL) {
	// allow numbers as args too.
	auto newtitle{ lua_tostring(pL, 1) };

	if (newtitle) {
		std::wstring widenewtitle{ stringToWstring(newtitle) };
		lua_pushboolean(pL, SetConsoleTitleW(widenewtitle.c_str()));
		return 1;
	}
	else {
		return luaL_argerror(pL, 1, "Argument cannot be converted to a string.");
	}
}

int LMS::Lua::apiClearConsole(lua_State* pL) {
	HANDLE hConsole{ GetStdHandle(STD_OUTPUT_HANDLE) };
	DWORD wrote{ 0 };
	COORD zerozero{ 0,0 };
	CONSOLE_SCREEN_BUFFER_INFO csbi{};

	GetConsoleScreenBufferInfo(hConsole, &csbi);
	auto siz{ csbi.dwSize.X * csbi.dwSize.Y };

	FillConsoleOutputCharacterW(hConsole, ' ', siz, zerozero, &wrote);
	FillConsoleOutputAttribute(hConsole, csbi.wAttributes, siz, zerozero, &wrote);
	SetConsoleCursorPosition(hConsole, zerozero);
	return 0;
}

RValue LMS::Lua::luaToRValue(lua_State* pL, int index) {
	arraySetOwner();
	switch (lua_type(pL, index)) {
		case LUA_TNONE:
		default: {
			Global::throwError("Invalid Lua stack index or type in luaToRValue! ind=" + std::to_string(index));
			return RValue{}; // unset.
		}

		case LUA_TNIL: {
			return RValue{ nullptr }; // GML undefined.
		}

		case LUA_TBOOLEAN: {
			return RValue{ static_cast<bool>(lua_toboolean(pL, index)) };
		}

		case LUA_TLIGHTUSERDATA: {
			return RValue{ const_cast<void*>(lua_topointer(pL, index)) };
		}

		case LUA_TNUMBER: {
			return RValue{ lua_tonumber(pL, index) };
		}

		case LUA_TSTRING: {
			return RValue{ lua_tostring(pL, index) };
		}

		case LUA_TFUNCTION: {
			return luaToMethod(pL, index);
		}

		case LUA_TUSERDATA: {
			RValue rv{ nullptr };
			/* check if RV array: */
			auto rptr{ reinterpret_cast<RValue**>(luaL_testudata(pL, index, "__LMS_metatable_RValue_Array__")) };
			auto sptr{ reinterpret_cast<RValue**>(luaL_testudata(pL, index, "__LMS_metatable_RValue_Struct__")) };
			if (rptr) {
				rv = **rptr;
			}
			else if (sptr) {
				rv = **sptr;
			}
			else {
				Global::throwError("Not implemented userdata conversion! Please report this as a bug.");
			}

			return rv;
		}

		case LUA_TTABLE: {
			RValue dummy{ nullptr }; // dummy return value for struct_set and array_set
			RValue ret{ nullptr }; // undefined by default.
			bool dothrowinvalid{ false };
			bool allkeysarenums{ true };

			/*
			* this loop is tricky and breaking or continuing from it may corrupt the stack
			* so we must always let it finish.
			*/
			for (lua_pushnil(pL); lua_next(pL, index) != 0; lua_pop(pL, 1) /* pop value */) {
				auto keytype{ lua_type(pL, -2) };
				// key is not a number? then mark that we should do a struct conversion.
				if (keytype != LUA_TNUMBER) {
					allkeysarenums = false;

					// key is not a string..?
					if (keytype == LUA_TNIL || (lua_tostring(pL, -2) == nullptr)) {
						dothrowinvalid = true;
					}
				}
			}

			if (dothrowinvalid) {
				luaL_argerror(pL, index, "This table has keys that are not numbers and not strings, invalid conversion.");
				return RValue{}; // UNSET
			}

			if (!allkeysarenums) {
				/* make a struct */
				ret.kind = VALUE_OBJECT;
				ret.pObj = YYObjectBase_Alloc(0, VALUE_UNSET, YYObjectBaseKind::KIND_YYOBJECTBASE, false);
				JS_GenericObjectConstructor(ret, curSelf, curOther, 0, nullptr);
				ret.pObj->m_class = "___struct___anon_modshovel";
				// set our class name to an anonymous modshovel struct.

				/* iterate again */
				for (lua_pushnil(pL); lua_next(pL, index) != 0; lua_pop(pL, 1)) {
					RValue args[]{ ret, RValue{lua_tostring(pL, -2)}, luaToRValue(pL, lua_gettop(pL)) };
					F_VariableStructSet(dummy, curSelf, curOther, 3, args);
				}
			}
			else {
				/* make an array */
				RValue args[]{ 0.0 /* length of array*/ }; // make a zero-length array
				F_ArrayCreate(ret, curSelf, curOther, 1, args);

				/* iterate again */
				for (lua_pushnil(pL); lua_next(pL, index) != 0; lua_pop(pL, 1)) {
					// decrement key.
					RValue args[]{ ret, lua_tonumber(pL, -2) - 1.0, luaToRValue(pL, lua_gettop(pL)) };
					F_ArraySet(dummy, curSelf, curOther, 3, args);
				}
			}

			return ret;
		}
	}
}

void LMS::Lua::rvalueToLua(lua_State* pL, RValue& rv) {
	arraySetOwner();
	switch (rv.kind & MASK_KIND_RVALUE) {
		case VALUE_UNSET: {
			Global::throwError("RValue Type is UNSET.");
			break;
		}

		case VALUE_BOOL: {
			lua_pushboolean(pL, rv.val > 0.5);
			break;
		}

		case VALUE_REAL: {
			lua_pushnumber(pL, rv.val);
			break;
		}

		case VALUE_INT32: {
			lua_pushinteger(pL, rv.v32);
			break;
		}

		case VALUE_INT64: {
			lua_pushinteger(pL, rv.v64);
			break;
		}

		case VALUE_STRING: {
			lua_pushstring(pL, rv.pString->m_thing);
			break;
		}

		case VALUE_PTR: {
			lua_pushlightuserdata(pL, rv.ptr);
			break;
		}

		case VALUE_UNDEFINED:
		case VALUE_NULL: {
			lua_pushnil(pL);
			break;
		}

		case VALUE_ARRAY: {
			/* push a light array wrapper... */
			auto rptr{ reinterpret_cast<RValue**>(lua_newuserdatauv(pL, sizeof(RValue*), 0)) };
			*rptr = new RValue{ rv };
			pinYYObjectBase((*rptr)->pObj);
			luaL_setmetatable(pL, "__LMS_metatable_RValue_Array__");
			break;
		}

		case VALUE_OBJECT: {
			/* both structs and methods are now handled by a single userdata... */
			pushYYObjectBase(luactx, rv);
			break;
		}
	}
}

int LMS::Lua::apiHookEvent(lua_State* pL) {
	auto objind{ static_cast<int>(lua_tonumber(pL, 1)) };
	auto evtype{ static_cast<int64>(lua_tonumber(pL, 2)) };
	auto evsubtype{ static_cast<int64>(lua_tonumber(pL, 3)) };

	luaL_argcheck(pL, objind >= 0, 1, "Object index cannot be less than zero.");
	luaL_argcheck(pL, evtype >= 0, 2, "Event type cannot be less than zero.");
	luaL_argcheck(pL, evsubtype >= 0, 3, "Event subtype cannot be less than zero.");

	luaL_argcheck(pL,
		lua_type(pL, 4) == LUA_TSTRING &&
		(strcmp(lua_tostring(pL, 4), "before") == 0 || strcmp(lua_tostring(pL, 4), "after") == 0),
		4, "Invalid hook type, can be either `before` or `after`.");

	luaL_argcheck(pL,
		lua_type(pL, 5) != LUA_TNIL,
		5, "Callable object cannot be nil."
	);

	auto hooktype{ lua_tostring(pL, 4) };


	auto pobj{ (*g_ppObjectHash)->findObject(objind) };
	if (!pobj) {
		return 0;
	}

	CEvent* cptr{};
	auto ok{ pobj->m_eventsMap->findElement(evsubtype | (evtype << 32ull), &cptr) };

	/* if an event does not exist, construct it: */
	if (!ok) {
		/* construct fake event objects and mark them with the "hehe" string =^-^= */
		CCode* code{ new CCode(reinterpret_cast<PFUNC_YYGMLScript_Internal>(&hookRoutineEvent)) };
		code->i_str = "hehe";
		cptr = new CEvent(code, static_cast<int>(objind));

		/* we don't have an original, sooo null! */
		eventOriginalsMap[genKey(static_cast<int>(objind), static_cast<int>(evtype), static_cast<int>(evsubtype))] = nullptr;

		/* need to insert the event and regen lists: */
		Insert_Event(pobj->m_eventsMap, evsubtype | (evtype << 32ull), cptr);
		Create_Object_Lists();
	}

	/* is this event not marked as replaced? */
	if (strcmp(cptr->e_code->i_str, "hehe") != 0) {
		/* replace the routine... */
		eventOriginalsMap[genKey(static_cast<int>(objind), static_cast<int>(evtype), static_cast<int>(evsubtype))] = cptr->e_code->i_pFunc->pFunc;
		cptr->e_code->i_pFunc->pFunc = &hookRoutineEvent;
		cptr->e_code->i_str = "hehe";
	}

	/* get the garbage table */
	std::string luaIdent{ std::string("gml_Object_") + std::string(pobj->m_pName) + "_" + getEventName(static_cast<int>(evtype)) + "_"};
	if (evtype != 4) {
		luaIdent += std::to_string(evsubtype);
	}
	else {
		luaIdent += (*g_ppObjectHash)->findObject(static_cast<uint>(evsubtype))->m_pName;
	}

	if (strcmp(hooktype, "before") == 0) {
		luaIdent += "_Before";
	}
	else if (strcmp(hooktype, "after") == 0) {
		luaIdent += "_After";
	}

	lua_getglobal(pL, "LMS");
	lua_pushstring(pL, "Garbage");
	lua_gettable(pL, -2);
	lua_pushstring(pL, luaIdent.c_str());
	lua_gettable(pL, -2);
	if (lua_type(pL, -1) == LUA_TNIL) {
		lua_pop(pL, 1); // pop nil, garbage becomes stacktop.
		lua_pushstring(pL, luaIdent.c_str()); // key
		lua_newtable(pL); // value
		lua_settable(pL, -3);
		// get the table again:
		lua_pushstring(pL, luaIdent.c_str());
		lua_gettable(pL, -2);
	}

	auto tablen{ findRealArrayLength(pL) }; /* stacktop - Object Table Len */
	if (tablen == 0) ++tablen;

	lua_pushinteger(pL, tablen);
	lua_pushvalue(pL, 5);
	lua_settable(pL, -3);

	lua_pop(pL, 1); // hook
	lua_pop(pL, 1); // garbage
	lua_pop(pL, 1); // lms

	luaIdent += "_" + std::to_string(tablen);
	lua_pushstring(pL, luaIdent.c_str());
	return 1;
}

int LMS::Lua::luaRuntimeCall(lua_State* pL) {
	RValue retval{ nullptr }; // `undefined` aka Nil is the default return value.
	RValue* args{ nullptr };
	int argc{ lua_gettop(pL) };
	if (argc > 0) {
		args = new RValue[argc];
		for (int i{ 0 }; i < argc; ++i) {
			args[i] = luaToRValue(pL, i + 1);
		}
	}

	RFunction* cur{ reinterpret_cast<RFunction*>(const_cast<void*>(lua_topointer(pL, lua_upvalueindex(1)))) };
	//*g_ppFunction = ;
	//(*g_ppFunction)->f_routine(retval, curSelf, curOther, argc, args);
	//*g_ppFunction = cur;
	cur->f_routine(retval, curSelf, curOther, argc, args);

	rvalueToLua(pL, retval);

	if (args) {
		delete[] args;
		args = nullptr;
	}

	return 1;
}

void LMS::Lua::initMetamethods(lua_State* pL) {
	/* LMS is at the stacktop */

	/* RArray */
	const luaL_Reg arraymt[]{
		{"__index", &mtArrayIndex},
		{"__newindex", &mtArrayNewindex},
		{"__gc", &mtArrayGc},
		{"__len", &mtArrayLen},
		{"__tostring", &mtArrayTostring},
		{"__next", &mtArrayNext},
		{"__pairs", &mtArrayPairs},
		{"__ipairs", &mtArrayIpairs},
		{"__eq", &mtArrayEq},
		{nullptr, nullptr}
	};

	luaL_newmetatable(pL, "__LMS_metatable_RValue_Array__");
	luaL_setfuncs(pL, arraymt, 0);
	lua_pop(pL, 1);

	/* YYObjectBase or CInstance */
	const luaL_Reg structmt[]{
		{"__index", &mtStructIndex},
		{"__newindex", &mtStructNewindex},
		{"__gc", &mtStructGc},
		{"__tostring", &mtStructTostring},
		{"__len", &mtStructLen},
		{"__eq", &mtStructEq},
		{nullptr, nullptr}
	};

	luaL_newmetatable(pL, "__LMS_metatable_RValue_Struct__");
	luaL_setfuncs(pL, structmt, 0);
	lua_pop(pL, 1);

	/* LMS.Builtin vars thing */
	const luaL_Reg tbuiltinmt[]{
		{"__index", &mtBuiltinIndex},
		{"__newindex", &mtBuiltinNewindex},
		{nullptr, nullptr}
	};

	luaL_newmetatable(pL, "__LMS_metatable_RValue_TBuiltin__");
	luaL_setfuncs(pL, tbuiltinmt, 0);
	lua_pop(pL, 1);

	/* Array Builtin vars thing */
	const luaL_Reg rbuiltinmt[]{
		{"__index", &mtRBuiltinIndex},
		{"__newindex", &mtRBuiltinNewindex},
		{"__len", &mtRBuiltinLen},
		{"__next", &mtRBuiltinNext},
		{"__pairs", &mtRBuiltinPairs},
		{"__ipairs", &mtRBuiltinIpairs},
		{"__eq", &mtRBuiltinEq},
		{nullptr, nullptr}
	};

	luaL_newmetatable(pL, "__LMS_metatable_RValue_RBuiltin__");
	luaL_setfuncs(pL, rbuiltinmt, 0);
	lua_pop(pL, 1);

	/* One way method call wrapper is now obsolete, use the Struct instead... */
}

void LMS::Lua::initRuntime(lua_State* pL) {
	/* LMS is at the stacktop */
	lua_pushstring(pL, "Runtime");
	lua_newtable(pL);

	/* initRuntime begin */
	for (int i{ 0 }; i < *p_the_numb; ++i) {
		auto func{ &((*g_pThe_functions)[i]) };
		char realname[64 + 1]{ '\0' };
		// extra null byte since functions may overflow apparently...
		std::memcpy(realname, func->f_name, sizeof(func->f_name));

		lua_pushstring(pL, realname);
		lua_pushlightuserdata(pL, func);
		lua_pushcclosure(pL, &luaRuntimeCall, 1);
		lua_settable(pL, -3); // -1 - value, -2 - key, -3 - Runtime table.

		if (strcmp(realname, "yyAsm") == 0) {
			deriveYYFree(reinterpret_cast<std::byte*>(func->f_routine));
		}
		else if (strcmp(realname, "array_create") == 0) {
			F_ArrayCreate = func->f_routine;
		}
		else if (strcmp(realname, "array_get") == 0) {
			F_ArrayGet = func->f_routine;
		}
		else if (strcmp(realname, "array_set") == 0) {
			F_ArraySet = func->f_routine;
		}
		else if (strcmp(realname, "array_length") == 0) {
			F_ArrayLength = func->f_routine;
		}
		else if (strcmp(realname, "object_get_parent") == 0) {
			deriveObjectHash(reinterpret_cast<std::byte*>(func->f_routine));
		}
		else if (strcmp(realname, "typeof") == 0) {
			F_Typeof = func->f_routine;
			deriveYYCreateString();
		}
		else if (strcmp(realname, "variable_struct_set_pre") == 0) {
			std::byte* pff{ reinterpret_cast<std::byte*>(func->f_routine) };
			deriveCopyRValuePre(pff);
		}
		else if (strcmp(realname, "object_get_name") == 0) {
			F_ObjectGetName = func->f_routine;
		}
		else if (strcmp(realname, "sprite_get_name") == 0) {
			F_SpriteGetName = func->f_routine;
		}
		else if (strcmp(realname, "room_get_name") == 0) {
			F_RoomGetName = func->f_routine;
		}
		else if (strcmp(realname, "font_get_name") == 0) {
			F_FontGetName = func->f_routine;
		}
		else if (strcmp(realname, "audio_get_name") == 0) {
			F_AudioGetName = func->f_routine;
		}
		else if (strcmp(realname, "path_get_name") == 0) {
			F_PathGetName = func->f_routine;
		}
		else if (strcmp(realname, "timeline_get_name") == 0) {
			F_TimelineGetName = func->f_routine;
		}
		else if (strcmp(realname, "tileset_get_name") == 0) {
			F_TilesetGetName = func->f_routine;
		}
		else if (strcmp(realname, "script_get_name") == 0) {
			F_ScriptGetName = func->f_routine;
		}
		else if (strcmp(realname, "variable_struct_get") == 0) {
			F_VariableStructGet = func->f_routine;
		}
		else if (strcmp(realname, "variable_struct_set") == 0) {
			F_VariableStructSet = func->f_routine;
		}
		else if (strcmp(realname, "variable_struct_names_count") == 0) {
			F_VariableStructNamesCount = func->f_routine;
		}
		else if (strcmp(realname, "shader_get_name") == 0) {
			F_ShaderGetName = func->f_routine;
		}
		else if (strcmp(realname, "@@array_set_owner@@") == 0) {
			F_ArraySetOwner = func->f_routine;
		}
		else if (strcmp(realname, "@@Global@@") == 0) {
			/* obtain global YYObjectBase */
			RValue globobj{ nullptr };
			RValue args[]{0.0}; // ignored...
			func->f_routine(globobj, nullptr, nullptr, 1, args);
			g_pGlobal = globobj.pObj;
			std::memset(&globobj, 0, sizeof(globobj)); // reset RValue to all bits zero (number 0.0) to prevent destruction
		}
		else if (strcmp(realname, "@@GetInstance@@") == 0) {
			F_JSGetInstance = func->f_routine;
		}
		else if (strcmp(realname, "string") == 0) {
			F_String = func->f_routine;
		}
		else if (strcmp(realname, "ds_map_create") == 0) {
			F_DsMapCreate = func->f_routine;
		}
		else if (strcmp(realname, "variable_global_set") == 0) {
			F_VariableGlobalSet = func->f_routine;
		}
		else if (strcmp(realname, "ds_map_replace") == 0) {
			F_DsMapReplace = func->f_routine;
		}
		else if (strcmp(realname, "ds_map_delete") == 0) {
			F_DsMapDelete = func->f_routine;
		}
		else if (strcmp(realname, "ds_map_find_value") == 0) {
			F_DsMapFindValue = func->f_routine;
		}
		else if (strcmp(realname, "ds_exists") == 0) {
			F_DsExists = func->f_routine;
		}
	}
	/* initRuntime end */

	/* create a ds map for pinnable objects. */
	RValue dsm{ -1.0 };
	F_DsMapCreate(dsm, curSelf, curOther, 0, nullptr);
	
	RValue setargs[]{ RValue{"__libmodshovel_gc_ds_map_index_please_do_not_destroy__"}, dsm };
	// global.mapnamehere = dsm;
	F_VariableGlobalSet(dsm, curSelf, curOther, 2, setargs);

	double dsind{ -1.0 };
	if (!dsm.tryToNumber(dsind) || dsind < 0.0) {
		Global::throwError("Failed to make a GC DS map. :(");
	}

	pinDsMap = dsind;

	lua_settable(pL, -3); // -1 - Runtime table, -2 - "Runtime", -3 - LMS global object.
}

void LMS::Lua::initBuiltin(lua_State* pL) {
	/* LMS is at the stacktop */
	lua_pushstring(pL, "Builtin");
	auto __nothing{ reinterpret_cast<int*>(lua_newuserdatauv(pL, sizeof(int), 0)) };
	(*__nothing) = 0; // juust in case.
	luaL_setmetatable(pL, "__LMS_metatable_RValue_TBuiltin__");

	for (std::size_t i{ 0 }; i < static_cast<std::size_t>(*g_pRVArrayLen); ++i) {
		auto& me{ g_pRVArray[i] };
		if (strcmp(me.f_name, "event_object") == 0) {
			GV_EventObject = me.f_getroutine;
		}
		else if (strcmp(me.f_name, "event_type") == 0) {
			GV_EventType = me.f_getroutine;
		}
		else if (strcmp(me.f_name, "event_number") == 0) {
			GV_EventSubtype = me.f_getroutine;
		}
		else if (strcmp(me.f_name, "instance_id") == 0) {
			GV_InstanceId = me.f_getroutine;
		}
		else if (strcmp(me.f_name, "instance_count") == 0) {
			GV_InstanceCount = me.f_getroutine;
		}
		else if (strcmp(me.f_name, "program_directory") == 0) {
			GV_ProgramDirectory = me.f_getroutine;
		}

		GMBuiltinVariable* v{ nullptr };
		for (std::size_t ii{ 0 }; true; ++ii) {
			// end of table :|
			if (BuiltinVariables[ii].name == nullptr) break;

			if (strcmp(BuiltinVariables[ii].name, me.f_name) == 0) {
				v = &BuiltinVariables[ii];
				break;
			}
		}

		if (v == nullptr) {
			// a special built-in variable that we must ignore (e.g. argument0, argument_relative, NaN, infinity, etc)
			continue;
		}

		builtinsMap[me.f_name] = std::make_pair(&me, v);
	}

	lua_settable(pL, -3); // -1 - Builtin table, -2 - "Builtin", -3 - LMS global object.
}

double LMS::Lua::getInstanceLen() {
	double thelen{ 0.0 };
	RValue tst{ 0.0 };
	if (!GV_InstanceCount(curSelf, ARRAY_INDEX_NO_INDEX, &tst) || !tst.tryToNumber(thelen)) {
		Global::throwError("Unable to obtain builtin variable instance_count.");
	}

	return thelen;
};

bool LMS::Lua::directWith(lua_State* pL, RValue& newself, double ind) {
	bool shouldbreak{ false };
	auto tmpSelf{ curSelf }, tmpOther{ curOther };
	curSelf = reinterpret_cast<CInstance*>(newself.pObj);
	curOther = tmpSelf;
	arraySetOwner();

	auto before{ lua_gettop(pL) };

	lua_pushvalue(pL, 2);
	pushYYObjectBase(pL, reinterpret_cast<YYObjectBase*>(curSelf));
	if (curSelf != curOther) {
		pushYYObjectBase(pL, reinterpret_cast<YYObjectBase*>(curOther));
	}
	else {
		lua_pushvalue(pL, -1);
	}

	lua_pushnumber(pL, ind);

	auto ok{ lua_pcall(pL, 3, LUA_MULTRET, 0) };
	if (ok != LUA_OK) {
		auto msg{ lua_tostring(pL, -1) };
		if (!msg) msg = "<unknown error>";
		Global::throwError(std::string("Fatal execution error in a with statement:\r\n") + msg);
	}

	auto after{ lua_gettop(pL) };

	auto diff{ after - before };
	if (diff != 0) {
		auto tmpval{ 0.0 };
		if (lua_type(pL, -1) == LUA_TNUMBER) tmpval = lua_tonumber(pL, -1);
		lua_pop(pL, 1);
		// pop leftover elements.
		--diff;
		if (diff > 0) {
			lua_pop(pL, diff);
		}

		if (tmpval > 0.0) {
			shouldbreak = true;
		}
	}

	/* restore pre-with self and other */
	curSelf = tmpSelf;
	curOther = tmpOther;
	arraySetOwner();

	return shouldbreak;
}

RValue LMS::Lua::luaToMethod(lua_State* pL, int funcind) {
	std::unique_lock<std::mutex> lock{ FreeMtMutex };
	RValue obj{ nullptr };

	auto funchash{ static_cast<lua_Integer>(reinterpret_cast<std::intptr_t>(lua_topointer(pL, funcind))) };
	if (funchash == 0) {
		luaL_error(pL, "Function hash value is zero. Please report this as a bug.\r\nstackind=%d", funcind);
	}

	/* find a free scapegoat YYC call routine: */
	auto myind{ 0 };
	for (auto i{ 0 }; true; ++i) {
		if (!ScapegoatMethods[i].f) {
			luaL_error(pL, "Reached the end of the scapegoat method table.");
		}

		if (ScapegoatMethods[i].k == funchash || ScapegoatMethods[i].k == 0) {
			myind = i;
			ScapegoatMethods[myind].k = funchash;
			break;
		}
	}

	lua_getglobal(pL, "LMS");
	lua_pushstring(pL, "Garbage");
	lua_gettable(pL, -2);
	lua_pushstring(pL, "Methods");
	lua_gettable(pL, -2);
	lua_pushinteger(pL, funchash);
	lua_pushvalue(pL, funcind);
	lua_settable(pL, -3);
	lua_pop(pL, 1); // methods
	lua_pop(pL, 1); // garbage
	lua_pop(pL, 1); // lms

	YYSetScriptRef(&obj, ScapegoatMethods[myind].f, reinterpret_cast<YYObjectBase*>(curSelf));
	std::string mytag{ "___anon___lms_method_" + std::to_string(myind) };
	char* mystr{ nullptr };
	MMSetLength(reinterpret_cast<void**>(&mystr), mytag.size() + 1);
	std::memset(mystr, 0, mytag.size() + 1);
	std::memcpy(mystr, mytag.data(), mytag.size());
	reinterpret_cast<CScriptRef*>(obj.pObj)->m_tag = mystr;
	CScriptRefVTable::Obtain(reinterpret_cast<CScriptRef*>(obj.pObj));
	CScriptRefVTable::Replace(reinterpret_cast<CScriptRef*>(obj.pObj));
	return obj;
}

int LMS::Lua::apiWith(lua_State* pL) {
	// do nothing on nils.
	if (lua_type(pL, 1) == LUA_TNIL) {
		return 0;
	}

	// arg 1 - instance id or object for a direct with, object index for an implicit with...
	double rvnum{ -10241024.0 };
	auto key{ luaToRValue(pL, 1) }; // can be YYObjectBase or a number
	bool isNumber{ key.tryToNumber(rvnum) }; // if RValue is a number type, convert it to a double (mind int64 and int64)
	// func is argument 2

	if (isNumber) {
		if (rvnum == -1.0) {
			// self
			key.kind = VALUE_OBJECT;
			key.pObj = reinterpret_cast<YYObjectBase*>(curSelf);
		}
		else if (rvnum == -2.0) {
			// other
			key.kind = VALUE_OBJECT;
			key.pObj = reinterpret_cast<YYObjectBase*>(curOther);
		}
		// -3 is `all`, handled below, just ignores the object index.
		else if (rvnum == -4.0) {
			// a with() with noone is a legit no-op in GML.
			// just return without executing anything...
			return 0;
		}
		else if (rvnum == -5.0) {
			// `global`, yes, I permit a `global` with(), at your own risk of course :p
			key.kind = VALUE_OBJECT;
			key.pObj = g_pGlobal;
		}
		else if (rvnum >= 100000.0) {
			// typical instance id, gladly @@GetInstance@@ will get the YYobjectbase that belongs to that ID.
			RValue args[]{ rvnum }; // will return to `key`
			F_JSGetInstance(key, curSelf, curOther, 1, args);
		}
		else if (rvnum < -5.0) {
			Global::throwError("Invalid instance id/number passed to apiWith(): " + std::to_string(rvnum));
		}
	}

	if ((key.kind & MASK_KIND_RVALUE) == VALUE_OBJECT) {
		directWith(pL, key, 1.0);
	}
	else {
		// need to do a loop.
		for (double i{ 0.0 }, cnt{ 1.0 }; i < getInstanceLen(); ++i) {
			RValue myinstid{ -4.0 /* noone */ };
			if (!GV_InstanceId(curSelf, static_cast<int>(i), &myinstid) || myinstid.val == -4.0) {
				Global::throwError("Unable to fetch instance_id from array o_O");
				return 0;
			}

			RValue args[]{ myinstid.val };
			F_JSGetInstance(myinstid, curSelf, curOther, 1, args);
			auto cinstptr{ reinterpret_cast<CInstance*>(myinstid.pObj) };
			if (!cinstptr) {
				Global::throwError("CInstance pointer is null...");
				return 0;
			}

			bool isdeactive{ (cinstptr->m_InstFlags & (0x80 | 1 | 2)) != 0 };

			// if we're all or the object_index variable matches...
			if ((rvnum == /*all*/ -3.0 || cinstptr->i_objectindex == rvnum)
				&& !isdeactive /* not deactivated... */) {

				if (directWith(pL, myinstid, cnt)) {
					break;
				}

				++cnt; // increment a counter if object index matched and the instance was active.
				isdeactive = (cinstptr->m_InstFlags & (0x80 | 1 | 2)) != 0;
				/* TODO: decrement index (i.e. seek back) if the instance got deactivated */
				/*
				if (reinterpret_cast<CInstance*>(myobjbase.pObj)->m_InstFlags & 0x80) {
					// instance got destroyed in a with() event, decrement the index.
					--i;
				}
				*/
			}
		}
	}

	return 0;
}

void LMS::Lua::initApi(lua_State* pL) {
	/* LMS is at the stacktop */
	lua_pushstring(pL, "Api");
	lua_newtable(pL);

	/* LMS API begin */
	lua_pushstring(pL, "createEventHook");
	lua_pushcfunction(pL, &apiHookEvent);
	lua_settable(pL, -3);
	lua_pushstring(pL, "createScriptHook");
	lua_pushcfunction(pL, &apiHookScript);
	lua_settable(pL, -3);
	lua_pushstring(pL, "signalScriptAction");
	lua_pushcfunction(pL, &apiSignalScriptAction);
	lua_settable(pL, -3);
	lua_pushstring(pL, "debugEnterReplLoop");
	lua_pushcfunction(pL, &apiDebugEnterRepl);
	lua_settable(pL, -3);
	lua_pushstring(pL, "with");
	lua_pushcfunction(pL, &apiWith);
	lua_settable(pL, -3);
	lua_pushstring(pL, "createAsyncEvent");
	lua_pushcfunction(pL, &apiCreateAsyncEvent);
	lua_settable(pL, -3);
	lua_pushstring(pL, "setHookFunction");
	lua_pushcfunction(pL, &apiSetHookFunction);
	lua_settable(pL, -3);
	lua_pushstring(pL, "fileWatch");
	lua_pushcfunction(pL, &apiFileWatch);
	lua_settable(pL, -3);
	lua_pushstring(pL, "setFileWatchFunction");
	lua_pushcfunction(pL, &apiSetFileWatchFunction);
	lua_settable(pL, -3);
	lua_pushstring(pL, "toInstance");
	lua_pushcfunction(pL, &apiToInstance);
	lua_settable(pL, -3);
	lua_pushstring(pL, "setConsoleShow");
	lua_pushcfunction(pL, &apiSetConsoleShow);
	lua_settable(pL, -3);
	lua_pushstring(pL, "setConsoleTitle");
	lua_pushcfunction(pL, &apiSetConsoleTitle);
	lua_settable(pL, -3);
	lua_pushstring(pL, "clearConsole");
	lua_pushcfunction(pL, &apiClearConsole);
	lua_settable(pL, -3);
	lua_pushstring(pL, "fSkipNextHook");
	lua_pushinteger(pL, HookBitFlags::SKIP_NEXT);
	lua_settable(pL, -3);
	lua_pushstring(pL, "fSkipOriginal");
	lua_pushinteger(pL, HookBitFlags::SKIP_ORIG);
	lua_settable(pL, -3);
	lua_pushstring(pL, "fSkipAfterHook");
	lua_pushinteger(pL, HookBitFlags::SKIP_AFTER);
	lua_settable(pL, -3);
	lua_pushstring(pL, "fSkipQueue");
	lua_pushinteger(pL, HookBitFlags::SKIP_TO);
	lua_settable(pL, -3);
	lua_pushstring(pL, "fSkipNone");
	lua_pushinteger(pL, HookBitFlags::SKIP_NONE);
	lua_settable(pL, -3);
	lua_pushstring(pL, "fWithBreak"); // returning a number >0 in a Api.with() statement will break from the loop.
	lua_pushinteger(pL, 1);
	lua_settable(pL, -3);
	lua_pushstring(pL, "fWithContinue"); // a 0 will continue; it's there for readability purposes.
	lua_pushinteger(pL, 0);
	lua_settable(pL, -3);
	/* LMS API end */

	lua_settable(pL, -3); // -1 - api table, -2 = "Api", -3 - LMS global object.
}

void LMS::Lua::initGarbage(lua_State* pL) {
	/* LMS is at the stacktop */
	lua_pushstring(pL, "Garbage");
	lua_newtable(pL);

	/*
	* This is a table used for storing functions, methods and stuff.
	* It should never be touched in any way by Lua.
	*/

	/* for watcher */
	lua_pushstring(pL, "Watcher");
	lua_newtable(pL);
	lua_settable(pL, -3);

	/* for passing methods */
	lua_pushstring(pL, "Methods");
	lua_newtable(pL);
	lua_settable(pL, -3);

	lua_settable(pL, -3); // -1 - garbage table, -2 = "Garbage", -3 - LMS global object.
}

void LMS::Lua::initVersion(lua_State* pL) {
	/* LMS is at the stacktop */
	lua_pushstring(pL, "Version");
	lua_newtable(pL);

	bool isdebug{ false };
#ifdef _DEBUG
	isdebug = true;
#endif

	lua_pushstring(pL, "Date");
	lua_pushstring(pL, __TIMESTAMP__);
	lua_settable(pL, -3);
	lua_pushstring(pL, "Debug");
	lua_pushboolean(pL, isdebug);
	lua_settable(pL, -3);

	lua_settable(pL, -3);
}

void LMS::Lua::initConstants(lua_State* pL) {
	/* LMS is at the stacktop */
	lua_pushstring(pL, "Constants");
	lua_newtable(pL);

	/* initConstants begin */
	// GameMaker built-ins:
	for (std::size_t i{ 0 }; true; ++i) {
		const auto& c{ Constants[i] };
		if (!c.name) {
			// {nullptr,0.0} item == end of table
			break;
		}

		lua_pushstring(pL, c.name);
		lua_pushnumber(pL, c.value);
		lua_settable(pL, -3);
	}
	// Assets:
	assetAddLoop(pL, F_ObjectGetName);
	assetAddLoop(pL, F_SpriteGetName);
	assetAddLoop(pL, F_RoomGetName);
	assetAddLoop(pL, F_FontGetName);
	assetAddLoop(pL, F_AudioGetName);
	assetAddLoop(pL, F_PathGetName);
	assetAddLoop(pL, F_TimelineGetName);
	assetAddLoop(pL, F_TilesetGetName);
	assetAddLoop(pL, F_ShaderGetName);
	assetAddLoop(pL, F_ScriptGetName); // runtime funcs
	assetAddLoop(pL, F_ScriptGetName, 100001.0); // gml scripts
	/* initConstants end */

	lua_settable(pL, -3);
}

void LMS::Lua::initGlobal(lua_State* pL) {
	/* LMS is at the stacktop */
	lua_pushstring(pL, "Global");

	pushYYObjectBase(pL, g_pGlobal);

	lua_settable(pL, -3); // -1 yyobj accessor, -2 "Global", -3 LMS global object
}

void LMS::Lua::initScripts(lua_State* pL) {
	/* LMS is at the stacktop */
	lua_pushstring(pL, "Scripts");
	lua_newtable(pL);

	/* initScripts begin */
	RValue name{};
	RValue args[]{ -1.0 };
	const char* nn{ "<undefined>" };
	for (lua_Number i{ 100001 }; true; ++i) {
		args[0].val = static_cast<double>(i);
		F_ScriptGetName(name, nullptr, nullptr, 1, args);

		nn = name.pString->get();
		if (nn && nn[0] != '<') {
			lua_pushstring(pL, nn);
			lua_pushnumber(pL, i);
			lua_pushcclosure(pL, &scriptCall, 1);
			lua_settable(pL, -3); // -1 - value, -2 - key, -3 - Runtime table.
		}
		else {
			break;
		}
	}
	/* initScripts end */

	lua_settable(pL, -3);
}

void LMS::Lua::Init() {
	/* lua init start */

	luactx = luaL_newstate();
	/* ensure we were able to allocate memory */
	if (!luactx) {
		Global::throwError("Failed to allocate memory for Lua!");
		return;
	}
	/* allocated memory for lua? print copyright stuff then */
	std::cout << LUA_COPYRIGHT << std::endl << LUA_AUTHORS << std::endl;
	/* continue initialization: */
	luaL_openlibs(luactx);
	lua_newtable(luactx);
	/* LMS object is at the top of the stack */
	initMetamethods(luactx);
	initRuntime(luactx);
	initBuiltin(luactx);
	initConstants(luactx);
	initGarbage(luactx);
	initScripts(luactx);
	initVersion(luactx);
	initApi(luactx);
	initGlobal(luactx);
	lua_setglobal(luactx, "LMS");
	/* lua stack is now empty */

	/* lua init end */

	/* execute initial file */
	auto ok{ luaL_dofile(luactx, "main.lua") };
	if (ok != LUA_OK) {
		auto errmsg{ lua_tostring(luactx, -1) };
		Global::throwError(std::string("Lua execution error:\r\n") + errmsg);
	}
}
