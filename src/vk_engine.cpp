
#include "vk_engine.h"
#include "VkBootstrap.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>

#include <iostream>
#include <fstream>
#include <cmath>
#include <glm/gtx/transform.hpp>
#include "vk_pipeline.h"
#include "vk_textures.h"
#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

#define VK_CHECK(x) \
	do { \
		VkResult err = x; \
		if (err) { \
			std::cout << "Detected Vulkan error: " << err << std::endl; \
			abort(); \
		} \
	} while(0) \

void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
	
	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

	m_camera = Camera();

	init_vulkan();

	init_swapchain();

	init_default_renderpass();
	init_framebuffers();

	init_commands();

	init_sync_structures();

	init_descriptors();

	init_pipelines();

	init_imgui();

	load_images();

	load_model();

	//everything went fine
	_isInitialized = true;
}

void VulkanEngine::cleanup()
{	
	if (_isInitialized) {
		for (auto& frame : m_frames) {
			vkWaitForFences(m_device, 1, &frame.m_renderFence, true, 1000000);
		}

		m_deletionQueue.flush();

		vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

		vkDestroyDevice(m_device, nullptr);
		vkDestroyInstance(m_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::init_vulkan() {
	vkb::InstanceBuilder builder;

	auto inst_ret = builder.set_app_name("Vulkan App")
		.request_validation_layers(true)
		.require_api_version(1, 1, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	m_instance = vkb_inst.instance;
	m_debug_messenger = vkb_inst.debug_messenger;

	SDL_Vulkan_CreateSurface(_window, m_instance, &m_surface);

	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 1)
		.set_surface(m_surface)
		.select()
		.value();

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters_features = {};
	shader_draw_parameters_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
	shader_draw_parameters_features.pNext = nullptr;
	shader_draw_parameters_features.shaderDrawParameters = VK_TRUE;
	vkb::Device vkbDevice = deviceBuilder.add_pNext(&shader_draw_parameters_features).build().value();

	m_device = vkbDevice.device;
	m_chosenGPU = physicalDevice.physical_device;

	m_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	m_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = m_chosenGPU;
	allocatorInfo.device = m_device;
	allocatorInfo.instance = m_instance;
	vmaCreateAllocator(&allocatorInfo, &m_allocator);

	m_gpuProperties = vkbDevice.physical_device.properties;
	std::cout << "The GPU has a minimum buffer alignment of " <<
		m_gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;
}

void VulkanEngine::init_imgui() {
	VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;

	VK_CHECK(vkCreateDescriptorPool(m_device, &pool_info, nullptr, &imguiPool));

	ImGui::CreateContext();

	ImGui_ImplSDL2_InitForVulkan(_window);

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = m_instance;
	init_info.PhysicalDevice = m_chosenGPU;
	init_info.Device = m_device;
	init_info.Queue = m_graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info, m_renderPass);

	immediate_submit([&](VkCommandBuffer cmd) {
		ImGui_ImplVulkan_CreateFontsTexture(cmd);
		});

	ImGui_ImplVulkan_DestroyFontUploadObjects();

	m_deletionQueue.push_function([=]() {
		vkDestroyDescriptorPool(m_device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
		});
}

size_t VulkanEngine::pad_uniform_buffer_size(size_t originalSize) {
	size_t minUboAlignment = m_gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;

	if (minUboAlignment > 0) {
		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	}
	return alignedSize;
}

void VulkanEngine::init_swapchain() {
	vkb::SwapchainBuilder swapChainBuilder{ m_chosenGPU, m_device, m_surface };
	vkb::Swapchain vkbSwapchain = swapChainBuilder
		.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	m_swapchain = vkbSwapchain.swapchain;
	m_swapchainImages = vkbSwapchain.get_images().value();
	m_swapchainImageViews = vkbSwapchain.get_image_views().value();

	m_swapchainImageFormat = vkbSwapchain.image_format;

	m_deletionQueue.push_function([=]() {
		vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	});

	VkExtent3D depthImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	m_depthFormat = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo dimg_info = vkinit::image_create_info(m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(m_allocator, &dimg_info, &dimg_allocinfo, 
		&m_depthImage.m_image, &m_depthImage.m_allocation, nullptr);

	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(m_depthFormat, 
		m_depthImage.m_image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(m_device, &dview_info, nullptr, &m_depthImageView));

	m_deletionQueue.push_function([=]() {
		vkDestroyImageView(m_device, m_depthImageView, nullptr);
		vmaDestroyImage(m_allocator, m_depthImage.m_image, m_depthImage.m_allocation);
	});
}

void VulkanEngine::init_commands() {
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(m_graphicsQueueFamily,
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_frames[i].m_commandPool));

		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(m_frames[i].m_commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_frames[i].m_mainCommandBuffer));

		m_deletionQueue.push_function([=]() {
			vkDestroyCommandPool(m_device, m_frames[i].m_commandPool, nullptr);
			});
	}

	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(m_graphicsQueueFamily);
	VK_CHECK(vkCreateCommandPool(m_device, &uploadCommandPoolInfo, nullptr, &m_uploadContext.commandPool));

	m_deletionQueue.push_function([=]() {
		vkDestroyCommandPool(m_device, m_uploadContext.commandPool, nullptr);
		});

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(m_uploadContext.commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_uploadContext.commandBuffer));
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {
	VkCommandBuffer cmd = m_uploadContext.commandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = vkinit::submit_info(&cmd);

	VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submit, m_uploadContext.uploadFence));

	vkWaitForFences(m_device, 1, &m_uploadContext.uploadFence, true, 9999999999);
	vkResetFences(m_device, 1, &m_uploadContext.uploadFence);

	vkResetCommandPool(m_device, m_uploadContext.commandPool, 0);
}

void VulkanEngine::init_default_renderpass() {
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = m_swapchainImageFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;

	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depth_attachment{};
	// Depth attachment
	depth_attachment.flags = 0;
	depth_attachment.format = m_depthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_attachment_ref{};
	depth_attachment_ref.attachment = 1;
	depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

	VkSubpassDependency dependency{};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency depth_dependency{};
	depth_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depth_dependency.dstSubpass = 0;
	depth_dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.srcAccessMask = 0;
	depth_dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency dependencies[2] = { dependency, depth_dependency };

	VkRenderPassCreateInfo render_pass_info{};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

	render_pass_info.attachmentCount = 2;
	render_pass_info.pAttachments = &attachments[0];

	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	render_pass_info.dependencyCount = 2;
	render_pass_info.pDependencies = &dependencies[0];

	VK_CHECK(vkCreateRenderPass(m_device, &render_pass_info, nullptr, &m_renderPass));

	m_deletionQueue.push_function([=]() {
		vkDestroyRenderPass(m_device, m_renderPass, nullptr);
		});
}

void VulkanEngine::init_framebuffers() {
	VkFramebufferCreateInfo fb_info{};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;

	fb_info.renderPass = m_renderPass;
	fb_info.attachmentCount = 1;
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;

	const uint32_t swapchain_imagecount = m_swapchainImages.size();
	m_framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	for (int i = 0; i < swapchain_imagecount; ++i) {
		VkImageView attachments[2];
		attachments[0] = m_swapchainImageViews[i];
		attachments[1] = m_depthImageView;

		fb_info.pAttachments = attachments;
		fb_info.attachmentCount = 2;

		VK_CHECK(vkCreateFramebuffer(m_device, &fb_info, nullptr, &m_framebuffers[i]));

		m_deletionQueue.push_function([=]() {
			vkDestroyFramebuffer(m_device, m_framebuffers[i], nullptr);
			vkDestroyImageView(m_device, m_swapchainImageViews[i], nullptr);
		});
	}
}

void VulkanEngine::init_sync_structures() {
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);

	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(m_device, &fenceCreateInfo, nullptr, &m_frames[i].m_renderFence));

		m_deletionQueue.push_function([=]() {
			vkDestroyFence(m_device, m_frames[i].m_renderFence, nullptr);
			});

		VK_CHECK(vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, &m_frames[i].m_presentSemaphore));
		VK_CHECK(vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, &m_frames[i].m_renderSemaphore));

		m_deletionQueue.push_function([=]() {
			vkDestroySemaphore(m_device, m_frames[i].m_presentSemaphore, nullptr);
			vkDestroySemaphore(m_device, m_frames[i].m_renderSemaphore, nullptr);
			});
	}

	VkFenceCreateInfo uploadFenceCreateInfo = vkinit::fence_create_info();

	VK_CHECK(vkCreateFence(m_device, &uploadFenceCreateInfo, nullptr, &m_uploadContext.uploadFence));
	m_deletionQueue.push_function([=]() {
		vkDestroyFence(m_device, m_uploadContext.uploadFence, nullptr);
		});
}

void VulkanEngine::init_descriptors() {
	std::vector<VkDescriptorPoolSize> sizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10}
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = 0;
	pool_info.maxSets = 10;
	pool_info.poolSizeCount = (uint32_t)sizes.size();
	pool_info.pPoolSizes = sizes.data();

	vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_descriptorPool);

	VkDescriptorSetLayoutBinding cameraBind = vkinit::descriptorset_layout_binding(
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		VK_SHADER_STAGE_VERTEX_BIT, 0);
	VkDescriptorSetLayoutBinding textureBind = vkinit::descriptorset_layout_binding(
		VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
		VK_SHADER_STAGE_FRAGMENT_BIT, 1, 2);
	VkDescriptorSetLayoutBinding samplerBind = vkinit::descriptorset_layout_binding(
		VK_DESCRIPTOR_TYPE_SAMPLER,
		VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutBinding modelFragBindings[] = { textureBind, samplerBind};

	VkDescriptorSetLayoutCreateInfo modelLayoutInfo = vkinit::descriptorset_layout_create_info(modelFragBindings, 2);
	vkCreateDescriptorSetLayout(m_device, &modelLayoutInfo, nullptr, &m_textureSetLayout);

	VkDescriptorSetLayoutCreateInfo modelVertexLayoutInfo = vkinit::descriptorset_layout_create_info(&cameraBind, 1);
	vkCreateDescriptorSetLayout(m_device, &modelVertexLayoutInfo, nullptr, &m_sceneSetLayout);

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		m_frames[i].cameraBuffer = create_buffer(sizeof(GPUCameraData), 
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.pNext = nullptr;
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_sceneSetLayout;

		vkAllocateDescriptorSets(m_device, &allocInfo, &m_frames[i].m_globalDescriptor);

		VkDescriptorBufferInfo cameraInfo;
		cameraInfo.buffer = m_frames[i].cameraBuffer.m_buffer;
		cameraInfo.offset = 0;
		cameraInfo.range = sizeof(GPUCameraData);

		VkWriteDescriptorSet cameraWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			m_frames[i].m_globalDescriptor, &cameraInfo, 0);

		vkUpdateDescriptorSets(m_device, 1, &cameraWrite, 0, nullptr);
	}

	m_deletionQueue.push_function([&]() {
		vkDestroyDescriptorSetLayout(m_device, m_sceneSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(m_device, m_textureSetLayout, nullptr);

		vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

		for (int i = 0; i < FRAME_OVERLAP; i++) {
			vmaDestroyBuffer(m_allocator, m_frames[i].cameraBuffer.m_buffer, m_frames[i].cameraBuffer.m_allocation);
		}
	});
}

void VulkanEngine::init_pipelines() {
	VkShaderModule modelFragShader;
	if (!load_shader_module("../../shaders/model_lighting.frag.spv", &modelFragShader)) {
		std::cout << "Error when building the model lighting frag shader" << std::endl;
	} else {
		std::cout << "Model Lighting frag shader successfully loaded" << std::endl;
	}
	VkShaderModule modelVertexShader;
	if (!load_shader_module("../../shaders/model_lighting.vert.spv", &modelVertexShader)) {
		std::cout << "Error when building the model lighting vertex shader" << std::endl;
	} else {
		std::cout << "Model Lighting vertex shader successfully loaded" << std::endl;
	}

	PipelineBuilder pipelineBuilder;
	pipelineBuilder.m_shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, modelVertexShader)
	);
	pipelineBuilder.m_shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, modelFragShader)
	);

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	VkDescriptorSetLayout setLayouts[] = { m_sceneSetLayout, m_textureSetLayout };
	pipeline_layout_info.setLayoutCount = 2;
	pipeline_layout_info.pSetLayouts = setLayouts;

	VK_CHECK(vkCreatePipelineLayout(m_device, &pipeline_layout_info, nullptr, &m_pipelineLayout));

	pipelineBuilder.m_pipelineLayout = m_pipelineLayout;
	pipelineBuilder.m_vertexInputInfo = vkinit::vertex_input_state_create_info();
	pipelineBuilder.m_inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder.m_viewport.x = 0.0f;
	pipelineBuilder.m_viewport.y = 0.0f;
	pipelineBuilder.m_viewport.width = (float)_windowExtent.width;
	pipelineBuilder.m_viewport.height = (float)_windowExtent.height;
	pipelineBuilder.m_viewport.minDepth = 0.0f;
	pipelineBuilder.m_viewport.maxDepth = 1.0f;

	pipelineBuilder.m_scissor.offset = { 0, 0 };
	pipelineBuilder.m_scissor.extent = _windowExtent;

	pipelineBuilder.m_rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
	pipelineBuilder.m_multisampling = vkinit::multisampling_state_create_info();
	pipelineBuilder.m_colorBlendAttachment = vkinit::color_blend_attachment_state();

	pipelineBuilder.m_depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();
	pipelineBuilder.m_vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder.m_vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder.m_vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder.m_vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	m_pipeline = pipelineBuilder.build_pipeline(m_device, m_renderPass);

	vkDestroyShaderModule(m_device, modelVertexShader, nullptr);
	vkDestroyShaderModule(m_device, modelFragShader, nullptr);

	m_deletionQueue.push_function([=]() {
		vkDestroyPipeline(m_device, m_pipeline, nullptr);
		vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
	});
}

void VulkanEngine::init_scene() {
	RenderObject monkey;
	monkey.mesh = get_mesh("monkey");
	monkey.material = get_material("defaultmesh");
	monkey.transformMatrix = glm::mat4{ 1.0f };

	m_renderables.push_back(monkey);

	RenderObject map;
	map.mesh = get_mesh("empire");
	map.material = get_material("texturedmesh");
	map.transformMatrix = glm::translate(glm::vec3{ 5, -10, 0});


	for (int x = -20; x <= 20; x++) {
		for (int y = -20; y <= 20; y++) {

			RenderObject tri;
			tri.mesh = get_mesh("triangle");
			tri.material = get_material("defaultmesh");
			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x, 0, y));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2, 0.2, 0.2));
			tri.transformMatrix = translation * scale;

			m_renderables.push_back(tri);
		}
	}

	Material* texturedMat = get_material("texturedmesh");

	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_NEAREST);

	VkSampler blockySampler;
	vkCreateSampler(m_device, &samplerInfo, nullptr, &blockySampler);

	m_deletionQueue.push_function([=]() {
		vkDestroySampler(m_device, blockySampler, nullptr);
		});

	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = blockySampler;
	imageBufferInfo.imageView = m_textures["empire_diffuse"].imageView;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet texture1 = vkinit::write_descriptor_image(
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texturedMat->textureSet, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(m_device, 1, &texture1, 0, nullptr);
}

void VulkanEngine::draw_model(VkCommandBuffer cmd) {
	glm::mat4 projection = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 200.0f);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.proj = projection;
	camData.view = m_camera.getViewMatrix();
	camData.viewproj = projection * camData.view;

	void* data;
	vmaMapMemory(m_allocator, get_current_frame().cameraBuffer.m_allocation, &data);
	memcpy(data, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(m_allocator, get_current_frame().cameraBuffer.m_allocation);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
		1, 1, &m_textureDescriptorSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
		0, 1, &get_current_frame().m_globalDescriptor, 0, nullptr);

	for (Mesh& mesh : m_importedModel.m_meshes) {
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.m_vertexBuffer.m_buffer, &offset);
		vkCmdBindIndexBuffer(cmd, mesh.m_indicesBuffer.m_buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(cmd, mesh.m_indices.size(), 1, 0, 0, 0);
	}
}

void VulkanEngine::draw_objects(VkCommandBuffer cmd, RenderObject* first, int count) {
	glm::vec3 camPos = { 0.f,-6.f,-10.f };

	glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
	//camera projection
	glm::mat4 projection = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 200.0f);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.proj = projection;
	camData.view = m_camera.getViewMatrix();
	camData.viewproj = projection * camData.view;

	void* data;
	vmaMapMemory(m_allocator, get_current_frame().cameraBuffer.m_allocation, &data);
	memcpy(data, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(m_allocator, get_current_frame().cameraBuffer.m_allocation);

	float framed = _frameNumber / 120.f;
	m_sceneParameters.ambientColor = { sin(framed), 0, cos(framed), 1 };

	char* sceneData;
	vmaMapMemory(m_allocator, m_sceneParameterBuffer.m_allocation, (void**)&sceneData);

	int frameIndex = _frameNumber % FRAME_OVERLAP;
	sceneData += pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;
	memcpy(sceneData, &m_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(m_allocator, m_sceneParameterBuffer.m_allocation);

	void* objectData;
	vmaMapMemory(m_allocator, get_current_frame().objectBuffer.m_allocation, &objectData);
	
	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

	for (int i = 0; i < count; i++) {
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object.transformMatrix;
	}

	vmaUnmapMemory(m_allocator, get_current_frame().objectBuffer.m_allocation);

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;
	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		//only bind the pipeline if it doesn't match with the already bound one
		if (object.material != lastMaterial) {

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
			lastMaterial = object.material;

			uint32_t uniform_offset = pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout,
				0, 1, &get_current_frame().m_globalDescriptor, 1, &uniform_offset);

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout,
				1, 1, &get_current_frame().objectDescriptor, 0, nullptr);

			if (object.material->textureSet != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					object.material->pipelineLayout, 2, 1, &object.material->textureSet, 0, nullptr);
			}
		}


		glm::mat4 model = object.transformMatrix;
		//final render matrix, that we are calculating on the cpu
		glm::mat4 mesh_matrix = projection * view * model;

		MeshPushConstants constants;
		constants.render_matrix = model;

		//upload the mesh to the GPU via push constants
		vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

		//only bind the mesh if it's a different one from last bind
		if (object.mesh != lastMesh) {
			//bind the mesh vertex buffer with offset 0
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->m_vertexBuffer.m_buffer, &offset);
			lastMesh = object.mesh;
		}
		//we can now draw
		vkCmdDraw(cmd, object.mesh->m_vertices.size(), 1, 0, i);
	}
}

bool VulkanEngine::load_shader_module(const char* filePath, VkShaderModule* output) {
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) return false;

	size_t fileSize = (size_t)file.tellg();
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) 
		return false;

	*output = shaderModule;
	return true;
}

void VulkanEngine::draw()
{
	if (SDL_GetWindowFlags(_window) & SDL_WINDOW_MINIMIZED) return;
	ImGui::Render();

	VK_CHECK(vkWaitForFences(m_device, 1, &get_current_frame().m_renderFence, true, 10000000));
	VK_CHECK(vkResetFences(m_device, 1, &get_current_frame().m_renderFence));

	VK_CHECK(vkResetCommandBuffer(get_current_frame().m_mainCommandBuffer, 0));

	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(m_device, m_swapchain, 1000000000,
		get_current_frame().m_presentSemaphore, nullptr, &swapchainImageIndex));

	VkCommandBuffer cmd = get_current_frame().m_mainCommandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.f));
	clearValue.color = { { 0.0f, 0.0f, flash, 1.0f } };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.f;

	VkRenderPassBeginInfo rpInfo = {};
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpInfo.pNext = nullptr;

	rpInfo.renderPass = m_renderPass;
	rpInfo.renderArea.offset.x = 0;
	rpInfo.renderArea.offset.y = 0;
	rpInfo.renderArea.extent = _windowExtent;
	rpInfo.framebuffer = m_framebuffers[swapchainImageIndex];

	VkClearValue clearValues[] = { clearValue, depthClear };

	rpInfo.clearValueCount = 2;
	rpInfo.pClearValues = &clearValues[0];

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	draw_model(cmd); 

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	submit.pWaitDstStageMask = &waitStage;

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &get_current_frame().m_presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &get_current_frame().m_renderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submit, get_current_frame().m_renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &m_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &get_current_frame().m_renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(m_graphicsQueue, &presentInfo));

	_frameNumber++;
}

FrameData& VulkanEngine::get_current_frame() {
	return m_frames[_frameNumber % FRAME_OVERLAP];
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;

	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;

	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &vmaallocInfo,
		&newBuffer.m_buffer,
		&newBuffer.m_allocation,
		nullptr));

	return newBuffer;
}

Material* VulkanEngine::create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name) {
	Material mat;
	mat.pipeline = pipeline;
	mat.pipelineLayout = layout;
	m_materials[name] = mat;

	return &m_materials[name];
}

Material* VulkanEngine::get_material(const std::string& name) {
	auto it = m_materials.find(name);
	if (it == m_materials.end()) return nullptr;
	else {
		return &(*it).second;
	}
}

Mesh* VulkanEngine::get_mesh(const std::string& name)
{
	auto it = m_meshes.find(name);
	if (it == m_meshes.end()) {
		return nullptr;
	}
	else {
		return &(*it).second;
	}
}

void VulkanEngine::load_model() {
	std::string objectPath = "../../assets/backpack/";
	m_importedModel = Model(objectPath + "backpack.obj");

	for (Texture& texture : m_importedModel.m_textures_loaded) {
		std::string filePath = objectPath + texture.path;
		bool result = vkutil::load_image_from_file((*this), filePath.c_str(), texture.image);

		VkImageViewCreateInfo imageInfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_SRGB,
			texture.image.m_image, VK_IMAGE_ASPECT_COLOR_BIT);
		vkCreateImageView(m_device, &imageInfo, nullptr, &texture.image.m_defaultView);
	}

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.pNext = nullptr;
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_textureSetLayout;

	vkAllocateDescriptorSets(m_device, &allocInfo, &m_textureDescriptorSet);

	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_NEAREST);

	VkSampler blockySampler;
	vkCreateSampler(m_device, &samplerInfo, nullptr, &blockySampler);

	VkDescriptorImageInfo textureImageInfo[2];
	textureImageInfo[0] = {};
	textureImageInfo[0].sampler = nullptr;
	textureImageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	textureImageInfo[0].imageView = m_importedModel.m_textures_loaded[0].image.m_defaultView;

	textureImageInfo[1] = {};
	textureImageInfo[1].sampler = nullptr;
	textureImageInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	textureImageInfo[1].imageView = m_importedModel.m_textures_loaded[1].image.m_defaultView;

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.sampler = blockySampler;

	VkWriteDescriptorSet setWrites[2];

	setWrites[0] = vkinit::write_descriptor_image(
		VK_DESCRIPTOR_TYPE_SAMPLER,
		m_textureDescriptorSet,
		&imageInfo, 0
	);

	setWrites[1] = vkinit::write_descriptor_image(
		VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
		m_textureDescriptorSet,
		textureImageInfo,
		1
	);
	setWrites[1].descriptorCount = 2;

	vkUpdateDescriptorSets(m_device, 2, setWrites, 0, nullptr);

	m_deletionQueue.push_function([=]() {
		vkDestroySampler(m_device, blockySampler, nullptr);
	});

	for (Mesh& mesh : m_importedModel.m_meshes) {
		upload_mesh(mesh);
	}
}

void VulkanEngine::load_images() {
	Texture lostEmpire;
	
	vkutil::load_image_from_file(*this, "../../assets/lost_empire-RGBA.png", lostEmpire.image);

	VkImageViewCreateInfo imageInfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_SRGB,
		lostEmpire.image.m_image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(m_device, &imageInfo, nullptr, &lostEmpire.imageView);

	m_textures["empire_diffuse"] = lostEmpire;

	m_deletionQueue.push_function([=]() {
		vkDestroyImageView(m_device, lostEmpire.imageView, nullptr);
	});
}

void VulkanEngine::load_meshes() {
	m_triangleMesh.m_vertices.resize(3);

	m_triangleMesh.m_vertices[0].position = { 1.f, 1.f, 0.0f };
	m_triangleMesh.m_vertices[1].position = { -1.f, 1.f, 0.0f };
	m_triangleMesh.m_vertices[2].position = { 0.f, -1.f, 0.0f };

	m_triangleMesh.m_vertices[0].color = { 0.f, 1.f, 0.0f };
	m_triangleMesh.m_vertices[1].color = { 0.f, 1.f, 0.0f };
	m_triangleMesh.m_vertices[2].color = { 0.f, 1.f, 0.0f };

	m_monkeyMesh.load_from_obj("../../assets/monkey_smooth.obj");

	Mesh lostEmpire{};
	lostEmpire.load_from_obj("../../assets/lost_empire.obj");

	upload_mesh(m_triangleMesh);
	upload_mesh(m_monkeyMesh);
	upload_mesh(lostEmpire);

	m_meshes["monkey"] = m_monkeyMesh;
	m_meshes["triangle"] = m_triangleMesh;
	m_meshes["empire"] = lostEmpire;
}

void VulkanEngine::upload_mesh(Mesh &mesh) {
	const size_t bufferSize = mesh.m_vertices.size() * sizeof(Vertex);

	VkBufferCreateInfo stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.pNext = nullptr;
	stagingBufferInfo.size = bufferSize;
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	VK_CHECK(vmaCreateBuffer(m_allocator, &stagingBufferInfo, &vmaallocInfo,
		&stagingBuffer.m_buffer,
		&stagingBuffer.m_allocation,
		nullptr));

	void* data;
	vmaMapMemory(m_allocator, stagingBuffer.m_allocation, &data);
	memcpy(data, mesh.m_vertices.data(), mesh.m_vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(m_allocator, stagingBuffer.m_allocation);

	VkBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	vertexBufferInfo.size = bufferSize;
	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	vmaallocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateBuffer(m_allocator, &vertexBufferInfo, &vmaallocInfo,
		&mesh.m_vertexBuffer.m_buffer,
		&mesh.m_vertexBuffer.m_allocation,
		nullptr));

	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = bufferSize;
		vkCmdCopyBuffer(cmd, stagingBuffer.m_buffer, mesh.m_vertexBuffer.m_buffer, 1, &copy);
	});

	VkDeviceSize indexBufferSize = sizeof(unsigned int) * mesh.m_indices.size();

	stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.pNext = nullptr;
	stagingBufferInfo.size = indexBufferSize;
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer indexStagingBuffer;
	VK_CHECK(vmaCreateBuffer(m_allocator, &stagingBufferInfo, &vmaallocInfo,
		&indexStagingBuffer.m_buffer,
		&indexStagingBuffer.m_allocation,
		nullptr));

	void* indexData;
	vmaMapMemory(m_allocator, indexStagingBuffer.m_allocation, &indexData);
	memcpy(indexData, mesh.m_indices.data(), mesh.m_indices.size() * sizeof(unsigned int));
	vmaUnmapMemory(m_allocator, indexStagingBuffer.m_allocation);

	vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	vertexBufferInfo.size = bufferSize;
	vertexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	vmaallocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateBuffer(m_allocator, &vertexBufferInfo, &vmaallocInfo,
		&mesh.m_indicesBuffer.m_buffer,
		&mesh.m_indicesBuffer.m_allocation,
		nullptr));

	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = indexBufferSize;
		vkCmdCopyBuffer(cmd, indexStagingBuffer.m_buffer, mesh.m_indicesBuffer.m_buffer, 1, &copy);
	});

	m_deletionQueue.push_function([=]() {
		vmaDestroyBuffer(m_allocator, mesh.m_vertexBuffer.m_buffer, mesh.m_vertexBuffer.m_allocation);
	});

	vmaDestroyBuffer(m_allocator, stagingBuffer.m_buffer, stagingBuffer.m_allocation);
}

void VulkanEngine::run()
{
	auto& io = ImGui::GetIO();
	SDL_Event e;
	bool bQuit = false;

	//main loop
	while (!bQuit)
	{
		//Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			if (io.WantCaptureMouse) break;
			//close the window when user alt-f4s or clicks the X button			
			if (e.type == SDL_QUIT) bQuit = true;
			else if (e.type == SDL_KEYDOWN) {
				SDL_Keycode type = e.key.keysym.sym;

				if (type == SDLK_LEFT) m_camera.processKeyboard(LEFT, m_cameraInfo.deltaTime);
				if (type == SDLK_RIGHT) m_camera.processKeyboard(RIGHT, m_cameraInfo.deltaTime);
				if (type == SDLK_UP) m_camera.processKeyboard(FORWARD, m_cameraInfo.deltaTime);
				if (type == SDLK_DOWN) m_camera.processKeyboard(BACKWARD, m_cameraInfo.deltaTime);
			} else if (e.type == SDL_MOUSEMOTION) {
				float mouseX = e.motion.x;
				float mouseY = e.motion.y;

				if (m_cameraInfo.firstMouse) {
					m_cameraInfo.lastX = mouseX;
					m_cameraInfo.lastY = mouseY;
					m_cameraInfo.firstMouse = false;
				}

				float xoffset = mouseX - m_cameraInfo.lastX;
				float yoffset = m_cameraInfo.lastY - mouseY;

				m_cameraInfo.lastX = mouseX;
				m_cameraInfo.lastY = mouseY;
				m_camera.processMouseMovement(xoffset, yoffset);
			} else if (e.type == SDL_MOUSEWHEEL) {
				float scrollY = e.wheel.y;
				m_camera.processMouseScroll(scrollY);
			}
		}

		float currentFrame = static_cast<float>(SDL_GetTicks());
		m_cameraInfo.deltaTime = currentFrame - m_cameraInfo.lastFrame;
		m_cameraInfo.deltaTime *= 0.001f;
		m_cameraInfo.lastFrame = currentFrame;

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame(_window);
		ImGui::NewFrame();

		draw();
	}
}

