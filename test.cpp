#define _CRT_SECURE_NO_WARNINGS 1
#include "PerformanceProFiler.h"
#include <thread>

//void test1()
//{
//	PerformanceProfiler pp;
//	PPSection* s1 = pp.CreateSection(__FILE__, __FUNCTION__, __LINE__, "数据库");
//	s1->Begin();
//	Sleep(500);
//	s1->End();
//
//	PPSection* s2 = pp.CreateSection(__FILE__, __FUNCTION__, __LINE__, "网络");
//	s2->Begin();
//	Sleep(1000);
//	s2->End();
//	pp.Output();
//}



void Test()
{
	PERFORMANCE_PROFILER_EE_BEGIN(PP1, "PP1");
	
	Sleep(1000);

	PERFORMANCE_PROFILER_EE_END(PP1);

	PERFORMANCE_PROFILER_EE_BEGIN(PP2, "PP2");

	Sleep(500);

	PERFORMANCE_PROFILER_EE_END(PP2);
}

//使用引用计数来解决递归问题，递归时如果每次都更新时间，
//时间就不会累加每次都会初始化为0，导致运行结果出错，累加时间也会出错，
//所以只有当引用计数为零时初始化，累加时也只有当引用计数为0时初始化。

void Run(int n)
{
	while (n--)
	{
		PERFORMANCE_PROFILER_EE_BEGIN(network, "网络传输");
		Sleep(1000);
		PERFORMANCE_PROFILER_EE_END(network);

		PERFORMANCE_PROFILER_EE_BEGIN(mid, "中间逻辑");
		Sleep(500);
		PERFORMANCE_PROFILER_EE_END(mid);

		PERFORMANCE_PROFILER_EE_BEGIN(sql, "数据库");
		Sleep(500);
		PERFORMANCE_PROFILER_EE_END(sql);
	}
}

void testThread()
{
	thread t1(Run, 3);
	thread t2(Run, 2);
	thread t3(Run, 1);

	t1.join();
	t2.join();
	t3.join();
}

int main()
{
	//test1();
	//Test();
	testThread();
	PerformanceProfiler::GetInstance()->OutPut();
	system("pause");
	return 0;
}
