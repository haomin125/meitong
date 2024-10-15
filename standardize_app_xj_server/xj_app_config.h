#ifndef XJ_APP_CONFIG_H
#define XJ_APP_CONFIG_H

#include "config.h"
#include "logger.h"

class AppProductLineConfig : public ProductLineConfig
{
public:
	AppProductLineConfig(const std::map<int, std::vector<int>> &boardToView, const std::map<int, int> &viewToBoard, const int targets = 1) : ProductLineConfig(boardToView, viewToBoard, targets) {}
	AppProductLineConfig(const std::map<int, std::vector<int>> &boardToView, const std::map<int, int> &viewToBoard, const std::vector<int> &targets) : ProductLineConfig(boardToView, viewToBoard, targets) {}
	virtual ~AppProductLineConfig() {}

	virtual bool setConfig();

	// vector of bound box parameters in order of bottom, top, left, and right
	std::vector<unsigned int> &boundBoxConfig(const int board, const int view);

private:
	std::map<int, std::map<int, std::vector<unsigned int>>> m_boundBoxConfig;
};

class AppWorkflowConfig : public WorkflowConfig
{
public:
	AppWorkflowConfig() : WorkflowConfig() {}
	virtual ~AppWorkflowConfig() {}

	virtual bool setConfig() { return true; }
};

class AppDetectorConfig : public DetectorConfig
{
public:
	// workflows should be pull out from product line configuration viewsPerBoard()
	AppDetectorConfig(const int workflows) : DetectorConfig(workflows)
	{
		initWorkflowConfigs<AppWorkflowConfig>();
	}
	virtual ~AppDetectorConfig() {}

	virtual bool setConfig() { return true; }

	virtual std::shared_ptr<WorkflowConfig> getWorkflowConfig(const int workflowId);
};

#endif // XJ_APP_CONFIG_H
