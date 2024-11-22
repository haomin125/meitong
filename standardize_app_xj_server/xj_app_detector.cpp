#include "xj_app_detector.h"
#include "xj_app_io_manager.h"
#include "xj_app_tracker.h"
#include "xj_app_data.h"
#include "xj_app_running_result.h"

#include "customized_json_config.h"
#include "running_status.h"
#include "classifier_result.h"

using namespace std;
using namespace boost::property_tree;

AppDetector::AppDetector(const std::shared_ptr<AppDetectorConfig> &pConfig, const std::shared_ptr<BaseTracker> &pTracker,
		const std::shared_ptr<Board> &pBoard) : BaseDetector(pConfig, pTracker, pBoard), m_iCaptureTimes(0), m_iTotalCaptureTimes(0)
{
		initWorkflows<AppWorkflow>();
		// set expose target result flag here if defined
		pConfig->exposeTargetResults(true);
		m_iTotalCaptureTimes  = CustomizedJsonConfig::instance().getVector<int>("CAMERA_TOTAL_IMAGES")[pBoard->boardId()];
		//cout << "m_iTotalCaptureTimes=" << m_iTotalCaptureTimes << endl;
}

bool AppDetector::createBoardTrackingHistory(const int plcDistance)
{
	if (m_pTracker == nullptr || m_pBoard == nullptr)
	{
		return false;
	}

	if (!m_pTracker->getHistory(m_pBoard->boardId()))
	{
		m_pTracker->setHistory(m_pBoard->boardId(), make_shared<AppTrackingHistory>(plcDistance));
	}
	m_pTrackingHistory = dynamic_pointer_cast<AppTrackingHistory>(m_pTracker->getHistory(m_pBoard->boardId()));

	return true;
}

bool AppDetector::addToTrackingHistory()
{
	if (m_pBoard == nullptr)
	{
		return false;
	}

	shared_ptr<AppTrackingObject> pObject = make_shared<AppTrackingObject>();
	pObject->setBoard(m_pBoard);
	m_pTrackingHistory->addToTrackingHistory(pObject);
	return true;
}

bool AppDetector::decideToSave(const cv::Mat &img, const ClassificationResult result, const std::string &imgName)
{
	//不需要在框架存图
	return true;
}

//获取强制信号
int AppDetector::getSignalByPurgeMode(const bool bIsResultOK)
{
	int resultSiganal = (int)PLCSinal::OK;
	switch(m_purgeMode)
	{
		case int(PurgeMode::NORMAL):
			resultSiganal = bIsResultOK ? (int)PLCSinal::OK : (int)PLCSinal::NG;
			break;
		case int(PurgeMode::ALL_OK):
			resultSiganal = (int)PLCSinal::OK;
			break;
		case int(PurgeMode::ALL_NG):
			resultSiganal = (int)PLCSinal::NG;
			break;
		case int(PurgeMode::OK_NG):
			resultSiganal = (productCount() - 1)%2 ?  (int)PLCSinal::OK : (int)PLCSinal::NG;
			break;
		default:
			resultSiganal = (int)PLCSinal::NG;
			break;
	}
	return resultSiganal;
}

//往PLC发送剔除信号
bool AppDetector::sendResultSignalToPLC(const bool bIsResultOK)
{
	//mock视频默认不发信号，如果要发信号，可以在配置文件开启
	if (m_pBoard->getViewCamera(0)->deviceName().substr(0, 4) == "Mock")
	{
		const bool bIsSend = CustomizedJsonConfig::instance().get<bool>("IS_ENABLE_SEND_SIGNAL_IN_MOCK_VIDEO");
		if(!bIsSend)
		{
			return true;
		}
	}
	
	const bool bIsUseIoCard = CustomizedJsonConfig::instance().get<bool>("IS_USE_IO_CARD");
	const int boardID = m_pBoard->boardId();
	int purgeSignal = getSignalByPurgeMode(bIsResultOK);
	if(bIsUseIoCard)
	{
		const vector<int> vAddress = CustomizedJsonConfig::instance().getVector<int>("IO_CARD_CAMERA_RESULT_BIT_ADDRESS");//发送结果信号IO点位
		const int signalTime = CustomizedJsonConfig::instance().get<int>("IO_CARD_SIGNAL_TIME_MS");//信号持续时间
		const int channel = 0;//通道： 0-对应0-7点, 1-对应8-15点
		const int address = vAddress[boardID];//io点(数值：0-7)
		if(purgeSignal == (int)PLCSinal::OK)
		{
			dynamic_pointer_cast<AppIoManagerIOCard>(ioManager())->writeBit(channel, address, 1);
			std::this_thread::sleep_for(std::chrono::milliseconds(signalTime));
			dynamic_pointer_cast<AppIoManagerIOCard>(ioManager())->writeBit(channel, address, 0);
			LogDEBUG << "extern: Board[" << boardID << "] send ok result signal to IO bit address: " << address;
		}
	}
	else
	{
		const int iOKData = CustomizedJsonConfig::instance().get<int>("PLC_MODBUS_TCP_OK_RESULT_VALUE");//OK信号数值
		const int iNGData = CustomizedJsonConfig::instance().get<int>("PLC_MODBUS_TCP_NG_RESULT_VALUE");//NG信号数值
		int data;
		if(bIs_PLC){
			data =  PLC_result;
		}else{
			data = (purgeSignal == (int)PLCSinal::OK) ? iOKData : iNGData;//发送数据	
		// const int data = (purgeSignal == (int)PLCSinal::OK) ? iOKData : iNGData;//发送数据	
		}		
		const vector<int> vAddress = CustomizedJsonConfig::instance().getVector<int>("PLC_MODBUS_TCP_CAMERA_RESULT_REGISTER_ADDRESS");//发送结果信号PLC寄存器地址
		const int address = vAddress[boardID];//PLC寄存器器地址（根据实际信号分配填写）
		if(!dynamic_pointer_cast<AppIoManagerPLC>(ioManager())->writeRegister(address, data))
		{
			LogERROR << "extern: Board[" << boardID << "] failed to send result data:" << data << " to PLC register address:" << address;
			return false;
		}
		LogINFO << "extern: Board[" << boardID << "] send " << (purgeSignal == (int)PLCSinal::OK ? "ok" : "ng") << " result data:" << data << " to PLC register address:" << address;
	}
	return true;
}

bool AppDetector::recordProductResultData(const std::vector<ClassificationResult>& vDefectResults)
{
    int valueTotalNum = 0;
	int valueTotalDefect = 0;
	const int boardID = m_pBoard->boardId();
	const bool bIsMesEnable = CustomizedJsonConfig::instance().get<bool>("IS_USE_MES_SYSTEM");
	if (false == bIsMesEnable && 0 == vDefectResults.size())
	{
		return true;
	}

	const bool bIsUseIoCard = CustomizedJsonConfig::instance().get<bool>("IS_USE_IO_CARD");
	const bool bIsMockCamera = (m_pBoard->getViewCamera(0)->deviceName().substr(0, 4) == "Mock");
	if (bIsMockCamera || bIsUseIoCard || nullptr == ioManager())
	{
		map<string, int> result;
		RunningInfo::instance().GetRunningData().GetRunningData(result);
		valueTotalNum = result["totalNum"];
		valueTotalDefect = result["totalDefect"];
	}
	else
	{
		// Get total number
		valueTotalNum = getRealProductCount();
		// Get total defect
		const int addressTotalDefect = CustomizedJsonConfig::instance().get<int>("PLC_MODBUS_TCP_TOTAL_DEFECT_COUNT_ADDRESS");
		if (!dynamic_pointer_cast<AppIoManagerPLC>(m_pIoManager)->readRegister(addressTotalDefect, valueTotalDefect))
		{
			LogINFO << "extern: Board[" << boardID << "]: read total defect register failed!";
			return false;
		}
		LogINFO << "extern: Board[" << boardID << "] valueTotalDefect: " << valueTotalDefect;
	}
	// 瑕疵品需记录到db.defect表格中
	string product = RunningInfo::instance().GetProductionInfo().GetCurrentProd();
	string lot = RunningInfo::instance().GetProductionInfo().GetCurrentLot();
	if (0 != vDefectResults.size())
	{
		AppRunningResult::instance().recordDefectData(product, boardID, lot, valueTotalNum, vDefectResults);
	}
	if (bIsMesEnable && !bIsMockCamera)
	{
		// 不管OK/NG都需要记录到MES系统中 (此处用产品序号作为index)
		AppRunningResult::instance().recordMesData(product, boardID, lot, valueTotalNum, valueTotalNum, valueTotalDefect, vDefectResults);
	}

	return true;
}

bool AppDetector::purgeBoardResult(const std::vector<std::vector<ClassificationResult>> &result, const bool needCalculateResult)
{
	// if(m_iCaptureTimes == (int)CaptureImageTimes::FIRST_TIMES)
	if(1)
	{
		m_vTotalResult.clear();
	}
	
	//step0:merge all target result
	const bool bIsEnable =  CustomizedJsonConfig::instance().get<bool>("IS_COUNT_MULTI_DEFECTS_PER_TARGET");
	if(!bIsEnable)
	{
		const int viewNum = result.size();
		for(int i = 0; i != viewNum; ++ i)
		{		
			const int targetNum = result[i].size();
			for(int j = 0; j != targetNum; ++j)
			{
				//m_vTotalResult.emplace_back(convertDefectType(result[i][j]));
				m_vTotalResult.emplace_back(result[i][j]);
			}
		}
	}
	else//单target统计多瑕疵
	{
		const map<int, int> &mapDefects = dynamic_pointer_cast<AppWorkflow>(m_workflows[0])->getDefects();
		for(const auto &iter : mapDefects)
		{
			//m_vTotalResult.emplace_back(convertDefectType(iter.first));
			m_vTotalResult.emplace_back(iter.first);
		}

		if(m_vTotalResult.size() == 0)
		{
			m_vTotalResult.emplace_back((int)ClassifierResultConstant::Good);
		}
	}

	// if(m_iCaptureTimes == m_iTotalCaptureTimes)
	if(1)
	{
		//step1: send data to UI
		if(needCalculateResult)
		{
			const int boardID = m_pBoard->boardId();
			RunningInfo::instance().GetRunningData().ProcessClassifyResult(boardID, m_vTotalResult, getRealProductCount());
		}

		// vector中只留下Defect Result
		m_vTotalResult.erase(std::remove(m_vTotalResult.begin(), m_vTotalResult.end(), (int)ClassifierResultConstant::Good), m_vTotalResult.end());
		bool bIsOK = (0 == m_vTotalResult.size());

		//发瑕疵类别给PLC统计计数
		if (bIs_PLC)
		{			
			auto max_it = max_element(m_vTotalResult.begin(), m_vTotalResult.end());
			if(max_it != m_vTotalResult.end()){
				PLC_result = *max_it;
			}
		}		

		// 记录生产信息
		recordProductResultData(m_vTotalResult);
		
		m_vTotalResult.clear();
		
		//step3:send signal to PLC
		return sendResultSignalToPLC(bIsOK);
	}
	else if(m_iCaptureTimes == (int)CaptureImageTimes::UNKNOWN_TIMES)
	{
		return sendResultSignalToPLC(false);
	}
	return true;
}

//参数重置
bool AppDetector::reconfigParameters(const int currentRunStatus)
{
	if(currentRunStatus == (int)RunStatus::PRODUCT_RUN)
	{
		const bool bIsUseIoCard = CustomizedJsonConfig::instance().get<bool>("IS_USE_IO_CARD");
		if(!bIsUseIoCard)
		{
			const bool bIsSend = CustomizedJsonConfig::instance().get<bool>("IS_ENABLE_SEND_SIGNAL_IN_MOCK_VIDEO");
			if(bIsSend)
			{
				setPlcParameters();
			}
			
		}
	}
	
	m_purgeMode = (const int)RunningInfo::instance().GetProductSetting().GetFloatSetting((const int)ProductSettingFloatMapper::PURGE_SIGNAL_TYPE);
	for(const auto &iter : m_workflows)
	{
		if(!dynamic_pointer_cast<AppWorkflow>(iter)->reconfigParameters(currentRunStatus))
		{
			return false;
		}
		dynamic_pointer_cast<AppWorkflow>(iter)->setCaptureImageTimes((int)CaptureImageTimes::UNKNOWN_TIMES);
	}

	return true;
}

void AppDetector::setImageSave(const shared_ptr<MultiThreadImageSaveBase> &pSave)
{
	for (const auto &itr : m_workflows)
	{
        dynamic_pointer_cast<AppWorkflow>(itr)->setImageSave(pSave);
	}
}

void AppDetector::setPlcParameters()
{
	const int boardID = m_pBoard->boardId();
	if(boardID != 0)
	{
		return;
	}

	ptree params;
	string sNodeName = ("UISetting.setup.plcParams");
	std::stringstream ss = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
	read_json(ss, params);
	for(const auto &param:params)
	{
		if(!param.second.get_child_optional("index") || !param.second.get_child_optional("id"))
		{
			LogERROR << "UISetting.setup.plcParams sub params is less";
			continue;
		}

		const int index = param.second.get<float>("index");
		const string sId = param.second.get<string>("id");
		if(param.second.get_child_optional("address"))
		{
			const int address = param.second.get<float>("address");
			const int value = (const int)RunningInfo::instance().GetProductSetting().GetFloatSetting(index);
			dynamic_pointer_cast<AppIoManagerPLC>(ioManager())->writeRegister(address, value);
		}
	}
}

void AppDetector::setCaptueImageTimesBySignal()
{
	const int boardID = m_pBoard->boardId();
	if(m_iTotalCaptureTimes > 1)//大于一次拍照
	{
		/*
		if(m_iCaptureTimes == (int)CaptureImageTimes::UNKNOWN_TIMES || m_iCaptureTimes == m_iTotalCaptureTimes)
		{
			int value = 0;
			const int address = CustomizedJsonConfig::instance().getVector<int>("PLC_MODBUS_TCP_CAMERA_INPUT_REGISTER_ADDRESS")[boardID];			
			dynamic_pointer_cast<AppIoManagerPLC>(ioManager())->readRegister(address, value);
			if(value == 1)
			{
				LogINFO << "extern: Board[" << boardID << "] read input data for first image capture signal:" << value << " from PLC register address:" << address;
				m_iCaptureTimes = (int)CaptureImageTimes::FIRST_TIMES;
			}
			else
			{
				m_iCaptureTimes = (int)CaptureImageTimes::UNKNOWN_TIMES;
			}
		}
		else if(m_iCaptureTimes == (int)CaptureImageTimes::FIRST_TIMES && m_iCaptureTimes < m_iTotalCaptureTimes)
		{
			m_iCaptureTimes = (int)CaptureImageTimes::SECOND_TIMES;
		}
		else if(m_iCaptureTimes == (int)CaptureImageTimes::SECOND_TIMES && m_iCaptureTimes < m_iTotalCaptureTimes)
		{
			m_iCaptureTimes = (int)CaptureImageTimes::THIRD_TIMES;
		}
		*/
		int value = 0;
		const int address = CustomizedJsonConfig::instance().getVector<int>("PLC_MODBUS_TCP_CAMERA_INPUT_REGISTER_ADDRESS")[boardID];			
		dynamic_pointer_cast<AppIoManagerPLC>(ioManager())->readRegister(address, value);		
			cout << " @@@@@@@@@@@@" << endl;	
		cout << "extern: Board[" << boardID << "] read input data for first image capture signal:" << value << " from PLC register address:" << address << endl;;
		if(value == 1)
		{
			cout << " !!!!!!!!!!!!!!!!" << endl;
			LogINFO << "extern: Board[" << boardID << "] read input data for first image capture signal:" << value << " from PLC register address:" << address;
			m_iCaptureTimes = (int)CaptureImageTimes::FIRST_TIMES;
			dynamic_pointer_cast<AppIoManagerPLC>(ioManager())->writeRegister(address, 0);
		}
		else if (value == 2)
		{
			cout << " !!!!!!!!!!!!!!!!" << endl;
			LogINFO << "extern: Board[" << boardID << "] read input data for first image capture signal:" << value << " from PLC register address:" << address;
			m_iCaptureTimes = (int)CaptureImageTimes::SECOND_TIMES;
			dynamic_pointer_cast<AppIoManagerPLC>(ioManager())->writeRegister(address, 0);
		}		
		else
		{
			m_iCaptureTimes = (int)CaptureImageTimes::UNKNOWN_TIMES;
		}
	}
	else
	{
		m_iCaptureTimes = (int)CaptureImageTimes::FIRST_TIMES;
	}

	for (const auto &itr : m_workflows)
	{
		dynamic_pointer_cast<AppWorkflow>(itr)->setCaptureImageTimes(m_iCaptureTimes);
	}
}

void AppDetector::setCaptueImageTimesByProductCount()
{
	if(m_iCaptureTimes < m_iTotalCaptureTimes)
	{
		m_iCaptureTimes++;
	}
	else
	{
		m_iCaptureTimes  = (int)CaptureImageTimes::FIRST_TIMES;
	}

	for (const auto &itr : m_workflows)
	{
		dynamic_pointer_cast<AppWorkflow>(itr)->setCaptureImageTimes(m_iCaptureTimes);
	}

}

void AppDetector::updateProductCountofWorkflow()
{
	for (const auto &itr : m_workflows)
	{
		dynamic_pointer_cast<AppWorkflow>(itr)->setProductNumber(productCount());
	}
}

void AppDetector::updateProductCountofWorkflow(const int iProductCount)
{
	m_iPlcProductNumber = iProductCount;
	for (const auto &itr : m_workflows)
	{
		dynamic_pointer_cast<AppWorkflow>(itr)->setProductNumber(iProductCount);
	}
}

int AppDetector::getRealProductCount()
{
	int iResult = 0;
	const bool bIsUseIoCard = CustomizedJsonConfig::instance().get<bool>("IS_USE_IO_CARD");
	if (m_pBoard->getViewCamera(0)->deviceName().substr(0, 4) == "Mock" || bIsUseIoCard)
	{
		iResult = productCount();
	}
	else
	{
		iResult = m_iPlcProductNumber;
	}
	return iResult;
}
