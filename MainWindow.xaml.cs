using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Reflection.Metadata;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.UI.Input.Inking;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace Winui3_XAMLIslands_InkCanvas
{
    /// <summary>
    /// An empty window that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainWindow : Window, INotifyPropertyChanged
    {
        [DllImport("User32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern IntPtr FindWindow(string? lpClassName, string? lpWindowName);

        [DllImport("User32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern IntPtr FindWindowEx(IntPtr hwndParent, IntPtr hwndChildAfter, string? lpszClass, string? lpszWindow);

        [DllImport("User32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern IntPtr SetParent(IntPtr hWndChild, IntPtr hWndParent);

        [DllImport("User32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern int SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("User32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern int PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        public const int WM_CLOSE = 0x0010;

        [DllImport("User32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern IntPtr GetProp(IntPtr hWnd, string lpString);

        public delegate int SUBCLASSPROC(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam, IntPtr uIdSubclass, uint dwRefData);

        [DllImport("Comctl32.dll", SetLastError = true)]
        public static extern bool SetWindowSubclass(IntPtr hWnd, SUBCLASSPROC pfnSubclass, uint uIdSubclass, uint dwRefData);

        [DllImport("Comctl32.dll", SetLastError = true)]
        public static extern int DefSubclassProc(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam);

        // https://learn.microsoft.com/en-us/windows/win32/dataxchg/using-data-copy
        private const int WM_COPYDATA = 0x004A;

        [StructLayout(LayoutKind.Sequential)]
        private struct COPYDATASTRUCT
        {
            public IntPtr dwData;
            public int cbData;
            public IntPtr lpData;
        }

        public const int WM_USER = 0x0400;
        public const int WM_GET_DRAWING_ATTRIBUTES = WM_USER + 100;
        public const int WM_SET_DRAWING_ATTRIBUTES = WM_USER + 101;
        public const int WM_SET_HANDLE = WM_USER + 102;

        public const int COPYDATA_GET_DRAWINGATTRUBUTES = 1;
        public const int COPYDATA_SET_DRAWINGATTRUBUTES = 2;
        public const int COPYDATA_GET_RECOGNIZEDTEXT = 3;

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct DrawingAttributesInfo
        {
            public byte R, G, B, A;
            public float Width, Height;
            public PenTipShape PenTipShape;
            [MarshalAs(UnmanagedType.U1)] public bool IsHighlighter;
            [MarshalAs(UnmanagedType.U1)] public bool IsPencil;
            public float PencilOpacity;
            [MarshalAs(UnmanagedType.U1)] public bool FitToCurve;
            [MarshalAs(UnmanagedType.U1)] public bool IgnorePressure;
            public System.Numerics.Matrix3x2 PenTipTransform;
        }

        private IntPtr hWndMain = IntPtr.Zero;
        private IntPtr m_hWndInkCanvasParent = IntPtr.Zero;
        private IntPtr m_hWndXamlIsland = IntPtr.Zero;
        private SUBCLASSPROC SubClassDelegate;
        private Action<DrawingAttributesInfo>? OnDrawingAttributesReceived;
        private DrawingAttributesInfo? m_CurrentDrawingAttributesInfo = null;

        public MainWindow()
        {
            this.InitializeComponent();
            hWndMain = WinRT.Interop.WindowNative.GetWindowHandle(this);
            this.Title = "WinUI 3 - Test UWP InkCanvas with XAML Islands";

            // In case process has not been terminated
            m_hWndInkCanvasParent = FindWindow("XAMLIslandsInkCanvas", null);
            if (m_hWndInkCanvasParent != IntPtr.Zero)
                PostMessage(m_hWndInkCanvasParent, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);

            string sExePath = AppContext.BaseDirectory;
            string sFullPath = System.IO.Path.Combine(sExePath, "Assets\\XAMLIslandsInkCanvas.exe");
            System.Diagnostics.Process proc = System.Diagnostics.Process.Start(sFullPath);
            proc.WaitForInputIdle();
            m_hWndInkCanvasParent = FindWindow("XAMLIslandsInkCanvas", null);
            m_hWndXamlIsland = FindWindowEx(m_hWndInkCanvasParent, IntPtr.Zero, "Windows.UI.Composition.DesktopWindowContentBridge", null);
            cp1.ContainerPanelInit("STATIC", "", hWndMain);
            if (m_hWndXamlIsland != IntPtr.Zero && cp1.hWndContainer != IntPtr.Zero)
            {
                cp1.SetOpacity(cp1.hWndContainer, 100);
                SetParent(m_hWndXamlIsland, cp1.hWndContainer);
                SendMessage(m_hWndInkCanvasParent, WM_SET_HANDLE, hWndMain, IntPtr.Zero);
            }

            SubClassDelegate = new SUBCLASSPROC(WindowSubClass);
            bool bRet = SetWindowSubclass(hWndMain, SubClassDelegate, 0, 0);

            OnDrawingAttributesReceived = daiReceived =>
            {
                m_CurrentDrawingAttributesInfo = daiReceived;
                rectInkDrawingColor.Fill = new SolidColorBrush(Windows.UI.Color.FromArgb(daiReceived.A, daiReceived.R, daiReceived.G, daiReceived.B));
                PenTipWidth = daiReceived.Width;
                if (daiReceived.IsPencil)
                    PenTipHeight = PenTipWidth;
                else
                    PenTipHeight = daiReceived.Height;
                m_PenTipShape = daiReceived.PenTipShape;               
                tsPenTipShape.IsOn = (m_PenTipShape == PenTipShape.Circle);
                m_IsPencil = daiReceived.IsPencil;
                tsPencil.IsOn = (m_IsPencil == true);
                PencilOpacity = daiReceived.PencilOpacity;
                m_IsHighlighter = daiReceived.IsHighlighter;
                tsHighlighter.IsOn = (m_IsHighlighter == true);              

                //PenTipTransform { { { M11: 1 M12: 0} { M21: 0 M22: 1} { M31: 0 M32: 0} } }  System.Numerics.Matrix3x2
                //IsIdentity  true    bool
                //M11 1   float
                //M12 0   float
                //M21 0   float
                //M22 1   float
                //M31 0   float
                //M32 0   float

                M11.Text = daiReceived.PenTipTransform.M11.ToString();
                M12.Text = daiReceived.PenTipTransform.M12.ToString();
                M21.Text = daiReceived.PenTipTransform.M21.ToString();
                M22.Text = daiReceived.PenTipTransform.M22.ToString();
                M31.Text = "0";
                M32.Text = "0";

                //System.Diagnostics.Debug.WriteLine($"Received color: {daiReceived.R}, {daiReceived.G}, {daiReceived.B}, Size: ({daiReceived.Width},{daiReceived.Height}), Pencil: {daiReceived.IsPencil}, PenTipShape: {daiReceived.PenTipShape}");
            };         

            ChangeCursor(rectInkDrawingColor, Microsoft.UI.Input.InputSystemCursor.Create(Microsoft.UI.Input.InputSystemCursorShape.Hand));

            tsHighlighter_Toggled(tsHighlighter, null);

            this.Closed += MainWindow_Closed;
        }

        private void ChangeCursor(UIElement control, Microsoft.UI.Input.InputCursor cursor)
        {
            //var cursorProperty = typeof(UIElement).GetProperty("ProtectedCursor", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            //var currentCursor = cursorProperty?.GetValue(control) as InputSystemCursor;
            Microsoft.UI.Input.InputCursor ic = cursor;
            var methodInfo = typeof(UIElement).GetMethod("set_ProtectedCursor", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
            if (methodInfo != null)
            {
                methodInfo.Invoke(control, new object[] { ic });
            }            
        }

        private void MainWindow_Closed(object sender, WindowEventArgs args)
        {
            if (m_hWndInkCanvasParent != IntPtr.Zero)
                PostMessage(m_hWndInkCanvasParent, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
        }

        private void buttonGetInkAttributes_Click(object sender, RoutedEventArgs e)
        {  
            SendMessage(m_hWndInkCanvasParent, WM_GET_DRAWING_ATTRIBUTES, hWndMain, IntPtr.Zero);
        }


        private async void buttonSetInkAttributes_Click(object sender, RoutedEventArgs e)
        {
            // If Pencil : Circle obligatory, if Highlighter : Rectangle

            if (tsPencil.IsOn && !tsPenTipShape.IsOn)
            {
                Windows.UI.Popups.MessageDialog md = new Windows.UI.Popups.MessageDialog("Pencil must be a Circle", "Error");
                WinRT.Interop.InitializeWithWindow.Initialize(md, hWndMain);
                _ = await md.ShowAsync();
            }
            else if (tsHighlighter.IsOn && tsPenTipShape.IsOn)
            {
                Windows.UI.Popups.MessageDialog md = new Windows.UI.Popups.MessageDialog("Highlighter must be a Rectangle", "Error");
                WinRT.Interop.InitializeWithWindow.Initialize(md, hWndMain);
                _ = await md.ShowAsync();
            }
            else if (tsPencil.IsOn && (PenTipWidth != PenTipHeight))
            {
                Windows.UI.Popups.MessageDialog md = new Windows.UI.Popups.MessageDialog("Width and Height must be equal for Pencil", "Error");
                WinRT.Interop.InitializeWithWindow.Initialize(md, hWndMain);
                _ = await md.ShowAsync();
            }
            else if (double.IsNaN(M11.Value) || double.IsNaN(M12.Value) || double.IsNaN(M21.Value) || double.IsNaN(M22.Value))
            {
                Windows.UI.Popups.MessageDialog md = new Windows.UI.Popups.MessageDialog("Matrix values must be filled\r(1:0 - 0:1 for Identity Matrix)", "Error");
                WinRT.Interop.InitializeWithWindow.Initialize(md, hWndMain);
                _ = await md.ShowAsync();
            }
            else
            {
                var dai = new DrawingAttributesInfo();

                if (rectInkDrawingColor.Fill is SolidColorBrush brush)
                {
                    Windows.UI.Color color = brush.Color;
                    dai.A = color.A;
                    dai.R = color.R;
                    dai.G = color.G;
                    dai.B = color.B;
                }

                dai.Width = (float)PenTipWidth;
                dai.Height = (float)PenTipHeight;
                dai.PenTipShape = m_PenTipShape;
                dai.IsPencil = m_IsPencil;
                if (m_IsPencil)
                    dai.PencilOpacity = PencilOpacity;
                dai.IsHighlighter = m_IsHighlighter;

                dai.PenTipTransform.M11 = (float)M11.Value;
                dai.PenTipTransform.M12 = (float)M12.Value;
                dai.PenTipTransform.M21 = (float)M21.Value;
                dai.PenTipTransform.M22 = (float)M22.Value;
                dai.PenTipTransform.M31 = (float)M31.Value;
                dai.PenTipTransform.M32 = (float)M32.Value;

                // {{ {M11:0,70710677 M12:0,70710677}
                //    {M21:-0,70710677 M22:0,70710677}
                //    {M31:0 M32:0} }}
                //https://graphicmaths.com/pure/matrices/matrix-2d-transformations/
                //dai.PenTipTransform = System.Numerics.Matrix3x2.CreateRotation((float)(Math.PI * 45 / 180));

                SendInkAttributes(m_hWndInkCanvasParent, dai);
            }
        }

        //IntPtr pInkPresenter = GetProp(m_hWndXamlIsland, "InkPresenter");
        //if (pInkPresenter != IntPtr.Zero)
        //{  
        //    //var unknown = Marshal.GetObjectForIUnknown(pInkPresenter);
        //    InkPresenter ip = WinRT.MarshalInterface<InkPresenter>.FromAbi(pInkPresenter);           
        //    Marshal.Release(pInkPresenter);
        //}

        public event PropertyChangedEventHandler? PropertyChanged;

        // PenTip Width and Height 

        private float _PenTipWidth = 2.0f;
        public float PenTipWidth
        {
            get => _PenTipWidth;
            set
            {
                if (_PenTipWidth != value)
                {
                    _PenTipWidth = value;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(PenTipWidth)));
                }
            }
        }
        
        private float _PenTipHeight = 2.0f;
        public float PenTipHeight
        {
            get => _PenTipHeight;
            set
            {
                if (_PenTipHeight != value)
                {
                    _PenTipHeight = value;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(PenTipHeight)));
                }
            }
        }

        // Pencil Opacity

        private float _PencilOpacity = 1.0f;
        public float PencilOpacity
        {
            get => _PencilOpacity;
            set
            {
                if (_PencilOpacity != value)
                {
                    _PencilOpacity = value;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(PencilOpacity)));
                }
            }
        }

        public PenTipShape m_PenTipShape = PenTipShape.Circle;
        private void tsPenTipShape_Toggled(object sender, RoutedEventArgs e)
        {           
            if (sender is ToggleSwitch ts)
            {
                m_PenTipShape = ts.IsOn ? PenTipShape.Circle : PenTipShape.Rectangle;
            }           
        }

        public bool m_IsPencil = false;
        private void tsPencil_Toggled(object sender, RoutedEventArgs e)
        {
            if (sender is ToggleSwitch ts)
            {
                m_IsPencil = ts.IsOn ? true : false;
                if (ts.IsOn)
                {                    
                    tsHighlighter.IsOn = false;
                }
            }
        }

        public bool m_IsHighlighter = false;
        private void tsHighlighter_Toggled(object sender, RoutedEventArgs? e)
        {
            if (sender is ToggleSwitch ts)
            {
                m_IsHighlighter = ts.IsOn ? true : false;             

                double nMax = m_IsHighlighter ? 64 : 24;

                sliderPenTipWidth.Maximum = nMax;
                if (sliderPenTipWidth.Value > nMax)
                    sliderPenTipWidth.Value = nMax;

                sliderPenTipHeight.Maximum = nMax;
                if (sliderPenTipHeight.Value > nMax)
                    sliderPenTipHeight.Value = nMax;

                if (ts.IsOn)
                {                   
                    tsPencil.IsOn = false;
                }
            }
        }        


        private int WindowSubClass(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam, IntPtr uIdSubclass, uint dwRefData)
        {
            switch (uMsg)
            {
                case WM_COPYDATA:
                    {                       
                        var cds = (COPYDATASTRUCT)Marshal.PtrToStructure(lParam, typeof(COPYDATASTRUCT))!;
                        if (cds.dwData == (IntPtr)COPYDATA_GET_DRAWINGATTRUBUTES)
                        {
                            var dai = (DrawingAttributesInfo)Marshal.PtrToStructure(cds.lpData, typeof(DrawingAttributesInfo))!;
                            OnDrawingAttributesReceived?.Invoke(dai);
                        }
                        if (cds.dwData == (IntPtr)COPYDATA_GET_RECOGNIZEDTEXT)
                        {
                            string sReceivedText = Marshal.PtrToStringUni((IntPtr)cds.lpData, cds.cbData / sizeof(char));                                                      
                           
                            //tbRecognizedText.Focus(FocusState.Programmatic);
                         
                            tbRecognizedText.Text = sReceivedText;
                         
                            //this.DispatcherQueue.TryEnqueue(() =>
                            //{
                            //    svText.UpdateLayout();
                            //    svText.ChangeView(null, svText.ScrollableHeight, null);
                            //});
                        }
                        return 1;
                    }
                    break;
            }
            return DefSubclassProc(hWnd, uMsg, wParam, lParam);
        }

        void SendInkAttributes(IntPtr hWndCppTarget, DrawingAttributesInfo attrs)
        {
            int nSize = Marshal.SizeOf(attrs);
            IntPtr pMem = Marshal.AllocHGlobal(nSize);
            Marshal.StructureToPtr(attrs, pMem, false);

            COPYDATASTRUCT cds = new()
            {
                dwData = (IntPtr)COPYDATA_SET_DRAWINGATTRUBUTES,
                cbData = nSize,
                lpData = pMem
            };

            IntPtr pCDS = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(COPYDATASTRUCT)));
            Marshal.StructureToPtr(cds, pCDS, false);

            SendMessage(hWndCppTarget, WM_COPYDATA, IntPtr.Zero, pCDS);

            Marshal.FreeHGlobal(pMem);
            Marshal.FreeHGlobal(pCDS);
        }

        private async void RectInkDrawingColor_Tapped(object sender, TappedRoutedEventArgs e)
        {
            var rectangle = ((Microsoft.UI.Xaml.Shapes.Rectangle)sender);
            var picker = new ColorPicker
            {
                IsAlphaEnabled = true,
                Color = ((SolidColorBrush)rectangle.Fill).Color,               
                //Margin = new Thickness(-300, 0, 0, 0)
            };

            var scrollViewer = new ScrollViewer
            {
                Content = picker,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto
            };

            var dialog = new ContentDialog
            {
                Title = "Choose a Color",
                Content = scrollViewer,
                PrimaryButtonText = "OK",
                CloseButtonText = "Cancel",
                //FullSizeDesired = true,
                //Height = 1000,
                //Width = 1000,
                XamlRoot = this.Content.XamlRoot
            };

            cp1.Hide(true);            
            var result = await dialog.ShowAsync();
            if (result == ContentDialogResult.Primary)
            {
                rectangle.Fill = new SolidColorBrush(picker.Color);
            }            
            cp1.Show(true);
        }
    }
}
