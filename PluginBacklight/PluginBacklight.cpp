#include <Windows.h>
#include "../../API/RainmeterAPI.h"
#include <string>
#include <HighLevelMonitorConfigurationAPI.h>
#pragma comment(lib,"Dxva2.lib")

#define IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS \
  CTL_CODE(FILE_DEVICE_VIDEO, 0x125, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS \
  CTL_CODE(FILE_DEVICE_VIDEO, 0x126, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS \
  CTL_CODE(FILE_DEVICE_VIDEO, 0x127, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Smooth backlight control,not uesd. I have no idea how to make it work:
// http://msdn.microsoft.com/en-us/library/jj647485(v=vs.85).aspx

// the max number of monitors can be controlled, change it to support more monitors
#define NMAX 3

static DWORD nRemoveCount = 0;
static DWORD nRemovable = 0;
static HWND hWndMessage = NULL;

enum ControlMethod
{
	Laptop,		// Laptop_IOCTL
	Monitor,	// Monitor_DDCCI
	None
};

class Measure
{
public:
	ControlMethod ctrl;

	double dBrightnessPercent;
	double dStep;

	BOOL returnnow;
	BYTE showlog;

	//virtual ~Measure() = 0;
	virtual ~Measure(){};	
	virtual BOOL MonitorInitialize(DWORD n1, DWORD n2){ return 0; };
	virtual void MonitorFinalize(BOOL mode){};
	BOOL ToGetBrightness(void);
	BOOL ToSetBrightness(char bang, int iTarget);
	
	Measure() : ctrl(Laptop), dBrightnessPercent(0), dStep(0), returnnow(true), showlog(0) {}

private:
	virtual BOOL GetBrightness(void){ return 0; };
	virtual void SetBrightness(char bang){};
};

BOOL Measure::ToGetBrightness(void)
{
	GetBrightness();
	dBrightnessPercent = floor(dBrightnessPercent + 0.5);
	return true;
}

BOOL Measure::ToSetBrightness(char bang, int iTarget)
{
	if (bang != 'S') ToGetBrightness();

	double dSet = dBrightnessPercent;

	switch (bang)
	{
	case 'U': dSet = floor((dSet + dStep) / dStep + 0.0) * dStep; break;
	case 'D': dSet = floor((dSet - dStep) / dStep + 0.5) * dStep; break;
	case 'C': dSet += iTarget; break;
	case 'S': dSet = iTarget; break;
	}

	dSet = floor(dSet < 0 ? 0 : (dSet > 100 ? 100 : dSet));

	if (dSet == dBrightnessPercent)return true;
	else dBrightnessPercent = dSet;

	SetBrightness(bang);

	return true;
}



class IOCTL: public Measure
{
public:
	IOCTL() : hDevice(NULL), bBrightnesslevelCur(255), bBrightnesslevelMax(0), powerboth(false), forcelevel(false) {};
	virtual ~IOCTL(){};
	BOOL MonitorInitialize(DWORD n1, DWORD n2);	// (forcelevel, powerboth)
	BOOL GetBrightness(void);
	void SetBrightness(char bang);
	void MonitorFinalize(BOOL mode);
private:
	// Laptop_IOCTL
	BOOL powerboth;
	BOOL forcelevel;

	BYTE Findlevel(BYTE bBrightness);
	HANDLE hDevice;
	BYTE bBrightness[3];
	BYTE bBrightnesslevel[256];	// Available levels				笔记本真实可选亮度值(最大100)
	BYTE bBrightnesslevelCur;	// Current level				存放当前亮度等级
	BYTE bBrightnesslevelMax;	// Number of available levels	可选亮度值数组长度
};

BOOL IOCTL::GetBrightness(void)
{
	// http://msdn.microsoft.com/en-us/library/windows/desktop/aa372656(v=vs.85).aspx

	DWORD nBrightnessReceiveByte;
	BYTE bBrightnessCur;

	DeviceIoControl(hDevice, IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS, NULL, 0,
		bBrightness, sizeof(byte) * 3, &nBrightnessReceiveByte, NULL);

	// http://msdn.microsoft.com/en-us/library/windows/desktop/aa372686(v=vs.85).aspx
	// BrightnessReceive[0] : DisplayPolicy
	// BrightnessReceive[1] : ACBRightness
	// BrightnessReceive[2] : DCBrightness

	if (bBrightness[0] == 1)bBrightnessCur = bBrightness[1];
	else bBrightnessCur = bBrightness[2];

	if (bBrightnesslevel[bBrightnesslevelCur] != bBrightnessCur)
	{
		// Calculate the Percent Value from bBrightnessCur
		DWORD dBrightnessPercentTmp = (bBrightnessCur - bBrightnesslevel[0]) * 100
			/ (bBrightnesslevel[bBrightnesslevelMax - 1] - bBrightnesslevel[0]);

		bBrightnesslevelCur = Findlevel(bBrightnessCur);

		if (!forcelevel)
		{
			dBrightnessPercent = dBrightnessPercentTmp;
		}
		else
		{
			dBrightnessPercent = floor(dBrightnessPercentTmp / dStep + 0.5) * dStep;
		}
	}

	return true;
}

void IOCTL::SetBrightness(char bang)
{
	BYTE bSet;

	// Get the level
	if (forcelevel || (bang != 'U' && bang != 'D'))
	{
		bSet = (BYTE)dBrightnessPercent * (bBrightnesslevel[bBrightnesslevelMax - 1] - bBrightnesslevel[0]) / 100
			+ bBrightnesslevel[0];
		bBrightnesslevelCur = Findlevel(bSet);
	}
	else bBrightnesslevelCur = bang == 'U' ? bBrightnesslevelCur + 1 : bBrightnesslevelCur - 1;

	bSet = bBrightnesslevel[bBrightnesslevelCur];

	if (!forcelevel)
	{
		// Output Real Percent Value
		dBrightnessPercent = (bSet - bBrightnesslevel[0]) * 100
			/ (bBrightnesslevel[bBrightnesslevelMax - 1] - bBrightnesslevel[0]);
		dBrightnessPercent = floor(dBrightnessPercent + 0.5);
	}

	////////////

	BYTE bBrightnessSet[3] = { 3, 0, 0 };
	DWORD tmp; // Useless

	if (powerboth)
	{
		bBrightnessSet[1] = bBrightnessSet[2] = bSet;
	}
	else if (bBrightness[0] == 1)
	{
		bBrightnessSet[0] = 1;
		bBrightnessSet[1] = bSet;
		bBrightnessSet[2] = bBrightness[2];
	}
	else
	{
		bBrightnessSet[0] = 2;
		bBrightnessSet[1] = bBrightness[1];
		bBrightnessSet[2] = bSet;
	}

	DeviceIoControl(hDevice, IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS,
		bBrightnessSet, sizeof(byte) * 3, NULL, 0, &tmp, NULL);
}

BOOL IOCTL::MonitorInitialize(DWORD n1, DWORD n2)
{
	WCHAR info[256];
	BOOL success;
	DWORD n;

	if (n1) forcelevel = true;
	if (n2) powerboth = true;

	hDevice = CreateFile(L"\\\\.\\LCD", GENERIC_READ | GENERIC_WRITE, FILE_ANY_ACCESS, NULL, OPEN_EXISTING, 0, NULL);
	success = DeviceIoControl(hDevice, IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS,
		NULL, 0, bBrightnesslevel, sizeof(BYTE) * 256, &n, NULL);

	bBrightnesslevelMax = (BYTE)n;

	if (success && bBrightnesslevel[1] > 0)
	{
		bBrightnesslevel[255] = 255;

		if (!forcelevel)
		{
			dStep = 100 / (bBrightnesslevelMax - 1);
		}

		// 数组数值从大到小排列的情况
		if (bBrightnesslevel[1] > bBrightnesslevel[2])
		{
			BYTE bBrightnesslevelTmp[256];
			DWORD nMax = bBrightnesslevelMax;
			for (n = 0; n < nMax; n++) bBrightnesslevelTmp[n] = bBrightnesslevel[n];
			for (n = 0; n < nMax; n++) bBrightnesslevel[n] = bBrightnesslevelTmp[nMax - n - 1];
		}

		if (showlog)
		{
			if (showlog == 1) RmLog(LOG_NOTICE, L"Backlight.dll: Available Levels for controlling:");
			else RmLog(LOG_NOTICE, L"Backlight.dll: 笔记本显示屏允许的亮度等级列表:");
			for (n = 1; n <= bBrightnesslevelMax; n++)
			{
				wsprintf(info, L"Level%d : %d", n, bBrightnesslevel[n - 1]);
				RmLog(LOG_NOTICE, info);
			}
		}

		return true;
	}
	else return false;
}

void IOCTL::MonitorFinalize(BOOL mode)
{
	CloseHandle(hDevice);
}

BYTE IOCTL::Findlevel(BYTE bBrightness)
{
	BYTE min = 0;
	BYTE max = bBrightnesslevelMax - 1;
	BYTE level = (min + max) / 2;

	while (max - min > 1)
	{
		if (bBrightnesslevel[level] == bBrightness)max = min = level;
		else if (bBrightnesslevel[level] > bBrightness)max = level;
		else if (bBrightnesslevel[level] < bBrightness)min = level;
		level = (min + max) / 2;
	}
	level = bBrightnesslevel[max] - bBrightness < bBrightness - bBrightnesslevel[min] ? max : min;

	return level;
}



class DDCCI: public Measure
{
public:
	DDCCI() : nPhysicalMonitor(0), nRemove(0), nMonitor(0) {};
	virtual ~DDCCI(){};

	BOOL MonitorInitialize(DWORD n1, DWORD n2);	// (nMonitor, nRemove)
	BOOL GetBrightness(void);
	void SetBrightness(char bang);
	void MonitorFinalize(BOOL mode);
private:
	// Monitor_DDCCI
	DWORD nRemove;
	DWORD nMonitor;
	HANDLE hPhysicalMonitor[NMAX];	// Handle to the PhysicalMonitor
	DWORD nPhysicalMonitor;			// Number of the PhysicalMonitor to control
	DWORD nBrightnessMax[NMAX];
	DWORD nBrightnessMin[NMAX];

	BOOL CheckMonitor(void);
};

BOOL DDCCI::CheckMonitor(void)		// for removable monitor
{
	if (nRemove != nRemoveCount)
	{
		if (nRemoveCount % 2 == 0)return false;	// Wait the Timer

		MonitorFinalize(false);
		nRemove = nRemoveCount;

		if (!MonitorInitialize(nMonitor, nRemove))
		{
			if (showlog)RmLog(LOG_WARNING, L"Backlight.dll: Fail to control monitor");
		}
	}
	return nPhysicalMonitor;
}

BOOL DDCCI::GetBrightness(void)
{
	if (nRemove && !CheckMonitor())
	{
		dBrightnessPercent = -1;
		return false;
	}

	//MC_VCP_CODE_TYPE pvct;
	DWORD nCurrentValue;
	DWORD nMaximumValue;
	DWORD nMinimumValue;

	GetMonitorBrightness(hPhysicalMonitor[0], &nMinimumValue, &nCurrentValue, &nMaximumValue);

	dBrightnessPercent = ((nCurrentValue - nMinimumValue) * 100 / (nMaximumValue - nMinimumValue));
	return true;
}

void DDCCI::SetBrightness(char bang)
{
	if (nRemove && !CheckMonitor())
	{
		dBrightnessPercent = -1;
		return;
	}

	DWORD nSet;
	DWORD i;
	for (i = 0; i < nPhysicalMonitor; i++)
	{
		nSet = (DWORD)dBrightnessPercent *  (nBrightnessMax[i] - nBrightnessMin[i]) / 100 + nBrightnessMin[i];

		SetMonitorBrightness(hPhysicalMonitor[i], nSet);
	}
}

BOOL DDCCI::MonitorInitialize(DWORD n1, DWORD n2)
{	
	WCHAR info[256];
	BOOL success;
	int i;
	DWORD n;

	nMonitor = n1;
	nRemove = n2;

	// Try to control desktop monitor
	HWND hWnd = GetForegroundWindow();
	HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	_PHYSICAL_MONITOR *pPhysicalMonitor = new _PHYSICAL_MONITOR[NMAX];
	DWORD nPhysicalMonitorGet;
	DWORD nValue;

	success = GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &nPhysicalMonitorGet);

	if (success)
	{
		// http://msdn.microsoft.com/en-us/library/dd692950(v=vs.85).aspx

		nPhysicalMonitorGet = nPhysicalMonitorGet > NMAX ? NMAX : nPhysicalMonitorGet;
		success = GetPhysicalMonitorsFromHMONITOR(hMonitor, nPhysicalMonitorGet, pPhysicalMonitor);

		if (success)
		{
			if (showlog)
			{
				if (showlog == 1)
				{
					RmLog(LOG_NOTICE, L"Backlight.dll: List of all monitors:");
					for (n = 0; n < nPhysicalMonitorGet; n++)
					{
						wsprintf(info, L"Monitor%d : %s ", n + 1, pPhysicalMonitor[n].szPhysicalMonitorDescription);
						RmLog(LOG_NOTICE, info);
					}
					RmLog(LOG_NOTICE, L"Backlight.dll: List of monitor in control:");
				}
				else
				{
					RmLog(LOG_NOTICE, L"Backlight.dll: 所有显示器列表:");
					for (n = 0; n < nPhysicalMonitorGet; n++)
					{
						wsprintf(info, L"显示器%d : %s ", n + 1, pPhysicalMonitor[n].szPhysicalMonitorDescription);
						RmLog(LOG_NOTICE, info);
					}
					RmLog(LOG_NOTICE, L"Backlight.dll: 控制中的显示器:");
				}
			}

			i = 0;

			for (n = 0; n < nPhysicalMonitorGet; n++)
			{
				if (nMonitor == 0 || nMonitor - 1 == n)
				{
					success = GetMonitorBrightness(pPhysicalMonitor[n].hPhysicalMonitor, &nBrightnessMin[i], &nValue, &nBrightnessMax[i]);
				}
				else success = false;

				if (success)
				{
					hPhysicalMonitor[i] = pPhysicalMonitor[n].hPhysicalMonitor;

					if (showlog)
					{
						if (showlog == 1) wsprintf(info, L"Monitor%d : Brightness : Max %d, Min %d, Current %d ", n + 1, nBrightnessMax[i], nBrightnessMin[i], nValue);
						else wsprintf(info, L"显示器%d : 亮度 : 最高 %d, 最低 %d, 当前 %d ", n + 1, nBrightnessMax[i], nBrightnessMin[i], nValue);
						RmLog(LOG_NOTICE, info);
					}
					i++;
				}
				else DestroyPhysicalMonitor(pPhysicalMonitor[n].hPhysicalMonitor);
			}

			if (i > 0)
			{
				nPhysicalMonitor = i;
				return true;
			}
		}
	}

	nPhysicalMonitor = 0;
	dBrightnessPercent = -1;
	if (showlog == 1) RmLog(LOG_NOTICE, L"None");
	else if (showlog) RmLog(LOG_NOTICE, L"无");
	return false;
}

void DDCCI::MonitorFinalize(BOOL mode)
{
	if (nRemove && mode)
	{
		nRemovable--;
		if (!nRemovable)
		{
			DestroyWindow(hWndMessage);
			UnregisterClass(L"PluginBacklightWindow", 0);
			nRemoveCount = 0;
			hWndMessage = NULL;
		}
	}

	for (DWORD n = 0; n < nPhysicalMonitor; n++)
	{
		DestroyPhysicalMonitor(hPhysicalMonitor[n]);
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_DISPLAYCHANGE)
	{
		RmLog(LOG_NOTICE, L"Backlight.dll: Reinitialize the monitor after 3s");
		//RmLog(LOG_NOTICE, L"Backlight.dll: 重载显示器");
		nRemoveCount++;
		if (nRemoveCount % 2 == 1)nRemoveCount++;
		SetTimer(hWnd, 1, 3000, nullptr);
	}
	else if (uMsg == WM_TIMER)
	{
		KillTimer(hWnd, 1);
		nRemoveCount++;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void RemovableMonitor(void)
{
	// to receive  WM_DISPLAYCHANGE message, it's the only way that I can find...
	WNDCLASS wc = { 0 };
	wc.lpfnWndProc = (WNDPROC)WndProc;
	wc.hInstance = 0;
	wc.lpszClassName = L"PluginBacklightWindow";
	ATOM className = RegisterClass(&wc);

	hWndMessage = CreateWindowEx(WS_EX_TOOLWINDOW, MAKEINTATOM(className), L"MonitorDetect",
		WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, 0, nullptr);

	RmLog(LOG_WARNING, L"Backlight.dll: Unknown Bang");
}


PLUGIN_EXPORT void Initialize(void** data, void* rm)
{
	int i;

	i = RmReadInt(rm, L"Showlog", 0);
	i = i < 0 ? 0 : (i > 2 ? 2 : i);
	BYTE showlog = (BYTE)i;

	i = RmReadInt(rm, L"Monitor", -1);
	ControlMethod ctrl = i >= 0 ? Monitor : Laptop;
	DWORD nMonitor = (DWORD)(i < 0 ? 0 : (i > NMAX ? NMAX : i));
	
	i = RmReadInt(rm, L"Divide", 10);
	i = i < 1 ? 1 : (i > 100 ? 100 : i);
	double dStep = 100 / i;

	WCHAR sPolicy[100] = { NULL };
	wcscpy_s(sPolicy, RmReadString(rm, L"Policy", L""));
	_wcslwr_s(sPolicy, wcslen(sPolicy) + 1);

	if (ctrl == Laptop)
	{
		Measure* measure = NULL;
		measure = new IOCTL();
		measure->dStep = dStep;
		measure->showlog = showlog;

		//--- Old Options
		i = RmReadInt(rm, L"LaptopPolicy", 0);
		if (i < 0 || i > 3)i = 0;
		DWORD forcelevel = i % 2 == 1 ? 1 : 0;
		DWORD powerboth = i >= 2 ? 1 : 0;
		//---

		forcelevel = wcsstr(sPolicy, L"forcelevel") ? 1 : 0;
		powerboth = wcsstr(sPolicy, L"bothpower") ? 1 : 0;

		if (measure->MonitorInitialize(forcelevel, powerboth))
		{
			// Ready to control laptop LCD
			measure->ctrl = ctrl;
			*data = measure;
			return;
		}
		else
		{
			delete measure;
			//measure = NULL;
			ctrl = Monitor;	
			if (showlog)RmLog(LOG_NOTICE, L"Backlight.dll: Fail to control Laptop LCD");
		}
	}

	if (ctrl == Monitor)
	{
		Measure* measure = NULL;
		measure = new DDCCI();
		measure->dStep = dStep;
		measure->showlog = showlog;

		DWORD nRemove = wcsstr(sPolicy, L"removable") ? 1 : 0;

		if (nRemove)
		{
			nRemovable++;
			if (!nRemoveCount)
			{
				RemovableMonitor();
				nRemoveCount = 1;
			}
			else nRemove = nRemoveCount;
		}

		*data = measure;

		if (measure->MonitorInitialize(nMonitor, nRemove))
		{
			// Ready to control Monitor
			measure->ctrl = Monitor;
		}
		else if (nRemove)
		{
			measure->ctrl = Monitor;
			if (showlog)RmLog(LOG_WARNING, L"Backlight.dll: Fail to control monitor");
		}
		else
		{
			measure->ctrl = None;
			RmLog(LOG_WARNING, L"Backlight.dll: Fail to control monitor, no more tries");
		}		
	}
}

PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
{
	//Measure* measure = (Measure*)data;
	*maxValue = 100;
}

PLUGIN_EXPORT double Update(void* data)
{
	Measure* measure = (Measure*)data;

	if (measure->ctrl == None)return -1.0;

	if (measure->returnnow) 
		measure->ToGetBrightness();
	else measure->returnnow = true;
	 	
	return measure->dBrightnessPercent;
}

//PLUGIN_EXPORT LPCWSTR GetString(void* data)
//{
//	Measure* measure = (Measure*)data;
//	return L"";
//}

PLUGIN_EXPORT void ExecuteBang(void* data, LPCWSTR args)
{
	Measure* measure = (Measure*)data;

	if (measure->ctrl == None)
	{
		RmLog(LOG_WARNING, L"Backlight.dll: Fail to control monitor");
		return;
	};

	measure->returnnow = false;

	std::wstring wholeBang = args;
	size_t pos = wholeBang.find(' ');

	int i = 0;

	if (pos != -1)
	{
		std::wstring bang = wholeBang.substr(0, pos);
		wholeBang.erase(0, pos + 1);

		if (_wcsicmp(bang.c_str(), L"ChangeBacklight") == 0)
		{
			if (1 == swscanf_s(wholeBang.c_str(), L"%d", &i))
			{
				measure->ToSetBrightness('C', i);
			}
			else
			{
				RmLog(LOG_WARNING, L"Backlight.dll: Incorrect number of arguments for bang");
			}
		}
		else if (_wcsicmp(bang.c_str(), L"SetBacklight") == 0)
		{
			if (1 == swscanf_s(wholeBang.c_str(), L"%d", &i))
			{
				measure->ToSetBrightness('S', i);
			}
			else
			{
				RmLog(LOG_WARNING, L"Backlight.dll: Incorrect number of arguments for bang");
			}
		}
		else
		{
			RmLog(LOG_WARNING, L"Backlight.dll: Unknown Bang");
		}
	}
	else if (_wcsicmp(args, L"Backlight+") == 0)
	{
		measure->ToSetBrightness('U', 0);
	}
	else if (_wcsicmp(args, L"Backlight-") == 0)
	{
		measure->ToSetBrightness('D', 0);
	}
	else
	{
		RmLog(LOG_WARNING, L"Backlight.dll: Unknown Bang");
	}
}

PLUGIN_EXPORT void Finalize(void* data)
{
	Measure* measure = (Measure*)data;

	if (measure->ctrl != None) measure->MonitorFinalize(true);

	delete measure;
}
