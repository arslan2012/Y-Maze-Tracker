// Y Maze Tracker.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "Y Maze Tracker.h"
#include "cvHighGUI.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/video/background_segm.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/tracking.hpp>
#include <opencv2/tracking/tracking_legacy.hpp>

#include <shobjidl.h>
#include <map>
#include <codecvt>
#include <string>

using namespace cv;
using namespace std;

#define MAX_LOADSTRING 100

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void                openFileDialog(HWND);
Ptr<Tracker>		getTracker(HWND);
void				mouseTracking(HWND, Ptr<Tracker>, PWSTR);
void CALLBACK		setCenterCoord(int, int, int, int, void*);
bool				PointInTriangle(Point2f, Point2f, Point2f, Point2f);
string				wstring_to_utf8(const wstring&);

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
String selectedTrackerType;						// selected tracker type
array<Point, 3> triangleCoords;					// cordinates of the triangle
Mat firstFrame;									// first frame of video

const auto windowname = L"Tracker";
const map<int, const wchar_t*> trackerTypes = {
	{IDC_GOTURN, L"GOTURN"},
	{IDC_CSRT, L"CSRT"},
	{IDC_KCF, L"KCF"},
	{IDC_DASIAMRPN, L"DaSiamRPN"},
	{IDC_MIL, L"MIL"},
	{IDC_BOOSTING, L"BOOSTING"},
	{IDC_TLD, L"TLD"},
	{IDC_MEDIANFLOW, L"MEDIANFLOW"},
	{IDC_MOSSE, L"MOSSE"},
};

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow) {
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// Initialize global strings
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_YMAZETRACKER, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);
	cvInitSystem(hInstance);

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow)) {
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_YMAZETRACKER));

	MSG msg;

	// Main message loop:
	while (GetMessage(&msg, nullptr, 0, 0)) {
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return (int)msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance) {
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_YMAZETRACKER));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_YMAZETRACKER);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
	hInst = hInstance; // Store instance handle in our global variable

	HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, 200, 380, nullptr, nullptr, hInstance, nullptr);

	if (!hWnd) {
		return FALSE;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_CREATE: {
		int y = 10;
		for (auto const& [id, name] : trackerTypes) {
			auto dwStyle = WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON;
			if (y == 10) {
				dwStyle |= WS_GROUP;
			}
			CreateWindowEx(WS_EX_WINDOWEDGE, L"BUTTON", name, dwStyle, 10, y, 180, 20, hWnd, (HMENU)id, hInst, NULL);
			y += 30;
		}
		CreateWindowEx(WS_EX_WINDOWEDGE, L"BUTTON", L"启用背景差分", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 10, y, 180, 20, hWnd, (HMENU)IDC_BACKSUB, hInst, NULL);
		SendMessage(GetDlgItem(hWnd, IDC_GOTURN), BM_SETCHECK, BST_CHECKED, 0);
		break;
	}
	case WM_COMMAND: {
		int wmId = LOWORD(wParam);
		// Parse the menu selections:
		switch (wmId) {
		case ID_FILE_OPEN:
			openFileDialog(hWnd);
			break;
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		// TODO: Add any drawing code that uses hdc here...
		EndPaint(hWnd, &ps);
		break;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	UNREFERENCED_PARAMETER(lParam);
	switch (message) {
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

void openFileDialog(HWND hDlg) {
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED |
		COINIT_DISABLE_OLE1DDE);
	if (SUCCEEDED(hr)) {
		IFileOpenDialog* pFileOpen;

		// Create the FileOpenDialog object.
		hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
			IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

		if (SUCCEEDED(hr)) {
			// Show the Open dialog box.
			hr = pFileOpen->Show(NULL);

			// Get the file name from the dialog box.
			if (SUCCEEDED(hr)) {
				IShellItem* pItem;
				hr = pFileOpen->GetResult(&pItem);
				if (SUCCEEDED(hr)) {
					PWSTR pszFilePath;
					hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

					// Display the file name to the user.
					if (SUCCEEDED(hr)) {
						Ptr<Tracker> tracker = getTracker(hDlg);
						mouseTracking(hDlg, tracker, pszFilePath);
					}
					CoTaskMemFree(pszFilePath);
					pItem->Release();
				}
			}
			pFileOpen->Release();
		}
		CoUninitialize();
	}
}

Ptr<Tracker> getTracker(HWND hDlg) {
	for (auto const& [id, name] : trackerTypes) {
		if (IsDlgButtonChecked(hDlg, id) == BST_CHECKED) {
			selectedTrackerType = wstring_to_utf8(name);
			switch (id) {
			case IDC_BOOSTING:
				return upgradeTrackingAPI(legacy::TrackerBoosting::create());
			case IDC_MIL:
				return TrackerMIL::create();
			case IDC_KCF:
				return TrackerKCF::create();
			case IDC_TLD:
				return upgradeTrackingAPI(legacy::TrackerTLD::create());
			case IDC_MEDIANFLOW:
				return upgradeTrackingAPI(legacy::TrackerMedianFlow::create());
			case IDC_MOSSE:
				return upgradeTrackingAPI(legacy::TrackerMOSSE::create());
			case IDC_CSRT: {
				auto params = TrackerCSRT::Params();
				params.use_color_names = true;
				return TrackerCSRT::create(params);
			}
			case IDC_GOTURN: 
				return TrackerGOTURN::create();
			case IDC_DASIAMRPN:
				return TrackerDaSiamRPN::create();
			default:
				break;
			}
			break;
		}
	}
}

void mouseTracking(HWND hDlg, Ptr<Tracker> tracker, PWSTR filename) {
	const bool useBackSub = IsDlgButtonChecked(hDlg, IDC_BACKSUB) == BST_CHECKED;
	Ptr<BackgroundSubtractor> pBackSub;
	if (useBackSub) {
		//create Background Subtractor objects
		pBackSub = createBackgroundSubtractorMOG2();
	}

	cvNamedWindow(windowname, WINDOW_NORMAL | WINDOW_KEEPRATIO | WINDOW_GUI_EXPANDED | CV_WINDOW_OPENGL);

	auto cap = VideoCapture(wstring_to_utf8(filename));
	if (!cap.isOpened()) {
		MessageBox(hDlg, L"Could not open the input video", filename, MB_ICONERROR);
		return;
	}
	Mat src, fgMask;
	cap >> src;
	firstFrame = src.clone();
	putText(firstFrame, "select center of the maze and then press enter", Point(100, 80), FONT_HERSHEY_COMPLEX, 0.75, Scalar(0, 0, 255), 2);
	cvSetMouseCallback(windowname, setCenterCoord, NULL);
	cvShowImage(windowname, firstFrame);
	cvWaitKey(0);
	fillPoly(firstFrame, triangleCoords, Scalar(255, 0, 0));
	cvShowImage(windowname, firstFrame);
	cvWaitKey(0);

	Mat findMouse = src.clone();
	putText(findMouse, "box select the mouse and then press enter", Point(100, 80), FONT_HERSHEY_COMPLEX, 0.75, Scalar(0, 0, 255), 2);
	auto bbox = selectROI(windowname, findMouse, false, false);
	// Initialize tracker with first frame and bounding box
	tracker->init(src, bbox);
	int a = 0, b = 0, c = 0, in_center = 0;

	for (auto frame = 1; !src.empty(); frame++, cap >> src) {
		if (useBackSub) {
			//update the background model
			pBackSub->apply(src, fgMask);
		}

		auto display = src.clone();
		string arm;
		// Update tracker
		if (tracker->update(src, bbox)) {
			// Tracking success
			auto p1 = Point(bbox.x, bbox.y);
			auto p2 = Point(bbox.x + bbox.width, bbox.y + bbox.height);
			auto mouse_center = Point((p1.x + p2.x) / 2, (p1.y + p2.y) / 2);
			auto center_coord = Point((triangleCoords[0].x + triangleCoords[1].x + triangleCoords[2].x) / 3, (triangleCoords[0].y + triangleCoords[1].y + triangleCoords[2].y) / 3);
			if (PointInTriangle(mouse_center, triangleCoords[0], triangleCoords[1], triangleCoords[2])) {
				arm = "center";
				in_center += 1;
			} else if (mouse_center.y > center_coord.y) {
				arm = 'c';
				c += 1;
			} else if (mouse_center.x > center_coord.x) {
				arm = 'b';
				b += 1;
			} else {
				arm = 'a';
				a += 1;
			}
			rectangle(display, p1, p2, Scalar(255, 25, 25), 2, 1);
			circle(display, mouse_center, 3, Scalar(25, 25, 255), 1);
		} else {
			// Tracking failure
			putText(display, "Tracking failure detected", Point(100, 80), FONT_HERSHEY_COMPLEX, 0.75, Scalar(0, 0, 255), 2);
		}
		// Display tracker type on frame
		putText(display, selectedTrackerType + " Tracker", Point(100, 20), FONT_HERSHEY_COMPLEX, 0.75, Scalar(50, 170, 50), 2);

		// Display FPS on frame
		putText(display, "Frame:" + to_string(frame) + ", Arm:" + arm, Point(100, 50), FONT_HERSHEY_COMPLEX, 0.75, Scalar(50, 170, 50), 2);
		// Display result
		cvShowImage(windowname, display);
		// Exit if ESC pressed
		cvWaitKey(1);
	}
	cap.release();
	cvDestroyAllWindows();
	wstring result = L"center:" + to_wstring(in_center) + L", a:" + to_wstring(a) + L", b:" + to_wstring(b) + L", c:" + to_wstring(c);
	MessageBox(hDlg, result.c_str(), L"结果", MB_OK);
}

void CALLBACK setCenterCoord(int event, int x, int y, int, void*) {
	if (event == CV_EVENT_LBUTTONDOWN) {
		triangleCoords[0] = triangleCoords[1];
		triangleCoords[1] = triangleCoords[2];
		triangleCoords[2] = Point(x, y);
		Mat showPoint = firstFrame.clone();
		for (auto coord : triangleCoords) {
			circle(showPoint, coord, 7, Scalar(0, 0, 255), -1);
		}
		cvShowImage(windowname, showPoint);
	}
}

float sign(Point2f p1, Point2f p2, Point2f p3) {
	return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}

bool PointInTriangle(Point2f pt, Point2f v1, Point2f v2, Point2f v3) {
	float d1, d2, d3;
	bool has_neg, has_pos;

	d1 = sign(pt, v1, v2);
	d2 = sign(pt, v2, v3);
	d3 = sign(pt, v3, v1);

	has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
	has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

	return !(has_neg && has_pos);
}

// convert wstring to UTF-8 string
string wstring_to_utf8(const wstring& wstr) {
	if (wstr.empty()) return string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	string strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
	return strTo;
}