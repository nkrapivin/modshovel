#pragma once
#include "pch.h"
#include "LibModShovel.h"
#include "minhook/include/MinHook.h"
#include "LibModShovel_GameMaker.h"
#include <vector>
#include <Psapi.h>

/*
* LibModShovel by nkrapivindev
* Defines the Hooks class used for fun(tm) memory stuff.
*/

namespace LMS {
	class Hooks {
	private:
		struct initialDllData {
		public:
			const char* version;
			/* reserved */
			std::uintptr_t padding[3];
		};

		static initialDllData initData;

		using initialDllProc = std::uintptr_t(*)(initialDllData* initData);


		static bool appliedHooks;
		static HMODULE base;
		static HANDLE curproc;
		static MODULEINFO modinfo;
		static PFUNC_InitYYC trampoline;
		static PFUNC_StartRoom sroom_trampoline;

		static bool ensureMinhookCall(MH_STATUS mh);
		static bool predSearch(unsigned char a, unsigned char b);
		static LPBYTE fastCodeSearch(const std::vector<unsigned char>& contents);
		static void initLLVM(SLLVMVars* pVars);
		static void startRoom(int numb, bool starting);

	public:
		
		static bool ApplyInitHooks();
	};
}
