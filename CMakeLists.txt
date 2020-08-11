cmake_minimum_required(VERSION 2.8)
project(DLMO)

set(CMAKE_CXX_STANDARD 14)

find_package(CUDA)

include_directories(src /usr/local/cuda/include /usr/local/cuda/samples/common/inc)
link_directories(/usr/local/cuda/lib64/)

add_executable(dlmo main.cpp)
add_executable(pci-test pci-test.cpp)
target_link_libraries(pci-test -lcudart -lcurand)