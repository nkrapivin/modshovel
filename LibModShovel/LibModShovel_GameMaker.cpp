#include "LibModShovel_GameMaker.h"
#include "LibModShovel.h"
#include "LibModShovel_Lua.h"
#include "LibModShovel_MethodAutogen.h"

/*
* LibModShovel by nkrapivindev
* Implements one-line GameMaker internal methods here.
*/

/* please set this to a pointer to the global stacktrace thing... */
SYYStackTrace** SYYStackTrace::s_ppStart{};
/* please set this to the address of global g_pFunction var... */
RFunction** g_ppFunction{};
/* our llvm vars */
SLLVMVars* g_pLLVMVars{};

/* runtime function table: */
int* g_pCurrentMaxLength{};
int* p_the_numb{};
RFunction** g_pThe_functions{};

/* built-in vars table: */
int* g_pRVArrayLen{};
RVariableRoutine* g_pRVArray{};
YYObjectBase* g_pGlobal{};

YYCreateString_t YYCreateString{};
YYRealloc_t YYRealloc{};
YYFree_t YYFree{};

FREE_RValue__Pre_t FREE_RValue__Pre{};
COPY_RValue_do__Post_t COPY_RValue_do__Post{};
CHash<CObjectGM>** g_ppObjectHash{};
Create_Object_Lists_t Create_Object_Lists{};
Insert_Event_t Insert_Event{};
YYGML_CallMethod_t YYGML_CallMethod{};
YYSetScriptRef_t YYSetScriptRef{};

Create_Async_Event_t Create_Async_Event{};

TRoutine JS_GenericObjectConstructor{};
YYObjectBase_Alloc_t YYObjectBase_Alloc{};

inline bool YYFree_valid_vkind(const unsigned rvkind) {
	return (((1 << VALUE_STRING) | (1 << VALUE_OBJECT) | (1 << VALUE_ARRAY)) & (1 << (rvkind & 0x1f))) != 0;
}

void MMSetLength(void** buf, size_t new_length) {
	void* newbuf{ YYRealloc(*buf, static_cast<int>(new_length)) };
	*buf = newbuf;
}

void COPY_RValue__Post(RValue* _pDest, const RValue* _pSource) {
	uint skind{ _pSource->kind };
	uint sflags{ _pSource->flags };

	_pDest->kind = _pSource->kind;
	_pDest->flags = _pSource->flags;

	if (YYFree_valid_vkind(skind)) {
		COPY_RValue_do__Post(_pDest, _pSource);
	}
	else {
		_pDest->v64 = _pSource->v64;
	}
}

void deriveYYFree(std::byte* dat) {
	dat += 145;

	std::uintptr_t pfn{ *reinterpret_cast<std::uintptr_t*>(dat) };
	pfn += reinterpret_cast<std::uintptr_t>(dat + sizeof(std::uintptr_t));

	YYFree = reinterpret_cast<YYFree_t>(pfn);
}

void deriveObjectHash(std::byte* dat) {
	dat += 17;

	std::uintptr_t paddr{ *reinterpret_cast<std::uintptr_t*>(dat) };

	g_ppObjectHash = reinterpret_cast<CHash<CObjectGM>**>(paddr);
}

void deriveCopyRValuePre(std::byte* dat) {
	dat += 294;

	std::uintptr_t pfn{ *reinterpret_cast<std::uintptr_t*>(dat) };
	pfn += reinterpret_cast<std::uintptr_t>(dat + sizeof(std::uintptr_t));

	COPY_RValue_do__Post = reinterpret_cast<COPY_RValue_do__Post_t>(pfn);
}

void deriveFreeRValue() {
	std::byte* dat{ &reinterpret_cast<std::byte*>(YYCreateString)[58] };

	std::uintptr_t pfn{ *reinterpret_cast<std::uintptr_t*>(dat) };
	pfn += reinterpret_cast<std::uintptr_t>(dat + sizeof(std::uintptr_t));

	FREE_RValue__Pre = reinterpret_cast<FREE_RValue__Pre_t>(pfn);
}

void deriveYYCreateString(std::byte* dat) {
	dat += 45;

	std::uintptr_t pfn{ *reinterpret_cast<std::uintptr_t*>(dat) };
	pfn += reinterpret_cast<std::uintptr_t>(dat + sizeof(std::uintptr_t));

	YYCreateString = reinterpret_cast<YYCreateString_t>(pfn);
	deriveFreeRValue();
}

YYGMLException::YYGMLException(CInstance* _pSelf, CInstance* _pOther, const char* _pMessage, const char* _pLongMessage, const char* _filename, int _line, const char** ppStackTrace, int numLines) {
	// we need this constructor to be implemented for good RTTI info, but I am way too lazy to actually do so
	// so how about this?
	std::abort();
}

YYGMLException::YYGMLException(const RValue& _val) {
	RValue copy{ _val };
	std::memcpy(&m_object, &copy, sizeof(m_object)); // raw copy...
	std::memset(&copy, 0, sizeof(copy)); // prevent copy from getting destructed...
	// initializing an RValue to all bits zero will make it a real number 0.0, which is fine.
}

const RValue& YYGMLException::GetExceptionObject() const {
	return *reinterpret_cast<const RValue*>(&m_object);
}

RValue& CInstanceBase::GetYYVarRef(int index) {
	return InternalGetYYVarRef(index);
}

RValue& CInstanceBase::GetYYVarRefL(int index) {
	return InternalGetYYVarRefL(index);
}

SYYStackTrace::SYYStackTrace(const char* _pName, int _line) {
	pName = _pName;
	line = _line;

	if (s_ppStart) {
		pNext = *s_ppStart;
		*s_ppStart = this;
	}
	else {
		pNext = nullptr;
	}

	//tos = g_ContextStackTop;
}

SYYStackTrace::~SYYStackTrace() {
	if (s_ppStart) {
		*s_ppStart = pNext;
	}
}

void RValue::__localFree() {
	if (YYFree_valid_vkind(kind)) {
		FREE_RValue__Pre(this);
	}
}

void RValue::__localCopy(const RValue& other) {
	if (&other != this) {
		DValue tmp{};
		memcpy(&tmp, &other, sizeof(RValue));
		bool fIsArray{ (tmp.kind & MASK_KIND_RVALUE) == VALUE_ARRAY };
		if (fIsArray) ++(reinterpret_cast<RValue*>(&tmp)->pArray->refcount);
		__localFree();
		if (fIsArray) --(reinterpret_cast<RValue*>(&tmp)->pArray->refcount);
		COPY_RValue__Post(this, &other);
	}
}

RValue::RValue() : v64{ 0LL }, flags{ 0 }, kind{ VALUE_UNSET } {
}

RValue::RValue(const RValue& other) : RValue() {
	__localCopy(other);
	//return *this;
}

RValue::RValue(const RValue* other) : RValue{ *other } {
}

RValue::RValue(RValue* other) : RValue{ *other } {
}

RValue::RValue(bool v) : val{ v ? 1.0 : 0.0 }, flags{ 0 }, kind{ VALUE_BOOL } {
}

RValue::RValue(int v) : v32{ v }, flags{ 0 }, kind{ VALUE_INT32 } {
}

RValue::RValue(double v) : val{ v }, flags{ 0 }, kind{ VALUE_REAL } {
}

RValue::RValue(long long v) : v64{ v }, flags{ 0 }, kind{ VALUE_INT64 } {
}

RValue::RValue(YYObjectBase* obj) : pObj{ obj }, flags{ 0 }, kind{ VALUE_OBJECT } {
}

RValue::RValue(RefDynamicArrayOfRValue* obj) : pArray{ obj }, flags{ 0 }, kind{ VALUE_ARRAY } {
	++(pArray->refcount);
}

RValue::RValue(std::nullptr_t) : v64{ 0LL }, flags{ 0 }, kind{ VALUE_UNDEFINED } {
}

RValue::RValue(const void* v) : ptr{ const_cast<void*>(v) }, flags{ 0 }, kind{ VALUE_PTR } {
}

RValue::RValue(const char* v) : v64{ 0LL }, flags{ 0 }, kind{ VALUE_INT64 } {
	// will turn an empty INT64 into a string value.
	YYCreateString(this, v);
}

RValue::~RValue() {
	__localFree();
}

void* RValue::operator new(size_t size) {
	void* mem{ nullptr };
	MMSetLength(&mem, size);
	return mem;
}

void* RValue::operator new[](size_t size) {
	void* mem{ nullptr };
	MMSetLength(&mem, size);
	return mem;
}

void RValue::operator delete(void* p) {
	if (p) YYFree(p);
}

void RValue::operator delete[](void* p) {
	if (p) YYFree(p);
}

RValue& RValue::operator++() {
	switch (kind & MASK_KIND_RVALUE) {
		case VALUE_INT32: ++v32; break;
		case VALUE_INT64: ++v64; break;
		case VALUE_REAL:
		case VALUE_BOOL: ++val; break;
		case VALUE_PTR: ptr = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(ptr) + 1); break;
		default: throw 123;
	}

	return *this;
}

RValue& RValue::operator--() {
	switch (kind & MASK_KIND_RVALUE) {
		case VALUE_INT32: --v32; break;
		case VALUE_INT64: --v64; break;
		case VALUE_REAL:
		case VALUE_BOOL: --val; break;
		case VALUE_PTR: ptr = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(ptr) - 1); break;
		default: throw 123;
	}

	return *this;
}

RValue RValue::operator++(int) {
	RValue tmp{ *this };
	operator++();
	return tmp;
}

RValue RValue::operator--(int) {
	RValue tmp{ *this };
	operator--();
	return tmp;
}

RValue::operator bool() const {
	switch (kind & MASK_KIND_RVALUE) {
		case VALUE_INT32: return v32 > 0;
		case VALUE_INT64: return v64 > 0;
		case VALUE_BOOL:
		case VALUE_REAL: return val > 0.5;
		case VALUE_PTR: return ptr != nullptr;
		default: throw 123;
	}
}

bool RValue::operator!() const {
	return !(this->operator bool());
}

RValue::operator double() const {
	switch (kind & MASK_KIND_RVALUE) {
	case VALUE_INT32: return static_cast<double>(v32);
	case VALUE_INT64: return static_cast<double>(v64);
	case VALUE_BOOL:
	case VALUE_REAL: return static_cast<double>(val);
	case VALUE_PTR: return static_cast<double>(reinterpret_cast<std::uintptr_t>(ptr));
	default: throw 123;
	}
}

RValue::operator int() const {
	switch (kind & MASK_KIND_RVALUE) {
	case VALUE_INT32: return static_cast<int>(v32);
	case VALUE_INT64: return static_cast<int>(v64);
	case VALUE_BOOL:
	case VALUE_REAL: return static_cast<int>(val);
	case VALUE_PTR: return static_cast<int>(reinterpret_cast<std::uintptr_t>(ptr));
	default: throw 123;
	}
}

RValue::operator long long() const {
	switch (kind & MASK_KIND_RVALUE) {
	case VALUE_INT32: return static_cast<long long>(v32);
	case VALUE_INT64: return static_cast<long long>(v64);
	case VALUE_BOOL:
	case VALUE_REAL: return static_cast<long long>(val);
	case VALUE_PTR: return static_cast<long long>(reinterpret_cast<std::uintptr_t>(ptr));
	default: throw 123;
	}
}

RValue::operator const void* () const {
	switch (kind & MASK_KIND_RVALUE) {
	case VALUE_INT32: return reinterpret_cast<void*>(static_cast<std::uintptr_t>(v32));
	case VALUE_INT64: return reinterpret_cast<void*>(static_cast<std::uintptr_t>(v64));
	case VALUE_BOOL:
	case VALUE_REAL: return reinterpret_cast<void*>(static_cast<std::uintptr_t>(val));
	case VALUE_PTR: return ptr;
	default: throw 123;
	}
}

bool RValue::operator==(const RValue& other) const {
	switch (kind & MASK_KIND_RVALUE) {
	case VALUE_INT32: return v32 == static_cast<int>(other);
	case VALUE_INT64: return v64 == static_cast<long long>(other);
	case VALUE_BOOL:
	case VALUE_REAL: return val == static_cast<double>(other);
	case VALUE_PTR: return ptr == static_cast<const void*>(other);
	default: throw 123;
	}
}

bool RValue::operator!=(const RValue& other) const {
	return !((*this) == other);
}

bool RValue::operator<=(const RValue& other) const {
	switch (kind & MASK_KIND_RVALUE) {
	case VALUE_INT32: return v32 <= static_cast<int>(other);
	case VALUE_INT64: return v64 <= static_cast<long long>(other);
	case VALUE_BOOL:
	case VALUE_REAL: return val <= static_cast<double>(other);
	case VALUE_PTR: return ptr <= static_cast<const void*>(other);
	default: throw 123;
	}
}

bool RValue::operator>=(const RValue& other) const {
	switch (kind & MASK_KIND_RVALUE) {
	case VALUE_INT32: return v32 >= static_cast<int>(other);
	case VALUE_INT64: return v64 >= static_cast<long long>(other);
	case VALUE_BOOL:
	case VALUE_REAL: return val >= static_cast<double>(other);
	case VALUE_PTR: return ptr >= static_cast<const void*>(other);
	default: throw 123;
	}
}

RValue& RValue::operator=(const RValue& other) {
	__localCopy(other);
	return *this;
}

bool RValue::tryToNumber(double& res) const {
	switch (kind & MASK_KIND_RVALUE) {
		case VALUE_REAL:
		case VALUE_BOOL: {
			res = val;
			return true;
		}

		case VALUE_INT32: {
			res = v32;
			return true;
		}

		case VALUE_INT64: {
			res = static_cast<double>(v64);
			return true;
		}

		case VALUE_PTR: {
			res = static_cast<double>(reinterpret_cast<std::uintptr_t>(ptr));
			return true;
		}
	}

	return false;
}

CEvent::CEvent(CCode* pCode, int objId) {
	e_code = pCode;
	m_OwnerObjectID = objId;
}

void* CEvent::operator new(size_t size) {
	void* block{ nullptr };
	MMSetLength(&block, size);
	return block;
}

void CEvent::operator delete(void* p) {
	if (p) YYFree(p);
}

void* YYVAR::operator new(size_t size) {
	void* block{ nullptr };
	MMSetLength(&block, size);
	return block;
}

void YYVAR::operator delete(void* p) {
	if (p) YYFree(p);
}

YYGMLFuncs::YYGMLFuncs(PFUNC_YYGMLScript_Internal pfn) {
	pName = "_LMS_AutoYYGMLFuncs_";
	pFunc = reinterpret_cast<PFUNC_YYGML>(pfn);
	pFuncVar = new YYVAR{};
	pFuncVar->pName = "_LMS_AutoVVAR_";
	pFuncVar->val = -1;
}

YYGMLFuncs::~YYGMLFuncs() {
	delete pFuncVar;
	pFuncVar = nullptr;
}

void YYGMLFuncs::operator delete(void* p) {
	if (p) YYFree(p);
}

void* YYGMLFuncs::operator new(size_t size) {
	void* block{ nullptr };
	MMSetLength(&block, size);
	return block;
}

void* CCode::operator new(size_t size) {
	void* block{ nullptr };
	MMSetLength(&block, size);
	return block;
}

void CCode::operator delete(void* p) {
	if (p) YYFree(p);
}

CCode::~CCode() {
	delete i_pFunc;
	i_pFunc = nullptr;
}

CCode::CCode(PFUNC_YYGMLScript_Internal pfn) {
	i_pCode = "";
	i_str = "";
	i_compiled = true;
	i_kind = 1;
	i_pVM = nullptr;
	i_pVMDebugInfo = nullptr;
	i_watch = false;
	i_offset = 0;
	i_args = 0;
	i_pPrototype = nullptr;
	i_locals = 0;
	i_flags = 0;
	i_pName = "_LMS_AutoCodeEntry_";
	i_CodeIndex = -1;
	m_pNext = nullptr;
	i_pFunc = new YYGMLFuncs{ pfn };
}

CScriptRefVTable::CScriptRefVTable(
	dtor_t _Destructor,
	getyyvarref_t _InternalGetYYVarRef,
	getyyvarref_t _InternalGetYYVarRefL,
	mark4gc_t _Mark4GC,
	mark4gc_t _MarkThisOnly4GC,
	mark4gc_t _MarkOnlyChildren4GC,
	free_t _Free,
	threadfree_t _ThreadFree,
	prefree_t _PreFree
) : Destructor{_Destructor},
	InternalGetYYVarRef{_InternalGetYYVarRef},
	InternalGetYYVarRefL{_InternalGetYYVarRefL},
	Mark4GC{_Mark4GC},
	MarkThisOnly4GC{_MarkThisOnly4GC},
	MarkOnlyChildren4GC{_MarkOnlyChildren4GC},
	Free{_Free},
	ThreadFree{_ThreadFree},
	PreFree{_PreFree} {
	/* nothing here... */
}

CScriptRefVTable CScriptRefVTable::Originals{
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr
};

CScriptRefVTable CScriptRefVTable::HookTable{
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr
};

void CScriptRef::HookPreFree() {
	LMS::Lua::FreeMethodAt(reinterpret_cast<std::uintptr_t>(m_tag));
	m_tag = nullptr;

#ifdef _DEBUG
	printf("[debug]: Freed method index %u\n", reinterpret_cast<std::uintptr_t>(m_tag));
	fflush(stdout);
#endif

	std::invoke(CScriptRefVTable::Originals.PreFree, this);
}

void CScriptRefVTable::Obtain(CScriptRef* obj) {
	if (Originals.Free) return;
	/* originals only */
	Originals = (**reinterpret_cast<PCScriptRefVTable*>(obj));
	/* may contain replacements */
	HookTable = Originals;
	HookTable.PreFree = &CScriptRef::HookPreFree;
}

void CScriptRefVTable::Replace(CScriptRef* obj) {
	/* le fun */
	(*reinterpret_cast<PCScriptRefVTable*>(obj)) = &HookTable;
}


