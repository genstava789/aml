#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include "iimgui.h"
#include <algorithm>
#include <atomic>
#include <string.h>
#include <math.h>

MYMOD(com.example.infinitemoney, Infinite Money Mod, 1.3, YourName)
NEEDGAME(com.rockstargames.gtasa)

uintptr_t pGTASA = 0;

// Structures for native GTA 2D rendering & 3D Vector
struct CRGBA
{
    unsigned char r, g, b, a;
    CRGBA() : r(0), g(0), b(0), a(255) {}
    CRGBA(unsigned char _r, unsigned char _g, unsigned char _b, unsigned char _a = 255) : r(_r), g(_g), b(_b), a(_a) {}
};

struct CRect
{
    float left;
    float bottom;
    float right;
    float top;
    CRect(float _left, float _top, float _right, float _bottom)
        : left(_left), bottom(_bottom), right(_right), top(_top) {}
    CRect() : left(0), bottom(0), right(0), top(0) {}
};

struct CVector
{
    float x, y, z;
    CVector() : x(0.0f), y(0.0f), z(0.0f) {}
    CVector(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

inline float GetDistance3D(const CVector& a, const CVector& b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

struct CPoolGeneric
{
    void* m_pObjects;
    uint8_t* m_byteMap;
    int32_t m_nSize;
    int32_t m_nFirstFree;
    bool m_bOwnsAllocations;
};

struct RsGlobalType
{
    const char *appName;
    int32_t maximumWidth;
    int32_t maximumHeight;
    int32_t maxFPS;
    int32_t quit;
    void *ps;
};

// Function pointers from libGTASA.so
void (*CGame_Process)();
void (*CGame_InitialiseWhenRestarting)();
void (*CHud_DrawAfterFade)() = nullptr;
void (*AND_TouchEvent)(int actionType, int trackNum, int x, int y) = nullptr;

typedef void* (*FindPlayerPed_fn)(int);
FindPlayerPed_fn FindPlayerPed = nullptr;

typedef void (*CCheat_MoneyArmourHealthCheat_fn)();
CCheat_MoneyArmourHealthCheat_fn CCheat_MoneyArmourHealthCheat = nullptr;

// Function pointers untuk Cheat Senjata & Fitur IDA Pro
typedef void (*CCheat_WeaponCheat1_fn)();
typedef void (*CCheat_WeaponCheat2_fn)();
typedef void (*CCheat_WeaponCheat3_fn)();
typedef void (*CCheat_WeaponCheat4_fn)();
typedef void (*CCheat_WeaponSkillsCheat_fn)();
typedef void (*CCheat_JetpackCheat_fn)();
typedef void (*CCheat_TogglePlayerInvincibility_fn)();
typedef void (*CPed_GiveWeapon_fn)(void* thisPed, int weaponType, unsigned int ammo, bool bSelect);

CCheat_WeaponCheat1_fn CCheat_WeaponCheat1 = nullptr;
CCheat_WeaponCheat2_fn CCheat_WeaponCheat2 = nullptr;
CCheat_WeaponCheat3_fn CCheat_WeaponCheat3 = nullptr;
CCheat_WeaponCheat4_fn CCheat_WeaponCheat4 = nullptr;
CCheat_WeaponSkillsCheat_fn CCheat_WeaponSkillsCheat = nullptr;
CCheat_JetpackCheat_fn CCheat_JetpackCheat = nullptr;
CCheat_TogglePlayerInvincibility_fn CCheat_TogglePlayerInvincibility = nullptr;
CPed_GiveWeapon_fn CPed_GiveWeapon = nullptr;

typedef void (*CStreaming_RequestModel_fn)(int modelIndex, int flags);
typedef void (*CStreaming_LoadAllRequestedModels_fn)(bool bPriority);
CStreaming_RequestModel_fn CStreaming_RequestModel = nullptr;
CStreaming_LoadAllRequestedModels_fn CStreaming_LoadAllRequestedModels = nullptr;

// Antrean eksekusi aman di Main Game Thread (CGame::Process)
// Menghindari race condition dan SIGSEGV dengan GraphicsThread!
std::atomic<int> g_QueuedWeaponCheat{0};
std::atomic<bool> g_QueuedOfficialCheat{false};
std::atomic<bool> g_QueuedJetpackCheat{false};
std::atomic<bool> g_QueuedGodModeCheat{false};

bool bGodModeActive = false;

typedef bool (*IsPedPointerValid_fn)(void* pPed);
IsPedPointerValid_fn IsPedPointerValid = nullptr;

typedef void (*CPed_GetBonePosition_fn)(uintptr_t pPed, CVector& outPosn, unsigned int boneTag, bool bCalledFromCamera);
CPed_GetBonePosition_fn CPed_GetBonePosition = nullptr;

typedef void (*CCamera_UpdateAimingCoors_fn)(uintptr_t pCamera, const CVector* pNewAimingCoors);
CCamera_UpdateAimingCoors_fn CCamera_UpdateAimingCoors = nullptr;
CCamera_UpdateAimingCoors_fn Orig_CCamera_UpdateAimingCoors = nullptr;

CPoolGeneric** pPedPoolPtr = nullptr;

typedef void (*CSprite2d_DrawRect_fn)(const CRect&, const CRGBA&);
CSprite2d_DrawRect_fn CSprite2d_DrawRect = nullptr;

typedef void (*CFont_PrintString_fn)(float, float, unsigned short*);
CFont_PrintString_fn CFont_PrintString = nullptr;

typedef void (*CFont_RenderFontBuffer_fn)();
CFont_RenderFontBuffer_fn CFont_RenderFontBuffer = nullptr;

typedef void (*CFont_SetColor_fn)(const CRGBA&);
CFont_SetColor_fn CFont_SetColor = nullptr;

typedef void (*CFont_SetScale_fn)(float);
CFont_SetScale_fn CFont_SetScale = nullptr;

typedef void (*CFont_SetFontStyle_fn)(uint8_t);
CFont_SetFontStyle_fn CFont_SetFontStyle = nullptr;

typedef void (*CFont_SetOrientation_fn)(uint8_t);
CFont_SetOrientation_fn CFont_SetOrientation = nullptr;

typedef void (*CFont_SetEdge_fn)(int8_t);
CFont_SetEdge_fn CFont_SetEdge = nullptr;

typedef void (*CFont_SetDropColor_fn)(const CRGBA&);
CFont_SetDropColor_fn CFont_SetDropColor = nullptr;

typedef void (*CFont_SetProportional_fn)(uint8_t);
CFont_SetProportional_fn CFont_SetProportional = nullptr;

typedef void (*CFont_SetWrapx_fn)(float);
CFont_SetWrapx_fn CFont_SetWrapx = nullptr;

// Game symbols
uintptr_t pPlayersArray = 0;
uint8_t* pPlayerInFocus = nullptr;
RsGlobalType* pRsGlobal = nullptr;

// Screen resolution
float g_ScreenWidth = 1920.0f;
float g_ScreenHeight = 1080.0f;

// Konfigurasi & Target Uang
int32_t targetMoney = 2000000;       // Default: $2.000.000
bool bDynamicInfiniteMoney = true;  // Pertahankan uang minimal sebesar targetMoney
bool bForceExactMoney = false;      // Selalu paksa uang tepat sebesar targetMoney
bool bAimAssistHead = false;        // Aim Assist Headshot otomatis
bool bHasLoggedThisSession = false;

// Native Floating CLEO-style Menu State
enum NativeMenuScreen {
    SCREEN_MAIN_MENU = 0,
    SCREEN_MANUAL_SET_MONEY = 1,
    SCREEN_WEAPONS_MENU = 2
};

bool bNativeMenuOpen = false;
NativeMenuScreen g_CurrentMenuScreen = SCREEN_MAIN_MENU;
int g_PressedItem = -1;
int g_MenuPage = 0;
int64_t g_ManualInputMoney = 2000000;
char g_FeedbackMsg[80] = "";
int32_t g_FeedbackTimer = 0;

// Swipe gesture detection (CLEO style swipe down)
bool g_SwipeActive = false;
float g_SwipeStartX = 0.0f;
float g_SwipeStartY = 0.0f;

// ImGui State (jika mod AML_ImGui tersedia)
IImGui* pImGui = nullptr;
bool bImGuiInitialized = false;
bool bShowMoneyMenu = false;
bool bConfigSavedNotice = false;

// Config objects
Config* pConfig = nullptr;
ConfigEntry* entryTarget = nullptr;
ConfigEntry* entryInfinite = nullptr;
ConfigEntry* entryForce = nullptr;
ConfigEntry* entryAimAssist = nullptr;

// Offset memori CPlayerInfo & CPlayerPed disesuaikan per arsitektur (32-bit vs 64-bit)
#if defined(AML32) || defined(__arm__) || !defined(__LP64__)
    static constexpr size_t PLAYER_INFO_SIZE          = 0x194;
    static constexpr size_t OFFSET_PED                = 0x0;
    static constexpr size_t OFFSET_MONEY              = 0xB8;
    static constexpr size_t OFFSET_DISPLAY_MONEY      = 0xBC;
    static constexpr size_t OFFSET_TARGETTED_PED      = 0x7A4;
    static constexpr size_t OFFSET_TARGETTED_PED_MIN  = 0x798;
    static constexpr size_t OFFSET_TARGETTED_PED_MAX  = 0x7AC;
    static constexpr size_t SIZEOF_CPED               = 0x7A4;
#else
    static constexpr size_t PLAYER_INFO_SIZE          = 0x1D8;
    static constexpr size_t OFFSET_PED                = 0x0;
    static constexpr size_t OFFSET_MONEY              = 0xF0;
    static constexpr size_t OFFSET_DISPLAY_MONEY      = 0xF4;
    static constexpr size_t OFFSET_TARGETTED_PED      = 0x988;
    static constexpr size_t OFFSET_TARGETTED_PED_MIN  = 0x978;
    static constexpr size_t OFFSET_TARGETTED_PED_MAX  = 0x998;
    static constexpr size_t SIZEOF_CPED               = 0x988;
#endif

// Helper untuk konversi ASCII ke GxtChar (unsigned short)
inline void ConvertAsciiToGxt(const char* src, unsigned short* dst, size_t maxLen)
{
    if (!src || !dst || maxLen < 2) return;
    size_t i = 0;
    while (src[i] && i < maxLen - 2)
    {
        char c = src[i];
        // CFont GTA SA menggunakan '~' untuk token formatting (~r~, ~g~, ~w~, dll).
        // Tilde tunggal atau tidak berpasangan akan membuat CFont::ParseToken
        // membaca memori melampaui buffer dan menyebabkan SIGSEGV!
        if (c == '~') c = '-';
        dst[i] = (unsigned short)(unsigned char)c;
        ++i;
    }
    // GXT strings di GTA SA memerlukan null-termination ganda untuk keamanan ekstra
    dst[i] = 0;
    dst[i + 1] = 0;
}

uintptr_t GetLocalPlayerPtr()
{
    if (!pPlayersArray) return 0;
    int playerIndex = (pPlayerInFocus != nullptr) ? *pPlayerInFocus : 0;
    if (playerIndex < 0 || playerIndex > 1) playerIndex = 0;
    return pPlayersArray + (playerIndex * PLAYER_INFO_SIZE);
}

bool IsPlayerInGame()
{
    uintptr_t localPlayer = GetLocalPlayerPtr();
    if (!localPlayer) return false;

    uintptr_t playerPed = *(uintptr_t*)(localPlayer + OFFSET_PED);
    if (FindPlayerPed != nullptr && FindPlayerPed(-1) == nullptr) return false;

    return (playerPed != 0);
}

int32_t GetCurrentPlayerMoney()
{
    uintptr_t localPlayer = GetLocalPlayerPtr();
    if (!localPlayer) return 0;
    return *(int32_t*)(localPlayer + OFFSET_MONEY);
}

void SetPlayerMoneyDirect(int32_t amount)
{
    uintptr_t localPlayer = GetLocalPlayerPtr();
    if (!localPlayer) return;
    *(int32_t*)(localPlayer + OFFSET_MONEY) = amount;
    *(int32_t*)(localPlayer + OFFSET_DISPLAY_MONEY) = amount;
    logger->Info("Uang player langsung diset ke: %d", amount);
}

void SaveMoneyConfig()
{
    if (pConfig && entryTarget && entryInfinite && entryForce)
    {
        entryTarget->SetInt(targetMoney);
        entryInfinite->SetBool(bDynamicInfiniteMoney);
        entryForce->SetBool(bForceExactMoney);
        if (entryAimAssist) entryAimAssist->SetBool(bAimAssistHead);
        pConfig->Save();
        logger->Info("Konfigurasi disimpan: Target=%d, Dynamic=%d, Force=%d, AimAssist=%d",
                     targetMoney, bDynamicInfiniteMoney, bForceExactMoney, bAimAssistHead);
        bConfigSavedNotice = true;
    }
}

void SetFeedback(const char* msg)
{
    snprintf(g_FeedbackMsg, sizeof(g_FeedbackMsg), "%s", msg);
    g_FeedbackTimer = 150; // Tampilkan ~3 detik
}

// -------------------------------------------------------------
// Native GTA 2D Drawing Helpers
// -------------------------------------------------------------
void DrawFilledBox(float left, float top, float right, float bottom, const CRGBA& color)
{
    if (!CSprite2d_DrawRect) return;
    CRect rect(left, top, right, bottom);
    CSprite2d_DrawRect(rect, color);
}

void DrawBorderedBox(float left, float top, float right, float bottom, const CRGBA& bgColor, const CRGBA& borderColor, float borderSize = 2.0f)
{
    DrawFilledBox(left, top, right, bottom, bgColor);
    DrawFilledBox(left, top, right, top + borderSize, borderColor);
    DrawFilledBox(left, bottom - borderSize, right, bottom, borderColor);
    DrawFilledBox(left, top, left + borderSize, bottom, borderColor);
    DrawFilledBox(right - borderSize, top, right, bottom, borderColor);
}

void DrawTextAt(float x, float y, const char* text, float scale, const CRGBA& color, uint8_t style = 1, uint8_t orientation = 1)
{
    if (!CFont_PrintString || !CFont_RenderFontBuffer || !text || text[0] == '\0') return;

    unsigned short gxtBuf[256];
    memset(gxtBuf, 0, sizeof(gxtBuf));
    ConvertAsciiToGxt(text, gxtBuf, 256);

    CRGBA dropColor(0, 0, 0, 255);

    if (CFont_SetScale) CFont_SetScale(scale);
    if (CFont_SetColor) CFont_SetColor(color);
    if (CFont_SetFontStyle) CFont_SetFontStyle(style);
    if (CFont_SetOrientation) CFont_SetOrientation(orientation);
    if (CFont_SetEdge) CFont_SetEdge(1);
    if (CFont_SetDropColor) CFont_SetDropColor(dropColor);
    if (CFont_SetWrapx) CFont_SetWrapx(10000.0f);
    if (CFont_SetProportional) CFont_SetProportional(1);

    CFont_PrintString(x, y, gxtBuf);
    CFont_RenderFontBuffer();
}

// -------------------------------------------------------------
// Helper Format Nominal Uang & Eksekusi Cheat
// -------------------------------------------------------------
inline void FormatMoneyNumber(int64_t amount, char* out, size_t outSize)
{
    if (amount <= 0)
    {
        snprintf(out, outSize, "$ 0");
        return;
    }
    char raw[32];
    snprintf(raw, sizeof(raw), "%lld", (long long)amount);
    int len = (int)strlen(raw);

    char formatted[48];
    int fIdx = 0;
    formatted[fIdx++] = '$';
    formatted[fIdx++] = ' ';

    for (int i = 0; i < len; ++i)
    {
        if (i > 0 && (len - i) % 3 == 0)
        {
            formatted[fIdx++] = '.';
        }
        formatted[fIdx++] = raw[i];
    }
    formatted[fIdx] = '\0';
    snprintf(out, outSize, "%s", formatted);
}

void SafeGiveWeapon(int weaponType, int modelIndex, int ammo)
{
    if (!FindPlayerPed) return;
    void* playerPed = FindPlayerPed(-1);
    if (!playerPed)
    {
        uintptr_t localPlayer = GetLocalPlayerPtr();
        if (localPlayer) playerPed = *(void**)(localPlayer + OFFSET_PED);
    }
    if (!playerPed) return;
    if (IsPedPointerValid && !IsPedPointerValid(playerPed)) return;

    if (modelIndex > 0 && CStreaming_RequestModel && CStreaming_LoadAllRequestedModels)
    {
        CStreaming_RequestModel(modelIndex, 2); // 2 = STREAMING_GAME_REQUIRED
        CStreaming_LoadAllRequestedModels(false);
    }

    if (CPed_GiveWeapon)
    {
        CPed_GiveWeapon(playerPed, weaponType, ammo, true);
        logger->Info("[Cheat] SafeGiveWeapon: tipe=%d, model=%d, ammo=%d berhasil diberikan.", weaponType, modelIndex, ammo);
    }
}

void ExecuteWeaponCheat(int kitNumber)
{
    if (!IsPlayerInGame()) return;

    switch (kitNumber)
    {
    case 1:
        if (CCheat_WeaponCheat1)
        {
            CCheat_WeaponCheat1();
            logger->Info("[Cheat] CCheat::WeaponCheat1 (0x3C1248) berhasil dieksekusi di Main Thread.");
        }
        break;

    case 2:
        if (CCheat_WeaponCheat2)
        {
            CCheat_WeaponCheat2();
            logger->Info("[Cheat] CCheat::WeaponCheat2 (0x3C1508) berhasil dieksekusi di Main Thread.");
        }
        break;

    case 3:
        if (CCheat_WeaponCheat3)
        {
            CCheat_WeaponCheat3();
            logger->Info("[Cheat] CCheat::WeaponCheat3 (0x3C178C) berhasil dieksekusi di Main Thread.");
        }
        break;

    case 4:
        if (CCheat_WeaponCheat4)
        {
            CCheat_WeaponCheat4();
            logger->Info("[Cheat] CCheat::WeaponCheat4 (0x3C199C) berhasil dieksekusi di Main Thread.");
        }
        break;

    case 5: // Max Weapon Skills (Hitman)
        if (CCheat_WeaponSkillsCheat)
        {
            CCheat_WeaponSkillsCheat();
            logger->Info("[Cheat] CCheat::WeaponSkillsCheat (0x3C2E90) berhasil dieksekusi di Main Thread.");
        }
        break;

    case 6: // Minigun 9999 Ammo (Weapon 38, Model 362)
        SafeGiveWeapon(38, 362, 9999);
        break;

    case 7: // Katana (Weapon 8, Model 339) + Parachute (Weapon 46, Model 371)
        SafeGiveWeapon(8, 339, 1);
        SafeGiveWeapon(46, 371, 1);
        break;

    default:
        break;
    }
}

void ExecuteOfficialCheat()
{
    if (CCheat_MoneyArmourHealthCheat)
    {
        CCheat_MoneyArmourHealthCheat();
        logger->Info("[Cheat] CCheat::MoneyArmourHealthCheat dieksekusi di Main Thread.");
    }
    else if (IsPlayerInGame())
    {
        int32_t cur = GetCurrentPlayerMoney();
        int32_t next = cur + 250000;
        SetPlayerMoneyDirect(next);
        if (targetMoney < next) targetMoney = next;
    }
}

void ExecuteJetpackCheat()
{
    if (CCheat_JetpackCheat)
    {
        CCheat_JetpackCheat();
        logger->Info("[Cheat] CCheat::JetpackCheat (0x3C2A40) dieksekusi di Main Thread.");
    }
}

void ExecuteGodModeCheat()
{
    if (CCheat_TogglePlayerInvincibility)
    {
        CCheat_TogglePlayerInvincibility();
        logger->Info("[Cheat] CCheat::TogglePlayerInvincibility (0x3C1AB0) dieksekusi di Main Thread: status=%d", bGodModeActive);
    }
}

// Queue functions (Dipanggil dari Touch Event / ImGui - Aman & Non-blocking)
void QueueOfficialCheat()
{
    if (!IsPlayerInGame())
    {
        SetFeedback(">> Gagal: Player belum di dalam gameplay!");
        return;
    }
    g_QueuedOfficialCheat.store(true);
    SetFeedback(">> Cheat Resmi Aktif: Health, Armor & Uang!");
}

void QueueWeaponCheat(int kitNumber)
{
    if (!IsPlayerInGame())
    {
        SetFeedback(">> Gagal: Player belum di dalam gameplay!");
        return;
    }
    g_QueuedWeaponCheat.store(kitNumber);

    switch (kitNumber)
    {
    case 1: SetFeedback(">> Kit 1 Diberikan: Thug Tools (0x3C1248)!"); break;
    case 2: SetFeedback(">> Kit 2 Diberikan: Professional Tools (0x3C1508)!"); break;
    case 3: SetFeedback(">> Kit 3 Diberikan: Nutter Tools (0x3C178C)!"); break;
    case 4: SetFeedback(">> Kit 4 Diberikan: Special Arsenal (0x3C199C)!"); break;
    case 5: SetFeedback(">> Max Weapon Skills: Hitman Level Semua (0x3C2E90)!"); break;
    case 6: SetFeedback(">> Minigun 9999 Peluru Berhasil Diberikan (0x59525C)!"); break;
    case 7: SetFeedback(">> Katana & Parachute Berhasil Diberikan!"); break;
    default: break;
    }
}

void QueueJetpackCheat()
{
    if (!IsPlayerInGame())
    {
        SetFeedback(">> Gagal: Player belum di dalam gameplay!");
        return;
    }
    g_QueuedJetpackCheat.store(true);
    SetFeedback(">> Cheat Jetpack Aktif: Jetpack Muncul (0x3C2A40)!");
}

void QueueGodModeCheat()
{
    if (!IsPlayerInGame())
    {
        SetFeedback(">> Gagal: Player belum di dalam gameplay!");
        return;
    }
    bGodModeActive = !bGodModeActive;
    g_QueuedGodModeCheat.store(true);
    SetFeedback(bGodModeActive ? ">> God Mode: AKTIF (0x3C1AB0)!" : ">> God Mode: NONAKTIF (0x3C1AB0)!");
}

// -------------------------------------------------------------
// Aim Assist Headshot (Otomatis Bidik Kepala NPC)
// Menggunakan CCamera::UpdateAimingCoors & CPed::GetBonePosition
// -------------------------------------------------------------
uintptr_t GetPlayerTargetedPed(uintptr_t playerPed)
{
    if (!playerPed) return 0;

    // Cek offset targetted ped standar
    uintptr_t candidate = *(uintptr_t*)(playerPed + OFFSET_TARGETTED_PED);
    if (candidate > 0x100000 && candidate != playerPed)
    {
        if (!IsPedPointerValid || IsPedPointerValid((void*)candidate))
        {
            return candidate;
        }
    }

    // Scan toleransi padding di sekitarnya hanya jika IsPedPointerValid tersedia
    if (IsPedPointerValid)
    {
        for (size_t off = OFFSET_TARGETTED_PED_MIN; off <= OFFSET_TARGETTED_PED_MAX; off += sizeof(void*))
        {
            uintptr_t c = *(uintptr_t*)(playerPed + off);
            if (c > 0x100000 && c != playerPed && IsPedPointerValid((void*)c))
            {
                return c;
            }
        }
    }

    return 0;
}

uintptr_t FindClosestPedToPoint(const CVector& point, uintptr_t ignorePed, float maxDistance)
{
    if (!pPedPoolPtr || !*pPedPoolPtr || !CPed_GetBonePosition) return 0;
    CPoolGeneric* pool = *pPedPoolPtr;
    if (!pool || !pool->m_pObjects || !pool->m_byteMap || pool->m_nSize <= 0 || pool->m_nSize > 500) return 0;

    uintptr_t bestPed = 0;
    float bestDist = maxDistance;

    for (int i = 0; i < pool->m_nSize; ++i)
    {
        if (pool->m_byteMap[i] & 0x80) continue; // slot kosong

        uintptr_t ped = (uintptr_t)pool->m_pObjects + (i * SIZEOF_CPED);
        if (ped == ignorePed || ped < 0x100000) continue;
        if (IsPedPointerValid && !IsPedPointerValid((void*)ped)) continue;

        CVector head(0.0f, 0.0f, 0.0f);
        CPed_GetBonePosition(ped, head, 8, false); // 8 = BONE_HEAD
        if (head.x == 0.0f && head.y == 0.0f && head.z == 0.0f) continue;

        float d = GetDistance3D(head, point);
        if (d < bestDist)
        {
            bestDist = d;
            bestPed = ped;
        }
    }
    return bestPed;
}

void Hooked_CCamera_UpdateAimingCoors(uintptr_t camera, const CVector* pNewAimingCoors)
{
    if (!bAimAssistHead || !pNewAimingCoors || !IsPlayerInGame())
    {
        if (Orig_CCamera_UpdateAimingCoors) Orig_CCamera_UpdateAimingCoors(camera, pNewAimingCoors);
        return;
    }

    uintptr_t localPlayer = GetLocalPlayerPtr();
    if (!localPlayer)
    {
        if (Orig_CCamera_UpdateAimingCoors) Orig_CCamera_UpdateAimingCoors(camera, pNewAimingCoors);
        return;
    }

    uintptr_t playerPed = *(uintptr_t*)(localPlayer + OFFSET_PED);
    if (!playerPed)
    {
        if (Orig_CCamera_UpdateAimingCoors) Orig_CCamera_UpdateAimingCoors(camera, pNewAimingCoors);
        return;
    }

    // 1. Dapatkan NPC yang sedang dibidik / dikunci oleh pemain
    uintptr_t targetPed = GetPlayerTargetedPed(playerPed);

    // 2. Jika tidak terkunci otomatis, cari NPC terdekat dari titik bidikan crosshair
    if (!targetPed && pPedPoolPtr && *pPedPoolPtr && CPed_GetBonePosition)
    {
        targetPed = FindClosestPedToPoint(*pNewAimingCoors, playerPed, 3.5f);
    }

    // 3. Alihkan koordinat bidikan kamera langsung ke kepala NPC (Bone 8: BONE_HEAD)
    if (targetPed && CPed_GetBonePosition)
    {
        CVector headPos(0.0f, 0.0f, 0.0f);
        CPed_GetBonePosition(targetPed, headPos, 8, false); // Bone 8 = BONE_HEAD

        if (headPos.x != 0.0f || headPos.y != 0.0f || headPos.z != 0.0f)
        {
            if (Orig_CCamera_UpdateAimingCoors)
            {
                Orig_CCamera_UpdateAimingCoors(camera, &headPos);
                return;
            }
        }
    }

    if (Orig_CCamera_UpdateAimingCoors)
    {
        Orig_CCamera_UpdateAimingCoors(camera, pNewAimingCoors);
    }
}

// -------------------------------------------------------------
// Definisi Menu Utama
// -------------------------------------------------------------
enum MainMenuAction
{
    ACTION_OPEN_SET_MONEY = 1,
    ACTION_TRIGGER_OFFICIAL_CHEAT = 2,
    ACTION_TOGGLE_AIM_ASSIST = 3,
    ACTION_CLOSE_MENU = 4
};

const int TOTAL_MENU_ITEMS = 7;
const int MAX_ITEMS_PER_PAGE = 10; // Mendukung pagination otomatis jika menu melebihi 10 item!

// -------------------------------------------------------------
// Native Floating Mod Menu Render & Layout
// -------------------------------------------------------------
void DrawNativeFloatingMenu()
{
    if (!CSprite2d_DrawRect || !CFont_PrintString) return;

    // HANYA GAMBAR JIKA PLAYER SUDAH DI DALAM GAMEPLAY!
    // Mencegah crash saat di layar FrontendIdle / Main Menu / Loading!
    if (!IsPlayerInGame()) return;

    if (pRsGlobal && pRsGlobal->maximumWidth > 0 && pRsGlobal->maximumHeight > 0)
    {
        g_ScreenWidth = (float)pRsGlobal->maximumWidth;
        g_ScreenHeight = (float)pRsGlobal->maximumHeight;
    }

    bool inGame = IsPlayerInGame();
    int32_t curMoney = inGame ? GetCurrentPlayerMoney() : 0;
    float scaleRatio = g_ScreenHeight / 1080.0f;
    if (scaleRatio < 0.65f) scaleRatio = 0.65f;
    if (scaleRatio > 1.35f) scaleRatio = 1.35f;

    // 1. FLOATING BUTTON ICON (DEFAULT DI TOP CENTER LAYAR)
    float btnWidth  = 290.0f * scaleRatio;
    float btnHeight = 54.0f  * scaleRatio;
    float btnLeft   = (g_ScreenWidth - btnWidth) * 0.5f; // Posisi TOP CENTER!
    float btnTop    = 15.0f  * scaleRatio;
    float btnRight  = btnLeft + btnWidth;
    float btnBottom = btnTop + btnHeight;

    if (!bNativeMenuOpen)
    {
        DrawBorderedBox(btnLeft, btnTop, btnRight, btnBottom, CRGBA(10, 16, 26, 235), CRGBA(255, 215, 0, 255), 2.5f * scaleRatio);
        DrawTextAt(btnLeft + (24.0f * scaleRatio), btnTop + (13.0f * scaleRatio), "[+] CHEAT MENU (AML)", 1.20f * scaleRatio, CRGBA(255, 230, 80, 255));
    }
    else
    {
        DrawBorderedBox(btnLeft, btnTop, btnRight, btnBottom, CRGBA(140, 25, 25, 240), CRGBA(255, 255, 255, 255), 2.5f * scaleRatio);
        DrawTextAt(btnLeft + (36.0f * scaleRatio), btnTop + (13.0f * scaleRatio), "[X] TUTUP MENU", 1.20f * scaleRatio, CRGBA(255, 255, 255, 255));
    }

    // 2. LAYAR MENU UTAMA (SCREEN_MAIN_MENU)
    if (bNativeMenuOpen && g_CurrentMenuScreen == SCREEN_MAIN_MENU)
    {
        int totalPages = (TOTAL_MENU_ITEMS + MAX_ITEMS_PER_PAGE - 1) / MAX_ITEMS_PER_PAGE;
        if (totalPages < 1) totalPages = 1;
        if (g_MenuPage >= totalPages) g_MenuPage = totalPages - 1;
        if (g_MenuPage < 0) g_MenuPage = 0;

        int startIndex = g_MenuPage * MAX_ITEMS_PER_PAGE;
        int itemsOnPage = TOTAL_MENU_ITEMS - startIndex;
        if (itemsOnPage > MAX_ITEMS_PER_PAGE) itemsOnPage = MAX_ITEMS_PER_PAGE;

        bool hasPagination = (TOTAL_MENU_ITEMS > MAX_ITEMS_PER_PAGE);

        float menuW = 620.0f * scaleRatio;
        float itemH = 50.0f * scaleRatio;
        float itemGap = 8.0f * scaleRatio;
        float headerH = 50.0f * scaleRatio;
        float subH = 36.0f * scaleRatio;
        float bottomPadding = (hasPagination ? 75.0f : 35.0f) * scaleRatio;
        float menuH = headerH + subH + (itemsOnPage * (itemH + itemGap)) + bottomPadding + (g_FeedbackTimer > 0 ? 30.0f * scaleRatio : 0.0f);

        float menuX = (g_ScreenWidth - menuW) * 0.5f;
        float menuY = (g_ScreenHeight - menuH) * 0.5f;

        // Background dan Border Jendela
        DrawBorderedBox(menuX, menuY, menuX + menuW, menuY + menuH, CRGBA(12, 16, 24, 245), CRGBA(255, 215, 0, 255), 3.0f * scaleRatio);

        // Header Title Bar
        DrawFilledBox(menuX, menuY, menuX + menuW, menuY + headerH, CRGBA(25, 85, 45, 255));
        DrawTextAt(menuX + (25.0f * scaleRatio), menuY + (12.0f * scaleRatio), "--- CHEAT SUITE & RESMI (AML) ---", 1.20f * scaleRatio, CRGBA(255, 255, 255, 255));

        // Subtitle Status & Uang
        char subBuf[128];
        snprintf(subBuf, sizeof(subBuf), "CJ: $%d | Target: $%d | %s", curMoney, targetMoney, inGame ? "In-Game" : "Menu");
        DrawTextAt(menuX + (25.0f * scaleRatio), menuY + headerH + (7.0f * scaleRatio), subBuf, 1.05f * scaleRatio, inGame ? CRGBA(120, 255, 140, 255) : CRGBA(255, 180, 70, 255));

        // Pemisah garis
        DrawFilledBox(menuX + (12.0f * scaleRatio), menuY + headerH + subH, menuX + menuW - (12.0f * scaleRatio), menuY + headerH + subH + (2.0f * scaleRatio), CRGBA(255, 215, 0, 180));

        // Label & Gaya Dinamis Tombol Menu
        char aimLabel[64];
        snprintf(aimLabel, sizeof(aimLabel), "[4] AIM ASSIST HEAD: [%s]", bAimAssistHead ? "AKTIF" : "NONAKTIF");
        char godLabel[64];
        snprintf(godLabel, sizeof(godLabel), "[6] GOD MODE / INVINCIBLE: [%s]", bGodModeActive ? "AKTIF" : "NONAKTIF");

        const char* itemLabels[7] = {
            "[1] MENU CHEAT SENJATA (WEAPONS KIT)",
            "[2] SET UANG (INPUT MANUAL NOMINAL)",
            "[3] CHEAT RESMI: HEALTH, ARMOR & UANG",
            aimLabel,
            "[5] CHEAT JETPACK (SPAWN JETPACK)",
            godLabel,
            "[X] TUTUP MENU"
        };
        CRGBA itemBgs[7] = {
            CRGBA(20, 45, 75, 235),
            CRGBA(25, 40, 60, 235),
            CRGBA(20, 50, 32, 235),
            bAimAssistHead ? CRGBA(22, 68, 36, 235) : CRGBA(38, 42, 54, 235),
            CRGBA(45, 30, 65, 235),
            bGodModeActive ? CRGBA(75, 25, 25, 235) : CRGBA(42, 38, 50, 235),
            CRGBA(55, 22, 22, 235)
        };
        CRGBA itemBorders[7] = {
            CRGBA(100, 200, 255, 230),
            CRGBA(255, 215, 0, 230),
            CRGBA(60, 225, 105, 230),
            bAimAssistHead ? CRGBA(80, 255, 130, 240) : CRGBA(140, 155, 175, 200),
            CRGBA(190, 120, 255, 230),
            bGodModeActive ? CRGBA(255, 80, 80, 240) : CRGBA(160, 140, 190, 200),
            CRGBA(225, 70, 70, 230)
        };
        CRGBA itemTextColors[7] = {
            CRGBA(180, 230, 255, 255),
            CRGBA(255, 235, 120, 255),
            CRGBA(140, 255, 160, 255),
            bAimAssistHead ? CRGBA(120, 255, 160, 255) : CRGBA(210, 220, 235, 255),
            CRGBA(230, 190, 255, 255),
            bGodModeActive ? CRGBA(255, 160, 160, 255) : CRGBA(220, 210, 240, 255),
            CRGBA(255, 130, 130, 255)
        };

        // Daftar Tombol Aksi Menu Sesuai Halaman Aktif
        float startY = menuY + headerH + subH + (12.0f * scaleRatio);
        float itemLeft = menuX + (20.0f * scaleRatio);
        float itemRight = menuX + menuW - (20.0f * scaleRatio);

        for (int i = 0; i < itemsOnPage; ++i)
        {
            int globalIndex = startIndex + i;

            float rowTop = startY + i * (itemH + itemGap);
            float rowBottom = rowTop + itemH;

            bool isPressed = (g_PressedItem == globalIndex);
            CRGBA bg = isPressed ? CRGBA(60, 170, 85, 255) : itemBgs[globalIndex];
            CRGBA border = isPressed ? CRGBA(255, 255, 255, 255) : itemBorders[globalIndex];

            DrawBorderedBox(itemLeft, rowTop, itemRight, rowBottom, bg, border, 2.0f * scaleRatio);
            DrawTextAt(itemLeft + (20.0f * scaleRatio), rowTop + (12.0f * scaleRatio), itemLabels[globalIndex], 1.15f * scaleRatio, itemTextColors[globalIndex]);
        }

        // Pagination Bar jika item menu melebihi MAX_ITEMS_PER_PAGE (10)
        if (hasPagination)
        {
            float pagY = startY + itemsOnPage * (itemH + itemGap) + (6.0f * scaleRatio);
            float pagBtnW = 140.0f * scaleRatio;
            float pagBtnH = 42.0f * scaleRatio;

            // Tombol Prev
            bool prevPressed = (g_PressedItem == 90);
            DrawBorderedBox(itemLeft, pagY, itemLeft + pagBtnW, pagY + pagBtnH,
                            prevPressed ? CRGBA(70, 90, 120, 255) : CRGBA(35, 45, 60, 230),
                            CRGBA(180, 200, 220, 255), 1.5f * scaleRatio);
            DrawTextAt(itemLeft + (15.0f * scaleRatio), pagY + (10.0f * scaleRatio), "[< PREV]", 1.10f * scaleRatio, CRGBA(255, 255, 255, 255));

            // Info Halaman
            char pagInfo[48];
            snprintf(pagInfo, sizeof(pagInfo), "HALAMAN %d / %d", g_MenuPage + 1, totalPages);
            DrawTextAt(itemLeft + pagBtnW + (20.0f * scaleRatio), pagY + (10.0f * scaleRatio), pagInfo, 1.10f * scaleRatio, CRGBA(255, 235, 120, 255));

            // Tombol Next
            bool nextPressed = (g_PressedItem == 91);
            float nextLeft = itemRight - pagBtnW;
            DrawBorderedBox(nextLeft, pagY, itemRight, pagY + pagBtnH,
                            nextPressed ? CRGBA(70, 90, 120, 255) : CRGBA(35, 45, 60, 230),
                            CRGBA(180, 200, 220, 255), 1.5f * scaleRatio);
            DrawTextAt(nextLeft + (15.0f * scaleRatio), pagY + (10.0f * scaleRatio), "[NEXT >]", 1.10f * scaleRatio, CRGBA(255, 255, 255, 255));
        }

        // Pesan Notifikasi Feedback
        if (g_FeedbackTimer > 0 && g_FeedbackMsg[0] != '\0')
        {
            --g_FeedbackTimer;
            DrawTextAt(menuX + (25.0f * scaleRatio), menuY + menuH - (25.0f * scaleRatio), g_FeedbackMsg, 1.05f * scaleRatio, CRGBA(60, 255, 100, 255));
        }
    }
    // 3. LAYAR SET UANG MANUAL DENGAN KEYPAD & APPLY BUTTON (SCREEN_MANUAL_SET_MONEY)
    else if (bNativeMenuOpen && g_CurrentMenuScreen == SCREEN_MANUAL_SET_MONEY)
    {
        float winW = 600.0f * scaleRatio;
        float winH = 540.0f * scaleRatio;
        float winX = (g_ScreenWidth - winW) * 0.5f;
        float winY = (g_ScreenHeight - winH) * 0.5f;

        // Background dan Border Jendela
        DrawBorderedBox(winX, winY, winX + winW, winY + winH, CRGBA(12, 16, 24, 248), CRGBA(255, 215, 0, 255), 3.0f * scaleRatio);

        // Header Title Bar
        DrawFilledBox(winX, winY, winX + winW, winY + (48.0f * scaleRatio), CRGBA(25, 85, 45, 255));
        DrawTextAt(winX + (25.0f * scaleRatio), winY + (12.0f * scaleRatio), "--- SET UANG: INPUT MANUAL NOMINAL ---", 1.15f * scaleRatio, CRGBA(255, 255, 255, 255));

        // Display Box Nominal Uang
        float dispLeft = winX + (20.0f * scaleRatio);
        float dispRight = winX + winW - (20.0f * scaleRatio);
        float dispTop = winY + (58.0f * scaleRatio);
        float dispH = 55.0f * scaleRatio;

        DrawBorderedBox(dispLeft, dispTop, dispRight, dispTop + dispH, CRGBA(8, 14, 22, 255), CRGBA(255, 215, 0, 255), 2.0f * scaleRatio);

        char moneyFormatted[64];
        FormatMoneyNumber(g_ManualInputMoney, moneyFormatted, sizeof(moneyFormatted));
        DrawTextAt(dispLeft + (20.0f * scaleRatio), dispTop + (13.0f * scaleRatio), moneyFormatted, 1.30f * scaleRatio, CRGBA(80, 255, 120, 255));

        // Quick Preset Buttons Row
        float quickTop = dispTop + dispH + (10.0f * scaleRatio);
        float quickH = 42.0f * scaleRatio;
        float totalQuickW = dispRight - dispLeft;
        float quickGap = 6.0f * scaleRatio;
        float qBtnW = (totalQuickW - 4.0f * quickGap) / 5.0f;

        const char* qLabels[5] = { "+100K", "+1M", "+10M", "MAX", "CLEAR" };
        int qIds[5] = { 100, 101, 102, 103, 104 };

        for (int q = 0; q < 5; ++q)
        {
            float qLeft = dispLeft + q * (qBtnW + quickGap);
            float qRight = qLeft + qBtnW;
            bool qPressed = (g_PressedItem == qIds[q]);

            CRGBA qBg = qPressed ? CRGBA(60, 160, 80, 255) : (q == 3 ? CRGBA(70, 50, 20, 230) : (q == 4 ? CRGBA(70, 25, 25, 230) : CRGBA(30, 42, 58, 230)));
            CRGBA qBorder = qPressed ? CRGBA(255, 255, 255, 255) : CRGBA(180, 200, 220, 200);

            DrawBorderedBox(qLeft, quickTop, qRight, quickTop + quickH, qBg, qBorder, 1.5f * scaleRatio);
            DrawTextAt(qLeft + (10.0f * scaleRatio), quickTop + (10.0f * scaleRatio), qLabels[q], 1.05f * scaleRatio, CRGBA(255, 255, 255, 255));
        }

        // Numeric Keypad Grid (4 baris x 3 kolom)
        float gridTop = quickTop + quickH + (10.0f * scaleRatio);
        float keyH = 46.0f * scaleRatio;
        float rowGap = 7.0f * scaleRatio;
        float colGap = 8.0f * scaleRatio;
        float keyW = (totalQuickW - 2.0f * colGap) / 3.0f;

        const char* keyLabels[4][3] = {
            { "1", "2", "3" },
            { "4", "5", "6" },
            { "7", "8", "9" },
            { "000", "0", "DEL" }
        };
        int keyIds[4][3] = {
            { 1, 2, 3 },
            { 4, 5, 6 },
            { 7, 8, 9 },
            { 20, 0, 21 }
        };

        for (int r = 0; r < 4; ++r)
        {
            float rTop = gridTop + r * (keyH + rowGap);
            float rBottom = rTop + keyH;

            for (int c = 0; c < 3; ++c)
            {
                float kLeft = dispLeft + c * (keyW + colGap);
                float kRight = kLeft + keyW;

                int kId = keyIds[r][c];
                bool kPressed = (g_PressedItem == kId);

                CRGBA kBg = kPressed ? CRGBA(80, 180, 90, 255) : (kId == 21 ? CRGBA(65, 30, 30, 230) : CRGBA(26, 34, 46, 230));
                CRGBA kBorder = kPressed ? CRGBA(255, 255, 255, 255) : CRGBA(90, 110, 140, 200);

                DrawBorderedBox(kLeft, rTop, kRight, rBottom, kBg, kBorder, 1.5f * scaleRatio);

                float fontScale = (kId == 20 || kId == 21) ? 1.15f * scaleRatio : 1.30f * scaleRatio;
                float textOffset = (kId == 20) ? (16.0f * scaleRatio) : (kId == 21 ? (20.0f * scaleRatio) : (keyW * 0.42f));

                DrawTextAt(kLeft + textOffset, rTop + (9.0f * scaleRatio), keyLabels[r][c], fontScale, CRGBA(255, 255, 255, 255));
            }
        }

        // Action Buttons Row (KEMBALI & TERAPKAN / APPLY)
        float actTop = gridTop + 4 * (keyH + rowGap) + (6.0f * scaleRatio);
        float actH = 50.0f * scaleRatio;

        // Tombol Kembali
        float backW = 180.0f * scaleRatio;
        bool backPressed = (g_PressedItem == 30);
        DrawBorderedBox(dispLeft, actTop, dispLeft + backW, actTop + actH,
                        backPressed ? CRGBA(160, 40, 40, 255) : CRGBA(70, 28, 28, 235),
                        CRGBA(240, 100, 100, 220), 2.0f * scaleRatio);
        DrawTextAt(dispLeft + (25.0f * scaleRatio), actTop + (13.0f * scaleRatio), "[< KEMBALI]", 1.15f * scaleRatio, CRGBA(255, 255, 255, 255));

        // Tombol Terapkan (APPLY)
        float applyLeft = dispLeft + backW + (10.0f * scaleRatio);
        float applyRight = dispRight;
        bool applyPressed = (g_PressedItem == 31);
        DrawBorderedBox(applyLeft, actTop, applyRight, actTop + actH,
                        applyPressed ? CRGBA(40, 200, 70, 255) : CRGBA(28, 130, 45, 240),
                        CRGBA(100, 255, 140, 255), 2.0f * scaleRatio);
        DrawTextAt(applyLeft + (30.0f * scaleRatio), actTop + (13.0f * scaleRatio), ">>> TERAPKAN (APPLY) <<<", 1.20f * scaleRatio, CRGBA(255, 255, 255, 255));

        // Feedback notification
        if (g_FeedbackTimer > 0 && g_FeedbackMsg[0] != '\0')
        {
            --g_FeedbackTimer;
            DrawTextAt(winX + (25.0f * scaleRatio), winY + winH - (22.0f * scaleRatio), g_FeedbackMsg, 1.05f * scaleRatio, CRGBA(60, 255, 100, 255));
        }
    }
    // 4. LAYAR MENU SENJATA KIT (SCREEN_WEAPONS_MENU)
    else if (bNativeMenuOpen && g_CurrentMenuScreen == SCREEN_WEAPONS_MENU)
    {
        const int TOTAL_WEAPON_ITEMS = 8;
        const char* wepLabels[TOTAL_WEAPON_ITEMS] = {
            "[1] KIT 1: THUG TOOLS (BAT, 9MM, AK47, RPG)",
            "[2] KIT 2: PROFESSIONAL (KNIFE, DEAGLE, M4, SNIPER)",
            "[3] KIT 3: NUTTER TOOLS (CHAINSAW, SPAS, HEAT-SEEK)",
            "[4] KIT 4: SPECIAL ARSENAL (MINIGUN & NV GOGGLES)",
            "[5] MAX WEAPON SKILLS: HITMAN LEVEL SEMUA",
            "[6] SENJATA BERAT: MINIGUN (9999 PELURU)",
            "[7] MELEE & GEAR: KATANA + PARACHUTE",
            "[< KEMBALI KE MENU UTAMA]"
        };

        CRGBA wepBgs[TOTAL_WEAPON_ITEMS] = {
            CRGBA(25, 45, 65, 235),
            CRGBA(22, 55, 45, 235),
            CRGBA(50, 35, 60, 235),
            CRGBA(65, 45, 20, 235),
            CRGBA(20, 60, 35, 235),
            CRGBA(70, 25, 25, 235),
            CRGBA(35, 40, 55, 235),
            CRGBA(55, 22, 22, 235)
        };
        CRGBA wepBorders[TOTAL_WEAPON_ITEMS] = {
            CRGBA(100, 180, 255, 230),
            CRGBA(60, 225, 140, 230),
            CRGBA(200, 140, 255, 230),
            CRGBA(255, 190, 50, 230),
            CRGBA(80, 255, 120, 230),
            CRGBA(255, 80, 80, 230),
            CRGBA(140, 180, 240, 230),
            CRGBA(240, 100, 100, 230)
        };
        CRGBA wepTextColors[TOTAL_WEAPON_ITEMS] = {
            CRGBA(200, 230, 255, 255),
            CRGBA(180, 255, 200, 255),
            CRGBA(240, 200, 255, 255),
            CRGBA(255, 235, 140, 255),
            CRGBA(140, 255, 160, 255),
            CRGBA(255, 180, 180, 255),
            CRGBA(210, 230, 255, 255),
            CRGBA(255, 140, 140, 255)
        };

        float menuW = 640.0f * scaleRatio;
        float itemH = 48.0f * scaleRatio;
        float itemGap = 8.0f * scaleRatio;
        float headerH = 50.0f * scaleRatio;
        float subH = 36.0f * scaleRatio;
        float menuH = headerH + subH + (TOTAL_WEAPON_ITEMS * (itemH + itemGap)) + (35.0f * scaleRatio) + (g_FeedbackTimer > 0 ? 30.0f * scaleRatio : 0.0f);

        float menuX = (g_ScreenWidth - menuW) * 0.5f;
        float menuY = (g_ScreenHeight - menuH) * 0.5f;

        // Background dan Border Jendela
        DrawBorderedBox(menuX, menuY, menuX + menuW, menuY + menuH, CRGBA(12, 16, 24, 248), CRGBA(255, 180, 0, 255), 3.0f * scaleRatio);

        // Header Title Bar
        DrawFilledBox(menuX, menuY, menuX + menuW, menuY + headerH, CRGBA(20, 45, 80, 255));
        DrawTextAt(menuX + (25.0f * scaleRatio), menuY + (12.0f * scaleRatio), "--- MENU CHEAT SENJATA (IDA PRO) ---", 1.20f * scaleRatio, CRGBA(255, 255, 255, 255));

        // Subtitle Info Memory
        DrawTextAt(menuX + (25.0f * scaleRatio), menuY + headerH + (7.0f * scaleRatio), "IDA ARM64: 0x3C1248 | 0x3C1508 | 0x3C178C | 0x59525C", 1.00f * scaleRatio, CRGBA(120, 215, 255, 255));

        // Pemisah garis
        DrawFilledBox(menuX + (12.0f * scaleRatio), menuY + headerH + subH, menuX + menuW - (12.0f * scaleRatio), menuY + headerH + subH + (2.0f * scaleRatio), CRGBA(255, 180, 0, 180));

        float startY = menuY + headerH + subH + (10.0f * scaleRatio);
        float itemLeft = menuX + (20.0f * scaleRatio);
        float itemRight = menuX + menuW - (20.0f * scaleRatio);

        for (int i = 0; i < TOTAL_WEAPON_ITEMS; ++i)
        {
            float rowTop = startY + i * (itemH + itemGap);
            float rowBottom = rowTop + itemH;

            bool isPressed = (g_PressedItem == 200 + i);
            CRGBA bg = isPressed ? CRGBA(60, 150, 220, 255) : wepBgs[i];
            CRGBA border = isPressed ? CRGBA(255, 255, 255, 255) : wepBorders[i];

            DrawBorderedBox(itemLeft, rowTop, itemRight, rowBottom, bg, border, 2.0f * scaleRatio);
            DrawTextAt(itemLeft + (16.0f * scaleRatio), rowTop + (12.0f * scaleRatio), wepLabels[i], 1.10f * scaleRatio, wepTextColors[i]);
        }

        // Pesan Feedback
        if (g_FeedbackTimer > 0 && g_FeedbackMsg[0] != '\0')
        {
            --g_FeedbackTimer;
            DrawTextAt(menuX + (25.0f * scaleRatio), menuY + menuH - (22.0f * scaleRatio), g_FeedbackMsg, 1.05f * scaleRatio, CRGBA(60, 255, 100, 255));
        }
    }
}

// -------------------------------------------------------------
// Touch Input Handling (Sentuhan Menu & Gesture Swipe)
// -------------------------------------------------------------
bool ProcessNativeMenuTouch(int actionType, int trackNum, float x, float y)
{
    // Hanya tangani sentuhan jari pertama (finger 0)
    if (trackNum != 0) return bNativeMenuOpen;

    float scaleRatio = g_ScreenHeight / 1080.0f;
    if (scaleRatio < 0.65f) scaleRatio = 0.65f;
    if (scaleRatio > 1.35f) scaleRatio = 1.35f;

    // 1. Sentuhan pada Floating Toggle Button (DEFAULT DI TOP CENTER LAYAR)
    float btnWidth  = 280.0f * scaleRatio;
    float btnHeight = 54.0f  * scaleRatio;
    float btnLeft   = (g_ScreenWidth - btnWidth) * 0.5f; // Posisi TOP CENTER!
    float btnTop    = 15.0f  * scaleRatio;
    float btnRight  = btnLeft + btnWidth;
    float btnBottom = btnTop + btnHeight;

    if (x >= btnLeft && x <= btnRight && y >= btnTop && y <= btnBottom)
    {
        if (actionType == 1) // TOUCH_RELEASE
        {
            bNativeMenuOpen = !bNativeMenuOpen;
            g_PressedItem = -1;
            g_FeedbackTimer = 0;
            if (!bNativeMenuOpen)
            {
                g_CurrentMenuScreen = SCREEN_MAIN_MENU;
            }
        }
        return true; // Sentuhan diserap, game tidak merespon
    }

    // 2. Sentuhan saat Jendela Menu Terbuka
    if (bNativeMenuOpen)
    {
        if (g_CurrentMenuScreen == SCREEN_MAIN_MENU)
        {
            int totalPages = (TOTAL_MENU_ITEMS + MAX_ITEMS_PER_PAGE - 1) / MAX_ITEMS_PER_PAGE;
            if (totalPages < 1) totalPages = 1;
            if (g_MenuPage >= totalPages) g_MenuPage = totalPages - 1;
            if (g_MenuPage < 0) g_MenuPage = 0;

            int startIndex = g_MenuPage * MAX_ITEMS_PER_PAGE;
            int itemsOnPage = TOTAL_MENU_ITEMS - startIndex;
            if (itemsOnPage > MAX_ITEMS_PER_PAGE) itemsOnPage = MAX_ITEMS_PER_PAGE;

            bool hasPagination = (TOTAL_MENU_ITEMS > MAX_ITEMS_PER_PAGE);

            float menuW = 600.0f * scaleRatio;
            float itemH = 52.0f * scaleRatio;
            float itemGap = 10.0f * scaleRatio;
            float headerH = 50.0f * scaleRatio;
            float subH = 36.0f * scaleRatio;
            float bottomPadding = (hasPagination ? 75.0f : 35.0f) * scaleRatio;
            float menuH = headerH + subH + (itemsOnPage * (itemH + itemGap)) + bottomPadding + (g_FeedbackTimer > 0 ? 30.0f * scaleRatio : 0.0f);

            float menuX = (g_ScreenWidth - menuW) * 0.5f;
            float menuY = (g_ScreenHeight - menuH) * 0.5f;

            bool insideWindow = (x >= menuX && x <= menuX + menuW && y >= menuY && y <= menuY + menuH);

            if (insideWindow)
            {
                float startY = menuY + headerH + subH + (12.0f * scaleRatio);
                float itemLeft = menuX + (20.0f * scaleRatio);
                float itemRight = menuX + menuW - (20.0f * scaleRatio);

                int touchedRow = -1;

                for (int i = 0; i < itemsOnPage; ++i)
                {
                    float rowTop = startY + i * (itemH + itemGap);
                    float rowBottom = rowTop + itemH;
                    if (x >= itemLeft && x <= itemRight && y >= rowTop && y <= rowBottom)
                    {
                        touchedRow = startIndex + i;
                        break;
                    }
                }

                if (hasPagination)
                {
                    float pagY = startY + itemsOnPage * (itemH + itemGap) + (6.0f * scaleRatio);
                    float pagBtnW = 140.0f * scaleRatio;
                    float pagBtnH = 42.0f * scaleRatio;

                    if (y >= pagY && y <= pagY + pagBtnH)
                    {
                        if (x >= itemLeft && x <= itemLeft + pagBtnW)
                        {
                            touchedRow = 90; // PREV
                        }
                        else if (x >= itemRight - pagBtnW && x <= itemRight)
                        {
                            touchedRow = 91; // NEXT
                        }
                    }
                }

                if (actionType == 2) // TOUCH_PUSH
                {
                    g_PressedItem = touchedRow;
                }
                else if (actionType == 1) // TOUCH_RELEASE
                {
                    if (g_PressedItem >= 0 && g_PressedItem == touchedRow)
                    {
                        if (g_PressedItem == 0) // [1] MENU CHEAT SENJATA
                        {
                            g_CurrentMenuScreen = SCREEN_WEAPONS_MENU;
                            g_FeedbackTimer = 0;
                        }
                        else if (g_PressedItem == 1) // [2] SET UANG
                        {
                            g_CurrentMenuScreen = SCREEN_MANUAL_SET_MONEY;
                            int32_t cMoney = GetCurrentPlayerMoney();
                            g_ManualInputMoney = (cMoney > 0) ? (int64_t)cMoney : (int64_t)targetMoney;
                            g_FeedbackTimer = 0;
                        }
                        else if (g_PressedItem == 2) // [3] CHEAT RESMI
                        {
                            QueueOfficialCheat();
                        }
                        else if (g_PressedItem == 3) // [4] AIM ASSIST HEAD
                        {
                            bAimAssistHead = !bAimAssistHead;
                            SaveMoneyConfig();
                            SetFeedback(bAimAssistHead ? ">> Aim Assist Head: DIAKTIFKAN!" : ">> Aim Assist Head: DINONAKTIFKAN!");
                            logger->Info("Aim Assist Head diubah: %d", bAimAssistHead);
                        }
                        else if (g_PressedItem == 4) // [5] CHEAT JETPACK
                        {
                            QueueJetpackCheat();
                        }
                        else if (g_PressedItem == 5) // [6] GOD MODE
                        {
                            QueueGodModeCheat();
                        }
                        else if (g_PressedItem == 6) // [X] TUTUP MENU
                        {
                            bNativeMenuOpen = false;
                            g_CurrentMenuScreen = SCREEN_MAIN_MENU;
                        }
                        else if (g_PressedItem == 90) // PREV PAGE
                        {
                            if (g_MenuPage > 0) --g_MenuPage;
                        }
                        else if (g_PressedItem == 91) // NEXT PAGE
                        {
                            if (g_MenuPage < totalPages - 1) ++g_MenuPage;
                        }
                    }
                    g_PressedItem = -1;
                }

                return true; // Sentuhan di dalam menu diserap penuh
            }
            else
            {
                // Sentuhan di luar jendela menu saat menu terbuka -> tutup menu
                if (actionType == 1) // TOUCH_RELEASE
                {
                    bNativeMenuOpen = false;
                    g_PressedItem = -1;
                    g_CurrentMenuScreen = SCREEN_MAIN_MENU;
                }
                return true;
            }
        }
        else if (g_CurrentMenuScreen == SCREEN_MANUAL_SET_MONEY)
        {
            float winW = 600.0f * scaleRatio;
            float winH = 540.0f * scaleRatio;
            float winX = (g_ScreenWidth - winW) * 0.5f;
            float winY = (g_ScreenHeight - winH) * 0.5f;

            bool insideWindow = (x >= winX && x <= winX + winW && y >= winY && y <= winY + winH);

            if (insideWindow)
            {
                float dispLeft = winX + (20.0f * scaleRatio);
                float dispRight = winX + winW - (20.0f * scaleRatio);
                float dispTop = winY + (58.0f * scaleRatio);
                float dispH = 55.0f * scaleRatio;

                float quickTop = dispTop + dispH + (10.0f * scaleRatio);
                float quickH = 42.0f * scaleRatio;
                float totalQuickW = dispRight - dispLeft;
                float quickGap = 6.0f * scaleRatio;
                float qBtnW = (totalQuickW - 4.0f * quickGap) / 5.0f;
                int qIds[5] = { 100, 101, 102, 103, 104 };

                float gridTop = quickTop + quickH + (10.0f * scaleRatio);
                float keyH = 46.0f * scaleRatio;
                float rowGap = 7.0f * scaleRatio;
                float colGap = 8.0f * scaleRatio;
                float keyW = (totalQuickW - 2.0f * colGap) / 3.0f;
                int keyIds[4][3] = {
                    { 1, 2, 3 },
                    { 4, 5, 6 },
                    { 7, 8, 9 },
                    { 20, 0, 21 }
                };

                float actTop = gridTop + 4 * (keyH + rowGap) + (6.0f * scaleRatio);
                float actH = 50.0f * scaleRatio;
                float backW = 180.0f * scaleRatio;
                float applyLeft = dispLeft + backW + (10.0f * scaleRatio);

                int touchedKey = -1;

                // Cek Quick Preset Buttons
                if (y >= quickTop && y <= quickTop + quickH)
                {
                    for (int q = 0; q < 5; ++q)
                    {
                        float qLeft = dispLeft + q * (qBtnW + quickGap);
                        float qRight = qLeft + qBtnW;
                        if (x >= qLeft && x <= qRight)
                        {
                            touchedKey = qIds[q];
                            break;
                        }
                    }
                }

                // Cek Keypad Grid
                if (touchedKey == -1)
                {
                    for (int r = 0; r < 4; ++r)
                    {
                        float rTop = gridTop + r * (keyH + rowGap);
                        float rBottom = rTop + keyH;
                        if (y >= rTop && y <= rBottom)
                        {
                            for (int c = 0; c < 3; ++c)
                            {
                                float kLeft = dispLeft + c * (keyW + colGap);
                                float kRight = kLeft + keyW;
                                if (x >= kLeft && x <= kRight)
                                {
                                    touchedKey = keyIds[r][c];
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }

                // Cek Action Buttons (Kembali & Apply)
                if (touchedKey == -1 && y >= actTop && y <= actTop + actH)
                {
                    if (x >= dispLeft && x <= dispLeft + backW)
                    {
                        touchedKey = 30; // KEMBALI
                    }
                    else if (x >= applyLeft && x <= dispRight)
                    {
                        touchedKey = 31; // APPLY
                    }
                }

                if (actionType == 2) // TOUCH_PUSH
                {
                    g_PressedItem = touchedKey;
                }
                else if (actionType == 1) // TOUCH_RELEASE
                {
                    if (g_PressedItem >= 0 && g_PressedItem == touchedKey)
                    {
                        if (touchedKey >= 0 && touchedKey <= 9)
                        {
                            if (g_ManualInputMoney == 0)
                            {
                                g_ManualInputMoney = touchedKey;
                            }
                            else if (g_ManualInputMoney * 10 + touchedKey <= 999999999)
                            {
                                g_ManualInputMoney = g_ManualInputMoney * 10 + touchedKey;
                            }
                        }
                        else if (touchedKey == 20) // 000
                        {
                            if (g_ManualInputMoney > 0)
                            {
                                if (g_ManualInputMoney <= 999999)
                                    g_ManualInputMoney *= 1000;
                                else
                                    g_ManualInputMoney = 999999999;
                            }
                        }
                        else if (touchedKey == 21) // DEL
                        {
                            g_ManualInputMoney /= 10;
                        }
                        else if (touchedKey == 100) // +100K
                        {
                            g_ManualInputMoney = std::min<int64_t>(g_ManualInputMoney + 100000, 999999999);
                        }
                        else if (touchedKey == 101) // +1M
                        {
                            g_ManualInputMoney = std::min<int64_t>(g_ManualInputMoney + 1000000, 999999999);
                        }
                        else if (touchedKey == 102) // +10M
                        {
                            g_ManualInputMoney = std::min<int64_t>(g_ManualInputMoney + 10000000, 999999999);
                        }
                        else if (touchedKey == 103) // MAX
                        {
                            g_ManualInputMoney = 999999999;
                        }
                        else if (touchedKey == 104) // CLEAR
                        {
                            g_ManualInputMoney = 0;
                        }
                        else if (touchedKey == 30) // KEMBALI
                        {
                            g_CurrentMenuScreen = SCREEN_MAIN_MENU;
                            g_FeedbackTimer = 0;
                        }
                        else if (touchedKey == 31) // APPLY / TERAPKAN
                        {
                            targetMoney = (int32_t)g_ManualInputMoney;
                            bDynamicInfiniteMoney = true;
                            if (IsPlayerInGame()) SetPlayerMoneyDirect(targetMoney);
                            SaveMoneyConfig();

                            char fb[80];
                            snprintf(fb, sizeof(fb), ">> Berhasil: Uang di-set ke $%d!", targetMoney);
                            SetFeedback(fb);
                            logger->Info("Uang berhasil diset manual dan disimpan: %d", targetMoney);
                        }
                    }
                    g_PressedItem = -1;
                }

                return true;
            }
            else
            {
                if (actionType == 1) // TOUCH_RELEASE
                {
                    bNativeMenuOpen = false;
                    g_PressedItem = -1;
                    g_CurrentMenuScreen = SCREEN_MAIN_MENU;
                }
                return true;
            }
        }
        else if (g_CurrentMenuScreen == SCREEN_WEAPONS_MENU)
        {
            const int TOTAL_WEAPON_ITEMS = 8;
            float menuW = 640.0f * scaleRatio;
            float itemH = 48.0f * scaleRatio;
            float itemGap = 8.0f * scaleRatio;
            float headerH = 50.0f * scaleRatio;
            float subH = 36.0f * scaleRatio;
            float menuH = headerH + subH + (TOTAL_WEAPON_ITEMS * (itemH + itemGap)) + (35.0f * scaleRatio) + (g_FeedbackTimer > 0 ? 30.0f * scaleRatio : 0.0f);

            float menuX = (g_ScreenWidth - menuW) * 0.5f;
            float menuY = (g_ScreenHeight - menuH) * 0.5f;

            bool insideWindow = (x >= menuX && x <= menuX + menuW && y >= menuY && y <= menuY + menuH);

            if (insideWindow)
            {
                float startY = menuY + headerH + subH + (10.0f * scaleRatio);
                float itemLeft = menuX + (20.0f * scaleRatio);
                float itemRight = menuX + menuW - (20.0f * scaleRatio);

                int touchedWep = -1;

                for (int i = 0; i < TOTAL_WEAPON_ITEMS; ++i)
                {
                    float rowTop = startY + i * (itemH + itemGap);
                    float rowBottom = rowTop + itemH;
                    if (x >= itemLeft && x <= itemRight && y >= rowTop && y <= rowBottom)
                    {
                        touchedWep = 200 + i;
                        break;
                    }
                }

                if (actionType == 2) // TOUCH_PUSH
                {
                    g_PressedItem = touchedWep;
                }
                else if (actionType == 1) // TOUCH_RELEASE
                {
                    if (g_PressedItem >= 200 && g_PressedItem == touchedWep)
                    {
                        int wepIndex = g_PressedItem - 200;
                        if (wepIndex >= 0 && wepIndex <= 3) // Kit 1 - 4
                        {
                            QueueWeaponCheat(wepIndex + 1);
                        }
                        else if (wepIndex == 4) // Max Weapon Skills
                        {
                            QueueWeaponCheat(5);
                        }
                        else if (wepIndex == 5) // Minigun 9999
                        {
                            QueueWeaponCheat(6);
                        }
                        else if (wepIndex == 6) // Katana + Parachute
                        {
                            QueueWeaponCheat(7);
                        }
                        else if (wepIndex == 7) // Kembali ke Menu Utama
                        {
                            g_CurrentMenuScreen = SCREEN_MAIN_MENU;
                            g_FeedbackTimer = 0;
                        }
                    }
                    g_PressedItem = -1;
                }
                return true;
            }
            else
            {
                if (actionType == 1) // TOUCH_RELEASE
                {
                    bNativeMenuOpen = false;
                    g_PressedItem = -1;
                    g_CurrentMenuScreen = SCREEN_MAIN_MENU;
                }
                return true;
            }
        }
    }

    // 3. Gesture Geser Turun (CLEO Swipe Down: dari TOP CENTER layar ke bawah)
    if (!bNativeMenuOpen)
    {
        if (actionType == 2) // TOUCH_PUSH
        {
            if (y < g_ScreenHeight * 0.35f && x > g_ScreenWidth * 0.30f && x < g_ScreenWidth * 0.70f)
            {
                g_SwipeActive = true;
                g_SwipeStartX = x;
                g_SwipeStartY = y;
            }
        }
        else if (actionType == 3 && g_SwipeActive) // TOUCH_MOVE
        {
            float deltaY = y - g_SwipeStartY;
            float deltaX = x - g_SwipeStartX;
            if (deltaY > (130.0f * scaleRatio) && (deltaX < 120.0f * scaleRatio && deltaX > -120.0f * scaleRatio))
            {
                bNativeMenuOpen = true;
                g_CurrentMenuScreen = SCREEN_MAIN_MENU;
                g_SwipeActive = false;
                g_FeedbackTimer = 0;
                return true;
            }
        }
        else if (actionType == 1) // TOUCH_RELEASE
        {
            g_SwipeActive = false;
        }
    }

    return false; // Teruskan sentuhan ke game
}

// -------------------------------------------------------------
// Hooked Game HUD & Touch Functions
// -------------------------------------------------------------
void Hooked_CHud_DrawAfterFade()
{
    if (CHud_DrawAfterFade) CHud_DrawAfterFade();
    if (IsPlayerInGame())
    {
        DrawNativeFloatingMenu();
    }
}

void Hooked_AND_TouchEvent(int actionType, int trackNum, int x, int y)
{
    if (!IsPlayerInGame())
    {
        if (AND_TouchEvent) AND_TouchEvent(actionType, trackNum, x, y);
        return;
    }

    // Catat resolusi dari sentuhan jika lebih tinggi
    if ((float)x > g_ScreenWidth) g_ScreenWidth = (float)x;
    if ((float)y > g_ScreenHeight) g_ScreenHeight = (float)y;

    if (ProcessNativeMenuTouch(actionType, trackNum, (float)x, (float)y))
    {
        return; // Sentuhan ditangani oleh Floating Mod Menu
    }

    if (AND_TouchEvent) AND_TouchEvent(actionType, trackNum, x, y);
}

// -------------------------------------------------------------
// Background Dynamic Money Logic
// -------------------------------------------------------------
void ApplyMoneyToPlayer(uintptr_t localPlayer)
{
    int32_t* pMoney = (int32_t*)(localPlayer + OFFSET_MONEY);
    int32_t* pDisplayMoney = (int32_t*)(localPlayer + OFFSET_DISPLAY_MONEY);

    if (bForceExactMoney)
    {
        if (*pMoney != targetMoney)
        {
            *pMoney = targetMoney;
            *pDisplayMoney = targetMoney;
            if (!bHasLoggedThisSession)
            {
                logger->Info("Uang diset ke nominal tetap: %d", targetMoney);
                bHasLoggedThisSession = true;
            }
        }
    }
    else if (bDynamicInfiniteMoney)
    {
        if (*pMoney < targetMoney)
        {
            *pMoney = targetMoney;
            *pDisplayMoney = targetMoney;
            if (!bHasLoggedThisSession)
            {
                logger->Info("Uang player berhasil diperbarui dinamis ke: %d", targetMoney);
                bHasLoggedThisSession = true;
            }
        }
    }
}

// -------------------------------------------------------------
// ImGui Integration (Bonus: jika player memasang mod AML_ImGui)
// -------------------------------------------------------------
void RenderImGuiMenuContent()
{
    if (!pImGui) return;

    bool inGame = IsPlayerInGame();
    int32_t currentMoney = inGame ? GetCurrentPlayerMoney() : 0;

    if (inGame)
    {
        pImGui->TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Status: Player Aktif di Gameplay");
        pImGui->Text("Uang Carl Johnson: $%d", currentMoney);
    }
    else
    {
        pImGui->TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Status: Di Menu / Loading");
        pImGui->TextDisabled("Uang Carl Johnson: (Menunggu Spawn)");
    }
    pImGui->Separator();

    pImGui->Text("Mode Cheat:");
    pImGui->Checkbox("Dynamic Infinite Money (Pertahankan minimal)", &bDynamicInfiniteMoney);
    pImGui->Checkbox("Force Exact Money (Paksa selalu tepat target)", &bForceExactMoney);

    pImGui->Spacing();
    pImGui->Text("Nominal Target Uang:");
    pImGui->InputInt("Target ($)", &targetMoney, 100000, 1000000);
    if (targetMoney < 0) targetMoney = 0;

    pImGui->Separator();
    pImGui->Text("Tindakan Cepat (Quick Actions):");

    if (pImGui->Button("+$100.000", ImVec2(pImGui->GetScaledX(120), 0)))
    {
        if (inGame)
        {
            int32_t newMoney = currentMoney + 100000;
            SetPlayerMoneyDirect(newMoney);
            if (targetMoney < newMoney) targetMoney = newMoney;
        }
    }
    pImGui->SameLine();
    if (pImGui->Button("+$1.000.000", ImVec2(pImGui->GetScaledX(120), 0)))
    {
        if (inGame)
        {
            int32_t newMoney = currentMoney + 1000000;
            SetPlayerMoneyDirect(newMoney);
            if (targetMoney < newMoney) targetMoney = newMoney;
        }
    }

    if (pImGui->Button("Set $2.000.000 (Default)", ImVec2(pImGui->GetScaledX(150), 0)))
    {
        targetMoney = 2000000;
        if (inGame) SetPlayerMoneyDirect(targetMoney);
    }
    pImGui->SameLine();
    if (pImGui->Button("Set $999.999.999 (Max)", ImVec2(pImGui->GetScaledX(150), 0)))
    {
        targetMoney = 999999999;
        if (inGame) SetPlayerMoneyDirect(targetMoney);
    }

    if (pImGui->Button("Reset ke $350 (Normal)", ImVec2(pImGui->GetScaledX(150), 0)))
    {
        targetMoney = 350;
        bDynamicInfiniteMoney = false;
        bForceExactMoney = false;
        if (inGame) SetPlayerMoneyDirect(350);
    }
    pImGui->SameLine();
    if (pImGui->Button("Kuras Uang ($0)", ImVec2(pImGui->GetScaledX(150), 0)))
    {
        targetMoney = 0;
        bDynamicInfiniteMoney = false;
        bForceExactMoney = false;
        if (inGame) SetPlayerMoneyDirect(0);
    }

    pImGui->Separator();
    pImGui->TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Cheat Senjata Kit (IDA Pro):");

    if (pImGui->Button("Kit 1: Thug Tools (0x3C1248)", ImVec2(pImGui->GetScaledX(240), 0)))
    {
        QueueWeaponCheat(1);
    }
    pImGui->SameLine();
    if (pImGui->Button("Kit 2: Professional (0x3C1508)", ImVec2(pImGui->GetScaledX(240), 0)))
    {
        QueueWeaponCheat(2);
    }

    if (pImGui->Button("Kit 3: Nutter Tools (0x3C178C)", ImVec2(pImGui->GetScaledX(240), 0)))
    {
        QueueWeaponCheat(3);
    }
    pImGui->SameLine();
    if (pImGui->Button("Kit 4: Special Arsenal (0x3C199C)", ImVec2(pImGui->GetScaledX(240), 0)))
    {
        QueueWeaponCheat(4);
    }

    if (pImGui->Button("Max Weapon Skills (0x3C2E90)", ImVec2(pImGui->GetScaledX(240), 0)))
    {
        QueueWeaponCheat(5);
    }
    pImGui->SameLine();
    if (pImGui->Button("Minigun 9999 Peluru (0x59525C)", ImVec2(pImGui->GetScaledX(240), 0)))
    {
        QueueWeaponCheat(6);
    }

    if (pImGui->Button("Katana + Parachute", ImVec2(pImGui->GetScaledX(240), 0)))
    {
        QueueWeaponCheat(7);
    }
    pImGui->SameLine();
    if (pImGui->Button("Spawn Jetpack (0x3C2A40)", ImVec2(pImGui->GetScaledX(240), 0)))
    {
        QueueJetpackCheat();
    }

    if (pImGui->Checkbox("God Mode / Invincible (0x3C1AB0)", &bGodModeActive))
    {
        QueueGodModeCheat();
    }

    pImGui->Separator();
    if (pImGui->Button("Simpan Pengaturan (.ini)", ImVec2(pImGui->GetScaledX(180), 0)))
    {
        SaveMoneyConfig();
    }

    if (bConfigSavedNotice)
    {
        pImGui->SameLine();
        pImGui->TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Tersimpan!");
    }
}

void RenderImGuiOverlay()
{
    if (!pImGui) return;

    if (bShowMoneyMenu)
    {
        pImGui->SetNextWindowSize(ImVec2(pImGui->GetScaledX(380.0f), pImGui->GetScaledY(360.0f)), ImGuiCond_FirstUseEver);
        if (pImGui->Begin("GTA San Andreas - Cheat Uang Interaktif", &bShowMoneyMenu, ImGuiWindowFlags_AlwaysAutoResize))
        {
            RenderImGuiMenuContent();
            pImGui->End();
        }
    }
}

void InitImGuiInterface()
{
    if (bImGuiInitialized) return;

    pImGui = (IImGui*)GetInterface("ImGui");
    if (pImGui != nullptr)
    {
        pImGui->AddRenderListener((void*)RenderImGuiOverlay);
        pImGui->AddMenuRenderListener((void*)RenderImGuiMenuContent);
        bImGuiInitialized = true;
        logger->Info("AML_ImGui interface terhubung!");
    }
}

// -------------------------------------------------------------
// Main Game Process & Lifecycle
// -------------------------------------------------------------
void Hooked_CGame_Process()
{
    CGame_Process();

    // Eksekusi antrean cheat di Main Game Thread secara aman (menghindari crash SIGSEGV pada GraphicsThread)
    int pendingKit = g_QueuedWeaponCheat.exchange(0);
    if (pendingKit > 0)
    {
        ExecuteWeaponCheat(pendingKit);
    }
    if (g_QueuedOfficialCheat.exchange(false))
    {
        ExecuteOfficialCheat();
    }
    if (g_QueuedJetpackCheat.exchange(false))
    {
        ExecuteJetpackCheat();
    }
    if (g_QueuedGodModeCheat.exchange(false))
    {
        ExecuteGodModeCheat();
    }

    if (!bImGuiInitialized)
    {
        InitImGuiInterface();
    }

    if (!pPlayersArray) return;

    uintptr_t localPlayer = GetLocalPlayerPtr();
    if (!localPlayer) return;

    uintptr_t playerPed = *(uintptr_t*)(localPlayer + OFFSET_PED);
    if (FindPlayerPed != nullptr && FindPlayerPed(-1) == nullptr)
    {
        playerPed = 0;
    }

    if (playerPed != 0)
    {
        ApplyMoneyToPlayer(localPlayer);
    }
    else
    {
        bHasLoggedThisSession = false;
    }
}

void Hooked_CGame_InitialiseWhenRestarting()
{
    logger->Info("CGame::InitialiseWhenRestarting terpanggil (New Game / Load Game terdeteksi).");
    bHasLoggedThisSession = false;
    bConfigSavedNotice = false;
    bNativeMenuOpen = false;
    g_CurrentMenuScreen = SCREEN_MAIN_MENU;
    g_PressedItem = -1;
    CGame_InitialiseWhenRestarting();
}

ON_ALL_MODS_LOAD()
{
    InitImGuiInterface();
}

extern "C" void OnModLoad()
{
    logger->SetTag("GTA_InfiniteMoney");

    pGTASA = aml->GetLib("libGTASA.so");
    if (!pGTASA)
    {
        logger->Error("libGTASA.so tidak ditemukan!");
        return;
    }

    // Inisialisasi konfigurasi (.ini) secara aman jika interface AMLConfig tersedia
    if (GetInterface("AMLConfig") != nullptr)
    {
        pConfig = new Config("GTA_InfiniteMoney");
        entryTarget = pConfig->Bind("TargetMoney", targetMoney, "MoneyCheat");
        entryInfinite = pConfig->Bind("DynamicInfiniteMoney", bDynamicInfiniteMoney, "MoneyCheat");
        entryForce = pConfig->Bind("ForceExactMoney", bForceExactMoney, "MoneyCheat");
        entryAimAssist = pConfig->Bind("AimAssistHead", bAimAssistHead, "AimAssist");

        if (entryTarget) targetMoney = entryTarget->GetInt();
        if (entryInfinite) bDynamicInfiniteMoney = entryInfinite->GetBool();
        if (entryForce) bForceExactMoney = entryForce->GetBool();
        if (entryAimAssist) bAimAssistHead = entryAimAssist->GetBool();

        pConfig->Save();
        logger->Info("Konfigurasi dimuat: TargetMoney=%d, Dynamic=%d, Force=%d, AimAssist=%d",
                     targetMoney, bDynamicInfiniteMoney, bForceExactMoney, bAimAssistHead);
    }

    // Resolusi symbol pemain dan layar
    pPlayersArray  = aml->GetSym(pGTASA, "_ZN6CWorld7PlayersE");
    pPlayerInFocus = (uint8_t*)aml->GetSym(pGTASA, "_ZN6CWorld13PlayerInFocusE");
    FindPlayerPed  = (FindPlayerPed_fn)aml->GetSym(pGTASA, "_Z13FindPlayerPedi");
    pRsGlobal      = (RsGlobalType*)aml->GetSym(pGTASA, "RsGlobal");

    // Resolusi cheat resmi CCheat::MoneyArmourHealthCheat (offset IDA: 0x3C1B64)
    CCheat_MoneyArmourHealthCheat = (CCheat_MoneyArmourHealthCheat_fn)aml->GetSym(pGTASA, "_ZN6CCheat22MoneyArmourHealthCheatEv");
    if (CCheat_MoneyArmourHealthCheat)
    {
        logger->Info("Symbol CCheat::MoneyArmourHealthCheat (_ZN6CCheat22MoneyArmourHealthCheatEv) berhasil ditemukan!");
    }
    else
    {
        logger->Error("Symbol CCheat::MoneyArmourHealthCheat tidak ditemukan di libGTASA.so!");
    }

    // Resolusi Weapon Cheats lewat Simbol & Alamat Memori Langsung dari IDA Pro
    // (Berdasarkan hasil analisa libGTASA.so ARM64 64-bit & ARMv7 32-bit di IDA Pro)
    #if defined(AML32) || defined(__arm__) || !defined(__LP64__)
        const uintptr_t OFF_WEAPON1     = 0x2BF994;
        const uintptr_t OFF_WEAPON2     = 0x2BFA08;
        const uintptr_t OFF_WEAPON3     = 0x2BFA7C;
        const uintptr_t OFF_WEAPON4     = 0x2BFAF0;
        const uintptr_t OFF_SKILLS      = 0x2C0BE8;
        const uintptr_t OFF_JETPACK     = 0x2C09C0;
        const uintptr_t OFF_INVINCIBLE  = 0x2BF8E4;
        const uintptr_t OFF_GIVEWEAPON  = 0x43D698;
        const uintptr_t OFF_REQMODEL    = 0x286398; // _ZN10CStreaming12RequestModelEii
        const uintptr_t OFF_LOADMODELS  = 0x287CF0; // _ZN10CStreaming22LoadAllRequestedModelsEb
    #else
        const uintptr_t OFF_WEAPON1     = 0x3C1248; // _ZN6CCheat12WeaponCheat1Ev
        const uintptr_t OFF_WEAPON2     = 0x3C1508; // _ZN6CCheat12WeaponCheat2Ev
        const uintptr_t OFF_WEAPON3     = 0x3C178C; // _ZN6CCheat12WeaponCheat3Ev
        const uintptr_t OFF_WEAPON4     = 0x3C199C; // _ZN6CCheat12WeaponCheat4Ev
        const uintptr_t OFF_SKILLS      = 0x3C2E90; // _ZN6CCheat17WeaponSkillsCheatEv
        const uintptr_t OFF_JETPACK     = 0x3C2A40; // _ZN6CCheat12JetpackCheatEv
        const uintptr_t OFF_INVINCIBLE  = 0x3C1AB0; // _ZN6CCheat25TogglePlayerInvincibilityEv
        const uintptr_t OFF_GIVEWEAPON  = 0x59525C; // _ZN4CPed10GiveWeaponE11eWeaponTypejb
        const uintptr_t OFF_REQMODEL    = 0x3949E0; // _ZN10CStreaming12RequestModelEii
        const uintptr_t OFF_LOADMODELS  = 0x396B28; // _ZN10CStreaming22LoadAllRequestedModelsEb
    #endif

    auto ResolveCheatFunc = [](uintptr_t base, const char* symName, uintptr_t offset) -> uintptr_t {
        uintptr_t fn = aml->GetSym(base, symName);
        if (fn)
        {
            logger->Info("[IDA] Simbol '%s' berhasil ditemukan di alamat: %p", symName, (void*)fn);
            return fn;
        }
        if (offset && base)
        {
            fn = base + offset;
            logger->Info("[IDA] Simbol '%s' memakai alamat offset IDA Pro: %p (base + 0x%lX)",
                         symName, (void*)fn, (unsigned long)offset);
            return fn;
        }
        logger->Error("[IDA] Gagal meresolusi simbol '%s'!", symName);
        return 0;
    };

    CCheat_WeaponCheat1 = (CCheat_WeaponCheat1_fn)ResolveCheatFunc(pGTASA, "_ZN6CCheat12WeaponCheat1Ev", OFF_WEAPON1);
    CCheat_WeaponCheat2 = (CCheat_WeaponCheat2_fn)ResolveCheatFunc(pGTASA, "_ZN6CCheat12WeaponCheat2Ev", OFF_WEAPON2);
    CCheat_WeaponCheat3 = (CCheat_WeaponCheat3_fn)ResolveCheatFunc(pGTASA, "_ZN6CCheat12WeaponCheat3Ev", OFF_WEAPON3);
    CCheat_WeaponCheat4 = (CCheat_WeaponCheat4_fn)ResolveCheatFunc(pGTASA, "_ZN6CCheat12WeaponCheat4Ev", OFF_WEAPON4);
    CCheat_WeaponSkillsCheat = (CCheat_WeaponSkillsCheat_fn)ResolveCheatFunc(pGTASA, "_ZN6CCheat17WeaponSkillsCheatEv", OFF_SKILLS);
    CCheat_JetpackCheat = (CCheat_JetpackCheat_fn)ResolveCheatFunc(pGTASA, "_ZN6CCheat12JetpackCheatEv", OFF_JETPACK);
    CCheat_TogglePlayerInvincibility = (CCheat_TogglePlayerInvincibility_fn)ResolveCheatFunc(pGTASA, "_ZN6CCheat25TogglePlayerInvincibilityEv", OFF_INVINCIBLE);
    CPed_GiveWeapon = (CPed_GiveWeapon_fn)ResolveCheatFunc(pGTASA, "_ZN4CPed10GiveWeaponE11eWeaponTypejb", OFF_GIVEWEAPON);
    CStreaming_RequestModel = (CStreaming_RequestModel_fn)ResolveCheatFunc(pGTASA, "_ZN10CStreaming12RequestModelEii", OFF_REQMODEL);
    CStreaming_LoadAllRequestedModels = (CStreaming_LoadAllRequestedModels_fn)ResolveCheatFunc(pGTASA, "_ZN10CStreaming22LoadAllRequestedModelsEb", OFF_LOADMODELS);

    // Resolusi symbol untuk Aim Assist Head
    IsPedPointerValid = (IsPedPointerValid_fn)aml->GetSym(pGTASA, "_Z17IsPedPointerValidP4CPed");
    CPed_GetBonePosition = (CPed_GetBonePosition_fn)aml->GetSym(pGTASA, "_ZN4CPed15GetBonePositionER5RwV3djb");
    pPedPoolPtr = (CPoolGeneric**)aml->GetSym(pGTASA, "_ZN6CPools10ms_pPedPoolE");

    uintptr_t pUpdateAimingCoors = aml->GetSym(pGTASA, "_ZN7CCamera17UpdateAimingCoorsERK7CVector");
    if (pUpdateAimingCoors)
    {
        aml->Hook((void*)pUpdateAimingCoors, (void*)Hooked_CCamera_UpdateAimingCoors, (void**)&Orig_CCamera_UpdateAimingCoors);
        logger->Info("Hook CCamera::UpdateAimingCoors (_ZN7CCamera17UpdateAimingCoorsERK7CVector) berhasil!");
    }
    else
    {
        logger->Error("Symbol CCamera::UpdateAimingCoors tidak ditemukan di libGTASA.so!");
    }

    if (pRsGlobal && pRsGlobal->maximumWidth > 0)
    {
        g_ScreenWidth  = (float)pRsGlobal->maximumWidth;
        g_ScreenHeight = (float)pRsGlobal->maximumHeight;
    }

    // Resolusi symbol 2D Rendering (CSprite2d & CFont) untuk CLEO Floating Menu
    CSprite2d_DrawRect     = (CSprite2d_DrawRect_fn)aml->GetSym(pGTASA, "_ZN9CSprite2d8DrawRectERK5CRectRK5CRGBA");
    CFont_PrintString      = (CFont_PrintString_fn)aml->GetSym(pGTASA, "_ZN5CFont11PrintStringEffPt");
    CFont_RenderFontBuffer = (CFont_RenderFontBuffer_fn)aml->GetSym(pGTASA, "_ZN5CFont16RenderFontBufferEv");
    CFont_SetColor         = (CFont_SetColor_fn)aml->GetSym(pGTASA, "_ZN5CFont8SetColorE5CRGBA");
    CFont_SetScale         = (CFont_SetScale_fn)aml->GetSym(pGTASA, "_ZN5CFont8SetScaleEf");
    CFont_SetFontStyle     = (CFont_SetFontStyle_fn)aml->GetSym(pGTASA, "_ZN5CFont12SetFontStyleEh");
    CFont_SetOrientation   = (CFont_SetOrientation_fn)aml->GetSym(pGTASA, "_ZN5CFont14SetOrientationEh");
    CFont_SetEdge          = (CFont_SetEdge_fn)aml->GetSym(pGTASA, "_ZN5CFont7SetEdgeEa");
    CFont_SetDropColor     = (CFont_SetDropColor_fn)aml->GetSym(pGTASA, "_ZN5CFont12SetDropColorE5CRGBA");
    CFont_SetProportional  = (CFont_SetProportional_fn)aml->GetSym(pGTASA, "_ZN5CFont15SetProportionalEh");
    CFont_SetWrapx         = (CFont_SetWrapx_fn)aml->GetSym(pGTASA, "_ZN5CFont8SetWrapxEf");

    // Hook HUD Rendering (CHud::DrawAfterFade atau CHud::Draw)
    uintptr_t pHudDraw = aml->GetSym(pGTASA, "_ZN4CHud13DrawAfterFadeEv");
    if (!pHudDraw) pHudDraw = aml->GetSym(pGTASA, "_ZN4CHud4DrawEv");
    if (pHudDraw)
    {
        aml->Hook((void*)pHudDraw, (void*)Hooked_CHud_DrawAfterFade, (void**)&CHud_DrawAfterFade);
        logger->Info("Hook CHud Drawing berhasil!");
    }
    else
    {
        logger->Error("Gagal menemukan symbol CHud Draw");
    }

    // Hook Touch Events (_Z14AND_TouchEventiiii) untuk sentuhan menu & swipe CLEO
    uintptr_t pTouch = aml->GetSym(pGTASA, "_Z14AND_TouchEventiiii");
    if (pTouch)
    {
        aml->Hook((void*)pTouch, (void*)Hooked_AND_TouchEvent, (void**)&AND_TouchEvent);
        logger->Info("Hook AND_TouchEvent berhasil!");
    }
    else
    {
        logger->Error("Gagal menemukan symbol AND_TouchEvent");
    }

    // Hook CGame::Process
    uintptr_t pProcess = aml->GetSym(pGTASA, "_ZN5CGame7ProcessEv");
    if (pProcess)
    {
        aml->Hook((void*)pProcess, (void*)Hooked_CGame_Process, (void**)&CGame_Process);
        logger->Info("Hook CGame::Process berhasil!");
    }

    // Hook CGame::InitialiseWhenRestarting
    uintptr_t pRestart = aml->GetSym(pGTASA, "_ZN5CGame24InitialiseWhenRestartingEv");
    if (pRestart)
    {
        aml->Hook((void*)pRestart, (void*)Hooked_CGame_InitialiseWhenRestarting, (void**)&CGame_InitialiseWhenRestarting);
        logger->Info("Hook CGame::InitialiseWhenRestarting berhasil!");
    }

    // Coba inisialisasi ImGui jika AML_ImGui tersedia
    InitImGuiInterface();
}
