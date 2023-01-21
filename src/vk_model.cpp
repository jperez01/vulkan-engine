#include "vk_model.h"

Model::Model() = default;

Model::Model(std::string path) {
    std::cout << "Starting model loading" << std::endl;
    loadModel(path);
    std::cout << "Ended model loading" << std::endl;
}

void Model::loadModel(std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate);

    if (!scene  || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cout << "Error::Assimp::" << importer.GetErrorString() << std::endl;
    }
    m_directory = path.substr(0, path.find_last_of('/'));

    processNode(scene->mRootNode, scene);
}

void Model::processNode(aiNode* node, const aiScene* scene) {
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        m_meshes.push_back(processMesh(mesh, scene));
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene);
    }
}

Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;
    
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        Vertex vertex;
        vertex.position = glm::vec3(
            mesh->mVertices[i].x,
            mesh->mVertices[i].y,
            mesh->mVertices[i].z
            );
        
        if (mesh->HasNormals()) {
            vertex.normal = glm::vec3(
            mesh->mNormals[i].x,
            mesh->mNormals[i].y,
            mesh->mNormals[i].z
            );
        }

        if (mesh->mTextureCoords[0]) {
            vertex.uv = glm::vec2(
                mesh->mTextureCoords[0][i].x,
                mesh->mTextureCoords[0][i].y
            );
        } else {
            vertex.uv = glm::vec2(0, 0);
        }

        vertices.push_back(vertex);
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    if (mesh->mMaterialIndex >= 0) {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        std::string materialType = "diffuse";
        std::vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, materialType);
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        
        materialType = "specular";
        std::vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, materialType);
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
    }
    Mesh newMesh;
    newMesh.m_indices = indices;
    newMesh.m_textures = textures;
    newMesh.m_vertices = vertices;

    return newMesh;
}

std::vector<Texture> Model::loadMaterialTextures(aiMaterial* material, aiTextureType type, std::string& typeName) {
    std::vector<Texture> textures;
    unsigned int count = material->GetTextureCount(type);

    for (unsigned int i = 0; i < count; i++) {
        aiString str;
        material->GetTexture(type, i, &str);
        bool skip = false;

        for (unsigned int j = 0; j < m_textures_loaded.size(); j++) {
            if (std::strcmp(m_textures_loaded[j].path.c_str(), str.C_Str()) == 0) {
                textures.push_back(m_textures_loaded[j]);
                skip = true;
                break;
            }
        }

        if (!skip) {
            Texture texture;
            std::string path = std::string(str.C_Str());
            texture.type = type;
            texture.path = str.C_Str();
            textures.push_back(texture);
            m_textures_loaded.push_back(texture);
        }
    }

    return textures;
}