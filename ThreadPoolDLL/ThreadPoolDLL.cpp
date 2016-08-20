// ThreadPoolDLL.cpp : définit les fonctions exportées pour l'application DLL.
//

#include "stdafx.h"
#include "ThreadPoolDLL.h"

#define VERSION "ThreadPool 1.0.0"

using namespace std;

#define myfree(ptr) if (ptr!=NULL) { free(ptr); ptr=NULL;}
#define myCloseHandle(ptr) if (ptr!=NULL) { CloseHandle(ptr); ptr=NULL;}

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

	ThreadPool(void);
	virtual ~ThreadPool(void);

	uint8_t GetThreadNumber(uint8_t thread_number);
	bool AllocateThreads(uint8_t thread_number);
	bool RequestThreadPool(DWORD pId,uint8_t thread_number,Public_MT_Data_Thread *Data);
	bool ReleaseThreadPool(DWORD pId);
	bool StartThreads(DWORD pId);
	bool WaitThreadsEnd(DWORD pId);
	bool GetThreadPoolStatus(void) {return(Status_Ok);}
	uint8_t GetCurrentThreadAllocated(void) {return(CurrentThreadsAllocated);}
	uint8_t GetCurrentThreadUsed(void) {return(CurrentThreadsUsed);}

	private :

	MT_Data_Thread MT_Thread[MAX_MT_THREADS];
	HANDLE thds[MAX_MT_THREADS];
	DWORD tids[MAX_MT_THREADS];
	Arch_CPU CPU;
	ULONG_PTR ThreadMask[MAX_MT_THREADS];
	HANDLE ghMutex,JobsEnded,ThreadPoolFree;

	bool Status_Ok,ThreadPoolRequested,JobsRunning;
	uint8_t TotalThreadsRequested,CurrentThreadsAllocated,CurrentThreadsUsed;
	DWORD ThreadPoolRequestProcessId;
	
	static DWORD WINAPI StaticThreadpool(LPVOID lpParam);

	void FreeData(void);
	void CreateThreadPool(void);

};


static ThreadPool Threads;


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


static void CreateThreadsMasks(Arch_CPU cpu, ULONG_PTR *TabMask,uint8_t NbThread)
{
	memset(TabMask,0,NbThread*sizeof(ULONG_PTR));

	if ((cpu.NbLogicCPU==0) || (cpu.NbPhysCore==0)) return;

	uint8_t current_thread=0;

	for(uint8_t i=0; i<cpu.NbPhysCore; i++)
	{
		uint8_t Nb_Core_Th=NbThread/cpu.NbPhysCore+( ((NbThread%cpu.NbPhysCore)>i) ? 1:0 );

		if (Nb_Core_Th>0)
		{
			for(uint8_t j=0; j<Nb_Core_Th; j++)
				TabMask[current_thread++]=GetCPUMask(cpu.ProcMask[i],j%cpu.NbHT[i]);
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


ThreadPool::ThreadPool(void)
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
	ghMutex=NULL;
	JobsEnded=NULL;
	ThreadPoolFree=NULL;
	TotalThreadsRequested=0;
	CurrentThreadsAllocated=0;
	CurrentThreadsUsed=0;
	JobsRunning=false;
	ThreadPoolRequested=false;
	Status_Ok=true;

	Get_CPU_Info(CPU);
	if ((CPU.NbLogicCPU==0) || (CPU.NbPhysCore==0))
	{
		Status_Ok=false;
		return;
	}

	ghMutex=CreateMutex(NULL,FALSE,NULL);
	if (ghMutex==NULL)
	{
		Status_Ok=false;
		return;
	}

	JobsEnded=CreateEvent(NULL,TRUE,TRUE,NULL);
	if (JobsEnded==NULL)
	{
		Status_Ok=false;
		FreeData();
		return;
	}

	ThreadPoolFree=CreateEvent(NULL,TRUE,TRUE,NULL);
	if (ThreadPoolFree==NULL)
	{
		Status_Ok=false;
		FreeData();
	}
}


ThreadPool::~ThreadPool(void)
{
	FreeData();
}


void ThreadPool::FreeData(void) 
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
		myCloseHandle(thds[i]);

	for (i=TotalThreadsRequested-1; i>=0; i--)
	{
		myCloseHandle(MT_Thread[i].nextJob);
		myCloseHandle(MT_Thread[i].jobFinished);
	}

	myCloseHandle(ThreadPoolFree);
	myCloseHandle(JobsEnded);
	myCloseHandle(ghMutex);
}


uint8_t ThreadPool::GetThreadNumber(uint8_t thread_number)
{
	if (thread_number==0) return((CPU.NbLogicCPU>MAX_MT_THREADS) ? MAX_MT_THREADS:CPU.NbLogicCPU);
	else return(thread_number);
}


bool ThreadPool::AllocateThreads(uint8_t thread_number)
{
	if (!Status_Ok) return(false);

	WaitForSingleObject(ghMutex,INFINITE);

	if (thread_number>CurrentThreadsAllocated)
	{
		TotalThreadsRequested=thread_number;
		while (JobsRunning)
		{
			ReleaseMutex(ghMutex);
			WaitForSingleObject(JobsEnded,INFINITE);
			WaitForSingleObject(ghMutex,INFINITE);
		}
		CreateThreadPool();
		if (!Status_Ok) return(false);
	}

	ReleaseMutex(ghMutex);

	return(true);
}


void ThreadPool::CreateThreadPool(void)
{
	int16_t i;

	if (CurrentThreadsAllocated>0)
	{
		for(i=0; i<CurrentThreadsAllocated; i++)
			SuspendThread(thds[i]);
	}

	CreateThreadsMasks(CPU,ThreadMask,TotalThreadsRequested);

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
		ReleaseMutex(ghMutex);
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
		ReleaseMutex(ghMutex);
		FreeData();
		return;
	}

	CurrentThreadsAllocated=TotalThreadsRequested;
}


bool ThreadPool::RequestThreadPool(DWORD pId,uint8_t thread_number,Public_MT_Data_Thread *Data)
{
	if (!Status_Ok) return(false);

	WaitForSingleObject(ghMutex,INFINITE);

	if (thread_number>CurrentThreadsAllocated)
	{
		ReleaseMutex(ghMutex);
		return(false);
	}

	while (ThreadPoolRequested)
	{
		ReleaseMutex(ghMutex);
		WaitForSingleObject(ThreadPoolFree,INFINITE);
		WaitForSingleObject(ghMutex,INFINITE);
	}

	for(uint8_t i=0; i<thread_number; i++)
		MT_Thread[i].MTData=Data+i;

	CurrentThreadsUsed=thread_number;

	ThreadPoolRequestProcessId=pId;

	ThreadPoolRequested=true;
	ResetEvent(ThreadPoolFree);

	ReleaseMutex(ghMutex);

	return(true);	
}

bool ThreadPool::ReleaseThreadPool(DWORD pId)
{
	if (!Status_Ok) return(false);

	WaitForSingleObject(ghMutex,INFINITE);

	if (ThreadPoolRequestProcessId!=pId)
	{
		ReleaseMutex(ghMutex);
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

	ReleaseMutex(ghMutex);

	return(true);
}


bool ThreadPool::StartThreads(DWORD pId)
{
	if (!Status_Ok) return(false);

	WaitForSingleObject(ghMutex,INFINITE);

	if ((!ThreadPoolRequested) || (CurrentThreadsUsed==0) || (ThreadPoolRequestProcessId!=pId))
	{
		ReleaseMutex(ghMutex);
		return(false);
	}

	if (JobsRunning)
	{
		ReleaseMutex(ghMutex);
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

	ReleaseMutex(ghMutex);
	return(true);	
}


bool ThreadPool::WaitThreadsEnd(DWORD pId)
{
	if (!Status_Ok) return(false);

	WaitForSingleObject(ghMutex,INFINITE);

	if ((!ThreadPoolRequested) || (CurrentThreadsUsed==0) || (ThreadPoolRequestProcessId!=pId))
	{
		ReleaseMutex(ghMutex);
		return(false);
	}

	if (!JobsRunning)
	{
		ReleaseMutex(ghMutex);
		return(true);
	}

	for(uint8_t i=0; i<CurrentThreadsUsed; i++)
		WaitForSingleObject(MT_Thread[i].jobFinished,INFINITE);

	for(uint8_t i=0; i<CurrentThreadsUsed; i++)
		MT_Thread[i].f_process=0;

	JobsRunning=false;
	SetEvent(JobsEnded);

	ReleaseMutex(ghMutex);

	return(true);
}


namespace ThreadPoolDLL
{
	uint8_t ThreadPoolInterface::GetThreadNumber(uint8_t thread_number)
		{return(Threads.GetThreadNumber(thread_number));}
	bool ThreadPoolInterface::AllocateThreads(uint8_t thread_number)
		{return(Threads.AllocateThreads(thread_number));}
	bool ThreadPoolInterface::RequestThreadPool(DWORD pId,uint8_t thread_number,Public_MT_Data_Thread *Data)
		{return(Threads.RequestThreadPool(pId,thread_number,Data));}
	bool ThreadPoolInterface::ReleaseThreadPool(DWORD pId)
		{return(Threads.ReleaseThreadPool(pId));}
	bool ThreadPoolInterface::StartThreads(DWORD pId)
		{return(Threads.StartThreads(pId));}
	bool ThreadPoolInterface::WaitThreadsEnd(DWORD pId)
		{return(Threads.WaitThreadsEnd(pId));}
	bool ThreadPoolInterface::GetThreadPoolStatus(void)
		{return(Threads.GetThreadPoolStatus());}
	uint8_t ThreadPoolInterface::GetCurrentThreadAllocated(void)
		{return(Threads.GetCurrentThreadAllocated());}
	uint8_t ThreadPoolInterface::GetCurrentThreadUsed(void)
		{return(Threads.GetCurrentThreadUsed());}
}
