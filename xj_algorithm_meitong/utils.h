
#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <opencv2/opencv.hpp>
#include <time.h>

std::string getTime();
std::string getAppFormatImageNameByCurrentTimeXJ(const int resultType, const int boardId, const int viewId, const int targetId, const std::string &sProductName,const std::string &sProductLot,const std::string &sCustomerEnd="");
void saveImage(const cv::Mat &image, const std::string &sImagePath, const std::string &sImageName, const std::string &sImageExt);

class AppTimer
{
public:
    AppTimer() : beg_(clock_::now()) {}
    void reset() { beg_ = clock_::now(); }
    double elapsed() const 
    {
        return std::chrono::duration_cast<second_>(clock_::now() - beg_).count();
    }

private:
    typedef std::chrono::high_resolution_clock clock_;
    typedef std::chrono::duration<double, std::ratio<1> > second_;
    std::chrono::time_point<clock_> beg_;   
};

template <typename T>
class SafeQueue {
private:
  std::queue<T> m_queue;
  std::mutex m_mutex;
public:
  SafeQueue() {}
  ~SafeQueue() {}

  bool empty() {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_queue.empty();
  }
  
  int size() {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_queue.size();
  }

  void enqueue(T& t) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_queue.push(t);
  }
  
  bool dequeue(T& t) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_queue.empty()) {
      return false;
    }

    t = std::move(m_queue.front());
    m_queue.pop();

    return true;
  }
};


/*==================================================================================================
                    多线程存图工具
===================================================================================================*/
class MultiThreadImageSaveBase
{
public:
    MultiThreadImageSaveBase(const int maxBufferImageNumber);
    ~MultiThreadImageSaveBase();

    void SetMaxBufferImageNumber(const int n);
    void AddImageData(const cv::Mat &image, const std::string &sImagePath, const std::string &sImageName, const std::string &sImageExt);
    
    void HangUpSaveThread();
    void WakeUpSaveThread();

private:
    void init();
    bool runSaveImage();
    bool isQueueEmpty();
    
    struct ImgData
    {
        cv::Mat image;
        std::string sImagePath;  
        std::string sImageName;
        std::string sImageExt;        
    };

    
    std::mutex m_mtx;
    std::condition_variable m_condV;
    SafeQueue<ImgData> m_qImageData;

    bool m_bIsHangUp;
    bool m_bIsStop;
    int m_MaxQueueSize;
    std::thread m_thread;
};

/**
 * @brief find circles on grayscale mat.
 * 
 * @param gray gray scale image, expect a binary mask.
 * @return Output vector of found circles. Each vector is encoded as  3 or 4 element
   floating-point vector \f$(x, y, radius)\f$ or \f$(x, y, radius, votes)\f$ .
 */
std::vector<cv::Vec3f> getCircles(cv::Mat &gray);

/**
 * @brief draw max contour from given binary mask to another.
 * 
 * @param src gray scale image, expect a binary mask.
 * @param dst gray scale image, expect a binary mask.
 * @param maxContour points of found max contour.
 * @param thickness line thickness. 
 */
void drawMaxContour(cv::Mat &src,cv::Mat &dst,std::vector<cv::Point2i> &maxContour,int thickness=-1);

/**
 * @brief merge a list of masks.
 * 
 * @param masks a list of masks.
 * @param dst merged mask.
 */
void mergeMask(std::vector<cv::Mat> &masks,cv::Mat &dst);

/**
 * @brief classify a defect mask to ng or ok by sum of contour areas.
 * 
 * @param mask defect mask.
 * @param thresh sum of contour area threshold.
 * @return true for ng, false for ok.
 */
bool isSumAreaNG(cv::Mat &mask,int thresh);

/**
 * @brief classify a defect mask to ng or ok by max contour area.
 * 
 * @param mask defect mask.
 * @param thresh max contour area threshold.
 * @return true for ng, false for ok.
 */
bool isMaxAreaNG(cv::Mat &mask, double thresh);

/**
 * @brief remove lowwer area contours from mask.
 * 
 * @param mask defect mask.
 * @param thresh area threshold.
 */
void cleanMask(cv::Mat &mask, double thresh);

/**
 * @brief print countours area from maximum to minimum
 * 
 * @param mask defect mask.
 */
void checkContoursArea(cv::Mat &mask);

/**
 * @brief plot countours
 * 
 * @param src defect mask.
 * @param dst canvas, expect a bgr image which have the same size if input mask.
 * @param thickness line thickness.
 * @param color line color.
 */
void plotContours(const cv::Mat &src,cv::Mat &dst,int thickness = 1, cv::Scalar color = cv::Scalar(0, 0, 255));

/**
 * @brief select specific channel from a Mat.
 * 
 * @param input input image.
 * @param output single channel from input image.
 * @param channelIndex channel index.
 */
bool selectChannel(const cv::Mat &input, cv::Mat& output, int channelIndex);

/**
 * @brief draw body poly mask given offset.
 * 
 * @param canvas canvas where mask draws.
 * @param offsets offsets of masks.
 * @param multipliers multipliers to the offsets.
 * @param objRect object bounding box.
 */
bool getOffsetPolyMasks(cv::Mat& canvas, std::vector<cv::Point2i>& offsets, std::vector<std::vector<cv::Point2f>>& multipliers, cv::Rect& objRect);

bool getMaxContour(const std::vector<std::vector<cv::Point>>& contours, int &maxAreaIdx, float& maxContourArea);
bool Hough_Circle(const cv::Mat &image);
bool findHorizontalEdge(const cv::Mat &roiImage, int &x, int iThresh, bool bIsReverse, bool bIsDarkLight);
bool findVerticalEdge(const cv::Mat &roiImage, int &y, int iThresh, bool bIsReverse, bool bIsDarkLight);

cv::Mat makeSquareImage(const cv::Mat &image);

bool xjTemplateMatch(const cv::Mat &image, const cv::Mat &templImage, cv::Rect &matchRC, const float score = 0.5f, const float scale = 1.0f);

void DynThreshold(const cv::Mat &src, const cv:: Mat &srcMean, cv::Mat *result, int offset, int LightDark);
#endif // UTILS_H