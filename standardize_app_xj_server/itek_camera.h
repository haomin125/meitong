#ifndef ITEK_CAMERA_H
#define ITEK_CAMERA_H

#include "camera.h"
#include "itek_camera_config.h"

// #include <pylon/PylonBase.h>
// #include <pylon/PylonIncludes.h>
// #include <pylon/InstantCamera.h>
// #include <GenApi/INodeMap.h>
#include <opencv2/opencv.hpp>

#include "IKapC.h"

/* Simple error handling. */
#define CHECK(errc)           \
	if (ITKSTATUS_OK != errc) \
	printErrorAndExit(errc)

class ItekCamera : public BaseCamera
{
public:
	ItekCamera(const std::string &cameraName, const std::string &deviceName, const std::shared_ptr<ItekCameraConfig> pConfig);
	virtual ~ItekCamera();

	// initialization of the camera
	virtual bool start();
	virtual bool open();
	virtual bool read(cv::Mat &frame);
	virtual void release();
	virtual bool restart();

private:
	// pay attention on the initialization order,
	// init should be earilier than shared_ptr other wise segmentation fault will occur
	// Pylon::PylonAutoInitTerm m_autoInitTerm;
	// std::shared_ptr<Pylon::CInstantCamera> m_pCapture;

	// Camera device handle.
	ITKDEVICE m_pCapture;

	std::vector<ITKBUFFER> m_vectorBuffer;

	// stream handle
	ITKSTREAM m_pStream;

	// current frame index
	int m_iCurFrameIndex;

	// offline flag
	bool m_bOffline;

	// overlap image
	std::shared_ptr<cv::Mat> m_pOverlapFrame;

	bool blockRead(cv::Mat &frame);
	bool nonBlockRead(cv::Mat &frame);

	// bool sendSoftwareTriggerCommand();
};

#endif // ITEK_CAMERA_H