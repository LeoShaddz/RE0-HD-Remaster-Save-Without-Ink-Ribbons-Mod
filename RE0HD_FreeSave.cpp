// RE0HD_FreeSave v4 - Resident Evil 0 HD (re0hd.exe, Steam, 32 bits)
// NAO COMPILADO nem TESTADO (nao consigo rodar o jogo aqui).
//
// Salvar na maquina de escrever SEM ink ribbon.
// Base: dois logs seus (com e sem fita) mostraram que o jogo, ao usar a maquina, procura a fita no inventario
// (funcao 0x4DB130 -> retorna -1 se nao tem) e, se nao acha, mostra a mensagem 0xD e NAO entra na fase de save (6).
//   PATCH 1 (0x57AD3B): o "je" que desvia para o caminho "sem fita" vira NOP -> o jogo sempre segue o caminho "com fita".
//   PATCH 2 (0x60BB30, opcional): a chamada que TIRA a fita do inventario apos salvar vira "add esp,8" (mesmo efeito
//            na pilha, sem remover). So com ConsumeRibbon=0.
// O ID da fita nao esta no exe (vem do dado do objeto), entao nenhum ID e necessario para isso.
// v3: RibbonToHerb - ao PEGAR um ink ribbon (ID 55) no cenario o inventario recebe erva verde (ID 43). IDs vistos no seu log.
//     O modelo no cenario continua o do ink ribbon (trocar o modelo exige achar onde o jogo cria o item no cenario).
// v2: sonda extra nas funcoes do inventario (BagFn_*), para achar o ID do ink ribbon e da erva verde ao PEGAR itens.
// Tambem grava no log os comandos de pegar item (LogItems=1), para descobrir os IDs da fita e da erva verde.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

typedef unsigned char u8;
typedef unsigned int  u32;

static int gFree = 1, gConsume = 1, gLogItems = 0, gSwap = 1, gRibbonId = 55, gHerbId = 43;
static char  gDir[MAX_PATH] = "";
static FILE* gF = NULL;
static volatile LONG gLines = 0;
static char gLast[256] = "";
static int gLogOn = 0;   // Log=1 no .ini liga o arquivo de log (padrao: desligado)
static void Log(const char* f, ...) {
    if (!gLogOn) return;
    char b[256];
    va_list a; va_start(a, f); _vsnprintf(b, sizeof(b) - 1, f, a); va_end(a); b[sizeof(b) - 1] = 0;
    if (InterlockedIncrement(&gLines) > 30000) return;
    if (!gF) { char p[MAX_PATH]; strcpy(p, gDir); strcat(p, "RE0HD_FreeSave.log"); gF = fopen(p, "a"); }
    if (!gF) return;
    fprintf(gF, "%s\n", b); fflush(gF);
}
// linha repetida em sequencia (ex.: busca por frame) so e gravada na primeira vez
static void LogDedupe(const char* f, ...) {
    char b[256];
    va_list a; va_start(a, f); _vsnprintf(b, sizeof(b) - 1, f, a); va_end(a); b[sizeof(b) - 1] = 0;
    if (strcmp(b, gLast) == 0) return;
    strcpy(gLast, b);
    Log("%s", b);
}

struct Probe { u32 addr; int len; const char* name; bool script; u8 sig[8]; };
static const Probe kP[] = {
    { 0x579390, 6, "item_get",      true,  { 0x83,0xEC,0x0C,0x8D,0x04,0x24 } },
    { 0x579520, 6, "item_getcheck", true,  { 0x83,0xEC,0x10,0x8D,0x04,0x24 } },
    { 0x56F180, 5, "ItemPut",       true,  { 0x83,0xEC,0x10,0x56,0x57 } },
    { 0x569090, 6, "Sub_GetItem",   true,  { 0x8B,0x0D,0xD0,0xEB,0xDC,0x00 } },
    // v2: candidatas a "adicionar/remover item no inventario" (args = ID do item, quantidade; this = inventario)
    { 0x4DB4C0, 5, "BagFn_4DB4C0",  false, { 0x83,0xEC,0x20,0x53,0x55 } },
    { 0x4DC070, 6, "BagFn_4DC070",  false, { 0x83,0xEC,0x14,0x57,0x8B,0xF9 } },
    { 0x4DB910, 6, "BagRemove",     false, { 0x83,0xEC,0x20,0x56,0x8B,0xF1 } },
    { 0x4DBC70, 7, "BagFn_4DBC70",  false, { 0x83,0xEC,0x08,0x55,0x57,0x8B,0xF9 } },
};
static const int NP = (int)(sizeof(kP) / sizeof(kP[0]));

// f[0..7] = EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX (pushad); f[8] = EFLAGS; f[9] = retorno; f[10..] = argumentos
static void __cdecl OnProbe(int id, u32* f) {
    const Probe& p = kP[id];
    // TROCA no pickup: o jogo chama BagFn_4DB4C0(id, quantidade) em 0x5E1437 ao pegar um item do cenario.
    // f[10] = 1o argumento (ID) na propria pilha; f[0] = EDI = objeto do pickup, que guarda o ID em +0x390.
    if (gSwap && p.addr == 0x4DB4C0 && f[9] == 0x5E1437 && (int)f[10] == gRibbonId) {
        Log("pickup: ink ribbon (ID %d) trocado por erva verde (ID %d), quantidade %u", gRibbonId, gHerbId, f[11]);
        f[10] = (u32)gHerbId;
        if (f[0] && !IsBadWritePtr((void*)(f[0] + 0x390), 4) && *(int*)(f[0] + 0x390) == gRibbonId) *(int*)(f[0] + 0x390) = gHerbId;
    }
    if (!gLogItems) return;
    if (p.script) {
        u32 ctx = f[10];
        char hex[64] = "?";
        if (ctx && !IsBadReadPtr((void*)(ctx + 8), 8)) {
            u32 code = *(u32*)(ctx + 8), pos = *(u32*)(ctx + 0xC);
            if (code && !IsBadReadPtr((void*)(code + pos), 16)) {
                const u8* c = (const u8*)(code + pos); hex[0] = 0;
                for (int i = 0; i < 16; ++i) sprintf(hex + strlen(hex), "%02X ", c[i]);
            }
        }
        Log("%s ret=%08X ctx=%08X bytecode: %s", p.name, f[9], ctx, hex);
    } else {
        if (strcmp(p.name, "SaveEvent") == 0 && f[10] && !IsBadReadPtr((void*)(f[10] + 0x38), 4))
            LogDedupe("%s ret=%08X ctx=%08X estado=%u", p.name, f[9], f[10], *(u32*)(f[10] + 0x38));
        else
            LogDedupe("%s ret=%08X this=%08X args=%u,%u,%u,%u", p.name, f[9], f[6], f[10], f[11], f[12], f[13]);
    }
}

static bool InstallProbe(int i) {
    const Probe& p = kP[i];
    if (IsBadReadPtr((void*)p.addr, 16)) return false;
    if (memcmp((void*)p.addr, p.sig, p.len) != 0) {
        const u8* c = (const u8*)p.addr;
        Log("%s: assinatura nao confere em %08X: %02X %02X %02X %02X %02X %02X %02X %02X", p.name, p.addr, c[0],c[1],c[2],c[3],c[4],c[5],c[6],c[7]);
        return true;                                                   // nao tenta de novo
    }
    u8* tr = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return false;
    u8* q = tr;
    *q++ = 0x9C; *q++ = 0x60;                                          // pushfd; pushad
    *q++ = 0x54;                                                       // push esp  (-> f)
    *q++ = 0x68; *(u32*)q = (u32)i; q += 4;                            // push id
    *q++ = 0xE8; *(u32*)q = (u32)&OnProbe - (u32)(q + 4); q += 4;      // call OnProbe
    *q++ = 0x83; *q++ = 0xC4; *q++ = 0x08;                             // add esp,8
    *q++ = 0x61; *q++ = 0x9D;                                          // popad; popfd
    memcpy(q, (void*)p.addr, p.len); q += p.len;                       // instrucoes originais (sem referencias relativas)
    *q++ = 0xE9; *(u32*)q = (p.addr + p.len) - (u32)(q + 4); q += 4;
    FlushInstructionCache(GetCurrentProcess(), tr, 64);
    DWORD old;
    if (!VirtualProtect((void*)p.addr, p.len, PAGE_EXECUTE_READWRITE, &old)) return false;
    u8* t = (u8*)p.addr;
    t[0] = 0xE9; *(u32*)(t + 1) = (u32)tr - (p.addr + 5);
    for (int k = 5; k < p.len; ++k) t[k] = 0x90;
    VirtualProtect((void*)p.addr, p.len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)p.addr, p.len);
    Log("%s: sonda instalada em %08X", p.name, p.addr);
    return true;
}


static bool PatchBytes(u32 addr, const u8* expect, const u8* repl, int len, const char* name) {
    if (IsBadReadPtr((void*)addr, len)) return false;
    if (memcmp((void*)addr, repl, len) == 0) { Log("%s: ja aplicado", name); return true; }
    if (memcmp((void*)addr, expect, len) != 0) {
        const u8* c = (const u8*)addr;
        Log("%s: bytes inesperados em %08X: %02X %02X %02X %02X %02X %02X (nao alterado)", name, addr, c[0],c[1],c[2],c[3],c[4],c[5]);
        return true;                                                   // nao tenta de novo
    }
    DWORD old;
    if (!VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)addr, repl, len);
    VirtualProtect((void*)addr, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, len);
    Log("%s: aplicado em %08X", name, addr);
    return true;
}
static bool ApplyPatches() {
    bool ok = true;
    if (gFree) {
        static const u8 ex1[5] = { 0x83,0xF8,0xFF,0x74,0x3B }, rp1[5] = { 0x83,0xF8,0xFF,0x90,0x90 };
        ok &= PatchBytes(0x57AD38, ex1, rp1, 5, "patch1 (sem fita)");
    }
    if (!gConsume) {
        static const u8 ex2[5] = { 0xE8,0xDB,0xFD,0xEC,0xFF }, rp2[5] = { 0x83,0xC4,0x08,0x90,0x90 };
        ok &= PatchBytes(0x60BB30, ex2, rp2, 5, "patch2 (nao consumir)");
    }
    return ok;
}

static DWORD WINAPI Init(LPVOID) {
    char ini[MAX_PATH]; HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Init, &self);
    GetModuleFileNameA(self, ini, MAX_PATH);
    char* s = strrchr(ini, '\\'); if (s) *(s + 1) = 0;
    strcpy(gDir, ini); strcat(ini, "RE0HD_FreeSave.ini");
    if (!GetPrivateProfileIntA("Main", "Enable", 1, ini)) return 0;
    gFree     = GetPrivateProfileIntA("Main", "FreeSave", 1, ini);
    gConsume  = GetPrivateProfileIntA("Main", "ConsumeRibbon", 1, ini);
    gLogItems = GetPrivateProfileIntA("Main", "LogItems", 0, ini);
    gLogOn    = GetPrivateProfileIntA("Main", "Log", 0, ini) != 0 || gLogItems != 0;
    gSwap     = GetPrivateProfileIntA("Main", "RibbonToHerb", 1, ini);
    gRibbonId = GetPrivateProfileIntA("Main", "RibbonItemId", 55, ini);
    gHerbId   = GetPrivateProfileIntA("Main", "HerbItemId", 43, ini);
    u32 base = (u32)GetModuleHandleA(NULL);
    Log("RE0HD_FreeSave v4 (base=%08X, esperado 00400000; FreeSave=%d ConsumeRibbon=%d RibbonToHerb=%d ids %d->%d)", base, gFree, gConsume, gSwap, gRibbonId, gHerbId);
    if (base != 0x400000) { Log("exe em base diferente: sonda desligada"); return 0; }
    bool done[16] = {}, patched = false;
    for (int t = 0; t < 240; ++t) {                                    // o codigo pode demorar a ficar legivel
        if (!patched) patched = ApplyPatches();
        int n = 0;
        for (int i = 0; i < NP; ++i) {
            bool want = gLogItems || (gSwap && kP[i].addr == 0x4DB4C0);
            if (!want) { done[i] = true; ++n; continue; }
            if (!done[i]) done[i] = InstallProbe(i);
            if (done[i]) ++n;
        }
        if (patched && n == NP) break;
        Sleep(500);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(NULL, 0, Init, NULL, 0, NULL); }
    return TRUE;
}
