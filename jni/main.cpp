#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include "iimgui.h"

MYMOD(com.example.infinitemoney, Infinite Money Mod, 1.3, YourName)
NEEDGAME(com.rockstargames.gtasa)

uintptr_t pGTASA = 0;

// Structures for native GTA 2D rendering
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
bool bHasLoggedThisSession = false;

// Native Floating CLEO-style Menu State
bool bNativeMenuOpen = false;
int g_PressedItem = -1;
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

// Offset memori CPlayerInfo disesuaikan per arsitektur (32-bit vs 64-bit)
#if defined(AML32) || defined(__arm__) || !defined(__LP64__)
    static constexpr size_t PLAYER_INFO_SIZE     = 0x194;
    static constexpr size_t OFFSET_PED           = 0x0;
    static constexpr size_t OFFSET_MONEY         = 0xB8;
    static constexpr size_t OFFSET_DISPLAY_MONEY = 0xBC;
#else
    static constexpr size_t PLAYER_INFO_SIZE     = 0x1D8;
    static constexpr size_t OFFSET_PED           = 0x0;
    static constexpr size_t OFFSET_MONEY         = 0xF0;
    static constexpr size_t OFFSET_DISPLAY_MONEY = 0xF4;
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
        pConfig->Save();
        logger->Info("Konfigurasi disimpan: Target=%d, Dynamic=%d, Force=%d", targetMoney, bDynamicInfiniteMoney, bForceExactMoney);
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
// Native Floating Mod Menu Render & Layout
// -------------------------------------------------------------
void DrawNativeFloatingMenu()
{
    if (!CSprite2d_DrawRect || !CFont_PrintString) return;

    // HANYA GAMBAR JIKA PLAYER SUDAH DI DALAM GAMEPLAY!
    // Ini mencegah crash saat di layar FrontendIdle / Main Menu / Loading!
    if (!IsPlayerInGame()) return;

    // Perbarui resolusi layar dari RsGlobal jika tersedia
    if (pRsGlobal && pRsGlobal->maximumWidth > 0 && pRsGlobal->maximumHeight > 0)
    {
        g_ScreenWidth = (float)pRsGlobal->maximumWidth;
        g_ScreenHeight = (float)pRsGlobal->maximumHeight;
    }

    bool inGame = IsPlayerInGame();
    int32_t curMoney = inGame ? GetCurrentPlayerMoney() : 0;
    float scaleRatio = g_ScreenHeight / 1080.0f;
    if (scaleRatio < 0.6f) scaleRatio = 0.6f;
    if (scaleRatio > 1.4f) scaleRatio = 1.4f;

    // 1. FLOATING BUTTON ICON (Selalu ada di pojok kiri atas)
    float btnLeft   = 25.0f * scaleRatio;
    float btnTop    = 20.0f * scaleRatio;
    float btnRight  = btnLeft + (250.0f * scaleRatio);
    float btnBottom = btnTop  + (55.0f  * scaleRatio);

    if (!bNativeMenuOpen)
    {
        // Tombol Buka Menu
        DrawBorderedBox(btnLeft, btnTop, btnRight, btnBottom, CRGBA(10, 15, 22, 220), CRGBA(255, 215, 0, 255), 2.5f * scaleRatio);
        DrawTextAt(btnLeft + (15.0f * scaleRatio), btnTop + (12.0f * scaleRatio), "[$] CHEAT UANG", 1.05f * scaleRatio, CRGBA(255, 230, 80, 255));
    }
    else
    {
        // Tombol Tutup Menu
        DrawBorderedBox(btnLeft, btnTop, btnRight, btnBottom, CRGBA(140, 25, 25, 235), CRGBA(255, 255, 255, 255), 2.5f * scaleRatio);
        DrawTextAt(btnLeft + (20.0f * scaleRatio), btnTop + (12.0f * scaleRatio), "[X] TUTUP MENU", 1.05f * scaleRatio, CRGBA(255, 255, 255, 255));
    }

    // 2. CLEO-STYLE FLOATING MOD MENU WINDOW
    if (bNativeMenuOpen)
    {
        float menuW = 540.0f * scaleRatio;
        float menuH = 610.0f * scaleRatio;
        float menuX = (g_ScreenWidth - menuW) * 0.5f;
        float menuY = (g_ScreenHeight - menuH) * 0.5f;

        // Background dan Border Utama Jendela
        DrawBorderedBox(menuX, menuY, menuX + menuW, menuY + menuH, CRGBA(12, 16, 24, 245), CRGBA(255, 215, 0, 255), 3.0f * scaleRatio);

        // Header Title Bar
        DrawFilledBox(menuX, menuY, menuX + menuW, menuY + (50.0f * scaleRatio), CRGBA(25, 85, 45, 255));
        DrawTextAt(menuX + (20.0f * scaleRatio), menuY + (12.0f * scaleRatio), "--- CHEAT UANG (CLEO MENU) ---", 1.05f * scaleRatio, CRGBA(255, 255, 255, 255));

        // Subtitle Status & Uang
        char subBuf[128];
        snprintf(subBuf, sizeof(subBuf), "CJ: $%d | Target: $%d | %s", curMoney, targetMoney, inGame ? "In-Game" : "Menu/Loading");
        DrawTextAt(menuX + (20.0f * scaleRatio), menuY + (58.0f * scaleRatio), subBuf, 0.85f * scaleRatio, inGame ? CRGBA(120, 255, 140, 255) : CRGBA(255, 180, 70, 255));

        // Pemisah garis
        DrawFilledBox(menuX + (10.0f * scaleRatio), menuY + (84.0f * scaleRatio), menuX + menuW - (10.0f * scaleRatio), menuY + (86.0f * scaleRatio), CRGBA(255, 215, 0, 180));

        // Daftar 10 Tombol Aksi Menu
        const char* itemLabels[10] = {
            "[+] TAMBAH $100.000",
            "[+] TAMBAH $1.000.000",
            "[=] SET $2.000.000 (DEFAULT)",
            "[=] SET $999.999.999 (MAX)",
            bDynamicInfiniteMoney ? "[*] DYNAMIC INFINITE: [AKTIF]" : "[*] DYNAMIC INFINITE: [NONAKTIF]",
            bForceExactMoney      ? "[*] FORCE EXACT: [AKTIF]"      : "[*] FORCE EXACT: [NONAKTIF]",
            "[-] RESET NORMAL ($350)",
            "[0] KURAS UANG ($0 / BROKE)",
            "[S] SIMPAN CONFIG (.INI)",
            "[X] TUTUP MENU"
        };

        float startY    = menuY + (95.0f * scaleRatio);
        float itemH     = 42.0f * scaleRatio;
        float itemGap   = 6.0f  * scaleRatio;
        float itemLeft  = menuX + (15.0f * scaleRatio);
        float itemRight = menuX + menuW - (15.0f * scaleRatio);

        for (int i = 0; i < 10; ++i)
        {
            float rowTop = startY + i * (itemH + itemGap);
            float rowBottom = rowTop + itemH;

            CRGBA itemBg     = (g_PressedItem == i) ? CRGBA(50, 160, 70, 255) : CRGBA(30, 36, 48, 220);
            CRGBA itemBorder = (g_PressedItem == i) ? CRGBA(255, 255, 255, 255) : CRGBA(80, 95, 120, 180);
            CRGBA textColor  = (i == 9) ? CRGBA(255, 100, 100, 255) : (i == 4 || i == 5) ? CRGBA(255, 230, 90, 255) : CRGBA(240, 245, 255, 255);

            DrawBorderedBox(itemLeft, rowTop, itemRight, rowBottom, itemBg, itemBorder, 1.5f * scaleRatio);
            DrawTextAt(itemLeft + (15.0f * scaleRatio), rowTop + (10.0f * scaleRatio), itemLabels[i], 0.95f * scaleRatio, textColor);
        }

        // Pesan Notifikasi Feedback di Bagian Bawah
        if (g_FeedbackTimer > 0 && g_FeedbackMsg[0] != '\0')
        {
            --g_FeedbackTimer;
            DrawTextAt(menuX + (20.0f * scaleRatio), menuY + menuH - (25.0f * scaleRatio), g_FeedbackMsg, 0.85f * scaleRatio, CRGBA(60, 255, 100, 255));
        }
    }
}

// -------------------------------------------------------------
// Touch Input Handling (Menangani sentuhan & gesture CLEO swipe)
// -------------------------------------------------------------
bool ProcessNativeMenuTouch(int actionType, int trackNum, float x, float y)
{
    // Hanya tangani sentuhan jari pertama (finger 0)
    if (trackNum != 0) return bNativeMenuOpen;

    float scaleRatio = g_ScreenHeight / 1080.0f;
    if (scaleRatio < 0.6f) scaleRatio = 0.6f;
    if (scaleRatio > 1.4f) scaleRatio = 1.4f;

    float btnLeft   = 25.0f * scaleRatio;
    float btnTop    = 20.0f * scaleRatio;
    float btnRight  = btnLeft + (250.0f * scaleRatio);
    float btnBottom = btnTop  + (55.0f  * scaleRatio);

    // 1. Sentuhan pada Floating Toggle Button
    if (x >= btnLeft && x <= btnRight && y >= btnTop && y <= btnBottom)
    {
        if (actionType == 1) // TOUCH_RELEASE
        {
            bNativeMenuOpen = !bNativeMenuOpen;
            g_PressedItem = -1;
            g_FeedbackTimer = 0;
        }
        return true; // Sentuhan diserap, game tidak merespon
    }

    // 2. Sentuhan saat Jendela Menu Terbuka
    if (bNativeMenuOpen)
    {
        float menuW = 540.0f * scaleRatio;
        float menuH = 610.0f * scaleRatio;
        float menuX = (g_ScreenWidth - menuW) * 0.5f;
        float menuY = (g_ScreenHeight - menuH) * 0.5f;

        bool insideWindow = (x >= menuX && x <= menuX + menuW && y >= menuY && y <= menuY + menuH);

        if (insideWindow)
        {
            float startY    = menuY + (95.0f * scaleRatio);
            float itemH     = 42.0f * scaleRatio;
            float itemGap   = 6.0f  * scaleRatio;
            float itemLeft  = menuX + (15.0f * scaleRatio);
            float itemRight = menuX + menuW - (15.0f * scaleRatio);

            int touchedRow = -1;
            for (int i = 0; i < 10; ++i)
            {
                float rowTop = startY + i * (itemH + itemGap);
                float rowBottom = rowTop + itemH;
                if (x >= itemLeft && x <= itemRight && y >= rowTop && y <= rowBottom)
                {
                    touchedRow = i;
                    break;
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
                    bool inGame = IsPlayerInGame();
                    int32_t cur = inGame ? GetCurrentPlayerMoney() : 0;

                    switch (g_PressedItem)
                    {
                        case 0: // +$100.000
                            if (inGame)
                            {
                                int32_t val = cur + 100000;
                                SetPlayerMoneyDirect(val);
                                if (targetMoney < val) targetMoney = val;
                                SetFeedback(">> Berhasil: Uang bertambah +$100.000!");
                            }
                            else SetFeedback(">> Gagal: Player belum spawn di game!");
                            break;

                        case 1: // +$1.000.000
                            if (inGame)
                            {
                                int32_t val = cur + 1000000;
                                SetPlayerMoneyDirect(val);
                                if (targetMoney < val) targetMoney = val;
                                SetFeedback(">> Berhasil: Uang bertambah +$1.000.000!");
                            }
                            else SetFeedback(">> Gagal: Player belum spawn di game!");
                            break;

                        case 2: // Set $2.000.000
                            targetMoney = 2000000;
                            if (inGame) SetPlayerMoneyDirect(2000000);
                            SetFeedback(">> Berhasil: Uang diset ke $2.000.000!");
                            break;

                        case 3: // Set $999.999.999 (Max)
                            targetMoney = 999999999;
                            if (inGame) SetPlayerMoneyDirect(999999999);
                            SetFeedback(">> Berhasil: Uang diset MAX $999.999.999!");
                            break;

                        case 4: // Toggle Dynamic Infinite
                            bDynamicInfiniteMoney = !bDynamicInfiniteMoney;
                            SetFeedback(bDynamicInfiniteMoney ? ">> Dynamic Infinite: AKTIF" : ">> Dynamic Infinite: NONAKTIF");
                            break;

                        case 5: // Toggle Force Exact
                            bForceExactMoney = !bForceExactMoney;
                            SetFeedback(bForceExactMoney ? ">> Force Exact: AKTIF" : ">> Force Exact: NONAKTIF");
                            break;

                        case 6: // Reset Normal ($350)
                            targetMoney = 350;
                            bDynamicInfiniteMoney = false;
                            bForceExactMoney = false;
                            if (inGame) SetPlayerMoneyDirect(350);
                            SetFeedback(">> Berhasil: Uang di-reset ke $350 (Normal)!");
                            break;

                        case 7: // Kuras Uang ($0)
                            targetMoney = 0;
                            bDynamicInfiniteMoney = false;
                            bForceExactMoney = false;
                            if (inGame) SetPlayerMoneyDirect(0);
                            SetFeedback(">> Uang dikuras habis menjadi $0!");
                            break;

                        case 8: // Simpan Config
                            SaveMoneyConfig();
                            SetFeedback(">> Pengaturan berhasil disimpan ke .ini!");
                            break;

                        case 9: // Tutup Menu
                            bNativeMenuOpen = false;
                            break;
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
            }
            return true;
        }
    }

    // 3. Gesture Geser Turun (CLEO Swipe Down: dari atas tengah ke bawah)
    if (!bNativeMenuOpen)
    {
        if (actionType == 2) // TOUCH_PUSH
        {
            if (y < g_ScreenHeight * 0.35f && x > g_ScreenWidth * 0.25f && x < g_ScreenWidth * 0.75f)
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

        if (entryTarget) targetMoney = entryTarget->GetInt();
        if (entryInfinite) bDynamicInfiniteMoney = entryInfinite->GetBool();
        if (entryForce) bForceExactMoney = entryForce->GetBool();

        pConfig->Save();
        logger->Info("Konfigurasi dimuat: TargetMoney=%d, Dynamic=%d, Force=%d", targetMoney, bDynamicInfiniteMoney, bForceExactMoney);
    }

    // Resolusi symbol pemain dan layar
    pPlayersArray  = aml->GetSym(pGTASA, "_ZN6CWorld7PlayersE");
    pPlayerInFocus = (uint8_t*)aml->GetSym(pGTASA, "_ZN6CWorld13PlayerInFocusE");
    FindPlayerPed  = (FindPlayerPed_fn)aml->GetSym(pGTASA, "_Z13FindPlayerPedi");
    pRsGlobal      = (RsGlobalType*)aml->GetSym(pGTASA, "RsGlobal");

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
