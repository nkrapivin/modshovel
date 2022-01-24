#pragma once
#include "LibModShovel_GameMaker.h"
#include "LibModShovel_Lua.h"

namespace LMS {
	struct MethodScapegoat {
		PFUNC_YYGMLScript_Internal f;
		long long k;
	};

	extern MethodScapegoat ScapegoatMethods[];
}

#ifdef MethodAutogen_Implementation
#define MAI_Stringify(x) #x
#define MAI_ToString(x) MAI_Stringify(x)
#define MAI_Joiner(x, y) x ## y
#define MAI_Join(x, y) MAI_Joiner(x, y)
#define fdef(ind) \
	{ ([](CInstance* selfinst, CInstance* otherinst, RValue& Result, int argc, RValue* args[])->RValue& \
	{ return LMS::Lua::MethodCallRoutine(selfinst, otherinst, Result, argc, args, (ind)); } ), (0LL) }

#define END() { (nullptr), (0LL) }
#endif /* MethodAutogen_Implementation */
