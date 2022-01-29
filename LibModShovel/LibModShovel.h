#pragma once
#include "pch.h"
#include <string>
/* this is very horrible, I know... */
namespace std {
	using byte = unsigned char;
	using string_view = string;
}
#ifdef LIBMODSHOVEL_EXPORTS
#define LIBMODSHOVEL_API __declspec(dllexport)
#else
#define LIBMODSHOVEL_API __declspec(dllimport)
#endif
#define LMSAPI LIBMODSHOVEL_API

/*
* LibModShovel by nkrapivindev
* Main header file of LMS, defines the dllexport shit and the Global class (needed for initialization)
*/

namespace LMS {
	class Global {
	private:
		/* this stuff should normally never be accessible*/
		static HMODULE myDllHandle;

		static bool waitForDebugger();
		static void printRandomQuote();

	public:
		/* these methods are used by other LMS classes */
		static bool throwError(const std::string_view strview);
		static void initStage2();
		//static HMODULE getMyself();

		/* these are for DllMain() */
		static bool Init(HMODULE hMe);
		static bool Quit();
	};
}
