// Copyright 2022 Eli Bukchin
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <jni.h>
#include <sys/system_properties.h>

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN
#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"
#include "openxr/openxr_reflection.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CFATAL(msg, ...) __android_log_print(ANDROID_LOG_FATAL, "myoculustest", msg, ##__VA_ARGS__)
#define CERROR(msg, ...) __android_log_print(ANDROID_LOG_ERROR, "myoculustest", msg, ##__VA_ARGS__)
#define CWARN(msg, ...) __android_log_print(ANDROID_LOG_WARN, "myoculustest", msg, ##__VA_ARGS__)
#define CINFO(msg, ...) __android_log_print(ANDROID_LOG_INFO, "myoculustest", msg, ##__VA_ARGS__)
#define CDEBUG(msg, ...) __android_log_print(ANDROID_LOG_DEBUG, "myoculustest", msg, ##__VA_ARGS__)
#define CTRACE(msg, ...) __android_log_print(ANDROID_LOG_VERBOSE, "myoculustest", msg, ##__VA_ARGS__)

#define NUM_VIEWES 2
#define NUM_PIPELINE_STAGES 2
#define NUM_VERTEX_ATTRIBUTES 2
#define MAX_IMAGES 4

#define UNKNOWN_SIZE 2

#define CHECKXR(res, errmsg, ...)      \
    if (!XR_SUCCEEDED(res)) {          \
        CERROR(errmsg, ##__VA_ARGS__); \
        return false;                  \
    }

#define CHECKVK(res, errmsg, ...)      \
    if (res != VK_SUCCESS) {           \
        CERROR(errmsg, ##__VA_ARGS__); \
        return false;                  \
    }

#define array_size(arr) sizeof(arr) / sizeof(arr[0])

typedef enum Sides {
    SIDE_LEFT,
    SIDE_RIGHT,
    SIDE_COUNT
} Sides;

typedef struct AndroidAppState {
    ANativeWindow* window;
    bool resumed;
} AndroidAppState;

typedef struct RenderTarget {
    VkImage colorImage;
    VkImage depthImage;
    VkImageView colorView;
    VkImageView depthView;
    VkFramebuffer fb;
} RenderTarget;

typedef struct DepthBuffer {
    VkDeviceMemory depthMemory;
    VkImage depthImage;
    VkImageLayout vkLayout;
} DepthBuffer;

typedef struct RenderPass {
    VkFormat colorFmt;
    VkFormat depthFmt;
    VkRenderPass pass;
} RenderPass;

typedef struct Pipeline {
    VkPipeline pipe;
    VkPrimitiveTopology topology;
} Pipeline;

typedef struct SwapchainImageContext {
    XrSwapchainImageVulkan2KHR swapchainImages[MAX_IMAGES];
    RenderTarget renderTarget[MAX_IMAGES];
    uint32_t imageCount;
    VkExtent2D size;
    DepthBuffer depthBuffer;
    RenderPass rp;
    Pipeline pipe;
    XrStructureType swapchainImageType;
} SwapchainImageContext;

typedef struct ShaderProgram {
    VkPipelineShaderStageCreateInfo shaderInfo[NUM_PIPELINE_STAGES];
} ShaderProgram;

typedef enum CmdBufferState {
    CBR_STATE_Undefined,
    CBR_STATE_Initialized,
    CBR_STATE_Recording,
    CBR_STATE_Executable,
    CBR_STATE_Executing
} CmdBufferState;

static char* CBR_STATE_STR[] = {
    "Undefined",
    "Initialized",
    "Recording",
    "Executable",
    "Executing"};

static char* VISULAIZED_SPACES[] = {
    "ViewFront",
    "Local",
    "Stage",
    "StageLeft",
    "StageRight",
    "StageLeftRotated",
    "StageRightRotated"};

static const uint32_t vertSpv[] = {0x07230203, 0x00010000, 0x000d000a, 0x00000029,
                                   0x00000000, 0x00020011, 0x00000001, 0x0006000b,
                                   0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
                                   0x00000000, 0x0003000e, 0x00000000, 0x00000001,
                                   0x0009000f, 0x00000000, 0x00000004, 0x6e69616d,
                                   0x00000000, 0x00000009, 0x0000000c, 0x00000017,
                                   0x00000021, 0x00030003, 0x00000002, 0x00000190,
                                   0x00090004, 0x415f4c47, 0x735f4252, 0x72617065,
                                   0x5f657461, 0x64616873, 0x6f5f7265, 0x63656a62,
                                   0x00007374, 0x00090004, 0x415f4c47, 0x735f4252,
                                   0x69646168, 0x6c5f676e, 0x75676e61, 0x5f656761,
                                   0x70303234, 0x006b6361, 0x000a0004, 0x475f4c47,
                                   0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79,
                                   0x656e696c, 0x7269645f, 0x69746365, 0x00006576,
                                   0x00080004, 0x475f4c47, 0x4c474f4f, 0x6e695f45,
                                   0x64756c63, 0x69645f65, 0x74636572, 0x00657669,
                                   0x00040005, 0x00000004, 0x6e69616d, 0x00000000,
                                   0x00040005, 0x00000009, 0x6c6f436f, 0x0000726f,
                                   0x00040005, 0x0000000c, 0x6f6c6f43, 0x00000072,
                                   0x00060005, 0x00000015, 0x505f6c67, 0x65567265,
                                   0x78657472, 0x00000000, 0x00060006, 0x00000015,
                                   0x00000000, 0x505f6c67, 0x7469736f, 0x006e6f69,
                                   0x00030005, 0x00000017, 0x00000000, 0x00030005,
                                   0x0000001b, 0x00667562, 0x00040006, 0x0000001b,
                                   0x00000000, 0x0070766d, 0x00040005, 0x0000001d,
                                   0x66756275, 0x00000000, 0x00050005, 0x00000021,
                                   0x69736f50, 0x6e6f6974, 0x00000000, 0x00040047,
                                   0x00000009, 0x0000001e, 0x00000000, 0x00040047,
                                   0x0000000c, 0x0000001e, 0x00000001, 0x00050048,
                                   0x00000015, 0x00000000, 0x0000000b, 0x00000000,
                                   0x00030047, 0x00000015, 0x00000002, 0x00040048,
                                   0x0000001b, 0x00000000, 0x00000005, 0x00050048,
                                   0x0000001b, 0x00000000, 0x00000023, 0x00000000,
                                   0x00050048, 0x0000001b, 0x00000000, 0x00000007,
                                   0x00000010, 0x00030047, 0x0000001b, 0x00000002,
                                   0x00040047, 0x00000021, 0x0000001e, 0x00000000,
                                   0x00020013, 0x00000002, 0x00030021, 0x00000003,
                                   0x00000002, 0x00030016, 0x00000006, 0x00000020,
                                   0x00040017, 0x00000007, 0x00000006, 0x00000004,
                                   0x00040020, 0x00000008, 0x00000003, 0x00000007,
                                   0x0004003b, 0x00000008, 0x00000009, 0x00000003,
                                   0x00040017, 0x0000000a, 0x00000006, 0x00000003,
                                   0x00040020, 0x0000000b, 0x00000001, 0x0000000a,
                                   0x0004003b, 0x0000000b, 0x0000000c, 0x00000001,
                                   0x0004002b, 0x00000006, 0x00000010, 0x3f800000,
                                   0x00040015, 0x00000011, 0x00000020, 0x00000000,
                                   0x0004002b, 0x00000011, 0x00000012, 0x00000003,
                                   0x00040020, 0x00000013, 0x00000003, 0x00000006,
                                   0x0003001e, 0x00000015, 0x00000007, 0x00040020,
                                   0x00000016, 0x00000003, 0x00000015, 0x0004003b,
                                   0x00000016, 0x00000017, 0x00000003, 0x00040015,
                                   0x00000018, 0x00000020, 0x00000001, 0x0004002b,
                                   0x00000018, 0x00000019, 0x00000000, 0x00040018,
                                   0x0000001a, 0x00000007, 0x00000004, 0x0003001e,
                                   0x0000001b, 0x0000001a, 0x00040020, 0x0000001c,
                                   0x00000009, 0x0000001b, 0x0004003b, 0x0000001c,
                                   0x0000001d, 0x00000009, 0x00040020, 0x0000001e,
                                   0x00000009, 0x0000001a, 0x0004003b, 0x0000000b,
                                   0x00000021, 0x00000001, 0x00050036, 0x00000002,
                                   0x00000004, 0x00000000, 0x00000003, 0x000200f8,
                                   0x00000005, 0x0004003d, 0x0000000a, 0x0000000d,
                                   0x0000000c, 0x0004003d, 0x00000007, 0x0000000e,
                                   0x00000009, 0x0009004f, 0x00000007, 0x0000000f,
                                   0x0000000e, 0x0000000d, 0x00000004, 0x00000005,
                                   0x00000006, 0x00000003, 0x0003003e, 0x00000009,
                                   0x0000000f, 0x00050041, 0x00000013, 0x00000014,
                                   0x00000009, 0x00000012, 0x0003003e, 0x00000014,
                                   0x00000010, 0x00050041, 0x0000001e, 0x0000001f,
                                   0x0000001d, 0x00000019, 0x0004003d, 0x0000001a,
                                   0x00000020, 0x0000001f, 0x0004003d, 0x0000000a,
                                   0x00000022, 0x00000021, 0x00050051, 0x00000006,
                                   0x00000023, 0x00000022, 0x00000000, 0x00050051,
                                   0x00000006, 0x00000024, 0x00000022, 0x00000001,
                                   0x00050051, 0x00000006, 0x00000025, 0x00000022,
                                   0x00000002, 0x00070050, 0x00000007, 0x00000026,
                                   0x00000023, 0x00000024, 0x00000025, 0x00000010,
                                   0x00050091, 0x00000007, 0x00000027, 0x00000020,
                                   0x00000026, 0x00050041, 0x00000008, 0x00000028,
                                   0x00000017, 0x00000019, 0x0003003e, 0x00000028,
                                   0x00000027, 0x000100fd, 0x00010038};

static const uint32_t fragSpv[] = {0x07230203, 0x00010000, 0x000d000a, 0x0000000d,
                                   0x00000000, 0x00020011, 0x00000001, 0x0006000b,
                                   0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
                                   0x00000000, 0x0003000e, 0x00000000, 0x00000001,
                                   0x0007000f, 0x00000004, 0x00000004, 0x6e69616d,
                                   0x00000000, 0x00000009, 0x0000000b, 0x00030010,
                                   0x00000004, 0x00000007, 0x00030003, 0x00000002,
                                   0x00000190, 0x00090004, 0x415f4c47, 0x735f4252,
                                   0x72617065, 0x5f657461, 0x64616873, 0x6f5f7265,
                                   0x63656a62, 0x00007374, 0x00090004, 0x415f4c47,
                                   0x735f4252, 0x69646168, 0x6c5f676e, 0x75676e61,
                                   0x5f656761, 0x70303234, 0x006b6361, 0x000a0004,
                                   0x475f4c47, 0x4c474f4f, 0x70635f45, 0x74735f70,
                                   0x5f656c79, 0x656e696c, 0x7269645f, 0x69746365,
                                   0x00006576, 0x00080004, 0x475f4c47, 0x4c474f4f,
                                   0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572,
                                   0x00657669, 0x00040005, 0x00000004, 0x6e69616d,
                                   0x00000000, 0x00050005, 0x00000009, 0x67617246,
                                   0x6f6c6f43, 0x00000072, 0x00040005, 0x0000000b,
                                   0x6c6f436f, 0x0000726f, 0x00040047, 0x00000009,
                                   0x0000001e, 0x00000000, 0x00040047, 0x0000000b,
                                   0x0000001e, 0x00000000, 0x00020013, 0x00000002,
                                   0x00030021, 0x00000003, 0x00000002, 0x00030016,
                                   0x00000006, 0x00000020, 0x00040017, 0x00000007,
                                   0x00000006, 0x00000004, 0x00040020, 0x00000008,
                                   0x00000003, 0x00000007, 0x0004003b, 0x00000008,
                                   0x00000009, 0x00000003, 0x00040020, 0x0000000a,
                                   0x00000001, 0x00000007, 0x0004003b, 0x0000000a,
                                   0x0000000b, 0x00000001, 0x00050036, 0x00000002,
                                   0x00000004, 0x00000000, 0x00000003, 0x000200f8,
                                   0x00000005, 0x0004003d, 0x00000007, 0x0000000c,
                                   0x0000000b, 0x0003003e, 0x00000009, 0x0000000c,
                                   0x000100fd, 0x00010038};

typedef struct Vertex {
    XrVector3f pos;
    XrVector3f color;
} Vertex;

#define Red \
    { 1, 0, 0 }
#define DarkRed \
    { 0.25f, 0, 0 }
#define Green \
    { 0, 1, 0 }
#define DarkGreen \
    { 0, 0.25f, 0 }
#define Blue \
    { 0, 0, 1 }
#define DarkBlue \
    { 0, 0, 0.25f }

// Vertices for a 1x1x1 meter cube. (Left/Right, Top/Bottom, Front/Back)
#define LBB \
    { -0.5f, -0.5f, -0.5f }
#define LBF \
    { -0.5f, -0.5f, 0.5f }
#define LTB \
    { -0.5f, 0.5f, -0.5f }
#define LTF \
    { -0.5f, 0.5f, 0.5f }
#define RBB \
    { 0.5f, -0.5f, -0.5f }
#define RBF \
    { 0.5f, -0.5f, 0.5f }
#define RTB \
    { 0.5f, 0.5f, -0.5f }
#define RTF \
    { 0.5f, 0.5f, 0.5f }

#define CUBE_SIDE(V1, V2, V3, V4, V5, V6, COLOR) {V1, COLOR}, {V2, COLOR}, {V3, COLOR}, {V4, COLOR}, {V5, COLOR}, {V6, COLOR},

const Vertex cubeVertices[] = {
    CUBE_SIDE(LTB, LBF, LBB, LTB, LTF, LBF, DarkRed)    // -X
    CUBE_SIDE(RTB, RBB, RBF, RTB, RBF, RTF, Red)        // +X
    CUBE_SIDE(LBB, LBF, RBF, LBB, RBF, RBB, DarkGreen)  // -Y
    CUBE_SIDE(LTB, RTB, RTF, LTB, RTF, LTF, Green)      // +Y
    CUBE_SIDE(LBB, RBB, RTB, LBB, RTB, LTB, DarkBlue)   // -Z
    CUBE_SIDE(LBF, LTF, RTF, LBF, RTF, RBF, Blue)       // +Z
};

// Winding order is clockwise. Each side uses a different color.
const uint16_t cubeIndices[] = {
    0, 1, 2, 3, 4, 5,        // -X
    6, 7, 8, 9, 10, 11,      // +X
    12, 13, 14, 15, 16, 17,  // -Y
    18, 19, 20, 21, 22, 23,  // +Y
    24, 25, 26, 27, 28, 29,  // -Z
    30, 31, 32, 33, 34, 35,  // +Z
};

static XrPosef pose_identity() {
    XrPosef result = {
        .orientation.w = 1.0f};
    return result;
}

static XrPosef pose_translation(const XrVector3f translation) {
    XrPosef result = pose_identity();
    result.position = translation;
    return result;
}

static XrPosef pose_rotateCCW_about_Yaxis(float radians, XrVector3f translation) {
    XrPosef result = pose_identity();
    result.orientation.x = 0.f;
    result.orientation.y = sinf(radians * 0.5f);
    result.orientation.z = 0.f;
    result.orientation.w = cosf(radians * 0.5f);
    result.position = translation;
    return result;
}

typedef struct CmdBuffer {
    CmdBufferState state;
    VkCommandPool pool;
    VkCommandBuffer buf;
    VkFence execFence;
} CmdBuffer;

typedef struct VertexBuffer {
    VkBuffer idxBuf;
    VkDeviceMemory idxMem;
    uint32_t idxCount;
    VkBuffer vtxBuf;
    VkDeviceMemory vtxMem;
    uint32_t vtxCount;
    VkVertexInputBindingDescription bindDesc;
    VkVertexInputAttributeDescription attrDesc[NUM_VERTEX_ATTRIBUTES];
} VertexBuffer;

typedef struct VulkanState {
    SwapchainImageContext swapchainImageContext[NUM_VIEWES];
    // std::map<const XrSwapchainImageBaseHeader*, SwapchainImageContext*> m_swapchainImageContextMap; ????

    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    uint32_t queueFamilyIndex;  // NOTE: Graphics queue
    VkQueue queue;
    VkSemaphore drawDone;

    VkPhysicalDeviceMemoryProperties memProps;
    ShaderProgram shaderProgram;
    CmdBuffer cmdBuffer;
    VkPipelineLayout pipelineLayout;
    VertexBuffer drawBuffer;
#if defined(NDEBUG)
    VkDebugUtilsMessengerEXT debugMessenger;
#endif
} VulkanState;

typedef struct XrInputState {
    XrActionSet actionsSet;
    XrAction grabAction;
    XrAction poseAction;
    XrAction vibrateAction;
    XrAction quitAction;
    XrPath handSubActionPath[SIDE_COUNT];
    XrSpace handSpace[SIDE_COUNT];
    float handScale[SIDE_COUNT];
    XrBool32 handActive[SIDE_COUNT];
} XrInputState;

typedef struct Swapchain {
    XrSwapchain handle;
    int32_t width;
    int32_t height;
} Swapchain;

typedef struct OpenXrProgram {
    XrInstance instance;
    XrSession session;
    XrSpace space;
    XrFormFactor formFactor;
    XrViewConfigurationType viewConfigType;
    XrEnvironmentBlendMode environmentBlendMode;
    XrSystemId systemID;
    XrGraphicsBindingVulkan2KHR graphicsBinding;
    XrViewConfigurationView configViews[NUM_VIEWES];
    Swapchain swapchains[NUM_VIEWES];
    XrView views[NUM_VIEWES];
    int64_t colorSwapchainFormat;
    XrSpace visualizedSpaces[array_size(VISULAIZED_SPACES)];
    XrSessionState sessionState;
    bool sessionRunning;
    XrEventDataBuffer eventDataBuffer;
    XrInputState input;
} OpenXrProgram;

static void app_handle_cmd(struct android_app* app, int32_t cmd) {
    AndroidAppState* state = (AndroidAppState*)app->userData;

    switch (cmd) {
        case APP_CMD_START: {
            CINFO("onStart()");
        } break;
        case APP_CMD_RESUME: {
            CINFO("onResume()");
            state->resumed = true;
        } break;
        case APP_CMD_PAUSE: {
            CINFO("onPause()");
            state->resumed = false;
        } break;
        case APP_CMD_STOP: {
            CINFO("onStop()");
        } break;
        case APP_CMD_DESTROY: {
            CINFO("onDestroy()");
            state->window = 0;
        } break;
        case APP_CMD_INIT_WINDOW: {
            CINFO("surfaceCreated()");
            state->window = app->window;
        } break;
        case APP_CMD_TERM_WINDOW: {
            CINFO("surfaceDestroyed()");
            state->window = 0;
        } break;
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: {
            CTRACE("%s", pCallbackData->pMessage);
        } break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: {
            CINFO("%s", pCallbackData->pMessage);
        } break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
            CWARN("%s", pCallbackData->pMessage);
        } break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
            CERROR("%s", pCallbackData->pMessage);
        } break;

        default:
            break;
    }
    return VK_FALSE;
}

static bool vulkan_commandbuffer_init(VkDevice device, uint32_t queueFamily, CmdBuffer* cbr) {
    VkCommandPoolCreateInfo commandPoolCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamily};

    VkResult result = vkCreateCommandPool(device, &commandPoolCI, 0, &cbr->pool);
    CHECKVK(result, "Failed to create command pool");

    VkCommandBufferAllocateInfo cbrAI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cbr->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};

    result = vkAllocateCommandBuffers(device, &cbrAI, &cbr->buf);
    CHECKVK(result, "Failed to allocate command buffer");

    VkFenceCreateInfo fenceCI = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    result = vkCreateFence(device, &fenceCI, 0, &cbr->execFence);
    CHECKVK(result, "Failed to allocate command buffer execution fence");

    cbr->state = CBR_STATE_Initialized;
    return true;
}

static bool vulkan_buffer_allocate(VkDevice device, VkMemoryRequirements memReq, VkPhysicalDeviceMemoryProperties* deviceMem, VkFlags flags, VkDeviceMemory* out) {
    for (uint32_t i = 0; i < deviceMem->memoryTypeCount; ++i) {
        if ((memReq.memoryTypeBits & (1 << i)) != 0u) {
            // Type is available, does it match user properties?
            if ((deviceMem->memoryTypes[i].propertyFlags & flags) == flags) {
                VkMemoryAllocateInfo memoryAI = {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .allocationSize = memReq.size,
                    .memoryTypeIndex = i};

                VkResult result = vkAllocateMemory(device, &memoryAI, 0, out);
                return result == VK_SUCCESS;
            }
        }
    }
    return false;
}

static bool vulkan_vertex_buffer_create(VkDevice device, VkPhysicalDeviceMemoryProperties* deviceMem, uint32_t indexCount, uint32_t vertexCount, VertexBuffer* buf) {
    VkBufferCreateInfo bufferCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .size = sizeof(uint16_t) * indexCount};
    VkResult result = vkCreateBuffer(device, &bufferCI, 0, &buf->idxBuf);
    CHECKVK(result, "Failed to craete index buffer");

    {
        VkMemoryRequirements memReq = {};
        vkGetBufferMemoryRequirements(device, buf->idxBuf, &memReq);
        if (!vulkan_buffer_allocate(
                device,
                memReq,
                deviceMem,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &buf->idxMem)) {
            CERROR("Failed to allocate index buffer memory");
            return false;
        }
    }

    result = vkBindBufferMemory(device, buf->idxBuf, buf->idxMem, 0);
    CHECKVK(result, "Failed to bind index buffer memory");

    bufferCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferCI.size = sizeof(Vertex) * vertexCount;
    result = vkCreateBuffer(device, &bufferCI, 0, &buf->vtxBuf);
    CHECKVK(result, "Failed to craete vertex buffer");

    {
        VkMemoryRequirements memReq = {};
        vkGetBufferMemoryRequirements(device, buf->vtxBuf, &memReq);
        if (!vulkan_buffer_allocate(
                device,
                memReq,
                deviceMem,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &buf->vtxMem)) {
            CERROR("Failed to allocate vertex buffer memory");
            return false;
        }
    }

    result = vkBindBufferMemory(device, buf->vtxBuf, buf->vtxMem, 0);
    CHECKVK(result, "Failed to bind vertex buffer memory");

    buf->bindDesc.binding = 0;
    buf->bindDesc.stride = sizeof(Vertex);
    buf->bindDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    buf->idxCount = indexCount;
    buf->vtxCount = vertexCount;

    return true;
}

static bool vulkan_buffer_update(VkDevice device, VkDeviceMemory mem, size_t size, void* data) {
    void* map = 0;
    VkResult result = vkMapMemory(device, mem, 0, size, 0, &map);
    CHECKVK(result, "Failed to map memory");
    memcpy(map, data, size);
    vkUnmapMemory(device, mem);
    return true;
}

static bool vulkan_initialize_resources(VulkanState* vulkan) {
    vulkan->shaderProgram.shaderInfo[0] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pName = "main",
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
    };

    VkShaderModuleCreateInfo moduleCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pCode = vertSpv,
        .codeSize = sizeof(vertSpv)};
    VkResult result = vkCreateShaderModule(vulkan->device, &moduleCI, 0, &vulkan->shaderProgram.shaderInfo[0].module);
    CHECKVK(result, "Failed to create Vertex shader");

    vulkan->shaderProgram.shaderInfo[1] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pName = "main",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    moduleCI.pCode = fragSpv;
    moduleCI.codeSize = sizeof(fragSpv);
    result = vkCreateShaderModule(vulkan->device, &moduleCI, 0, &vulkan->shaderProgram.shaderInfo[1].module);
    CHECKVK(result, "Failed to create Fragment shader");

    VkSemaphoreCreateInfo semaphoreCI = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    result = vkCreateSemaphore(vulkan->device, &semaphoreCI, 0, &vulkan->drawDone);
    CHECKVK(result, "Failed to create Draw Done semaphore");

    if (!vulkan_commandbuffer_init(vulkan->device, vulkan->queueFamilyIndex, &vulkan->cmdBuffer)) {
        CERROR("Failed to initialize commandbuffer");
        return false;
    }

    {
        VkPushConstantRange pcr = {
            .offset = 0,
            .size = 4 * 4 * sizeof(float),
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT};

        VkPipelineLayoutCreateInfo pipelineLayoutCI = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pcr};

        VkResult result = vkCreatePipelineLayout(vulkan->device, &pipelineLayoutCI, 0, &vulkan->pipelineLayout);
        CHECKVK(result, "Failed to create pipeline layout");
    }

    vulkan->drawBuffer.attrDesc[0] = (VkVertexInputAttributeDescription){
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, pos)};

    vulkan->drawBuffer.attrDesc[1] = (VkVertexInputAttributeDescription){
        .location = 1,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, color)};

    uint32_t indexCount = array_size(cubeIndices);
    uint32_t vertexCount = array_size(cubeVertices);

    if (!vulkan_vertex_buffer_create(vulkan->device, &vulkan->memProps, indexCount, vertexCount, &vulkan->drawBuffer)) {
        CERROR("Failed to create buffers");
        return false;
    }

    if (!vulkan_buffer_update(vulkan->device, vulkan->drawBuffer.idxMem, sizeof(uint16_t) * indexCount, (void*)cubeIndices)) {
        CERROR("Failed to update index buffer");
        return false;
    }

    if (!vulkan_buffer_update(vulkan->device, vulkan->drawBuffer.vtxMem, sizeof(Vertex) * vertexCount, (void*)cubeVertices)) {
        CERROR("Failed to update vertex buffer");
        return false;
    }

    return true;
}

static bool vulkan_find_layer(const char* layer) {
    uint32_t layerCount;
    VkResult result = vkEnumerateInstanceLayerProperties(&layerCount, 0);
    CHECKVK(result, "Failed to count Vulkan Layer Properties");

    if (layerCount > 0) {
        VkLayerProperties* layers = malloc(sizeof(VkLayerProperties) * layerCount);
        if (!layers) {
            CERROR("Failed to allocate memory for layer enumeration");
            return false;
        }

        result = vkEnumerateInstanceLayerProperties(&layerCount, layers);
        CHECKVK(result, "Failed to get Vulkan Layer Properties");

        for (uint32_t i = 0; i < layerCount; ++i) {
            if (0 == strcmp(layer, layers[i].layerName)) {
                free(layers);
                return true;
            }
        }

        free(layers);
    }
    return false;
}

static bool vulkan_initialize_device(OpenXrProgram* program, VulkanState* vulkan) {
    XrGraphicsRequirementsVulkan2KHR graphicsRequirements = {
        .type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};

    {
        PFN_xrGetVulkanGraphicsRequirements2KHR func = 0;
        XrResult result = xrGetInstanceProcAddr(
            program->instance,
            "xrGetVulkanGraphicsRequirements2KHR",
            (PFN_xrVoidFunction*)&func);
        CHECKXR(result, "Failld to get 'xrGetVulkanGraphicsRequirements2KHR'");

        result = func(program->instance, program->systemID, &graphicsRequirements);
        CHECKXR(result, "Failed to get graphoc requirements");
    }

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "MyOculusTest",
        .applicationVersion = 1,
        .pEngineName = "MyOculusTest",
        .apiVersion = VK_API_VERSION_1_0};

    VkInstanceCreateInfo instanceCI = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo};

    uint32_t extensionCount = 0;
    const char* requiredExtensions[128];
    uint32_t layerCount = 0;
    const char* requiredLayers[128];

#if defined(NDEBUG)
    char* validationLayer = "VK_LAYER_KHRONOS_validation";
    requiredLayers[layerCount++] = validationLayer;

    char* debugExtension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    requiredExtensions[extensionCount++] = debugExtension;
#endif

    for (uint32_t i = 0; i < layerCount; ++i) {
        if (!vulkan_find_layer(requiredLayers[i])) {
            CERROR("Missing layer: %s", requiredLayers[i]);
            return false;
        }
    }

    instanceCI.enabledLayerCount = layerCount;
    instanceCI.ppEnabledLayerNames = requiredLayers;
    instanceCI.enabledExtensionCount = extensionCount;
    instanceCI.ppEnabledExtensionNames = requiredExtensions;

#if defined(NDEBUG)
    VkDebugUtilsMessengerCreateInfoEXT debugUtilsCI = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = vulkan_debug_callback};
    {
        VkBaseInStructure** next = (VkBaseInStructure**)&instanceCI.pNext;
        while (*next) {
            next = &(*next)->pNext;
        }
        *next = &debugUtilsCI;
    }
#endif

    XrVulkanInstanceCreateInfoKHR ci = {
        .type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR,
        .systemId = program->systemID,
        .pfnGetInstanceProcAddr = &vkGetInstanceProcAddr,
        .vulkanCreateInfo = &instanceCI,
        0};

    {
        PFN_xrCreateVulkanInstanceKHR func = 0;
        XrResult result = xrGetInstanceProcAddr(
            program->instance,
            "xrCreateVulkanInstanceKHR",
            (PFN_xrVoidFunction*)&func);
        CHECKXR(result, "Failed to find 'xrCreateVulkanInstanceKHR'");

        VkResult vkres;
        result = func(program->instance, &ci, &vulkan->instance, &vkres);
        CHECKXR(result, "Failed to create Vulkan Instance [XR]");
        CHECKVK(vkres, "Failed to create Vulkan Instance [VK]");
    }
#if defined(NDEBUG)
    {
        PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vulkan->instance, "vkCreateDebugUtilsMessengerEXT");
        if (!func || func(vulkan->instance, &debugUtilsCI, 0, &vulkan->debugMessenger)) {
            CERROR("Failed to create debug messenger");
            return false;
        }
    }
#endif

    XrVulkanGraphicsDeviceGetInfoKHR deviceGI = {
        .type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR,
        .systemId = program->systemID,
        .vulkanInstance = vulkan->instance};

    {
        PFN_xrGetVulkanGraphicsDevice2KHR func = 0;
        XrResult result = xrGetInstanceProcAddr(
            program->instance,
            "xrGetVulkanGraphicsDevice2KHR",
            (PFN_xrVoidFunction*)&func);
        CHECKXR(result, "Failed to find 'xrGetVulkanGraphicsDevice2KHR'");

        result = func(program->instance, &deviceGI, &vulkan->physical);
        CHECKXR(result, "Failed to get physical device");
    }

    float queuePriorities = 0;
    VkDeviceQueueCreateInfo queueCI = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount = 1,
        .pQueuePriorities = &queuePriorities};
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(vulkan->physical, &queueFamilyCount, 0);
        if (!queueFamilyCount) {
            CERROR("Device has no queue families!");
            return false;
        }
        VkQueueFamilyProperties queueFamilies[128];
        vkGetPhysicalDeviceQueueFamilyProperties(vulkan->physical, &queueFamilyCount, queueFamilies);

        bool found = false;
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
                vulkan->queueFamilyIndex = queueCI.queueFamilyIndex = i;
                found = true;
                break;
            }
        }

        if (!found) {
            CERROR("Failed to find Graphics Queue family");
            return false;
        }
    }

    VkPhysicalDeviceFeatures features = {};
    VkDeviceCreateInfo deviceCI = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCI,
        .pEnabledFeatures = &features};

    XrVulkanDeviceCreateInfoKHR xrDeviceCI = {
        .type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR,
        .systemId = program->systemID,
        .pfnGetInstanceProcAddr = &vkGetInstanceProcAddr,
        .vulkanCreateInfo = &deviceCI,
        .vulkanPhysicalDevice = vulkan->physical,
        .vulkanAllocator = 0};

    {
        PFN_xrCreateVulkanDeviceKHR func = 0;
        XrResult result = xrGetInstanceProcAddr(
            program->instance,
            "xrCreateVulkanDeviceKHR",
            (PFN_xrVoidFunction*)&func);
        CHECKXR(result, "Failed to find 'xrCreateVulkanDeviceKHR'");

        VkResult vkresult;
        result = func(program->instance, &xrDeviceCI, &vulkan->device, &vkresult);
        CHECKXR(result, "Failed to create vulkan logical device [XR]");
        CHECKVK(vkresult, "Failed to create vulkan logical device [VK]");
    }

    vkGetDeviceQueue(vulkan->device, queueCI.queueFamilyIndex, 0, &vulkan->queue);

    vkGetPhysicalDeviceMemoryProperties(vulkan->physical, &vulkan->memProps);

    if (!vulkan_initialize_resources(vulkan)) {
        return false;
    }

    program->graphicsBinding.instance = vulkan->instance;
    program->graphicsBinding.physicalDevice = vulkan->physical;
    program->graphicsBinding.device = vulkan->device;
    program->graphicsBinding.queueFamilyIndex = queueCI.queueFamilyIndex;
    program->graphicsBinding.queueIndex = 0;
    return true;
}

static bool program_log_extensions(char* layer, char* indent) {
    XrResult result;
    uint32_t extensionCount;
    result = xrEnumerateInstanceExtensionProperties(0, 0, &extensionCount, 0);
    CHECKXR(result, "Failed to count Instance Extensions");

    if (extensionCount > 0) {
        XrExtensionProperties* extensions = malloc(sizeof(XrExtensionProperties) * extensionCount);
        for (uint32_t i = 0; i < extensionCount; ++i) {
            extensions[i] = (XrExtensionProperties){.type = XR_TYPE_EXTENSION_PROPERTIES};
        }

        result = xrEnumerateInstanceExtensionProperties(0, extensionCount, &extensionCount, extensions);
        if (!XR_SUCCEEDED(result)) {
            CERROR("Failed to get Instance Extensions");
            free(extensions);
            return false;
        }

        CTRACE("%sAvailable Extensions [%u]:", indent, extensionCount);
        for (uint32_t i = 0; i < extensionCount; ++i) {
            CTRACE("%s  %s [%d]", indent, extensions[i].extensionName, extensions[i].extensionVersion);
        }

        free(extensions);
    } else {
        CTRACE("%sNo extensions available", indent);
    }
    return true;
}

static bool program_craete_instance(OpenXrProgram* program, XrInstanceCreateInfoAndroidKHR* androidInstanceCI) {
    // NOTE: log extensions
    if (!program_log_extensions(0, "")) {
        return false;
    }

    // NOTE: log layers
    {
        uint32_t layerCount;
        XrResult result = xrEnumerateApiLayerProperties(0, &layerCount, 0);
        CHECKXR(result, "Failed to count Api Layers");

        if (layerCount > 0) {
            XrApiLayerProperties* layers = malloc(sizeof(XrApiLayerProperties) * layerCount);
            for (uint32_t i = 0; i < layerCount; ++i) {
                layers[i] = (XrApiLayerProperties){.type = XR_TYPE_API_LAYER_PROPERTIES};
            }

            result = xrEnumerateApiLayerProperties(layerCount, &layerCount, layers);
            if (!XR_SUCCEEDED(result)) {
                CERROR("Failed to get Api Layers");
                free(layers);
                return false;
            }

            CTRACE("Available Layers [%u]:", layerCount);
            for (uint32_t i = 0; i < layerCount; ++i) {
                CTRACE("  %s [%llu:%u]: %s", layers[i].layerName, layers[i].specVersion, layers[i].layerVersion, layers[i].description);
                program_log_extensions(layers[i].layerName, "    ");
            }
            free(layers);
        } else {
            CTRACE("No layers Available");
        }
    }

    const char* extensions[] = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};

    XrInstanceCreateInfo instanceCI = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO,
        .enabledExtensionCount = 2,
        .enabledExtensionNames = extensions,
        .next = (XrBaseInStructure*)androidInstanceCI};

    strcpy(instanceCI.applicationInfo.applicationName, "MyOculusTest");
    instanceCI.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    {
        XrResult result = xrCreateInstance(&instanceCI, &program->instance);
        CHECKXR(result, "Failed to create XR instance");

        XrInstanceProperties instanceProps = {
            .type = XR_TYPE_INSTANCE_PROPERTIES};
        result = xrGetInstanceProperties(program->instance, &instanceProps);
        CHECKXR(result, "Failed to get instance properties");

        CINFO("Instance: '%s' [%llu]", instanceProps.runtimeName, instanceProps.runtimeVersion);
    }

    return true;
}

static bool program_initialize_system(OpenXrProgram* program, VulkanState* vulkan) {
    if (program->instance == 0) {
        CERROR("Instance not initialized!");
        return false;
    }

    program->formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    program->viewConfigType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    program->environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    XrSystemGetInfo systemGI = {
        .type = XR_TYPE_SYSTEM_GET_INFO,
        .formFactor = program->formFactor};
    XrResult result = xrGetSystem(program->instance, &systemGI, &program->systemID);
    CHECKXR(result, "Failed to get system");

    CTRACE("Using system %llu for form factor %u", program->systemID, program->formFactor);

    // TODO: LogViewConfigurations();

    return vulkan_initialize_device(program, vulkan);
}

static char* ref_space_to_string(XrReferenceSpaceType e) {
    switch (e) {
        case XR_REFERENCE_SPACE_TYPE_VIEW:
            return "View";
        case XR_REFERENCE_SPACE_TYPE_LOCAL:
            return "Local";
        case XR_REFERENCE_SPACE_TYPE_STAGE:
            return "Stage";
        case XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT:
            return "Unbounded(MSFT)";
        case XR_REFERENCE_SPACE_TYPE_COMBINED_EYE_VARJO:
            return "Combined Eye Varjo";
        default:
            return "Unknown";
    }
}

#define strcpy_s(dest, src) strncpy(dest, src, sizeof(dest))

static bool program_initialize_actions(OpenXrProgram* program) {
    XrActionSetCreateInfo actionsSetCI = {
        .type = XR_TYPE_ACTION_SET_CREATE_INFO,
        .priority = 0};
    strcpy_s(actionsSetCI.actionSetName, "gameplay");
    strcpy_s(actionsSetCI.localizedActionSetName, "Gameplay");

    XrResult result = xrCreateActionSet(program->instance, &actionsSetCI, &program->input.actionsSet);
    CHECKXR(result, "Failed to create action set");

    result = xrStringToPath(program->instance, "/user/hand/left", &program->input.handSubActionPath[SIDE_LEFT]);
    CHECKXR(result, "Failed to create left sub action path");

    result = xrStringToPath(program->instance, "/user/hand/right", &program->input.handSubActionPath[SIDE_RIGHT]);
    CHECKXR(result, "Failed to create right sub action path");

    {
        XrActionCreateInfo actionCI = {
            .type = XR_TYPE_ACTION_CREATE_INFO,
            .actionType = XR_ACTION_TYPE_FLOAT_INPUT,
            .countSubactionPaths = SIDE_COUNT,
            .subactionPaths = program->input.handSubActionPath,
        };
        strcpy_s(actionCI.actionName, "grab_object");
        strcpy_s(actionCI.localizedActionName, "Grab Object");
        result = xrCreateAction(program->input.actionsSet, &actionCI, &program->input.grabAction);
        CHECKXR(result, "Failed to create grab action");

        actionCI.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy_s(actionCI.actionName, "hand_post");
        strcpy_s(actionCI.localizedActionName, "Hand Pose");
        result = xrCreateAction(program->input.actionsSet, &actionCI, &program->input.poseAction);
        CHECKXR(result, "Failed to create pose action");

        actionCI.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
        strcpy_s(actionCI.actionName, "vibrate_hand");
        strcpy_s(actionCI.localizedActionName, "Vibrate Hand");
        result = xrCreateAction(program->input.actionsSet, &actionCI, &program->input.vibrateAction);
        CHECKXR(result, "Failed to create vibrate hand action");

        actionCI.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        strcpy_s(actionCI.actionName, "quit_session");
        strcpy_s(actionCI.localizedActionName, "Quit Session");
        actionCI.countSubactionPaths = 0;
        actionCI.subactionPaths = 0;
        result = xrCreateAction(program->input.actionsSet, &actionCI, &program->input.quitAction);
        CHECKXR(result, "Failed to create vibrane hand action");
    }

    XrPath selectPath[SIDE_COUNT];
    XrPath squeezeValuePath[SIDE_COUNT];
    XrPath squeezeForcePath[SIDE_COUNT];
    XrPath squeezeClickPath[SIDE_COUNT];
    XrPath posePath[SIDE_COUNT];
    XrPath hapticPath[SIDE_COUNT];
    XrPath menuClickPath[SIDE_COUNT];
    XrPath clickPath[SIDE_COUNT];
    XrPath triggerValuePath[SIDE_COUNT];

    result = xrStringToPath(program->instance, "/user/hand/left/input/select/click", &selectPath[SIDE_LEFT]);
    CHECKXR(result, "Failed to find 'left click' path");
    result = xrStringToPath(program->instance, "/user/hand/right/input/select/click", &selectPath[SIDE_RIGHT]);
    CHECKXR(result, "Failed to find 'right click' path");
    result = xrStringToPath(program->instance, "/user/hand/left/input/squeeze/value", &squeezeValuePath[SIDE_LEFT]);
    CHECKXR(result, "Failed to find 'left squeeze value' path");
    result = xrStringToPath(program->instance, "/user/hand/right/input/squeeze/value", &squeezeValuePath[SIDE_RIGHT]);
    CHECKXR(result, "Failed to find 'right squeeze value' path");
    result = xrStringToPath(program->instance, "/user/hand/left/input/squeeze/force", &squeezeForcePath[SIDE_LEFT]);
    CHECKXR(result, "Failed to find 'left squeeze force' path");
    result = xrStringToPath(program->instance, "/user/hand/right/input/squeeze/force", &squeezeForcePath[SIDE_RIGHT]);
    CHECKXR(result, "Failed to find 'right squeeze force' path");
    result = xrStringToPath(program->instance, "/user/hand/left/input/squeeze/click", &squeezeClickPath[SIDE_LEFT]);
    CHECKXR(result, "Failed to find 'left squeeze click' path");
    result = xrStringToPath(program->instance, "/user/hand/right/input/squeeze/click", &squeezeClickPath[SIDE_RIGHT]);
    CHECKXR(result, "Failed to find 'right squeeze click' path");
    result = xrStringToPath(program->instance, "/user/hand/left/input/grip/pose", &posePath[SIDE_LEFT]);
    CHECKXR(result, "Failed to find 'left grip' path");
    result = xrStringToPath(program->instance, "/user/hand/right/input/grip/pose", &posePath[SIDE_RIGHT]);
    CHECKXR(result, "Failed to find 'right grip' path");
    result = xrStringToPath(program->instance, "/user/hand/left/output/haptic", &hapticPath[SIDE_LEFT]);
    CHECKXR(result, "Failed to find 'left haptic' path");
    result = xrStringToPath(program->instance, "/user/hand/right/output/haptic", &hapticPath[SIDE_RIGHT]);
    CHECKXR(result, "Failed to find 'right haptic' path");
    result = xrStringToPath(program->instance, "/user/hand/left/input/menu/click", &menuClickPath[SIDE_LEFT]);
    CHECKXR(result, "Failed to find 'left menu' path");
    result = xrStringToPath(program->instance, "/user/hand/right/input/menu/click", &menuClickPath[SIDE_RIGHT]);
    CHECKXR(result, "Failed to find 'right menu' path");
    result = xrStringToPath(program->instance, "/user/hand/left/input/b/click", &clickPath[SIDE_LEFT]);
    CHECKXR(result, "Failed to find 'left button' path");
    result = xrStringToPath(program->instance, "/user/hand/right/input/b/click", &clickPath[SIDE_RIGHT]);
    CHECKXR(result, "Failed to find 'right button' path");
    result = xrStringToPath(program->instance, "/user/hand/left/input/trigger/value", &triggerValuePath[SIDE_LEFT]);
    CHECKXR(result, "Failed to find 'left trigger' path");
    result = xrStringToPath(program->instance, "/user/hand/right/input/trigger/value", &triggerValuePath[SIDE_RIGHT]);
    CHECKXR(result, "Failed to find 'right trigger' path");

    {  // NOTE: base bindings
        XrPath simpleController;
        result = xrStringToPath(program->instance, "/interaction_profiles/khr/simple_controller", &simpleController);
        CHECKXR(result, "Failed to find 'simple controller' path");

        XrActionSuggestedBinding bindings[] = {
            {program->input.grabAction, selectPath[SIDE_LEFT]},
            {program->input.grabAction, selectPath[SIDE_RIGHT]},
            {program->input.poseAction, posePath[SIDE_LEFT]},
            {program->input.poseAction, posePath[SIDE_RIGHT]},
            {program->input.quitAction, menuClickPath[SIDE_LEFT]},
            {program->input.quitAction, menuClickPath[SIDE_RIGHT]},
            {program->input.vibrateAction, hapticPath[SIDE_LEFT]},
            {program->input.vibrateAction, hapticPath[SIDE_RIGHT]}};

        XrInteractionProfileSuggestedBinding suggestedBindings = {
            .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
            .interactionProfile = simpleController,
            .suggestedBindings = bindings,
            .countSuggestedBindings = array_size(bindings)};
        result = xrSuggestInteractionProfileBindings(program->instance, &suggestedBindings);
        CHECKXR(result, "Failed to suggest simple controller bindings");
    }

    {  // NOTE: oculus specific bindings
        XrPath oculusController;
        result = xrStringToPath(program->instance, "/interaction_profiles/oculus/touch_controller", &oculusController);
        CHECKXR(result, "Failed to find 'simple controller' path");

        XrActionSuggestedBinding bindings[] = {
            {program->input.grabAction, squeezeValuePath[SIDE_LEFT]},
            {program->input.grabAction, squeezeValuePath[SIDE_RIGHT]},
            {program->input.poseAction, posePath[SIDE_LEFT]},
            {program->input.poseAction, posePath[SIDE_RIGHT]},
            {program->input.quitAction, menuClickPath[SIDE_LEFT]},
            {program->input.vibrateAction, hapticPath[SIDE_LEFT]},
            {program->input.vibrateAction, hapticPath[SIDE_RIGHT]}};

        XrInteractionProfileSuggestedBinding suggestedBindings = {
            .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
            .interactionProfile = oculusController,
            .suggestedBindings = bindings,
            .countSuggestedBindings = array_size(bindings)};
        result = xrSuggestInteractionProfileBindings(program->instance, &suggestedBindings);
        CHECKXR(result, "Failed to suggest simple controller bindings");
    }

    // NOTE: Create space
    XrActionSpaceCreateInfo actionSpaceCI = {
        .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
        .action = program->input.poseAction,
        .poseInActionSpace.orientation.w = 1.0f,
        .subactionPath = program->input.handSubActionPath[SIDE_LEFT]};
    result = xrCreateActionSpace(program->session, &actionSpaceCI, &program->input.handSpace[SIDE_LEFT]);
    CHECKXR(result, "Failed to create left action space");

    actionSpaceCI.subactionPath = program->input.handSubActionPath[SIDE_RIGHT];
    result = xrCreateActionSpace(program->session, &actionSpaceCI, &program->input.handSpace[SIDE_RIGHT]);
    CHECKXR(result, "Failed to create right action space");

    XrSessionActionSetsAttachInfo sessionAttachInfo = {
        .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
        .countActionSets = 1,
        .actionSets = &program->input.actionsSet};
    result = xrAttachSessionActionSets(program->session, &sessionAttachInfo);
    CHECKXR(result, "Failed to assach session actions");
    return true;
}

static XrReferenceSpaceCreateInfo program_ref_space_ci(char* ref) {
    XrReferenceSpaceCreateInfo result = {
        .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .poseInReferenceSpace = pose_identity()};

    if (strcasecmp(ref, "View")) {
        result.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    } else if (strcasecmp(ref, "ViewFront")) {
        result.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        result.poseInReferenceSpace = pose_translation((XrVector3f){0.f, 0.f, -2.f});
    } else if (strcasecmp(ref, "Local")) {
        result.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    } else if (strcasecmp(ref, "Stage")) {
        result.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    } else if (strcasecmp(ref, "StageLeft")) {
        result.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        result.poseInReferenceSpace = pose_rotateCCW_about_Yaxis(
            0.0f,
            (XrVector3f){-2.f, 0.f, -2.f});
    } else if (strcasecmp(ref, "StageRight")) {
        result.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        result.poseInReferenceSpace = pose_rotateCCW_about_Yaxis(
            0.0f,
            (XrVector3f){2.f, 0.f, -2.f});
    } else if (strcasecmp(ref, "StageLeftRotated")) {
        result.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        result.poseInReferenceSpace = pose_rotateCCW_about_Yaxis(
            3.14f / 3.0f,
            (XrVector3f){-2.f, 0.f, -2.f});
    } else if (strcasecmp(ref, "StageRightRotated")) {
        result.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        result.poseInReferenceSpace = pose_rotateCCW_about_Yaxis(
            -3.14f / 3.0f,
            (XrVector3f){2.f, 0.f, -2.f});
    } else {
        result.type = XR_TYPE_UNKNOWN;
    }
    return result;
}

static bool program_initialize_session(OpenXrProgram* program) {
    if (program->instance == 0) {
        CERROR("Instance not initialized!");
        return false;
    }
    CINFO("Creating Session...");
    XrSessionCreateInfo sessionCI = {
        .type = XR_TYPE_SESSION_CREATE_INFO,
        .systemId = program->systemID,
        .next = &program->graphicsBinding};

    {
        XrResult result = xrCreateSession(program->instance, &sessionCI, &program->session);
        CHECKXR(result, "Failed to create session");
    }

    {
        uint32_t refSpaceCount;
        XrResult result = xrEnumerateReferenceSpaces(program->session, 0, &refSpaceCount, 0);
        CHECKXR(result, "Failed to get ref spaces count");

        XrReferenceSpaceType* spaces = malloc(sizeof(XrReferenceSpaceType) * refSpaceCount);
        result = xrEnumerateReferenceSpaces(program->session, refSpaceCount, &refSpaceCount, spaces);
        if (!XR_SUCCEEDED(result)) {
            CERROR("Failed to get ref spaces");
            free(spaces);
            return false;
        }

        CINFO("Available spaces: %u", refSpaceCount);
        for (uint32_t i = 0; i < refSpaceCount; ++i) {
            CTRACE("  %s", ref_space_to_string(spaces[i]));
        }
        free(spaces);
    }

    if (!program_initialize_actions(program)) {
        CERROR("Failed to initialize actions");
        return false;
    }

    uint32_t visualizedSpacesCount = array_size(VISULAIZED_SPACES);
    for (uint32_t i = 0; i < visualizedSpacesCount; ++i) {
        XrReferenceSpaceCreateInfo refSpaceCI = program_ref_space_ci(VISULAIZED_SPACES[i]);
        XrResult result = xrCreateReferenceSpace(program->session, &refSpaceCI, &program->visualizedSpaces[i]);
        if (!XR_SUCCEEDED(result)) {
            CWARN("Failed to create ref space '%s'", VISULAIZED_SPACES[i]);
        }
    }

    {
        XrReferenceSpaceCreateInfo refSpaceCI = program_ref_space_ci("Local");
        XrResult result = xrCreateReferenceSpace(program->session, &refSpaceCI, &program->space);
        CHECKXR(result, "Failed to get app space");
    }
    return true;
}

static int16_t vulkan_select_swapchain_format(int16_t* formats, uint32_t formatCount) {
    int64_t supported[] = {
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM};

    for (uint32_t i = 0; i < array_size(supported); ++i) {
        for (uint32_t j = 0; j < formatCount; ++j) {
            if (formats[j] == supported[i]) {
                return formats[j];
            }
        }
    }
    return VK_FORMAT_UNDEFINED;
}

static bool vulkan_depth_buffer_create(VulkanState* vulkan, VkFormat depthFormat, XrSwapchainCreateInfo* swapchainCI, DepthBuffer* depthBuffer) {
    VkImageCreateInfo imageCI = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = {.width = swapchainCI->width, .height = swapchainCI->height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = depthFormat,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .samples = swapchainCI->sampleCount,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    VkResult result = vkCreateImage(vulkan->device, &imageCI, 0, &depthBuffer->depthImage);

    VkMemoryRequirements memReq = {};
    vkGetImageMemoryRequirements(vulkan->device, depthBuffer->depthImage, &memReq);
    if (!vulkan_buffer_allocate(
            vulkan->device,
            memReq,
            &vulkan->memProps,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &depthBuffer->depthMemory)) {
        CERROR("Faield to allocate depth buffer memory");
        return false;
    }

    result = vkBindImageMemory(vulkan->device, depthBuffer->depthImage, depthBuffer->depthMemory, 0);
    CHECKVK(result, "Failed to bind depth buffer memory");
    return true;
}

static bool vulkan_render_pass_create(VulkanState* vulkan, VkFormat color, VkFormat depth, RenderPass* rp) {
    rp->colorFmt = color;
    rp->depthFmt = depth;
    VkAttachmentReference colorRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkAttachmentDescription attachments[2];
    uint32_t attachmentCount = 0;

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    };

    if (color != VK_FORMAT_UNDEFINED) {
        colorRef.attachment = attachmentCount++;
        attachments[colorRef.attachment] = (VkAttachmentDescription){
            .format = color,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
    }

    if (depth != VK_FORMAT_UNDEFINED) {
        depthRef.attachment = attachmentCount++;

        attachments[depthRef.attachment] = (VkAttachmentDescription){
            .format = depth,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        subpass.pDepthStencilAttachment = &depthRef;
    }

    VkRenderPassCreateInfo rpCI = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .attachmentCount = attachmentCount,
        .pAttachments = attachments};

    VkResult result = vkCreateRenderPass(vulkan->device, &rpCI, 0, &rp->pass);
    CHECKVK(result, "Failed to create Render pass");

    return true;
}

static bool vulkan_pipeline_create(VulkanState* vulkan, VkExtent2D extent, RenderPass* rp, Pipeline* pipe) {
    VkPipelineVertexInputStateCreateInfo vertexInputCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vulkan->drawBuffer.bindDesc,
        .vertexAttributeDescriptionCount = array_size(vulkan->drawBuffer.attrDesc),
        .pVertexAttributeDescriptions = &vulkan->drawBuffer.attrDesc};

    VkPipelineInputAssemblyStateCreateInfo inputAssembleCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .primitiveRestartEnable = VK_FALSE,
        .topology = pipe->topology,
    };

    VkPipelineRasterizationStateCreateInfo raserizerCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0,
        .depthBiasClamp = 0,
        .depthBiasSlopeFactor = 0,
        .lineWidth = 1.0f};

    VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {
        .blendEnable = 0,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT};

    VkPipelineColorBlendStateCreateInfo colorBlendStateCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachmentState,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_NO_OP,
        .blendConstants = {1.0f, 1.0f, 1.0f, 1.0f}};

    VkRect2D scissor = {{0, 0}, extent};
    VkViewport viewport = {
        0.0f,
        0.0f,
        (float)extent.width,
        (float)extent.height,
        0.0f,
        1.0f};

    VkPipelineViewportStateCreateInfo viewportCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor};

    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {
            .failOp = VK_STENCIL_OP_KEEP,
            .passOp = VK_STENCIL_OP_KEEP,
            .depthFailOp = VK_STENCIL_OP_KEEP,
            .compareOp = VK_COMPARE_OP_ALWAYS},
        .back = {.failOp = VK_STENCIL_OP_KEEP, .passOp = VK_STENCIL_OP_KEEP, .depthFailOp = VK_STENCIL_OP_KEEP, .compareOp = VK_COMPARE_OP_ALWAYS},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f};

    VkPipelineMultisampleStateCreateInfo multiSampleCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

    VkGraphicsPipelineCreateInfo pipeCI = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = array_size(vulkan->shaderProgram.shaderInfo),
        .pStages = vulkan->shaderProgram.shaderInfo,
        .pVertexInputState = &vertexInputCI,
        .pInputAssemblyState = &inputAssembleCI,
        .pTessellationState = 0,
        .pViewportState = &viewportCI,
        .pRasterizationState = &raserizerCI,
        .pMultisampleState = &multiSampleCI,
        .pDepthStencilState = &depthStencilStateCI,
        .pColorBlendState = &colorBlendStateCI,
        .layout = vulkan->pipelineLayout,
        .renderPass = rp->pass,
        .subpass = 0};
    VkResult result = vkCreateGraphicsPipelines(vulkan->device, 0, 1, &pipeCI, 0, &pipe->pipe);
    CHECKVK(result, "Failed to create Pipeline");
    return true;
}

static XrSwapchainImageBaseHeader* vulkan_allocate_swapchain_images(VulkanState* vulkan, XrSwapchainCreateInfo* swapchainCI, uint32_t imageCount, uint32_t viewID) {
    SwapchainImageContext* this = &vulkan->swapchainImageContext[viewID];
    this->imageCount = imageCount;

    this->size.width = swapchainCI->width;
    this->size.height = swapchainCI->height;
    VkFormat colorFormat = (VkFormat)swapchainCI->format;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    if (!vulkan_depth_buffer_create(vulkan, depthFormat, swapchainCI, &this->depthBuffer)) {
        CERROR("Faield to creaate depth buffer, View[%u] ", viewID);
        return 0;
    }

    if (!vulkan_render_pass_create(vulkan, colorFormat, depthFormat, &this->rp)) {
        CERROR("Faield to creaate render pass, View[%u] ", viewID);
        return 0;
    }

    if (!vulkan_pipeline_create(vulkan, this->size, &this->rp, &this->pipe)) {
        CERROR("Faield to creaate pipeline, View[%u] ", viewID);
        return 0;
    }

    for (uint32_t i = 0; i < imageCount; ++i) {
        this->swapchainImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
    }
    return (XrSwapchainImageBaseHeader*)this->swapchainImages;
}

static bool program_initialize_swapchains(OpenXrProgram* program, VulkanState* vulkan) {
    XrSystemProperties systemProperties = {
        .type = XR_TYPE_SYSTEM_PROPERTIES};
    XrResult result = xrGetSystemProperties(program->instance, program->systemID, &systemProperties);
    CHECKXR(result, "Failed t fetch system properties");

    CINFO("System properties: '%s' VendorId [%u]", systemProperties.systemName, systemProperties.vendorId);
    CINFO("  Graphics: MaxWidth=%u, MaxHeight=%u, MaxLayers=%u",
          systemProperties.graphicsProperties.maxSwapchainImageWidth,
          systemProperties.graphicsProperties.maxSwapchainImageHeight,
          systemProperties.graphicsProperties.maxLayerCount);
    CINFO("  [%s] Orientation Tracking", systemProperties.trackingProperties.orientationTracking ? "V" : " ");
    CINFO("  [%s] Position Tracking", systemProperties.trackingProperties.positionTracking ? "V" : " ");

    uint32_t viewCount;
    result = xrEnumerateViewConfigurationViews(
        program->instance,
        program->systemID,
        program->viewConfigType,
        0,
        &viewCount,
        0);
    CHECKXR(result, "Failed to get view count");

    result = xrEnumerateViewConfigurationViews(
        program->instance,
        program->systemID,
        program->viewConfigType,
        viewCount,
        &viewCount,
        program->configViews);
    CHECKXR(result, "Failed to enumerate view configs");

    if (viewCount > 0) {
        uint32_t formatCount;
        result = xrEnumerateSwapchainFormats(program->session, 0, &formatCount, 0);
        CHECKXR(result, "Failed to get swapchain format count");

        int64_t* formats = malloc(sizeof(int16_t) * formatCount);
        result = xrEnumerateSwapchainFormats(program->session, formatCount, &formatCount, formats);
        if (!XR_SUCCEEDED(result)) {
            CERROR("Failed to get swapchain formats");
            free(formats);
            return false;
        }
        program->colorSwapchainFormat = vulkan_select_swapchain_format(formats, formatCount);
        // TODO: Print swapchain formats and the selected one.
        CINFO("Selected swapchain format: %d", program->colorSwapchainFormat);
        free(formats);

        // NOTE: create swapchain
        for (uint32_t i = 0; i < viewCount; ++i) {
            XrSwapchainCreateInfo swapchainCI = {
                .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                .arraySize = 1,
                .format = program->colorSwapchainFormat,
                .width = program->configViews[i].recommendedImageRectWidth,
                .height = program->configViews[i].recommendedImageRectHeight,
                .mipCount = 1,
                .faceCount = 1,
                .sampleCount = program->configViews[i].recommendedSwapchainSampleCount,
                .usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT};
            program->swapchains[i].width = swapchainCI.width;
            program->swapchains[i].height = swapchainCI.height;
            result = xrCreateSwapchain(program->session, &swapchainCI, &program->swapchains[i].handle);
            CHECKXR(result, "Faield to create swapchain %u", i);

            uint32_t imageCount;
            result = xrEnumerateSwapchainImages(program->swapchains[i].handle, 0, &imageCount, 0);
            CHECKXR(result, "Faield to get image count for swapchain %u", i);

            XrSwapchainImageBaseHeader* imagesBase = vulkan_allocate_swapchain_images(
                vulkan,
                &swapchainCI,
                imageCount,
                i);
            if (!imagesBase) {
                CERROR("Failed to allocate swapchain image base");
                return false;
            }

            result = xrEnumerateSwapchainImages(
                program->swapchains[i].handle,
                imageCount,
                &imageCount,
                imagesBase);
            CHECKXR(result, "Faield to get swapchain %u's images", i);
        }
    }

    return true;
}

static XrEventDataBaseHeader* program_try_next_event(OpenXrProgram* program, XrResult* xrResult) {
    program->eventDataBuffer.type = XR_TYPE_EVENT_DATA_BUFFER;
    *xrResult = xrPollEvent(program->instance, &program->eventDataBuffer);
    if (*xrResult == XR_SUCCESS) {
        if (program->eventDataBuffer.type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
            XrEventDataEventsLost* event = (XrEventDataEventsLost*)&program->eventDataBuffer;
            CWARN("%u events lost", event->lostEventCount);
        }
        return (XrEventDataBaseHeader*)&program->eventDataBuffer;
    }

    return 0;
}

static bool program_session_state_changed(OpenXrProgram* program, XrEventDataSessionStateChanged* event, bool* exitRenderLoop, bool* requestRestart) {
    XrSessionState oldState = program->sessionState;
    program->sessionState = event->state;

    CINFO("XrEventDataSessionStateChanged: state %u -> %u, session=[%llu] time=[%lld]",
          oldState, event->state, event->session, event->time);

    if ((event->session != XR_NULL_HANDLE) && (event->session != program->session)) {
        CERROR("XrEventDataSessionStateChanged for unknown session");
        return false;
    }

    switch (program->sessionState) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo sessionBI = {
                .type = XR_TYPE_SESSION_BEGIN_INFO,
                .primaryViewConfigurationType = program->viewConfigType};
            XrResult result = xrBeginSession(program->session, &sessionBI);
            CHECKXR(result, "Failed to begin session");
            program->sessionRunning = true;
        } break;
        case XR_SESSION_STATE_STOPPING: {
            if (program->session == XR_NULL_HANDLE) {
                return false;
            }
            program->sessionRunning = false;
            XrResult result = xrEndSession(program->sessionRunning);
            CHECKXR(result, "Failed to end session");
        } break;
        case XR_SESSION_STATE_EXITING: {
            *exitRenderLoop = true;
            *requestRestart = false;
        } break;
        case XR_SESSION_STATE_LOSS_PENDING: {
            *exitRenderLoop = true;
            *requestRestart = true;
        } break;
        default:
            break;
    }

    return true;
}

static bool program_poll_events(OpenXrProgram* program, bool* exitRenderLoop, bool* requestRestart) {
    *exitRenderLoop = false;
    *requestRestart = false;

    XrResult result;
    XrEventDataBaseHeader* event;
    while (event = program_try_next_event(program, &result)) {
        switch (event->type) {
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                XrEventDataInstanceLossPending* e = (XrEventDataInstanceLossPending*)event;
                CWARN("XrEventDataInstanceLossPending by %lld", e->lossTime);
                *exitRenderLoop = true;
                *requestRestart = true;
            } break;
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                XrEventDataSessionStateChanged* e = (XrEventDataSessionStateChanged*)event;
                return program_session_state_changed(program, e, exitRenderLoop, requestRestart);
            } break;
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
                // TODO: LogActionSourceName(m_input.grabAction, "Grab");
                // TODO: LogActionSourceName(m_input.quitAction, "Quit");
                // TODO: LogActionSourceName(m_input.poseAction, "Pose");
                // TODO: LogActionSourceName(m_input.vibrateAction, "Vibrate");
            } break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            default: {
                CTRACE("Ignoring event type: %u", event->type);
            } break;
        }
    }
    CHECKXR(result, "Error during event polling");
    return true;
}

static bool program_poll_actions(OpenXrProgram* program) {
    program->input.handActive[0] = XR_FALSE;
    program->input.handActive[1] = XR_FALSE;

    XrActiveActionSet activeActionSet = {
        .actionSet = program->input.actionsSet,
        .subactionPath = XR_NULL_PATH};

    XrActionsSyncInfo syncInfo = {
        .type = XR_TYPE_ACTIONS_SYNC_INFO,
        .countActiveActionSets = 1,
        .activeActionSets = &activeActionSet};

    XrResult result = xrSyncActions(program->session, &syncInfo);
    CHECKXR(result, "Failed to Sync actions");

    for (uint32_t hand = 0; hand < SIDE_COUNT; ++hand) {
        XrActionStateGetInfo getInfo = {
            .type = XR_TYPE_ACTION_STATE_GET_INFO,
            .action = program->input.grabAction,
            .subactionPath = program->input.handSubActionPath[hand]};

        XrActionStateFloat grabValue = {
            .type = XR_TYPE_ACTION_STATE_FLOAT};

        result = xrGetActionStateFloat(program->session, &getInfo, &grabValue);
        CHECKXR(result, "Failed to get grab value [%u]", hand);

        if (grabValue.isActive == XR_TRUE) {
            program->input.handScale[hand] = 1.0f - 0.5f * grabValue.currentState;
            if (grabValue.currentState > 0.9f) {
                XrHapticVibration vibration = {
                    .type = XR_TYPE_HAPTIC_VIBRATION,
                    .amplitude = 1.0f,
                    .duration = XR_MIN_HAPTIC_DURATION,
                    .frequency = XR_FREQUENCY_UNSPECIFIED};

                XrHapticActionInfo hapticActionInfo = {
                    .type = XR_TYPE_HAPTIC_ACTION_INFO,
                    .action = program->input.vibrateAction,
                    .subactionPath = program->input.handSubActionPath[hand]};

                result = xrApplyHapticFeedback(program->session, &hapticActionInfo, (XrHapticBaseHeader*)&vibration);
                CHECKXR(result, "Failed to apply haptic feedback [%u]", hand);
            }
        }
        getInfo.action = program->input.poseAction;
        XrActionStatePose poseState = {
            .type = XR_TYPE_ACTION_STATE_POSE};

        result = xrGetActionStatePose(program->session, &getInfo, &poseState);
        CHECKXR(result, "Failed to get hand pose state [%u]", hand);
        program->input.handActive[hand] = poseState.isActive;
    }

    XrActionStateGetInfo getInfo = {
        .type = XR_TYPE_ACTION_STATE_GET_INFO,
        .action = program->input.quitAction};
    XrActionStateBoolean quitValue = {
        .type = XR_TYPE_ACTION_STATE_BOOLEAN};

    result = xrGetActionStateBoolean(program->session, &getInfo, &quitValue);
    CHECKXR(result, "Failed to get quit action value");
    if ((quitValue.isActive == XR_TRUE) && (quitValue.changedSinceLastSync = XR_TRUE) && (quitValue.currentState == XR_TRUE)) {
        result = xrRequestExitSession(program->session);
        CHECKXR(result, "Failed to request quit session");
    }
}

static bool program_render_frame(OpenXrProgram* program, VulkanState* vulkan) {
    // TODO: program->RenderFrame();
}

void android_main(struct android_app* app) {
    JNIEnv* env;
    (*app->activity->vm)->AttachCurrentThread(app->activity->vm, &env, 0);

    AndroidAppState state = {};

    app->userData = &state;
    app->onAppCmd = app_handle_cmd;

    XrInstanceCreateInfoAndroidKHR instanceCI = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR,
        .applicationVM = app->activity->vm,
        .applicationActivity = app->activity->clazz};

    bool requestRestart = false;  // TODO: remove?
    bool exitRenderLoop = false;  // TODO: remove?

    VulkanState vulkan = {};
    for (uint32_t i = 0; i < NUM_VIEWES; ++i) {
        vulkan.swapchainImageContext[i].pipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        vulkan.swapchainImageContext[i].swapchainImageType = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
        vulkan.swapchainImageContext[i].depthBuffer.vkLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    OpenXrProgram program = {
        .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
        .viewConfigType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        .graphicsBinding = {.type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR},
        .input = {
            .handScale = {1.0f, 1.0f}}};

    CINFO("Starting...");

    PFN_xrInitializeLoaderKHR initializeLoader = 0;
    XrResult xrResult = XR_SUCCESS;
    xrResult = xrGetInstanceProcAddr(0, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)(&initializeLoader));
    if (XR_SUCCEEDED(xrResult)) {
        XrLoaderInitInfoAndroidKHR loaderInitInfoAndroid = {
            .type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
            .applicationVM = app->activity->vm,
            .applicationContext = app->activity->clazz};
        xrResult = initializeLoader(&loaderInitInfoAndroid);
    }

    bool result = false;
    if (XR_SUCCEEDED(xrResult)) {
        result = program_craete_instance(&program, &instanceCI);
        result = result && program_initialize_system(&program, &vulkan);
        result = result && program_initialize_session(&program);
        result = result && program_initialize_swapchains(&program, &vulkan);
    }

    if (result) {
        while (app->destroyRequested == 0) {
            for (;;) {
                int events;
                struct android_poll_source* source;
                const int timeoutMilliseconds =
                    (!state.resumed && !program.sessionRunning && app->destroyRequested == 0) ? -1 : 0;
                if (ALooper_pollAll(timeoutMilliseconds, 0, &events, (void**)&source) < 0) {
                    break;
                }

                // Process this event.
                if (source) {
                    source->process(app, source);
                }
            }

            if (!program_poll_events(&program, &exitRenderLoop, &requestRestart)) {
                exitRenderLoop = true;
                requestRestart = true;
            }
            if (!program.sessionRunning) {
                usleep(250000);
                continue;
            }

            if (!program_poll_actions(&program)) {
                CERROR("Failed to poll actions");
                exitRenderLoop = true;
            }

            // TODO: program->RenderFrame();
        }
    }

    // TODO: cleanup
    (*app->activity->vm)->DetachCurrentThread(app->activity->vm);
}