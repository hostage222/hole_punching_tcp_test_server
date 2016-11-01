project(test_server)
cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (CMAKE_BUILD_TYPE==Debug)
  add_definitions(-D_GLIBCXX_DEBUG)
ENDIF()

find_package(Boost 1.40 COMPONENTS system date_time REQUIRED)
IF (UNIX)
  find_package(Threads)
ENDIF()

include_directories(${Boost_INCLUDE_DIR})

aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})

target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES})

IF (WIN32)
  target_link_libraries(${PROJECT_NAME} ws2_32 wsock32)
  set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-D_WIN32_WINNT=0x0501")
  add_definitions(-D_WIN32_WINNT=0x0501)
ELSEIF (UNIX)
  target_link_libraries(${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})
ENDIF()
