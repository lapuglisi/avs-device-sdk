cmake_minimum_required(VERSION 3.1)

include(${AVS_CMAKE_BUILD}/BuildDefaults.cmake)

add_library(acsdkAPLCapabilityCommonInterfacesTestUtils INTERFACE)
target_include_directories(acsdkAPLCapabilityCommonInterfacesTestUtils INTERFACE include)