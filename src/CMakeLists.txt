
# Add source to this project's executable.
add_executable(vulkan_guide
    main.cpp
    vk_engine.cpp
    vk_engine.h
    vk_types.h
    vk_initializers.cpp
    vk_initializers.h
    vk_pipeline.h
    vk_pipeline.cpp
    vk_textures.h
    vk_textures.cpp
    vk_mesh.h
    vk_mesh.cpp
    vk_model.h
    vk_model.cpp
    ./utils/camera.h
    ./utils/camera.cpp
    ./utils/vk_descriptor.h
    ./utils/vk_descriptor.cpp)

set_property(TARGET vulkan_guide PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vulkan_guide>")

set(ASSIMP_LIB "${PROJECT_SOURCE_DIR}/lib/assimp.lib")
target_include_directories(vulkan_guide PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
${PROJECT_SOURCE_DIR}/include)
target_link_libraries(vulkan_guide vkbootstrap vma glm tinyobjloader imgui stb_image assetlib
    Vulkan::Vulkan sdl2 ${ASSIMP_LIB})

add_dependencies(vulkan_guide Shaders)
