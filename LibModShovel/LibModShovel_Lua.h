#pragma once

#include "pch.h"
#include "LibModShovel.h"
#include "LibModShovel_Hooks.h"
#include "LibModShovel_GameMaker.h"
#include <exception>
#include <stdexcept>

#include "LibModShovel_GMConstants.h"
#include "LibModShovel_GMBuiltins.h"
#include <unordered_map>
#include <string>
#include <thread>
#include <mutex>

#include "lua-5.4.3/src/lua.h"
#include "lua-5.4.3/src/lualib.h"
#include "lua-5.4.3/src/lauxlib.h"

/*
* LibModShovel by nkrapivindev
* Header file of all things Lua
*/

namespace LMS {
	enum HookBitFlags : unsigned long long {
		SKIP_NONE = (0 << 0),
		SKIP_NEXT = (1 << 0),
		SKIP_ORIG = (1 << 1),
		SKIP_AFTER = (1 << 2),
		SKIP_TO = (1 << 3)
	};

	class Lua {
	private:
		
		static HookBitFlags tmpFlags;

		static lua_State* luactx;
		static CInstance* curSelf;
		static CInstance* curOther;

		static std::unordered_map<std::uintptr_t, OVERLAPPED*> watcherMap;

		static VOID WINAPI ovCompletionRoutine(
			_In_    DWORD dwErrorCode,
			_In_    DWORD dwNumberOfBytesTransfered,
			_Inout_ LPOVERLAPPED lpOverlapped);

		static double pinDsMap;
		static void pinYYObjectBase(YYObjectBase* pObj);
		static void unpinYYObjectBase(YYObjectBase* pObj);

		static void arraySetOwner();
		static double getInstanceLen();
		static lua_Integer findRealArrayLength(lua_State* pL);
		static bool directWith(lua_State* pL, RValue& newself, double ind);
		static RValue luaToMethod(lua_State* pL, int funcind);

		static std::wstring stringToWstring(const std::string& str);
		static std::string wstringToString(const std::wstring& str);
		static std::string getProgramDirectory();

		static void doScriptHookCall(bool& callorig, bool& callafter, const std::string& prefix, const std::string& stacktracename, RValue& Result, RValue*& newargarr, RValue**& newargs, int& newargc, RValue**& args);
		static void doEventHookCall(bool& callorig, bool& callafter, const std::string& prefix, const std::string& stacktracename);

		static void pushYYObjectBase(lua_State* pL, YYObjectBase* thing);
		static void pushYYObjectBase(lua_State* pL, const RValue& rv);
		static void pushYYCScriptArgs(lua_State* pL, int argc, RValue* args[]);
		static void pushRBuiltinAccessor(lua_State* pL, CInstance* owner, const std::string& name);
		static std::string getEventName(int evtype);


		static void hookRoutineEvent(CInstance* selfinst, CInstance* otherinst);

		static std::unordered_map<unsigned long long, PFUNC_YYGML> eventOriginalsMap;
		static std::unordered_map<std::string, std::pair<RVariableRoutine*, GMBuiltinVariable*>> builtinsMap;

		//static int hookRoutineMethod(lua_State* pL);

		static int scriptCall(lua_State* pL);

		/* metamethods */

		/* RArray */
		static int mtArrayLen(lua_State* pL);
		static int mtArrayIndex(lua_State* pL);
		static int mtArrayNewindex(lua_State* pL);
		static int mtArrayGc(lua_State* pL);
		static int mtArrayTostring(lua_State* pL);
		static int mtArrayNext(lua_State* pL);
		static int mtArrayPairs(lua_State* pL);
		static int mtArrayIpairs(lua_State* pL);
		static int mtArrayEq(lua_State* pL);

		/* YYObjectBase or CInstance */
		static int mtStructIndex(lua_State* pL);
		static int mtStructNewindex(lua_State* pL);
		static int mtStructGc(lua_State* pL);
		static int mtStructTostring(lua_State* pL);
		static int mtStructLen(lua_State* pL);
		static int mtStructEq(lua_State* pL);

		/* LMS.Builtins */
		static int mtBuiltinIndex(lua_State* pL);
		static int mtBuiltinNewindex(lua_State* pL);

		/* Builtin array variable accessor */
		static int mtRBuiltinIndex(lua_State* pL);
		static int mtRBuiltinNewindex(lua_State* pL);
		static int mtRBuiltinLen(lua_State* pL);
		static int mtRBuiltinNext(lua_State* pL);
		static int mtRBuiltinPairs(lua_State* pL);
		static int mtRBuiltinIpairs(lua_State* pL);
		static int mtRBuiltinEq(lua_State* pL);

		/* One-way method wrapper */
		static int mtOneWayMethodCall(lua_State* pL);
		static int mtOneWayMethodGc(lua_State* pL);
		static int mtOneWayMethodTostring(lua_State* pL);
		static int mtOneWayMethodEq(lua_State* pL);

		/* API */
		static int apiHookEvent(lua_State* pL);
		static int apiDebugEnterRepl(lua_State* pL);
		static int apiWith(lua_State* pL);
		static int apiToInstance(lua_State* pL);
		static int apiCreateAsyncEvent(lua_State* pL);
		static int apiSignalScriptAction(lua_State* pL);
		static int apiSetHookFunction(lua_State* pL);
		static int apiHookScript(lua_State* pL);
		static int apiSetFileWatchFunction(lua_State* pL);
		static int apiFileWatch(lua_State* pL);
		static int apiSetConsoleShow(lua_State* pL);

		static RValue luaToRValue(lua_State* pL, int index);
		static void rvalueToLua(lua_State* pL, RValue& rv);
		static void initMetamethods(lua_State* pL);
		static void initRuntime(lua_State* pL);
		static void initBuiltin(lua_State* pL);
		static void initConstants(lua_State* pL);
		static void initGarbage(lua_State* pL);
		static void initScripts(lua_State* pL);
		static void initVersion(lua_State* pL);
		static void initGlobal(lua_State* pL);
		static void initApi(lua_State* pL);
		static int luaRuntimeCall(lua_State* pL);
	public:
		static void Init();
		static RValue& HookScriptRoutine(CInstance* selfinst, CInstance* otherinst, RValue& Result, int argc, RValue* args[], unsigned long long index);
		static RValue& MethodCallRoutine(CInstance* selfinst, CInstance* otherinst, RValue& Result, int argc, RValue* args[], unsigned long long index);
		static void FreeMethodAt(long long index);
	};
}
