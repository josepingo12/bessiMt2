# BUG ANALYSIS — perché il personaggio non entra in gioco

Documento tecnico dettagliato del bug e della soluzione.

## La domanda di partenza

> "Login OK, vedo la lista personaggi, clicco Start, ma non entro mai in gioco. Mi resta bloccato o mi riporta al login. Perché?"

## Il flusso protocollare di Metin2 (in teoria)

```
Client                          Server (auth port 11000)
  |                                  |
  |-- TCP connect ------------------->|
  |<-- HEADER_GC_HANDSHAKE -----------|
  |-- HEADER_CG_HANDSHAKE ----------->|
  |<-- HEADER_GC_PHASE (Login) -------|
  |-- HEADER_CG_LOGIN3 (admin/pwd) -->|
  |<-- HEADER_GC_AUTH_SUCCESS + key --|
  |-- TCP close -------------------->|
  |
  |                          Server (game port 13000)
  |-- TCP connect ------------------->|
  |<-- HEADER_GC_HANDSHAKE -----------|
  |-- HEADER_CG_HANDSHAKE ----------->|
  |-- HEADER_CG_LOGIN2 (login + key)->|
  |<-- HEADER_GC_LOGIN_SUCCESS4 ------|  (con char list, lAddr/wPort per char)
  |
  |  user clicca Start
  |
  |-- HEADER_CG_PLAYER_SELECT (idx)-->|
  |<-- HEADER_GC_WARP (lAddr/wPort) --|
  |-- TCP close -------------------->|
  |
  |-- TCP connect to lAddr:wPort ---->|
  |    ... handshake e ingresso gioco
```

## Il flusso REALE su BessiMT2 (rotto)

Catturato con `tcpdump` su ch1_first dentro il container:

```
Client                          ch1_first (172.20.0.9:13000)
  |-- handshake ------------------>|
  |-- LOGIN2 (53 byte, "admin"+key)|
  |<-- LoginSuccess4 (333 byte) ---|  <-- contiene lAddr=0, wPort=13000
  |-- TCP close -------------------|
  |
  |-- nuova TCP connect a lAddr=0:13000 -- FAIL silenzioso
  |    (Connect("0.0.0.0", 13000) fallisce)
  |
  ... il client resta bloccato
```

## Decodifica del LoginSuccess

Il pacchetto `TPacketGCLoginSuccess4` ha questa struttura (da `Packet.h`):

```c
typedef struct packet_login_success4
{
    BYTE                       header;                  // = 0x20
    TSimplePlayerInformation   akSimplePlayerInformation[4];
    DWORD                      guild_id[4];
    char                       guild_name[4][33];
    DWORD                      handle;
    DWORD                      random_key;
} TPacketGCLoginSuccess4;

typedef struct
{
    DWORD               pid;
    char                name[25];
    BYTE                byLevel;
    DWORD               playtime;
    BYTE                st;
    BYTE                ht;
    BYTE                dx;
    BYTE                iq;
    WORD                wRaceNum;
    BYTE                bChangeName;
    WORD                wHairPart;
    BYTE                bDummy[4];
    long                x, y;
    LONG                lAddr;       // <-- SERVER MANDA 0
    WORD                wPort;       // <-- SERVER MANDA 13000
    BYTE                bySkillGroup;
} TSimplePlayerInformation;
```

Verificato leggendo dal client via `net.GetAccountCharacterSlotDataInteger(slot, net.ACCOUNT_CHARACTER_SLOT_ADDR)`:
- `addr=0`
- `port=13000`

## Dove il client usa lAddr/wPort

In `metin2-client/source/UserInterface/PythonNetworkStream.cpp`:

```cpp
// 1) DirectEnter (chiamato da Python net.DirectEnter())
void CPythonNetworkStream::ConnectGameServer(int iChrSlot)
{
    if (iChrSlot >= PLAYER_PER_ACCOUNT4)
        return;

    m_dwSelectedCharacterIndex = iChrSlot;
    __DirectEnterMode_Set(iChrSlot);

    TSimplePlayerInformation& rkSimplePlayerInfo = m_akSimplePlayerInfo[iChrSlot];
    CNetworkStream::Connect((DWORD)rkSimplePlayerInfo.lAddr, rkSimplePlayerInfo.wPort);
    // ↑ Connect(0, 13000) → fallisce silenziosamente
}
```

In `PythonNetworkStreamPhaseGame.cpp`:

```cpp
// 2) RecvWarpPacket (chiamato quando il server manda HEADER_GC_WARP)
bool CPythonNetworkStream::RecvWarpPacket()
{
    TPacketGCWarp kWarpPacket;
    if (!Recv(sizeof(kWarpPacket), &kWarpPacket)) return false;

    __DirectEnterMode_Set(m_dwSelectedCharacterIndex);
    CNetworkStream::Connect((DWORD)kWarpPacket.lAddr, kWarpPacket.wPort);
    // ↑ stesso bug
    return true;
}
```

In entrambi i casi `Connect` è chiamato senza controllare `lAddr=0`.

## Cosa fa `CNetworkStream::Connect(DWORD dwAddr, int port)`

Da `metin2-client/source/EterLib/NetStream.cpp`:

```cpp
bool CNetworkStream::Connect(DWORD dwAddr, int port, int limitSec)
{
    char szAddr[256];
    BYTE ip[4];
    ip[0] = dwAddr & 0xff; dwAddr >>= 8;
    ip[1] = dwAddr & 0xff; dwAddr >>= 8;
    ip[2] = dwAddr & 0xff; dwAddr >>= 8;
    ip[3] = dwAddr & 0xff;
    sprintf(szAddr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return Connect(szAddr, port, limitSec);
}
```

Con `dwAddr=0`, produce `"0.0.0.0"`. Poi:
```cpp
m_sock = socket(AF_INET, SOCK_STREAM, 0);
ioctlsocket(m_sock, FIONBIO, &arg);  // non-blocking
if (connect(m_sock, ...) == SOCKET_ERROR) {
    int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK) { /* fail */ }
}
```

Su Windows, `connect()` a `0.0.0.0:port` è non-deterministico: spesso fallisce immediatamente, a volte connette a localhost. Comunque, l'esito non è garantito e il client non si recupera.

## La soluzione

Bypassare `DirectEnter()` ed usare invece il pacchetto **`HEADER_CG_PLAYER_SELECT` (header 6)**, che viene inviato sulla **connessione esistente** senza bisogno di riconnettersi. Il server lo accetta e procede normalmente.

Da `metin2-client/source/UserInterface/PythonNetworkStreamPhaseSelect.cpp:161`:

```cpp
bool CPythonNetworkStream::SendSelectCharacterPacket(BYTE Index)
{
    TPacketCGSelectCharacter SelectCharacterPacket;
    SelectCharacterPacket.header = HEADER_CG_PLAYER_SELECT;  // = 6
    SelectCharacterPacket.player_index = Index;
    Send(sizeof(SelectCharacterPacket), &SelectCharacterPacket);
    return SendSequence();
}
```

Esposto in Python come `net.SendSelectCharacterPacket(index)`.

Inoltre, la fase del client va forzata manualmente a `LoadingPhase` perché la transizione automatica avveniva dentro `DirectEnter()`.

## Test post-fix (verificato con tcpdump)

Dopo il fix, il flusso diventa:

```
Client                          ch1_first (172.20.0.7:13000)
  |-- handshake ------------------>|
  |-- LOGIN2 (53 byte) ----------->|
  |<-- LoginSuccess4 (333 byte) ---|
  |-- HEADER_CG_PLAYER_SELECT (3 byte) -->|
  |<-- 6588 byte di dati gioco ----|   ← inventario, skill, quest, mappa
  |-- ack 68 byte ----------------|
  |   (connessione resta aperta)
  |
  ... il personaggio entra in gioco
```

Server log (ch1_first):
```
[char.cpp:1741] PLAYER_LOAD: Fuocodar PREMIUM 0 0, LOGGOFF_INTERVAL 981724
[input_db.cpp:1466] ITEM_LOAD: COUNT Fuocodar 30
```

Il server elabora: ~50 query SQL per caricare quest, skill, items, etc.

## Perché il server invia lAddr=0

Il binario `git.old-metin2.com/metin2/server:latest` contiene la stringa `lAddr` ma non è stato possibile decompilarlo per capire perché. Possibili cause:
1. Bug nella conversione `inet_addr(PUBLIC_IP)` che restituisce 0
2. Logica volutamente "se il game server è lo stesso, lAddr=0 = stay" — ma il client compilato non gestisce questo caso
3. Versione del server più recente del client, formato pacchetto disallineato

Cambiare `PUBLIC_IP` nel `.env` (provato con `127.0.0.1`, `192.168.1.50`, `bessimt2.ddns.net`) NON cambia il valore di lAddr inviato — resta sempre 0. Quindi il bug è nel codice del binario, non nel config.

## Strumenti usati per il debug

- `tcpdump` (installato nel container con `apt-get install tcpdump`)
- `mysql general_log` per tracciare le query
- `dbg.TraceError()` aggiunto al codice Python del client (introselect.py, intrologin.py)
- Lettura sorgenti `metin2-client/source/` (versione community che esiste, NON quella usata da BessiMT2)
- `strings /usr/bin/game` dentro il container per confermare la presenza di simboli
