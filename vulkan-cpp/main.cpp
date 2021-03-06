#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

#include <wayland-client.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

#include <cairo.h>

#include "xdg-shell.h"

#include "vulkan/instance.h"
#include "vulkan/surface.h"
#include "vulkan/device.h"
#include "vulkan/render-pass.h"
#include "vulkan/swapchain.h"
#include "vulkan/command-pool.h"

#include "vulkan/vertex.h"

struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_subcompositor *subcompositor = NULL;
struct wl_surface *wl_surface;
struct wl_region *region;
struct wl_output *output;

struct xdg_wm_base *xdg_wm_base = NULL;
struct xdg_surface *xdg_surface = NULL;
struct xdg_toplevel *xdg_toplevel = NULL;

#define WINDOW_WIDTH 480
#define WINDOW_HEIGHT 360

#define MAX_FRAMES_IN_FLIGHT 2

// Vulkan validation layers.
const char *validation_layers[] = {
    "VK_LAYER_KHRONOS_validation",
};
#ifdef NDEBUG
const bool enable_validation_layers = false;
#else
const bool enable_validation_layers = true;
#endif
VkLayerProperties *vulkan_layer_properties = NULL;

// Logical device.
// Vulkan surface.
// Swapchain.
// Image views.
// Render pass.
// Shaders.
uint8_t *vert_shader_code = NULL;
uint32_t vert_shader_code_size = 0;
uint8_t *frag_shader_code = NULL;
uint32_t frag_shader_code_size = 0;
VkPipelineShaderStageCreateInfo vk_vert_shader_stage_create_info;
VkPipelineShaderStageCreateInfo vk_frag_shader_stage_create_info;
VkPipelineShaderStageCreateInfo *vulkan_shader_stage_create_infos = NULL;
VkPipelineVertexInputStateCreateInfo vulkan_vert_input_state_create_info;
VkPipelineInputAssemblyStateCreateInfo vulkan_input_assembly_state_create_info;
VkPipelineViewportStateCreateInfo vulkan_viewport_state_create_info;
VkPipelineRasterizationStateCreateInfo vulkan_rasterization_state_create_info;
VkPipelineMultisampleStateCreateInfo vulkan_multisample_state_create_info;
VkPipelineColorBlendAttachmentState vulkan_color_blend_attachment_state;
VkPipelineColorBlendStateCreateInfo vulkan_color_blend_state_create_info;
VkDynamicState dynamic_states[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
};
VkPipelineDynamicStateCreateInfo vulkan_dynamic_state_create_info;
VkPipelineLayoutCreateInfo vulkan_layout_create_info;
VkPipelineLayout vulkan_layout = NULL;
VkGraphicsPipelineCreateInfo vulkan_graphics_pipeline_create_info;
VkPipeline vulkan_graphics_pipeline = NULL;
// Framebuffers.
// Command pool.
// Vertex buffer.
VkBuffer vk_vertex_buffer = NULL;
VkDeviceMemory vk_vertex_buffer_memory = NULL;
// Input buffer.
VkBuffer vk_index_buffer = NULL;
VkDeviceMemory vk_index_buffer_memory = NULL;
// Command buffer.
VkCommandBufferAllocateInfo vk_command_buffer_allocate_info;
VkCommandBuffer *vk_command_buffers = NULL;
VkCommandBufferBeginInfo vulkan_command_buffer_begin_info; // Unused.
VkRenderPassBeginInfo vulkan_render_pass_begin_info;
VkClearValue vulkan_clear_color;
// Sync objects.
VkSemaphore *vk_image_available_semaphores = NULL;
VkSemaphore *vk_render_finished_semaphores = NULL;
VkFence *vk_in_flight_fences = NULL;

float clear_alpha = 0.1f;
vk::Vertex vertices[3] = {
    {{ 0.0f, -0.5f }, { 1.0f, 1.0f, 0.0f }},
    {{ 0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f }},
    {{ -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f }},
};
uint16_t indices[3] = {
    0, 1, 2,
};
uint32_t current_frame = 0;

struct wl_subsurface *subsurface;

uint32_t image_width;
uint32_t image_height;
uint32_t image_size;
uint32_t *image_data;

void load_image()
{
    cairo_surface_t *cairo_surface = cairo_image_surface_create_from_png(
        "miku@2x.png");
    cairo_t *cr = cairo_create(cairo_surface);
    image_width = cairo_image_surface_get_width(cairo_surface);
    image_height = cairo_image_surface_get_height(cairo_surface);
    image_size = sizeof(uint32_t) * (image_width * image_height);
    image_data = (uint32_t*)malloc(image_size);
    memcpy(image_data, cairo_image_surface_get_data(cairo_surface), image_size);

    // Color correct.
    for (uint32_t i = 0; i < (image_width * image_height); ++i) {
        uint32_t color = 0x00000000;
        color += ((image_data[i] & 0xff000000));  // Set alpha.
        color += ((image_data[i] & 0x00ff0000) >> 16);   // Set blue.
        color += ((image_data[i] & 0x0000ff00));   // Set green.
        color += ((image_data[i] & 0x000000ff) << 16);  // Set red.
        image_data[i] = color;
    }

    cairo_surface_destroy(cairo_surface);
    cairo_destroy(cr);
}

//===========
// Vulkan
//===========

static void load_shader(const char *path, uint8_t* *code, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);

    *code = (uint8_t*)malloc(sizeof(uint8_t) * *size);
    fread(*code, *size, 1, f);

    fclose(f);
}

static void create_vulkan_graphics_pipeline(std::shared_ptr<vk::Device> device,
        std::shared_ptr<vk::RenderPass> render_pass)
{
    VkResult result;

    // Load shader codes.
    load_shader("vert.spv", &vert_shader_code, &vert_shader_code_size);
    load_shader("frag.spv", &frag_shader_code, &frag_shader_code_size);

    // Create vertex shader module.
    VkShaderModule vert_shader_module;

    VkShaderModuleCreateInfo vert_create_info;
    vert_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_create_info.codeSize = vert_shader_code_size;
    vert_create_info.pCode = (const uint32_t*)vert_shader_code;
    vert_create_info.flags = 0;
    vert_create_info.pNext = NULL;

    result = vkCreateShaderModule(device->vk_device(), &vert_create_info, NULL,
        &vert_shader_module);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create vertex shader module!\n");
        return;
    }
    fprintf(stderr, "Vertex shader module created.\n");

    // Create fragment shader module.
    VkShaderModule frag_shader_module;

    VkShaderModuleCreateInfo frag_create_info;
    frag_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_create_info.codeSize = frag_shader_code_size;
    frag_create_info.pCode = (const uint32_t*)frag_shader_code;
    frag_create_info.flags = 0;
    frag_create_info.pNext = NULL;

    result = vkCreateShaderModule(device->vk_device(), &frag_create_info, NULL,
        &frag_shader_module);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create fragment shader module!\n");
        return;
    }
    fprintf(stderr, "Fragment shader module created.\n");

    // Write shader stage create infos.
    vk_vert_shader_stage_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vk_vert_shader_stage_create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vk_vert_shader_stage_create_info.module = vert_shader_module;
    vk_vert_shader_stage_create_info.pName = "main";

    vk_frag_shader_stage_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vk_frag_shader_stage_create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    vk_frag_shader_stage_create_info.module = frag_shader_module;
    vk_frag_shader_stage_create_info.pName = "main";

    // Shader stage.
    vulkan_shader_stage_create_infos = (VkPipelineShaderStageCreateInfo*)malloc(
        sizeof(VkPipelineShaderStageCreateInfo) * 2
    );
    *(vulkan_shader_stage_create_infos + 0) = vk_vert_shader_stage_create_info;
    *(vulkan_shader_stage_create_infos + 1) = vk_frag_shader_stage_create_info;

    // Vertex input.
    vulkan_vert_input_state_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto binding_description = vk::Vertex::get_binding_description();
    auto attribute_descriptions = vk::Vertex::get_attribute_descriptions();

    vulkan_vert_input_state_create_info.vertexBindingDescriptionCount = 1;
    vulkan_vert_input_state_create_info.pVertexBindingDescriptions =
        &binding_description;
    vulkan_vert_input_state_create_info.vertexAttributeDescriptionCount = 2;
    vulkan_vert_input_state_create_info.pVertexAttributeDescriptions =
        attribute_descriptions.data();

    // Input assembly.
    vulkan_input_assembly_state_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    vulkan_input_assembly_state_create_info.topology =
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    vulkan_input_assembly_state_create_info.primitiveRestartEnable = VK_FALSE;

    // Viewport.
    vulkan_viewport_state_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vulkan_viewport_state_create_info.viewportCount = 1;
    vulkan_viewport_state_create_info.scissorCount = 1;

    // Rasterization.
    vulkan_rasterization_state_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    vulkan_rasterization_state_create_info.depthClampEnable = VK_FALSE;
    vulkan_rasterization_state_create_info.rasterizerDiscardEnable = VK_FALSE;
    vulkan_rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_FILL;
    vulkan_rasterization_state_create_info.lineWidth = 1.0f;
    vulkan_rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
    vulkan_rasterization_state_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    vulkan_rasterization_state_create_info.depthBiasEnable = VK_FALSE;

    // Multisampling.
    vulkan_multisample_state_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    vulkan_multisample_state_create_info.sampleShadingEnable = VK_FALSE;
    vulkan_multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blend attachment.
    vulkan_color_blend_attachment_state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    vulkan_color_blend_attachment_state.blendEnable = VK_FALSE;

    // Color blending.
    vulkan_color_blend_state_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    vulkan_color_blend_state_create_info.logicOpEnable = VK_FALSE;
    vulkan_color_blend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
    vulkan_color_blend_state_create_info.attachmentCount = 1;
    vulkan_color_blend_state_create_info.pAttachments =
        &vulkan_color_blend_attachment_state;
    vulkan_color_blend_state_create_info.blendConstants[0] = 0.0f;
    vulkan_color_blend_state_create_info.blendConstants[1] = 0.0f;
    vulkan_color_blend_state_create_info.blendConstants[2] = 0.0f;
    vulkan_color_blend_state_create_info.blendConstants[3] = 0.0f;

    // Dynamic states.
    vulkan_dynamic_state_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    vulkan_dynamic_state_create_info.dynamicStateCount = 2;
    vulkan_dynamic_state_create_info.pDynamicStates = dynamic_states;

    // Layout.
    vulkan_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    vulkan_layout_create_info.setLayoutCount = 0;
    vulkan_layout_create_info.pushConstantRangeCount = 0;

    result = vkCreatePipelineLayout(device->vk_device(),
        &vulkan_layout_create_info, NULL, &vulkan_layout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create pipeline layout!\n");
        return;
    }
    fprintf(stderr, "Pipeline layout created.\n");

    vulkan_graphics_pipeline_create_info.sType =
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    vulkan_graphics_pipeline_create_info.stageCount = 2;
    vulkan_graphics_pipeline_create_info.pStages = vulkan_shader_stage_create_infos;
    vulkan_graphics_pipeline_create_info.pVertexInputState =
        &vulkan_vert_input_state_create_info;
    vulkan_graphics_pipeline_create_info.pInputAssemblyState =
        &vulkan_input_assembly_state_create_info;
    vulkan_graphics_pipeline_create_info.pViewportState =
        &vulkan_viewport_state_create_info;
    vulkan_graphics_pipeline_create_info.pRasterizationState =
        &vulkan_rasterization_state_create_info;
    vulkan_graphics_pipeline_create_info.pMultisampleState =
        &vulkan_multisample_state_create_info;
    vulkan_graphics_pipeline_create_info.pColorBlendState =
        &vulkan_color_blend_state_create_info;
    vulkan_graphics_pipeline_create_info.pDynamicState =
        &vulkan_dynamic_state_create_info;
    vulkan_graphics_pipeline_create_info.layout = vulkan_layout;
    vulkan_graphics_pipeline_create_info.renderPass = render_pass->vk_render_pass();
    vulkan_graphics_pipeline_create_info.subpass = 0;
    vulkan_graphics_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;

    result = vkCreateGraphicsPipelines(device->vk_device(), VK_NULL_HANDLE,
        1, &vulkan_graphics_pipeline_create_info, NULL,
        &vulkan_graphics_pipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create graphics pipeline!\n");
        return;
    }
    fprintf(stderr, "Graphics pipeline created - pipeline: %p\n",
        vulkan_graphics_pipeline);

    /*
    vkDestroyShaderModule(vulkan_device, frag_shader_module, NULL);
    vkDestroyShaderModule(vulkan_device, vert_shader_module, NULL);
    */
}

static void create_buffer(
        std::shared_ptr<vk::Instance> instance,
        std::shared_ptr<vk::Device> device,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer *buffer,
        VkDeviceMemory *buffer_memory)
{
    VkResult result;

    VkBufferCreateInfo create_info;
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size = size;
    create_info.usage = usage;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Zero or null.
    create_info.flags = 0;
    create_info.pNext = NULL;
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices = NULL;

    result = vkCreateBuffer(device->vk_device(), &create_info,
        NULL, buffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create buffer!\n");
        return;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device->vk_device(),
        *buffer, &requirements);
    fprintf(stderr, "Buffer memory requirements:\n");
    fprintf(stderr, " - size: %ld\n", requirements.size);
    fprintf(stderr, " - alignment: %ld\n", requirements.alignment);
    fprintf(stderr, " - memoryTypeBits: %d\n", requirements.memoryTypeBits);

    // Find memory type.
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(instance->vk_physical_device(),
        &memory_properties);
    fprintf(stderr, "Memory properties - memoryTypeCount: %d\n",
        memory_properties.memoryTypeCount);

    uint32_t type_filter = requirements.memoryTypeBits;
    uint32_t memory_type = 0;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) &&
                (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            memory_type = i;
            break;
        }
    }
    fprintf(stderr, "Memory type selected: %d\n", memory_type);

    VkMemoryAllocateInfo allocate_info;
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;

    // Zero or null.
    allocate_info.pNext = NULL;

    result = vkAllocateMemory(device->vk_device(), &allocate_info,
        NULL, buffer_memory);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate buffer memory!\n");
        return;
    }
    fprintf(stderr, "Buffer memory allocated.\n");

    vkBindBufferMemory(device->vk_device(), *buffer, *buffer_memory, 0);
}

static void copy_buffer(
        std::shared_ptr<vk::Device> device,
        std::shared_ptr<vk::CommandPool> command_pool,
        VkBuffer src_buffer,
        VkBuffer dst_buffer,
        VkDeviceSize size)
{
    VkResult result;

    // Allocate command buffers.
    VkCommandBufferAllocateInfo allocate_info;
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandPool = command_pool->vk_command_pool();
    allocate_info.commandBufferCount = 1;

    allocate_info.pNext = NULL;

    VkCommandBuffer command_buffer;
    result = vkAllocateCommandBuffers(device->vk_device(), &allocate_info,
        &command_buffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate command buffers!\n");
        return;
    }

    // Begin command buffer.
    VkCommandBufferBeginInfo begin_info;
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to begin command buffer!\n");
        return;
    }

    //=================
    // Begin Copy
    //=================

    VkBufferCopy copy_region;
    copy_region.size = size;
    vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);

    //=================
    // End Copy
    //=================

    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to end command buffer!\n");
        return;
    }

    // Submit.
    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    // Zero or null.
    submit_info.pNext = NULL;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = NULL;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = NULL;
    submit_info.pWaitDstStageMask = NULL;

    vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->graphics_queue());

    vkFreeCommandBuffers(device->vk_device(),
        command_pool->vk_command_pool(),
        1, &command_buffer);
}

static void create_vulkan_vertex_buffer(
        std::shared_ptr<vk::Instance> instance,
        std::shared_ptr<vk::Device> device,
        std::shared_ptr<vk::CommandPool> command_pool)
{
    VkResult result;

    VkDeviceSize buffer_size = sizeof(vertices[0]) * 3;
    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory = NULL;

    VkBufferUsageFlags staging_buffer_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlags staging_buffer_properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    create_buffer(instance, device,
        buffer_size,
        staging_buffer_usage,
        staging_buffer_properties,
        &staging_buffer,
        &staging_buffer_memory);
    fprintf(stderr, "Staging buffer created.\n");

    //
    void *data = NULL;
    result = vkMapMemory(device->vk_device(), staging_buffer_memory,
        0, buffer_size,
        0, &data);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to map staging buffer memory\n");
    }
    fprintf(stderr, "Staging buffer memory mapped.\n");
    memcpy(data, vertices, buffer_size);
    vkUnmapMemory(device->vk_device(), staging_buffer_memory);
    fprintf(stderr, "Staging buffer memory unmapped.\n");

    create_buffer(instance, device,
        buffer_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &vk_vertex_buffer,
        &vk_vertex_buffer_memory);
    fprintf(stderr, "Vertex buffer created.\n");

    // Copy buffer.
    copy_buffer(device, command_pool,
        staging_buffer, vk_vertex_buffer,
        buffer_size);

    vkDestroyBuffer(device->vk_device(), staging_buffer, NULL);
    vkFreeMemory(device->vk_device(), staging_buffer_memory, NULL);
}

static void create_vulkan_index_buffer(
        std::shared_ptr<vk::Instance> instance,
        std::shared_ptr<vk::Device> device,
        std::shared_ptr<vk::CommandPool> command_pool)
{
    VkResult result;

    VkDeviceSize buffer_size = sizeof(indices[0]) * 3;
    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory = NULL;

    VkBufferUsageFlags staging_buffer_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlags staging_buffer_properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    create_buffer(instance, device,
        buffer_size,
        staging_buffer_usage,
        staging_buffer_properties,
        &staging_buffer,
        &staging_buffer_memory);
    fprintf(stderr, "Staging buffer for index created.\n");

    //
    void *data = NULL;
    result = vkMapMemory(device->vk_device(), staging_buffer_memory,
        0, buffer_size,
        0, &data);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to map staging buffer memory\n");
    }
    fprintf(stderr, "Staging buffer memory mapped.\n");
    memcpy(data, vertices, buffer_size);
    vkUnmapMemory(device->vk_device(), staging_buffer_memory);
    fprintf(stderr, "Staging buffer memory unmapped.\n");

    create_buffer(instance, device,
        buffer_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &vk_index_buffer,
        &vk_index_buffer_memory);
    fprintf(stderr, "Index buffer created.\n");

    // Copy buffer.
    copy_buffer(device, command_pool,
        staging_buffer, vk_vertex_buffer,
        buffer_size);

    vkDestroyBuffer(device->vk_device(), staging_buffer, NULL);
    vkFreeMemory(device->vk_device(), staging_buffer_memory, NULL);
}

static void create_vulkan_command_buffers(std::shared_ptr<vk::Device> device,
        std::shared_ptr<vk::CommandPool> command_pool)
{
    VkResult result;

    vk_command_buffers = new VkCommandBuffer[MAX_FRAMES_IN_FLIGHT];

    vk_command_buffer_allocate_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    vk_command_buffer_allocate_info.commandPool = command_pool->vk_command_pool();
    vk_command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vk_command_buffer_allocate_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    result = vkAllocateCommandBuffers(device->vk_device(),
        &vk_command_buffer_allocate_info, vk_command_buffers);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate command buffers!\n");
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        fprintf(stderr, "Command buffer allocated. - command buffer: %p\n",
            vk_command_buffers[i]);
    }
}

static void create_vulkan_sync_objects(std::shared_ptr<vk::Device> device)
{
    VkResult result;

    vk_image_available_semaphores = new VkSemaphore[MAX_FRAMES_IN_FLIGHT];
    vk_render_finished_semaphores = new VkSemaphore[MAX_FRAMES_IN_FLIGHT];
    vk_in_flight_fences = new VkFence[MAX_FRAMES_IN_FLIGHT];

    VkSemaphoreCreateInfo semaphore_create_info;
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_create_info.flags = 0;
    semaphore_create_info.pNext = NULL;

    VkFenceCreateInfo fence_create_info;
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.pNext = NULL;
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        result = vkCreateSemaphore(device->vk_device(), &semaphore_create_info,
            NULL, &vk_image_available_semaphores[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create image available semaphore!\n");
            return;
        }
        fprintf(stderr, "Image available semaphore created.\n");
        result = vkCreateSemaphore(device->vk_device(), &semaphore_create_info,
            NULL, &vk_render_finished_semaphores[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create render finished semaphore!\n");
            return;
        }
        fprintf(stderr, "Render finished semaphore created.\n");
        result = vkCreateFence(device->vk_device(), &fence_create_info,
            NULL, &vk_in_flight_fences[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create fence!\n");
            return;
        }
        fprintf(stderr, "Fence created.\n");
    }
}

static void record_command_buffer(VkCommandBuffer command_buffer,
        uint32_t image_index, std::shared_ptr<vk::Swapchain> swapchain,
        std::shared_ptr<vk::RenderPass> render_pass)
{
    VkResult result;

    VkCommandBufferBeginInfo command_buffer_begin_info;
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = NULL;
    command_buffer_begin_info.pNext = NULL;

    result = vkBeginCommandBuffer(command_buffer,
        &command_buffer_begin_info);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to begin recording command buffer!\n");
        return;
    }
    fprintf(stderr, "Begin command buffer.\n");

    vulkan_render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    vulkan_render_pass_begin_info.renderPass = render_pass->vk_render_pass();
    vulkan_render_pass_begin_info.framebuffer = swapchain->framebuffers()[image_index];
    vulkan_render_pass_begin_info.renderArea.offset.x = 0;
    vulkan_render_pass_begin_info.renderArea.offset.y = 0;
    vulkan_render_pass_begin_info.renderArea.extent = swapchain->extent();

    vulkan_clear_color.color.float32[0] = 0.0f;
    vulkan_clear_color.color.float32[1] = 0.0f;
    vulkan_clear_color.color.float32[2] = 0.0f;
    vulkan_clear_color.color.float32[3] = clear_alpha;

    // Change alpha for next frame.
    clear_alpha += 0.0005f;
    if (clear_alpha >= 1.0f) {
        clear_alpha = 0.1f;
    }

    vulkan_render_pass_begin_info.clearValueCount = 1;
    vulkan_render_pass_begin_info.pClearValues = &vulkan_clear_color;

    fprintf(stderr, "vkCmdBeginRenderPass() - command buffer: %p\n",
        command_buffer);
    vkCmdBeginRenderPass(command_buffer, &vulkan_render_pass_begin_info,
        VK_SUBPASS_CONTENTS_INLINE);
    fprintf(stderr, "Begin render pass.\n");

    //==============
    // In Commands
    //==============
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        vulkan_graphics_pipeline);

    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchain->extent().width;
    viewport.height = (float)swapchain->extent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = swapchain->extent();
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    VkBuffer vertex_buffers[] = {
        vk_vertex_buffer,
    };
    VkDeviceSize offsets[] = {
        0,
    };
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

    vkCmdBindIndexBuffer(command_buffer, vk_index_buffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdDraw(command_buffer, 3, 1, 0, 0);
    //===============
    // Out Commands
    //===============

    vkCmdEndRenderPass(command_buffer);

    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to record command buffer!\n");
        return;
    }
    fprintf(stderr, "End command buffer.\n");
}

void draw_frame(std::shared_ptr<vk::Device> device,
        std::shared_ptr<vk::Swapchain> swapchain,
        std::shared_ptr<vk::RenderPass> render_pass)
{
    VkResult result;

    vkWaitForFences(device->vk_device(), 1, &vk_in_flight_fences[current_frame],
        VK_TRUE, 2048);
    vkResetFences(device->vk_device(), 1, &vk_in_flight_fences[current_frame]);

    uint32_t image_index;
    result = vkAcquireNextImageKHR(device->vk_device(),
        swapchain->vk_swapchain(),
        UINT64_MAX,
        vk_image_available_semaphores[current_frame],
        VK_NULL_HANDLE,
        &image_index);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to acquire next image!\n");
        return;
    }
    fprintf(stderr, "Acquired next image. - image index: %d\n", image_index);

    vkResetCommandBuffer(vk_command_buffers[current_frame], 0);
    record_command_buffer(vk_command_buffers[current_frame], image_index,
        swapchain,
        render_pass);

    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = {
        vk_image_available_semaphores[current_frame],
    };
    VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &vk_command_buffers[current_frame];

    VkSemaphore signal_semaphores[] = {
        vk_render_finished_semaphores[current_frame],
    };
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    // Set zero or null.
    submit_info.pNext = NULL;

    result = vkQueueSubmit(device->graphics_queue(), 1, &submit_info,
        vk_in_flight_fences[current_frame]);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to submit draw command buffer!\n");
        return;
    }

    VkPresentInfoKHR present_info;
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;

    VkSwapchainKHR swapchains[] = {
        swapchain->vk_swapchain(),
    };
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;

    present_info.pImageIndices = &image_index;

    // Set zero or null.
    present_info.pNext = NULL;
    present_info.pResults = NULL;

    fprintf(stderr, "vkQueuePresentKHR() - queue: %p\n", device->present_queue());
    result = vkQueuePresentKHR(device->present_queue(), &present_info);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to present queue!\n");
        return;
    }

    current_frame += (current_frame + 1) & MAX_FRAMES_IN_FLIGHT;
}

//===========
// XDG
//===========
static void xdg_wm_base_ping_handler(void *data,
        struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping_handler,
};

static void xdg_surface_configure_handler(void *data,
        struct xdg_surface *xdg_surface, uint32_t serial)
{
    fprintf(stderr, "xdg_surface_configure_handler()\n");
    xdg_surface_ack_configure(xdg_surface, serial);
}

const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure_handler,
};

static void xdg_toplevel_configure_handler(void *data,
        struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height,
        struct wl_array *states)
{
}

static void xdg_toplevel_close_handler(void *data,
        struct xdg_toplevel *xdg_toplevel)
{
}

const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure_handler,
    .close = xdg_toplevel_close_handler,
};

//==============
// Output
//==============

//==============
// Global
//==============
static void global_registry_handler(void *data, struct wl_registry *registry,
        uint32_t id, const char *interface, uint32_t version)
{
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = (struct wl_compositor*)wl_registry_bind(
            registry,
            id,
            &wl_compositor_interface,
            version
        );
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        xdg_wm_base = (struct xdg_wm_base*)wl_registry_bind(registry,
            id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);
    } else {
        printf("(%d) Got a registry event for <%s> id <%d>\n",
            version, interface, id);
    }
}

static void global_registry_remover(void *data, struct wl_registry *registry,
        uint32_t id)
{
    printf("Got a registry losing event for <%d>\n", id);
}

static const struct wl_registry_listener registry_listener = {
    global_registry_handler,
    global_registry_remover
};


int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Can't connect to display.\n");
        exit(1);
    }
    printf("Connected to display.\n");

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_roundtrip(display);
    fprintf(stderr, "wl_display_roundtrip()\n");

    if (compositor == NULL || xdg_wm_base == NULL) {
        fprintf(stderr, "Can't find compositor or xdg_wm_base.\n");
        exit(1);
    }

    // Check surface.
    wl_surface = wl_compositor_create_surface(compositor);
    if (wl_surface == NULL) {
        fprintf(stderr, "Can't create surface.\n");
        exit(1);
    }

    // Check xdg surface.
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, wl_surface);
    if (xdg_surface == NULL) {
        fprintf(stderr, "Can't create xdg surface.\n");
        exit(1);
    }
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    if (xdg_toplevel == NULL) {
        exit(1);
    }
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);


    // MUST COMMIT! or not working on weston.
    wl_surface_commit(wl_surface);

    wl_display_roundtrip(display);

    // Vulkan init.
    auto instance = std::make_shared<vk::Instance>();
    // Vulkan surface.
    auto surface = std::make_shared<vk::Surface>(instance, display, wl_surface);
    // Logical device.
    auto device = std::make_shared<vk::Device>(instance, surface);
    // Render pass.
    auto render_pass = std::make_shared<vk::RenderPass>(
        VK_FORMAT_B8G8R8A8_SRGB, device);
    // Swapchain.
    auto swapchain = std::make_shared<vk::Swapchain>(instance, surface, device,
        render_pass->vk_render_pass(),
        WINDOW_WIDTH, WINDOW_HEIGHT);

    create_vulkan_graphics_pipeline(device, render_pass);
    // create_vulkan_command_pool(device);
    // Command pool.
    auto command_pool = std::make_shared<vk::CommandPool>(device);

    create_vulkan_vertex_buffer(instance, device, command_pool);
    create_vulkan_index_buffer(instance, device, command_pool);
    create_vulkan_command_buffers(device, command_pool);
    create_vulkan_sync_objects(device);

    draw_frame(device, swapchain, render_pass);

    wl_surface_commit(wl_surface);

    int res = wl_display_dispatch(display);
    fprintf(stderr, "Initial dispatch.\n");
    while (res != -1) {
        res = wl_display_dispatch(display);
        fprintf(stderr, "wl_display_dispatch() called.\n");
        draw_frame(device, swapchain, render_pass);
    }
    fprintf(stderr, "wl_display_dispatch() - res: %d\n", res);

    wl_display_disconnect(display);
    printf("Disconnected from display.\n");

    return 0;
}
