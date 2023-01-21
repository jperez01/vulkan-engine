#version 450

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 color;
layout (location = 3) in vec2 vTexCoord;

layout (location = 0) out vec2 texUV;

layout(set = 0, binding = 0) uniform CameraBuffer{
	mat4 view;
	mat4 proj;
	mat4 viewproj;
} cameraData;

void main() {
	gl_Position = cameraData.viewproj * vec4(position, 1.0f);
	texUV = vTexCoord;
}