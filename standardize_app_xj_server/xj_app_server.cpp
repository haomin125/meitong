#include "xj_app_server.h"
#include "xj_app_tracker.h"
#include "xj_app_data.h"
#include "xj_app_io_manager.h"
#include "xj_app_running_result.h"

#include "haikang_camera.h"
#include "haikang_camera_config.h"

#include "itek_camera.h"
#include "itek_camera_config.h"

#include "mock_camera.h"
#include "mock_camera_config.h"
#include "running_status.h"
#include "shared_utils.h"
#include "timer_utils.hpp"

#include "customized_json_config.h"

#include <boost/foreach.hpp>
#include <thread>
#include <chrono>
#include <numeric>

#define DATABASE_ELAPSE_MINUTE 60     // the minutes to update report in database

using namespace boost::property_tree;
using namespace std;
using namespace cv;


atomic_bool XJAppServer::m_bIsStop(false);
mutex XJAppServer::m_productNameMutex;

XJAppServer::XJAppServer(const std::shared_ptr<CameraManager> &pCameraManager, const int plcDistance) : m_pCameraManager(pCameraManager),
																										m_pIoManager(nullptr),
																										m_pSaveImageMultiThread(nullptr),
                                                                                                        m_pProductLine(nullptr),
                                                                                                        m_iPlcDistance(plcDistance),
                                                                                                        m_sProductName(RunningInfo::instance().GetProductionInfo().GetCurrentProd())
{
	m_pDetectors.clear();
	m_cameras.clear();
}

XJAppServer::DetectorRunStatus::Status XJAppServer::DetectorRunStatus::getStatus()
{
	int runStatus = RunningInfo::instance().GetProductionInfo().GetRunStatus();
	int testStatus = RunningInfo::instance().GetTestProductionInfo().GetTestStatus();
	int triggerCameraImageStatus = RunningInfo::instance().GetTestProductionInfo().GetTriggerCameraImageStatus();

	if (runStatus == 1)
	{
		return XJAppServer::DetectorRunStatus::Status::PRODUCT_RUN;
	}

	if (testStatus == 1)
	{
		return XJAppServer::DetectorRunStatus::Status::TEST_RUN;
	}

	if (triggerCameraImageStatus == 1)
	{
		return XJAppServer::DetectorRunStatus::Status::TEST_RUN_WITH_CAMERA;
	}

	return XJAppServer::DetectorRunStatus::Status::NOT_RUN;
}

bool XJAppServer::initXJApp(std::shared_ptr<BaseIoManager> &pPlcManager)
{
	m_pIoManager = pPlcManager;

	// initial json configuration
#ifdef __linux__
	ClassifierResult appDefects("standardize_app", "/opt/config/standardize_app_config.json");
#else
	ClassifierResult appDefects("standardize_app", "C:\\opt\\config\\standardize_app_config.json");
#endif

	// initial product line
	if (!initProductLine())
	{
		LogERROR << "Initial product line failed";
		return false;
	}

	//bIsMultiThread为false：所有detector共用一个存图线程， bIsMultiThread为true:每个workflow单独一个存图线程
	const bool bIsMultiThread =  CustomizedJsonConfig::instance().get<bool>("IS_USE_MULTI_THREAD_PER_CAMERA");
	if(!bIsMultiThread)
	{
		const int maxBufferImageNumber =  CustomizedJsonConfig::instance().get<int>("MAX_BUFFER_IMAGE_SAVE_NUM");
		m_pSaveImageMultiThread = make_shared<MultiThreadImageSaveBase>(maxBufferImageNumber);
	}

	// initial tracker, all detector will shared the same tracker here
	shared_ptr<AppTracker> pTracker = make_shared<AppTracker>((const int)m_pProductLine->boardsSize());

	// initial board with multiple views, each view has 1 target in product line
	// if you have multiple targets here. please see me for details -- Luo Bin
	m_pDetectors.clear();
	for (int boardIdx = 0; boardIdx < (int)m_pProductLine->boardsSize(); boardIdx++)
	{
		shared_ptr<AppTimer> pTimer = make_shared<AppTimer>();
		m_vTimer.emplace_back(pTimer);
		shared_ptr<AppTimer> pTimer_total = make_shared<AppTimer>();
		m_vTimer_total.emplace_back(pTimer_total);

		shared_ptr<Board> pBoard = m_pProductLine->getBoard(boardIdx);
		shared_ptr<ProductLineConfig> pProdlineConfig = m_pProductLine->getConfig();

		// set initial margin in each target of the view
		if (!initTargetMarginInBoard(boardIdx))
		{
			LogERROR << "Initial target margin in board[" << boardIdx << "] failed";
			return false;
		}

		// initial detector config, the workflow number should be pulled out from product line configuration
		shared_ptr<AppDetectorConfig> pDetectorConfig = make_shared<AppDetectorConfig>(pProdlineConfig->viewsPerBoard(boardIdx));

		// initial detector
		shared_ptr<AppDetector> pDetector = make_shared<AppDetector>(pDetectorConfig, pTracker, pBoard);
		if (!pDetector->createBoardTrackingHistory(m_iPlcDistance))
		{
			LogERROR << "Create board tracking history failed";
			return false;
		}

		if(!bIsMultiThread && m_pSaveImageMultiThread)
		{
			pDetector->setImageSave(m_pSaveImageMultiThread);
		}
		
		pDetector->setIoManager(pPlcManager);
		m_pDetectors.emplace_back(ServerDetector(pDetector));
	}

	// set the running data status count to the last board ID
	const bool bIsUseKey = CustomizedJsonConfig::instance().get<bool>("IS_USE_BOARDID_KEY");
	if(bIsUseKey)
	{
		const int iBoardIdKey = CustomizedJsonConfig::instance().get<int>("USE_BOARDID_KEY");
		RunningInfo::instance().GetRunningData().boardIdKey(iBoardIdKey);
	}
	else
	{
		RunningInfo::instance().GetRunningData().useBoardIdKey(false);
	}
	
	// log default product name which has be initialed in constructor
	LogDEBUG << "Default Product: " << m_sProductName;

	return true;
}

bool XJAppServer::initProductLine()
{
	try
	{
		ptree ptreeConfig;
		string sNodeName = ("PRODUCT_LINE_CONFIG");
		std::stringstream ss = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
		read_json(ss, ptreeConfig);

		//board to  view config
		map<int, vector<int>> boardToView;
		ptree ptreeBoardToView = ptreeConfig.get_child("BOARD_TO_VIEW");
		int boardIdx = 0;
		for(const auto &iterBoard : ptreeBoardToView)
		{
			vector<int> vView;
			for(const auto &iterView : iterBoard.second)
			{
				const int camIdx = stoi(iterView.second.data());
				vView.emplace_back(camIdx);
			}
			boardToView[boardIdx] = vView;
			boardIdx++;
		}

		//view to board config
		map<int, int> viewToBoard;
		ptree ptreeViewToBoard = ptreeConfig.get_child("VIEW_TO_BOARD");
		int viewIdx = 0;
		for(const auto &iterView : ptreeViewToBoard)
		{
			const int boardIdx = stoi(iterView.second.data());
			viewToBoard[viewIdx] = boardIdx;
			viewIdx++;
		}

		//target int view config
		std::vector<int> targetsInView = CustomizedJsonConfig::instance().getVector<int>("PRODUCT_LINE_CONFIG.TAERGET_IN_VIEW");
		shared_ptr<AppProductLineConfig> pProdlineConfig = make_shared<AppProductLineConfig>(boardToView, viewToBoard, targetsInView);
		if (!pProdlineConfig->isValid())
		{
			LogERROR << "Invalid product line configuration";
			return false;
		}

		// save camera to board mapping in running data for trace back board id from camera index
		RunningInfo::instance().GetRunningData().setCameraToBoard(viewToBoard);

		pProdlineConfig->setConfig();
		m_pProductLine = make_shared<ProductLine>(pProdlineConfig);

	}
	catch (const exception &e)
	{
		LogERROR << "Catch standard exception: " << e.what();
		return false;
	}
	return m_pProductLine != nullptr;
}

bool XJAppServer::initTargetMarginInBoard(const int boardId)
{
	shared_ptr<Board> pBoard = m_pProductLine->getBoard(boardId);
	shared_ptr<ProductLineConfig> pProdlineConfig = m_pProductLine->getConfig();

	if (pBoard == nullptr || pProdlineConfig == nullptr)
	{
		LogERROR << "Invalid board or product line config";
		return false;
	}

	for (int viewIdx = 0; viewIdx < (int)pBoard->viewsSize(); viewIdx++)
	{
		shared_ptr<View> pView = pBoard->getView(viewIdx);
		for(int targetIdx = 0; targetIdx < pProdlineConfig->targetsInViewPerBoard(boardId); targetIdx++)
		{
			shared_ptr<TargetObject> pTarget = pView->getTarget(targetIdx);
			vector<unsigned int> margins = dynamic_pointer_cast<AppProductLineConfig>(pProdlineConfig)->boundBoxConfig(boardId, viewIdx);
			pTarget->setBoundMargin(margins[0], margins[1], margins[2], margins[3]);
		}
	}

	return true;
}

void XJAppServer::initProductCameras()
{
	shared_ptr<ProductLineConfig> pProductLineConfig = m_pProductLine->getConfig();

	//MvGvspPixelType
	map<string, MvGvspPixelType> mapPixelFormat = 
					{{"Mono8", PixelType_Gvsp_Mono8}, 
					 {"BayerRG8", PixelType_Gvsp_BayerRG8}, 
					 {"RGB8", PixelType_Gvsp_RGB8_Packed}};

	const int numCam = m_pProductLine->getConfig()->totalViews();
	vector<string> vCamIP = CustomizedJsonConfig::instance().getVector<string>("CAMERA_IP");
	vector<string> vCamType = CustomizedJsonConfig::instance().getVector<string>("CAMERA_TYPE");
	vector<int> vCamTimeOut = CustomizedJsonConfig::instance().getVector<int>("CAMERA_TIMEOUT");
	vector<int> vImageWidth = CustomizedJsonConfig::instance().getVector<int>("CAMERA_IMAGE_WIDTH");
	vector<int> vImageHeight = CustomizedJsonConfig::instance().getVector<int>("CAMERA_IMAGE_HEIGHT");	
	vector<int> vOffsetX = CustomizedJsonConfig::instance().getVector<int>("CAMERA_OFFSET_X");
	vector<int> vOffsetY = CustomizedJsonConfig::instance().getVector<int>("CAMERA_OFFSET_Y");
	vector<float> vFps = CustomizedJsonConfig::instance().getVector<float>("CAMERA_FPS");
	vector<float> vExposure = CustomizedJsonConfig::instance().getVector<float>("CAMERA_EXPOSURE");
	vector<float> vGamma = CustomizedJsonConfig::instance().getVector<float>("CAMERA_GAMMA");
	vector<float> vGain = CustomizedJsonConfig::instance().getVector<float>("CAMERA_GAIN");
	vector<int> vTriggerMode = CustomizedJsonConfig::instance().getVector<int>("CAMERA_TRIGGER_MODE");
	vector<string> vPixelFormat = CustomizedJsonConfig::instance().getVector<string>("CAMERA_PIXEL_FORMAT");
	vector<int> vDebouncerTime = CustomizedJsonConfig::instance().getVector<int>("CAMERA_LINE_DEBOUNCER_TIME");
	const vector<bool> vIsSoftTrigger = CustomizedJsonConfig::instance().getVector<bool>("CAMERA_SOFT_TRIGGER_ENABLE");
	
	map<int, pair<string, string>> cameraNames;
	for(int camIdx = 0; camIdx != numCam; camIdx ++)
	{
		cameraNames[camIdx] = make_pair("CAM" + to_string(camIdx), vCamIP[camIdx]);
		if(vCamType[camIdx] == "LineScan")
		{
			const int overlap = 0;
			const string sTriggerMode = vTriggerMode[camIdx] ? "On": "Off";
			shared_ptr<ItekCameraConfig> pConfig = make_shared<ItekCameraConfig>(vCamTimeOut[camIdx], "null.pb", vImageWidth[camIdx], vImageHeight[camIdx], vFps[camIdx], vOffsetX[camIdx], vOffsetY[camIdx], vExposure[camIdx], sTriggerMode, 8192, 200, 10, 0.0, vPixelFormat[camIdx]);			
			pConfig->setOverlap(overlap);
        	pConfig->setBufferNumbers(1);	
			pConfig->setGammaEnable(true);
			pConfig->setGamma(vGamma[camIdx]);
			if(vIsSoftTrigger[camIdx])
			{
				pConfig->setSoftTriggerWaitTime(10);
				pConfig->setSoftTriggerEnable(true);
			}

			shared_ptr<ItekCamera> pCamera = make_shared<ItekCamera>(cameraNames[camIdx].first, cameraNames[camIdx].second, pConfig);	
			m_pCameraManager->addCamera(pCamera);
			m_cameras[pProductLineConfig->getBoard(camIdx)].emplace_back(pCamera);	
		}
		else
		{
			if(mapPixelFormat.find(vPixelFormat[camIdx]) == mapPixelFormat.end())
			{
				LogERROR << "CAM" << camIdx << " pixel format not support in /opt/config/configuration.json file, right type such as: Mono8, BayerRG8, RGB8";
				return;
			}
			const int pixelFormat = mapPixelFormat[vPixelFormat[camIdx]];
			shared_ptr<HaikangCameraConfig> pConfig = make_shared<HaikangCameraConfig>(vCamTimeOut[camIdx], "null.pb", vImageWidth[camIdx], vImageHeight[camIdx], vFps[camIdx], vOffsetX[camIdx], vOffsetY[camIdx], pixelFormat, vExposure[camIdx]);
			pConfig->setBalanceAutoEnable(false);
			pConfig->setBayerGammaType(MV_CC_GAMMA_TYPE::MV_CC_GAMMA_TYPE_USER_CURVE);
			pConfig->setTriggerMode(vTriggerMode[camIdx]);
			pConfig->setGainEnable(true);
			pConfig->setGain(vGain[camIdx]);
			pConfig->setGammaEnable(true);
			pConfig->setGamma(vGamma[camIdx]);
			pConfig->setLineDebouncerTime(vDebouncerTime[camIdx]);//过滤信号防止误触发
			if(vIsSoftTrigger[camIdx])
			{
				pConfig->setSoftTriggerWaitTime(10);
				pConfig->setSoftTriggerImageNodeNumber(1);
				pConfig->setSoftTriggerEnable(true);
			}
			
			shared_ptr<HaikangCamera> pCamera = make_shared<HaikangCamera>(cameraNames[camIdx].first, cameraNames[camIdx].second, pConfig);	
			m_pCameraManager->addCamera(pCamera);
			m_cameras[pProductLineConfig->getBoard(camIdx)].emplace_back(pCamera);		
		}
	}

}

void XJAppServer::initMockCameras(const string &filePath)
{
	const string sMockPath = CustomizedJsonConfig::instance().get<string>("MOCK_VIDEO_PATH");
	const string sType = CustomizedJsonConfig::instance().get<string>("MOCK_VIDEO_TYPE");
	const vector<string> vMockFile = CustomizedJsonConfig::instance().getVector<string>("MOCK_VIDEO_FILES");
	const int numCam = m_pProductLine->getConfig()->totalViews();

	// shared_ptr<MockCameraConfig> pConfig[numCam];
	shared_ptr<MockDirectoryConfig> pConfig[numCam];
	map<int, pair<string, string>> cameraNames;
	for(int camIdx = 0 ; camIdx < numCam; camIdx ++)
	{
		cameraNames[camIdx] = make_pair("CAM" + to_string(camIdx), "Mock Camera " + to_string(camIdx + 1));
		// pConfig[camIdx] = make_shared<MockCameraConfig>("null.pb", sMockPath + vMockFile[camIdx]);
		pConfig[camIdx] = make_shared<MockDirectoryConfig>("null.pb", sMockPath + vMockFile[camIdx], sType);
	}

	// init mock cameras
	// initCameras<MockCamera, MockCameraConfig>(cameraNames, pConfig);
	initCameras<MockDirectory, MockDirectoryConfig>(cameraNames, pConfig);
}

bool XJAppServer::initCameraManager(const string &cameraType, const string &filePath)
{
	m_sCameraType = cameraType;
	if (cameraType == "haikang")
	{
		LogDEBUG << "Adding Haikang cameras";
		initProductCameras();
	}
	else if (cameraType == "mock")
	{
		LogDEBUG << "Adding Mock cameras";
		initMockCameras(filePath);
	}

	// set cameras in product line
	for (const auto &itr : m_cameras)
	{
		m_pProductLine->setBoardCamera(itr.first, itr.second);
	}

	return true;
}

bool XJAppServer::camerasAreReady(const int boardId)
{
	vector<string> startedCameras;

	startedCameras.clear();
	for (const auto &itr : m_cameras[boardId])
	{
		if (itr->cameraStarted())
		{
			startedCameras.emplace_back(itr->cameraName());
		}
	}

	return !startedCameras.empty();
}

bool XJAppServer::updateDatabaseReport(const int boardId)
{
	string product = RunningInfo::instance().GetProductionInfo().GetCurrentProd();
	string lot = RunningInfo::instance().GetProductionInfo().GetCurrentLot();

	return RunningInfo::instance().GetRunningData().saveToDatabase(product, boardId, lot);
}

bool XJAppServer::initBoardParameters(const int boardId)
{
	// reset save image type, size, and save image handler
	m_pDetectors[boardId].m_pDetector->saveImageType((const int)RunningInfo::instance().GetProductSetting().GetFloatSetting((const int)ProductSettingFloatMapper::SAVE_IMAGE_TYPE));
	m_pDetectors[boardId].m_pDetector->saveImageSize((const int)RunningInfo::instance().GetProductSetting().GetFloatSetting((const int)ProductSettingFloatMapper::IMAGE_SAVE_SIZE));
	m_pDetectors[boardId].m_pDetector->resetSaveImageHandler();

	DetectorRunStatus runStatus;
	return m_pDetectors[boardId].m_pDetector->reconfigParameters((int)runStatus.getStatus());
}

bool XJAppServer::isProductSwitched()
{
	unique_lock<mutex> lock(m_productNameMutex);
	string currentProductName = RunningInfo::instance().GetProductionInfo().GetCurrentProd();
	if (currentProductName != m_sProductName)
	{
		m_sProductName = currentProductName;
		return true;
	}
	return false;
}

bool XJAppServer::runDetector(const int boardId)
{
	if (boardId < 0 || boardId >= m_pDetectors.size())
	{
		LogERROR << "extern: Invalid board Id: " << boardId;
		return false;
	}

	const vector<double> vScale = CustomizedJsonConfig::instance().getVector<double>("CAMERA_IMAGE_DRAW_SCALE");
	const vector<int> vThickness = CustomizedJsonConfig::instance().getVector<int>("CAMERA_IMAGE_DRAW_THICKNESS");
	const vector<int> boardCountAddressVec = CustomizedJsonConfig::instance().getVector<int>("PLC_MODBUS_TCP_TOTAL_NUM_COUNT_ADDRESS_LIST");

	if(vScale.size() <= boardId)
	{
		LogERROR << "extern: json config: DRAW_SCALE";
		return false;
	}
	if(vThickness.size() <= boardId)
	{
		LogERROR << "extern: json config: DRAW_SCALE";
		return false;
	}

	if(m_sCameraType == "mock")
	{
		const int timeInterval = CustomizedJsonConfig::instance().get<int>("MOCK_VIDEO_FRAME_TIME_INTERVAL_MS");
		std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval));
		// if (boardId == 1)
		// {
		// 	std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval + 1000));
		// }

		//////////////////////////// STEP1: get next image ////////////////////////////
		// product count will be increased if get next frame sucessfully, to make log//
		// consistant, we add 1 here instead                                         //
		m_vTimer[boardId]->reset();
		LogDEBUG << "extern: Board[" << boardId << "] get next frame started. \t Product count: " << m_pDetectors[boardId].m_pDetector->productCount() + 1;
		if (!m_pDetectors[boardId].m_pDetector->getNextFrame())
		{
			LogERROR << "extern: Board[" << boardId << "] get next image failed";
			return false;
		}
		//set capture image times in detector
		// m_pDetectors[boardId].m_pDetector->setCaptueImageTimesByProductCount();
		m_pDetectors[boardId].m_pDetector->setCaptueImageTimesBySignal();	
		const double t1 = m_vTimer[boardId]->elapsed();
		LogDEBUG << "extern: Board[" << boardId << "] Time" << m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() << " : get image time cost " << t1  << " seconds";
		m_pDetectors[boardId].m_pDetector->updateProductCountofWorkflow();
		// jishi1.emplace_back(t1);
		// if (jishi1.size() == 20)
		// {
		// 	int i = 0;
		// 	for (double num : jishi1)
		// 	{
		// 		i++;
		// 		cout << "get image: Board[" << boardId << "] : 第" << i << "次" << num << " seconds" << endl;
		// 	}
		// 	float maxValue = *max_element(jishi1.begin(), jishi1.end());
		// 	double sum = accumulate(jishi1.begin(), jishi1.end(), 0.0);
		// 	double average = static_cast<double>(sum) / jishi1.size();
		// 	cout << "get image:Maximum value: " << maxValue << endl;
		// 	cout << "get image:Average value: " << average << endl;
		// 	jishi1.clear();
		// }		
	}
	else
	{
		//set capture image times in detector
		m_vTimer[boardId]->reset();
		m_pDetectors[boardId].m_pDetector->setCaptueImageTimesBySignal();	
		if(m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() == (int)CaptureImageTimes::UNKNOWN_TIMES)
		{
			return true;
		}		
		shared_ptr<HaikangCameraConfig> pConfig = dynamic_pointer_cast<HaikangCameraConfig>(m_cameras[boardId][0]->config());
		if(!pConfig)
		{
			LogERROR << "Board[" << boardId << "] no camera config";
			return false;
		}
		if(m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() == (int)CaptureImageTimes::FIRST_TIMES)
		{
			vector<float> vExposure = CustomizedJsonConfig::instance().getVector<float>("CAMERA_EXPOSURE_FIRST");
			vector<float> vGain = CustomizedJsonConfig::instance().getVector<float>("CAMERA_GAIN_FRIST");
			pConfig->setExposure(vExposure[boardId]);	
			pConfig->setGain(vGain[boardId]);			
			pConfig->setConfig();
		}
		else if(m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() == (int)CaptureImageTimes::SECOND_TIMES)
		{
			m_vTimer_total[boardId]->reset(); //每个产品第一次拍照开始计时
			// if (boardId == 0)
			// {
			// 	signal_0 == 1;
			// }else
			// {
			// 	signal_1 == 1;
			// }
			
			vector<float> vExposure = CustomizedJsonConfig::instance().getVector<float>("CAMERA_EXPOSURE_SECOND");
			vector<float> vGain = CustomizedJsonConfig::instance().getVector<float>("CAMERA_GAIN_SECOND");
			pConfig->setExposure(vExposure[boardId]);	
			pConfig->setGain(vGain[boardId]);
			pConfig->setConfig();	
		}
		const double t0 = m_vTimer[boardId]->elapsed();
		LogDEBUG << "extern: Board[" << boardId << "] Time" << m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() << " : setConfig time cost " << t0  << " seconds";
	
		//////////////////////////// STEP1: get next image ////////////////////////////
		// product count will be increased if get next frame sucessfully, to make log//
		// consistant, we add 1 here instead                                         //
		m_vTimer[boardId]->reset();
		LogDEBUG << "extern: Board[" << boardId << "] get next frame started. \t Product count: " << m_pDetectors[boardId].m_pDetector->productCount() + 1;
		if (!m_pDetectors[boardId].m_pDetector->getNextFrame())
		{
			LogERROR << "extern: Board[" << boardId << "] get next image failed";
			// 对该工位的所有相机进行重连
			for (int workflowId = 0; workflowId < m_pDetectors[boardId].m_pDetector->totalWorkflows(); workflowId++)
			{
				LogERROR << "extern: Detector[" << workflowId << "]'s camera deadly crashed, trying to restart camera";
				if(!m_pDetectors[boardId].m_pDetector->restartWorkflowCamera(workflowId))
				{	
					LogERROR << "extern: Board[" << boardId << "] Failed to restart camera Id:" << workflowId;
				}else
				{
					LogDEBUG << "extern: Board[" << boardId << "] Finished camera" << workflowId << " restart";
					break;
				}
			}
			return false;
		}	
		const double t1 = m_vTimer[boardId]->elapsed();
		LogDEBUG << "extern: Board[" << boardId << "] Time" << m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() << " : get image time cost " << t1  << " seconds";
		// jishi1.emplace_back(t1);
		// if (jishi1.size() == 20)
		// {
		// 	int i = 0;
		// 	for (double num : jishi1)
		// 	{
		// 		i++;
		// 		cout << "get image: 第" << i << "次" << num << " seconds" << endl;
		// 	}
		// 	float maxValue = *max_element(jishi1.begin(), jishi1.end());
		// 	double sum = accumulate(jishi1.begin(), jishi1.end(), 0.0);
		// 	double average = static_cast<double>(sum) / jishi1.size();
		// 	cout << "get image:Maximum value: " << maxValue << endl;
		// 	cout << "get image:Average value: " << average << endl;
		// 	jishi1.clear();
		// }
	}

	const int totalTimes = m_pDetectors[boardId].m_pDetector->getCaptureImageTotalTimes();
	const int dubug_times =  CustomizedJsonConfig::instance().get<int>("CAPTUREIMAGETIMES");
	const int times = (dubug_times == 1 || dubug_times == 2) ? dubug_times : m_pDetectors[boardId].m_pDetector->getCaptureImageTimes();
	if(dubug_times == 1 || dubug_times == 2)
	{
		m_pDetectors[boardId].m_pDetector->resetProductCount(m_pDetectors[boardId].m_pDetector->productCount());
	}
	else
	{
		if(times != totalTimes && (m_pDetectors[boardId].m_pDetector->productCount() != 1))
		{
			m_pDetectors[boardId].m_pDetector->resetProductCount(m_pDetectors[boardId].m_pDetector->productCount() - 1);
		}
	}
	m_pDetectors[boardId].m_pDetector->updateProductCountofWorkflow(); //使用框架计数

	//////////////////////////// STEP2: pre-process next image ////////////////////////////
	m_vTimer[boardId]->reset();
	LogDEBUG << "extern: Board[" << boardId << "] process image started. \t Product count: " << m_pDetectors[boardId].m_pDetector->productCount();
	// if (nullptr != m_pIoManager)  //使用PLC计数
	// {
	// 	const int boardCountAddress = boardCountAddressVec.at(boardId);
	// 	int boardProductCount = 0;
	// 	if (dynamic_pointer_cast<AppIoManagerPLC>(m_pIoManager)->readRegister(boardCountAddress, boardProductCount))
	// 	{
	// 		m_pDetectors[boardId].m_pDetector->updateProductCountofWorkflow(boardProductCount);
	// 		LogDEBUG << "extern: Board[" << boardId << "] read product count from plc, address : " << boardCountAddress << ", value: " << boardProductCount;
	// 	}
	// 	else
	// 	{
	// 		LogERROR << "extern: Board[" << boardId << "] read product count from plc failed! ";
	// 	}
	// }
	
	if (!m_pDetectors[boardId].m_pDetector->imagePreProcess())
	{
		LogERROR << "extern: Board[" << boardId << "] process next image failed";
		//send NG signal to PLC
		const bool bIsResultOK = false;
		m_pDetectors[boardId].m_pDetector->sendResultSignalToPLC(bIsResultOK);
		m_pDetectors[boardId].m_pDetector->setCaptureImageTimes((int)CaptureImageTimes::UNKNOWN_TIMES);
		//AppRunningResult::instance().setProductCountResult(boardId, m_pDetectors[boardId].m_pDetector->productCount(), false);
		return false;
	}
	const double t2 = m_vTimer[boardId]->elapsed();
	LogDEBUG << "extern: Board[" << boardId << "] Time" << m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() << " : pre-process time cost " << t2 << " seconds";

	//////////////////////////// STEP3: computer vision process and classify next image ////////////////////////////
	m_vTimer[boardId]->reset();
	LogDEBUG << "extern: Board[" << boardId << "] classify started. \t Product count: " << m_pDetectors[boardId].m_pDetector->productCount();
	if (!m_pDetectors[boardId].m_pDetector->classifyBoard())
	{
		LogERROR << "extern: Board[" << boardId << "] classify board failed";
		//send NG signal to PLC
		const bool bIsResultOK = false;
		m_pDetectors[boardId].m_pDetector->sendResultSignalToPLC(bIsResultOK);
		m_pDetectors[boardId].m_pDetector->setCaptureImageTimes((int)CaptureImageTimes::UNKNOWN_TIMES);
		//AppRunningResult::instance().setProductCountResult(boardId, m_pDetectors[boardId].m_pDetector->productCount(), false);
		return false;
	}
	const double t3 = m_vTimer[boardId]->elapsed();
	LogDEBUG << "extern: Board[" << boardId << "] Time" << m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() << " : classify board time cost " << t3 << " seconds";

	//////////////////////////// STEP4: purge and cleanup next image ////////////////////////////
	m_vTimer[boardId]->reset();
	LogDEBUG << "extern: Board[" << boardId << "] cleanup started. \t Product count: " << m_pDetectors[boardId].m_pDetector->productCount();
	if (!m_pDetectors[boardId].m_pDetector->cleanupBoard())
	{
		LogERROR << "extern: Board[" << boardId << "] cleanp board and PLC remove failed";
		m_pDetectors[boardId].m_pDetector->setCaptureImageTimes((int)CaptureImageTimes::UNKNOWN_TIMES);
		// return false;
	}
	const double t4 = m_vTimer[boardId]->elapsed();
	LogDEBUG << "extern: Board[" << boardId << "] Time" << m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() << " : cleanp board and PLC remove time cost " << t4 << " seconds";

	//////////////////////////// STEP5: draw next image ////////////////////////////
	m_vTimer[boardId]->reset();
	LogDEBUG << "extern: Board[" << boardId << "] drawing started. \t Product count: " << m_pDetectors[boardId].m_pDetector->productCount();
	if (!m_pDetectors[boardId].m_pDetector->drawBoard(vScale[boardId]))
	{
		LogERROR << "extern: Board[" << boardId << "] draw board failed";
		return false;
	}
	const double t5 = m_vTimer[boardId]->elapsed();
	LogDEBUG << "extern: Board[" << boardId << "] Time" << m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() << " : draw board time cost " << t5 << " seconds";
	LogINFO << "extern: Board[" << boardId << "] Time" << m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() << " : total detect time cost except for getting image " << t2 + t3 + t4 + t5 << " seconds";

	// if(m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() == (int)CaptureImageTimes::FIRST_TIMES || signal_0 == 1 && boardId == 0)
	// {
	// 	const double t100 = m_vTimer_total[boardId]->elapsed();
	// 	LogINFO << "extern: Board[" << boardId << "] Time" << m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() << " : total detect time cost" << t100 << " seconds";
		// jishi_0.emplace_back(t100);
		// if (jishi_0.size() == 5)
		// {
		// 	int i = 0;
		// 	for (double num : jishi_0)
		// 	{
		// 		i++;
		// 		cout << "两次拍照检测完成总时间: Board[" << boardId << "] : 第" << i << "次" << num << " seconds" << endl;
		// 	}
		// 	float maxValue = *max_element(jishi_0.begin(), jishi_0.end());
		// 	double sum = accumulate(jishi_0.begin(), jishi_0.end(), 0.0);
		// 	double average = static_cast<double>(sum) / jishi_0.size();
		// 	cout << "两次拍照检测完成总时间:Maximum value: " << maxValue << endl;
		// 	cout << "两次拍照检测完成总时间:Average value: " << average << endl;
		// 	jishi_0.clear();
		// }
		// signal_0 = 0;
	// }
	// if(m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() == (int)CaptureImageTimes::FIRST_TIMES || signal_1 == 1 && boardId == 1)
	// {
	// 	const double t100 = m_vTimer_total[boardId]->elapsed();
	// 	LogINFO << "extern: Board[" << boardId << "] Time" << m_pDetectors[boardId].m_pDetector->getCaptureImageTimes() << " : total detect time cost" << t100 << " seconds";
		// jishi_1.emplace_back(t100);
		// if (jishi_1.size() == 5)
		// {
		// 	int i = 0;
		// 	for (double num : jishi_1)
		// 	{
		// 		i++;
		// 		cout << "两次拍照检测完成总时间: Board[" << boardId << "] : 第" << i << "次" << num << " seconds" << endl;
		// 	}
		// 	float maxValue = *max_element(jishi_1.begin(), jishi_1.end());
		// 	double sum = accumulate(jishi_1.begin(), jishi_1.end(), 0.0);
		// 	double average = static_cast<double>(sum) / jishi_1.size();
		// 	cout << "两次拍照检测完成总时间:Maximum value: " << maxValue << endl;
		// 	cout << "两次拍照检测完成总时间:Average value: " << average << endl;
		// 	jishi_1.clear();
		// }
		// signal_0 = 0;
	// }
	
	return true;
}

bool XJAppServer::parametersTest(const int boardId)
{
	if (boardId < 0 || boardId >= m_pDetectors.size())
	{
		LogERROR << "extern: Invalid board Id: " << boardId;
		return false;
	}

	string productName = RunningInfo::instance().GetTestProductionInfo().GetCurrentProd();
	if (productName == "N/A")
	{
		LogERROR << "extern: Test product is unavailable";
		return false;
	}

	string imageFileName = RunningInfo::instance().GetTestProductSetting().getImageName();
	if (imageFileName == "N/A")
	{
		LogERROR << "extern: Invalid processed image name: " << imageFileName;
		return false;
	}

	int phase = RunningInfo::instance().GetTestProductSetting().getPhase();
	LogINFO << "extern: Parameter test under " << productName << " start...";

	//////////////////////////// STEP1: read image from file ////////////////////////////
	LogDEBUG << "extern: Board[" << boardId << "] read next frame from " << imageFileName << " started.";
	if (!m_pDetectors[boardId].m_pDetector->readFrameFromFile(imageFileName))
	{
		LogERROR << "extern: Board[" << boardId << "] read next image from " << imageFileName << " failed";
		return false;
	}

	if (phase != (int)TestProcessPhase::NO_PROCESS)
	{
		//////////////////////////// STEP2: pre-process next image /////////////////////////
		LogDEBUG << "extern: Board[" << boardId << "] process image started.";
		if (nullptr != m_pIoManager)
		{
			const vector<int> boardCountAddressVec = CustomizedJsonConfig::instance().getVector<int>("PLC_MODBUS_TCP_TOTAL_NUM_COUNT_ADDRESS_LIST");
			const int boardCountAddress = boardCountAddressVec.at(boardId);
			int boardProductCount = 0;
			dynamic_pointer_cast<AppIoManagerPLC>(m_pIoManager)->readRegister(boardCountAddress, boardProductCount);
			m_pDetectors[boardId].m_pDetector->updateProductCountofWorkflow(boardProductCount);
			LogDEBUG << "extern: Board[" << boardId << "] read product count from plc, address : " << boardCountAddress << ", value: " << boardProductCount;
		}
		
		if (!m_pDetectors[boardId].m_pDetector->imagePreProcess())
		{
			LogERROR << "extern: Board[" << boardId << "] process next image failed";
			return false;
		}
	}

	if (phase == (int)TestProcessPhase::PRE_PROCESS_AND_CLASSIFY)
	{
		//////////////////////////// STEP3: computer vision and classify next image ////////////////////////////
		LogDEBUG << "extern:Board[" << boardId << "] classify started.";
		if (!m_pDetectors[boardId].m_pDetector->classifyBoard())
		{
			LogERROR << "extern: Board[" << boardId << "] classify board failed";
			return false;
		}
	}

	//////////////////////////// STEP4: draw next image ///////////////////////////////
	LogDEBUG << "extern: Board[" << boardId << "] drawing started.";
	double scale = 0.1;
	if (!m_pDetectors[boardId].m_pDetector->drawBoard(scale))
	{
		LogERROR << "extern: Board[" << boardId << "] draw board failed";
		return false;
	}

	LogINFO << "extern: Parameter test under " << productName << " end.";
	return true;
}

bool XJAppServer::saveCameraTriggeredTestImages(const int boardId)
{
	if (boardId < 0 || boardId >= m_pDetectors.size())
	{
		LogERROR << "extern: Invalid board Id: " << boardId;
		return false;
	}

	string productName = RunningInfo::instance().GetTestProductionInfo().GetCurrentProd();
	if (productName == "N/A")
	{
		LogERROR << "extern: Test product is unavailable";
		return false;
	}

	LogINFO << "extern: Save camera triggered images under " << productName << " start...";

	// replace your own code to build your file path, and file name here if needed.
	// you can get camera name from RunningInfo::instance().GetTestProductSetting().getCameraName()
	// if it is available
#ifdef __linux__
	string filePath("/opt/products");
#else
	string filePath("C:\\opt\\products");
#endif
	string fileName = "TestImage";
	m_pDetectors[boardId].m_pDetector->saveFrameToFile(filePath, fileName);

	LogINFO << "extern: Save camera triggered images under " << productName << " end.";
	return true;
}

void XJAppServer::startDetector(const int boardId)
{
	bool areCamerasReady = false;
	timer_utils::Timer<chrono::minutes> reportTimer;
	DetectorRunStatus runStatus;

	try
	{
		reportTimer.Reset();
		while (true)
		{
			if (m_bIsStop)
			{
				LogDEBUG << "extern: Board[" << boardId << "] exit...";
				break;
			}
			//////////////////////////// PRESTEP0: get current running status ////////////////////////////
			//  get current Run/Test/TriggerCameraImage status
			DetectorRunStatus::Status currentRunStatus = runStatus.getStatus();

			//////////////////////////// PRESTEP1: running status check ////////////////////////////
			//  if system is not running which is 0, sleep and continue
			if (currentRunStatus == DetectorRunStatus::Status::NOT_RUN)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				if (isProductSwitched())
				{
					LogDEBUG << "extern: Board[" << boardId << "] current product: " << m_sProductName;
					// add your product reconfiguration code here
				}
				// set the first time start flag to true, wait for next time to initialize paramters
				m_pDetectors[boardId].m_bIsFirstStart = true;
				continue;
			}

			//////////////////////////// PRESTEP2: camera status check ////////////////////////////
			// if system is running, or trigger camera image status is true,
			// check if camera is ready, then we go to get next images
			if (currentRunStatus == DetectorRunStatus::Status::PRODUCT_RUN || currentRunStatus == DetectorRunStatus::Status::TEST_RUN_WITH_CAMERA)
			{
				if (!areCamerasReady)
				{
					if (!camerasAreReady(boardId))
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(5));
						continue;
					}
					areCamerasReady = true;
				}
			}

			//////////////////////////// PRESTEP3: restarted status check ////////////////////////////
			// if the system is restarted then at the first time we will reinitialize parameters
			if (m_pDetectors[boardId].m_bIsFirstStart)
			{
				if (!initBoardParameters(boardId))
				{
					LogERROR << "extern: Board[" << boardId << "] initial parameters error, try again. ";
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}
				m_pDetectors[boardId].m_bIsFirstStart = false; // only initalize once when first time to start
			}

			//////////////////////////// PRESTEP4: update database report ////////////////////////////
			//  update database report using report_add, it will update record with time, product, board...
			if (reportTimer.Elapsed().count() >= DATABASE_ELAPSE_MINUTE)
			{
				reportTimer.Reset();
				if (!updateDatabaseReport(boardId))
				{
					LogERROR << "extern: Board[" << boardId << "] update report in database failed";
					continue;
				}
			}

			switch (currentRunStatus)
			{
				case DetectorRunStatus::Status::PRODUCT_RUN:
					runDetector(boardId);
					break;
				case DetectorRunStatus::Status::TEST_RUN:
					if (RunningInfo::instance().GetTestProductSetting().getBoardId() == boardId)
					{
						parametersTest(boardId);
						RunningInfo::instance().GetTestProductionInfo().SetTestStatus(0);
					}
					break;
				case DetectorRunStatus::Status::TEST_RUN_WITH_CAMERA:
					if (RunningInfo::instance().GetTestProductSetting().getBoardId() == boardId)
					{
						saveCameraTriggeredTestImages(boardId);
					}
					break;
				default:
					LogERROR << "extern: Detector has invalid running status";
					break;
			}
		}
	}
	catch(const cv::Exception &e)
	{
		LogERROR<<"extern: catch cv::exception, Board[" << boardId << "] :"<<e.what() << ", detector stop abnormally";
	}
	catch(const std::exception& e)
    {
		LogERROR<<"extern: catch std::exception, Board[" << boardId << "] :"<< e.what() << ", detector stop abnormally";
    }
	catch (...)
	{
		LogCRITICAL << "extern: Board[" << boardId << "] Critical error happens, detector stop abnormally...";
		RunningInfo::instance().GetRunningData().setCustomerDataByName("critical", "detector-stop-abnormally");
	}

	// update database before detector exits
	if (!updateDatabaseReport(boardId))
	{
		LogERROR << "extern: Board[" << boardId << "] update report in database failed";
	}

	return;
}

void XJAppServer::checkHeartBeat()
{
	const bool bIsUseIoCard = CustomizedJsonConfig::instance().get<bool>("IS_USE_IO_CARD");
	const int signal_change_interval_ms = CustomizedJsonConfig::instance().get<int>("HEART_BEAT_SIGNAL_TIME_INTERVAL_MS");
	const int iBit = CustomizedJsonConfig::instance().get<int>("IO_CARD_HEART_BEAT_BIT_ADDRESS");
	const int iAddress = CustomizedJsonConfig::instance().get<int>("PLC_MODBUS_TCP_HEART_BEAT_ADDRESS");

	int iSignalIndex = 0;
	DetectorRunStatus runStatus;
	timer_utils::Timer<std::chrono::milliseconds> checkTimer;
	checkTimer.Reset();
	while (true)
	{
		if (m_bIsStop)
		{
			 LogDEBUG<< "check heartbeat exit...";
			break;
		}

		if(checkTimer.Elapsed().count() > signal_change_interval_ms)
		{
			checkTimer.Reset();

			iSignalIndex++;
			if(iSignalIndex == INT_MAX)
			{
				iSignalIndex = 0;
			}

			const int data = iSignalIndex%2;
			if(bIsUseIoCard)
			{
				dynamic_pointer_cast<AppIoManagerIOCard>(m_pIoManager)->writeBit(0, iBit, 1);
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				dynamic_pointer_cast<AppIoManagerIOCard>(m_pIoManager)->writeBit(0, iBit, 0);
				LogINFO << "send heart beat data:" << 1 << " to IO bit address:" << iBit;
			}
			else
			{
				dynamic_pointer_cast<AppIoManagerPLC>(m_pIoManager)->writeRegister(iAddress, data);					
				LogINFO << "send heart beat data:" << data << " to PLC register address:" << iAddress;		
			}
		}
	}
}

void XJAppServer::updateMergeTotal()
{
	const bool bIsUseIoCard = CustomizedJsonConfig::instance().get<bool>("IS_USE_IO_CARD");
	const int signal_change_interval_ms = CustomizedJsonConfig::instance().get<int>("HEART_BEAT_SIGNAL_TIME_INTERVAL_MS");
	timer_utils::Timer<std::chrono::milliseconds> checkTimer;
	checkTimer.Reset();
	while (true)
	{
		if (m_bIsStop)
		{
			LogDEBUG<< "check heartbeat exit...";
			break;
		}
		DetectorRunStatus runStatus;
		if (runStatus.getStatus() == XJAppServer::DetectorRunStatus::Status::NOT_RUN)
		{
			checkTimer.Reset();
			continue;
		}
		if(checkTimer.Elapsed().count() > signal_change_interval_ms)
		{
			checkTimer.Reset();
			int valueTotalNum = 0;
			int valueTotalDefect = 0;
			if (m_sCameraType == "mock" || bIsUseIoCard || nullptr == m_pIoManager)
			{
				map<string, int> result;
				RunningInfo::instance().GetRunningData().GetRunningData(result);
				valueTotalNum = result["totalNum"];
				valueTotalDefect = result["totalDefect"];
			}
			else
			{
				const int addressTotalNum = CustomizedJsonConfig::instance().get<int>("PLC_MODBUS_TCP_TOTAL_NUM_COUNT_ADDRESS");
				dynamic_pointer_cast<AppIoManagerPLC>(m_pIoManager)->readRegister(addressTotalNum, valueTotalNum);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			
				const int addressTotalDefect = CustomizedJsonConfig::instance().get<int>("PLC_MODBUS_TCP_TOTAL_DEFECT_COUNT_ADDRESS");
				dynamic_pointer_cast<AppIoManagerPLC>(m_pIoManager)->readRegister(addressTotalDefect, valueTotalDefect);
			}

			LogDEBUG << "update: valueTotalNum" << valueTotalNum << " valueTotalDefect:" << valueTotalDefect;
			AppRunningResult::instance().updateDisplayTotalInfo(valueTotalNum, valueTotalDefect);			
		}
	}
	return;
}

void XJAppServer::stopDetectors()
{
	// TODO to set stop flag
	LogDEBUG << "Detectors ask to stop...";
	m_bIsStop = true;
}
