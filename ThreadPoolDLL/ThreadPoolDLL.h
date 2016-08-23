#ifdef THREADPOOLDLL_EXPORTS
#define THREADPOOLDLL_API __declspec(dllexport) 
#else
#define THREADPOOLDLL_API __declspec(dllimport) 
#endif

#include <stdint.h>

#define MAX_MT_THREADS 128
#define MAX_THREAD_POOL 4

typedef void (*ThreadPoolFunction)(void *ptr);


typedef struct _Public_MT_Data_Thread
{
	ThreadPoolFunction pFunc;
	void *pClass;
	uint8_t f_process,thread_Id;
} Public_MT_Data_Thread;



namespace ThreadPoolDLL
{

class ThreadPoolInterface
{
	public:

	static THREADPOOLDLL_API uint8_t GetThreadNumber(uint8_t thread_number,bool logical,uint8_t nPool);
	static THREADPOOLDLL_API bool AllocateThreads(DWORD pId,uint8_t thread_number,uint8_t offset,uint8_t nPool);
	static THREADPOOLDLL_API bool DeAllocateThreads(DWORD pId,uint8_t nPool);
	static THREADPOOLDLL_API bool RequestThreadPool(DWORD pId,uint8_t thread_number,Public_MT_Data_Thread *Data,uint8_t nPool);
	static THREADPOOLDLL_API bool ReleaseThreadPool(DWORD pId,uint8_t nPool);
	static THREADPOOLDLL_API bool StartThreads(DWORD pId,uint8_t nPool);
	static THREADPOOLDLL_API bool WaitThreadsEnd(DWORD pId,uint8_t nPool);
	static THREADPOOLDLL_API bool GetThreadPoolStatus(uint8_t nPool);
	static THREADPOOLDLL_API uint8_t GetCurrentThreadAllocated(uint8_t nPool);
	static THREADPOOLDLL_API uint8_t GetCurrentThreadUsed(uint8_t nPool);
	static THREADPOOLDLL_API uint8_t GetLogicalCPUNumber(uint8_t nPool);
	static THREADPOOLDLL_API uint8_t GetPhysicalCoreNumber(uint8_t nPool);
	static THREADPOOLDLL_API void Init(void);
};

}