#include "xj_app_running_result.h"
#include "running_status.h"
#include "logger.h"
#include "shared_utils.h"
#include "customized_json_config.h"

using namespace std;
using namespace cv;


AppRunningResult *AppRunningResult::m_instance = nullptr;
mutex AppRunningResult::m_mutex_nglist;
mutex AppRunningResult::m_mutex_map;
mutex AppRunningResult::m_mutex;
mutex AppRunningResult::m_mutex_mes;

AppRunningResult &AppRunningResult::instance()
{
    unique_lock<mutex> lock(m_mutex);
    if(m_instance == nullptr)
    {
        m_instance = new AppRunningResult();
    }
    return *m_instance;
}

void AppRunningResult::Exit()
{
    unique_lock<mutex> lock(m_mutex);
    if(m_instance)
    {
        delete m_instance;
        m_instance = nullptr;
    }
}

AppRunningResult::AppRunningResult():m_iDisplayTotalNumber(0), m_iDisplayTotalDefect(0)
{

}

AppRunningResult::~AppRunningResult()
{
    Exit();
}

void AppRunningResult::setCurrentResultMap(const cv::Mat &frame, const ClassificationResult result, const string sCamID, const int captureTimes)
{
    unique_lock<mutex> lock(m_mutex_map);
    m_mapCurrentProcessedFrameResult[sCamID][captureTimes] = std::make_pair(frame, result);
}
	
void AppRunningResult::getCurrentResult(cv::Mat &frame, ClassificationResult &result, const string sCamID, const int captureTimes, const bool checkCameraStataus)
{
    unique_lock<mutex> lock(m_mutex_map);
    if(m_mapCurrentProcessedFrameResult.find(sCamID) != m_mapCurrentProcessedFrameResult.end())
    {
        if(m_mapCurrentProcessedFrameResult[sCamID].find(captureTimes) != m_mapCurrentProcessedFrameResult[sCamID].end())
        {
            frame = m_mapCurrentProcessedFrameResult[sCamID][captureTimes].first;
            result = m_mapCurrentProcessedFrameResult[sCamID][captureTimes].second;
        }
    }		
}

void AppRunningResult::updateDisplayTotalInfo(const int iTotalNumber, const int iTotalDefect)
{
    if (0 == iTotalNumber && 0 == iTotalDefect) 
    {
        // clear
	    unique_lock<mutex> lock(m_mutex);
        if (m_vTotalDefectIndices.size() != 0)
        {
            vector<int>().swap(m_vTotalDefectIndices);
        }   
	    m_iDisplayTotalNumber = iTotalNumber;
    	m_iDisplayTotalDefect = iTotalDefect;   
    }
    else
    {
	    unique_lock<mutex> lock(m_mutex);
	    m_iDisplayTotalNumber = iTotalNumber;
    	m_iDisplayTotalDefect = iTotalDefect;
    }
}

void AppRunningResult::getNGProductCountList(std::vector<int>& vTotalDefectIndices, int &iTotalNumber, int &iTotalDefect)
{
   vTotalDefectIndices = m_vTotalDefectIndices;
   iTotalNumber = m_iDisplayTotalNumber;
   iTotalDefect = m_iDisplayTotalDefect;
}

void AppRunningResult::recordDefectData(const std::string &prod, const int boardId, const std::string &lotNumber, const int product_no, const vector<ClassificationResult>& defects)
{
    try
    {
        AppDatabase::DefectData newReport(shared_utils::getTime(0), prod, to_string(product_no), lotNumber, to_string(boardId));
        for (auto iDefect : defects)
        {
            if ((int)iDefect >= ClassifierResult::getNameSize() - 1 || (int)iDefect <= (int)ClassifierResultConstant::Good)
            {
                continue;
            }
            const string& sDefectName = ClassifierResult::getName(iDefect);
            if (0 == newReport.defects.count(sDefectName))
            {
                newReport.defects.insert(std::make_pair(sDefectName, 1));
            }        
        }
	    AppDatabase db;
        db.defectAdd(newReport);

        // 更新 m_vTotalDefectIndices
        if (m_vTotalDefectIndices.end() == std::find(m_vTotalDefectIndices.begin(), m_vTotalDefectIndices.end(), product_no))
        {
            m_vTotalDefectIndices.emplace_back(product_no);
        }
    }
    catch(const std::exception& e)
    {
        LogERROR << "extern: Catch std exception: " << e.what();
    }
    
    return;
}

bool AppRunningResult::recordMesData(const std::string &prod, const int boardId, const std::string &lotNumber, 
        const int product_no, const int iTotalNum, const int iNgNum, const vector<ClassificationResult>& defects)
{
    try
    {
        unique_lock<mutex> lock(m_mutex_mes);
        AppDatabase::MesData newMesData(shared_utils::getTime(0), prod, lotNumber, to_string(boardId), to_string(product_no), 
                                        "F205-T", to_string(iTotalNum), to_string(iNgNum));
        newMesData.getShiftTypeFromDate();
        for (auto iDefect : defects)
        {
            if ((int)iDefect >= ClassifierResult::getNameSize() - 1 || (int)iDefect <= (int)ClassifierResultConstant::Good)
            {
                continue;
            }
            newMesData.defects.insert(std::make_pair(ClassifierResult::getName(iDefect), 1));
        }

        AppDatabase db;
        db.mesAdd(newMesData);
    }
    catch(const std::exception& e)
    {
        LogERROR << "extern: Catch std exception: " << e.what();
    }

    return true;
}
