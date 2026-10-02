#include <mod/amlmod.h>
#include <mod/logger.h>

MYMOD(com.example.infinitemoney, Infinite Money Mod, 1.0, YourName)
NEEDGAME(com.rockstargames.gtasa)

uintptr_t pGTASA = 0;
void (*CGame_Process)();

uintptr_t pPlayersArray = 0;
uint8_t* pPlayerInFocus = nullptr;

// Flag agar modifikasi memori hanya dieksekusi 1 kali saat masuk gameplay
bool bMoneyApplied = false;

// Nominal uang yang diinginkan (contoh: 2 Juta Dollar)
const int32_t TARGET_MONEY = 2000000;

void Hooked_CGame_Process()
{
    // Jalankan loop asli game
    CGame_Process();

    // Jalankan hanya jika belum pernah di-set dan data player sudah siap
    if (!bMoneyApplied && pPlayersArray)
    {
        int playerIndex = (pPlayerInFocus != nullptr) ? *pPlayerInFocus : 0;
        uintptr_t localPlayer = pPlayersArray + (playerIndex * 0x1D8);

        // Pastikan memori game sudah diinisialisasi (bukan nullptr / 0)
        if (localPlayer != 0)
        {
            // 1. Tulis uang asli (Actual Money) di offset 0xF0
            *(int32_t*)(localPlayer + 0xF0) = TARGET_MONEY;

            // 2. Tulis uang tampilan HUD di offset 0xF4 agar tidak ada animasi rolling bertahap
            *(int32_t*)(localPlayer + 0xF4) = TARGET_MONEY;

            bMoneyApplied = true; // Tandai sudah selesai, jangan di-loop lagi
            logger->Info("Uang berhasil diset instan ke: %d", TARGET_MONEY);
        }
    }
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

    // Ambil symbol array CWorld::Players dan PlayerInFocus
    pPlayersArray = aml->GetSym(pGTASA, "_ZN6CWorld7PlayersE");
    pPlayerInFocus = (uint8_t*)aml->GetSym(pGTASA, "_ZN6CWorld13PlayerInFocusE");

    // Hook game process
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
}
