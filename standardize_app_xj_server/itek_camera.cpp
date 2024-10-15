#include "itek_camera.h"
#include "logger.h"

#include <sstream>
#include <thread>
// #include <pylon/TlFactory.h>
// #include <pylon/PylonImage.h>
// #include <pylon/ImageFormatConverter.h>

using namespace std;
using namespace cv;
// using namespace Pylon;
// using namespace GenApi;

/* This function will be registered as a callback function that is called
when the device is removed. Only stdcall functions can be registered. */
void IKAPC_CC removalCallbackFunction(void *context, ITKEVENTINFO eventInfo)
{
	/* Retrieve event type, feature index and countstamp */
	// uint32_t type = 0;
	// uint64_t countstamp = 0;

	// ITKSTATUS res = ItkEventInfoGetPrm(eventInfo, ITKEVENTINFO_PRM_TYPE, &type);
	// CHECK(res);

	// res = ItkEventInfoGetPrm(eventInfo, ITKEVENTINFO_PRM_HOST_TIME_STAMP, &countstamp);
	// CHECK(res);
	LogERROR << "camera has been removed";
}

/* This function demonstrates how to retrieve the error message for the last failed
function call. */
void printErrorAndExit(ITKSTATUS errc)
{
	// fprintf(stderr, "Error Code:%08x\n", errc);
	stringstream ss;
	// char test = 0xe0;
	// ss << test;
	ss << "Error Code: " << hex << setw(8) << setfill('0') << errc;
	// LogERROR << ss.str();

	// *m_pCapture = nullptr;
	// m_pCapture = nullptr;

	// ItkManTerminate();  /* Releases all resources. */
	// pressEnterToExit();

	// exit(EXIT_FAILURE);
}

ItekCamera::ItekCamera(const string &cameraName, const string &deviceName, const shared_ptr<ItekCameraConfig> pConfig) : BaseCamera(cameraName, deviceName, pConfig),
																														 //    m_autoInitTerm(),
																														 m_pCapture(nullptr),
																														 m_pStream(nullptr),
																														 m_iCurFrameIndex(0),
																														 m_bOffline(false)
{
}

bool ItekCamera::start()
{
	m_pException->reset();
	if (cameraStarted())
	{
		LogDEBUG << "Camera " << cameraName() << " has been started already";
		return true;
	}

	if (m_pConfig == nullptr)
	{
		LogERROR << "Camera " << cameraName() << " has invalid configuration holder";
		return false;
	}

	if (dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->deviceClass().empty())
	{
		LogERROR << "Camera " << cameraName() << " has invalid device class definition";
		return false;
	}

	string deviceClass = dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->deviceClass();
	try
	{
		ITKSTATUS res = ITKSTATUS_OK; /* Return value of IKapC methods.			*/
		uint32_t numDevices;		  /* Number of available devices.				*/
		// Create an instant camera object to grap images from multiple cameras
		// CTlFactory &tlFactory = CTlFactory::GetInstance();
		// ITransportLayer *pTL = tlFactory.CreateTl(deviceClass.c_str());
		// if (pTL == nullptr)
		// {
		// 	LogERROR << "Camera " << cameraName() << " has no " << deviceClass << " transport layer available.";
		// 	return false;
		// }
		// DeviceInfoList_t devices;
		// if (pTL->EnumerateDevices(devices) == 0)
		// {
		// 	LogERROR << "Camera " << cameraName() << " has no devices found";
		// 	return false;
		// }

		/* Before using any IKapC methods, the IKapC runtime must be initialized. */
		res = ItkManInitialize();
		CHECK(res);

		/* Enumerate all camera devices. You must call
		ItkManGetDeviceCount() before creating a device. */
		res = ItkManGetDeviceCount(&numDevices);
		CHECK(res);

		LogINFO << "Device number: " << numDevices;

		if (numDevices == 0)
		{
			LogERROR << "No device found!";
			/* Before exiting a program, ItkManTerminate() should be called to release
			all IKapC related resources. */
			ItkManTerminate();
			// pressEnterToExit();
			// exit(EXIT_FAILURE);
			return false;
		}

		// locate the current device index
		int deviceIdx = -1;
		ITKDEV_INFO di;
		for (int idx = 0; idx < numDevices; idx++)
		{
			res = ItkManGetDeviceInfo(idx, &di);
			// CHECK(res);

			if (deviceName() == (string)(di.SerialNumber))
			{
				deviceIdx = idx;
				break;
			}
		}
		if (deviceIdx == -1)
		{
			LogERROR << "Camera " << cameraName() << " has no devices matched with " << deviceName();
			return false;
		}

		// create capture with current device
		LogINFO << "Camera " << cameraName() << " create and open current device " << deviceClass;

		// m_pCapture = make_shared<CInstantCamera>(pTL->CreateDevice(devices[deviceIdx]));
		/* Open device. */
		// m_pCapture = make_shared<ITKDEVICE>(nullptr);
		res = ItkDevOpen(deviceIdx, ITKDEV_VAL_ACCESS_MODE_EXCLUSIVE, &m_pCapture);
		// res = ItkDevOpen(devIndex, ITKDEV_VAL_ACCESS_MODE_CONTROL, &hDev);
		CHECK(res);
		m_bOffline = false;
		// m_pCapture->Open();

		if (!open())
		{
			LogERROR << "Camera " << cameraName() << " open failed";
			return false;
		}
		setCameraStarted(true);
	}
	// catch (GenICam::GenericException &e)
	// {
	// 	LogERROR << "Camera " << cameraName() << ": " << e.GetDescription();
	// 	m_pException->setDescription(e.GetDescription());
	// 	return false;
	// }
	catch (...)
	{
		LogERROR << "Camera " << cameraName() << " has non-camera-API related error";
		m_pException->setDescription("Unexpected exception");
		return false;
	}

	return true;
}

bool ItekCamera::open()
{
	m_pException->reset();
	if (cameraOpened())
	{
		LogDEBUG << "Camera " << cameraName() << " has been opened already";
		return true;
	}

	if (m_pCapture == nullptr)
	{
		LogERROR << "Camera " << cameraName() << " has empty capture";
		return false;
	}

	try
	{
		// if (!m_pCapture->IsOpen())
		// {
		// 	LogERROR << "Camera " << cameraName() << " should start first";
		// 	return false;
		// }
		LogDEBUG << "open camera";
		if (dynamic_pointer_cast<ItekCameraConfig>(m_pConfig) == nullptr)
		{
			LogERROR << "Camera " << cameraName() << " has invalid configuration holder";
			return false;
		}

		// dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->setNodemap(m_pCapture->GetNodeMap());
		dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->setCameraHandle(m_pCapture);
		if (!dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->setConfig())
		{
			LogERROR << "Camera " << cameraName() << " set configuration failed";
			return false;
		}
		// start grabbing should be grabbed after all the camera has been set,
		// it can not be grabbed for each iteration, other wise the camera will get error and not be able to open
		// m_pCapture->StartGrabbing(GrabStrategy_LatestImageOnly);

		ITKFEATURE hFeature; /* Feature handle.							*/
		ITKSTATUS res = ITKSTATUS_OK;
		// Image width.
		int64_t nWidth = 0;
		// Image height.
		int64_t nHeight = 0;
		uint32_t pixelFormatLen = 16;
		char pixelFormat[16];

		res = ItkDevGetInt64(m_pCapture, "Width", &nWidth);
		CHECK(res);

		res = ItkDevGetInt64(m_pCapture, "Height", &nHeight);
		CHECK(res);

		res = ItkDevToString(m_pCapture, "PixelFormat", pixelFormat, &pixelFormatLen);
		CHECK(res);

		uint32_t dtype = CV_8UC1;
		uint32_t nFormat = ITKBUFFER_VAL_FORMAT_MONO8;
		m_vectorBuffer.clear();
		ITKBUFFER hBuffer;
		int numBuffers = dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->bufferNumbers();
		for (int index = 0; index < numBuffers; index++)
		{
			if (strcmp(pixelFormat, "Mono8") == 0)
			{
				dtype = CV_8UC1;
				nFormat = ITKBUFFER_VAL_FORMAT_MONO8;
				// res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_MONO8, &hBuffer);
				// CHECK(res);
			}
			// else if (strcmp(pixelFormat, "Mono10") == 0)
			// {
			// 	dtype = CV_16UC1;
			// nFormat = ITKBUFFER_VAL_FORMAT_MONO10;
			//     res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_MONO10, &hBuffer);
			//     CHECK(res);
			// }
			// else if (strcmp(pixelFormat, "Mono10Packed") == 0)
			// {
			// 	dtype = CV_16UC1;
			// nFormat = ITKBUFFER_VAL_FORMAT_MONO10PACKED;
			//     res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_MONO10PACKED, &hBuffer);
			//     CHECK(res);
			// }
			else if (strcmp(pixelFormat, "BayerGR8") == 0)
			{
				dtype = CV_8UC3;
				nFormat = ITKBUFFER_VAL_FORMAT_BAYER_GR8;
				// res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_GR8, &hBuffer);
				// CHECK(res);
			}
			else if (strcmp(pixelFormat, "BayerRG8") == 0)
			{
				dtype = CV_8UC3;
				nFormat = ITKBUFFER_VAL_FORMAT_BAYER_RG8;
				// res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_RG8, &hBuffer);
				// CHECK(res);
			}
			else if (strcmp(pixelFormat, "BayerGB8") == 0)
			{
				dtype = CV_8UC3;
				nFormat = ITKBUFFER_VAL_FORMAT_BAYER_GB8;
				// res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_GB8, &hBuffer);
				// CHECK(res);
			}
			else if (strcmp(pixelFormat, "BayerBG8") == 0)
			{
				dtype = CV_8UC3;
				nFormat = ITKBUFFER_VAL_FORMAT_BAYER_BG8;
				// res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_BG8, &hBuffer);
				// CHECK(res);
			}
			else if (strcmp(pixelFormat, "RGB8") == 0)
			{
				dtype = CV_8UC3;
				nFormat = ITKBUFFER_VAL_FORMAT_BGR888;
				// res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_BG8, &hBuffer);
				// CHECK(res);
			}
			// else if (strcmp(pixelFormat, "BayerGR10") == 0)
			// {
			// 	dtype = CV_10UC3;
			// nFormat = ITKBUFFER_VAL_FORMAT_BAYER_GR10;
			//     res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_GR10, &hBuffer);
			//     CHECK(res);
			// }
			// else if (strcmp(pixelFormat, "BayerRG10") == 0)
			// {
			// 	dtype = CV_10UC3;
			// nFormat = ITKBUFFER_VAL_FORMAT_BAYER_RG10;
			//     res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_RG10, &hBuffer);
			//     CHECK(res);
			// }
			// else if (strcmp(pixelFormat, "BayerGB10") == 0)
			// {
			// 	dtype = CV_10UC3;
			// nFormat = ITKBUFFER_VAL_FORMAT_BAYER_GB10;
			//     res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_GB10, &hBuffer);
			//     CHECK(res);
			// }
			// else if (strcmp(pixelFormat, "BayerBG10") == 0)
			// {
			// 	dtype = CV_10UC3;
			// nFormat = ITKBUFFER_VAL_FORMAT_BAYER_BG10;
			//     res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_BG10, &hBuffer);
			//     CHECK(res);
			// }
			// else if (strcmp(pixelFormat, "BayerGR10Packed") == 0)
			// {
			// 	dtype = CV_10UC3;
			// nFormat = ITKBUFFER_VAL_FORMAT_BAYER_GR10PACKED;
			//     res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_GR10PACKED, &hBuffer);
			//     CHECK(res);
			// }
			// else if (strcmp(pixelFormat, "BayerRG10Packed") == 0)
			// {
			// 	dtype = CV_10UC3;
			// nFormat = ITKBUFFER_VAL_FORMAT_BAYER_RG10PACKED;
			//     res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_RG10PACKED, &hBuffer);
			//     CHECK(res);
			// }
			// else if (strcmp(pixelFormat, "BayerGB10Packed") == 0)
			// {
			// 	dtype = CV_10UC3;
			// nFormat = ITKBUFFER_VAL_FORMAT_BAYER_GB10PACKED;
			//     res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_GB10PACKED, &hBuffer);
			//     CHECK(res);
			// }
			// else if (strcmp(pixelFormat, "BayerBG10Packed") == 0)
			// {
			// 	dtype = CV_10UC3;
			// 		nFormat = ITKBUFFER_VAL_FORMAT_BAYER_BG10PACKED;
			//     res = ItkBufferNew(nWidth, nHeight, ITKBUFFER_VAL_FORMAT_BAYER_BG10PACKED, &hBuffer);
			//     CHECK(res);
			// }
			else
			{
				// fprintf(stderr, "Camera does not support pixel format %s.", pixelFormat);
				LogERROR << "Camera does not support pixel format " << pixelFormat << ".";
				ItkManTerminate();
				// pressEnterToExit();
				// exit(EXIT_FAILURE);
				return false;
			}

			res = ItkBufferNew(nWidth, nHeight, nFormat, &hBuffer);
			CHECK(res);

			m_vectorBuffer.emplace_back(hBuffer);
		}

		/* Disable compress transfer mode */
		// res = ItkDevAllocFeature(m_pCapture, "CompressionTransfer", &hFeature);
		// CHECK(res);

		// res = ItkFeatureSetBool(hFeature, 0);
		// CHECK(res);

		// res = ItkDevFreeFeature(hFeature);
		// CHECK(res);

		/* Set aqcqusition framerate */
		// res = ItkDevAllocFeature(hDev, "AcquisitionLineRate", &hFeature);
		// CHECK(res);

		// double max_line_rate = 0, cur_line_rate = 0;
		// res = ItkFeatureGetDoubleMax(hFeature, &max_line_rate);
		// CHECK(res);

		// res = ItkFeatureGetDouble(hFeature, &cur_line_rate);
		// CHECK(res);

		// printf("line rate cur is: %lf, max is: %lf.\n", cur_line_rate, max_line_rate);

		// res = ItkDevFreeFeature(hFeature);
		// CHECK(res);

		/* Get stream count specified by the device handle. */
		// The number of data stream.
		uint32_t numStreams = 0;
		res = ItkDevGetStreamCount(m_pCapture, &numStreams);
		CHECK(res);

		if (numStreams == 0)
		{
			LogERROR << "Camera does not have image stream channel.";
			/* Before exiting a program, ItkManTerminate() should be called to release
			all IKapC related resources. */
			ItkManTerminate();
			// pressEnterToExit();
			// exit(EXIT_FAILURE);
			return false;
		}

		// ITKSTREAM			hStream;				                                            /* Stream handle.							*/
		/* Allocate stream handle for image grab. */
		res = ItkDevAllocStream(m_pCapture, 0, m_vectorBuffer[0], &m_pStream);
		if (m_pStream == nullptr)
		{
			return false;
		}
		CHECK(res);
		for (int index = 1; index < numBuffers; index++)
		{
			res = ItkStreamAddBuffer(m_pStream, m_vectorBuffer[index]);
			CHECK(res);
		}

		uint32_t xferMode = ITKSTREAM_VAL_TRANSFER_MODE_SYNCHRONOUS_WITH_PROTECT; /* Transfer image in synchronous with protect mode. */
		uint32_t autoClear = ITKSTREAM_VAL_AUTO_CLEAR_DISABLE;					  /* Transfer auto-clear enable.				*/
		uint32_t timeOut = -1;													  /* Image transfer timeout.					*/
		/* Set transfer mode which means the grab will not be flush a full buffer before an entire image
		is released by user. */
		res = ItkStreamSetPrm(m_pStream, ITKSTREAM_PRM_TRANSFER_MODE, &xferMode);
		CHECK(res);

		res = ItkStreamSetPrm(m_pStream, ITKSTREAM_PRM_AUTO_CLEAR, &autoClear);
		CHECK(res);

		res = ItkStreamSetPrm(m_pStream, ITKSTREAM_PRM_TIME_OUT, &timeOut);
		CHECK(res);

		uint32_t interTimeout = 3600000;
		// 设置行间超时时间
		//
		// Set packet inter timeout
		res = ItkStreamSetPrm(m_pStream, ITKSTREAM_PRM_GV_PACKET_INTER_TIMEOUT, &interTimeout);
		CHECK(res);

		// Register the callback which will be called when the device is removed
		// res = ItkDevRegisterCallback(hDev, “DeviceRemove”, removalCallbackFunction, context);
		// Check(res);

		// /* Register callback which will be called at the end of one image completely. */
		// res = ItkStreamRegisterCallback(hStream, ITKSTREAM_VAL_EVENT_TYPE_END_OF_FRAME, cbOnEndOfFrame, hBuffers);
		// CHECK(res);

		// /* Register callback which will be called at start of stream. */
		// res = ItkStreamRegisterCallback(hStream, ITKSTREAM_VAL_EVENT_TYPE_START_OF_STREAM, cbOnStartOfStream, NULL);
		// CHECK(res);

		// /* Register callback which will be called at end of stream. */
		// res = ItkStreamRegisterCallback(hStream, ITKSTREAM_VAL_EVENT_TYPE_END_OF_STREAM, cbOnEndOfStream, NULL);
		// CHECK(res);

		// /* Register callback which will be called at frame is lost */
		// res = ItkStreamRegisterCallback(hStream, ITKSTREAM_VAL_EVENT_TYPE_FRAME_LOST, cbOnFrameLost, NULL);
		// CHECK(res);

		// last_rcv_block_id = 0;
		// g_buffer_index = 0;

		res = ItkStreamStart(m_pStream, ITKSTREAM_CONTINUOUS);
		CHECK(res);

		m_pOverlapFrame = make_shared<Mat>(dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->overlap(),
										   nWidth, dtype, Scalar(255, 255, 255));

		setCameraOpened(true);
	}
	// catch (GenICam::GenericException &e)
	// {
	// 	LogERROR << "Camera " << cameraName() << ": " << e.GetDescription();
	// 	m_pException->setDescription(e.GetDescription());
	// 	return false;
	// }
	catch (...)
	{
		LogERROR << "Camera " << cameraName() << " has non-camera-API related error";
		m_pException->setDescription("Unexpected exception");
		return false;
	}

	return true;
}

bool ItekCamera::read(Mat &frame)
{
	m_pException->reset();
	if (!cameraStarted() || !cameraOpened())
	{
		LogERROR << "Make sure camera " << cameraName() << " is started before reading.";
		return false;
	}

	if (m_pCapture == nullptr)
	{
		LogERROR << "Camera " << cameraName() << " has empty capture";
		return false;
	}

	try
	{
		bool validFrame = m_pConfig->isBlockRead() ? blockRead(frame) : nonBlockRead(frame);
		m_iTotalFrames += validFrame ? 1 : 0;
		if (!validFrame)
		{
			return false;
		}
	}
	// catch (GenICam::GenericException &e)
	// {
	// 	LogERROR << "Camera " << cameraName() << ": " << e.GetDescription();
	// 	m_pException->setDescription(e.GetDescription());
	// 	return false;
	// }
	catch (...)
	{
		LogERROR << "Camera " << cameraName() << " has non-camera-API related error";
		m_pException->setDescription("Unexpected exception");
		return false;
	}
	return true;
}

bool ItekCamera::blockRead(Mat &frame)
{
	// we should have all validation done before calling this method
	lock_guard<mutex> lock(m_mutex);
	return nonBlockRead(frame);
}

bool ItekCamera::nonBlockRead(Mat &frame)
{
	// we should have all validation done before calling this method
	try
	{
		// create pylon image format converter and pylon image
		// CImageFormatConverter formatConverter;
		// CPylonImage pylonImage;

		// This smart pointer will receive the grab result data.
		// CGrabResultPtr ptrGrabResult;

		// Wait for an image to retrieve. we use timeout 10ms here! in case we need to retry
		// if (m_pCapture == nullptr || !m_pCapture->IsOpen())
		if (m_pCapture == nullptr)
		{
			LogERROR << "Camera " << cameraName() << " does not open correctly";
			return false;
		}
		// if (!m_pCapture->IsGrabbing())
		// {
		// 	LogERROR << "Camera " << cameraName() << " IsGrabbing() failed";
		// 	return false;
		// }

		ITKSTATUS res = ITKSTATUS_OK;
		unsigned bufferStatus = 0;
		int64_t nImageSize = 0;
		ITKBUFFER hBuffer = m_vectorBuffer[m_iCurFrameIndex];

		res = ItkBufferGetPrm(hBuffer, ITKBUFFER_PRM_STATE, &bufferStatus);
		CHECK(res);

		// 当图像缓冲区满或者图像缓冲区非满但是无法采集完整的一帧图像时。
		//
		// When buffer is full or buffer is not full but cannot grab a complete frame of image.
		bool bTimeLimit = true;
		int timeout = m_pConfig->timeout();
		if (timeout == -1)
		{
			bTimeLimit = false;
		}

		int numBuffers = dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->bufferNumbers();
		auto start = std::chrono::high_resolution_clock::now();
		// std::chrono::duration<double> diff = std::chrono::high_resolution_clock::now() - start;
		int timeRemaining = timeout;
		while (!bTimeLimit || timeRemaining >= 0)
		{
			if (true == m_bOffline)
			{
				break;
			}

			std::chrono::duration<double, std::milli> diff = std::chrono::high_resolution_clock::now() - start;
			timeRemaining = timeout - diff.count();

			ITKSTATUS res = ITKSTATUS_OK;
			unsigned bufferStatus = 0;
			int64_t nImageSize = 0;
			ITKBUFFER pSrcBuffer = m_vectorBuffer[m_iCurFrameIndex];

			res = ItkBufferGetPrm(pSrcBuffer, ITKBUFFER_PRM_STATE, &bufferStatus);
			CHECK(res);

			// 当图像缓冲区满或者图像缓冲区非满但是无法采集完整的一帧图像时。
			//
			// When buffer is full or buffer is not full but cannot grab a complete frame of image.
			if (bufferStatus == ITKBUFFER_VAL_STATE_FULL || bufferStatus == ITKBUFFER_VAL_STATE_UNCOMPLETED)
			//if (bufferStatus == ITKBUFFER_VAL_STATE_FULL)
			{
				// 保存图像。
				//
				// Save image.
				/*
				res = ItkBufferSave(hBuffer,g_saveFileName,ITKBUFFER_VAL_TIFF);
				CHECK(res);
				*/

				ITKFEATURE hFeature; /* Feature handle.							*/
				ITKSTATUS res = ITKSTATUS_OK;
				// Image width.
				int64_t nWidth = 0;
				// Image height.
				int64_t nHeight = 0;
				uint32_t pixelFormatLen = 16;
				char pixelFormat[16];

				res = ItkDevGetInt64(m_pCapture, "Width", &nWidth);
				CHECK(res);

				res = ItkDevGetInt64(m_pCapture, "Height", &nHeight);
				CHECK(res);

				res = ItkDevToString(m_pCapture, "PixelFormat", pixelFormat, &pixelFormatLen);
				CHECK(res);

				uint32_t nOutputPixelFormat = 0;
				uint32_t nConvertOps = 0;
				if (strcmp(pixelFormat, "Mono8") == 0)
				{
					nConvertOps = ITKBUFFER_VAL_FORMAT_MONO8;
					nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_MONO8;
				}
				// else if (strcmp(pixelFormat, "Mono10") == 0)
				// {
				// 	nConvertOps = ITKBUFFER_VAL_FORMAT_MONO10;
				// 	nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_MONO10;
				// }
				// else if (strcmp(pixelFormat, "Mono10Packed") == 0)
				// {
				// 	nConvertOps = ITKBUFFER_VAL_FORMAT_MONO10PACKED;
				// 	nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_MONO10PACKED;
				// }
				else if (strcmp(pixelFormat, "BayerGR8") == 0)
				{
					nConvertOps = ITKBUFFER_VAL_BAYER_GRBG;
					nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR888;
				}
				else if (strcmp(pixelFormat, "BayerRG8") == 0)
				{
					nConvertOps = ITKBUFFER_VAL_BAYER_RGGB;
					nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR888;
				}
				else if (strcmp(pixelFormat, "BayerGB8") == 0)
				{
					nConvertOps = ITKBUFFER_VAL_BAYER_GBRG;
					nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR888;
				}
				else if (strcmp(pixelFormat, "BayerBG8") == 0)
				{
					nConvertOps = ITKBUFFER_VAL_BAYER_BGGR;
					nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR888;
				}
				else if (strcmp(pixelFormat, "RGB8") == 0)
				{
					nConvertOps = ITKBUFFER_VAL_FORMAT_RGB888;
					nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR888;
				}
				// else if (strcmp(pixelFormat, "BayerGR10") == 0)
				// {
				// 	nConvertOps = ITKBUFFER_VAL_BAYER_GRBG;
				// 	nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR101010;
				// }
				// else if (strcmp(pixelFormat, "BayerRG10") == 0)
				// {
				// 	nConvertOps = ITKBUFFER_VAL_BAYER_RGGB;
				// 	nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR101010;
				// }
				// else if (strcmp(pixelFormat, "BayerGB10") == 0)
				// {
				// 	nConvertOps = ITKBUFFER_VAL_BAYER_GBRG;
				// 	nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR101010;
				// }
				// else if (strcmp(pixelFormat, "BayerBG10") == 0)
				// {
				// 	nConvertOps = ITKBUFFER_VAL_BAYER_BGGR;
				// 	nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR101010;
				// }
				// else if (strcmp(pixelFormat, "BayerGR10Packed") == 0)
				// {
				// 	nConvertOps = ITKBUFFER_VAL_BAYER_GRBG;
				// 	nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR101010;
				// }
				// else if (strcmp(pixelFormat, "BayerRG10Packed") == 0)
				// {
				// 	nConvertOps = ITKBUFFER_VAL_BAYER_RGGB;
				// 	nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR101010;
				// }
				// else if (strcmp(pixelFormat, "BayerGB10Packed") == 0)
				// {
				// 	nConvertOps = ITKBUFFER_VAL_BAYER_GBRG;
				// 	nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR101010;
				// }
				// else if (strcmp(pixelFormat, "BayerBG10Packed") == 0)
				// {
				// 	nConvertOps = ITKBUFFER_VAL_BAYER_BGGR;
				// 	nOutputPixelFormat = ITKBUFFER_VAL_FORMAT_BGR101010;
				// }
				else
				{
					// fprintf(stderr, "Camera does not support pixel format %s.", pixelFormat);
					LogERROR << "Camera does not support pixel format " << pixelFormat << ".";
					ItkManTerminate();
					// pressEnterToExit();
					// exit(EXIT_FAILURE);
					return false;
				}

				int overlap = dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->overlap();

				ITKBUFFER pDesBuffer = nullptr;
				void *pDst = nullptr;

				if (string(pixelFormat).find("Mono") == string::npos)
				{
					res = ItkBufferNew(nWidth, nHeight, nOutputPixelFormat, &pDesBuffer);
					CHECK(res);
					res = ItkBufferBayerConvert(pSrcBuffer, pDesBuffer, nConvertOps);
					CHECK(res);
					frame = Mat(nHeight + overlap, nWidth, CV_8UC3);
					memcpy(frame.data, m_pOverlapFrame->data, overlap * nWidth * 3); // copy overlapFrame to frame
					pDst = frame.data + overlap * nWidth * 3;
					// 读取缓冲区数据。
					//
					// Read buffer data.
					res = ItkBufferGetPrm(pDesBuffer, ITKBUFFER_PRM_SIZE, &nImageSize);
					CHECK(res);
					res = ItkBufferRead(pSrcBuffer, 0, pDst, (uint32_t)nImageSize);
					CHECK(res);
					memcpy(m_pOverlapFrame->data, frame.data + (frame.rows - overlap) * nWidth * 3, overlap * nWidth * 3); // copy new overFrame to overFrame
				}
				else // Mono
				{
					pDesBuffer = pSrcBuffer;
					frame = Mat(nHeight + overlap, nWidth, CV_8UC1);
					memcpy(frame.data, m_pOverlapFrame->data, overlap * nWidth * 1); // copy overlapFrame to frame
					pDst = frame.data + overlap * nWidth * 1;
					// 读取缓冲区数据。
					//
					// Read buffer data.
					res = ItkBufferGetPrm(pDesBuffer, ITKBUFFER_PRM_SIZE, &nImageSize);
					CHECK(res);
					res = ItkBufferRead(pSrcBuffer, 0, pDst, (uint32_t)nImageSize);
					CHECK(res);
					memcpy(m_pOverlapFrame->data, frame.data + (frame.rows - overlap) * nWidth * 1, overlap * nWidth * 1); // copy new overFrame to overFrame
				}

				if (dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->transposeEnable())
				{
					transpose(frame, frame);
				}

				m_iCurFrameIndex++;
				m_iCurFrameIndex = m_iCurFrameIndex % numBuffers;

				bufferStatus = ITKBUFFER_VAL_STATE_EMPTY;
				res = ItkBufferSetPrm(pSrcBuffer, ITKBUFFER_PRM_STATE, &bufferStatus);
				CHECK(res);
				return true;
			}
			// else if (bufferStatus == ITKBUFFER_VAL_STATE_UNCOMPLETED)
			// {
			// 	LogERROR << "uncompleted image";
			// 	m_iCurFrameIndex++;
			// 	m_iCurFrameIndex = m_iCurFrameIndex % numBuffers;

			// 	bufferStatus = ITKBUFFER_VAL_STATE_EMPTY;
			// 	res = ItkBufferSetPrm(pSrcBuffer, ITKBUFFER_PRM_STATE, &bufferStatus);
			// 	CHECK(res);

			// 	return false;
			// }
			else if (bufferStatus == ITKBUFFER_VAL_STATE_EMPTY)
			{
				// has no image
				continue;
			}
		}

		if (timeRemaining < 0)
		{
			LogINFO << "get image timeout, has no image";
			// return false;
		}

		// Check that grab results are waiting if timeout is infinite(-1)
		// if ((m_pConfig->timeout() == -1) && m_pCapture->GetGrabResultWaitObject().Wait(0))
		// {
		// 	LogDEBUG << "Camera " << cameraName() << " check if there are images still in the camera queue";
		// 	// All triggered images are still waiting in the output queue
		// 	// they are now retrieved.
		// 	// The grabbing continues in the background, e.g. when using hardware trigger mode,
		// 	// as long as the grab engine does not run out of buffers.
		// 	int nBuffersInQueue = 0;
		// 	CGrabResultPtr ptrClearGrabResult;
		// 	while (m_pCapture->RetrieveResult(0, ptrClearGrabResult, TimeoutHandling_Return))
		// 	{
		// 		nBuffersInQueue++;
		// 	}
		// 	LogDEBUG << "Camera " << cameraName() << " cleared " << nBuffersInQueue << " images from camera queue";
		// }

		// // if it is soft trigger, send trigger command first
		// if (dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->softTriggerEnable() && !sendSoftwareTriggerCommand())
		// {
		// 	return false;
		// }

		// m_pCapture->RetrieveResult(m_pConfig->timeout(), ptrGrabResult, TimeoutHandling_ThrowException);

		// Image grabbed successfully?
		// if (ptrGrabResult->GrabSucceeded())
		// {
		// 	// Convert the grabbed buffer to pylon image
		// 	LogINFO << "Camera " << cameraName() << " get image successfully";
		// 	if (dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->pixelFormat() != "Mono8")
		// 	{
		// 		formatConverter.OutputPixelFormat = PixelType_BGR8packed;
		// 		formatConverter.Convert(pylonImage, ptrGrabResult);
		// 		frame = Mat(ptrGrabResult->GetHeight(), ptrGrabResult->GetWidth(), CV_8UC3, (uint8_t *)pylonImage.GetBuffer()).clone();
		// 	}
		// 	else
		// 	{
		// 		formatConverter.OutputPixelFormat = PixelType_Mono8;
		// 		formatConverter.Convert(pylonImage, ptrGrabResult);
		// 		frame = Mat(ptrGrabResult->GetHeight(), ptrGrabResult->GetWidth(), CV_8UC1, (uint8_t *)pylonImage.GetBuffer()).clone();
		// 	}
		// 	return true;
		// }
	}
	// catch (GenICam::GenericException &e)
	// {
	// 	LogERROR << "Camera " << cameraName() << ": " << e.GetDescription();
	// 	m_pException->setDescription(e.GetDescription());
	// 	return false;
	// }
	catch (...)
	{
		LogERROR << "Camera " << cameraName() << " has non-camera-API related error";
		m_pException->setDescription("Unexpected exception");
		return false;
	}

	LogERROR << "Camera " << cameraName() << " got invalid frame";
	return false;
}

// bool ItekCamera::sendSoftwareTriggerCommand()
// {
// 	LogINFO << "Camera " << cameraName() << " send soft trigger start...";
// 	try
// 	{
// 		m_pCapture->ExecuteSoftwareTrigger();
// 		this_thread::sleep_for(chrono::milliseconds(dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->softTriggerWaitTime()));
// 	}
// 	catch (GenICam::GenericException &e)
// 	{
// 		LogERROR << "Camera " << cameraName() << ": " << e.GetDescription();
// 		m_pException->setDescription(e.GetDescription());
// 		return false;
// 	}
// 	catch (...)
// 	{
// 		LogERROR << "Camera " << cameraName() << " has non-camera-API related error";
// 		m_pException->setDescription("Unexpected exception");
// 		return false;
// 	}
// 	LogINFO << "Camera " << cameraName() << " send soft trigger end";

// 	return true;
// }

void ItekCamera::release()
{

	m_pException->reset();
	if (m_pCapture != nullptr)
	{
		try
		{
			if (cameraOpened())
			{
				// Stop grabbing images.
				ItkStreamStop(m_pStream);

				// 清除回调函数。
				//
				// Unregister callback functions.
				// UnRegisterCallback();

				/* ... Deregister the removal callback. */
				// res = ItkDevUnregisterCallback(hDev, "DeviceRemove");

				// Free data stream and buffers.
				// cout << "remove m_vectorBuffer:" << m_vectorBuffer.size() << endl;
				for (auto it = m_vectorBuffer.begin(); it != m_vectorBuffer.end(); it++)
				{
					ItkStreamRemoveBuffer(m_pStream, *it);
					ItkBufferFree(*it);
				}
				// std::vector<ITKBUFFER>().swap(m_vectorBuffer); // why cause signal 11???
				m_vectorBuffer.clear();
				//cout << "m_vectorBuffer:" << m_vectorBuffer.size() << endl;
				ItkDevFreeStream(m_pStream);
				m_pStream = nullptr;
				m_iCurFrameIndex = 0;

				// m_pCapture->StopGrabbing();
				setCameraOpened(false);
			}
		}
		// catch (GenICam::GenericException &e)
		// {
		// 	LogERROR << "Camera " << cameraName() << ": " << e.GetDescription();
		// 	m_pException->setDescription(e.GetDescription());
		// }
		catch (...)
		{
			LogERROR << "Camera " << cameraName() << " has non-camera-API related error";
			m_pException->setDescription("Unexpected exception");
		}
	}
}

bool ItekCamera::restart()
{
	if (m_pCapture == nullptr)
	{
		LogERROR << "Camera " << cameraName() << " has empty capture";
		return false;
	}

	release();
	if (!m_pException->getDescription().empty())
	{
		// we do not need to log exception which already logged in release
		LogERROR << "Camera " << cameraName() << " release caught exception";
		return false;
	}

	m_pException->reset();
	try
	{
		ITKSTATUS res = ITKSTATUS_OK;
		// Close camera device.
		res = ItkDevClose(m_pCapture);
		CHECK(res);
		/* Shut down the IKapC runtime system. Don't call any IKapC method after
		calling ItkManTerminate(). */
		ItkManTerminate();
		m_pCapture = nullptr;
		dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->setCameraHandle(m_pCapture);

		// m_pCapture->Close();
		// m_pCapture->DestroyDevice();
	}
	// catch (GenICam::GenericException &e)
	// {
	// 	LogERROR << "Camera " << cameraName() << ": " << e.GetDescription();
	// 	m_pException->setDescription(e.GetDescription());
	// 	return false;
	// }
	catch (...)
	{
		LogERROR << "Camera " << cameraName() << " has non-camera-API related error";
		m_pException->setDescription("Unexpected exception");
		return false;
	}
	setCameraStarted(false);

	std::this_thread::sleep_for(std::chrono::milliseconds(CAMERA_RESTART_SLEEP_INTERVAL));

	return start();
}

ItekCamera::~ItekCamera()
{
	release();
	if (m_pCapture != nullptr)
	{
		try
		{
			ITKSTATUS res = ITKSTATUS_OK;
			// Close camera device.
			res = ItkDevClose(m_pCapture);
			CHECK(res);
			/* Shut down the IKapC runtime system. Don't call any IKapC method after
			calling ItkManTerminate(). */
			ItkManTerminate();
			m_pCapture = nullptr;
			dynamic_pointer_cast<ItekCameraConfig>(m_pConfig)->setCameraHandle(m_pCapture);

			// m_pCapture->Close();
			// m_pCapture->DestroyDevice();
		}
		// catch (GenICam::GenericException &e)
		// {
		// 	LogERROR << "Camera " << cameraName() << ": " << e.GetDescription();
		// 	// we will not override the last exception in when calling release()
		// 	if (m_pException->getDescription().empty())
		// 	{
		// 		m_pException->setDescription(e.GetDescription());
		// 	}
		// }
		catch (...)
		{
			LogERROR << "Camera " << cameraName() << " has non-camera-API related error";
			// we will not override the last exception in when calling release()
			if (m_pException->getDescription().empty())
			{
				m_pException->setDescription("Unexpected exception");
			}
		}
	}
	setCameraStarted(false);
}
