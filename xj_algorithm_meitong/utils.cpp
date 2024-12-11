#include "utils.h"

using namespace cv;
using namespace std;

std::string getAppFormatImageNameByCurrentTimeXJ(const int resultType, const int boardId, const int viewId, const int targetId, const std::string &sProductName,const std::string &sProductLot,const std::string &sCustomerEnd)
{

    std::string curTime = getTime();
    curTime.erase(std::remove(curTime.begin(), curTime.end(), ':'), curTime.end());
    std::string sFileName =  "C" + to_string(resultType) + "-TM" + curTime + "-B" + to_string(boardId) + 
                    "-V" + to_string(viewId) + "-T" + to_string(targetId) + "-" + sProductName + "-" + sProductLot + (sCustomerEnd.empty()? sCustomerEnd : (+ "-" + sCustomerEnd)) ;
    return sFileName;
}

void saveImage(const cv::Mat &image, const std::string &sImagePath, const std::string &sImageName, const std::string &sImageExt)
{
    CV_Assert(!image.empty());
    std::string sSvaeImage = sImagePath + "/" + sImageName + sImageExt;
    cv::imwrite(sSvaeImage, image);
}

std::string getTimeString(const time_t &ttime, const std::string &format, const std::chrono::system_clock::duration duration)
{
    struct tm mtime;
    char buffer[128];
    std::ostringstream oss;

#ifdef __linux__
    localtime_r(&ttime, &mtime);
#elif _WIN64
    localtime_s(&mtime, &ttime);
#endif
    strftime(buffer, sizeof(buffer), format.c_str(), &mtime);
    if (duration != std::chrono::system_clock::duration::zero())
    {
        std::ostringstream oss;
        oss << buffer << "." << std::setw(5) << std::setfill('0') << duration.count() / 10000;
        return oss.str().c_str();
    }
    else
    {
        return std::string(buffer);
    }
}

std::string getTime()
{
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::chrono::system_clock::duration duration = now.time_since_epoch();
    duration -= std::chrono::duration_cast<std::chrono::seconds>(duration);
    time_t ttime = std::chrono::system_clock::to_time_t(now);
    return getTimeString(ttime, "%Y%m%d-%H:%M:%S", duration);
}


MultiThreadImageSaveBase::MultiThreadImageSaveBase(const int maxBufferImageNumber):
        m_bIsHangUp(false),
        m_bIsStop(false),
        m_MaxQueueSize(maxBufferImageNumber)
{
    init();
}

MultiThreadImageSaveBase::~MultiThreadImageSaveBase()
{
    m_bIsStop = true;
    m_bIsHangUp = false;
    m_condV.notify_all();
    if(m_thread.joinable()) 
    {
		m_thread.join();
    }

}

void MultiThreadImageSaveBase::init()
{
    m_thread = thread(&MultiThreadImageSaveBase::runSaveImage, this);
}

bool MultiThreadImageSaveBase::isQueueEmpty()
{
    unique_lock<mutex> lck(m_mtx);
    return m_qImageData.empty();
}

void MultiThreadImageSaveBase::SetMaxBufferImageNumber(const int n)
{
    unique_lock<std::mutex> lck(m_mtx);
    m_MaxQueueSize =  n;
}

void MultiThreadImageSaveBase::AddImageData(const cv::Mat &image, const std::string &sImagePath, const std::string &sImageName, const std::string &sImageExt)
{
    unique_lock<std::mutex> lck(m_mtx);
    if(m_qImageData.size() > m_MaxQueueSize)
    {
        return;
    }

    ImgData data;
    data.sImagePath = sImagePath;
    data.sImageName = sImageName;
    data.sImageExt = sImageExt;
    data.image = image.clone();
    m_qImageData.enqueue(data);
}

void MultiThreadImageSaveBase::HangUpSaveThread()
{
    //unique_lock<std::mutex> lck(m_mtx);
    m_bIsHangUp = true;
}

void MultiThreadImageSaveBase::WakeUpSaveThread()
{
    unique_lock<mutex> lck(m_mtx);
    if(!m_qImageData.empty())
    {
        m_bIsHangUp = false;
        m_condV.notify_one();
    } 
}

bool MultiThreadImageSaveBase::runSaveImage()
{
    while (!m_bIsStop)
    {
        unique_lock<mutex> lck(m_mtx);
        if (m_qImageData.empty() || m_bIsHangUp)
        {
            m_condV.wait(lck);
        }

        ImgData imgData;
        m_qImageData.dequeue(imgData);
        if (imgData.sImageName.empty() || imgData.image.empty())
        {
            continue;
        }
        saveImage(imgData.image, imgData.sImagePath, imgData.sImageName, imgData.sImageExt);
        lck.unlock();
    }
    return true;
}

void drawMaxContour(Mat &src,Mat &dst,vector<Point2i> &maxContour,int thickness)
{
    dst=src.clone();
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(src, 
                    contours, 
                    hierarchy,
                    RetrievalModes::RETR_EXTERNAL,
                    ContourApproximationModes::CHAIN_APPROX_NONE);
    int  maxAreaIdx = 0;
    double area = 0.0;

    if (contours.size()==0)
    {
        return;
    }

    for( int i = 0; i < contours.size(); i++ )
    {
        double tempArea = contourArea( contours[i] );

        if (tempArea > area)
        {
            area = tempArea;
            maxAreaIdx = i;
        }
    }

    dst=0;
    drawContours(dst, 
                    contours,
                    maxAreaIdx,
                    Scalar(255),
                    thickness,
                    8,
                    hierarchy);
    maxContour=contours[maxAreaIdx];
}


vector<Vec3f> getCircles(Mat &gray)
{
    // smooth it, otherwise a lot of false circles may be detected
    Mat temp=gray.clone();
    GaussianBlur( temp, temp, Size(9, 9), 2, 2 );
    vector<Vec3f> circles;
    HoughCircles(temp, circles, HOUGH_GRADIENT,
                 2, temp.rows/4, 200, 100 );

    return circles;
}


void mergeMask(vector<Mat> &masks, Mat &dst)
{
    dst=masks[0].clone();
    for(size_t i=0;i<masks.size();++i)
    {
        bitwise_or(dst,masks[i],dst);
    }
}

bool isSumAreaNG(Mat &mask,int thresh)
{
    int totalArea=countNonZero(mask);
    //compare thresh with sum area of contours
    if (totalArea>=thresh)
        return true;
    else
        return false;
}

bool isMaxAreaNG(Mat &mask,double thresh)
{
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(mask, 
                    contours, 
                    hierarchy,
                    RetrievalModes::RETR_EXTERNAL,
                    ContourApproximationModes::CHAIN_APPROX_NONE);

    //find max contour
    double area = 0.0;
    for( int i = 0; i < contours.size(); i++ )
    {
        double tempArea = contourArea( contours[i] );
        if (tempArea > area)
        {
            area = tempArea;
        }
    }
    //compare thresh with max contour area
    if (area>=thresh)
        return true;
    else
        return false;
}

void cleanMask(Mat &mask,double thresh)
{

    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(mask, 
                    contours, 
                    hierarchy,
                    RetrievalModes::RETR_EXTERNAL,
                    ContourApproximationModes::CHAIN_APPROX_NONE);

    //draw contours which area larger than thresh
    for( int i = 0; i < contours.size(); i++ )
    {
        double tempArea = contourArea( contours[i] );
        if (tempArea > thresh)
        {
        drawContours(mask, 
                        contours,
                        i,
                        Scalar(255),
                        -1,
                        8,
                        hierarchy);
        }
    }

}


void checkContoursArea(Mat &mask)
{
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(mask, 
                    contours, 
                    hierarchy,
                    RetrievalModes::RETR_EXTERNAL,
                    ContourApproximationModes::CHAIN_APPROX_NONE);
    int  maxAreaIdx = 0;
    double area = 0.0;

    vector<double> areas;
    if (contours.size()==0)
    {
        return;
    }

    for( int i = 0; i < contours.size(); i++ )
    {
        double tempArea = contourArea( contours[i] );
        areas.push_back(tempArea);
    }
    sort(areas.rbegin(),areas.rend());
    for (auto v: areas)
        cout << v << endl;
}


void plotContours(const Mat &src,Mat &dst, int thickness, Scalar color)
{
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(src, 
                    contours, 
                    hierarchy,
                    RetrievalModes::RETR_EXTERNAL,
                    ContourApproximationModes::CHAIN_APPROX_NONE);

    if (contours.size()==0)
    {
        return;
    }
    if (dst.channels()==1)
    {
    cvtColor(dst,dst,COLOR_GRAY2BGR);
    }
 
    drawContours(dst,contours,-1,color);

}


bool selectChannel(const Mat &input,Mat& output,int channelIndex)
{ 
    if(channelIndex > input.channels()){return false;}
    vector<Mat> channels(input.channels());
    split(input,channels);
    output = channels[channelIndex];
    return true;
}


bool getOffsetPolyMasks(Mat& canvas,vector<Point2i>& offsets,vector<vector<Point2f>>& multipliers, Rect& objRect)
{
    if(offsets.size()!=multipliers.size()){return false;}
    
    vector<vector<Point2i>> polys;
    for(int i=0;i<offsets.size();++i)
    {
        vector<Point2i> poly;
        Point2i offset=offsets[i];
        vector<Point2f> mults=multipliers[i];
        for (int j=0;j<mults.size();++j){poly.emplace_back(offset.x+mults[j].x*objRect.width, offset.y+mults[j].y*objRect.height);}
        polys.push_back(poly);
    }
    
    drawContours(canvas,polys,-1,Scalar(255),-1);
    return true;
}

// bool Hough_Circle(const cv::Mat &image)
// {
//     Mat grayImg = image.clone();
//     Mat thresholdImg;
//     Canny(grayImg, thresholdImg, 10, 20);
//     vector<Vec3f> vCircles;
//     HoughCircles(thresholdImg, vCircles, 1, 1, 20, 12, 21, 12, 12);
//     return true;
// }

bool getMaxContour(const vector<vector<Point>>& contours, int &maxAreaIdx, float& maxContourArea)
{
    maxAreaIdx = -1;
    maxContourArea = 0.0;
    Rect box;
    int widthThr1 = 220;
    int widthThr2 = 260;
    int heightThr1 = 220;
    int heightThr2 = 260;
    for (int i = 0; i != contours.size(); i++)
    {
        box = boundingRect(contours[i]);
        // if ((widthThr1<box.width<widthThr2) & (heightThr1<box.height<heightThr2))
        if (widthThr1 < box.width & widthThr2 > box.width & heightThr1 < box.height & heightThr2 > box.height )
        {
            cout << "widthThr1.width:" << box.width << endl;
            cout << "widthThr1.height:" << box.height << endl;
            // double tempArea = contourArea(contours[i]);
            double tempArea = box.width * box.height;
            cout << "widthThr1.I  :" << i  << "  " << tempArea << endl;
            if (tempArea > maxContourArea)
            {
                maxContourArea = tempArea;
                maxAreaIdx = i;
            }
        }
    }
    return true;
}

bool findHorizontalEdge(const cv::Mat &roiImage, int &x, int iThresh, bool bIsReverse, bool bIsDarkLight)
{
     x  = -1;
    Mat grayImage, rowImage;
    if(roiImage.channels() != 1)
    {
        cvtColor(roiImage, grayImage, COLOR_BGR2GRAY);
    }
    else
    {
        grayImage = roiImage;
    }
    cvtColor(roiImage, grayImage, COLOR_BGR2GRAY);
    reduce(grayImage, rowImage, 0, cv::REDUCE_AVG, CV_32F);
    if(!bIsReverse)
    {
        for(int i = 0; i != rowImage.cols; ++i)
        {
            float pV = rowImage.at<float>(0, i);
            if(bIsDarkLight ? (pV >= iThresh):(pV < iThresh)) 
            {
                x = i;
                break;
            }
        }
    }
    else
    {
        for(int i = rowImage.cols-1; i > 0; --i)
        {
            float pV = rowImage.at<float>(0, i);
            if(bIsDarkLight ? (pV >= iThresh):(pV < iThresh)) 
            {
                x = i;
                break;
            }
        }
    }
    return x != -1;
}

bool findVerticalEdge(const Mat &roiImage, int &y, int iThresh, bool bIsReverse, bool bIsDarkLight)
{
    y = -1;
    Mat grayImage, colImage;
    if(roiImage.channels() != 1)
    {
        cvtColor(roiImage, grayImage, COLOR_BGR2GRAY);
    }
    else
    {
        grayImage = roiImage;
    }
    
    reduce(grayImage, colImage, 1, REDUCE_AVG, CV_32F);
    if(!bIsReverse)
    {
        for(int i = 0; i != colImage.rows; ++i)
        {
            float pV = colImage.at<float>(i, 0);
            if(bIsDarkLight ? (pV >= iThresh):(pV < iThresh)) 
            {
                y = i;
                break;
            }
        }
    }
    else
    {
        for(int i = colImage.rows-1; i > 0; --i)
        {
            float pV = colImage.at<float>(i, 0);
            if(bIsDarkLight ? (pV >= iThresh):(pV < iThresh)) 
            {
                y = i;
                break;
            }
        }
    }
    return y != -1;
}

Mat makeSquareImage(const Mat &image)
{
    const int w = image.cols;
    const int h = image.rows;
    if(w < h)
    {
        Mat dstImage;
        Mat mask = Mat::zeros(Size(h - w, h), image.type());
        hconcat(image, mask, dstImage);
        return dstImage;
    }
    else if(h < w)
    {
        Mat dstImage;
        Mat mask = Mat::zeros(Size(w, w - h), image.type());
        vconcat(image, mask, dstImage);
        return dstImage;
    }
    return image;
}

bool xjTemplateMatch(const cv::Mat &image, const cv::Mat &templImage, cv::Rect &matchRC, const float score, const float scale)
{
    Mat resizedImage;
    resize(image, resizedImage, Size(0,0), scale, scale);
    Mat matchResult(resizedImage.rows - templImage.rows + 1, resizedImage.cols - templImage.cols + 1, CV_32FC1);
    matchTemplate(resizedImage, templImage, matchResult, TM_CCOEFF_NORMED);
    double dMaxVal; //分数最大值
    Point ptMaxLoc; //最大值坐标
    minMaxLoc(matchResult, 0, &dMaxVal, 0, &ptMaxLoc); //寻找结果矩阵中的最大值
    if(dMaxVal >= score)
    {
        matchRC = Rect(ptMaxLoc.x/scale, ptMaxLoc.y/scale, templImage.cols/scale, templImage.rows/scale);
        return true;
    }
    return false;
}

/// @brief 利用局部阈值分割图像(动态阈值)
/// @param src  输入图像
/// @param srcMean 输入平滑后的图像
/// @param result 分割后区域
/// @param offset  灰度值偏移量（对比度）
/// @param LightDark 提取区域类型（ ‘light’, ‘dark’, ‘equal’, ‘not_equal’）
/// @return void
void DynThreshold(const cv::Mat &src, const cv:: Mat &srcMean, cv::Mat *result, int offset, int LightDark)
{
        if (src.empty() || srcMean.empty() || offset == 0)
        {
            return;
        }
        int Row = src.rows;
        int Col = src.cols;
        int SubVal = 0; // 临时变量：两张图像像素差
        // 开始遍历像素
        for (int i = 0; i < Row; i++)
        {
            const uchar *datasrc = src.ptr<uchar>(i);         // 获取行指针 指针访问图像像素
            const uchar *datasrcMean = srcMean.ptr<uchar>(i); // 获取行指针 指针访问图像像素
            uchar *dataresult = result->ptr<uchar>(i);        // 获取行指针 指针访问图像像素
            for (int j = 0; j < Col; j++)
            {
                switch (LightDark)
                {
                case 1:
                    SubVal = datasrc[j] - datasrcMean[j];
                    if (SubVal >= offset)
                    {
                        dataresult[j] = 255;
                    }
                    break;
                case 2:
                    SubVal = datasrcMean[j] - datasrc[j];
                    if (SubVal >= offset)
                    {
                        dataresult[j] = 255;
                    }
                    break;
                case 3:
                    SubVal = datasrc[j] - datasrcMean[j];
                    if (SubVal >= -offset && SubVal <= offset)
                    {
                        dataresult[j] = 255;
                    }
                    break;
                case 4:
                    SubVal = datasrc[j] - datasrcMean[j];
                    if (SubVal <= -offset && SubVal >= offset)
                    {
                        dataresult[j] = 255;
                    }
                    break;
                default:
                    break;
                }
            }
        }
}

