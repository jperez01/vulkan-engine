set(CMAKE_CXX_STANDARD 17)

add_library(assetlib STATIC
    "asset_loader.h"
    "asset_loader.cpp"
    "texture_asset.h"
    "texture_asset.cpp"
)

target_include_directories(assetlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(assetlib PRIVATE json lz4)