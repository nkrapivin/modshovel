#include "pch.h"
#include "LibModShovel_Lua.h"
#include <iostream>
#include "LibModShovel_GMAutogen.h"

/*
* LibModShovel by nkrapivindev
* Class: LMS::Lua
* Purpose:: All-things-Lua!
* TODO: refactor eventscript hook to not copy&paste the fuck code >:(
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
TRoutine F_ArraySetOwner{};
TRoutine F_JSGetInstance{}; // instance id to YYObjectBase
TRoutine F_Method{};

TGetVarRoutine GV_EventType{};
TGetVarRoutine GV_EventSubtype{};
TGetVarRoutine GV_EventObject{};
TGetVarRoutine GV_InstanceCount{};
TGetVarRoutine GV_InstanceId{};

struct RBuiltinData {
	CInstance* owner;
	RVariableRoutine* var;
	LMS::GMBuiltinVariable* extra;
};

std::unordered_map<unsigned long long, PFUNC_YYGML> LMS::Lua::eventOriginalsMap{};
std::unordered_map<std::string, std::pair<RVariableRoutine*, LMS::GMBuiltinVariable*>> LMS::Lua::builtinsMap{};

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
				lua_pop(pL, 1);
				if (!msg) msg = "<LMS: nullptr string!>";
				if (!typname) typname = "<LMS: invalid type!>";
				// [index:type] = value
				std::cout << "[" << (i + 1) << ":" << typname << "] = " << msg << std::endl;
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
		| (static_cast<unsigned long long>(evType) << 32ull)
		| (static_cast<unsigned long long>(evSubType) << 48ull);
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
	luaL_setmetatable(pL, "__LMS_metatable_RValue_Struct__");
}

void LMS::Lua::pushYYObjectBase(lua_State* pL, const RValue& rv) {
	auto yyptr{ reinterpret_cast<RValue**>(lua_newuserdatauv(pL, sizeof(RValue*), 0)) };
	*yyptr = new RValue{ rv };
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

LMS::HookBitFlags LMS::Lua::tmpFlags{};

void LMS::Lua::pushYYCScriptArgs(lua_State* pL, int argc, RValue* args[]) {
	lua_newtable(pL);
	if (argc > 0) {
		for (int i{ 0 }; i < argc; ++i) {
			rvalueToLua(pL, *(args[i]));
			lua_seti(pL, -2, 1 + i);
		}
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
	SYYStackTrace __stack{ myname.c_str(), 1 };

	lua_getglobal(luactx, "LMS");
	lua_getfield(luactx, -1, "Garbage");

	/* before hooks first! */
	lua_getfield(luactx, -1, beforekey.c_str());

	if (lua_type(luactx, -1) != LUA_TNIL) {
		lua_len(luactx, -1);
		auto tablen{ static_cast<lua_Integer>(lua_tonumber(luactx, -1)) };
		lua_pop(luactx, 1);

		for (lua_Integer i{ 0 }; i < tablen; ++i) {
			// we keep a copy of this table on the stack (for reference stuff...)
			pushYYCScriptArgs(luactx, newargc, newargs);

			// function
			lua_geti(luactx, -2, 1 + i);
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
				lua_pushvalue(luactx, -1);
			}

			// args:
			lua_pushvalue(luactx, -4);

			// result:
			rvalueToLua(luactx, Result);

			tmpFlags = HookBitFlags::SKIP_NONE;
			auto ok{ lua_pcall(luactx, 4, 1, 0) };
			if (ok != LUA_OK) {
				Global::throwError(std::string("Script hook Lua execution error (Before):\r\n") + lua_tostring(luactx, -1));
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
			lua_len(luactx, -1);
			newargc = static_cast<int>(lua_tonumber(luactx, -1));
			lua_pop(luactx, 1);

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
					lua_geti(luactx, -1, 1 + i);
					newargarr[i] = luaToRValue(luactx, -1);
					newargs[i] = &newargarr[i];
					lua_pop(luactx, 1);
				}
			}

			lua_pop(luactx, 1); // pop args table that we kept on the stack.
		}
	}

	lua_pop(luactx, 1); // pop before table.

	if (callorig) {
		trampoline(selfinst, otherinst, Result, newargc, newargs);
	}

	/* after hooks */
	lua_getfield(luactx, -1, afterkey.c_str());

	if (lua_type(luactx, -1) != LUA_TNIL) {
		lua_len(luactx, -1);
		auto tablen{ static_cast<lua_Integer>(lua_tonumber(luactx, -1)) };
		lua_pop(luactx, 1);

		for (lua_Integer i{ 0 }; i < tablen; ++i) {
			// we keep a copy of this table on the stack (for reference stuff...)
			pushYYCScriptArgs(luactx, newargc, newargs);

			// function
			lua_geti(luactx, -2, 1 + i);
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
				lua_pushvalue(luactx, -1);
			}

			// args:
			lua_pushvalue(luactx, -4);

			// result:
			rvalueToLua(luactx, Result);

			tmpFlags = HookBitFlags::SKIP_NONE;
			auto ok{ lua_pcall(luactx, 4, 1, 0) };
			if (ok != LUA_OK) {
				Global::throwError(std::string("Script hook Lua execution error (After):\r\n") + lua_tostring(luactx, -1));
			}

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
			lua_len(luactx, -1);
			newargc = static_cast<int>(lua_tonumber(luactx, -1));
			lua_pop(luactx, 1);

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
					lua_geti(luactx, -1, 1 + i);
					newargarr[i] = luaToRValue(luactx, -1);
					newargs[i] = &newargarr[i];
					lua_pop(luactx, 1);
				}
			}

			lua_pop(luactx, 1); // pop args table that we kept on the stack.
		}
	}

	lua_pop(luactx, 1); // pop after table.

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

void LMS::Lua::hookRoutineEvent(CInstance* selfinst, CInstance* otherinst) {
	auto tmpself{ curSelf }, tmpother{ curOther };
	curSelf = selfinst;
	curOther = otherinst;
	arraySetOwner();

	RValue ev_obj{}, ev_type{}, ev_subtype{};
	GV_EventObject(curSelf, ARRAY_INDEX_NO_INDEX, &ev_obj);
	GV_EventType(curSelf, ARRAY_INDEX_NO_INDEX, &ev_type);
	GV_EventSubtype(curSelf, ARRAY_INDEX_NO_INDEX, &ev_subtype);

	std::string stacktracename{"gml_Object_"};
	stacktracename += curSelf->m_pObject->m_pName;
	stacktracename += "_";
	stacktracename += getEventName(static_cast<int>(ev_type.val));
	stacktracename += "_";
	if (ev_type.val != 4.0) {
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

	auto func{ eventOriginalsMap[key] };

	SYYStackTrace __stack{ stacktracename.c_str(), 1 };

	/* push LMS.Garbage to the stack: */
	lua_getglobal(luactx, "LMS");
	lua_getfield(luactx, -1, "Garbage");

	/* before hooks first! */
	lua_getfield(luactx, -1, beforeKey.c_str());

	if (lua_type(luactx, -1) != LUA_TNIL) {
		lua_len(luactx, -1);
		auto tablen{ static_cast<lua_Integer>(lua_tonumber(luactx, -1)) };
		lua_pop(luactx, 1);

		for (lua_Integer i{ 0 }; i < tablen; ++i) {
			auto before{ lua_gettop(luactx) };
			lua_geti(luactx, -1, 1 + i);
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
			
			auto ok{ lua_pcall(luactx, 2, LUA_MULTRET, 0) };
			if (ok != LUA_OK) {
				Global::throwError(std::string("Event hook Lua execution error (Before):\r\n") + lua_tostring(luactx, -1));
			}

			auto after{ lua_gettop(luactx) };
			auto diff{ after - before };
			if (diff != 0) {
				auto action{ 0.0 };
				if (lua_type(luactx, -1) == LUA_TNUMBER) action = lua_tonumber(luactx, -1);
				lua_pop(luactx, 1);
				--diff;
				if (diff > 0) {
					lua_pop(luactx, diff);
				}

				auto myflags{ static_cast<HookBitFlags>(action) };

				if (callorig) callorig = (myflags & HookBitFlags::SKIP_ORIG) == 0;
				if (callafter) callafter = (myflags & HookBitFlags::SKIP_AFTER) == 0;
				if (myflags & HookBitFlags::SKIP_TO) {
					break;
				}

				if (myflags & HookBitFlags::SKIP_NEXT) {
					++i;
				}
			}
		}
	}

	lua_pop(luactx, 1);

	/* call the original if defined: */
	if (callorig && func) {
		func(selfinst, otherinst);
	}

	/* and now, after hooks! */
	lua_getfield(luactx, -1, afterKey.c_str());

	if (lua_type(luactx, -1) != LUA_TNIL && callafter) {
		lua_len(luactx, -1);
		auto tablen{ static_cast<lua_Integer>(lua_tonumber(luactx, -1)) };
		lua_pop(luactx, 1);

		for (lua_Integer i{ 0 }; i < tablen; ++i) {
			auto before{ lua_gettop(luactx) };
			lua_geti(luactx, -1, 1 + i);
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

			auto ok{ lua_pcall(luactx, 2, LUA_MULTRET, 0) };
			if (ok != LUA_OK) {
				Global::throwError(std::string("Event hook Lua execution error (After):\r\n") + lua_tostring(luactx, -1));
			}

			auto after{ lua_gettop(luactx) };
			auto diff{ after - before };
			if (diff != 0) {
				auto action{ 0.0 };
				if (lua_type(luactx, -1) == LUA_TNUMBER) action = lua_tonumber(luactx, -1);
				lua_pop(luactx, 1);
				--diff;
				if (diff > 0) {
					lua_pop(luactx, diff);
				}

				auto myflags{ static_cast<HookBitFlags>(action) };
				if (myflags & HookBitFlags::SKIP_TO) {
					break;
				}

				if (myflags & HookBitFlags::SKIP_NEXT) {
					++i;
				}
			}
		}
	}

	lua_pop(luactx, 1);

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

	RValue rv{ nullptr };
	RValue args[]{ **rptr, lua_tonumber(pL, 2) - 1.0 };
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
	delete (*rptr);

	return 0;
}

int LMS::Lua::mtArrayTostring(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Array__")) };

	RValue res{ nullptr };
	RValue args[]{ **rptr };
	F_String(res, curSelf, curOther, 1, args);

	lua_pushfstring(pL, "array:%s", res.pString->get());
	return 1;
}

int LMS::Lua::mtStructIndex(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

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

	RValue res{ nullptr };
	RValue args[]{ **optr, luaToRValue(pL, 2), luaToRValue(pL, 3) };

	F_VariableStructSet(res, curSelf, curOther, 3, args);
	return 0;
}

int LMS::Lua::mtStructGc(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	//std::cout << "Freeing struct wrapper at " << reinterpret_cast<void*>(*optr) << std::endl;
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

int LMS::Lua::mtBuiltinIndex(lua_State* pL) {
	// ignored
	auto __tb{ luaL_checkudata(pL, 1, "__LMS_metatable_RValue_TBuiltin__") };

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

	RValue res{ nullptr };
	auto ind{ static_cast<int>(lua_tonumber(pL, 2)) };
	
	if (!rbptr->var->f_getroutine(rbptr->owner, (ind - 1), &res)) {
		Global::throwError(
			std::string("Failed to get built-in variable ") + std::string(rbptr->var->f_name) + std::string(":\r\n")
			+ std::string("Lua Array index: ") + std::to_string(ind)
		);
	}

	rvalueToLua(pL, res);
	return 1;
}

int LMS::Lua::mtRBuiltinNewindex(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

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
		// instance_id is special, it's length is `instance_count`.
		if (!GV_InstanceCount(rbptr->owner, ARRAY_INDEX_NO_INDEX, &res)) {
			Global::throwError("Failed to get instance_count!");
		}
	}
	else {
		res.val = static_cast<double>(rbptr->extra->arrayLength);
	}

	rvalueToLua(pL, res);
	return 1;
}

int LMS::Lua::mtOneWayMethodCall(lua_State* pL) {
	auto mtptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_OWMethod__")) };

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
	res = YYGML_CallMethod(curSelf, curOther, res, argc, **mtptr, yyc_args);

	if (argc > 0) {
		delete[] yyc_args;
		delete[] args;
	}

	rvalueToLua(pL, res);
	return 1;
}

int LMS::Lua::mtOneWayMethodGc(lua_State* pL) {
	auto mtptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_OWMethod__")) };
	delete (*mtptr);

	//std::cout << "Freeing method wrapper at " << reinterpret_cast<void*>(*mtptr) << std::endl;
	return 0;
}

int LMS::Lua::mtOneWayMethodTostring(lua_State* pL) {
	auto rptr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_OWMethod__")) };

	RValue res{ nullptr };
	RValue args[]{ **rptr };
	F_String(res, curSelf, curOther, 1, args);

	lua_pushfstring(pL, "owmethod:%s", res.pString->get());
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

int LMS::Lua::apiHookScript(lua_State* pL) {
	auto myind{ static_cast<lua_Integer>(lua_tonumber(pL, 1)) };
	auto hooktype{ lua_tostring(pL, 2) };
	
	luaL_argcheck(pL, myind >= 100000, 1, "Script index must be 100000 or larger!");
	luaL_argcheck(pL, strcmp(hooktype, "before") == 0 || strcmp(hooktype, "after") == 0, 2,
		"Hook type must be either 'before' or 'after'.");
	luaL_argcheck(pL, lua_type(pL, 3) != LUA_TNIL, 3, "Callable object is `nil`.");

	RValue scrname{ nullptr };
	RValue args[]{ myind };
	F_ScriptGetName(scrname, curSelf, curOther, 1, args);
	const char* myn{ scrname.pString->get() };
	luaL_argcheck(pL, myn && myn[0] != '<', 1, "Script does not exist!");

	std::string realname{ std::string("gml_Script_") + myn };

	bool found{ false };

	for (auto i{ 0 }; i < g_pLLVMVars->nYYCode; ++i) {
		if (realname == g_pLLVMVars->pGMLFuncs[i].pName) {
			// found!
			myind -= 100000;
			if (ScapegoatScripts[myind].t) {
				// already hooked?
				found = true;
				break;
			}

			auto fun{ g_pLLVMVars->pGMLFuncs[i].pFunc };
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
		}
	}

	luaL_argcheck(pL, found, 1, "Unable to find a script in the YYGML table.");

	if (strcmp(hooktype, "before") == 0) {
		realname += "_Before";
	}
	else if (strcmp(hooktype, "after") == 0) {
		realname += "_After";
	}

	lua_getglobal(pL, "LMS");
	lua_getfield(pL, -1, "Garbage");
	lua_getfield(pL, -1, realname.c_str());
	if (lua_type(pL, -1) == LUA_TNIL) {
		lua_pop(pL, 1); // pop nil
		lua_newtable(pL); // empty table
		lua_setfield(pL, -2, realname.c_str());
		lua_getfield(pL, -1, realname.c_str());
	}

	lua_len(pL, -1);
	auto tablen{ static_cast<lua_Integer>(lua_tonumber(pL, -1)) };
	lua_pop(pL, 1); // length

	lua_pushvalue(pL, 3);
	lua_seti(pL, -2, 1 + tablen);

	lua_pop(pL, 1); // hook table
	lua_pop(pL, 1); // 'Garbage'
	lua_pop(pL, 1); // 'LMS'

	lua_pushstring(pL, realname.c_str());
	return 1;
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
			Global::throwError("LUA_TFUNCTION to gml method conversion is not implemented.");
			return RValue{};
		}

		case LUA_TUSERDATA: {
			RValue rv{ nullptr };
			/* check if RV array: */
			auto rptr{ reinterpret_cast<RValue**>(luaL_testudata(pL, index, "__LMS_metatable_RValue_Array__")) };
			auto sptr{ reinterpret_cast<RValue**>(luaL_testudata(pL, index, "__LMS_metatable_RValue_Struct__")) };
			auto owmtptr{ reinterpret_cast<RValue**>(luaL_testudata(pL, index, "__LMS_metatable_RValue_OWMethod__")) };
			if (rptr) {
				rv = **rptr;
			}
			else if (sptr) {
				rv = **sptr;
			}
			else if (owmtptr) {
				rv = **owmtptr;
			}
			else {
				Global::throwError("Not implemented userdata conversion!");
			}

			return rv;
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
			auto rptr{ reinterpret_cast<RValue**>(lua_newuserdatauv(pL, sizeof(RefDynamicArrayOfRValue*), 0)) };
			*rptr = new RValue{ rv };
			luaL_setmetatable(pL, "__LMS_metatable_RValue_Array__");
			break;
		}

		case VALUE_OBJECT: {
			/* check the kind of YYObjectBase ... */
			YYObjectBase* obj{ rv.pObj };
			/* methods require a special funny: */
			if (obj->m_kind == YYObjectBaseKind::KIND_SCRIPTREF) {
				auto rvptr{ reinterpret_cast<RValue**>(lua_newuserdatauv(pL, sizeof(RValue**), 0)) };
				*rvptr = new RValue{ rv };
				luaL_setmetatable(pL, "__LMS_metatable_RValue_OWMethod__");
			}
			else {
				/* otherwise push a generic struct wrapper: */
				pushYYObjectBase(luactx, rv);
			}

			break;
		}
	}
}

int LMS::Lua::apiHookEvent(lua_State* pL) {
	auto objind{ static_cast<uint>(lua_tonumber(pL, 1)) };
	auto evtype{ static_cast<uint64>(lua_tonumber(pL, 2)) };
	auto evsubtype{ static_cast<uint64>(lua_tonumber(pL, 3)) };
	auto hooktype{ lua_tostring(pL, 4) };

	luaL_argcheck(pL, strcmp(hooktype, "before") == 0 || strcmp(hooktype, "after") == 0, 4, "Invalid hook type, can be either `before` or `after`.");

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
	lua_getfield(pL, -1, "Garbage");
	lua_getfield(pL, -1, luaIdent.c_str());
	if (lua_type(pL, -1) == LUA_TNIL) { /* stacktop - Object Table */
		lua_pop(pL, 1); // pop nil
		lua_newtable(pL); // empty table
		lua_setfield(pL, -2, luaIdent.c_str());
		lua_getfield(pL, -1, luaIdent.c_str());
	}

	lua_len(pL, -1); /* stacktop - Object Table */
	auto tablen{ static_cast<lua_Integer>(lua_tonumber(pL, -1)) }; /* stacktop - Object Table Len */
	lua_pop(pL, 1);

	lua_pushvalue(pL, 5);
	lua_seti(pL, -2, 1 + tablen);

	lua_pop(pL, 1); // hook
	lua_pop(pL, 1); // garbage
	lua_pop(pL, 1); // lms

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
		{nullptr, nullptr}
	};

	luaL_newmetatable(pL, "__LMS_metatable_RValue_RBuiltin__");
	luaL_setfuncs(pL, rbuiltinmt, 0);
	lua_pop(pL, 1);

	/* One way method call wrapper */
	const luaL_Reg owmethod[]{
		{"__call", &mtOneWayMethodCall},
		{"__gc", &mtOneWayMethodGc},
		{"__tostring", &mtOneWayMethodTostring},
		{nullptr, nullptr}
	};

	luaL_newmetatable(pL, "__LMS_metatable_RValue_OWMethod__");
	luaL_setfuncs(pL, owmethod, 0);
	lua_pop(pL, 1);
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
	}
	/* initRuntime end */

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
	RValue tst{ 0.0 };
	if (!GV_InstanceCount(curSelf, ARRAY_INDEX_NO_INDEX, &tst)) {
		Global::throwError("What the fuck?? (in getInstanceLen)");
	}

	return static_cast<double>(tst.val);
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
	pushYYObjectBase(pL, reinterpret_cast<YYObjectBase*>(curOther));
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
			Global::throwError("Invalid number passed to apiWith(): " + std::to_string(rvnum));
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
	lua_pushstring(pL, "toInstance");
	lua_pushcfunction(pL, &apiToInstance);
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
		if (c.name == nullptr && c.value == 0.0) {
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
