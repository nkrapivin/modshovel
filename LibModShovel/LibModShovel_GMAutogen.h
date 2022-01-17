#pragma once
#include "LibModShovel_GameMaker.h"
#include "LibModShovel_Lua.h"


namespace LMS {
	struct ScapegoatPair {
		PFUNC_YYGMLScript_Internal f; // func
		PFUNC_YYGMLScript_Internal t; // trampoline
		const char* n; // name
	};

	extern ScapegoatPair ScapegoatScripts[];
}

#ifdef GMAutogen_Implementation
#define fdef(ind) \
	{ ([](CInstance* selfinst, CInstance* otherinst, RValue& Result, int argc, RValue* args[])->RValue& \
	{ return LMS::Lua::HookScriptRoutine(selfinst, otherinst, Result, argc, args, (ind)); } ), (nullptr), (nullptr) }

#define END() { (nullptr), (nullptr), (nullptr) }
#endif /* GMAutogen_Implementation */
