
#ifndef XJ_APP_AGORITHM_H
#define XJ_APP_AGORITHM_H

#include <string>
#include <map>
#include <memory>
#include <iostream>
#include "utils.h"

//UI参数
struct stConfigParamsA
{
    //基本参数
    std::string sProductName;
    std::string sProductLot;
    int runStatus;
    int phase;
    int saveImageType;
    int viewId;
    int boardId;
    int numTargetInView;
    std::shared_ptr<MultiThreadImageSaveBase> pSaveImageMultiThread;

    //瑕疵检测参数
    std::map<int, float> fParams;

    //画框参数
    std::map<int, std::vector<cv::Rect>> mapBox;
};

//非UI参数结构体
struct stConfigParamsB
{
    std::map<std::string, float> fParams;
    std::map<std::string, std::string> strParams;
    std::map<std::string, std::vector<float>> vecFParams;
    std::vector<std::string> vCameraNames;
};

class XJAppAlgorithm
{
public:
    XJAppAlgorithm(std::map<int, float> &mapAutoUpdateParams);
    ~XJAppAlgorithm();

    bool init(const stConfigParamsA &stParamsA, const stConfigParamsB &stParamsB);
    std::vector<std::vector<int>> detectAnalyze(const cv:: Mat &image, cv::Mat &processedImage, const int productCount, const int nCaptureTimes = 1);

private:
    void *m_pBase;
};


#endif //XJ_APP_AGORITHM_H