#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <cstring>
#include <cmath>
#include <vulkan/vulkan.h>

#define VK_CHECK(x) do { \
    VkResult err = x; \
    if (err) { \
        std::cerr << "Vulkan Error: " << err << " at line " << __LINE__ << std::endl; \
        exit(1); \
    } \
} while (0)

struct PushConstants {
    uint32_t M;
    uint32_t N;
    uint32_t K;
};

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

struct UMABuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

UMABuffer createUMABuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size) {
    UMABuffer b;
    b.size = size;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &bufferInfo, nullptr, &b.buffer));

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, b.buffer, &memReqs);

    // Host-Visible + Device-Local (UMA zero-copy on Qualcomm mobile SoC)
    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t memTypeIndex = 0;
    try {
        memTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, flags);
    } catch (...) {
        // Fallback to host visible coherent if device local flag is separate
        flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        memTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, flags);
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &b.memory));
    VK_CHECK(vkBindBufferMemory(device, b.buffer, b.memory, 0));
    VK_CHECK(vkMapMemory(device, b.memory, 0, size, 0, &b.mapped));

    return b;
}

void destroyUMABuffer(VkDevice device, UMABuffer& b) {
    if (b.mapped) {
        vkUnmapMemory(device, b.memory);
        b.mapped = nullptr;
    }
    if (b.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, b.buffer, nullptr);
        b.buffer = VK_NULL_HANDLE;
    }
    if (b.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, b.memory, nullptr);
        b.memory = VK_NULL_HANDLE;
    }
}

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "🐺 REAL VULKAN COMPUTE BENCHMARK: ADRENO 660 (TURNIP MESA)" << std::endl;
    std::cout << "Snapdragon 888 (SM8350) - Xiaomi Mi 11 Ultra (star)" << std::endl;
    std::cout << "==========================================================" << std::endl;

    // 1. Vulkan Instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Lupin Adreno 660 Compute";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "LupinOS";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    VkInstance instance;
    VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &instance));

    // 2. Physical Device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::cerr << "No Vulkan devices found!" << std::endl;
        return 1;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    VkPhysicalDevice physicalDevice = devices[0];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    std::cout << "🎮 GPU Device: " << props.deviceName << " (Vulkan "
              << VK_VERSION_MAJOR(props.apiVersion) << "."
              << VK_VERSION_MINOR(props.apiVersion) << "."
              << VK_VERSION_PATCH(props.apiVersion) << ")" << std::endl;

    // 3. Logical Device with Compute Queue
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t computeFamily = 0;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            computeFamily = i;
            break;
        }
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = computeFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueInfo;

    VkDevice device;
    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));

    VkQueue queue;
    vkGetDeviceQueue(device, computeFamily, 0, &queue);

    // 4. Matrix Dimensions: 512 x 512 (FLOP count = 2 * 512^3 = 268.4 MFLOP)
    uint32_t M = 512, N = 512, K = 512;
    size_t countA = M * K;
    size_t countB = K * N;
    size_t countC = M * N;

    std::cout << "📐 Matrix GEMM: [" << M << "x" << K << "] * [" << K << "x" << N << "]" << std::endl;
    std::cout << "⚡ Total FLOPs: " << (2.0 * M * N * K) / 1e6 << " MFLOP" << std::endl;

    // Allocate UMA zero-copy buffers
    UMABuffer bufA = createUMABuffer(device, physicalDevice, countA * sizeof(float));
    UMABuffer bufB = createUMABuffer(device, physicalDevice, countB * sizeof(float));
    UMABuffer bufC = createUMABuffer(device, physicalDevice, countC * sizeof(float));

    float* ptrA = (float*)bufA.mapped;
    float* ptrB = (float*)bufB.mapped;
    float* ptrC = (float*)bufC.mapped;

    // Fill inputs
    for (size_t i = 0; i < countA; i++) ptrA[i] = 1.0f;
    for (size_t i = 0; i < countB; i++) ptrB[i] = 2.0f;
    std::memset(ptrC, 0, countC * sizeof(float));

    // 5. Load and create Compute Shader Module
    auto spvCode = readFile("/tmp/vulkan/gemm.spv");
    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = spvCode.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(spvCode.data());

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule));

    // 6. Descriptor Set Layout
    VkDescriptorSetLayoutBinding bindings[3]{};
    for (int i = 0; i < 3; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout descLayout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descLayout));

    // Push Constants
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipeLayoutInfo{};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &descLayout;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &pushRange;

    VkPipelineLayout pipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &pipelineLayout));

    // Compute Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;

    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));

    // Descriptor Pool & Set
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 3;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    VkDescriptorPool descPool;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descPool));

    VkDescriptorSetAllocateInfo setAllocInfo{};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = descPool;
    setAllocInfo.descriptorSetCount = 1;
    setAllocInfo.pSetLayouts = &descLayout;

    VkDescriptorSet descSet;
    VK_CHECK(vkAllocateDescriptorSets(device, &setAllocInfo, &descSet));

    // Write Descriptors
    VkDescriptorBufferInfo dbiA{ bufA.buffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo dbiB{ bufB.buffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo dbiC{ bufC.buffer, 0, VK_WHOLE_SIZE };

    VkWriteDescriptorSet writes[3]{};
    for (int i = 0; i < 3; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    writes[0].pBufferInfo = &dbiA;
    writes[1].pBufferInfo = &dbiB;
    writes[2].pBufferInfo = &dbiC;
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);

    // Command Pool & Buffer
    VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = computeFamily;

    VkCommandPool cmdPool;
    VK_CHECK(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &cmdPool));

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = cmdPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer));

    // Record Commands
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beginInfo));

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);

    PushConstants pc{ M, N, K };
    vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

    // Group size = 16x16, so 512/16 = 32 groups in X and Y
    vkCmdDispatch(cmdBuffer, (N + 15) / 16, (M + 15) / 16, 1);

    VK_CHECK(vkEndCommandBuffer(cmdBuffer));

    // Fence for Synchronization
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    // Warmup
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(device, 1, &fence));

    // Benchmark: 20 Iterations
    int iters = 20;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; i++) {
        VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));
        VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(device, 1, &fence));
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double avg_ms = total_ms / iters;
    double total_flops = 2.0 * (double)M * (double)N * (double)K;
    double gflops = (total_flops / (avg_ms / 1000.0)) / 1e9;

    // 7. Verify Checksum: Expected C[i] = K * (1.0 * 2.0) = 512 * 2.0 = 1024.0f
    float expected = (float)K * 2.0f;
    float actual_c0 = ptrC[0];
    float actual_cend = ptrC[countC - 1];

    std::cout << "----------------------------------------------------------" << std::endl;
    std::cout << "⏱️  Adreno 660 GPU Dispatch Latency: " << avg_ms << " ms" << std::endl;
    std::cout << "🚀 Compute Throughput: " << gflops << " GFLOPS" << std::endl;
    std::cout << "🔍 Verification: Expected=" << expected << ", C[0]=" << actual_c0
              << ", C[last]=" << actual_cend << std::endl;

    if (std::abs(actual_c0 - expected) < 1e-3f && std::abs(actual_cend - expected) < 1e-3f) {
        std::cout << "🎉 SUCCESS: 100% HARDWARE COMPUTATION VERIFIED ON ADRENO 660!" << std::endl;
    } else {
        std::cout << "❌ MISMATCH IN COMPUTATION!" << std::endl;
    }
    std::cout << "==========================================================" << std::endl;

    // Cleanup
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyDescriptorPool(device, descPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descLayout, nullptr);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    destroyUMABuffer(device, bufA);
    destroyUMABuffer(device, bufB);
    destroyUMABuffer(device, bufC);

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    return 0;
}
