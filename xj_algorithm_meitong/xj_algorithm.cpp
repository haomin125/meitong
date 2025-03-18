#include "xj_algorithm.h"
#include "data.h"
#include "utils.h"
#include <numeric>

using namespace cv;
using namespace std;
using namespace boost::property_tree::json_parser;
using namespace boost::property_tree;

enum DefectType:int
{
    good         =       1,
    defect1      =       2,
    defect2      =       3,
    defect3      =       4,
    defect4      =       5,
    defect5      =       6,
    defect6      =       7,
    defect7      =       8,
    defect8      =       9,
    defect9      =       10,
    defect10     =       11,
    defect11     =       12,
    defect12     =       13,
    defect13     =       14,
    defect14     =       15,
    defect15     =       16,
    defect16     =       17,
    defect17     =       18,
    defect18     =       19,
    defect19     =       20,
    defect20     =       21,
    defect21     =       22,
    defect22     =       23,
    defect23     =       24,
    defect24     =       25,
    defect25     =       26,
    defect26     =       27,
    defect27     =       28,
    defect28     =       29,
    defect29     =       30,
    defect30     =       31

};

#define OK_SOURCE_IMAGE_SAVE_PATH "/opt/history/good"
#define NG_SOURCE_IMAGE_SAVE_PATH "/opt/history/bad"

#define DRAW_OK_COLOR Scalar(0, 255, 0)
#define DRAW_NG_COLOR Scalar(0, 0, 255)
// #define TARGET_SIZE 1024
// #define EXTEND_LENGTH 60
// #define CLASS_HEIGHT 224
// #define CLASS_WIDTH 448

#define TARGET_SIZE 640
#define EXTEND_LENGTH 60

//YOLO
#define SEG_SCALEFACTOR 4
#define SEG_CHANNELS 32
// #define SEG_SCALEFACTOR 1   //original_size/resized_size
// #define SEG_CHANNELS 11     //num_classes+1


XJAlgorithm::XJAlgorithm(map<int, float> &mapAutoUpdateParams):
    m_mapAutoUpdateParams(mapAutoUpdateParams),
    m_tensorrtYoloDL(nullptr),  //
    m_maxBatchSize(1),  //在类的构造函数中初始化成员变量m_maxBatchSize为1
    m_sProductName("N/A"),  //在类的构造函数中初始化成员变量m_sProductName为"N/A"
    m_neituoHeight(120)
{
}

XJAlgorithm::~XJAlgorithm()
{
}


bool XJAlgorithm::init(const stConfigParamsA &stParamsA, const stConfigParamsB &stParamsB)
{
    m_stParamsA = stParamsA;
    m_stParamsB = stParamsB;
    // if(!(m_stParamsA.boardId==0||m_stParamsA.boardId==1)){return true;}
    ft2->loadFontData("/opt/app/simhei.ttf",0); //
    const int numCategory = m_stParamsB.vecFParams.at("NUM_CATEGORY")[m_stParamsA.boardId]; //numCategory->m_vDisableDefectType
    m_vDisableDefectType.clear();
    for(float i=0;i<numCategory;i++)
    {
        if(!m_stParamsA.fParams.at(m_stParamsA.boardId+40) || !m_stParamsA.fParams.at(m_stParamsA.boardId*50+300+i))
        {
            m_vDisableDefectType.push_back(i);  //
        }
    }

    if(m_sProductName != m_stParamsA.sProductName)
    {
        const int inputWidth = TARGET_SIZE;
        const int inputHeight = TARGET_SIZE;
        const int inputChannel = 3;

        m_roiOffsetX = m_stParamsB.vecFParams.at("ROI_OFFSET_X")[m_stParamsA.boardId];//此处为取像roi
        m_roiOffsetY = m_stParamsB.vecFParams.at("ROI_OFFSET_Y")[m_stParamsA.boardId];
        m_roiWidth = m_stParamsB.vecFParams.at("ROI_WIDTH")[m_stParamsA.boardId];
        m_roiHeight = m_stParamsB.vecFParams.at("ROI_HEIGHT")[m_stParamsA.boardId];

        //扣图检测物性
        m_wuxingWidth = m_stParamsB.vecFParams.at("WUXING_X")[m_stParamsA.boardId];
        m_wuxingHeight = m_stParamsB.vecFParams.at("WUXING_Y")[m_stParamsA.boardId];
        m_wuxingWidthOffset = m_stParamsB.vecFParams.at("WUXING_X_OFFSET")[m_stParamsA.boardId];
        m_wuxingHeightOffset = m_stParamsB.vecFParams.at("WUXING_Y_OFFSET")[m_stParamsA.boardId];

        // m_vDisableDefectType = m_stParamsB.vecFParams.at("DISABLE_DEFECT_TYPE_DET_CAM" +  to_string(m_stParamsA.boardId + 1));

        m_vMinDefectArea.clear();
        m_vMinDefectProb.clear();
        m_vMinDefectDiag.clear();
        m_vMinDefectProb_C_pic1.clear();
        m_vMinDefectArea_C_pic1.clear();
        m_vMinDefectDiag_C_pic1.clear();
        m_vMinDefectProb_NC_pic1.clear();
        m_vMinDefectArea_NC_pic1.clear();
        m_vMinDefectDiag_NC_pic1.clear();
        m_vDisableDefectType.clear();
        
        m_maxBatchSize = m_stParamsB.vecFParams.at("MAX_BATCH_SIZE")[m_stParamsA.boardId];
        const float NmsThresh = m_stParamsB.vecFParams.at("MNS_THRESHOLD")[m_stParamsA.boardId];
        const float ConfThresh = m_stParamsB.vecFParams.at("CONF_THRESHOLD")[m_stParamsA.boardId];
        const string model_path = m_stParamsB.strParams.at("MODEL_PATH_CAM" +  to_string(m_stParamsA.boardId + 1));

        //YOLOV8
        const int maskThr = m_stParamsB.vecFParams.at("MASK_THR")[m_stParamsA.boardId]; 
        const int detbox_num = inputWidth * inputHeight / 32 / 32 * 21; //yolo标准式可化简为：w*h/32/32*(4*4+2*2+1*1)

        //中心区center 第一次拍照
        m_vMinDefectProb_C_pic1 = m_stParamsB.vecFParams.at("DEFECT_MIN_PROB_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC1_C");
        m_vMinDefectArea_C_pic1 = m_stParamsB.vecFParams.at("DEFECT_MIN_AREA_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC1_C");
        m_vMinDefectDiag_C_pic1 = m_stParamsB.vecFParams.at("DEFECT_MIN_DIAG_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC1_C");
        //非中心区not center
        m_vMinDefectProb_NC_pic1 = m_stParamsB.vecFParams.at("DEFECT_MIN_PROB_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC1_NC");
        m_vMinDefectArea_NC_pic1 = m_stParamsB.vecFParams.at("DEFECT_MIN_AREA_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC1_NC");
        m_vMinDefectDiag_NC_pic1 = m_stParamsB.vecFParams.at("DEFECT_MIN_DIAG_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC1_NC");
        //边缘区
        m_vMinDefectProb_E_pic1 = m_stParamsB.vecFParams.at("DEFECT_MIN_PROB_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC1_E");
        m_vMinDefectArea_E_pic1 = m_stParamsB.vecFParams.at("DEFECT_MIN_AREA_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC1_E");
        m_vMinDefectDiag_E_pic1 = m_stParamsB.vecFParams.at("DEFECT_MIN_DIAG_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC1_E");

        //中心区center 第二次拍照
        m_vMinDefectProb_C_pic2 = m_stParamsB.vecFParams.at("DEFECT_MIN_PROB_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC2_C");
        m_vMinDefectArea_C_pic2 = m_stParamsB.vecFParams.at("DEFECT_MIN_AREA_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC2_C");
        m_vMinDefectDiag_C_pic2 = m_stParamsB.vecFParams.at("DEFECT_MIN_DIAG_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC2_C");
        //非中心区not center
        m_vMinDefectProb_NC_pic2 = m_stParamsB.vecFParams.at("DEFECT_MIN_PROB_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC2_NC");
        m_vMinDefectArea_NC_pic2 = m_stParamsB.vecFParams.at("DEFECT_MIN_AREA_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC2_NC");
        m_vMinDefectDiag_NC_pic2 = m_stParamsB.vecFParams.at("DEFECT_MIN_DIAG_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC2_NC");
        //边缘区
        m_vMinDefectProb_E_pic2 = m_stParamsB.vecFParams.at("DEFECT_MIN_PROB_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC2_E");
        m_vMinDefectArea_E_pic2 = m_stParamsB.vecFParams.at("DEFECT_MIN_AREA_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC2_E");
        m_vMinDefectDiag_E_pic2 = m_stParamsB.vecFParams.at("DEFECT_MIN_DIAG_CAM" + to_string(m_stParamsA.boardId + 1) + "_PIC2_E");

        // m_tensortRtInfer = make_shared<xj::TensorrtEngineBase>();
        m_tensorrtYoloDL = make_shared<YoloClassifier>(model_path, YoloOutputType::DETECTION, m_maxBatchSize, numCategory, inputWidth, inputHeight, inputChannel);
        
        //if(m_stParamsA.boardId==200)
        //{m_tensortRtInfer->setDetParameters(model_path, inputWidth, inputHeight, inputChannel, m_maxBatchSize, NmsThresh, ConfThresh);}
        //else
        //{m_tensortRtInfer->setYolo8Parameters(model_path, inputWidth, inputHeight, inputChannel, numCategory, m_maxBatchSize, NmsThresh, ConfThresh);}
        m_tensorrtYoloDL->setInferParameters(ConfThresh, NmsThresh, maskThr, SEG_SCALEFACTOR, SEG_CHANNELS, detbox_num);

        // if(m_stParamsA.boardId==200)
        // {
        //     // cout << "cam: " << m_stParamsA.boardId << "\t加载yolo5模型" << endl;
        //     if(!m_tensortRtInfer->loadDetModel())
        //     {
        //         cout << "board[" << m_stParamsA.boardId << "] failed to initial model!!!" << endl;
        //         return false;
        //     }
        // }
        // else
        // {
        //     // cout << "cam: " << m_stParamsA.boardId << "\t加载yolo8模型" << endl;
        //     if(!m_tensortRtInfer->loadYolo8Model())
        //     {
        //         cout << "board[" << m_stParamsA.boardId << "] failed to initial model!!!" << endl;
        //         return false;
        //     }
        // }
        if(!m_tensorrtYoloDL->loadModel())
        {
            cout << "board[" << m_stParamsA.boardId << "] failed to initial model!!!" << endl;
            return false;
        }
    }
    return true;
}
vector<vector<int>> XJAlgorithm::detectAnalyze(const Mat &image, Mat &processedImage, const int productCount, const int nCaptureTimes)
{
	m_timer.reset();    
    processedImage = image.clone();
    int result = (int)DefectType::good; //1?
    //defectResult是行数为m_stParamsA.numTargetInView的二维向量，每一行初始化为vector<int>()，存储缺陷结果
    vector<vector<int>> defectResult(m_stParamsA.numTargetInView, vector<int>());
    if(nCaptureTimes == (int)CaptureImageTimes::UNKNOWN_TIMES)
    {
        result = (int)DefectType::defect1;
        defectResult[0].emplace_back(result);
        return defectResult;
    }
    // if(!(m_stParamsA.boardId==0||m_stParamsA.boardId==1)){return defectResult;}
    vector<float> is_board = m_stParamsB.vecFParams.at("is_board");
    if(m_stParamsA.boardId == is_board[0] || m_stParamsA.boardId == is_board[1] || m_stParamsA.boardId == is_board[2])
    {
        result = (int)DefectType::defect1;
        defectResult[0].emplace_back(result);
        return defectResult;
    }
    if(is_board.size() == 4)
    {
        if(debug_add == is_board[3] && is_board[3] != -1)
        {
            result = (int)DefectType::defect1;
            defectResult[0].emplace_back(result);
            return defectResult;
        }
        debug_add++;
    }

	const double t1 = m_timer.elapsed();
    cout << "detectAnalyze: Board[" << m_stParamsA.boardId << "] :  time cost " << t1 << " seconds" << endl;
    //step1: locate box
    m_timer.reset();
    Rect roiRect, roi_origin;
    if(!locateBox(image, roi_origin, roiRect, nCaptureTimes)) //当定位失败时，result被强制为defect1=2; 扩展ROI为正方形
    {
        cout << "[ERROR] locateBox" << endl; 
        result = (int)DefectType::defect10;
        defectResult[0].emplace_back(result);
        // imwrite("locateBox.png", image);
        if(1)
        {
            string sFilePath = "/opt/history/temp";         
            string sCustomerEnd = "locateBox" + m_stParamsB.vCameraNames[m_stParamsA.boardId];
            string sFileName = getAppFormatImageNameByCurrentTimeXJ(1, m_stParamsA.boardId, 0, 0, m_stParamsA.sProductName, m_stParamsA.sProductLot, sCustomerEnd);
            m_stParamsA.pSaveImageMultiThread->AddImageData(processedImage, sFilePath, sFileName, ".png");
        }
        return defectResult;
    }

    const double t2 = m_timer.elapsed();
    cout << "detectAnalyze: Board[" << m_stParamsA.boardId << "] : locateBox time cost " << t2 << " seconds" << endl;

    Mat roiImage = image(roiRect);
    m_product_diameter = roi_origin.width > roi_origin.height ? roi_origin.width : roi_origin.height;
    m_productCentre = m_product_diameter / 5.7; //中心区420
    m_productEdge = m_product_diameter / 2.2; //边缘区1130

    //step2: 屏蔽背景区域
    Mat resultImage;
    missBackground(roiImage, resultImage);
    
    // 红光暗场屏蔽区域
    if(nCaptureTimes == 2 && m_stParamsA.boardId == 0)
    {
        const int radius_is = m_wuxingWidth / 2.7;
        cv::Mat mask3 = cv::Mat::ones(resultImage.rows, resultImage.cols, CV_8UC1);  //CV_8UC1：8位单通道图像
        cv::circle(mask3, Point(resultImage.cols / 2, resultImage.rows / 2), radius_is, cv::Scalar(0), -1);
        cv::Mat dst;
        resultImage.copyTo(dst,mask3);
        resultImage = dst.clone();
        imwrite("/opt/app/test/红光暗场屏蔽区域.png", resultImage);
    }    
    //step3:split ROI 
    vector<Rect> vTargetRect;
    vector<Mat> vTargetImage;
    m_timer.reset();
    if(!extractROI(resultImage, roiRect, vTargetRect, vTargetImage))
    {
        cout << "ERROR extractROI" << endl; 
        result = (int)DefectType::defect1;
        defectResult[0].emplace_back(result);
        imwrite("/opt/app/test/extractROI.png", roiImage);
        return defectResult;
    }
   
    const double t3 = m_timer.elapsed();
    cout << "detectAnalyze: Board[" << m_stParamsA.boardId << "] : extractROI time cost " << t3 << " seconds" << endl;
    m_timer.reset();
    //step4: get detect result by DL
    for (int i = 0; i < vTargetImage.size(); i++)   // 
    {
        result = (int)DefectType::good;
        std::string s_ResultIdName; //结果ID命名在图片上
        if(!detectByDL(resultImage, vTargetRect[i], vTargetImage[i], result, defectResult, processedImage, s_ResultIdName, nCaptureTimes))
        {
            result = (int)DefectType::defect1;
            defectResult[0].emplace_back(result);
        }
        // cout << "defectResult----  " << i << "____" << defectResult[i].size() << endl;

        // for (size_t j = 0; j < defectResult[i].size() ; j++)
        // {
        //     cout  << "____" << defectResult[i][0] << endl;
        // }

        //step5: save image
        // cout << "~~~~~~~~~~~~~~~~~~~ " << m_stParamsA.pSaveImageMultiThread << endl;
        if(m_stParamsA.pSaveImageMultiThread && m_stParamsB.fParams.at("IS_SAVE_PROCESS_IMAGE"))
        {
            if(m_stParamsA.saveImageType == (int)SaveImageType::ALL || ((result != (int)DefectType::good) && m_stParamsA.saveImageType == (int)SaveImageType::NG_ONLY))
            {
                // cout << "~------------------- " << m_stParamsA.pSaveImageMultiThread << endl;
                string sFilePath = (result == (int)DefectType::good) ? OK_SOURCE_IMAGE_SAVE_PATH : NG_SOURCE_IMAGE_SAVE_PATH;     
                string sCustomerEnd = "CNT" + to_string(productCount) + "-PIC" + to_string(nCaptureTimes) + s_ResultIdName;
                string sFileName = getAppFormatImageNameByCurrentTimeXJ(result, m_stParamsA.boardId, 0, i, m_stParamsA.sProductName, m_stParamsA.sProductLot, sCustomerEnd);
                m_stParamsA.pSaveImageMultiThread->AddImageData(vTargetImage[i], sFilePath, sFileName, ".png");
            }
        }
    }  
    //在UI显示，可视化区分中心区/非中心区/边缘区
    cv::circle(processedImage, Point(resultImage.cols / 2, resultImage.rows / 2), m_productCentre, Scalar(255, 0, 255), 3, cv::LINE_8);
    cv::circle(processedImage, Point(resultImage.cols / 2, resultImage.rows / 2), m_productEdge, Scalar(255, 0, 255), 3, cv::LINE_8);

    const double t4 = m_timer.elapsed();
    cout << "detectAnalyze: Board[" << m_stParamsA.boardId << "] : detectByDL time cost " << t4 << " seconds" << endl;
    jishi1.emplace_back(t4);
    if (jishi1.size() == 100 && m_stParamsB.fParams.at("IS_DEBUG"))
    {
        int i = 0;
        for (double num : jishi1)
        {
            i++;
            cout << "detectByDL: Board[" << m_stParamsA.boardId << "] : 第" << i << "次" << num << " seconds" << endl;
        }
		float maxValue = *max_element(jishi1.begin(), jishi1.end());
		double sum = accumulate(jishi1.begin(), jishi1.end(), 0.0);
		double average = static_cast<double>(sum) / jishi1.size();
		cout << "detectByDL:Maximum value: " << maxValue << endl;
		cout << "detectByDL:Average value: " << average << endl;
        jishi1.clear();
    }
                      
    return defectResult;
}

bool XJAlgorithm::locateBox(const Mat& image, Rect &box_origin, Rect &box, const int nCaptureTimes)
{
    Rect globalRC(0, 0, image.cols, image.rows);
    Rect roiRC(m_roiOffsetX, m_roiOffsetX, m_roiWidth, m_roiHeight);
    roiRC &= globalRC;
    if(roiRC.height<=0 || roiRC.width<=0)
    {
        return false;
    }
    //step1: preprocess
    Mat roiImage = image(roiRC);
    Mat resizeImg;
    int resize_scale = m_wuxingWidth / 242;
    cout << " m_wuxingWidth " << m_wuxingWidth << resize_scale << endl;
    resize(roiImage, resizeImg, Size(roiImage.cols / resize_scale, roiImage.rows / resize_scale));
    Mat grayImage, binaryImage, bilater, edges;
    vector<Mat> channels;
    split(resizeImg, channels);
    if(m_stParamsA.boardId == 0)
    {
        bilateralFilter(channels[2], bilater, 3, 3, 3);
        Canny(bilater, edges, 60, 200);
        if (nCaptureTimes == 1)
        {
            Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(3, 3));
            morphologyEx(edges, binaryImage, MORPH_CLOSE, kernel);  
        }else
        {
            Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(5, 5));
            morphologyEx(edges, binaryImage, MORPH_CLOSE, kernel);  
        }
    }else
    {
        if (nCaptureTimes == 1)
        {
            int lowthre = m_stParamsB.vecFParams.at("CANNYTHRE")[0];
            int highthre = m_stParamsB.vecFParams.at("CANNYTHRE")[1];
            bilateralFilter(channels[0], bilater, 3, 3, 3);
            Canny(bilater, edges, lowthre, highthre);
        }else
        {
            cvtColor(resizeImg, grayImage, COLOR_RGB2GRAY);
            if(m_stParamsB.fParams.at("IS_DEBUG"))
            {imwrite("/opt/app/test/grayImage.png", grayImage);}
            bilateralFilter(channels[1], bilater, 5, 5, 5);
            Canny(bilater, edges, 10, 80);
        }   
        Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(3, 3));
        morphologyEx(edges, binaryImage, MORPH_CLOSE, kernel);  
    }    
    if(m_stParamsB.fParams.at("IS_DEBUG"))
    {
        imwrite("/opt/app/test/bilater.png", bilater);
        imwrite("/opt/app/test/Canny.png", edges);
        imwrite("/opt/app/test/binaryImage.png", binaryImage);
    }

    //test houghcirle
    // Hough_Circle(grayImage);
    //step2: find contour
    vector<vector<Point>> contours;
    findContours(binaryImage, contours, RETR_LIST, CHAIN_APPROX_SIMPLE);
    if(contours.size() == 0)
    {
        return false;
    }

    //step3: get max contour
    int maxIdx = 0;
    float maxArea = 0;
    if(!getContour(contours, maxIdx, maxArea, resize_scale))
    {
        return false;
    }

    Mat roiImage_ = resizeImg.clone();
    if(m_stParamsB.fParams.at("IS_DEBUG"))
    {
        // for (size_t i = 0; i < contours.size(); i++)
        // {
        //     box = boundingRect(contours[i]);
            box = boundingRect(contours[maxIdx]);
            rectangle(roiImage_, box, Scalar(0,255,255),4);
        // }
        drawContours(roiImage_, contours, maxIdx, Scalar(0,255,255), 4);
        imwrite("/opt/app/test/rectangle.png", roiImage_);
    }

    //step4: get max bounding box and return result
    box = boundingRect(contours[maxIdx]);
    box.x *= resize_scale; //左上角横坐标
    box.y *= resize_scale; //左上角纵坐标
    box.width *= resize_scale;
    box.height *= resize_scale;
    box_origin = box;

    cout << "box.width:" << box.width << endl;
    cout << "box.height:" << box.height << endl;

    box.x += m_roiOffsetX;  //得到配置文件中的ROI_OFFSET_X=0
    box.y += m_roiOffsetY;  //得到配置文件中的m_roiOffsetY=0

    //step5: extend foreground ROI edge
    box.x -= EXTEND_LENGTH; //左上角横坐标
    box.y -= EXTEND_LENGTH; //左上角纵坐标
    box.width += EXTEND_LENGTH * 2;
    box.height += EXTEND_LENGTH * 2;
    if (box.width > box.height)
    {
        box.height = box.width;
    }
    else
    {
        box.width = box.height;
    }
    box &= globalRC;
    if(box.height<=0 || box.width<=0)
    {
        return false;
    }
    return true;
}

bool XJAlgorithm::getContour(const vector<vector<Point>>& contours, int &maxAreaIdx, float& maxContourArea, const int &resize_scale)
{
    maxAreaIdx = -1;
    maxContourArea = 0.0;
    Rect box;
    int widthThr1 = (m_wuxingWidth - m_wuxingWidthOffset) / resize_scale;
    int widthThr2 = (m_wuxingWidth + m_wuxingWidthOffset) / resize_scale;
    int heightThr1 = (m_wuxingHeight - m_wuxingHeightOffset) / resize_scale;
    int heightThr2 = (m_wuxingHeight + m_wuxingHeightOffset) / resize_scale;
    for (int i = 0; i != contours.size(); i++)
    {
        box = boundingRect(contours[i]);
        // if ((widthThr1<box.width<widthThr2) & (heightThr1<box.height<heightThr2))
        if (widthThr1 < box.width & widthThr2 > box.width & heightThr1 < box.height & heightThr2 > box.height )  //检测物性
        {
            // cout << "widthThr1.width:" << box.width << endl;
            // cout << "widthThr1.height:" << box.height << endl;
            // double tempArea = contourArea(contours[i]);
            double tempArea = box.width * box.height;
            // cout << "widthThr1.I  :" << i  << "  " << tempArea << endl;
            if (tempArea > maxContourArea)
            {
                maxContourArea = tempArea;
                maxAreaIdx = i;
            }
        }
    }
    if (maxAreaIdx == -1)
    {
        return false;
    }
    return true;
}

//split box to ROI
bool XJAlgorithm::extractROI(const Mat &roiImage, const Rect &roiRect, vector<Rect> &vTargetRect, vector<Mat> &vTargetImage)
{
    const int numTargetX = 5;
    const int numTargetY = 5;
    const int width = roiImage.cols;
    const int height = roiImage.rows;
    const int targetSize = TARGET_SIZE;

    for(int i = 0; i != numTargetY; ++i)
    {
        for(int j = 0; j != numTargetX; ++j)
        {
            const int overlapX = (numTargetX * targetSize - width)/(float)(numTargetX - 1);
            const int overlapY = (numTargetY * targetSize - height)/(float)(numTargetY - 1);

            int left = j *(targetSize - overlapX);
			int right = left + targetSize;
			int top = i *(targetSize - overlapY);
			int bot = top + targetSize ;

            if(right > roiImage.cols)
			{
				right = roiImage.cols;
				left = roiImage.cols - targetSize;
                if (left < 0) left = 0;
			}

			if(bot > roiImage.rows)
			{
				bot = roiImage.rows;
				top = roiImage.rows - targetSize;
                if (top < 0) top = 0;
			}

            Rect targetRc = Rect(Point(left, top), Point(right, bot));
            Mat targetImage = roiImage(targetRc).clone();
        
			//fix coor in source image not roi region
			// targetRc.x += roiRect.tl().x;
			// targetRc.y += roiRect.tl().y;
			vTargetRect.emplace_back(targetRc);
            vTargetImage.emplace_back(targetImage);
        }
    }
    return true;
}

Mat XJAlgorithm::preprocessImage(const Mat &roiImage)
{
    Mat targetImage = makeSquareImage(roiImage).clone();
    resize(targetImage, targetImage, Size(TARGET_SIZE, TARGET_SIZE));
    return targetImage;
}

// bool XJAlgorithm::detectByDL(Mat &roiImage, const Rect &roiRect, Mat &targetImage, int &result, vector<vector<int>> &defectResult, Mat &processedImage)
bool XJAlgorithm::detectByDL(cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &targetImage, int &result, std::vector<std::vector<int>> &defectResult, cv::Mat &processedImage, std::string &s_ResultIdName, const int nCaptureTimes)
{
    processedImage = roiImage.clone();
    //step1:pre-process
    vector<Mat> images;
    vector<Rect> roiRect_;
    vector<vector<YoloOutputDetect>> detectionOutput;
    targetImage = preprocessImage(targetImage);
    // targetImage = imread("/opt/image/0306/meitong_wujian/ming/test.png");   //测试小图
    images.push_back(targetImage);
    roiRect_.push_back(roiRect);
    
    //step2:detect by DL
    // vector<vector<Detection>> vResult;
	// cout<<"images.size():  "<< images.size() <<endl;
    // const double t4 = m_timer.elapsed();
    m_tensorrtYoloDL->getDetectionResult(images, detectionOutput);  

    // const double t5 = m_timer.elapsed() - t4;
    // cout << "小图推理时间: Board[" << m_stParamsA.boardId << "] : getDetectionResult time cost " << t5 << " seconds" << endl;
    // jishi.emplace_back(t5);
    // if (jishi.size() == 1000 && m_stParamsB.fParams.at("IS_DEBUG"))
    // {
    //     int i = 0;
    //     for (double num : jishi)
    //     {
    //         i++;
    //         cout << "小图推理时间: Board[" << m_stParamsA.boardId << "] : 第" << i << "次" << num << " seconds" << endl;
    //     }
	// 	float maxValue = *max_element(jishi.begin(), jishi.end());
	// 	double sum = accumulate(jishi.begin(), jishi.end(), 0.0);
	// 	double average = static_cast<double>(sum) / jishi.size();
	// 	cout << "小图1000次推理时间:Maximum value: " << maxValue << endl;
	// 	cout << "小图1000次推理时间:Average value: " << average << endl;
    //     jishi.clear();
    // }
    
    // step3:post-process
    // float scale = std::max(roiImage.rows, roiImage.cols) / (float)TARGET_SIZE;  //归一化系数
	// cout<<"detectionOutput.size():  "<< detectionOutput.size() <<endl;
    // 单张小图训练，只循环一次
    for	(int j=0; j<detectionOutput.size(); j++) {
		Rect box;
		int objectId;
		float confidences;
		float tempS;
        float diagL;
        
        // float diagXianshang = 0;
        // cv::Mat maskXianshang = cv::Mat::zeros(640, 640, CV_8UC1);

        std::vector<cv::Rect> boxesXianshang, boxesDianshang, boxesYiwudian;
        // float diagLXianshang;

	    // cout<<"detectionOutput.at(j).size():  "<< detectionOutput.at(j).size() <<endl;
        // 有多少个瑕疵循环多少次
		for	(int i=0; i<detectionOutput.at(j).size(); i++) {
            YoloOutputDetect &det = detectionOutput[j][i];
			objectId = det.id;
            confidences = det.confidence;

            if(nCaptureTimes == 2 && m_stParamsA.boardId == 0 && objectId != 0)  //红光暗场只检禁止瑕疵
            {
               continue;
            }
            
			// cout<<"objectId " << objectId<<endl;
			box.x = int(detectionOutput.at(j).at(i).box.x + roiRect.x);    //相对扣图的坐标
			box.y = int(detectionOutput.at(j).at(i).box.y + roiRect.y);    //相对扣图的坐标
			box.width = int(detectionOutput.at(j).at(i).box.width);
			box.height = int(detectionOutput.at(j).at(i).box.height);

            // //测试小图
            // Mat ceshi_xiaotu = targetImage.clone();
            // rectangle(ceshi_xiaotu, detectionOutput.at(j).at(i).box, Scalar(0,255,255), 5, 8);
            // imwrite("/opt/app/test/ceshi_xiaotu.png", ceshi_xiaotu);

			//tempS = box.width*box.width*m_fPPS*m_fPPS + box.height*box.height*m_fPPS*m_fPPS;    //转化为物理尺寸的对角线平方
            //tempS = box.width*m_fPPS*box.height*m_fPPS;    //物理尺寸的面积
            tempS = box.area();
            diagL = std::sqrt(box.width*box.width + box.height*box.height); //瑕疵对角线长度

            // cout << "模型检测结果C" << objectId + 2 << "  PROB:" << confidences << " _  AREA:" << tempS << "_  DIAG:" << diagL << endl;
            s_ResultIdName = "-PROB" + to_string(confidences) + "-AREA" + to_string(tempS) + "-DIAG" + to_string(diagL) + "-ID" + to_string(objectId + 2);

            //防止边缘附近的背景上的瑕疵误检
            //定义一个空的掩膜图，对应ROI区
            cv::Mat mask3 = cv::Mat::zeros(roiImage.rows, roiImage.cols, CV_8UC1);
            cv::Mat mask4 = mask3.clone();
            cv::circle(mask3, Point(roiImage.cols / 2, roiImage.rows / 2), m_product_diameter / 2, cv::Scalar(255), -1);
            //对每个检测到的瑕疵定义一个掩膜图
            cv::Rect roi(box.x, box.y, box.width, box.height);
            cv::rectangle(mask4, roi, cv::Scalar(255), -1);
            cv::Mat mask5;
            cv::bitwise_and(mask3, mask4, mask5);
            int whiteArea = cv::countNonZero(mask5);
            std::cout << "相交区域的面积： " << whiteArea << std::endl;

            float centerX = box.x + box.width/2;    
            float centerY = box.y + box.height/2;
            //计算产品中心到瑕疵中心的距离: 1、取扣图边缘和扩展之后边缘的中心来比较，判断瑕疵是不是在背景  2、与设定的中心区比较，调用松/紧参数
            float d_defect2center = std::sqrt((centerX-roiImage.cols/2)*(centerX-roiImage.cols/2) + (centerY-roiImage.rows/2)*(centerY-roiImage.rows/2));
            if (d_defect2center > (m_product_diameter + roiImage.cols) / 4 && whiteArea < 10){    //如果检测到的瑕疵的中心点在背景区（防止模型异常或早期的训练数据影响）
                cout << "放过背景上的瑕疵" << endl;
                continue;
            }
            if (nCaptureTimes == 1)
            {
                if(d_defect2center <= m_productCentre){
                    m_vMinDefectArea = m_vMinDefectArea_C_pic1;
                    m_vMinDefectProb = m_vMinDefectProb_C_pic1;
                    m_vMinDefectDiag = m_vMinDefectDiag_C_pic1;
                }
                else if(m_productEdge > d_defect2center && d_defect2center > m_productCentre){
                    m_vMinDefectArea = m_vMinDefectArea_NC_pic1;
                    m_vMinDefectProb = m_vMinDefectProb_NC_pic1;
                    m_vMinDefectDiag = m_vMinDefectDiag_NC_pic1;
                }
                else{
                    m_vMinDefectArea = m_vMinDefectArea_E_pic1;
                    m_vMinDefectProb = m_vMinDefectProb_E_pic1;
                    m_vMinDefectDiag = m_vMinDefectDiag_E_pic1;
                }
            }
            else
            {
                if (d_defect2center <= m_productCentre){
                    m_vMinDefectArea = m_vMinDefectArea_C_pic2;
                    m_vMinDefectProb = m_vMinDefectProb_C_pic2;
                    m_vMinDefectDiag = m_vMinDefectDiag_C_pic2;
                }
                else if(m_productEdge > d_defect2center && d_defect2center > m_productCentre){
                    m_vMinDefectArea = m_vMinDefectArea_NC_pic2;
                    m_vMinDefectProb = m_vMinDefectProb_NC_pic2;
                    m_vMinDefectDiag = m_vMinDefectDiag_NC_pic2;
                }
                else{
                    m_vMinDefectArea = m_vMinDefectArea_E_pic2;
                    m_vMinDefectProb = m_vMinDefectProb_E_pic2;
                    m_vMinDefectDiag = m_vMinDefectDiag_E_pic2;
                }               
            }

            // 瑕疵：后处理判断 OK/NG
            if (tempS > m_vMinDefectArea[objectId] && diagL >= m_vMinDefectDiag[objectId] && confidences >= m_vMinDefectProb[objectId] && whiteArea >= 1)
            {  
                Scalar scalar = Scalar(0,255,255);
                result = objectId + 2;  //good是1，瑕疵从2开始
                defectResult[0].emplace_back(result);
                rectangle(roiImage, box, scalar, 5, 8);
                // line(roiImage, Point(centerX, centerY), Point(roiImage.cols/2, roiImage.rows/2), scalar, 2);
                processedImage = roiImage.clone();
                continue;
            }
            
            //TO DO 2: 对角线、长宽、面积之间的模糊关系？ 
            switch (objectId + 2)
            {
            case DefectType::defect2: 
                if (diagL > 5 && confidences >= m_vMinDefectProb[objectId])
                {
                    boxesXianshang.push_back(box);
                }
                break;
            case DefectType::defect3:
                if (tempS > 5 && confidences >= m_vMinDefectProb[objectId])
                {
                    boxesDianshang.push_back(box);
                }
                break;
            case DefectType::defect7:
                if (tempS > 5 && confidences >= m_vMinDefectProb[objectId])
                {
                    boxesYiwudian.push_back(box);
                }
                break;
            default:
                break;
            }
		}
        
        //检测线伤：线图上线伤累积面积和对角线， 计算累加值，只要面积或对角线其中一个符合就判NG（要有两个以上瑕疵才能用，不然一个会和上面冲突）
        // if (!detectXianShang(boxesXianshang, m_vMinDefectArea, m_vMinDefectDiag, objectId))
        // {
        //     for (int i = 0; i < boxesXianshang.size(); i++)
        //     {
        //         Scalar scalar = Scalar(0,255,255);
        //         result = (int)DefectType::defect2;
        //         defectResult[0].emplace_back(result);
        //         rectangle(roiImage, boxesXianshang[i], scalar, 5, 8);     
        //         processedImage = roiImage.clone();
        //         continue;
        //     }
        // }
        //检测点伤：
        // if (!detectDianShang(boxesDianshang, m_vMinDefectArea, m_vMinDefectDiag, objectId))
        // {
        //     for (int i = 0; i < boxesDianshang.size(); i++)
        //     {
        //         Scalar scalar = Scalar(0,255,255);
        //         result = (int)DefectType::defect3;
        //         defectResult[0].emplace_back(result);
        //         rectangle(roiImage, boxesDianshang[i], scalar, 5, 8);
        //         processedImage = roiImage.clone();
        //         continue;
        //     }
        // }
        //检测异物点：
        // if (!detectYiWuDian(boxesYiwudian, m_vMinDefectArea, m_vMinDefectDiag, objectId))
        // {
        //     for (int i = 0; i < boxesYiwudian.size(); i++)
        //     {
        //         Scalar scalar = Scalar(0,255,255);
        //         result = (int)DefectType::defect3;
        //         defectResult[0].emplace_back(result);
        //         rectangle(roiImage, boxesYiwudian[i], scalar, 5, 8);
        //         processedImage = roiImage.clone();
        //         continue;
        //     }
        // }
	}
    return true;
}

bool XJAlgorithm::detectXianShang(const std::vector<cv::Rect> &boxesXianshang, const std::vector<float> area, const std::vector<float> diag, const int objectId)
{
    if (boxesXianshang.size() > 1)
    {
        float totalArea = 0;
        float totalDiag = 0;
        for (int i = 0; i < boxesXianshang.size(); i++)
        {
            totalArea += boxesXianshang[i].area();
            totalDiag += sqrt(boxesXianshang[i].width * boxesXianshang[i].width + boxesXianshang[i].height * boxesXianshang[i].height);
        }
        cout << " totalArea  "  << totalArea << endl;
        cout << " totalDiag  "  << totalDiag << endl;
        if (totalArea > area[objectId] || totalDiag > diag[objectId])
        {
            cout << "线伤累加判断瑕疵" << endl;
            return false;
        }
    }
    return true;
}

bool XJAlgorithm::detectDianShang(const std::vector<cv::Rect> &boxesDianshang, const std::vector<float> area, const std::vector<float> diag, const int objectId)
{
    if (boxesDianshang.size() > 1)
    {
        float totalArea = 0;
        float totalDiag = 0;
        for (int i = 0; i < boxesDianshang.size(); i++)
        {
            totalArea += boxesDianshang[i].area();
            totalDiag += sqrt(boxesDianshang[i].width * boxesDianshang[i].width + boxesDianshang[i].height * boxesDianshang[i].height);
        }
        cout << " totalArea  "  << totalArea << endl;
        cout << " totalDiag  "  << totalDiag << endl;
        if (totalArea > area[objectId] || totalDiag > diag[objectId])
        {
            cout << "点伤累加判断瑕疵" << endl;
            return false;
        }
    }
    return true;
}

bool XJAlgorithm::detectYiWuDian(const std::vector<cv::Rect> &boxesYiwudian, const std::vector<float> area, const std::vector<float> diag, const int objectId)
{
    if (boxesYiwudian.size() > 1)
    {
        float totalArea = 0;
        float totalDiag = 0;
        for (int i = 0; i < boxesYiwudian.size(); i++)
        {
            totalArea += boxesYiwudian[i].area();
            totalDiag += sqrt(boxesYiwudian[i].width * boxesYiwudian[i].width + boxesYiwudian[i].height * boxesYiwudian[i].height);
        }
        cout << " totalArea  "  << totalArea << endl;
        cout << " totalDiag  "  << totalDiag << endl;
        if (totalArea > area[objectId] || totalDiag > diag[objectId])
        {
            cout << "异物点累加判断瑕疵" << endl;
            return false;
        }
    }
    return true;
}












