# BessiMT2 - Fix entrata in gioco

Documentazione completa del debug e del fix per il problema "il personaggio non entra in gioco" sul server BessiMT2.

## TL;DR — Il fix

In `pack/root/introselect.py` del client, **sostituire `net.DirectEnter(chrSlot)` con**:

```python
net.SendSelectCharacterPacket(chrSlot)
self.stream.SetLoadingPhase()
```

Questo bypassa un bug nel binario del server che invia `lAddr=0` al client.

---

## Sintomo

- Login OK (auth verifica password, server processa)
- Lista personaggi caricata correttamente (vedi Fuocodar nello slot)
- Click su "Start" → 3 secondi di animazione
- **Il personaggio non entra in gioco**: a volte ti riporta al login, a volte resta bloccato

## Causa root

Il binario del server `git.old-metin2.com/metin2/server:latest` ha un bug:
nel pacchetto `LoginSuccess` (dopo l'auth) e nel `WARP packet`, il campo `lAddr` (indirizzo del game server a cui il client si deve riconnettere) è impostato a **0** invece dell'IP corretto del server.

Il client `metin2.exe`, in `RecvWarpPacket()` e `DirectEnter()`, chiama sempre:
```cpp
CNetworkStream::Connect((DWORD)kWarpPacket.lAddr, kWarpPacket.wPort);
```

Senza controllare se `lAddr=0`. Il connect a `0.0.0.0:13000` fallisce silenziosamente.

**Il client invia il pacchetto di selezione personaggio** (verificato con tcpdump),
ma poi tenta una nuova connessione TCP all'indirizzo `0.0.0.0:13000` che non va da nessuna parte.

## Soluzione

Invece di chiamare `net.DirectEnter()` (che apre nuova connessione fallita), si usa `net.SendSelectCharacterPacket()` che manda `HEADER_CG_PLAYER_SELECT` (header 6) sulla connessione **già aperta** con il game server. Il server processa correttamente e invia ~6588 byte di dati personaggio sulla stessa connessione.

Poi si forza manualmente la transizione di fase del client a `LoadingPhase` perché il flag `__DirectEnterMode_IsSet()` interno non è settato (saltato by design).

## Stato del server BessiMT2

### Stack docker (cartella `metin2-deploy/`)
- `mysql` (5.5) — DB
- `web` — Pannello PHP/Laravel su `https://bessimt2.ddns.net/`
- `caddy` — Reverse proxy con Let's Encrypt
- `auth` — Server di autenticazione (porta 11000)
- `db` — DBcache server (porta 15000, interno)
- `ch1_first` — Game server CH1 (porta 13000) + Mark server
- `ch1_game1` — Game server (porta 13001)
- `ch1_game2` — Game server (porta 13002)
- `game99` — Game server (porta 13099)

### Variabili `.env` rilevanti
```bash
PUBLIC_IP=127.0.0.1
GAME_MAX_LEVEL=105
WEB_APP_URL=https://bessimt2.ddns.net
MYSQL_HOST=mysql
MYSQL_USER=root
MYSQL_PASSWORD=metin2
```

### Porte
- 11000 (auth)
- 13000-13002 (game CH1)
- 13099 (game99)
- 8181 (web)
- 80/443 (caddy)
- 3308 (mysql esterno per debug)

---

## Cronologia del debug — cosa abbiamo provato

In ordine cronologico, tutto quello che abbiamo escluso o sistemato:

1. **Account `prova` bloccato** (`status=NOTAVAIL`)
   → Sbloccato con `UPDATE account.account SET status='OK' WHERE login='prova'`

2. **Personaggio creato con dati corrotti** (`map_index=0`, `part_main=0`)
   → Non era la causa del kick, ma comunque un personaggio "rotto" causa "invalid idx 0"
   → Fix: `UPDATE player.player SET map_index=21, part_main=11699 WHERE id=...`

3. **Hash password admin in formato MySQL `PASSWORD()`**
   → L'auth supporta solo Argon2id (formato Laravel). Generato con:
   ```bash
   docker exec metin2-deploy-web-1 php -r 'echo password_hash("test1234", PASSWORD_ARGON2ID);'
   ```

4. **`availDt` di admin in futuro** (10 anni in avanti = bloccato fino al 2036)
   → Reset a `0000-00-00 00:00:00`

5. **`GAME_AUTH_SERVER` non configurato sui game server**
   → Sembrava la causa, ma settandolo a `auth 12000` i game server si bloccavano a `Blend_Item_init`. Rolled back.

6. **`PUBLIC_IP` originale era `bessimt2.ddns.net`**
   → Cambiato a `127.0.0.1`. Aiuta perché il client gira sulla stessa macchina del server.

7. **DLL extra aggiunte per il gamepad** (python27.dll, DevIL.dll, etc.)
   → Non erano la causa. Rimesse tutte.

8. **Mark server `GAME_MARK_SERVER: 0`**
   → Causava errore "Guild Mark login requested but i'm not a mark server!". Rimesso a 1.

9. **Pacchetto `DirectEnter` non spedito sulla rete** ← LA VERA CAUSA
   → Tcpdump conferma: il client chiama `net.DirectEnter()` ma il pacchetto va sulla connessione vecchia che è chiusa.
   → Inoltre il server invia `lAddr=0` quindi `Connect()` fallisce comunque.

---

## Modifiche al client (`BessiMT2-Client`)

### `pack/root/introselect.py`

Cercare il blocco:
```python
if app.GetTime() - self.startReservingTime > 3.0:
    if False == self.openLoadingFlag:
        chrSlot=self.stream.GetCharacterSlot()
        net.DirectEnter(chrSlot)
        self.openLoadingFlag = True
        ...
```

Sostituire con:
```python
if app.GetTime() - self.startReservingTime > 0.1:
    if False == self.openLoadingFlag:
        chrSlot=self.stream.GetCharacterSlot()
        net.SendSelectCharacterPacket(chrSlot)
        self.stream.SetLoadingPhase()
        self.openLoadingFlag = True
        ...
```

Cambiamenti chiave:
1. **`net.DirectEnter(chrSlot)` → `net.SendSelectCharacterPacket(chrSlot)`**: invia `HEADER_CG_PLAYER_SELECT` invece di tentare riconnessione TCP
2. **`self.stream.SetLoadingPhase()`** aggiunto: forza la transizione di fase (il binario non lo fa perché `DirectEnterMode` non è settato)
3. **Timer `> 3.0` → `> 0.1`**: invia subito invece che aspettare 3 secondi (la connessione si stava chiudendo prima)

---

## Comandi utili

### Riavviare lo stack server
```bash
cd ~/Desktop/mt2/metin2-deploy
docker compose down
docker compose up -d
```

### Lanciare il client (con gamepad hook)
```cmd
cd C:\Users\josep\Desktop\mt2\BessiMT2-Client
.\gamepad_launcher.exe
```

Senza gamepad:
```cmd
.\metin2.exe
```

### Account utili
- `admin` / `test1234` (GM IMPLEMENTOR, char Fuocodar lvl 90)
- `prova` / *(quella che hai usato in registrazione web)*

### Reset password admin (Argon2)
```bash
docker exec metin2-deploy-mysql-1 mysql -uroot -pmetin2 -e "UPDATE account.account SET password='\$argon2id\$v=19\$m=65536,t=4,p=1\$RTJwMG0wWnlUU1ZJLmJPYg\$47gVU0hFU6dVAsB5/Mo/5svAcrYlziXiWany9HEswaA' WHERE login='admin';"
```

### Sbloccare un account
```bash
docker exec metin2-deploy-mysql-1 mysql -uroot -pmetin2 -e "UPDATE account.account SET status='OK', availDt='0000-00-00 00:00:00' WHERE login='NOMELOGIN';"
```

### Spostare un personaggio su mappa specifica
```bash
docker exec metin2-deploy-mysql-1 mysql -uroot -pmetin2 -e "UPDATE player.player SET map_index=1, x=469300, y=964200, exit_map_index=1, exit_x=469300, exit_y=964200 WHERE id=ID_PERSONAGGIO;"
```

### Logs
- Auth: `~/Desktop/mt2/metin2-deploy/storage/log/auth/daily_YYYY-MM-DD`
- CH1 first: `~/Desktop/mt2/metin2-deploy/storage/log/ch1/first/daily_YYYY-MM-DD`
- Client errori: `BessiMT2-Client/syserr.txt`

### MySQL query log (per debug)
```bash
docker exec metin2-deploy-mysql-1 mysql -uroot -pmetin2 -e 'SET GLOBAL general_log_file="/tmp/mysql.log"; SET GLOBAL general_log=ON;'
docker exec metin2-deploy-mysql-1 tail -f /tmp/mysql.log
```

---

## File in questo repo

- `client-fix/introselect.py` — la versione FIXATA del file Python del client
- `client-fix/introselect.original.py` — la versione originale per confronto
- `metin2-deploy/` — config docker server (docker-compose.yml + .env.example + Caddyfile)
- `gamepad-hook/` — sorgenti C++ del hook gamepad (XInput) + launcher
- `db-fixes.sql` — script SQL con tutti i fix DB applicati durante il debug
- `BUG-ANALYSIS.md` — analisi dettagliata del bug (tcpdump + sorgenti)
- `GAMEPAD.md` — **documentazione completa del gamepad** (come funziona, come modificare i tasti, come ricompilare)

---

## Limiti / cose da fare

- Il fix è lato CLIENT. Il server invia ancora `lAddr=0` ma non è più un problema.
- Se cambi server (ad esempio passi da ch1_first a ch1_game1 via warp), potrebbe non funzionare perché `RecvWarpPacket` ha lo stesso bug `Connect(lAddr=0)`.
- La soluzione "pulita" sarebbe ricompilare il binario del server con la fix, ma non abbiamo il sorgente.

## Autore del debug

Sessione di debug del 25 aprile 2026 — fix trovato dopo tcpdump dei pacchetti TCP, analisi del binario del server e dei sorgenti `metin2-client/` di riferimento.
