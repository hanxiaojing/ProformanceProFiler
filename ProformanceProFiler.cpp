#include "PerformanceProFiler.h"

PPSection* PerformanceProfiler::CreateSection(const char* filename, const char* function, int line, const char* desc)
{
	//如果是第一次，那么必须需要查找
	PPSection* pps = NULL;
	PPNode node(filename, function, line, desc);
	unique_lock<mutex> Lock(_mutex);
	
	map<PPNode, PPSection*>::iterator it = _ppMap.find(node);
	if (it != _ppMap.end())
	{
		pps = it->second;
	}
	else
	{
		pps = new PPSection;
		_ppMap[node] = pps;
	}

	return pps;
}

void PerformanceProfiler::OutPut()
{
	int flag = ConfigManager::GetInstance()->GetOptions();
	if (flag & PPCO_SAVE_TO_CONSOLE)
	{
		ConsoleAdapter csa;
		PerformanceProfiler::GetInstance()->_Output(csa);
	}
	if (flag & PPCO_SAVE_TO_FILE)
	{
		FileSaveAdapter fsa("PerformanceProfilerReport.txt");
		PerformanceProfiler::GetInstance()->_Output(fsa);
	}
}



bool PerformanceProfiler::CompareByCallCount(map<PPNode, PPSection*>::iterator lhs,
	map<PPNode, PPSection*>::iterator rhs)
{
	return lhs->second->_totalCallCount > rhs->second->_totalCallCount;
}

bool PerformanceProfiler::CompareByCostTime(map<PPNode, PPSection*>::iterator lhs,
	map<PPNode, PPSection*>::iterator rhs)
{
	return lhs->second->_totalCostTime > rhs->second->_totalCostTime;
}
