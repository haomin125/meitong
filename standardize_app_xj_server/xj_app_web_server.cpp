#include "xj_app_web_server.h"
#include "xj_app_io_manager.h"
#include "xj_app_running_result.h"
#include "xj_app_json_config.h"
#include "xj_app_data.h"

#include "logger.h"
#include "database.h"
#include "db_utils.h"
#include "running_status.h"
#include "shared_utils.h"
#include "camera_manager.h"
#include "haikang_camera_config.h"
#include "itek_camera_config.h"
#include "customized_json_config.h"
#include "xj_app_database.h"
#include "utils.h"


#include <map>
#include <string>
#include <algorithm>
#include <numeric>
#include <boost/foreach.hpp>

using namespace std;
using namespace boost::property_tree;

//float to string
string Convert(float Num)
{
	ostringstream oss;
	oss << Num;
	return oss.str();
}

void stringSplit(const string& str, const string& splits, vector<string>& res)
{
	if (str == "")		return;
	//在字符串末尾也加入分隔符，方便截取最后一段
	string strs = str + splits;
	size_t pos = strs.find(splits);
	int step = splits.size();
 
	// 若找不到内容则字符串搜索函数返回 npos
	while (pos != strs.npos)
	{
		string temp = strs.substr(0, pos);
		res.push_back(temp);
		//去掉已分割的字符串,在剩下的字符串中进行分割
		strs = strs.substr(pos + step, strs.size());
		pos = strs.find(splits);
	}
}

AppWebServer::AppWebServer(const shared_ptr<CameraManager> pCameraManager) : ApbWebServer(pCameraManager), m_pCameraManager(pCameraManager)
{
	addWebCmds();
}

AppWebServer::AppWebServer(const shared_ptr<CameraManager> pCameraManager, const string &addr, const unsigned short port) : ApbWebServer(pCameraManager, addr, port), m_pCameraManager(pCameraManager)
{
	addWebCmds();
}

int AppWebServer::addWebCmds()
{
	LogINFO << "Add APP web server commands...";

	ReadSetting();
	SettingWrite();
	TestSettingWrite();

	SetCurrentProd();
	CreateTestProduction();
	SetDefaultAutoParams();

	SetRect();
	SetParams();
	GetDetectingParams();
	GetTestingParams();

	GetPlcParams();
	SetPlcParams();

	GetCommonParams();
	SetCommonParams();

	SetCameraParams();
	SaveCameraParams();
	GetCameraParams();
	TriggerCamera();

	Preview();
	ImageProcessed();
	autoUpdateParams();

	SetImagePath();
	GetImagePath();

	GetHistoryNg();

	GetDefect();
	GetRealReportWithLot();

	GetMesData();

	//20231127-by zhz
	GetCameraResultImage();
	GetCurrentResult();

	return 0;
}

int AppWebServer::ImageProcessed()
{
	/*
   GET: http://localhost:8080/image_processed
   Return: true/false
 */
	m_server.resource["^/image_processed"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		try
		{
			int testStatus = RunningInfo::instance().GetTestProductionInfo().GetTestStatus();
			response->write(testStatus == -1 ? "true" : "false");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::TestSettingWrite()
{
	/*
   POST: http://localhost:8080/test_setting_write?prod=Product03
   Return: Success
   process phase: 0 no process at all, 1 pre-process only, 2 pre-process and classification
 */
	m_server.resource["^/test_setting_write"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			// reset test and trigger camera flag to false
			RunningInfo::instance().GetTestProductionInfo().SetTestStatus(0);
			RunningInfo::instance().GetTestProductionInfo().SetTriggerCameraImageStatus(0);

			string prod = SimpleWeb::getValue(query_fields, "prod");
			ptree pt;
			read_json(request->content, pt);
			if (pt.empty())
			{
				LogERROR << "Invalid content in test product setting";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Content");
				return 1;
			}

			if (RunningInfo::instance().GetTestProductionInfo().GetCurrentProd() == "N/A")
			{
				LogERROR << "Test product setting ignored due to product name unavailable";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "Product Name Unavailable");
				return 1;
			}

			if (RunningInfo::instance().GetTestProductionInfo().GetCurrentProd() != prod)
			{
				LogERROR << "Test product does not match, current " << RunningInfo::instance().GetTestProductionInfo().GetCurrentProd() << ", request " << prod;
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "Test Product Does Not Match With Current Product");
				return 1;
			}

			// load test product setting and other test related values if they exist
			ProductSetting::PRODUCT_SETTING setting;
			setting.loadFromJson(pt);
			RunningInfo::instance().GetTestProductSetting().loadFromJson(pt);
			RunningInfo::instance().GetTestProductSetting().UpdateCurrentProductSetting(setting);

			// set test flag to true
			RunningInfo::instance().GetTestProductionInfo().SetTestStatus(1);

			LogINFO << "Update product setting " << prod << " successfully";

			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::ReadSetting()
{
	/*
	GET:
	http: //localhost:8080/setting_read?prod=Product01
	Return (example) : {
		"boolean_settings": [
			"false",
			"false",
			"true",
			"true",
			"true",
			"false",
			"false",
			"false",
			"false",
			"false",
			"false",
			"false",
			"false",
			"false",
			"false",
			"false"
		],
		"float_settings": [
			"0",
			"500",
			"99",
			"0.439999998",
			"0.5",
			"0.579999983",
			"30",
			"20",
			"20",
			"0",
			"0",
			"0",
			"0",
			"0",
			"0",
			"0"
		],
		"string_settings": [
			"D:\\opt\\seeking.jpg",
			"D:\\opt\\seeking.jpg",
			"D:\\opt\\seeking.jpg"
		]
	}
	*/
	m_server.resource["^/setting_read"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			string prod = SimpleWeb::getValue(query_fields, "prod");
			ProductSetting::PRODUCT_SETTING setting;
			Database db;
			if (db.prod_setting_read(prod, setting))
			{
				LogERROR << "Read product setting in database failed";
				response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Product Setting Failed");
				return 1;
			}
			else
			{
				boost::property_tree::ptree root;
				boost::property_tree::ptree boolArr;

				for (int idx = 0; idx < setting.bool_settings.size(); idx++)
				{
					boost::property_tree::ptree cell;
					cell.put_value(setting.bool_settings[idx] ? "true" : "false");
					boolArr.push_back(make_pair("", cell));
				}
				root.put_child("boolean_settings", boolArr);

				boost::property_tree::ptree floatArr;
				for (int idx = 0; idx < setting.float_settings.size(); idx++)
				{
					boost::property_tree::ptree cell;
					cell.put_value(setting.float_settings[idx]);
					floatArr.push_back(make_pair("", cell));
				}
				root.put_child("float_settings", floatArr);

				boost::property_tree::ptree fileArr;
				for (int idx = 0; idx < setting.string_settings.size(); idx++)
				{
					boost::property_tree::ptree cell;
					string s(setting.string_settings[idx].data());
					cell.put_value(s);
					fileArr.push_back(make_pair("", cell));
				}
				root.put_child("string_settings", fileArr);

				stringstream ss;
				write_json(ss, root);
				response->write(ss);
			}
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
		}
	};
	return 0;
}

int AppWebServer::SetCurrentProd() 
{
	/*
	POST: http://localhost:8080/set_current_prod
	{"prod":"Product04"}
	*/
	m_server.resource["^/set_current_prod"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) 
	{
		try 
		{
			ptree pt;
			read_json(request->content, pt);
			string sProd = pt.get<string>("prod");
			LogINFO << "Set product: " << sProd;
			if(RunningInfo::instance().GetProductionInfo().SetCurrentProd(sProd)) 
			{
					// reset product setting in memory by new value
					if(RunningInfo::instance().GetProductSetting().UpdateCurrentProductSettingFromDB(sProd)) 
					{
						string lot = RunningInfo::instance().GetProductSetting().GetStringSetting(0);
						RunningInfo::instance().GetProductionInfo().SetCurrentLot(lot);
						response->write("Success");
						db_utils::ALARM("SetProduct");
					}
					else 
					{
						LogERROR << "Update current product setting failed";
						response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Update Current Product Setting Failed");
						return 1;
					}
			}
			else 
			{
					LogERROR << "Set current product " << sProd << " failed";
					response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Set Current Product Failed");
					return 1;
			}
		}
		catch(const exception &e) 
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::CreateTestProduction()
{
	/*
   POST: http://localhost:8080/create_test_prod?prod=Product03
   Return: Success
   three settings could be empty, which means it is a new test production without any existing setting
   process phase: 0 no porcess at all, 1 pre-process only, 2 pre-process and classification
 */
	m_server.resource["^/create_test_prod"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			string prod = SimpleWeb::getValue(query_fields, "prod");
			ptree pt;
			read_json(request->content, pt);
			// set current test product info and reset all test related flags to false
			RunningInfo::instance().GetTestProductionInfo().SetCurrentProd(prod, false);
			RunningInfo::instance().GetTestProductionInfo().SetTestStatus(0);
			RunningInfo::instance().GetTestProductionInfo().SetTriggerCameraImageStatus(0);

			// load product setting, and other test related values
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
			if (!pt.empty())//产品存在
			{
				setting.loadFromJson(pt);
			}
			else//新建产品
			{
				setting.reset();
			}

			RunningInfo::instance().GetTestProductSetting().setPhase(30);
			RunningInfo::instance().GetTestProductSetting().UpdateCurrentProductSetting(setting);
			LogINFO << "Create test product " << prod << " and setting successfully";

			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::SettingWrite()
{
	/*
   POST: http://localhost:8080/setting_write?prod=Product03
   Return: Success
 */
	m_server.resource["^/setting_write"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			string prod = SimpleWeb::getValue(query_fields, "prod");
			//ptree pt;
			//read_json(request->content, pt);
			
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
			//setting.loadFromJson(pt);

			// use lambda function to release database resource before UpdateCurrentProductSetting, which open database again
			auto writeProdSetting = [](const string &prod, const ProductSetting::PRODUCT_SETTING &setting)
			{
				Database db;
				return db.prod_setting_write(prod, setting);
			};


			if (writeProdSetting(prod, setting))
			{
				LogERROR << "Update product setting " << prod << " in database failed";
				response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Update Product Setting " + prod + " Failed");
				return 1;
			}

			// update the memory each time setting is confirmed
			RunningInfo::instance().GetProductSetting().UpdateCurrentProductSetting(setting);
			LogINFO << "Update product setting " << prod << " success";
			response->write("Success");
			db_utils::ALARM("ModifyParameters");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::Preview()
{
	/*
   POST: http://localhost:8080/preview?prod=DGB&group=all&function=all&board=0&camera=0
   Return: Success
 */
	m_server.resource["^/preview"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			string function = SimpleWeb::getValue(query_fields, "function");
			string sBoardId = SimpleWeb::getValue(query_fields, "board");
			string sCameraId = SimpleWeb::getValue(query_fields, "camera");
			const int boardId = stoi(sBoardId);
			const int cameradId = stoi(sCameraId);
			
			int phase = (int)TestProcessPhase::PRE_PROCESS_AND_CLASSIFY;
			RunningInfo::instance().GetTestProductSetting().setBoardId(boardId);
			RunningInfo::instance().GetTestProductSetting().setPhase(phase);
			RunningInfo::instance().GetTestProductionInfo().SetTestStatus(1);
			int testStatus(1);
			while (testStatus != 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				testStatus = RunningInfo::instance().GetTestProductionInfo().GetTestStatus();
			}

			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::autoUpdateParams()
{
	/*
   POST: http://localhost:8080/auto_update_params?prod=DGB&group=boxSetting&function=okBox&board=0&camera=0
   Return: Success
 */
	m_server.resource["^/auto_update_params"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			string group = SimpleWeb::getValue(query_fields, "group");
			string function = SimpleWeb::getValue(query_fields, "function");
			string sBoard = SimpleWeb::getValue(query_fields, "board");
			string sCamera = SimpleWeb::getValue(query_fields, "camera");
			const int boardId = stoi(sBoard);

			int phase = -1;
			ptree ptCamGroups;
			string sNodeName = ("UISetting.setup.flawParams-" + sCamera);
			std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
			read_json(ss1, ptCamGroups);
			for(const auto &ptGroup:ptCamGroups)
			{
				const string sGroupId = ptGroup.second.get<string>("id");
				if(sGroupId.find(group) != string::npos)
				{
					const auto &items = ptGroup.second.get_child("params");
					for(const auto &item:items)
					{
						const int item_index = item.second.get<int>("index");
						const string sItemId = item.second.get<string>("id");
						if(sItemId.find(function) != string::npos)
						{
							phase = item_index;
							break;
						}
					}
					break;
				}
			}
			RunningInfo::instance().GetTestProductSetting().setBoardId(boardId);
			RunningInfo::instance().GetTestProductSetting().setPhase(phase);
			RunningInfo::instance().GetTestProductionInfo().SetTestStatus(1);
			int testStatus(1);
			while (testStatus != 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				testStatus = RunningInfo::instance().GetTestProductionInfo().GetTestStatus();
			}

			if(RunningInfo::instance().GetTestProductSetting().getPhase() == (int)TestProcessPhase::AUTO_UPDATE_PARAMS_FINISH)
			{
				LogINFO << "Update product setting " << " success";
			}
			
			LogINFO << "Update product setting " << function << " success";
			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::SetImagePath()
{
	/*
   POST: http://localhost:8080/set_image_path?image_path=imagePath
   Return: Success
 */
	m_server.resource["^/set_image_path"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			string image_path = SimpleWeb::getValue(query_fields, "image_path");
			RunningInfo::instance().GetTestProductSetting().setImageName(image_path);
			LogINFO << "Update product setting " << image_path << " success";
			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::GetImagePath()
{
	/*
   POST: http://localhost:8080/get_image_path
   Return: Success
 */
	m_server.resource["^/get_image_path"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		try
		{
			string imagePath = RunningInfo::instance().GetTestProductSetting().getImageName();
			response->write(imagePath);
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::SetRect()
{
	/*
	POST: http://localhost:8080/set_rect?function=setRect
	example:
	{
		"rect":[
			[
				{
					"x": "569.817337",
					"y": "75.853229",
					"width": "260.265983",
					"height": "203.044384"
				}
			],
			[
				{
					"x": "405.535972",
					"y": "341.656786",
					"width": "302.720718",
					"height": "252.882551"
				}
			],
			[
				{
					"x": "219.104310",
					"y": "151.533408",
					"width": "537.144688",
					"height": "395.013620"
				}
			]
		]
	}
   Return: Success




   
 */
	m_server.resource["^/set_rect"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		string prod = RunningInfo::instance().GetProductionInfo().GetCurrentProd();
		auto query_fields = request->parse_query_string();
		try
		{
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
			string function = SimpleWeb::getValue(query_fields, "function");
			string board = SimpleWeb::getValue(query_fields, "board");
			string cam = SimpleWeb::getValue(query_fields, "camera");
			const int camIdx = stoi(cam);

			//step1:获取UI发送过来的json
			ptree pt;
			read_json(request->content, pt);
			// load product setting, and other test related values
			if (pt.empty())
			{
				LogERROR << "Invalid content in test product setting";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Content");
				return 1;
			}

			if (!pt.get_child_optional("rect"))
			{
				LogERROR << "find tect failed";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "find rect failed");
				return 1;
			}


			//step2:画框坐标复位归零
			vector<int> vecMax = CustomizedJsonConfig::instance().getVector<int>("UISetting.setup.draw.maxLimit");			
			const int startIndex = CustomizedJsonConfig::instance().get<int>("UISetting.setup.draw.startIndex");
			const int numGroup = vecMax.size();	//画框组数
			const int totalBoxNum = accumulate(vecMax.begin(), vecMax.end(), 0);
			int rectIdx = startIndex + camIdx * totalBoxNum * 4;
			for(int m = 0; m < numGroup; ++ m)
			{
				const int maxNumBox = vecMax[m]; //每组最大框数量
				for(int n = 0; n < maxNumBox; ++ n)
				{
					setting.float_settings[rectIdx + 0] = 0;
					setting.float_settings[rectIdx + 1] = 0;
					setting.float_settings[rectIdx + 2] = 0;
					setting.float_settings[rectIdx + 3] = 0;
					rectIdx += 4; 
				}
			}

			//step3:画框坐标赋值
			rectIdx = startIndex + camIdx * totalBoxNum * 4;
			ptree groups = pt.get_child("rect"); // get_child得到数组对象
			int groupIdx = 0;
			for(const auto &group:groups)
			{
				int boxIdx = 0;
				for(const auto & box:group.second)
				{ 
					setting.float_settings[rectIdx + 0] = box.second.get<float>("x");
					setting.float_settings[rectIdx + 1] = box.second.get<float>("y");
					setting.float_settings[rectIdx + 2] = box.second.get<float>("width");
					setting.float_settings[rectIdx + 3] = box.second.get<float>("height");
					rectIdx += 4;
					boxIdx ++;
				}
				
				if(boxIdx < vecMax[groupIdx])
				{
					rectIdx += 4 * (vecMax[groupIdx] - boxIdx);
				}
				groupIdx ++;
			}

			//step4:更新内存数据
			RunningInfo::instance().GetTestProductSetting().UpdateCurrentProductSetting(setting);

			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::SetParams()
{
	/*
		POST: http://localhost:8080/set_params?prod=DGB&group=firstDet&function=dimension&board=0&camera=0
		{
			"enable": 1,
			"parameters": {
				"baseDimension": 67.5,
				"upTolerance": 0.4,
				"lowTolerance": 0.4,
				"leftWidthMeasure": "67.6",
				"rightWidthMeasure": "67.8",
				"leftWidthRatio": 0.037,
				"rightWidthRatio": 0.037
			}
		}
	*/
	m_server.resource["^/set_params"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			//step1:获取web端发送过来的json
			ptree pt;
			read_json(request->content, pt);
			if (pt.empty())
			{
				LogERROR << "Invalid content in test product setting";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Content");
				return 1;
			}

			string sBoard = SimpleWeb::getValue(query_fields, "board");
			string sCam = SimpleWeb::getValue(query_fields, "camera");
			string sGroup = SimpleWeb::getValue(query_fields, "group");
			string sFunction = SimpleWeb::getValue(query_fields, "function");
			string sProd = SimpleWeb::getValue(query_fields, "prod");
			LogINFO << "set_params: prod=" << sProd << " board=" << sBoard << " camera=" << sCam << " group=" << sGroup << " function=" << sFunction;

			if (pt.get_child_optional("enable"))
			{
				ptree parametersTree;
				if(pt.get_child_optional("parameters"))
				{
					parametersTree = pt.get_child("parameters");
				}
				
				//step2:从数据库获取数据
				ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
			
				//step3:解析配置文件json和赋值给数据库数据
				ptree ptCamGroups;
				string sNodeName = ("UISetting.setup.flawParams-" + sCam);
				std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
				read_json(ss1, ptCamGroups);
				for(const auto &group:ptCamGroups)
				{
					string sGroupId = group.second.get<string>("id");
					if(sGroupId == sGroup)
					{
						auto items = group.second.get_child("params");
						for(const auto &item:items)
						{
							string sItemId = item.second.get<string>("id");
							if(sItemId == sFunction)
							{
								//更新enable值
								const int item_index = item.second.get<int>("index");
								if (item_index >= setting.float_settings.size())
								{
									LogERROR << "item:" << sItemId << " index:" << item_index << " is out of range of float_settings size";
									response->write(SimpleWeb::StatusCode::server_error_internal_server_error, item_index + " index is out of range of float_settings size");
									return 1;
								}
								setting.float_settings[item_index] = pt.get<float>("enable");

								//更新所有参数值
								auto params = item.second.get_child("paramsList");
								for(const auto &param:params)
								{
									const int param_index = param.second.get<int>("index");
									string sParamId = param.second.get<string>("id");
									if (param.second.get_child_optional("isRecordToDB"))
									{
										if (!param.second.get<bool>("isRecordToDB") && pt.get_child_optional("parameters"))
										{
											const string& sJsonNodeName = "flawParams-"+ sCam + "." + sGroupId + "." + sItemId + "." + sParamId;
											AppJsonConfig::instance().set<float>(sJsonNodeName, parametersTree.get<float>(sParamId));
											continue;
										}
									}

									if (param_index >= setting.float_settings.size() ||  param_index < 0)
									{
										LogERROR << "param:" << sParamId << " index:" << param_index << " is out of range of floot_setting size";
										response->write(SimpleWeb::StatusCode::server_error_internal_server_error, sParamId + " index is out of range of floot_setting size");
										return 1;
									}
									
									if (pt.get_child_optional("parameters"))
									{
										setting.float_settings[param_index] = parametersTree.get<float>(sParamId);
									}								
								}
								break;
							}//if(sItemId == sFunction)
						}
						break;
					}//if(sGroupId == sGroup)
				}
					
				//step4:更新内存数据，未保存数据
				RunningInfo::instance().GetTestProductSetting().UpdateCurrentProductSetting(setting);
			}

			//step5:更新成功
			response->write("Success");
		}
		catch (const exception &e)
		{
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}


int AppWebServer::GetDetectingParams()
{
	/*	
	GET:http: //localhost:8080/get_detecting_parameters?prod=Product01
	Return detecting_parameters:{	
		rectTips:
		[
			{ txt: '提示文本', img: '提示图片的地址' },
			{ txt: '提示文本', img: '提示图片的地址' },
			{ txt: '提示文本', img: '提示图片的地址' },
			{ txt: '提示文本', img: '提示图片的地址' },
			{ txt: '提示文本', img: '提示图片的地址' }
		]，
		"rect": [
			[
				{
					"x": "862.413",
					"y": "979.463",
					"width": "3865.64",
					"height": "1677.19"
				}
			],
			[
				{
					"x": "1897.41",
					"y": "748.773",
					"width": "311.745",
					"height": "2119.87"
				},
				{
					"x": "3138.15",
					"y": "586.665",
					"width": "405.268",
					"height": "2431.61"
				}
			],
			[
				{
					"x": "1610.6",
					"y": "773.712",
					"width": "118.463",
					"height": "274.336"
				},
				{
					"x": "2957.34",
					"y": "811.121",
					"width": "112.228",
					"height": "261.866"
				}
			]
    	],
		"params_group1": {
			"params_item1": {
				"enable": "1",
				"parameters": {
					"baseDimension": "67.5",
					"upTolerance": "0.4",
					"lowTolerance": "0.4",
					"leftWidthMeasure": "72.5",
					"rightWidthMeasure": "72.5",
					"leftWidthRatio": "0.037",
					"rightWidthRatio": "0.037"
				}
			},
			"params_item2": {
				"enable": "1",
				"parameters": {
					"blackSpotDefectSensitivity": "20",
					"blackSpotDefectMinArea": "30"
				}
			},
			"params_item3": {
				"enable": "1",
				"parameters": {
					"whiteEdgeDefectSensitivity": "70",
					"whiteEdgeDefectMinArea": "35"
				}
			}
		},
		"params_group2": {
			"params_item4": {
				"enable": "1",
				"parameters": {
					"spotDefectSensitivity": "35",
					"spotDefectMinArea": "55",
					"spotDefectMinLength": "10"
				}
			},
			"params_item5": {
				"enable": "1",
				"parameters": {
					"scratchDefectSensitivity": "5",
					"scratchDefectMinRatio": "10",
					"scratchDefectMinLength": "40"
				}
			},
			"params_item6": {
				"enable": "1",
				"parameters": {
					"weekDefectCenterSize": "2",
					"weekDefectCenterSensitivity": "50",
					"weekDefectCenterMinNum": "3"
				}
			}
		},
		"imagePath": "N\/A"
	}
	*/

	m_server.resource["^/get_detecting_parameters"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{			
			//step1:获取数据中产品数据
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
			string sBoard = SimpleWeb::getValue(query_fields, "board");
			string sCam = SimpleWeb::getValue(query_fields, "camera");
			string sProd = SimpleWeb::getValue(query_fields, "prod");
			if (!sProd.empty())
			{
				Database db;
				if (db.prod_setting_read(sProd, setting))
				{
					LogERROR << "Read product setting in database failed";
					response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Product Setting Failed");
				}
			}
			LogINFO << "get_detecting_parameters: prod = " << sProd << " board = " << sBoard << " camera = " << sCam;

			//step2:解析json
			ptree ptCamGroups;
			string sNodeName = ("UISetting.setup.flawParams-" + sCam);
			std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
			read_json(ss1, ptCamGroups);
		
			//step3:构造json和赋值
			ptree root;
			//step3-0:画框提示


			//step3-1:画框参数
			ptree boxes;
			vector<int> vecMax = CustomizedJsonConfig::instance().getVector<int>("UISetting.setup.draw.maxLimit");			
			vector<int> vecMin = CustomizedJsonConfig::instance().getVector<int>("UISetting.setup.draw.minLimit");
			const int startIndex = CustomizedJsonConfig::instance().get<int>("UISetting.setup.draw.startIndex");			
			if(vecMax.size() != vecMin.size())
			{
				LogERROR << "draw box size is not right!!!";
				response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "draw box size is not right");
				return 1;
			}
			
			const int camIdx = stoi(sCam);
			const int totalBoxNum = accumulate(vecMax.begin(), vecMax.end(), 0);
			int rectIdx = startIndex + camIdx * totalBoxNum * 4;
			const int numBoxGroup = vecMax.size();		
			for(int i = 0; i != numBoxGroup; i++)
			{
				ptree boxGroup;
				const int numBox = vecMax[i];
				for(int j = 0; j != numBox; ++j)
				{
					if((rectIdx +  3) >= setting.float_settings.size())
					{
						LogERROR << "box seeting index:" << (rectIdx +  3) << " is out of range of floot_setting size";
						response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "box setting index is out of range of floot_setting size");
						return 1;
					}

					ptree x(Convert(setting.float_settings[rectIdx +  0]));
					ptree y(Convert(setting.float_settings[rectIdx +  1]));
					ptree w(Convert(setting.float_settings[rectIdx +  2]));
					ptree h(Convert(setting.float_settings[rectIdx +  3]));
					rectIdx += 4;

					ptree rect;
					rect.put_child("x", x);
					rect.put_child("y", y);
					rect.put_child("width", w);
					rect.put_child("height", h);
					boxGroup.push_back(make_pair("", rect));
				}			
				boxes.push_back(make_pair("", boxGroup));
			}
			root.put_child("rect", boxes);

			//step3-2:瑕疵参数	
			for(const auto &group:ptCamGroups)
			{
				ptree newGroup;
				const string sGroupId = group.second.get<string>("id");
				const auto &items = group.second.get_child("params");
				for(const auto &item:items)
				{
					ptree newItem_params;
					const int item_index = item.second.get<int>("index");
					const string sItemId = item.second.get<string>("id");
					const float item_enable_value = setting.float_settings[item_index];
					if(item_index >= setting.float_settings.size())
					{
						LogERROR << "Item:" << sItemId << " index:" << item_index << " is out of range of float_settings size";
						response->write(SimpleWeb::StatusCode::server_error_internal_server_error, sItemId + " index is out of range of float_settingS size");
						return 1;
					}

					const auto &params = item.second.get_child("paramsList");
					for(const auto &param:params)
					{
						const int param_index = param.second.get<int>("index");
						const string sParamId = param.second.get<string>("id");
						float param_value = 0.f;
						if (param.second.get_child_optional("isRecordToDB"))
						{
							if (!param.second.get<bool>("isRecordToDB"))
							{
								const string& sJsonNodeName = "flawParams-"+ sCam + "." + sGroupId + "." + sItemId + "." + sParamId;
								param_value = AppJsonConfig::instance().get<float>(sJsonNodeName);
								ptree newParam(Convert(param_value));
								newItem_params.put_child(sParamId, newParam);
								continue;
							}
						}

						if(param_index >= setting.float_settings.size() || param_index < 0)
						{
							LogERROR << "param:" << sParamId << " index:" << param_index << " is out of range of floot_setting size";
							response->write(SimpleWeb::StatusCode::server_error_internal_server_error, sParamId + " index is out of range of floot_setting size");
							return 1;
						}
						param_value = setting.float_settings[param_index];
						ptree newParam(Convert(param_value));
						newItem_params.put_child(sParamId, newParam);
					}

					ptree newItem;
					ptree newItem_enable(Convert(item_enable_value));
					newItem.put_child("enable", newItem_enable);
					newItem.put_child("parameters", newItem_params);
					newGroup.put_child(sItemId, newItem);
				}
				root.put_child(sGroupId, newGroup);
			}

			//step3-3:图片路径
			ptree imagePath(RunningInfo::instance().GetTestProductSetting().getImageName());
			root.put_child("imagePath", imagePath);

			//step4: 字符发送前端
			stringstream ss;
			write_json(ss, root);
			response->write(ss);
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
		}
	};

	return 0;
}

int AppWebServer::GetTestingParams()
{
	/*	
	GET:http://localhost:8080/get_testing_parameters?prod=DGB&board=0&camera=0
	Return detecting_parameters:{	
		rectTips:
		[
			{ txt: '提示文本', img: '提示图片的地址' },
			{ txt: '提示文本', img: '提示图片的地址' },
			{ txt: '提示文本', img: '提示图片的地址' },
			{ txt: '提示文本', img: '提示图片的地址' },
			{ txt: '提示文本', img: '提示图片的地址' }
		]，
		"rect": [
			[
				{
					"x": "862.413",
					"y": "979.463",
					"width": "3865.64",
					"height": "1677.19"
				}
			],
			[
				{
					"x": "1897.41",
					"y": "748.773",
					"width": "311.745",
					"height": "2119.87"
				},
				{
					"x": "3138.15",
					"y": "586.665",
					"width": "405.268",
					"height": "2431.61"
				}
			],
			[
				{
					"x": "1610.6",
					"y": "773.712",
					"width": "118.463",
					"height": "274.336"
				},
				{
					"x": "2957.34",
					"y": "811.121",
					"width": "112.228",
					"height": "261.866"
				}
			]
    	],
		"params_group1": {
			"params_item1": {
				"enable": "1",
				"parameters": {
					"baseDimension": "67.5",
					"upTolerance": "0.4",
					"lowTolerance": "0.4",
					"leftWidthMeasure": "72.5",
					"rightWidthMeasure": "72.5",
					"leftWidthRatio": "0.037",
					"rightWidthRatio": "0.037"
				}
			},
			"params_item2": {
				"enable": "1",
				"parameters": {
					"blackSpotDefectSensitivity": "20",
					"blackSpotDefectMinArea": "30"
				}
			},
			"params_item3": {
				"enable": "1",
				"parameters": {
					"whiteEdgeDefectSensitivity": "70",
					"whiteEdgeDefectMinArea": "35"
				}
			}
		},
		"params_group2": {
			"params_item4": {
				"enable": "1",
				"parameters": {
					"spotDefectSensitivity": "35",
					"spotDefectMinArea": "55",
					"spotDefectMinLength": "10"
				}
			},
			"params_item5": {
				"enable": "1",
				"parameters": {
					"scratchDefectSensitivity": "5",
					"scratchDefectMinRatio": "10",
					"scratchDefectMinLength": "40"
				}
			},
			"params_item6": {
				"enable": "1",
				"parameters": {
					"weekDefectCenterSize": "2",
					"weekDefectCenterSensitivity": "50",
					"weekDefectCenterMinNum": "3"
				}
			}
		},
		"imagePath": "N\/A"
	}
	*/

	m_server.resource["^/get_testing_parameters"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{			
			//step1:获取前端参数
			string sBoard = SimpleWeb::getValue(query_fields, "board");
			string sCam = SimpleWeb::getValue(query_fields, "camera");
			string sProd = SimpleWeb::getValue(query_fields, "prod");
			LogINFO << "get_detecting_parameters: prod = " << sProd << " board = " << sBoard << " camera = " << sCam;

			//step2:解析json
			ptree ptCamGroups;
			string sNodeName = ("UISetting.setup.flawParams-" + sCam);
			std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
			read_json(ss1, ptCamGroups);
		
			//step3:构造json和赋值
			ptree root;
			//step3-0:画框提示


			//step3-1:画框参数
			ptree boxes;
			vector<int> vecMax = CustomizedJsonConfig::instance().getVector<int>("UISetting.setup.draw.maxLimit");			
			vector<int> vecMin = CustomizedJsonConfig::instance().getVector<int>("UISetting.setup.draw.minLimit");
			const int startIndex = CustomizedJsonConfig::instance().get<int>("UISetting.setup.draw.startIndex");			
			if(vecMax.size() != vecMin.size())
			{
				LogERROR << "draw box size is not right!!!";
				response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "draw box size is not right");
				return 1;
			}
			
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
			const int camIdx = stoi(sCam);
			const int totalBoxNum = accumulate(vecMax.begin(), vecMax.end(), 0);
			int rectIdx = startIndex + camIdx * totalBoxNum * 4;
			const int numBoxGroup = vecMax.size();		
			for(int i = 0; i != numBoxGroup; i++)
			{
				ptree boxGroup;
				const int numBox = vecMax[i];
				for(int j = 0; j != numBox; ++j)
				{
					if((rectIdx +  3) >= setting.float_settings.size())
					{
						LogERROR << "box seeting index:" << (rectIdx +  3) << " is out of range of floot_setting size";
						response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "box setting index is out of range of floot_setting size");
						return 1;
					}

					ptree x(Convert(setting.float_settings[rectIdx +  0]));
					ptree y(Convert(setting.float_settings[rectIdx +  1]));
					ptree w(Convert(setting.float_settings[rectIdx +  2]));
					ptree h(Convert(setting.float_settings[rectIdx +  3]));
					rectIdx += 4;

					ptree rect;
					rect.put_child("x", x);
					rect.put_child("y", y);
					rect.put_child("width", w);
					rect.put_child("height", h);
					boxGroup.push_back(make_pair("", rect));
				}			
				boxes.push_back(make_pair("", boxGroup));
			}
			root.put_child("rect", boxes);

			//step3-2:瑕疵参数	
			for(const auto &group:ptCamGroups)
			{
				ptree newGroup;
				const string sGroupId = group.second.get<string>("id");
				const auto &items = group.second.get_child("params");
				for(const auto &item:items)
				{
					ptree newItem_params;
					const int item_index = item.second.get<int>("index");
					const string sItemId = item.second.get<string>("id");
					const float item_enable_value = setting.float_settings[item_index];
					if(item_index >= setting.float_settings.size())
					{
						LogERROR << "Item:" << sItemId << " index:" << item_index << " is out of range of float_settings size";
						response->write(SimpleWeb::StatusCode::server_error_internal_server_error, sItemId + " index is out of range of float_settingS size");
						return 1;
					}

					const auto &params = item.second.get_child("paramsList");
					for(const auto &param:params)
					{
						const int param_index = param.second.get<int>("index");
						const string sParamId = param.second.get<string>("id");
						float param_value = 0.f;
						if (param.second.get_child_optional("isRecordToDB"))
						{
							if (!param.second.get<bool>("isRecordToDB"))
							{
								const string& sJsonNodeName = "flawParams-"+ sCam + "." + sGroupId + "." + sItemId + "." + sParamId;
								param_value = AppJsonConfig::instance().get<float>(sJsonNodeName);
								ptree newParam(Convert(param_value));
								newItem_params.put_child(sParamId, newParam);
								continue;
							}
						}

						if(param_index >= setting.float_settings.size() || param_index < 0)
						{
							LogERROR << "param:" << sParamId << " index:" << param_index << " is out of range of floot_setting size";
							response->write(SimpleWeb::StatusCode::server_error_internal_server_error, sParamId + " index is out of range of floot_setting size");
							return 1;
						}
						param_value = setting.float_settings[param_index];
						ptree newParam(Convert(param_value));
						newItem_params.put_child(sParamId, newParam);
					}

					ptree newItem;
					ptree newItem_enable(Convert(item_enable_value));
					newItem.put_child("enable", newItem_enable);
					newItem.put_child("parameters", newItem_params);
					newGroup.put_child(sItemId, newItem);
				}
				root.put_child(sGroupId, newGroup);
			}

			//step3-3:图片路径
			ptree imagePath(RunningInfo::instance().GetTestProductSetting().getImageName());
			root.put_child("imagePath", imagePath);

			//step4: 字符发送前端
			stringstream ss;
			write_json(ss, root);
			response->write(ss);

		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
		}
	};

	return 0;

}

//获取PLC参数 
int AppWebServer::GetPlcParams()
{
	/*
	GET: http://localhost:8080/get_plc_params?prod=prod1
		{
			"enable": "true"
		}
	*/
	m_server.resource["^/get_plc_params"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();	
			string prod = SimpleWeb::getValue(query_fields, "prod");
			if(!prod.empty())
			{
				Database db;
				if (db.prod_setting_read(prod, setting))
				{
					LogERROR << "Read product setting in database failed";
					response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Product Setting Failed");
				}
			}

			ptree params;
			string sNodeName = ("UISetting.setup.plcParams");
			std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
			read_json(ss1, params);

			ptree root;
			ptree plc_params;	
			for(const auto &param:params)
			{
				if(!param.second.get_child_optional("index") || !param.second.get_child_optional("id"))
				{
					LogERROR << "no complete child node[index, id] in parent node [UISetting.setup.commonParams]";
					return 1;
				}
				const int index = param.second.get<float>("index");
				const string sId = param.second.get<string>("id");
				ptree paramValue(Convert(setting.float_settings[index]));
				plc_params.put_child(sId, paramValue);
			}
			root.put_child("plc_params", plc_params);

			stringstream ss;
			write_json(ss, root);
			response->write(ss);
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
		}
	};
	return 0;
}

//设置PLC参数
int AppWebServer::SetPlcParams()
{
	/*
	POST: http://localhost:8080/set_plc_params?prod=prod1
		{
		"enable": "true"
		}
	*/
	m_server.resource["^/set_plc_params"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			//step1:从数据库获取数值
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();	
			string sProd = SimpleWeb::getValue(query_fields, "prod");
			if(!sProd.empty())
			{
				Database db;
				if (db.prod_setting_read(sProd, setting))
				{
					LogERROR << "Read product setting in database failed";
					response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Product Setting Failed");
				}
			}

			//step2:解析UI端json
			ptree pt;
			read_json(request->content, pt);
			if(pt.empty())
			{
				LogERROR << "Invalid content in test product setting";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Content");
				return 1;
			}
			if (!pt.get_child_optional("plc_params"))
			{
				LogERROR << "find plc_params failed";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "find plc_params failed");
				return 1;
			}
			ptree parametersTree = pt.get_child("plc_params");
	
			//step3:更新数值
			ptree params;
			string sNodeName = ("UISetting.setup.plcParams");
			std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
			read_json(ss1, params);
			for(const auto &param:params)
			{
				if(!param.second.get_child_optional("index") || !param.second.get_child_optional("id"))
				{
					LogERROR << "no complete child node[index, id] in parent node [UISetting.setup.commonParams]";
					return 1;
				}
				const int index = param.second.get<float>("index");
				const string sId = param.second.get<string>("id");
				setting.float_settings[index] = parametersTree.get<float>(sId);
			}

			//step4:保存到数据库
			auto writeProdSetting = [](const string &prod, const ProductSetting::PRODUCT_SETTING &setting)
			{
				Database db;
				return db.prod_setting_write(prod, setting);
			};

			if(writeProdSetting(sProd, setting))
			{
				LogERROR << "Update product setting " << sProd << " in database failed";
				response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Update Product Setting " + sProd + " Failed");
				return 1;
			}

			// update the memory each time setting is confirmed
			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::SetCommonParams()
{
	/*
	POST: http://localhost:8080/set_common_params?prod=prod1
		{
			"enable": "true"
		}
	*/
	m_server.resource["^/set_common_params"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			//step1:从数据库获取数值
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();	
			string sProd = SimpleWeb::getValue(query_fields, "prod");
			if(!sProd.empty())
			{
				Database db;
				if (db.prod_setting_read(sProd, setting))
				{
					LogERROR << "Read product setting in database failed";
					response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Product Setting Failed");
				}
			}

			//step2:解析UI端json
			ptree pt;
			read_json(request->content, pt);
			if (pt.empty())
			{
				LogERROR << "Invalid content in test product setting";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Content");
				return 1;
			}
			if (!pt.get_child_optional("common_params"))
			{
				LogERROR << "find common_params failed";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "find common_params failed");
				return 1;
			}
			ptree parametersTree = pt.get_child("common_params");
	
			//step3:更新数值
			ptree params;
			string sNodeName = ("UISetting.setup.commonParams");
			std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
			read_json(ss1, params);
			for(const auto &param:params)
			{
				if(!param.second.get_child_optional("index") || !param.second.get_child_optional("id"))
				{
					LogERROR << "no complete child node[index, id] in parent node [UISetting.setup.commonParams]";
					return 1;
				}
				const int index = param.second.get<float>("index");
				const string sId = param.second.get<string>("id");
				setting.float_settings[index] = parametersTree.get<float>(sId);
			}

			//step4:保存到数据库
			auto writeProdSetting = [](const string &prod, const ProductSetting::PRODUCT_SETTING &setting)
			{
				Database db;
				return db.prod_setting_write(prod, setting);
			};

			if(writeProdSetting(sProd, setting))
			{
				LogERROR << "Update product setting " << sProd << " in database failed";
				response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Update Product Setting " + sProd + " Failed");
				return 1;
			}

			// update the memory each time setting is confirmed
			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::GetCommonParams()
{
	m_server.resource["^/get_common_params"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();	
			string prod = SimpleWeb::getValue(query_fields, "prod");
			if(!prod.empty())
			{
				Database db;
				if (db.prod_setting_read(prod, setting))
				{
					LogERROR << "Read product setting in database failed";
					response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Product Setting Failed");
				}
			}

			ptree params;
			string sNodeName = ("UISetting.setup.commonParams");
			std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
			read_json(ss1, params);

			ptree root;
			ptree plc_params;	
			for(const auto &param:params)
			{
				if(!param.second.get_child_optional("index") || !param.second.get_child_optional("id"))
				{
					LogERROR << "no complete child node[index, id] in parent node [UISetting.setup.commonParams]";
					return 1;
				}
				const int index = param.second.get<float>("index");
				const string sId = param.second.get<string>("id");
				ptree paramValue(Convert(setting.float_settings[index]));
				plc_params.put_child(sId, paramValue);
			}
			root.put_child("common_params", plc_params);

			stringstream ss;
			write_json(ss, root);
			response->write(ss);
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
		}
	};
	return 0;
}

//获取相机参数
int AppWebServer::GetCameraParams()
{
	/*
	GET: http://localhost:8080/get_camera_params?prod=prod1
	*/
	m_server.resource["^/get_camera_params"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			// step1: 解析传入的参数并读取数据库参数
			const string sBoard = SimpleWeb::getValue(query_fields, "board");
			const string sCam = SimpleWeb::getValue(query_fields, "camera");
			const string sProd = SimpleWeb::getValue(query_fields, "prod");
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
			if (!sProd.empty())
			{
				Database db;
				if (db.prod_setting_read(sProd, setting))
				{
					LogERROR << "Read product setting in database failed";
					response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Product Setting Failed");
				}
			}

			//step2:解析相机参数配置
			ptree camConfigParams;
			string sNodeName = "UISetting.setup.cameraParams-" + sCam;
			std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
			read_json(ss1, camConfigParams);

			//step3:构造参数json
			ptree root;
			ptree cameraParams;
			for(const auto &paramconfig:camConfigParams)
			{
				const string sID = paramconfig.second.get<string>("id");
				if(!paramconfig.second.get_child_optional("index") || !paramconfig.second.get_child_optional("id"))
				{
					LogERROR << "no complete child node[index, id] in parent node [" << sNodeName << "]";
					return 1;
				}
				// step4:判断是从数据库读取还是从配置文件中读取 
				// tip: 如果从配置文件中获取，则所有工程的相机参数共用一个，此处与数据库中读取的情况不一致
				if (paramconfig.second.get_child_optional("isRecordToDB"))
				{
					if (!paramconfig.second.get<bool>("isRecordToDB"))
					{
						const string& sJsonNodeName = "cameraParams-"+ sCam + "." + sID;
						ptree param(Convert(AppJsonConfig::instance().get<float>(sJsonNodeName)));
						cameraParams.put_child(sID, param);
						continue;
					}
				}
				// 否则从数据库读取
				const int index = paramconfig.second.get<int>("index");
				if(index >= setting.float_settings.size() || index < 0)
				{
					LogERROR << "cameraParams-" << sCam << "." << sID << " index:" << index << " is out of range of floot_setting size";
					return 1;
				}			
				ptree param(Convert(setting.float_settings[index]));
				cameraParams.put_child(sID, param);
			}
			root.put_child("params", cameraParams);

			//step5:转化成字符流发送给前端
			stringstream ss;
			write_json(ss, root);
			response->write(ss);
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
		}
	};
	return 0;
}

//设置相机参数
int AppWebServer::SetCameraParams()
{
	/*
   POST: http://localhost:8080/set_camera_params?prod=prod1
   {
     "params": {
		"exposure": "500",
		"gain": 2,
		"gammaEnable": true,
		"gammaValue": 1.1
	  }
   }
  */
	m_server.resource["^/set_camera_params"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			//step1:获取数据库数据
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();	
			const string sBoard = SimpleWeb::getValue(query_fields, "board");
			const string sCam = SimpleWeb::getValue(query_fields, "camera");
			string sProd = SimpleWeb::getValue(query_fields, "prod");
			if (!sProd.empty())
			{
				Database db;
				if (db.prod_setting_read(sProd, setting))
				{
					LogERROR << "Read product setting in database failed";
					response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Product Setting Failed");
				}
			}

			//step2:解析前端发过来的json
			ptree pt;
			read_json(request->content, pt);
			// load product setting, and other test related values
			if(pt.empty())
			{
				LogERROR << "Invalid content in test product setting";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Content");
				return 1;
			}
			if (!pt.get_child_optional("params"))
			{
				LogERROR << "find camera_params failed";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "find camera_params failed");
				return 1;
			}

			ptree parametersTree = pt.get_child("params");
			if(!parametersTree.get_child_optional("exposure") || !parametersTree.get_child_optional("gain") ||
				!parametersTree.get_child_optional("gammaEnable") || !parametersTree.get_child_optional("gammaValue"))
			{
				LogERROR << "find parameters in camera_params failed";
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "find parameters in camera_params failed");
				return 1;
			}

			int exposure = int(parametersTree.get<int>("exposure"));
			float gain = parametersTree.get<float>("gain");
			bool gammaEnable = parametersTree.get<bool>("gammaEnable");
			float gammaValue = parametersTree.get<float>("gammaValue");

			//step3:解析配置json,更新数据
			ptree camConfigParams;
			string sNodeName = "UISetting.setup.cameraParams-" + sCam;
			std::stringstream ss1 = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
			read_json(ss1, camConfigParams);
			for(const auto &paramconfig:camConfigParams)
			{
				const string sID = paramconfig.second.get<string>("id");
				// step4:判断是记录到数据库还是记录到配置文件中 
				// tip: 如果记录到配置文件中，则所有工程的相机参数共用一个，此处与数据库的情况不一致
				if (paramconfig.second.get_child_optional("isRecordToDB"))
				{
					if (!paramconfig.second.get<bool>("isRecordToDB"))
					{
						const string& sJsonNodeName = "cameraParams-"+ sCam + "." + sID;
						if(sID == "gammaEnable")
						{
							AppJsonConfig::instance().set<float>(sJsonNodeName, (float)gammaEnable);
						}
						else
						{
							AppJsonConfig::instance().set<float>(sJsonNodeName, parametersTree.get<float>(sID));
						}
						continue;
					}
				}
				// 写入到数据库中
				const int index = paramconfig.second.get<int>("index");
				if(index >= setting.float_settings.size() || index < 0)
				{
					LogERROR << "cameraParams-" << sCam << "." << sID << " index:" << index << " is out of range of floot_setting size";
					response->write(SimpleWeb::StatusCode::client_error_bad_request, " index is out of range of floot_setting size");
					return 1;
				}
				if(sID == "gammaEnable")
				{
					setting.float_settings[index] = gammaEnable;
				}
				else
				{
					setting.float_settings[index] = int(parametersTree.get<float>(sID));
				}
				
			}
			RunningInfo::instance().GetTestProductSetting().UpdateCurrentProductSetting(setting);

			//step5:相机参数解析，相机设置
			string cameraName = "CAM" + sCam;
			if(getCameraManager()->getCamera(cameraName) == nullptr)
			{
				LogERROR << "Invalid camera" << cameraName;
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Camera");
				return 1;
			}

			vector<string> vCamType = CustomizedJsonConfig::instance().getVector<string>("CAMERA_TYPE");
			if(getCameraManager()->getCamera(cameraName)->deviceName().substr(0, 4) != "Mock")
			{
				if (!getCameraManager()->getCamera(cameraName)->cameraStarted())
				{
					if (!getCameraManager()->startCamera(cameraName))
					{
						LogERROR << "Start " << cameraName << " failed";
						response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Start Camera Failed");
						return 1;
					}
				}
				shared_ptr<CameraConfig> config = getCameraManager()->getCamera(cameraName)->config();
				if ("AreaArray" == vCamType.at(stoi(sBoard)))
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
				else if ("LineScan" == vCamType.at(stoi(sBoard)))
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
			}

			db_utils::ALARM("ModifyParameters");
			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}


//完成2022-11-01
int AppWebServer::SaveCameraParams()
{
	/*
   POST: http://localhost:8080/save_camera_params?prod=prod1
   {
   }
  */
	m_server.resource["^/save_camera_params"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();
			string prod = SimpleWeb::getValue(query_fields, "prod");
			auto writeProdSetting = [](const string &prod, const ProductSetting::PRODUCT_SETTING &setting)
			{
				Database db;
				return db.prod_setting_write(prod, setting);
			};

			if(writeProdSetting(prod, setting))
			{
				LogERROR << "Update product setting " << prod << " in database failed";
				response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Update Product Setting " + prod + " Failed");
				return 1;
			}
			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

//完成2022-11-02
int AppWebServer::SetDefaultAutoParams()
{
	/*
   POST: http://localhost:8080/set_default_auto_params
   Return: Success
	*/
	m_server.resource["^/set_default_auto_params"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		try
		{
			//1) 获取当前设定最新内存数据
			ProductSetting::PRODUCT_SETTING setting = RunningInfo::instance().GetTestProductSetting().GetSettings();

			//2) 设置状态参数: purge mode, run mode, image save type
			setting.float_settings[int(ProductSettingFloatMapper::SAVE_IMAGE_TYPE)] = (int)SaveImageType::ALL;//全部保存
			setting.float_settings[int(ProductSettingFloatMapper::PURGE_SIGNAL_TYPE)] = (int)PurgeMode::NORMAL;//正常剔除
			setting.float_settings[int(ProductSettingFloatMapper::RUN_MODE)]  = (int)RunMode::RUN_DETECT;//检测模式

			//3) 设置相机参数：camera params
			const int numCamera = CustomizedJsonConfig::instance().get<int>("UISetting.common.cameraNumber");
			for(int camIdx = 0; camIdx != numCamera; ++camIdx)
			{
				ptree camConfigParams;
				string sNodeName = "UISetting.setup.cameraParams-" + to_string(camIdx);
				std::stringstream ssCamera = CustomizedJsonConfig::instance().getJsonStream(sNodeName);
				read_json(ssCamera, camConfigParams);
				for(const auto &paramconfig:camConfigParams)
				{
					const string sID = paramconfig.second.get<string>("id");
					const float defaultValue = paramconfig.second.get<float>("defaultValue");

					if (paramconfig.second.get_child_optional("defaultValue"))
					{
						if (paramconfig.second.get_child_optional("isRecordToDB"))
						{
							if (!paramconfig.second.get<bool>("isRecordToDB"))
							{
								const string& sJsonNodeName = "cameraParams-"+ to_string(camIdx) + "." + sID;
								AppJsonConfig::instance().set<float>(sJsonNodeName, defaultValue);
								continue;
							}
						}

						const int index = paramconfig.second.get<int>("index");
						if(index >= setting.float_settings.size() || index < 0)
						{
							LogERROR <<  sNodeName <<  " index:" << index << " is out of range of float_settings size: " << setting.float_settings.size();
							response->write(SimpleWeb::StatusCode::server_error_internal_server_error, sNodeName + " index is out of range of float_settingS size");
							return 1;
						}
						setting.float_settings[index] = defaultValue;
					}
				}
			}
			
			//4) 设置PLC参数：PLC params
			ptree ptPlcParams;
			string sPlcNodeName = ("UISetting.setup.plcParams");
			std::stringstream ssPlc = CustomizedJsonConfig::instance().getJsonStream(sPlcNodeName);
			read_json(ssPlc, ptPlcParams);
			for(const auto &ptParam:ptPlcParams)
			{
				const int index = ptParam.second.get<float>("index");
				if(ptParam.second.get_child_optional("defaultValue"))
				{
					if(index >= setting.float_settings.size())
					{
						LogERROR <<  sPlcNodeName <<  " index:" << index << " is out of range of float_settings size: " << setting.float_settings.size();
						response->write(SimpleWeb::StatusCode::server_error_internal_server_error, sPlcNodeName + " index is out of range of float_settingS size");
						return 1;
					}
					const float defaultValue = ptParam.second.get<float>("defaultValue");
					setting.float_settings[index] = defaultValue;
				}
			}
			
			//5)瑕疵参数



			RunningInfo::instance().GetTestProductSetting().UpdateCurrentProductSetting(setting);
			response->write("Success");
		
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;
}

int AppWebServer::TriggerCamera()
{
	/*
	POST: http://localhost:8080/trigger_camera
	Return: Success
	*/
	m_server.resource["^/trigger_camera"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request)
	{
		auto query_fields = request->parse_query_string();
		try
		{
			response->write("Success");
		}
		catch (const exception &e)
		{
			LogERROR << e.what();
			response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
			return 1;
		}
		return 0;
	};
	return 0;	
}


 int AppWebServer::GetHistoryNg()
 {
	/*  GET: http://localhost:8080/cameras/0/history_image
  	Return:
    image in jpg format
  */
	m_server.resource["^/get_history_ng"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) 
	{
		auto query_fields = request->parse_query_string();
		try 
		{
			boost::property_tree::ptree root;
			boost::property_tree::ptree record;
			const map<string, string>  &customerData = RunningInfo::instance().GetRunningData().getCustomerData();
			for(auto it = customerData.begin(); it != customerData.end(); it++ )
			{
				string sImagePath(it->second);
				string::size_type idx = sImagePath.find(".jpg");
				if (idx == string::npos)
				{
					continue;
				}
				const string sKey =  it->first;
				const string sBoardID =  sKey.substr(0, 1);
				const int boardId = stoi(sBoardID);
				boost::property_tree::ptree histrory;
				histrory.put_child("path", boost::property_tree::ptree(sImagePath));
				histrory.put_child("board", boost::property_tree::ptree(sBoardID));
				histrory.put_child("camera", boost::property_tree::ptree(sBoardID));

				vector<string> vResult;
				stringSplit(sImagePath, "/", vResult);
				string strLast(vResult[vResult.size() - 1]);

				string::size_type idx2 = strLast.find("-TM202");
				int defectType = stoi(strLast.substr(1, idx2 -1));

				string::size_type idx3 = strLast.find("-CNT");
				string::size_type idx4 = strLast.find("-PIC");
				string sProductCount(strLast.substr(idx3 + 4, idx4 -idx3 - 4));
				string sCaptureTimes(strLast.substr(idx4 + 4, 1));
				string sImageName = CustomizedJsonConfig::instance().getVector<string>("CAMERA_NAME")[boardId];
				string sDefectType = CustomizedJsonConfig::instance().get<string>("UISetting.i18n.chn.common.label.defect" + to_string(defectType - 1));
				//cout << "sDefectType=" << sDefectType << endl;
				//cout << "sImageName=" << sImageName << endl;
				//cout << "sProductCount=" << sProductCount << endl;
				
				string sLabelKey = "产品" + sProductCount + "_" + sCaptureTimes + " " + sImageName + "_" + sDefectType;
				//cout << "sLabelKey=" << sLabelKey << endl;
				histrory.put_child("labelKey", boost::property_tree::ptree(sLabelKey));
				record.push_back(make_pair("", histrory));
			}
			root.put_child("history", record);
		
			stringstream ss;
			write_json(ss, root);
			response->write(ss);			
		}
		catch(...) 
		{
			LogERROR << "Bad Request";
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
		}
	};
	return 0;
 }



 int AppWebServer::GetCameraResultImage()
 {
	/* 
		GET: http://localhost:8080/cameras/0/1/read_result_image
		Return:
			image in jpg format
  	*/
 	m_server.resource["^/cameras/([0-9])/([1-3])/read_result_image"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) 
	{
		string id = request->path_match[1];
		string sTimes = request->path_match[2];
		try 
		{
			if(m_pCameraManager->getCamera("CAM" + id) == nullptr) 
			{
				LogERROR << "Invalid camera: CAM" << id;
		#ifdef __linux__
				cv::Mat fakeFrame = cv::imread("/opt/seeking.jpg", cv::IMREAD_COLOR);
		#elif _WIN64
				cv::Mat fakeFrame = cv::imread("C:\\opt\\seeking.jpg", cv::IMREAD_COLOR);
		#endif
				sendImage(response, fakeFrame);
			}
			else 
			{
				//LogINFO << "Received camera image request, camera Id: " << id;
				cv::Mat image;
				ClassificationResult result;				
				int captureTimes = stoi(sTimes);
				AppRunningResult::instance().getCurrentResult(image, result, "CAM" + id, captureTimes);
				if(image.empty()) 
				{
					LogERROR << "Camera CAM" << id << " current processed image is invalid";
					response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Camera Current Processed Image Invalid");
					db_utils::ALARM("CameraError");
				}
				else 
				{
					sendImage(response, image);
					// int camMode = RunningInfo::instance().GetProductionInfo().GetCameraDisplayMode();
					// if(camMode == 1) 
					// { 
					// 	// which means to display all images
					// 	sendImage(response, image);
					// }
					// else if(result != ClassifierResultConstant::Good && result != ClassifierResultConstant::Classifying) 
					// {
					// 	sendImage(response, image);
					// }
				}
			}
		}
		catch(...) 
		{
			LogERROR << "Bad Request";
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
		}
  	};

 }

 void AppWebServer::sendImage(const shared_ptr<HttpServer::Response> &response, const cv::Mat &frame) 
 {
		if(frame.empty()) 
		{
			LogERROR << "Frame is empty";
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
			return;
		}

		// encode image into jpg
		vector<uchar> ubuff; //buffer for coding
		vector<int> param = {cv::IMWRITE_JPEG_QUALITY, 95};
		cv::imencode(".jpg", frame, ubuff, param);
		// Uncomment the following line to enable Cache-Control
		// header.emplace("Cache-Control", "max-age=86400");

		SimpleWeb::CaseInsensitiveMultimap header;
		vector<char> buff(ubuff.begin(), ubuff.end());

		header.emplace("Content-Length", to_string(ubuff.size()));
		response->write(header);

		// Trick to define a recursive function within this scope (for example purposes)
		class FileServer {
		public:
		static void read_and_send(const shared_ptr<HttpServer::Response> &response, const vector<char> &ifs, const long long start) {
			// Read and send 128 KB at a time
			const long long MAX_BUFFER_SIZE = 131072; // Safe when server is running on one thread
			static vector<char> buffer(MAX_BUFFER_SIZE);
			streamsize read_length;
			if(start < ifs.size()) {
			if((start + MAX_BUFFER_SIZE) < ifs.size()) {
				read_length = MAX_BUFFER_SIZE;
			}
			else {
				read_length = ifs.size() - start;
			}
			for(int i = 0; i < read_length; ++i)
				buffer[i] = ifs[start + i];

			response->write(&buffer[0], read_length);

			const long long index = start + read_length;
			if(read_length == MAX_BUFFER_SIZE) {
				response->send([response, ifs, index](const SimpleWeb::error_code &ec) {
				if(!ec)
					read_and_send(response, ifs, index);
				else
					LogERROR << "Connection interrupted";
				});
			}
			}
		}
		};
		FileServer::read_and_send(response, buff, 0);
}

int AppWebServer::GetCurrentResult()
{
	/* 
		GET: http://localhost:8080/current_result
		Return:
			{
				"totalNum": 1000,
				"totalDefect": 10
				"ng_list":[20, 32, 51]
			}
  	*/
 	m_server.resource["^/current_result"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) 
	{
		string id = request->path_match[1];
		try 
		{
			int iTotalNumber = 0, iTotalDefect = 0;
			std::vector<int> vTotalDefectIndices;
			AppRunningResult::instance().getNGProductCountList(vTotalDefectIndices, iTotalNumber, iTotalDefect);

			ptree root;
			string sTotalNumID("totalNum");
			ptree totalNumValue(Convert(iTotalNumber));
			root.put_child(sTotalNumID, totalNumValue);

			string sTotalDefectID("totalDefect");
			ptree totalDefectValue(Convert(iTotalDefect));
			root.put_child(sTotalDefectID, totalDefectValue);

			string sNGListID("ng_list");
			ptree ngList;
			for(const auto &item : vTotalDefectIndices)
			{
				ptree value(Convert(item));
				ngList.push_back(make_pair("", value));	
			}
			root.put_child(sNGListID, ngList);
			
			stringstream ss;
			write_json(ss, root);
			response->write(ss);
		}
		catch(...) 
		{
			LogERROR << "Bad Request";
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
		}
	};
	return 0;
}

int AppWebServer::GetDefect() 
{
	/*
	GET: http://localhost:8080/get_defect?startTime=1711959991&endTime=1712133154&prod=5566&board=-1&lot=01
	Return:
    "0": {
        "time": "2024-06-19 16:22:29",
        "prod": "5566",
        "lot": "01",
        "board": "4",
        "prod_no": "1",
        "defect1": "0",
        "defect2": "1",
        "defect3": "0"
    }
	*/
	m_server.resource["^/get_defect"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) 
	{
    	auto query_fields = request->parse_query_string();
		try 
		{
			const string startTimeStamp = SimpleWeb::getValue(query_fields, "startTime");
			const string startTime = shared_utils::getTime(startTimeStamp);
			const string endTimeStamp = SimpleWeb::getValue(query_fields, "endTime");
			const string endTime = shared_utils::getTime(endTimeStamp);
			const string prod = SimpleWeb::getValue(query_fields, "prod");
			const string board = SimpleWeb::getValue(query_fields, "board");
			//const string lot = SimpleWeb::getValue(query_fields, "lot");
			const string lot = "-1";  // 返回所有批号的，因为UI要做模糊/精确查询
			const bool bIsUseKey = CustomizedJsonConfig::instance().get<bool>("IS_USE_BOARDID_KEY");
			
			AppDatabase db;
			vector<AppDatabase::DefectData> vt;
			if(db.defectRead(vt, startTime, endTime, prod, board, lot)) 
			{
				LogERROR << "Read defect in database failed";
				response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Defect Failed");
			}
			else 
			{
				boost::property_tree::ptree root;
				for(int i = 0; i < vt.size(); ++i) 
				{
					boost::property_tree::ptree cell;
					cell.put<string>("time", vt[i].hhmmss);
					cell.put<string>("prod", vt[i].prod);
					cell.put<string>("lot", vt[i].lot);
					cell.put<string>("board", vt[i].board);
					cell.put<string>("prod_no", vt[i].prod_no);
					for(const auto &itr : vt[i].defects) 
					{
						cell.put<string>(itr.first, to_string(itr.second));
					}
					root.push_back(make_pair(to_string(i), cell));
				}

				stringstream ss;
				write_json(ss, root);
				response->write(ss);
			}
		}
		catch(...) {
			LogERROR << "Bad Request";
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
		}
	};

	return 0;
}

int AppWebServer::GetRealReportWithLot()
{
	/*
	GET: http://localhost:8080/get_real_report_with_lot?startTime=1711959991&endTime=1712174125&prod=5566&board=-1&lot=01
	*/
	m_server.resource["^/get_real_report_with_lot"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) 
    {
		auto query_fields = request->parse_query_string();
		try 
		{
			const string startTimeStamp = SimpleWeb::getValue(query_fields, "startTime");
			const string startTime = shared_utils::getTime(startTimeStamp);
			const string endTimeStamp = SimpleWeb::getValue(query_fields, "endTime");
			const string endTime = shared_utils::getTime(endTimeStamp);
			const string prod = SimpleWeb::getValue(query_fields, "prod");
			const string board = SimpleWeb::getValue(query_fields, "board");
			//const string lot = SimpleWeb::getValue(query_fields, "lot");
			const string lot = "-1";  // 返回所有批号的，因为UI要做模糊/精确查询
		    const bool bIsUseKey = CustomizedJsonConfig::instance().get<bool>("IS_USE_BOARDID_KEY");
			const int iBoardIdKey = CustomizedJsonConfig::instance().get<int>("USE_BOARDID_KEY");
				
			AppDatabase db;
			vector<AppDatabase::ReportData> vReportData;
			if(db.report_read_with_lot(vReportData, startTime, endTime, prod, board)) 
			{
				LogERROR << "Read report in database failed";
				response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Report in Failed");
			}
			else
			{
				if (vReportData.size() == 0)
				{
					boost::property_tree::ptree root;
					stringstream ss;
					write_json(ss, root);
					response->write(ss);
					return;
				}
				// 删除总数为0的记录
				vReportData.erase(remove_if(vReportData.begin(), vReportData.end(), [](const auto& report) {
					return "0" == report.total;
				}), vReportData.end());
				// report记录按照时间排序 升序
				sort(vReportData.begin(), vReportData.end(), [](const Database::ReportData& rp1, const Database::ReportData& rp2) {
					return rp1.hhmmss < rp2.hhmmss;
				});

				/* 获取真实的统计总数 */
				vector<int> vRealTotal(vReportData.size(), 0);
				if ("-1" == board && bIsUseKey)
				{
					// 当不同board检测同一个产品时， 统计总数时只需记录一个board的数据
					vector<AppDatabase::ReportData> vKeyReportData;
					if (0 == db.report_read_with_lot(vKeyReportData, startTime, endTime, prod, to_string(iBoardIdKey)))
					{
						// 删除总数为0的记录
						vKeyReportData.erase(remove_if(vKeyReportData.begin(), vKeyReportData.end(), [](const auto& report) {
							return "0" == report.total;
						}), vKeyReportData.end());
						for (const auto& iter : vKeyReportData)
						{
							if ("0" == iter.total)
							{
								continue;
							}
							auto reportIter = find_if(vReportData.begin(), vReportData.end(), [=](const AppDatabase::ReportData& info) {
							return ((info.prod == iter.prod)
									&& (info.lot == iter.lot)
									&& (iter.hhmmss <= info.hhmmss));
							});
							int iReportID = vReportData.size() - 1;
							if (reportIter != vReportData.end())
							{
								iReportID = reportIter - vReportData.begin();
							}

							vRealTotal.at(iReportID) += stoi(iter.total);
						}
					}			
				}

				// 记录详细瑕疵记录
				vector<AppDatabase::DefectData> vDefectData;
				if(db.defectRead(vDefectData, startTime, endTime, prod, board, lot))
				{
					LogERROR << "Read defect in database failed";
					response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Defect Failed");
				}
				else 
				{
					// defect 记录按时间排序 
					// sort(vDefectData.begin(), vDefectData.end(), [](const Database::DefectData& rp1, const Database::DefectData& rp2) {
					// 	return rp1.hhmmss < rp2.hhmmss;
					// });
					vector<AppDatabase::DefectData> vCombineDefectData;
					if ("-1" == board && bIsUseKey)
					{
						// defect数据的整理，将同一个产品名，同一批号的不同工位的产品按产品序号合并
						const int iDeltaUnixTime = 600;   // 设置间隔时间，如果同一个产品序号，但时间间隔不满足要求，则当作新的记录处理，此处设置为10min
						for (auto defect : vDefectData)
						{
							auto iter = find_if(vCombineDefectData.begin(), vCombineDefectData.end(), [=](const AppDatabase::DefectData& info) {
								return ((info.prod == defect.prod)
										&& (info.lot == defect.lot)
										&& (info.prod_no == defect.prod_no)
										&& (abs(stoi(getUnixTime(defect.hhmmss)) - stoi(getUnixTime(info.hhmmss))) < iDeltaUnixTime));
							});
							if (iter == vCombineDefectData.end())
							{
								vCombineDefectData.emplace_back(defect);						
							}
						}
					}
					else
					{
						vCombineDefectData.assign(vDefectData.begin(), vDefectData.end());
					}

					vector<map<string, string>> vRealDefects;   // 产品缺陷统计(一个产品只有一个瑕疵项)  map: key: defect name, value: defect number
					vRealDefects.resize(vReportData.size());
					//defect记录插入到对应时间的report记录中
					for (auto defect : vCombineDefectData)
					{
						// 找到第一个时间比defect记录时间晚的，表明要插入的项在reportIter中
						auto reportIter = find_if(vReportData.begin(), vReportData.end(), [=](const AppDatabase::ReportData& info) {
							return ((info.prod == defect.prod)
									&& (info.lot == defect.lot)
									&& (defect.hhmmss <= info.hhmmss));
						});
						int iReportID = vReportData.size() - 1;
						if (reportIter != vReportData.end())
						{
							iReportID = reportIter - vReportData.begin();
						}
						for (auto mapIter : defect.defects)
						{							
							if (mapIter.second != 0)  // 一个NG产品只记录一个瑕疵即可
							{
								string sNumStr = vRealDefects.at(iReportID)[mapIter.first];							
								vRealDefects.at(iReportID)[mapIter.first] = (sNumStr.empty() ? "1" : to_string(stoi(sNumStr) + 1));
								break;
							}
						}
					}
					boost::property_tree::ptree root;
					for(int i = 0; i < vReportData.size(); ++i) 
					{
						boost::property_tree::ptree cell;
						boost::property_tree::ptree real_cell;
						cell.put<string>("time", vReportData[i].hhmmss);
						cell.put<string>("prod", vReportData[i].prod);
						cell.put<string>("lot", vReportData[i].lot);
						cell.put<string>("total", vReportData[i].total);
						cell.put<string>("total_defect", vReportData[i].total_defect);
						for(const auto &itr : vReportData[i].defects) 
						{
							cell.put<string>(itr.first, itr.second);
						}

						int iRealTotalDefect = 0;   // 真实的瑕疵数，一个产品只对应一个瑕疵
						for(const auto &itr : vRealDefects.at(i)) 
						{
							real_cell.put<string>((itr.first), itr.second);
							iRealTotalDefect += (itr.second.empty() ? 0 : stoi(itr.second));
						}
						real_cell.put<string>("total_defect", to_string(iRealTotalDefect));
						if ("-1" == board && bIsUseKey)
						{
							real_cell.put<string>("total", to_string(vRealTotal.at(i)));
						}
						else
						{
							real_cell.put<string>("total", vReportData[i].total);
						}
						
						cell.push_back(make_pair("real", real_cell));
						root.push_back(make_pair(to_string(i), cell));
					}

					stringstream ss;
					write_json(ss, root);
					response->write(ss);
				}

			}
		}
		catch(...) {
			LogERROR << "Bad Request";
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
		}
		};

		return 0;
}


int AppWebServer::GetMesData()
{
	/*
	GET: http://localhost:8080/get_mes_data
	*/
	m_server.resource["^/get_mes_data"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) 
	{
    	auto query_fields = request->parse_query_string();
		try 
		{
			const string startTimeStamp = SimpleWeb::getValue(query_fields, "startTime");
			const string startTime = shared_utils::getTime(startTimeStamp);
			const string endTimeStamp = SimpleWeb::getValue(query_fields, "endTime");
			const string endTime = shared_utils::getTime(endTimeStamp);
			const string startProdNo = SimpleWeb::getValue(query_fields, "startProdNo");
			const string endProdNo = SimpleWeb::getValue(query_fields, "endProdNo");
			const string prod = SimpleWeb::getValue(query_fields, "prod");
			const string board = SimpleWeb::getValue(query_fields, "board");
			const string lot = SimpleWeb::getValue(query_fields, "lot");

			AppDatabase db;
			vector<AppDatabase::MesData> vt;
			if(db.mesRead(vt, startTime, endTime, startProdNo, endProdNo, prod, board, lot)) 
			{
				LogERROR << "Read mes data in database failed";
				response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Defect Failed");
			}
			else
			{
				boost::property_tree::ptree root;
				for(int i = 0; i < vt.size(); ++i) 
				{
					boost::property_tree::ptree cell;
					cell.put<string>("time", vt[i].hhmmss);
					cell.put<string>("prod", vt[i].prod);
					cell.put<string>("board", vt[i].board);
					cell.put<string>("lot", vt[i].lot);
					cell.put<string>("prod_no", vt[i].prod_no);
					cell.put<string>("total", vt[i].total);
					cell.put<string>("total_defect", vt[i].total_defect);
					cell.put<string>("plc_code", vt[i].plc_code);
					cell.put<string>("shift_type", vt[i].shift_type);
					for(const auto &itr : vt[i].defects) 
					{
						cell.put<string>(itr.first, to_string(itr.second));
					}
					root.push_back(make_pair(to_string(i), cell));
				}
				stringstream ss;
				write_json(ss, root);
				response->write(ss);
			}
		}
		catch(...) {
			LogERROR << "Bad Request";
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
		}
	};

	return 0;
}

