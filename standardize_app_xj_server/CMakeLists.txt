# cmakelists: standardize_app_xj_server

cmake_minimum_required (VERSION 3.8)

project (standardize_app_xj_server VERSION 1.0.0)

enable_testing()

# version
configure_file (version.h.in version.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

#===================================================================== OpenCV
find_package (OpenCV REQUIRED)
include_directories (${OpenCV_INCLUDE_DIRS})
if(UNIX)
    include_directories ("/usr/local/include/opencv" "/usr/local/include/opencv2")
    link_directories("/usr/local/lib")
    # to install opencv go with the bash here: https://github.com/milq/milq/blob/master/scripts/bash/install-opencv.sh
    # set(OpenCV_LIBS opencv_core opencv_imgproc opencv_calib3d opencv_video opencv_features2d opencv_ml opencv_highgui opencv_objdetect opencv_imgcodecs )
endif()
#===================================================================== OpenCV

#===================================================================== Boost
if(UNIX)
    set(BOOST_LIBRARYDIR "/usr/local/lib")
    if(UBUNTU20)
        find_package(Boost 1.68.0 REQUIRED)
    else()
        find_package(Boost 1.68.0 REQUIRED COMPONENTS system filesystem thread)
    endif()
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
else()
    set(Boost_USE_STATIC_LIBS ON)
    find_package(Boost 1.68.0 REQUIRED COMPONENTS system filesystem thread)
    include_directories ("$ENV{BOOST_ROOT}")
    link_directories ("$ENV{BOOST_ROOT}/lib64-msvc-14.1")
endif()
#===================================================================== Boost


if(UNIX)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -pthread")
    set (INSTALL_CONFIG_DIR /opt/config)
    # the sequence is really IMPORTANT
    # depency libs should be put after
    if(UBUNTU20)
        set (XJ_SERVER_LIBS product_line board view target_object bound_box camera camera_manager config classifier_result detector io_card_manager io_manager plctag logger thread_pool tracker shared_utils apb-web-server web-server database workflow customized_json_config opencv_utils)
    else()
        set (XJ_SERVER_LIBS product_line board view target_object bound_box camera camera_manager config classifier_result detector io_card_manager io_manager plctag logger thread_pool tracker shared_utils apb-web-server web-server database workflow customized_json_config opencv_utils)
    endif()
else()
    set (INSTALL_CONFIG_DIR C:\\opt\\config)
    set (XJ_SERVER_LIBS product_line board view target_object bound_box camera camera_manager config classifier_result detector io_manager plctag logger thread_pool tracker shared_utils web-server database workflow customized_json_config opencv_utils utility)
endif()

#===================================================================== itek-camera
if(UNIX)
    set(ITEK_LIBS IKapC)
    set(ITEK_INCLUDE "/opt/Itek/IKap/Include")
    set(ITEK_LIBS_DIR "/opt/Itek/IKap/Lib/lib64")
endif()
include_directories (${ITEK_INCLUDE})
link_directories (${ITEK_LIBS_DIR})
#===================================================================== itek-camera

#===================================================================== Haikang-camera
if(UNIX)
	set (HAIKANG_LIBS_DIR "/opt/MVS/lib/64/")
	set (HAIKANG_INCLUDE "/opt/MVS/include/")
else()
	set (HAIKANG_LIBS_DIR "$ENV{HAIKANG_ROOT}/Development/Libraries/win64")
	set (HAIKANG_INCLUDE "$ENV{HAIKANG_ROOT}/Development/Includes")
endif()
set (HAIKANG_LIBS MvCameraControl)

include_directories (${HAIKANG_INCLUDE})
link_directories (${HAIKANG_LIBS_DIR})

set (XJ_SERVER_LIBS ${XJ_SERVER_LIBS} camera_haikang config_haikang)
#===================================================================== Haikang-camera

#===================================================================== IOCard
if(UNIX)
	set(BIODAQ_LIBS biodaq biodaqutil)
	set(IOCARD_LIBS_DIR "/opt/advantech/libs")
	set(IOCARD_INCLUDE "/opt/advantech/inc")

	include_directories(${IOCARD_INCLUDE})
    link_directories(${IOCARD_LIBS_DIR})
endif()
#===================================================================== IOCard

#===================================================================== DeepLearning
if(UNIX)
    if(ONNX)
        link_directories("/usr/local/onnxruntime-linux-x64-1.12.0/lib")
    endif()
    if(LIBTORCH)
        link_directories("/usr/local/cuda/lib64")
        set(Torch_DIR "/usr/local/libtorch/share/cmake/Torch")
        find_package(Torch REQUIRED)
    endif()
    if(TENSORRT)
        link_directories("/usr/local/cuda/lib64") 
        link_directories("/usr/local/TensorRT-8.2.2.1/lib")
    endif()
endif()
#===================================================================== DeepLearning

if(UNIX)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNO_CLASSIFY")
	message(STATUS "===============" ${CMAKE_CXX_FLAGS} "===============")
endif()

if(UNIX)
	include_directories ("/opt/include")
	link_directories ("/opt/lib")
else()
	include_directories ("C:\\opt\\include")
	link_directories ("C:\\opt\\lib")
endif()


set(SRC_FILES main.cpp itek_camera_config.cpp itek_camera.cpp xj_app_server.cpp xj_app_config.cpp xj_app_tracker.cpp xj_app_detector.cpp xj_app_workflow.cpp xj_app_io_manager.cpp xj_app_web_server.cpp utils.cpp xj_app_running_result.cpp xj_app_database.cpp xj_app_json_config.cpp)

add_executable (xjserver ${SRC_FILES})
target_link_libraries (xjserver ${XJ_SERVER_LIBS} ${Boost_LIBRARIES} ${OpenCV_LIBS})

if(UNIX)
    if(UBUNTU20)
        target_link_libraries (xjserver -ldl -lstdc++fs -lboost_system -lxj_algorithm)
    else()
        target_link_libraries (xjserver -ldl -lstdc++fs -lxj_algorithm)
    endif()
endif()

#===================================================================== DeepLearning
if(UNIX)
    if(ONNX)
        target_link_libraries (xjserver onnxruntime)
    endif()
    if(LIBTORCH)
        target_link_libraries (xjserver ${TORCH_LIBRARIES})
    endif()
    if(TENSORRT)
        target_link_libraries(xjserver nvinfer cudart)
    endif()
endif()
#===================================================================== DeepLearning

#===================================================================== Haikang-camera
target_link_libraries (xjserver ${HAIKANG_LIBS})
target_include_directories (xjserver PRIVATE ${HAIKANG_INCLUDE_DIR})
#===================================================================== Haikang-camera

#===================================================================== Itek-camera
target_link_libraries (xjserver ${ITEK_LIBS})
target_include_directories (xjserver PRIVATE ${ITEK_INCLUDE})
#===================================================================== Itek-camera

#===================================================================== IOCard
if(UNIX)
	target_link_libraries (xjserver ${BIODAQ_LIBS})
	target_include_directories (xjserver PRIVATE ${IOCARD_INCLUDE})
endif()
#===================================================================== IOCard

#===================================================================== Boost
target_include_directories (xjserver PRIVATE ${Boost_INCLUDE_DIR})
#===================================================================== Boost

add_subdirectory (test)
add_test (NAME app_test COMMAND app_test)

# install configuration file
install (FILES standardize_app_config.json configuration.json DESTINATION ${INSTALL_CONFIG_DIR})
