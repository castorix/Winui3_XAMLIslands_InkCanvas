
// Références :
// https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/xaml-islands/host-standard-control-with-xaml-islands-cpp
// https://github.com/microsoft/Windows-universal-samples/tree/main/Samples/SimpleInk

#define NOMINMAX
#include <windows.h>
#include <tchar.h>

// [C++ Language Standard] : ISO C++17 Standard(std : c++17)
// or C++20 for co_await
// [C/C++] [Language] Conformance mode : No

#include <unknwn.h>
#include <winrt/base.h>

#include <inspectable.h>

#include <winrt/windows.ui.xaml.hosting.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/windows.ui.xaml.controls.h>
#include <winrt/Windows.ui.xaml.media.h>

#include <winrt/Windows.UI.Input.Inking.h>

#include <winrt/Windows.System.h> // DispatcherQueue
#include <winrt/Windows.UI.Popups.h>

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h> // Click events

#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>

#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Input.h>

#include <shobjidl.h>

#include <unordered_set>

using namespace winrt;
using namespace winrt::Windows::UI::Xaml::Hosting;

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

HINSTANCE hInst;
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
int nWidth = 600, nHeight = 400;

#define WM_GET_DRAWING_ATTRIBUTES (WM_USER + 100)
#define WM_SET_DRAWING_ATTRIBUTES (WM_USER + 101)
#define WM_SET_HANDLE  (WM_USER + 102)

#define COPYDATA_GET_DRAWINGATTRUBUTES 1
#define COPYDATA_SET_DRAWINGATTRUBUTES 2
#define COPYDATA_GET_RECOGNIZEDTEXT 3

#pragma pack(push, 1)
struct DrawingAttributesInfo
{
	uint8_t R, G, B, A;
	float Width;
	float Height;
	winrt::Windows::UI::Input::Inking::PenTipShape PenTipShape;
	bool IsHighlighter;
	bool IsPencil;
	float PencilOpacity;
	bool FitToCurve;
	bool IgnorePressure;
	winrt::Windows::Foundation::Numerics::float3x2 PenTipTransform;
};
#pragma pack(pop)

void SendDrawingAttributes(HWND hWndTarget, winrt::Windows::UI::Input::Inking::InkPresenter const& presenter);
void ApplyDrawingAttributesToToolbar(const DrawingAttributesInfo& info, winrt::Windows::UI::Xaml::Controls::InkToolbarPenButton const& penButton);
winrt::hstring GetRecognizerLabel(winrt::hstring const& name);
void RecognizeInkAndSendText(winrt::Windows::UI::Input::Inking::InkPresenter const& presenter,
	HWND hWndTarget, winrt::Windows::UI::Input::Inking::InkRecognizerContainer const& recognizerContainer);

winrt::Windows::UI::Input::Inking::InkUnprocessedInput::PointerPressed_revoker m_pointerPressedToken;
winrt::Windows::UI::Input::Inking::InkUnprocessedInput::PointerMoved_revoker m_pointerMovedToken;
winrt::Windows::UI::Input::Inking::InkUnprocessedInput::PointerReleased_revoker m_pointerReleasedToken;

winrt::Windows::UI::Xaml::Shapes::Polyline lasso = nullptr;
bool isBoundRect = false;
winrt::Windows::Foundation::Rect boundingRect{};
void ClearDrawnBoundingRect();
void ClearSelection();
void DrawBoundingRect();
void DrawStrokeBoundingRect(winrt::Windows::Foundation::Rect const& rect);
void AddAccelerators(winrt::Windows::UI::Xaml::UIElement const& control);

// Crashes if Win32 exe is in same directory as WinUI 3 exe because of files like resources.pri or App.xbf
DesktopWindowXamlSource desktopSource;

winrt::Windows::UI::Xaml::Controls::InkToolbar m_InkToolbar{};
winrt::Windows::UI::Xaml::Controls::InkCanvas m_InkCanvas{};
winrt::Windows::UI::Xaml::Controls::Canvas m_SelectionCanvas{};
winrt::Windows::UI::Xaml::Controls::ScrollViewer m_ScrollViewer{};
winrt::Windows::UI::Xaml::Controls::Grid m_OutputGrid{};
winrt::Windows::UI::Xaml::Controls::InkToolbarToolButton m_LastRealTool{ nullptr };
winrt::Windows::UI::Xaml::Controls::Canvas m_BoundingBox{ nullptr };

float m_Height = 0;
winrt::Windows::Foundation::Numerics::float3x2 m_Matrix = Windows::Foundation::Numerics::float3x2::identity();
winrt::Windows::UI::Input::Inking::PenTipShape m_PenTipShape = winrt::Windows::UI::Input::Inking::PenTipShape::Circle;
float m_Opacity = 1.0f;

HWND m_hWndTarget = NULL;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	hInst = hInstance;
	WNDCLASSEX wcex =
	{
		sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInst, LoadIcon(NULL, IDI_APPLICATION),
		LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), NULL, TEXT("XAMLIslandsInkCanvas"), NULL,
	};
	if (!RegisterClassEx(&wcex))
		return MessageBox(NULL, TEXT("Cannot register class !"), TEXT("Error"), MB_ICONERROR | MB_OK);
	int nX = (GetSystemMetrics(SM_CXSCREEN) - nWidth) / 2, nY = (GetSystemMetrics(SM_CYSCREEN) - nHeight) / 2;
	//HWND hWnd = CreateWindowEx(WS_EX_NOACTIVATE, wcex.lpszClassName, TEXT("XAML Islands InkCanvas"), WS_OVERLAPPEDWINDOW, nX, nY, nWidth, nHeight, NULL, NULL, hInst, NULL);
	HWND hWnd = CreateWindowEx(WS_EX_NOACTIVATE, wcex.lpszClassName, TEXT("XAML Islands InkCanvas"), WS_POPUP, nX, nY, nWidth, nHeight, NULL, NULL, hInst, NULL);

	if (!hWnd)
		return MessageBox(NULL, TEXT("Cannot create window !"), TEXT("Error"), MB_ICONERROR | MB_OK);

	winrt::init_apartment(apartment_type::single_threaded);
	// https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.hosting.windowsxamlmanager.initializeforcurrentthread?view=winrt-26100
	// If you create a DesktopWindowXamlSource object before you create the Windows.UI.Xaml.UIElement objects that will be hosted in it,
	// you don’t need to call this method.In this scenario, the UWP XAML framework will be initialized for you when you instantiate the DesktopWindowXamlSource object.
	//WindowsXamlManager winxamlmanager = WindowsXamlManager::InitializeForCurrentThread();
	auto interop = desktopSource.as<IDesktopWindowXamlSourceNative>();
	check_hresult(interop->AttachToWindow(hWnd));
	HWND hWndXamlIsland = nullptr;
	interop->get_WindowHandle(&hWndXamlIsland);
	SetWindowPos(hWndXamlIsland, 0, 0, 0, 800, 600, SWP_SHOWWINDOW);

	// XAML
	
		using namespace winrt::Windows::UI::Xaml;
		using namespace winrt::Windows::UI::Xaml::Controls;
		using namespace winrt::Windows::UI::Xaml::Media;
		using namespace winrt::Windows::UI::Core;

		// Main Grid container
		Grid xamlGrid{};
		xamlGrid.Background(SolidColorBrush{ Windows::UI::Colors::Black() });

		// Define rows: Row 0 = Auto, Row 1 = Star
		RowDefinition rowToolbar{};
		rowToolbar.Height(GridLength{ 0, GridUnitType::Auto });
		xamlGrid.RowDefinitions().Append(rowToolbar);

		RowDefinition rowCanvas{};
		rowCanvas.Height(GridLength{ 1, GridUnitType::Star });
		xamlGrid.RowDefinitions().Append(rowCanvas);
		
		// InkCanvas		
		m_InkCanvas.InkPresenter().InputDeviceTypes(
			static_cast<CoreInputDeviceTypes>(
				static_cast<uint32_t>(CoreInputDeviceTypes::Mouse) |
				static_cast<uint32_t>(CoreInputDeviceTypes::Touch) |
				static_cast<uint32_t>(CoreInputDeviceTypes::Pen)
				)
		);

		// Does not work : XAMLIslands window still owned by this process after SetParent
		//SetProp(hWnd, L"InkPresenter", reinterpret_cast<HANDLE>(winrt::get_abi(presenter)));
		//auto raw = reinterpret_cast<IInspectable*>(winrt::get_abi(presenter));
		//raw->AddRef(); 
		/*com_ptr<IInspectable> ptr{ presenter.as<IInspectable>() };
		ptr.get()->AddRef();
		SetProp(hWndXamlIsland, L"InkPresenter", reinterpret_cast<HANDLE>(ptr.get()));*/
		
	/*	Border canvasBorder;
		canvasBorder.Background(SolidColorBrush{ Windows::UI::Colors::White() });
		canvasBorder.Child(inkCanvas);*/

		// Assign target for InkToolbar		
		m_InkToolbar.TargetInkCanvas(m_InkCanvas);

		// Add InkToolbar to row 0
		Grid::SetRow(m_InkToolbar, 0);
		xamlGrid.Children().Append(m_InkToolbar);
		
		m_InkToolbar.InkDrawingAttributesChanged([](InkToolbar const& sender, auto const& args)
			{
				auto newAttributes = sender.InkDrawingAttributes();
			/*	Beep(3000, 10);
				Windows::UI::Color color = newAttributes.Color();
				OutputDebugString((L"New color: R=" + std::to_wstring(color.R) +
						L" G=" + std::to_wstring(color.G) +
						L" B=" + std::to_wstring(color.B) + L"\n").c_str());*/
				
				auto toolButton = sender.ActiveTool();
				if (!toolButton.try_as<InkToolbarHighlighterButton>())
				{
					if (newAttributes.Kind() != Windows::UI::Input::Inking::InkDrawingAttributesKind::Pencil)
					{
						if (m_Height != 0)
						{
							//Beep(3000, 10);
							newAttributes.Size({ newAttributes.Size().Width, m_Height });
						}
						if (m_Matrix != Windows::Foundation::Numerics::float3x2::identity())
						{
							newAttributes.PenTipTransform(m_Matrix);
						}
						newAttributes.PenTip(m_PenTipShape);
						m_InkCanvas.InkPresenter().UpdateDefaultDrawingAttributes(newAttributes);
					}
					else
					{
						newAttributes.PencilProperties().Opacity(m_Opacity);
						m_InkCanvas.InkPresenter().UpdateDefaultDrawingAttributes(newAttributes);
					}
				}
			}
		);

		// Create custom button to delete all strikes
		InkToolbarCustomToolButton m_ButtonDeleteAll;
		ToolTipService::SetToolTip(m_ButtonDeleteAll, box_value(L"Delete all Ink strokes"));	
		m_ButtonDeleteAll.Content(box_value(L"\U0001F5D1"));
		m_ButtonDeleteAll.FontFamily(Media::FontFamily(L"Segoe UI Emoji"));
		m_ButtonDeleteAll.FontSize(24);
		m_InkToolbar.Children().Append(m_ButtonDeleteAll);

		// Create custom button to change background image
		InkToolbarCustomToolButton m_ButtonBackgroundImage;
		ToolTipService::SetToolTip(m_ButtonBackgroundImage, box_value(L"Change background image"));	
		m_ButtonBackgroundImage.Content(box_value(L"\U0001F5BC"));
		m_ButtonBackgroundImage.FontFamily(Media::FontFamily(L"Segoe UI Emoji"));
		m_ButtonBackgroundImage.FontSize(24);
		m_InkToolbar.Children().Append(m_ButtonBackgroundImage);;

		// Create custom button for Recognizer
		InkToolbarCustomToolButton m_ButtonRecognizer;
		ToolTipService::SetToolTip(m_ButtonRecognizer, box_value(L"Recognize text"));
		m_ButtonRecognizer.Content(box_value(L"\U0001F524"));
		m_ButtonRecognizer.FontFamily(Media::FontFamily(L"Segoe UI Emoji"));
		m_ButtonRecognizer.FontSize(24);
		m_InkToolbar.Children().Append(m_ButtonRecognizer);

		// Create InkRecognizerContainer and get recognizers
		winrt::Windows::UI::Input::Inking::InkRecognizerContainer m_RecognizerContainer{};
		auto recognizers = m_RecognizerContainer.GetRecognizers();
		if (!recognizers.Size())
		{
			OutputDebugString(L"No recognizers available.\n");
		}

		// Prepare selected recognizer
		winrt::Windows::UI::Input::Inking::IInkRecognizer m_SelectedRecognizer{ nullptr };

		m_ButtonDeleteAll.Click([&](auto const&, auto const&)
			{
				m_InkCanvas.InkPresenter().StrokeContainer().Clear();
				m_SelectionCanvas.Children().Clear();
				m_BoundingBox = nullptr;
				if (m_LastRealTool)
				{
					m_InkToolbar.ActiveTool(m_LastRealTool);
				}
			});

		m_ButtonRecognizer.Click([&](auto const&, auto const&)
			{
				RecognizeInkAndSendText(m_InkCanvas.InkPresenter(), m_hWndTarget, m_RecognizerContainer);
				if (m_LastRealTool)
				{
					m_InkToolbar.ActiveTool(m_LastRealTool);
				}
			});

		// Use first recognizer as default
		m_SelectedRecognizer = recognizers.GetAt(0);

		// Create custom button to select a recognizer
		InkToolbarCustomToolButton m_ButtonSelectRecognizer{};	
		m_ButtonSelectRecognizer.Content(winrt::box_value(GetRecognizerLabel(m_SelectedRecognizer.Name())));
		ToolTipService::SetToolTip(m_ButtonSelectRecognizer, box_value(L"Select Ink Recognizer"));
		m_InkToolbar.Children().Append(m_ButtonSelectRecognizer);
	
		// Create flyout
		MenuFlyout menuFlyout{};

		for (auto const& recognizer : recognizers)
		{
			MenuFlyoutItem item{};
			item.Text(recognizer.Name());

			item.Click([&m_RecognizerContainer, recognizer, &m_ButtonSelectRecognizer, &m_SelectedRecognizer](auto const&, auto const&)
				{
					m_RecognizerContainer.SetDefaultRecognizer(recognizer);
					m_SelectedRecognizer = recognizer;
					m_ButtonSelectRecognizer.Content(winrt::box_value(GetRecognizerLabel(m_SelectedRecognizer.Name())));
					OutputDebugString((L"Recognizer set to: " + recognizer.Name() + L"\n").c_str());
				});
			menuFlyout.Items().Append(item);
		}

		menuFlyout.Closed([&](auto const&, auto const&)
			{
				if (m_LastRealTool)
				{
					m_InkToolbar.ActiveTool(m_LastRealTool);
				}
			});

		// Show flyout on click
		m_ButtonSelectRecognizer.Click([menuFlyout](auto const& sender, auto const&)
			{
				auto button = sender.as<FrameworkElement>();
				menuFlyout.ShowAt(button);
			});

		// Create the lasso button
		InkToolbarCustomToolButton m_ButtonLasso;		
		ToolTipService::SetToolTip(m_ButtonLasso, box_value(L"Select ink strokes\nCtrl + Mouse to select several strokes\nStandard keyboard shortcuts to copy/cut/paste"));
		m_ButtonLasso.Content(box_value(L"\U00002702"));
		m_ButtonLasso.FontFamily(Media::FontFamily(L"Segoe UI Emoji"));
		m_ButtonLasso.FontSize(24);
		m_InkToolbar.Children().Append(m_ButtonLasso);

		m_ButtonLasso.Click([hWndMain = hWndXamlIsland, scrollViewer = m_ScrollViewer](auto const&, auto const&)
			{
				m_InkCanvas.InkPresenter().InputProcessingConfiguration().RightDragAction(winrt::Windows::UI::Input::Inking::InkInputRightDragAction::LeaveUnprocessed);

				winrt::Windows::System::DispatcherQueue dispatcherQueue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();

				m_pointerPressedToken = m_InkCanvas.InkPresenter().UnprocessedInput().PointerPressed(
					winrt::auto_revoke,
					[hWndMain, scrollViewer](auto const& sender, winrt::Windows::UI::Core::PointerEventArgs const& args)
					{
						lasso = Windows::UI::Xaml::Shapes::Polyline();
						lasso.Stroke(SolidColorBrush(winrt::Windows::UI::Colors::Blue()));
						lasso.StrokeThickness(1);
						winrt::Windows::UI::Xaml::Media::DoubleCollection dashArray;
						dashArray.Append(5.0);
						dashArray.Append(2.0);
						lasso.StrokeDashArray(dashArray);
						lasso.Points().Append(args.CurrentPoint().RawPosition());
						m_SelectionCanvas.Children().Append(lasso);
						isBoundRect = true;

						bool bRet = scrollViewer.Focus(winrt::Windows::UI::Xaml::FocusState::Pointer);
						SetFocus(hWndMain);
					});

				m_pointerMovedToken = m_InkCanvas.InkPresenter().UnprocessedInput().PointerMoved(
					winrt::auto_revoke,
					[&](auto const& sender, winrt::Windows::UI::Core::PointerEventArgs const& args)
					{
						if (isBoundRect)
						{
							lasso.Points().Append(args.CurrentPoint().RawPosition());
						}
					});

				m_pointerReleasedToken = m_InkCanvas.InkPresenter().UnprocessedInput().PointerReleased(
					winrt::auto_revoke,
					[&](auto const& sender, winrt::Windows::UI::Core::PointerEventArgs const& args)
					{
						lasso.Points().Append(args.CurrentPoint().RawPosition());
						isBoundRect = false;

						auto strokeContainer = m_InkCanvas.InkPresenter().StrokeContainer();
						auto allStrokes = strokeContainer.GetStrokes();

						std::unordered_set<uint32_t> previouslySelected;
						for (auto const& stroke : allStrokes)
						{
							if (stroke.Selected())
							{
								previouslySelected.insert(stroke.Id());
							}
						}

						// Clear all selections temporarily
						for (auto const& stroke : allStrokes)
						{
							stroke.Selected(false);
						}

						// Select new strokes with polyline
						strokeContainer.SelectWithPolyLine(lasso.Points());

						bool isCtrlPressed = (args.KeyModifiers() & winrt::Windows::System::VirtualKeyModifiers::Control)
							== winrt::Windows::System::VirtualKeyModifiers::Control;

						// Merge previous + new selection if Ctrl held
						for (auto const& stroke : allStrokes)
						{
							uint32_t id = stroke.Id();
							bool newlySelected = stroke.Selected();
							bool previously = previouslySelected.contains(id);
							stroke.Selected(isCtrlPressed ? (newlySelected || previously) : newlySelected);
						}

						// Compute combined bounding rectangle of all selected strokes
						bool hasSelection = false;
						winrt::Windows::Foundation::Rect combinedRect{};

						for (auto const& stroke : allStrokes)
						{
							if (stroke.Selected())
							{
								auto bounds = stroke.BoundingRect();
								if (!hasSelection)
								{
									combinedRect = bounds;
									hasSelection = true;
								}
								else
								{
									float x1 = std::min(combinedRect.X, bounds.X);
									float y1 = std::min(combinedRect.Y, bounds.Y);
									float x2 = std::max(combinedRect.X + combinedRect.Width, bounds.X + bounds.Width);
									float y2 = std::max(combinedRect.Y + combinedRect.Height, bounds.Y + bounds.Height);
									combinedRect.X = x1;
									combinedRect.Y = y1;
									combinedRect.Width = x2 - x1;
									combinedRect.Height = y2 - y1;
								}
							}
						}

						boundingRect = combinedRect;

						// Clear previous visuals
						m_SelectionCanvas.Children().Clear();
						m_BoundingBox = nullptr;

						// Draw combined bounding rectangle
						DrawBoundingRect();

						// Draw individual bounding rectangles for each selected stroke, smaller and lighter color
						for (auto const& stroke : allStrokes)
						{
							if (stroke.Selected())
							{
								auto bounds = stroke.BoundingRect();
								DrawStrokeBoundingRect(bounds);
							}
						}					

					});
			});

		// Create custom button to load Ink Strokes
		InkToolbarCustomToolButton m_ButtonLoad;
		ToolTipService::SetToolTip(m_ButtonLoad, box_value(L"Load Ink strokes"));
		m_ButtonLoad.Content(box_value(L"\U0001F4C2"));
		m_ButtonLoad.FontFamily(Media::FontFamily(L"Segoe UI Emoji"));
		m_ButtonLoad.FontSize(24);
		m_InkToolbar.Children().Append(m_ButtonLoad);

		// Create custom button to Save Ink Strokes
		InkToolbarCustomToolButton m_ButtonSave;
		ToolTipService::SetToolTip(m_ButtonSave, box_value(L"Save Ink strokes"));	
		m_ButtonSave.Content(box_value(L"\U0001F4BE"));
		m_ButtonSave.FontFamily(Media::FontFamily(L"Segoe UI Emoji"));
		m_ButtonSave.FontSize(24);
		m_InkToolbar.Children().Append(m_ButtonSave);

		bool m_IsPickerOpen = false;

		m_ButtonSave.Click([&m_IsPickerOpen, scrollViewer = m_ScrollViewer](auto const&, auto const&) -> winrt::fire_and_forget
			{
				if (m_IsPickerOpen)
					co_return;

				m_IsPickerOpen = true;

				if (m_hWndTarget)
				{
					winrt::Windows::Storage::Pickers::FileSavePicker savePicker;
					savePicker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
					savePicker.FileTypeChoices().Insert(L"Gif with embedded ISF", single_threaded_vector<hstring>({ L".gif" }));
					savePicker.SuggestedFileName(L"InkDrawing");

					auto initializeWithWindow = savePicker.as<IInitializeWithWindow>();
					initializeWithWindow->Initialize(m_hWndTarget);

					auto file = co_await savePicker.PickSaveFileAsync();
					if (file)
					{
						auto stream = co_await file.OpenAsync(winrt::Windows::Storage::FileAccessMode::ReadWrite);
						auto strokes = m_InkCanvas.InkPresenter().StrokeContainer();
						co_await strokes.SaveAsync(stream);
						co_await stream.FlushAsync();

						OutputDebugString(L"Strokes saved.\n");
					}
				}

				m_IsPickerOpen = false;

				if (m_LastRealTool)
				{
					m_InkToolbar.ActiveTool(m_LastRealTool);
					bool bRet = scrollViewer.Focus(winrt::Windows::UI::Xaml::FocusState::Pointer);
				}
			});

		m_ButtonLoad.Click([&m_IsPickerOpen, scrollViewer = m_ScrollViewer](auto const&, auto const&) -> winrt::fire_and_forget
			{
				if (m_IsPickerOpen)
					co_return;

				m_IsPickerOpen = true;

				if (m_hWndTarget)
				{
					winrt::Windows::Storage::Pickers::FileOpenPicker openPicker;
					openPicker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
					openPicker.FileTypeFilter().Append(L".gif");

					auto initializeWithWindow = openPicker.as<IInitializeWithWindow>();
					initializeWithWindow->Initialize(m_hWndTarget);

					auto file = co_await openPicker.PickSingleFileAsync();
					if (file)
					{
						auto stream = co_await file.OpenAsync(winrt::Windows::Storage::FileAccessMode::Read);
						auto strokes = m_InkCanvas.InkPresenter().StrokeContainer();
						strokes.Clear();
						co_await strokes.LoadAsync(stream);

						OutputDebugString(L"Strokes loaded.\n");
					}
				}

				m_IsPickerOpen = false;

				if (m_LastRealTool)
				{
					m_InkToolbar.ActiveTool(m_LastRealTool);
					bool bRet = scrollViewer.Focus(winrt::Windows::UI::Xaml::FocusState::Pointer);
				}
			});

		// Monitor tool changes to reset active tool when a custom button is clicked and focus is lost
		m_InkToolbar.ActiveToolChanged([&m_ButtonBackgroundImage, &m_ButtonRecognizer, &m_ButtonSelectRecognizer,
			&m_ButtonDeleteAll, &m_ButtonLoad, &m_ButtonSave](auto const&, auto const&)
			{
				auto tool = m_InkToolbar.ActiveTool();
				// Store the tool only if it's not a custom button
				if (tool != m_ButtonBackgroundImage && tool != m_ButtonRecognizer
					&& tool != m_ButtonSelectRecognizer && tool != m_ButtonDeleteAll
					&& tool != m_ButtonLoad && tool != m_ButtonSave)
				{
					m_LastRealTool = tool;
				}
			});

		// InkCanvas Containers (ScrollViewer - Grid)
		m_ScrollViewer.ZoomMode(ZoomMode::Enabled);
		m_ScrollViewer.MinZoomFactor(1.0f);
		m_ScrollViewer.VerticalScrollMode(ScrollMode::Enabled);
		m_ScrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
		m_ScrollViewer.HorizontalScrollMode(ScrollMode::Enabled);
		m_ScrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
		m_ScrollViewer.IsTabStop(true);
		
		m_OutputGrid.Background(Application::Current().Resources().Lookup(box_value(L"SystemControlBackgroundChromeWhiteBrush")).as<Brush>());
		m_OutputGrid.Children().Append(m_InkCanvas);
		m_OutputGrid.Children().Append(m_SelectionCanvas);

		// Set Grid height to "Auto" - interpreted as allowing flexible height. Don't set height directly.
		m_ScrollViewer.Content(m_OutputGrid);

		// Add ScrollViewer to row 1
		Grid::SetRow(m_ScrollViewer, 1);
		xamlGrid.Children().Append(m_ScrollViewer);

		// Add canvas + border to row 1
		//Grid::SetRow(canvasBorder, 1);
		//xamlGrid.Children().Append(canvasBorder);

		winrt::Windows::UI::Input::Inking::InkStrokeInput::StrokeStarted_revoker m_strokeStartedRevoker;
		winrt::Windows::UI::Input::Inking::InkStrokeInput::StrokeEnded_revoker m_strokeEndedRevoker;

		m_strokeStartedRevoker = m_InkCanvas.InkPresenter().StrokeInput().StrokeStarted(
			winrt::auto_revoke,
			[scrollViewer = m_ScrollViewer, hWndMain = hWndXamlIsland](winrt::Windows::UI::Input::Inking::InkStrokeInput const& /*sender*/, PointerEventArgs const& /*args*/)
			{
				ClearSelection();
				m_pointerPressedToken.revoke();
				m_pointerMovedToken.revoke();
				m_pointerReleasedToken.revoke();

				bool bRet = scrollViewer.Focus(winrt::Windows::UI::Xaml::FocusState::Pointer);
				SetFocus(hWndMain);
			});

		m_strokeEndedRevoker = m_InkCanvas.InkPresenter().StrokeInput().StrokeEnded(
			winrt::auto_revoke,
			[&](winrt::Windows::UI::Input::Inking::InkStrokeInput const& /*sender*/, PointerEventArgs const& /*args*/)
			{
				ClearSelection();
				m_pointerPressedToken.revoke();
				m_pointerMovedToken.revoke();
				m_pointerReleasedToken.revoke();

			});
		
		m_ButtonBackgroundImage.Click([&m_IsPickerOpen, outputGrid = m_OutputGrid, scrollViewer = m_ScrollViewer](auto const&, auto const&)->winrt::fire_and_forget
			{
				if (m_IsPickerOpen)
					co_return;

				m_IsPickerOpen = true;

				winrt::Windows::System::DispatcherQueue dispatcherQueue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
				if (dispatcherQueue)
				{
					if (m_hWndTarget)
					{
						dispatcherQueue.TryEnqueue([&m_IsPickerOpen, outputGrid, scrollViewer]()
							{
								winrt::Windows::Storage::Pickers::FileOpenPicker picker;
								picker.ViewMode(winrt::Windows::Storage::Pickers::PickerViewMode::Thumbnail);
								picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::PicturesLibrary);
								picker.FileTypeFilter().Append(L".jpg");
								picker.FileTypeFilter().Append(L".png");
								picker.FileTypeFilter().Append(L".bmp");
								picker.FileTypeFilter().Append(L".gif");

								auto initializeWithWindow = picker.as<IInitializeWithWindow>();
								initializeWithWindow->Initialize(m_hWndTarget);

								auto dispatcherQueue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();

								picker.PickSingleFileAsync().Completed(
									[dispatcherQueue, &m_IsPickerOpen, lastTool = m_LastRealTool, toolbar = m_InkToolbar, outputGrid, scrollViewer](auto const& asyncOp, auto const&)
									{
										try
										{
											auto file = asyncOp.GetResults();
											if (file)
											{
												file.OpenAsync(winrt::Windows::Storage::FileAccessMode::Read).Completed(
													[dispatcherQueue, &m_IsPickerOpen, lastTool, toolbar, outputGrid, scrollViewer](auto const& streamOp, auto const&)
													{
														try
														{
															auto stream = streamOp.GetResults();

															dispatcherQueue.TryEnqueue([stream, lastTool, toolbar, scrollViewer, outputGrid]()
																{
																	winrt::Windows::UI::Xaml::Media::Imaging::BitmapImage bitmapImage;
																	bitmapImage.SetSource(stream);

																	winrt::Windows::UI::Xaml::Media::ImageBrush brush;
																	brush.ImageSource(bitmapImage);
																	brush.Stretch(winrt::Windows::UI::Xaml::Media::Stretch::UniformToFill);

																	outputGrid.Background(brush);

																	if (lastTool)
																	{
																		toolbar.ActiveTool(lastTool);
																	}
																	bool bRet = scrollViewer.Focus(winrt::Windows::UI::Xaml::FocusState::Pointer);
																});

															m_IsPickerOpen = false;
														}
														catch (...)
														{
															OutputDebugString(L"Failed to open stream.\n");
														}
													});
											}
											else
											{
												// File picker canceled, restore tool immediately
												dispatcherQueue.TryEnqueue([lastTool, toolbar, scrollViewer]()
													{
														if (lastTool)
														{
															toolbar.ActiveTool(lastTool);
														}
														bool bRet = scrollViewer.Focus(winrt::Windows::UI::Xaml::FocusState::Pointer);
													});

												m_IsPickerOpen = false;
											}
										}
										catch (...)
										{
											OutputDebugString(L"File picker failed.\n");

											// Restore tool on error
											dispatcherQueue.TryEnqueue([lastTool, toolbar, scrollViewer]()
												{
													if (lastTool)
													{
														toolbar.ActiveTool(lastTool);
													}
													bool bRet = scrollViewer.Focus(winrt::Windows::UI::Xaml::FocusState::Pointer);
												});
											m_IsPickerOpen = false;
										}
									});
							});
					}
					else
					{
						m_IsPickerOpen = false;

						if (m_LastRealTool)
						{
							m_InkToolbar.ActiveTool(m_LastRealTool);
							bool bRet = scrollViewer.Focus(winrt::Windows::UI::Xaml::FocusState::Pointer);
						}
					}
				}
			});

	// Apply to XAML island
	xamlGrid.UpdateLayout();
	desktopSource.Content(xamlGrid);	

	AddAccelerators(m_ScrollViewer);

	//ShowWindow(hWnd, SW_SHOWNORMAL);
	//UpdateWindow(hWnd);
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
	{
		return 0;
	}
	break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hDC = BeginPaint(hWnd, &ps);

		EndPaint(hWnd, &ps);
	}
	break;
	case WM_GET_DRAWING_ATTRIBUTES:
	{	
		HWND hWndRequester = (HWND)wParam;
		if (m_InkCanvas)
			SendDrawingAttributes(hWndRequester, m_InkCanvas.InkPresenter());
	}
	break;
	case WM_SET_HANDLE:
	{
		m_hWndTarget = (HWND)wParam;
		SetTimer(hWnd, 1, 5000, NULL);
	}
	break;
	case WM_TIMER:
	{
		if (wParam == 1)
		{
			//DWORD_PTR result = 0;
			//if (!SendMessageTimeout(m_hWndTarget, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result))
			if (!IsWindow(m_hWndTarget))
			{
				Beep(7000, 10);
				PostMessage(hWnd, WM_CLOSE, 0, 0);
			}
		}
	}
	break;
	case WM_COPYDATA:
	{
		auto pCDS = reinterpret_cast<COPYDATASTRUCT*>(lParam);
		if (pCDS->dwData == COPYDATA_SET_DRAWINGATTRUBUTES && pCDS->cbData == sizeof(DrawingAttributesInfo))
		{
			DrawingAttributesInfo* info = reinterpret_cast<DrawingAttributesInfo*>(pCDS->lpData);
			
			auto da = info->IsPencil
				? winrt::Windows::UI::Input::Inking::InkDrawingAttributes::CreateForPencil()
				: winrt::Windows::UI::Input::Inking::InkDrawingAttributes();

			//da = m_InkCanvas.InkPresenter().CopyDefaultDrawingAttributes();	
		
			if (info->IsPencil)
			{
				WINRT_ASSERT(da.Kind() == Windows::UI::Input::Inking::InkDrawingAttributesKind::Pencil);
				if (info->PencilOpacity != 0)
					da.PencilProperties().Opacity(info->PencilOpacity);
				else
					da.PencilProperties().Opacity(1.0f);
				m_Opacity = da.PencilProperties().Opacity();
			}

			if (!info->IsPencil)
			{
				da.DrawAsHighlighter(info->IsHighlighter);
				if (info->IsHighlighter)
				{					
					if (info->PenTipShape == winrt::Windows::UI::Input::Inking::PenTipShape::Rectangle)
						da.PenTip(info->PenTipShape);
				}
				else
					da.PenTip(info->PenTipShape);
				m_PenTipShape = info->PenTipShape;

				if (info->PenTipTransform.m11 == 0 && info->PenTipTransform.m12 == 0 &&
					info->PenTipTransform.m21 == 0 && info->PenTipTransform.m22 == 0 &&
					info->PenTipTransform.m31 == 0 && info->PenTipTransform.m32 == 0)
				{
					// Replace with identity matrix if all elements are 0
					da.PenTipTransform(Windows::Foundation::Numerics::float3x2::identity());
				}
				else
				{
					// Use the provided matrix
					da.PenTipTransform(info->PenTipTransform);
					m_Matrix = info->PenTipTransform;
				}
			}

			if (info->Width != 0 && info->Width != 0)
			{
				da.Size({ info->Width, info->Height });
				m_Height = info->Height;
			}
			else
				da.Size({ 2, 2 });
			da.Color({ info->A, info->R, info->G, info->B });			
			da.FitToCurve(info->FitToCurve);
			da.IgnorePressure(info->IgnorePressure);

			if (m_InkCanvas)
			{				
				/*m_InkCanvas.InkPresenter().UpdateDefaultDrawingAttributes(da);*/

				auto toolObj = info->IsPencil
					? m_InkToolbar.GetToolButton(winrt::Windows::UI::Xaml::Controls::InkToolbarTool::Pencil)
					: m_InkToolbar.GetToolButton(winrt::Windows::UI::Xaml::Controls::InkToolbarTool::BallpointPen);

				if (info->IsHighlighter)
					toolObj = m_InkToolbar.GetToolButton(winrt::Windows::UI::Xaml::Controls::InkToolbarTool::Highlighter);

				if (auto penButton = toolObj.try_as<winrt::Windows::UI::Xaml::Controls::InkToolbarPenButton>())
				{					
					ApplyDrawingAttributesToToolbar(*info, penButton);	

					// To avoid RPC_E_CANTCALLOUT_ININPUTSYNCCALL					
					auto inkToolbar = m_InkToolbar; // capture by value
					inkToolbar.Dispatcher().RunAsync(
						winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
						[inkToolbar, toolObj]()
						{
							inkToolbar.ActiveTool(toolObj);
						});	
					m_InkCanvas.InkPresenter().UpdateDefaultDrawingAttributes(da);
				}
			}			
			return TRUE;
		}
		break;
	}

	case WM_DESTROY:
	{
		PostQuitMessage(0);
		return 0;
	}
	break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

void SendDrawingAttributes(HWND hWndTarget, winrt::Windows::UI::Input::Inking::InkPresenter const& presenter)
{
	auto da = presenter.CopyDefaultDrawingAttributes();
	winrt::Windows::UI::Color color = da.Color();

	DrawingAttributesInfo info = {};
	info.R = color.R;
	info.G = color.G;
	info.B = color.B;
	info.A = color.A;
	info.Width = da.Size().Width;
	info.Height = da.Size().Height;
	info.PenTipShape = da.PenTip();
	info.IsHighlighter = da.DrawAsHighlighter();
	info.IsPencil = (da.Kind() == winrt::Windows::UI::Input::Inking::InkDrawingAttributesKind::Pencil);
	if (info.IsPencil)
	{
		WINRT_ASSERT(da.Kind() == Windows::UI::Input::Inking::InkDrawingAttributesKind::Pencil);
		info.PencilOpacity = da.PencilProperties().Opacity();
	}
	else
		info.PencilOpacity = 1.0f;
	info.FitToCurve = da.FitToCurve();
	info.IgnorePressure = da.IgnorePressure();
	info.PenTipTransform = da.PenTipTransform();

	COPYDATASTRUCT cds = {};
	cds.dwData = COPYDATA_GET_DRAWINGATTRUBUTES;
	cds.cbData = sizeof(info);
	cds.lpData = &info;
	SendMessage(hWndTarget, WM_COPYDATA, reinterpret_cast<WPARAM>(nullptr), reinterpret_cast<LPARAM>(&cds));
}

void ApplyDrawingAttributesToToolbar(const DrawingAttributesInfo& info,	winrt::Windows::UI::Xaml::Controls::InkToolbarPenButton const& penButton)
{
	using namespace winrt;
	using namespace Windows::UI;
	using namespace Windows::UI::Xaml::Media;

	// Update brush color — try to find a match in the palette
	auto palette = penButton.Palette();
	Color targetColor = ColorHelper::FromArgb(info.A, info.R, info.G, info.B);

	int matchingIndex = -1;
	for (uint32_t i = 0; i < palette.Size(); ++i)
	{
		if (auto solidBrush = palette.GetAt(i).try_as<SolidColorBrush>())
		{
			Color c = solidBrush.Color();
			if (c.A == targetColor.A && c.R == targetColor.R && c.G == targetColor.G && c.B == targetColor.B)
			{
				matchingIndex = static_cast<int>(i);
				break;
			}
		}
	}

	if (matchingIndex >= 0)
	{
		penButton.SelectedBrushIndex(matchingIndex);
	}
	else
	{
		// Add new color to palette
		palette.Append(SolidColorBrush{ targetColor });
		penButton.SelectedBrushIndex(palette.Size() - 1);
	}

	// Update stroke width
	//double nAverageSize = (info.Width + info.Height) / 2.0;
	/*penButton.SelectedStrokeWidth(nAverageSize);*/
	penButton.SelectedStrokeWidth(info.Width);

	// penButton.ToolKind();
	// info.penTipShape not used...	
}

// To be completed from https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.inking.inkrecognizer.name?view=winrt-26100

winrt::hstring GetRecognizerLabel(winrt::hstring const& name)
{
	std::wstring_view n = name.c_str();

	if (n.find(L"Français") != std::wstring_view::npos || n.find(L"French") != std::wstring_view::npos) return L"FR";
	if (n.find(L"English") != std::wstring_view::npos) return L"EN";
	if (n.find(L"Español") != std::wstring_view::npos || n.find(L"Spanish") != std::wstring_view::npos) return L"ES";
	if (n.find(L"Deutsch") != std::wstring_view::npos || n.find(L"German") != std::wstring_view::npos) return L"DE";
	if (n.find(L"Italian") != std::wstring_view::npos) return L"IT";
	if (n.find(L"日本語") != std::wstring_view::npos || n.find(L"Japanese") != std::wstring_view::npos) return L"JP";
	if (n.find(L"中文") != std::wstring_view::npos || n.find(L"Chinese") != std::wstring_view::npos) return L"ZH";
	if (n.find(L"한글") != std::wstring_view::npos || n.find(L"Korean") != std::wstring_view::npos) return L"KO";
	if (n.find(L"русского") != std::wstring_view::npos || n.find(L"Russian") != std::wstring_view::npos) return L"RU";
	if (n.find(L"српски") != std::wstring_view::npos || n.find(L"Serbian") != std::wstring_view::npos) return L"SR";

	return L"?";
}

void RecognizeInkAndSendText(
	winrt::Windows::UI::Input::Inking::InkPresenter const& presenter,
	HWND hWndTarget,
	winrt::Windows::UI::Input::Inking::InkRecognizerContainer const& recognizerContainer)
{
	auto strokes = presenter.StrokeContainer().GetStrokes();

	if (strokes.Size() > 0)
	{
		recognizerContainer.RecognizeAsync(presenter.StrokeContainer(), winrt::Windows::UI::Input::Inking::InkRecognitionTarget::All)
			.Completed([hWndTarget](auto const& asyncOp, auto const&)
				{
					try
					{
						auto results = asyncOp.GetResults();
						std::wstring recognizedText = L"";

						for (auto const& result : results)
						{
							auto candidates = result.GetTextCandidates();
							if (candidates.Size() > 0)
							{
								recognizedText += candidates.GetAt(0).c_str(); // Best match
								recognizedText += L"\n";
							}
						}

						//if (!recognizedText.empty())
						{
							std::wstring copyText = recognizedText;

							COPYDATASTRUCT cds = {};
							cds.dwData = COPYDATA_GET_RECOGNIZEDTEXT;
							cds.cbData = static_cast<DWORD>((copyText.size() + 1) * sizeof(wchar_t));
							cds.lpData = (void*)copyText.c_str();

							SendMessage(hWndTarget, WM_COPYDATA, reinterpret_cast<WPARAM>(nullptr), reinterpret_cast<LPARAM>(&cds));
						}
					}
					catch (...)
					{
						OutputDebugString(L"Ink recognition failed.\n");
					}
				});
	}
	else
	{
		COPYDATASTRUCT cds = {};
		cds.dwData = COPYDATA_GET_RECOGNIZEDTEXT;
		cds.cbData = static_cast<DWORD>(sizeof(wchar_t));
		cds.lpData = (void*)L"";

		SendMessage(hWndTarget, WM_COPYDATA, reinterpret_cast<WPARAM>(nullptr), reinterpret_cast<LPARAM>(&cds));
	}	
}

void ClearDrawnBoundingRect()
{
	if (m_SelectionCanvas.Children().Size() > 0)
	{
		m_SelectionCanvas.Children().Clear();
		boundingRect.Width = 0;
		boundingRect.Height = 0;
		m_BoundingBox = nullptr;
	}
}

void ClearSelection()
{
	auto strokes = m_InkCanvas.InkPresenter().StrokeContainer().GetStrokes();
	for (auto const& stroke : strokes)
	{
		stroke.Selected(false);
	}
	ClearDrawnBoundingRect();
}

winrt::Windows::Foundation::Point m_lastPointerPosition{};
bool m_isDragging = false;

void DrawBoundingRect()
{
	if (boundingRect.Width <= 0 || boundingRect.Height <= 0) return;

	if (!m_BoundingBox)
	{
		m_BoundingBox = winrt::Windows::UI::Xaml::Controls::Canvas{};

		m_BoundingBox.PointerPressed(
			winrt::Windows::UI::Xaml::Input::PointerEventHandler(
				[&](auto const& sender, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e)
				{
					//Beep(1000, 10);
					m_isDragging = true;
					m_lastPointerPosition = e.GetCurrentPoint(m_SelectionCanvas).Position();
					m_BoundingBox.CapturePointer(e.Pointer());
					e.Handled(true);
				}));

		m_BoundingBox.PointerMoved(
			winrt::Windows::UI::Xaml::Input::PointerEventHandler(
				[&](auto const& sender, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e)
				{
					if (!m_isDragging) return;

					auto current = e.GetCurrentPoint(m_SelectionCanvas).Position();
					float dx = current.X - m_lastPointerPosition.X;
					float dy = current.Y - m_lastPointerPosition.Y;
					m_lastPointerPosition = current;

					auto strokeContainer = m_InkCanvas.InkPresenter().StrokeContainer();
					auto allStrokes = strokeContainer.GetStrokes();

					std::vector<winrt::Windows::UI::Input::Inking::InkStroke> newStrokes;
					for (auto const& stroke : allStrokes)
					{
						if (stroke.Selected())
						{
							auto newStroke = stroke.Clone();
							winrt::Windows::Foundation::Numerics::float3x2 translate = winrt::Windows::Foundation::Numerics::make_float3x2_translation(dx, dy);
							newStroke.PointTransform(stroke.PointTransform() * translate);
							newStroke.Selected(true);
							newStrokes.push_back(newStroke);
						}
					}
					strokeContainer.DeleteSelected();

					for (auto const& stroke : newStrokes)
					{
						strokeContainer.AddStroke(stroke);
					}

					// Move the bounding box
					double left = winrt::Windows::UI::Xaml::Controls::Canvas::GetLeft(m_BoundingBox);
					double top = winrt::Windows::UI::Xaml::Controls::Canvas::GetTop(m_BoundingBox);
					winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(m_BoundingBox, left + dx);
					winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(m_BoundingBox, top + dy);
				}));

		m_BoundingBox.PointerReleased(
			winrt::Windows::UI::Xaml::Input::PointerEventHandler(
				[&](auto const& sender, winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& e)
				{
					m_isDragging = false;
					m_BoundingBox.ReleasePointerCapture(e.Pointer());
				}));

		m_BoundingBox.PointerEntered([](auto const&, auto const& e)
			{
				Windows::UI::Core::CoreCursor cursor(Windows::UI::Core::CoreCursorType::SizeAll, 1);
				Windows::UI::Core::CoreWindow::GetForCurrentThread().PointerCursor(cursor);
			});

		m_BoundingBox.PointerExited([](auto const&, auto const&)
			{
				Windows::UI::Core::CoreCursor arrowCursor(Windows::UI::Core::CoreCursorType::Arrow, 1);
				Windows::UI::Core::CoreWindow::GetForCurrentThread().PointerCursor(arrowCursor);
			});

		m_SelectionCanvas.Children().Append(m_BoundingBox);
	}
	else
	{
		m_BoundingBox.Children().Clear();
	}

	m_BoundingBox.Width(boundingRect.Width);
	m_BoundingBox.Height(boundingRect.Height);

	// Draw visible bounding rectangle inside container
	winrt::Windows::UI::Xaml::Shapes::Rectangle outerRect;
	outerRect.Width(boundingRect.Width);
	outerRect.Height(boundingRect.Height);
	outerRect.Stroke(winrt::Windows::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Blue()));
	outerRect.StrokeThickness(1);
	outerRect.StrokeDashArray().Append(5);
	outerRect.StrokeDashArray().Append(2);
	outerRect.Fill(winrt::Windows::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));

	// Make container hit-testable
	m_BoundingBox.IsHitTestVisible(true);

	m_BoundingBox.Children().Append(outerRect);

	// Position the container
	winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(m_BoundingBox, boundingRect.X);
	winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(m_BoundingBox, boundingRect.Y);
}


void DrawStrokeBoundingRect(winrt::Windows::Foundation::Rect const& strokeRect)
{
	if (!m_BoundingBox) return;

	winrt::Windows::UI::Xaml::Shapes::Rectangle visualRect;
	visualRect.Width(strokeRect.Width);
	visualRect.Height(strokeRect.Height);
	visualRect.Stroke(winrt::Windows::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::DarkBlue()));
	visualRect.StrokeThickness(0.5);
	visualRect.StrokeDashArray().Append(2);
	visualRect.StrokeDashArray().Append(1);
	visualRect.Fill(winrt::Windows::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
	visualRect.IsHitTestVisible(false);

	// Offset relative to the bounding box
	winrt::Windows::Foundation::Point offset{
		strokeRect.X - boundingRect.X,
		strokeRect.Y - boundingRect.Y
	};

	winrt::Windows::UI::Xaml::Controls::Canvas::SetLeft(visualRect, offset.X);
	winrt::Windows::UI::Xaml::Controls::Canvas::SetTop(visualRect, offset.Y);

	m_BoundingBox.Children().Append(visualRect);
}

void AddAccelerators(winrt::Windows::UI::Xaml::UIElement const& control)
{
	using namespace winrt;
	using namespace Windows::System;
	using namespace Windows::UI::Xaml::Input;

	control.KeyboardAcceleratorPlacementMode(KeyboardAcceleratorPlacementMode::Hidden);
	auto strokeContainer = m_InkCanvas.InkPresenter().StrokeContainer();

	// Suppr
	KeyboardAccelerator deleteAccelerator;
	deleteAccelerator.Key(VirtualKey::Delete);
	deleteAccelerator.Modifiers(VirtualKeyModifiers::None);
	deleteAccelerator.Invoked([strokeContainer](KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& e)
		{
			strokeContainer.DeleteSelected();
			ClearDrawnBoundingRect();
			Windows::UI::Core::CoreCursor arrowCursor(Windows::UI::Core::CoreCursorType::Arrow, 1);
			Windows::UI::Core::CoreWindow::GetForCurrentThread().PointerCursor(arrowCursor);
			e.Handled(true);
		});
	control.KeyboardAccelerators().Append(deleteAccelerator);

	// Ctrl+X
	KeyboardAccelerator cutCtrlX;
	cutCtrlX.Key(VirtualKey::X);
	cutCtrlX.Modifiers(VirtualKeyModifiers::Control);
	cutCtrlX.Invoked([strokeContainer](KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& e)
		{
			strokeContainer.CopySelectedToClipboard();
			strokeContainer.DeleteSelected();
			ClearDrawnBoundingRect();
			Windows::UI::Core::CoreCursor arrowCursor(Windows::UI::Core::CoreCursorType::Arrow, 1);
			Windows::UI::Core::CoreWindow::GetForCurrentThread().PointerCursor(arrowCursor);
			e.Handled(true);
		});
	control.KeyboardAccelerators().Append(cutCtrlX);

	// Shift+Suppr
	KeyboardAccelerator cutShiftSuppr;
	cutShiftSuppr.Key(VirtualKey::Delete);
	cutShiftSuppr.Modifiers(VirtualKeyModifiers::Shift);
	cutShiftSuppr.Invoked([strokeContainer](KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& e)
		{
			strokeContainer.CopySelectedToClipboard();
			strokeContainer.DeleteSelected();
			ClearDrawnBoundingRect();
			Windows::UI::Core::CoreCursor arrowCursor(Windows::UI::Core::CoreCursorType::Arrow, 1);
			Windows::UI::Core::CoreWindow::GetForCurrentThread().PointerCursor(arrowCursor);
			e.Handled(true);
		});
	control.KeyboardAccelerators().Append(cutShiftSuppr);

	// Ctrl+A
	KeyboardAccelerator selectAllAccelerator;
	selectAllAccelerator.Key(VirtualKey::A);
	selectAllAccelerator.Modifiers(VirtualKeyModifiers::Control);
	selectAllAccelerator.Invoked([strokeContainer](KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& e)
		{
			auto strokes = strokeContainer.GetStrokes();
			for (auto const& stroke : strokes)
			{
				stroke.Selected(true);
				auto strokeRect = stroke.BoundingRect();

				if (boundingRect.Width == 0 && boundingRect.Height == 0)
				{
					boundingRect = strokeRect;
				}
				else
				{
					boundingRect = Windows::Foundation::Rect{
						std::min(boundingRect.X, strokeRect.X),
						std::min(boundingRect.Y, strokeRect.Y),
						std::max(boundingRect.X + boundingRect.Width, strokeRect.X + strokeRect.Width) - std::min(boundingRect.X, strokeRect.X),
						std::max(boundingRect.Y + boundingRect.Height, strokeRect.Y + strokeRect.Height) - std::min(boundingRect.Y, strokeRect.Y)
					};
				}
			}

			// Update visuals (bounding box, etc.)
			DrawBoundingRect();
			for (auto const& stroke : strokes)
			{
				if (stroke.Selected())
				{
					DrawStrokeBoundingRect(stroke.BoundingRect());
				}
			}
			e.Handled(true);
		});
	control.KeyboardAccelerators().Append(selectAllAccelerator);

	// Ctrl+C
	KeyboardAccelerator copyCtrlC;
	copyCtrlC.Key(VirtualKey::C);
	copyCtrlC.Modifiers(VirtualKeyModifiers::Control);
	copyCtrlC.Invoked([strokeContainer](KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& e)
		{
			strokeContainer.CopySelectedToClipboard();
			e.Handled(true);
		});
	control.KeyboardAccelerators().Append(copyCtrlC);

	// Ctrl+Insert
	KeyboardAccelerator copyCtrlInsert;
	copyCtrlInsert.Key(VirtualKey::Insert);
	copyCtrlInsert.Modifiers(VirtualKeyModifiers::Control);
	copyCtrlInsert.Invoked([strokeContainer](KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& e)
		{
			strokeContainer.CopySelectedToClipboard();
			e.Handled(true);
		});
	control.KeyboardAccelerators().Append(copyCtrlInsert);

	// Ctrl+V
	KeyboardAccelerator pasteCtrlV;
	pasteCtrlV.Key(VirtualKey::V);
	pasteCtrlV.Modifiers(VirtualKeyModifiers::Control);
	pasteCtrlV.Invoked([strokeContainer](KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& e)
		{
			if (strokeContainer.CanPasteFromClipboard())
			{
				if (strokeContainer.CanPasteFromClipboard())
				{
					// Snapshot current strokes
					std::vector<winrt::Windows::UI::Input::Inking::InkStroke> beforeStrokes;
					for (auto const& stroke : strokeContainer.GetStrokes())
					{
						beforeStrokes.push_back(stroke);
					}

					// Paste strokes at (0, 0)
					winrt::Windows::Foundation::Rect pastedRect = strokeContainer.PasteFromClipboard({ 0.0, 0.0 });

					// Get new strokes
					std::vector<winrt::Windows::UI::Input::Inking::InkStroke> newStrokes;
					for (auto const& stroke : strokeContainer.GetStrokes())
					{
						bool found = false;
						for (auto const& oldStroke : beforeStrokes)
						{
							if (stroke.Id() == oldStroke.Id())
							{
								found = true;
								break;
							}
						}
						if (!found)
						{
							newStrokes.push_back(stroke);
						}
					}

					// Select only the new strokes
					for (auto const& stroke : strokeContainer.GetStrokes())
					{
						stroke.Selected(false); // Deselect all first
					}
					for (auto const& stroke : newStrokes)
					{
						stroke.Selected(true);
					}

					// Redraw bounding visuals
					boundingRect = pastedRect;
					DrawBoundingRect();
					for (auto const& stroke : newStrokes)
					{
						DrawStrokeBoundingRect(stroke.BoundingRect());
					}
				}
			}
			else
			{
				Beep(8000, 10);
			}
			e.Handled(true);
		});
	control.KeyboardAccelerators().Append(pasteCtrlV);

	// Shift+Insert
	KeyboardAccelerator pasteShiftInsert;
	pasteShiftInsert.Key(VirtualKey::Insert);
	pasteShiftInsert.Modifiers(VirtualKeyModifiers::Shift);
	pasteShiftInsert.Invoked([strokeContainer](KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& e)
		{
			if (strokeContainer.CanPasteFromClipboard())
			{
				if (strokeContainer.CanPasteFromClipboard())
				{
					// Snapshot current strokes
					std::vector<winrt::Windows::UI::Input::Inking::InkStroke> beforeStrokes;
					for (auto const& stroke : strokeContainer.GetStrokes())
					{
						beforeStrokes.push_back(stroke);
					}

					// Paste strokes at (0, 0)
					winrt::Windows::Foundation::Rect pastedRect = strokeContainer.PasteFromClipboard({ 0.0, 0.0 });

					// Get new strokes
					std::vector<winrt::Windows::UI::Input::Inking::InkStroke> newStrokes;
					for (auto const& stroke : strokeContainer.GetStrokes())
					{
						bool found = false;
						for (auto const& oldStroke : beforeStrokes)
						{
							if (stroke.Id() == oldStroke.Id())
							{
								found = true;
								break;
							}
						}
						if (!found)
						{
							newStrokes.push_back(stroke);
						}
					}

					// Select only the new strokes
					for (auto const& stroke : strokeContainer.GetStrokes())
					{
						stroke.Selected(false); // Deselect all first
					}
					for (auto const& stroke : newStrokes)
					{
						stroke.Selected(true);
					}

					// Redraw bounding visuals
					boundingRect = pastedRect;
					DrawBoundingRect();
					for (auto const& stroke : newStrokes)
					{
						DrawStrokeBoundingRect(stroke.BoundingRect());
					}
				}
			}
			else
			{
				Beep(8000, 10);
			}
			e.Handled(true);
		});
	control.KeyboardAccelerators().Append(pasteShiftInsert);
}
