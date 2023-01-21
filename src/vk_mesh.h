#pragma once

#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

#include "vk_types.h"

struct Texture;

struct VertexInputDescription {
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;

	VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex {
	alignas(16) glm::vec3 position;
	alignas(16) glm::vec3 normal;
	alignas(16) glm::vec3 color;
	alignas(16) glm::vec2 uv;

	static VertexInputDescription get_vertex_description();
};

struct Mesh {
	std::vector<Vertex> m_vertices;
	std::vector<Texture> m_textures;
	std::vector<unsigned int> m_indices;

	AllocatedBuffer m_vertexBuffer;
	AllocatedBuffer m_indicesBuffer;

	bool load_from_obj(const char* filename);
};