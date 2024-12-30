#ifndef XJ_APP_SERVER_H
#define XJ_APP_SERVER_H

#include "camera_manager.h"
#include "product_line.h"
#include "io_manager.h"
#include "xj_app_detector.h"
#include "timer_utils.hpp"

#include <atomic>
#include <string>
#include <mutex>


class XJAppServer
{
public:
	XJAppServer(const std::shared_ptr<CameraManager> &pCameraManager, const int plcDistance);
	virtual ~XJAppServer() {};

	struct DetectorRunStatus
	{
		enum class Status : int
		{
			NOT_RUN = 0,
			PRODUCT_RUN = 1,
			TEST_RUN = 2,
			TEST_RUN_WITH_CAMERA = 3
		};

		DetectorRunStatus() {}

		Status getStatus();
	};

	enum class TestProcessPhase : int
	{
		NO_PROCESS = 0,
		PRE_PROCESS_ONLY = 1,
		PRE_PROCESS_AND_CLASSIFY = 2
	};

	int totalDetectors() { return (int)m_pDetectors.size(); }

	bool initXJApp(std::shared_ptr<BaseIoManager> &pPlcManager);
#ifdef __linux__
	bool initCameraManager(const std::string &cameraType, const std::string &filePath = "/opt/");
#elif _WIN64
	bool initCameraManager(const std::string &cameraType, const std::string &filePath = "C:\\opt\\");
#endif

	void startDetector(const int boardId);
	void checkHeartBeat();
	void updateMergeTotal();
	
	static void stopDetectors();
private:
	static std::atomic_bool m_bIsStop;

	static std::mutex m_productNameMutex;
	std::string m_sProductName;
	std::string m_sCameraType;

	std::shared_ptr<BaseIoManager> m_pIoManager;
	std::shared_ptr<CameraManager> m_pCameraManager;
	std::shared_ptr<ProductLine> m_pProductLine;

	std::shared_ptr<MultiThreadImageSaveBase> m_pSaveImageMultiThread;

	struct ServerDetector
	{
		bool m_bIsFirstStart;
		std::shared_ptr<AppDetector> m_pDetector;

		ServerDetector(const std::shared_ptr<AppDetector> &pDetector) : m_bIsFirstStart(true), m_pDetector(pDetector) {}
	};
	std::vector<ServerDetector> m_pDetectors;

	// cameras per board
	std::map<int, std::vector<std::shared_ptr<BaseCamera>>> m_cameras;

	int m_iPlcDistance;

	std::vector<std::shared_ptr<AppTimer>> m_vTimer;
	std::vector<std::shared_ptr<AppTimer>> m_vTimer_total;
    // std::vector<double> jishi_0;
    // std::vector<double> jishi_1;
    // std::vector<double> jishi1;
	// int signal_0 = 0;
	// int signal_1 = 0;

	std::shared_ptr<ThreadPool> m_pWorkerThread;

	bool initProductLine();
	bool initTargetMarginInBoard(const int boardId);
	bool initBoardParameters(const int boardId);

	void initProductCameras();
	void initMockCameras(const std::string &filePath);

	bool camerasAreReady(const int boardId);
	bool updateDatabaseReport(const int boardId);

	bool runDetector(const int boardId);
	bool parametersTest(const int boardId);
	bool saveCameraTriggeredTestImages(const int boardId);

	bool isProductSwitched();

	// init cameras
	template <typename T, typename G>
	void initCameras(const std::map<int, std::pair<std::string, std::string>> &cameras, const std::shared_ptr<G> pConfig[])
	{
		std::shared_ptr<ProductLineConfig> pProductLineConfig = m_pProductLine->getConfig();
		for (const auto &itr : cameras)
		{
			std::shared_ptr<T> pCamera = std::make_shared<T>(itr.second.first, itr.second.second, pConfig[itr.first]);
			m_pCameraManager->addCamera(pCamera);
			m_cameras[pProductLineConfig->getBoard(itr.first)].emplace_back(pCamera);
		}
	}
	
};

#endif // XJ_APP_SERVER_H