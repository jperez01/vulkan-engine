// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>

//we will add our main reusable types here
struct AllocatedBuffer {
	VkBuffer m_buffer;
	VmaAllocation m_allocation;
};

struct AllocatedBufferUntyped {
	VkBuffer _buffer{};
	VmaAllocation _allocation{};
	VkDeviceSize _size{0};
	VkDescriptorBufferInfo get_info(VkDeviceSize offset = 0);
};

struct AllocatedImage {
	VkImage m_image;
	VmaAllocation m_allocation;
	VkImageView m_defaultView;
	int mipLevels;
};

struct Texture {
	AllocatedImage image;
	VkImageView imageView;

	std::string type;
	std::string path;
	bool isLoaded = false;
};