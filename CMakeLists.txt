cmake_minimum_required(VERSION 3.10)
project(ROOT_Project)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the ROOT package
find_package(ROOT REQUIRED)

# Find the Eigen3 package
find_package(Eigen3 REQUIRED)

# Find the Boost program_options package
find_package(Boost REQUIRED COMPONENTS program_options)

# Build professor source files into static library
file(GLOB PROF_SRCS "professor/src/*.cc")
add_library(prof2 STATIC ${PROF_SRCS})
target_link_libraries(prof2 Eigen3::Eigen)
target_include_directories(prof2 PUBLIC professor/include)

# Parametrization
add_executable(main main.cxx)
target_include_directories(main PUBLIC ${ROOT_INCLUDE_DIRS})
target_link_libraries(main prof2 ${ROOT_LIBRARIES} Boost::program_options)

# locate all bins in file
add_executable(scan scan.cxx)
target_include_directories(scan PUBLIC ${ROOT_INCLUDE_DIRS})
target_link_libraries(scan ${ROOT_LIBRARIES} Boost::program_options)
