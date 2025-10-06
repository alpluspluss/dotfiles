cmake_minimum_required(VERSION 3.25)
project(install-app VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LibArchive REQUIRED)

add_executable(install-app src/main.cc)
target_link_libraries(install-app PRIVATE LibArchive::LibArchive)

install(TARGETS install-app DESTINATION bin)
