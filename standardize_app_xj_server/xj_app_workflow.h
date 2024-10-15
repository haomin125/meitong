#ifndef XJ_APP_WORKFLOW_H
#define XJ_APP_WORKFLOW_H

#include <opencv2/opencv.hpp>
#include "workflow.h"
#include "xj_app_algorithm.h"


class AppWorkflow : public BaseWorkflow
{
public:
	AppWorkflow(const int workflowId, const std::shared_ptr<WorkflowConfig> &pConfig);
	virtual ~AppWorkflow() {}

	virtual bool imagePreProcess();
	virtual bool computerVisionProcess();

	//重置参数
	bool reconfigParameters(const int currentRunStatus);

	//设置多线程存图指针
	void setImageSave(const std::shared_ptr<MultiThreadImageSaveBase> &pSave){ m_pSaveImageMultiThread = pSave; }

	void setCaptureImageTimes(const int nTimes){m_nCaptureImageTimes = nTimes;}

	std::map<int, int>& getDefects(){ return m_mapDefects;}

	void setProductNumber(int iProductNumber){m_iProductNumber = iProductNumber;}
	int getProductNumber(){return m_iProductNumber;}

protected:
	virtual void drawDesignedTargets(const double scale, const int thickness = 3);

private:
	//自动更新参数
	std::map<int, float> m_mapAutoUpdateParams;
	std::map<int, std::string> m_mapAutoUpdateItem;

	//运行状态
	int m_runStatus;

	int m_nCaptureImageTimes;//拍照次数
	int m_nTotalCaptureTimes;//总拍照次数

	//产品
	int m_iProductNumber;//产品数
	std::string m_sProductName;//产品名称
	std::string m_sProductLot;//产品批号
	
	//存图
	int m_numNGHistory;
	int m_iSaveImageType; //0-保存全部， 1-保存NG， 2-不保存
	int m_purgeMode;//0-正常剔除， 1-全部OK， 2-全部NG, 3-OK-NG交替
	int m_runMode;//0-检测  1-空跑

	//多线程存图
	std::shared_ptr<MultiThreadImageSaveBase> m_pSaveImageMultiThread;
	//算法库调用指针
	std::shared_ptr<XJAppAlgorithm> m_pAlgorithm;
	
	std::map<int, int> m_mapDefects;//瑕疵映射表<瑕疵类别，个数>
	std::map<int, int> m_mapDefectIndexToType; //<瑕疵索引, 瑕疵类别>

	int convertDefectType(const int defectIndex);

	bool initCameraParams(const int currentRunStatus);    // 从数据库/配置文件中初始化相机曝光、增益等设置
	bool initParamsA(stConfigParamsA &stParamsA, const int currentRunStatus);
	bool initParamsB(stConfigParamsB &stParamsB, const int currentRunStatus);
};

#endif // XJ_APP_WORKFLOW_H
