#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#include <stdio.h>
#include <string>

#pragma comment(lib, "Xinput.lib")
#pragma comment(lib, "user32.lib")

namespace
{
	enum EAction
	{
		ACT_NONE = 0,
		ACT_MOVE_FORWARD,
		ACT_MOVE_BACKWARD,
		ACT_MOVE_LEFT,
		ACT_MOVE_RIGHT,
		ACT_ATTACK,
		ACT_PICKUP,
		ACT_JUMP,
		ACT_SKILL_1, ACT_SKILL_2, ACT_SKILL_3, ACT_SKILL_4,
		ACT_SKILL_5, ACT_SKILL_6, ACT_SKILL_7, ACT_SKILL_8,
		ACT_MENU_INVENTORY,
		ACT_MENU_CHARACTER,
		ACT_MENU_SKILL,
		ACT_MENU_QUEST,
		ACT_MENU_MAP,
		ACT_MENU_SYSTEM,
		ACT_COUNT
	};

	struct SDefaultMap { int action; WORD vkey; };
	const SDefaultMap kDefault[] =
	{
		{ ACT_MOVE_FORWARD,     'W' },
		{ ACT_MOVE_BACKWARD,    'S' },
		{ ACT_MOVE_LEFT,        'A' },
		{ ACT_MOVE_RIGHT,       'D' },
		{ ACT_ATTACK,           VK_SPACE },
		{ ACT_PICKUP,           'Z' },
		{ ACT_JUMP,             VK_SPACE },
		{ ACT_SKILL_1,          VK_F1 },
		{ ACT_SKILL_2,          VK_F2 },
		{ ACT_SKILL_3,          VK_F3 },
		{ ACT_SKILL_4,          VK_F4 },
		{ ACT_SKILL_5,          VK_F5 },
		{ ACT_SKILL_6,          VK_F6 },
		{ ACT_SKILL_7,          VK_F7 },
		{ ACT_SKILL_8,          VK_F8 },
		{ ACT_MENU_INVENTORY,   'I' },
		{ ACT_MENU_CHARACTER,   'C' },
		{ ACT_MENU_SKILL,       'V' },
		{ ACT_MENU_QUEST,       'Q' },
		{ ACT_MENU_MAP,         'M' },
		{ ACT_MENU_SYSTEM,      VK_ESCAPE },
	};

	WORD g_action_to_vk[ACT_COUNT] = {0};
	bool g_action_prev[ACT_COUNT] = {false};

	HANDLE   g_hThread = NULL;
	HANDLE   g_hStopEvent = NULL;
	HMODULE  g_hSelfModule = NULL;
	int      g_userIdx = -1; // active XInput user index, -1 = none
	int      g_dbgCounter = 0;

	float    g_deadzoneStick   = 0.22f;
	int      g_deadzoneTrigger = 60; // 0-255

	FILE* g_log = NULL;

	void LOG(const char* fmt, ...)
	{
		if (!g_log) return;
		va_list ap;
		va_start(ap, fmt);
		SYSTEMTIME st;
		GetLocalTime(&st);
		fprintf(g_log, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
		vfprintf(g_log, fmt, ap);
		fprintf(g_log, "\n");
		fflush(g_log);
		va_end(ap);
	}

	void SendVKey(WORD vkey, bool down)
	{
		// Send TWO INPUT events: one VK-based and one SCANCODE-based.
		// This way both Windows-message games and DirectInput games (Metin2)
		// receive the keystroke.
		INPUT in[2];
		ZeroMemory(in, sizeof(in));

		WORD scan = (WORD)MapVirtualKeyA(vkey, MAPVK_VK_TO_VSC);

		in[0].type = INPUT_KEYBOARD;
		in[0].ki.wVk = vkey;
		in[0].ki.wScan = scan;
		in[0].ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;

		in[1].type = INPUT_KEYBOARD;
		in[1].ki.wVk = 0;
		in[1].ki.wScan = scan;
		in[1].ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);

		if (vkey == VK_LEFT || vkey == VK_RIGHT || vkey == VK_UP || vkey == VK_DOWN ||
			vkey == VK_INSERT || vkey == VK_DELETE || vkey == VK_HOME || vkey == VK_END ||
			vkey == VK_PRIOR || vkey == VK_NEXT)
		{
			in[0].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
			in[1].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
		}

		SendInput(2, in, sizeof(INPUT));
	}

	void ApplyDefaultMapping()
	{
		for (int i = 0; i < ACT_COUNT; ++i)
			g_action_to_vk[i] = 0;
		for (size_t i = 0; i < sizeof(kDefault)/sizeof(kDefault[0]); ++i)
			g_action_to_vk[ kDefault[i].action ] = kDefault[i].vkey;
	}

	void DispatchAction(int action, bool active)
	{
		if (action <= 0 || action >= ACT_COUNT) return;
		if (g_action_to_vk[action] == 0) return;
		if (active != g_action_prev[action])
		{
			SendVKey(g_action_to_vk[action], active);
			g_action_prev[action] = active;
			LOG("action %d -> vk %u %s", action, g_action_to_vk[action], active ? "DOWN" : "UP");
		}
	}

	// Read state from ALL XInput slots and merge: any non-zero stick/trigger
	// or pressed button wins. This handles the case where Windows assigns the
	// controller to slot 1+ instead of 0.
	bool MergedState(XINPUT_GAMEPAD& out)
	{
		ZeroMemory(&out, sizeof(out));
		bool anyPresent = false;
		int chosen = -1;
		int bestPriority = -1;

		for (int i = 0; i < XUSER_MAX_COUNT; ++i)
		{
			XINPUT_STATE st;
			ZeroMemory(&st, sizeof(st));
			if (XInputGetState(i, &st) != ERROR_SUCCESS) continue;
			anyPresent = true;

			int priority = 0;
			if (st.Gamepad.wButtons) priority += 100;
			priority += abs((int)st.Gamepad.sThumbLX) / 1000;
			priority += abs((int)st.Gamepad.sThumbLY) / 1000;
			priority += st.Gamepad.bLeftTrigger / 5;
			priority += st.Gamepad.bRightTrigger / 5;

			if (priority > bestPriority)
			{
				bestPriority = priority;
				chosen = i;
				out = st.Gamepad;
			}
		}

		if (chosen >= 0 && chosen != g_userIdx)
		{
			LOG("Active XInput slot switched: %d -> %d", g_userIdx, chosen);
			g_userIdx = chosen;
		}
		return anyPresent;
	}

	void PollOnce()
	{
		HWND fg = GetForegroundWindow();
		DWORD fgPid = 0;
		GetWindowThreadProcessId(fg, &fgPid);
		bool fgOk = (fgPid == GetCurrentProcessId());

		XINPUT_GAMEPAD pad;
		if (!MergedState(pad))
			return;

		// Heartbeat every ~4s
		if (++g_dbgCounter % 250 == 0)
		{
			LOG("HEARTBEAT slot=%d fgOk=%d LX=%d LY=%d btnMask=0x%04X RT=%d",
				g_userIdx, (int)fgOk,
				(int)pad.sThumbLX, (int)pad.sThumbLY,
				(unsigned int)pad.wButtons, (int)pad.bRightTrigger);
		}

		// Use the merged gamepad state as if it were the regular state
		XINPUT_STATE st;
		ZeroMemory(&st, sizeof(st));
		st.Gamepad = pad;

		if (!fgOk) return;

		// Normalize sticks to [-1, 1]
		float lx = (float)st.Gamepad.sThumbLX / 32767.0f;
		float ly = (float)st.Gamepad.sThumbLY / 32767.0f;
		// In XInput, Y is INVERTED (up = positive). We want up = forward.
		// We invert here so ly < 0 = up = forward (matching DirectInput convention)
		ly = -ly;

		auto applyDz = [](float v) {
			float a = v < 0 ? -v : v;
			if (a < 0.22f) return 0.0f;
			float sign = v < 0 ? -1.0f : 1.0f;
			float s = (a - 0.22f) / (1.0f - 0.22f);
			if (s > 1.0f) s = 1.0f;
			return sign * s;
		};

		lx = applyDz(lx);
		ly = applyDz(ly);

		DispatchAction(ACT_MOVE_FORWARD,  ly < -0.35f);
		DispatchAction(ACT_MOVE_BACKWARD, ly >  0.35f);
		DispatchAction(ACT_MOVE_LEFT,     lx < -0.35f);
		DispatchAction(ACT_MOVE_RIGHT,    lx >  0.35f);

		WORD btn = st.Gamepad.wButtons;
		DispatchAction(ACT_ATTACK, st.Gamepad.bRightTrigger > g_deadzoneTrigger);
		DispatchAction(ACT_JUMP,        (btn & XINPUT_GAMEPAD_A) != 0);
		DispatchAction(ACT_PICKUP,      (btn & XINPUT_GAMEPAD_Y) != 0);
		DispatchAction(ACT_SKILL_1,     (btn & XINPUT_GAMEPAD_X) != 0);
		DispatchAction(ACT_SKILL_2,     (btn & XINPUT_GAMEPAD_B) != 0);
		DispatchAction(ACT_SKILL_3,     (btn & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
		DispatchAction(ACT_SKILL_4,     (btn & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);

		DispatchAction(ACT_MENU_INVENTORY, (btn & XINPUT_GAMEPAD_DPAD_UP) != 0);
		DispatchAction(ACT_MENU_CHARACTER, (btn & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
		DispatchAction(ACT_MENU_SKILL,     (btn & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
		DispatchAction(ACT_MENU_MAP,       (btn & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
		DispatchAction(ACT_MENU_SYSTEM,    (btn & XINPUT_GAMEPAD_START) != 0);
		DispatchAction(ACT_MENU_QUEST,     (btn & XINPUT_GAMEPAD_BACK) != 0);

		// =========================================================
		// RIGHT STICK = CAMERA (hold RMB + move mouse, like Metin2)
		// =========================================================
		float rx = (float)pad.sThumbRX / 32767.0f;
		float ry = (float)pad.sThumbRY / 32767.0f;
		// Y up = pitch up (positive). We invert so up-stick = look up
		ry = -ry;

		auto applyDzCam = [](float v) {
			float a = v < 0 ? -v : v;
			if (a < 0.18f) return 0.0f;
			float sign = v < 0 ? -1.0f : 1.0f;
			float s = (a - 0.18f) / (1.0f - 0.18f);
			if (s > 1.0f) s = 1.0f;
			// quadratic curve for finer control near center
			s = s * s;
			return sign * s;
		};

		float rxDz = applyDzCam(rx);
		float ryDz = applyDzCam(ry);
		bool cameraActive = (rxDz != 0.0f || ryDz != 0.0f);

		static bool s_rmbHeld = false;
		const float CAM_SENS = 18.0f; // sensibilità camera

		if (cameraActive)
		{
			if (!s_rmbHeld)
			{
				INPUT in; ZeroMemory(&in, sizeof(in));
				in.type = INPUT_MOUSE;
				in.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
				SendInput(1, &in, sizeof(INPUT));
				s_rmbHeld = true;
				LOG("camera RMB DOWN");
			}
			INPUT in; ZeroMemory(&in, sizeof(in));
			in.type = INPUT_MOUSE;
			in.mi.dx = (LONG)(rxDz * CAM_SENS);
			in.mi.dy = (LONG)(ryDz * CAM_SENS);
			in.mi.dwFlags = MOUSEEVENTF_MOVE;
			SendInput(1, &in, sizeof(INPUT));
		}
		else
		{
			if (s_rmbHeld)
			{
				INPUT in; ZeroMemory(&in, sizeof(in));
				in.type = INPUT_MOUSE;
				in.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
				SendInput(1, &in, sizeof(INPUT));
				s_rmbHeld = false;
				LOG("camera RMB UP");
			}
		}
	}

	DWORD WINAPI GamepadThread(LPVOID)
	{
		LOG("Gamepad thread started (XInput, all-slots scan)");
		ApplyDefaultMapping();

		while (WaitForSingleObject(g_hStopEvent, 16) == WAIT_TIMEOUT)
		{
			PollOnce();
		}

		LOG("Gamepad thread exit");
		return 0;
	}
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_hSelfModule = hMod;
		DisableThreadLibraryCalls(hMod);

		char path[MAX_PATH] = {0};
		GetModuleFileNameA(hMod, path, MAX_PATH);
		std::string logPath = path;
		size_t dot = logPath.find_last_of('\\');
		if (dot != std::string::npos) logPath = logPath.substr(0, dot + 1);
		logPath += "gamepad_hook.log";
		g_log = fopen(logPath.c_str(), "a");
		LOG("DLL attached (XInput build)");

		g_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		g_hThread = CreateThread(NULL, 0, GamepadThread, NULL, 0, NULL);
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		if (g_hStopEvent) SetEvent(g_hStopEvent);
		if (g_hThread)
		{
			WaitForSingleObject(g_hThread, 2000);
			CloseHandle(g_hThread);
			g_hThread = NULL;
		}
		if (g_hStopEvent) { CloseHandle(g_hStopEvent); g_hStopEvent = NULL; }
		if (g_log) { fclose(g_log); g_log = NULL; }
	}
	return TRUE;
}
