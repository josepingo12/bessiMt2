#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string>

static std::string GetExeDir()
{
	char buf[MAX_PATH] = {0};
	GetModuleFileNameA(NULL, buf, MAX_PATH);
	std::string s = buf;
	size_t p = s.find_last_of('\\');
	if (p != std::string::npos) s = s.substr(0, p);
	return s;
}

static BOOL InjectDll(HANDLE hProcess, const char* dllPath)
{
	SIZE_T sz = strlen(dllPath) + 1;
	LPVOID remoteBuf = VirtualAllocEx(hProcess, NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remoteBuf) return FALSE;
	if (!WriteProcessMemory(hProcess, remoteBuf, dllPath, sz, NULL))
		return FALSE;

	HMODULE hK32 = GetModuleHandleA("kernel32.dll");
	LPTHREAD_START_ROUTINE pLoadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(hK32, "LoadLibraryA");
	HANDLE hThr = CreateRemoteThread(hProcess, NULL, 0, pLoadLib, remoteBuf, 0, NULL);
	if (!hThr) return FALSE;

	WaitForSingleObject(hThr, 5000);
	DWORD exitCode = 0;
	GetExitCodeThread(hThr, &exitCode);
	CloseHandle(hThr);
	VirtualFreeEx(hProcess, remoteBuf, 0, MEM_RELEASE);
	return exitCode != 0;
}

int main(int argc, char** argv)
{
	std::string baseDir = GetExeDir();
	std::string exePath = baseDir + "\\metin2.exe";
	std::string dllPath = baseDir + "\\gamepad_hook.dll";

	if (argc > 1)
		exePath = argv[1];

	printf("Gamepad Launcher\n");
	printf("  exe: %s\n", exePath.c_str());
	printf("  dll: %s\n", dllPath.c_str());

	STARTUPINFOA si = { sizeof(si) };
	PROCESS_INFORMATION pi = {0};

	// Launch suspended so we can inject before main runs
	BOOL ok = CreateProcessA(
		exePath.c_str(), NULL, NULL, NULL, FALSE,
		CREATE_SUSPENDED, NULL, baseDir.c_str(), &si, &pi);

	if (!ok)
	{
		printf("CreateProcess failed: %lu\n", GetLastError());
		return 1;
	}

	printf("Process launched suspended. PID=%lu, injecting DLL...\n", pi.dwProcessId);

	if (!InjectDll(pi.hProcess, dllPath.c_str()))
	{
		printf("InjectDll failed: %lu\n", GetLastError());
		TerminateProcess(pi.hProcess, 1);
		return 2;
	}

	printf("DLL injected, resuming process.\n");
	ResumeThread(pi.hThread);

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}
