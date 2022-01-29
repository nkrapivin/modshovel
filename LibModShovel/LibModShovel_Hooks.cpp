#include "pch.h"
#include "LibModShovel.h"
#include "LibModShovel_Hooks.h"
#include "LibModShovel_GameMaker.h"
#include <algorithm>

/*
* LibModShovel by nkrapivindev
* Class:   LMS::Hooks
* Purpose: Memory search and hook stuff...
*/

constexpr unsigned char bany = '?';
bool LMS::Hooks::appliedHooks{};
HMODULE LMS::Hooks::base{};
HANDLE LMS::Hooks::curproc{};
MODULEINFO LMS::Hooks::modinfo{};
PFUNC_InitYYC LMS::Hooks::trampoline{};
PFUNC_StartRoom LMS::Hooks::sroom_trampoline{};

LMS::Hooks::initialDllData LMS::Hooks::initData{};
CHash<CObjectGM>* g_pObjectHash{};

bool LMS::Hooks::predSearch(unsigned char a, unsigned char b) {
	return b == a || b == bany;
}

/* a little search helper: */
LPBYTE LMS::Hooks::fastCodeSearch(const std::vector<unsigned char>& contents, const char* funcname) {
	DWORD themask{ PAGE_EXECUTE_READ };
	LPBYTE address_low{ reinterpret_cast<LPBYTE>(modinfo.lpBaseOfDll) };
	LPBYTE address_high{ address_low + modinfo.SizeOfImage };
	MEMORY_BASIC_INFORMATION mbi{};

	while (address_low < address_high && VirtualQuery(address_low, &mbi, sizeof(mbi))) {
		if ((mbi.State == MEM_COMMIT) && (mbi.Protect & themask) && !(mbi.Protect & PAGE_GUARD)) {
			LPBYTE mbeg{ reinterpret_cast<LPBYTE>(mbi.BaseAddress) };
			LPBYTE mend{ mbeg + mbi.RegionSize };
			LPBYTE mres{ std::search(mbeg, mend, contents.begin(), contents.end(), predSearch) };

			if (mres != mend) {
				return mres;
			}
		}

		address_low += mbi.RegionSize;
		ZeroMemory(&mbi, sizeof(mbi));
	}

	Global::throwError("Failed to find function " + std::string(funcname));
	return nullptr;
}

bool LMS::Hooks::ensureMinhookCall(MH_STATUS mh) {
	if (mh != MH_STATUS::MH_OK) {
		return Global::throwError(std::string("MinHook call failed, expected status MH_OK (0), got ") + MH_StatusToString(mh));
	}

	return true;
}

void LMS::Hooks::initLLVM(SLLVMVars* _pVars) {
	trampoline(_pVars);
	/* post llvm init: */
	g_pLLVMVars = _pVars;
	SYYStackTrace::s_ppStart = reinterpret_cast<SYYStackTrace**>(g_pLLVMVars->pYYStackTrace);

	/* chainload other DLLs: */
	HMODULE hTest{ LoadLibraryW(L"Initial.dll") };
	if (hTest != nullptr && hTest != INVALID_HANDLE_VALUE) {
		initialDllProc proc{ reinterpret_cast<initialDllProc>(GetProcAddress(hTest, "LMS_Second_DLL_Init")) };
		if (proc) {
			initData.version = __TIMESTAMP__;
			proc(&initData);
		}
	}
}

void LMS::Hooks::startRoom(int numb, bool starting) {
	/* prevent double execution just in case: */
	static bool justThisOnce{ false };

	/* start second stage initialization before first room events perform, ONCE: */
	if (!justThisOnce) {
		Global::initStage2();
		justThisOnce = true;
	}

	/* pass back to original: */
	sroom_trampoline(numb, starting);
}

bool LMS::Hooks::ApplyInitHooks() {
	if (appliedHooks) {
		return Global::throwError("Hooks were already applied!");
	}

	/* obtain info about loaded executable: */
	base = GetModuleHandleW(nullptr);
	curproc = GetCurrentProcess();
	GetModuleInformation(curproc, base, &modinfo, sizeof(modinfo));

	/* do the search: */
	LPBYTE pf_InitLLVM{ fastCodeSearch({
			// fetch first argument
			0x8b, 0x44, 0x24, 0x04,
			// set zero
			0xc7, 0x00, 0x00, 0x00, 0x00, 0x00,
			// mov globalvar
			0x8b, 0x0d, /*....*/
		}, "InitLLVM"),
	};

	LPBYTE pf_StartRoom{ fastCodeSearch({
			// push ebp
			0x55,
			// mov ebp,esp
			0x8b, 0xec,
			// push -0x1
			0x6a, 0xff,
			// push [address] (New_Room)
			0x68, bany, bany, bany, bany,
			// mov eax,fs:[0x0]
			0x64, 0xa1, 0x00, 0x00, 0x00, 0x00,
			// push eax
			0x50,
			// sub esp, 0x1c
			0x83, 0xec, 0x1c,
			// push ebx
			0x53,
			// push esi
			0x56,
			// push edi
			0x57,
			// mov eax,[address]
			0xa1, bany, bany, bany, bany,
			// xor eax,ebp
			0x33, 0xc5,
			// push eax
			0x50,
			// lea eax=>local_10,[ebp + -0xc]
			0x8d, 0x45, 0xf4,
			// mov fs:[0x0],eax
			0x64, 0xa3, 0x00, 0x00, 0x00, 0x00,
			// cmp byte ptr [ebp + param_2],0x0
			0x80, 0x7d, 0x0c, 0x00
		}, "StartRoom")
	};

	/* lulz */
	LPBYTE pf_Function_Add{ fastCodeSearch({
			// mov runtime function length
			0x8b, 0x15, /* runtime func len addr */bany, bany, bany, bany,
			// mov runtime function capacity
			0xa1,       /* runtime func capacity addr */bany, bany, bany, bany,
			// cmp length with capacity
			0x3b, 0xd0,
			// jl if cmp is false
			0x7c, 0x28, /* ... */
		}, "Function_Add")
	};

	LPBYTE pf_YYRealloc{ fastCodeSearch({
			// push 0 (false)
			0x6a, 0x00,
			// push linenumber
			0x68, bany, bany, bany, bany,
			// push filestring
			0x68, bany, bany, bany, bany,
			// push dword ptr [esp + _newsize]
			0xff, 0x74, 0x24, 0x14,
			// push dword ptr [esp + _p]
			0xff, 0x74, 0x24, 0x14,
			// call internal realloc with linenum and stuff
			0xe8, bany, bany, bany, bany,
			// add esp, 0x14
			0x83, 0xc4, 0x14,
			// ret
			0xc3 /* 0xcc, 0xcc, ... */
		}, "YYRealloc")
	};

	LPBYTE pf_Sound_ReportSystemStatus{ fastCodeSearch({
			// push "available"
			0x68, bany, bany, bany, bany,
			// sub esp,0x8
			0x83, 0xec, 0x08,
			// xorps xmm0,xmm0
			0x0f, 0x57, 0xc0,
			// movsd qword ptr [esp]=>local_c,xmm0
			0xf2, 0x0f, 0x11, 0x04, 0x24,
			// push "status"
			0x68, bany, bany, bany, bany,
			// push "audio_system_status"
			0x68, bany, bany, bany, bany,
			// sub esp,0x8
			0x83, 0xec, 0x08,
			// movsd qword ptr [esp]=>local_1c,xmm0
			0xf2, 0x0f, 0x11, 0x04, 0x24
		}, "Sound_ReportSystemStatus")
	};

	YYRealloc = reinterpret_cast<YYRealloc_t>(pf_YYRealloc);

	LPBYTE p_length{ &pf_Function_Add[2] };
	LPBYTE p_capacity{ &pf_Function_Add[7] };
	LPBYTE p_runtimetable{ &pf_Function_Add[37] };

	LPBYTE pf_Variable_BuiltIn_Add{ fastCodeSearch({
			// mov eax,[address]
			0xa1, bany, bany, bany, bany,
			// cmp eax,0x1f4 (500)
			0x3d, 0xf4, 0x01, 0x00, 0x00,
			// jnz
			0x75, 0x0e,
			// push [address] (INTERNAL ERROR)
			0x68 /* ... */
		}, "Variable_BuiltIn_Add")
	};

	LPBYTE pf_Create_Object_Lists{ fastCodeSearch({
			// push esi
			0x56,
			// push edi
			0x57,
			// xor eax,eax
			0x33, 0xc0,
			// mov ecx,0x100
			0xb9, 0x00, 0x01, 0x00, 0x00,
			// mov edi,[address]
			0xbf /* ... */
		}, "Create_Object_Lists")
	};

	LPBYTE pf_Insert_Event{ fastCodeSearch({
			// sub esp,0xc
			0x83, 0xec, 0x0c,
			// push ebx
			0x53,
			// push ebp
			0x55,
			// push esi
			0x56,
			// mov esi,ecx
			0x8b, 0xf1,
			// push edi
			0x57,
			// mov dword ptr [esp + local_4],esi
			0x89, 0x74, 0x24, 0x18,
			// mov eax, dword ptr [esi + 0x4]
			0x8b, 0x46, 0x04,
			// cmp eax, dword ptr [esi + 0xc]
			0x3b, 0x46, 0x0c,
			// jle...
			0x7e, 0x05,
			// call ....
			0xe8, bany, bany, bany, bany,
			// mov eax dword ptr [esp + param_2]
			0x8b, 0x44, 0x24, 0x24,
			// inc dword ptr [esi + 4]
			0xff, 0x46, 0x04,
			// :) push first part of hash
			0x68, 0xb9, 0x79, 0x37, 0x9e,
			// push second part of hash
			0x68, 0x55, 0x7c, 0x4a, 0x7f
		}, "CGMEvent::Insert_Event")
	};

	LPBYTE pf_cmp_userfunc{ fastCodeSearch({
			// sub esp,0x18
			0x83, 0xec, 0x18,
			// mov eax, dword ptr [esp + param_1]
			0x8b, 0x44, 0x24, 0x1c,
			// lea edx=>,local_18,[esp]
			0x8d, 0x14, 0x24,
			// mov dword ptr [esp]=>local_18,eax
			0x89, 0x04, 0x24,
			// xorps xmm0,xmm0
			0x0f, 0x57, 0xc0,
			// mov eax, dword ptr [esp + param_2]
			0x8b, 0x44, 0x24, 0x20,
			// mov dword ptr [esp + local_14],eax
			0x89, 0x44, 0x24, 0x04,
			// mov eax,[address]
			0xa1 /* ... */
		}, "cmp_userfunc")
	};

	LPBYTE pf_JS_GenericObjectConstructor{ fastCodeSearch({
			// mov eax, dword ptr [esp + param_1]
			0x8b, 0x44, 0x24, 0x04,
			// sub esp, 0x10
			0x83, 0xec, 0x10,
			// push esi
			0x56,
			// mov esi, dword ptr [eax]
			0x8b, 0x30,
			// push edi,
			0x57,
			// or dword ptr [esi + 0x3c],0x1
			0x83, 0x4e, 0x3c, 0x01,
			// mov dword ptr [esi + 0x1c],"Object"
			0xc7, 0x46, 0x1c,
			bany, bany, bany, bany,
			// mov dword ptr [esi + 0x20],func
			0xc7, 0x46, 0x20,
			bany, bany, bany, bany,
			// mov dword ptr [esi + 0x24],func
			0xc7, 0x46, 0x24,
			bany, bany, bany, bany
		}, "JS_GenericObjectConstructor")
	};

	LPBYTE pf_YYObjectBase_Alloc{ fastCodeSearch({
			// push ebx
			0x53,
			// push ebp
			0x55,
			// mov ebp, dword ptr [esp + param_3]
			0x8b, 0x6c, 0x24, 0x14,
			// push esi
			0x56,
			// push edi
			0x57,
			// mov esi, dword ptr [ebp*0x4 + addr]
			0x8b, 0x34, 0xad,
			bany, bany, bany, bany,
			// test esi,esi
			0x85, 0xf6,
			// jz ...
			0x74, 0x7d,
			// mov eax, dword ptr [esi + 8]
			0x8b, 0x46, 0x08,
			// mov ebx, dword ptr [esp + param_1]
			0x8b, 0x5c, 0x24, 0x14,
			// mov dword ptr [ebp*0x4 + addr],eax
			0x89, 0x04, 0xad
			/* ... */
		}, "YYObjectBase::Alloc")
	};

	LPBYTE pf_YYSetScriptRef{ fastCodeSearch({
			// push ebp
			0x55,
			// mov ebp,esp
			0x8b, 0xec,
			// push -0x1
			0x6a, 0xff,
			// push [addr]
			0x68, bany, bany, bany, bany,
			// mov eax,fs:[0x0]
			0x64, 0xa1, 0x00, 0x00, 0x00, 0x00,
			// push eax
			0x50,
			// push ebx
			0x53,
			// push esi
			0x56,
			// push edi
			0x57,
			// mov eax,[addr]
			0xa1, bany, bany, bany, bany,
			// xor eax,ebp
			0x33, 0xc5,
			// push eax
			0x50,
			// lea eax=>local_10,[ebp + -0xc]
			0x8d, 0x45, 0xf4,
			// mov fs:[0x0],eax
			0x64, 0xa3, 0x00, 0x00, 0x00, 0x00,
			// mov esi, dword ptr [ebp + param_1]
			0x8b, 0x75, 0x08,
			// push 0xa0
			0x68, 0xa0, 0x00, 0x00, 0x00,
			// mov dword ptr [esi + 0xc],0x6
			0xc7, 0x46, 0x0c, 0x06, 0x00, 0x00, 0x00,
			// call func
			0xe8, bany, bany, bany, bany,
			// add esp,0x4
			0x83, 0xc4, 0x04
			/* ... */
		}, "YYSetScriptRef")
	};

	LPBYTE pf_YYGetInt32{ fastCodeSearch({
			// sub esp,0x10
			0x83, 0xec, 0x10,
			// push esi
			0x56,
			// push edi
			0x57,
			// mov edi, dword ptr [esp + param_2]
			0x8b, 0x7c, 0x24, 0x20,
			// mov esi,edi
			0x8b, 0xf7,
			// shl esi,0x4
			0xc1, 0xe6, 0x04,
			// add esi, dword ptr [esp + param_1]
			0x03, 0x74, 0x24, 0x1c,
			// mov eax, dword ptr [esi + 0xc]
			0x8b, 0x46, 0x0c,
			// and eax,UNSET_MASK_RV
			0x25, 0xff, 0xff, 0xff, 0x00,
			// cmp eax, 0xd
			0x83, 0xf8, 0x0d,
			// ja switchD_0197f7aa::caseD_2
			0x0f, bany, bany, bany, bany, bany,
			// movzx eax, byte ptr [eax + switchtable]
			0x0f, bany, bany, bany, bany, bany, bany,
			/* cases go below... */
			0xff
		}, "YYGetInt32")
	};

	LPBYTE pf_CreateAsynEventWithDSMapAndBuffer{ fastCodeSearch({
			0x55,
			0x8b, 0xec,
			0x6a, 0xff,
			0x68, bany, bany, bany, bany,
			0x64, 0xa1, 0x00, 0x00, 0x00, 0x00,
			0x50,
			0x56,
			0xa1, bany, bany, bany, bany,
			0x33, 0xc5,
			0x50,
			0x8d, 0x45, 0xf4,
			0x64, 0xa3, 0x00, 0x00, 0x00, 0x00,
			0x6a, 0x0c,
			0xe8, bany, bany, bany, bany,
			0x8b, 0xf0,
			0x83, 0xc4, 0x04,
			0x85, 0xf6,
			bany, bany,
			0x0f, 0x57, 0xc0,
			0x66, 0x0f, 0xd6, 0x06,
			0xc7, 0x46, 0x08,
			0x00, 0x00, 0x00, 0x00,
			bany, bany,
			0x33, 0xf6,
			0x8b, 0x45, 0x08,
			0x89, 0x06,
			0x8b, 0x45, 0x0c,
			0x89, 0x46, 0x08,
			0x8b, 0x45, 0x10,
			0x6a, 0x44,
			0x89, 0x46, 0x04,
			0xe8 /* ... */
		}, "CreateAsynEventWithDSMapAndBuffer")
	};

	CreateAsynEventWithDSMapAndBuffer = reinterpret_cast<CreateAsynEventWithDSMapAndBuffer_t>(pf_CreateAsynEventWithDSMapAndBuffer);
	pf_YYGetInt32 += 167;
	g_ppFunction = reinterpret_cast<RFunction**>(*reinterpret_cast<std::uintptr_t*>(pf_YYGetInt32));

	YYSetScriptRef = reinterpret_cast<YYSetScriptRef_t>(pf_YYSetScriptRef);
	JS_GenericObjectConstructor = reinterpret_cast<TRoutine>(pf_JS_GenericObjectConstructor);
	YYObjectBase_Alloc = reinterpret_cast<YYObjectBase_Alloc_t>(pf_YYObjectBase_Alloc);

	LPBYTE pf_YYGML_CallMethod{ &pf_cmp_userfunc[65] };
	std::uintptr_t pfn_CallMethod{ *reinterpret_cast<std::uintptr_t*>(pf_YYGML_CallMethod) };
	pfn_CallMethod += reinterpret_cast<std::uintptr_t>(pf_YYGML_CallMethod + sizeof(std::uintptr_t));
	YYGML_CallMethod = reinterpret_cast<YYGML_CallMethod_t>(pfn_CallMethod);

	LPBYTE pf_CreateAsyncEv{ &pf_Sound_ReportSystemStatus[50] };
	std::uintptr_t pfn_CreateAsyncEv{ *reinterpret_cast<std::uintptr_t*>(pf_CreateAsyncEv) };
	pfn_CreateAsyncEv += reinterpret_cast<std::uintptr_t>(pf_CreateAsyncEv + sizeof(std::uintptr_t));
	Create_Async_Event = reinterpret_cast<Create_Async_Event_t>(pfn_CreateAsyncEv);

	LPBYTE p_rvarraylength{ &pf_Variable_BuiltIn_Add[1] };
	LPBYTE p_rvarray{ &pf_Variable_BuiltIn_Add[32] };

	Create_Object_Lists = reinterpret_cast<Create_Object_Lists_t>(pf_Create_Object_Lists);
	(*reinterpret_cast<LPBYTE*>(&Insert_Event)) = pf_Insert_Event;

	p_the_numb = reinterpret_cast<int*>(*reinterpret_cast<std::uintptr_t*>(p_length));
	g_pCurrentMaxLength = reinterpret_cast<int*>(*reinterpret_cast<std::uintptr_t*>(p_capacity));
	g_pThe_functions = reinterpret_cast<RFunction**>(*reinterpret_cast<std::uintptr_t*>(p_runtimetable));

	g_pRVArrayLen = reinterpret_cast<int*>(*reinterpret_cast<std::uintptr_t*>(p_rvarraylength));
	g_pRVArray = reinterpret_cast<RVariableRoutine*>(*reinterpret_cast<std::uintptr_t*>(p_rvarray));


	/* let's init minhook */
	/*! minhook begin */
	ensureMinhookCall(MH_Initialize());
	ensureMinhookCall(MH_CreateHook(pf_InitLLVM, &initLLVM, reinterpret_cast<LPVOID*>(&trampoline)));
	ensureMinhookCall(MH_CreateHook(pf_StartRoom, &startRoom, reinterpret_cast<LPVOID*>(&sroom_trampoline)));
	ensureMinhookCall(MH_EnableHook(MH_ALL_HOOKS));
	/*! minhook end */

	/* we're done here: */
	appliedHooks = true;
	return true;
}

