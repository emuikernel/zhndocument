#ifndef _ThreadPool_H_
#define _ThreadPool_H_

#pragma warning(disable: 4530)
#pragma warning(disable: 4786)

#include <cassert>
#include <vector>
#include <queue>
#include <windows.h>

using namespace std;

class ThreadJob		//������࣬�û�����������������
{
public:
	//���̳߳ص��õ��麯�������û�����
	virtual void DoJob(void *pPara) = 0;
};

//�̳߳�
class ThreadPool
{
public:

	//����������̳߳��е��߳���Ҫ���������
	struct JobItem 
	{
		void (*_pFunc)(void *);		 //�����������û�����
		void *_pPara;				 //�������Ĳ��������û�����

		JobItem(void (*pFunc)(void *) = NULL, void *pPara = NULL) : _pFunc(pFunc), _pPara(pPara) 
		{ 

		};
	};

	//�̳߳��е��̰߳�װ�ṹ���߳�����ڱ���һ���̵߳��й���Ϣ
	struct ThreadItem
	{
		HANDLE _Handle;				//�߳��ں˶���ľ��
		ThreadPool *_pThreadPool;	//�߳������̳߳ص�ָ��
		DWORD _dwLastBeginTime;		//���һ�δ�������Ŀ�ʼʱ��
		DWORD _dwCount;				//�̴߳���������ĸ���
		bool _fIsRunning;			//�߳��Ƿ����ڴ������������ָʾ�߳��Ƿ�����

		ThreadItem(ThreadPool *pThreadPool = NULL) : _pThreadPool(pThreadPool), _Handle(NULL), _dwLastBeginTime(0), 
			_dwCount(0), _fIsRunning(false) 
		{ 

		};

		~ThreadItem()
		{
			if(_Handle)
			{
				CloseHandle(_Handle);	//�ͷ��߳��ں˶���ľ��
				_Handle = NULL;
			}
		}
	};

public:
	std::queue<JobItem *> _JobQueue;			//��������У��ö����е�������JobItem�ᰴ��FIFO���̳߳��е��߳̽��д���
	std::vector<ThreadItem *> _ThreadVector;	//�̳߳أ�������һϵ�е��߳���߳�����Ƕ��̵߳İ�װ

	CRITICAL_SECTION _csThreadVector;	 //���ڶ��߳���vector�������л��� 
	CRITICAL_SECTION _csWorkQueue;		 //���ڶ���������еĲ������л���

	HANDLE _EventEnd;			//����֪ͨ
	HANDLE _EventComplete;		//�¼��ں˶�������ָʾ�Ƿ��������
	HANDLE _SemaphoreCall;		//�����źţ�ָʾ����������Ƿ�����������Ҫ���д���
	HANDLE _SemaphoreDel;		//ɾ���߳��ź�

	long _lThreadNum;		//�̳߳����ܵ��߳���	
	long _lRunningNum;		//�̳߳��е�ǰ���ڴ�����������߳�����ע�ⲻ���������е��߳���

public:
	//dwNumΪ�̳߳����߳�������ȱʡ����Ϊ4
	ThreadPool(DWORD dwThreadNum = 4) : _lThreadNum(0), _lRunningNum(0) 
	{
		InitializeCriticalSection(&_csThreadVector);
		InitializeCriticalSection(&_csWorkQueue);

		//CreateEvent�����������Զ����ã���ʼ״̬Ϊδ�������¼��ں˶���
		_EventComplete = CreateEvent(0, false, false, NULL);
		_EventEnd = CreateEvent(0, true, false, NULL);

		//�����߳�ͬ������ʼ��Դ����Ϊ0������ǰ�ź�������δ����״������Wait Function���߳̽��ᱻ����
		_SemaphoreCall = CreateSemaphore(0, 0, 0x7FFFFFFF, NULL);
		_SemaphoreDel = CreateSemaphore(0, 0, 0x7FFFFFFF, NULL);

		assert(_SemaphoreCall != INVALID_HANDLE_VALUE);
		assert(_EventComplete != INVALID_HANDLE_VALUE);
		assert(_EventEnd != INVALID_HANDLE_VALUE);
		assert(_SemaphoreDel != INVALID_HANDLE_VALUE);

		AdjustSize(dwThreadNum <= 0 ? 4 : dwThreadNum);
	}
	~ThreadPool()
	{
		DeleteCriticalSection(&_csWorkQueue);

		CloseHandle(_EventEnd);
		CloseHandle(_EventComplete);
		CloseHandle(_SemaphoreCall);
		CloseHandle(_SemaphoreDel);

		vector<ThreadItem*>::iterator iter;
		for(iter = _ThreadVector.begin(); iter != _ThreadVector.end(); iter++)
		{
			if(*iter)
				delete *iter;
		}

		DeleteCriticalSection(&_csThreadVector);
	}

	//�����̳߳ع�ģ������iThreadNumΪ�̳߳����̵߳���Ŀ
	int AdjustSize(int iThreadNum)
	{
		if(iThreadNum > 0)
		{
			ThreadItem *pNew;

			//ע�⣬������Ҫ��ռ�Ķ��̳߳ؽ��в��������������Ҫ����
			EnterCriticalSection(&_csThreadVector);

			for(int _i=0; _i<iThreadNum; _i++)
			{
				_ThreadVector.push_back(pNew = new ThreadItem(this)); 

				//WIN32 API����CreateThread�����߳��ں˶��󣬲������������Ƽ���CRT����_beginthreadex
				//DefaultJobProcΪ�̺߳�����һ���̱߳���������CPU���ȣ����ִ���������
				pNew->_Handle = CreateThread(NULL, 0, DefaultJobProc, pNew, 0, NULL);

				// �����̵߳����ȼ�
				SetThreadPriority(pNew->_Handle, THREAD_PRIORITY_BELOW_NORMAL);
			}

			LeaveCriticalSection(&_csThreadVector);
		}
		else
		{
			iThreadNum *= -1;
			ReleaseSemaphore(_SemaphoreDel, iThreadNum > _lThreadNum ? _lThreadNum : iThreadNum, NULL);
		}

		return (int)_lThreadNum;
	}

	//��¶���û���API
	//�����̳߳��е��̶߳��û�ָ����������д���
	void Call(void (*pFunc)(void *), void *pPara = NULL)
	{
		assert(pFunc);

		EnterCriticalSection(&_csWorkQueue);

		//��һ���û��������������뵽�̳߳ص����������
		_JobQueue.push(new JobItem(pFunc, pPara));

		LeaveCriticalSection(&_csWorkQueue);

		//ָʾ�̳߳��е��߳���һ����������Ҫ���д�����
		//�������ĸ��߳̽��д�������CPU���Ȼ��ƾ������û�����Ҫ����
		ReleaseSemaphore(_SemaphoreCall, 1, NULL);	 
	}

	//�����̳߳�
	inline void Call(ThreadJob * p, void *pPara = NULL)
	{
		Call(CallProc, new CallProcPara(p, pPara));
	}
	//�����̳߳�, ��ͬ���ȴ�
	bool EndAndWait(DWORD dwWaitTime = INFINITE)
	{
		SetEvent(_EventEnd);
		return WaitForSingleObject(_EventComplete, dwWaitTime) == WAIT_OBJECT_0;
	}
	//�����̳߳�
	inline void End()
	{
		SetEvent(_EventEnd);
	}
	inline DWORD Size()
	{
		return (DWORD)_lThreadNum;
	}
	inline DWORD GetRunningSize()
	{
		return (DWORD)_lRunningNum;
	}
	bool IsRunning()
	{
		return _lRunningNum > 0;
	}

public:
	//�̴߳������������Ǿ�̬��Ա����
	static DWORD WINAPI DefaultJobProc(LPVOID lpParameter = NULL)
	{
		ThreadItem *pThread = static_cast<ThreadItem*>(lpParameter);
		assert(pThread);

		ThreadPool *pThreadPoolObj = pThread->_pThreadPool;	//�õ����߳������̳߳ص�ָ��
		assert(pThreadPoolObj);

		InterlockedIncrement(&pThreadPoolObj->_lThreadNum);

		HANDLE hWaitHandle[3];
		hWaitHandle[0] = pThreadPoolObj->_SemaphoreCall;
		hWaitHandle[1] = pThreadPoolObj->_SemaphoreDel;
		hWaitHandle[2] = pThreadPoolObj->_EventEnd;

		JobItem * pJob;		//��ǰ�̴߳����������
		bool fHasJob;		//��ǰ�̳߳ص�������������Ƿ���������

		while(true)
		{
			//�ú������ص�ԭ��������
			//1��_SemaphoreCall��Ϊ����̬�������̵߳�����ReleaseSemaphore
			//2��_SemaphoreDel��Ϊ����̬
			//3��_EventEnd�����������̵߳�����SetEvent
			DWORD wr = WaitForMultipleObjects(3, hWaitHandle, false, INFINITE);	

			//��Ӧɾ���߳��ź�
			if(wr == WAIT_OBJECT_0 + 1) 
			{
				break;
			}

			//���������������FIFOȡ��һ��������
			EnterCriticalSection(&pThreadPoolObj->_csWorkQueue);

			fHasJob = !pThreadPoolObj->_JobQueue.empty();
			if(fHasJob)
			{
				pJob = pThreadPoolObj->_JobQueue.front();
				pThreadPoolObj->_JobQueue.pop();			//����������̳߳ص���������е���

				assert(pJob);
			}

			LeaveCriticalSection(&pThreadPoolObj->_csWorkQueue);

			//�յ������߳��źţ��ж��Ƿ�����߳�
			//ֻ�е��̳߳ص����������Ϊ��ʱ�Ż���ֹ�߳�
			if(wr == WAIT_OBJECT_0 + 2 && !fHasJob) 
			{
				break;
			}

			if(fHasJob && pJob)		//���¼������������������
			{
				InterlockedIncrement(&pThreadPoolObj->_lRunningNum);

				pThread->_dwLastBeginTime = GetTickCount();
				pThread->_dwCount++;
				pThread->_fIsRunning = true;

				pJob->_pFunc(pJob->_pPara);		//�����û�������

				delete pJob;
				pJob = NULL;

				pThread->_fIsRunning = false;

				InterlockedDecrement(&pThreadPoolObj->_lRunningNum);


				break;	//add for testing
			}
		}

		//�̼߳�����ֹ����Ҫ���������е���Դ
		//ɾ������ṹ
		EnterCriticalSection(&pThreadPoolObj->_csThreadVector);

		//���̳߳���ɾ������ʹ��STL�㷨find
		pThreadPoolObj->_ThreadVector.erase(find(pThreadPoolObj->_ThreadVector.begin(), pThreadPoolObj->_ThreadVector.end(), pThread));

		LeaveCriticalSection(&pThreadPoolObj->_csThreadVector);

		delete pThread;
		pThread = NULL;

		InterlockedDecrement(&pThreadPoolObj->_lThreadNum);

		if(!pThreadPoolObj->_lThreadNum)	//�̳߳����Ѿ�û���κ��߳��������
		{
			//SetEvent(pThreadPoolObj->_EventComplete);	//�����¼��ں˶���
		}

		return 0;
	}

	//�����û������麯��
	static void CallProc(void *pPara) 
	{
		CallProcPara *cp = static_cast<CallProcPara *>(pPara);
		assert(cp);

		if(cp)
		{
			cp->_pObj->DoJob(cp->_pPara);	//��̬����

			delete cp;
			cp = NULL;
		}
	}

	//�û�����ṹ
	struct CallProcPara 
	{
		ThreadJob* _pObj;		//�û����� 
		void *_pPara;			//�û�����

		CallProcPara(ThreadJob* p = NULL, void *pPara = NULL) : _pObj(p), _pPara(pPara) 
		{

		};
	};
};

#endif