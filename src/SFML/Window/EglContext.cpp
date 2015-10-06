////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2013 Jonathan De Wachter (dewachter.jonathan@gmail.com)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Window/EglContext.hpp>
#include <SFML/Window/WindowImpl.hpp>
#include <SFML/OpenGL.hpp>
#include <SFML/System/Err.hpp>
#include <SFML/System/Sleep.hpp>
#include <SFML/System/Mutex.hpp>
#include <SFML/System/Lock.hpp>
#ifdef SFML_SYSTEM_ANDROID
    #include <SFML/System/Android/Activity.hpp>
#endif
#ifdef SFML_SYSTEM_LINUX
    #include <X11/Xlib.h>
#endif

namespace
{
    EGLDisplay getInitializedDisplay()
    {
#if defined(SFML_SYSTEM_LINUX)

        static EGLDisplay display = EGL_NO_DISPLAY;
        
        if (display == EGL_NO_DISPLAY)
        {
#ifdef SFML_BCMHOST
            bcm_host_init();
#endif

            display = eglCheck(eglGetDisplay(EGL_DEFAULT_DISPLAY));
            eglCheck(eglInitialize(display, NULL, NULL));
        }
        
        return display;
    
#elif defined(SFML_SYSTEM_ANDROID)

    // On Android, its native activity handles this for us
    sf::priv::ActivityStates* states = sf::priv::getActivity(NULL);
    sf::Lock lock(states->mutex);

    return states->display;
    
#endif
    }
}


namespace sf
{
namespace priv
{
////////////////////////////////////////////////////////////
EglContext::EglContext(EglContext* shared) :
m_display (EGL_NO_DISPLAY),
m_context (EGL_NO_CONTEXT),
m_surface (EGL_NO_SURFACE),
m_config  (NULL)
{
    // Get the initialized EGL display
    m_display = getInitializedDisplay();

    // Get the best EGL config matching the default video settings
    m_config = getBestConfig(m_display, VideoMode::getDesktopMode().bitsPerPixel, ContextSettings());
    
    // Note: The EGL specs say that attrib_list can be NULL when passed to eglCreatePbufferSurface,
    // but this is resulting in a segfault. Bug in Android?
    EGLint attrib_list[] = { 
        EGL_WIDTH, 1,
        EGL_HEIGHT,1,
        EGL_NONE
    };

    m_surface = eglCheck(eglCreatePbufferSurface(m_display, m_config, attrib_list));

    // Create EGL context
    createContext(shared);
}


////////////////////////////////////////////////////////////
EglContext::EglContext(EglContext* shared, const ContextSettings& settings, const WindowImpl* owner, unsigned int bitsPerPixel) :
m_display (EGL_NO_DISPLAY),
m_context (EGL_NO_CONTEXT),
m_surface (EGL_NO_SURFACE),
m_config  (NULL)
{
#ifdef SFML_SYSTEM_ANDROID

    // On Android, we must save the created context
    ActivityStates* states = getActivity(NULL);
    Lock lock(states->mutex);

    states->context = this;

#endif

    // Get the initialized EGL display
    m_display = getInitializedDisplay();
    
    // Get the best EGL config matching the requested video settings
    m_config = getBestConfig(m_display, bitsPerPixel, settings);
    
    // Create EGL context
    createContext(shared);
    

#ifdef SFML_BCMHOST
    static EGL_DISPMANX_WINDOW_T nativewindow;

    DISPMANX_ELEMENT_HANDLE_T dispman_element;
    DISPMANX_DISPLAY_HANDLE_T dispman_display;
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    VC_RECT_T dst_rect;
    VC_RECT_T src_rect;
    uint32_t screen_width;
    uint32_t screen_height;

    VC_DISPMANX_ALPHA_T dispman_alpha;

    // Disable alpha to prevent app looking composed on whatever dispman
    // is showing (X11) - lifted from SDL source: video/raspberry/SDL_rpivideo.c
    //
    dispman_alpha.flags = DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS;
    dispman_alpha.opacity = 0xFF;
    dispman_alpha.mask = 0;

    // create an EGL window surface
    graphics_get_display_size(0 /* LCD */, &screen_width, &screen_height);

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = screen_width;
    dst_rect.height = screen_height;
      
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = screen_width << 16;
    src_rect.height = screen_height << 16;  

    dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
    dispman_update = vc_dispmanx_update_start( 0 );

    dispman_element = vc_dispmanx_element_add( dispman_update, dispman_display,
       0/*layer*/, &dst_rect, 0/*src*/,
       &src_rect, DISPMANX_PROTECTION_NONE, &dispman_alpha, 0/*clamp*/, DISPMANX_NO_ROTATE );
      
    nativewindow.element = dispman_element;
    nativewindow.width = screen_width;
    nativewindow.height = screen_height;
    vc_dispmanx_update_submit_sync( dispman_update );

    createSurface((EGLNativeWindowType)&nativewindow);
#elif !defined(SFML_SYSTEM_ANDROID)

    // Create EGL surface (except on Android because the window is created 
    // asynchronously, its activity manager will call it for us)
    createSurface((EGLNativeWindowType)owner->getSystemHandle());
#endif
}


////////////////////////////////////////////////////////////
EglContext::EglContext(EglContext* shared, const ContextSettings& settings, unsigned int width, unsigned int height) :
m_display (EGL_NO_DISPLAY),
m_context (EGL_NO_CONTEXT),
m_surface (EGL_NO_SURFACE),
m_config  (NULL)
{
}


////////////////////////////////////////////////////////////
EglContext::~EglContext()
{
    // Deactivate the current context
    EGLContext currentContext = eglCheck(eglGetCurrentContext());

    if (currentContext == m_context)
    {
        eglCheck(eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    }

    // Destroy context
    if (m_context != EGL_NO_CONTEXT)
    {
        eglCheck(eglDestroyContext(m_display, m_context));
    }

    // Destroy surface
    if (m_surface != EGL_NO_SURFACE)
    {
        eglCheck(eglDestroySurface(m_display, m_surface));
    }
}


////////////////////////////////////////////////////////////
bool EglContext::makeCurrent()
{
    return m_surface != EGL_NO_SURFACE && eglCheck(eglMakeCurrent(m_display, m_surface, m_surface, m_context));
}


////////////////////////////////////////////////////////////
void EglContext::display()
{
    if (m_surface != EGL_NO_SURFACE)
        eglCheck(eglSwapBuffers(m_display, m_surface));
}


////////////////////////////////////////////////////////////
void EglContext::setVerticalSyncEnabled(bool enabled)
{
    eglCheck(eglSwapInterval(m_display, enabled ? 1 : 0));
}


////////////////////////////////////////////////////////////
void EglContext::createContext(EglContext* shared)
{
    const EGLint contextVersion[] = {
        EGL_CONTEXT_CLIENT_VERSION, 1,
        EGL_NONE
    };
    
    EGLContext toShared;

    if (shared)
        toShared = shared->m_context;
    else
        toShared = EGL_NO_CONTEXT;

    // Create EGL context
    m_context = eglCheck(eglCreateContext(m_display, m_config, toShared, contextVersion));
}


////////////////////////////////////////////////////////////
void EglContext::createSurface(EGLNativeWindowType window)
{
    m_surface = eglCheck(eglCreateWindowSurface(m_display, m_config, window, NULL));
}


////////////////////////////////////////////////////////////
void EglContext::destroySurface()
{
    eglCheck(eglDestroySurface(m_display, m_surface));
    m_surface = EGL_NO_SURFACE;

    // Ensure that this context is no longer active since our surface is now destroyed
    setActive(false);
}


////////////////////////////////////////////////////////////
EGLConfig EglContext::getBestConfig(EGLDisplay display, unsigned int bitsPerPixel, const ContextSettings& settings)
{
    // Set our video settings constraint
    const EGLint attributes[] = {
        EGL_BUFFER_SIZE, bitsPerPixel,
        EGL_DEPTH_SIZE, settings.depthBits,
        EGL_STENCIL_SIZE, settings.stencilBits,
        EGL_SAMPLE_BUFFERS, settings.antialiasingLevel,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
        EGL_NONE
    };
    
    EGLint configCount;
    EGLConfig configs[1];
    
    // Ask EGL for the best config matching our video settings
    eglCheck(eglChooseConfig(display, attributes, configs, 1, &configCount));
    
    return configs[0];
}


#ifdef SFML_SYSTEM_LINUX
////////////////////////////////////////////////////////////
XVisualInfo EglContext::selectBestVisual(::Display* XDisplay, unsigned int bitsPerPixel, const ContextSettings& settings)
{
    // Get the initialized EGL display
    EGLDisplay display = getInitializedDisplay();
    
    // Get the best EGL config matching the default video settings
    EGLConfig config = getBestConfig(display, bitsPerPixel, settings);
    
    // Retrieve the visual id associated with this EGL config
    EGLint nativeVisualId;
    
    eglCheck(eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &nativeVisualId));
    
    if (nativeVisualId == 0)
    {
        // Should never happen...
        err() << "No EGL visual found. You should check your graphics driver" << std::endl;
        
        return XVisualInfo();
    }
    
    XVisualInfo vTemplate;
    vTemplate.visualid = static_cast<VisualID>(nativeVisualId);

    // Get X11 visuals compatible with this EGL config
    XVisualInfo *availableVisuals, bestVisual;
    int visualCount = 0;
    
    availableVisuals = XGetVisualInfo(XDisplay, VisualIDMask, &vTemplate, &visualCount);
    
    if (visualCount == 0)
    {
        // Can't happen...
        err() << "No X11 visual found. Bug in your EGL implementation ?" << std::endl;
        
        return XVisualInfo();
    }
    
    // Pick up the best one
    bestVisual = availableVisuals[0];
    XFree(availableVisuals);
    
    return bestVisual;
}
#endif

} // namespace priv

} // namespace sf
