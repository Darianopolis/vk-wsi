// #include "vk-wsi-test.hpp"

// void vkwsi_test_init(vkwsi_test* test)
// {
//     SDL_Init(SDL_INIT_VIDEO);
//     SDL_Vulkan_LoadLibrary(nullptr);
//     auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());

//     std::vector<const char*> instance_extensions {
//         VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
//         VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
//     };
//     {
//         Uint32 instance_extension_count;
//         auto* list = SDL_Vulkan_GetInstanceExtensions(&instance_extension_count);
//         for (uint32_t i = 0; i < instance_extension_count; ++i) {
//             instance_extensions.emplace_back(list[i]);
//         }
//     }

//     VkInstance instance;
//     vk_instance_fn(vkCreateInstance);

//     vk_check(vkCreateInstance(ptr_to(VkInstanceCreateInfo {
//         .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
//         .pApplicationInfo = ptr_to(VkApplicationInfo {
//             .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
//             .apiVersion = VK_API_VERSION_1_3,
//         }),
//         .enabledExtensionCount = uint32_t(instance_extensions.size()),
//         .ppEnabledExtensionNames = instance_extensions.data(),
//     }), nullptr, &instance));

//     vk_instance_fn(vkEnumeratePhysicalDevices);
//     vk_instance_fn(vkGetPhysicalDeviceProperties2);
//     vk_instance_fn(vkGetPhysicalDeviceQueueFamilyProperties);
//     vk_instance_fn(vkCreateDevice);
//     vk_instance_fn(vkGetDeviceProcAddr);
//     vk_instance_fn(vkGetPhysicalDeviceSurfaceFormatsKHR);
//     vk_instance_fn(vkGetPhysicalDeviceSurfaceCapabilities2KHR);
//     vk_instance_fn(vkDestroySurfaceKHR);
//     vk_instance_fn(vkDestroyDevice);
//     vk_instance_fn(vkDestroyInstance);

//     std::vector<VkPhysicalDevice> physical_devices;
//     vk_enumerate(physical_devices, vkEnumeratePhysicalDevices, instance);
//     for (uint32_t i = 0; i < physical_devices.size(); ++i) {
//         VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
//         vkGetPhysicalDeviceProperties2(physical_devices[i], &props);

//         std::cout << std::format(" device[{}] = {}\n", i, props.properties.deviceName);
//     }

//     std::cout << std::format("Number of GPUs: {}\n", physical_devices.size());

//     auto physical_device = physical_devices[0];
//     {
//         VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
//         vkGetPhysicalDeviceProperties2(physical_device, &props);

//         std::cout << std::format(" running on: {}\n", props.properties.deviceName);
//     }

//     // Find graphics queue

//     std::vector<VkQueueFamilyProperties> queue_props;
//     vk_enumerate(queue_props, vkGetPhysicalDeviceQueueFamilyProperties, physical_device);

//     uint32_t queue_family = ~0u;
//     for (uint32_t i = 0; i < queue_props.size(); ++i) {
//         if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
//             queue_family = i;
//             break;
//         }
//     }

//     // Create logical device

//     VkDevice device = {};
//     std::array device_extensions {
//         VK_KHR_SWAPCHAIN_EXTENSION_NAME,
//         VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
// #ifdef USE_SWAPCHAIN_MAINTENANCE
//         VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
// #endif
//     };

//     vk_check(vkCreateDevice(physical_device, ptr_to(VkDeviceCreateInfo {
//         .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
//         .pNext = ptr_to(VkPhysicalDeviceVulkan12Features {
//             .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
//             .pNext = ptr_to(VkPhysicalDeviceVulkan13Features {
//                 .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
// #ifdef USE_SWAPCHAIN_MAINTENANCE
//                     .pNext = ptr_to(VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT {
//                         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
//                         .swapchainMaintenance1 = true,
//                     }),
// #endif
//                 .synchronization2 = true,
//                 .dynamicRendering = true,
//             }),
//             .timelineSemaphore = true,
//         }),
//         .queueCreateInfoCount = 1,
//         .pQueueCreateInfos = ptr_to(VkDeviceQueueCreateInfo {
//             .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
//             .queueFamilyIndex = queue_family,
//             .queueCount = 1,
//             .pQueuePriorities = ptr_to(1.f),
//         }),
//         .enabledExtensionCount = uint32_t(device_extensions.size()),
//         .ppEnabledExtensionNames = device_extensions.data(),
//     }), nullptr, &device));

//     vk_device_fn(vkGetDeviceQueue);
//     vk_device_fn(vkCreateCommandPool);
//     vk_device_fn(vkAllocateCommandBuffers);
//     vk_device_fn(vkCreateSemaphore);
//     vk_device_fn(vkCreatePipelineLayout);
//     vk_device_fn(vkCreateDescriptorPool);
//     vk_device_fn(vkCreateGraphicsPipelines);
//     vk_device_fn(vkWaitForFences);
//     vk_device_fn(vkResetFences);
//     vk_device_fn(vkQueueWaitIdle);
//     vk_device_fn(vkDestroyImageView);
//     vk_device_fn(vkCreateSwapchainKHR);
//     vk_device_fn(vkDestroySwapchainKHR);
//     vk_device_fn(vkGetSwapchainImagesKHR);
//     vk_device_fn(vkCreateImageView);
//     vk_device_fn(vkAcquireNextImageKHR);
//     vk_device_fn(vkCmdPipelineBarrier2);
//     vk_device_fn(vkBeginCommandBuffer);
//     vk_device_fn(vkCmdBeginRendering);
//     vk_device_fn(vkCmdSetViewport);
//     vk_device_fn(vkCmdSetScissor);
//     vk_device_fn(vkCmdBindPipeline);
//     vk_device_fn(vkCmdDraw);
//     vk_device_fn(vkCmdEndRendering);
//     vk_device_fn(vkEndCommandBuffer);
//     vk_device_fn(vkQueueSubmit2);
//     vk_device_fn(vkQueuePresentKHR);
//     vk_device_fn(vkWaitSemaphores);
//     vk_device_fn(vkDestroyCommandPool);
//     vk_device_fn(vkDestroySemaphore);
//     vk_device_fn(vkDestroyPipelineLayout);
//     vk_device_fn(vkDestroyPipeline);
//     vk_device_fn(vkCreateFence);
//     vk_device_fn(vkDestroyFence);
//     vk_device_fn(vkDestroyDescriptorPool);
//     vk_device_fn(vkCmdClearColorImage);
//     vk_device_fn(vkResetCommandPool);

//     // Get graphics queue

//     VkQueue queue = {};
//     vkGetDeviceQueue(device, queue_family, 0, &queue);