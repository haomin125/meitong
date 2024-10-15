#ifndef OPENCV_UTILS_H
#define OPENCV_UTILS_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

namespace opencv_utils
{
	/////////////////////////////////////////////////////////////////////////////////
	//  Name: saveImageInTemp
	//  Description: save image file into /opt/history/temp, report error if directory
	//               does not exists, file name will add the current time by default
	//  Parameters: 
	//     string: file name
	//     cv::Mat: image
	//     int: index which will be added between filename and current time if defined
	//     string: file extension, which supported by opencv imwrite
	//  Output: a image file should be created under /opt/history/temp
	//  Return:
	//     boolean: true if save file successful
	////////////////////////////////////////////////////////////////////////////////
	bool saveImageInTemp(const std::string &fileName, const cv::Mat &image, const int idx = -1, const std::string &ext = "png");

	/////////////////////////////////////////////////////////////////////////////////
	//  Name: saveImageInTest
	//  Description: save image file into file path, create directory if it
	//               does not exists, file name will not attache the current time
	//               if the last flag is false
	//  Parameters: 
	//     string: file path
	//     string: file name
	//     cv::Mat: image
	//     bool: true if need attach the current time in file name, default is false
	//     int: index which will be added between filename and current time if defined
	//     string: file extension, which supported by opencv imwrite
	//  Output: a image file should be created under file path
	//  Return:
	//     boolean: true if save file successful
	////////////////////////////////////////////////////////////////////////////////
	bool saveImageInTest(const std::string &filePath, const std::string &fileName, const cv::Mat &image, const bool attacheTime = false, const int idx = -1, const std::string &ext = "png");

	namespace {
		// private method for save images
		bool saveImage(const std::string &filePath, const std::string &fileName, const cv::Mat &image, const bool createDirectory = false);
	}

}  // namespace opencv_utils

#endif // OPENCV_UTILS_H