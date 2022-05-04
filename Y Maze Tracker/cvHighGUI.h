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

#ifndef __HIGHGUI_H_
#define __HIGHGUI_H_

#if defined(__OPENCV_BUILD) && defined(BUILD_PLUGIN)
#undef __OPENCV_BUILD  // allow public API only
#endif

#include "opencv2/core/utility.hpp"
#if defined(__OPENCV_BUILD)
#include "opencv2/core/private.hpp"
#endif

#include "opencv2/imgproc.hpp"
#include "opencv2/imgproc/imgproc_c.h"

#include "opencv2/imgcodecs.hpp"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>

#include <windows.h>
#undef small
#undef min
#undef max
#undef abs

/* Errors */
#define HG_OK          0 /* Don't bet on it! */
#define HG_BADNAME    -1 /* Bad window or file name */
#define HG_INITFAILED -2 /* Can't initialize HigHGUI */
#define HG_WCFAILED   -3 /* Can't create a window */
#define HG_NULLPTR    -4 /* The null pointer where it should not appear */
#define HG_BADPARAM   -5

#define __BEGIN__ __CV_BEGIN__
#define __END__  __CV_END__
#define EXIT __CV_EXIT__

#define CV_WINDOW_MAGIC_VAL     0x00420042
#define CV_TRACKBAR_MAGIC_VAL   0x00420043

typedef void (CV_CDECL* CvMouseCallback)(int event, int x, int y, int flags, void* param);
typedef void (CV_CDECL* CvOpenGlDrawCallback)(void* userdata);

//Yannick Verdie 2010, Max Kostin 2015
void cvSetModeWindow_W32(const wchar_t* name, double prop_value);
CvRect cvGetWindowRect_W32(const wchar_t* name);
double cvGetModeWindow_W32(const wchar_t* name);
double cvGetPropWindowAutoSize_W32(const wchar_t* name);
double cvGetRatioWindow_W32(const wchar_t* name);
double cvGetOpenGlProp_W32(const wchar_t* name);
double cvGetPropVisible_W32(const wchar_t* name);
double cvGetPropTopmost_W32(const wchar_t* name);
void cvSetPropTopmost_W32(const wchar_t* name, const bool topmost);
double cvGetPropVsync_W32(const wchar_t* name);
void cvSetPropVsync_W32(const wchar_t* name, const bool enabled);
void setWindowTitle_W32(const LPCWSTR& name, const LPCWSTR& title);
int cvInitSystem(HINSTANCE);
int cvNamedWindow(const wchar_t*, int);
void cvResizeWindow(const wchar_t* name, int width, int height);
void cvSetMouseCallback(const wchar_t* name, CvMouseCallback on_mouse, void* param);
void cvShowImage(const wchar_t* name, const cv::Mat& image);
int cvWaitKey(int delay);
void cvDestroyAllWindows(void);
cv::Rect selectROI(const LPCWSTR& windowName, cv::InputArray img, bool showCrosshair, bool fromCenter);

int pollKey_W32();


enum {
    //These 3 flags are used by cvSet/GetWindowProperty
    CV_WND_PROP_FULLSCREEN = 0, //to change/get window's fullscreen property
    CV_WND_PROP_AUTOSIZE = 1, //to change/get window's autosize property
    CV_WND_PROP_ASPECTRATIO = 2, //to change/get window's aspectratio property
    CV_WND_PROP_OPENGL = 3, //to change/get window's opengl support
    CV_WND_PROP_VISIBLE = 4,

    //These 2 flags are used by cvNamedWindow and cvSet/GetWindowProperty
    CV_WINDOW_NORMAL = 0x00000000, //the user can resize the window (no constraint)  / also use to switch a fullscreen window to a normal size
    CV_WINDOW_AUTOSIZE = 0x00000001, //the user cannot resize the window, the size is constrainted by the image displayed
    CV_WINDOW_OPENGL = 0x00001000, //window with opengl support

    //Those flags are only for Qt
    CV_GUI_EXPANDED = 0x00000000, //status bar and tool bar
    CV_GUI_NORMAL = 0x00000010, //old fashious way

    //These 3 flags are used by cvNamedWindow and cvSet/GetWindowProperty
    CV_WINDOW_FULLSCREEN = 1,//change the window to fullscreen
    CV_WINDOW_FREERATIO = 0x00000100,//the image expends as much as it can (no ratio constraint)
    CV_WINDOW_KEEPRATIO = 0x00000000//the ration image is respected.
};
enum WindowFlags {
    WINDOW_NORMAL = 0x00000000, //!< the user can resize the window (no constraint) / also use to switch a fullscreen window to a normal size.
    WINDOW_AUTOSIZE = 0x00000001, //!< the user cannot resize the window, the size is constrainted by the image displayed.
    WINDOW_OPENGL = 0x00001000, //!< window with opengl support.

    WINDOW_FULLSCREEN = 1,          //!< change the window to fullscreen.
    WINDOW_FREERATIO = 0x00000100, //!< the image expends as much as it can (no ratio constraint).
    WINDOW_KEEPRATIO = 0x00000000, //!< the ratio of the image is respected.
    WINDOW_GUI_EXPANDED = 0x00000000, //!< status bar and tool bar
    WINDOW_GUI_NORMAL = 0x00000010, //!< old fashious way
};
enum {
    CV_EVENT_MOUSEMOVE = 0,
    CV_EVENT_LBUTTONDOWN = 1,
    CV_EVENT_RBUTTONDOWN = 2,
    CV_EVENT_MBUTTONDOWN = 3,
    CV_EVENT_LBUTTONUP = 4,
    CV_EVENT_RBUTTONUP = 5,
    CV_EVENT_MBUTTONUP = 6,
    CV_EVENT_LBUTTONDBLCLK = 7,
    CV_EVENT_RBUTTONDBLCLK = 8,
    CV_EVENT_MBUTTONDBLCLK = 9,
    CV_EVENT_MOUSEWHEEL = 10,
    CV_EVENT_MOUSEHWHEEL = 11
};
enum {
    CV_EVENT_FLAG_LBUTTON = 1,
    CV_EVENT_FLAG_RBUTTON = 2,
    CV_EVENT_FLAG_MBUTTON = 4,
    CV_EVENT_FLAG_CTRLKEY = 8,
    CV_EVENT_FLAG_SHIFTKEY = 16,
    CV_EVENT_FLAG_ALTKEY = 32
};

inline void convertToShow(const cv::Mat& src, cv::Mat& dst, bool toRGB = true) {
    const int src_depth = src.depth();
    CV_Assert(src_depth != CV_16F && src_depth != CV_32S);
    cv::Mat tmp;
    switch (src_depth) {
    case CV_8U:
        tmp = src;
        break;
    case CV_8S:
        cv::convertScaleAbs(src, tmp, 1, 127);
        break;
    case CV_16S:
        cv::convertScaleAbs(src, tmp, 1 / 255., 127);
        break;
    case CV_16U:
        cv::convertScaleAbs(src, tmp, 1 / 255.);
        break;
    case CV_32F:
    case CV_64F: // assuming image has values in range [0, 1)
        src.convertTo(tmp, CV_8U, 255., 0.);
        break;
    }
    cv::cvtColor(tmp, dst, toRGB ? cv::COLOR_BGR2RGB : cv::COLOR_BGRA2BGR, dst.channels());
}

inline void convertToShow(const cv::Mat& src, const CvMat* arr, bool toRGB = true) {
    cv::Mat dst = cv::cvarrToMat(arr);
    convertToShow(src, dst, toRGB);
    CV_Assert(dst.data == arr->data.ptr);
}

#endif /* __HIGHGUI_H_ */