#pragma once

/*
* LibModShovel by nkrapivindev
* GameMaker Studio 2.3.3 internal structures and classes.
*/

#include <string_view>

// fwd decls:
using uint = unsigned;
using int64 = long long;
class CInstanceBase;
class YYObjectBase;
class CInstance;
class CWeakRef;
class CEvent;
class VMBuffer;
struct RValue;
struct RefDynamicArrayOfRValue;
struct GCContext;
struct SLink;
struct pcre;
struct pcre_extra;
class CObjectGM;
class CPhysicsObject;
class CSkeletonInstance;
class CSequenceInstance;
class cInstancePathAndTimeline;
class CScript;
class CScriptRef;
class CCode;

struct YYVAR {
	const char* pName;
	int val;
	void* operator new(size_t size);
	void operator delete(void* p);
};

using PFUNC_YYGML = void(*)(CInstance* _pSelf, CInstance* _pOther);
using PFUNC_YYGMLScript_Internal = RValue & (*)(CInstance* selfinst, CInstance* otherinst, RValue& Result, int argc, RValue* args[]);


struct YYGMLFuncs {
	const char* pName;
	PFUNC_YYGML pFunc;
	YYVAR* pFuncVar;

	YYGMLFuncs(PFUNC_YYGMLScript_Internal pfn);
	~YYGMLFuncs();
	void* operator new(size_t size);
	void operator delete(void* p);
};

struct SYYStackTrace {
	SYYStackTrace* pNext;
	const char* pName;
	int line;
	//int tos;

	static SYYStackTrace** s_ppStart;
	SYYStackTrace(const char* _pName, int _line);
	~SYYStackTrace();
};

typedef struct tagYYRECT {
	int left;
	int top;
	int right;
	int bottom;
} YYRECT, *PYYRECT;

struct SLinkListEx {
	SLink* head;
	SLink* tail;
	int offset;
};

struct SLink {
	SLink* next;
	SLink* prev;
	SLinkListEx* list;
};

class cInstancePathAndTimeline {
	int i_pathindex;
	float i_pathposition;
	float i_pathpositionprevious;
	float i_pathspeed;
	float i_pathscale;
	float i_pathorientation;
	int i_pathend;
	float i_pathxstart;
	float i_pathystart;
	int i_timelineindex;
	float i_timelineprevposition;
	float i_timelineposition;
	float i_timelinespeed;
};

constexpr int MASK_KIND_RVALUE = 0x0ffffff;
constexpr int VALUE_REAL = 0;		// Real value
constexpr int VALUE_STRING = 1;		// String value
constexpr int VALUE_ARRAY = 2;		// Array value
constexpr int VALUE_OBJECT = 6;	// YYObjectBase* value 
constexpr int VALUE_INT32 = 7;		// Int32 value
constexpr int VALUE_UNDEFINED = 5;	// Undefined value
constexpr int VALUE_PTR = 3;			// Ptr value
constexpr int VALUE_VEC3 = 4;		// Vec3 (x,y,z) value (within the RValue)
constexpr int VALUE_VEC4 = 8;		// Vec4 (x,y,z,w) value (allocated from pool)
constexpr int VALUE_VEC44 = 9;		// Vec44 (matrix) value (allocated from pool)
constexpr int VALUE_INT64 = 10;		// Int64 value
constexpr int VALUE_ACCESSOR = 11;	// Actually an accessor
constexpr int VALUE_NULL = 12;	// JS Null
constexpr int VALUE_BOOL = 13;	// Bool value
constexpr int VALUE_ITERATOR = 14;	// JS For-in Iterator
constexpr int VALUE_REF = 15;		// Reference value (uses the ptr to point at a RefBase structure)
constexpr int VALUE_UNSET = MASK_KIND_RVALUE;


enum class YYObjectBaseKind {
	KIND_YYOBJECTBASE = 0,
	KIND_CINSTANCE,
	KIND_ACCESSOR,
	KIND_SCRIPTREF,
	KIND_PROPERTY,
	KIND_ARRAY,
	KIND_WEAKREF,

	KIND_CONTAINER,

	KIND_SEQUENCE,
	KIND_SEQUENCEINSTANCE,
	KIND_SEQUENCETRACK,
	KIND_SEQUENCECURVE,
	KIND_SEQUENCECURVECHANNEL,
	KIND_SEQUENCECURVEPOINT,
	KIND_SEQUENCEKEYFRAMESTORE,
	KIND_SEQUENCEKEYFRAME,
	KIND_SEQUENCEKEYFRAMEDATA,
	KIND_SEQUENCEEVALTREE,
	KIND_SEQUENCEEVALNODE,
	KIND_SEQUENCEEVENT,

	KIND_NINESLICE,

	KIND_MAX
};

template<typename T>
struct _RefThing {
	T m_thing;
	int m_refCount;
	int m_size;

	void inc() {
		++m_refCount;
	}

	T get() const { return m_thing; }
	int size() const { return m_size; }

	static _RefThing<T>* assign(_RefThing<T>* _other) {
		if (_other != NULL) {
			_other->inc();
		}

		return _other;
	}
};

using RefString = _RefThing<const char*>;
using RefKeep = _RefThing<void*>;
using RefInstance = _RefThing<YYObjectBase*>;

#pragma pack(push, 4)
struct DValue {
public:
	union {
		int64 __64;
		void* __ptr;
		RefDynamicArrayOfRValue* __arr;
		YYObjectBase* __obj;
	};

	uint flags;
	uint kind;
};

struct RValue {
private:
	void __localFree();
	void __localCopy(const RValue& other);

public:
	union {
		int v32;
		int64 v64;
		double val;
		void* ptr;
		RefDynamicArrayOfRValue* pArray;
		YYObjectBase* pObj;
		RefString* pString;
	};

	uint flags;
	uint kind;

	RValue();
	RValue(const RValue& other);
	RValue(bool v);
	RValue(int v);
	RValue(double v);
	RValue(long long v);
	RValue(PFUNC_YYGMLScript_Internal v, YYObjectBase* mtself); /* method constructor */
	RValue& operator=(const RValue& other);
	/* stringpointer stuff better be explicit */
	explicit RValue(YYObjectBase* obj);
	explicit RValue(RefDynamicArrayOfRValue* v);
	explicit RValue(std::nullptr_t);
	explicit RValue(void* v);
	explicit RValue(const char* v);
	const RValue& operator[](std::size_t indx) const;
	RValue& operator[](std::size_t indx);
	bool tryToNumber(double& res) const;
	~RValue();

	void* operator new(size_t size);
	void* operator new[](size_t size);
	void operator delete(void* p);
	void operator delete[](void* p);
};
#pragma pack(pop)

using YYCreateString_t = void(*)(RValue* pRv, const char* pcStr);
extern YYCreateString_t YYCreateString;

enum class EJSRetValBool {
	EJSRVB_FALSE,
	EJSRVB_TRUE,
	EJSRVB_TYPE_ERROR
};

enum class EHasInstanceRetVal {
	E_FALSE,
	E_TRUE,
	E_TYPE_ERROR
};

struct GCContext {
	RValue* pRValueFreeList;
	RValue* pRValueFreeListTail;
	RValue** pRValsToDecRef;
	bool* pRValsToDecRefFree;
	int maxRValsToDecRef;
	int numRValsToDecRef;
	RefDynamicArrayOfRValue** pArraysToFree;
	int maxArraysToFree;
	int numArraysToFree;
};

class CInstanceBase {
public:
	RValue* yyvars;
	virtual ~CInstanceBase() = 0;

	RValue& GetYYVarRef(int index);
	virtual RValue& InternalGetYYVarRef(int index) = 0;
	RValue& GetYYVarRefL(int index);
	virtual RValue& InternalGetYYVarRefL(int index) = 0;
};

class YYObjectBase : CInstanceBase {
	virtual bool Mark4GC(uint* _pM, int _numObjects) = 0;
	virtual bool MarkThisOnly4GC(uint* _pM, int _numObjects) = 0;
	virtual bool MarkOnlyChildren4GC(uint* _pM, int _numObjects) = 0;
	virtual void Free(bool preserve_map) = 0;
	virtual void ThreadFree(YYObjectBase* _pO, bool preserve_map, GCContext* _pGCContext) = 0;
	virtual void PreFree() = 0;

public:
	YYObjectBase* m_pNextObject;
	YYObjectBase* m_pPrevObject;
	YYObjectBase* m_prototype;
	pcre* m_pcre;
	pcre_extra* m_pcreExtra;
	const char* m_class;
	void (*m_getOwnProperty)(YYObjectBase* obj, RValue* val, const char* name);
	void (*m_deleteProperty)(YYObjectBase* obj, RValue* val, const char* name, bool dothrow);
	EJSRetValBool(*m_defineOwnProperty)(YYObjectBase* obj, const char* name, RValue* val, bool dothrow);
	void* m_yyvarsMap;
	CWeakRef** m_pWeakRefs;
	uint m_numWeakRefs;
	uint m_nvars;
	uint m_flags;
	uint m_capacity;
	uint m_visited;
	uint m_visitedGC;
	int m_GCgen;
	int m_GCcreationframe;
	int m_slot;
	YYObjectBaseKind m_kind;
	int m_rvalueInitType;
	int m_curSlot;
};

class CInstance : YYObjectBase {
	int64 m_CreateCounter;
public:
	CObjectGM* m_pObject;
	CPhysicsObject* m_pPhysicsObject;
	CSkeletonInstance* m_pSkeletonAnimation;
	CSequenceInstance* m_pControllingSeqInst;
	uint m_InstFlags;
	int i_id;
	int i_objectindex;
	int i_spriteindex;
	float i_sequencePos;
	float i_lastSequencePos;
	float i_sequenceDir;
	float i_imageindex;
	float i_imagespeed;
	float i_imagescalex;
	float i_imagescaley;
	float i_imageangle;
	float i_imagealpha;
	uint i_imageblend;
	float i_x;
	float i_y;
	float i_xstart;
	float i_ystart;
	float i_xprevious;
	float i_yprevious;
	float i_direction;
	float i_speed;
	float i_friction;
	float i_gravitydir;
	float i_gravity;
	float i_hspeed;
	float i_vspeed;
	YYRECT i_bbox;
	int i_timer[12];
	cInstancePathAndTimeline* m_pPathAndTimeline;
	CCode* i_initcode;
	CCode* i_precreatecode;
	CObjectGM* m_pOldObject;
	int m_nLayerID;
	int i_maskindex;
	short m_nMouseOver;
	CInstance* m_pNext;
	CInstance* m_pPrev;
	SLink m_collisionLink;
	SLink m_dirtyLink;
	SLink m_withLink;
	float i_depth;
	float i_currentdepth;
	float i_lastImageNumber;
	uint m_collisionTestNumber;
};

struct RefDynamicArrayOfRValue : YYObjectBase {
	int refcount;
	int flags;
	RValue* pArray;
	int64 owner;
	int visited;
	int length;
};

using TRoutine = void(*)(RValue& Result, CInstance* selfinst, CInstance* otherinst, int argc, RValue arg[]);
using JSConstructorFunc = TRoutine;
using TObjectCall = TRoutine;

extern TRoutine F_Typeof;
extern TRoutine F_ArrayCreate;
extern TRoutine F_ArrayGet;
extern TRoutine F_ArraySet;
extern TRoutine F_ArrayLength;
void deriveYYCreateString();

struct RFunction {
	char f_name[64];
	TRoutine f_routine;
	int f_argnumb;
	uint m_UsageCount; /* always UINT_MAX */
};

class CScriptRef : YYObjectBase {
	CScript* m_callScript;
	TObjectCall m_callCpp;
	PFUNC_YYGMLScript_Internal m_callYYC;
	RValue m_scope;
	RValue m_boundThis;
	YYObjectBase* m_pStatic;
	EHasInstanceRetVal(*m_hasInstance)(YYObjectBase* obj, RValue* val);
	JSConstructorFunc m_construct;
	const char* m_tag;
};

enum class eGML_TYPE {
	eGMLT_NONE,
	eGMLT_DOUBLE,
	eGMLT_STRING,
	eGMLT_INT32 = 4,
	eGMLT_ERROR = -1
};

struct RToken {
	int kind;
	eGML_TYPE type;
	int ind;
	int ind2;
	RValue value;
	int itemnumb;
	RToken* items;
	int position;
};

class CCode {
	virtual ~CCode();
public:
	CCode* m_pNext;
	int i_kind;
	bool i_compiled;
	const char* i_str;
	RToken i_token;
	RValue i_value;
	VMBuffer* i_pVM;
	VMBuffer* i_pVMDebugInfo;
	const char* i_pCode;
	const char* i_pName;
	int i_CodeIndex;
	YYGMLFuncs* i_pFunc;
	bool i_watch;
	int i_offset;
	int i_locals;
	int i_args;
	int i_flags;
	YYObjectBase* i_pPrototype;

	CCode(PFUNC_YYGMLScript_Internal pfn);
	void* operator new(size_t size);
	void operator delete(void* p);
};

class CEvent {
public:
	CCode* e_code;
	int m_OwnerObjectID;

	CEvent(CCode* pCode, int objId);
	void* operator new(size_t size);
	void operator delete(void* p);
};

extern RFunction** g_ppFunction;
extern int* g_pCurrentMaxLength;
extern int* p_the_numb;
extern RFunction** g_pThe_functions;

struct SLLVMVars {
	char* pWad;				// pointer to the Wad (always nullptr since GMS 2 YYC)
	int   nWadFileLength;		// the length of the wad (does not really matter)
	int   nGlobalVariables;	// global varables
	int   nInstanceVariables;	// instance variables
	int   nYYCode; // amount of elements in pGMLFuncs
	YYVAR** ppVars; // variable ids, also includes built-ins.
	YYVAR** ppFuncs; // global scripts and stuff
	YYGMLFuncs* pGMLFuncs; // gml scripts and methods
	void* pYYStackTrace;		// pointer to the stack trace
};

struct SYYCaseEntry {
	RValue entry;
	int value;
};

using TGetVarRoutine = bool(*)(CInstance* selfinst, int array_index, RValue* out_val);
using TSetVarRoutine = bool(*)(CInstance* selfinst, int array_index, RValue* in_val);

using YYRealloc_t = void*(*)(void* _p, int _newsize);
extern YYRealloc_t YYRealloc;

using YYFree_t = void(*)(void* _p);
extern YYFree_t YYFree;
void deriveYYFree(std::byte* dat);

typedef struct RVariableRoutine_t {
	const char* f_name;
	TGetVarRoutine f_getroutine;
	TSetVarRoutine f_setroutine;
	bool f_canset;
} RVariableRoutine;

extern SLLVMVars* g_pLLVMVars;
extern int* g_pRVArrayLen;
extern RVariableRoutine* g_pRVArray;
using PFUNC_InitYYC = void(*)(SLLVMVars* _pVars);
using PFUNC_MainLoop_Init = void(*)();
using PFUNC_StartRoom = void(*)(int numb, bool starting);

using FREE_RValue__Pre_t = void(*)(RValue* _pValue);
extern FREE_RValue__Pre_t FREE_RValue__Pre;

using COPY_RValue_do__Post_t = void(*)(RValue* _pDest, const RValue* _pSource);
extern COPY_RValue_do__Post_t COPY_RValue_do__Post;



#include <limits>
constexpr void* POINTER_NULL = 0;
//constexpr void* POINTER_INVALID = ~static_cast<std::uintptr_t>(POINTER_NULL);
constexpr int ARRAY_INDEX_NO_INDEX = std::numeric_limits<int>::min();

extern void MMSetLength(void** buf, size_t new_length);
extern void deriveCopyRValuePre(std::byte* dat);

template<typename T>
struct CHashNode {
	CHashNode<T>* m_pPrev;
	CHashNode<T>* m_pNext;
	uint m_ID;
	T* m_pObj;
};

template<typename T>
struct CHashLink {
	CHashNode<T>* m_pFirst;
	CHashNode<T>* m_pLast;
};

template<typename T>
class CHash {
	CHashLink<CHashNode<T>>* m_pHashingTable;
	int m_HashingMask;
	int m_Count;

public:
	T* findObject(uint id) {
		for (
			auto node{ m_pHashingTable[m_HashingMask & id].m_pFirst };
			node != nullptr;
			node = node->m_pNext) {
			if (node->m_ID == id) {
				return reinterpret_cast<T*>(node->m_pObj);
			}
		}

		return nullptr;
	}
};

template<typename T>
struct SLinkedListNode {
	SLinkedListNode<T>* m_pNext;
	SLinkedListNode<T>* m_pPrev;
	T* m_pObj;
};

template<typename T>
struct SLinkedList {
	SLinkedListNode<T>* m_pFirst;
	SLinkedListNode<T>* m_pLast;
	int m_Count;
};

using Hash = uint;
using uint64 = unsigned long long;

template<typename Tkey>
inline Hash CHashMapCalculateHash(Tkey _k);

template<>
inline Hash CHashMapCalculateHash<int>(int _k) {
	return (_k * -0x61c8864f + 1) & 0x7fffffff;
}

template<>
inline Hash CHashMapCalculateHash<YYObjectBase*>(YYObjectBase* _k) {
	return ((static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_k)) >> 6) * 7 + 1) & 0x7fffffff;
}

template<>
inline Hash CHashMapCalculateHash<uint64>(uint64 _k) {
	return (static_cast<int>((_k * 0x9E3779B97F4A7C55ull) >> 0x20ull) + 1) & 0x7fffffff;
}

template<>
inline Hash CHashMapCalculateHash<void*>(void* _k) {
	return ((static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_k)) >> 8) + 1) & 0x7fffffff;
}

template<>
inline Hash CHashMapCalculateHash<unsigned char*>(unsigned char* _k) {
	return (reinterpret_cast<std::uintptr_t>(_k) + 1) & 0x7fffffff;
}

template<typename Tkey, typename Tvalue>
struct Element {
	Tvalue v;
	Tkey k;
	Hash h;
};

template<typename Tkey, typename Tvalue, int TinitialMask>
class CHashMap {
public:
	int m_curSize;
	int m_numUsed;
	int m_curMask;
	int m_growThreshold;
	Element<Tkey, Tvalue>* m_elements;
	void(*m_deleter)(Tkey* pKey, Tvalue* pVal);

	bool findElement(Tkey tk, Tvalue* outElem) {
		Hash myhash{ CHashMapCalculateHash<Tkey>(tk) };
		int idealpos{ static_cast<int>(m_curMask & myhash) };

		for (auto node{ m_elements[idealpos] }; node.h != 0; node = m_elements[(++idealpos) & m_curMask]) {
			if (node.k == tk) {
				*outElem = node.v;
				return true;
			}
		}

		return false;
	}
};

class CPhysicsDataGM {
public:
	float* m_physicsVertices;
	bool m_physicsObject;
	bool m_physicsSensor;
	bool m_physicsAware;
	bool m_physicsKinematic;
	int m_physicsShape;
	int m_physicsGroup;
	float m_physicsDensity;
	float m_physicsRestitution;
	float m_physicsLinearDamping;
	float m_physicsAngularDamping;
	float m_physicsFriction;
	int m_physicsVertexCount;
};

class CObjectGM {
public:
	const char* m_pName;
	CObjectGM* m_pParent;
	CHashMap<int, CObjectGM*, 2>* m_childrenMap;
	CHashMap<unsigned long long, CEvent*, 3>* m_eventsMap;
	CPhysicsDataGM m_physicsData;
	SLinkedList<CInstance> m_Instances;
	SLinkedList<CInstance> m_Instances_Recursive;
	uint m_Flags;
	int m_spriteindex;
	int m_depth;
	int m_parent;
	int m_mask;
	int m_ID;
};

extern CHash<CObjectGM>** g_ppObjectHash;

using Create_Object_Lists_t = void(*)();
extern Create_Object_Lists_t Create_Object_Lists;
using Insert_Event_t = void(__thiscall*)(CHashMap<unsigned long long, CEvent*, 3>* self, unsigned long long key, CEvent* value);
extern Insert_Event_t Insert_Event;

//YYRValue& YYGML_CallMethod( CInstance* _pSelf, CInstance* _pOther, YYRValue& _result, int _argc, const YYRValue& _method, YYRValue** _args );
using YYGML_CallMethod_t = RValue&(*)(CInstance* pSelf, CInstance* pOther, RValue& Result, int argc, const RValue& method, RValue** args);
extern YYGML_CallMethod_t YYGML_CallMethod;

// yes I know that is very cursed but that's how it is!
class YYGMLException {
public:
	char m_object[sizeof RValue];

	YYGMLException(CInstance* _pSelf, CInstance* _pOther, const char* _pMessage, const char* _pLongMessage, const char* _filename, int _line, const char** ppStackTrace, int numLines);
	YYGMLException(const RValue& _val);
	const RValue& GetExceptionObject() const;
};

void deriveObjectHash(std::byte* func);

extern YYObjectBase* g_pGlobal;
extern TRoutine F_String;

using Create_Async_Event_t = void(*)(int dsmapindex, int event_index);
extern Create_Async_Event_t Create_Async_Event;

extern TRoutine JS_GenericObjectConstructor;

using YYObjectBase_Alloc_t = YYObjectBase*(*)(uint _n, uint _rvalueInitType, YYObjectBaseKind _objectkind, bool force_allocate_yyvars);
extern YYObjectBase_Alloc_t YYObjectBase_Alloc;
