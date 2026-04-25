# Gamepad Setup — BessiMT2

Documentazione del supporto gamepad per il client Metin2.

## Come funziona

`gamepad_hook.dll` è una DLL che viene **iniettata** in `metin2.exe` da `gamepad_launcher.exe`. Legge il gamepad via **XInput** (API Microsoft nativa per controller Xbox) e simula tasti tastiera + mouse.

Il gioco non sa che esiste un gamepad: vede solo input keyboard/mouse simulati. Quindi **NON c'è una voce "Gamepad" nelle impostazioni del gioco**.

## Come avviare il gioco con il gamepad

```cmd
cd C:\Users\josep\Desktop\mt2\BessiMT2-Client
.\gamepad_launcher.exe
```

Il launcher:
1. Lancia `metin2.exe` come processo sospeso
2. Inietta `gamepad_hook.dll` nel suo address space
3. Risume il processo, il client parte normalmente

Per lanciare senza gamepad: `.\metin2.exe` direttamente.

## Mappatura attuale dei tasti

| Gamepad | Azione | Simula |
|---|---|---|
| Stick sinistro | Movimento personaggio | W / A / S / D |
| **Stick destro** | **Camera (rotazione visuale)** | **Tasto destro mouse + mouse move** |
| Trigger destro (RT) | Attacca | SPACE |
| A | Salta | SPACE |
| Y | Raccogli | Z |
| X | Skill 1 | F1 |
| B | Skill 2 | F2 |
| LB | Skill 3 | F3 |
| RB | Skill 4 | F4 |
| D-pad ↑ | Inventario | I |
| D-pad ← | Personaggio | C |
| D-pad → | Skill | V |
| D-pad ↓ | Mappa | M |
| Start | Menu sistema | ESC |
| Back | Quest | Q |

## File chiave

- `gamepad-hook/gamepad_hook.cpp` — codice del hook (logica, mappatura)
- `gamepad-hook/launcher.cpp` — codice del launcher (inietta il DLL)
- `gamepad-hook/gamepad_hook.vcxproj` — progetto Visual Studio del hook
- `gamepad-hook/launcher.vcxproj` — progetto Visual Studio del launcher

Output build: `gamepad_hook/bin/gamepad_hook.dll` e `gamepad_hook/bin/gamepad_launcher.exe`.

## Come modificare i tasti

Apri `gamepad_hook.cpp` e modifica la struct `kDefault[]` (riga ~32):

```cpp
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
```

Per cambiare cosa fa un pulsante del gamepad, vai nella funzione `PollOnce()` (riga ~200) e modifica le righe `DispatchAction(...)`. Esempio per cambiare il pulsante che fa "skill 1":

```cpp
// Prima:
DispatchAction(ACT_SKILL_1,     (btn & XINPUT_GAMEPAD_X) != 0);
// Dopo (skill 1 sul pulsante B invece che X):
DispatchAction(ACT_SKILL_1,     (btn & XINPUT_GAMEPAD_B) != 0);
```

Tasti gamepad disponibili:
- `XINPUT_GAMEPAD_A` / `B` / `X` / `Y`
- `XINPUT_GAMEPAD_LEFT_SHOULDER` / `RIGHT_SHOULDER` (LB / RB)
- `XINPUT_GAMEPAD_DPAD_UP` / `DOWN` / `LEFT` / `RIGHT`
- `XINPUT_GAMEPAD_START` / `BACK`
- `XINPUT_GAMEPAD_LEFT_THUMB` / `RIGHT_THUMB` (click stick)

Tasti tastiera Windows (VK code):
- Lettere/numeri: `'A'`, `'1'`, ecc. (singoli char tra apici)
- F1-F12: `VK_F1` ... `VK_F12`
- Frecce: `VK_LEFT`, `VK_RIGHT`, `VK_UP`, `VK_DOWN`
- `VK_SPACE`, `VK_RETURN`, `VK_ESCAPE`, `VK_TAB`, `VK_LSHIFT`, `VK_LCONTROL`, `VK_LMENU` (alt)

## Come modificare la sensibilità della camera

In `PollOnce()`, cerca:
```cpp
const float CAM_SENS = 18.0f; // sensibilità camera
```

Aumentalo per camera più rapida (es. 30), abbassalo per più lenta (es. 10).

Per la deadzone (zona morta dello stick prima che la camera si muova), in `applyDzCam()`:
```cpp
if (a < 0.18f) return 0.0f;
```
0.18 = 18% del raggio. Più alto = stick deve essere più spinto per attivare la camera.

## Come ricompilare

Serve **Visual Studio 2022 Community** con il workload **"Sviluppo di applicazioni desktop con C++"**.

### Da PowerShell

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild "C:\Users\josep\Desktop\mt2\gamepad_hook\gamepad_hook.vcxproj" /p:Configuration=Release /p:Platform=Win32
```

Output: `C:\Users\josep\Desktop\mt2\gamepad_hook\bin\gamepad_hook.dll`

### Copiare la DLL nel client

```powershell
# Killa il gioco se è aperto
taskkill /F /IM metin2.exe /IM gamepad_launcher.exe

# Copia la nuova DLL
copy "C:\Users\josep\Desktop\mt2\gamepad_hook\bin\gamepad_hook.dll" `
     "C:\Users\josep\Desktop\mt2\BessiMT2-Client\gamepad_hook.dll"
```

Poi rilancia con `gamepad_launcher.exe`.

## Debug — il log

`BessiMT2-Client/gamepad_hook.log` contiene:
- `DLL attached (XInput build)` quando il hook si carica
- `Active XInput slot switched: X -> Y` quando rileva su quale slot è il controller
- `HEARTBEAT slot=N fgOk=1 LX=... LY=... btnMask=0xXXXX RT=...` ogni 4 secondi (per debug)
- `action N -> vk M DOWN/UP` quando un tasto cambia stato
- `camera RMB DOWN/UP` quando la camera entra/esce dalla modalità rotazione

Se i `HEARTBEAT` mostrano sempre `LX=0 LY=0 btnMask=0x0000`, il controller non sta inviando dati. Possibili cause:
- Xbox Game Bar interferisce → killa il processo `GameBar.exe`
- Steam Input prende l'esclusiva → chiudi Steam o disabilita Steam Input nelle impostazioni del controller in Steam
- Il controller è su uno slot XInput diverso → la nuova versione del hook scansiona TUTTI gli slot quindi questo è già gestito

## Storia / cosa NON funzionava prima

1. **Versione SDL2 originale**: SDL2 non riusciva a leggere il controller perché `dinput8.dll` di metin2.exe lo acquisiva in modalità esclusiva
2. **Slot XInput sbagliato**: il controller a volte è su slot 1 (non 0). Il primo hook leggeva solo lo slot 0
3. **Cached game window handle**: il primo hook salvava il handle della finestra all'avvio. Quando il gioco passava da splash → main, il handle diventava stale e il hook ignorava tutti gli input
4. **SendInput senza KEYEVENTF_SCANCODE**: i giochi DirectInput leggono via scancode HW. Senza il flag, gli eventi keyboard simulati non arrivavano al gioco

Tutti risolti nella versione corrente. Vedi `gamepad_hook.cpp` per i dettagli implementativi.
