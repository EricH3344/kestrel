cmake_minimum_required(VERSION 3.21)

foreach(required_variable OPENCV_SOURCE_DIR OPENCV_DOWNLOAD_DIR OPENCV_VERSION OPENCV_SHA256)
    if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "FetchOpenCV.cmake requires ${required_variable}")
    endif()
endforeach()

set(opencv_url "https://github.com/opencv/opencv/archive/refs/tags/${OPENCV_VERSION}.tar.gz")
set(opencv_archive "${OPENCV_DOWNLOAD_DIR}/opencv-${OPENCV_VERSION}.tar.gz")
set(opencv_root "${OPENCV_SOURCE_DIR}/opencv-${OPENCV_VERSION}")

file(MAKE_DIRECTORY "${OPENCV_DOWNLOAD_DIR}")

if (EXISTS "${opencv_archive}")
    file(SHA256 "${opencv_archive}" existing_hash)
    if (NOT "${existing_hash}" STREQUAL "${OPENCV_SHA256}")
        message(STATUS "Discarding OpenCV archive with an unexpected checksum")
        file(REMOVE "${opencv_archive}")
    endif()
endif()

if (NOT EXISTS "${opencv_archive}")
    message(STATUS "Downloading OpenCV ${OPENCV_VERSION}")
    file(DOWNLOAD
        "${opencv_url}"
        "${opencv_archive}"
        EXPECTED_HASH "SHA256=${OPENCV_SHA256}"
        STATUS download_status
        SHOW_PROGRESS
        TLS_VERIFY ON
    )
    list(GET download_status 0 download_code)
    list(GET download_status 1 download_message)
    if (NOT download_code EQUAL 0)
        file(REMOVE "${opencv_archive}")
        message(FATAL_ERROR "Could not download OpenCV: ${download_message}")
    endif()
endif()

if (EXISTS "${opencv_root}/CMakeLists.txt")
    message(STATUS "Using extracted OpenCV ${OPENCV_VERSION} source")
    return()
endif()

file(REMOVE_RECURSE "${OPENCV_SOURCE_DIR}")
file(MAKE_DIRECTORY "${OPENCV_SOURCE_DIR}")
message(STATUS "Extracting OpenCV ${OPENCV_VERSION}")
file(ARCHIVE_EXTRACT
    INPUT "${opencv_archive}"
    DESTINATION "${OPENCV_SOURCE_DIR}"
)

if (NOT EXISTS "${opencv_root}/CMakeLists.txt")
    message(FATAL_ERROR "The OpenCV archive did not contain the expected source directory")
endif()
