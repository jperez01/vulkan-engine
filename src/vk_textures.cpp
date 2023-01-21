#include <vk_textures.h>
#include <iostream>

#include <vk_initializers.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "asset_loader.h"
#include "texture_asset.h"

bool vkutil::load_image_from_file(VulkanEngine& engine, const char* file, AllocatedImage& outImage) {
    int texWidth, texHeight, texChannels;

    stbi_uc* pixels = stbi_load(file, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        std::cout << "Failed to load texture file " << file << std::endl;
        return false;
    }

    void* pixel_ptr = pixels;
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    VkFormat image_format = VK_FORMAT_R8G8B8A8_SRGB;

    AllocatedBuffer stagingBuffer = engine.create_buffer(imageSize, 
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void* data;
    vmaMapMemory(engine.m_allocator, stagingBuffer.m_allocation, &data);

    memcpy(data, pixel_ptr, static_cast<size_t>(imageSize));

    vmaUnmapMemory(engine.m_allocator, stagingBuffer.m_allocation);
    stbi_image_free(pixels);

    VkExtent3D imageExtent;
    imageExtent.width = static_cast<uint32_t>(texWidth);
    imageExtent.height = static_cast<uint32_t>(texHeight);
    imageExtent.depth = 1;

    VkImageCreateInfo dimg_info = vkinit::image_create_info(image_format,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);

    AllocatedImage newImage;

    VmaAllocationCreateInfo dimg_allocinfo = {};
    dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(engine.m_allocator, &dimg_info, &dimg_allocinfo, &newImage.m_image, &newImage.m_allocation, nullptr);

    engine.immediate_submit([&](VkCommandBuffer cmd) {
        VkImageSubresourceRange range;
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        VkImageMemoryBarrier imageBarrier_toTransfer = {};
        imageBarrier_toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

        imageBarrier_toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier_toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier_toTransfer.image = newImage.m_image;
        imageBarrier_toTransfer.subresourceRange = range;

        imageBarrier_toTransfer.srcAccessMask = 0;
        imageBarrier_toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toTransfer);

        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;

        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = imageExtent;

        vkCmdCopyBufferToImage(cmd, stagingBuffer.m_buffer, newImage.m_image, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        VkImageMemoryBarrier toReadable = imageBarrier_toTransfer;

        toReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        toReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &toReadable);
    });

    engine.m_deletionQueue.push_function([=]() {
        vmaDestroyImage(engine.m_allocator, newImage.m_image, newImage.m_allocation);
    });

    vmaDestroyBuffer(engine.m_allocator, stagingBuffer.m_buffer, stagingBuffer.m_allocation);

    std::cout << "Texture loaded successfully" << file << std::endl;

    outImage = newImage;
    return true;
}

bool vkutil::load_image_from_asset(VulkanEngine& engine, const char* filename, AllocatedImage& outImage) {
    assets::AssetFile file;
    bool loaded = assets::load_binaryfile(filename, file);

    if (!loaded) {
        std::cout << "Error when loading image" << std::endl;
        return false;
    }

    assets::TextureInfo textureInfo = assets::read_texture_info(&file);

    VkDeviceSize imageSize = textureInfo.textureSize;
    VkFormat image_format;

    switch(textureInfo.textureFormat) {
        case assets::TextureFormat::RGBA8:
            image_format = VK_FORMAT_R8G8B8A8_UNORM;
            break;
        default:
            return false;
    }

    AllocatedBuffer stagingBuffer = engine.create_buffer(imageSize, 
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    
    void* data;
    vmaMapMemory(engine.m_allocator, stagingBuffer.m_allocation, &data);

    assets::unpack_texture(&textureInfo, file.binaryBlob.data(), file.binaryBlob.size(), (char*) data);

    vmaUnmapMemory(engine.m_allocator, stagingBuffer.m_allocation);

    outImage = upload_image(textureInfo.pixelSize[0], textureInfo.pixelSize[1], image_format, engine, stagingBuffer);

    vmaDestroyBuffer(engine.m_allocator, stagingBuffer.m_buffer, stagingBuffer.m_allocation);
}

AllocatedImage vkutil::upload_image(int texWidth, int texHeight, VkFormat image_format, VulkanEngine& engine, AllocatedBuffer& stagingBuffer)
{
	VkExtent3D imageExtent;
	imageExtent.width = static_cast<uint32_t>(texWidth);
	imageExtent.height = static_cast<uint32_t>(texHeight);
	imageExtent.depth = 1;

	VkImageCreateInfo dimg_info = vkinit::image_create_info(image_format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);

	AllocatedImage newImage;

	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	//allocate and create the image
	vmaCreateImage(engine.m_allocator, &dimg_info, &dimg_allocinfo, &newImage.m_image, &newImage.m_allocation, nullptr);

	//transition image to transfer-receiver	
	engine.immediate_submit([&](VkCommandBuffer cmd) {
		VkImageSubresourceRange range;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;
		range.baseArrayLayer = 0;
		range.layerCount = 1;

		VkImageMemoryBarrier imageBarrier_toTransfer = {};
		imageBarrier_toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

		imageBarrier_toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrier_toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier_toTransfer.image = newImage.m_image;
		imageBarrier_toTransfer.subresourceRange = range;

		imageBarrier_toTransfer.srcAccessMask = 0;
		imageBarrier_toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		//barrier the image into the transfer-receive layout
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toTransfer);

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = imageExtent;

		//copy the buffer into the image
		vkCmdCopyBufferToImage(cmd, stagingBuffer.m_buffer, newImage.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		VkImageMemoryBarrier imageBarrier_toReadable = imageBarrier_toTransfer;

		imageBarrier_toReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier_toReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		imageBarrier_toReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrier_toReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		//barrier the image into the shader readable layout
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toReadable);
		});


	//build a default imageview
	VkImageViewCreateInfo view_info = vkinit::imageview_create_info(image_format, newImage.m_image, VK_IMAGE_ASPECT_COLOR_BIT);

	vkCreateImageView(engine.m_device, &view_info, nullptr, &newImage.m_defaultView);


	engine.m_deletionQueue.push_function([=, &engine]() {

		vmaDestroyImage(engine.m_allocator, newImage.m_image, newImage.m_allocation);
	});

	newImage.mipLevels = 1;// mips.size();
	return newImage;
}