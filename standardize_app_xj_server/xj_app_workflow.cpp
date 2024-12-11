#include "xj_app_workflow.h"
#include "xj_app_config.h"
#include "xj_app_data.h"
#include "xj_app_running_result.h"
#include "customized_json_config.h"
#include "xj_app_json_config.h"
#include "camera_manager.h"
#include "itek_camera_config.h"
#include "haikang_camera_config.h"

#include "shared_utils.h"
#include "opencv_utils.h"
#include "running_status.h"
#include <boost/foreach.hpp>
#include "database.h"
#include "db_utils.h"
#include <numeric>


using namespace boost::property_tree;
using namespace std;
using namespace cv;

AppWorkflow::AppWorkflow(const int workflowId, const std::shared_ptr<WorkflowConfig> &pConfig) : 
			BaseWorkflow(workflowId, pConfig),
			m_pSaveImageMultiThread(nullptr),
			m_pAlgorithm(make_shared<XJAppAlgorithm>(m_mapAutoUpdateParams)),
			m_iSaveImageType((int)SaveImageType::ALL),
			m_purgeMode((int)PurgeMode::NORMAL),
			m_runMode((int)RunMode::RUN_DETECT),
			m_sProductName("N/A"),
			m_sProductLot("N/A"),
			m_numNGHistory(0),
			m_iProductNumber(0),
			m_nCaptureImageTimes(0),
			m_nTotalCaptureTimes(0)
{
	const bool bIsMultiThread =  CustomizedJsonConfig::instance().get<bool>("IS_USE_MULTI_THREAD_PER_CAMERA");
	if(bIsMultiThread)
	{
		const int maxBufferImageNumber =  CustomizedJsonConfig::instance().get<int>("MAX_BUFFER_IMAGE_SAVE_NUM");
		m_pSaveImageMultiThread = make_shared<MultiThreadImageSaveBase>(maxBufferImageNumber);
	}

	
}

int AppWorkflow::convertDefectType(const int defectIndex)
{
	if(m_mapDefectIndexToType.find(defectIndex) ==  m_mapDefectIndexToType.end())
	{
		LogERROR << "Board[" << boardId() <<  "] " << ClassifierResult::getName(defectIndex) << " no config";
		return 0;
	}
	return m_mapDefectIndexToType[defectIndex];
}

bool AppWorkflow::initParamsA(stConfigParamsA &stParamsA, const int currentRunStatus)
{
	stParamsA.fParams.clear();

	//step1: intial basic params
	stParamsA.sProductName = m_sProductName;
	stParamsA.sProductLot  = m_sProductLot;
	stParamsA.boardId = boardId();
	stParamsA.viewId = workflowId();
	stParamsA.numTargetInView = getView()->targetsSize();
	stParamsA.runStatus = currentRunStatus;
	stParamsA.saveImageType = m_iSaveImageType;
	stParamsA.pSaveImageMultiThread = m_pSaveImageMultiThread;

	//step2: intial params from db and configurantion.json file
	ProductSetting::PRODUCT_SETTING setting;
	if(currentRunStatus == (int)RunStatus::PRODUCT_RUN)
	{
		setting = RunningInfo::instance().GetProductSetting().GetSettings();
		if (!m_sProductName.empty())
		{
			Database db;
			if (db.prod_setting_read(m_sProductName, setting))
			{
				LogERROR << "Board[" << boardId() <<  "] Read product setting in database failed";
				return false;
			}
		}
	}
	else
	{
		setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
		int phase = RunningInfo::instance().GetTestProductSetting().getPhase();
		stParamsA.phase = phase;
	}
	
	//step3: 初始化瑕疵参数
	ptree ptCamGroups;
	string sNodeName = ("UISetting.setup.flawParams-" + to_string(boardId()));
	std::stringstream ss = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
	read_json(ss, ptCamGroups);
	for(const auto &group:ptCamGroups)//1)组
	{
		const string sGroupId = group.second.get<string>("id");
		const auto &items = group.second.get_child("params");
		for(const auto &item:items)//2)项
		{
			const int item_index = item.second.get<int>("index");
			const string sItemId = item.second.get<string>("id");
			stParamsA.fParams[item_index] = setting.float_settings[item_index];
			if(item_index >= setting.float_settings.size())
			{
				LogERROR << "Board[" << boardId() <<  "] Item:" << sItemId << " index:" << item_index << " is out of range of bool_setting size";
				return false;
			}

			const auto &params = item.second.get_child("paramsList");
			for(const auto &param:params)//3)参数
			{
				const int param_index = param.second.get<int>("index");
				const string sParamId = param.second.get<string>("id");
				if (param.second.get_child_optional("isRecordToDB"))
				{
					if (!param.second.get<bool>("isRecordToDB"))
					{
						const string& sJsonNodeName = "flawParams-"+ to_string(boardId()) + "." + sGroupId + "." + sItemId + "." + sParamId;
						stParamsA.fParams[param_index] = AppJsonConfig::instance().get<float>(sJsonNodeName);
						continue;
					}
				}

				if(param_index >= setting.float_settings.size() || param_index < 0)
				{
					LogERROR << "Board[" << boardId() <<  "] param:" << sParamId << " index:" << param_index << " is out of range of floot_setting size";
					return false;
				}
				stParamsA.fParams[param_index] = setting.float_settings[param_index];
			}
		}
	}

	//step4:初始化画框参数
	stParamsA.mapBox.clear();
	const vector<int> vecMax = CustomizedJsonConfig::instance().getVector<int>("UISetting.setup.draw.maxLimit");			
	const vector<int> vecMin = CustomizedJsonConfig::instance().getVector<int>("UISetting.setup.draw.minLimit");
	const int startIndex = CustomizedJsonConfig::instance().get<int>("UISetting.setup.draw.startIndex");			
	if(vecMax.size() != vecMin.size())
	{
		LogERROR << "Board[" << boardId() <<  "] draw box size is not right!!!";
		return false;
	}
	
	const int camIdx = boardId();
	const int totalBoxNum = accumulate(vecMax.begin(), vecMax.end(), 0);
	int rectIdx = startIndex + camIdx * totalBoxNum * 4;
	const int numBoxGroup = vecMax.size();		
	for (int i = 0; i < numBoxGroup; ++i)
	{
		const int numBox = vecMax[i];
		for(int j = 0; j < numBox; ++j)
		{
			if((rectIdx +  3) >= setting.float_settings.size())
			{
				LogERROR << "Board[" << boardId() <<  "] box seeting index:" << (rectIdx +  3) << " is out of range of floot_setting size";				
				return false;
			}

			int x = setting.float_settings[rectIdx +  0];
			int y = setting.float_settings[rectIdx +  1];
			int w = setting.float_settings[rectIdx +  2];
			int h = setting.float_settings[rectIdx +  3];
			rectIdx += 4;

			Rect box(x, y, w, h);
			if(x > 0 && y > 0 && w > 0 && h > 0 && box.area() > 0)
			{
				stParamsA.mapBox[i].emplace_back(box);
			}
			
		}			
	}

	LogINFO << "Board[" << boardId() <<  "] initial parmsA successfully";
	return true;
}

bool AppWorkflow::initParamsB(stConfigParamsB &stParamsB, const int currentRunStatus)
{
	//intial params from configurantion.json file
	ptree fParams;
	string sNodeName1 = "PRIVATED_ALGORITHM_FLOAT_PARAMS_CONFIG";
	std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName1);
	read_json(ss1, fParams);
	for(const auto &param:fParams)
	{
		const string sKey = param.first;
		const float fValue = stof(param.second.data());
		stParamsB.fParams[sKey] = fValue;
	}

	ptree strParams;
	string sNodeName2 = "PRIVATED_ALGORITHM_STRING_PARAMS_CONFIG";
	std::stringstream ss2 = CustomizedJsonConfig::instance().getJsonStream(sNodeName2);
	read_json(ss2, strParams);
	for(const auto &param:strParams)
	{
		const string sKey = param.first;
		const string sText(param.second.data());
		stParamsB.strParams[sKey] = sText;
	}

	ptree vFParams;
	string sNodeName3 = ("PRIVATED_ALGORITHM_VECTOR_FLOAT_PARAMS_CONFIG");
	std::stringstream ss3 = CustomizedJsonConfig::instance().getJsonStream(sNodeName3);
	read_json(ss3, vFParams);
	for (const auto &param:vFParams) 
	{
		const string sKey = param.first;
		const vector<float> vecFloat = CustomizedJsonConfig::instance().getVector<float>("PRIVATED_ALGORITHM_VECTOR_FLOAT_PARAMS_CONFIG."+sKey);
		stParamsB.vecFParams[sKey] = vecFloat;
	}

	stParamsB.vCameraNames = CustomizedJsonConfig::instance().getVector<string>("CAMERA_NAME");
	LogINFO << "Board[" << boardId() <<  "] initial parmsB successfully";
	return true;
}

bool AppWorkflow::initCameraParams(const int currentRunStatus)
{
	const string sCam = to_string(boardId());
	// step1: 判断是否需要更新相机参数
	string cameraName = "CAM" + sCam;
	if(getView()->getCamera() == nullptr)
	{
		LogERROR << "Invalid camera" << cameraName;
		return false;
	}
	if(getView()->getCamera()->deviceName().substr(0, 4) == "Mock")
	{
		return true;
	}

	//step2: 解析配置json和读取数据库参数
	string sProd;
	ProductSetting::PRODUCT_SETTING setting;
	if(currentRunStatus == (int)RunStatus::PRODUCT_RUN)
	{
		setting = RunningInfo::instance().GetProductSetting().GetSettings();
		sProd = RunningInfo::instance().GetProductionInfo().GetCurrentProd();
	}
	else {
		setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
		sProd = RunningInfo::instance().GetTestProductionInfo().GetCurrentProd();
	}
	if (!sProd.empty())
	{
		Database db;
		if (db.prod_setting_read(sProd, setting))
		{
			LogERROR << "Read product setting in database failed";
			return false;
		}
	}
	ptree camConfigParams;
	string sNodeName = "UISetting.setup.cameraParams-" + sCam;
	std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
	read_json(ss1, camConfigParams);
	map<string, float> mapCameraParams;
	for(const auto &paramconfig:camConfigParams)
	{
		const string sID = paramconfig.second.get<string>("id");
		if(!paramconfig.second.get_child_optional("index") || !paramconfig.second.get_child_optional("id"))
		{
			LogERROR << "no complete child node[index, id] in parent node [" << sNodeName << "]";
			return false;
		}
		// step3: 判断是从数据库读取还是从配置文件中读取 
		// tip: 如果从配置文件中获取，则所有工程的相机参数共用一个，此处与数据库中读取的情况不一致
		if (paramconfig.second.get_child_optional("isRecordToDB"))
		{
			if (!paramconfig.second.get<bool>("isRecordToDB"))
			{
				const string& sJsonNodeName = "cameraParams-"+ sCam + "." + sID;
				mapCameraParams.insert(make_pair(sID, AppJsonConfig::instance().get<float>(sJsonNodeName)));
				continue;
			}
		}
		// 否则从数据库读取
		const int index = paramconfig.second.get<int>("index");
		if(index >= setting.float_settings.size() || index < 0)
		{
			LogERROR << "cameraParams-" << sCam << "." << sID << " index:" << index << " is out of range of floot_setting size";
			return false;
		}
		mapCameraParams.insert(make_pair(sID, setting.float_settings[index]));	
	}
	int exposure = int(mapCameraParams["exposure"]);
	float gain = mapCameraParams["gain"];
	bool gammaEnable = is_double_equal(mapCameraParams["gammaEnable"], 1., 10e-3);
	float gammaValue = mapCameraParams["gammaValue"];

	//step4:相机参数解析，相机设置
	vector<string> vCamType = CustomizedJsonConfig::instance().getVector<string>("CAMERA_TYPE");
	if (!getView()->getCamera()->cameraStarted())
	{
		if (!getView()->getCamera()->start())
		{
			LogERROR << "Start " << cameraName << " failed";
			return false;
		}
	}
	shared_ptr<CameraConfig> config = getView()->getCamera()->config();
	if ("AreaArray" == vCamType.at(boardId()))
	{
		dynamic_pointer_cast<HaikangCameraConfig>(config)->setExposure(exposure);
		dynamic_pointer_cast<HaikangCameraConfig>(config)->setGain(gain);
		if (gammaEnable)
		{
			dynamic_pointer_cast<HaikangCameraConfig>(config)->setGammaEnable(gammaEnable);
			dynamic_pointer_cast<HaikangCameraConfig>(config)->setGamma(gammaValue);
		}
		else
		{
			dynamic_pointer_cast<HaikangCameraConfig>(config)->setGammaEnable(gammaEnable);
			dynamic_pointer_cast<HaikangCameraConfig>(config)->setGamma(-1);
		}
		dynamic_pointer_cast<HaikangCameraConfig>(config)->setConfig();
	}
	else if ("LineScan" == vCamType.at(boardId()))
	{
		dynamic_pointer_cast<ItekCameraConfig>(config)->setExposure(exposure);
		// dynamic_pointer_cast<ItekCameraConfig>(config)->setGain(gain);  // has no member named ‘setGain’
		if (gammaEnable)
		{
			dynamic_pointer_cast<ItekCameraConfig>(config)->setGammaEnable(gammaEnable);
			dynamic_pointer_cast<ItekCameraConfig>(config)->setGamma(gammaValue);
		}
		else
		{
			dynamic_pointer_cast<ItekCameraConfig>(config)->setGammaEnable(gammaEnable);
			dynamic_pointer_cast<ItekCameraConfig>(config)->setGamma(-1);
		}
		dynamic_pointer_cast<ItekCameraConfig>(config)->setConfig();
	}
	else 
	{
	}

	return true;
}

bool AppWorkflow::reconfigParameters(const int currentRunStatus)
{
	m_mapDefectIndexToType.clear();
	m_mapDefectIndexToType[0] = (int)ClassifierResultConstant::Classifying;
	m_mapDefectIndexToType[1] = (int)ClassifierResultConstant::Good;
	// init camera parameters
	if (!initCameraParams(currentRunStatus))
	{
		LogERROR << "Board[" << boardId() <<  "] initial camera parameters failed!";
		return false;
	}

	ptree ptDefectConfig;
	string sNodeName = "UISetting.common.DetectorToCategory";
	std::stringstream ss = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
	read_json(ss, ptDefectConfig);
	int boardIdx = 0;
	for(const auto &ptBoardDefect:ptDefectConfig)
	{
		if(boardIdx == boardId())
		{
			int defectIdx = 0;
			for(const auto &ptDefect:ptBoardDefect.second)
			{
				const int defectType = ClassifierResult::getResult(string(ptDefect.second.data()));
				m_mapDefectIndexToType[defectIdx + 2] = defectType;
				defectIdx ++;
			}
			break;
		}
		boardIdx ++;
	}

	m_runStatus = currentRunStatus;
	m_nTotalCaptureTimes = CustomizedJsonConfig::instance().getVector<int>("CAMERA_TOTAL_IMAGES")[boardId()];
	if(currentRunStatus == (int)RunStatus::PRODUCT_RUN)
	{
		m_sProductName = RunningInfo::instance().GetProductionInfo().GetCurrentProd();
		m_sProductLot = RunningInfo::instance().GetProductionInfo().GetCurrentLot();
		if (!RunningInfo::instance().GetProductSetting().UpdateCurrentProductSettingFromDB(m_sProductName))
		{
			LogERROR << "Board[" << boardId() <<  "] Read " << m_sProductName << " setting from database failed!";
			return false;
		}
	}
	else
    {
		m_sProductName = RunningInfo::instance().GetTestProductionInfo().GetCurrentProd();
		m_sProductLot = RunningInfo::instance().GetTestProductionInfo().GetCurrentLot();
        m_pView->resetResult();
		//自动更新项目参数
		m_mapAutoUpdateItem.clear();
		ptree ptCamGroups;
		string sNodeName = ("UISetting.setup.flawParams-" + to_string(boardId()));
		std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
		read_json(ss1, ptCamGroups);
		for(const auto &ptGroup:ptCamGroups)
		{
			const string sGroupId = ptGroup.second.get<string>("id");
			const auto &items = ptGroup.second.get_child("params");
			for(const auto &item:items)
			{
				const int item_index = item.second.get<int>("index");
				const string sItemId = item.second.get<string>("id");
				m_mapAutoUpdateItem[item_index] = sItemId;	
			}	
		}
    }

	RunningInfo::instance().GetRunningData().clearCustomerData();
	LogINFO << "Board[" << boardId() <<  "] load product " << m_sProductName << " parameters begin!";
	if(currentRunStatus == (int)RunStatus::PRODUCT_RUN)
	{
		m_iSaveImageType = (const int)RunningInfo::instance().GetProductSetting().GetFloatSetting(int(ProductSettingFloatMapper::SAVE_IMAGE_TYPE));
		m_purgeMode = (const int)RunningInfo::instance().GetProductSetting().GetFloatSetting((const int)ProductSettingFloatMapper::PURGE_SIGNAL_TYPE);
		m_runMode = (const int)RunningInfo::instance().GetProductSetting().GetFloatSetting((const int)ProductSettingFloatMapper::RUN_MODE);
	}
	else
	{
		m_iSaveImageType = (const int)RunningInfo::instance().GetTestProductSetting().GetFloatSetting(int(ProductSettingFloatMapper::SAVE_IMAGE_TYPE));
		m_purgeMode = (const int)RunningInfo::instance().GetTestProductSetting().GetFloatSetting((const int)ProductSettingFloatMapper::PURGE_SIGNAL_TYPE);
		m_runMode = (const int)RunningInfo::instance().GetTestProductSetting().GetFloatSetting((const int)ProductSettingFloatMapper::RUN_MODE);
	}
	LogINFO << "Board[" << boardId() <<  "] software status: save image type = " << m_iSaveImageType << ", purge mode = " << m_purgeMode << ", run mode = " << m_runMode;

	stConfigParamsA stParamsA;
	stConfigParamsB stParamsB;
	if(!initParamsA(stParamsA, currentRunStatus) || !initParamsB(stParamsB, currentRunStatus))
	{
		LogERROR << "Board[" << boardId() <<  "] failed to initial app params";
		return false;
	}
	
	if(!m_pAlgorithm || !m_pAlgorithm->init(stParamsA, stParamsB))
	{
		LogERROR << "Board[" << boardId() <<  "] failed to initial agorithm params";
		return false;
	}
	
	LogINFO << "Board[" << boardId() <<  "] load product " << m_sProductName << " parameters end!";
	return true;
}

bool AppWorkflow::imagePreProcess()
{
	// extract frame into image in targets of the view
	shared_ptr<View> pView = getView();
	if (pView == nullptr || !pView->isValid() || m_workflowImage.empty())
	{
		return false;
	}

	try
	{
		if(m_workflowImage.channels() == 1)
		{
			cvtColor(m_workflowImage, m_workflowImage, COLOR_GRAY2BGR);
		}
		
		//是否要挂起存图线程
		const bool bIsHangUp =  CustomizedJsonConfig::instance().get<bool>("IS_NEED_HANGUP_IMAGE_SAVE_THREAD_BEFORE_IMAGE_PROCESS");
		if(bIsHangUp)
		{
			m_pSaveImageMultiThread->HangUpSaveThread();
		}

		//单target统计多瑕疵
		const bool bIsEnable =  CustomizedJsonConfig::instance().get<bool>("IS_COUNT_MULTI_DEFECTS_PER_TARGET");
		const int dubug_times =  CustomizedJsonConfig::instance().get<int>("CAPTUREIMAGETIMES");
		if(bIsEnable)
		{
			m_mapDefects.clear();
		}
		
		const int numTargets = pView->targetsSize();
		if(m_runMode == (int)RunMode::RUN_DETECT)
		{
			if (dubug_times != 0)
			{
				m_nCaptureImageTimes = dubug_times;
			}					
			vector<vector<int>> vTotalResultType = m_pAlgorithm->detectAnalyze(m_workflowImage, m_workflowProcessedImage, m_iProductNumber, m_nCaptureImageTimes);
			if(vTotalResultType.size() != numTargets)
			{
				LogERROR << "Board[" << boardId() <<  "] result no match number of targets";
				return false;
			}
			
			for (int targetIdx = 0; targetIdx != numTargets; targetIdx++)
			{	
				vector<int> &vResult = vTotalResultType[targetIdx];
				if(vResult.size() == 0)
				{
					pView->getTarget(targetIdx)->setResult(ClassifierResultConstant::Good);	
				}
				else
				{
					//单target统计多瑕疵
					if(bIsEnable)
					{
						for(const auto& result:vResult)
						{
							m_mapDefects[result]++;
						}
					}
					pView->getTarget(targetIdx)->setResult(vResult[0]);		
				}		
			}

			//自动更新参数
			if(m_runStatus == (int)RunStatus::TEST_RUN)
			{
				int phase = RunningInfo::instance().GetTestProductSetting().getPhase();
				if(m_mapAutoUpdateItem.find(phase) != m_mapAutoUpdateItem.end())
				{
					ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
					for(const auto & param :m_mapAutoUpdateParams)
					{
						setting.float_settings[param.first] = param.second;
						LogINFO << "Board[" << boardId() <<  "] auto update " << m_mapAutoUpdateItem[phase] << " param: index = " << param.first << ", value = "  << param.second;
					}

					m_mapAutoUpdateParams.clear();
					RunningInfo::instance().GetTestProductSetting().UpdateCurrentProductSetting(setting);
					phase = (int)TestProcessPhase::AUTO_UPDATE_PARAMS_FINISH;
					RunningInfo::instance().GetTestProductSetting().setPhase(phase);
				}		
			}
		}
		else
		{
			for (int targetIdx = 0; targetIdx != numTargets; targetIdx++)
			{
				pView->getTarget(targetIdx)->setResult(ClassifierResultConstant::Good);																
			}	
		}
	}
	catch (const Exception &e)
	{
		LogERROR << "Catch opencv exception: " << e.what();
		return false;
	}
	catch (const exception &e)
	{
		LogERROR << "Catch standard exception: " << e.what();
		return false;
	}

	return true;
}

bool AppWorkflow::computerVisionProcess()
{
	// app need to write its own code to handle computer vision related process properly
	// this method only works with framework 5.0.0.0 and up
	return true;
}

void AppWorkflow::drawDesignedTargets(const double scale, const int thickness)
{
	if (m_workflowImage.empty() || m_pView == nullptr)
	{
		LogERROR << "extern: Board[" << boardId() <<  "] Invalid input iamge";
		return;
	}

	//1、获取结果
	bool bIsOK = true;
	ClassificationResult result = m_pView->getResult();
	if(result != ClassifierResultConstant::Good)
	{
		bIsOK = false;
	}

	//2、绘制图像
	if(m_workflowProcessedImage.empty() || m_runMode == (int)RunMode::RUN_EMPTY)
	{
		m_workflowProcessedImage = m_workflowImage.clone();
	}
	
	const int boardID = boardId();
	const vector<int> vPosX = CustomizedJsonConfig::instance().getVector<int>("CAMERA_IMAGE_DRAW_TEXT_POS_X");
	if(vPosX.size() <= boardID)
	{
		LogERROR << "json config: CAMERA_IMAGE_DRAW_TEXT_POS_X";
		return;
	}
	const vector<int> vPosY = CustomizedJsonConfig::instance().getVector<int>("CAMERA_IMAGE_DRAW_TEXT_POS_Y");
	if(vPosY.size() <= boardID)
	{
		LogERROR << "json config: CAMERA_IMAGE_DRAW_TEXT_POS_Y";
		return;
	}
	const vector<int> vFontScale = CustomizedJsonConfig::instance().getVector<int>("CAMERA_IMAGE_DRAW_TEXT_FONT_SCALE");
	if(vFontScale.size() <= boardID)
	{
		LogERROR << "json config: CAMERA_IMAGE_DRAW_TEXT_FONT_SCALE";
		return;
	}

	Scalar color = bIsOK ? Scalar(0, 255, 0) : Scalar(255, 0, 255);
	// string sText = "pic" + to_string(m_nCaptureImageTimes) + "/"  + to_string(m_nTotalCaptureTimes);
	string sText = "pic" + to_string(m_nCaptureImageTimes);
	sText += bIsOK ? "-OK": "-NG";
	if(m_nCaptureImageTimes == (int)CaptureImageTimes::UNKNOWN_TIMES || m_nCaptureImageTimes > m_nTotalCaptureTimes)
	{
		color = Scalar(255, 255, 255);
	}
	putText(m_workflowProcessedImage, sText, Point(vPosX[boardID], vPosY[boardID]), FONT_HERSHEY_SIMPLEX, vFontScale[boardID], color, thickness);

	//3、缩放结果图
	const bool bIsResize =  CustomizedJsonConfig::instance().get<bool>("IS_SAVE_RESIZE_RESULT_IMAGE");
	if(bIsResize)
	{
		resize(m_workflowProcessedImage, m_workflowProcessedImage, cv::Size(0, 0), scale, scale, INTER_NEAREST);
	}
	LogINFO << "extern: Board[" << boardId() <<  "] STEP 1, vPosX size: "<<vPosX.size()<< "vPosY size: "<<vPosY.size()<<" vFontScale size"<<vFontScale.size();

	//4、保存结果图
	if(m_nCaptureImageTimes != (int)CaptureImageTimes::UNKNOWN_TIMES && m_nCaptureImageTimes <= m_nTotalCaptureTimes)
	{
		if(m_pSaveImageMultiThread)
		{
			if(bIsOK && (m_iSaveImageType == (int)SaveImageType::ALL || (int)SaveImageType::BOARD_START + boardID == m_iSaveImageType))
			{
				//1)保存OK原图
				const bool bIsSaveSource =  CustomizedJsonConfig::instance().get<bool>("IS_NEED_SAVE_SOURCE_IMAGE_IN_APP");
				string sCameraName = CustomizedJsonConfig::instance().getVector<string>("CAMERA_NAME")[boardID];
				string sCustomerEnd = "CNT" + to_string(m_iProductNumber) + "-PIC" + to_string(m_nCaptureImageTimes);
				if(bIsSaveSource)
				{				
					string sSavePath = "/opt/history/origin/good/";
					string sFileName = getAppFormatImageNameByCurrentTimeXJ(convertDefectType((int)result), boardId(), workflowId(), 0, m_sProductName, m_sProductLot, sCustomerEnd);
					m_pSaveImageMultiThread->AddImageData(m_workflowImage, sSavePath, sFileName);
				}
				
				//2)保存OK结果图
				const bool bIsSave =  CustomizedJsonConfig::instance().get<bool>("IS_NEED_SAVE_OK_RESULT_IMAGE");
				if(bIsSave)
				{
					string sSavePath = "/opt/history/resultImage/good/";
					string sFileName = getAppFormatImageNameByCurrentTimeXJ(convertDefectType((int)result), boardId(), workflowId(), 0, m_sProductName, m_sProductLot, sCustomerEnd);
					m_pSaveImageMultiThread->AddImageData(m_workflowProcessedImage, sSavePath, sFileName, ".jpg");
				}			
			}	
			else if(!bIsOK && (m_iSaveImageType != (int)SaveImageType::NO && (int)SaveImageType::BOARD_START + boardID != m_iSaveImageType))
			{
				//1)保存NG原图
				const bool bIsSaveSource =  CustomizedJsonConfig::instance().get<bool>("IS_NEED_SAVE_SOURCE_IMAGE_IN_APP");
				string sCameraName = CustomizedJsonConfig::instance().getVector<string>("CAMERA_NAME")[boardID];
				string sCustomerEnd = "CNT" + to_string(m_iProductNumber) + "-PIC" + to_string(m_nCaptureImageTimes);
				if(bIsSaveSource)
				{
					string sSavePath = "/opt/history/origin/bad/";
					string sFileName = getAppFormatImageNameByCurrentTimeXJ(convertDefectType((int)result), boardId(), workflowId(), 0, m_sProductName, m_sProductLot, sCustomerEnd);
					m_pSaveImageMultiThread->AddImageData(m_workflowImage, sSavePath, sFileName);
				}

				//2)保存NG结果图
				string sSavePath = "/opt/history/resultImage/bad/";
				string sFileName = getAppFormatImageNameByCurrentTimeXJ(convertDefectType((int)result), boardId(), workflowId(), 0, m_sProductName, m_sProductLot, sCustomerEnd);
				m_pSaveImageMultiThread->AddImageData(m_workflowProcessedImage, sSavePath, sFileName, ".jpg");

				//3)走马灯
				m_numNGHistory ++;
				const int num = CustomizedJsonConfig::instance().get<int>("UISetting.operation.historyNg.num");
				m_numNGHistory = m_numNGHistory > num ? 1 : m_numNGHistory;
				string sID = to_string(boardId()) + "-" + to_string(m_numNGHistory);
				RunningInfo::instance().GetRunningData().setCustomerDataByName(sID, sSavePath + sFileName + ".jpg");
			}	
			m_pSaveImageMultiThread->WakeUpSaveThread();//唤醒存图线程
		}
	}

	//5、缩放图像到UI显示结果图
	if(!bIsResize)
	{
		resize(m_workflowProcessedImage, m_workflowProcessedImage, cv::Size(0, 0), scale, scale, INTER_NEAREST);
	}
	LogINFO << "extern: Board[" << boardId() <<  "] STEP 2";
	//6.设置结果
	int nCaptureImageTimes = (m_nCaptureImageTimes == (int)CaptureImageTimes::UNKNOWN_TIMES) ? (int)CaptureImageTimes::FIRST_TIMES : m_nCaptureImageTimes;
	AppRunningResult::instance().setCurrentResultMap(m_workflowProcessedImage, result, "CAM" + to_string(boardID),  nCaptureImageTimes);
	LogINFO << "extern: Board[" << boardId() <<  "] STEP 3";
}

