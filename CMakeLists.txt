cmake_minimum_required(VERSION 3.16)
project(gtfs_graph_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(gtfs_graph
    src/main.cpp
    src/TransitGraph.cpp
    src/algorithms/DfsSearch.cpp
    src/algorithms/DijkstraSearch.cpp
)

target_include_directories(gtfs_graph PRIVATE include)
target_link_libraries(gtfs_graph PRIVATE nlohmann_json::nlohmann_json)
