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
#define fdef(ind) \
	{ ([](CInstance* selfinst, CInstance* otherinst, RValue& Result, int argc, RValue* args[])->RValue& \
	{ return LMS::Lua::MethodCallRoutine(selfinst, otherinst, Result, argc, args, (ind)); } ), (0LL) }

#define END() { (nullptr), (0LL) }
#endif /* MethodAutogen_Implementation */
