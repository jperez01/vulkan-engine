#pragma once

#include <string>
#include <iostream>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "vk_mesh.h"

class Model {
    public:
        Model();
        Model(std::string path);

        void draw();

        std::vector<Texture> m_textures_loaded;
        std::vector<Mesh> m_meshes;
    
    private:
        std::string m_directory;

        void loadModel(std::string& path);
        void processNode(aiNode* node, const aiScene* scene);
        Mesh processMesh(aiMesh* mesh, const aiScene* scene);

        std::vector<Texture> loadMaterialTextures(aiMaterial* material, aiTextureType type, std::string& typeName);
};