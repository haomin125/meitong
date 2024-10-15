#include "itek_camera_config.h"
#include "logger.h"

#include <sstream>

using namespace std;
using namespace cv;
// using namespace Pylon;
// using namespace GenApi;

ItekCameraConfig::ItekCameraConfig() : CameraConfig(),
									   m_sDeviceClass("GigEVision"),
									   m_dimension(0, 0, 0),
									   m_offset(0, 0),
									   m_iExposure(10000),
									   m_sTriggerMode("Off"),
									   m_iPacketSize(8192),
									   m_iInterpacketDelay(20000),
									   m_iReceiveThreadPriority(50),
									   m_fLineDebouncerTimeAbs(10.0),
									   m_sPixelFormat("BayerRG8"),
									   m_bGammaEnable(false),
									   m_fGamma(-1),
									   m_bSoftTriggerEnable(false),
									   m_iSoftTriggerWaitInMilliSeconds(5),
									   m_pCapture(nullptr),
									   m_iBufferNumbers(1),
									   m_iOverlap(0),
									   m_bTransposeEnable(false)
{
}

/**
 *  itek camera we use non-blocking read to read for image
 **/
ItekCameraConfig::ItekCameraConfig(const string &model, const int width, const int height, const int fps, const int x,
								   const int y, const int exposure, const std::string &triggerMode, const int packetSize,
								   const int interpacketDelay, const int priority, const float timeAbs, const std::string &pixelFormat,
								   const bool gammaEnable, const float gamma) : CameraConfig(model, false),
																				m_sDeviceClass("GigEVision"),
																				m_dimension(width, height, fps),
																				m_offset(x, y),
																				m_iExposure(exposure),
																				m_sTriggerMode(triggerMode),
																				m_iPacketSize(packetSize),
																				m_iInterpacketDelay(interpacketDelay),
																				m_iReceiveThreadPriority(priority),
																				m_fLineDebouncerTimeAbs(timeAbs),
																				m_sPixelFormat(pixelFormat),
																				m_bGammaEnable(gammaEnable),
																				m_fGamma(gamma),
																				m_bSoftTriggerEnable(false),
																				m_iSoftTriggerWaitInMilliSeconds(5),
																				m_pCapture(nullptr),
																				m_iBufferNumbers(1),
																				m_iOverlap(0),
																				m_bTransposeEnable(false)
{
}

ItekCameraConfig::ItekCameraConfig(const int readTimeout, const string &model, const int width, const int height,
								   const int fps, const int x, const int y, const int exposure, const std::string &triggerMode,
								   const int packetSize, const int interpacketDelay, const int priority, const float timeAbs,
								   const std::string &pixelFormat, const bool gammaEnable, const float gamma) : CameraConfig(model, readTimeout, false),
																												m_sDeviceClass("GigEVision"),
																												m_dimension(width, height, fps),
																												m_offset(x, y),
																												m_iExposure(exposure),
																												m_sTriggerMode(triggerMode),
																												m_iPacketSize(packetSize),
																												m_iInterpacketDelay(interpacketDelay),
																												m_iReceiveThreadPriority(priority),
																												m_fLineDebouncerTimeAbs(timeAbs),
																												m_sPixelFormat(pixelFormat),
																												m_bGammaEnable(gammaEnable),
																												m_fGamma(gamma),
																												m_bSoftTriggerEnable(false),
																												m_iSoftTriggerWaitInMilliSeconds(5),
																												m_pCapture(nullptr),
																												m_iBufferNumbers(1),
																												m_iOverlap(0),
																												m_bTransposeEnable(false)
{
}

/**
 * Itek camera initialization list:
 * Height, width
 * Exposure
 * FPS
 **/
bool ItekCameraConfig::setConfig()
{
	// if use camera default or manufacture settings, simply return true
	if (CameraConfig::setConfig())
	{
		return true;
	}

	// if (m_pNodeMap == nullptr)
	// {
	// 	LogERROR << "Invalid INodeMap";
	// 	return false;
	// }

	LogDEBUG << "Read timeout set to " << timeout() << " ms";

	/* set the width and height */
	// CIntegerPtr width(m_pNodeMap->GetNode("Width"));
	// CIntegerPtr height(m_pNodeMap->GetNode("Height"));
	// if (width.IsValid() && height.IsValid())
	// {
	// try
	// {
	ITKSTATUS res = ITKSTATUS_OK;
	res = ItkDevSetInt64(m_pCapture, "Width", m_dimension.m_width);
	if (ITKSTATUS_OK != res)
	{
		stringstream ss;
		ss << "Error Code: " << hex << setw(8) << setfill('0') << res;
		LogWARNING << "Set camera width error, " << ss.str();
	}
	// else
	// {
	int64_t my_width = 0;
	ItkDevGetInt64(m_pCapture, "Width", &my_width);
	LogDEBUG << "Width set to: " << my_width;
	// }

	res = ItkDevSetInt64(m_pCapture, "Height", m_dimension.m_height);
	if (ITKSTATUS_OK != res)
	{
		stringstream ss;
		ss << "Error Code: " << hex << setw(8) << setfill('0') << res;
		LogWARNING << "Set camera height error, " << ss.str();
	}
	// else
	// {
	int64_t my_height = 0;
	ItkDevGetInt64(m_pCapture, "Height", &my_height);
	LogDEBUG << "Height set to: " << my_height;
	// }
	// 	width->SetValue(m_dimension.m_width);
	// 	height->SetValue(m_dimension.m_height);
	// 	int64_t my_width = width->GetValue();
	// 	int64_t my_height = height->GetValue();

	// 	LogDEBUG << "Width set to: " << my_width << "\t Height set to: " << my_height;
	// }
	// catch (GenICam::GenericException &e)
	// {
	// 	LogWARNING << "Set camera height/width error: " << e.what();
	// }
	// }

	/* set the exposure time */
	// CEnumerationPtr exposureAuto(m_pNodeMap->GetNode("ExposureAuto"));
	// CIntegerPtr exposureTimeRaw(m_pNodeMap->GetNode("ExposureTimeRaw"));
	// if (IsWritable(exposureAuto) && exposureTimeRaw.IsValid())
	// {
	// 	try
	// 	{
	// ITKSTATUS res = ITKSTATUS_OK;
	res = ItkDevSetDouble(m_pCapture, "ExposureTime", m_iExposure);
	if (ITKSTATUS_OK != res)
	{
		stringstream ss;
		ss << "Error Code: " << hex << setw(8) << setfill('0') << res;
		LogWARNING << "Set camera exposure time error, " << ss.str();
	}
	else
	{
		double my_exposureTime = 0;
		ItkDevGetDouble(m_pCapture, "ExposureTime", &my_exposureTime);
		LogDEBUG << "Exposure time set to: " << my_exposureTime;
	}
	// exposureAuto->FromString("Off");
	// LogDEBUG << "Exposure auto disabled";
	// exposureTimeRaw->SetValue(m_iExposure);
	// LogDEBUG << "Exposure set to: " << m_iExposure;
	// 	}
	// 	catch (GenICam::GenericException &e)
	// 	{
	// 		LogWARNING << "Set camera exposure error: " << e.what();
	// 	}
	// }

	/* set the FPS */
	// CBooleanPtr acquisitionFrameRateEnable(m_pNodeMap->GetNode("AcquisitionFrameRateEnable"));
	// CFloatPtr acquisitionFrameRateAbs(m_pNodeMap->GetNode("AcquisitionFrameRateAbs"));
	// if (IsAvailable(acquisitionFrameRateEnable) && acquisitionFrameRateAbs.IsValid())
	// {
	// 	try
	// 	{
	// 		acquisitionFrameRateEnable->SetValue(1);
	// 		LogDEBUG << "Frame rate enabled";
	// 		// convert uint64 to double which may loss precisions
	// 		acquisitionFrameRateAbs->SetValue((double)m_dimension.m_fpsOrLineRate);
	// 		double currentFrameRate = acquisitionFrameRateAbs->GetValue();
	// 		LogDEBUG << "Frame rate set to: " << currentFrameRate;
	// 	}
	// 	catch (GenICam::GenericException &e)
	// 	{
	// 		LogWARNING << "Set camera frame rate error: " << e.what();
	// 	}
	// }

	/* set the offset of x and y */
	// CIntegerPtr offsetX(m_pNodeMap->GetNode("OffsetX"));
	// CIntegerPtr offsetY(m_pNodeMap->GetNode("OffsetY"));
	// if (offsetX.IsValid() && offsetY.IsValid())
	// {
	// 	try
	// 	{

	// ITKSTATUS res = ITKSTATUS_OK;
	res = ItkDevSetInt64(m_pCapture, "OffsetX", m_offset.m_x);
	if (ITKSTATUS_OK != res)
	{
		stringstream ss;
		ss << "Error Code: " << hex << setw(8) << setfill('0') << res;
		LogWARNING << "Set camera offset X error, " << ss.str();
	}
	// else
	// {
	int64_t my_offsetX = 0;
	ItkDevGetInt64(m_pCapture, "OffsetX", &my_offsetX);
	LogDEBUG << "OffsetX set to: " << my_offsetX;
	// }

	// 		offsetX->SetValue(m_offset.m_x);
	// 		offsetY->SetValue(m_offset.m_y);
	// 		int64_t currentX = offsetX->GetValue();
	// 		int64_t currentY = offsetY->GetValue();
	// 		LogDEBUG << "Offset X to: " << currentX << "\t Y to: " << currentY;
	// 	}
	// 	catch (GenICam::GenericException &e)
	// 	{
	// 		LogWARNING << "Set camera offset X/Y error: " << e.what();
	// 	}
	// }

	/* set the trigger mode */

	// res = ItkDevSetInt64(m_pCapture, "Width", m_dimension.m_width);
	// Open frame trigger mode.
	res = ItkDevFromString(m_pCapture, "TriggerSelector", "FrameStart");
	res = ItkDevFromString(m_pCapture, "TriggerMode", m_sTriggerMode.c_str());
	if (ITKSTATUS_OK != res)
	{
		stringstream ss;
		ss << "Error Code: " << hex << setw(8) << setfill('0') << res;
		LogWARNING << "Set camera Frame start trigger mode error, " << ss.str();
	}
	else
	{
		LogDEBUG << "Frame start trigger mode set to: " << m_sTriggerMode;
	}

	// Select trigger source.
	res = ItkDevFromString(m_pCapture, "TriggerSource", "Line3");
	if (ITKSTATUS_OK != res)
	{
		stringstream ss;
		ss << "Error Code: " << hex << setw(8) << setfill('0') << res;
		LogWARNING << "Set camera Frame start trigger source error, " << ss.str();
	}
	else
	{
		LogDEBUG << "Frame start trigger source set to: Line3";
	}
	// Set the number of frames to be captured each time.
	// res = ItkDevSetInt64(m_pCapture, "TriggerFrameCount", 1);

	// CEnumerationPtr triggerSelector(m_pNodeMap->GetNode("TriggerSelector"));
	// CEnumerationPtr triggerSource(m_pNodeMap->GetNode("TriggerSource"));
	// CEnumerationPtr triggerMode(m_pNodeMap->GetNode("TriggerMode"));
	// if (IsAvailable(triggerSelector) && IsAvailable(triggerSource) && IsAvailable(triggerMode))
	// {
	// 	try
	// 	{
	// 		triggerSelector->SetIntValue(triggerSelector->GetEntryByName("FrameStart")->GetValue());
	// 		LogDEBUG << "Select trigger frame start";
	// 		if (m_bSoftTriggerEnable)
	// 		{
	// 			triggerSource->SetIntValue(triggerSource->GetEntryByName("Software")->GetValue());
	// 			LogDEBUG << "Select source by software";
	// 			triggerMode->SetIntValue(triggerMode->GetEntryByName("On")->GetValue());
	// 			LogDEBUG << "Force trigger mode On";
	// 		}
	// 		else
	// 		{
	// 			triggerSource->SetIntValue(triggerSource->GetEntryByName("Line1")->GetValue());
	// 			LogDEBUG << "Select source by line1";
	// 			triggerMode->SetIntValue(triggerMode->GetEntryByName(m_sTriggerMode)->GetValue());
	// 			LogDEBUG << "Select trigger mode " << m_sTriggerMode;
	// 		}
	// 	}
	// 	catch (GenICam::GenericException &e)
	// 	{
	// 		LogWARNING << "Set trigger mode error: " << e.what();
	// 	}
	// }

	/* set delay, and packet size, those 2 parameters are in transport layer*/
	// CIntegerPtr packetSize(m_pNodeMap->GetNode("GevSCPSPacketSize"));
	// CIntegerPtr interpacketDelay(m_pNodeMap->GetNode("GevSCPD"));
	// if (packetSize.IsValid() && interpacketDelay.IsValid())
	// {
	// 	try
	// 	{
	// 		packetSize->SetValue(m_iPacketSize);
	// 		interpacketDelay->SetValue(m_iInterpacketDelay);
	// 		LogDEBUG << "Set PacketSize to: " << packetSize->GetValue();
	// 		LogDEBUG << "Set InterpacketDelay to: " << interpacketDelay->GetValue();
	// 	}
	// 	catch (GenICam::GenericException &e)
	// 	{
	// 		LogWARNING << "Set camera packet size, and inter packet delay error: " << e.what();
	// 	}
	// }

	/* set prioty */
	/* it is not available now, comment it out
	CIntegerPtr priority(m_pNodeMap->GetNode("ReceiveThreadPriority"));
	if (priority.IsValid())
	{
		try
		{
			priority->SetValue(m_iReceiveThreadPriority);
			LogINFO << "Set ReceiveThreadPriority to: " << priority->GetValue();
		}
		catch (GenICam::GenericException &e)
		{
			LogWARNING << "Set camera receive thread priority error: " << e.what();
		}
	}
	*/

	// set line debouncer time abs
	// CEnumerationPtr(m_pNodeMap->GetNode("LineSelector"))->FromString("Line1");
	// CFloatPtr timeAbs(m_pNodeMap->GetNode("LineDebouncerTimeAbs"));
	// if (timeAbs.IsValid())
	// {
	// 	try
	// 	{
	// 		timeAbs->SetValue(m_fLineDebouncerTimeAbs);
	// 		LogDEBUG << "Set LineDebouncerTimeAbs to: " << timeAbs->GetValue();
	// 	}
	// 	catch (GenICam::GenericException &e)
	// 	{
	// 		LogWARNING << "Set camera line debouncer time abs error: " << e.what();
	// 	}
	// }
	res = ItkDevSetInt64(m_pCapture, "LineDebouncingPeriod", (int64_t)m_fLineDebouncerTimeAbs);
	if (ITKSTATUS_OK != res)
	{
		stringstream ss;
		ss << "Error Code: " << hex << setw(8) << setfill('0') << res;
		LogWARNING << "Set camera LineDebouncingPeriod error, " << ss.str();
	}
	else
	{
		int64_t my_lineDebouncingPeriod = 0;
		ItkDevGetInt64(m_pCapture, "LineDebouncingPeriod", &my_lineDebouncingPeriod);
		LogDEBUG << "LineDebouncingPeriod set to: " << my_lineDebouncingPeriod;
	}

	// set pixel format
	// if (!m_sPixelFormat.empty())
	// {
	// 	try
	// 	{
	// 		CEnumerationPtr(m_pNodeMap->GetNode("PixelFormat"))->FromString(m_sPixelFormat);
	// 		LogDEBUG << "Set PixelFormat to: " << m_sPixelFormat;
	// 	}
	// 	catch (GenICam::GenericException &e)
	// 	{
	// 		LogWARNING << "Set pixel format error: " << e.what();
	// 	}
	// }

	if (!m_sPixelFormat.empty())
	{
		res = ItkDevFromString(m_pCapture, "PixelFormat", m_sPixelFormat.c_str());
		if (ITKSTATUS_OK != res)
		{
			stringstream ss;
			ss << "Error Code: " << hex << setw(8) << setfill('0') << res;
			LogWARNING << "Set camera pixel format error, " << ss.str();
		}
		else
		{
			// string my_pixelFormat;
			// ItkDevToString(m_pCapture, "PixelFormat", my_pixelFormat);
			LogDEBUG << "Pixel format set to: " << m_sPixelFormat;
		}
	}

	// enable gamma only with sRGB, gamma 0.4
	// if (m_bGammaEnable && m_fGamma == -1)
	// {
	// 	try
	// 	{
	// 		CBooleanPtr(m_pNodeMap->GetNode("GammaEnable"))->SetValue(true);
	// 		CEnumerationPtr(m_pNodeMap->GetNode("GammaSelector"))->FromString("sRGB");
	// 		LogDEBUG << "Gamma is enabled, default is sRGB";
	// 	}
	// 	catch (GenICam::GenericException &e)
	// 	{
	// 		LogWARNING << "Enable gamma error: " << e.what();
	// 	}
	// }

	// if (m_fGamma != -1)
	// {
	// 	try
	// 	{
	// 		CBooleanPtr(m_pNodeMap->GetNode("GammaEnable"))->SetValue(true);
	// 		CEnumerationPtr(m_pNodeMap->GetNode("GammaSelector"))->FromString("User");
	// 		CFloatPtr(m_pNodeMap->GetNode("Gamma"))->SetValue(m_fGamma);
	// 		LogDEBUG << "Gamma is enabled and set to " << m_fGamma;
	// 	}
	// 	catch (GenICam::GenericException &e)
	// 	{
	// 		LogWARNING << "Set gamma " << m_fGamma << " error: " << e.what();
	// 	}
	// }

	return true;
}

std::stringstream ItekCameraConfig::getConfigParameters()
{
	m_root.put("width", m_dimension.m_width);
	m_root.put("height", m_dimension.m_height);
	m_root.put("fps", m_dimension.m_fpsOrLineRate);

	m_root.put("x", m_offset.m_x);
	m_root.put("y", m_offset.m_y);

	m_root.put("exposure", m_iExposure);
	m_root.put("trigger_mode", (string)m_sTriggerMode);
	m_root.put("packet_size", m_iPacketSize);
	m_root.put("interpacket_delay", m_iInterpacketDelay);
	m_root.put("receive_thread_priority", m_iReceiveThreadPriority);
	m_root.put("debounce_time", m_fLineDebouncerTimeAbs);
	m_root.put("pixel_format", (string)m_sPixelFormat);
	m_root.put("gamma_enable", m_bGammaEnable);
	m_root.put("gamma", m_fGamma);

	m_root.put("device_class", (string)m_sDeviceClass);
	m_root.put("soft_trigger_enable", m_bSoftTriggerEnable);
	m_root.put("soft_trigger_wait_time_in_milliseconds", m_iSoftTriggerWaitInMilliSeconds);

	return writePtree();
}

void ItekCameraConfig::setConfigParameters(const stringstream &parameters)
{
	if (parameters.str().empty())
	{
		LogERROR << "Invalid parameter content";
		return;
	}

	readPtree(parameters);

	m_dimension = ImageDimension(m_root.get<int>("width"), m_root.get<int>("height"), m_root.get<int>("fps"));
	m_offset = ImageOffset(m_root.get<int>("x"), m_root.get<int>("y"));
	m_iExposure = m_root.get<int>("exposure");
	m_sTriggerMode = (m_root.get<string>("trigger_mode"));
	m_iPacketSize = m_root.get<int>("packet_size");
	m_iInterpacketDelay = m_root.get<int>("interpacket_delay");
	m_iReceiveThreadPriority = m_root.get<int>("receive_thread_priority");
	m_fLineDebouncerTimeAbs = m_root.get<float>("debounce_time");
	m_sPixelFormat = (m_root.get<string>("pixel_format"));
	m_bGammaEnable = m_root.get<bool>("gamma_enable");
	m_fGamma = m_root.get<float>("gamma");

	m_sDeviceClass = (m_root.get<string>("device_class"));
	m_bSoftTriggerEnable = m_root.get<bool>("soft_trigger_enable");
	m_iSoftTriggerWaitInMilliSeconds = m_root.get<int>("soft_trigger_wait_time_in_milliseconds");

	setConfig();
}