#version 450

layout (location = 0) out vec4 outColor;
layout (location = 0) in vec2 fragUV;

layout (set = 1, binding = 0) uniform sampler samp;
layout (set = 1, binding = 1) uniform texture2D textures[2];

void main() {
    vec4 color = texture(sampler2D(textures[0], samp), fragUV);
    color += texture(sampler2D(textures[1], samp), fragUV);
    
    outColor = color;
}