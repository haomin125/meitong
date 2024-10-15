#include "xj_algorithm.h"
#include "data.h"
#include "utils.h"


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
#define  EXTEND_LENGTH 60
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

        //传统
        //检测物性
        m_isCheckWuxing = m_stParamsB.vecFParams.at("IS_CHECK_WUXING")[m_stParamsA.boardId];
        m_wuxingWidth = m_stParamsB.vecFParams.at("WUXING_X")[m_stParamsA.boardId];
        m_wuxingHeight = m_stParamsB.vecFParams.at("WUXING_Y")[m_stParamsA.boardId];

        // m_vDisableDefectType = m_stParamsB.vecFParams.at("DISABLE_DEFECT_TYPE_DET_CAM" +  to_string(m_stParamsA.boardId + 1));

        m_vMinDefectArea.clear();
        m_vMinDefectProb.clear();
        m_vMinDefectDiag.clear();
        m_vMinDefectProb_C.clear();
        m_vMinDefectArea_C.clear();
        m_vMinDefectDiag_C.clear();
        m_vMinDefectProb_NC.clear();
        m_vMinDefectArea_NC.clear();
        m_vMinDefectDiag_NC.clear();
        m_vDisableDefectType.clear();
        
        m_maxBatchSize = m_stParamsB.vecFParams.at("MAX_BATCH_SIZE")[m_stParamsA.boardId];
        const float NmsThresh = m_stParamsB.vecFParams.at("MNS_THRESHOLD")[m_stParamsA.boardId];
        const float ConfThresh = m_stParamsB.vecFParams.at("CONF_THRESHOLD")[m_stParamsA.boardId];
        const string model_path = m_stParamsB.strParams.at("MODEL_PATH_CAM" +  to_string(m_stParamsA.boardId + 1));

        //YOLOV8
        const int maskThr = m_stParamsB.vecFParams.at("MASK_THR")[m_stParamsA.boardId]; 
        const int detbox_num = inputWidth * inputHeight / 32 / 32 * 21; //yolo标准式可化简为：w*h/32/32*(4*4+2*2+1*1)
        //YOLOV8

        if(m_stParamsB.fParams.at("IS_USE_MODEL_CONFIG"))   //? 此处没有执行
        {
            string json_path = model_path;
            string tmp = ".engine";
            json_path = json_path.replace(json_path.find(tmp), tmp.length(), ".json");

            ifstream ifs(json_path, std::ios_base::in);
            ptree rootNode;
            vector<float> vTest1, vTest2;
            read_json(ifs, rootNode);
            for(ptree::iterator itr=rootNode.begin();itr!=rootNode.end();++itr)
            {
                string sKey = itr->first;   //itr->first获取当前元素的键，itr->second获取当前元素的值
                if(sKey == "MIN_PROB")
                {   
                    for(const auto &it : rootNode.get_child(sKey)) //it为常量引用，表示不能修改其指向的内容；auto表示编译器自动推断it类型
                    {
                        m_vMinDefectProb.emplace_back(it.second.get_value<float>());
                    }
                }
                else if(sKey == "MIN_AREA")
                {
                    for(const auto &it : rootNode.get_child(sKey))
                    {
                        m_vMinDefectArea.emplace_back(it.second.get_value<float>());
                    }
                }
            }
        }
        else    //此处执行
        {
            //中心区center
            m_vMinDefectProb_C = m_stParamsB.vecFParams.at("DEFECT_MIN_PROB_CAM_C" + to_string(m_stParamsA.boardId + 1));
            m_vMinDefectArea_C = m_stParamsB.vecFParams.at("DEFECT_MIN_AREA_CAM_C" + to_string(m_stParamsA.boardId + 1));
            m_vMinDefectDiag_C = m_stParamsB.vecFParams.at("DEFECT_MIN_DIAG_CAM_C" + to_string(m_stParamsA.boardId + 1));
            //非中心区not center
            m_vMinDefectProb_NC = m_stParamsB.vecFParams.at("DEFECT_MIN_PROB_CAM_NC" + to_string(m_stParamsA.boardId + 1));
            m_vMinDefectArea_NC = m_stParamsB.vecFParams.at("DEFECT_MIN_AREA_CAM_NC" + to_string(m_stParamsA.boardId + 1));
            m_vMinDefectDiag_NC = m_stParamsB.vecFParams.at("DEFECT_MIN_DIAG_CAM_NC" + to_string(m_stParamsA.boardId + 1));
        }

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
    if(m_stParamsA.boardId == is_board[0] || m_stParamsA.boardId == is_board[1] || m_stParamsA.boardId == is_board[2]){return defectResult;}
    //step1: locate box
    Rect roiRect;
    if(!locateBox(image, roiRect, nCaptureTimes)) //当定位失败时，result被强制为defect1=2
    {
        cout << "[ERROR] locateBox" << endl; 
        result = (int)DefectType::defect1;
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
    Mat roiImage = image(roiRect);

    // Mat roiImage = image.clone();

    //step2: detect defect by cv

    //STEP1：定义一个空的掩膜图,对应光学区/非光学区
    int maskW1 = roiImage.cols;
    int maskH1 = roiImage.rows;
    // cv::Mat mask1 = cv::Mat::zeros(maskH1, maskW1, CV_8UC1);
    // cv::Mat mask2 = mask1.clone();  //
    cv::Point center(maskW1/2, maskH1/2);
    int radius1 = 420;
    // cv::circle(mask1, center1, radius1, cv::Scalar(255), -1);

    //STEP2：定义一个空的掩膜图,对应前景区/背景区
    cv::Mat mask2 = cv::Mat::zeros(maskH1, maskW1, CV_8UC1);  //CV_8UC1：8位单通道图像
    // cv::Point center2 = center1;
    int radius2 = 1280;     //留了一点点背景区
    cv::circle(mask2, center, radius2, cv::Scalar(255), -1);
    // imwrite("/opt/test/mask2.png", mask2);
    // imwrite("/opt/test/roiImage.png", roiImage);
    //STEP3: 屏蔽背景区域,用mask2和输入图片做bitwise_and
    cv::Mat roiImage_;
    cv::bitwise_and(roiImage, roiImage, roiImage_, mask2);
    // roiImage.copyTo(roiImage_,mask2);
    // imwrite("/opt/test/roiImage_.png", roiImage_);

    // 红光暗场屏蔽区域
    if(nCaptureTimes == 2 && m_stParamsA.boardId == 0)
    // if(m_stParamsA.boardId == 1)
    {
        int radius3 = 1100;
        cv::Mat mask3 = cv::Mat::ones(maskH1, maskW1, CV_8UC1);  //CV_8UC1：8位单通道图像
        cv::circle(mask3, center, radius3, cv::Scalar(0), -1);
        cv::Mat dst;
        roiImage_.copyTo(dst,mask3);
        roiImage_ = dst.clone();
    }

    //step3:split ROI 
    vector<Rect> vTargetRect;
    vector<Mat> vTargetImage;
    if(!extractROI(roiImage_, roiRect, vTargetRect, vTargetImage))
    {
        cout << "ERROR extractROI" << endl; 
        result = (int)DefectType::defect1;
        defectResult[0].emplace_back(result);
        imwrite("extractROI.png", roiImage);
        return defectResult;
    }

    //step2.1: 传统检测物性
    if (m_isCheckWuxing == 1 && !checkWuxing(roiRect)){
        // cout << "传统检测物性" <<endl;
        result = (int)DefectType::defect10;
        defectResult[0].emplace_back(result);
        return defectResult;
    }


    //step4: get detect result by DL
    for (int i = 0; i < vTargetImage.size(); i++)   // 
    {
        result = (int)DefectType::good;
        if(!detectByDL(maskW1, maskH1, radius1, radius2, center, roiImage_, vTargetRect[i], vTargetImage[i], result, defectResult, processedImage))
        {
            result = (int)DefectType::defect1;
            defectResult[0].emplace_back(result);
        }
        // cout << "defectResult----  " << i << "____" << defectResult[i].size() << endl;

        for (size_t j = 0; j < defectResult[i].size() ; j++)
        {
            cout  << "____" << defectResult[i][0] << endl;
        }

        //step5: save image
        if(m_stParamsA.pSaveImageMultiThread && m_stParamsB.fParams.at("IS_SAVE_PROCESS_IMAGE"))
        {
            if(m_stParamsA.saveImageType == (int)SaveImageType::ALL || ((result != (int)DefectType::good) && m_stParamsA.saveImageType == (int)SaveImageType::NG_ONLY))
            {
                string sFilePath = (result == (int)DefectType::good) ? OK_SOURCE_IMAGE_SAVE_PATH : NG_SOURCE_IMAGE_SAVE_PATH;         
                string sCustomerEnd = "CNT" + to_string(productCount) + "-PIC" + to_string(nCaptureTimes) + "_" + m_stParamsB.vCameraNames[m_stParamsA.boardId];
                string sFileName = getAppFormatImageNameByCurrentTimeXJ(result, m_stParamsA.boardId, 0, i, m_stParamsA.sProductName, m_stParamsA.sProductLot, sCustomerEnd);
                m_stParamsA.pSaveImageMultiThread->AddImageData(vTargetImage[i], sFilePath, sFileName, ".png");
            }
        }
    }  
    //在roiImage上画圆，可视化区分中心区/非中心区
    cv::circle(processedImage, center, radius1, Scalar(0, 255, 0), 2, cv::LINE_8);

    // for (int i = 0; i < vTargetRect.size(); i++)
    // {
    //     rectangle(processedImage, vTargetRect[i], Scalar(255, 255, 255), 5, 8); 
    // }  
                      
    return defectResult;
}

bool XJAlgorithm::locateBox(const Mat& image, Rect &box, const int nCaptureTimes)
{
    Rect globalRC(0, 0, image.cols, image.rows);
    Rect roiRC(m_roiOffsetX, m_roiOffsetX, m_roiWidth, m_roiHeight);
    roiRC &= globalRC;
    if(roiRC.height<=0 || roiRC.width<=0)
    {
        return false;
    }
    Mat roiImage = image(roiRC);

    //step1: preprocess
    // int thresholdValue  = m_stParamsB.fParams.at("BOX_BINARY_THRESHOLD");
    // int areaThreshold  = m_stParamsB.fParams.at("BOX_BINARY_AREA_THRESHOLD"); //大约2500*2500
    int thresholdValue(5);
    int areaThreshold(40000);
    int thresh_binary;
    if (nCaptureTimes == 1)
    // if(m_stParamsA.boardId == 0 ||m_stParamsA.boardId == 2)
    {  
        thresholdValue = m_stParamsB.vecFParams.at("BOX_BINARY_THRESHOLD" + to_string(m_stParamsA.boardId + 1))[0];
        areaThreshold  = m_stParamsB.vecFParams.at("BOX_BINARY_AREA_THRESHOLD" + to_string(m_stParamsA.boardId + 1))[0]; //大约2500*2500
        thresh_binary = 1;  //
    }
    if(nCaptureTimes == 2)
    // if(m_stParamsA.boardId == 1 ||m_stParamsA.boardId == 3)
    {
        thresholdValue = m_stParamsB.vecFParams.at("BOX_BINARY_THRESHOLD" + to_string(m_stParamsA.boardId + 1))[1];
        areaThreshold  = m_stParamsB.vecFParams.at("BOX_BINARY_AREA_THRESHOLD" + to_string(m_stParamsA.boardId + 1))[1]; //大约2500*2500
        thresh_binary = 0;
    }
    //通过传统算法判断是4种图像中的哪种图像？根据判断结果设置BOX_BINARY_THRESHOLD和BOX_BINARY_AREA_THRESHOLD参数


    Mat grayImage, binaryImage;
    cvtColor(roiImage, grayImage, COLOR_RGB2GRAY);
    imwrite("grayImage.png", grayImage);
    //blur(grayImage, grayImage, Size(3, 3));
    // threshold(grayImage, binaryImage, thresholdValue, 255, THRESH_BINARY_INV); //an 取反
    threshold(grayImage, binaryImage, thresholdValue, 255, thresh_binary); //an 取反
    imwrite("binaryImage.png", binaryImage);

    //step2: find contour
    vector<vector<Point>> contours;
    findContours(binaryImage, contours, 1, CHAIN_APPROX_SIMPLE);
    if(contours.size() == 0)
    {
        return false;
    }

    Mat roiImage_ = roiImage.clone();
    cout << "contours.size()___" << contours.size() << endl;
    for (size_t i = 0; i < contours.size(); i++)
    {
        box = boundingRect(contours[i]);
        rectangle(roiImage_, box, Scalar(0,255,255),4);
    }
    // drawContours(roiImage_, contours, 936, Scalar(0,255,255), 4);
    imwrite("rectangle.png", roiImage_);

    //step3: get max contour
    int maxIdx = 0;
    float maxArea = 0;
    getMaxContour(contours, maxIdx, maxArea);
    if(maxArea <= 0)
    {
        cout << "[ERROR] locateBox  maxArea<=0 " << endl;
        return false;
    }

    //step4: get max bounding box and return result
    box = boundingRect(contours[maxIdx]);
    cout << "box.width:" << box.width << endl;
    cout << "box.height:" << box.height << endl;

    box.x += m_roiOffsetX;  //得到配置文件中的ROI_OFFSET_X=0
    box.y += m_roiOffsetY;  //得到配置文件中的m_roiOffsetY=0

    //step5: extend foreground ROI edge
    box.x -= EXTEND_LENGTH; //左上角横坐标
    box.y -= EXTEND_LENGTH; //左上角纵坐标
    box.width += EXTEND_LENGTH * 2;
    box.height += EXTEND_LENGTH * 2;
    box &= globalRC;
    if(box.height<=0 || box.width<=0)
    {
        return false;
    }

    return true;
}

bool XJAlgorithm::checkWuxing(Rect &box)
{
    if (m_wuxingWidth < ((box.width-EXTEND_LENGTH*2)-40) || m_wuxingWidth > ((box.width-EXTEND_LENGTH*2)+40) || m_wuxingHeight < ((box.height-EXTEND_LENGTH*2)-40) || m_wuxingHeight > ((box.height-EXTEND_LENGTH*2)+40))
    {
        cout<<"box.width-80:  "<< box.width-80 <<endl;
        cout<<"box.height-80:  "<< box.height-80 <<endl;
        cout<<"box.width+80:  "<< box.width+80 <<endl;
        cout<<"box.height+80:  "<< box.height+80 <<endl;
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

// // add
// //计算两个box之间的最小距离
// int XJAlgorithm::calculateMinDistance(const cv::Rect& box1, const cv::Rect& box2)
// {
//     int dx = std::max(box1.x - (box2.x + box2.w), box2.x - (box1.x + box1.w));
//     int dy = std::max(box1.y - (box2.y + box2.h), box2.y - (box1.y + box1.h));
//     return std::max(dx, 0) + std::max(dy, 0); // 返回横向和纵向距离之和
// }

// // cv::Rect getRect() const {
// //     return cv::Rect(x, y, w, h);
// cv::Rect XJAlgorithm::getRect(cv::Rect& box){    //?
//     return cv::Rect(x, y, w, h);
// }

// //判断两个box是否相交
// bool XJAlgorithm::isIntersecting(const cv::Rect& box1, const cv::Rect& box2) {
//     // return (box1.getRect() & box2.getRect()).area() > 0; // 使用 OpenCV 检查相交
//     return (getRect(box1) & getRect(box2)).area() > 0;
// }

// // 计算相交的面积
// int XJAlgorithm::calculateIntersectionArea(const cv::Rect& box1, const cv::Rect& box2) {
//     // return (box1.getRect() & box2.getRect()).area();    return (getRect(box1) & getRect(box2)).area();
// }

// // 计算相并的面积
// int XJAlgorithm::calculateUnionArea(const cv::Rect& box1, const cv::Rect& box2) {
//     // return (box1.getRect() & box2.getRect()).area();
//     return (getRect(box1) | getRect(box2)).area();
// }

// //
// int XJAlgorithm::itrXianshang(std::vector<cv::Rect>& boxes, float& diagLXianshang) 
// {
    
//     // 遍历所有 box，计算相近边的最小距离和相交情况
//     for (size_t i = 0; i < boxes.size(); ++i) {
//         for (size_t j = i + 1; j < boxes.size(); ++j) {
            
//             const Box& box1 = boxes[i];
//             const Box& box2 = boxes[j];

//             // 计算最小距离
//             int minDistance = calculateMinDistance(box1, box2);
//             std::cout << "Box " << i << " 和 Box " << j << " 之间的最小距离: " << minDistance << std::endl;
//             if (minDistance < 20){
//                 float diagLi = std::sqrt(boxes[i].width*boxes[i].width + boxes[i].height*boxes[i].height);
//                 float diagLj = std::sqrt(boxes[j].width*boxes[j].width + boxes[j].height*boxes[j].height);
//                 diagLXianshang = diagLXianshang + diagLi + diagLj;
//             }

//             // 判断是否相交
//             if (isIntersecting(box1, box2)) {
//                 int intersectionArea = calculateIntersectionArea(box1, box2);
//                 int unionArea = calculateUnionArea(box1, box2);
//                 if ((intersectionArea/unionArea) > 0.01){   //交并比
//                     float diagLi = std::sqrt(boxes[i].width*boxes[i].width + boxes[i].height*boxes[i].height);
//                     float diagLj = std::sqrt(boxes[j].width*boxes[j].width + boxes[j].height*boxes[j].height);
//                     diagLXianshang = diagLXianshang + diagLi + diagLj;
//                 }
//                 std::cout << "Box " << i << " 和 Box " << j << " 相交，交集面积: " << intersectionArea << std::endl;
//                 if 
//             } else {
//                 std::cout << "Box " << i << " 和 Box " << j << " 不相交。" << std::endl;
//             }
//         }
//     }

//     return 0;
// }
// // add

// bool XJAlgorithm::detectByDL(Mat &roiImage, const Rect &roiRect, Mat &targetImage, int &result, vector<vector<int>> &defectResult, Mat &processedImage)
bool XJAlgorithm::detectByDL(int &maskW1, int &maskH1, int &radius1, int &radius2, cv::Point &center1, cv::Mat &roiImage, const cv::Rect &roiRect, cv::Mat &targetImage, int &result, std::vector<std::vector<int>> &defectResult, cv::Mat &processedImage)
{
    processedImage = roiImage.clone();
    //step1:pre-process
    vector<Mat> images;
    vector<Rect> roiRect_;
    vector<vector<YoloOutputDetect>> detectionOutput;
    targetImage = preprocessImage(targetImage);
    // Mat targetImage_ = imread("bad.png");
    images.push_back(targetImage);
    roiRect_.push_back(roiRect);
    
    //step2:detect by DL
    // vector<vector<Detection>> vResult;
	// cout<<"images.size():  "<< images.size() <<endl;
    m_tensorrtYoloDL->getDetectionResult(images, detectionOutput);  

    // step3:post-process
    // float scale = std::max(roiImage.rows, roiImage.cols) / (float)TARGET_SIZE;  //归一化系数
	// cout<<"detectionOutput.size():  "<< detectionOutput.size() <<endl;

    int numDianshang = 0;
    std::vector<cv::Rect> boxesDianshang;

    for	(int j=0; j<detectionOutput.size(); j++) {
		Rect box;
		int objectId;
		float confidences;
		float tempS;
        float diagL;
        
        // float diagXianshang = 0;
        // cv::Mat maskXianshang = cv::Mat::zeros(640, 640, CV_8UC1);

        int numXianshang = 0;
        std::vector<cv::Rect> boxesXianshang;
        // float diagLXianshang;

	    // cout<<"detectionOutput.at(j).size():  "<< detectionOutput.at(j).size() <<endl;
		for	(int i=0; i<detectionOutput.at(j).size(); i++) {
            YoloOutputDetect &det = detectionOutput[j][i];
			objectId = detectionOutput.at(j).at(i).id;
			// cout<<"objectId " << objectId<<endl;

			box.x = int(detectionOutput.at(j).at(i).box.x + roiRect.x);    //相对扣图的坐标
			box.y = int(detectionOutput.at(j).at(i).box.y + roiRect.y);    //相对扣图的坐标
			box.width = int(detectionOutput.at(j).at(i).box.width);
			box.height = int(detectionOutput.at(j).at(i).box.height);

			//tempS = box.width*box.width*m_fPPS*m_fPPS + box.height*box.height*m_fPPS*m_fPPS;    //转化为物理尺寸的对角线平方
            //tempS = box.width*m_fPPS*box.height*m_fPPS;    //物理尺寸的面积
            tempS = box.area();     
            diagL = std::sqrt(box.width*box.width + box.height*box.height); //瑕疵对角线长度

            

            //防止边缘附近的背景上的瑕疵误检
            //定义一个空的掩膜图，对应ROI区
            cv::Mat mask3 = cv::Mat::zeros(maskH1, maskW1, CV_8UC1);
            cv::Mat mask4 = mask3.clone();
            cv::Point center3 = center1;
            int radius3 = radius2-116;    //缩窄背景区域
            cv::circle(mask3, center3, radius3, cv::Scalar(255), -1);
            //对每个检测到的瑕疵定义一个掩膜图
            cv::Rect roi(box.x, box.y, box.width, box.height);
            cv::rectangle(mask4, roi, cv::Scalar(255), -1);
            cv::Mat mask5;
            cv::bitwise_and(mask3, mask4, mask5);
            int whiteArea = cv::countNonZero(mask5);
            // std::cout << "相交区域的面积： " << whiteArea << std::endl;

            float centerX = box.x + box.width/2;    
            float centerY = box.y + box.height/2;
            //计算产品中心到瑕疵中心的距离，与radius1比较大小，由此判断调用松/紧参数
            float d_defect2center = std::sqrt((centerX-maskW1/2)*(centerX-maskW1/2) + (centerY-maskH1/2)*(centerY-maskH1/2));
            if (d_defect2center >= radius2){    //如果检测到的瑕疵的中心点在背景区（防止模型异常或早期的训练数据影响）
                continue;
            }
            if (d_defect2center <= radius1){
                m_vMinDefectArea = m_vMinDefectArea_C;
                m_vMinDefectProb = m_vMinDefectProb_C;
                m_vMinDefectDiag = m_vMinDefectDiag_C;
            }
            if (d_defect2center > radius1){
                m_vMinDefectArea = m_vMinDefectArea_NC;
                m_vMinDefectProb = m_vMinDefectProb_NC;
                m_vMinDefectDiag = m_vMinDefectDiag_NC;
            }

            
            if (tempS > m_vMinDefectArea[(int)det.id] && diagL >= m_vMinDefectDiag[(int)det.id] && det.confidence >= m_vMinDefectProb[(int)det.id] && whiteArea >= 1)
            {  
                Scalar scalar = Scalar(0,0,255);
                // value = (int)LocalLevel::C;
                result = objectId + 2;  //good是1，瑕疵从2开始
                defectResult[0].emplace_back(result);
                //rectangle(m_workflowProcessedImage, box, scalar, 2, 8);
                rectangle(roiImage, box, scalar, 5, 8);     
                processedImage = roiImage.clone();          
            
            }	  
            
            //TO DO 2: 对角线、长宽、面积之间的模糊关系？
			//string sizeStr = "S" + getStringFromFloat(tempS,1) + " C" + getStringFromFloat(detectionOutput.at(j).at(i).confidence);
            if (objectId == 1)  //线伤
            {   
                if (diagL > 5)
                {
                    numXianshang += 1;
                    boxesXianshang.push_back(box);    
                }
            }    

            if (objectId == 2)  //点伤
            {
                numDianshang += 1;
                boxesDianshang.push_back(box); 	
            }
		}
        
        //在同一张小图上检测更小的线伤
        for (int i = 1; i < boxesXianshang.size(); i++)
        {
            Scalar scalar = Scalar(0,0,255);
            // value = (int)LocalLevel::C;
            result = objectId + 2;  //good是1，瑕疵从2开始
            defectResult[0].emplace_back(result);
            //rectangle(m_workflowProcessedImage, box, scalar, 2, 8);
            rectangle(roiImage, boxesXianshang[i-1], scalar, 5, 8);     
            processedImage = roiImage.clone();
        }                  		
	}

    //在这里判断点伤（基于整张ROI图），以及别的需要基于整张ROI图判断的瑕疵，以及存ROI图

    return true;
    // for (int i = 0; i != vResult.size(); ++i)
    // {
    //     for (int j = 0; j != vResult[i].size(); ++j)
    //     {
    //         Detection &det = vResult[i][j];
    //         Rect box((det.bbox[0] - det.bbox[2] / 2) * scale, (det.bbox[1] - det.bbox[3] / 2) * scale, det.bbox[2] * scale, det.bbox[3] * scale);
    //         float boxArea = m_stParamsB.fParams.at("IS_USE_MODEL_CONFIG")?(det.bbox[2]*det.bbox[3])/((float)TARGET_SIZE*(float)TARGET_SIZE):box.area();
    //         box.x += roiRect.x;
    //         box.y += roiRect.y;
    //         // int width = box.width;
    //         // int height = box.height;
    //         if (!isDisableDet(det.class_id))
    //         {
    //             if (boxArea > m_vMinDefectArea[(int)det.class_id] && det.conf >= m_vMinDefectProb[(int)det.class_id])
    //             {
    //                 result = det.class_id + 2;
    //                 defectResult[0].emplace_back(result);
    //                 rectangle(processedImage, box, Scalar(0, 0, 255), 5);
    //                 stringstream ss;
    //                 string text1;
    //                 ss.setf(ios::fixed);
    //                 ss << setprecision(2) << det.conf;
    //                 text1 = ss.str();
    //                 string text = to_string((int)det.class_id) + " " + text1;
    //                 int y = box.y-70>0?box.y:box.y+70;
    //                 int x = box.x+box.width+200<processedImage.cols?box.x:processedImage.cols-200;
    //                 ft2->putText(processedImage, text, cv::Point(x, y), 70, DRAW_NG_COLOR, cv::FILLED, cv::LINE_AA, true);
    //                 if(m_stParamsB.fParams.at("IS_DEBUG"))
    //                 {
    //                     // std::cout << "cam[" << m_stParamsA.boardId + 1 << "]    ---det.class_id:" << det.class_id << "---det.conf:" << det.conf << "---box.area:" << box.area() << std::endl;
    //                     std::cout << "cam[" << m_stParamsA.boardId + 1 << "]    ---det.class_id:" << det.class_id << "---det.conf:" << det.conf << "---box.area:" << boxArea << std::endl;
    //                 }
    //             }
    //         }
    //     }
    // }
    // return true;

}

// bool XJAlgorithm::isDisableDet(const float defectType)
// {
//     for(size_t i = 0; i != m_vDisableDefectType.size(); ++i)
//     {
//         if(m_vDisableDefectType[i] ==  defectType)  //比较输出结果和配置文件类别
//         {
//             return true;
//         }
//     }
//     return false;
// }



















