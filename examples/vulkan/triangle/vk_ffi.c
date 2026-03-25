/* vk_ffi.c — Minimal Vulkan + XCB wrapper for Glyph (all long long ABI) */
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES 2
#define VK_CHECK(f) do { VkResult _r = (f); if (_r != VK_SUCCESS) { \
    fprintf(stderr, "Vulkan error %d at %s:%d\n", _r, __FILE__, __LINE__); exit(1); } } while(0)

/* ── State ─────────────────────────────────────────────────────────── */
static xcb_connection_t        *conn;
static xcb_window_t             win;
static xcb_intern_atom_reply_t *del_reply;
static uint32_t                 win_w, win_h;
static int                      should_quit;

static VkInstance               instance;
static VkPhysicalDevice         physDevice;
static VkDevice                 device;
static VkQueue                  gfxQueue;
static uint32_t                 queueFamily;
static VkSurfaceKHR             surface;
static VkSwapchainKHR           swapchain;
static VkFormat                 swapFormat;
static VkExtent2D               swapExtent;
static uint32_t                 swapImageCount;
static VkImage                  swapImages[8];
static VkImageView              swapImageViews[8];
static VkRenderPass             renderPass;
static VkFramebuffer            framebuffers[8];
static VkPipelineLayout         pipelineLayout;
static VkPipeline               graphicsPipeline;
static VkDescriptorSetLayout    descSetLayout;
static VkDescriptorPool         descPool;
static VkCommandPool            cmdPool;

static VkCommandBuffer          cmdBufs[MAX_FRAMES];
static VkFence                  fences[MAX_FRAMES];
static VkSemaphore              imageAvailSems[MAX_FRAMES];
static VkSemaphore              renderDoneSems[MAX_FRAMES];

static VkBuffer                 vertBuf, idxBuf;
static VkDeviceMemory           vertMem, idxMem;

static VkBuffer                 uboBuf[MAX_FRAMES];
static VkDeviceMemory           uboMem[MAX_FRAMES];
static VkDescriptorSet          descSets[MAX_FRAMES];
static uint8_t                 *uboMapped[MAX_FRAMES];

static VkImage                  depthImage;
static VkDeviceMemory           depthMem;
static VkImageView              depthView;
static VkFormat                 depthFormat;

/* ── Helpers ───────────────────────────────────────────────────────── */

static uint32_t find_memory_type(uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physDevice, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((typeBits & (1 << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    fprintf(stderr, "No suitable memory type!\n"); exit(1);
}

static void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags props, VkBuffer *buf, VkDeviceMemory *mem) {
    VkBufferCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VK_CHECK(vkCreateBuffer(device, &ci, NULL, buf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, *buf, &mr);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size, .memoryTypeIndex = find_memory_type(mr.memoryTypeBits, props) };
    VK_CHECK(vkAllocateMemory(device, &ai, NULL, mem));
    VK_CHECK(vkBindBufferMemory(device, *buf, *mem, 0));
}

static VkShaderModule load_shader(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open shader: %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint32_t *code = malloc(sz);
    fread(code, 1, sz, f); fclose(f);
    VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sz, .pCode = code };
    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(device, &ci, NULL, &mod));
    free(code);
    return mod;
}

/* Read Glyph string: {char* ptr, int64_t len} */
static char* glyph_to_cstr(long long s) {
    char *ptr = *(char**)s;
    int64_t len = *(int64_t*)(s + 8);
    char *buf = malloc(len + 1);
    memcpy(buf, ptr, len); buf[len] = '\0';
    return buf;
}

/* ── Window (XCB) ──────────────────────────────────────────────────── */

long long vk_init_window(long long w, long long h) {
    win_w = (uint32_t)w; win_h = (uint32_t)h; should_quit = 0;
    conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn)) { fprintf(stderr, "XCB connect failed\n"); return -1; }

    const xcb_setup_t *setup = xcb_get_setup(conn);
    xcb_screen_t *scr = xcb_setup_roots_iterator(setup).data;

    win = xcb_generate_id(conn);
    uint32_t vals[2] = { scr->black_pixel,
        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_STRUCTURE_NOTIFY };
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, scr->root,
                      0, 0, win_w, win_h, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, vals);

    /* WM_DELETE_WINDOW protocol */
    xcb_intern_atom_cookie_t pc = xcb_intern_atom(conn, 1, 12, "WM_PROTOCOLS");
    xcb_intern_atom_cookie_t dc = xcb_intern_atom(conn, 0, 16, "WM_DELETE_WINDOW");
    xcb_intern_atom_reply_t *pr = xcb_intern_atom_reply(conn, pc, NULL);
    del_reply = xcb_intern_atom_reply(conn, dc, NULL);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, pr->atom, 4, 32, 1, &del_reply->atom);
    free(pr);

    const char *title = "Glyph Vulkan Triangle";
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_NAME,
                        XCB_ATOM_STRING, 8, strlen(title), title);
    xcb_map_window(conn, win);
    xcb_flush(conn);
    return 0;
}

long long vk_poll_events(long long dummy) {
    xcb_generic_event_t *ev;
    while ((ev = xcb_poll_for_event(conn))) {
        uint8_t t = ev->response_type & 0x7f;
        if (t == XCB_KEY_PRESS) {
            if (((xcb_key_press_event_t*)ev)->detail == 9) should_quit = 1;
        } else if (t == XCB_CLIENT_MESSAGE) {
            if (((xcb_client_message_event_t*)ev)->data.data32[0] == del_reply->atom)
                should_quit = 1;
        }
        free(ev);
    }
    return (long long)should_quit;
}

long long vk_get_width(long long dummy)  { return (long long)swapExtent.width; }
long long vk_get_height(long long dummy) { return (long long)swapExtent.height; }

/* ── Vulkan Instance ───────────────────────────────────────────────── */

long long vk_init_instance(long long dummy) {
    const char *exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME };
    VkApplicationInfo ai = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Glyph Triangle", .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &ai, .enabledExtensionCount = 2, .ppEnabledExtensionNames = exts };
    VK_CHECK(vkCreateInstance(&ci, NULL, &instance));
    return 0;
}

/* ── Surface ───────────────────────────────────────────────────────── */

long long vk_init_surface(long long dummy) {
    VkXcbSurfaceCreateInfoKHR ci = { .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = conn, .window = win };
    VK_CHECK(vkCreateXcbSurfaceKHR(instance, &ci, NULL, &surface));
    return 0;
}

/* ── Physical + Logical Device ─────────────────────────────────────── */

long long vk_init_device(long long dummy) {
    uint32_t cnt = 0;
    vkEnumeratePhysicalDevices(instance, &cnt, NULL);
    VkPhysicalDevice *ds = malloc(cnt * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(instance, &cnt, ds);
    physDevice = ds[0]; free(ds);

    uint32_t qc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &qc, NULL);
    VkQueueFamilyProperties *qp = malloc(qc * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &qc, qp);
    queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qc; i++) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physDevice, i, surface, &present);
        if ((qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { queueFamily = i; break; }
    }
    free(qp);
    if (queueFamily == UINT32_MAX) { fprintf(stderr, "No graphics+present queue\n"); return -1; }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queueFamily, .queueCount = 1, .pQueuePriorities = &prio };
    const char *de[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = de };
    VK_CHECK(vkCreateDevice(physDevice, &dci, NULL, &device));
    vkGetDeviceQueue(device, queueFamily, 0, &gfxQueue);
    return 0;
}

/* ── Swapchain ─────────────────────────────────────────────────────── */

long long vk_init_swapchain(long long dummy) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, surface, &caps);

    uint32_t fc;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &fc, NULL);
    VkSurfaceFormatKHR *fmts = malloc(fc * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &fc, fmts);
    swapFormat = fmts[0].format;
    VkColorSpaceKHR cs = fmts[0].colorSpace;
    for (uint32_t i = 0; i < fc; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            swapFormat = fmts[i].format; cs = fmts[i].colorSpace; break;
        }
    }
    free(fmts);

    uint32_t pc;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface, &pc, NULL);
    VkPresentModeKHR *pms = malloc(pc * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface, &pc, pms);
    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < pc; i++) {
        if (pms[i] == VK_PRESENT_MODE_MAILBOX_KHR) { pm = pms[i]; break; }
    }
    free(pms);

    swapExtent = (caps.currentExtent.width != UINT32_MAX)
        ? caps.currentExtent : (VkExtent2D){ win_w, win_h };

    uint32_t ic = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && ic > caps.maxImageCount) ic = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface, .minImageCount = ic, .imageFormat = swapFormat,
        .imageColorSpace = cs, .imageExtent = swapExtent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = pm, .clipped = VK_TRUE };
    VK_CHECK(vkCreateSwapchainKHR(device, &ci, NULL, &swapchain));

    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, NULL);
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);

    for (uint32_t i = 0; i < swapImageCount; i++) {
        VkImageViewCreateInfo iv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapImages[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = swapFormat,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        VK_CHECK(vkCreateImageView(device, &iv, NULL, &swapImageViews[i]));
    }
    return 0;
}

/* ── Depth Buffer ──────────────────────────────────────────────────── */

static void init_depth(void) {
    depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = depthFormat,
        .extent = { swapExtent.width, swapExtent.height, 1 },
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT };
    VK_CHECK(vkCreateImage(device, &ici, NULL, &depthImage));

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, depthImage, &mr);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    VK_CHECK(vkAllocateMemory(device, &ai, NULL, &depthMem));
    VK_CHECK(vkBindImageMemory(device, depthImage, depthMem, 0));

    VkImageViewCreateInfo iv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depthImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = depthFormat,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 } };
    VK_CHECK(vkCreateImageView(device, &iv, NULL, &depthView));
}

/* ── Render Pass ───────────────────────────────────────────────────── */

long long vk_init_render_pass(long long dummy) {
    init_depth();
    VkAttachmentDescription atts[2] = {
        { .format = swapFormat, .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR },
        { .format = depthFormat, .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL }
    };
    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &cref, .pDepthStencilAttachment = &dref };
    VkSubpassDependency deps[2] = {
        { .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
          .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT },
        { .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
          .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = 0,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT }
    };
    VkRenderPassCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2, .pAttachments = atts,
        .subpassCount = 1, .pSubpasses = &sp,
        .dependencyCount = 2, .pDependencies = deps };
    VK_CHECK(vkCreateRenderPass(device, &ci, NULL, &renderPass));
    return 0;
}

/* ── Framebuffers ──────────────────────────────────────────────────── */

long long vk_init_framebuffers(long long dummy) {
    for (uint32_t i = 0; i < swapImageCount; i++) {
        VkImageView atts[2] = { swapImageViews[i], depthView };
        VkFramebufferCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = renderPass, .attachmentCount = 2, .pAttachments = atts,
            .width = swapExtent.width, .height = swapExtent.height, .layers = 1 };
        VK_CHECK(vkCreateFramebuffer(device, &ci, NULL, &framebuffers[i]));
    }
    return 0;
}

/* ── Command Pool + Buffers ────────────────────────────────────────── */

long long vk_init_command_pool(long long dummy) {
    VkCommandPoolCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamily };
    VK_CHECK(vkCreateCommandPool(device, &ci, NULL, &cmdPool));
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES };
    VK_CHECK(vkAllocateCommandBuffers(device, &ai, cmdBufs));
    return 0;
}

/* ── Vertex + Index Buffers ────────────────────────────────────────── */

long long vk_init_vertex_buffer(long long dummy) {
    float verts[] = {
         1.0f,  1.0f, 0.0f,   1.0f, 0.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,   0.0f, 1.0f, 0.0f,
         0.0f, -1.0f, 0.0f,   0.0f, 0.0f, 1.0f
    };
    uint32_t indices[] = { 0, 1, 2 };
    VkDeviceSize vSz = sizeof(verts), iSz = sizeof(indices);

    VkBuffer sv, si; VkDeviceMemory svm, sim;
    create_buffer(vSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &sv, &svm);
    create_buffer(iSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &si, &sim);

    void *data;
    vkMapMemory(device, svm, 0, vSz, 0, &data); memcpy(data, verts, vSz); vkUnmapMemory(device, svm);
    vkMapMemory(device, sim, 0, iSz, 0, &data); memcpy(data, indices, iSz); vkUnmapMemory(device, sim);

    create_buffer(vSz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vertBuf, &vertMem);
    create_buffer(iSz, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &idxBuf, &idxMem);

    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb;
    VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &cb));
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cb, &bi);
    VkBufferCopy r = { .size = vSz }; vkCmdCopyBuffer(cb, sv, vertBuf, 1, &r);
    r.size = iSz; vkCmdCopyBuffer(cb, si, idxBuf, 1, &r);
    vkEndCommandBuffer(cb);

    VkSubmitInfo sub = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &cb };
    vkQueueSubmit(gfxQueue, 1, &sub, VK_NULL_HANDLE);
    vkQueueWaitIdle(gfxQueue);

    vkFreeCommandBuffers(device, cmdPool, 1, &cb);
    vkDestroyBuffer(device, sv, NULL); vkFreeMemory(device, svm, NULL);
    vkDestroyBuffer(device, si, NULL); vkFreeMemory(device, sim, NULL);
    return 0;
}

/* ── Uniform Buffers ───────────────────────────────────────────────── */

long long vk_init_uniform_buffers(long long dummy) {
    VkDeviceSize sz = 3 * 16 * sizeof(float); /* 3 mat4 = 192 bytes */
    for (int i = 0; i < MAX_FRAMES; i++) {
        create_buffer(sz, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &uboBuf[i], &uboMem[i]);
        vkMapMemory(device, uboMem[i], 0, sz, 0, (void**)&uboMapped[i]);
    }
    return 0;
}

/* ── Descriptors ───────────────────────────────────────────────────── */

long long vk_init_descriptors(long long dummy) {
    VkDescriptorSetLayoutBinding b = { .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT };
    VkDescriptorSetLayoutCreateInfo lci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &b };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &lci, NULL, &descSetLayout));

    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES };
    VkDescriptorPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = MAX_FRAMES, .poolSizeCount = 1, .pPoolSizes = &ps };
    VK_CHECK(vkCreateDescriptorPool(device, &pci, NULL, &descPool));

    VkDeviceSize uSz = 3 * 16 * sizeof(float);
    for (int i = 0; i < MAX_FRAMES; i++) {
        VkDescriptorSetAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descPool, .descriptorSetCount = 1, .pSetLayouts = &descSetLayout };
        VK_CHECK(vkAllocateDescriptorSets(device, &ai, &descSets[i]));
        VkDescriptorBufferInfo bi = { .buffer = uboBuf[i], .range = uSz };
        VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descSets[i], .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bi };
        vkUpdateDescriptorSets(device, 1, &w, 0, NULL);
    }
    return 0;
}

/* ── Pipeline ──────────────────────────────────────────────────────── */

long long vk_init_pipeline(long long vert_path, long long frag_path) {
    char *vp = glyph_to_cstr(vert_path);
    char *fp = glyph_to_cstr(frag_path);
    VkShaderModule vm = load_shader(vp), fm = load_shader(fp);
    free(vp); free(fp);

    VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &descSetLayout };
    VK_CHECK(vkCreatePipelineLayout(device, &plci, NULL, &pipelineLayout));

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vm, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fm, .pName = "main" }
    };

    VkVertexInputBindingDescription vb = { .stride = 6 * sizeof(float),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription va[2] = {
        { .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
        { .location = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 3 * sizeof(float) }
    };
    VkPipelineVertexInputStateCreateInfo viCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vb,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = va };
    VkPipelineInputAssemblyStateCreateInfo iaCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo vpCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo msCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineDepthStencilStateCreateInfo dsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL };
    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cbCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cba };
    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn };

    VkGraphicsPipelineCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &viCI, .pInputAssemblyState = &iaCI,
        .pViewportState = &vpCI, .pRasterizationState = &rsCI,
        .pMultisampleState = &msCI, .pDepthStencilState = &dsCI,
        .pColorBlendState = &cbCI, .pDynamicState = &dynCI,
        .layout = pipelineLayout, .renderPass = renderPass };
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, NULL, &graphicsPipeline));

    vkDestroyShaderModule(device, vm, NULL);
    vkDestroyShaderModule(device, fm, NULL);
    return 0;
}

/* ── Sync Primitives ───────────────────────────────────────────────── */

long long vk_init_sync(long long dummy) {
    for (int i = 0; i < MAX_FRAMES; i++) {
        VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        VK_CHECK(vkCreateFence(device, &fci, NULL, &fences[i]));
        VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(device, &sci, NULL, &imageAvailSems[i]));
        VK_CHECK(vkCreateSemaphore(device, &sci, NULL, &renderDoneSems[i]));
    }
    return 0;
}

/* ── Per-frame Rendering ───────────────────────────────────────────── */

long long vk_frame_begin(long long frame) {
    vkWaitForFences(device, 1, &fences[frame], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fences[frame]);
    return 0;
}

long long vk_acquire_image(long long frame) {
    uint32_t idx;
    VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
        imageAvailSems[frame], VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) return -1;
    return (long long)idx;
}

long long vk_set_f32(long long frame, long long idx, long long fval) {
    union { long long i; double d; } u;
    u.i = fval;
    ((float*)uboMapped[frame])[idx] = (float)u.d;
    return 0;
}

long long vk_record_begin(long long frame, long long img) {
    VkCommandBuffer cb = cmdBufs[frame];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VK_CHECK(vkBeginCommandBuffer(cb, &bi));
    VkClearValue clr[2] = { { .color = {{ 0.0f, 0.0f, 0.2f, 1.0f }} },
                             { .depthStencil = { 1.0f, 0 } } };
    VkRenderPassBeginInfo rp = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderPass, .framebuffer = framebuffers[img],
        .renderArea = {{ 0, 0 }, swapExtent },
        .clearValueCount = 2, .pClearValues = clr };
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    return 0;
}

long long vk_record_draw(long long frame) {
    VkCommandBuffer cb = cmdBufs[frame];
    VkViewport vp = { 0, 0, (float)swapExtent.width, (float)swapExtent.height, 0.0f, 1.0f };
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = {{ 0, 0 }, swapExtent };
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
        0, 1, &descSets[frame], 0, NULL);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vertBuf, &off);
    vkCmdBindIndexBuffer(cb, idxBuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, 3, 1, 0, 0, 0);
    return 0;
}

long long vk_record_end(long long frame) {
    vkCmdEndRenderPass(cmdBufs[frame]);
    VK_CHECK(vkEndCommandBuffer(cmdBufs[frame]));
    return 0;
}

long long vk_submit(long long frame, long long img) {
    VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &imageAvailSems[frame],
        .pWaitDstStageMask = &ws,
        .commandBufferCount = 1, .pCommandBuffers = &cmdBufs[frame],
        .signalSemaphoreCount = 1, .pSignalSemaphores = &renderDoneSems[frame] };
    VK_CHECK(vkQueueSubmit(gfxQueue, 1, &si, fences[frame]));
    return 0;
}

long long vk_present(long long frame, long long img) {
    uint32_t idx = (uint32_t)img;
    VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &renderDoneSems[frame],
        .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &idx };
    VkResult r = vkQueuePresentKHR(gfxQueue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) return -1;
    return 0;
}

/* ── Cleanup ───────────────────────────────────────────────────────── */

long long vk_cleanup(long long dummy) {
    vkDeviceWaitIdle(device);

    for (int i = 0; i < MAX_FRAMES; i++) {
        vkDestroyFence(device, fences[i], NULL);
        vkDestroySemaphore(device, imageAvailSems[i], NULL);
        vkDestroySemaphore(device, renderDoneSems[i], NULL);
        vkDestroyBuffer(device, uboBuf[i], NULL);
        vkFreeMemory(device, uboMem[i], NULL);
    }
    vkDestroyPipeline(device, graphicsPipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyDescriptorPool(device, descPool, NULL);
    vkDestroyDescriptorSetLayout(device, descSetLayout, NULL);
    vkDestroyBuffer(device, vertBuf, NULL); vkFreeMemory(device, vertMem, NULL);
    vkDestroyBuffer(device, idxBuf, NULL);  vkFreeMemory(device, idxMem, NULL);
    vkDestroyCommandPool(device, cmdPool, NULL);

    for (uint32_t i = 0; i < swapImageCount; i++) {
        vkDestroyFramebuffer(device, framebuffers[i], NULL);
        vkDestroyImageView(device, swapImageViews[i], NULL);
    }
    vkDestroyImageView(device, depthView, NULL);
    vkDestroyImage(device, depthImage, NULL);
    vkFreeMemory(device, depthMem, NULL);
    vkDestroyRenderPass(device, renderPass, NULL);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    if (del_reply) free(del_reply);
    xcb_destroy_window(conn, win);
    xcb_disconnect(conn);
    return 0;
}
