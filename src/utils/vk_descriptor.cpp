#include "vk_descriptor.h"
#include <algorithm>

namespace vkutil {
    void DescriptorAllocator::init(VkDevice newDevice) {
        device = newDevice;
    }

    void DescriptorAllocator::cleanup() {
        for (auto pool : freePools) {
            vkDestroyDescriptorPool(device, pool, nullptr);
        }
        for (auto pool : usedPools) {
            vkDestroyDescriptorPool(device, pool, nullptr);
        }
    }

    VkDescriptorPool DescriptorAllocator::grab_pool() {
        if (freePools.size() > 0) {
            VkDescriptorPool pool = freePools.back();
            freePools.pop_back();
            return pool;
        } else {
            return createPool(device, descriptorSizes, 1000, 0);
        }
    }

    bool DescriptorAllocator::allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout) {
        if (currentPool == VK_NULL_HANDLE) {
            currentPool = grab_pool();
            usedPools.push_back(currentPool);
        }

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;

        allocInfo.pSetLayouts = &layout;
        allocInfo.descriptorPool = currentPool;
        allocInfo.descriptorSetCount = 1;

        VkResult allocResult = vkAllocateDescriptorSets(device, &allocInfo, set);
        bool needReallocate = false;

        switch (allocResult) {
            case VK_SUCCESS:
                return true;
            case VK_ERROR_FRAGMENTED_POOL:
            case VK_ERROR_OUT_OF_POOL_MEMORY:
                needReallocate = true;
                break;
            default:
                return false;
        }

        if (needReallocate) {
            currentPool = grab_pool();
            usedPools.push_back(currentPool);

            allocInfo.descriptorPool = currentPool;
            allocResult = vkAllocateDescriptorSets(device, &allocInfo, set);

            if (allocResult == VK_SUCCESS) return true;
        }
        return false;
    }

    void DescriptorAllocator::reset_pools() {
        for (auto pool : usedPools) {
            vkResetDescriptorPool(device, pool, 0);
            freePools.push_back(pool);
        }
        usedPools.clear();

        currentPool = VK_NULL_HANDLE;
    }

    VkDescriptorPool createPool(VkDevice device, const DescriptorAllocator::PoolSizes& poolSizes,
        int count, VkDescriptorPoolCreateFlags flags) {
            std::vector<VkDescriptorPoolSize> sizes;
            sizes.reserve(poolSizes.sizes.size());
            for (auto size : poolSizes.sizes) {
                sizes.push_back({
                    size.first, 
                    uint32_t(size.second * count)
                });
            }

            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = flags;
            pool_info.maxSets = count;
            pool_info.poolSizeCount = (uint32_t)sizes.size();
            pool_info.pPoolSizes = sizes.data();

            VkDescriptorPool descriptorPool;
            vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool);

            return descriptorPool;
    }

    void DescriptorLayoutCache::init(VkDevice newDevice) {
        device = newDevice;
    }

    void DescriptorLayoutCache::cleanup() {
        for (auto pair : layoutCache) {
            vkDestroyDescriptorSetLayout(device, pair.second, nullptr);
        }
    }

    VkDescriptorSetLayout DescriptorLayoutCache::create_descriptor_layout(VkDescriptorSetLayoutCreateInfo* info) {
        DescriptorLayoutInfo layoutInfo;
        layoutInfo.bindings.reserve(info->bindingCount);
        bool isSorted = true;
        int lastBinding = -1;

        for (int i = 0; i < info->bindingCount; i++) {
            layoutInfo.bindings.push_back(info->pBindings[i]);

            if (info->pBindings[i].binding > lastBinding) {
                lastBinding = info->pBindings[i].binding;
            } else {
                isSorted = false;
            }
        }

        if (!isSorted) {
            std::sort(layoutInfo.bindings.begin(), layoutInfo.bindings.end(),
                [](VkDescriptorSetLayoutBinding& a, VkDescriptorSetLayoutBinding& b) {
                    return a.binding < b.binding;
                });
        }

        auto it = layoutCache.find(layoutInfo);
        if (it != layoutCache.end()) {
            return (*it).second;
        } else {
            VkDescriptorSetLayout layout;
            vkCreateDescriptorSetLayout(device, info, nullptr, &layout);

            layoutCache[layoutInfo] = layout;
            return layout;
        }
    }

    bool DescriptorLayoutCache::DescriptorLayoutInfo::operator==(const DescriptorLayoutInfo& other) const {
        if (other.bindings.size() != bindings.size()) {
            return false;
        } else {
            for (int i = 0; i < bindings.size(); i++) {
                if (other.bindings[i].binding != bindings[i].binding) {
                    return false;
                }
                if (other.bindings[i].descriptorType != bindings[i].descriptorType){
                    return false;
                }
                if (other.bindings[i].descriptorCount != bindings[i].descriptorCount){
                    return false;
                }
                if (other.bindings[i].stageFlags != bindings[i].stageFlags){
                    return false;
                }
            }
        }

        return true;
    }

    size_t DescriptorLayoutCache::DescriptorLayoutInfo::hash() const {
        using std::hash;

        size_t result = hash<size_t>()(bindings.size());

        for (const VkDescriptorSetLayoutBinding& binding : bindings) {
            size_t binding_hash = binding.binding | binding.descriptorType << 8 
                | binding.descriptorCount << 16 | binding.stageFlags << 24;

                result ^= hash<size_t>()(binding_hash);
        }

        return result;
    }

    DescriptorBuilder DescriptorBuilder::begin(DescriptorLayoutCache* layoutCache,
        DescriptorAllocator* allocator) {
            DescriptorBuilder builder;
            builder.cache = layoutCache;
            builder.alloc = allocator;

            return builder;
        }

    DescriptorBuilder& DescriptorBuilder::bind_buffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo,
        VkDescriptorType type, VkShaderStageFlags stageFlags) {
            VkDescriptorSetLayoutBinding newBinding = {};
            newBinding.descriptorCount = 1;
            newBinding.descriptorType = type;
            newBinding.pImmutableSamplers = nullptr;
            newBinding.stageFlags = stageFlags;
            newBinding.binding = binding;

            bindings.push_back(newBinding);

            VkWriteDescriptorSet newWrite = {};
            newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            newWrite.pNext = nullptr;

            newWrite.descriptorCount = 1;
            newWrite.descriptorType = type;
            newWrite.pBufferInfo = bufferInfo;
            newWrite.dstBinding = binding;

            writes.push_back(newWrite);
            return *this;
    }
    DescriptorBuilder& DescriptorBuilder::bind_image(uint32_t binding, VkDescriptorImageInfo* imageInfo,
        VkDescriptorType type, VkShaderStageFlags stageFlags) {
            VkDescriptorSetLayoutBinding newBinding = {};
            newBinding.descriptorCount = 1;
            newBinding.descriptorType = type;
            newBinding.pImmutableSamplers = nullptr;
            newBinding.stageFlags = stageFlags;
            newBinding.binding = binding;

            bindings.push_back(newBinding);

            VkWriteDescriptorSet newWrite = {};
            newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            newWrite.pNext = nullptr;

            newWrite.descriptorCount = 1;
            newWrite.descriptorType = type;
            newWrite.pImageInfo = imageInfo;
            newWrite.dstBinding = binding;

            writes.push_back(newWrite);
            return *this;
        }

    bool DescriptorBuilder::build(VkDescriptorSet& set, VkDescriptorSetLayout& layout) {
        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = nullptr;

        layoutInfo.pBindings = bindings.data();
        layoutInfo.bindingCount = bindings.size();

        layout = cache->create_descriptor_layout(&layoutInfo);

        bool success = alloc->allocate(&set, layout);
        if (!success) return false;

        for (VkWriteDescriptorSet& w : writes) {
            w.dstSet = set;
        }

        vkUpdateDescriptorSets(alloc->device, writes.size(), writes.data(), 0, nullptr);

        return true;
    }
    bool DescriptorBuilder::build(VkDescriptorSet& set) {
        VkDescriptorSetLayout layout;
        return build(set, layout);
    }
}