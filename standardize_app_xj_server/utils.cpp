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
    m_condV.notify_one();
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
    unique_lock<std::mutex> lck(m_mtx);
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
    try
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
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }
    return true;
}

std::string getUnixTime(const std::string& sTimeStr)
{
    if (sTimeStr.empty())
    {
        return "";
    }
    
    //时间字符串转成UNIX时间戳
    struct tm tm; 
    memset(&tm, 0, sizeof(tm));
    sscanf(sTimeStr.c_str(), "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon--;

    std::stringstream stss;
    const time_t ptm_st = mktime(&tm);
    return to_string(ptm_st);
}

bool is_double_equal(const double& x1, const double& x2, const double& precision)
{
    return fabs(x1 - x2) < precision;
}


