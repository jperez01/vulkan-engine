#include "asset_loader.h"

#include <iostream>
#include <fstream>

assets::CompressionMode assets::parse_compression(const char* f) {
    if (strcmp(f, "LZ4")) return assets::CompressionMode::LZ4;
    else return assets::CompressionMode::None;
}

bool assets::save_binaryfile(const char* path, const assets::AssetFile& file) {
    std::ofstream outfile;
    outfile.open(path, std::ios::binary | std::ios::out);

    outfile.write(file.type, 4);

    uint32_t version = file.version;
    outfile.write((const char*)&version, sizeof(uint32_t));

    uint32_t length = file.json.size();
    outfile.write((const char*)&length, sizeof(uint32_t));

    uint32_t bloblength = file.binaryBlob.size();
    outfile.write((const char*)&bloblength, sizeof(uint32_t));

    outfile.write(file.json.data(), length);
    outfile.write(file.binaryBlob.data(), file.binaryBlob.size());

    outfile.close();

    return true;
}

bool assets::load_binaryfile(const char* path, assets::AssetFile& outputFile) {
    std::ifstream infile;
    infile.open(path, std::ios::binary);

    if (!infile.is_open()) return false;

    infile.seekg(0);

    infile.read(outputFile.type, 4);
    infile.read((char*)&outputFile.version, sizeof(uint32_t));

    uint32_t jsonlen = 0;
    infile.read((char*)&jsonlen, sizeof(uint32_t));

    uint32_t bloblen = 0;
    infile.read((char*)&bloblen, sizeof(uint32_t));

    outputFile.json.resize(jsonlen);
    infile.read(outputFile.json.data(), jsonlen);

    outputFile.binaryBlob.resize(bloblen);
    infile.read(outputFile.binaryBlob.data(), bloblen);

    return true;
}