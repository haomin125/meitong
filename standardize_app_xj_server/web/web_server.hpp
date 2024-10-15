#ifndef WEB_SERVER_HPP
#define WEB_SERVER_HPP

#include "server_http.hpp"
#include "camera_manager.h"
#include "io_manager.h"
#include "timer_utils.hpp"

// Added for the json-example
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

// Added for the default_resource example
#include <algorithm>
#include <fstream>

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

class WebServer {
public:
  WebServer(const std::shared_ptr<CameraManager> pCameraManager);
  WebServer(const std::shared_ptr<CameraManager> pCameraManager, const std::string &address, const unsigned short port);
  virtual ~WebServer() {};

  HttpServer &getServer() { return m_server; }

  void setIoManager(const std::shared_ptr<BaseIoManager> pIoManager) { m_pIoManager = pIoManager; }
  void setLightSourceManager(const std::shared_ptr<BaseIoManager> pIoManager) { m_pLightSourceManager = pIoManager; }

  // timer and defect ratio related interface
  void resetTimer() { m_timer.Reset(); }
  void setTimerIntervalInSeconds(const int seconds) { m_iTimerIntervaInSeconds = seconds; }
  void setMaxDefectRatio(const float ratio) { m_fMaxDefectRatio = ratio; }

  // update report in database need board id, add interface here
  void setTotalBoards(const int total) { m_iTotalBoards = total; }
  int totalBoards() { return m_iTotalBoards; }

protected:
  // HTTP-server at port 8080 using 1 thread
  // Unless you do more heavy non-threaded processing in the resources,
  // 1 thread is usually faster than several threads
  HttpServer m_server;
  std::shared_ptr<CameraManager> getCameraManager() { return m_pCameraManager; }
  std::shared_ptr<BaseIoManager> getIoManager() { return m_pIoManager; }

private:
  int addWebCmds();
  int ErrorHandling();
  void sendImage(const std::shared_ptr<HttpServer::Response> &response, const cv::Mat &frame);
  bool updateDatabaseReport(const std::string &product, const std::string &lot);

  // Functions below are for only test purpose.
  int PostString();
  int PostJson();

  int GetInfo();
  int GetMatchNumber();
  int GetWork();
  int GetDefault();

  // Get commands
  int GetUISetting();
  int GetListCameras();
  int GetCamera();
  int StartCamera();
  int StopCamera();
  int GetCameraImage();
  int GetCameraReadFrame();
  int GetListUsers();
  int GetListUsersByRole();
  int GetRole();
  int ReadSetting();
  int GetDebugSettingImage();
  int GetAlarms();
  int GetAlarmEventTotal();
  int GetHistoryTotal();
  int GetProdList();
  int GetReport();
  int GetReportWithLot();
  int GetAlarmTop();
  int GetEventByMsg();
  int GetRunStatus();
  int GetAutoStatus();
  int GetCameraDisplayMode();
  int GetCurrentProd();
  int GetCurrentLotNumber();
  int GetRunningData();
  int GetHistoryFiles();
  int GetDefectStats();
  int GetCameraParameters();

  // Post Command
  int Login();
  int ChangePwd();
  int SettingWrite();
  int SetAutoManual();
  int SetCameraDisplayMode();
  int StartRun();
  int ResetCalculation();
  int AlarmAck();
  int AlarmAckAll();
  int AlarmDelete();
  int AlarmDeleteAll();
  int AlarmClear();
  int SetCurrentProd();
  int DeleteProd();
  int DeleteProds();
  int SetCurrentLot();
  int SetCameraParameters();
  int SetLightSourceValue();

  // add more test related interfaces here
  int CreateTestProduction();
  int TestSettingWrite();
  int TriggerCameraImages();
  int SetTestCameraIndex();
  int ResetTestProduction();

  // add, update, and delete user
  int AddUser();
  int DeleteUser();
  int DeleteUsers();
  int UpdateUserInfo();

  // reuse this one to handle different io manager to test if io write is sucessful
  int IoWrite();


  std::shared_ptr<CameraManager> m_pCameraManager;
  std::shared_ptr<BaseIoManager> m_pIoManager;
  std::shared_ptr<BaseIoManager> m_pLightSourceManager;

  timer_utils::Timer<std::chrono::seconds> m_timer;
  int m_iTimerIntervaInSeconds;
  float m_fMaxDefectRatio;

  int m_iTotalBoards;
};

#endif //WEB_SERVER_HPP
