
#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <opencv2/opencv.hpp>
#include <time.h>
#include "safe_queue.h"

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


class MultiThreadImageSaveBase
{
public:
    MultiThreadImageSaveBase(const int maxBufferImageNumber);
    ~MultiThreadImageSaveBase();

    void SetMaxBufferImageNumber(const int n);
    void AddImageData(const cv::Mat &image, const std::string &sImagePath, const std::string &sImageName, const std::string &sImageExt = ".png");
    
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

    SafeQueue<ImgData> m_qImageData;

    std::mutex m_mtx;
    std::condition_variable m_condV;
    std::thread m_thread;

    bool m_bIsHangUp;
    bool m_bIsStop;
    int m_MaxQueueSize;  
};

/**
 * @brief get unix time
 * @param prod <input> time in format "%Y-%m-%d %H:%M:%S"
 * @return unix time
 */
std::string getUnixTime(const std::string& sTimeStr);
/**
 * @brief determine whether two double data are equal within the allowable accuracy range
 * @param x1 <input> comparison number
 * @param x2 <input> number of Comparables
 * @param precision <input> compare accuracy, when the interpolation between the two is less than this value, it indicates equality
 * @return Return whether it is equal
 * */
bool is_double_equal(const double& x1, const double& x2, const double& precision);



#endif // UTILS_H
