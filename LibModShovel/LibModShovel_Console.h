#pragma once
#include "pch.h"

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

	public:
		static bool Init();
		static bool Quit();
	};
}
