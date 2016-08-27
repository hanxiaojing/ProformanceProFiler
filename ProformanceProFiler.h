#define _CRT_SECURE_NO_WARNINGS 1
#pragma once
#include <iostream>
using namespace std;
#include <time.h>
#include <map>
#include <Windows.h>
#include <assert.h>
#include <stdarg.h>
#include <algorithm>

// C++11
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

#ifdef _WIN32
#include <Windows.h>
#include<Psapi.h>
#pragma comment(lib,"Psapi.lib")
#else
#include <pthread.h>
#endif // _WIN32

static int GetThreadId()
{
#ifdef _WIN32
	return ::GetCurrentThreadId();
#else
	return ::thread_self();
#endif
}

//性能剖析其项目总结：
//1.杂项：适配器--用来打印存储信息以及时间消耗信息
//		  开启操作符--用来做选择操作是开还是关，是前台打印还是文件打印
//		  单例模式--让类只生成一个对象，使类对象的多少控制在自己手中
//2.组成核心的成员PPNode--包含文件名，函数名，line以及描述信息，还有打印信息的函数
//3.组成核心成员2---PPSection---包含所有的耗费时间以及线程如何处理的问题，设计的成员变量多达8至9个，用map<int,LongType>存储线程的引用计数
//线程总共的调用次数，耗费时间等，然后还有总共的引用计数，总共的耗费时间，总共的调用次数等
//4.最核心的部分：包含成员函数为1.构造函数，设置了程序跑完打印信息的程式
//					2.compare比较函数，决定数据打印时按照什么顺序来
//					3.Output函数，所有需要打印的信息全部由这个函数来搞定--很强大，为了线程安全都是需要加锁的
//5.begin和end宏也是很重要的：外部调用基本上都是用的它，begin和end函数设置引用计数解决多线程的思想很强，
//需要根据引用计数的有无来决定时间的开始与结束，然后还有递归问题，引用计数就有优势了

////////////////////////////////////////////////////
//开启操作符
enum PP_CONFIG_OPTION
{
	PPCO_NONE = 0,		//不做剖析
	PPCO_PROFILER = 2,	//开启剖析
	PPCO_SAVE_TO_CONSOLE = 4,	//保存到控制台
	PPCO_SAVE_TO_FILE = 8,		//保存到文件
	PPCO_SAVE_BY_CALL_COUNT = 16,	//按调用次数降序保存
	PPCO_SAVE_BY_COST_TIME = 32,	//按调用花费时间降序保存

};

////单例基类
template<class T>
class Singleton
{
public:
	static T* GetInstance()
	{
		if (_sInstance == NULL)
		{
			unique_lock<mutex> lock(_mutex);
			if (_sInstance == NULL)
			{
				_sInstance = new T();
			}
		}

		return _sInstance;
	}
protected:
	Singleton()
	{}

	static T* _sInstance;
	static mutex _mutex;
};

template<class T>
T* Singleton<T>::_sInstance = NULL;

template<class T>
mutex Singleton<T>::_mutex;

////饿汉模式
//template<class T>
//class Singleton
//{
//public:
//	static T* GetInstance()
//	{
//		assert(_sInstance);
//		return _sInstance;
//	}
//protected:
//	static T* _sInstance;
//
//};
//
//template<class T>
//T* Singleton<T>::_sInstance = new T;

///////////////////////////////////////////////////
//配置管理，设置操作符
class ConfigManager : public Singleton<ConfigManager>
{
public:
	void SetOptions(int flag)
	{
		_flag = flag;
	}

	int GetOptions()
	{
		return _flag;
	}

	ConfigManager()
		:_flag(PPCO_PROFILER | PPCO_SAVE_TO_CONSOLE | PPCO_SAVE_TO_FILE)
	{}

private:
	int _flag;
};

typedef long long LongType;
/////////////////////////////////////////////////////////////////
//保存适配器
class SaveAdapter
{
public:
	virtual void Save(const char* fmt, ...) = 0;//纯虚函数
};

class ConsoleAdapter :public SaveAdapter//适配器的作用是什么？
{
public:
	virtual void Save(const char* format, ...)
	{	
		va_list args;
		va_start(args, format);
		vfprintf(stdout, format, args);
		va_end(args);
	}
};

class FileSaveAdapter :public SaveAdapter
{
public:
	FileSaveAdapter(const char* filename)
	{
		_fout = fopen(filename, "w");
		assert(_fout);
	}

	~FileSaveAdapter()
	{
		if (_fout)
		{
			fclose(_fout);
		}
	}

	virtual void Save(const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		vfprintf(_fout, format, args);
		va_end(args);
	}

protected:
	//防拷贝
	FileSaveAdapter(const FileSaveAdapter&);
	FileSaveAdapter& operator=(const FileSaveAdapter&);

protected:
	FILE* _fout;
};

struct PPNode
{
	string _filename;
	string _function;
	int _line;
	string _desc;

	PPNode(const char* filename, const char* function, int line, const char* desc)
		:_filename(filename)
		, _function(function)
		, _line(line)
		, _desc(desc)
	{}

	bool operator<(const PPNode& node)const
	{
		if (_line < node._line)
			return true;
		else
		{
			if (_function < node._function)
				return true;
			else
				return false;
		}
	}

	bool operator==(const PPNode& p)const
	{
		return _filename == p._filename
			&& _function == p._function
			&& _line == p._line;
	}

	//打印PPNode节点信息
	void Serialize(SaveAdapter& sa)const
	{
		sa.Save("Filename:%s, Function:%s, Line:%d\n", _filename.c_str(),
			_function.c_str(), _line);
	}

};

struct PPSection
{
public:
	PPSection()
		:_beginTime(0)
		, _totalCostTime(0)
		, _totalCallCount(0)
		, _totalRefCount(0)
	{}
	////////////////////////////////////////////
	//整体框架：在主类中设置一个map<ppNode,ppSection>其中PPNode包含文件名，函数名，当前行，描述信息，PPSection包含了开始时间，调用次数，花费时间，每个线程花费的时间，每个线程的次数，引用技术器
	//每次每次定义一个这个对象，都需要先调用构造函数,其中__FUNCTION__会定位到当前函数,、然后当前函数的数据进行操作，BEGIN函数计算开始时间，以及引用计数，调用次数，END函数计算引用计数，花费时间
	///////////////////////////////////////////////////

	//线程打印信息
	void Serialize(SaveAdapter& sa)
	{
		//如果总的引用计数不等于0，表示剖析段不匹配
		if (_totalRefCount)
			sa.Save("Performance Profiler Not Match!\n");
		//序列化效率统计信息
		auto costTimeIt = _costTimeMap.begin();
		for (; costTimeIt != _costTimeMap.end(); ++costTimeIt)
		{
			LongType callCount = _callCountMap[costTimeIt->first];
			sa.Save("Thread Id:%d, Cost Time:%.2f,Call Count:%d\n",
				costTimeIt->first, (double)costTimeIt->second / CLOCKS_PER_SEC, callCount);
		}

		sa.Save("Total CostTime:%.2f Total Call Count:%d\n",
			(double)_totalCostTime / CLOCKS_PER_SEC, _totalCallCount);
	}

	void Begin(int threadId)//int threadId)
	{
		unique_lock<mutex> lock(_mutex);
		//更新调用次数
		++_callCountMap[threadId];
		//auto& refCount = _refCountMap[threadId];
		if (_refCountMap[threadId] == 0)
		{
			_beginTimeMap[threadId] = clock();
			//开始统计资源
		}
		++_refCountMap[threadId];
		++_totalRefCount;
		++_totalCallCount;
	}

	void End(int threadId)
	{
		unique_lock<mutex> lock(_mutex);
		//更新引用计数
		LongType refCount = --_refCountMap[threadId];
		--_totalRefCount;
		//引用计数<=0时，更新剖析段花费的时间
		if (refCount <= 0)
		{
			//////////////////////////////////////////
			map<int, LongType>::iterator it = _beginTimeMap.find(threadId);
			if (it != _beginTimeMap.end())
			{
				LongType costTime = clock() - it->second;
				if (_refCountMap[threadId] == 0)
				{
					_costTimeMap[threadId] += costTime;
				}
				else
					_costTimeMap[threadId] = costTime;

				_totalCostTime += costTime;
			}
		}
	}
	
	//加锁
	//<threadid,资源统计>
	map<int, LongType> _beginTimeMap;//开始时间统计
	map<int, LongType> _costTimeMap;//花费时间统计
	map<int, LongType> _callCountMap;//调用次数统计
	map<int, LongType> _refCountMap;//引用计数统计


	time_t _beginTime;//总的开始时间
	time_t _totalCostTime;//总的花费时间
	int _totalCallCount;//总的调用次数
	int _totalRefCount;//总的引用计数
	mutex _mutex;
};

//为什么要用单例？因为有一个成员变量map可以把不同的函数给插入到map中去，
//而不用多创建类，所以单例就行了
class PerformanceProfiler: public Singleton<PerformanceProfiler>
{
	
public:
	friend class Singleton<PerformanceProfiler>;
	PPSection* CreateSection(const char* filename, const char* function, int line, const char* desc);

	//void Output()
	//{
	//	ConsoleAdapter csa;//使用单例调用，每个控制台继承了适配器的Save函数，用来打印文件名，函数名，line和描述信息
	//	_Output(csa);
	//	
	//	FileSaveAdapter fsa("PerFormanceProfileReport.txt");//同上
	//	_Output(fsa);
	//}

	static void OutPut();

protected:
	static bool CompareByCallCount(map<PPNode, PPSection*>::iterator lhs,
		map<PPNode, PPSection*>::iterator rhs);
	static bool CompareByCostTime(map<PPNode, PPSection*>::iterator lhs,
		map<PPNode, PPSection*>::iterator rhs);

	PerformanceProfiler()
	{
		// 程序结束时输出剖析结果
		atexit(OutPut);

		time(&_beginTime);
	}

	// 输出序列化信息
	void _Output(SaveAdapter& sa)//为了有序使用了vector能够调用sort排序，只要自己添加COMPARE函数就行了，也就是仿函数
	{
		sa.Save("==================Performance Profiler Report==============\n\n");
		sa.Save("Profiler Begin Time: %s\n", ctime(&_beginTime));
		unique_lock<mutex> lock(_mutex);
		vector<map<PPNode,PPSection*>::iterator> vInfos;
	
		auto it = _ppMap.begin();//PPSection作为查询值,里面保存运行时间，运行次数，开始时间和结束时间

		for (; it != _ppMap.end(); ++it)
		{
			vInfos.push_back(it);
		}

		//按配置条件对剖析结果进行排序输出
		int flag = ConfigManager::GetInstance()->GetOptions();
		if (flag&PPCO_SAVE_BY_COST_TIME)
			sort(vInfos.begin(), vInfos.end(), CompareByCostTime);
		else
			sort(vInfos.begin(), vInfos.end(), CompareByCallCount);

		for (int index = 0; index < vInfos.size(); ++index)
		{
			sa.Save("NO%d. Description:%s\n", index + 1, vInfos[index]->first._desc.c_str());
			vInfos[index]->first.Serialize(sa);
			vInfos[index]->second->Serialize(sa);
			sa.Save("\n");
		}

		sa.Save("================================end=======================\n\n");
		//while (it != _ppMap.end())//所有的信息都存储在了map中map<PPNode,PPSection*>,PPNode作为键值，里面保存了文件名，函数名，line和描述
		//{
		//	sa.Save("NO%d. Desc:%s\n", num++, it->first._desc.c_str());
		//	sa.Save("Filename:%s Function:%s, Line:%d\n", it->first._filename.c_str(), it->first._function.c_str(), it->first._line);
		//	sa.Save("CostTime:%.2f,CallCount:%d\n\n", (double)it->second->_costTime / 1000, it->second->_callCount);

		//	++it;
		//}
	}
private:
	map<PPNode, PPSection*> _ppMap;
	time_t _beginTime;
	mutex _mutex;
};

//atexit
struct Release
{
	~Release()
	{
		PerformanceProfiler::GetInstance()->OutPut();
	}

};

//static Release rl;
//#define PERFORMANCE_PROFILE_RS
///////////////////////////////////////////////////////////////
//剖析性能段开启
#define PERFORMANCE_PROFILER_EE_BEGIN(sign,desc)  \
	PPSection* sign##section = NULL;	\
if (ConfigManager::GetInstance()->GetOptions()&PPCO_PROFILER)\
{\
	sign##section = PerformanceProfiler::GetInstance()->CreateSection(__FILE__, __FUNCTION__, __LINE__, desc); \
	sign##section->Begin(GetThreadId()); \
}


#define PERFORMANCE_PROFILER_EE_END(sign) \
if (sign##section)\
	sign##section->End(GetThreadId())


//设置剖析选项
#define SET_PERFORMANCE_PROFILER_OPTIONS(flag)	\
	ConfigManager::GetInstance()->SetOptions(flag)
