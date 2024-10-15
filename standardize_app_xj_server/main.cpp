#include "version.h"
#include "web_server.hpp"
#include "xj_app_web_server.h"
#include "xj_app_server.h"
#include "xj_app_io_manager.h"
#include "customized_json_config.h"
#include "xj_app_json_config.h"
#include "database.h"
#include "logger.h"


#include <csignal>
#include <thread>
#include <iostream>

#ifdef __linux__
#include <unistd.h>
#elif _WIN64
#include "XGetopt.h"
#endif

using namespace std;

namespace
{
volatile sig_atomic_t signalStatus;
}

void signalHandler(int signal)
{
	signalStatus = signal;
	LogINFO << "signalHandler(): catch signal: " << signalStatus;

	XJAppServer::stopDetectors();
}

int main(int argc, char const *argv[])
{
	string usage = "Usage: xj_server -a address -p port -d plc_distance -c camera_type -l log_level -o logoutput -f mock_file_path \r\n \
   address     : IP address, eg.  127.0.0.1\r\n \
   port        : eg. 8080\r\n \
   plc_distance: integer\r\n \
   log_level   : integer (logCRITICAL = 0, logERROR = 1, logWARNING = 2, logDEBUG = 3, logINFO = 4)\r\n \
   camera_type : mock  (mock camera, default using real camera)\r\n \
   logoutput   : 0  (0: to terminal, default to log file)\r\n \
   file_path   : mock file path (only valid if camera type is mock)\r\n \
                   ";

	// parse the command line
	string webAddress = "localhost";
	int webPort = 8080;
	int plcDistance = 0;
	string cameraType = "haikang";
	// string cameraType = "mock";
#ifdef __linux__
	string mockFilePath = "/opt/";
	string logFile = "/opt/xj_server.log";
#elif _WIN64
	string mockFilePath = "C:\\opt\\";
	string logFile = "C:\\opt\\xj_server.log";
#endif
	LogLevel logLevel = LogLevel::logDEBUG;
	int opt;

#ifdef __linux__
	while ((opt = getopt(argc, (char **)argv, "a:p:d:c:l:o:f:")) != -1)
	{
#elif _WIN64
	while ((opt = getopt(argc, (TCHAR **)argv, "a:p:d:c:l:o:f:")) != -1)
	{
#endif
		switch (opt)
		{
		case 'a':
			webAddress = optarg;
			break;
		case 'p':
			webPort = atoi(optarg);
			break;
		case 'd':
			plcDistance = atoi(optarg);
			break;
		case 'c':
			cameraType = optarg;
			break;
		case 'f':
			mockFilePath = optarg;
			break;
		case 'o':
			if (atoi(optarg) == 0)
				logFile = "";
			break;
		case 'l':
			logLevel = (LogLevel)atoi(optarg);
			break;
		case '?':
			cout << usage << endl;
			return 0;
		default:
			return 0;
		}
	}

	Logger::instance().setFileName(logFile);
	Logger::instance().setLoggerLevel(logLevel);
	LogINFO << "XJ SERVER " << PROJECT_NAME << "(" << PROJECT_VERSION << ") START...";

	// signal handling
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGABRT, signalHandler);
	signal(SIGSEGV, signalHandler);
#ifdef __linux__
	signal(SIGQUIT, signalHandler);
#endif

	LogDEBUG << "app Version: " << "1.0.0";

	// set params config path
	const string sParamsJsonPath = CustomizedJsonConfig::instance().get<string>("PARAMS_CONFIG_PATH");
	AppJsonConfig::instance().setJsonPath(sParamsJsonPath);

	// initial camera manager
	shared_ptr<CameraManager> pCameraManager = make_shared<CameraManager>();

	// initial IoManager
	shared_ptr<BaseIoManager> pIoManager = nullptr;
	const bool bIsSend = CustomizedJsonConfig::instance().get<bool>("IS_ENABLE_SEND_SIGNAL_IN_MOCK_VIDEO");
	if(cameraType != "mock" || bIsSend)
	{
		const bool bIsUseIoCard = CustomizedJsonConfig::instance().get<bool>("IS_USE_IO_CARD");
		if(bIsUseIoCard)
		{
			const string sDeviceName = CustomizedJsonConfig::instance().get<string>("IO_CARD_DEVICE_NAME");
		#ifdef _WIN64
			pIoManager = make_shared<AppIoManagerIOCard>(sDeviceName);
		#else
			pIoManager = make_shared<AppIoManagerIOCard>(sDeviceName);
		#endif
			//关闭所有io点
			for(int i = 0; i != 8; ++i)
			{
				dynamic_pointer_cast<AppIoManagerIOCard>(pIoManager)->writeBit(0, i, 0);
				dynamic_pointer_cast<AppIoManagerIOCard>(pIoManager)->writeBit(1, i, 0);
			}
		}
		else
		{
			const string sServerIp =  CustomizedJsonConfig::instance().get<string>("PLC_MODBUS_TCP_IP");
			const int serverPort = CustomizedJsonConfig::instance().get<int>("PLC_MODBUS_TCP_PORT");
			const int sendRecvTimeout = CustomizedJsonConfig::instance().get<int>("PLC_MODBUS_TCP_TIMEOUT");
			
			#ifdef _WIN64
				pIoManager = make_shared<AppIoManagerPLC>(sServerIp, serverPort, sendRecvTimeout);
			#else
				pIoManager = make_shared<AppIoManagerPLC>(sServerIp, serverPort, sendRecvTimeout);
			#endif

		}
	}

	// initial web server
	AppWebServer webServer(pCameraManager, webAddress, webPort);
	webServer.setIoManager(pIoManager);

	HttpServer &httpServer = webServer.getServer();

	// initial xj server
	XJAppServer xjServer(pCameraManager, plcDistance);
	if (!xjServer.initXJApp(pIoManager))
	{
		LogCRITICAL << "Initial XJ app failed, exit...";
		RunningInfo::instance().GetRunningData().setCustomerDataByName("critical", "initial-XJ-server-failed");
		exit(-1);
	}
	webServer.setTotalBoards(xjServer.totalDetectors());

	string host = httpServer.config.address.empty() ? "localhost" : httpServer.config.address;
	cout << "starting web server at " << host << ":" << httpServer.config.port << "..." << endl;
	LogDEBUG << "Starting web server at " << host << ":" << httpServer.config.port << "...";

	// start web server
	thread webServerThread([&httpServer]() {
		try
		{
			httpServer.start();
		}
		catch (...)
		{
			LogCRITICAL << "Start web server failed, exit...";
			RunningInfo::instance().GetRunningData().setCustomerDataByName("critical", "start-web-server-failed");
			exit(-1);
		}
	});

	// wait for camera starts
	LogINFO << "Detectors have been initialized, and wait for cameras...";
	if (webServerThread.joinable() && xjServer.initCameraManager(cameraType, mockFilePath))
	{
		LogINFO << "Camera manager has been initialized";

		// start detector threads
		vector<thread> detectorThreads;
		for (int detectorIdx = 0; detectorIdx < xjServer.totalDetectors(); detectorIdx++)
		{
			LogINFO << "Detector thread " << detectorIdx << " started...";
			detectorThreads.emplace_back(&XJAppServer::startDetector, &xjServer, detectorIdx);
		}

		const bool bIsEnable = CustomizedJsonConfig::instance().get<bool>("IS_ENABLE_HEART_BEAT_SIGNAL_CHECK");
		if(bIsEnable)
		{
			if(cameraType != "mock" || bIsSend)
			{
				thread heartBeatThread(&XJAppServer::checkHeartBeat, &xjServer);
				LogINFO << "Check heartbeat thread started...";
				heartBeatThread.join();
			}
		}
		const bool bIsUseMergeServer = CustomizedJsonConfig::instance().get<bool>("IS_USE_MERGE_SERVER");
		if (bIsUseMergeServer)
		{
			thread updateMergeTotalThread(&XJAppServer::updateMergeTotal, &xjServer);
			updateMergeTotalThread.join();
		}

		// detector thread join, main thread is waiting for detector thread to finish
		for (auto &itr : detectorThreads)
		{
			itr.join();
		}
		
	}

	// when we reach this point, the detector thread either exit or never start
	LogINFO << "All detector threads exited, we stop web server as well";
	httpServer.stop();

	// web server thread join, main thread is waiting for web thread to finish
	webServerThread.join();

	LogDEBUG << "XJ SERVER STOPPED";

	return 0;
}