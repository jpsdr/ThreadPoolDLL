#ifndef __ThreadPoolDLL_H__
#define __ThreadPoolDLL_H__

#ifdef THREADPOOLDLL_EXPORTS
#define THREADPOOLDLL_API __declspec(dllexport) 
#else
#define THREADPOOLDLL_API __declspec(dllimport) 
#endif

#include <stdint.h>

#include "ThreadPoolDef.h"

#define THREADPOOL_DDL_VERSION "ThreadPoolDLL 1.1.1"

typedef struct _UserData
{
	uint16_t UserId;
	int8_t nPool;
} UserData;


class ThreadPoolInterface
{
	public :

	virtual ~ThreadPoolInterface(void);
	static ThreadPoolInterface* Init(uint8_t num);

	public :

	uint8_t GetThreadNumber(uint8_t thread_number,bool logical);
	bool AllocateThreads(uint16_t &UserId,uint8_t thread_number,uint8_t offset_core,uint8_t offset_ht,bool UseMaxPhysCore,int8_t nPool);
	bool DeAllocateThreads(uint16_t UserId);
	bool RequestThreadPool(uint16_t UserId,uint8_t thread_number,Public_MT_Data_Thread *Data,int8_t nPool,bool Exclusive);
	bool ReleaseThreadPool(uint16_t UserId);
	bool StartThreads(uint16_t UserId);
	bool WaitThreadsEnd(uint16_t UserId);
	bool GetThreadPoolStatus(uint16_t UserId,int8_t nPool);
	uint8_t GetCurrentThreadAllocated(uint16_t UserId,int8_t nPool);
	uint8_t GetCurrentThreadUsed(uint16_t UserId,int8_t nPool);
	uint8_t GetLogicalCPUNumber(void);
	uint8_t GetPhysicalCoreNumber(void);

	bool GetThreadPoolInterfaceStatus(void) {return(Status_Ok);}
	int8_t GetCurrentPoolCreated(void) {return((Status_Ok) ? NbrePool:-1);}

	protected :

	ThreadPoolInterface(void);

	CRITICAL_SECTION CriticalSection;
	BOOL CSectionOk;
	HANDLE JobsEnded[MAX_THREAD_POOL],ThreadPoolFree[MAX_THREAD_POOL];
	UserData TabId[MAX_USERS];
	uint16_t NbreUsers;
	HANDLE EndExclusive;

	bool Status_Ok;
	bool ThreadPoolRequested[MAX_THREAD_POOL],JobsRunning[MAX_THREAD_POOL];
	bool ExclusiveMode;
	uint8_t NbrePool,NbrePoolEvent;

	bool CreatePoolEvent(uint8_t num);
	void FreeData(void);
	void FreePool(void);
	bool EnterCS(void);
	void LeaveCS(void);
	
	private :

	ThreadPoolInterface (const ThreadPoolInterface &other);
	ThreadPoolInterface& operator = (const ThreadPoolInterface &other);
	bool operator == (const ThreadPoolInterface &other) const;
	bool operator != (const ThreadPoolInterface &other) const;
};


namespace ThreadPoolDLL
{

class ThreadPoolInterfaceDLL
{
	public:

	static THREADPOOLDLL_API ThreadPoolInterface* Init(uint8_t num);
};

}

#endif // __ThreadPoolDLL_H__
