#ifndef ITEK_CAMERA_CONFIG_H
#define ITEK_CAMERA_CONFIG_H

#include "config.h"

// #include <pylon/PylonBase.h>
// #include <pylon/PylonIncludes.h>
// #include <GenApi/INodeMap.h>
#include <opencv2/opencv.hpp>
#include "IKapC.h"


class ItekCameraConfig : public CameraConfig
{
public:
	ItekCameraConfig();
	ItekCameraConfig(const std::string &model, const int width, const int height,
		const int fps, const int x, const int y, const int exposure = 10000, const std::string &triggerMode = "Off",
		const int packetSize = 8192, const int interpacketDelay = 20000, const int priority = 50, const float timeAbs = 10.0,
		const std::string &pixelFormat = "BayerRG8", const bool gammaEnable = false, const float gamma = -1);
	ItekCameraConfig(const int readTimeout, const std::string &model, const int width, const int height,
		const int fps, const int x, const int y, const int exposure = 10000, const std::string &triggerMode = "Off",
		const int packetSize = 8192, const int interpacketDelay = 20000, const int priority = 50, const float timeAbs = 10.0,
		const std::string &pixelFormat = "BayerRG8", const bool gammaEnable = false, const float gamma = -1);
	virtual ~ItekCameraConfig() {}

	// allow app to set its own camera device class, which should be "ItekGigE", "ItekUsb", and etc. default is "ItekGigE"
	std::string &deviceClass() { return m_sDeviceClass; }
	void setDeviceClass(const std::string &device) { m_sDeviceClass = device; }

	ImageDimension &imageDimension() { return m_dimension; }
	void setImageDimension(const int width, const int height, const int fps) { m_dimension = ImageDimension(width, height, fps); }
	ImageOffset &imageOffset() { return m_offset; }
	void setImageOffset(const int x, const int y) { m_offset = ImageOffset(x, y); }
	int exposure() const { return m_iExposure; }
	void setExposure(const int exposure) { m_iExposure = exposure; }
	std::string &triggerMode() { return m_sTriggerMode; }
	void setTriggerMode(const std::string &mode) { m_sTriggerMode = mode; }

	int packetSize() const { return m_iPacketSize; }
	void setPacketSize(const int packetSize) { m_iPacketSize = packetSize; }
	int interpacketDelay() const { return m_iInterpacketDelay; }
	void setInterpacketDelay(const int delay) { m_iInterpacketDelay = delay; }
	int receiveThreadPriority() const { return m_iReceiveThreadPriority; }
	void setReceiveThreadPriority(const int priority) { m_iReceiveThreadPriority = priority; }
	float lineDebouncerTimeAbs() const { return m_fLineDebouncerTimeAbs; }
	void setLineDebouncerTimeAbs(const float timeAbs) { m_fLineDebouncerTimeAbs = timeAbs; }

	std::string &pixelFormat() { return m_sPixelFormat; }
	void setPixelFormat(const std::string &format) { m_sPixelFormat = format; }

	// Here we explain how to use gammaEnable and gamma value:
	// if gammaEnable is true, but gamma = -1, gamma is enable, gamma selector set to sRGB, camera use default 0.4 as gamma value
	// if gamma > 0, gamma is enable, gamma selector set to User, gamma value set to the value what user defined here
	bool isGammaEnable() const { return m_bGammaEnable; }
	void setGammaEnable(const bool gammaEnable) { m_bGammaEnable = gammaEnable; }
	float gamma() const { return m_fGamma; }
	void setGamma(const float gamma) { m_fGamma = gamma; };

	bool softTriggerEnable() const { return m_bSoftTriggerEnable; }
	void setSoftTriggerEnable(const bool softTrigger) { m_bSoftTriggerEnable = softTrigger; }

	int softTriggerWaitTime() const { return m_iSoftTriggerWaitInMilliSeconds; }
	void setSoftTriggerWaitTime(const int timeInMilliSeconds) { m_iSoftTriggerWaitInMilliSeconds = timeInMilliSeconds; }

	// void setNodemap(GENAPI_NAMESPACE::INodeMap &nodemap) { m_pNodeMap = &nodemap; }
	void setCameraHandle(ITKDEVICE pCameraHandle) { m_pCapture = pCameraHandle; }

	virtual bool setConfig();

	virtual std::stringstream getConfigParameters();
	virtual void setConfigParameters(const std::stringstream &parameters);

	int bufferNumbers() const { return m_iBufferNumbers; }
	void setBufferNumbers(const int bufferNum) { m_iBufferNumbers = bufferNum; }

	int overlap() const { return m_iOverlap; }
	void setOverlap(const int overlap) { m_iOverlap = overlap; }

	void setHeight(const int height) { m_dimension.m_height = height; }

	void setTransposeEnable(const bool transposeEnable) { m_bTransposeEnable = transposeEnable; }
	bool transposeEnable() const { return m_bTransposeEnable; }
protected:
	// GENAPI_NAMESPACE::INodeMap *m_pNodeMap;
	ITKDEVICE m_pCapture;

private:
	std::string m_sDeviceClass;
	ImageDimension m_dimension;
	ImageOffset m_offset;
	int m_iExposure;
	// "Off": TriggerMode_Off, "On": TriggerMode_On, default is "Off"
	std::string m_sTriggerMode;
	int m_iPacketSize;
	int m_iInterpacketDelay;
	int m_iReceiveThreadPriority;
	float m_fLineDebouncerTimeAbs;
	std::string m_sPixelFormat;

	bool m_bGammaEnable;
	float m_fGamma;

	// soft trigger related, default soft trigger enable is false, wait time is 5 ms
	bool m_bSoftTriggerEnable;
	int m_iSoftTriggerWaitInMilliSeconds;

	int m_iBufferNumbers;
	int m_iOverlap;

	bool m_bTransposeEnable;
};

#endif // ITEK_CAMERA_CONFIG