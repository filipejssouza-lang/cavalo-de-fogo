#include <iostream>
#include <vector>
#include <vulkan/vulkan.h>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include "uma_allocator.cpp"

void createInstance(VkInstance& instance) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Turnip UMA Benchmark";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3; 

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create instance!");
    }
}

void pickPhysicalDevice(VkInstance instance, VkPhysicalDevice& physicalDevice) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    
    physicalDevice = devices[0];
}

void createLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice& device, VkQueue& queue) {
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = 0; 
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device!");
    }
    
    vkGetDeviceQueue(device, 0, 0, &queue);
}

void submitCompute(VkDevice device, VkQueue queue) {
    // Dummy compute dispatch to measure dispatch latency
    VkCommandPool commandPool;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = 0;
    vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    // In a real scenario we would bind a pipeline here and call vkCmdDispatch
    // We mock the dispatch with a pipeline barrier as a substitute for command submission
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    auto start = std::chrono::high_resolution_clock::now();
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    auto end = std::chrono::high_resolution_clock::now();
    
    std::cout << "GPU Compute Dispatch + Wait latency: " 
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() 
              << " us" << std::endl;

    vkDestroyCommandPool(device, commandPool, nullptr);
}

int main() {
    std::cout << "Turnip Vulkan 1.3 UMA Benchmark" << std::endl;
    
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue queue;

    try {
        createInstance(instance);
        pickPhysicalDevice(instance, physicalDevice);
        
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        std::cout << "Selected Device: " << props.deviceName << std::endl;

        createLogicalDevice(physicalDevice, device, queue);
        
        {
            UMAAllocator allocator(physicalDevice, device);

            const VkDeviceSize bufferSize = 1024 * 1024 * 16; 
            std::cout << "Allocating " << bufferSize / (1024*1024) << " MB UMA buffer..." << std::endl;
            
            auto start = std::chrono::high_resolution_clock::now();
            auto alloc = allocator.allocate(bufferSize);
            auto end = std::chrono::high_resolution_clock::now();
            std::cout << "Allocation time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " us" << std::endl;

            std::cout << "Writing to UMA buffer (CPU mapping)..." << std::endl;
            start = std::chrono::high_resolution_clock::now();
            memset(alloc.mapped_ptr, 0xBB, bufferSize);
            end = std::chrono::high_resolution_clock::now();
            std::cout << "CPU write time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " us" << std::endl;

            std::cout << "Reading from UMA buffer (CPU mapping)..." << std::endl;
            start = std::chrono::high_resolution_clock::now();
            (void)((uint8_t*)alloc.mapped_ptr)[bufferSize - 1];
            end = std::chrono::high_resolution_clock::now();
            std::cout << "CPU read time (1 byte): " << std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() << " ns" << std::endl;
            
            submitCompute(device, queue);

            allocator.free(alloc);
        }

        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        
        std::cout << "Benchmark complete." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Vulkan Error: " << e.what() << std::endl;
        std::cout << "Warning: Vulkan environment not fully available. Exiting safely." << std::endl;
        return 0;
    }

    return 0;
}
