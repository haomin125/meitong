#include "xj_app_config.h"

using namespace std;

bool AppProductLineConfig::setConfig()
{
	if (totalBoards() <= 0 || totalViews() <= 0)
	{
		return false;
	}

	LogINFO << "Total boards: " << totalBoards() << ", total views: " << totalViews();

	for (int boardIdx = 0; boardIdx < totalBoards(); boardIdx++)
	{
		LogINFO << "total targets in board[" << boardIdx << "]: " << targetsInViewPerBoard(boardIdx);
		map<int, vector<unsigned int>> viewBoundMargin;
		for (int viewIdx = 0; viewIdx < viewsPerBoard(boardIdx); viewIdx++)
		{
			vector<unsigned int> margin = {0, 0, 0, 0};
			viewBoundMargin.emplace(viewIdx, margin);
		}
		m_boundBoxConfig.emplace(boardIdx, viewBoundMargin);
	}

	return true;
}

vector<unsigned int> &AppProductLineConfig::boundBoxConfig(const int board, const int view)
{
	return m_boundBoxConfig[board][view];
}

shared_ptr<WorkflowConfig> AppDetectorConfig::getWorkflowConfig(const int workflowId)
{
	if (m_workflowConfigs.empty() || workflowId >= m_workflowConfigs.size())
	{
		return nullptr;
	}

	return dynamic_pointer_cast<AppWorkflowConfig>(m_workflowConfigs[workflowId]);
}

