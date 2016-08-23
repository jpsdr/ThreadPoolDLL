// ThreadPoolDLL.cpp : définit les fonctions exportées pour l'application DLL.
//

#include "stdafx.h"
#include "ThreadPoolDLL.h"

#define VERSION "ThreadPool 1.0.1"

using namespace std;

#define myfree(ptr) if (ptr!=NULL) { free(ptr); ptr=NULL;}
#define myCloseHandle(ptr) if (ptr!=NULL) { CloseHandle(ptr); ptr=NULL;}

//#define MAX_USERS 500

typedef struct _MT_Data_Thread
{
	Public_MT_Data_Thread *MTData;
	uint8_t f_process,thread_Id;
	HANDLE nextJob, jobFinished;
} MT_Data_Thread;


typedef struct _Arch_CPU
{
	uint8_t NbPhysCore,NbLogicCPU;
	uint8_t NbHT[64];
	ULONG_PTR ProcMask[64];
} Arch_CPU;


class ThreadPool
{
	public :

	virtual ~ThreadPool(void);
	static void Init(void);

	protected :

	Arch_CPU CPU;

	public :
	uint8_t GetThreadNumber(uint8_t thread_number,bool logical);
	bool AllocateThreads(DWORD pId,uint8_t thread_number,uint8_t offset);
	bool DeAllocateThreads(DWORD pId);
	bool RequestThreadPool(DWORD pId,uint8_t thread_number,Public_MT_Data_Thread *Data);
	bool ReleaseThreadPool(DWORD pId);
	bool StartThreads(DWORD pId);
	bool WaitThreadsEnd(DWORD pId);
	bool GetThreadPoolStatus(void) {return(Status_Ok);}
	uint8_t GetCurrentThreadAllocated(void) {return(CurrentThreadsAllocated);}
	uint8_t GetCurrentThreadUsed(void) {return(CurrentThreadsUsed);}
	uint8_t GetLogicalCPUNumber(void) {return(CPU.NbLogicCPU);}
	uint8_t GetPhysicalCoreNumber(void) {return(CPU.NbPhysCore);}

	protected :

	ThreadPool(void);

	MT_Data_Thread MT_Thread[MAX_MT_THREADS];
	HANDLE thds[MAX_MT_THREADS];
	DWORD tids[MAX_MT_THREADS];
	ULONG_PTR ThreadMask[MAX_MT_THREADS];
	CRITICAL_SECTION CriticalSection;
	BOOL CSectionOk;
	HANDLE JobsEnded,ThreadPoolFree;
//	DWORD TabId[MAX_USERS];

	bool Status_Ok,ThreadPoolRequested,JobsRunning;
	uint8_t TotalThreadsRequested,CurrentThreadsAllocated,CurrentThreadsUsed;
	DWORD ThreadPoolRequestProcessId;
	uint16_t NbreUsers;
	
	void FreeData(void);
	void FreeThreadPool(void);
	void CreateThreadPool(uint8_t offset);

	private :

	static DWORD WINAPI StaticThreadpool(LPVOID lpParam);

	ThreadPool (const ThreadPool &other);
	ThreadPool& operator = (const ThreadPool &other);
	bool operator == (const ThreadPool &other) const;
	bool operator != (const ThreadPool &other) const;
};



// Helper function to count set bits in the processor mask.
static uint8_t CountSetBits(ULONG_PTR bitMask)
{
    DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
    uint8_t bitSetCount = 0;
    ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;    
    DWORD i;
    
    for (i = 0; i <= LSHIFT; ++i)
    {
        bitSetCount += ((bitMask & bitTest)?1:0);
        bitTest/=2;
    }

    return bitSetCount;
}


static void Get_CPU_Info(Arch_CPU& cpu)
{
    bool done = false;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer=NULL;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr=NULL;
    DWORD returnLength=0;
    uint8_t logicalProcessorCount=0;
    uint8_t processorCoreCount=0;
    DWORD byteOffset=0;

	cpu.NbLogicCPU=0;
	cpu.NbPhysCore=0;

    while (!done)
    {
        BOOL rc=GetLogicalProcessorInformation(buffer, &returnLength);

        if (rc==FALSE) 
        {
            if (GetLastError()==ERROR_INSUFFICIENT_BUFFER) 
            {
                myfree(buffer);
                buffer=(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(returnLength);

                if (buffer==NULL) return;
            } 
            else
			{
				myfree(buffer);
				return;
			}
        } 
        else done=true;
    }

    ptr=buffer;

    while ((byteOffset+sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION))<=returnLength) 
    {
        switch (ptr->Relationship) 
        {
			case RelationProcessorCore :
	            // A hyperthreaded core supplies more than one logical processor.
				cpu.NbHT[processorCoreCount]=CountSetBits(ptr->ProcessorMask);
		        logicalProcessorCount+=cpu.NbHT[processorCoreCount];
				cpu.ProcMask[processorCoreCount++]=ptr->ProcessorMask;
			    break;
			default : break;
        }
        byteOffset+=sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        ptr++;
    }
	free(buffer);

	cpu.NbPhysCore=processorCoreCount;
	cpu.NbLogicCPU=logicalProcessorCount;
}


static ULONG_PTR GetCPUMask(ULONG_PTR bitMask, uint8_t CPU_Nb)
{
    uint8_t LSHIFT=sizeof(ULONG_PTR)*8-1;
    uint8_t i=0,bitSetCount=0;
    ULONG_PTR bitTest=1;    

	CPU_Nb++;
	while (i<=LSHIFT)
	{
		if ((bitMask & bitTest)!=0) bitSetCount++;
		if (bitSetCount==CPU_Nb) return(bitTest);
		else
		{
			i++;
			bitTest<<=1;
		}
	}
	return(0);
}


static void CreateThreadsMasks(Arch_CPU cpu, ULONG_PTR *TabMask,uint8_t NbThread,uint8_t offset)
{
	memset(TabMask,0,NbThread*sizeof(ULONG_PTR));

	if ((cpu.NbLogicCPU==0) || (cpu.NbPhysCore==0)) return;

	uint8_t current_thread=0;

	for(uint8_t i=0; i<cpu.NbPhysCore; i++)
	{
		uint8_t Nb_Core_Th=NbThread/cpu.NbPhysCore+( ((NbThread%cpu.NbPhysCore)>i) ? 1:0 );

		if (Nb_Core_Th>0)
		{
			const uint8_t offs=(cpu.NbHT[i]>offset) ? offset:cpu.NbHT[i]-1;

			for(uint8_t j=0; j<Nb_Core_Th; j++)
				TabMask[current_thread++]=GetCPUMask(cpu.ProcMask[i],(j+offs)%cpu.NbHT[i]);
		}
	}
}


DWORD WINAPI ThreadPool::StaticThreadpool(LPVOID lpParam )
{
	MT_Data_Thread *data=(MT_Data_Thread *)lpParam;
	
	while (true)
	{
		WaitForSingleObject(data->nextJob,INFINITE);
		switch(data->f_process)
		{
			case 1 :
				if (data->MTData!=NULL)
				{
					data->MTData->thread_Id=data->thread_Id;
					data->MTData->pFunc(data->MTData);
				}
				break;
			case 255 : return(0); break;
			default : break;
		}
		ResetEvent(data->nextJob);
		SetEvent(data->jobFinished);
	}
}


ThreadPool::ThreadPool(void):Status_Ok(true)
{
	int16_t i;

	for (i=0; i<MAX_MT_THREADS; i++)
	{
		MT_Thread[i].MTData=NULL;
		MT_Thread[i].f_process=0;
		MT_Thread[i].thread_Id=(uint8_t)i;
		MT_Thread[i].jobFinished=NULL;
		MT_Thread[i].nextJob=NULL;
		thds[i]=NULL;
	}
//	memset(TabId,0,MAX_USERS*sizeof(DWORD));
	CSectionOk=FALSE;
	JobsEnded=NULL;
	ThreadPoolFree=NULL;
	TotalThreadsRequested=0;
	CurrentThreadsAllocated=0;
	CurrentThreadsUsed=0;
	NbreUsers=0;
	JobsRunning=false;
	ThreadPoolRequested=false;

	Get_CPU_Info(CPU);
	if ((CPU.NbLogicCPU==0) || (CPU.NbPhysCore==0))
	{
		Status_Ok=false;
		return;
	}

	CSectionOk=InitializeCriticalSectionAndSpinCount(&CriticalSection,0x00000040);
	if (CSectionOk==FALSE)
	{
		Status_Ok=false;
		return;
	}

	JobsEnded=CreateEvent(NULL,TRUE,TRUE,NULL);
	if (JobsEnded==NULL)
	{
		FreeData();
		Status_Ok=false;
		return;
	}

	ThreadPoolFree=CreateEvent(NULL,TRUE,TRUE,NULL);
	if (ThreadPoolFree==NULL)
	{
		FreeData();
		Status_Ok=false;
	}
}


ThreadPool::~ThreadPool(void)
{
	Status_Ok=false;
	FreeData();
}


void ThreadPool::FreeThreadPool(void) 
{
	int16_t i;

	for (i=TotalThreadsRequested-1; i>=0; i--)
	{
		if (thds[i]!=NULL)
		{
			MT_Thread[i].f_process=255;
			SetEvent(MT_Thread[i].nextJob);
			WaitForSingleObject(thds[i],INFINITE);
			myCloseHandle(thds[i]);
		}
	}

	for (i=TotalThreadsRequested-1; i>=0; i--)
	{
		myCloseHandle(MT_Thread[i].nextJob);
		myCloseHandle(MT_Thread[i].jobFinished);
	}

	TotalThreadsRequested=0;
	CurrentThreadsAllocated=0;
	CurrentThreadsUsed=0;
	JobsRunning=false;
	ThreadPoolRequested=false;
}


void ThreadPool::FreeData(void) 
{
	myCloseHandle(ThreadPoolFree);
	myCloseHandle(JobsEnded);
	if (CSectionOk==TRUE)
	{
		DeleteCriticalSection(&CriticalSection);
		CSectionOk=FALSE;
	}
}


uint8_t ThreadPool::GetThreadNumber(uint8_t thread_number,bool logical)
{
	const uint8_t nCPU=(logical) ? CPU.NbLogicCPU:CPU.NbPhysCore;

	if (thread_number==0) return((nCPU>MAX_MT_THREADS) ? MAX_MT_THREADS:nCPU);
	else return(thread_number);
}


bool ThreadPool::AllocateThreads(DWORD pId,uint8_t thread_number,uint8_t offset)
{
	if (!Status_Ok) return(false);

	EnterCriticalSection(&CriticalSection);

	if (thread_number==0)
	{
		LeaveCriticalSection(&CriticalSection);
		return(false);
	}

/*	if (NbreUsers>=MAX_USERS)
	{
		ReleaseMutex(ghMutex);
		return(false);
	}

	uint16_t i=0;
	while ((NbreUsers>i) && (TabId[i]!=pId)) i++;
	if (i==NbreUsers)
	{
		TabId[i]=pId;
		NbreUsers++;
	}*/
	NbreUsers++;

	if (thread_number>CurrentThreadsAllocated)
	{
		TotalThreadsRequested=thread_number;
		while (JobsRunning)
		{
			LeaveCriticalSection(&CriticalSection);
			WaitForSingleObject(JobsEnded,INFINITE);
			EnterCriticalSection(&CriticalSection);
		}
		CreateThreadPool(offset);
		if (!Status_Ok) return(false);
	}

	LeaveCriticalSection(&CriticalSection);

	return(true);
}


bool ThreadPool::DeAllocateThreads(DWORD pId)
{
	if (!Status_Ok) return(false);

	EnterCriticalSection(&CriticalSection);

	if (NbreUsers==0)
	{
		LeaveCriticalSection(&CriticalSection);
		return(true);
	}

/*	uint16_t i=0;
	while ((NbreUsers>i) && (TabId[i]!=pId)) i++;
	if (i==NbreUsers)
	{
		ReleaseMutex(ghMutex);
		return(false);
	}

	if (i<NbreUsers-1)
	{
		for(uint16_t j=i+1; j<NbreUsers; j++)
			TabId[j-1]=TabId[j];
	}
	NbreUsers--;
	TabId[NbreUsers]=0;*/
	NbreUsers--;

	if (NbreUsers==0) FreeThreadPool();

	LeaveCriticalSection(&CriticalSection);

	return(true);
}



void ThreadPool::CreateThreadPool(uint8_t offset)
{
	int16_t i;

	if (CurrentThreadsAllocated>0)
	{
		for(i=0; i<CurrentThreadsAllocated; i++)
			SuspendThread(thds[i]);
	}

	CreateThreadsMasks(CPU,ThreadMask,TotalThreadsRequested,offset);

	for(i=0; i<CurrentThreadsAllocated; i++)
	{
		SetThreadAffinityMask(thds[i],ThreadMask[i]);
		ResumeThread(thds[i]);
	}

	i=CurrentThreadsAllocated;
	while ((i<TotalThreadsRequested) && Status_Ok)
	{
		MT_Thread[i].jobFinished=CreateEvent(NULL,TRUE,TRUE,NULL);
		MT_Thread[i].nextJob=CreateEvent(NULL,TRUE,FALSE,NULL);
		Status_Ok=Status_Ok && ((MT_Thread[i].jobFinished!=NULL) && (MT_Thread[i].nextJob!=NULL));
		i++;
	}
	if (!Status_Ok)
	{
		FreeThreadPool();
		LeaveCriticalSection(&CriticalSection);
		FreeData();
		return;
	}

	i=CurrentThreadsAllocated;
	while ((i<TotalThreadsRequested) && Status_Ok)
	{
		thds[i]=CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)StaticThreadpool,&MT_Thread[i],CREATE_SUSPENDED,&tids[i]);
		Status_Ok=Status_Ok && (thds[i]!=NULL);
		if (Status_Ok)
		{
			SetThreadAffinityMask(thds[i],ThreadMask[i]);
			ResumeThread(thds[i]);
		}
		i++;
	}
	if (!Status_Ok)
	{
		FreeThreadPool();
		LeaveCriticalSection(&CriticalSection);
		FreeData();
		return;
	}

	CurrentThreadsAllocated=TotalThreadsRequested;
}


bool ThreadPool::RequestThreadPool(DWORD pId,uint8_t thread_number,Public_MT_Data_Thread *Data)
{
	if (!Status_Ok) return(false);

	EnterCriticalSection(&CriticalSection);

	if (thread_number>CurrentThreadsAllocated)
	{
		LeaveCriticalSection(&CriticalSection);
		return(false);
	}

	while (ThreadPoolRequested)
	{
		LeaveCriticalSection(&CriticalSection);
		WaitForSingleObject(ThreadPoolFree,INFINITE);
		EnterCriticalSection(&CriticalSection);
	}

	for(uint8_t i=0; i<thread_number; i++)
		MT_Thread[i].MTData=Data+i;

	CurrentThreadsUsed=thread_number;

	ThreadPoolRequestProcessId=pId;

	ThreadPoolRequested=true;
	ResetEvent(ThreadPoolFree);

	LeaveCriticalSection(&CriticalSection);

	return(true);	
}


bool ThreadPool::ReleaseThreadPool(DWORD pId)
{
	if (!Status_Ok) return(false);

	EnterCriticalSection(&CriticalSection);

	if (ThreadPoolRequestProcessId!=pId)
	{
		LeaveCriticalSection(&CriticalSection);
		return(false);
	}

	if (ThreadPoolRequested)
	{
		for(uint8_t i=0; i<CurrentThreadsUsed; i++)
			MT_Thread[i].MTData=NULL;
		CurrentThreadsUsed=0;
		ThreadPoolRequested=false;
		ThreadPoolRequestProcessId=0;
		SetEvent(ThreadPoolFree);
	}

	LeaveCriticalSection(&CriticalSection);

	return(true);
}


bool ThreadPool::StartThreads(DWORD pId)
{
	if (!Status_Ok) return(false);

	EnterCriticalSection(&CriticalSection);

	if ((!ThreadPoolRequested) || (CurrentThreadsUsed==0) || (ThreadPoolRequestProcessId!=pId))
	{
		LeaveCriticalSection(&CriticalSection);
		return(false);
	}

	if (JobsRunning)
	{
		LeaveCriticalSection(&CriticalSection);
		return(true);
	}

	JobsRunning=true;
	ResetEvent(JobsEnded);

	for(uint8_t i=0; i<CurrentThreadsUsed; i++)
	{
		MT_Thread[i].f_process=1;
		ResetEvent(MT_Thread[i].jobFinished);
		SetEvent(MT_Thread[i].nextJob);
	}

	LeaveCriticalSection(&CriticalSection);

	return(true);	
}


bool ThreadPool::WaitThreadsEnd(DWORD pId)
{
	if (!Status_Ok) return(false);

	EnterCriticalSection(&CriticalSection);

	if ((!ThreadPoolRequested) || (CurrentThreadsUsed==0) || (ThreadPoolRequestProcessId!=pId))
	{
		LeaveCriticalSection(&CriticalSection);
		return(false);
	}

	if (!JobsRunning)
	{
		LeaveCriticalSection(&CriticalSection);
		return(true);
	}

	for(uint8_t i=0; i<CurrentThreadsUsed; i++)
		WaitForSingleObject(MT_Thread[i].jobFinished,INFINITE);

	for(uint8_t i=0; i<CurrentThreadsUsed; i++)
		MT_Thread[i].f_process=0;

	JobsRunning=false;
	SetEvent(JobsEnded);

	LeaveCriticalSection(&CriticalSection);

	return(true);
}



static ThreadPool *Threads[MAX_THREAD_POOL]={NULL,NULL,NULL,NULL};


void ThreadPool::Init(void)
{
	static ThreadPool Pool[MAX_THREAD_POOL];

	for(uint8_t i=0; i<MAX_THREAD_POOL; i++)
		Threads[i]=&Pool[i];
}


namespace ThreadPoolDLL
{
	uint8_t ThreadPoolInterface::GetThreadNumber(uint8_t thread_number,bool logical,uint8_t nPool)
	{
		if (nPool>=MAX_THREAD_POOL) nPool=0;
		if (Threads[nPool]!=NULL) return(Threads[nPool]->GetThreadNumber(thread_number,logical));
		else return(0);
	}
	bool ThreadPoolInterface::AllocateThreads(DWORD pId,uint8_t thread_number,uint8_t offset,uint8_t nPool)
	{
		if (nPool>=MAX_THREAD_POOL) nPool=0;
		if (Threads[nPool]!=NULL) return(Threads[nPool]->AllocateThreads(pId,thread_number,offset));
		else return(false);
	}
	bool ThreadPoolInterface::DeAllocateThreads(DWORD pId,uint8_t nPool)
	{
		if (nPool>=MAX_THREAD_POOL) nPool=0;
		if (Threads[nPool]!=NULL) return(Threads[nPool]->DeAllocateThreads(pId));
		else return(false);
	}
	bool ThreadPoolInterface::RequestThreadPool(DWORD pId,uint8_t thread_number,Public_MT_Data_Thread *Data,uint8_t nPool)
	{
		if (nPool>=MAX_THREAD_POOL) nPool=0;
		if (Threads[nPool]!=NULL) return(Threads[nPool]->RequestThreadPool(pId,thread_number,Data));
		else return(false);
	}
	bool ThreadPoolInterface::ReleaseThreadPool(DWORD pId,uint8_t nPool)
	{
		if (nPool>=MAX_THREAD_POOL) nPool=0;
		if (Threads[nPool]!=NULL) return(Threads[nPool]->ReleaseThreadPool(pId));
		else return(false);
	}
	bool ThreadPoolInterface::StartThreads(DWORD pId,uint8_t nPool)
	{
		if (Threads[nPool]!=NULL) return(Threads[nPool]->StartThreads(pId));
		else return(false);
	}
	bool ThreadPoolInterface::WaitThreadsEnd(DWORD pId,uint8_t nPool)
	{
		if (nPool>=MAX_THREAD_POOL) nPool=0;
		if (Threads[nPool]!=NULL) return(Threads[nPool]->WaitThreadsEnd(pId));
		else return(false);
	}
	bool ThreadPoolInterface::GetThreadPoolStatus(uint8_t nPool)
	{
		if (Threads[nPool]!=NULL) return(Threads[nPool]->GetThreadPoolStatus());
		else return(false);
	}
	uint8_t ThreadPoolInterface::GetCurrentThreadAllocated(uint8_t nPool)
	{
		if (nPool>=MAX_THREAD_POOL) nPool=0;
		if (Threads[nPool]!=NULL) return(Threads[nPool]->GetCurrentThreadAllocated());
		else return(0);
	}
	uint8_t ThreadPoolInterface::GetCurrentThreadUsed(uint8_t nPool)
	{
		if (nPool>=MAX_THREAD_POOL) nPool=0;
		if (Threads[nPool]!=NULL) return(Threads[nPool]->GetCurrentThreadUsed());
		else return(0);
	}
	uint8_t ThreadPoolInterface::GetLogicalCPUNumber(uint8_t nPool)
	{
		if (nPool>=MAX_THREAD_POOL) nPool=0;
		if (Threads[nPool]!=NULL) return(Threads[nPool]->GetLogicalCPUNumber());
		else return(0);
	}
	uint8_t ThreadPoolInterface::GetPhysicalCoreNumber(uint8_t nPool)
	{
		if (nPool>=MAX_THREAD_POOL) nPool=0;
		if (Threads[nPool]!=NULL) return(Threads[nPool]->GetPhysicalCoreNumber());
		else return(0);
	}
	void ThreadPoolInterface::Init(void) {ThreadPool::Init();}
}
