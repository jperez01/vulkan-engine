#pragma once

#include "vk_types.h"
#include "vk_engine.h"

class VulkanEngine;

namespace vkutil {
    bool load_image_from_file(VulkanEngine& engine, const char* file, AllocatedImage& outImage);

    bool load_image_from_asset(VulkanEngine& engine, const char* filename, AllocatedImage& outImage);

    AllocatedImage upload_image(int texWidth, int texHeight, VkFormat image_format, VulkanEngine& engine, AllocatedBuffer& stagingBuffer);
};

