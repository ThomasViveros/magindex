#[[
cmake -S . -B build `
    -DCMAKE_TOOLCHAIN_FILE="C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static
]]


cmake_minimum_required(VERSION 3.31)
project(Magindex)

set(CMAKE_CXX_STANDARD 20)

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(ZLIB REQUIRED)

add_library(Magindex STATIC
        src/api/api_interface.cpp
        src/importer/scryfall_client.cpp
        src/processor/scryfall_processor.cpp
        src/importer/scryfall_importer.cpp
        src/storage/sqlite/sqlite_storage.cpp)

target_include_directories(Magindex
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(Magindex
        PUBLIC
        CURL::libcurl
        nlohmann_json::nlohmann_json
        SQLiteCpp
        ZLIB::ZLIB
)
