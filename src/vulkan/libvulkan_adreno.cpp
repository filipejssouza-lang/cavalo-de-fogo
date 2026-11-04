#include <iostream>
#include <vector>
#include <fstream>
#include <cstring>
#include <vulkan/vulkan.h>

struct PushConstants {
    uint32_t M;
    uint32_t N;
    uint32_t K;
};

class VulkanComputeEngine {
public:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t computeFamily = 0;

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSet descSet = VK_NULL_HANDLE;

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    // Fixed 1024x1024 UMA Buffers
    VkBuffer bufA = VK_NULL_HANDLE, bufB = VK_NULL_HANDLE, bufC = VK_NULL_HANDLE;
    VkDeviceMemory memA = VK_NULL_HANDLE, memB = VK_NULL_HANDLE, memC = VK_NULL_HANDLE;
    float *ptrA = nullptr, *ptrB = nullptr, *ptrC = nullptr;
    size_t maxElements = 1024 * 1024;

    bool is_initialized = false;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    }

    void allocateUMA(VkBuffer& buffer, VkDeviceMemory& memory, float*& ptr, size_t bytes) {
        VkBufferCreateInfo bInfo{};
        bInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bInfo.size = bytes;
        bInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device, &bInfo, nullptr, &buffer);

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device, buffer, &req);

        uint32_t memType = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkMemoryAllocateInfo aInfo{};
        aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        aInfo.allocationSize = req.size;
        aInfo.memoryTypeIndex = memType;
        vkAllocateMemory(device, &aInfo, nullptr, &memory);
        vkBindBufferMemory(device, buffer, memory, 0);
        vkMapMemory(device, memory, 0, bytes, 0, (void**)&ptr);
    }

    bool init(const char* spv_path) {
        if (is_initialized) return true;

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Lupin Adreno 660 Shared";
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;
        if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) return false;

        uint32_t devCount = 0;
        vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
        if (devCount == 0) return false;
        std::vector<VkPhysicalDevice> devs(devCount);
        vkEnumeratePhysicalDevices(instance, &devCount, devs.data());
        physicalDevice = devs[0];

        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, qf.data());

        for (uint32_t i = 0; i < qfCount; i++) {
            if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                computeFamily = i;
                break;
            }
        }

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = computeFamily;
        qInfo.queueCount = 1;
        qInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo dInfo{};
        dInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.queueCreateInfoCount = 1;
        dInfo.pQueueCreateInfos = &qInfo;
        if (vkCreateDevice(physicalDevice, &dInfo, nullptr, &device) != VK_SUCCESS) return false;
        vkGetDeviceQueue(device, computeFamily, 0, &queue);

        // Load SPIR-V
        std::ifstream f(spv_path, std::ios::ate | std::ios::binary);
        if (!f.is_open()) return false;
        size_t sz = f.tellg();
        std::vector<char> spv(sz);
        f.seekg(0);
        f.read(spv.data(), sz);
        f.close();

        VkShaderModuleCreateInfo smInfo{};
        smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smInfo.codeSize = sz;
        smInfo.pCode = (const uint32_t*)spv.data();
        if (vkCreateShaderModule(device, &smInfo, nullptr, &shaderModule) != VK_SUCCESS) return false;

        // Descriptors & Pipeline
        VkDescriptorSetLayoutBinding b[3]{};
        for (int i = 0; i < 3; i++) {
            b[i].binding = i;
            b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[i].descriptorCount = 1;
            b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo dlInfo{};
        dlInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dlInfo.bindingCount = 3;
        dlInfo.pBindings = b;
        vkCreateDescriptorSetLayout(device, &dlInfo, nullptr, &descLayout);

        VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants) };
        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &descLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pcRange;
        vkCreatePipelineLayout(device, &plInfo, nullptr, &pipelineLayout);

        VkComputePipelineCreateInfo cpInfo{};
        cpInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpInfo.stage.module = shaderModule;
        cpInfo.stage.pName = "main";
        cpInfo.layout = pipelineLayout;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &pipeline);

        // Alloc UMA
        allocateUMA(bufA, memA, ptrA, maxElements * sizeof(float));
        allocateUMA(bufB, memB, ptrB, maxElements * sizeof(float));
        allocateUMA(bufC, memC, ptrC, maxElements * sizeof(float));

        VkDescriptorPoolSize pSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
        VkDescriptorPoolCreateInfo dpInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &pSize };
        vkCreateDescriptorPool(device, &dpInfo, nullptr, &descPool);

        VkDescriptorSetAllocateInfo dsInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descPool, 1, &descLayout };
        vkAllocateDescriptorSets(device, &dsInfo, &descSet);

        VkDescriptorBufferInfo dbiA{ bufA, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo dbiB{ bufB, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo dbiC{ bufC, 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet w[3]{};
        for (int i = 0; i < 3; i++) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = descSet;
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        w[0].pBufferInfo = &dbiA;
        w[1].pBufferInfo = &dbiB;
        w[2].pBufferInfo = &dbiC;
        vkUpdateDescriptorSets(device, 3, w, 0, nullptr);

        VkCommandPoolCreateInfo cpci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, 0, computeFamily };
        vkCreateCommandPool(device, &cpci, nullptr, &cmdPool);

        VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, cmdPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
        vkAllocateCommandBuffers(device, &cbai, &cmdBuffer);

        VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0 };
        vkCreateFence(device, &fci, nullptr, &fence);

        is_initialized = true;
        return true;
    }

    void compute(const float* a, const float* b, float* c, uint32_t M, uint32_t N, uint32_t K) {
        if (!is_initialized) return;

        size_t countA = M * K;
        size_t countB = K * N;
        size_t countC = M * N;

        std::memcpy(ptrA, a, countA * sizeof(float));
        std::memcpy(ptrB, b, countB * sizeof(float));

        vkResetCommandBuffer(cmdBuffer, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr };
        vkBeginCommandBuffer(cmdBuffer, &bi);

        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);

        PushConstants pc{ M, N, K };
        vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

        vkCmdDispatch(cmdBuffer, (N + 15) / 16, (M + 15) / 16, 1);
        vkEndCommandBuffer(cmdBuffer);

        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cmdBuffer, 0, nullptr };
        vkQueueSubmit(queue, 1, &si, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &fence);

        std::memcpy(c, ptrC, countC * sizeof(float));
    }

    void cleanup() {
        if (!is_initialized) return;
        vkDestroyFence(device, fence, nullptr);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        vkDestroyDescriptorPool(device, descPool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descLayout, nullptr);
        vkDestroyShaderModule(device, shaderModule, nullptr);

        vkUnmapMemory(device, memA);
        vkUnmapMemory(device, memB);
        vkUnmapMemory(device, memC);

        vkDestroyBuffer(device, bufA, nullptr);
        vkDestroyBuffer(device, bufB, nullptr);
        vkDestroyBuffer(device, bufC, nullptr);

        vkFreeMemory(device, memA, nullptr);
        vkFreeMemory(device, memB, nullptr);
        vkFreeMemory(device, memC, nullptr);

        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        is_initialized = false;
    }
};

static VulkanComputeEngine g_engine;

extern "C" {
    int vulkan_adreno_init(const char* spv_path) {
        return g_engine.init(spv_path) ? 1 : 0;
    }

    void vulkan_adreno_gemm(const float* a, const float* b, float* c, uint32_t M, uint32_t N, uint32_t K) {
        g_engine.compute(a, b, c, M, N, K);
    }

    void vulkan_adreno_cleanup() {
        g_engine.cleanup();
    }
}
