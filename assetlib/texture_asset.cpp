#include "texture_asset.h"
#include <json.hpp>
#include <lz4.h>

assets::TextureFormat parse_format(const char* f) {
    if (strcmp(f, "RGBA8") == 0) return assets::TextureFormat::RGBA8;
    else return assets::TextureFormat::Unknown;
}

assets::TextureInfo assets::read_texture_info(AssetFile* file) {
    TextureInfo info;
    
    nlohmann::json metadata = nlohmann::json::parse(file->json);

    std::string formatString = metadata["format"];
    info.textureFormat = parse_format(formatString.c_str());

    std::string compressionString = metadata["compression"];
    info.compressionMode = parse_compression(compressionString.c_str());

    info.pixelSize[0] = metadata["width"];
    info.pixelSize[1] = metadata["height"];
    info.textureSize = metadata["buffer_size"];
    info.originalFile = metadata["original_file"];

    return info;
}

void assets::unpack_texture(TextureInfo* info, const char* sourcebuffer, size_t sourceSize, char* destination) {
    if (info->compressionMode == CompressionMode::LZ4) {
        LZ4_decompress_safe(sourcebuffer, destination, sourceSize, info->textureSize);
    } else {
        memcpy(destination, sourcebuffer, sourceSize);
    }
}

assets::AssetFile assets::pack_texture(assets::TextureInfo* info, void* pixelData) {
    nlohmann::json metadata;
    metadata["format"] = "RGBA8";
    metadata["width"] = info->pixelSize[0];
    metadata["height"] = info->pixelSize[1];
    metadata["buffer_size"] = info->textureSize;
    metadata["original_file"] = info->originalFile;

    AssetFile file;
    file.type[0] = 'T';
	file.type[1] = 'E';
	file.type[2] = 'X';
	file.type[3] = 'I';
	file.version = 1;

    // Find max data needed for compression
    int compressStaging = LZ4_compressBound(info->textureSize);

    file.binaryBlob.resize(compressStaging);

    //Like memcpy but compresses the data and returns a compressed size
    int compressedSize = LZ4_compress_default((const char*)pixelData, file.binaryBlob.data(), 
        info->textureSize, compressStaging);

    file.binaryBlob.resize(compressedSize);

    metadata["compression"] = "LZ4";

    std::string stringified = metadata.dump();
    file.json = stringified;

    return file;
}