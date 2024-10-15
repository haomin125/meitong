#ifndef APP_WEB_SERVER_H
#define APP_WEB_SERVER_H

#include "apb_web_server.hpp"
#include "running_status.h"
#include <boost/optional/optional.hpp>


class AppWebServer : public ApbWebServer
{
public:
    AppWebServer(const std::shared_ptr<CameraManager> pCameraManager);
	AppWebServer(const std::shared_ptr<CameraManager> pCameraManager, const std::string &address, const unsigned short port);
	virtual ~AppWebServer() {};

private:
	int addWebCmds();

    //产品
    int CreateTestProduction();
    int SetCurrentProd();
    int SetDefaultAutoParams();

    //读写数据库
    int ReadSetting();
    int SettingWrite();
    int TestSettingWrite();

    //显示图片路径
    int GetImagePath();
    int SetImagePath();

    //瑕疵参数和画框参数
    int GetDetectingParams();
    int GetTestingParams();//自动更新参数后刷新页面
    int SetRect();
    int SetParams();

    //相机参数
    int GetCameraParams();
    int SetCameraParams();
    int SaveCameraParams();
    int TriggerCamera();

    //PLC参数
	int GetPlcParams();
	int SetPlcParams();

    //公共参数
	int GetCommonParams();
	int SetCommonParams();

    int ImageProcessed();//图像处理
    int Preview();//测试预览
    int autoUpdateParams();//自动更新参数
    int GetDefect();   // Get defective data from database
    int GetRealReportWithLot();
    int GetMesData(); // Get Mes records;

    //by zhz 20231128 合并UI接口
    int GetCameraResultImage();
    int GetCurrentResult();
    int GetHistoryNg();//获取历史记录图片(走马灯)
    
    void sendImage(const std::shared_ptr<HttpServer::Response> &response, const cv::Mat &frame);

private:
    //by zhz 20231128
    std::shared_ptr<CameraManager> m_pCameraManager;
};

#endif //APP_WEB_SERVER_H
