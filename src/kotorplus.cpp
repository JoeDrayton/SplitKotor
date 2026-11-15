// KotorPlus - engine fixes and UI additions for KOTOR 1 (v1.03, unlocked Steam/GOG exe).
// Loads as dinput8.dll from the game folder and forwards DirectInput8Create to the real one.
//
// Features
//   * Frame cap (low-CPU pacing) - the high-FPS "stuck after combat" bug goes away at 60.
//   * Unstick hotkey: ends the leader's combat round and clears its action queue.
//   * Freeze diagnostics hotkey: writes combat-round state to KotorPlus.log.
//   * Combat readout: on-screen d20 roll + attack bonus vs defense, hit chance, result, damage.
//   * Item comparison: equip screen description also shows the item currently in that slot.
//   * Stuck-after-combat fix: the "break out of the engaged animation" check in ProcessInput
//     measured the player's drive speed while the same code was zeroing the drive input, so it
//     read 0 forever and the player could never move again. The speed test is removed.
//   * Walk toggle, middle-mouse free look, hold-to-skip dialogue, no display mode switch at start.
//   * Companion: runs kp_comp.ncs on the player every couple of seconds (spawns/follows).
//
// Addresses come from the KotOR Patch Manager address database (kotor1_0_3.db, MIT) and
// were checked against the decrypted Steam exe. Every call-site patch verifies its bytes first.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <gl/GL.h>
#include <stdio.h>
#include <share.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "winmm.lib")

// ---------------------------------------------------------------- engine types / addresses
struct CExoString { char* str; uint32_t len; };

#define APP_MANAGER   (*(char**)0x7A39FC)
#define CLIENT_APP    (*(void**)(APP_MANAGER + 4))
#define SERVER_APP    (*(void**)(APP_MANAGER + 8))
#define INVALID_OBJ   0x7F000000u

typedef void* (__thiscall* GetAttack_t)(void* round, int idx);
typedef uint16_t (__thiscall* RollDice_t)(void* rules, uint8_t num, uint8_t sides);
typedef int (__thiscall* AttackMod_t)(void* stats, void* target);
typedef int16_t (__thiscall* ArmorClass_t)(void* stats, void* attacker, int flag);
typedef void (__thiscall* SetDescription_t)(void* gui, CExoString* text);
typedef CExoString* (__thiscall* GetFullName_t)(void* stats, CExoString* out);
typedef CExoString* (__thiscall* GetPropDesc_t)(void* item, CExoString* out);
typedef uint32_t (__thiscall* GetEquippedItemID_t)(void* ccreature, int slot);
typedef uint32_t (__thiscall* ClientToServer_t)(void* server, uint32_t id);
typedef void* (__thiscall* GetItemByID_t)(void* server, uint32_t id);
typedef void* (__thiscall* GetClientItem_t)(void* sitem);
typedef void* (__thiscall* GetPlayerCreature_t)(void* server);
typedef void (__thiscall* EndCombatRound_t)(void* round, int flag);
typedef void (__thiscall* ClearAllActions_t)(void* obj, int includeAttacks);
typedef void (__thiscall* ExoStrCtor_t)(CExoString* s, const char* text);
typedef void (__thiscall* ExoStrDtor_t)(CExoString* s);

static GetAttack_t         oGetAttack        = (GetAttack_t)0x4D2B50;
static RollDice_t          oRollDice         = (RollDice_t)0x550B60;
static AttackMod_t         oAttackMod        = (AttackMod_t)0x5A7EA0;
static ArmorClass_t        oArmorClass       = (ArmorClass_t)0x5AD0E0;
static SetDescription_t    oSetDescription   = (SetDescription_t)0x6B56B0;
static GetFullName_t       GetFullName       = (GetFullName_t)0x5A4C60;
static GetPropDesc_t       GetPropertyDesc   = (GetPropDesc_t)0x55F340;
static GetEquippedItemID_t GetEquippedItemID = (GetEquippedItemID_t)0x60CA80;
static ClientToServer_t    ClientToServer    = (ClientToServer_t)0x4AEA30;
static GetItemByID_t       GetServerItem     = (GetItemByID_t)0x4AE760;
static GetClientItem_t     GetClientItem     = (GetClientItem_t)0x554190;
static GetPlayerCreature_t GetPlayerCreature = (GetPlayerCreature_t)0x4B2820;
static EndCombatRound_t    EndCombatRound    = (EndCombatRound_t)0x4D4620;
static ClearAllActions_t   ClearAllActions   = (ClearAllActions_t)0x4CCD80;
static ExoStrCtor_t        ExoStrCtor        = (ExoStrCtor_t)0x5E5A90;
static ExoStrDtor_t        ExoStrDtor        = (ExoStrDtor_t)0x5E5C20;
typedef int (__thiscall* GetPauseState_t)(void* server, int type);
static GetPauseState_t     GetPauseState     = (GetPauseState_t)0x4AE980;

// Creature / combat round / attack data offsets
#define CRE_COMBAT_ROUND   0x9C8
#define CRE_STATS          0xA74
#define CR_OWNER           0x9B4
#define CR_ROUND_STARTED   0x934
#define CR_SPELL_ROUND     0x938
#define CR_TIMER           0x944
#define CR_ROUND_LENGTH    0x94C
#define CR_OVERLAP         0x950
#define CR_PAUSED          0x958
#define CR_PAUSED_BY       0x95C
#define CR_PAUSE_TIMER     0x960
#define CR_INFINITE_PAUSE  0x964
#define CR_ENGAGED         0x9B8
#define CR_MASTER          0x9BC
#define CR_MASTER_ID       0x9C0
#define AD_DAMAGE_LIST     28
#define AD_BASE_DAMAGE     56
#define AD_RESULT          0x5C
#define EQ_CREATURE        0x64
#define EQ_SELECTED_SLOT   0x4278
#define CITEM_NAME         396

// ---------------------------------------------------------------- config / log
static char g_dir[MAX_PATH];
static int  g_fpsCap = 60, g_readout = 1, g_readoutLines = 6, g_readoutSecs = 10, g_itemCompare = 1;
static int  g_borderless = 1, g_autoUnstick = 1, g_readoutPartyOnly = 1, g_combatFpsCap = 60, g_combatHoldMs = 5000;
static int  g_keyUnstick = VK_F10, g_keyReadout = VK_F11, g_keyDump = VK_F9;
static int  g_stuckFix = 1, g_keyWalk = 'B', g_freeLook = 1, g_holdSkip = 1, g_keySkip = VK_SPACE;
static int  g_noModeSwitch = 1, g_companion = 1, g_walk;
static FILE* g_log;
static LARGE_INTEGER g_qpf;

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

static char g_ini[MAX_PATH];
static int  g_keyMenu = VK_F8;
static void LoadConfig() {
    char* ini = g_ini; sprintf_s(g_ini, "%s\\KotorPlus.ini", g_dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        FILE* f; if (fopen_s(&f, ini, "w") == 0) {
            fputs("; KotorPlus settings. Delete this file to restore defaults.\n"
                  "[KotorPlus]\n"
                  "; Frame cap. 0 = uncapped.\n"
                  "FpsCap=240\n"
                  "; Optional lower frame cap during combat and CombatCapHoldMs after it. 0 = off\n"
                  "CombatFpsCap=0\nCombatCapHoldMs=5000\n"
                  "; Fix for 'can't move after combat' (hold a direction for a moment to break free)\n"
                  "StuckFix=1\n"
                  "; Walk/run toggle key (66 = B)\n"
                  "WalkKey=66\n"
                  "; Hold middle mouse: mouse turns the camera, you keep walking the same way\n"
                  "FreeLook=1\n"
                  "; Hold SkipKey (32 = Space) in a conversation to play it this many times faster (0 = off, 2-4)\n"
                  "DialogSpeed=3\nSkipKey=32\n"
                  "; Don't switch the monitor's display mode at startup (no black screens)\n"
                  "NoModeSwitch=1\n"
#ifndef KP_PUBLIC
                  "; KotorPlus companion (needs kp_comp.ncs in Override)\n"
                  "Companion=0\n"
#endif
                  "; Key for the in-game KotorPlus settings panel (119 = F8)\n"
                  "MenuKey=119\n"
                  "; On-screen combat readout (toggle in game with ReadoutKey)\n"
                  "CombatReadout=0\nReadoutLines=6\nReadoutSeconds=10\n"
                  "; 1 = only show attacks by or against you and your party\n"
                  "ReadoutPartyOnly=1\n"
                  "; Equip screen shows the item you already have in that slot\n"
                  "ItemCompare=1\n"
                  "; Borderless window (needs FullScreen=0 and AllowWindowedMode=1 in swkotor.ini)\n"
                  "Borderless=1\n"
                  "; Detects a wedged combat round (timer frozen 4s, game not paused) and ends it\n"
                  "AutoUnstick=1\n"
                  "; Virtual-key codes: 120=F9 121=F10 122=F11\n"
                  "DumpKey=120\nUnstickKey=121\nReadoutKey=122\n", f);
            fclose(f);
        }
    }
    g_fpsCap       = GetPrivateProfileIntA("KotorPlus", "FpsCap", 60, ini);
    g_readout      = GetPrivateProfileIntA("KotorPlus", "CombatReadout", 0, ini);
    g_readoutLines = GetPrivateProfileIntA("KotorPlus", "ReadoutLines", 6, ini);
    g_readoutSecs  = GetPrivateProfileIntA("KotorPlus", "ReadoutSeconds", 10, ini);
    g_itemCompare  = GetPrivateProfileIntA("KotorPlus", "ItemCompare", 1, ini);
    g_borderless   = GetPrivateProfileIntA("KotorPlus", "Borderless", 1, ini);
    g_autoUnstick  = GetPrivateProfileIntA("KotorPlus", "AutoUnstick", 1, ini);
    g_readoutPartyOnly = GetPrivateProfileIntA("KotorPlus", "ReadoutPartyOnly", 1, ini);
    g_combatFpsCap = GetPrivateProfileIntA("KotorPlus", "CombatFpsCap", 0, ini);
    g_combatHoldMs = GetPrivateProfileIntA("KotorPlus", "CombatCapHoldMs", 5000, ini);
    g_keyDump      = GetPrivateProfileIntA("KotorPlus", "DumpKey", VK_F9, ini);
    g_keyUnstick   = GetPrivateProfileIntA("KotorPlus", "UnstickKey", VK_F10, ini);
    g_keyReadout   = GetPrivateProfileIntA("KotorPlus", "ReadoutKey", VK_F11, ini);
    g_stuckFix     = GetPrivateProfileIntA("KotorPlus", "StuckFix", 1, ini);
    g_keyWalk      = GetPrivateProfileIntA("KotorPlus", "WalkKey", 'B', ini);
    g_freeLook     = GetPrivateProfileIntA("KotorPlus", "FreeLook", 1, ini);
    g_holdSkip     = GetPrivateProfileIntA("KotorPlus", "DialogSpeed", 3, ini);
    g_keySkip      = GetPrivateProfileIntA("KotorPlus", "SkipKey", VK_SPACE, ini);
    g_noModeSwitch = GetPrivateProfileIntA("KotorPlus", "NoModeSwitch", 1, ini);
#ifdef KP_PUBLIC
    g_companion    = 0;
#else
    g_companion    = GetPrivateProfileIntA("KotorPlus", "Companion", 0, ini);
#endif
    g_keyMenu      = GetPrivateProfileIntA("KotorPlus", "MenuKey", VK_F8, ini);
    if (g_readoutLines < 1) g_readoutLines = 1;
    if (g_readoutLines > 16) g_readoutLines = 16;
}

// ---------------------------------------------------------------- patch helpers
// Redirect an E8 call at `site` (that must currently call `expected`) to `hook`.
static bool HookCall(uintptr_t site, uintptr_t expected, void* hook, const char* name) {
    uint8_t* p = (uint8_t*)site;
    if (IsBadReadPtr(p, 5) || p[0] != 0xE8 || site + 5 + *(int32_t*)(p + 1) != expected) {
        Log("SKIP %s: unexpected bytes at %08X (wrong exe version or still encrypted)", name, (unsigned)site);
        return false;
    }
    DWORD old; VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t*)(p + 1) = (int32_t)((uintptr_t)hook - (site + 5));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    Log("hooked %s at %08X", name, (unsigned)site);
    return true;
}

static void** FindIAT(HMODULE mod, const char* dll, const char* func) {
    auto base = (uint8_t*)mod;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
        if (_stricmp((char*)(base + imp->Name), dll)) continue;
        auto names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        auto addrs = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto ibn = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (!strcmp((char*)ibn->Name, func)) return (void**)&addrs->u1.Function;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------- combat readout capture
struct AttackLine {
    DWORD tick; void* data; bool final;
    char attacker[40], target[40];
    int roll, mod, ac, result, damage;
};
static AttackLine g_lines[16];
static int g_lineHead = 0;
static struct { void* data; void* attacker; void* target; int roll, mod; bool haveRoll, haveMod; } g_pend;

static void NameOf(void* creature, char* out, size_t n) {
    out[0] = 0;
    if (!creature) return;
    void* stats = *(void**)((char*)creature + CRE_STATS);
    if (!stats) return;
    CExoString s = { 0, 0 };
    GetFullName(stats, &s);
    if (s.str) strncpy_s(out, n, s.str, _TRUNCATE);
    ExoStrDtor(&s);
}

static void* __fastcall Hook_GetAttack(void* round, void*, int idx) {
    void* d = oGetAttack(round, idx);
    g_pend.data = d;
    g_pend.attacker = *(void**)((char*)round + CR_OWNER);
    g_pend.haveRoll = g_pend.haveMod = false;
    return d;
}
static uint16_t __fastcall Hook_RollDice(void* rules, void*, uint8_t num, uint8_t sides) {
    uint16_t r = oRollDice(rules, num, sides);
    g_pend.roll = r; g_pend.haveRoll = true;
    return r;
}
static int __fastcall Hook_AttackMod(void* stats, void*, void* target) {
    int m = oAttackMod(stats, target);
    g_pend.mod = m; g_pend.target = target; g_pend.haveMod = true;
    return m;
}
static void* LeaderCreature();
// True if a server creature is one of the (up to 3) client party members.
static bool IsPartyCreature(void* creature) {
    if (!creature) return false;
    __try {
        uint32_t id = *(uint32_t*)((char*)creature + 4);
        char* internal = *(char**)((char*)CLIENT_APP + 4);
        char* party = internal ? *(char**)(internal + 0x270) : nullptr;
        if (!party) return false;
        uint32_t mask = ~*(uint32_t*)0x74665C;
        int count = *(int*)party;
        for (int i = 0; i < count && i < 3; i++) {
            uint32_t cid = *(uint32_t*)(party + 0x24 + i * 0x88);
            if ((cid & mask) == id) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

static int16_t __fastcall Hook_ArmorClass(void* stats, void*, void* attacker, int flag) {
    int16_t ac = oArmorClass(stats, attacker, flag);
    void* leader = nullptr;
    __try { leader = LeaderCreature(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    bool aP = attacker == leader || IsPartyCreature(attacker);
    bool tP = g_pend.target == leader || IsPartyCreature(g_pend.target);
    bool involvesParty = !g_readoutPartyOnly || aP || tP;
    if (g_pend.haveRoll && g_pend.haveMod && g_readout && involvesParty) {
        AttackLine& L = g_lines[g_lineHead];
        g_lineHead = (g_lineHead + 1) % 16;
        memset(&L, 0, sizeof(L));
        L.tick = GetTickCount(); L.data = g_pend.data;
        L.roll = g_pend.roll; L.mod = g_pend.mod; L.ac = ac;
        NameOf(attacker ? attacker : g_pend.attacker, L.attacker, sizeof(L.attacker));
        // the target pointer passed to GetAttackModifierVersus is the target creature
        NameOf(g_pend.target, L.target, sizeof(L.target));
    }
    g_pend.haveRoll = g_pend.haveMod = false;
    return ac;
}

// Damage and the final result are written later in the same frame; read them once.
static void FinalizeLines() {
    for (auto& L : g_lines) {
        if (!L.tick || L.final || !L.data) continue;
        uint8_t* d = (uint8_t*)L.data;
        L.result = d[AD_RESULT];
        int sum = 0;
        uint16_t* dl = (uint16_t*)(d + AD_DAMAGE_LIST);
        for (int i = 0; i < 15; i++) if (dl[i] != 0xFFFF && dl[i] < 10000) sum += dl[i];
        if (sum == 0) { int16_t b = *(int16_t*)(d + AD_BASE_DAMAGE); if (b > 0) sum = b; }
        L.damage = sum;
        L.final = true;
    }
}

// ---------------------------------------------------------------- item comparison
// The description box scrolls and the item's own text is long, so a short summary of the
// equipped item goes at the TOP (the full equipped details stay at the bottom).
// Picks the stat lines out of the game's property text: "Damage: ...", "Critical Threat:" + its
// value line, defense / attack / ability bonuses. Skips "Feats Required" and the flavour text.
static void KeyStats(const char* props, char* out, size_t n) {
    out[0] = 0;
    if (!props) return;
    char line[256]; int lines = 0; bool joinNext = false, skipping = false;
    for (const char* p = props; *p && lines < 5; ) {
        const char* e = strchr(p, '\n'); size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len); line[len] = 0;
        p = e ? e + 1 : p + len;
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = 0;
        if (!len) { skipping = false; joinNext = false; continue; }
        if (skipping) continue;
        if (strstr(line, "Feats Required")) { skipping = true; continue; }
        if (joinNext) { strcat_s(out, n, " "); strcat_s(out, n, line); strcat_s(out, n, "\n"); joinNext = false; lines++; continue; }
        bool stat = strstr(line, "Damage") || strstr(line, "Critical") || strstr(line, "Defense") || strstr(line, "Armor") ||
                    strstr(line, "Attack") || strstr(line, "Bonus") || strstr(line, "+") || strstr(line, "Dexterity") ||
                    strstr(line, "Immunity") || strstr(line, "Resist");
        if (!stat) continue;
        strcat_s(out, n, "  "); strcat_s(out, n, line);
        if (line[len - 1] == ':') joinNext = true; else { strcat_s(out, n, "\n"); lines++; }
    }
}

static void __stdcall DoSetDescription(void* gui, CExoString* desc, void* hoveredItem) {
    // The coloured hover card (DrawEquipCard) does the comparison now; the game's own description
    // text is left as it was. ItemCompare=2 brings back the text summary.
    if (g_itemCompare != 2 || !desc || !desc->str) { oSetDescription(gui, desc); return; }
    __try {
        void* ccre = *(void**)((char*)gui + EQ_CREATURE);
        int slot = *(int*)((char*)gui + EQ_SELECTED_SLOT);
        uint32_t cid = ccre ? GetEquippedItemID(ccre, slot) : INVALID_OBJ;
        static char buf[8192], stats[1024];
        if (cid == INVALID_OBJ || cid == 0) {
            sprintf_s(buf, "EQUIPPED NOW: nothing in this slot\n\n%s", desc->str);
        } else {
            void* srv = SERVER_APP;
            void* eq = GetServerItem(srv, ClientToServer(srv, cid));
            if (!eq) { oSetDescription(gui, desc); return; }
            if (eq == hoveredItem) {
                sprintf_s(buf, "EQUIPPED NOW: this item\n\n%s", desc->str);
            } else {
                const char* name = "Equipped item";
                void* citem = GetClientItem(eq);
                CExoString* cn = citem ? (CExoString*)((char*)citem + CITEM_NAME) : nullptr;
                if (cn && cn->str && cn->len) name = cn->str;
                CExoString props = { 0, 0 };
                GetPropertyDesc(eq, &props);
                KeyStats(props.str, stats, sizeof(stats));
                sprintf_s(buf, "EQUIPPED NOW: %s\n%s\n%s\n\n--- %s (equipped) ---\n%s",
                          name, stats[0] ? stats : "  (no special properties)\n", desc->str, name,
                          props.str ? props.str : "(no special properties)");
                ExoStrDtor(&props);
            }
        }
        CExoString s = { 0, 0 };
        ExoStrCtor(&s, buf);
        oSetDescription(gui, &s);
        ExoStrDtor(&s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("item compare failed; showing normal description");
        oSetDescription(gui, desc);
    }
}

// ---------------------------------------------------------------- equipment hover card: stat parsing
// Turns the game's property text into numbers, e.g.
//   "Damage: Physical, 2-12" -> Damage 7 (shown "2-12")   "Critical Threat:" + "19-20,x2" -> Critical 2 faces
//   "Attack Modifier: +2"    "Defense Bonus: 6"   "Skills: Awareness +4" -> Awareness 4
struct ItemStat { char key[40]; float v; char disp[24]; };
static void Trim(char* s) {
    size_t n = strlen(s); while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\t')) s[--n] = 0;
    size_t i = 0; while (s[i] == ' ') i++; if (i) memmove(s, s + i, strlen(s + i) + 1);
}
static int ParseItemStats(const char* text, ItemStat* out, int max) {
    int n = 0;
    if (!text) return 0;
    char line[256], pending[64] = "";
    bool skip = false;
    for (const char* p = text; *p && n < max; ) {
        const char* e = strchr(p, '\n'); size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len); line[len] = 0; p = e ? e + 1 : p + len;
        Trim(line);
        if (!line[0]) { skip = false; pending[0] = 0; continue; }
        if (skip) continue;
        if (strstr(line, "Feats Required") || strstr(line, "Restricted") || strstr(line, "Save:")) { skip = true; continue; }
        char key[64], val[192];
        if (pending[0]) { strcpy_s(key, pending); strcpy_s(val, line); pending[0] = 0; }
        else {
            char* c = strchr(line, ':');
            if (!c) continue;                                   // flavour text
            *c = 0; strcpy_s(key, line); strcpy_s(val, c + 1); Trim(key); Trim(val);
            if (!val[0]) { strcpy_s(pending, key); continue; }  // value is on the next line
        }
        if (strstr(key, "Balanced") || strstr(key, "Range") || strstr(key, "Uses") || strstr(key, "Area")) continue;
        ItemStat& s = out[n];
        int a, b, m;
        const char* dash = nullptr;
        for (const char* q = val; *q; q++) if (*q == '-' && q > val && q[-1] >= '0' && q[-1] <= '9') { dash = q; break; }
        if (strstr(key, "Critical") && sscanf_s(val, "%d-%d,x%d", &a, &b, &m) == 3) {
            strcpy_s(s.key, "Critical"); s.v = (float)(b - a + 1) * m; sprintf_s(s.disp, "%d-%d x%d", a, b, m); n++;
        } else if (dash) {                                       // "Physical, 2-12"
            const char* d = dash; while (d > val && d[-1] >= '0' && d[-1] <= '9') d--;
            if (sscanf_s(d, "%d-%d", &a, &b) == 2) {
                strcpy_s(s.key, key); s.v = (a + b) / 2.0f; sprintf_s(s.disp, "%d-%d", a, b); n++;
            }
        } else {                                                  // "+2", "6", "Awareness +4", "Fortitude +2"
            char* num = nullptr;
            for (char* q = val; *q; q++) if ((*q == '+' || *q == '-' || (*q >= '0' && *q <= '9')) && (q == val || q[-1] == ' ')) { num = q; break; }
            if (!num) continue;
            float f = (float)atof(num);
            char name[64]; size_t k = (size_t)(num - val); if (k >= sizeof(name)) k = sizeof(name) - 1;
            memcpy(name, val, k); name[k] = 0; Trim(name);
            strcpy_s(s.key, name[0] ? name : key);
            if (strstr(s.key, "Defense Bonus")) strcpy_s(s.key, "Defense");
            else if (strstr(s.key, "Attack Modifier")) strcpy_s(s.key, "Attack");
            else if (strstr(s.key, "Max Dexterity")) strcpy_s(s.key, "Max Dex");
            s.v = f; sprintf_s(s.disp, "%s%g", (f > 0 && strchr(num, '+')) ? "+" : "", f); n++;
        }
    }
    return n;
}

// Call site 0x6B7A19: hovered server item is in EDI, desc pointer is on the stack.
__declspec(naked) static void Thunk_SetDescription() {
    __asm {
        mov eax, [esp + 4]
        push edi
        push eax
        push ecx
        call DoSetDescription
        ret 4
    }
}

// ---------------------------------------------------------------- freeze recorder
// Function-entry hooks that only record (all registers/stack preserved), so we can see what
// the combat state machine did right before the player got stuck.
enum EvId { EV_START_ROUND = 1, EV_END_ROUND, EV_S_COMBATSTATE, EV_C_COMBATSTATE, EV_ATTACK };
static const char* EvName(int id) {
    switch (id) {
    case EV_START_ROUND: return "StartCombatRound";
    case EV_END_ROUND: return "EndCombatRound";
    case EV_S_COMBATSTATE: return "Server SetCombatState";
    case EV_C_COMBATSTATE: return "Client SetCombatState";
    case EV_ATTACK: return "AttackRoll";
    }
    return "?";
}
struct Ev { LONGLONG t; uint32_t frame; int id; void* obj; int arg; int timer, len; };
static Ev g_ev[1024]; static volatile long g_evN;
static uint32_t g_frame;
static LONGLONG QpcNow() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }

static DWORD* g_pLastCombatTick;  // adaptive frame cap
static void* g_cachedLeader;
static void __stdcall RecordEvent(int id, void* self, int arg) {
    Ev& e = g_ev[g_evN++ & 1023];
    e.t = QpcNow(); e.frame = g_frame; e.id = id; e.obj = self; e.arg = arg; e.timer = e.len = 0;
    if (id == EV_START_ROUND || id == EV_END_ROUND) {   // self = combat round
        __try {
            e.timer = *(int*)((char*)self + CR_TIMER);
            e.len = *(int*)((char*)self + CR_ROUND_LENGTH);
            e.obj = *(void**)((char*)self + CR_OWNER);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_pLastCombatTick && g_cachedLeader && e.obj == g_cachedLeader &&
        (id == EV_START_ROUND || (id == EV_S_COMBATSTATE && arg)))
        *g_pLastCombatTick = GetTickCount();
}

static void* g_tramp[8];
// Builds: <copied prologue bytes> jmp target+len
static void* MakeTrampoline(uintptr_t target, int len) {
    uint8_t* t = (uint8_t*)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(t, (void*)target, len);
    t[len] = 0xE9; *(int32_t*)(t + len + 1) = (int32_t)((target + len) - ((uintptr_t)t + len + 5));
    return t;
}
static bool Detour(uintptr_t target, int len, const uint8_t* expect, void* stub, int slot, const char* name) {
    if (memcmp((void*)target, expect, len)) { Log("SKIP recorder hook %s: unexpected bytes", name); return false; }
    g_tramp[slot] = MakeTrampoline(target, len);
    DWORD old; VirtualProtect((void*)target, len, PAGE_EXECUTE_READWRITE, &old);
    uint8_t* p = (uint8_t*)target;
    p[0] = 0xE9; *(int32_t*)(p + 1) = (int32_t)((uintptr_t)stub - (target + 5));
    for (int i = 5; i < len; i++) p[i] = 0x90;
    VirtualProtect((void*)target, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    Log("recorder hooked %s", name);
    return true;
}

// stub: save everything, RecordEvent(id, ecx, [esp+4]), restore, continue in trampoline
#define REC_STUB(fn, ID, SLOT)                                   \
    __declspec(naked) static void fn() {                         \
        __asm pushad                                             \
        __asm pushfd                                             \
        __asm mov eax, [esp + 40]                                \
        __asm push eax                                           \
        __asm push ecx                                           \
        __asm push ID                                            \
        __asm call RecordEvent                                   \
        __asm popfd                                              \
        __asm popad                                              \
        __asm jmp dword ptr [g_tramp + SLOT * 4]                 \
    }
REC_STUB(Stub_StartRound, 1, 0)
REC_STUB(Stub_EndRound, 2, 1)
REC_STUB(Stub_SCombatState, 3, 2)
REC_STUB(Stub_CCombatState, 4, 3)

static void InstallRecorder() {
    const uint8_t sStart[] = { 0x8B, 0x44, 0x24, 0x08, 0x53 };
    const uint8_t sFs[]    = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    const uint8_t sCSet[]  = { 0x53, 0x8B, 0x5C, 0x24, 0x08 };
    Detour(0x4D5F70, 5, sStart, (void*)Stub_StartRound, 0, "CSWSCombatRound::StartCombatRound");
    Detour(0x4D4620, 6, sFs, (void*)Stub_EndRound, 1, "CSWSCombatRound::EndCombatRound");
    Detour(0x4F2610, 6, sFs, (void*)Stub_SCombatState, 2, "CSWSCreature::SetCombatState");
    Detour(0x6182A0, 5, sCSet, (void*)Stub_CCombatState, 3, "CSWCCreature::SetCombatState");
}

// per-frame history
static float g_frameMs[256]; static int g_creDelta[256];
static LONGLONG g_lastFrameQpc;

static void DumpRecorder(void* leader) {
    double f = (double)g_qpf.QuadPart;
    LONGLONG now = QpcNow();
    Log("---- recorder: last events for the party leader (newest last) ----");
    long n = g_evN; int shown = 0;
    for (long i = (n > 1024 ? n - 1024 : 0); i < n; i++) {
        Ev& e = g_ev[i & 1023];
        if (e.obj != leader) continue;
        if (n - i > 400) continue;
        Log("  %7.2fs ago  frame %u  %-22s arg=%d timer=%d len=%d", (now - e.t) / f, e.frame, EvName(e.id), e.arg, e.timer, e.len);
        shown++;
    }
    if (!shown) Log("  (no combat events recorded for the leader)");
    char line[1024]; int o = 0;
    o += sprintf_s(line + o, sizeof(line) - o, "  last 60 frames (ms / creature delta ms):");
    for (int k = 60; k >= 1; k--) {
        int i = (g_frame - k) & 255;
        o += sprintf_s(line + o, sizeof(line) - o, " %.1f/%d", g_frameMs[i], g_creDelta[i]);
        if (o > 900) break;
    }
    Log("%s", line);
    __try {
        // CExoLinkedList at CSWSObject+252 -> internal {head, tail, count}; node {prev, next, data}
        void** internal = *(void***)((char*)leader + 252);
        if (internal) {
            int count = (int)(intptr_t)internal[2];
            o = sprintf_s(line, "  action queue: %d node(s):", count);
            void** node = (void**)internal[0];
            for (int k = 0; node && k < 8; k++, node = (void**)node[1]) {
                uint32_t* data = (uint32_t*)node[2];
                if (data) o += sprintf_s(line + o, sizeof(line) - o, " [id=%u grp=%u]", data[0], *(uint16_t*)((char*)data + 108));
            }
            Log("%s", line);
        }
        Log("  creature anim=%d", *(int*)((char*)leader + 212));
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("  action queue: unreadable"); }
}

// ---------------------------------------------------------------- hotkeys: unstick / dump
static void* LeaderCreature() {
    void* srv = SERVER_APP;
    return srv ? GetPlayerCreature(srv) : nullptr;
}

static void DumpState(const char* why) {
    __try {
        char* c = (char*)LeaderCreature();
        if (!c) { Log("%s: no leader creature", why); return; }
        char* r = *(char**)(c + CRE_COMBAT_ROUND);
        char name[40]; NameOf(c, name, sizeof(name));
        if (!r) { Log("%s: %s has no combat round", why, name); return; }
        Log("%s: %s started=%d spell=%d timer=%d len=%d overlap=%d paused=%d pausedBy=%08X pauseTimer=%d infinite=%d engaged=%d master=%d masterId=%08X fpsCap=%d",
            why, name, *(int*)(r + CR_ROUND_STARTED), *(int*)(r + CR_SPELL_ROUND), *(int*)(r + CR_TIMER),
            *(int*)(r + CR_ROUND_LENGTH), *(int*)(r + CR_OVERLAP), *(int*)(r + CR_PAUSED),
            *(unsigned*)(r + CR_PAUSED_BY), *(int*)(r + CR_PAUSE_TIMER), *(int*)(r + CR_INFINITE_PAUSE),
            *(int*)(r + CR_ENGAGED), *(int*)(r + CR_MASTER), *(unsigned*)(r + CR_MASTER_ID), g_fpsCap);
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("%s: dump failed", why); }
}

typedef void* (__thiscall* GetGameObject_t)(void* server, uint32_t id);
static GetGameObject_t GetGameObject = (GetGameObject_t)0x4AE750;

typedef void* (__thiscall* CGetPlayerCreature_t)(void* client);
typedef void (__thiscall* CVoid_t)(void* obj);
typedef void (__thiscall* CSetInt_t)(void* obj, int v);
static CGetPlayerCreature_t ClientPlayerCreature = (CGetPlayerCreature_t)0x5ED540;
static CVoid_t ClientClearQueuedAnimCb = (CVoid_t)0x60F1A0;
static CVoid_t ClientClearAllActions = (CVoid_t)0x63D470;
static CSetInt_t ClientSetCombatState = (CSetInt_t)0x6182A0;

static void DumpClient(const char* why) {
    __try {
        char* cc = (char*)ClientPlayerCreature(CLIENT_APP);
        if (!cc) { Log("%s: no client creature", why); return; }
        Log("%s client: queuedAnimCb=%p doneAtk1=%p doneAtk2=%p hitCb=%p loopAnim=%u running=%d anim_base=%p",
            why, *(void**)(cc + 0xF4), *(void**)(cc + 1052), *(void**)(cc + 1056), *(void**)(cc + 1008),
            *(uint16_t*)(cc + 88), *(int*)(cc + 992), *(void**)(cc + 104));
        char line[400]; int o = sprintf_s(line, "%s client raw 0xE0-0x120:", why);
        for (int k = 0xE0; k < 0x120; k += 4) o += sprintf_s(line + o, sizeof(line) - o, " %08X", *(uint32_t*)(cc + k));
        Log("%s", line);
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("%s: client dump failed", why); }
}

static void ResetClient() {
    __try {
        char* cc = (char*)ClientPlayerCreature(CLIENT_APP);
        if (!cc) return;
        ClientClearQueuedAnimCb(cc);
        ClientSetCombatState(cc, 0);
        ClientClearAllActions(cc);
        Log("client reset: queued anim callback cleared, combat state 0, client actions cleared");
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("client reset failed"); }
}

// Same reset the engine does in IncrementTimer when the combat "master" cannot be found:
// drop the engagement, forget the master, end the round.
static void ResetEngagement(char* r) {
    *(int*)(r + CR_ENGAGED) = 0;
    *(int*)(r + CR_MASTER) = 0;
    *(uint32_t*)(r + CR_MASTER_ID) = INVALID_OBJ;
    EndCombatRound(r, 1);
}

static DWORD g_toastTick; static char g_toast[96];
// On-screen notices are off: the game should look stock apart from the F8 panel. Log only.
static void Toast(const char* s) { Log("notice: %s", s); }

static void Unstick() {
    DumpState("unstick (before)");
    __try {
        char* c = (char*)LeaderCreature();
        if (!c) return;
        char* r = *(char**)(c + CRE_COMBAT_ROUND);
        if (r) ResetEngagement(r);
        ClearAllActions(c, 1);
        DumpClient("unstick (before client reset)");
        ResetClient();
        Toast("KotorPlus: unstuck - combat round ended, actions cleared");
        Log("unstick done");
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("unstick failed"); }
}

static bool KeyPressed(int vk, bool& prev) {
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool hit = down && !prev; prev = down; return hit;
}

static bool OurWindowFocused() {
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// ---------------------------------------------------------------- overlay drawing
static HGLRC g_fontCtx; static GLuint g_fontBase; static int g_fontH = 16;

static void EnsureFont(HDC dc, int screenH) {
    HGLRC ctx = wglGetCurrentContext();
    if (ctx == g_fontCtx && g_fontBase) return;
    g_fontCtx = ctx;
    g_fontH = screenH >= 1400 ? 22 : screenH >= 1000 ? 18 : 15;
    HFONT f = CreateFontA(-g_fontH, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS,
                          CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, "Consolas");
    HGDIOBJ old = SelectObject(dc, f);
    g_fontBase = glGenLists(96);
    wglUseFontBitmapsA(dc, 32, 96, g_fontBase);
    SelectObject(dc, old); DeleteObject(f);
}

static void Text(int x, int y, const char* s, float r, float g, float b) {
    glColor4f(0, 0, 0, 1); glRasterPos2i(x + 1, y + 1);
    glListBase(g_fontBase - 32); glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
    glColor4f(r, g, b, 1); glRasterPos2i(x, y);
    glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
}

static void Rect(int x, int y, int w, int h, float a) {
    glColor4f(0.02f, 0.03f, 0.06f, a);
    glBegin(GL_QUADS); glVertex2i(x, y); glVertex2i(x + w, y); glVertex2i(x + w, y + h); glVertex2i(x, y + h); glEnd();
}

static int HitChance(int mod, int ac) {
    int hits = 1;                        // natural 20 always hits
    for (int r = 2; r <= 19; r++) if (r + mod >= ac) hits++;
    return hits * 5;
}

// ---------------------------------------------------------------- Ctrl+F12 screenshot (game + overlay)
// Windows' own screen capture often shows a stale frame of this window, so read the finished
// back buffer ourselves and write a BMP next to the exe.
static bool g_shotRequested;
static void SaveScreenshot(int W, int H) {
    int stride = (W * 3 + 3) & ~3;
    uint8_t* buf = (uint8_t*)malloc((size_t)stride * H);
    if (!buf) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, W, H, 0x80E0 /*GL_BGR*/, GL_UNSIGNED_BYTE, buf);   // bottom-up, like BMP
    SYSTEMTIME t; GetLocalTime(&t);
    char path[MAX_PATH];
    sprintf_s(path, "%s\\KotorPlus_%04d%02d%02d_%02d%02d%02d.bmp", g_dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    FILE* f;
    if (fopen_s(&f, path, "wb") == 0) {
        BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
        fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + stride * H;
        ih.biSize = sizeof(ih); ih.biWidth = W; ih.biHeight = H; ih.biPlanes = 1; ih.biBitCount = 24;
        fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f); fwrite(buf, stride, H, f); fclose(f);
        Log("screenshot saved: %s", path);
    }
    free(buf);
}

// ---------------------------------------------------------------- F8 settings panel
// A row with val == nullptr is a section header.
struct MenuItem { const char* label; const char* key; int* val; int n; int opts[8]; const char* names[8]; bool restart; };
#define ONOFF 2, { 1, 0 }, { "On", "Off" }
#define HEADER(t) { t, nullptr, nullptr, 0, {}, {}, false }
static MenuItem g_menu[] = {
    HEADER("Performance"),
    { "Frame cap",                      "FpsCap",           &g_fpsCap,       6, { 60, 120, 144, 165, 240, 0 }, { "60", "120", "144", "165", "240", "Uncapped" }, false },
    { "Combat frame cap",               "CombatFpsCap",     &g_combatFpsCap, 4, { 0, 60, 120, 144 }, { "Off", "60", "120", "144" }, false },
    HEADER("Fixes"),
    { "Can't move after combat",        "StuckFix",         &g_stuckFix,     ONOFF, false },
    { "Frozen combat round watchdog",   "AutoUnstick",      &g_autoUnstick,  ONOFF, false },
    HEADER("Controls"),
    { "Walk instead of run  (B)",       nullptr,            &g_walk,         2, { 0, 1 }, { "Off", "On" }, false },
    { "Free look  (hold middle mouse)", "FreeLook",         &g_freeLook,     ONOFF, false },
    { "Hold Space: fast dialogue",      "DialogSpeed",      &g_holdSkip,     4, { 0, 2, 3, 4 }, { "Off", "2x", "3x", "4x" }, false },
    { "Equip screen item compare",      "ItemCompare",      &g_itemCompare,  ONOFF, false },
    HEADER("Display"),
    { "Borderless window",              "Borderless",       &g_borderless,   ONOFF, true },
    { "No black screens at startup",    "NoModeSwitch",     &g_noModeSwitch, ONOFF, true },
};
static const int g_menuN = sizeof(g_menu) / sizeof(g_menu[0]);
static bool g_menuOpen; static int g_menuHover = -1;
static int g_menuRowY[32], g_menuRowH, g_menuX, g_menuW;
static bool g_optVisible, g_optHot, g_closeHot, g_restartPending;
static int g_arrowL0, g_arrowL1, g_arrowR0, g_arrowR1;   // click zones of the < and > selectors
static RECT g_optRect, g_closeRect;

static void RectC(int x, int y, int w, int h, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS); glVertex2i(x, y); glVertex2i(x + w, y); glVertex2i(x + w, y + h); glVertex2i(x, y + h); glEnd();
}
static const char* MenuValue(const MenuItem& m) {
    for (int k = 0; k < m.n; k++) if (*m.val == m.opts[k]) return m.names[k];
    static char buf[16]; sprintf_s(buf, "%d", *m.val); return buf;
}

// Anti-aliased text: glyphs rendered once with GDI into a texture atlas (wglUseFontBitmaps is 1-bit).
struct AAFont { GLuint tex; int h, ascent; float u0[96], v0[96], u1[96], v1[96]; int w[96]; };
static AAFont g_fBody, g_fTitle, g_fSmall;
static void BuildFont(AAFont& f, int px, int weight, const char* face, int spacing) {
    const int S = 512;
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = S; bi.bmiHeader.biHeight = -S; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC mdc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ ob = SelectObject(mdc, bmp);
    HFONT font = CreateFontA(-px, 0, 0, 0, weight, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    HGDIOBJ of = SelectObject(mdc, font);
    SetTextColor(mdc, RGB(255, 255, 255)); SetBkColor(mdc, RGB(0, 0, 0)); SetBkMode(mdc, OPAQUE);
    TEXTMETRICA tm; GetTextMetricsA(mdc, &tm);
    f.h = tm.tmHeight; f.ascent = tm.tmAscent;
    int x = 1, y = 1;
    for (int c = 32; c < 128; c++) {
        char ch = (char)c; SIZE sz; GetTextExtentPoint32A(mdc, &ch, 1, &sz);
        int cw = sz.cx + 2;
        if (x + cw >= S) { x = 1; y += f.h + 2; }
        TextOutA(mdc, x + 1, y, &ch, 1);
        int i = c - 32;
        f.u0[i] = (float)x / S; f.v0[i] = (float)y / S; f.u1[i] = (float)(x + cw) / S; f.v1[i] = (float)(y + f.h) / S;
        f.w[i] = sz.cx + spacing;
        x += cw + 1;
    }
    GdiFlush();
    uint32_t* px32 = (uint32_t*)bits;
    for (int i = 0; i < S * S; i++) { uint32_t g = px32[i] & 0xFF; px32[i] = (g << 24) | 0xFFFFFF; }   // white, alpha = coverage
    glGenTextures(1, &f.tex); glBindTexture(GL_TEXTURE_2D, f.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, S, S, 0, 0x80E1 /*GL_BGRA*/, GL_UNSIGNED_BYTE, bits);
    SelectObject(mdc, of); DeleteObject(font); SelectObject(mdc, ob); DeleteObject(bmp); DeleteDC(mdc);
}
static int TextW(const AAFont& f, const char* s) { int w = 0; for (; *s; s++) if (*s >= 32 && *s < 128) w += f.w[*s - 32]; return w; }
static void TextAA(const AAFont& f, int x, int y, const char* s, float r, float g, float b, float a = 1.0f) {
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, f.tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    for (int pass = 0; pass < 2; pass++) {           // soft shadow, then the text
        float ox = pass ? 0.f : 1.f, oy = pass ? 0.f : 1.f;
        if (pass) glColor4f(r, g, b, a); else glColor4f(0, 0, 0, a * 0.8f);
        int cx = x;
        for (const char* p = s; *p; p++) {
            if (*p < 32 || *p >= 128) continue;
            int i = *p - 32;
            float gw = (f.u1[i] - f.u0[i]) * 512, gh = (float)f.h;
            glTexCoord2f(f.u0[i], f.v0[i]); glVertex2f(cx + ox, y + oy);
            glTexCoord2f(f.u1[i], f.v0[i]); glVertex2f(cx + ox + gw, y + oy);
            glTexCoord2f(f.u1[i], f.v1[i]); glVertex2f(cx + ox + gw, y + oy + gh);
            glTexCoord2f(f.u0[i], f.v1[i]); glVertex2f(cx + ox, y + oy + gh);
            cx += f.w[i];
        }
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}
static void Upper(char* d, size_t n, const char* s) { size_t i = 0; for (; s[i] && i + 1 < n; i++) d[i] = (char)toupper((unsigned char)s[i]); d[i] = 0; }

// KOTOR-style frame: octagon (cut corners), dark navy fill, bright blue edge with a soft outer glow.
static void Frame(int x, int y, int w, int h, int cut, float fillA) {
    float px[8] = { (float)x + cut, (float)x + w - cut, (float)x + w, (float)x + w, (float)x + w - cut, (float)x + cut, (float)x, (float)x };
    float py[8] = { (float)y, (float)y, (float)y + cut, (float)y + h - cut, (float)y + h, (float)y + h, (float)y + h - cut, (float)y + cut };
    glBegin(GL_POLYGON);
    for (int i = 0; i < 8; i++) {
        float t = (py[i] - y) / h;                                   // top lighter, bottom darker
        glColor4f(0.04f + 0.03f * (1 - t), 0.09f + 0.05f * (1 - t), 0.17f + 0.08f * (1 - t), fillA);
        glVertex2f(px[i], py[i]);
    }
    glEnd();
    const float glow[3][2] = { { 5.0f, 0.10f }, { 3.0f, 0.22f }, { 1.6f, 0.95f } };   // width, alpha
    for (auto& gl : glow) {
        glLineWidth(gl[0]);
        glColor4f(0.30f, 0.62f, 0.95f, gl[1]);
        glBegin(GL_LINE_LOOP); for (int i = 0; i < 8; i++) glVertex2f(px[i], py[i]); glEnd();
    }
    glLineWidth(1);
}

static int EnsureMenuFonts(int H) {
    static int builtFor;
    int bodyPx = H / 44 < 16 ? 16 : H / 44;
    if (builtFor != H) {
        builtFor = H;
        if (g_fBody.tex) { glDeleteTextures(1, &g_fBody.tex); glDeleteTextures(1, &g_fTitle.tex); glDeleteTextures(1, &g_fSmall.tex); }
        BuildFont(g_fBody, bodyPx, FW_SEMIBOLD, "Bahnschrift SemiBold", 1);
        BuildFont(g_fTitle, bodyPx * 3 / 2, FW_BOLD, "Bahnschrift SemiBold", 3);
        BuildFont(g_fSmall, bodyPx * 4 / 5, FW_NORMAL, "Bahnschrift", 1);
    }
    return bodyPx;
}

static void DrawMenu(int W, int H) {
    int bodyPx = EnsureMenuFonts(H);
    const float LBL[3] = { 0.42f, 0.72f, 0.98f }, HOT[3] = { 0.98f, 0.92f, 0.36f }, VAL[3] = { 0.92f, 0.95f, 1.0f };
    int rowH = g_fBody.h + bodyPx / 2, headH = g_fBody.h + bodyPx / 3;
    int pad = bodyPx * 3 / 2, w = bodyPx * 30;
    int titleH = g_fTitle.h + bodyPx;
    int h = titleH + pad / 2 + pad;
    for (int i = 0; i < g_menuN; i++) h += g_menu[i].val ? rowH : headH + (i ? bodyPx / 2 : 0);
    h += g_fSmall.h + pad;
    int x0 = (W - w) / 2, y0 = (H - h) / 2;
    RectC(0, 0, W, H, 0.0f, 0.01f, 0.03f, 0.55f);                   // dim the game behind
    Frame(x0, y0, w, h, bodyPx, 0.94f);
    // title bar
    Frame(x0 + bodyPx * 4, y0 - g_fTitle.h / 2, w - bodyPx * 8, titleH - bodyPx / 3, bodyPx / 2, 1.0f);
    int tw = TextW(g_fTitle, "KOTORPLUS");
    TextAA(g_fTitle, x0 + (w - tw) / 2, y0 - g_fTitle.h / 2 + (titleH - bodyPx / 3 - g_fTitle.h) / 2, "KOTORPLUS", 0.85f, 0.93f, 1.0f);
    int y = y0 + titleH + pad / 2;
    g_menuX = x0 + pad / 2; g_menuW = w - pad; g_menuRowH = rowH;
    for (int i = 0; i < g_menuN; i++) {
        const MenuItem& m = g_menu[i];
        char up[96];
        if (!m.val) {
            if (i) y += bodyPx / 2;
            Upper(up, sizeof(up), m.label);
            TextAA(g_fSmall, x0 + pad, y + (headH - g_fSmall.h) / 2, up, 0.55f, 0.62f, 0.72f);
            int lx = x0 + pad + TextW(g_fSmall, up) + bodyPx / 2;
            RectC(lx, y + headH / 2, x0 + w - pad - lx, 1, 0.30f, 0.55f, 0.85f, 0.45f);
            g_menuRowY[i] = -1;
            y += headH;
            continue;
        }
        g_menuRowY[i] = y;
        bool hot = i == g_menuHover;
        if (hot) {
            RectC(x0 + pad / 2, y, w - pad, rowH, 0.12f, 0.30f, 0.55f, 0.45f);
            RectC(x0 + pad / 2, y, 3, rowH, HOT[0], HOT[1], HOT[2], 0.9f);
        }
        const float* lc = hot ? HOT : LBL;
        int ty = y + (rowH - g_fBody.h) / 2;
        TextAA(g_fBody, x0 + pad, ty, m.label, lc[0], lc[1], lc[2]);
        char val[32]; Upper(val, sizeof(val), MenuValue(m));
        bool off = !strcmp(val, "OFF");
        int vw = TextW(g_fBody, val), aw = TextW(g_fBody, "<");
        int right = x0 + w - pad, boxW = bodyPx * 6;
        int cx = right - boxW / 2;
        g_arrowL0 = right - boxW - aw * 2; g_arrowL1 = right - boxW + aw * 3;
        g_arrowR0 = right - aw * 4;        g_arrowR1 = right + aw * 2;
        float a = hot ? 1.0f : 0.55f;
        TextAA(g_fBody, right - boxW, ty, "<", lc[0], lc[1], lc[2], a);
        TextAA(g_fBody, right - aw, ty, ">", lc[0], lc[1], lc[2], a);
        if (off) TextAA(g_fBody, cx - vw / 2, ty, val, 0.55f, 0.6f, 0.68f);
        else TextAA(g_fBody, cx - vw / 2, ty, val, VAL[0], VAL[1], VAL[2]);
        y += rowH;
    }
    if (g_restartPending) {
        const char* hint = "RESTART THE GAME TO APPLY THE DISPLAY CHANGE";
        TextAA(g_fSmall, x0 + (w - TextW(g_fSmall, hint)) / 2, y + pad / 4, hint, HOT[0], HOT[1], HOT[2], 0.9f);
    }
    // Close button on the bottom edge, like the game's own screens
    int cw = bodyPx * 9, ch = g_fBody.h + bodyPx / 2;
    int cx0 = x0 + (w - cw) / 2, cy0 = y0 + h - ch / 2;
    g_closeRect = { cx0, cy0, cx0 + cw, cy0 + ch };
    Frame(cx0, cy0, cw, ch, bodyPx / 3, 1.0f);
    const float* cc = g_closeHot ? HOT : LBL;
    TextAA(g_fBody, cx0 + (cw - TextW(g_fBody, "CLOSE")) / 2, cy0 + (ch - g_fBody.h) / 2, "CLOSE", cc[0], cc[1], cc[2]);
}

// ---------------------------------------------------------------- equipment hover card
// On the Equipment screen, hovering an item in the list shows a card next to the list comparing it
// with what is equipped in that slot: green +, red -, grey = for each stat.
// Up to two references: the item picked in the list (column 1) and, when you have picked something
// else, the item that is really equipped (column 2). has1/has2: that reference exists.
struct CardRow { char key[40]; char disp[24]; float d1, d2; bool lost; };
static bool g_cardVisible, g_cardSame, g_cardEmpty, g_cardTwo;
static POINT g_mouse;                                                       // client coords, set each frame
static char g_cardTitle[64], g_cardVs[64], g_cardVs2[64];
static CardRow g_cardRows[16]; static int g_cardN;
static RECT g_cardList; static int g_cardRowY;

// The GUI manager keeps popups on a modal stack (+0x94) and normal screens in a panel list (+0x88).
// Main-menu Options is modal; the in-game menu screens (Equipment, in-game Options) are normal
// panels. Returns the screen with vtable `vt` if it is the one the player is looking at.
static char* FindScreen(uint32_t vt) {
    char* gm = *(char**)0x7A39F4;
    if (!gm) return nullptr;
    char** modal = *(char***)(gm + 0x94); int nm = *(int*)(gm + 0x98);
    if (modal && nm > 0) return *(uint32_t*)modal[nm - 1] == vt ? modal[nm - 1] : nullptr;
    char** panels = *(char***)(gm + 0x88); int np = *(int*)(gm + 0x8C);
    for (int i = 0; panels && i < np; i++) if (*(uint32_t*)panels[i] == vt) return panels[i];
    return nullptr;
}

static void ItemName(void* sitem, char* out, size_t n) {
    out[0] = 0;
    void* ci = sitem ? GetClientItem(sitem) : nullptr;
    CExoString* s = ci ? (CExoString*)((char*)ci + CITEM_NAME) : nullptr;
    if (s && s->str) strncpy_s(out, n, s->str, _TRUNCATE);
}
static int ItemStats(void* sitem, ItemStat* out, int max) {
    CExoString props = { 0, 0 };
    GetPropertyDesc(sitem, &props);
    int n = ParseItemStats(props.str, out, max);
    ExoStrDtor(&props);
    return n;
}

static void UpdateEquipCard() {
    static void *lastItem, *lastEq; static int lastSlot = -1;
    g_cardVisible = false;
    if (!g_itemCompare || g_menuOpen) return;
    __try {
        char* eqs = FindScreen(0x7569A0);                         // CSWGuiInGameEquip
        if (!eqs) return;
        // Remember the really-equipped item's name while it still resolves (before anything is
        // picked); afterwards only a nameless copy of it is left at +0x42B0.
        // +0x42AC is only filled in once something is picked, so before that the plain equipped
        // lookup still returns the real item: keep its name per slot.
        static int namedSlot = -1; static char origName[64];
        {
            uint32_t oc = *(uint32_t*)(eqs + 0x42AC);
            void* cc = *(void**)(eqs + EQ_CREATURE);
            int sl = *(int*)(eqs + EQ_SELECTED_SLOT);
            uint32_t pc = cc ? GetEquippedItemID(cc, sl) : INVALID_OBJ;
            if ((oc == INVALID_OBJ || !oc || oc == pc) && pc != INVALID_OBJ && pc) {
                void* s = SERVER_APP;
                if (void* it = GetServerItem(s, ClientToServer(s, pc))) { ItemName(it, origName, sizeof(origName)); namedSlot = sl; }
            }
        }
        char* lb = eqs + 0x30D8;                                  // items list box
        char* hov = *(char**)(lb + 0x2DC);                        // hovered control
        if (!hov || *(uint32_t*)hov != 0x7568F8) return;          // CSWGuiInGameItemEntry
        uint32_t cid = *(uint32_t*)(hov + 0x1C4);
        if (cid == INVALID_OBJ || !cid) return;
        void* srv = SERVER_APP;
        void* item = GetServerItem(srv, ClientToServer(srv, cid));
        if (!item) return;
        // Two references: the item picked in the list (clicking previews it on the character, so
        // GetEquippedItemID returns it) and the item that was really equipped when the slot was
        // opened (+0x42AC, the entry the game labels "(Equipped)"; 0x7F000000 if it was empty).
        void* ccre = *(void**)(eqs + EQ_CREATURE);
        int slot = *(int*)(eqs + EQ_SELECTED_SLOT);
        uint32_t pcid = ccre ? GetEquippedItemID(ccre, slot) : INVALID_OBJ;
        uint32_t ocid = *(uint32_t*)(eqs + 0x42AC);
        void* eq = (pcid != INVALID_OBJ && pcid) ? GetServerItem(srv, ClientToServer(srv, pcid)) : nullptr;
        void* orig = (ocid != INVALID_OBJ && ocid) ? GetServerItem(srv, ClientToServer(srv, ocid)) : nullptr;
        // while something else is picked the original is swapped out and its id no longer resolves;
        // the screen keeps a copy of it at +0x42B0 (OnItemSelected describes that copy too)
        if (!orig && ocid != INVALID_OBJ && ocid) orig = *(void**)(eqs + 0x42B0);
        if (orig == eq || (eq && ocid == pcid)) orig = nullptr;  // nothing different picked: one column
        int* le = (int*)(lb + 4);
        g_cardList = { le[0], le[1], le[0] + le[2], le[1] + le[3] };
        // the screen can stay in the panel list behind another menu tab: only while the mouse is on the list
        if (g_mouse.x < g_cardList.left || g_mouse.x >= g_cardList.right || g_mouse.y < g_cardList.top || g_mouse.y >= g_cardList.bottom) return;
        int* he = (int*)(hov + 4);
        g_cardRowY = (he[1] >= le[1] && he[1] < le[1] + le[3]) ? he[1] : le[1];
        g_cardVisible = true;
        static void* lastOrig;
        if (item == lastItem && eq == lastEq && orig == lastOrig && slot == lastSlot) return;
        lastItem = item; lastEq = eq; lastOrig = orig; lastSlot = slot;

        ItemName(item, g_cardTitle, sizeof(g_cardTitle));
        g_cardTwo = orig != nullptr;
        g_cardSame = item == eq && !g_cardTwo;                    // hovering what is equipped, nothing else picked
        g_cardEmpty = !eq && !g_cardTwo;
        if (eq) ItemName(eq, g_cardVs, sizeof(g_cardVs)); else strcpy_s(g_cardVs, "nothing");
        if (orig) {
            ItemName(orig, g_cardVs2, sizeof(g_cardVs2));
            if (!g_cardVs2[0]) strcpy_s(g_cardVs2, slot == namedSlot && origName[0] ? origName : "your equipped item");
        } else g_cardVs2[0] = 0;
        ItemStat hs[16], s1[16], s2[16];
        int nh = ItemStats(item, hs, 16), n1 = eq ? ItemStats(eq, s1, 16) : 0, n2 = orig ? ItemStats(orig, s2, 16) : 0;
        auto find = [](ItemStat* a, int n, const char* k) -> ItemStat* { for (int i = 0; i < n; i++) if (!strcmp(a[i].key, k)) return &a[i]; return nullptr; };
        // every stat that any of the three items has, hovered item's stats first
        const char* keys[48]; int nk = 0;
        auto add = [&](const char* k) { for (int i = 0; i < nk; i++) if (!strcmp(keys[i], k)) return; if (nk < 48) keys[nk++] = k; };
        for (int i = 0; i < nh; i++) add(hs[i].key);
        for (int i = 0; i < n1; i++) add(s1[i].key);
        for (int i = 0; i < n2; i++) add(s2[i].key);
        g_cardN = 0;
        for (int i = 0; i < nk && g_cardN < 16; i++) {
            ItemStat* h = find(hs, nh, keys[i]); ItemStat* a = find(s1, n1, keys[i]); ItemStat* b = find(s2, n2, keys[i]);
            CardRow& r = g_cardRows[g_cardN++];
            strcpy_s(r.key, keys[i]); strcpy_s(r.disp, h ? h->disp : "-"); r.lost = !h;
            float hv = h ? h->v : 0.0f;
            r.d1 = hv - (a ? a->v : 0.0f);
            r.d2 = hv - (b ? b->v : 0.0f);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_cardVisible = false; }
}

static void DrawEquipCard(int W, int H) {
    int bodyPx = EnsureMenuFonts(H);
    const float LBL[3] = { 0.42f, 0.72f, 0.98f }, HOT[3] = { 0.98f, 0.92f, 0.36f };
    int rowH = g_fBody.h + bodyPx / 4, pad = bodyPx;
    int w = bodyPx * (g_cardTwo ? 23 : 19);
    int h = pad + g_fBody.h + g_fSmall.h * (g_cardTwo ? 3 : 1) + bodyPx / 2 + bodyPx / 2 + (g_cardN ? g_cardN : 1) * rowH + pad;
    int x = g_cardList.right + bodyPx / 2;
    if (x + w > W - 8) x = g_cardList.left - w - bodyPx / 2;
    int y = g_cardRowY - bodyPx / 2;
    if (y + h > H - 8) y = H - 8 - h;
    if (y < 8) y = 8;
    Frame(x, y, w, h, bodyPx / 2, 0.96f);
    int tx = x + pad, ty = y + pad;
    char up[96]; Upper(up, sizeof(up), g_cardTitle);
    TextAA(g_fBody, tx, ty, up, HOT[0], HOT[1], HOT[2]);
    ty += g_fBody.h;
    char sub[96], vs[64];
    const float DIM[3] = { 0.55f, 0.62f, 0.72f }, C1[3] = { 0.62f, 0.80f, 1.0f }, C2[3] = { 0.95f, 0.80f, 0.45f };
    if (g_cardSame) TextAA(g_fSmall, tx, ty, "CURRENTLY EQUIPPED", DIM[0], DIM[1], DIM[2]);
    else if (g_cardEmpty) TextAA(g_fSmall, tx, ty, "SLOT IS EMPTY - ALL GAINS", DIM[0], DIM[1], DIM[2]);
    else if (!g_cardTwo) { Upper(vs, sizeof(vs), g_cardVs); sprintf_s(sub, "VS EQUIPPED: %s", vs); TextAA(g_fSmall, tx, ty, sub, DIM[0], DIM[1], DIM[2]); }
    else {
        // column 1 = your pick, column 2 = what is really equipped (colour-coded headers)
        Upper(vs, sizeof(vs), g_cardVs);  sprintf_s(sub, "A  VS PICKED: %s", vs);   TextAA(g_fSmall, tx, ty, sub, C1[0], C1[1], C1[2]);
        ty += g_fSmall.h;
        Upper(vs, sizeof(vs), g_cardVs2); sprintf_s(sub, "B  VS EQUIPPED: %s", vs); TextAA(g_fSmall, tx, ty, sub, C2[0], C2[1], C2[2]);
        ty += g_fSmall.h;
    }
    ty += g_fSmall.h + bodyPx / 3;
    if (g_cardTwo) {                                              // column headers above the deltas
        int c2 = x + w - pad, c1 = c2 - bodyPx * 4;
        TextAA(g_fSmall, c1 - TextW(g_fSmall, "A"), ty - g_fSmall.h - bodyPx / 4, "A", C1[0], C1[1], C1[2]);
        TextAA(g_fSmall, c2 - TextW(g_fSmall, "B"), ty - g_fSmall.h - bodyPx / 4, "B", C2[0], C2[1], C2[2]);
    }
    RectC(tx, ty, w - pad * 2, 1, 0.30f, 0.55f, 0.85f, 0.6f);
    ty += bodyPx / 2;
    if (!g_cardN) TextAA(g_fBody, tx, ty, "No stats", 0.55f, 0.6f, 0.68f);
    int valX = x + w * (g_cardTwo ? 42 : 52) / 100, dX2 = x + w - pad, dX1 = g_cardTwo ? dX2 - bodyPx * 4 : dX2;
    auto delta = [&](float dv, int rx) {
        char d[24];
        if (fabsf(dv) < 0.05f) strcpy_s(d, "=");
        else if (fabsf(dv - (int)dv) < 0.05f) sprintf_s(d, "%+d", (int)(dv + (dv > 0 ? 0.01f : -0.01f)));
        else sprintf_s(d, "%+.1f", dv);
        float cr = dv > 0.05f ? 0.40f : dv < -0.05f ? 1.0f : 0.6f;
        float cg = dv > 0.05f ? 0.95f : dv < -0.05f ? 0.42f : 0.65f;
        float cb = dv > 0.05f ? 0.45f : dv < -0.05f ? 0.38f : 0.7f;
        TextAA(g_fBody, rx - TextW(g_fBody, d), ty, d, cr, cg, cb);
    };
    for (int i = 0; i < g_cardN; i++) {
        const CardRow& r = g_cardRows[i];
        bool lost = r.lost;
        TextAA(g_fBody, tx, ty, r.key, lost ? 0.5f : LBL[0], lost ? 0.58f : LBL[1], lost ? 0.68f : LBL[2]);
        TextAA(g_fBody, valX, ty, r.disp, lost ? 0.5f : 0.92f, lost ? 0.55f : 0.95f, lost ? 0.62f : 1.0f);
        if (!g_cardSame && !g_cardEmpty) {
            delta(r.d1, dX1);
            if (g_cardTwo) delta(r.d2, dX2);
        }
        ty += rowH;
    }
}

// Button drawn into the game's Options screen, in the game's own button style (colours sampled
// from the screen: border #0091D4, fill #000016, text #00A8FA, highlight olive-yellow).
static void DrawOptionsButton(int H) {
    if (!g_fBody.tex) return;
    const RECT& r = g_optRect;
    int w = r.right - r.left, h = r.bottom - r.top;
    RectC(r.left, r.top, w, h, 0.0f, 0.0f, 0.086f, 1.0f);
    float br = g_optHot ? 0.62f : 0.0f, bg = g_optHot ? 0.64f : 0.57f, bb = g_optHot ? 0.05f : 0.83f;
    glLineWidth(H >= 1000 ? 2.0f : 1.0f);
    glColor4f(br, bg, bb, 1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(r.left + 0.5f, r.top + 0.5f); glVertex2f(r.right - 0.5f, r.top + 0.5f);
    glVertex2f(r.right - 0.5f, r.bottom - 0.5f); glVertex2f(r.left + 0.5f, r.bottom - 0.5f);
    glEnd();
    glLineWidth(1);
    const char* t = "KOTORPLUS";
    float tr = g_optHot ? 0.78f : 0.0f, tg = g_optHot ? 0.80f : 0.66f, tb = g_optHot ? 0.05f : 0.98f;
    TextAA(g_fSmall, r.left + (w - TextW(g_fSmall, t)) / 2, r.top + (h - g_fSmall.h) / 2, t, tr, tg, tb);
}

static void DrawOverlay(HDC dc) {
    DWORD now = GetTickCount();
    bool showToast = g_toastTick && now - g_toastTick < 3000;
    int count = 0;
    if (g_readout) for (auto& L : g_lines) if (L.tick && now - L.tick < (DWORD)g_readoutSecs * 1000) count++;
    if (!count && !showToast && !g_menuOpen && !g_shotRequested && !g_optVisible && !g_cardVisible) return;
    static bool firstDraw;
    // Draw with our own GL context on the window being presented. The engine's current context at
    // SwapBuffers time is not always the window's (main menu: another surface), so drawing with it
    // can land somewhere invisible.
    // The engine presents through more than one HDC of the same window, so key the context on the
    // window: a context works with any DC of that window.
    static HGLRC myRC; static HWND myWnd;
    HGLRC prevRC = wglGetCurrentContext(); HDC prevDC = wglGetCurrentDC();
    HWND wnd = WindowFromDC(dc);
    if (!myRC || myWnd != wnd) {
        if (myRC) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(myRC); }
        myRC = wglCreateContext(dc); myWnd = wnd; g_fontBase = 0; g_fontCtx = nullptr;
        if (!myRC) { Log("overlay: wglCreateContext failed (%lu)", GetLastError()); wglMakeCurrent(prevDC, prevRC); return; }
    }
    if (!wglMakeCurrent(dc, myRC)) { wglMakeCurrent(prevDC, prevRC); return; }
    RECT cr; GetClientRect(wnd, &cr);
    int W = cr.right - cr.left, H = cr.bottom - cr.top;
    if (W <= 0 || H <= 0) { wglMakeCurrent(prevDC, prevRC); return; }
    glViewport(0, 0, W, H);
    if (!firstDraw) { firstDraw = true; Log("overlay: first draw (%d readout line(s), toast=%d) %dx%d", count, showToast, W, H); }
    EnsureFont(dc, H);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE); glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST);
    glDisable(0x8804 /*GL_FRAGMENT_PROGRAM_ARB*/); glDisable(0x8620 /*GL_VERTEX_PROGRAM_ARB*/);
    // The engine leaves extra multitexture units enabled on some screens (main menu); textured
    // units would tint our quads and bitmap text to nothing, so switch them all off.
    typedef void (APIENTRY* ActiveTex_t)(GLenum);
    static ActiveTex_t activeTex = (ActiveTex_t)wglGetProcAddress("glActiveTextureARB");
    GLint prevUnit = 0x84C0;
    if (activeTex) glGetIntegerv(0x84E0 /*GL_ACTIVE_TEXTURE*/, &prevUnit);
    for (int u = activeTex ? 3 : 0; u >= 0; u--) {
        if (activeTex) activeTex(0x84C0 + u);
        glDisable(GL_TEXTURE_2D); glDisable(GL_TEXTURE_1D);
        glDisable(0x8513 /*CUBE_MAP*/); glDisable(0x84F5 /*RECTANGLE*/);
        glDisable(GL_TEXTURE_GEN_S); glDisable(GL_TEXTURE_GEN_T);
    }
    glColorMask(1, 1, 1, 1);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawBuffer(GL_BACK);

    int lineH = g_fontH + 4, pad = 8;
    if (count) {
        int n = count < g_readoutLines ? count : g_readoutLines;
        int boxW = g_fontH * 34, boxH = n * lineH + pad * 2 + lineH;
        int x0 = W - boxW - 16, y0 = (int)(H * 0.18f);
        Rect(x0, y0, boxW, boxH, 0.62f);
        Text(x0 + pad, y0 + pad + g_fontH, "COMBAT  (roll + bonus vs defense)", 0.55f, 0.8f, 1.0f);
        int shown = 0;
        for (int k = 1; k <= 16 && shown < n; k++) {
            AttackLine& L = g_lines[(g_lineHead - k + 32) % 16];
            if (!L.tick || now - L.tick >= (DWORD)g_readoutSecs * 1000) continue;
            int total = L.roll + L.mod;
            bool hit = L.roll == 20 || (L.roll != 1 && total >= L.ac);
            bool crit = L.result == 3 || L.result == 10;
            char line[200];
            const char* res = crit ? "CRIT" : hit ? "HIT " : "MISS";
            if (hit && L.final && L.damage > 0)
                sprintf_s(line, "%.12s > %.12s  %2d%+d=%2d vs %2d  %s %3d  %d%%",
                          L.attacker, L.target, L.roll, L.mod, total, L.ac, res, L.damage, HitChance(L.mod, L.ac));
            else
                sprintf_s(line, "%.12s > %.12s  %2d%+d=%2d vs %2d  %s      %d%%",
                          L.attacker, L.target, L.roll, L.mod, total, L.ac, res, HitChance(L.mod, L.ac));
            float a = 1.0f;
            int y = y0 + pad + g_fontH + lineH * (shown + 1);
            if (crit) Text(x0 + pad, y, line, 1.0f, 0.82f * a, 0.25f);
            else if (hit) Text(x0 + pad, y, line, 0.55f, 1.0f, 0.55f);
            else Text(x0 + pad, y, line, 1.0f, 0.5f, 0.5f);
            shown++;
        }
    }
    if (showToast) {
        int w = (int)strlen(g_toast) * (g_fontH * 55 / 100) + 24;
        Rect(W / 2 - w / 2, 60, w, lineH + 12, 0.75f);
        Text(W / 2 - w / 2 + 12, 60 + 6 + g_fontH, g_toast, 1.0f, 0.9f, 0.4f);
    }
    if (g_menuOpen) DrawMenu(W, H);
    else if (g_optVisible) { EnsureMenuFonts(H); DrawOptionsButton(H); }
    if (!g_menuOpen && g_cardVisible) DrawEquipCard(W, H);
    if (g_shotRequested) { g_shotRequested = false; SaveScreenshot(W, H); }

    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glPopAttrib();
    if (activeTex) activeTex(prevUnit);
    wglMakeCurrent(prevDC, prevRC);
}

// ---------------------------------------------------------------- SwapBuffers hook + frame pacing
typedef BOOL (WINAPI* SwapBuffers_t)(HDC);
static SwapBuffers_t oSwapBuffers;
static LARGE_INTEGER g_next;
static HANDLE g_timer;

// Adaptive cap: combat (and a few seconds after it) runs at CombatFpsCap, everything else at FpsCap.
static DWORD g_lastCombatTick;
static bool g_wasCombat;

static int CurrentCap() {
    if (g_combatFpsCap <= 0) return g_fpsCap;
    bool combat = g_lastCombatTick && GetTickCount() - g_lastCombatTick < (DWORD)g_combatHoldMs;
    if (combat != g_wasCombat) {
        g_wasCombat = combat;
        Log("frame cap -> %d (%s)", combat ? g_combatFpsCap : g_fpsCap, combat ? "combat" : "exploring");
    }
    return combat ? g_combatFpsCap : g_fpsCap;
}

static void PaceFrame() {
    int cap = CurrentCap();
    if (cap <= 0) return;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    LONGLONG period = g_qpf.QuadPart / cap;
    if (g_next.QuadPart == 0 || now.QuadPart - g_next.QuadPart > period * 4) g_next.QuadPart = now.QuadPart;
    g_next.QuadPart += period;
    for (;;) {
        QueryPerformanceCounter(&now);
        LONGLONG left = g_next.QuadPart - now.QuadPart;
        if (left <= 0) break;
        LONGLONG us = left * 1000000 / g_qpf.QuadPart;
        if (us > 1500 && g_timer) {
            LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(us - 1000) * 10;  // 100ns units, relative
            SetWaitableTimer(g_timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(g_timer, 20);
        } else if (us > 200) {
            Sleep(0);
        }
    }
}

static bool g_kDump, g_kUnstick, g_kReadout, g_kWalk;

// ---------------------------------------------------------------- walk toggle
// CSWPlayerControlCamRelative::GetMaxSpeed uses the walk rate when bit 0 of the client
// creature's byte at +0x2D4 is set (the same flag scripts use to force walking).
#define CC_WALK_FLAGS 0x2D4
static bool g_walkSetByUs;
static void UpdateWalk() {
    __try {
        uint8_t* f = (uint8_t*)ClientPlayerCreature(CLIENT_APP) + CC_WALK_FLAGS;
        if ((uintptr_t)f == CC_WALK_FLAGS) return;
        if (g_walk) { if (!(*f & 1)) { *f |= 1; g_walkSetByUs = true; } }
        else if (g_walkSetByUs) { *f &= ~1; g_walkSetByUs = false; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---------------------------------------------------------------- middle-mouse free look
// Holding the right mouse button is the engine's mouse-look (global flag 0x8338F0), but movement
// is camera-relative so turning the camera also turns where you walk. While the middle button is
// held we turn mouse-look on and feed CSWPlayerControlCamRelative::Control the camera orientation
// from the moment the button went down, so the camera turns freely and the walk direction stays.
#define G_MOUSE_LOOK (*(int*)0x8338F0)
// The keyboard still steers: ProcessInput reads the mouse-look flag through g_keysMouseLook (patched
// at 0x623A37), which stays 0 during free look so A/D keep turning instead of strafing, and every
// turn the camera makes while A or D is down is applied to the locked walk direction too.
static volatile bool g_lookActive;
static bool g_lockValid;
static float g_lockQuat[4], g_prevQuat[4];   // engine Quaternion layout: w, x, y, z
static int g_keysMouseLook;
static void QuatMul(const float* a, const float* b, float* o) {
    float w = a[0]*b[0] - a[1]*b[1] - a[2]*b[2] - a[3]*b[3];
    float x = a[0]*b[1] + a[1]*b[0] + a[2]*b[3] - a[3]*b[2];
    float y = a[0]*b[2] - a[1]*b[3] + a[2]*b[0] + a[3]*b[1];
    float z = a[0]*b[3] + a[1]*b[2] - a[2]*b[1] + a[3]*b[0];
    float n = sqrtf(w*w + x*x + y*y + z*z); if (n < 1e-6f) n = 1;
    o[0] = w / n; o[1] = x / n; o[2] = y / n; o[3] = z / n;
}
// A / D turn the locked walk direction ourselves (the game's keyboard camera turn does not run
// while mouse-look is on), at FreeLookTurnRate degrees per second around the world up axis.
static float g_turnRate = 150.0f;
static void __stdcall FreeLookFix(float* q) {
    static LONGLONG last;
    LONGLONG now = QpcNow();
    float dt = last ? (float)((now - last) / (double)g_qpf.QuadPart) : 0.0f;
    last = now;
    if (dt > 0.1f) dt = 0.1f;
    if (!g_lookActive || !q) { g_lockValid = false; return; }
    if (!g_lockValid) { memcpy(g_lockQuat, q, 16); g_lockValid = true; return; }
    int dir = ((GetAsyncKeyState('A') & 0x8000) ? 1 : 0) - ((GetAsyncKeyState('D') & 0x8000) ? 1 : 0);
    if (dir) {
        float half = dir * g_turnRate * 3.14159265f / 180.0f * dt * 0.5f;
        float yaw[4] = { cosf(half), 0.0f, 0.0f, sinf(half) }, out[4];
        QuatMul(yaw, g_lockQuat, out);
        memcpy(g_lockQuat, out, 16);
    }
    memcpy(q, g_lockQuat, 16);
}
static uintptr_t g_freeLookRet = 0x679BA2;
// replaces "call [eax+24h] (Camera::GetOrientation); mov ecx, eax" at 0x679B9D
__declspec(naked) static void Stub_FreeLook() {
    __asm {
        call dword ptr [eax + 0x24]
        push eax
        push eax
        call FreeLookFix
        pop eax
        mov ecx, eax
        jmp dword ptr [g_freeLookRet]
    }
}
// CClientExoAppInternal::UpdateCamera: a keyboard camera turn (A/D) takes a branch that skips the
// mouse turn. During free look A/D steer the walk direction instead, so always take the mouse path.
// Replaces "fld [esp+14h]; fcomp [0]; fnstsw ax; test ah,44h; jnp 5F6029h" at 0x5F5FE1.
static uintptr_t g_camKeyPath = 0x5F5FF2, g_camMousePath = 0x5F6029;
__declspec(naked) static void Stub_CamKeyTurn() {
    __asm {
        cmp byte ptr [g_lookActive], 0
        jne mouse
        fld dword ptr [esp + 0x14]
        fcomp dword ptr ds:[0x73D700]
        fnstsw ax
        test ah, 0x44
        jnp mouse
        jmp dword ptr [g_camKeyPath]
    mouse:
        jmp dword ptr [g_camMousePath]
    }
}

static void UpdateFreeLook(bool focused) {
    static bool was;
    bool mmb = focused && (GetAsyncKeyState(VK_MBUTTON) & 0x8000);
    if (mmb) { g_lookActive = true; G_MOUSE_LOOK = 1; }
    else if (was) { g_lookActive = false; G_MOUSE_LOOK = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 1 : 0; }
    was = mmb;
}

// ---------------------------------------------------------------- hold Space: fast-forward dialogue
// While SkipKey is held in a conversation, the game clock runs DialogSpeed x faster (the main loop
// calls SetGameSpeed every frame; we scale its argument) and every open Miles voice stream plays
// DialogSpeed x faster so the lines stay in sync. Faster playback also raises the pitch.
static float g_speedMul = 1.0f;
typedef void (__thiscall* SetGameSpeed_t)(void* client, float s);
static SetGameSpeed_t oSetGameSpeed = (SetGameSpeed_t)0x5F2F60;
static void __fastcall Hook_SetGameSpeed(void* self, void*, float s) { oSetGameSpeed(self, s * g_speedMul); }

typedef void* HSTREAM;
typedef HSTREAM (WINAPI* AilOpenStream_t)(void* dig, const char* name, int mem);
typedef void (WINAPI* AilCloseStream_t)(HSTREAM s);
typedef int (WINAPI* AilStreamRate_t)(HSTREAM s);
typedef void (WINAPI* AilSetStreamRate_t)(HSTREAM s, int rate);
static AilOpenStream_t oAilOpenStream; static AilCloseStream_t oAilCloseStream;
static AilStreamRate_t AilStreamRate; static AilSetStreamRate_t AilSetStreamRate;
struct VoiceStream { HSTREAM h; int base; bool fast; };
static volatile int g_inConversation;
static VoiceStream g_streams[32];
static CRITICAL_SECTION g_streamLock;

static HSTREAM WINAPI Hook_AilOpenStream(void* dig, const char* name, int mem) {
    HSTREAM h = oAilOpenStream(dig, name, mem);
    static int logged;
    if (name && logged < 20) { logged++; Log("audio stream: %s (in conversation: %d)", name, g_inConversation); }
    // The game opens streams by memory reference ("\\\\file,offset"), so voice lines can't be told
    // apart by name. Only streams opened while a conversation is running are sped up; music that
    // was already playing is left alone.
    if (h && g_inConversation) {
        EnterCriticalSection(&g_streamLock);
        for (auto& s : g_streams) if (!s.h) { s.h = h; s.base = 0; s.fast = false; break; }
        LeaveCriticalSection(&g_streamLock);
    }
    return h;
}
static void WINAPI Hook_AilCloseStream(HSTREAM h) {
    EnterCriticalSection(&g_streamLock);
    for (auto& s : g_streams) if (s.h == h) s.h = nullptr;
    LeaveCriticalSection(&g_streamLock);
    oAilCloseStream(h);
}

static void UpdateDialogSpeed(bool focused) {
    bool conv = false;
    __try {
        // In a conversation the leader's +0x54 holds the conversation object (0x7F000000 otherwise).
        char* leader = (char*)LeaderCreature();
        conv = leader && *(uint32_t*)(leader + 0x54) != INVALID_OBJ;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (conv != (g_inConversation != 0)) {
        g_inConversation = conv;
        if (!conv) {   // conversation over: forget its streams
            EnterCriticalSection(&g_streamLock);
            for (auto& s : g_streams) { if (s.h && s.fast && AilSetStreamRate) AilSetStreamRate(s.h, s.base); s.h = nullptr; }
            LeaveCriticalSection(&g_streamLock);
        }
    }
    bool fast = conv && g_holdSkip > 1 && focused && (GetAsyncKeyState(g_keySkip) & 0x8000);
    float mul = fast ? (float)g_holdSkip : 1.0f;
    if (mul != g_speedMul) { g_speedMul = mul; Log("dialogue speed x%.0f", mul); }
    if (!AilStreamRate || !AilSetStreamRate) return;
    EnterCriticalSection(&g_streamLock);
    __try {
        for (auto& s : g_streams) {
            if (!s.h) continue;
            if (fast && !s.fast) { s.base = AilStreamRate(s.h); if (s.base > 0) { AilSetStreamRate(s.h, (int)(s.base * mul)); s.fast = true; } }
            else if (!fast && s.fast) { AilSetStreamRate(s.h, s.base); s.fast = false; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    LeaveCriticalSection(&g_streamLock);
}

// ---------------------------------------------------------------- companion driver
// Runs kp_comp.ncs with the player as OBJECT_SELF; the script spawns the companion if she is not
// in the area and keeps her close. Cheap: one tiny script every 1.5 s.
typedef int (__thiscall* RunScript_t)(void* vm, CExoString* name, uint32_t self, int one);
static RunScript_t RunScript = (RunScript_t)0x5D0FC0;
#define VIRTUAL_MACHINE (*(void**)0x7A3A00)
static void UpdateCompanion() {
#ifdef KP_PUBLIC
    return;
#endif
    static DWORD last; static bool logged;
    // Companion off: every 5 s run kp_comp_off, which removes her if she is still in this area
    // (e.g. left behind in a save).
    if (GetTickCount() - last < (DWORD)(g_companion ? 1500 : 5000)) return;
    last = GetTickCount();
    __try {
        void* srv = SERVER_APP;
        char* leader = (char*)LeaderCreature();
        if (!srv || !leader || !VIRTUAL_MACHINE || GetPauseState(srv, 2)) return;
        CExoString s = { 0, 0 };
        ExoStrCtor(&s, g_companion ? "kp_comp" : "kp_comp_off");
        RunScript(VIRTUAL_MACHINE, &s, *(uint32_t*)(leader + 4), 1);
        ExoStrDtor(&s);
        if (!logged) { logged = true; Log("companion: kp_comp running"); }
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("companion: script call failed"); g_companion = 0; }
}

// Borderless window: run the game windowed (FullScreen=0) and make the window a
// border-less popup that covers the monitor it is on.
static void KeepBorderless(HDC dc) {
    static DWORD last;
    if (!g_borderless || GetTickCount() - last < 500) return;
    last = GetTickCount();
    HWND w = WindowFromDC(dc);
    if (!w || IsIconic(w) || GetForegroundWindow() != w) return;  // never fight the user when alt-tabbed
    LONG style = GetWindowLongA(w, GWL_STYLE);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoA(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi);
    RECT m = mi.rcMonitor, cur; GetWindowRect(w, &cur);
    bool hasFrame = (style & (WS_CAPTION | WS_THICKFRAME)) != 0;
    if (!hasFrame && EqualRect(&cur, &m)) return;
    SetWindowLongA(w, GWL_STYLE, (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP);
    LONG ex = GetWindowLongA(w, GWL_EXSTYLE);
    SetWindowLongA(w, GWL_EXSTYLE, ex & ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
    SetWindowPos(w, nullptr, m.left, m.top, m.right - m.left, m.bottom - m.top,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    Log("borderless: window set to %dx%d at %d,%d", m.right - m.left, m.bottom - m.top, m.left, m.top);
}

// Auto-unstick watchdog: the leader's combat round is open, the game is not paused, but the
// round timer has not moved for a few seconds -> the round state machine is wedged. End it.
static void Watchdog(char* leader) {
    static char* lastRound; static int lastTimer, lastPaused; static LONGLONG since;
    static DWORD cooldown;
    if (!g_autoUnstick || !leader) return;
    char* r = *(char**)(leader + CRE_COMBAT_ROUND);
    void* srv = SERVER_APP;
    if (!r || !srv || GetPauseState(srv, 2)) { lastRound = nullptr; return; }

    // (An old "stale engagement" rule lived here. engaged=1 + master=1 with no round is simply how
    // the engine leaves a combat master after a fight, so it fired mid-combat; the real cause of
    // the can't-move bug is fixed by the StuckFix patch.)
    if (!*(int*)(r + CR_ROUND_STARTED)) { lastRound = nullptr; return; }
    int timer = *(int*)(r + CR_TIMER), paused = *(int*)(r + CR_PAUSED);
    LONGLONG now = QpcNow();
    if (r != lastRound || timer != lastTimer || paused != lastPaused) {
        lastRound = r; lastTimer = timer; lastPaused = paused; since = now; return;
    }
    double frozen = (now - since) / (double)g_qpf.QuadPart;
    if (frozen > 4.0 && GetTickCount() - cooldown > 5000) {
        cooldown = GetTickCount();
        Log("AUTO-UNSTICK: leader round frozen %.1fs (timer=%d len=%d paused=%d pauseTimer=%d infinite=%d engaged=%d master=%d)",
            frozen, timer, *(int*)(r + CR_ROUND_LENGTH), paused, *(int*)(r + CR_PAUSE_TIMER),
            *(int*)(r + CR_INFINITE_PAUSE), *(int*)(r + CR_ENGAGED), *(int*)(r + CR_MASTER));
        DumpRecorder(leader);
        EndCombatRound(r, 1);
        Toast("KotorPlus: combat freeze detected and fixed");
        since = now;
    }
}

static void FrameStats() {
    LONGLONG now = QpcNow();
    if (g_lastFrameQpc) g_frameMs[g_frame & 255] = (float)((now - g_lastFrameQpc) * 1000.0 / g_qpf.QuadPart);
    g_lastFrameQpc = now;
    __try {
        char* leader = (char*)LeaderCreature();
        g_creDelta[g_frame & 255] = leader ? *(int*)(leader + 0xCC) : -1;
        // combat detection for the adaptive frame cap: any party member with an open round or engagement
        bool fighting = false;
        if (leader) {
            char* r = *(char**)(leader + CRE_COMBAT_ROUND);
            if (r && (*(int*)(r + CR_ROUND_STARTED) || *(int*)(r + CR_ENGAGED) || *(int*)(r + CR_MASTER))) fighting = true;
        }
        if (fighting) g_lastCombatTick = GetTickCount();
        g_cachedLeader = leader;
        Watchdog(leader);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_frame++;
}

static void SetStuckFix(bool on) {
    uint8_t* p = (uint8_t*)0x623BB2;
    const uint8_t ctx[] = { 0xDF, 0xE0, 0xF6, 0xC4, 0x41 };
    bool orig = p[0] == 0x75 && p[1] == 0x5C, done = p[0] == 0x90 && p[1] == 0x90;
    if (memcmp(p - 5, ctx, sizeof(ctx)) || p[2] != 0x8B || (!orig && !done)) { Log("SKIP stuck-after-combat patch: unexpected bytes"); return; }
    DWORD old; VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &old);
    p[0] = on ? 0x90 : 0x75; p[1] = on ? 0x90 : 0x5C;
    VirtualProtect(p, 2, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 2);
    Log("stuck-after-combat fix %s", on ? "ON (break-out no longer needs a drive speed)" : "OFF");
}

typedef void (__thiscall* SetPauseState_t)(void* server, int type, int on);
static SetPauseState_t SetPauseState = (SetPauseState_t)0x4AE9A0;
static bool g_menuPausedGame, g_kMenu, g_menuL, g_menuR;

static void RunLeaderScript(const char* name) {
    __try {
        char* leader = (char*)LeaderCreature();
        if (!leader || !VIRTUAL_MACHINE) return;
        CExoString s = { 0, 0 };
        ExoStrCtor(&s, name);
        RunScript(VIRTUAL_MACHINE, &s, *(uint32_t*)(leader + 4), 1);
        ExoStrDtor(&s);
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("script %s failed", name); }
}

static void MenuChange(int i, int dir) {
    MenuItem& m = g_menu[i];
    int k = 0;
    while (k < m.n && m.opts[k] != *m.val) k++;
    k = (k >= m.n) ? 0 : (k + dir + m.n) % m.n;
    *m.val = m.opts[k];
    if (m.key) { char v[16]; sprintf_s(v, "%d", *m.val); WritePrivateProfileStringA("KotorPlus", m.key, v, g_ini); }
    if (m.restart) g_restartPending = true;
    if (m.val == &g_stuckFix) SetStuckFix(g_stuckFix != 0);
    if (m.val == &g_companion && !g_companion) RunLeaderScript("kp_comp_off");
    if (m.val == &g_fpsCap || m.val == &g_combatFpsCap) g_wasCombat = !g_wasCombat;   // re-log the cap
    Log("menu: %s -> %s", m.label, MenuValue(m));
}

static void SetMenuOpen(bool open) {
    if (open == g_menuOpen) return;
    g_menuOpen = open;
    void* srv = SERVER_APP;
    __try {
        if (open) {
            g_menuPausedGame = srv && LeaderCreature() && !GetPauseState(srv, 2);
            if (g_menuPausedGame) SetPauseState(srv, 2, 1);
        } else if (g_menuPausedGame) { SetPauseState(srv, 2, 0); g_menuPausedGame = false; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static inline bool InRect(POINT p, const RECT& r) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

// "KotorPlus" button on the game's own Options screen (main menu and in-game). The button goes in
// the next free slot under the game's option buttons, read from the live GUI objects.
static void UpdateOptionsButton(POINT pt, bool click) {
    g_optVisible = false;
    __try {
        char* top = FindScreen(0x758838);                     // main-menu CSWGuiOptionsMain
        if (!top) top = FindScreen(0x755DE0);                 // in-game CSWGuiInGameOptions
        if (!top) return;
        uint32_t vt = *(uint32_t*)top;
        static const int mainBtns[] = { 0x64, 0x228, 0x3EC, 0x5B0, 0x774 };
        static const int gameBtns[] = { 0x64, 0x228, 0x3EC, 0x5B0, 0x774, 0x938, 0xAFC, 0xCC0 };
        const int* btn; int nb, backOff;
        if (vt == 0x758838) { btn = mainBtns; nb = 5; backOff = 0xD7C; }        // CSWGuiOptionsMain
        else if (vt == 0x755DE0) { btn = gameBtns; nb = 8; backOff = 0x1BE8; } // CSWGuiInGameOptions
        else return;
        int px = *(int*)(top + 4), py = *(int*)(top + 8);  // panel extent origin
        int lowY = -1, prevY = -1, bx = 0, bw = 0, bh = 0;
        for (int i = 0; i < nb; i++) {
            int* e = (int*)(top + btn[i] + 4);
            if (e[2] <= 0 || e[3] <= 0) continue;
            if (e[1] > lowY) { prevY = lowY; lowY = e[1]; bx = e[0]; bw = e[2]; bh = e[3]; }
            else if (e[1] > prevY) prevY = e[1];
        }
        if (lowY < 0) return;
        int step = prevY >= 0 ? lowY - prevY : bh + bh / 8;
        int* back = (int*)(top + backOff + 4);
        RECT r = { px + bx, py + lowY + step, px + bx + bw, py + lowY + step + bh };
        if (back[3] > 0 && r.bottom > py + back[1] - 4) {
            // Column is full (in-game Options): put the button in the bottom bar, mirroring Close
            // on the other side of the bar.
            int bxl = px + back[0], bw2 = back[2];
            int left = bxl - bw2 - bw2 / 2;
            if (left < px + bx) left = px + bx;
            r = { left, py + back[1], left + bw2, py + back[1] + back[3] };
        }
        g_optRect = r;
        g_optVisible = true;
        g_optHot = InRect(pt, r);
        if (g_optHot && click) SetMenuOpen(true);
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_optVisible = false; }
}

static void UpdateMenu(HDC dc, bool focused) {
    if (focused && KeyPressed(g_keyMenu, g_kMenu)) SetMenuOpen(!g_menuOpen);
    POINT pt; GetCursorPos(&pt); ScreenToClient(WindowFromDC(dc), &pt);
    g_mouse = pt;
    bool l = focused && KeyPressed(VK_LBUTTON, g_menuL), r = focused && KeyPressed(VK_RBUTTON, g_menuR);
    if (!g_menuOpen) { g_menuHover = -1; UpdateOptionsButton(pt, l); return; }
    g_optVisible = false;
    g_menuHover = -1;
    if (g_menuRowH && pt.x >= g_menuX && pt.x < g_menuX + g_menuW)
        for (int i = 0; i < g_menuN; i++)
            if (g_menu[i].val && g_menuRowY[i] >= 0 && pt.y >= g_menuRowY[i] && pt.y < g_menuRowY[i] + g_menuRowH) g_menuHover = i;
    g_closeHot = InRect(pt, g_closeRect);
    if (l && g_closeHot) { SetMenuOpen(false); return; }
    if (g_menuHover >= 0 && (l || r)) {
        bool back = r || (pt.x >= g_arrowL0 && pt.x < g_arrowL1);   // "<" or right-click steps back
        MenuChange(g_menuHover, back ? -1 : 1);
    }
}

// While the panel is open the game must not see mouse clicks (its own buttons sit behind the
// panel). CClientExoAppInternal::HandleInputEvent calls one Perform*Action per button event.
// A release is only swallowed if its press was, so the game never thinks a button is stuck down.
static volatile int g_swallowMouse, g_ateL, g_ateR;
static uintptr_t oLDown = 0x61F880, oLUp = 0x620530, oRDown = 0x61F940, oRUp = 0x61F960;
__declspec(naked) static void Thunk_LDown() { __asm { mov eax, g_swallowMouse
                                                      mov g_ateL, eax
                                                      test eax, eax
                                                      jne skip
                                                      jmp dword ptr [oLDown]
                                                      skip: ret 4 } }
__declspec(naked) static void Thunk_LUp()   { __asm { cmp g_ateL, 0
                                                      jne skip
                                                      jmp dword ptr [oLUp]
                                                      skip: mov g_ateL, 0
                                                      ret } }
__declspec(naked) static void Thunk_RDown() { __asm { mov eax, g_swallowMouse
                                                      mov g_ateR, eax
                                                      test eax, eax
                                                      jne skip
                                                      jmp dword ptr [oRDown]
                                                      skip: ret } }
__declspec(naked) static void Thunk_RUp()   { __asm { cmp g_ateR, 0
                                                      jne skip
                                                      jmp dword ptr [oRUp]
                                                      skip: mov g_ateR, 0
                                                      ret } }

static DWORD g_initTick;
static void ApplyCodePatches();
static volatile long g_patched;

static BOOL WINAPI Hook_SwapBuffers(HDC dc) {
    static bool first;
    if (!first) { first = true; Log("first frame %.1fs after KotorPlus loaded", (GetTickCount() - g_initTick) / 1000.0); }
    {   // diagnostics: which windows get presented
        static HDC seen[4]; static int nSeen;
        bool known = false;
        for (int i = 0; i < nSeen; i++) if (seen[i] == dc) known = true;
        if (!known && nSeen < 4) {
            seen[nSeen++] = dc;
            HWND w = WindowFromDC(dc); RECT r = {}; GetClientRect(w, &r); char cls[64] = ""; GetClassNameA(w, cls, 64);
            Log("present dc=%p hwnd=%p class=%s visible=%d client=%dx%d fg=%d", dc, w, cls, IsWindowVisible(w), (int)r.right, (int)r.bottom, GetForegroundWindow() == w);
        }
    }
    if (!g_patched) ApplyCodePatches();
    KeepBorderless(dc);
    FrameStats();
    bool focused = OurWindowFocused();
    if (focused) {
        if (KeyPressed(g_keyDump, g_kDump)) { DumpState("F9 dump"); __try { DumpRecorder(LeaderCreature()); } __except (EXCEPTION_EXECUTE_HANDLER) {} DumpClient("F9 dump"); Toast("KotorPlus: state written to KotorPlus.log"); }
        if (KeyPressed(g_keyUnstick, g_kUnstick)) Unstick();
        if (KeyPressed(g_keyReadout, g_kReadout)) { g_readout = !g_readout; Toast(g_readout ? "Combat readout ON" : "Combat readout OFF"); }
        if (g_keyWalk && KeyPressed(g_keyWalk, g_kWalk)) { g_walk = !g_walk; Toast(g_walk ? "Walk" : "Run"); }
        static bool kShot;
        if (KeyPressed(VK_F12, kShot) && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) g_shotRequested = true;
    }
    UpdateMenu(dc, focused);
    g_swallowMouse = g_menuOpen;
    UpdateEquipCard();
    UpdateWalk();
    if (g_freeLook || g_lookActive) UpdateFreeLook(focused && g_freeLook);
    g_keysMouseLook = g_lookActive ? 0 : G_MOUSE_LOOK;
    UpdateDialogSpeed(focused);
    UpdateCompanion();
    __try { FinalizeLines(); DrawOverlay(dc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    BOOL r = oSwapBuffers(dc);
    PaceFrame();
    return r;
}

// ---------------------------------------------------------------- dinput8 forwarding + init
typedef HRESULT (WINAPI* DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
static DI8Create_t oDI8Create;

#pragma comment(linker, "/EXPORT:DirectInput8Create=_DirectInput8Create@20")
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID* o, void* u) {
    if (!oDI8Create) return E_FAIL;
    return oDI8Create(h, v, r, o, u);
}

// The decrypted code starts IsValidResolution with these bytes; on the packed Steam exe they are noise.
static bool CodeUnpacked() {
    const uint8_t expect[] = { 0x8B, 0x44, 0x24, 0x04, 0x3D, 0x20, 0x03, 0x00, 0x00 };
    const uint8_t patched[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x08, 0x00 };
    __try {
        return !memcmp((void*)0x5F0C60, expect, sizeof(expect)) || !memcmp((void*)0x5F0C60, patched, sizeof(patched));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Windowed/borderless: the game still calls ChangeDisplaySettingsA at startup and shutdown,
// which blanks every monitor for a few seconds. The window already covers the monitor.
typedef LONG (WINAPI* ChangeDisplaySettingsA_t)(DEVMODEA*, DWORD);
static ChangeDisplaySettingsA_t oChangeDisplaySettingsA;
static LONG WINAPI Hook_ChangeDisplaySettingsA(DEVMODEA* dm, DWORD flags) {
    if (g_noModeSwitch && g_borderless) {
        if (dm) Log("display mode switch skipped (%ux%u @%uHz, flags %X)", (unsigned)dm->dmPelsWidth, (unsigned)dm->dmPelsHeight, (unsigned)dm->dmDisplayFrequency, (unsigned)flags);
        else Log("display mode restore skipped");
        return DISP_CHANGE_SUCCESSFUL;
    }
    return oChangeDisplaySettingsA(dm, flags);
}

// Import slot lookup. The Steam exe resolves its real imports itself after unpacking, so when the
// import directory has no entry we look for the resolved function pointer in the exe's data.
static void** FindImportSlot(const char* dll, const char* func) {
    HMODULE exe = GetModuleHandleA(nullptr);
    if (void** p = FindIAT(exe, dll, func)) return p;
    void* target = GetProcAddress(GetModuleHandleA(dll), func);
    if (!target) return nullptr;
    auto base = (uint8_t*)exe;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        void** p = (void**)(base + sec->VirtualAddress);
        size_t n = sec->Misc.VirtualSize / sizeof(void*);
        __try { for (size_t k = 0; k < n; k++) if (p[k] == target) return &p[k]; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return nullptr;
}
static bool HookImport(const char* dll, const char* func, void* hook, void** orig) {
    void** slot = FindImportSlot(dll, func);
    if (!slot) return false;
    if (*slot == hook) return true;
    DWORD old; VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
    *orig = *slot; *slot = hook;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}
// Safe to call repeatedly: re-installs a hook if the import slot was rewritten (Steam unpacker).
static void InstallImportHooks(bool logMissing) {
    static bool swapLogged, cdsLogged;
    if (HookImport("GDI32.dll", "SwapBuffers", (void*)Hook_SwapBuffers, (void**)&oSwapBuffers)) {
        if (!swapLogged) { swapLogged = true; Log("hooked SwapBuffers (frame cap, overlay, hotkeys)"); }
    } else if (logMissing) Log("SwapBuffers import not found yet");
    if (HookImport("USER32.dll", "ChangeDisplaySettingsA", (void*)Hook_ChangeDisplaySettingsA, (void**)&oChangeDisplaySettingsA)) {
        if (!cdsLogged) { cdsLogged = true; Log("hooked ChangeDisplaySettingsA (no black screens)"); }
    }
    static bool ailLogged;
    if (HMODULE mss = GetModuleHandleA("mss32.dll")) {
        if (!AilStreamRate) AilStreamRate = (AilStreamRate_t)GetProcAddress(mss, "_AIL_stream_playback_rate@4");
        if (!AilSetStreamRate) AilSetStreamRate = (AilSetStreamRate_t)GetProcAddress(mss, "_AIL_set_stream_playback_rate@8");
        bool a = HookImport("mss32.dll", "_AIL_open_stream@12", (void*)Hook_AilOpenStream, (void**)&oAilOpenStream);
        bool b = HookImport("mss32.dll", "_AIL_close_stream@4", (void*)Hook_AilCloseStream, (void**)&oAilCloseStream);
        if (a && b && !ailLogged) { ailLogged = true; Log("hooked Miles voice streams (fast dialogue audio)"); }
    }
}
static DWORD WINAPI PatchWaiter(void*) {
    int unpackedAt = -1;
    for (int i = 0; i < 1200; i++) {   // up to 60 s
        Sleep(50);
        if (!CodeUnpacked()) continue;
        if (unpackedAt < 0) {
            unpackedAt = i;
            ApplyCodePatches();
            Log("game code unpacked after %.2fs - patches applied", (i + 1) * 0.05);
        }
        InstallImportHooks(false);
        if (i - unpackedAt > 100) return 0;   // keep re-asserting import hooks for 5 s after unpacking
    }
    Log("ERROR: game code never looked unpacked - unsupported exe version?");
    return 0;
}

static void Init(HMODULE self) {
    GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
    if (char* s = strrchr(g_dir, '\\')) *s = 0;
    char path[MAX_PATH]; sprintf_s(path, "%s\\KotorPlus.log", g_dir);
    g_log = _fsopen(path, "w", _SH_DENYNO);
    LoadConfig();
    Log("KotorPlus 1.0 loaded. FpsCap=%d CombatReadout=%d ItemCompare=%d", g_fpsCap, g_readout, g_itemCompare);

    char sys[MAX_PATH]; GetSystemDirectoryA(sys, MAX_PATH); strcat_s(sys, "\\dinput8.dll");
    HMODULE real = LoadLibraryA(sys);
    oDI8Create = real ? (DI8Create_t)GetProcAddress(real, "DirectInput8Create") : nullptr;
    if (!oDI8Create) Log("ERROR: could not load system dinput8.dll");

    // Stop the engine's own busy-wait limiter from fighting ours.
    *(float*)0x7A3C64 = 0.0f;

    QueryPerformanceFrequency(&g_qpf);
    timeBeginPeriod(1);
    g_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    if (!g_timer) g_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    g_initTick = GetTickCount();
    InitializeCriticalSection(&g_streamLock);
    InstallImportHooks(true);
    if (CodeUnpacked()) ApplyCodePatches();
    else {
        Log("game code is still packed (Steam DRM) - patches will be applied once it is unpacked");
        CreateThread(nullptr, 0, PatchWaiter, nullptr, 0, nullptr);
    }
}

// All patches of the game's own code. On the Steam exe the code is still encrypted when DllMain
// runs, so this is called later (PatchWaiter / first frame) once the bytes are readable.
static void ApplyCodePatches() {
    if (!CodeUnpacked() || InterlockedExchange(&g_patched, 1)) return;
    // CClientExoAppInternal::IsValidResolution only allows 4:3 modes up to 1600x1200.
    // Make it accept any mode so Width/Height in swkotor.ini (e.g. 1920x1080) are used.
    {
        uint8_t* p = (uint8_t*)0x5F0C60;
        const uint8_t expect[] = { 0x8B, 0x44, 0x24, 0x04, 0x3D, 0x20, 0x03, 0x00, 0x00 };
        if (!memcmp(p, expect, sizeof(expect))) {
            const uint8_t patch[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x08, 0x00 };  // mov eax,1; ret 8
            DWORD old; VirtualProtect(p, sizeof(patch), PAGE_EXECUTE_READWRITE, &old);
            memcpy(p, patch, sizeof(patch));
            VirtualProtect(p, sizeof(patch), old, &old);
            Log("patched IsValidResolution: any resolution allowed");
        } else Log("SKIP IsValidResolution patch: unexpected bytes");
    }

    // Borderless alt-tab: WinMessageHandler treats the game as "fullscreen" on focus changes and then
    // minimizes the window + restores the desktop display mode (black screens), and on return switches
    // the mode back and re-creates the renderer. Make both branches take the windowed path.
    if (g_borderless) {
        struct { uintptr_t at; uint8_t expect[4]; uint8_t newByte; int idx; const char* what; } p[] = {
            { 0x4029CD, { 0x83, 0xF8, 0x01, 0x75 }, 0xEB, 3, "deactivate: no minimize / desktop mode restore" },
            { 0x402A42, { 0x85, 0xC0, 0x74, 0x2C }, 0xEB, 2, "activate: no display mode switch / renderer rebuild" },
        };
        for (auto& x : p) {
            uint8_t* b = (uint8_t*)x.at;
            if (memcmp(b, x.expect, 4)) { Log("SKIP alt-tab patch (%s): unexpected bytes", x.what); continue; }
            DWORD old; VirtualProtect(b + x.idx, 1, PAGE_EXECUTE_READWRITE, &old);
            b[x.idx] = x.newByte;
            VirtualProtect(b + x.idx, 1, old, &old);
            Log("patched alt-tab: %s", x.what);
        }
        // After >1s away the engine skipped ActivateRenderWindow (input + cursor) in "fullscreen" mode,
        // expecting the renderer rebuild we now skip. Always reactivate: jne -> 2 NOPs.
        uint8_t* a = (uint8_t*)0x402ACD;
        const uint8_t ea[] = { 0x85, 0xC0, 0x75, 0x05 };
        if (!memcmp(a, ea, 4)) {
            DWORD old; VirtualProtect(a + 2, 2, PAGE_EXECUTE_READWRITE, &old);
            a[2] = 0x90; a[3] = 0x90;
            VirtualProtect(a + 2, 2, old, &old);
            Log("patched alt-tab: always reactivate input/cursor on return");
        } else Log("SKIP alt-tab reactivate patch: unexpected bytes");
    }

    g_pLastCombatTick = &g_lastCombatTick;
    InstallRecorder();
    HookCall(0x5BACDA, 0x4D2B50, (void*)Hook_GetAttack, "ResolveAttackRoll/GetAttack");
    HookCall(0x5BAD0F, 0x550B60, (void*)Hook_RollDice, "ResolveAttackRoll/RollDice");
    HookCall(0x5BAD38, 0x5A7EA0, (void*)Hook_AttackMod, "ResolveAttackRoll/GetAttackModifierVersus");
    HookCall(0x5BAD64, 0x5AD0E0, (void*)Hook_ArmorClass, "ResolveAttackRoll/GetArmorClassVersus");
    HookCall(0x6B7A19, 0x6B56B0, (void*)Thunk_SetDescription, "Equip/OnItemSelected/SetDescription");
    // same, for items with no special properties (the game shows a "no properties" GUI string)
    HookCall(0x6B7A47, 0x6B56B0, (void*)Thunk_SetDescription, "Equip/OnItemSelected/SetDescription (plain items)");
    HookCall(0x6034D8, 0x5F2F60, (void*)Hook_SetGameSpeed, "MainLoop/SetGameSpeed (fast dialogue)");
    HookCall(0x62129E, 0x61F880, (void*)Thunk_LDown, "HandleInputEvent/LButtonDown (panel blocks clicks)");
    HookCall(0x6212B1, 0x620530, (void*)Thunk_LUp, "HandleInputEvent/LButtonUp");
    HookCall(0x6212CA, 0x61F940, (void*)Thunk_RDown, "HandleInputEvent/RButtonDown");
    HookCall(0x6212DB, 0x61F960, (void*)Thunk_RUp, "HandleInputEvent/RButtonUp");

    // Stuck after combat. CClientExoAppInternal::ProcessInput: while the "drive animation blocked"
    // flag is set (left on after an engaged attack animation) and you push a direction, a 0.3s timer
    // runs and then it only unblocks if CSWPlayerControlCamRelative::GetCurSpeed() > 0.25. But the
    // same code zeroes the drive input while the timer runs, so the speed is always 0 and it retries
    // forever (verified live: 38/38 checks read 0.000). Drop the speed test: jne -> 2 NOPs.
    if (g_stuckFix) SetStuckFix(true);

    // Free look: detour the camera orientation read in CSWPlayerControlCamRelative::Control.
    // Always installed; it does nothing unless FreeLook is on and the middle button is held.
    {
        uint8_t* p = (uint8_t*)0x679B9D;
        const uint8_t ex[] = { 0xFF, 0x50, 0x24, 0x8B, 0xC8, 0xE8 };
        if (!memcmp(p, ex, sizeof(ex))) {
            DWORD old; VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
            p[0] = 0xE9; *(int32_t*)(p + 1) = (int32_t)((uintptr_t)Stub_FreeLook - (0x679B9D + 5));
            VirtualProtect(p, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
            Log("hooked free look (middle mouse)");
            // ProcessInput: "mov ebp, [0x8338F0]" -> read g_keysMouseLook (keys keep turning in free look)
            uint8_t* k = (uint8_t*)0x623A37;
            const uint8_t ek[] = { 0x8B, 0x2D, 0xF0, 0x38, 0x83, 0x00 };
            if (!memcmp(k, ek, sizeof(ek))) {
                VirtualProtect(k, 6, PAGE_EXECUTE_READWRITE, &old);
                *(int**)(k + 2) = &g_keysMouseLook;
                VirtualProtect(k, 6, old, &old);
                FlushInstructionCache(GetCurrentProcess(), k, 6);
            } else Log("SKIP free look key patch: unexpected bytes");
            // UpdateCamera: keep mouse camera turning while A/D are held in free look
            uint8_t* c = (uint8_t*)0x5F5FE1;
            const uint8_t ec[] = { 0xD9, 0x44, 0x24, 0x14, 0xD8, 0x1D, 0x00, 0xD7, 0x73, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x44, 0x7B, 0x37 };
            if (!memcmp(c, ec, sizeof(ec))) {
                VirtualProtect(c, sizeof(ec), PAGE_EXECUTE_READWRITE, &old);
                c[0] = 0xE9; *(int32_t*)(c + 1) = (int32_t)((uintptr_t)Stub_CamKeyTurn - (0x5F5FE1 + 5));
                for (size_t i = 5; i < sizeof(ec); i++) c[i] = 0x90;
                VirtualProtect(c, sizeof(ec), old, &old);
                FlushInstructionCache(GetCurrentProcess(), c, sizeof(ec));
            } else Log("SKIP free look camera patch: unexpected bytes");
        } else { Log("SKIP free look: unexpected bytes"); g_freeLook = 0; }
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); Init(h); }
    return TRUE;
}
