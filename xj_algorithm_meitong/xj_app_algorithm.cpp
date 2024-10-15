#include "xj_app_algorithm.h"
#include "xj_algorithm.h"

using namespace cv;
using namespace std;


XJAppAlgorithm::XJAppAlgorithm(map<int, float> &mapAutoUpdateParams):m_pBase(nullptr)
{
    XJAlgorithm *p = (XJAlgorithm *)m_pBase;
    if( p!= nullptr)
    {
        delete p;
        p = nullptr;
    }

    m_pBase = new XJAlgorithm(mapAutoUpdateParams);
}

XJAppAlgorithm::~XJAppAlgorithm()
{
    XJAlgorithm *p = (XJAlgorithm *)m_pBase;
    if( p!= nullptr)
    {
        delete p;
        p = nullptr;
    }
}

bool XJAppAlgorithm::init(const stConfigParamsA &stParamsA, const stConfigParamsB &stParamsB)
{
    XJAlgorithm *pXJAlgorithm = (XJAlgorithm *)m_pBase;
    return pXJAlgorithm && pXJAlgorithm->init(stParamsA, stParamsB);
}

std::vector<std::vector<int>> XJAppAlgorithm::detectAnalyze(const cv::Mat &image, cv::Mat &processedImage, const int productCount, const int nCaptureTimes)
{
    XJAlgorithm *pXJAlgorithm = (XJAlgorithm *)m_pBase;
    return pXJAlgorithm->detectAnalyze(image, processedImage, productCount, nCaptureTimes);
}
