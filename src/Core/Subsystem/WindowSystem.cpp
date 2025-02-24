/**
 * @file
 * @author 9chu
 * @date 2022/2/15
 * 此文件为 LuaSTGPlus 项目的一部分，版权与许可声明详见 COPYRIGHT.txt。
 */
#include <lstg/Core/Subsystem/WindowSystem.hpp>

#include <SDL_syswm.h>

// fvck Xlib.h
#ifdef Status
#undef Status
#endif

#include <lstg/Core/Logging.hpp>
#include <lstg/Core/Subsystem/SubsystemContainer.hpp>
#include <lstg/Core/AppBase.hpp>  // for Cmdline
#include "../detail/SDLHelper.hpp"

#ifdef LSTG_PLATFORM_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

using namespace std;
using namespace lstg;
using namespace lstg::Subsystem;

LSTG_DEF_LOG_CATEGORY(WindowSystem);

namespace
{
    /**
     * 获取平台窗体特性
     */
    WindowFeatures GetWindowFeatures()
    {
        auto ret = static_cast<WindowFeatures>(0);

#if defined(SDL_VIDEO_DRIVER_COCOA) || defined(SDL_VIDEO_DRIVER_DIRECTFB) || defined(SDL_VIDEO_DRIVER_WINDOWS) || \
    defined(SDL_VIDEO_DRIVER_WINDOWS) || defined(SDL_VIDEO_DRIVER_X11)
        // 支持窗口化的平台
        ret |= WindowFeatures::SupportWindowMode;
        ret |= WindowFeatures::ProgrammingResizable;
#elif defined(SDL_VIDEO_DRIVER_EMSCRIPTEN)
        // 支持窗口化但是不支持大小调整的平台
        ret |= WindowFeatures::SupportWindowMode;
#elif defined(SDL_VIDEO_DRIVER_PSP) || defined(SDL_VIDEO_DRIVER_UIKIT) || defined(SDL_VIDEO_DRIVER_ANDROID) || \
    defined(SDL_VIDEO_DRIVER_RPI)
        // 不支持窗口化也不支持大小调整的平台
#else
#error "Unsupported video driver yet"
#endif

        // 支持从控制台指定强制全屏
        auto cmdForceFullscreen = AppBase::GetCmdline().GetOption<bool>("force-fullscreen", false);
        if (cmdForceFullscreen)
        {
            LSTG_LOG_INFO_CAT(WindowSystem, "Force fullscreen mode is enabled");
            ret ^= WindowFeatures::SupportWindowMode;  // 去掉窗口化支持标记位
        }

        return ret;
    }
}

WindowSystem::WindowSystem(SubsystemContainer& container)
    : m_pEventBusSystem(container.Get<EventBusSystem>())
{
    static_cast<void>(container);

    // 初始化 SDL 视频子系统
    int ev = ::SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (ev < 0)
    {
        LSTG_LOG_CRITICAL_CAT(WindowSystem, "Initialize SDL video subsystem fail, SDL_GetError: {}", SDL_GetError());
        LSTG_THROW(WindowInitializeFailedException, "Initialize SDL video subsystem fail, SDL_GetError: {}", SDL_GetError());
    }

    // 获取窗体平台特性
    m_iFeatures = GetWindowFeatures();

    // 初始化窗口，如果平台支持窗口化，则创建一个 640x480 的小窗口作为初始窗口
    if (m_iFeatures & (WindowFeatures::SupportWindowMode | WindowFeatures::ProgrammingResizable))
    {
        static const int kInitialWidth = 640;
        static const int kInitialHeight = 480;

        m_pWindow = ::SDL_CreateWindow("LuaSTGPlus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kInitialWidth, kInitialHeight,
            SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI);
    }
    else
    {
#ifdef SDL_VIDEO_DRIVER_EMSCRIPTEN
        // HTML5 特殊处理，保留外部大小不变
        int canvasWidth = 0, canvasHeight = 0;
        auto ev = ::emscripten_get_canvas_element_size("#canvas", &canvasWidth, &canvasHeight);
        if (ev != EMSCRIPTEN_RESULT_SUCCESS)
        {
            LSTG_LOG_CRITICAL_CAT(WindowSystem, "Get canvas size fail, ret: {}", ev);
            LSTG_THROW(WindowInitializeFailedException, "Get canvas size fail, ret: {}", ev);
        }
        // 需要 Resizable，否则 canvas 的大小变化不会自动产生 Resize 事件
        m_pWindow = ::SDL_CreateWindow("LuaSTGPlus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, canvasWidth, canvasHeight,
            SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
#else
        // 其他情况总是创建全屏窗口
        m_pWindow = ::SDL_CreateWindow("LuaSTGPlus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0,
            SDL_WINDOW_HIDDEN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
#endif
    }

    if (m_pWindow == nullptr)
    {
        LSTG_LOG_CRITICAL_CAT(WindowSystem, "Create render window fail, SDL_GetError: {}", SDL_GetError());
        LSTG_THROW(WindowInitializeFailedException, "Create render window fail, SDL_GetError: {}", SDL_GetError());
    }

    if (::SDL_GetWindowFlags(m_pWindow) & SDL_WINDOW_ALLOW_HIGHDPI)
        m_iFeatures |= WindowFeatures::HighDPISupport;
}

WindowSystem::~WindowSystem()
{
    ::SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

const char* WindowSystem::GetTitle() const noexcept
{
    return ::SDL_GetWindowTitle(m_pWindow);
}

void WindowSystem::SetTitle(const char* title) noexcept
{
    ::SDL_SetWindowTitle(m_pWindow, title);
}

std::tuple<int, int> WindowSystem::GetSize() const noexcept
{
    int w = 0, h = 0;
    ::SDL_GetWindowSize(m_pWindow, &w, &h);
    return { w, h };
}

void WindowSystem::SetSize(int width, int height) noexcept
{
    int orgW = 0, orgH = 0;
    ::SDL_GetWindowSize(m_pWindow, &orgW, &orgH);

    ::SDL_SetWindowSize(m_pWindow, width, height);
    ::SDL_SetWindowPosition(m_pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    int newW = 0, newH = 0;
    ::SDL_GetWindowSize(m_pWindow, &newW, &newH);

    // 主动发送一个窗口大小变化事件
    if (orgW != newW || orgH != newH)
    {
        SDL_Event resizeEvent;
        ::memset(&resizeEvent, 0, sizeof(resizeEvent));
        resizeEvent.window.type = SDL_WINDOWEVENT;
        resizeEvent.window.timestamp = ::SDL_GetTicks();
        resizeEvent.window.windowID = ::SDL_GetWindowID(m_pWindow);
        resizeEvent.window.event = SDL_WINDOWEVENT_RESIZED;
        resizeEvent.window.data1 = newW;
        resizeEvent.window.data2 = newH;
        SubsystemEvent event {resizeEvent};
        m_pEventBusSystem->EmitEvent(std::move(event));
    }
}

std::tuple<int, int> WindowSystem::GetRenderSize() const noexcept
{
    int w = 0, h = 0;
    ::SDL_GL_GetDrawableSize(m_pWindow, &w, &h);
    return { w, h };
}

void WindowSystem::Raise() noexcept
{
    ::SDL_RaiseWindow(m_pWindow);
}

void WindowSystem::Show() noexcept
{
    ::SDL_ShowWindow(m_pWindow);
}

void WindowSystem::Hide() noexcept
{
    ::SDL_HideWindow(m_pWindow);
}

bool WindowSystem::IsFullScreen() const noexcept
{
    auto flag = ::SDL_GetWindowFlags(m_pWindow);
    return ((flag & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP) ||
        ((flag & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN);
}

bool WindowSystem::IsMinimized() const noexcept
{
    auto flag = ::SDL_GetWindowFlags(m_pWindow);
    return (flag & SDL_WINDOW_MINIMIZED) == SDL_WINDOW_MINIMIZED;
}

bool WindowSystem::HasFocus() const noexcept
{
    SDL_Window* focused = ::SDL_GetKeyboardFocus();
    return (focused == m_pWindow) || ((::SDL_GetWindowFlags(m_pWindow) & SDL_WINDOW_INPUT_FOCUS) == SDL_WINDOW_INPUT_FOCUS);
}

Result<void> WindowSystem::ToggleFullScreen(bool fullscreen) noexcept
{
    if (!fullscreen && !(m_iFeatures & WindowFeatures::SupportWindowMode))
        return make_error_code(errc::not_supported);
    int ev = ::SDL_SetWindowFullscreen(m_pWindow, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (ev < 0)
        return lstg::detail::MakeSDLError(ev);
    return {};
}

Result<bool> WindowSystem::IsMouseCursorVisible() noexcept
{
    int ev = ::SDL_ShowCursor(SDL_QUERY);
    if (ev < 0)
        return lstg::detail::MakeSDLError(ev);
    return ev == SDL_ENABLE;
}

Result<void> WindowSystem::SetMouseCursorVisible(bool shown) noexcept
{
    int ev = ::SDL_ShowCursor(shown ? SDL_ENABLE : SDL_DISABLE);
    if (ev < 0)
        return lstg::detail::MakeSDLError(ev);
    return {};
}
