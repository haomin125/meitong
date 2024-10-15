#ifndef XJ_APP_RUNNING_RESULT_H
#define XJ_APP_RUNNING_RESULT_H

#include <fstream>
#include <string>
#include <mutex>
#include <map>
#include <deque>
#include <array>
#include <ostream>
#include <algorithm>

#include <string>
#include <mutex>
#include <opencv2/opencv.hpp>
#include "classifier_result.h"
#include "xj_app_database.h"

class AppRunningResult
{
public: 
	static AppRunningResult &instance();
	static void Exit();
public:
    void setCurrentResultMap(const cv::Mat &frame, const ClassificationResult result, const std::string sCamId, const int captureTimes);
	void getCurrentResult(cv::Mat &frame, ClassificationResult &result, const std::string sCamId, const int captureTimes, const bool checkCameraStataus = true);
	/**
	 * @brief Get the product index of defective products and count of total products
	 * @param vTotalDefectIndices <output> the product index of defective products
	 * @param iTotalNumber <output> count of total production
	 * @param iTotalDefect <output> count of defective production
	 * @return none
	 */
	void getNGProductCountList(std::vector<int>& vTotalDefectIndices, int &iTotalNumber, int &iTotalDefect);
	void updateDisplayTotalInfo(const int iTotalNumber, const int iTotalDefect);

	void clearProductCountResult();
	/**
	 * @brief add(or update) a defect record
	 * @param prod <input> current product name
	 * @param board <input> current board id
	 * @param lotNumber <input> lot number
	 * @param prod_no <input> product count
	 * @param defects <input> defect type list
	 * @return none
	 */
	void recordDefectData(const std::string &prod, const int boardId, const std::string &lotNumber, const int product_no, const vector<ClassificationResult>& defects);

	/******* MES SYSTEM***********/
	/**
	 * @brief add(or update) a mes record
	 * @param iBoard <input> current board id
	 * @param index <input> current prod no.
	 * @param iTotalNum <input> counting of total products
	 * @param iNgNum <input> counting of defective products
	 * @param sDate <input> record date
	 * @param defects <input> defect type list
	 * @return true-sucessful, false-failed
	 */
	bool recordMesData(const std::string &prod, const int boardId, const std::string &lotNumber, const int product_no, 
			const int iTotalNum, const int iNgNum, const vector<ClassificationResult>& defects);
protected:
	static AppRunningResult* m_instance;

private:
	AppRunningResult();
	virtual ~AppRunningResult();
	static std::mutex m_mutex_map;
	static std::mutex m_mutex_nglist;
	static std::mutex m_mutex;
	static std::mutex m_mutex_mes;
	//每个相机多次拍照的结果和图像
    std::map<std::string, std::map<int, std::pair<cv::Mat, ClassificationResult>>> m_mapCurrentProcessedFrameResult;
	//记录所有瑕疵产品的产品号，用于UI刷新走马灯状态
	std::vector<int> m_vTotalDefectIndices;

	//记录寄存器中的产品总数和NG总数
	int m_iDisplayTotalNumber = 0;
	int m_iDisplayTotalDefect = 0;
};


#endif //XJ_APP_RUNNING_RESULT_H
