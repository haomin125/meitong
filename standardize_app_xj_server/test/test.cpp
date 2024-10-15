
#include <sstream>
#include <chrono>

#include <experimental/filesystem>
#include <opencv2/opencv.hpp>
#include "logger.h"

/////////////////////// MACROS ///////////////////////
////////// Macros for product specific ///////////////
////////// change it with different product //////////

///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
/////////////////////// END ///////////////////////////

using namespace cv;
using namespace std;

bool extractImageFrom(const Mat &frame, Mat &cropped, int &top, int &bottom, int &left, int &right)
{
	// app need to write test method here
	cropped = frame;
	top = bottom = left = right = 0;

	return true;
}

int main(int argc, char const *argv[])
{
	if (argc < 3 || !experimental::filesystem::exists(argv[1]) || !experimental::filesystem::exists(argv[2]))
	{
		cout << "Usage: app_test image_file_path cropped_image_file_path" << endl;
		return 0;
	}

	std::cout << "App test start...\n";

	//create log file
	Logger::instance().setFileName("./app_test.log");
	Logger::instance().setLoggerLevel(LogLevel::logINFO);

	// add code to test image processing (cropped part)
	String images_path = argv[1];
	vector<String> images;

	Mat src, dst;
	int bottom, top, left, right;

	cv::glob(images_path, images, false);

	for (int i = 0; i < images.size(); i++)
	{
		stringstream iss;
		iss << argv[2] << "/" << i << ".png";
		src = cv::imread(images[i]);
		if (!src.empty())
		{
			auto startTime = chrono::system_clock::now();
			if (!extractImageFrom(src, dst, bottom, top, left, right))
			{
				LogERROR << "extract image process part failed";
				continue;
			}
			auto endTime = chrono::system_clock::now();
			imwrite(iss.str(), dst);
			LogINFO << "process time in ms: " << chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
		}
	}

	std::cout << "App test end...\n";

	return 0;
}
