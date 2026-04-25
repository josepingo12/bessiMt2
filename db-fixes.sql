-- ============================================================================
-- BessiMT2 - SQL fixes applicati durante il debug
-- ============================================================================
-- Questi fix sono utili se hai un account/personaggio in stato corrotto.
-- Eseguire dentro il container mysql:
--   docker exec metin2-deploy-mysql-1 mysql -uroot -pmetin2 -e "..."
-- ============================================================================


-- 1) Sbloccare un account creato via web (status=NOTAVAIL → OK)
-- ----------------------------------------------------------------------------
-- Quando registri un account dal sito web (https://...), Laravel lo crea con
-- status='NOTAVAIL' in attesa di verifica email. Se l'email non arriva, lo sblocchi così:

UPDATE account.account
SET status = 'OK', availDt = '0000-00-00 00:00:00'
WHERE login = 'NOMEACCOUNT';


-- 2) Resettare la password di admin a "test1234" (Argon2)
-- ----------------------------------------------------------------------------
-- Il server auth supporta SOLO il formato Argon2id (Laravel password_hash).
-- L'hash MySQL PASSWORD() (formato *XXXX...) NON viene riconosciuto.
-- Genera l'hash con:
--   docker exec metin2-deploy-web-1 php -r 'echo password_hash("test1234", PASSWORD_ARGON2ID);'

UPDATE account.account
SET password = '$argon2id$v=19$m=65536,t=4,p=1$RTJwMG0wWnlUU1ZJLmJPYg$47gVU0hFU6dVAsB5/Mo/5svAcrYlziXiWany9HEswaA',
    status = 'OK',
    availDt = '0000-00-00 00:00:00'
WHERE login = 'admin';


-- 3) Riparare un personaggio con dati corrotti (map_index=0, part_main=0)
-- ----------------------------------------------------------------------------
-- Sintomo: dopo la creazione, il client crasha con "invalid idx 0".
-- Soluzione: settare map_index e part_main validi.

-- Per Empire 1 (Shinsoo) - mappa 1 (a1)
UPDATE player.player
SET map_index = 1, x = 469300, y = 964200,
    exit_map_index = 1, exit_x = 469300, exit_y = 964200,
    part_main = 11699
WHERE id = ID_PERSONAGGIO;
UPDATE player.player_index SET empire = 1 WHERE id = (SELECT account_id FROM player.player WHERE id = ID_PERSONAGGIO);

-- Per Empire 2 (Chunjo) - mappa 21 (b1)
UPDATE player.player
SET map_index = 21, x = 64087, y = 167591,
    exit_map_index = 21, exit_x = 64087, exit_y = 167591,
    part_main = 11699
WHERE id = ID_PERSONAGGIO;
UPDATE player.player_index SET empire = 2 WHERE id = (SELECT account_id FROM player.player WHERE id = ID_PERSONAGGIO);

-- Per Empire 3 (Jinno) - mappa 41 (c1)
UPDATE player.player
SET map_index = 41, x = 957579, y = 255162,
    exit_map_index = 41, exit_x = 957579, exit_y = 255162,
    part_main = 11699
WHERE id = ID_PERSONAGGIO;
UPDATE player.player_index SET empire = 3 WHERE id = (SELECT account_id FROM player.player WHERE id = ID_PERSONAGGIO);


-- 4) Cancellare un personaggio totalmente rotto (per ricrearlo dal client)
-- ----------------------------------------------------------------------------
-- ATTENZIONE: cancella dati permanentemente. Backup prima!

-- Rimuovi item del char
DELETE FROM player.item WHERE owner_id = ID_PERSONAGGIO;
-- Rimuovi affect, quest, ecc.
DELETE FROM player.affect WHERE pid = ID_PERSONAGGIO;
DELETE FROM player.quest WHERE dwPID = ID_PERSONAGGIO;
DELETE FROM player.player_deleted WHERE id = ID_PERSONAGGIO;
-- Rimuovi il char stesso
DELETE FROM player.player WHERE id = ID_PERSONAGGIO;
-- Resetta lo slot nell'index dell'account
UPDATE player.player_index
SET pid1 = IF(pid1 = ID_PERSONAGGIO, 0, pid1),
    pid2 = IF(pid2 = ID_PERSONAGGIO, 0, pid2),
    pid3 = IF(pid3 = ID_PERSONAGGIO, 0, pid3),
    pid4 = IF(pid4 = ID_PERSONAGGIO, 0, pid4)
WHERE id IN (SELECT account_id FROM player.player WHERE id = ID_PERSONAGGIO);


-- 5) Verificare lo stato di un account
-- ----------------------------------------------------------------------------
SELECT
    a.id, a.login, a.status, a.availDt,
    LEFT(a.password, 20) AS pwd_format,
    pi.empire, pi.pid1, pi.pid2, pi.pid3, pi.pid4
FROM account.account a
LEFT JOIN player.player_index pi ON pi.id = a.id
WHERE a.login = 'NOMEACCOUNT';


-- 6) Lista personaggi su un account
-- ----------------------------------------------------------------------------
SELECT
    p.id, p.name, p.level, p.job, p.map_index, p.x, p.y,
    p.part_main, p.last_play, p.playtime
FROM player.player p
JOIN account.account a ON a.id = p.account_id
WHERE a.login = 'NOMEACCOUNT';


-- 7) GM (admin power) — verifica
-- ----------------------------------------------------------------------------
SELECT * FROM common.gmlist;
SELECT * FROM common.gmhost;

-- Aggiungere un GM (usare con cautela)
-- INSERT INTO common.gmlist (mAccount, mName, mContactIP, mServerIP, mAuthority)
-- VALUES ('admin', 'NOMEPERSONAGGIO', 'ALL', 'ALL', 'IMPLEMENTOR');


-- 8) Abilitare query log MySQL (per debugging)
-- ----------------------------------------------------------------------------
SET GLOBAL general_log_file = '/tmp/mysql.log';
SET GLOBAL general_log = ON;
-- Dopo il debug, disabilitarlo:
-- SET GLOBAL general_log = OFF;
