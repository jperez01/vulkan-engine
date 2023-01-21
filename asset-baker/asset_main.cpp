#include <filesystem>
#include <asset_loader.h>
#include <texture_asset.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "tiny_obj_loader.h"

namespace fs = std::filesystem;

using namespace assets;

bool convert_image(const fs::path& input, const fs::path& output) {
    int texWidth, texHeight, texChannels;

    stbui_uc* pixels = stbi_load(input.u8string().c_str(), &texWidth, 
        &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        std::cout << "Failed to load texture file" << input << std::endl;
        return false;
    }

    int texture_size = texWidth * texHeight * 4;

    TextureInfo texinfo;
    texinfo.textureSize = texture_size;
    texinfo.pixelSize[0] = texWidth;
    texinfo.pixelSize[1] = texHeight;
    texinfo.textureFormat = TextureFormat::RGBA8;
    texinfo.originalFile = input.string();
    assets::AssetFile newImage = assets::pack_texture(&texInfo, pixels);

    stbi_image_free(pixels);
    save_binaryfile(output.string().c_str(), newImage);

    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "You need to put the path to the info file.";
        return -1;
    } else {
        fs::path path{ argv[1] };

        fs::path directory = path;

        std::cout << "Loading asset directory at " << directory << std::endl;

        for (auto& p : fs::directory_iterator(directory)) {
            std::cout << "File: " << p;

            if (p.path().extension() == ".png") {
                std::cout << "found a texture" << std::endl;

                auto newpath = p.path();
                newpath.replace_extension(".tx");
                convert_image(p.path(), newpath);
            }
        }
    }
}