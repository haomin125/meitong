#include "web_server.hpp"
#include <ctime>
#include <utility>
#include <string>

#include "classifier_result.h"
#include "customized_json_config.h"
#include "database.h"
#include "db_utils.h"
#include "logger.h"
#include "running_status.h"
#include "shared_utils.h"

#include <boost/optional/optional.hpp>
#include <iomanip>

using namespace std;
using namespace boost::property_tree;

// HTTP-server at port 8080 using 1 thread
// Unless you do more heavy non-threaded processing in the resources,
// 1 thread is usually faster than several threads
WebServer::WebServer(const shared_ptr<CameraManager> pCameraManager) : m_pCameraManager(pCameraManager),
                                                                       m_pIoManager(nullptr),
                                                                       m_pLightSourceManager(nullptr),
                                                                       m_timer(),
                                                                       m_iTimerIntervaInSeconds(24 * 3600),
                                                                       m_fMaxDefectRatio(FLT_MAX),
                                                                       m_iTotalBoards(0) {
  m_server.config.port = 8080;
  addWebCmds();
}


WebServer::WebServer(const shared_ptr<CameraManager> pCameraManager, const string &addr, const unsigned short port)
    : m_pCameraManager(pCameraManager),
      m_pIoManager(nullptr),
      m_pLightSourceManager(nullptr),
      m_timer(),
      m_iTimerIntervaInSeconds(24 * 3600),
      m_fMaxDefectRatio(FLT_MAX),
      m_iTotalBoards(0) {
  // we can not set address in config, it will crash when calling server.Start()
  // m_server.config.address = addr;
  m_server.config.port = port;
  addWebCmds();
}

int WebServer::ErrorHandling() {
  m_server.on_error = [](shared_ptr<HttpServer::Request> /*request*/, const SimpleWeb::error_code & /*ec*/) {
    // Handle errors here
    // Note that connection timeouts will also call this handle with ec set to SimpleWeb::errc::operation_canceled
    // camera starts/closes will cause this one to be called, we will investigate it later
    //LogCRITICAL << "Something is wrong, client exit!";
  };

  return 0;
}

void WebServer::sendImage(const shared_ptr<HttpServer::Response> &response, const cv::Mat &frame) {
  if(frame.empty()) {
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

bool WebServer::updateDatabaseReport(const string &product, const string &lot) {
  if(m_iTotalBoards == 0 || product.empty() || lot.empty()) {
    LogERROR << "invalid product or lot number";
    return false;
  }

  for(int i = 0; i < m_iTotalBoards; i++) {
    if(!RunningInfo::instance().GetRunningData().saveToDatabase(product, i, lot)) {
      LogERROR << "save report of board[" << i << "] failed...";
      return false;
    }
  }

  return true;
}

int WebServer::addWebCmds() {
  LogINFO << "Add web server commands...";
  ErrorHandling();
  PostString();
  PostJson();

  GetInfo();
  GetMatchNumber();
  GetWork();
  GetDefault();

  // do_Get commands
  GetUISetting();
  GetListCameras();
  GetCamera();
  StartCamera();
  StopCamera();
  GetCameraImage();
  GetCameraReadFrame();
  GetListUsers();
  GetRole();
  ReadSetting();
  GetDebugSettingImage();
  GetAlarms();
  GetAlarmEventTotal();
  GetHistoryTotal();
  GetProdList();
  GetReport();
  GetAlarmTop();
  GetRunStatus();
  GetAutoStatus();
  GetCameraDisplayMode();
  GetCurrentProd();
  GetCurrentLotNumber();
  GetRunningData();
  GetHistoryFiles();
  GetDefectStats();
  GetCameraParameters();

  // Post Command
  Login();
  ChangePwd();
  IoWrite();
  SettingWrite();
  SetAutoManual();
  SetCameraDisplayMode();
  StartRun();
  ResetCalculation();
  AlarmAck();
  AlarmAckAll();
  AlarmDelete();
  AlarmDeleteAll();
  AlarmClear();
  SetCurrentProd();
  DeleteProd();
  DeleteProds();
  SetCurrentLot();
  SetCameraParameters();
  SetLightSourceValue();

  CreateTestProduction();
  TestSettingWrite();
  TriggerCameraImages();
  SetTestCameraIndex();
  ResetTestProduction();

  AddUser();
  DeleteUser();
  DeleteUsers();
  UpdateUserInfo();

  return 0;
}

int WebServer::PostString() {
  // Add resources using path-regex and method-string, and an anonymous function
  // POST-example for the path /string, responds the posted string
  m_server.resource["^/string$"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto content = request->content.string();
    *response << "HTTP/1.1 200 OK\r\nContent-Length: " << content.length() << "\r\n\r\n"
              << content;
  };

  return 0;
}

int WebServer::PostJson() {
  // POST-example for the path /json, responds firstName+" "+lastName from the posted json
  // Responds with an appropriate error message if the posted json is not valid, or if firstName or lastName is missing
  // Example posted json:
  // {
  //   "firstName": "John",
  //   "lastName": "Smith",
  //   "age": 25
  // }
  m_server.resource["^/json$"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      auto name = pt.get<string>("firstName") + " " + pt.get<string>("lastName");

      *response << "HTTP/1.1 200 OK\r\n"
                << "Content-Length aaa: " << name.length() << "\r\n\r\n"
                << name;
    }
    catch(const exception &e) {
      LogERROR << e.what();
      *response << "HTTP/1.1 400 Bad Request\r\nContent-Length hhhh: " << strlen(e.what()) << "\r\n\r\n"
                << e.what();
    }
  };

  return 0;
}

int WebServer::GetInfo() {
  // GET-example for the path /info
  // Responds with request-information
  m_server.resource["^/info$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    // for reference only
    boost::property_tree::ptree root, arr, elem1, elem2;
    elem1.put<int>("key0", 0);
    elem1.put<bool>("key1", true);
    elem2.put<float>("key2", 2.2f);
    elem2.put<double>("key3", 3.3);
    arr.push_back(make_pair("", elem1));
    arr.push_back(make_pair("", elem2));
    root.put_child("path1.path2", arr);

    stringstream ss;
    write_json(ss, root);

    stringstream stream;
    stream << "<h1>Request from " << request->remote_endpoint_address() << ":" << request->remote_endpoint_port() << "</h1>";
    stream << request->method << " " << request->path << " HTTP/" << request->http_version;
    stream << "<h2>Test Query Fields</h2>";

    auto query_fields = request->parse_query_string();
    for(auto &field : query_fields)
      stream << field.first << ": " << field.second << "<br>";
    stream << "<h2>Header Fields</h2>";

    for(auto &field : request->header)
      stream << field.first << ": " << field.second << "<br>";

    response->write(stream);
  };

  return 0;
}

int WebServer::GetMatchNumber() {
  // GET-example for the path /match/[number], responds with the matched string in path (number)
  // For instance a request GET /match/123 will receive: 123
  m_server.resource["^/match/([0-9]+)$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    response->write(request->path_match[1]);
  };

  return 0;
}


int WebServer::GetWork() {
  // GET-example simulating heavy work in a separate thread
  m_server.resource["^/work$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> /*request*/) {
    thread work_thread([response] {
      this_thread::sleep_for(chrono::seconds(5));
      response->write("Work done");
    });
    work_thread.detach();
  };

  return 0;
}


int WebServer::GetDefault() {
  // Default GET-example. If no other matches, this anonymous function will be called.
  // Will respond with content in the web/-directory, and its subdirectories.
  // Default file: index.html
  // Can for instance be used to retrieve an HTML 5 client that uses REST-resources on this server
  m_server.default_resource["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    stringstream stream;
    stream << "<h1>Invalid Request from " << request->remote_endpoint_address() << ":" << request->remote_endpoint_port() << "</h1>";
    stream << request->method << " " << request->path << " HTTP/" << request->http_version;
    stream << "<h2>Request Query Fields</h2>";

    auto query_fields = request->parse_query_string();
    for(auto &field : query_fields)
      stream << field.first << ": " << field.second << "<br>";
    stream << "<h2>Request Header Fields</h2>";

    for(auto &field : request->header)
      stream << field.first << ": " << field.second << "<br>";
    response->write(SimpleWeb::StatusCode::client_error_bad_request, stream);
  };

  return 0;
}


// do_Get commands
int WebServer::GetUISetting() {
  // GET-example for the path /info
  // Responds with request-information
  m_server.resource["^/ui_setting"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    stringstream stream = CustomizedJsonConfig::instance().getJsonStream("UISetting");

    if(stream.str().empty()) {
      LogERROR << "Invalid UI configurations";
      response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Invalid UI Configurations");
    }
    else {
      response->write(stream);
    }
  };

  return 0;
}

int WebServer::GetListCameras() {
  /*
  GET: http://localhost:8080/get_cameras?board=1
  Return:
  {
	"cameras": [
		"CAM2",
		"CAM3",
		"CAM4",
		"CAM5"
	]
  }
  */
  m_server.resource["^/get_cameras"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
	auto query_fields = request->parse_query_string();
	try {
		string board = SimpleWeb::getValue(query_fields, "board");
		vector<int> cameras = RunningInfo::instance().GetRunningData().getCamerasFromBoard(stoi(board));
		if (cameras.empty()) {
			LogERROR << "Get camera list of board " << board << " failed";
			response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Get Camera List Failed");
		}
		else {
			boost::property_tree::ptree root;
			boost::property_tree::ptree cameraList;

			for (int idx = 0; idx < cameras.size(); idx++) {
				boost::property_tree::ptree cell;
				cell.put_value("CAM" + to_string(cameras[idx]));
				cameraList.push_back(make_pair("", cell));
			}
			root.put_child("cameras", cameraList);

			stringstream ss;
			write_json(ss, root);
			response->write(ss);
		}
	}
	catch (...) {
		LogERROR << "Bad Request";
		response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
	}
  };

  return 0;
}

int WebServer::GetCamera() {
  m_server.resource["^/cameras/([1-9])"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    string id = request->path_match[1];
    try {
      shared_ptr<BaseCamera> camera = m_pCameraManager->getCamera("CAM" + id);

      // todo: what to return to UI
      response->write("Success");
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}

int WebServer::StartCamera() {
  /* 
  GET: http://localhost:8080/cameras/0/start
  Return:
    Success or Fail
  */
  m_server.resource["^/cameras/([0-9])/start"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    string id = request->path_match[1];

    // Here is the map from UI id to cameraName
    // Might ask UI to directly send server cameraName
    // TODO....
    try {
      if(m_pCameraManager->startCamera("CAM" + id)) {
        LogINFO << "CAM" << id << " OK";
        response->write("Success");
      }
      else {
        LogERROR << "CAM" << id << " failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Start Camera Failed");
      }
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}

int WebServer::StopCamera() {
  /* 
  GET: http://localhost:8080/cameras/0/stop
  Return:
    Success
  */
  m_server.resource["^/cameras/([0-9])/stop"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    string id = request->path_match[1];
    try {
      // Here is the map from UI id to cameraName
      // Might ask UI to directly send server cameraName
      // TODO....
      m_pCameraManager->stopCamera("CAM" + id);

      response->write("Success");
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}

int WebServer::GetCameraImage() {
  /* 
  GET: http://localhost:8080/cameras/0/image
  Return:
    image in jpg format
  */
  m_server.resource["^/cameras/([0-9])/image"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    string id = request->path_match[1];
    try {
      if(m_pCameraManager->getCamera("CAM" + id) == nullptr) {

        LogERROR << "Invalid camera: CAM" << id;

#ifdef __linux__
        cv::Mat fakeFrame = cv::imread("/opt/seeking.jpg", cv::IMREAD_COLOR);
#elif _WIN64
        cv::Mat fakeFrame = cv::imread("C:\\opt\\seeking.jpg", cv::IMREAD_COLOR);
#endif
        sendImage(response, fakeFrame);
      }
      else {
        cv::Mat frame;
        ClassificationResult result;

        //LogINFO << "Received camera image request, camera Id: " << id;
        m_pCameraManager->getCamera("CAM" + id)->getCurrentProcessedFrameAndResult(frame, result);
        if(frame.empty()) {
          LogERROR << "Camera CAM" << id << " current processed image is invalid";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Camera Current Processed Image Invalid");
          db_utils::ALARM("CameraError");
        }
        else {
          int camMode = RunningInfo::instance().GetProductionInfo().GetCameraDisplayMode();
          if(camMode == 1) { // which means to display all images
            sendImage(response, frame);
          }
          else if(result != ClassifierResultConstant::Good && result != ClassifierResultConstant::Classifying) {
            sendImage(response, frame);
          }
          //LogINFO << "Send camera image, camera Id: " << id;
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

int WebServer::GetCameraReadFrame() {
  /*
  Notice: To get camera static image, need to start Camera first.
  GET: http://localhost:8080/cameras/0/read_frame
  Return:
    image in jpg format
  */
  m_server.resource["^/cameras/([0-9])/read_frame"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    string id = request->path_match[1];
    try {
      if(m_pCameraManager->getCamera("CAM" + id) == nullptr) {
        LogERROR << "Invalid camera CAM" << id;
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Camera");
        db_utils::ALARM("CameraError");
      }
      else {
        cv::Mat frame;
        if(!m_pCameraManager->getCamera("CAM" + id)->read(frame)) {
          LogERROR << "Camera CAM" << id << " read frame failed";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Camera Read Error");
        }
        else {
          sendImage(response, frame);
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

int WebServer::GetListUsers() {
  /* return all User Info
  GET: http://localhost:8080/usrs
  Return:
    { "0": { "name": "haihang",
	         "pwd": "xyz",
			 "role": "Engineer"
           }, 
      "1": { "name": "xiaoqiang",
	         "pwd": "abc",
			 "role": "Engineer"
		   }
    }
  */
  m_server.resource["^/usrs"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      vector<Database::UserData> vt;
      Database db;
      if(db.user_list(vt)) {
        LogERROR << "Get user list in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Get User List Failed");
      }
      else {
        if (vt.empty()) {
		  LogERROR << "Users not found";
		  response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Users Not Found");
		  db_utils::ALARM("UserNotFound");
		}
		else {
		  boost::property_tree::ptree root;
		  for (int i = 0; i < vt.size(); ++i) {
		    boost::property_tree::ptree cell;
		    cell.put<string>("name", vt[i].name);
            cell.put<string>("pwd", vt[i].pwd);
			cell.put<string>("role", vt[i].role);

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
int WebServer::GetRole() {
  /*
  GET: http://localhost:8080/role?user=haihang
  Return: { "role": "Engineer"}
  */
  m_server.resource["^/role"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string user = SimpleWeb::getValue(query_fields, "user");
      string role;
      Database db;
      if(db.user_role(role, user)) {
        LogERROR << "Get user role in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Get User Role Failed");
      }
      else {
        boost::property_tree::ptree root, cell;
        cell.put_value(role);
        root.push_back(make_pair("role", cell));

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

int WebServer::ReadSetting()
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
{
  m_server.resource["^/setting_read"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string prod = SimpleWeb::getValue(query_fields, "prod");

      ProductSetting::PRODUCT_SETTING setting;
      Database db;
      if(db.prod_setting_read(prod, setting)) {
        LogERROR << "Read product setting in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Product Setting Failed");
      }
      else {
        boost::property_tree::ptree root;
        boost::property_tree::ptree boolArr;

        for(int idx = 0; idx < setting.bool_settings.size(); idx++) {
		  boost::property_tree::ptree cell;
          cell.put_value(setting.bool_settings[idx] ? "true" : "false");
          boolArr.push_back(make_pair("", cell));
        }
        root.put_child("boolean_settings", boolArr);

        boost::property_tree::ptree floatArr;
        for(int idx = 0; idx < setting.float_settings.size(); idx++) {
		  boost::property_tree::ptree cell;
          cell.put_value(setting.float_settings[idx]);
          floatArr.push_back(make_pair("", cell));
        }
        root.put_child("float_settings", floatArr);

        boost::property_tree::ptree fileArr;
        for(int idx = 0; idx < setting.string_settings.size(); idx++) {
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
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
    }
  };
  return 0;
}

int WebServer::GetDebugSettingImage() {
  // we use this method for product setting testing purpose, which paired with setting_write with test = 1
  // this one will not really read product setting, but read product setting result, which will be the
  // image processed by new setting
  /* 
  GET: http://localhost:8080/cameras/0/debug_setting_image
  Return:
    image in jpg format
  */
  m_server.resource["^/cameras/([0-9])/debug_setting_image"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    string id = request->path_match[1];
    try {
      if(m_pCameraManager->getCamera("CAM" + id) == nullptr) {

        LogERROR << "Invalid Camera: CAM" << id;

#ifdef __linux__
        cv::Mat fakeFrame = cv::imread("/opt/seeking.jpg", cv::IMREAD_COLOR);
#elif _WIN64
        cv::Mat fakeFrame = cv::imread("C:\\opt\\seeking.jpg", cv::IMREAD_COLOR);
#endif
        sendImage(response, fakeFrame);
      }
      else {
        cv::Mat frame;
        ClassificationResult result;

        // get current processed frame and result without check camera status
        m_pCameraManager->getCamera("CAM" + id)->getCurrentProcessedFrameAndResult(frame, result, false);
        if(frame.empty()) {
          LogERROR << "Camera CAM" << id << " current processed image is invalid";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Camera Current Processed Image Invalid");
          db_utils::ALARM("CameraError");
        }
        else {
          LogINFO << "Camera CAM" << id << " current processed image has been sent sucessfully";
          sendImage(response, frame);
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

int WebServer::GetAlarms() {
  /*
  GET: http://localhost:8080/get_alarms?startIndex=0&endIndex=10&filter=All
  Return: {
            database records based on filter (All/Event/Alarm)
            in start_time desc order
          }
  exmaple:
  { "0": { "uuid": "2067545f443d49929f5825c8c7776cf8", 
           "code": "E002",
		   "type": "Event",
		   "msg": "Login",
		   "start_time": "2018-08-24 15:31:00",
		   "end_time": "NULL",
		   "ack_time": "NULL",
		   "user": "NULL",
		   "notes": "NULL" },
	"1": { "uuid": "4f2af7a229e14f2b9925abf12ef32c49",
	       "code": "E001",
		   "type": "Event",
		   "msg": "ServerStart",
		   "start_time": "2018-08-24 15:30:40",
		   "end_time": "NULL",
		   "ack_time": "NULL",
		   "user": "NULL",
		   "notes": "NULL" }
	}
  */
  m_server.resource["^/get_alarms"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string startIndex = SimpleWeb::getValue(query_fields, "startIndex");
      string endIndex = SimpleWeb::getValue(query_fields, "endIndex");
      string filter = SimpleWeb::getValue(query_fields, "filter");

      if (filter == "All" || filter == "Event" || filter == "Alarm") {
        vector<Database::AlarmData> vt;
        Database db;
        if(db.alarm_read(vt, stoi(startIndex), stoi(endIndex), filter)) {
          LogERROR << "Read alarm in database failed";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Alarm Failed");
        }
        else {
          boost::property_tree::ptree root;
          for (int i = 0; i < vt.size(); ++i) {
            boost::property_tree::ptree cell;
            cell.put<string>("uuid", vt[i].uuid);
            cell.put<string>("code", vt[i].code);
            cell.put<string>("type", vt[i].type);
            cell.put<string>("msg", vt[i].msg);
            cell.put<string>("start_time", vt[i].start_time);
            cell.put<string>("end_time", vt[i].end_time);
            cell.put<string>("ack_time", vt[i].ack_time);
            cell.put<string>("user", vt[i].user);
            cell.put<string>("notes", vt[i].notes);
            root.push_back(make_pair(to_string(i), cell));
          }

          stringstream ss;
          write_json(ss, root);
          response->write(ss);
        }
      }
      else {
        LogERROR << "Request with invalid filter";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Request With Invalid Filter");
      }
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}


int WebServer::GetAlarmEventTotal() {
  /*
  GET: http://localhost:8080/alarm_event_total?filter=All
  Return: {
            database total records based on filter (All/Event/Alarm)
          }
  example:
          { "total": "5408" }
  */
  m_server.resource["^/alarm_event_total"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string filter = SimpleWeb::getValue(query_fields, "filter");
      if (filter == "All" || filter == "Event" || filter == "Alarm") {
        int count;
        Database db;
        if (db.alarm_get_total(count, filter)) {
          LogERROR << "Get total alarm in database failed";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Get Total Alarm Failed");
        }
        else {
          boost::property_tree::ptree root;
          boost::property_tree::ptree cell;
          cell.put_value(count);
          root.push_back(make_pair("total", cell));

          stringstream ss;
          write_json(ss, root);
          response->write(ss);
        }
      }
      else {
        LogERROR << "Request with invalid filter";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Request With Invalid Filter");
      }
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}


int WebServer::GetHistoryTotal() {
  /*
  GET: http://localhost:8080/history_total?startTime=1533949427&endTime=1535411027&camid=1&defectid=2
  Return: {
            database total records based on filter (All/Event/Alarm)
          }
  example:
          { "total": "59" }
  */
  m_server.resource["^/history_total"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string startTimeStamp = SimpleWeb::getValue(query_fields, "startTime");
      string startTime = shared_utils::getTime(startTimeStamp);
      string endTimeStamp = SimpleWeb::getValue(query_fields, "endTime");
      string endTime = shared_utils::getTime(endTimeStamp);
      string camid = SimpleWeb::getValue(query_fields, "camid");
      string defectid = SimpleWeb::getValue(query_fields, "defectid");

      int result;
      Database db;
      if(db.history_total(result, startTime, endTime, stoi(camid), stoi(defectid))) {
        LogERROR << "Read total history in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Total History Failed");
      }
      else {
        boost::property_tree::ptree root;
        boost::property_tree::ptree cell;
        cell.put_value(result);
        root.push_back(make_pair("total", cell));

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

int WebServer::GetProdList() {
  /*  return Prod list
    GET: http://localhost:8080/get_prod_list
    Return: {
               prod list
            }
	example:
	        { "0": "prod2", 
			  "1": "xyz"
			} 
  */
  m_server.resource["^/get_prod_list"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      vector<string> vt;
      Database db;
      if(db.prod_list(vt)) {
        LogERROR << "Get product list in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Get Product List Failed");
      }
      else {
        boost::property_tree::ptree root;
        for(int i = 0; i < vt.size(); ++i) {
          boost::property_tree::ptree cell;
          cell.put_value(vt[i]);

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

int WebServer::GetReport() {
  /*
  GET: http://localhost:8080/get_report?startTime=1533949427&endTime=1534381427&prod=Product01&board=1  (timestamp)
  Return: {
             return all data between datetime range
          }
  example:
  { "0": { "time": "2018-08-11",
           "total": "0",
		   "total_defect": "0",
		   "Empty": "0",
		   "Leak": "0",
		   ...
		   "Double": "0" },
    "1": { "time": "2018-08-12",
	       "total": "0",
		   "total_defect": "0",
		   "Empty": "0",
		   "Leak": "0",
		   ...
		   "Double": "0" }
  } 
  */
  m_server.resource["^/get_report"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string startTimeStamp = SimpleWeb::getValue(query_fields, "startTime");
      string startTime = shared_utils::getTime(startTimeStamp);
      string endTimeStamp = SimpleWeb::getValue(query_fields, "endTime");
      string endTime = shared_utils::getTime(endTimeStamp);
      string prod = SimpleWeb::getValue(query_fields, "prod");
      string board = SimpleWeb::getValue(query_fields, "board");

      vector<Database::ReportData> vt;
      Database db;
      if(db.report_read(vt, startTime, endTime, prod, board)) {
        LogERROR << "Read report in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Read Report in Failed");
      }
      else {
        boost::property_tree::ptree root;
        for(int i = 0; i < vt.size(); ++i) {
          boost::property_tree::ptree cell;
          cell.put<string>("time", vt[i].hhmmss);
          cell.put<string>("total", vt[i].total);
          cell.put<string>("total_defect", vt[i].total_defect);
          for(const auto &itr : vt[i].defects) {
            cell.put<string>(itr.first, itr.second);
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

int WebServer::GetAlarmTop() {
  /*
    GET: http://localhost:8080/get_alarm_top?num=10&startTime=1533513600&endTime=1533600000  (timestamp)
    Return: {
              return top 10 alarm data between datetime range
            }
    example:
    { "0": { "code": "A002", "number": "1" }, 
      "1": { "code": "A001", "number": "2" } } 
  */
  m_server.resource["^/get_alarm_top"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string startTimeStamp = SimpleWeb::getValue(query_fields, "startTime");
      string startTime = shared_utils::getTime(startTimeStamp);
      string endTimeStamp = SimpleWeb::getValue(query_fields, "endTime");
      string endTime = shared_utils::getTime(endTimeStamp);
      string num = SimpleWeb::getValue(query_fields, "num");

      vector<Database::AlarmTopStat> vt;
      Database db;
      if(db.alarm_get_top(vt, stoi(num), startTime, endTime)) {
        LogERROR << "Get top alarm in datbase failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Get Top Alarm Failed");
      }
      else {
        boost::property_tree::ptree root;
        for(int i = 0; i < vt.size(); ++i) {
          boost::property_tree::ptree cell;
          cell.put<string>("code", vt[i].code);
          cell.put<string>("number", vt[i].number);

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

int WebServer::GetRunStatus() {
  /*
  GET: http://localhost:8080/get_run_status
  Return: {
            return current auto status: Run: 1 , Stop:0
          }
  example: { "run": "0" } 
  */
  m_server.resource["^/get_run_status"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      int runStatus = RunningInfo::instance().GetProductionInfo().GetRunStatus();

      boost::property_tree::ptree root, cell;
      cell.put_value(runStatus);
      root.push_back(make_pair("run", cell));

      stringstream ss;
      write_json(ss, root);
      response->write(ss);
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}

int WebServer::GetAutoStatus() {
  /*
  GET: http://localhost:8080/get_auto_status
  Return: {
            return current auto status: Auto: 1 , Manual:0
          }
  example: { "auto": "1" } 
  */
  m_server.resource["^/get_auto_status"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      int autoStatus = RunningInfo::instance().GetProductionInfo().GetAutoStatus();

      boost::property_tree::ptree root, cell;
      cell.put_value(autoStatus);
      root.push_back(make_pair("auto", cell));

      stringstream ss;
      write_json(ss, root);
      response->write(ss);
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}

int WebServer::GetCameraDisplayMode() {
  /*
  GET: http://localhost:8080/get_camera_display_mode
  Return: {
            return current camera display mode: OK: 1  , NG:0
          }
  example: { "mode": "1" } 
  */
  m_server.resource["^/get_camera_display_mode"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      int mode = RunningInfo::instance().GetProductionInfo().GetCameraDisplayMode();

      boost::property_tree::ptree root, cell;
      cell.put_value(mode);
      root.push_back(make_pair("mode", cell));

      stringstream ss;
      write_json(ss, root);
      response->write(ss);
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}

int WebServer::GetCurrentProd() {
  /*
  GET: http://localhost:8080/get_current_prod
  Return: {
            return current prod
          }
  exmaple: { "prod": "zhuanggu" } 
  */
  m_server.resource["^/get_current_prod"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      string prod = RunningInfo::instance().GetProductionInfo().GetCurrentProd();

      boost::property_tree::ptree root, cell;
      cell.put_value(prod);
      root.push_back(make_pair("prod", cell));

      stringstream ss;
      write_json(ss, root);
      response->write(ss);
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}

int WebServer::GetCurrentLotNumber() {
  /*
  GET: http://localhost:8080/get_current_lot
  Return: {
            return current lot number
          }
  example: { "lot": "12345" } 
  */
  m_server.resource["^/get_current_lot"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      string lot = RunningInfo::instance().GetProductionInfo().GetCurrentLot();

      boost::property_tree::ptree root, cell;
      cell.put_value(lot);
      root.push_back(make_pair("lot", cell));

      stringstream ss;
      write_json(ss, root);
      response->write(ss);
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}

int WebServer::GetRunningData() {
  /*
  GET: http://localhost:8080/get_running_data
  Return: {
            return running data
          }
  example:
     { "alarm": "0","
	   "...": "0",				// here it should the real defect name, such as "FlyInk": "0"
	   "totalDefect": "0",
	   "totalNumber": "0" } 
  */
  m_server.resource["^/get_running_data"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      map<string, int> result;
      RunningInfo::instance().GetRunningData().GetRunningData(result);

      // check defect rate, default is every 24 hours, and timer is in seconds
      if(m_timer.Elapsed().count() >= m_iTimerIntervaInSeconds && RunningInfo::instance().GetRunningData().exceedDefectRatio(m_iTotalBoards, m_fMaxDefectRatio)) {
        db_utils::ALARM("HighDefectRatio");
        m_timer.Reset();
      }

      boost::property_tree::ptree root;
      map<string, int>::iterator it;
      for(it = result.begin(); it != result.end(); ++it) {
        boost::property_tree::ptree cell;
        cell.put_value(it->second);
        root.push_back(make_pair(it->first, cell));
      }

      map<string, string> msgMap;
      RunningInfo::instance().GetRunningData().GetRunningMsg(msgMap);
      map<string, string>::iterator ssMapIt;
      for(ssMapIt = msgMap.begin(); ssMapIt != msgMap.end(); ++ssMapIt) {
        boost::property_tree::ptree cell;
        cell.put_value(ssMapIt->second);
        root.push_back(make_pair(ssMapIt->first, cell));
      }

      stringstream ss;
      write_json(ss, root);
      response->write(ss);
    }
    catch(...) {
      LogERROR << "Bad Request";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
  };

  return 0;
}

int WebServer::GetHistoryFiles() {
  /**
  GET: http://localhost:8080/get_history_files?startTime=1533949427&endTime=1535411027&startIndex=0&endIndex=10&camid=1&defectid=2  (timestamp)
  Return: {
            return all files between datetime range
  }
  example:
  { "0": "20180821140406.128714CAM0D01.png",
    "1": "20180821140406.205510CAM0D01.png",
	"2": "20180821140406.281307CAM0D01.png",
	"3": "20180821140406.342144CAM0D01.png",
  } 
  */
  m_server.resource["^/get_history_files"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string startTimeStamp = SimpleWeb::getValue(query_fields, "startTime");
      string startTime = shared_utils::getTime(startTimeStamp);
      string endTimeStamp = SimpleWeb::getValue(query_fields, "endTime");
      string endTime = shared_utils::getTime(endTimeStamp);
      string startIndex = SimpleWeb::getValue(query_fields, "startIndex");
      string endIndex = SimpleWeb::getValue(query_fields, "endIndex");
      string camid = SimpleWeb::getValue(query_fields, "camid");
      string defectid = SimpleWeb::getValue(query_fields, "defectid");

      vector<string> result;
      Database db;
      if(db.history_read(result, startTime, endTime, stoi(camid), stoi(defectid), stoi(startIndex), stoi(endIndex))) {
        LogERROR << "Get history files in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Get History Files Failed");
      }
      else {
        boost::property_tree::ptree root;
        for(int i = 0; i < result.size(); ++i) {
          boost::property_tree::ptree cell;
          cell.put_value(result[i]);

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

int WebServer::GetDefectStats() {
  /*
  GET: http://localhost:8080/get_defect_stats
  Return: {
      return 3D array to carry target classification results of each board
  }
  example:
  { 
    "DefectStats" : [ [ [1,2,1,1], [1,1,1,1] ], [ [1,1,2,1], [1,4,1,1] ], [ [1,1,1,1], [3,2,1,1] ] ]
  } 
  */
  m_server.resource["^/get_defect_stats"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      map<int, vector<vector<ClassificationResult>>> results = RunningInfo::instance().GetRunningData().getAllClassificationResults();
      boost::property_tree::ptree root;
      if(results.empty()) {
        LogINFO << "Defect stats is empty: { \"DefectStats\": [] }";
        response->write("{ \"DefectStats\": [] }");
      }
      else {
        boost::property_tree::ptree cellBoard;
        for(const auto &itr : results) {
          boost::property_tree::ptree cellView;
          for(const auto &outer : itr.second) {
            boost::property_tree::ptree cellTarget;
            for(const auto &inner : outer) {
              boost::property_tree::ptree cell;
              cell.put_value(inner);
              cellTarget.push_back(make_pair("", cell));
            }
            cellView.push_back(make_pair("", cellTarget));
          }
          cellBoard.push_back(make_pair("", cellView));
        }

        root.add_child("DefectStats", cellBoard);

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

int WebServer::GetCameraParameters() {
  /*
  GET: http://localhost:8080/get_cameras_parameters
  Return: {
      return cameras parameters with camera index, and setting in json blob
  }
  example:
  { 
      "CAM0" : {
			"width": "685",
		    "height": "492",
			"fps": "40",
			"x": "0",
			"y": "0",
			"exposure": "500",
			"trigger_mode": "Off",
			"packet_size": "8192",
			"interpacket_delay": "20000",
			"receive_thread_priority": "50",
			"debounce_time": "10"
	  },
	  "CAM1" : {
	        ...
	  },
	  "CAMn" : {
			...
	  }
  } 
  */
  m_server.resource["^/get_cameras_parameters"]["GET"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      LogINFO << "Get parameters of all " << m_pCameraManager->camerasSize() << " cameras...";
      if(m_pCameraManager->camerasSize() == 0) {
        LogERROR << "There is no cameras associated";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "No Cameras");
      }
      else {
        boost::property_tree::ptree root;
        bool isValid = true;
        for(int id = 0; id < m_pCameraManager->camerasSize(); id++) {
          if(m_pCameraManager->getCamera("CAM" + to_string(id)) == nullptr) {
            LogERROR << "Invalid camera CAM" << id;
            response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Invalid Camera");
            isValid = false;
            break;
          }

          shared_ptr<CameraConfig> config = m_pCameraManager->getCamera("CAM" + to_string(id))->config();
          stringstream parameters = config->getConfigParameters();
          if(parameters.str().empty()) {
            LogERROR << "Invalid parameters in CAM" << id;
            response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Invalid Camera Parameters");
            isValid = false;
            break;
          }

          boost::property_tree::ptree cell;
          read_json(parameters, cell);
          root.add_child("CAM" + to_string(id), cell);
        }

        if(isValid) {
          stringstream ss;
          write_json(ss, root);
          LogINFO << "Get all cameras parameters: \n" << ss.str();
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

// Post Command
int WebServer::Login() {
  /*
  POST: http://localhost:8080/login
  Json: { “user”: “haihang”,
		  “pwd”: “xyz”,
		  "expireInDays": 60
		}
  Return user role: { "role": "Engineer", "expired": false }
  Return user role: "Engineer" if expireInDays = -1 or not provided
  */
  m_server.resource["^/login"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      string name = pt.get<string>("user");
      string pwd = pt.get<string>("pwd");

      int pwdExpiredDays = -1;
      if(pt.get_child_optional("expireInDays")) {
        pwdExpiredDays = pt.get<int>("expireInDays");
      }

      LogINFO << "login: " << name << ", " << pwd << ", " << pwdExpiredDays;

      if(name.size() == 0) {
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Command");
        return 1;
      }

      // use lambda function to release database resource before dbutils::ALARM, which open database again
      vector<Database::UserData> vt;
      auto userList = [](vector<Database::UserData> &usrs) { Database db; return db.user_list(usrs); };
      if(userList(vt)) {
        LogERROR << "Get user list in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Get User List Failed");
        return 1;
      }

      for(const auto &itr : vt) {
        if(itr.name == name) {
          if(itr.pwd == pwd) {
            if(pwdExpiredDays < 0 || itr.pwd_time.empty()) {
              response->write(itr.role);
            }
            else {
              // new feature to check password expired or not
              string now = shared_utils::getTime(0);
              int days = shared_utils::getTimeDiff(itr.pwd_time, now) / 24;

              LogINFO << "total days since last change: " << days;

              boost::property_tree::ptree root;
              boost::property_tree::ptree cell;
              cell.put_value(itr.role);
              root.push_back(make_pair("role", cell));
              cell.put_value(days >= pwdExpiredDays ? "true" : "false");
              root.push_back(make_pair("expired", cell));

              stringstream ss;
              write_json(ss, root);
              response->write(ss);
            }

            // login successfully, set current user
            RunningInfo::instance().SetUser(name);
            db_utils::ALARM("Login");
            return 0;
          }
          else {
            response->write(SimpleWeb::StatusCode::client_error_bad_request, "incorrect-password");
            return 1;
          }
        }
      }

      response->write(SimpleWeb::StatusCode::client_error_bad_request, "incorrect-user-name");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::ChangePwd() {
  /* change login password
   POST: http://localhost:8080/change_password
   JSON: {"user":"haihang",
          "pwd":"xyz",
          "new_pwd":"1234"}
  */
  m_server.resource["^/change_password"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      string name = pt.get<string>("user");
      string pwd = pt.get<string>("pwd");
      string new_pwd = pt.get<string>("new_pwd");

      if(name.size() == 0) {
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Command");
        return 1;
      }

      // use lambda function to release database resource before dbutils::ALARM, which open database again
      vector<Database::UserData> vt;
      auto userList = [](vector<Database::UserData> &usrs) { Database db; return db.user_list(usrs); };
      auto changePwd = [](const string &usr, const string &new_pwd) { Database db; return db.change_pwd(usr, new_pwd); };
      if(userList(vt)) {
        LogERROR << "Get user list in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Get User List Failed");
        return 1;
      }

      for(int i = 0; i < vt.size(); ++i) {
        if(vt[i].name == name) {
          if(vt[i].pwd == pwd) {
            if(changePwd(name, new_pwd)) {
              LogERROR << "Change password in database failed";
              response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Change Password Failed");
              return 1;
            }
            response->write("Success");
            db_utils::ALARM("ChangePassword");
            return 0;
          }
          else {
            response->write(SimpleWeb::StatusCode::client_error_bad_request, "incorrect-password");
            return 1;
          }
        }
      }

      response->write(SimpleWeb::StatusCode::client_error_bad_request, "incorrect-user-name");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::AddUser()
{
  /* add user
   POST: http://localhost:8080/add_usr
   JSON: {"user":"haihang",
          "pwd":"xyz",
          "role":"engineer"}
  */
  m_server.resource["^/add_usr"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      string name = pt.get<string>("user");
      string pwd = pt.get<string>("pwd");
      string role = pt.get<string>("role");

      if(name.size() == 0) {
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Command");
        return 1;
      }

      // use lambda function to release database resource before dbutils::ALARM, which open database again
      auto addUser = [](const string &usr, const string &pwd, const string &role) { Database db; return db.add_user(usr, pwd, role); };
      if(addUser(name, pwd, role)) {
        LogERROR << "Add user in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Add User Failed");
        return 1;
      }

      response->write("Success");
      db_utils::ALARM("AddUser");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::DeleteUser()
{
  /* delete user
   POST: http://localhost:8080/delete_usr?user=name
  */
  m_server.resource["^/delete_usr"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string name = SimpleWeb::getValue(query_fields, "user");

      if(name.size() == 0) {
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "InValid Command");
        return 1;
      }

      // use lambda function to release database resource before dbutils::ALARM, which open database again
      auto deleteUser = [](const string &usr) { Database db; return db.delete_user(usr); };
      if(deleteUser(name)) {
        LogERROR << "Delete user in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Delete User Failed");
        return 1;
      }

      response->write("Success");
      db_utils::ALARM("DeleteUser");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::DeleteUsers() {
  /* delete users
   we will combine this one with delete_usr?user=name later when UI no longer use the single delete
   POST: http://localhost:8080/delete_usrs
   JSON: {"users":["haihang","test","usr1"]}
  */
  m_server.resource["^/delete_usrs"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      auto deleteUser = [](const string &usr) { Database db; return db.delete_user(usr); };
      for(auto &child : pt.get_child("users")) {
        auto first = child.first;
        auto user = child.second.get_value<string>();
        if(deleteUser(user)) {
          LogWARNING << "Delete " << user << " in database failed";
          continue;
        }
      }

      response->write("Success");
      db_utils::ALARM("DeleteUsers");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::UpdateUserInfo()
{
  /* update user info
   POST: http://localhost:8080/update_usr_info
   JSON: {"user":"haihang",
          "pwd":"xyz",
          "role":"engineer"}
  */
  m_server.resource["^/update_usr_info"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      string name = pt.get<string>("user");
      string pwd = pt.get<string>("pwd");
      string role = pt.get<string>("role");

      if(name.size() == 0) {
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Command");
        return 1;
      }

      // use lambda function to release database resource before dbutils::ALARM, which open database again
      auto updateUser = [](const string &usr, const string &pwd, const string &role) { Database db; return db.update_user_info(usr, pwd, role); };
      if(updateUser(name, pwd, role)) {
        LogERROR << "Update user info in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Update User Info Failed");
        return 1;
      }

      response->write("Success");
      db_utils::ALARM("UpdateUserInfo");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::IoWrite() {
  /*  IO Control
  POST: http://localhost:8080/io_write
  example:
   { 
     "id": 2,
     "parameter": 3456,
     "data": 12
   }
  Return: success
*/
  m_server.resource["^/io_write"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      int id = pt.get<int>("id");
      int parameter = pt.get<int>("parameter");
      short data = pt.get<short>("data");

      // rewrite this one to allow we write value using io manager
      if(m_pIoManager) {
        m_pIoManager->write(id, parameter, data);
        response->write("Success");
      }
      else {
        LogERROR << "Invalid io manager, not defined";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Invalid IO Manager");
		return 1;
      }
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
	return 0;
  };
  return 0;
}

int WebServer::SettingWrite() {
  /*
   POST: http://localhost:8080/setting_write?prod=Product03
   example:
   {
    "boolean_settings": [
        "false",
        "false",
        "true",
        "true",
        "true"
    ],
    "float_settings": [
        "0",
        "500",
        "99",
        "0.439999998",
        "0.5",
        "0.579999983",
        "30",
        "20"
    ],
    "string_settings": [
        "D:\\opt\\seeking.jpg"
    ]
   }
   Return: Success
 */
  m_server.resource["^/setting_write"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string prod = SimpleWeb::getValue(query_fields, "prod");
      ptree pt;
      read_json(request->content, pt);

      ProductSetting::PRODUCT_SETTING setting;
      setting.loadFromJson(pt);

      // use lambda function to release database resource before UpdateCurrentProductSetting, which open database again
      auto writeProdSetting = [](const string &prod, const ProductSetting::PRODUCT_SETTING &setting) { Database db; return db.prod_setting_write(prod, setting); };
      if(writeProdSetting(prod, setting)) {
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
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
      return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::SetAutoManual() {
  /*
   POST: http://localhost:8080/auto_manual
   example:   {"auto":1}
  */
  m_server.resource["^/auto_manual"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      int value = pt.get<int>("auto");
      RunningInfo::instance().GetProductionInfo().SetAutoStatus(value);
      response->write("Success");
      value ? db_utils::ALARM("AutoMode") : db_utils::ALARM("ManualMode");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
	return 0;
  };
  return 0;
}

int WebServer::SetCameraDisplayMode() {
  /*
   POST: http://localhost:8080/set_camera_display_mode
   example:   {"mode":1}
  */
  m_server.resource["^/set_camera_display_mode"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      int value = pt.get<int>("mode");

      RunningInfo::instance().GetProductionInfo().SetCameraDisplayMode(value);

      response->write("Success");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
	return 0;
  };
  return 0;
}

int WebServer::StartRun() {
  /* start run
   POST: http://localhost:8080/start_run
   example:  {"run":0}
  */
  m_server.resource["^/start_run"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      int value = pt.get<int>("run");

      // reset test and trigger flag to false even run is 0, because we leave setting test page at this point
      RunningInfo::instance().GetTestProductionInfo().reset();
      RunningInfo::instance().GetTestProductSetting().reset();
      if(value == 0) {
        if(RunningInfo::instance().GetProductionInfo().SetRunStatus(0)) {
          this_thread::sleep_for(chrono::milliseconds(10));
          LogINFO << "Stop";
          m_pCameraManager->stopCameras();
          response->write("Success");
          db_utils::ALARM("StopRun");

          string product = RunningInfo::instance().GetProductionInfo().GetCurrentProd();
          string lot = RunningInfo::instance().GetProductionInfo().GetCurrentLot();
          LogINFO << "Writing to report db when stop run: " << product;
          updateDatabaseReport(product, lot);
        }
        else {
          LogERROR << "Stop run failed";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Stop Run Failed");
		  return 1;
        }
      }
      else {
        LogINFO << "Start";

        RunningInfo::instance().GetRunningData().clearAlarm();
        if(m_pCameraManager->startCameras()) {
          if(RunningInfo::instance().GetProductionInfo().SetRunStatus(1)) {
            LogINFO << "Start run OK";
            response->write("Success");
            db_utils::ALARM("StartRun");
          }
          else {
            // Note: ??? here we might need to turn off cameras to keep database and status consistent
            LogERROR << "Start run fail";
            response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Start Run Failed");
			return 1;
          }
        }
        else {
          LogINFO << "Start cameras failed";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Start Cameras Failed");
		  return 1;
        }
      }
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
	return 0;
  };
  return 0;
}

int WebServer::ResetCalculation() {
  /*
  POST: http://localhost:8080/reset_calculation
  JSON: {"reset": 1}
  */
  m_server.resource["^/reset_calculation"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);
      int value = pt.get<int>("reset");
      if(value != 1) {
        LogERROR << "Invalid command";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Command");
		return 1;
      }
      else {
        RunningInfo::instance().GetRunningData().ClearDisplayData();
        response->write("Success");
      }
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
	return 0;
  };
  return 0;
}

int WebServer::AlarmAck() {
  /* 
   POST: http://localhost:8080/alarm_ack
   {
    "uuid": "890f6f4162e24632a3c4e3a2a2db60e7",
    "user": "haihang",
    "notes": "test"
	}
  */
  m_server.resource["^/alarm_ack"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      string uuid = pt.get<string>("uuid");
      string user = pt.get<string>("user");
      string notes = pt.get<string>("notes");

      Database db;
      if(db.alarm_ack(uuid, user, "ack", notes) == 0) {
        response->write("Success");
      }
      else {
        LogERROR << "Ack alarm in database failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Ack Alarm Failed");
		return 1;
      }
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
	return 0;
  };
  return 0;
}

int WebServer::AlarmAckAll() {
  /*
  POST: http://localhost:8080/alarm_ack_all
  JSON: {
    "user":"haihang",
    "uuids":["4567","2224567"]
  }
  */
  m_server.resource["^/alarm_ack_all"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      Database db;
      string user = pt.get<string>("user");
      for(auto &child : pt.get_child("uuids")) {
        auto first = child.first;
        auto uuid = child.second.get_value<string>();
        if(db.alarm_ack(uuid, user, "ack", "bulk")) {
          LogERROR << "Ack all alarms in database failed";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Ack ALL Alarms Failed");
          return 1;
        }
      }
      response->write("Success");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::AlarmDelete() {
  /*
  POST: http://localhost:8080/alarm_delete
  {
    "uuid": "890f6f4162e24632a3c4e3a2a2db60e7",
    "user": "haihang",
    "notes": "test"
  }
  */
  m_server.resource["^/alarm_delete"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
	try {
	  ptree pt;
	  read_json(request->content, pt);

	  string uuid = pt.get<string>("uuid");
	  string user = pt.get<string>("user");
	  string notes = pt.get<string>("notes");

	  Database db;
	  if (db.alarm_ack(uuid, user, "delete", notes) == 0) {
		response->write("Success");
	  }
	  else {
		LogERROR << "Delete alarm in database failed";
		response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Delete Alarm Failed");
		return 1;
	  }
	}
	catch (const exception &e) {
	  LogERROR << e.what();
	  response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
	}
	return 0;
  };
  return 0;
}

int WebServer::AlarmDeleteAll() {
  /*
   POST: http://localhost:8080/alarm_delete_all
   {"user":"haihang",
    "uuids":["4567","2224567"]}
  */
  m_server.resource["^/alarm_delete_all"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      Database db;
      string user = pt.get<string>("user");
      for(auto &child : pt.get_child("uuids")) {
        auto first = child.first;
        auto uuid = child.second.get_value<string>();
        if(db.alarm_ack(uuid, user, "delete", "bulk")) {
          LogERROR << "Delete all alarms in database failed";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Delete All Alarms Failed");
          return 1;
        }
      }
      response->write("Success");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::AlarmClear() {
  /*
   this is a method to clear alarm in running data only
   POST: http://localhost:8080/alarm_clear
  */
  m_server.resource["^/alarm_clear"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> /*request*/) {
    try {
      LogINFO << "clear alarm in running data only";
      RunningInfo::instance().GetRunningData().clearAlarm();
      response->write("Success");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
	return 0;
  };
  return 0;
}

int WebServer::SetCurrentProd() {
  /*
   POST: http://localhost:8080/set_current_prod
   {"prod":"Product04"}
  */
  m_server.resource["^/set_current_prod"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);
      string value = pt.get<string>("prod");

      LogINFO << "Set product: " << value;

      if(RunningInfo::instance().GetProductionInfo().SetCurrentProd(value)) {
        // reset product setting in memory by new value
        if(RunningInfo::instance().GetProductSetting().UpdateCurrentProductSettingFromDB(value)) {
          response->write("Success");
          db_utils::ALARM("SetProduct");
        }
        else {
          LogERROR << "Update current product setting failed";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Update Current Product Setting Failed");
		  return 1;
        }
      }
      else {
        LogERROR << "Set current product " << value << " failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Set Current Product Failed");
		return 1;
      }
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
	return 0;
  };
  return 0;
}

int WebServer::DeleteProd() {
  /*
   POST: http://localhost:8080/delete_prod
   {"prod":"Product04"}
  */
  m_server.resource["^/delete_prod"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);
      string value = pt.get<string>("prod");

      LogINFO << "Delete product: " << value;
      if(RunningInfo::instance().GetProductionInfo().GetCurrentProd() == value) {
        LogERROR << "Can not delete current product";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Delete Current Product Not Allowed");
        return 1;
      }

      Database db;
      if(db.production_info_delete_by_prod(value)) {
        LogERROR << "Delete product info failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Delete Product Info Failed");
        return 1;
      }

      // delete the same prodcut in product setting and defect report
      db.prod_setting_delete(value);
      db.report_delete_by_prod(value);

      response->write("Success");
      db_utils::ALARM("DeleteProduct");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::DeleteProds() {
  /*
   we will combine this request and delete_prod later when UI no longer use the single delete
   POST: http://localhost:8080/delete_prods
   {"prods":["Product04","Test"]}
  */
  m_server.resource["^/delete_prods"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
	try {
	  ptree pt;
	  read_json(request->content, pt);

	  for (auto &child : pt.get_child("prods")) {
		auto first = child.first;
		auto prod = child.second.get_value<string>();

		LogINFO << "Delete product: " << prod;
		if (RunningInfo::instance().GetProductionInfo().GetCurrentProd() == prod) {
		  LogERROR << "Can not delete current product";
		  response->write(SimpleWeb::StatusCode::client_error_bad_request, "Delete Current Product Not Allowed");
		  return 1;
		}

		Database db;
		if (db.production_info_delete_by_prod(prod)) {
		  LogERROR << "Delete product info failed";
		  response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Delete Product Info Failed");
		  return 1;
		}

		// delete the same prodcut in product setting and defect report
		db.prod_setting_delete(prod);
		db.report_delete_by_prod(prod);
	  }

	  response->write("Success");
	  db_utils::ALARM("DeleteProducts");
	}
	catch (const exception &e) {
	  LogERROR << e.what();
	  response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
	}
	return 0;
  };
  return 0;
}

int WebServer::SetCurrentLot() {
  /*
   POST: http://localhost:8080/set_current_lot
   {"lot":"L123456"}
  */
  m_server.resource["^/set_current_lot"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);
      string value = pt.get<string>("lot");

      string product = RunningInfo::instance().GetProductionInfo().GetCurrentProd();
      string previousLot = RunningInfo::instance().GetProductionInfo().GetCurrentLot();

      if(RunningInfo::instance().GetProductionInfo().SetCurrentLot(value)) {
        response->write("Success");
        db_utils::ALARM("SetLotNumber");
      }
      else {
        LogERROR << "Set current lot number failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Set Current Lot Number Failed");
		return 1;
      }

      if(value != previousLot) {
        // we need to update database for previous lot
        LogINFO << "Writing to report db when lot is changed: " << product;
        updateDatabaseReport(product, previousLot);
      }
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
	return 0;
  };
  return 0;
}

int WebServer::SetCameraParameters() {
  /*
   POST: http://localhost:8080/cameras/0/set_parameters/
   {
     "CAM0": {
		"width": "685",
		"height": "492",
		"fps": "40",
		"x": "0",
		"y": "0",
		"exposure": "500",
		"trigger_mode": "On",
		"packet_size": "8192",
		"interpacket_delay": "20000",
		"receive_thread_priority": "50",
		"debounce_time": "10"
	  }
   }
  */
  m_server.resource["^/cameras/([0-9])/set_parameters"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    string id = request->path_match[1];
    LogINFO << "Set parameters to camera CAM" << id;
    try {
      ptree pt;
      read_json(request->content, pt);

      ptree parameters = pt.get_child("CAM" + id);
      if(parameters.empty()) {
        LogERROR << "Invalid parameters in CAM" << id;
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Camera Parameters");
        return 1;
      }

      if(m_pCameraManager->getCamera("CAM" + id) == nullptr) {
        LogERROR << "Invalid camera CAM" << id;
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Camera");
        return 1;
      }

      // check if camera is started, otherwise, start camera, when UI leave the page which trigger this request, close the camera
      if(!m_pCameraManager->getCamera("CAM" + id)->cameraStarted() && !m_pCameraManager->startCamera("CAM" + id)) {
        LogERROR << "Start camera CAM" << id << " failed";
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Start Camera Failed");
        return 1;
      }

      stringstream ss;
      write_json(ss, parameters);
      LogINFO << ss.str();

      shared_ptr<CameraConfig> config = m_pCameraManager->getCamera("CAM" + id)->config();
      config->setConfigParameters(ss);
      response->write("Success");
      db_utils::ALARM("ModifyParameters");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
	  return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::SetLightSourceValue() {
  /*
   POST: http://localhost:8080/set_light_source_value
   { 
     "1": "1234"
   }
  */
  m_server.resource["^/set_light_source_value"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);
      if(pt.empty()) {
        LogERROR << "Invalid content in light source setting";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Content");
        return 1;
      }

      if(m_pLightSourceManager == nullptr) {
        LogDEBUG << "Ignore due to light source io manager unavailable";
        response->write("Success");
        db_utils::ALARM("ModifyParameters");
        return 0;
      }

      auto isNumber = [](const std::string &s) { return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit); };
      for(auto &itr : pt) {
        if(!isNumber(itr.first)) {
          LogERROR << "Invalid format, light source channel is not integer";
          response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Setting Format");
          return 1;
        }

        int channel = stoi(itr.first);
        int value = pt.get<int>(itr.first);
        if(!m_pLightSourceManager->write(channel, value)) {
          LogERROR << "Set light source value failed";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Set Light Source Value Failed");
          return 1;
        }
      }

      response->write("Success");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
      return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::CreateTestProduction() {
  /*
   POST: http://localhost:8080/create_test_prod?prod=Product03
   example:
   {
    "boolean_settings": [
        "false",
        "false",
        "true",
        "true",
        "true"
    ],
    "float_settings": [
        "0",
        "500",
        "99",
        "0.439999998",
        "0.5",
        "0.579999983",
        "30",
        "20"
    ],
    "string_settings": [
        "D:\\opt\\seeking.jpg"
    ],
	"camera_index": 1,
	"image_name": "file_path",
	"process_phase": 1
   }
   Return: Success
   three settings could be empty, which means it is a new test production without any existing setting
   process phase: 0 no porcess at all, 1 pre-process only, 2 pre-process and classification
 */
  m_server.resource["^/create_test_prod"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      string prod = SimpleWeb::getValue(query_fields, "prod");
      ptree pt;
      read_json(request->content, pt);

      // set current test product info and reset all test related flags to false
      RunningInfo::instance().GetTestProductionInfo().SetCurrentProd(prod, false);
      RunningInfo::instance().GetTestProductionInfo().SetTestStatus(0);
      RunningInfo::instance().GetTestProductionInfo().SetTriggerCameraImageStatus(0);

      // load product setting, and other test related values
      ProductSetting::PRODUCT_SETTING setting;
      if(!pt.empty()) {
        setting.loadFromJson(pt);
        RunningInfo::instance().GetTestProductSetting().loadFromJson(pt);
      }
      RunningInfo::instance().GetTestProductSetting().UpdateCurrentProductSetting(setting);

      LogINFO << "Create test product " << prod << " and setting successfully";
      response->write("Success");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
      return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::TestSettingWrite() {
  /*
   POST: http://localhost:8080/test_setting_write?prod=Product03
   example:
   {
    "boolean_settings": [
        "false",
        "false",
        "true",
        "true",
        "true"
    ],
    "float_settings": [
        "0",
        "500",
        "99",
        "0.439999998",
        "0.5",
        "0.579999983",
        "30",
        "20"
    ],
    "string_settings": [
        "D:\\opt\\seeking.jpg"
    ]
	"camera_index": 1,
	"image_name": "file_path",
	"process_phase": 2
   }
   Return: Success
   process phase: 0 no process at all, 1 pre-process only, 2 pre-process and classification
 */
  m_server.resource["^/test_setting_write"]["POST"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    auto query_fields = request->parse_query_string();
    try {
      // reset test and trigger camera flag to false
      RunningInfo::instance().GetTestProductionInfo().SetTestStatus(0);
      RunningInfo::instance().GetTestProductionInfo().SetTriggerCameraImageStatus(0);

      string prod = SimpleWeb::getValue(query_fields, "prod");
      ptree pt;
      read_json(request->content, pt);

      if(pt.empty()) {
        LogERROR << "Invalid content in test product setting";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Content");
        return 1;
      }

      if(RunningInfo::instance().GetTestProductionInfo().GetCurrentProd() == "N/A") {
        LogERROR << "Test product setting ignored due to product name unavailable";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Product Name Unavailable");
        return 1;
      }

      if(RunningInfo::instance().GetTestProductionInfo().GetCurrentProd() != prod) {
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
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
      return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::TriggerCameraImages() {
  /*
	POST:
	http: //localhost:8080/trigger_camera_images
	example : { "trigger" : 1 }
  */
  m_server.resource["^/trigger_camera_images"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      // reset test flag to false
      RunningInfo::instance().GetTestProductionInfo().SetTestStatus(0);
      RunningInfo::instance().GetTestProductionInfo().SetTriggerCameraImageStatus(0);

      ptree pt;
      read_json(request->content, pt);

      if(pt.empty()) {
        LogERROR << "Invalid content in triggering camera images";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Content");
        return 1;
      }

      if(RunningInfo::instance().GetTestProductionInfo().GetCurrentProd() == "N/A") {
        LogERROR << "Trigger camera images ignored due to product name unavailable";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Product Name Unavailable");
        return 1;
      }

      bool value = pt.get<int>("trigger") == 1 ? true : false;
      LogINFO << "Trigger camera images flag: " << boolalpha << value;

      string cameraName = RunningInfo::instance().GetTestProductSetting().getCameraName();
      LogINFO << "Trigger camera images from camera: " << cameraName;

      if(value) {
        // start camera or cameras which can be triggered to save testing images
        bool start = (cameraName == "N/A") ? m_pCameraManager->startCameras() : m_pCameraManager->startCamera(cameraName);
        if(start) {
          LogINFO << "Camera or cameras start...";
          RunningInfo::instance().GetTestProductionInfo().SetTriggerCameraImageStatus(1);
        }
        else {
          LogERROR << "Start camera or cameras Failed";
          response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Start Cameras Failed");
          return 1;
        }
      }
      else {
        // stop cameras and reset test status back to 0
        RunningInfo::instance().GetTestProductionInfo().SetTriggerCameraImageStatus(0);
        this_thread::sleep_for(chrono::milliseconds(10));
        m_pCameraManager->stopCameras();
        LogINFO << "All cameras stop...";
      }
      response->write("Success");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
      return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::SetTestCameraIndex() {
  /*
   POST: http://localhost:8080/set_test_camera_index
   { 
     "camera_index": 3
   }
  */
  m_server.resource["^/set_test_camera_index"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      ptree pt;
      read_json(request->content, pt);

      if(pt.empty()) {
        LogERROR << "Invalid content in setting camera index";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Invalid Content");
        return 1;
      }

      if(RunningInfo::instance().GetTestProductionInfo().GetCurrentProd() == "N/A") {
        LogERROR << "Set test camera index ignored due to product name unavailable";
        response->write(SimpleWeb::StatusCode::client_error_bad_request, "Product Name Unavailable");
        return 1;
      }

      if(pt.get_child_optional("camera_index")) {
        int value = pt.get<int>("camera_index");
        string name = value == -1 ? "N/A" : "CAM" + to_string(value);
        int boardId = value == -1 ? -1 : RunningInfo::instance().GetRunningData().getBoardIdFromCamera(value);
        LogINFO << "current camera name: " << name;
        LogINFO << "current board id: " << boardId;
        RunningInfo::instance().GetTestProductSetting().setCameraName(name);
        RunningInfo::instance().GetTestProductSetting().setBoardId(boardId);
      }

      response->write("Success");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
      return 1;
    }
    return 0;
  };
  return 0;
}

int WebServer::ResetTestProduction() {
  /*
   POST: http://localhost:8080/reset_test_prod
  */
  m_server.resource["^/reset_test_prod"]["POST"] = [this](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
    try {
      LogINFO << "Reset test product info, and setting...";
      RunningInfo::instance().GetTestProductionInfo().reset();
      RunningInfo::instance().GetTestProductSetting().reset();

      response->write("Success");
    }
    catch(const exception &e) {
      LogERROR << e.what();
      response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
      return 1;
    }
    return 0;
  };
  return 0;
}