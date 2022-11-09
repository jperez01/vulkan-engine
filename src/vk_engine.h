// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_mesh.h"

#include <glm/glm.hpp>
#include <vk_types.h>
#include <vector>
#include <functional>
#include <deque>

struct Material {
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	VkDescriptorSet textureSet{VK_NULL_HANDLE};
};

struct RenderObject {
	Mesh* mesh;
	Material* material;

	glm::mat4 transformMatrix;
};

struct MeshPushConstants {
	glm::vec4 data;
	glm::mat4 render_matrix;
};

struct DeletionQueue {
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		for (auto it = deletors.rbegin(); it != deletors.rend(); ++it)
			(*it)();

		deletors.clear();
	}
};

struct GPUCameraData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
};

struct GPUSceneData {
	glm::vec4 fogColor;
	glm::vec4 fogDistances;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection;
	glm::vec4 sunlightColor;
};

struct FrameData {
	VkSemaphore m_presentSemaphore, m_renderSemaphore;
	VkFence m_renderFence;

	VkCommandPool m_commandPool;
	VkCommandBuffer m_mainCommandBuffer;

	AllocatedBuffer cameraBuffer;
	VkDescriptorSet m_globalDescriptor;

	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptor;
};

struct GPUObjectData {
	glm::mat4 modelMatrix;
};

struct UploadContext {
	VkFence uploadFence;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
};

struct Texture {
	AllocatedImage image;
	VkImageView imageView;
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:

	VkInstance m_instance;
	VkDebugUtilsMessengerEXT m_debug_messenger;
	VkPhysicalDevice m_chosenGPU;
	VkDevice m_device;
	VkSurfaceKHR m_surface;

	VkSwapchainKHR m_swapchain;
	VkFormat m_swapchainImageFormat;
	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;

	VkQueue m_graphicsQueue;
	uint32_t m_graphicsQueueFamily;

	FrameData m_frames[FRAME_OVERLAP];
	FrameData& get_current_frame();

	UploadContext m_uploadContext;

	VkDescriptorSetLayout m_globalSetLayout;
	VkDescriptorSetLayout m_objectSetLayout;
	VkDescriptorSetLayout m_singleTextureSetLayout;
	VkDescriptorPool m_descriptorPool;

	GPUSceneData m_sceneParameters;
	AllocatedBuffer m_sceneParameterBuffer;

	VkRenderPass m_renderPass;
	std::vector<VkFramebuffer> m_framebuffers;

	VkPipelineLayout m_pipelineLayout;
	VkPipeline m_pipeline;

	DeletionQueue m_deletionQueue;

	VmaAllocator m_allocator;

	VkPipeline m_meshPipeline;
	Mesh m_triangleMesh;

	VkPipelineLayout m_meshPipelineLayout;

	VkPhysicalDeviceProperties m_gpuProperties;

	Mesh m_monkeyMesh;

	VkImageView m_depthImageView;
	AllocatedImage m_depthImage;

	VkFormat m_depthFormat;

	std::vector<RenderObject> m_renderables;
	std::unordered_map<std::string, Material> m_materials;
	std::unordered_map<std::string, Mesh> m_meshes;
	std::unordered_map<std::string, Texture> m_textures;

	bool _isInitialized{ false };
	int _frameNumber {0};

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_default_renderpass();
	void init_framebuffers();
	void init_sync_structures();
	void init_pipelines();
	void init_scene();
	void init_descriptors();

	bool load_shader_module(const char* filePath, VkShaderModule* output);

	Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
	Material* get_material(const std::string& name);
	Mesh* get_mesh(const std::string& name);

	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);

	void load_images();

	void load_meshes();
	void upload_mesh(Mesh& mesh);

	size_t pad_uniform_buffer_size(size_t originalSize);
};
