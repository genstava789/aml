#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include "iimgui.h"

MYMOD(com.example.infinitemoney, Infinite Money Mod, 1.2, YourName)
NEEDGAME(com.rockstargames.gtasa)

uintptr_t pGTASA = 0;

// Function pointers
void (*CGame_Process)();
void (*CGame_InitialiseWhenRestarting)();
typedef void* (*FindPlayerPed_fn)(int);
FindPlayerPed_fn FindPlayerPed = nullptr;

// Game symbols
uintptr_t pPlayersArray = 0;
uint8_t* pPlayerInFocus = nullptr;

// Konfigurasi & Target Uang
int32_t targetMoney = 2000000;       // Default: $2.000.000
bool bDynamicInfiniteMoney = true;  // Pertahankan uang minimal sebesar targetMoney
bool bForceExactMoney = false;      // Selalu paksa uang tepat sebesar targetMoney

// Status sesi agar log tidak spam setiap frame
bool bHasLoggedThisSession = false;

// ImGui State
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
    // 32-bit (armeabi-v7a): sizeof(CPlayerInfo) = 0x194
    static constexpr size_t PLAYER_INFO_SIZE     = 0x194;
    static constexpr size_t OFFSET_PED           = 0x0;
    static constexpr size_t OFFSET_MONEY         = 0xB8;
    static constexpr size_t OFFSET_DISPLAY_MONEY = 0xBC;
#else
    // 64-bit (arm64-v8a): sizeof(CPlayerInfo) = 0x1D8
    static constexpr size_t PLAYER_INFO_SIZE     = 0x1D8;
    static constexpr size_t OFFSET_PED           = 0x0;
    static constexpr size_t OFFSET_MONEY         = 0xF0;
    static constexpr size_t OFFSET_DISPLAY_MONEY = 0xF4;
#endif

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
        // Jika uang kurang dari target (misal saat New Game reset ke $350, atau setelah belanja)
        if (*pMoney < targetMoney)
        {
            *pMoney = targetMoney;
            *pDisplayMoney = targetMoney; // Hindari animasi rolling lambat pada HUD
            if (!bHasLoggedThisSession)
            {
                logger->Info("Uang player berhasil diperbarui dinamis ke: %d", targetMoney);
                bHasLoggedThisSession = true;
            }
        }
    }
}

void RenderMoneyMenuContent()
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

void RenderMoneyOverlay()
{
    if (!pImGui) return;

    // 1. Floating Toggle Button di layar
    pImGui->SetNextWindowBgAlpha(0.65f);
    pImGui->SetNextWindowPos(ImVec2(15.0f, 15.0f), ImGuiCond_FirstUseEver);
    if (pImGui->Begin("MoneyCheatToggle", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (pImGui->Button(bShowMoneyMenu ? "Tutup Cheat [$]" : "Cheat Uang [$]"))
        {
            bShowMoneyMenu = !bShowMoneyMenu;
            bConfigSavedNotice = false;
        }
        pImGui->End();
    }

    // 2. Window Menu Interaktif jika sedang dibuka
    if (bShowMoneyMenu)
    {
        pImGui->SetNextWindowSize(ImVec2(pImGui->GetScaledX(380.0f), pImGui->GetScaledY(360.0f)), ImGuiCond_FirstUseEver);
        if (pImGui->Begin("GTA San Andreas - Cheat Uang Interaktif", &bShowMoneyMenu, ImGuiWindowFlags_AlwaysAutoResize))
        {
            RenderMoneyMenuContent();
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
        pImGui->AddRenderListener((void*)RenderMoneyOverlay);
        pImGui->AddMenuRenderListener((void*)RenderMoneyMenuContent);
        bImGuiInitialized = true;
        logger->Info("AML_ImGui interface berhasil terhubung! Menu interaktif siap.");
    }
}

void Hooked_CGame_Process()
{
    // Jalankan loop asli game terlebih dahulu
    CGame_Process();

    // Pastikan ImGui interface dicoba hubungkan jika belum terhubung
    if (!bImGuiInitialized)
    {
        InitImGuiInterface();
    }

    if (!pPlayersArray) return;

    uintptr_t localPlayer = GetLocalPlayerPtr();
    if (!localPlayer) return;

    // Ambil pointer CPlayerPed di offset 0
    uintptr_t playerPed = *(uintptr_t*)(localPlayer + OFFSET_PED);

    // Verifikasi tambahan dengan FindPlayerPed jika tersedia
    if (FindPlayerPed != nullptr && FindPlayerPed(-1) == nullptr)
    {
        playerPed = 0;
    }

    // Hanya terapkan jika player ped sudah aktif di gameplay (bukan saat di menu / loading)
    if (playerPed != 0)
    {
        ApplyMoneyToPlayer(localPlayer);
    }
    else
    {
        // Player belum spawn (masih di menu atau loading) -> reset status logging untuk sesi berikutnya
        bHasLoggedThisSession = false;
    }
}

void Hooked_CGame_InitialiseWhenRestarting()
{
    logger->Info("CGame::InitialiseWhenRestarting terpanggil (New Game / Load Game terdeteksi).");
    bHasLoggedThisSession = false;
    bConfigSavedNotice = false;
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
    else
    {
        logger->Info("AMLConfig tidak tersedia, memakai konfigurasi default: TargetMoney=%d", targetMoney);
    }

    // Ambil symbol array CWorld::Players dan PlayerInFocus
    pPlayersArray = aml->GetSym(pGTASA, "_ZN6CWorld7PlayersE");
    pPlayerInFocus = (uint8_t*)aml->GetSym(pGTASA, "_ZN6CWorld13PlayerInFocusE");

    // Ambil symbol FindPlayerPed jika tersedia
    FindPlayerPed = (FindPlayerPed_fn)aml->GetSym(pGTASA, "_Z13FindPlayerPedi");

    if (!pPlayersArray)
    {
        logger->Error("Gagal menemukan symbol CWorld::Players");
        return;
    }

    // Hook CGame::Process
    uintptr_t pProcess = aml->GetSym(pGTASA, "_ZN5CGame7ProcessEv");
    if (pProcess)
    {
        aml->Hook((void*)pProcess, (void*)Hooked_CGame_Process, (void**)&CGame_Process);
        logger->Info("Hook CGame::Process berhasil!");
    }
    else
    {
        logger->Error("Gagal menemukan CGame::Process");
    }

    // Hook CGame::InitialiseWhenRestarting untuk mendeteksi New Game / Load Game secara instan
    uintptr_t pRestart = aml->GetSym(pGTASA, "_ZN5CGame24InitialiseWhenRestartingEv");
    if (pRestart)
    {
        aml->Hook((void*)pRestart, (void*)Hooked_CGame_InitialiseWhenRestarting, (void**)&CGame_InitialiseWhenRestarting);
        logger->Info("Hook CGame::InitialiseWhenRestarting berhasil!");
    }

    // Coba inisialisasi ImGui jika AML_ImGui sudah dimuat lebih awal
    InitImGuiInterface();
}
