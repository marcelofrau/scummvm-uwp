#pragma once

#include "pch.h"
#include "ScummVMMain.h"

// Rewrites the ExitProcess import slot of every loaded module so exit() lands
// in an instrumented hook. Idempotent — call again after LoadLibrary.
void PatchExitProcessImports();

// App class that owns the CoreWindow and drives ScummVMMain.
ref class App sealed : public Windows::ApplicationModel::Core::IFrameworkView
{
public:
    App();

    // IFrameworkView methods.
    virtual void Initialize(Windows::ApplicationModel::Core::CoreApplicationView^ applicationView);
    virtual void SetWindow(Windows::UI::Core::CoreWindow^ window);
    virtual void Load(Platform::String^ entryPoint);
    virtual void Run();
    virtual void Uninitialize();

protected:
    // Application lifecycle event handlers.
    void OnActivated(Windows::ApplicationModel::Core::CoreApplicationView^ applicationView, Windows::ApplicationModel::Activation::IActivatedEventArgs^ args);
    void OnSuspending(Platform::Object^ sender, Windows::ApplicationModel::SuspendingEventArgs^ args);
    void OnResuming(Platform::Object^ sender, Platform::Object^ args);
    void OnWindowSizeChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::WindowSizeChangedEventArgs^ args);
    void OnVisibilityChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::VisibilityChangedEventArgs^ args);
    void OnWindowClosed(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::CoreWindowEventArgs^ args);
    void OnWindowActivated(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::WindowActivatedEventArgs^ args);

    // Input event handlers.
    void OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);
    void OnKeyUp(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);

private:
    std::shared_ptr<DX::DeviceResources> m_deviceResources;
    std::unique_ptr<scummvm_uwp::ScummVMMain> m_main;
    bool m_windowClosed;
    bool m_windowVisible;
    bool m_emulationPaused;
    LARGE_INTEGER m_perfFrequency;
    LARGE_INTEGER m_lastFrameTime;
    Windows::ApplicationModel::ExtendedExecution::ExtendedExecutionSession^ m_extSession;
    Windows::System::Display::DisplayRequest^ m_displayRequest;
};
