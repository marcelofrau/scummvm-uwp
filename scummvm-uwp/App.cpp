#include "pch.h"
#include "App.h"

#include <ppltasks.h>

using namespace dosbox_uwp;

using namespace concurrency;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::System;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;

// The main function is only used to initialize our IFrameworkView class.
[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^)
{
	auto direct3DApplicationSource = ref new Direct3DApplicationSource();
	CoreApplication::Run(direct3DApplicationSource);
	return 0;
}

IFrameworkView^ Direct3DApplicationSource::CreateView()
{
	return ref new App();
}

App::App() :
	m_windowClosed(false),
	m_windowVisible(true)
{
}

// The first method called when the IFrameworkView is being created.
void App::Initialize(CoreApplicationView^ applicationView)
{
	OutputDebugStringA("[dosbox-uwp] App::Initialize\n");
	LogInit();

	applicationView->Activated +=
		ref new TypedEventHandler<CoreApplicationView^, IActivatedEventArgs^>(this, &App::OnActivated);

	CoreApplication::Suspending +=
		ref new EventHandler<SuspendingEventArgs^>(this, &App::OnSuspending);

	CoreApplication::Resuming +=
		ref new EventHandler<Platform::Object^>(this, &App::OnResuming);

	m_deviceResources = std::make_shared<DX::DeviceResources>();
	OutputDebugStringA("[dosbox-uwp] App::Initialize done\n");
}

// Called when the CoreWindow object is created (or re-created).
void App::SetWindow(CoreWindow^ window)
{
	OutputDebugStringA("[dosbox-uwp] App::SetWindow\n");

	window->SizeChanged += 
		ref new TypedEventHandler<CoreWindow^, WindowSizeChangedEventArgs^>(this, &App::OnWindowSizeChanged);

	window->VisibilityChanged +=
		ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>(this, &App::OnVisibilityChanged);

	window->Closed += 
		ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &App::OnWindowClosed);

	window->KeyDown +=
		ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &App::OnKeyDown);

	window->KeyUp +=
		ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &App::OnKeyUp);

	window->Dispatcher->AcceleratorKeyActivated +=
		ref new TypedEventHandler<CoreDispatcher^, AcceleratorKeyEventArgs^>(this, &App::OnAcceleratorKeyActivated);

#ifdef MOUSE_SUPPORT
	window->PointerMoved +=
		ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerMoved);
	window->PointerPressed +=
		ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerPressed);
	window->PointerReleased +=
		ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerReleased);
	window->PointerWheelChanged +=
		ref new TypedEventHandler<CoreWindow^, PointerEventArgs^>(this, &App::OnPointerWheelChanged);

	// Xbox + Windows: cursor hidden from start — DOS apps + PUREMENU render own cursor
	window->PointerCursor = nullptr;
	OutputDebugStringA("[dosbox-uwp] CoreCursor(nullptr)\n");
#endif

	DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();

	currentDisplayInformation->DpiChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &App::OnDpiChanged);

	currentDisplayInformation->OrientationChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &App::OnOrientationChanged);

	DisplayInformation::DisplayContentsInvalidated +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &App::OnDisplayContentsInvalidated);

	m_deviceResources->SetWindow(window);

	SystemNavigationManager^ nav = SystemNavigationManager::GetForCurrentView();
	nav->BackRequested +=
		ref new EventHandler<BackRequestedEventArgs^>(this, &App::OnBackRequested);

	OutputDebugStringA("[dosbox-uwp] App::SetWindow done\n");
}

// Initializes scene resources, or loads a previously saved app state.
void App::Load(Platform::String^ entryPoint)
{
	OutputDebugStringA("[dosbox-uwp] App::Load\n");
	if (m_main == nullptr)
	{
		m_main = std::unique_ptr<dosbox_uwpMain>(new dosbox_uwpMain(m_deviceResources));
	}
	OutputDebugStringA("[dosbox-uwp] App::Load done\n");
}

// This method is called after the window becomes active.
void App::Run()
{
	OutputDebugStringA("[dosbox-uwp] App::Run enter\n");

	while (!m_windowClosed)
	{
		if (m_windowVisible)
		{
			CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

			m_main->Update();

			if (m_main->WasFilePickerRequested()) {
				OpenFilePicker();
			}

			if (m_main->Render())
			{
				m_deviceResources->Present(m_deviceResources->GetSyncInterval(), 0);
			}

			m_main->ProcessPendingLoad();
		}
		else
		{
			CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
		}
	}

	OutputDebugStringA("[dosbox-uwp] App::Run exit\n");
}

// Required for IFrameworkView.
// Terminate events do not cause Uninitialize to be called. It will be called if your IFrameworkView
// class is torn down while the app is in the foreground.
void App::Uninitialize()
{
}

// Application lifecycle event handlers.

void App::OnActivated(CoreApplicationView^ applicationView, IActivatedEventArgs^ args)
{
	// Run() won't start until the CoreWindow is activated.
	CoreWindow::GetForCurrentThread()->Activate();
}

void App::OnSuspending(Platform::Object^ sender, SuspendingEventArgs^ args)
{
	// Save app state asynchronously after requesting a deferral. Holding a deferral
	// indicates that the application is busy performing suspending operations. Be
	// aware that a deferral may not be held indefinitely. After about five seconds,
	// the app will be forced to exit.
	SuspendingDeferral^ deferral = args->SuspendingOperation->GetDeferral();

	create_task([this, deferral]()
	{
        m_deviceResources->Trim();

		// Insert your code here.

		deferral->Complete();
	});
}

void App::OnResuming(Platform::Object^ sender, Platform::Object^ args)
{
	// Restore any data or state that was unloaded on suspend. By default, data
	// and state are persisted when resuming from suspend. Note that this event
	// does not occur if the app was previously terminated.

	// Insert your code here.
}

// Window event handlers.

void App::OnWindowSizeChanged(CoreWindow^ sender, WindowSizeChangedEventArgs^ args)
{
	m_deviceResources->SetLogicalSize(Size(sender->Bounds.Width, sender->Bounds.Height));
	m_main->CreateWindowSizeDependentResources();
}

void App::OnVisibilityChanged(CoreWindow^ sender, VisibilityChangedEventArgs^ args)
{
	m_windowVisible = args->Visible;
}

void App::OnWindowClosed(CoreWindow^ sender, CoreWindowEventArgs^ args)
{
	LogShutdown();
	m_windowClosed = true;
}

	void App::OpenFilePicker()
{
	m_main->SetLoadState(dosbox_uwpMain::LOAD_PICKING);

	auto picker = ref new Windows::Storage::Pickers::FileOpenPicker();
	picker->ViewMode = Windows::Storage::Pickers::PickerViewMode::List;
	picker->FileTypeFilter->Append(".zip");
	picker->FileTypeFilter->Append(".dosz");
	picker->FileTypeFilter->Append(".exe");
	picker->FileTypeFilter->Append(".com");
	picker->FileTypeFilter->Append(".bat");
	picker->FileTypeFilter->Append(".iso");
	picker->FileTypeFilter->Append(".chd");
	picker->FileTypeFilter->Append(".cue");
	picker->FileTypeFilter->Append(".img");
	picker->FileTypeFilter->Append(".ima");
	picker->FileTypeFilter->Append(".vhd");
	picker->FileTypeFilter->Append(".conf");

	// StorageFile from picker requires WinRT ReadBufferAsync → WriteBufferAsync (no CopyFileFromAppW).
	create_task(picker->PickSingleFileAsync()).then([this](Windows::Storage::StorageFile^ file)
	{
		if (file == nullptr)
		{
			spdlog::info("[Picker] Cancelled");
			m_main->SetLoadState(dosbox_uwpMain::LOAD_IDLE);
			return;
		}

		m_main->SetLoadState(dosbox_uwpMain::LOAD_READING);
		m_main->ActivateLoadingScreen();
		spdlog::info("[Picker] Picked: {}", std::string(file->Name->Data(), file->Name->Data() + file->Name->Length()));

		create_task(Windows::Storage::FileIO::ReadBufferAsync(file)).then([this, file](Windows::Storage::Streams::IBuffer^ buffer)
		{
			if (buffer == nullptr || buffer->Length == 0)
			{
				spdlog::error("[Picker] Read failed or empty");
				m_main->SetLoadState(dosbox_uwpMain::LOAD_FAILED);
				return;
			}

			m_main->SetLoadState(dosbox_uwpMain::LOAD_BOOTING);

			auto localFolder = Windows::Storage::ApplicationData::Current->LocalFolder;

			create_task(localFolder->CreateFolderAsync(
				L"temp", Windows::Storage::CreationCollisionOption::OpenIfExists))
			.then([this, file, buffer](Windows::Storage::StorageFolder^ tempFolder)
			{
				create_task(tempFolder->CreateFileAsync(
					file->Name, Windows::Storage::CreationCollisionOption::ReplaceExisting))
				.then([this, buffer](Windows::Storage::StorageFile^ tempFile)
				{
					create_task(Windows::Storage::FileIO::WriteBufferAsync(tempFile, buffer))
					.then([this, tempFile]()
					{
						std::wstring localPath = tempFile->Path->Data();
						spdlog::info("[Picker] Copied to: '{}'",
							std::string(localPath.begin(), localPath.end()));
						m_main->QueueLoadRom(localPath, {});
					});
				});
			});
		});
	});
}

void App::OnBackRequested(Platform::Object^ sender, Windows::UI::Core::BackRequestedEventArgs^ args)
{
	args->Handled = true;
	if (m_main)
	{
		auto& menu = m_main->GetMenu();
		if (menu.m_fileBrowser.IsVisible())
		{
			menu.m_fileBrowser.OnBack();
		}
		// FrontendMenu is bootstrap — no toggle
	}
}

void App::OnKeyDown(CoreWindow^ sender, KeyEventArgs^ args)
{
	auto key = args->VirtualKey;

	if (key == VirtualKey::F10)
	{
		// F10 is visual-only on BIOS screen (FrontendMenu shows "F10 = Menu")
		// When game loaded, pass to core for PUREMENU
		args->Handled = true;
		if (m_main && m_main->IsLoaded())
		{
			m_main->OnKeyEvent(VirtualKey::F10, true, 0, false);
			m_main->OnKeyEvent(VirtualKey::F10, false, 0, false);
		}
		return;
	}

	if (key == VirtualKey::F12)
		args->Handled = true;

	// Ctrl+L = force system FileOpenPicker fallback
	if (key == VirtualKey::L && m_ctrlHeld)
	{
		spdlog::info("[App] Ctrl+L -> system FileOpenPicker fallback");
		args->Handled = true;
		OpenFilePicker();
		return;
	}

	// Track Ctrl state for Ctrl+L
	if (key == VirtualKey::Control) m_ctrlHeld = true;

	m_main->OnKeyEvent(key, true, (uint32_t)args->KeyStatus.ScanCode, args->KeyStatus.IsExtendedKey);
}

void App::OnKeyUp(CoreWindow^ sender, KeyEventArgs^ args)
{
	auto key = args->VirtualKey;
	if (key == VirtualKey::Control) m_ctrlHeld = false;
	m_main->OnKeyEvent(key, false, (uint32_t)args->KeyStatus.ScanCode, args->KeyStatus.IsExtendedKey);
}

void App::OnAcceleratorKeyActivated(CoreDispatcher^ sender, AcceleratorKeyEventArgs^ args)
{
    // Alt (Menu) goes through accelerator path in UWP, never reaches OnKeyDown
    if (args->VirtualKey == VirtualKey::Menu)
    {
        args->Handled = true;
        bool down = (args->EventType == CoreAcceleratorKeyEventType::SystemKeyDown);
        m_main->OnKeyEvent(VirtualKey::Menu, down,
            (uint32_t)args->KeyStatus.ScanCode, args->KeyStatus.IsExtendedKey);
        return;
    }

	if (args->EventType == CoreAcceleratorKeyEventType::SystemKeyDown &&
		args->VirtualKey == VirtualKey::F10)
	{
		args->Handled = true;
		if (m_main && m_main->IsLoaded())
		{
			m_main->OnKeyEvent(VirtualKey::F10, true, 0, false);
			m_main->OnKeyEvent(VirtualKey::F10, false, 0, false);
		}
		return;
	}
}

#ifdef MOUSE_SUPPORT
void App::OnPointerMoved(CoreWindow^ sender, PointerEventArgs^ args)
{
	auto pt = args->CurrentPoint->Position;
	float normX = pt.X / sender->Bounds.Width;
	float normY = pt.Y / sender->Bounds.Height;
	if (normX < 0) normX = 0;
	if (normX > 1) normX = 1;
	if (normY < 0) normY = 0;
	if (normY > 1) normY = 1;
	if (m_main)
	{
		m_main->SetMousePointerId(args->CurrentPoint->PointerId);
		m_main->OnPointerMove(normX, normY, pt.X, pt.Y);
	}
}

void App::OnPointerPressed(CoreWindow^ sender, PointerEventArgs^ args)
{
	auto pt = args->CurrentPoint->Position;
	auto props = args->CurrentPoint->Properties;
	float normX = pt.X / sender->Bounds.Width;
	float normY = pt.Y / sender->Bounds.Height;
	if (normX < 0) normX = 0;
	if (normX > 1) normX = 1;
	if (normY < 0) normY = 0;
	if (normY > 1) normY = 1;

	if (m_main)
	{
		m_main->SetMousePointerId(args->CurrentPoint->PointerId);
		if (props->IsLeftButtonPressed)  m_main->OnPointerDown(normX, normY, 1);
		if (props->IsRightButtonPressed) m_main->OnPointerDown(normX, normY, 2);
		if (props->IsMiddleButtonPressed) m_main->OnPointerDown(normX, normY, 3);
	}

	// Hide native cursor — DOS apps + PUREMENU render their own
	sender->PointerCursor = nullptr;
	OutputDebugStringA("[dosbox-uwp] PtrPress — cursor hidden\n");
}

void App::OnPointerReleased(CoreWindow^ sender, PointerEventArgs^ args)
{
	unsigned btn = 0;
	auto props = args->CurrentPoint->Properties;
	switch (props->PointerUpdateKind)
	{
	case PointerUpdateKind::LeftButtonReleased:  btn = 1; break;
	case PointerUpdateKind::RightButtonReleased: btn = 2; break;
	case PointerUpdateKind::MiddleButtonReleased: btn = 3; break;
	}

	if (m_main)
	{
		m_main->SetMousePointerId(args->CurrentPoint->PointerId);
		m_main->OnPointerUp(btn);
		if (!props->IsLeftButtonPressed && !props->IsRightButtonPressed && !props->IsMiddleButtonPressed)
			m_main->OnPointerRelease();
	}
}

void App::OnPointerWheelChanged(CoreWindow^ sender, PointerEventArgs^ args)
{
	int delta = args->CurrentPoint->Properties->MouseWheelDelta;
	if (m_main) m_main->OnPointerWheel(delta);
}
#endif

// DisplayInformation event handlers.

void App::OnDpiChanged(DisplayInformation^ sender, Object^ args)
{
	// Note: The value for LogicalDpi retrieved here may not match the effective DPI of the app
	// if it is being scaled for high resolution devices. Once the DPI is set on DeviceResources,
	// you should always retrieve it using the GetDpi method.
	// See DeviceResources.cpp for more details.
	m_deviceResources->SetDpi(sender->LogicalDpi);
	m_main->CreateWindowSizeDependentResources();
}

void App::OnOrientationChanged(DisplayInformation^ sender, Object^ args)
{
	m_deviceResources->SetCurrentOrientation(sender->CurrentOrientation);
	m_main->CreateWindowSizeDependentResources();
}

void App::OnDisplayContentsInvalidated(DisplayInformation^ sender, Object^ args)
{
	m_deviceResources->ValidateDevice();
}