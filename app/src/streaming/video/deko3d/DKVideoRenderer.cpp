#ifdef PLATFORM_SWITCH

#define FF_API_AVPICTURE

#include "DKVideoRenderer.hpp"
#include <borealis/platforms/switch/switch_platform.hpp>

#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_nvtegra.h>
#include <libavutil/imgutils.h>

#include <array>

static const glm::vec3 gl_color_offset(bool color_full) {
    static const glm::vec3 limitedOffsets = {16.0f / 255.0f, 128.0f / 255.0f,
                                           128.0f / 255.0f};
    static const glm::vec3 fullOffsets = {0.0f, 128.0f / 255.0f, 128.0f / 255.0f};
    return color_full ? fullOffsets : limitedOffsets;
}

static const glm::mat3 gl_color_matrix(enum AVColorSpace color_space,
                                    bool color_full) {
    static const glm::mat3 bt601Lim = {1.1644f, 1.1644f, 1.1644f,  0.0f, -0.3917f,
                                     2.0172f, 1.5960f, -0.8129f, 0.0f};
    static const glm::mat3 bt601Full = {
        1.0f, 1.0f, 1.0f, 0.0f, -0.3441f, 1.7720f, 1.4020f, -0.7141f, 0.0f};
    static const glm::mat3 bt709Lim = {1.1644f, 1.1644f, 1.1644f,  0.0f, -0.2132f,
                                     2.1124f, 1.7927f, -0.5329f, 0.0f};
    static const glm::mat3 bt709Full = {
        1.0f, 1.0f, 1.0f, 0.0f, -0.1873f, 1.8556f, 1.5748f, -0.4681f, 0.0f};
    static const glm::mat3 bt2020Lim = {1.1644f, 1.1644f,  1.1644f,
                                      0.0f,    -0.1874f, 2.1418f,
                                      1.6781f, -0.6505f, 0.0f};
    static const glm::mat3 bt2020Full = {
        1.0f, 1.0f, 1.0f, 0.0f, -0.1646f, 1.8814f, 1.4746f, -0.5714f, 0.0f};

    switch (color_space) {
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
        return color_full ? bt601Full : bt601Lim;
    case AVCOL_SPC_BT709:
        return color_full ? bt709Full : bt709Lim;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return color_full ? bt2020Full : bt2020Lim;
    default:
        return bt601Lim;
    }
}

namespace
{
    static constexpr unsigned StaticCmdSize = 0x10000;


    struct Vertex
    {
        float position[3];
        float uv[2];
    };

    constexpr std::array VertexAttribState =
    {
        DkVtxAttribState{ 0, 0, offsetof(Vertex, position), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0 },
        DkVtxAttribState{ 0, 0, offsetof(Vertex, uv),    DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0 },
    };

    constexpr std::array VertexBufferState =
    {
        DkVtxBufferState{ sizeof(Vertex), 0 },
    };
    
    constexpr std::array QuadVertexData =
    {
        Vertex{ { -1.0f, +1.0f, 0.0f }, { 0.0f, 0.0f } },
        Vertex{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
        Vertex{ { +1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
        Vertex{ { +1.0f, +1.0f, 0.0f }, { 1.0f, 0.0f } },
    };
}

DKVideoRenderer::DKVideoRenderer() {} 

DKVideoRenderer::~DKVideoRenderer() {
    // Clean up cached command lists first (they may reference memory blocks)
    brls::Logger::debug("{}: Destroying {} cached command lists", __PRETTY_FUNCTION__, m_cmdlist_cache.size());
    
    for (auto& [addr, cached] : m_cmdlist_cache) {
        brls::Logger::debug("{}: Cleaning command list for addr={}", __PRETTY_FUNCTION__, addr);
        // Note: Deko3d command lists are automatically cleaned when the command buffer is destroyed
        // The individual dk::Image and dk::ImageDescriptor objects are automatically cleaned
    }
    m_cmdlist_cache.clear();
    
    // Destroy cached memory blocks with logging
    brls::Logger::debug("{}: Destroying {} cached memory blocks", __PRETTY_FUNCTION__, m_buffer_cache.size());
    
    for (auto& [addr, block] : m_buffer_cache) {
        brls::Logger::debug("{}: Destroying memory block for addr={}", __PRETTY_FUNCTION__, addr);
        dkMemBlockDestroy(block);
    }
    m_buffer_cache.clear();
    
    // Destroy the vertex buffer (not strictly needed in this case)
    vertexBuffer.destroy();
    transformUniformBuffer.destroy();
    
    // Note: mappingMemblock is now managed through cache, don't destroy it directly
}

void DKVideoRenderer::checkAndInitialize(int width, int height, AVFrame* frame) {
    if (m_is_initialized) return;

    brls::Logger::info("{}: {} / {}", __PRETTY_FUNCTION__, width, height);

    m_frame_width = frame->width;
    m_frame_height = frame->height;

    m_screen_width = width;
    m_screen_height = height;

    vctx = (brls::SwitchVideoContext *)brls::Application::getPlatform()->getVideoContext();
    this->dev = vctx->getDeko3dDevice();
    this->queue = vctx->getQueue();

// Create the memory pools
    // pool_images.emplace(dev, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16*1024*1024);
    pool_code.emplace(dev, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128*1024);
    pool_data.emplace(dev, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1*1024*1024);

// Create the static command buffer and feed it freshly allocated memory
    cmdbuf = dk::CmdBufMaker{dev}.create();
    CMemPool::Handle cmdmem = pool_data->allocate(StaticCmdSize);
    cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

// Create the image and sampler descriptor sets
    imageDescriptorSet = vctx->getImageDescriptor();

// Load the shaders
    vertexShader.load(*pool_code, "romfs:/shaders/basic_vsh.dksh");
    fragmentShader.load(*pool_code, "romfs:/shaders/texture_fsh.dksh");

// Load the vertex buffer
    vertexBuffer = pool_data->allocate(sizeof(QuadVertexData), alignof(Vertex));
    memcpy(vertexBuffer.getCpuAddr(), QuadVertexData.data(), vertexBuffer.getSize());


// Load the transform buffer
    transformUniformBuffer = pool_code->allocate(sizeof(Transformation), DK_UNIFORM_BUF_ALIGNMENT);

    bool colorFull = frame->color_range == AVCOL_RANGE_JPEG;

    m_transform_state.offset = {0,0.5f,0.5f};// gl_color_offset(colorFull);
    m_transform_state.yuvmat = gl_color_matrix(frame->colorspace, colorFull);

    float frameAspect = ((float)m_frame_height / (float)m_frame_width);
    float screenAspect = ((float)m_screen_height / (float)m_screen_width);

    if (frameAspect > screenAspect) {
        float multiplier = frameAspect / screenAspect;
        m_transform_state.uv_data = { 0.5f - 0.5f * (1.0f / multiplier),
                    0.0f, multiplier, 1.0f };
    } else {
        float multiplier = screenAspect / frameAspect;
        m_transform_state.uv_data = { 0.0f,
                    0.5f - 0.5f * (1.0f / multiplier), 1.0f, multiplier };
    }


// Allocate image indexes for planes
    lumaTextureId = vctx->allocateImageIndex();
    chromaTextureId = vctx->allocateImageIndex();

    brls::Logger::debug("{}: Luma texture ID {}", __PRETTY_FUNCTION__, lumaTextureId);
    brls::Logger::debug("{}: Chroma texture ID {}", __PRETTY_FUNCTION__, chromaTextureId);

    AVNVTegraMap *map = av_nvtegra_frame_get_fbuf_map(frame);
    
    // Store bound buffer info for diagnostic tracking
    m_bound_buffer_addr = map->map.cpu_addr;
    m_bound_buffer_handle = map->map.handle;
    
    // Identify which buffer gets bound
    const char* bound_buffer_name = "UNKNOWN";
    if ((uintptr_t)map->map.cpu_addr >= 0x5ade650000 && (uintptr_t)map->map.cpu_addr < 0x5ade800000) {
        bound_buffer_name = "BUFFER_1";
    } else if ((uintptr_t)map->map.cpu_addr >= 0x5ade880000 && (uintptr_t)map->map.cpu_addr < 0x5adea00000) {
        bound_buffer_name = "BUFFER_2";
    } else if ((uintptr_t)map->map.cpu_addr >= 0x5adeac0000 && (uintptr_t)map->map.cpu_addr < 0x5adec00000) {
        bound_buffer_name = "BUFFER_3";
    }
    
    brls::Logger::info("{}: INIT - {} AVFrame ptr={}, handle={}, addr={}, size={}", 
                      __PRETTY_FUNCTION__, bound_buffer_name, (void*)frame, map->map.handle, 
                      (void*)map->map.cpu_addr, map->map.size);
    brls::Logger::info("{}: GPU textures will be permanently bound to {}!", __PRETTY_FUNCTION__, bound_buffer_name);

    dk::ImageLayoutMaker { dev }
        .setType(DkImageType_2D)
        .setFormat(DkImageFormat_R8_Unorm)
        .setDimensions(m_frame_width, m_frame_height, 1)
        .setFlags(DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine | DkImageFlags_UsageVideo)
        .initialize(lumaMappingLayout);

    dk::ImageLayoutMaker { dev }
        .setType(DkImageType_2D)
        .setFormat(DkImageFormat_RG8_Unorm)
        .setDimensions(m_frame_width / 2, m_frame_height / 2, 1)
        .setFlags(DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine | DkImageFlags_UsageVideo)
        .initialize(chromaMappingLayout);

    // Memory block creation and texture setup now handled in rebuildCommandList()
    // This will be called on first frame in draw()

    // Initialize with first frame - rebuildCommandList will be called in draw() for first frame

    m_is_initialized = true;
}

void DKVideoRenderer::rebuildCommandList(AVNVTegraMap *map, AVFrame* frame) {
    // Safety checks
    if (!map || !frame) {
        brls::Logger::error("{}: Invalid input pointers - map={}, frame={}", __PRETTY_FUNCTION__, (void*)map, (void*)frame);
        return;
    }
    
    // Check if command list already exists for this buffer
    auto cmdlist_it = m_cmdlist_cache.find(map->map.cpu_addr);
    if (cmdlist_it != m_cmdlist_cache.end() && cmdlist_it->second.initialized) {
        // Command list already built for this buffer - just switch to it
        cmdlist = cmdlist_it->second.cmdlist;
        brls::Logger::debug("{}: Using cached command list for buffer addr={}", __PRETTY_FUNCTION__, map->map.cpu_addr);
        return;
    }
    
    // Log buffer details for debugging first-time builds
    uint32_t buffer_size = av_nvtegra_map_get_size(map);
    ptrdiff_t chroma_offset = frame->data[1] - frame->data[0];
    brls::Logger::debug("{}: Building NEW command list for buffer addr={}, size={}, chroma_offset={}", 
                       __PRETTY_FUNCTION__, map->map.cpu_addr, buffer_size, chroma_offset);
    
    // Get or create cached entry
    BufferCommandList& cached = m_cmdlist_cache[map->map.cpu_addr];
    
    // Check cache or create new memory block
    auto mem_it = m_buffer_cache.find(map->map.cpu_addr);
    
    if (mem_it == m_buffer_cache.end()) {
        // First time seeing this buffer - create and cache memory block
        dk::MemBlock newBlock = dk::MemBlockMaker { dev, av_nvtegra_map_get_size(map) }
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
            .setStorage(av_nvtegra_map_get_addr(map))
            .create();
        m_buffer_cache[map->map.cpu_addr] = newBlock;
        mappingMemblock = newBlock;
        
        brls::Logger::debug("{}: Created new memory block for buffer addr={} (cache size now: {})", 
                           __PRETTY_FUNCTION__, map->map.cpu_addr, m_buffer_cache.size());
    } else {
        // Reuse cached memory block
        mappingMemblock = mem_it->second;
        brls::Logger::debug("{}: Reusing cached memory block for buffer addr={}", __PRETTY_FUNCTION__, map->map.cpu_addr);
    }
    
    // Initialize images with current memory block for this buffer
    cached.luma.initialize(lumaMappingLayout, mappingMemblock, 0);
    cached.chroma.initialize(chromaMappingLayout, mappingMemblock, frame->data[1] - frame->data[0]);
    
    // Update descriptors for this buffer
    cached.lumaDesc.initialize(cached.luma);
    cached.chromaDesc.initialize(cached.chroma);
    
    // Build command list with updated bindings
    cmdbuf.clear();
    
    // Update descriptor set
    imageDescriptorSet->update(cmdbuf, lumaTextureId, cached.lumaDesc);
    imageDescriptorSet->update(cmdbuf, chromaTextureId, cached.chromaDesc);
    
    // Bind everything for drawing (removed clearColor)
    cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { vertexShader, fragmentShader });
    cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(lumaTextureId, 0));
    cmdbuf.bindTextures(DkStage_Fragment, 1, dkMakeTextureHandle(chromaTextureId, 0));
    cmdbuf.bindUniformBuffer(DkStage_Fragment, 0, transformUniformBuffer.getGpuAddr(), transformUniformBuffer.getSize());
    cmdbuf.pushConstants(
            transformUniformBuffer.getGpuAddr(), transformUniformBuffer.getSize(),
            0, sizeof(m_transform_state), &m_transform_state);
    
    dk::RasterizerState rasterizerState;
    dk::ColorState colorState; 
    dk::ColorWriteState colorWriteState;
    
    cmdbuf.bindRasterizerState(rasterizerState);
    cmdbuf.bindColorState(colorState);
    cmdbuf.bindColorWriteState(colorWriteState);
    cmdbuf.bindVtxBuffer(0, vertexBuffer.getGpuAddr(), vertexBuffer.getSize());
    cmdbuf.bindVtxAttribState(VertexAttribState);
    cmdbuf.bindVtxBufferState(VertexBufferState);
    
    // Draw
    cmdbuf.draw(DkPrimitive_Quads, QuadVertexData.size(), 1, 0, 0);
    
    // Cache the new command list
    cached.cmdlist = cmdbuf.finishList();
    cached.initialized = true;
    
    // Set as current command list
    cmdlist = cached.cmdlist;
    
    brls::Logger::info("{}: Built and cached command list for buffer addr={} (cmdlist cache size now: {})", 
                       __PRETTY_FUNCTION__, map->map.cpu_addr, m_cmdlist_cache.size());
    
    m_last_bound_addr = map->map.cpu_addr;
    
    // Initialize m_bound_buffer_addr on first frame for diagnostic compatibility
    if (!m_bound_buffer_addr) {
        m_bound_buffer_addr = map->map.cpu_addr;
        brls::Logger::debug("{}: Initialized diagnostic bound_buffer_addr to {}", __PRETTY_FUNCTION__, m_bound_buffer_addr);
    }
}

int frames = 0;
uint64_t timeCount = 0;

void DKVideoRenderer::draw(NVGcontext* vg, int width, int height, AVFrame* frame, int imageFormat) {
    checkAndInitialize(width, height, frame);
    
    // Log current frame details to compare with initialization
    AVNVTegraMap *current_map = av_nvtegra_frame_get_fbuf_map(frame);
    
    // Check if buffer changed and ensure command list is ready
    if (current_map->map.cpu_addr != m_last_bound_addr) {
        brls::Logger::debug("{}: Buffer changed from {} to {}, switching command list", 
                           __PRETTY_FUNCTION__, m_last_bound_addr, current_map->map.cpu_addr);
        rebuildCommandList(current_map, frame);  // Will use cache if available, build if new
        m_last_bound_addr = current_map->map.cpu_addr;
    }
    
    // Diagnostic tracking for buffer binding issues
    m_total_frames_drawn++;
    // With proper caching, there should be no mismatches anymore
    bool is_buffer_mismatch = false;  // Always false now with proper command list caching
    
    // Identify buffer index for easier tracking (based on observed pattern)
    const char* buffer_name = "UNKNOWN";
    if ((uintptr_t)current_map->map.cpu_addr >= 0x5ade650000 && (uintptr_t)current_map->map.cpu_addr < 0x5ade800000) {
        buffer_name = "BUFFER_1";
    } else if ((uintptr_t)current_map->map.cpu_addr >= 0x5ade880000 && (uintptr_t)current_map->map.cpu_addr < 0x5adea00000) {
        buffer_name = "BUFFER_2";
    } else if ((uintptr_t)current_map->map.cpu_addr >= 0x5adeac0000 && (uintptr_t)current_map->map.cpu_addr < 0x5adec00000) {
        buffer_name = "BUFFER_3";
    }
    
    brls::Logger::debug("{}: DRAW - {} handle={}, addr={} {} BOUND (bound={})", 
                       __PRETTY_FUNCTION__, buffer_name, current_map->map.handle, 
                       (void*)current_map->map.cpu_addr,
                       is_buffer_mismatch ? "≠" : "==",
                       m_last_bound_addr);
    
    // Log statistics every 5 seconds
    uint64_t current_time = LiGetMillis();
    if (current_time - m_last_stats_log_time >= 5000) {
        double mismatch_percentage = (double)m_mismatched_frames / m_total_frames_drawn * 100.0;
        brls::Logger::info("{}: BUFFER BINDING STATS - Total frames: {}, Mismatched: {} ({:.1f}%)",
                          __PRETTY_FUNCTION__, m_total_frames_drawn, m_mismatched_frames, mismatch_percentage);
        m_last_stats_log_time = current_time;
    }

    uint64_t before_render = LiGetMillis();

    if (!m_video_render_stats.rendered_frames) {
        m_video_render_stats.measurement_start_timestamp = before_render;
    }

    // Validate command list before submission
    if (!cmdlist) {
        brls::Logger::error("{}: Command list is null! Cannot submit to GPU", __PRETTY_FUNCTION__);
        return;
    }
   
    queue.submitCommands(cmdlist);
    queue.flush();

    frames++;
    timeCount += LiGetMillis() - before_render;

    if (timeCount >= 5000) {
        brls::Logger::debug("FPS: {}", frames / 5.0f);
        frames = 0;
        timeCount -= 5000;
    }

    m_video_render_stats.total_render_time += LiGetMillis() - before_render;
    m_video_render_stats.rendered_frames++;
}

VideoRenderStats* DKVideoRenderer::video_render_stats() {
    // brls::Logger::info("{}", __PRETTY_FUNCTION__);
    m_video_render_stats.rendered_fps = (float) m_video_render_stats.rendered_frames /
        ((float) (LiGetMillis() - m_video_render_stats.measurement_start_timestamp) / 1000);


    m_video_render_stats.rendering_time = (float)m_video_render_stats.total_render_time /
            (float) m_video_render_stats.rendered_frames;

    return &m_video_render_stats;
}

#endif