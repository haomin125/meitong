#ifndef XJ_AGORITHM_H
#define XJ_AGORITHM_H

#include <string>
#include <map>
#include <memory>
#include <opencv2/opencv.hpp>
#include <opencv2/freetype.hpp>
#include <iostream>
#include <fstream>
// #include <boost/json.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "xj_app_algorithm.h"
// #include "tensorrt_engine_base.h"
#include "xj_app_yolo_classifier.h"


class XJAlgorithm
{
public:
    XJAlgorithm(std::map<int, float> &mapAutoUpdateParams);
    ~XJAlgorithm();

    bool init(const stConfigParamsA &stParamsA, const stConfigParamsB &stParamsB);
    std::vector<std::vector<int>> detectAnalyze(const cv:: Mat &image, cv::Mat &processedImage, const int productCount, const int nCaptureTimes);

private:
    bool locateBox(const cv::Mat& image, cv::Rect &box_origin, cv::Rect &box, const int nCaptureTimes);
    bool checkWuxing(cv::Rect &box);    //
    bool extractROI(const cv::Mat &roiImage, const cv::Rect &roiRect, std::vector<cv::Rect> &vTargetRect, std::vector<cv::Mat> &vTargetImage);

    cv::Mat preprocessImage(const cv::Mat &roiImage);
    bool detectByDL(int &maskW1, int &maskH1, int &radius1, int &radius2, cv::Point &center1, cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &targetImage, int &result, std::vector<std::vector<int>> &defectResult, cv::Mat &processedImage, std::string &s_modelResult, const int nCaptureTimes);

    bool detectCharacter(const cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectTiaoxingma(const cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectLogo(const cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectLogoHunliao(const cv::Mat& image, cv::Mat &processedImage);

    bool locateNeituoGapROI(const cv::Mat& image, const cv::Rect &roiRC,  cv::Rect &dstRC, const int nCaptureTimes);
    bool detectNeituoGap(const cv::Mat& image, const cv::Rect &roiRC, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectNeiDuanHunliao(const cv::Mat& image, cv::Mat &processedImage);
    bool detectYoubaohumo(const cv::Mat& image, cv::Mat &processedImage);

    bool detectMianZhiHunLiao(const cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectMianZhiHunLiaoEmb(const cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectTianGaiErDuo(const cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectPingKaYiChang(const cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectWuTiaoXingMa(const cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectDiGaiErDuo(const cv::Mat &image, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectDiGaiNeiChangHeight(const cv::Mat &image, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectDiGaiBaoHuMo(const cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool detectTianGaiBaoHuMo(const cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &processedImage, const int nCaptureTimes);
    bool isDisableDet(const float defectType);
    bool getContour(const std::vector<std::vector<cv::Point>>& contours, int &maxAreaIdx, float& maxContourArea, const int &resize_scale);
    
    //参数结构体成员变量
	stConfigParamsA m_stParamsA;
	stConfigParamsB m_stParamsB;

    //自动更新参数
    std::map<int, float> &m_mapAutoUpdateParams;

    //产品名称
	std::string m_sProductName;

    //深度学习tensorrt引擎
    // std::shared_ptr<xj::TensorrtEngineBase> m_tensortRtInfer;
    // std::shared_ptr<xj::TensorrtEngineBase> m_tensortRtInfer_cls;
    // std::shared_ptr<xj::TensorrtEngineBase> m_tensortRtInfer_emb;
    int m_maxBatchSize;

    // std::shared_ptr<YoloClassifier> m_model; 			//yolo模型数据
	// std::shared_ptr<TensorrtClassifier> m_tensorrtDL;	//DLAV2模型数据
	std::shared_ptr<YoloClassifier> m_tensorrtYoloDL;	//YOLOV8模型数据

    std::vector<float> m_vMinDefectProb;
    std::vector<float> m_vMinDefectArea;
    std::vector<float> m_vMinDefectDiag;
    std::vector<float> m_vMinDefectProb_C;
    std::vector<float> m_vMinDefectArea_C;
    std::vector<float> m_vMinDefectDiag_C;
    std::vector<float> m_vMinDefectProb_NC;
    std::vector<float> m_vMinDefectArea_NC;
    std::vector<float> m_vMinDefectDiag_NC;
    std::vector<float> m_vDisableDefectType;

    float m_neituoHeight;
    int m_roiOffsetX;
    int m_roiOffsetY;
    int m_roiWidth;
    int m_roiHeight;

    //物性
    int m_isCheckWuxing;
    int m_wuxingWidth;
    int m_wuxingHeight;
    int m_wuxingWidthOffset;
    int m_wuxingHeightOffset;

    //UI params
    float m_minNeituoHeight;
    float m_maxNeituoHeight;
    float m_neituoGap;
    float m_baohumoEdgeArea;

    //template image
    cv::Mat m_templateCharacterImage;
    cv::Mat m_templateTiaoxingmaImage;
    cv::Mat m_templateLogoImage;

    cv::Ptr<cv::freetype::FreeType2> ft2 = cv::freetype::createFreeType2();

    float debug_add = 0;
    //计时器
	AppTimer m_timer;
    std::vector<double> jishi;
    std::vector<double> jishi1;
};


#endif //XJ_AGORITHM_H
