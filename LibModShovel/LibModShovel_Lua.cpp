#include "pch.h"
#include "LibModShovel_Lua.h"
#include <iostream>
#include "LibModShovel_Console.h"
#include "LibModShovel_GMAutogen.h"
#include "LibModShovel_MethodAutogen.h"
#include <array>

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

TGetVarRoutine GV_EventType{};
TGetVarRoutine GV_EventSubtype{};
TGetVarRoutine GV_EventObject{};
TGetVarRoutine GV_InstanceCount{};
TGetVarRoutine GV_InstanceId{};
TGetVarRoutine GV_ProgramDirectory{};
TGetVarRoutine GV_PhyColPoints{};

std::mutex FreeMtMutex{};
LMS_ExceptionHandler_t LMS_ExceptionHandler{};

struct RBuiltinData {
	CInstance* owner;
	RVariableRoutine* var;
	LMS::GMBuiltinVariable* extra;
	double(*getlenroutine)(lua_State* pL);
};

std::unordered_map<std::uint32_t, PFUNC_YYGML> LMS::Lua::eventOriginalsMap{};
std::unordered_map<std::string, std::pair<RVariableRoutine*, LMS::GMBuiltinVariable*>> LMS::Lua::builtinsMap{};
double LMS::Lua::pinDsMap{ -1.0 };

RFunction* F_DsExists{};
RFunction* F_DsMapCreate{};
RFunction* F_DsMapFindValue{};
RFunction* F_DsMapReplace{};
RFunction* F_DsMapDelete{};
RFunction* F_JSArraySetOwner{};
RFunction* F_JSGetInstance{};
RFunction* F_ObjectGetName{};
RFunction* F_ArrayCreate{};
RFunction* F_ArrayLength{};
RFunction* F_ArrayGet{};
RFunction* F_ArraySet{};
RFunction* F_String{};
RFunction* F_VariableStructGet{};
RFunction* F_VariableStructSet{};
RFunction* F_ShaderGetName{};
RFunction* F_TimelineGetName{};
RFunction* F_ScriptGetName{};
RFunction* F_VariableGlobalSet{};
RFunction* F_Typeof{};
RFunction* F_SpriteGetName{};
RFunction* F_RoomGetName{};
RFunction* F_FontGetName{};
RFunction* F_AudioGetName{};
RFunction* F_PathGetName{};
RFunction* F_TilesetGetName{};
RFunction* F_VariableStructGetNames{};
LMS::HookBitFlags LMS::Lua::tmpFlags{ LMS::HookBitFlags::SKIP_NONE };

/* called by LMS when a new array or struct wrapper is created and returned to Lua */
void LMS::Lua::pinYYObjectBase(YYObjectBase* pObj) {
	if (!pObj) {
		return;
	}

	if (!rcall(F_DsExists, curSelf, curOther, pinDsMap, 1.0/*ds_type_map*/)) {
		/* ds map destroyed? */
		return;
	}

	auto refcountkey{ reinterpret_cast<const void*>(reinterpret_cast<LPBYTE>(pObj) + 1) };
	auto objkey{ reinterpret_cast<const void*>(pObj) };
	/* obtain refcount */
	RValue refcount{ rcall(F_DsMapFindValue, curSelf, curOther, pinDsMap, RValue{refcountkey}) };

	/* no value? */
	if ((refcount.kind & MASK_KIND_RVALUE) == VALUE_UNDEFINED) {
		// object:
		rcall(F_DsMapReplace, curSelf, curOther, pinDsMap, RValue{ objkey }, RValue{ pObj });
		rcall(F_DsMapReplace, curSelf, curOther, pinDsMap, RValue{ refcountkey }, 1.0);
	}
	else {
		/* increment refcount: */
		rcall(F_DsMapReplace, curSelf, curOther, pinDsMap, RValue{ refcountkey }, ++refcount);
	}
}

/* only called by '__gc' metamethods in struct/array wrappers. */
void LMS::Lua::unpinYYObjectBase(YYObjectBase* pObj) {
	if (!pObj) {
		return;
	}

	auto refcountkey{ reinterpret_cast<const void*>(reinterpret_cast<LPBYTE>(pObj) + 1) };
	auto objkey{ reinterpret_cast<const void*>(pObj) };

	if (!rcall(F_DsExists, curSelf, curOther, pinDsMap, 1.0/*ds_type_map*/)) {
		/* ds map destroyed? */
		return;
	}

	/* obtain refcount */
	RValue refcount{ rcall(F_DsMapFindValue, curSelf, curOther, pinDsMap, RValue{refcountkey}) };

	if ((refcount.kind & MASK_KIND_RVALUE) == VALUE_UNDEFINED) {
		luaL_error(luactx, "Trying to unpin a non-existing YYObjectBase.");
	}
	else {
		/* decrement refcount and update: */
		rcall(F_DsMapReplace, curSelf, curOther, pinDsMap, RValue{ refcountkey }, --refcount);
		/* do we need to free? */
		if (static_cast<double>(refcount) <= 0.0) {
			rcall(F_DsMapDelete, curSelf, curOther, pinDsMap, RValue{ refcountkey });
			rcall(F_DsMapDelete, curSelf, curOther, pinDsMap, RValue{ objkey });

#ifdef _DEBUG
			//std::cout << "Freed pinned YYObjectBase: 0x" << args[1].ptr << std::endl;
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
		throw std::runtime_error{ "stringToWstring String conversion failed (1)." };

	ws.resize(siz);

	siz = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), str.size(), const_cast<wchar_t*>(ws.data()), siz);
	if (siz <= 0)
		throw std::runtime_error{ "stringToWstring String conversion failed (2)." };

	return ws;
}

std::string LMS::Lua::wstringToString(const std::wstring& str) {
	std::string as{};

	// ws is already empty.
	if (str.size() == 0)
		return as;

	int siz{ WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, str.c_str(), str.size(), nullptr, 0, nullptr, nullptr) };
	if (siz <= 0)
		throw std::runtime_error{ "wstringToString String conversion failed (1)." };

	as.resize(siz);

	siz = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, str.c_str(), str.size(), const_cast<char*>(as.data()), siz, nullptr, nullptr);
	if (siz <= 0)
		throw std::runtime_error{ "wstringToString String conversion failed (2)." };

	return as;
}

int LMS::Lua::apiDebugEnterRepl(lua_State* pL) {
	std::cout << "Entering REPL loop, type 'quit' to continue..." << std::endl;

	static std::array<std::string, 8> luaCode2String{
		"LUA_OK - All is fine.",
		"LUA_YIELD - A coroutine yielded.",
		"LUA_ERRRUN - A runtime error.",
		"LUA_ERRSYNTAX - A syntax error.",
		"LUA_ERRMEM - A memory allocation error.",
		"LUA_ERRERR - An error while running the message handler.",
		"LUA_ERRFILE - A file-related error.",
		"<LMS:Unknown Error> - A... What...? Corrupted state."
	};

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
		std::cout << "Code: " << luaCode2String[ok] << std::endl;
		std::cout << "Returns:" << std::endl;
		stackPrinter(pL);
		/* pop all the return values... */
		if (diff != 0) {
			lua_pop(pL, diff);
		}
	}

	std::cout << std::endl << "Leaving REPL loop..." << std::endl;
	return 0;
}

std::uint32_t genKey(int objId, int evType, int evSubType) {
	return
		( (static_cast<std::uint32_t>(objId     & 0xffffU) <<  0U)
		| (static_cast<std::uint32_t>(evType    &   0xffU) << 16U)
		| (static_cast<std::uint32_t>(evSubType &   0xffU) << 24U)
		);
}

void LMS::Lua::arraySetOwner() {
	rcall(F_JSArraySetOwner, curSelf, curOther, static_cast<int64>(reinterpret_cast<std::intptr_t>(curSelf)));
}

void LMS::Lua::pushRBuiltinAccessor(lua_State* pL, CInstance* pOwner, const std::string& name) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(lua_newuserdatauv(pL, sizeof(RBuiltinData), 0)) };
	const auto item{ builtinsMap.find(name) };

	if (item == builtinsMap.end()) {
		luaL_error(pL, "Unknown builtin variable name %s", name.c_str());
	}

	rbptr->owner = pOwner;
	rbptr->var = item->second.first;
	rbptr->extra = item->second.second;
	rbptr->getlenroutine = nullptr;
	if (strcmp(rbptr->var->f_name, "instance_id") == 0) {
		rbptr->getlenroutine = &getInstanceLen;
	}
	else if (strcmp(rbptr->var->f_name, "phy_collision_x") == 0
		|| strcmp(rbptr->var->f_name, "phy_collision_y") == 0
		|| strcmp(rbptr->var->f_name, "phy_col_normal_x") == 0
		|| strcmp(rbptr->var->f_name, "phy_col_normal_y") == 0) {
		rbptr->getlenroutine = &getPhyColPoints;
	}

	luaL_setmetatable(pL, "__LMS_metatable_RValue_RBuiltin__");
}

std::string LMS::Lua::getEventName(int evtype) {
	static std::array<std::string, 14> event2Name{ "Create", "Destroy", "Alarm", "Step", "Collision", "Keyboard", "Mouse", "Other", "Draw", "KeyPress",
	"KeyRelease", "Trigger", "CleanUp", "Gesture" };

	return event2Name[evtype];
}

void LMS::Lua::pushYYObjectBase(lua_State* pL, YYObjectBase* thing) {
	auto yyptr{ reinterpret_cast<RValue**>(lua_newuserdatauv(pL, sizeof(RValue*), 1)) };
	*yyptr = new RValue{ thing };
	if ((*yyptr)->pObj && (*yyptr)->pObj->m_kind == YYObjectBaseKind::KIND_ARRAY) {
		(*yyptr)->kind = VALUE_ARRAY;
		(*yyptr)->pArray->refcount++;
	}

	pinYYObjectBase((*yyptr)->pObj);
	luaL_setmetatable(pL, "__LMS_metatable_RValue_Struct__");
}

void LMS::Lua::pushYYObjectBase(lua_State* pL, const RValue& rv) {
	auto yyptr{ reinterpret_cast<RValue**>(lua_newuserdatauv(pL, sizeof(RValue*), 1)) };
	*yyptr = new RValue{ rv };
	pinYYObjectBase((*yyptr)->pObj);
	luaL_setmetatable(pL, "__LMS_metatable_RValue_Struct__");
}

int LMS::Lua::scriptCall(lua_State* pL) {
	double mtind{ lua_tonumber(pL, lua_upvalueindex(1)) };
	RValue mvtret{ nullptr };
	RValue rvmtind{ mtind };

	std::unique_ptr<RValue[]> args{  };
	std::unique_ptr<RValue*[]> yyc_args{  };
	std::size_t argc{ static_cast<std::size_t>(lua_gettop(pL)) };
	if (argc > 0) {
		args = std::make_unique<RValue[]>(argc);
		yyc_args = std::make_unique<RValue* []>(argc);
		for (std::size_t i{ 0 }; i < argc; ++i) {
			args[i] = luaToRValue(pL, 1 + i);
			yyc_args[i] = &args[i];
		}
	}

	try {
		mvtret = YYGML_CallMethod(curSelf, curOther, mvtret, static_cast<int>(argc), rvmtind, yyc_args.get());
	}
	catch (const YYGMLException& e) {
		if (LMS_ExceptionHandler) {
			LMS_ExceptionHandler(e.GetExceptionObject());
		}
		else {
			throw;
		}
	}

	rvalueToLua(pL, mvtret);
	return 1;
}

void LMS::Lua::stackPrinter(lua_State* pL) {
	std::cout << "-- Stacktrace begin                                      --" << std::endl;
	std::cout << "-- Format is [stackIndex:valType] = valueAsString        --" << std::endl;

	/* the stack in lua starts at element lua_gettop() and ends at 1. 0 is an invalid stack index. -1 is a pseudo ind. */
	auto top{ lua_gettop(pL) };
	std::cout << "-- Top element index: " << top << " --" << std::endl;
	for (auto i{ top }; i > 0; --i) {
		auto typ{ lua_type(pL, i) };

		std::cout
			<< "[" << i << ":"
			<< (lua_typename(pL, typ) ? lua_typename(pL, typ) : "!LMSUnknownType!")
			<< "] = ";

		switch (typ) {
			case LUA_TNONE:          std::cout << "<none type, LUA_TNONE>"; break;
			case LUA_TNIL:           std::cout << "nil"; break;
			case LUA_TBOOLEAN:       std::cout << (lua_toboolean(pL, i) ? "true" : "false"); break;
			case LUA_TLIGHTUSERDATA: std::cout << "<lightuserdata, pointer=0x" << lua_topointer(pL, i) << ">"; break;
			case LUA_TNUMBER:        std::cout << lua_tonumber(pL, i); break;
			case LUA_TSTRING:        std::cout << (lua_tostring(pL, i) ? lua_tostring(pL, i) : "<nullptr string>"); break;
			case LUA_TTABLE:         std::cout << "<table, length=" << luaL_len(pL, i) << ">"; break;
			case LUA_TFUNCTION:      std::cout << "<function>"; break;
			case LUA_TUSERDATA:      std::cout << "<userdata, pointer=0x" << lua_topointer(pL, i) << ">"; break;
			case LUA_TTHREAD:        std::cout << "<thread>"; break;
			case LUA_NUMTYPES:       std::cout << "<LUA_TNUMTYPES?!>"; break;
			default:                 std::cout << "<LMS: ERROR, Unknown Lua type constant. Stack is FUCKED>"; break;
		}

		std::cout << std::endl;
	}

	std::cout << "-- Stacktrace finished                                   --" << std::endl;
}

int LMS::Lua::atPanicLua(lua_State* pL) {
	std::cout << "-- PANIC! Stacktrace begin, from top to bottom:          --" << std::endl;

	stackPrinter(pL);
	
	Global::throwError("We're very sorry, Lua had entered a panic state, please see the full stacktrace in the debug console. The program will close after you click OK.");

	/* technically the function never returns since throw Error calls std::abort, but still. */
	return 0; /* didn't push anything. */
}

lua_Integer LMS::Lua::findRealArrayLength(lua_State* pL, bool hasStrings) {
	auto table{ lua_gettop(pL) };
	auto notempty{ false };

	lua_Integer last{ 0 };

	/* loop through the table until we find the LAST key. */
	for (lua_pushnil(pL); lua_next(pL, table) != 0; lua_pop(pL, 1)) {
		notempty = true;

		if (hasStrings) {
			++last;
		}
		else {
			int isnum{};
			auto key{ static_cast<lua_Integer>(lua_tonumberx(pL, -2, &isnum)) };

			if (!isnum) {
				return luaL_argerror(pL, table, "The table has non-numeric keys, not an array?");
			}

			last = std::max(last, key);
		}
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

void LMS::Lua::doScriptHookCall(bool& callorig, bool& callafter, const std::string& prefix, const std::string& stacktracename, RValue& Result, std::unique_ptr<RValue[]>& newargarr, std::unique_ptr<RValue*[]>& newargs, int& newargc) {
	if (lua_type(luactx, -1) == LUA_TNIL) return;

	auto tablen{ findRealArrayLength(luactx) };
	for (lua_Integer i{ 0 }; i < tablen; ++i) {
		// we keep a copy of this table on the stack (for reference stuff...)
		pushYYCScriptArgs(luactx, newargc, newargs.get());

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

		lua_call(luactx, 4, 1);

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
		Result = luaToRValue(luactx, lua_gettop(luactx));
		lua_pop(luactx, 1); // pop result

		// fetch args, they *should* be at the stacktop:
		newargc = static_cast<int>(findRealArrayLength(luactx));

		newargs = nullptr;
		newargarr = nullptr;

		if (newargc > 0) {
			newargarr = std::make_unique<RValue[]>(newargc);
			newargs = std::make_unique<RValue*[]>(newargc);
			for (int ii{ 0 }; ii < newargc; ++ii) {
				lua_pushinteger(luactx, 1 + ii);
				lua_gettable(luactx, -2);
				newargarr[ii] = luaToRValue(luactx, lua_gettop(luactx));
				lua_pop(luactx, 1);
				newargs[ii] = &newargarr[ii];
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
	std::unique_ptr<RValue*[]> newargs{  };
	std::unique_ptr<RValue[]> newargarr{  };

	/* need to copy arguments... */
	if (newargc > 0) {
		newargarr = std::make_unique<RValue[]>(newargc);
		newargs = std::make_unique<RValue*[]>(newargc);
		for (auto i{ 0 }; i < newargc; ++i) {
			newargarr[i] = *(args[i]);
			newargs[i] = &newargarr[i];
		}
	}

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
		doScriptHookCall(callorig, callafter, "Before", beforekey, Result, newargarr, newargs, newargc);
		lua_pop(luactx, 1); // pop before table.
	}

	if (callorig && trampoline) {
		trampoline(selfinst, otherinst, Result, newargc, newargs.get());
	}

	/* after hooks */
	if (!skiphooks) {
		lua_pushstring(luactx, afterkey.c_str());
		lua_gettable(luactx, -2);
		doScriptHookCall(callorig, callafter, "After", beforekey, Result, newargarr, newargs, newargc);
		lua_pop(luactx, 1); // pop after table.
	}

	/* pop garbage and lms */
	lua_pop(luactx, 1); // garbage
	lua_pop(luactx, 1); // lms

	/* deallocate args: */
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

	lua_call(luactx, 2 + argc, LUA_MULTRET);
	auto after{ lua_gettop(luactx) };
	auto diff{ after - before };
	if (diff != 0) {
		Result = luaToRValue(luactx, lua_gettop(luactx));
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

		lua_call(luactx, 2, LUA_MULTRET);
		auto after{ lua_gettop(luactx) };
		auto diff{ after - before };

		if (diff != 0) {
			auto action{ HookBitFlags::SKIP_NONE };
			if (lua_type(luactx, -1) == LUA_TNUMBER) action = static_cast<HookBitFlags>(static_cast<lua_Integer>(lua_tonumber(luactx, -1)));
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

	RValue ev_obj{}, ev_type{}, ev_subtype{};
	GV_EventObject(curSelf, ARRAY_INDEX_NO_INDEX, &ev_obj);
	GV_EventType(curSelf, ARRAY_INDEX_NO_INDEX, &ev_type);
	GV_EventSubtype(curSelf, ARRAY_INDEX_NO_INDEX, &ev_subtype);
	RValue ev_objname{ rcall(F_ObjectGetName, curSelf, curOther, ev_obj) };

	std::string stacktracename{"gml_Object_"};
	stacktracename += ev_objname.pString->get();
	stacktracename += "_";
	stacktracename += getEventName(static_cast<int>(ev_type));
	stacktracename += "_";
	if (static_cast<double>(ev_type) != 4.0 /*collsion event*/) {
		stacktracename += std::to_string(static_cast<int>(ev_subtype));
	}
	else {
		stacktracename += curOther->m_pObject->m_pName;
	}

	std::string beforeKey{ stacktracename + "_Before" };
	std::string afterKey{ stacktracename + "_After" };

	auto callorig{ true };
	auto callafter{ true };
	auto skiphooks{ false };

	auto func{ eventOriginalsMap[genKey(
			static_cast<int>(ev_obj),
			static_cast<int>(ev_type),
			static_cast<int>(ev_subtype)
		)] };

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

double LMS::Lua::assetAddLoop(lua_State* pL, RFunction* routine, double start) {
	double i{ 0.0 };

	try {
		const char* name{ "<undefined>" };

		for (i = start; true; ++i) {
			RValue n{ rcall(routine, curSelf, curOther, i) };

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

int LMS::Lua::mtStructNext(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };
	auto argc{ lua_gettop(pL) };

	if ((!(*optr)) || (!((*optr)->pObj))) {
		lua_pushnil(pL);
		return 1;
	}

	if ((*optr)->isArray()) {
		auto arraylen{ static_cast<double>(rcall(F_ArrayLength, curSelf, curOther, **optr)) };

		// -1 cuz if the argument is `nil`, the index increments and we start at 0.
		lua_Integer arrayind{ -1 };
		if (argc > 1 && lua_type(pL, 2) != LUA_TNIL) {
			// convert lua array index to gml index.
			// all further operations will use gml array indexing.
			int isn{};
			arrayind = static_cast<lua_Integer>(lua_tonumberx(pL, 2, &isn) - 1.0);
			if (!isn) return luaL_argerror(pL, 2, "Array key can only be a number.");
		}

		// advance to next element.
		// so if an index 0 is passed on a 2 length array, we get the [1]th element
		++arrayind;

		// index out of range?
		if (arrayind < 0 || arrayind >= static_cast<lua_Integer>(arraylen)) {
			lua_pushnil(pL);
			return 1;
		}

		// otherwise:
		RValue res{ rcall(F_ArrayGet, curSelf, curOther, **optr, arrayind) };
		lua_pushinteger(pL, 1 + arrayind);
		rvalueToLua(pL, res);
		return 2;
	}
	else {
		lua_getiuservalue(pL, 1, 1);
		if (lua_type(pL, -1) == LUA_TNIL) {
			lua_pop(pL, 1);
			/* oh shit we don't have a names table assigned shit uhh let's make it real quick... */
			mtStructPushNamesTable(pL, **optr);
			lua_setiuservalue(pL, 1, 1);
			lua_getiuservalue(pL, 1, 1);
			if (lua_type(pL, -1) == LUA_TNIL) {
				/* UHHHHHHHHHHHHHHHHHHHHHHH */
				return luaL_error(pL, "Unable to generate a varnames table.");
			}
		}

		auto mytable{ lua_gettop(pL) };

		if (argc > 1 && lua_type(pL, 2) != LUA_TNIL) {
			lua_pushvalue(pL, 2);
		}
		else {
			lua_pushnil(pL);
		}

		auto nextret{ lua_next(pL, mytable) };
		if (nextret == 0) {
			/* no more elements: */
			lua_pop(pL, 1); // pop the table.
			lua_pushnil(pL);
			return 1;
		}
		else {
			lua_pop(pL, 1); // the value will be integer 0, ignore that.
			/* key is at the stacktop now */
			auto keyidx{ lua_gettop(pL) };
			/* get the value: */
			luaL_getmetafield(pL, 1, "__index");
			lua_pushvalue(pL, 1);
			lua_pushvalue(pL, keyidx);
			// we know for a fact that our __index always takes two args and always returns a value.
			lua_call(pL, 2, 1);
			lua_remove(pL, keyidx - 1); // get rid of the table below the key.
			return 2;
		}
	}
}

int LMS::Lua::mtStructPairs(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	if ((*optr) && !((*optr)->isArray())) {
		mtStructPushNamesTable(pL, **optr);
		lua_setiuservalue(pL, 1, 1);
	}

	luaL_getmetafield(pL, 1, "__next");
	lua_pushvalue(pL, 1);
	lua_pushnil(pL);
	return 3;
}

int LMS::Lua::mtStructIpairs(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	if ((*optr) && !((*optr)->isArray())) {
		mtStructPushNamesTable(pL, **optr);
		lua_setiuservalue(pL, 1, 1);
	}

	luaL_getmetafield(pL, 1, "__next");
	lua_pushvalue(pL, 1);
	lua_pushinteger(pL, 0);
	return 3;
}

lua_Integer LMS::Lua::mtStructPushNamesTable(lua_State* pL, const RValue& yyobj) {
	lua_newtable(pL);
	auto tabind{ lua_gettop(pL) };

	RValue clone{ yyobj };
	lua_Integer indx{ 0 };

	if (clone.pObj != nullptr) {
		if (clone.pObj->m_kind == YYObjectBaseKind::KIND_CINSTANCE) {
			/* also push built-in var names: */
			for (auto i{ 0 }; true; ++i) {
				if (!BuiltinVariables[i].name) {
					break;
				}

				if (BuiltinVariables[i].isSelf) {
					lua_pushstring(pL, BuiltinVariables[i].name);
					lua_pushinteger(pL, 0);
					lua_settable(pL, tabind);
					++indx;
				}
			}
		}
		
		while (clone.pObj) {
			auto mynames{ rcall(F_VariableStructGetNames, curSelf, curOther, clone) };
			auto mynameslen{ static_cast<lua_Integer>(rcall(F_ArrayLength, curSelf, curOther, mynames)) };

			for (lua_Integer i{ 0 }; i < mynameslen; ++i) {
				RValue it{ rcall(F_ArrayGet, curSelf, curOther, mynames, i) };
				rvalueToLua(pL, it);
				lua_pushinteger(pL, 0);
				lua_settable(pL, tabind);
				++indx;
			}

			/* traverse through the prototype list: */
			clone.pObj = clone.pObj->m_prototype;
		}
	}

	/* return the length of the table: */
	return (indx == 0) ? 0 : (indx + 1);
}

int LMS::Lua::mtStructIndex(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	if ((!(*optr)) || (!((*optr)->pObj)) || lua_type(pL, 2) == LUA_TNIL) {
		lua_pushnil(pL);
	}
	else if ((*optr)->isArray()) {
		int isn{};
		double rind{ lua_tonumberx(pL, 2, &isn) - 1.0 };
		if (!isn) return luaL_error(pL, "Array key can only be a number.");

		auto arrlen{ rcall(F_ArrayLength, curSelf, curOther, **optr) };
		if (rind < 0.0 || rind >= static_cast<double>(arrlen)) {
			lua_pushnil(pL);
		}
		else {
			RValue res{ rcall(F_ArrayGet, curSelf, curOther, **optr, rind) };
			rvalueToLua(pL, res);
		}
	}
	else {
		if ((*optr)->pObj->m_kind == YYObjectBaseKind::KIND_CINSTANCE) {
			auto n{ std::string(lua_tostring(pL, 2)) };
			const auto& it{ builtinsMap.find(n) };
			if (it != builtinsMap.end() && it->second.second->arrayLength > 0) {
				// push accessor...
				pushRBuiltinAccessor(pL, reinterpret_cast<CInstance*>((*optr)->pObj), n);
				return 1;
			}
		}

		RValue res{ rcall(F_VariableStructGet, curSelf, curOther, **optr, luaToRValue(pL, 2)) };
		rvalueToLua(pL, res);
	}

	return 1;
}

int LMS::Lua::mtStructNewindex(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	if ((!(*optr)) || (!((*optr)->pObj)) || lua_type(pL, 2) == LUA_TNIL) {
		/*
		* nil keys are not allowed.
		* TODO: throw an error...?
		*/
	}
	else if ((*optr)->isArray()) {
		int isn{};
		double n{ lua_tonumberx(pL, 2, &isn) - 1.0 };
		if (!isn) return luaL_error(pL, "Array key can only be a number.");
		if (n < 0.0) {
			lua_pushnil(pL);
		}
		else {
			rcall(F_ArraySet, curSelf, curOther, **optr, n, luaToRValue(pL, 3));
		}
	}
	else {
		rcall(F_VariableStructSet, curSelf, curOther, **optr, luaToRValue(pL, 2), luaToRValue(pL, 3));
		mtStructPushNamesTable(pL, **optr);
		lua_setiuservalue(pL, 1, 1);
	}

	return 0;
}

int LMS::Lua::mtStructGc(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	if (*optr) {
		if ((*optr)->pObj) {
			unpinYYObjectBase((*optr)->pObj);
		}

		delete (*optr);
		(*optr) = nullptr;
	}
	
	return 0;
}

int LMS::Lua::mtStructTostring(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };
	if ((!(*optr)) || (!((*optr)->pObj))) {
		lua_pushstring(pL, "yyobjectbase:null");
		return 1;
	}

	RValue res{ rcall(F_String, curSelf, curOther, **optr) };
	lua_pushfstring(pL, "yyobjectbase:%s", res.pString->get());
	return 1;
}

int LMS::Lua::mtStructLen(lua_State* pL) {
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	if ((!(*optr)) || (!((*optr)->pObj))) {
		lua_pushinteger(pL, 0);
		return 1;
	}

	if ((*optr)->isArray()) {
		RValue arrlen{ rcall(F_ArrayLength, curSelf, curOther, **optr) };
		rvalueToLua(pL, arrlen);
	}
	else {
		lua_getiuservalue(pL, 1, 1);
		if (lua_type(pL, -1) == LUA_TNIL) {
			lua_pop(pL, 1);
			/* oh shit we don't have a names table assigned shit uhh let's make it real quick... */
			mtStructPushNamesTable(pL, **optr);
			lua_setiuservalue(pL, 1, 1);
			lua_getiuservalue(pL, 1, 1);
			if (lua_type(pL, -1) == LUA_TNIL) {
				/* UHHHHHHHHHHHHHHHHHHHHHHH */
				return luaL_error(pL, "Unable to generate a varnames table (#LEN).");
			}
		}
		
		auto len{ findRealArrayLength(pL, true) };
		lua_pop(pL, 1); // pop names table.

		lua_pushinteger(pL, len);
	}

	return 1;
}

int LMS::Lua::mtStructEq(lua_State* pL) {
	auto rptr1{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };
	auto rptr2{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 2, "__LMS_metatable_RValue_Struct__")) };
	lua_pushboolean(pL, (*rptr1) && (*rptr2) && (((*rptr1)->kind) == ((*rptr2)->kind) && ((*rptr1)->pObj) == ((*rptr2)->pObj)));
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
		return luaL_error(pL, "unknown builtin var name %s", name.c_str());
	}

	if (it->second.second->arrayLength > 0) {
		// push array accessor...
		pushRBuiltinAccessor(pL, reinterpret_cast<CInstance*>(curSelf), name);
	}
	else {
		// get the thing
		RValue v{ nullptr };
		if (!it->second.first->f_getroutine(reinterpret_cast<CInstance*>(curSelf), ARRAY_INDEX_NO_INDEX, &v)) {
			return luaL_error(pL, "Failed to obtain builtin variable %s", name.c_str());
		}

		rvalueToLua(pL, v);
	}

	return 1;
}

int LMS::Lua::mtBuiltinNewindex(lua_State* pL) {
	// ignored
	auto __tb{ luaL_checkudata(pL, 1, "__LMS_metatable_RValue_TBuiltin__") };

	if (lua_type(pL, 2) == LUA_TNIL) {
		return 0;
	}

	auto name{ std::string(lua_tostring(pL, 2)) };
	auto theval{ luaToRValue(pL, 3) };
	auto it{ builtinsMap.find(name) };
	if (it == builtinsMap.end()) {
		return luaL_error(pL, "newindex unknown builtin var name %s", name.c_str());
	}
	
	if (!it->second.first->f_canset || !it->second.first->f_setroutine) {
		return luaL_error(pL, "builtin variable %s is READ ONLY.", name.c_str());
	}

	if (!it->second.first->f_setroutine(reinterpret_cast<CInstance*>(curSelf), ARRAY_INDEX_NO_INDEX, &theval)) {
		return luaL_error(pL, "failed to assign to builtin variable %s", name.c_str());
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
	int isn{};
	auto ind{ static_cast<int>(lua_tonumberx(pL, 2, &isn) - 1.0) };
	if (!isn) return luaL_argerror(pL, 2, "Builtin array var key can only be a number.");

	arrlen = rbptr->extra->arrayLength;
	if (rbptr->getlenroutine) {
		arrlen = rbptr->getlenroutine(pL);
	}

	if (ind < 0 || ind >= arrlen) {
		lua_pushnil(pL);
		return 1;
	}
	
	RValue res{ nullptr };
	if (!rbptr->var->f_getroutine(rbptr->owner, ind, &res)) {
		return luaL_error(pL, "Failed to index builtin variable %s at index %d", rbptr->var->f_name, static_cast<int>(ind + 1));
	}

	rvalueToLua(pL, res);
	return 1;
}

int LMS::Lua::mtRBuiltinNewindex(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

	luaL_argcheck(pL, lua_type(pL, 2) != LUA_TNIL, 2, "new index cannot be nil.");

	int isn{};
	auto ind{ static_cast<int>(lua_tonumberx(pL, 2, &isn) - 1.0) };
	if (!isn) return luaL_argerror(pL, 2, "Builtin array var key can only be a number.");
	auto theval{ luaToRValue(pL, 3) };

	if (!rbptr->var->f_setroutine(rbptr->owner, ind, &theval)) {
		return luaL_error(pL, "Failed to set builtin variable %s at index %d", rbptr->var->f_name, static_cast<int>(ind + 1));
	}

	return 0;
}

int LMS::Lua::mtRBuiltinLen(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

	RValue res{ 0.0 }; // initialize to real

	if (rbptr->getlenroutine) {
		res = rbptr->getlenroutine(pL);
	}
	else {
		res = static_cast<double>(rbptr->extra->arrayLength);
	}

	rvalueToLua(pL, res);
	return 1;
}

int LMS::Lua::mtRBuiltinNext(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

	lua_Integer arrayind{ -1 };
	RValue arraylen{ 0.0 }; // initialize to real

	if (rbptr->getlenroutine) {
		arraylen = rbptr->getlenroutine(pL);
	}
	else {
		arraylen = static_cast<double>(rbptr->extra->arrayLength);
	}

	if (lua_gettop(pL) > 1 && lua_type(pL, 2) != LUA_TNIL) {
		int isn{};
		arrayind = static_cast<lua_Integer>(lua_tonumberx(pL, 2, &isn) - 1.0);
		if (!isn) return luaL_argerror(pL, 2, "Builtin var array key can only be a number.");
	}

	// advance to next array element.
	++arrayind;

	if (arrayind < 0 || arrayind >= static_cast<lua_Integer>(arraylen)) {
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

	luaL_getmetafield(pL, 1, "__next");
	lua_pushvalue(pL, 1);
	lua_pushnil(pL);
	return 3;
}

int LMS::Lua::mtRBuiltinIpairs(lua_State* pL) {
	auto rbptr{ reinterpret_cast<RBuiltinData*>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_RBuiltin__")) };

	luaL_getmetafield(pL, 1, "__next");
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
	auto optr{ reinterpret_cast<RValue**>(luaL_checkudata(pL, 1, "__LMS_metatable_RValue_Struct__")) };

	if ((!(*optr)) || (!((*optr)->pObj))) {
		return 0;
	}

	std::unique_ptr<RValue[]> args{  };
	std::unique_ptr<RValue*[]> yyc_args{  };
	int argc{ lua_gettop(pL) - 1 };
	if (argc > 0) {
		args = std::make_unique<RValue[]>(argc);
		yyc_args = std::make_unique<RValue* []>(argc);
		for (int i{ 0 }; i < argc; ++i) {
			args[i] = luaToRValue(pL, i + 2);
			yyc_args[i] = &args[i];
		}
	}

	RValue res{ nullptr };

	try {
		res = YYGML_CallMethod(curSelf, curOther, res, argc, **optr, yyc_args.get());
	}
	catch (const YYGMLException& e) {
		if (LMS_ExceptionHandler) {
			LMS_ExceptionHandler(e.GetExceptionObject());
		}
		else {
			throw;
		}
	}

	/* an argument might be inside those args so we convert first, then free YYC args. */
	rvalueToLua(pL, res);

	return 1;
}

int LMS::Lua::apiToInstance(lua_State* pL) {
	if (lua_gettop(pL) < 1) return luaL_error(pL, "toInstance called with no arguments, expected at least one.");

	if (lua_type(pL, 1) == LUA_TUSERDATA) {
		// already a userdata, duplicate the argument and return
		lua_pushvalue(pL, 1);
	}
	else {
		int isn{};
		auto num{ lua_tonumberx(pL, 1, &isn) };
		if (!isn) return luaL_argerror(pL, 2, "Argument cannot be converted to a number.");

		// return nil for noone (a legal no-op)
		if (num == /*noone*/ -4.0) {
			lua_pushnil(pL);
		}
		else {
			RValue inst{ rcall(F_JSGetInstance, curSelf, curOther, num) };
			rvalueToLua(pL, inst);
		}
	}

	return 1;
}

int LMS::Lua::apiCreateAsyncEvent(lua_State* pL) {
	int isn1{}, isn2{};
	auto dsmap{ lua_tonumberx(pL, 1, &isn1) };
	auto evsubtype{ lua_tonumberx(pL, 2, &isn2) };

	if (!isn1) return luaL_argerror(pL, 1, "Argument1 cannot be converted to a number.");
	if (!isn2) return luaL_argerror(pL, 2, "Argument2 cannot be converted to a number.");

	if (lua_gettop(pL) < 3 || lua_type(pL, 3) == LUA_TNIL) {
		Create_Async_Event(static_cast<int>(dsmap), static_cast<int>(evsubtype));
	}
	else {
		int isn3{};
		auto bufindex{ lua_tonumberx(pL, 3, &isn3) };
		if (!isn3) return luaL_argerror(pL, 3, "Argument3 cannot be converted to a number.");
		CreateAsynEventWithDSMapAndBuffer(static_cast<int>(dsmap), static_cast<int>(bufindex), static_cast<int>(evsubtype));
	}

	return 0;
}

int LMS::Lua::apiSignalScriptAction(lua_State* pL) {
	int isn{};
	auto tmp{ static_cast<HookBitFlags>(static_cast<lua_Integer>(lua_tonumberx(pL, 1, &isn))) };
	if (!isn) return luaL_argerror(pL, 1, "Argument1 cannot be converted to a number.");
	tmpFlags = tmp;
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
		RValue scrname{ rcall(F_ScriptGetName, curSelf, curOther, lua_tonumber(pL, 1)) };
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
				return luaL_error(pL, "MinHook script fail, %s", MH_StatusToString(ok));
			}
			ok = MH_EnableHook(fun);
			if (ok != MH_STATUS::MH_OK) {
				return luaL_error(pL, "MinHook enable script fail, %s", MH_StatusToString(ok));
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
			std::cerr << "Live reload failed for file " << aname << ", stacktrace:" << std::endl;
			stackPrinter(luactx);
			lua_pop(luactx, diff);
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

int LMS::Lua::exceptionHandler(const RValue& e) {
	RValue e_message{}, e_longMessage{}, e_script{}, e_line{}, e_stacktrace{};
	e.pObj->m_getOwnProperty(e.pObj, &e_message, "message");
	e.pObj->m_getOwnProperty(e.pObj, &e_longMessage, "longMessage");
	e.pObj->m_getOwnProperty(e.pObj, &e_script, "script");
	e.pObj->m_getOwnProperty(e.pObj, &e_line, "line");
	e.pObj->m_getOwnProperty(e.pObj, &e_stacktrace, "stacktrace");

	lua_newtable(luactx);
	auto exctab{ lua_gettop(luactx) };

	lua_pushstring(luactx, "is_lms_exception");
	lua_pushboolean(luactx, true);
	lua_settable(luactx, exctab);

	lua_pushstring(luactx, "e_class");
	lua_pushstring(luactx, e.pObj->m_class);
	lua_settable(luactx, exctab);

	lua_pushstring(luactx, "e_message");
	lua_pushstring(luactx, e_message.pString->get());
	lua_settable(luactx, exctab);

	lua_pushstring(luactx, "e_longMessage");
	lua_pushstring(luactx, e_longMessage.pString->get());
	lua_settable(luactx, exctab);

	lua_pushstring(luactx, "e_script");
	lua_pushstring(luactx, e_script.pString->get());
	lua_settable(luactx, exctab);

	lua_pushstring(luactx, "e_line");
	lua_pushnumber(luactx, static_cast<double>(e_line));
	lua_settable(luactx, exctab);

	lua_pushstring(luactx, "e_stacktrace");
	lua_newtable(luactx);
	auto stacktab{ lua_gettop(luactx) };
	if ((( e_stacktrace.kind & MASK_KIND_RVALUE) == VALUE_ARRAY)
		&& e_stacktrace.pArray
		&& e_stacktrace.pArray->pArray
		&& e_stacktrace.pArray->length > 0) {

		auto thelen{ e_stacktrace.pArray->length };
		for (auto i{ 0 }; i < thelen; ++i) {
			const auto& item{ e_stacktrace.pArray->pArray[i] };
			lua_pushinteger(luactx, 1 + i);
			if ((item.kind & MASK_KIND_RVALUE) == VALUE_STRING) {
				lua_pushstring(luactx, item.pString->get());
			}
			else {
				lua_pushnumber(luactx, static_cast<double>(item));
			}
			lua_settable(luactx, stacktab);
		}
	}
	lua_settable(luactx, exctab);

	/* the return value is ignored but we better return for Lua error magic to work. */
	return lua_error(luactx);
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

int LMS::Lua::apiNext(lua_State* pL) {
	auto argc{ lua_gettop(pL) }; /* will be passed to next() */

	/* push the needed next() function at the top of the stack: */
	if (luaL_getmetafield(pL, 1, "__next") == LUA_TNIL) { /* no metatable or __next does not exist? */
		//lua_pop(pL, 1); /* pop unneeded nil value, try the original next() */
		lua_getglobal(pL, "LMS");
		lua_pushstring(pL, "Garbage");
		lua_gettable(pL, -2);
		lua_pushstring(pL, "OriginalNext");
		lua_gettable(pL, -2);
		/* the original next() is at the stack top, get rid of table stuff below: */
		lua_remove(pL, lua_gettop(pL) - 1); // pop garbage
		lua_remove(pL, lua_gettop(pL) - 1); // pop LMS
	}
	
	/* duplicate arguments for a function call: */
	for (int start{ 1 }; start <= argc; ++start) {
		lua_pushvalue(pL, start);
	}

	/* next() here is always called with two arguments, and return values are just passed through. */
	lua_call(pL, argc, LUA_MULTRET);
	auto after{ lua_gettop(pL) };
	return after - argc;
}

int LMS::Lua::apiCatchGmlExceptions(lua_State* pL) {
	auto oldstate{ LMS_ExceptionHandler != nullptr };

	if (lua_gettop(pL) > 0 && lua_type(pL, 1) != LUA_TNIL && (lua_toboolean(pL, 1) || lua_tonumber(pL, 1) > 0.5 || lua_tointeger(pL, 1) > 0)) {
		LMS_ExceptionHandler = &exceptionHandler;
	}
	else {
		LMS_ExceptionHandler = nullptr;
	}

	lua_pushboolean(pL, oldstate);
	return 1;
}

RValue LMS::Lua::luaToRValue(lua_State* pL, int index) {
	arraySetOwner();
	switch (lua_type(pL, index)) {
		case LUA_TNONE:
		default: {
			luaL_error(pL, "RValue is unset, stack index=%d", index);
			return RValue{}; // unset.
		}

		case LUA_TNIL: {
			return RValue{ nullptr }; // GML undefined.
		}

		case LUA_TBOOLEAN: {
			return RValue{ static_cast<bool>(lua_toboolean(pL, index) ? true : false) };
		}

		case LUA_TLIGHTUSERDATA: {
			return RValue{ lua_topointer(pL, index) };
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
			auto sptr{ reinterpret_cast<RValue**>(luaL_testudata(pL, index, "__LMS_metatable_RValue_Struct__")) };
			auto rptr{ reinterpret_cast<RBuiltinData*>(luaL_testudata(pL, index, "__LMS_metatable_RValue_RBuiltin__")) };
			if (sptr) {
				rv = **sptr;
			}
			else if (rptr) {
				luaL_getmetafield(pL, index, "__len");
				lua_pushvalue(pL, index);
				lua_call(pL, 1, 1);
				auto weirdlen{ luaToRValue(pL, lua_gettop(pL)) };
				lua_pop(pL, 1); // pop length
				
				rv = rcall(F_ArrayCreate, curSelf, curOther, weirdlen);
				
				auto weirdlendbl{ static_cast<double>(weirdlen) };
				for (auto i{ 0.0 }; i < weirdlendbl; ++i) {
					luaL_getmetafield(pL, index, "__index");
					lua_pushvalue(pL, index);
					lua_pushnumber(pL, 1.0 + i);
					lua_call(pL, 2, 1);

					rcall(F_ArraySet, curSelf, curOther, rv, i, luaToRValue(pL, lua_gettop(pL)));
					lua_pop(pL, 1);
				}
			}
			else {
				auto errname{ "<unknown>" };
				if (luaL_getmetafield(pL, index, "__name") != LUA_TNIL) errname = lua_tostring(pL, -1);
				luaL_error(pL, "Unimplemented userdata conversion. __name=%s", errname);
			}

			return rv;
		}

		case LUA_TTABLE: {
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
				RValue ret{ YYObjectBase_Alloc(0, VALUE_UNSET, YYObjectBaseKind::KIND_YYOBJECTBASE, false) };
				JS_GenericObjectConstructor(ret, curSelf, curOther, 0, nullptr);
				ret.pObj->m_class = "___struct___anon_modshovel";
				// set our class name to an anonymous modshovel struct.

				/* iterate again */
				for (lua_pushnil(pL); lua_next(pL, index) != 0; lua_pop(pL, 1)) {
					auto structkey{ RValue{ lua_tostring(pL, -2) } };
					rcall(F_VariableStructSet, curSelf, curOther, ret, structkey, luaToRValue(pL, lua_gettop(pL)));
				}

				return ret;
			}
			else {
				/* make an array */
				RValue arr{ rcall(F_ArrayCreate, curSelf, curOther, 0.0) };

				/* iterate again */
				for (lua_pushnil(pL); lua_next(pL, index) != 0; lua_pop(pL, 1)) {
					// decrement key.
					auto arrind{ lua_tonumber(pL, -2) - 1.0 };
					rcall(F_ArraySet, curSelf, curOther, arr, arrind, luaToRValue(pL, lua_gettop(pL)));
				}

				return arr;
			}
		}
	}
}

void LMS::Lua::rvalueToLua(lua_State* pL, RValue& rv) {
	arraySetOwner();
	switch (rv.kind & MASK_KIND_RVALUE) {
		case VALUE_UNSET: {
			luaL_error(pL, "RValue type is UNSET.");
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
			lua_pushstring(pL, rv.pString->get());
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

		case VALUE_ARRAY:
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
		std::invoke(Insert_Event, pobj->m_eventsMap, evsubtype | (evtype << 32ull), cptr);
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
	if (evtype != 4 /* collision event */) {
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
	std::unique_ptr<RValue[]> args{  };
	int argc{ lua_gettop(pL) };
	if (argc > 0) {
		args = std::make_unique<RValue[]>(argc);
		for (int i{ 0 }; i < argc; ++i) {
			args[i] = luaToRValue(pL, i + 1);
		}
	}
	
	RValue rcallres{ rdcall(reinterpret_cast<RFunction*>(const_cast<void*>(lua_topointer(pL, lua_upvalueindex(1)))), curSelf, curOther, argc, args.get()) };
	rvalueToLua(pL, rcallres);

	return 1;
}

void LMS::Lua::initMetamethods(lua_State* pL) {
	/* LMS is at the stacktop */

	/* YYObjectBase or CInstance */
	const luaL_Reg structmt[]{
		{"__index", &mtStructIndex},
		{"__newindex", &mtStructNewindex},
		{"__gc", &mtStructGc},
		{"__tostring", &mtStructTostring},
		{"__len", &mtStructLen},
		{"__eq", &mtStructEq},
		{"__call", &mtStructCall},
		{"__pairs", &mtStructPairs},
		{"__ipairs", &mtStructIpairs},
		{"__len", &mtStructLen},
		{"__next", &mtStructNext},
		{"__close", &mtStructGc},
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
		//{"__close", &mtStructGc}
		{nullptr, nullptr}
	};

	luaL_newmetatable(pL, "__LMS_metatable_RValue_RBuiltin__");
	luaL_setfuncs(pL, rbuiltinmt, 0);
	lua_pop(pL, 1);

	/* One way method call wrapper is now obsolete, use the Struct instead... */
	/* Array userdata is now obsolete, struct does the same thing... */
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
			F_ArrayCreate = func;
		}
		else if (strcmp(realname, "array_get") == 0) {
			F_ArrayGet = func;
		}
		else if (strcmp(realname, "array_set") == 0) {
			F_ArraySet = func;
		}
		else if (strcmp(realname, "array_length") == 0) {
			F_ArrayLength = func;
		}
		else if (strcmp(realname, "object_get_parent") == 0) {
			deriveObjectHash(reinterpret_cast<std::byte*>(func->f_routine));
		}
		else if (strcmp(realname, "typeof") == 0) {
			deriveYYCreateString(reinterpret_cast<std::byte*>(func->f_routine));
		}
		else if (strcmp(realname, "variable_struct_set_pre") == 0) {
			deriveCopyRValuePre(reinterpret_cast<std::byte*>(func->f_routine));
		}
		else if (strcmp(realname, "object_get_name") == 0) {
			F_ObjectGetName = func;
		}
		else if (strcmp(realname, "sprite_get_name") == 0) {
			F_SpriteGetName = func;
		}
		else if (strcmp(realname, "room_get_name") == 0) {
			F_RoomGetName = func;
		}
		else if (strcmp(realname, "font_get_name") == 0) {
			F_FontGetName = func;
		}
		else if (strcmp(realname, "audio_get_name") == 0) {
			F_AudioGetName = func;
		}
		else if (strcmp(realname, "path_get_name") == 0) {
			F_PathGetName = func;
		}
		else if (strcmp(realname, "timeline_get_name") == 0) {
			F_TimelineGetName = func;
		}
		else if (strcmp(realname, "tileset_get_name") == 0) {
			F_TilesetGetName = func;
		}
		else if (strcmp(realname, "script_get_name") == 0) {
			F_ScriptGetName = func;
		}
		else if (strcmp(realname, "variable_struct_get") == 0) {
			F_VariableStructGet = func;
		}
		else if (strcmp(realname, "variable_struct_set") == 0) {
			F_VariableStructSet = func;
		}
		else if (strcmp(realname, "shader_get_name") == 0) {
			F_ShaderGetName = func;
		}
		else if (strcmp(realname, "@@array_set_owner@@") == 0) {
			F_JSArraySetOwner = func;
		}
		else if (strcmp(realname, "@@Global@@") == 0) {
			/* obtain global YYObjectBase */
			g_pGlobal = rcall(func, curSelf, curOther).pObj;
		}
		else if (strcmp(realname, "@@GetInstance@@") == 0) {
			F_JSGetInstance = func;
		}
		else if (strcmp(realname, "string") == 0) {
			F_String = func;
		}
		else if (strcmp(realname, "ds_map_create") == 0) {
			F_DsMapCreate = func;
		}
		else if (strcmp(realname, "variable_global_set") == 0) {
			F_VariableGlobalSet = func;
		}
		else if (strcmp(realname, "ds_map_replace") == 0) {
			F_DsMapReplace = func;
		}
		else if (strcmp(realname, "ds_map_delete") == 0) {
			F_DsMapDelete = func;
		}
		else if (strcmp(realname, "ds_map_find_value") == 0) {
			F_DsMapFindValue = func;
		}
		else if (strcmp(realname, "ds_exists") == 0) {
			F_DsExists = func;
		}
		else if (strcmp(realname, "variable_struct_get_names") == 0) {
			F_VariableStructGetNames = func;
		}
	}
	/* initRuntime end */

	/* create a ds map for pinnable objects. */
	RValue dsm{ rcall(F_DsMapCreate, curSelf, curOther) };
	rcall(F_VariableGlobalSet, curSelf, curOther, RValue{ "__libmodshovel_gc_ds_map_index_please_do_not_destroy__" }, dsm);
	pinDsMap = static_cast<double>(dsm);

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
		else if (strcmp(me.f_name, "phy_collision_points") == 0) {
			GV_PhyColPoints = me.f_getroutine;
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

double LMS::Lua::getInstanceLen(lua_State* pL) {
	RValue tst{ 0.0 };
	if (!GV_InstanceCount(curSelf, ARRAY_INDEX_NO_INDEX, &tst)) {
		luaL_error(pL, "Failed to get instance_count variable.");
	}

	if ((tst.kind & MASK_KIND_RVALUE) == VALUE_UNDEFINED)
		return 0.0;
	else
		return static_cast<double>(tst);
};

double LMS::Lua::getPhyColPoints(lua_State* pL) {
	RValue tst{ 0.0 };
	if (!GV_PhyColPoints(curSelf, ARRAY_INDEX_NO_INDEX, &tst)) {
		luaL_error(pL, "Failed to get phy_col_points variable.");
	}

	if ((tst.kind & MASK_KIND_RVALUE) == VALUE_UNDEFINED)
		return 0.0;
	else
		return static_cast<double>(tst);
}

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

	lua_call(pL, 3, LUA_MULTRET);
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
	auto myind{ -1 };
	auto needtoadd{ false };
	for (auto i{ 0 }; true; ++i) {
		if (!ScapegoatMethods[i].f) {
			luaL_error(pL, "Reached the end of the scapegoat method table.");
		}

		if (ScapegoatMethods[i].k == funchash || ScapegoatMethods[i].k == 0) {
			myind = i;
			needtoadd = ScapegoatMethods[myind].k == 0;
			break;
		}
	}

	if (myind == -1) {
		luaL_error(pL, "What the heck? myind=-1 in luaToMethod?");
	}

	if (needtoadd) {
		ScapegoatMethods[myind].k = funchash;
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
	}

	YYSetScriptRef(&obj, ScapegoatMethods[myind].f, reinterpret_cast<YYObjectBase*>(curSelf));
	CScriptRefVTable::Obtain(reinterpret_cast<CScriptRef*>(obj.pObj));
	CScriptRefVTable::Replace(reinterpret_cast<CScriptRef*>(obj.pObj));
	/* after we've set our VTable, we can set our custom tag. */
	reinterpret_cast<CScriptRef*>(obj.pObj)->m_tag = reinterpret_cast<char*>(static_cast<std::uintptr_t>(myind));
	/* needed to trigger GC of an object ASAP */
	pinYYObjectBase(obj.pObj);
	unpinYYObjectBase(obj.pObj);
	/* we're done here. */
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
			key = RValue{ reinterpret_cast<YYObjectBase*>(curSelf) };
		}
		else if (rvnum == -2.0) {
			// other
			key = RValue{ reinterpret_cast<YYObjectBase*>(curOther) };
		}
		// -3 is `all`, handled below, just ignores the object index.
		else if (rvnum == -4.0) {
			// a with() with noone is a legit no-op in GML.
			// just return without executing anything...
			return 0;
		}
		else if (rvnum == -5.0) {
			// `global`, yes, I permit a `global` with(), at your own risk of course :p
			key = RValue{ g_pGlobal };
		}
		else if (rvnum >= 100000.0) {
			// typical instance id, gladly @@GetInstance@@ will get the YYobjectbase that belongs to that ID.
			rcall(F_JSGetInstance, curSelf, curOther, rvnum);
		}
		else if (rvnum < -5.0) {
			luaL_argerror(pL, 1, "Invalid instance id or number passed to function.");
		}
	}

	if ((key.kind & MASK_KIND_RVALUE) == VALUE_OBJECT) {
		directWith(pL, key, 1.0);
	}
	else {
		// need to do a loop.
		for (double i{ 0.0 }, cnt{ 1.0 }; i < getInstanceLen(pL); ++i) {
			RValue myinstid{ -4.0 /* noone */ };
			if (!GV_InstanceId(curSelf, static_cast<int>(i), &myinstid) || static_cast<double>(myinstid) == -4.0) {
				return luaL_error(pL, "Unable to fetch instance id at index %d", static_cast<int>(i));
			}

			myinstid = rcall(F_JSGetInstance, curSelf, curOther, myinstid);
			auto cinstptr{ reinterpret_cast<CInstance*>(myinstid.pObj) };
			if (!cinstptr) {
				return luaL_error(pL, "in with() CInstance pointer is null.");
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
	lua_pushstring(pL, "catchGMLExceptions");
	lua_pushcfunction(pL, &apiCatchGmlExceptions);
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

	/* for next */
	lua_pushstring(pL, "OriginalNext");
	lua_getglobal(pL, "next");
	lua_settable(pL, -3);

	/* and now rewire the original next. */
	lua_pushcfunction(pL, &apiNext);
	lua_setglobal(pL, "next");

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
	const char* nn{ "<undefined>" };
	for (lua_Number i{ 100001 }; true; ++i) {
		RValue name{ rcall(F_ScriptGetName, curSelf, curOther, i) };

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
		Global::throwError("Failed to allocate memory for Lua! Try restarting your computer.");
		return;
	}
	/* allocated memory for lua? print copyright stuff then */
	std::cout << LUA_COPYRIGHT << std::endl << LUA_AUTHORS << std::endl;
	/* continue initialization: */
	lua_atpanic(luactx, &atPanicLua);
	luaL_openlibs(luactx);
	/* push LMS table */
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
	auto mbefore{ lua_gettop(luactx) };
	luaL_loadfile(luactx, "main.lua");
	lua_call(luactx, 0, LUA_MULTRET);
	auto mafter{ lua_gettop(luactx) };
	auto mdiff{ mafter - mbefore };
	if (mdiff != 0) {
		/* handle main.lua returns, nothing for now */
		lua_pop(luactx, mdiff);
	}
}
