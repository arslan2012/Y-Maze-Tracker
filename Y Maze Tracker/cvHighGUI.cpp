/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of Intel Corporation may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "cvHighGUI.h"
#include "resource.h"

#include <opencv2/core/utils/logger.hpp>
#include <opencv2/core/utils/trace.hpp>
#include <opencv2/core/opengl.hpp>

using namespace cv;

#include <windowsx.h> // required for GET_X_LPARAM() and GET_Y_LPARAM() macros

#ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wmissing-declarations"
#endif

#if (_WIN32_IE < 0x0500)
#pragma message("WARNING: Win32 UI needs to be compiled with _WIN32_IE >= 0x0500 (_WIN32_IE_IE50)")
#define _WIN32_IE 0x0500
#endif

#include <commctrl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <map>
#include <memory>
#include <algorithm>
#include <vector>
#include <functional>
#include <GL/gl.h>

#define icvGetWindowLongPtr GetWindowLongPtr
#define icvSetWindowLongPtr(hwnd, id, ptr) SetWindowLongPtr(hwnd, id, (LONG_PTR)(ptr))
#define icvGetClassLongPtr  GetClassLongPtr

#define CV_USERDATA GWLP_USERDATA
#define CV_WNDPROC GWLP_WNDPROC
#define CV_HCURSOR GCLP_HCURSOR
#define CV_HBRBACKGROUND GCLP_HBRBACKGROUND

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

Mutex& getWindowMutex() {
    static Mutex* g_window_mutex = new Mutex();
    return *g_window_mutex;
}

static void FillBitmapInfo(BITMAPINFO* bmi, int width, int height, int bpp, int origin) {
    CV_Assert(bmi && width >= 0 && height >= 0 && (bpp == 8 || bpp == 24 || bpp == 32));

    BITMAPINFOHEADER* bmih = &(bmi->bmiHeader);

    memset(bmih, 0, sizeof(*bmih));
    bmih->biSize = sizeof(BITMAPINFOHEADER);
    bmih->biWidth = width;
    bmih->biHeight = origin ? abs(height) : -abs(height);
    bmih->biPlanes = 1;
    bmih->biBitCount = (unsigned short)bpp;
    bmih->biCompression = BI_RGB;

    if (bpp == 8) {
        RGBQUAD* palette = bmi->bmiColors;
        int i;
        for (i = 0; i < 256; i++) {
            palette[i].rgbBlue = palette[i].rgbGreen = palette[i].rgbRed = (BYTE)i;
            palette[i].rgbReserved = 0;
        }
    }
}


struct CvWindow : public std::enable_shared_from_this<CvWindow> {
    CvWindow(const LPCWSTR& name_)
        : signature(CV_WINDOW_MAGIC_VAL)
        , name(name_) {
        // nothing
    }

    ~CvWindow() {
        signature = -1;
    }

    void destroy();

    int signature;
    cv::Mutex mutex;
    HWND hwnd = 0;
    LPCWSTR name;
    HWND frame = 0;

    HDC dc = 0;
    HGDIOBJ image = 0;
    int last_key = 0;
    int flags = 0;
    int status = 0;//0 normal, 1 fullscreen (YV)

    CvMouseCallback on_mouse = nullptr;
    void* on_mouse_param = nullptr;

    struct {
        HWND toolbar = 0;
        int pos = 0;
        int rows = 0;
        WNDPROC toolBarProc = nullptr;
    }
    toolbar;

    int width = -1;
    int height = -1;

    // OpenGL support
    bool useGl = false;
    HGLRC hGLRC = 0;

    CvOpenGlDrawCallback glDrawCallback = nullptr;
    void* glDrawData = nullptr;
};

#define HG_BUDDY_WIDTH  130

#ifndef TBIF_SIZE
#define TBIF_SIZE  0x40
#endif

#ifndef TB_SETBUTTONINFO
#define TB_SETBUTTONINFO (WM_USER + 66)
#endif

#ifndef TBM_GETTOOLTIPS
#define TBM_GETTOOLTIPS  (WM_USER + 30)
#endif

static
std::vector< std::shared_ptr<CvWindow> >& getWindowsList() {
    static std::vector< std::shared_ptr<CvWindow> > g_windows;
    return g_windows;
}


// Mutex must be locked
static
std::shared_ptr<CvWindow> icvFindWindowByName(const LPCWSTR& name) {
    auto& g_windows = getWindowsList();
    for (auto it = g_windows.begin(); it != g_windows.end(); ++it) {
        auto window = *it;
        if (!window)
            continue;
        if (window->name == name)
            return window;
    }
    return std::shared_ptr<CvWindow>();
}

// Mutex must be locked
static
std::shared_ptr<CvWindow> icvFindWindowByHandle(HWND hwnd) {
    auto& g_windows = getWindowsList();
    for (auto it = g_windows.begin(); it != g_windows.end(); ++it) {
        auto window = *it;
        if (!window)
            continue;
        if (window->hwnd == hwnd || window->frame == hwnd)
            return window;
    }
    return std::shared_ptr<CvWindow>();
}


static LRESULT CALLBACK HighGUIProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static void icvUpdateWindowPos(CvWindow& window);

typedef int (CV_CDECL* CvWin32WindowCallback)(HWND, UINT, WPARAM, LPARAM, int*);
static CvWin32WindowCallback hg_on_preprocess = 0, hg_on_postprocess = 0;
static HINSTANCE hg_hinstance = 0;

static const LPCWSTR const highGUIclassName = L"HighGUI class";
static const LPCWSTR const mainHighGUIclassName = L"Main HighGUI class";

static void icvCleanupHighgui() {
    cvDestroyAllWindows();
    UnregisterClass(highGUIclassName, hg_hinstance);
    UnregisterClass(mainHighGUIclassName, hg_hinstance);
}

int cvInitSystem(HINSTANCE hInstance) {
    hg_hinstance = hInstance;
    static int wasInitialized = 0;

    // check initialization status
    if (!wasInitialized) {
        (void)getWindowMutex();  // force mutex initialization
        (void)getWindowsList();  // Initialize the storage

        // Register the class
        WNDCLASS wndc;
        wndc.style = CS_OWNDC | CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
        wndc.lpfnWndProc = WindowProc;
        wndc.cbClsExtra = 0;
        wndc.cbWndExtra = 0;
        wndc.hInstance = hg_hinstance;
        wndc.lpszClassName = highGUIclassName;
        wndc.lpszMenuName = highGUIclassName;
        wndc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_YMAZETRACKER));
        wndc.hCursor = (HCURSOR)LoadCursor(0, (LPCWSTR)(size_t)IDC_CROSS);
        wndc.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);

        RegisterClass(&wndc);

        wndc.lpszClassName = mainHighGUIclassName;
        wndc.lpszMenuName = mainHighGUIclassName;
        wndc.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
        wndc.lpfnWndProc = MainWindowProc;

        RegisterClass(&wndc);
        atexit(icvCleanupHighgui);

        wasInitialized = 1;
    }

    setlocale(LC_NUMERIC, "C");  // FIXIT must be removed

    return 0;
}

 int cvStartWindowThread() {
    return 0;
}


static std::shared_ptr<CvWindow> icvWindowByHWND(HWND hwnd) {
    AutoLock lock(getWindowMutex());
    CvWindow* window = (CvWindow*)icvGetWindowLongPtr(hwnd, CV_USERDATA);
    window = window != 0 &&
        window->signature == CV_WINDOW_MAGIC_VAL ? window : 0;
    if (window) {
        return window->shared_from_this();
    } else {
        return std::shared_ptr<CvWindow>();
    }
}


static const LPCWSTR const icvWindowPosRootKey = L"Software\\OpenCV\\HighGUI\\Windows\\";

// Window positions saving/loading added by Philip Gruebele.
//<a href="mailto:pgruebele@cox.net">pgruebele@cox.net</a>
// Restores the window position from the registry saved position.
static void
icvLoadWindowPos(const LPCWSTR name, CvRect& rect) {
    HKEY hkey;
    wchar_t szKey[1024];
    wcscpy_s(szKey, 1024, icvWindowPosRootKey);
    wcscat_s(szKey, 1024, name);

    rect.x = rect.y = CW_USEDEFAULT;
    rect.width = rect.height = 320;

    if (RegOpenKeyEx(HKEY_CURRENT_USER, szKey, 0, KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS) {
        // Yes we are installed.
        DWORD dwType = 0;
        DWORD dwSize = sizeof(int);

        RegQueryValueEx(hkey, L"Left", NULL, &dwType, (BYTE*)&rect.x, &dwSize);
        RegQueryValueEx(hkey, L"Top", NULL, &dwType, (BYTE*)&rect.y, &dwSize);
        RegQueryValueEx(hkey, L"Width", NULL, &dwType, (BYTE*)&rect.width, &dwSize);
        RegQueryValueEx(hkey, L"Height", NULL, &dwType, (BYTE*)&rect.height, &dwSize);

        // Snap rect into closest monitor in case it falls outside it. // Adi Shavit
        // set WIN32 RECT to be the loaded size
        POINT tl_w32 = { rect.x, rect.y };
        POINT tr_w32 = { rect.x + rect.width, rect.y };

        // find monitor containing top-left and top-right corners, or NULL
        HMONITOR hMonitor_l = MonitorFromPoint(tl_w32, MONITOR_DEFAULTTONULL);
        HMONITOR hMonitor_r = MonitorFromPoint(tr_w32, MONITOR_DEFAULTTONULL);

        // if neither are contained - the move window to origin of closest.
        if (NULL == hMonitor_l && NULL == hMonitor_r) {
            // find monitor nearest to top-left corner
            HMONITOR hMonitor_closest = MonitorFromPoint(tl_w32, MONITOR_DEFAULTTONEAREST);

            // get coordinates of nearest monitor
            MONITORINFO mi;
            mi.cbSize = sizeof(mi);
            GetMonitorInfo(hMonitor_closest, &mi);

            rect.x = mi.rcWork.left;
            rect.y = mi.rcWork.top;
        }

        if (rect.width != (int)CW_USEDEFAULT && (rect.width < 0 || rect.width > 3000))
            rect.width = 100;
        if (rect.height != (int)CW_USEDEFAULT && (rect.height < 0 || rect.height > 3000))
            rect.height = 100;

        RegCloseKey(hkey);
    }
}


// Window positions saving/loading added by Philip Gruebele.
//<a href="mailto:pgruebele@cox.net">pgruebele@cox.net</a>
// philipg.  Saves the window position in the registry
static void
icvSaveWindowPos(const wchar_t* name, CvRect rect) {
    static const DWORD MAX_RECORD_COUNT = 100;
    HKEY hkey;
    wchar_t szKey[1024];
    wchar_t rootKey[1024];
    wcscpy_s(szKey, 1024, icvWindowPosRootKey);
    wcscat_s(szKey, 1024, name);

    if (RegOpenKeyEx(HKEY_CURRENT_USER, szKey, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
        HKEY hroot;
        DWORD count = 0;
        FILETIME oldestTime = { UINT_MAX, UINT_MAX };
        wchar_t oldestKey[1024];
        wchar_t currentKey[1024];

        wcscpy_s(rootKey, 1024, icvWindowPosRootKey);
        rootKey[wcslen(rootKey) - 1] = '\0';
        if (RegCreateKeyEx(HKEY_CURRENT_USER, rootKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ + KEY_WRITE, 0, &hroot, NULL) != ERROR_SUCCESS)
            //RegOpenKeyEx(HKEY_CURRENT_USER,rootKey,0,KEY_READ,&hroot) != ERROR_SUCCESS)
            return;

        for (;;) {
            DWORD csize = sizeof(currentKey);
            FILETIME accesstime = { 0, 0 };
            LONG code = RegEnumKeyEx(hroot, count, currentKey, &csize, NULL, NULL, NULL, &accesstime);
            if (code != ERROR_SUCCESS && code != ERROR_MORE_DATA)
                break;
            count++;
            if (oldestTime.dwHighDateTime > accesstime.dwHighDateTime ||
                (oldestTime.dwHighDateTime == accesstime.dwHighDateTime &&
                    oldestTime.dwLowDateTime > accesstime.dwLowDateTime)) {
                oldestTime = accesstime;
                wcscpy_s(oldestKey, 1024, currentKey);
            }
        }

        if (count >= MAX_RECORD_COUNT)
            RegDeleteKey(hroot, oldestKey);
        RegCloseKey(hroot);

        if (RegCreateKeyEx(HKEY_CURRENT_USER, szKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &hkey, NULL) != ERROR_SUCCESS)
            return;
    } else {
        RegCloseKey(hkey);
        if (RegOpenKeyEx(HKEY_CURRENT_USER, szKey, 0, KEY_WRITE, &hkey) != ERROR_SUCCESS)
            return;
    }

    RegSetValueEx(hkey, L"Left", 0, REG_DWORD, (BYTE*)&rect.x, sizeof(rect.x));
    RegSetValueEx(hkey, L"Top", 0, REG_DWORD, (BYTE*)&rect.y, sizeof(rect.y));
    RegSetValueEx(hkey, L"Width", 0, REG_DWORD, (BYTE*)&rect.width, sizeof(rect.width));
    RegSetValueEx(hkey, L"Height", 0, REG_DWORD, (BYTE*)&rect.height, sizeof(rect.height));
    RegCloseKey(hkey);
}

static Rect getImageRect_(CvWindow& window);

CvRect cvGetWindowRect_W32(const wchar_t* name) {
    CV_FUNCNAME("cvGetWindowRect_W32");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    Rect r = getImageRect_(*window);

    CvRect result = cvRect(r.x, r.y, r.width, r.height);
    return result;
}

static Rect getImageRect_(CvWindow& window) {
    RECT rect = { 0 };
    GetClientRect(window.hwnd, &rect);
    POINT pt = { rect.left, rect.top };
    ClientToScreen(window.hwnd, &pt);
    Rect result(pt.x, pt.y, rect.right - rect.left, rect.bottom - rect.top);
    return result;
}

double cvGetModeWindow_W32(const wchar_t* name)//YV
{
    CV_FUNCNAME("cvGetModeWindow_W32");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    return window->status;
}

static bool setModeWindow_(CvWindow& window, int mode);

void cvSetModeWindow_W32(const wchar_t* name, double prop_value)//Yannick Verdie
{
    CV_FUNCNAME("cvSetModeWindow_W32");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    (void)setModeWindow_(*window, (int)prop_value);
}

static bool setModeWindow_(CvWindow& window, int mode) {
    if (window.flags & CV_WINDOW_AUTOSIZE)//if the flag CV_WINDOW_AUTOSIZE is set
        return false;

    if (window.status == mode)
        return true;

    {
        DWORD dwStyle = (DWORD)GetWindowLongPtr(window.frame, GWL_STYLE);
        CvRect position;

        if (window.status == CV_WINDOW_FULLSCREEN && mode == CV_WINDOW_NORMAL) {
            icvLoadWindowPos(window.name, position);
            SetWindowLongPtr(window.frame, GWL_STYLE, dwStyle | WS_CAPTION | WS_THICKFRAME);

            SetWindowPos(window.frame, HWND_TOP, position.x, position.y, position.width, position.height, SWP_NOZORDER | SWP_FRAMECHANGED);
            window.status = CV_WINDOW_NORMAL;

            return true;
        }

        if (window.status == CV_WINDOW_NORMAL && mode == CV_WINDOW_FULLSCREEN) {
            //save dimension
            RECT rect = { 0 };
            GetWindowRect(window.frame, &rect);
            CvRect rectCV = cvRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
            icvSaveWindowPos(window.name, rectCV);

            //Look at coordinate for fullscreen
            HMONITOR hMonitor;
            MONITORINFO mi;
            hMonitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);

            mi.cbSize = sizeof(mi);
            GetMonitorInfo(hMonitor, &mi);

            //fullscreen
            position.x = mi.rcMonitor.left; position.y = mi.rcMonitor.top;
            position.width = mi.rcMonitor.right - mi.rcMonitor.left; position.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
            SetWindowLongPtr(window.frame, GWL_STYLE, dwStyle & ~WS_CAPTION & ~WS_THICKFRAME);

            SetWindowPos(window.frame, HWND_TOP, position.x, position.y, position.width, position.height, SWP_NOZORDER | SWP_FRAMECHANGED);
            window.status = CV_WINDOW_FULLSCREEN;

            return true;
        }
    }

    return false;
}

static double getPropTopmost_(CvWindow& window);

double cvGetPropTopmost_W32(const wchar_t* name) {
    CV_Assert(name);

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error(Error::StsNullPtr, "NULL window");

    return getPropTopmost_(*window);
}

static double getPropTopmost_(CvWindow& window) {
    LONG style = GetWindowLongA(window.frame, GWL_EXSTYLE); // -20
    if (!style) {
        std::wstringstream errorMsg;
        errorMsg << "window(" << window.name << "): failed to retrieve extended window style using GetWindowLongA(); error code: " << GetLastError();
        CV_Error_(Error::StsError, ("%s", errorMsg.str()));
    }

    bool result = (style & WS_EX_TOPMOST) == WS_EX_TOPMOST;
    return result ? 1.0 : 0.0;
}

static bool setPropTopmost_(CvWindow& window, bool topmost);

void cvSetPropTopmost_W32(const wchar_t* name, const bool topmost) {
    CV_Assert(name);

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error(Error::StsNullPtr, "NULL window");

    (void)setPropTopmost_(*window, topmost);
}

static bool setPropTopmost_(CvWindow& window, bool topmost) {
    HWND flag = topmost ? HWND_TOPMOST : HWND_TOP;
    BOOL success = SetWindowPos(window.frame, flag, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    if (!success) {
        std::wstringstream errorMsg;
        errorMsg << "window(" << window.name << "): error reported by SetWindowPos(" << (topmost ? "HWND_TOPMOST" : "HWND_TOP") << "), error code:  " << GetLastError();
        CV_Error_(Error::StsError, ("%s", errorMsg.str()));
        return false;
    }
    return true;
}

static double getPropVsync_(CvWindow& window);

double cvGetPropVsync_W32(const wchar_t* name) {
    if (!name)
        CV_Error(Error::StsNullPtr, "'name' argument must not be NULL");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsBadArg, ("there is no window named '%s'", name));

    double result = getPropVsync_(*window);
    return cvIsNaN(result) ? -1.0 : result;
}

static double getPropVsync_(CvWindow& window) {
    // https://www.khronos.org/opengl/wiki/Swap_Interval
    // https://www.khronos.org/registry/OpenGL/extensions/EXT/WGL_EXT_extensions_string.txt
    // https://www.khronos.org/registry/OpenGL/extensions/EXT/WGL_EXT_swap_control.txt

    if (!wglMakeCurrent(window.dc, window.hGLRC))
        CV_Error(Error::OpenGlApiCallError, "Can't Activate The GL Rendering Context");

    typedef const char* (APIENTRY* PFNWGLGETEXTENSIONSSTRINGEXTPROC)(void);
    PFNWGLGETEXTENSIONSSTRINGEXTPROC wglGetExtensionsString = NULL;
    wglGetExtensionsString = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)wglGetProcAddress("wglGetExtensionsStringEXT");
    if (wglGetExtensionsString == NULL)
        return std::numeric_limits<double>::quiet_NaN(); // wglGetProcAddress failed to get wglGetExtensionsStringEXT

    const char* wgl_extensions = wglGetExtensionsString();
    if (wgl_extensions == NULL)
        return std::numeric_limits<double>::quiet_NaN(); // Can't get WGL extensions string

    if (strstr(wgl_extensions, "WGL_EXT_swap_control") == NULL)
        return std::numeric_limits<double>::quiet_NaN(); // WGL extensions don't contain WGL_EXT_swap_control

    typedef int (APIENTRY* PFNWGLGETSWAPINTERVALPROC)(void);
    PFNWGLGETSWAPINTERVALPROC wglGetSwapInterval = 0;
    wglGetSwapInterval = (PFNWGLGETSWAPINTERVALPROC)wglGetProcAddress("wglGetSwapIntervalEXT");
    if (wglGetSwapInterval == NULL)
        return std::numeric_limits<double>::quiet_NaN(); // wglGetProcAddress failed to get wglGetSwapIntervalEXT

    return wglGetSwapInterval();
}

static bool setPropVsync_(CvWindow& window, bool enable_vsync);

void cvSetPropVsync_W32(const wchar_t* name, const bool enable_vsync) {
    if (!name)
        CV_Error(Error::StsNullPtr, "'name' argument must not be NULL");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsBadArg, ("there is no window named '%s'", name));

    (void)setPropVsync_(*window, enable_vsync);
}

static bool setPropVsync_(CvWindow& window, bool enable_vsync) {
    if (!wglMakeCurrent(window.dc, window.hGLRC))
        CV_Error(Error::OpenGlApiCallError, "Can't Activate The GL Rendering Context");

    typedef const char* (APIENTRY* PFNWGLGETEXTENSIONSSTRINGEXTPROC)(void);
    PFNWGLGETEXTENSIONSSTRINGEXTPROC wglGetExtensionsString = NULL;
    wglGetExtensionsString = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)wglGetProcAddress("wglGetExtensionsStringEXT");
    if (wglGetExtensionsString == NULL)
        CV_Error(Error::OpenGlApiCallError, "wglGetProcAddress failed to get wglGetExtensionsStringEXT");

    const char* wgl_extensions = wglGetExtensionsString();
    if (wgl_extensions == NULL)
        CV_Error(Error::OpenGlApiCallError, "Can't get WGL extensions string");

    if (strstr(wgl_extensions, "WGL_EXT_swap_control") == NULL)
        CV_Error(Error::OpenGlApiCallError, "WGL extensions don't contain WGL_EXT_swap_control");

    typedef BOOL(APIENTRY* PFNWGLSWAPINTERVALPROC)(int);
    PFNWGLSWAPINTERVALPROC wglSwapInterval = 0;
    wglSwapInterval = (PFNWGLSWAPINTERVALPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (wglSwapInterval == NULL)
        CV_Error(Error::OpenGlApiCallError, "wglGetProcAddress failed to get wglSwapIntervalEXT");

    wglSwapInterval(enable_vsync);
    return true;
}

void setWindowTitle_W32(const LPCWSTR& name, const LPCWSTR& title) {
    auto window = icvFindWindowByName(name);

    if (!window) {
        cvNamedWindow(name, CV_WINDOW_AUTOSIZE);
        window = icvFindWindowByName(name);
    }

    if (!window)
        CV_Error(Error::StsNullPtr, "NULL window");

    if (!SetWindowText(window->frame, title))
        CV_Error_(Error::StsError, ("Failed to set \"%s\" window title to \"%s\"", name, title));
}

double cvGetPropWindowAutoSize_W32(const wchar_t* name) {
    double result = -1;

    CV_FUNCNAME("cvSetCloseCallback");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    result = window->flags & CV_WINDOW_AUTOSIZE;

    return result;
}

double cvGetRatioWindow_W32(const wchar_t* name) {
    double result = -1;

    CV_FUNCNAME("cvGetRatioWindow_W32");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    result = static_cast<double>(window->width) / window->height;

    return result;
}

double cvGetOpenGlProp_W32(const wchar_t* name) {
    double result = -1;

    CV_FUNCNAME("cvGetOpenGlProp_W32");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    if (!window)
        return -1;

    result = window->useGl;

    CV_UNUSED(name);

    return result;
}

double cvGetPropVisible_W32(const wchar_t* name) {
    double result = -1;

    CV_FUNCNAME("cvGetPropVisible_W32");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    result = (bool)window ? 1.0 : 0.0;

    return result;
}


// OpenGL support



namespace {
    void createGlContext(HWND hWnd, HDC& hGLDC, HGLRC& hGLRC, bool& useGl) {
        CV_FUNCNAME("createGlContext");

        AutoLock lock(getWindowMutex());

        useGl = false;

        int PixelFormat;

        static PIXELFORMATDESCRIPTOR pfd =
        {
            sizeof(PIXELFORMATDESCRIPTOR), // Size Of This Pixel Format Descriptor
            1,                             // Version Number
            PFD_DRAW_TO_WINDOW |           // Format Must Support Window
            PFD_SUPPORT_OPENGL |           // Format Must Support OpenGL
            PFD_DOUBLEBUFFER,              // Must Support Double Buffering
            PFD_TYPE_RGBA,                 // Request An RGBA Format
            32,                            // Select Our Color Depth
            0, 0, 0, 0, 0, 0,              // Color Bits Ignored
            0,                             // No Alpha Buffer
            0,                             // Shift Bit Ignored
            0,                             // No Accumulation Buffer
            0, 0, 0, 0,                    // Accumulation Bits Ignored
            32,                            // 32 Bit Z-Buffer (Depth Buffer)
            0,                             // No Stencil Buffer
            0,                             // No Auxiliary Buffer
            PFD_MAIN_PLANE,                // Main Drawing Layer
            0,                             // Reserved
            0, 0, 0                        // Layer Masks Ignored
        };

        hGLDC = GetDC(hWnd);
        if (!hGLDC)
            CV_Error(Error::OpenGlApiCallError, "Can't Create A GL Device Context");

        PixelFormat = ChoosePixelFormat(hGLDC, &pfd);
        if (!PixelFormat)
            CV_Error(Error::OpenGlApiCallError, "Can't Find A Suitable PixelFormat");

        if (!SetPixelFormat(hGLDC, PixelFormat, &pfd))
            CV_Error(Error::OpenGlApiCallError, "Can't Set The PixelFormat");

        hGLRC = wglCreateContext(hGLDC);
        if (!hGLRC)
            CV_Error(Error::OpenGlApiCallError, "Can't Create A GL Rendering Context");

        if (!wglMakeCurrent(hGLDC, hGLRC))
            CV_Error(Error::OpenGlApiCallError, "Can't Activate The GL Rendering Context");

        useGl = true;
    }

    void releaseGlContext(CvWindow& window) {
        //CV_FUNCNAME("releaseGlContext");

        AutoLock lock(getWindowMutex());

        if (window.hGLRC) {
            wglDeleteContext(window.hGLRC);
            window.hGLRC = NULL;
        }

        if (window.dc) {
            ReleaseDC(window.hwnd, window.dc);
            window.dc = NULL;
        }

        window.useGl = false;
    }

    void drawGl(CvWindow& window) {
        CV_FUNCNAME("drawGl");

        AutoLock lock(getWindowMutex());

        if (!wglMakeCurrent(window.dc, window.hGLRC))
            CV_Error(Error::OpenGlApiCallError, "Can't Activate The GL Rendering Context");

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (window.glDrawCallback)
            window.glDrawCallback(window.glDrawData);

        if (!SwapBuffers(window.dc))
            CV_Error(Error::OpenGlApiCallError, "Can't swap OpenGL buffers");
    }

    void resizeGl(CvWindow& window) {
        CV_FUNCNAME("resizeGl");

        AutoLock lock(getWindowMutex());

        if (!wglMakeCurrent(window.dc, window.hGLRC))
            CV_Error(Error::OpenGlApiCallError, "Can't Activate The GL Rendering Context");

        glViewport(0, 0, window.width, window.height);
    }
}

static std::shared_ptr<CvWindow> namedWindow_(const LPCWSTR& name, int flags);

int cvNamedWindow(const wchar_t* name, int flags) {
    CV_FUNCNAME("cvNamedWindow");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    // Check the name in the storage
    auto window = icvFindWindowByName(name);
    if (window) {
        return 1;
    }

    window = namedWindow_(name, flags);
    return (bool)window;
}

static std::shared_ptr<CvWindow> namedWindow_(const LPCWSTR& name, int flags) {
    AutoLock lock(getWindowMutex());

    HWND hWnd, mainhWnd;
    DWORD defStyle = WS_VISIBLE | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;

    bool useGl;
    HDC hGLDC;
    HGLRC hGLRC;

    CvRect rect;
    icvLoadWindowPos(name, rect);

    if (!(flags & CV_WINDOW_AUTOSIZE))//YV add border in order to resize the window
        defStyle |= WS_SIZEBOX;

    if (flags & CV_WINDOW_OPENGL)
        defStyle |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

    mainhWnd = CreateWindow(mainHighGUIclassName, name, defStyle | WS_OVERLAPPED,
        rect.x, rect.y, rect.width, rect.height, 0, 0, hg_hinstance, 0);
    if (!mainhWnd)
        CV_Error_(Error::StsError, ("Frame window can not be created: '%s'", name));

    ShowWindow(mainhWnd, SW_SHOW);

    //YV- remove one border by changing the style
    hWnd = CreateWindow(highGUIclassName, L"", (defStyle & ~WS_SIZEBOX) | WS_CHILD, CW_USEDEFAULT, 0, rect.width, rect.height, mainhWnd, 0, hg_hinstance, 0);
    if (!hWnd)
        CV_Error(Error::StsError, "Frame window can not be created");
    useGl = false;
    hGLDC = 0;
    hGLRC = 0;
    createGlContext(hWnd, hGLDC, hGLRC, useGl);

    ShowWindow(hWnd, SW_SHOW);

    auto window = std::make_shared<CvWindow>(name);

    window->hwnd = hWnd;
    window->frame = mainhWnd;
    window->flags = flags;
    window->image = 0;

    if (!useGl) {
        window->dc = CreateCompatibleDC(0);
        window->hGLRC = 0;
        window->useGl = false;
    } else {
        window->dc = hGLDC;
        window->hGLRC = hGLRC;
        window->useGl = true;
    }

    window->glDrawCallback = 0;
    window->glDrawData = 0;

    window->last_key = 0;
    window->status = CV_WINDOW_NORMAL;//YV

    window->on_mouse = 0;
    window->on_mouse_param = 0;

    icvSetWindowLongPtr(hWnd, CV_USERDATA, window.get());
    icvSetWindowLongPtr(mainhWnd, CV_USERDATA, window.get());

    auto& g_windows = getWindowsList();
    g_windows.push_back(window);

    // Recalculate window pos
    icvUpdateWindowPos(*window);

    return window;
}

 void cvSetOpenGlContext(const wchar_t* name) {
    CV_FUNCNAME("cvSetOpenGlContext");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    if (!window->useGl)
        CV_Error(Error::OpenGlNotSupported, "Window doesn't support OpenGL");

    if (!wglMakeCurrent(window->dc, window->hGLRC))
        CV_Error(Error::OpenGlApiCallError, "Can't Activate The GL Rendering Context");
}

 void cvUpdateWindow(const wchar_t* name) {
    CV_FUNCNAME("cvUpdateWindow");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    InvalidateRect(window->hwnd, 0, 0);
}

 void cvSetOpenGlDrawCallback(const wchar_t* name, CvOpenGlDrawCallback callback, void* userdata) {
    CV_FUNCNAME("cvCreateOpenGLCallback");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    if (!window->useGl)
        CV_Error(Error::OpenGlNotSupported, "Window was created without OpenGL context");

    window->glDrawCallback = callback;
    window->glDrawData = userdata;
}


static void icvRemoveWindow(const std::shared_ptr<CvWindow>& window_) {
    CV_Assert(window_);
    AutoLock lock(getWindowMutex());
    CvWindow& window = *window_;

    RECT wrect = { 0,0,0,0 };

    auto& g_windows = getWindowsList();
    for (auto it = g_windows.begin(); it != g_windows.end(); ++it) {
        const std::shared_ptr<CvWindow>& w = *it;
        if (w.get() == &window) {
            g_windows.erase(it);
            break;
        }
    }

    if (window.useGl)
        releaseGlContext(window);

    if (window.frame)
        GetWindowRect(window.frame, &wrect);
    icvSaveWindowPos(window.name, cvRect(wrect.left, wrect.top, wrect.right - wrect.left, wrect.bottom - wrect.top));

    if (window.hwnd)
        icvSetWindowLongPtr(window.hwnd, CV_USERDATA, 0);
    if (window.frame)
        icvSetWindowLongPtr(window.frame, CV_USERDATA, 0);

    if (window.toolbar.toolbar)
        icvSetWindowLongPtr(window.toolbar.toolbar, CV_USERDATA, 0);

    if (window.dc && window.image)
        DeleteObject(SelectObject(window.dc, window.image));

    if (window.dc)
        DeleteDC(window.dc);
}


void cvDestroyWindow(const wchar_t* name) {
    CV_FUNCNAME("cvDestroyWindow");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name string");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    window->destroy();
}


void CvWindow::destroy() {
    SendMessage(hwnd, WM_CLOSE, 0, 0);
    SendMessage(frame, WM_CLOSE, 0, 0);
    // Do NOT call _remove_window -- CvWindow list will be updated automatically ...
}

static void icvScreenToClient(HWND hwnd, RECT* rect) {
    POINT p;
    p.x = rect->left;
    p.y = rect->top;
    ScreenToClient(hwnd, &p);
    OffsetRect(rect, p.x - rect->left, p.y - rect->top);
}


/* Calculatess the window coordinates relative to the upper left corner of the mainhWnd window */
static RECT icvCalcWindowRect(CvWindow& window) {
    RECT crect = { 0 }, trect = { 0 }, rect = { 0 };

    GetClientRect(window.frame, &crect);
    if (window.toolbar.toolbar) {
        GetWindowRect(window.toolbar.toolbar, &trect);
        icvScreenToClient(window.frame, &trect);
        SubtractRect(&rect, &crect, &trect);
    } else
        rect = crect;

    return rect;
}
static inline RECT icvCalcWindowRect(CvWindow* window) { CV_Assert(window); return icvCalcWindowRect(*window); }


// returns FALSE if there is a problem such as ERROR_IO_PENDING.
static bool icvGetBitmapData(CvWindow& window, SIZE& size, int& channels, void*& data) {
    GdiFlush();

    HGDIOBJ h = GetCurrentObject(window.dc, OBJ_BITMAP);
    size.cx = size.cy = 0;
    data = 0;

    if (h == NULL)
        return false;

    BITMAP bmp = {};
    if (GetObject(h, sizeof(bmp), &bmp) == 0)
        return false;

    size.cx = abs(bmp.bmWidth);
    size.cy = abs(bmp.bmHeight);

    channels = bmp.bmBitsPixel / 8;

    data = bmp.bmBits;

    return true;
}
static bool icvGetBitmapData(CvWindow& window, SIZE& size) {
    int channels = 0;
    void* data = nullptr;
    return icvGetBitmapData(window, size, channels, data);
}


static void icvUpdateWindowPos(CvWindow& window) {
    RECT rect = { 0 };

    if ((window.flags & CV_WINDOW_AUTOSIZE) && window.image) {
        int i;
        SIZE size = { 0,0 };
        icvGetBitmapData(window, size);  // TODO check return value?

        // Repeat two times because after the first resizing of the mainhWnd window
        // toolbar may resize too
        for (i = 0; i < (window.toolbar.toolbar ? 2 : 1); i++) {
            RECT rmw = { 0 }, rw = icvCalcWindowRect(&window);
            MoveWindow(window.hwnd, rw.left, rw.top,
                rw.right - rw.left, rw.bottom - rw.top, FALSE);
            GetClientRect(window.hwnd, &rw);
            GetWindowRect(window.frame, &rmw);
            // Resize the mainhWnd window in order to make the bitmap fit into the child window
            MoveWindow(window.frame, rmw.left, rmw.top,
                size.cx + (rmw.right - rmw.left) - (rw.right - rw.left),
                size.cy + (rmw.bottom - rmw.top) - (rw.bottom - rw.top), TRUE);
        }
    }

    rect = icvCalcWindowRect(window);
    MoveWindow(window.hwnd, rect.left, rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top, TRUE);
}

namespace {
    std::map<LPCWSTR, cv::ogl::Texture2D> wndTexs;
    std::map<LPCWSTR, cv::ogl::Texture2D> ownWndTexs;
    std::map<LPCWSTR, cv::ogl::Buffer> ownWndBufs;

    void glDrawTextureCallback(void* userdata) {
        cv::ogl::Texture2D* texObj = static_cast<cv::ogl::Texture2D*>(userdata);

        cv::ogl::render(*texObj);
    }
}

static void showImage_(CvWindow& window, const Mat& image);

void cvShowImage(const wchar_t* name, const Mat& image) {
    CV_FUNCNAME("cvShowImage");

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name");

    std::shared_ptr<CvWindow> window;
    {
        AutoLock lock(getWindowMutex());

        window = icvFindWindowByName(name);
        if (!window) {
            cvNamedWindow(name, CV_WINDOW_AUTOSIZE);
            window = icvFindWindowByName(name);
        }
    }

    if (!window)
        return; // keep silence here.
    if (window->useGl) {
        cvSetOpenGlContext(name);

        cv::ogl::Texture2D& tex = ownWndTexs[name];
        tex.copyFrom(image);
        tex.setAutoRelease(false);

        cvSetOpenGlDrawCallback(name, glDrawTextureCallback, &tex);

        cvUpdateWindow(name);
        return;
    }
    return showImage_(*window, image);
}

static void showImage_(CvWindow& window, const Mat& image) {
    AutoLock lock(window.mutex);

    SIZE size = { 0, 0 };
    int channels = 0;
    void* dst_ptr = 0;
    const int channels0 = 3;
    bool changed_size = false; // philipg

    if (window.image) {
        // if there is something wrong with these system calls, we cannot display image...
        if (!icvGetBitmapData(window, size, channels, dst_ptr))
            return;
    }

    if (size.cx != image.cols || size.cy != image.rows || channels != channels0) {
        changed_size = true;

        uchar buffer[sizeof(BITMAPINFO) + 255 * sizeof(RGBQUAD)];
        BITMAPINFO* binfo = (BITMAPINFO*)buffer;

        DeleteObject(SelectObject(window.dc, window.image));
        window.image = 0;

        size.cx = image.cols;
        size.cy = image.rows;
        channels = channels0;

        FillBitmapInfo(binfo, size.cx, size.cy, channels * 8, 1);

        window.image = SelectObject(window.dc,
            CreateDIBSection(window.dc, binfo, DIB_RGB_COLORS, &dst_ptr, 0, 0)
        );
    }

    {
        cv::Mat dst(size.cy, size.cx, CV_8UC3, dst_ptr, (size.cx * channels + 3) & -4);
        convertToShow(image, dst, false);
        CV_Assert(dst.data == (uchar*)dst_ptr);
        cv::flip(dst, dst, 0);
    }

    // only resize window if needed
    if (changed_size)
        icvUpdateWindowPos(window);
    InvalidateRect(window.hwnd, 0, 0);
    // philipg: this is not needed and just slows things down
    //    UpdateWindow(window->hwnd);
}

static void resizeWindow_(CvWindow& window, const Size& size);

void cvResizeWindow(const wchar_t* name, int width, int height) {
    CV_FUNCNAME("cvResizeWindow");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    return resizeWindow_(*window, Size(width, height));
}

static void resizeWindow_(CvWindow& window, const Size& size) {
    RECT rmw = { 0 }, rw = { 0 }, rect = { 0 };

    // Repeat two times because after the first resizing of the mainhWnd window
    // toolbar may resize too
    for (int i = 0; i < (window.toolbar.toolbar ? 2 : 1); i++) {
        rw = icvCalcWindowRect(window);
        MoveWindow(window.hwnd, rw.left, rw.top,
            rw.right - rw.left, rw.bottom - rw.top, FALSE);
        GetClientRect(window.hwnd, &rw);
        GetWindowRect(window.frame, &rmw);
        // Resize the mainhWnd window in order to make the bitmap fit into the child window
        MoveWindow(window.frame, rmw.left, rmw.top,
            size.width + (rmw.right - rmw.left) - (rw.right - rw.left),
            size.height + (rmw.bottom - rmw.top) - (rw.bottom - rw.top), TRUE);
    }

    rect = icvCalcWindowRect(window);
    MoveWindow(window.hwnd, rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top, TRUE);
}

static void moveWindow_(CvWindow& window, const Point& pt);

void cvMoveWindow(const wchar_t* name, int x, int y) {
    CV_FUNCNAME("cvMoveWindow");

    AutoLock lock(getWindowMutex());

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL name");

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    (void)moveWindow_(*window, Point(x, y));
}

static void moveWindow_(CvWindow& window, const Point& pt) {
    RECT rect = { 0 };
    GetWindowRect(window.frame, &rect);  // TODO check return value
    MoveWindow(window.frame, pt.x, pt.y, rect.right - rect.left, rect.bottom - rect.top, TRUE);
}


static LRESULT CALLBACK
MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto window_ = icvWindowByHWND(hwnd);
    if (!window_)
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

    CvWindow& window = *window_;

    switch (uMsg) {
    case WM_COPY:
        ::SendMessage(window.hwnd, uMsg, wParam, lParam);
        break;

    case WM_DESTROY:

        icvRemoveWindow(window_);
        // Do nothing!!!
        //PostQuitMessage(0);
        break;

    case WM_GETMINMAXINFO:
        if (!(window.flags & CV_WINDOW_AUTOSIZE)) {
            MINMAXINFO* minmax = (MINMAXINFO*)lParam;
            RECT rect = { 0 };
            LRESULT retval = DefWindowProc(hwnd, uMsg, wParam, lParam);

            minmax->ptMinTrackSize.y = 100;
            minmax->ptMinTrackSize.x = 100;
            return retval;
        }
        break;

    case WM_WINDOWPOSCHANGED:
    {
        WINDOWPOS* pos = (WINDOWPOS*)lParam;

        // Update the toolbar pos/size
        if (window.toolbar.toolbar) {
            RECT rect = { 0 };
            GetWindowRect(window.toolbar.toolbar, &rect);
            MoveWindow(window.toolbar.toolbar, 0, 0, pos->cx, rect.bottom - rect.top, TRUE);
        }

        if (!(window.flags & CV_WINDOW_AUTOSIZE))
            icvUpdateWindowPos(window);

        break;
    }

    case WM_WINDOWPOSCHANGING:
    {
        // Snap window to screen edges with multi-monitor support. // Adi Shavit
        LPWINDOWPOS pos = (LPWINDOWPOS)lParam;

        RECT rect = { 0 };
        GetWindowRect(window.frame, &rect);

        HMONITOR hMonitor;
        hMonitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);

        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(hMonitor, &mi);

        const int SNAP_DISTANCE = 15;

        if (abs(pos->x - mi.rcMonitor.left) <= SNAP_DISTANCE)
            pos->x = mi.rcMonitor.left;               // snap to left edge
        else
            if (abs(pos->x + pos->cx - mi.rcMonitor.right) <= SNAP_DISTANCE)
                pos->x = mi.rcMonitor.right - pos->cx; // snap to right edge

        if (abs(pos->y - mi.rcMonitor.top) <= SNAP_DISTANCE)
            pos->y = mi.rcMonitor.top;                 // snap to top edge
        else
            if (abs(pos->y + pos->cy - mi.rcMonitor.bottom) <= SNAP_DISTANCE)
                pos->y = mi.rcMonitor.bottom - pos->cy; // snap to bottom edge
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE)
            SetFocus(window.hwnd);
        break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (window.on_mouse) {
            int flags = (wParam & MK_LBUTTON ? CV_EVENT_FLAG_LBUTTON : 0) |
                (wParam & MK_RBUTTON ? CV_EVENT_FLAG_RBUTTON : 0) |
                (wParam & MK_MBUTTON ? CV_EVENT_FLAG_MBUTTON : 0) |
                (wParam & MK_CONTROL ? CV_EVENT_FLAG_CTRLKEY : 0) |
                (wParam & MK_SHIFT ? CV_EVENT_FLAG_SHIFTKEY : 0) |
                (GetKeyState(VK_MENU) < 0 ? CV_EVENT_FLAG_ALTKEY : 0);
            int event = (uMsg == WM_MOUSEWHEEL ? CV_EVENT_MOUSEWHEEL : CV_EVENT_MOUSEHWHEEL);

            // Set the wheel delta of mouse wheel to be in the upper word of 'event'
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            flags |= (delta << 16);

            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ::ScreenToClient(hwnd, &pt); // Convert screen coordinates to client coordinates.

            RECT rect = { 0 };
            GetClientRect(window.hwnd, &rect);

            SIZE size = { 0,0 };
            if (window.useGl) {
                cv::ogl::Texture2D* texObj = static_cast<cv::ogl::Texture2D*>(window.glDrawData);
                size.cx = texObj->cols();
                size.cy = texObj->rows();
            } else {
                icvGetBitmapData(window, size);
            }

            int x = cvRound((float)pt.x * size.cx / MAX(rect.right - rect.left, 1));
            int y = cvRound((float)pt.y * size.cy / MAX(rect.bottom - rect.top, 1));
            window.on_mouse(event, x, y, flags, window.on_mouse_param);
        }
        break;

    case WM_ERASEBKGND:
    {
        RECT cr = { 0 }, tr = { 0 }, wrc = { 0 };
        HRGN rgn, rgn1, rgn2;
        int ret;
        HDC hdc = (HDC)wParam;
        GetWindowRect(window.hwnd, &cr);
        icvScreenToClient(window.frame, &cr);
        if (window.toolbar.toolbar) {
            GetWindowRect(window.toolbar.toolbar, &tr);
            icvScreenToClient(window.frame, &tr);
        } else
            tr.left = tr.top = tr.right = tr.bottom = 0;

        GetClientRect(window.frame, &wrc);

        rgn = CreateRectRgn(0, 0, wrc.right, wrc.bottom);
        rgn1 = CreateRectRgn(cr.left, cr.top, cr.right, cr.bottom);
        rgn2 = CreateRectRgn(tr.left, tr.top, tr.right, tr.bottom);
        CV_Assert_N(rgn != 0, rgn1 != 0, rgn2 != 0);

        ret = CombineRgn(rgn, rgn, rgn1, RGN_DIFF);
        ret = CombineRgn(rgn, rgn, rgn2, RGN_DIFF);

        if (ret != NULLREGION && ret != ERROR)
            FillRgn(hdc, rgn, (HBRUSH)icvGetClassLongPtr(hwnd, CV_HBRBACKGROUND));

        DeleteObject(rgn);
        DeleteObject(rgn1);
        DeleteObject(rgn2);
    }
    return 1;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


static LRESULT CALLBACK HighGUIProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto window_ = icvWindowByHWND(hwnd);
    if (!window_) {
        // This window is not mentioned in HighGUI storage
        // Actually, this should be error except for the case of calls to CreateWindow
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    CvWindow& window = *window_;

    // Process the message
    switch (uMsg) {
    case WM_COPY:
    {
        if (!::OpenClipboard(hwnd))
            break;

        HDC hDC = 0;
        HDC memDC = 0;
        HBITMAP memBM = 0;

        // We'll use a do-while(0){} scope as a single-run breakable scope
        // Upon any error we can jump out of the single-time while scope to clean up the resources.
        do {
            if (!::EmptyClipboard())
                break;

            if (!window.image)
                break;

            // Get window device context
            if (0 == (hDC = ::GetDC(hwnd)))
                break;

            // Create another DC compatible with hDC
            if (0 == (memDC = ::CreateCompatibleDC(hDC)))
                break;

            // Determine the bitmap's dimensions
            SIZE size = { 0,0 };
            int nchannels = 3;
            void* data = NULL;  // unused
            icvGetBitmapData(window, size, nchannels, data);

            // Create bitmap to draw on and it in the new DC
            if (0 == (memBM = ::CreateCompatibleBitmap(hDC, size.cx, size.cy)))
                break;

            if (!::SelectObject(memDC, memBM))
                break;

            // Begin drawing to DC
            if (!::SetStretchBltMode(memDC, COLORONCOLOR))
                break;

            RGBQUAD table[256];
            if (1 == nchannels) {
                for (int i = 0; i < 256; ++i) {
                    table[i].rgbBlue = (unsigned char)i;
                    table[i].rgbGreen = (unsigned char)i;
                    table[i].rgbRed = (unsigned char)i;
                }
                if (!::SetDIBColorTable(window.dc, 0, 255, table))
                    break;
            }

            // The image copied to the clipboard will be in its original size, regardless if the window itself was resized.

            // Render the image to the dc/bitmap (at original size).
            if (!::BitBlt(memDC, 0, 0, size.cx, size.cy, window.dc, 0, 0, SRCCOPY))
                break;

            // Finally, set bitmap to clipboard
            ::SetClipboardData(CF_BITMAP, memBM);
        } while (0, 0); // (0,0) instead of (0) to avoid MSVC compiler warning C4127: "conditional expression is constant"

        //////////////////////////////////////////////////////////////////////////
        // if handle is allocated (i.e. != 0) then clean-up.
        if (memBM) ::DeleteObject(memBM);
        if (memDC) ::DeleteDC(memDC);
        if (hDC)   ::ReleaseDC(hwnd, hDC);
        ::CloseClipboard();
        break;
    }

    case WM_WINDOWPOSCHANGING:
    {
        LPWINDOWPOS pos = (LPWINDOWPOS)lParam;
        RECT rect = icvCalcWindowRect(window);
        pos->x = rect.left;
        pos->y = rect.top;
        pos->cx = rect.right - rect.left;
        pos->cy = rect.bottom - rect.top;
    }
    break;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
        if (window.on_mouse) {
            POINT pt;

            int flags = (wParam & MK_LBUTTON ? CV_EVENT_FLAG_LBUTTON : 0) |
                (wParam & MK_RBUTTON ? CV_EVENT_FLAG_RBUTTON : 0) |
                (wParam & MK_MBUTTON ? CV_EVENT_FLAG_MBUTTON : 0) |
                (wParam & MK_CONTROL ? CV_EVENT_FLAG_CTRLKEY : 0) |
                (wParam & MK_SHIFT ? CV_EVENT_FLAG_SHIFTKEY : 0) |
                (GetKeyState(VK_MENU) < 0 ? CV_EVENT_FLAG_ALTKEY : 0);
            int event = uMsg == WM_LBUTTONDOWN ? CV_EVENT_LBUTTONDOWN :
                uMsg == WM_RBUTTONDOWN ? CV_EVENT_RBUTTONDOWN :
                uMsg == WM_MBUTTONDOWN ? CV_EVENT_MBUTTONDOWN :
                uMsg == WM_LBUTTONUP ? CV_EVENT_LBUTTONUP :
                uMsg == WM_RBUTTONUP ? CV_EVENT_RBUTTONUP :
                uMsg == WM_MBUTTONUP ? CV_EVENT_MBUTTONUP :
                uMsg == WM_LBUTTONDBLCLK ? CV_EVENT_LBUTTONDBLCLK :
                uMsg == WM_RBUTTONDBLCLK ? CV_EVENT_RBUTTONDBLCLK :
                uMsg == WM_MBUTTONDBLCLK ? CV_EVENT_MBUTTONDBLCLK :
                CV_EVENT_MOUSEMOVE;
            if (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN || uMsg == WM_MBUTTONDOWN)
                SetCapture(hwnd);
            if (uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONUP || uMsg == WM_MBUTTONUP)
                ReleaseCapture();

            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            if (window.flags & CV_WINDOW_AUTOSIZE) {
                // As user can't change window size, do not scale window coordinates. Underlying windowing system
                // may prevent full window from being displayed and in this case coordinates should not be scaled.
                window.on_mouse(event, pt.x, pt.y, flags, window.on_mouse_param);
            } else {
                // Full window is displayed using different size. Scale coordinates to match underlying positions.
                RECT rect = { 0 };
                SIZE size = { 0, 0 };

                GetClientRect(window.hwnd, &rect);

                if (window.useGl) {
                    cv::ogl::Texture2D* texObj = static_cast<cv::ogl::Texture2D*>(window.glDrawData);
                    size.cx = texObj->cols();
                    size.cy = texObj->rows();
                } else {
                    icvGetBitmapData(window, size);
                }

                int x = cvRound((float)pt.x * size.cx / MAX(rect.right - rect.left, 1));
                int y = cvRound((float)pt.y * size.cy / MAX(rect.bottom - rect.top, 1));
                window.on_mouse(event, x, y, flags, window.on_mouse_param);
            }
        }
        break;

    case WM_PAINT:
        if (window.image != 0) {
            int nchannels = 3;
            SIZE size = { 0,0 };
            PAINTSTRUCT paint;
            HDC hdc;
            RGBQUAD table[256];

            // Determine the bitmap's dimensions
            void* data = 0;  // unused
            icvGetBitmapData(window, size, nchannels, data);

            hdc = BeginPaint(hwnd, &paint);
            SetStretchBltMode(hdc, COLORONCOLOR);

            if (nchannels == 1) {
                int i;
                for (i = 0; i < 256; i++) {
                    table[i].rgbBlue = (unsigned char)i;
                    table[i].rgbGreen = (unsigned char)i;
                    table[i].rgbRed = (unsigned char)i;
                }
                SetDIBColorTable(window.dc, 0, 255, table);
            }

            if (window.flags & CV_WINDOW_AUTOSIZE) {
                BitBlt(hdc, 0, 0, size.cx, size.cy, window.dc, 0, 0, SRCCOPY);
            } else {
                RECT rect = { 0 };
                GetClientRect(window.hwnd, &rect);
                StretchBlt(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                    window.dc, 0, 0, size.cx, size.cy, SRCCOPY);
            }
            //DeleteDC(hdc);
            EndPaint(hwnd, &paint);
        }
        else if (window.useGl) {
            drawGl(window);
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        else {
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        return 0;

    case WM_ERASEBKGND:
        if (window.image)
            return 0;
        break;

    case WM_DESTROY:

        icvRemoveWindow(window_);
        // Do nothing!!!
        //PostQuitMessage(0);
        break;

    case WM_SETCURSOR:
        SetCursor((HCURSOR)icvGetClassLongPtr(hwnd, CV_HCURSOR));
        return 0;

    case WM_KEYDOWN:
        window.last_key = (int)wParam;
        return 0;

    case WM_SIZE:
        window.width = LOWORD(lParam);
        window.height = HIWORD(lParam);
        if (window.useGl)
            resizeGl(window);
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    LRESULT ret;

    if (hg_on_preprocess) {
        int was_processed = 0;
        int rethg = hg_on_preprocess(hwnd, uMsg, wParam, lParam, &was_processed);
        if (was_processed)
            return rethg;
    }
    ret = HighGUIProc(hwnd, uMsg, wParam, lParam);

    if (hg_on_postprocess) {
        int was_processed = 0;
        int rethg = hg_on_postprocess(hwnd, uMsg, wParam, lParam, &was_processed);
        if (was_processed)
            return rethg;
    }

    return ret;
}


static LRESULT CALLBACK HGToolbarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto window_ = icvWindowByHWND(hwnd);
    if (!window_)
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

    CvWindow& window = *window_;

    // Control messages processing
    switch (uMsg) {
        // Slider processing
    case WM_HSCROLL:
    {
        HWND slider = (HWND)lParam;
        int pos = (int)SendMessage(slider, TBM_GETPOS, 0, 0);

        SetFocus(window.hwnd);
        return 0;
    }

    case WM_NCCALCSIZE:
    {
        LRESULT ret = CallWindowProc(window.toolbar.toolBarProc, hwnd, uMsg, wParam, lParam);
        int rows = (int)SendMessage(hwnd, TB_GETROWS, 0, 0);

        if (window.toolbar.rows != rows) {
            SendMessage(window.toolbar.toolbar, TB_BUTTONCOUNT, 0, 0);
            window.toolbar.rows = rows;
        }
        return ret;
    }
    }

    return CallWindowProc(window.toolbar.toolBarProc, hwnd, uMsg, wParam, lParam);
}


void cvDestroyAllWindows(void) {
    std::vector< std::shared_ptr<CvWindow> > g_windows;
    {
        AutoLock lock(getWindowMutex());
        g_windows = getWindowsList();  // copy
    }
    for (auto it = g_windows.begin(); it != g_windows.end(); ++it) {
        auto window_ = *it;
        if (!window_)
            continue;

        {
            CvWindow& window = *window_;

            HWND mainhWnd = window.frame;
            HWND hwnd = window.hwnd;

            SendMessage(hwnd, WM_CLOSE, 0, 0);
            SendMessage(mainhWnd, WM_CLOSE, 0, 0);
        }

        window_.reset();
    }
    // TODO needed?
    {
        AutoLock lock(getWindowMutex());
        getWindowsList().clear();
    }
}

static void showSaveDialog(CvWindow& window) {
    if (!window.image)
        return;

    SIZE sz;
    int channels;
    void* data;
    if (!icvGetBitmapData(window, sz, channels, data))
        return; // nothing to save

    wchar_t szFileName[MAX_PATH] = L"";
    // try to use window title as file name
    GetWindowText(window.frame, szFileName, MAX_PATH);

    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
#ifdef OPENFILENAME_SIZE_VERSION_400
    // we are not going to use new fields any way
    ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400;
#else
    ofn.lStructSize = sizeof(ofn);
#endif
    ofn.hwndOwner = window.hwnd;
    ofn.lpstrFilter =
#ifdef HAVE_PNG
        L"Portable Network Graphics files (*.png)\0*.png\0"
#endif
        L"Windows bitmap (*.bmp;*.dib)\0*.bmp;*.dib\0"
#ifdef HAVE_JPEG
        L"JPEG files (*.jpeg;*.jpg;*.jpe)\0*.jpeg;*.jpg;*.jpe\0"
#endif
#ifdef HAVE_TIFF
        L"TIFF Files (*.tiff;*.tif)\0*.tiff;*.tif\0"
#endif
#ifdef HAVE_JASPER
        L"JPEG-2000 files (*.jp2)\0*.jp2\0"
#endif
#ifdef HAVE_WEBP
        L"WebP files (*.webp)\0*.webp\0"
#endif
        L"Portable image format (*.pbm;*.pgm;*.ppm;*.pxm;*.pnm)\0*.pbm;*.pgm;*.ppm;*.pxm;*.pnm\0"
#ifdef HAVE_OPENEXR
        L"OpenEXR Image files (*.exr)\0*.exr\0"
#endif
        L"Radiance HDR (*.hdr;*.pic)\0*.hdr;*.pic\0"
        L"Sun raster files (*.sr;*.ras)\0*.sr;*.ras\0"
        L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOREADONLYRETURN | OFN_NOCHANGEDIR;
#ifdef HAVE_PNG
    ofn.lpstrDefExt = L"png";
#else
    ofn.lpstrDefExt = L"bmp";
#endif

    if (GetSaveFileName(&ofn)) {
        cv::Mat tmp;
        cv::flip(cv::Mat(sz.cy, sz.cx, CV_8UC(channels), data, (sz.cx * channels + 3) & -4), tmp, 0);
        cv::imwrite(cv::format("%s", szFileName), tmp);
    }
}

/*
 * message received. check if it belongs to our windows (frame, hwnd).
 * returns true (and value in keyCode) if a key was pressed.
 * otherwise returns false (indication to continue event loop).
 */
static bool handleMessage(MSG& message, int& keyCode) {
    std::shared_ptr<CvWindow> window_;
    {
        AutoLock lock(getWindowMutex());
        window_ = icvFindWindowByHandle(message.hwnd);
    }
    if (window_) {
        CvWindow& window = *window_;

        switch (message.message) {
        case WM_DESTROY:
            // fallthru
        case WM_CHAR:
            DispatchMessage(&message);
            keyCode = (int)message.wParam;
            return true;

        case WM_SYSKEYDOWN:
            if (message.wParam == VK_F10) {
                keyCode = (int)(message.wParam << 16);
                return true;
            }
            break;

        case WM_KEYDOWN:
            // Intercept Ctrl+C for copy to clipboard
            if ('C' == message.wParam && (::GetKeyState(VK_CONTROL) >> 15)) {
                ::SendMessage(message.hwnd, WM_COPY, 0, 0);
                return false;
            }

            // Intercept Ctrl+S for "save as" dialog
            if ('S' == message.wParam && (::GetKeyState(VK_CONTROL) >> 15)) {
                showSaveDialog(window);
                return false;
            }

            TranslateMessage(&message);
            if ((message.wParam >= VK_F1 && message.wParam <= VK_F24) ||
                message.wParam == VK_HOME || message.wParam == VK_END ||
                message.wParam == VK_UP || message.wParam == VK_DOWN ||
                message.wParam == VK_LEFT || message.wParam == VK_RIGHT ||
                message.wParam == VK_INSERT || message.wParam == VK_DELETE ||
                message.wParam == VK_PRIOR || message.wParam == VK_NEXT) {
                DispatchMessage(&message);
                keyCode = (int)(message.wParam << 16);
                return true;
            }

            // fallthru

        default:
            DispatchMessage(&message);
            break;
        }
    } else {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    return false; // no keyCode to return, keep processing
}

/*
 * process until queue is empty but don't wait.
 */
int pollKey_W32() {
    CV_TRACE_FUNCTION();
    for (;;) {
        MSG message;
        if (PeekMessage(&message, 0, 0, 0, PM_REMOVE) == FALSE)
            return -1;

        int keyCode = -1;
        if (handleMessage(message, keyCode))
            return keyCode;
    }
}

int cvWaitKey(int delay) {
    int64 time0 = cv::getTickCount();
    int64 timeEnd = time0 + (int64)(delay * 0.001f * cv::getTickFrequency());

    for (;;) {
        MSG message;

        if ((delay <= 0) && !getWindowsList().empty())
            GetMessage(&message, 0, 0, 0);
        else if (PeekMessage(&message, 0, 0, 0, PM_REMOVE) == FALSE) {
            int64 t = cv::getTickCount();
            if (t - timeEnd >= 0)
                return -1;  // no messages and no more time
            Sleep(1);
            continue;
        }

        int keyCode = -1;
        if (handleMessage(message, keyCode))
            return keyCode;
    }
}

static void createToolbar_(CvWindow& window) {
    CV_Assert(!window.toolbar.toolbar);

    const int default_height = 30;

    // CreateToolbarEx is deprecated and forces linking against Comctl32.lib.
    window.toolbar.toolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
        WS_CHILD | CCS_TOP | TBSTYLE_WRAPABLE | BTNS_AUTOSIZE | BTNS_BUTTON,
        0, 0, 0, 0,
        window.frame, NULL, GetModuleHandle(NULL), NULL);
    // CreateToolbarEx automatically sends this but CreateWindowEx doesn't.
    SendMessage(window.toolbar.toolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    RECT rect;
    GetClientRect(window.frame, &rect);
    MoveWindow(window.toolbar.toolbar, 0, 0,
        rect.right - rect.left, default_height, TRUE);
    SendMessage(window.toolbar.toolbar, TB_AUTOSIZE, 0, 0);
    ShowWindow(window.toolbar.toolbar, SW_SHOW);

    window.toolbar.pos = 0;
    window.toolbar.rows = 0;
    window.toolbar.toolBarProc =
        (WNDPROC)icvGetWindowLongPtr(window.toolbar.toolbar, CV_WNDPROC);

    icvUpdateWindowPos(window);

    // Subclassing from toolbar
    icvSetWindowLongPtr(window.toolbar.toolbar, CV_WNDPROC, HGToolbarProc);
    icvSetWindowLongPtr(window.toolbar.toolbar, CV_USERDATA, (void*)&window);

}

void cvSetMouseCallback(const wchar_t* name, CvMouseCallback on_mouse, void* param) {
    CV_FUNCNAME("cvSetMouseCallback");

    if (!name)
        CV_Error(Error::StsNullPtr, "NULL window name");

    AutoLock lock(getWindowMutex());

    auto window = icvFindWindowByName(name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", name));

    window->on_mouse = on_mouse;
    window->on_mouse_param = param;
}


void* cvGetWindowHandle(const wchar_t* window_name) {
    CV_FUNCNAME("cvGetWindowHandle");

    AutoLock lock(getWindowMutex());

    if (window_name == 0)
        CV_Error(Error::StsNullPtr, "NULL window name");

    auto window = icvFindWindowByName(window_name);
    if (!window)
        CV_Error_(Error::StsNullPtr, ("NULL window: '%s'", window_name));

    return (void*)window->hwnd;
}


 void
cvSetPreprocessFuncWin32_(const void* callback) {
    hg_on_preprocess = (CvWin32WindowCallback)callback;
}

 void
cvSetPostprocessFuncWin32_(const void* callback) {
    hg_on_postprocess = (CvWin32WindowCallback)callback;
}