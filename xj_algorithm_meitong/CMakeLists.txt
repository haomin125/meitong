project(xj_algorithm)
cmake_minimum_required (VERSION 3.8)

find_package (OpenCV REQUIRED)
include_directories (${OpenCV_INCLUDE_DIRS})

set(SRC_FILES xj_app_algorithm.cpp xj_algorithm.cpp utils.cpp)


# target_link_libraries (xjserver ${XJ_SERVER_LIBS} ${Boost_LIBRARIES} ${OpenCV_LIBS} nvinfer cudart)

#DL-tensorrt
    if(UNIX)
        include_directories(/usr/local/cuda/include)
        link_directories(/usr/local/cuda/lib64)
        include_directories(/usr/local/TensorRT-8.5.1.7/include)
        link_directories(/usr/local/TensorRT-8.5.1.7/lib)
        include_directories ("./tensorrt/")
        enable_language(CUDA)
        option(CUDA_USE_STATIC_CUDA_RUNTIME OFF)
        # file(GLOB_RECURSE SRCS ${CMAKE_CURRENT_SOURCE_DIR}/tensorrt/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/tensorrt/*.cu)
        file(GLOB_RECURSE SRCS ${CMAKE_CURRENT_SOURCE_DIR}/tensorrt/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/tensorrt/*.h)
        set(SRC_FILES ${SRC_FILES} ${SRCS})
    endif()


link_directories(xj_algorithm)
add_library(xj_algorithm SHARED ${SRC_FILES})
target_link_libraries(xj_algorithm ${OpenCV_LIBS} nvinfer cudart)
