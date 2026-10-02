#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

MYMOD(com.example.infinitemoney, Infinite Money Mod, 1.1, YourName)
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

void Hooked_CGame_Process()
{
    // Jalankan loop asli game terlebih dahulu
    CGame_Process();

    if (!pPlayersArray) return;

    int playerIndex = (pPlayerInFocus != nullptr) ? *pPlayerInFocus : 0;
    if (playerIndex < 0 || playerIndex > 1) playerIndex = 0;

    uintptr_t localPlayer = pPlayersArray + (playerIndex * PLAYER_INFO_SIZE);
    if (!localPlayer) return;

    // Ambil pointer CPlayerPed di offset 0
    uintptr_t playerPed = *(uintptr_t*)(localPlayer + OFFSET_PED);

    // Verifikasi tambahan dengan FindPlayerPed jika tersedia di symbol table
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
    CGame_InitialiseWhenRestarting();
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
        Config* cfg = new Config("GTA_InfiniteMoney");
        ConfigEntry* entryTarget = cfg->Bind("TargetMoney", targetMoney, "MoneyCheat");
        ConfigEntry* entryInfinite = cfg->Bind("DynamicInfiniteMoney", bDynamicInfiniteMoney, "MoneyCheat");
        ConfigEntry* entryForce = cfg->Bind("ForceExactMoney", bForceExactMoney, "MoneyCheat");

        if (entryTarget) targetMoney = entryTarget->GetInt();
        if (entryInfinite) bDynamicInfiniteMoney = entryInfinite->GetBool();
        if (entryForce) bForceExactMoney = entryForce->GetBool();

        cfg->Save();
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
}
