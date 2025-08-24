#include "SwitchMoonlightSessionDecoderAndRenderProvider.hpp"
#include "FFmpegVideoDecoder.hpp"
#include "Settings.hpp"
#include "SDLAudiorenderer.hpp"

#ifdef PLATFORM_SWITCH
#include "AudrenAudioRenderer.hpp"
#endif

#ifdef PLATFORM_SWITCH
#include "DKVideoRenderer.hpp"
#elif defined(USE_METAL_RENDERER)
#include "MetalVideoRenderer.hpp"
#elif defined(USE_GL_RENDERER)
#include "GLVideoRenderer.hpp"
#else
#error No renderer selected, enable USE_GL_RENDERER or USE_METAL_RENDERER
#endif

IFFmpegVideoDecoder*
SwitchMoonlightSessionDecoderAndRenderProvider::video_decoder() {
    return new FFmpegVideoDecoder();
}

IVideoRenderer*
SwitchMoonlightSessionDecoderAndRenderProvider::video_renderer() {
#ifdef PLATFORM_SWITCH
    return new DKVideoRenderer();
#elif defined(USE_METAL_RENDERER)
    return new MetalVideoRenderer();
#elif defined(USE_GL_RENDERER)
    return new GLVideoRenderer();
#endif
}

IAudioRenderer*
SwitchMoonlightSessionDecoderAndRenderProvider::audio_renderer() {
#ifdef PLATFORM_SWITCH
    if (Settings::instance().audio_backend() == SDL) {
        return new SDLAudioRenderer();
    } else {
        return new AudrenAudioRenderer();
    }
#else
    return new SDLAudioRenderer();
#endif
}
