#ifndef XJ_APP_DETECTOR_H
#define XJ_APP_DETECTOR_H

#include "detector.h"
#include "xj_app_config.h"
#include "xj_app_workflow.h"

class AppDetector : public BaseDetector
{
public:
	AppDetector(const std::shared_ptr<AppDetectorConfig> &pConfig, const std::shared_ptr<BaseTracker> &pTracker,const std::shared_ptr<Board> &pBoard);
	virtual ~AppDetector() {};
	virtual bool createBoardTrackingHistory(const int plcDistance);

	bool reconfigParameters(const int currentRunStatus);
	bool sendResultSignalToPLC(const bool bIsResultOK);

	void updateProductCountofWorkflow();
	void updateProductCountofWorkflow(const int iProductCount);
	int getRealProductCount();

	void setImageSave(const std::shared_ptr<MultiThreadImageSaveBase> &pSave);

	void setCaptureImageTimes(const int iCaptureTimes){m_iCaptureTimes = iCaptureTimes;}
	int getCaptureImageTimes(){return m_iCaptureTimes;}
	int getCaptureImageTotalTimes(){return m_iTotalCaptureTimes;}

	void setCaptueImageTimesBySignal();
	void setCaptueImageTimesByProductCount();

	// 记录生产信息 以便存储到数据库或者上传MES系统
	bool recordProductResultData(const std::vector<ClassificationResult>& vDefectResults);

protected:
	virtual bool addToTrackingHistory();
	virtual bool purgeBoardResult(const std::vector<std::vector<ClassificationResult>> &result, const bool needCalculateResult = true);
	virtual bool decideToSave(const cv::Mat &img, const ClassificationResult result, const std::string &imgName);

private:
	//根据不同剔除模式获取剔除信号
	int getSignalByPurgeMode(const bool bIsResultOK);

	//设置PLC参数
	void setPlcParameters();

	std::vector<ClassificationResult> m_vTotalResult;
	int m_purgeMode;//0-正常剔除， 1-全部OK， 2-全部NG, 3-OK-NG交替
	int m_iCaptureTimes;//当前拍照次数
	int m_iTotalCaptureTimes;//总拍照次数
	int m_iPlcProductNumber = 0;   //每个工位上PLC寄存器的计数值
	ClassificationResult PLC_result = 1;
	bool bIs_PLC = false;
	int m_SecondResult;//暗场拍照结果保存不发送
};

#endif // XJ_APP_DETECTOR_H
