#include <iostream>
#include <vector>
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <mutex>
#include <memory>

class UMAAllocator {
private:
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkPhysicalDeviceMemoryProperties memProperties;
    uint32_t umaMemoryTypeIndex;

    struct Block {
        VkDeviceMemory memory;
        VkDeviceSize size;
        VkDeviceSize offset;
        bool in_use;
        void* mapped_ptr;
    };

    std::vector<Block> memoryPool;
    std::mutex allocatorMutex;

    uint32_t findUMAMemoryType() {
        // Find memory type that is both HOST_VISIBLE and DEVICE_LOCAL (UMA signature)
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                return i;
            }
        }
        
        // Fallback: Just HOST_VISIBLE if UMA is strictly not found (though UMA is expected on mobile)
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
                return i;
            }
        }
        throw std::runtime_error("Failed to find suitable memory type for UMA!");
    }

public:
    UMAAllocator(VkPhysicalDevice pDev, VkDevice dev) : physicalDevice(pDev), device(dev) {
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
        umaMemoryTypeIndex = findUMAMemoryType();
    }

    ~UMAAllocator() {
        std::lock_guard<std::mutex> lock(allocatorMutex);
        for (auto& block : memoryPool) {
            if (block.mapped_ptr) {
                vkUnmapMemory(device, block.memory);
            }
            vkFreeMemory(device, block.memory, nullptr);
        }
        memoryPool.clear();
    }

    struct Allocation {
        VkDeviceMemory memory;
        VkDeviceSize size;
        VkDeviceSize offset;
        void* mapped_ptr;
        size_t block_index;
    };

    Allocation allocate(VkDeviceSize size) {
        std::lock_guard<std::mutex> lock(allocatorMutex);
        
        // Simple sub-allocator: try to find free block
        for (size_t i = 0; i < memoryPool.size(); ++i) {
            if (!memoryPool[i].in_use && memoryPool[i].size >= size) {
                memoryPool[i].in_use = true;
                return { memoryPool[i].memory, memoryPool[i].size, memoryPool[i].offset, memoryPool[i].mapped_ptr, i };
            }
        }

        // Allocate new block
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = size;
        allocInfo.memoryTypeIndex = umaMemoryTypeIndex;

        VkDeviceMemory memory;
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate UMA memory!");
        }

        void* mapped = nullptr;
        vkMapMemory(device, memory, 0, size, 0, &mapped);

        memoryPool.push_back({ memory, size, 0, true, mapped });
        return { memory, size, 0, mapped, memoryPool.size() - 1 };
    }

    void free(const Allocation& alloc) {
        std::lock_guard<std::mutex> lock(allocatorMutex);
        if (alloc.block_index < memoryPool.size()) {
            memoryPool[alloc.block_index].in_use = false;
        }
    }
};
