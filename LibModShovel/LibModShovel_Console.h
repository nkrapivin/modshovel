#pragma once
#include "pch.h"

#include <cstdio>
#include <iostream>

/*
*  LibModShovel by nkrapivindev
*  Console class header file
*/

namespace LMS {
	class Console {
	private:
		static HANDLE prevConIn;
		static HANDLE prevConOut;
		static HANDLE prevConErr;

		static HANDLE conIn;
		static HANDLE conOut;

		static FILE* oldStdout;
		static FILE* oldStdin;
		static FILE* oldStderr;

	public:
		static bool Init();
		static bool Quit();
	};
}
