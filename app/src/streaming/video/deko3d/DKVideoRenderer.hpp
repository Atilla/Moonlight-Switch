#ifdef PLATFORM_SWITCH

#pragma once
#include "IVideoRenderer.hpp"
#include <deko3d.hpp>

#include <glm/mat4x4.hpp>

#include <borealis.hpp>
#include <borealis/platforms/switch/switch_video.hpp>
#include <nanovg/framework/CShader.h>
#include <nanovg/framework/CExternalImage.h>
#include <nanovg/framework/CDescriptorSet.h>
#include <optional>
#include <unordered_map>

struct AVFrame;
struct AVNVTegraMap;

struct Transformation {
    glm::mat3 yuvmat;
    glm::vec3 offset;
    glm::vec4 uv_data;
};

class DKVideoRenderer : public IVideoRenderer {
  public:
    DKVideoRenderer();
    ~DKVideoRenderer();

    void draw(NVGcontext* vg, int width, int height, AVFrame* frame, int imageFormat) override;

    VideoRenderStats* video_render_stats() override;

  private:
    void checkAndInitialize(int width, int height, AVFrame* frame);
    void rebuildCommandList(AVNVTegraMap *map, AVFrame* frame);

    bool m_is_initialized = false;
    
    int m_frame_width = 0;
    int m_frame_height = 0;
    int m_screen_width = 0;
    int m_screen_height = 0;

    brls::SwitchVideoContext *vctx = nullptr;
    dk::Device dev;
    dk::Queue queue;

    // std::optional<CMemPool> pool_images;
    std::optional<CMemPool> pool_code;
    std::optional<CMemPool> pool_data;

    dk::UniqueCmdBuf cmdbuf;
    DkCmdList cmdlist;

    CDescriptorSet<4096U> *imageDescriptorSet;
    // CDescriptorSet<1> samplerDescriptorSet;

    CShader vertexShader;
    CShader fragmentShader;

    CMemPool::Handle vertexBuffer;
    CMemPool::Handle transformUniformBuffer;
    Transformation m_transform_state;  // Cached transformation for command rebuilding

    dk::ImageLayout lumaMappingLayout; 
    dk::ImageLayout chromaMappingLayout; 
    dk::MemBlock mappingMemblock;

    dk::Image luma;
    dk::Image chroma;

    dk::ImageDescriptor lumaDesc;
    dk::ImageDescriptor chromaDesc;

    int lumaTextureId = 0;
    int chromaTextureId = 0;

    VideoRenderStats m_video_render_stats = {};
    
    // Diagnostic tracking for buffer binding issues
    void* m_bound_buffer_addr = nullptr;  // Address of buffer bound during initialization
    uint32_t m_bound_buffer_handle = 0;   // Handle of buffer bound during initialization
    uint64_t m_total_frames_drawn = 0;    // Total frames passed to draw()
    uint64_t m_mismatched_frames = 0;     // Frames with different buffer than bound
    uint64_t m_last_stats_log_time = 0;   // Last time we logged statistics
    
    // Dynamic buffer binding support
    std::unordered_map<void*, dk::MemBlock> m_buffer_cache;  // Cache memory blocks per buffer
    void* m_last_bound_addr = nullptr;  // Address of currently bound buffer
};

#endif // PLATFORM_SWITCH