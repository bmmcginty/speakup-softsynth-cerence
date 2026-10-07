/*
 * wine-bridge-speakup-cerence: host the 64-bit Cerence engine under Wine.
 *
 * The engine is a 64-bit Windows driver pair (runatts_*.dll plus its license
 * manager) that NVDA loads through ctypes.  This program reproduces the same
 * call sequence from a plain Win32 executable for the native Linux
 * speakup-cerence front end.
 *
 * It is a thin host: it does not embed any Cerence code or voice data.  The
 * DLLs and the per-voice packages have to be supplied by the caller.
 */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>

#define VE_CURRENT_VERSION       0x0520
#define VPLATFORM_CURRENT_VERSION 0x0200
#define VE_MAX_STRING_LENGTH     128

#define NUAN_OK                  0
#define NUAN_E_TTS_USERSTOP      0x80000807

/* Parameter identifiers used by the engine ABI. */
enum {
    PARAM_LANGUAGE = 1,
    PARAM_VOICE = 2,
    PARAM_VOICE_OPERATING_POINT = 3,
    PARAM_FREQUENCY = 4,
    PARAM_TYPE_OF_CHAR = 7,
    PARAM_VOLUME = 8,
    PARAM_SPEECHRATE = 9,
    PARAM_PITCH = 10,
    PARAM_WAITFACTOR = 11,
    PARAM_READMODE = 12,
    PARAM_TEXTMODE = 13,
    PARAM_MARKER_MODE = 19,
    PARAM_INITMODE = 20,
    PARAM_DISABLE_FINAL_SILENCE = 22,
    PARAM_TIMBRE = 24,
};

#define VE_INITMODE_LOAD_ONCE_OPEN_ALL 0xC
#define VE_TEXTMODE_STANDARD 1
#define READMODE_SENT 1
#define READMODE_CHAR 2
#define VE_MRK_ON 1
#define VE_TYPE_OF_CHAR_UTF8 2

#define PITCH_MIN 50
#define PITCH_MAX 200
#define RATE_MIN 50
#define RATE_MAX 400
#define VOLUME_MIN 0
#define VOLUME_MAX 100
#define WAITFACTOR_MAX 9

#define VE_MSG_ENDPROCESS 0x00000002
#define VE_MSG_OUTBUFREQ  0x00000008
#define VE_MSG_OUTBUFDONE 0x00000010
#define VE_MRK_BOOKMARK   0x0008

typedef struct {
    uint16_t fmtVersion;
    const char *pBinBrokerInfo;
    void *pIHeap;
    void *hHeap;
    void *pICritSec;
    void *hCSClass;
    void *pIDataStream;
    void *pIDataMapping;
    void *hDataClass;
    void *pILog;
    void *hLog;
    void *pIClock;
    void *hClock;
    void *pIThread;
    void *pISemaphore;
    void *hThdClass;
} VE_INSTALL;

typedef struct {
    void *start;
    unsigned cByte;
    unsigned cFlags;
} VPLATFORM_MEMBLOCK;

typedef struct {
    uint16_t fmtVersion;
    uint16_t u16NbrOfDataInstall;
    const wchar_t **apDataInstall;
    VPLATFORM_MEMBLOCK stHeap;
    void *pDatPtr_Table;
    const wchar_t *szBinaryBroker;
    const wchar_t *szFileListFile;
    unsigned bFlags;
    unsigned rfu1;
} VPLATFORM_RESOURCES;

typedef struct {
    void *pHandleData;
    unsigned u32Check;
} VE_HSAFE;

typedef union {
    uint16_t usValue;
    char szStringValue[VE_MAX_STRING_LENGTH];
} VE_PARAM_VALUE;

typedef struct {
    unsigned eID;
    VE_PARAM_VALUE uValue;
} VE_PARAM;

typedef struct {
    unsigned eMessage;
    int lValue;
    void *pParam;
} VE_CALLBACKMSG;

typedef struct {
    char szLanguage[VE_MAX_STRING_LENGTH];
    char szLanguageTLW[4];
    char szVersion[VE_MAX_STRING_LENGTH];
} VE_LANGUAGE;

typedef struct {
    char szVersion[VE_MAX_STRING_LENGTH];
    char szLanguage[VE_MAX_STRING_LENGTH];
    char szVoiceName[VE_MAX_STRING_LENGTH];
    char szVoiceAge[VE_MAX_STRING_LENGTH];
    char szVoiceType[VE_MAX_STRING_LENGTH];
    char szForeignLanguages[VE_MAX_STRING_LENGTH];
} VE_VOICEINFO;

typedef struct {
    char szVersion[VE_MAX_STRING_LENGTH];
    char szLanguage[VE_MAX_STRING_LENGTH];
    char szVoiceName[VE_MAX_STRING_LENGTH];
    char szVoiceOperatingPoint[VE_MAX_STRING_LENGTH];
    uint16_t u16Freq;
} VE_SPEECHDBINFO;

typedef struct {
    unsigned eMrkType;
    size_t cntSrcPos;
    size_t cntSrcTextLen;
    size_t cntDestPos;
    unsigned cntDestLen;
    unsigned usValue;
    unsigned ulValue;
    const char *szValue;
} VE_MARKINFO;

typedef struct {
    unsigned eAudioFormat;
    size_t cntPcmBufLen;
    void *pOutPcmBuf;
    size_t cntMrkListLen;
    VE_MARKINFO *pMrkList;
} VE_OUTDATA;

typedef struct {
    int eTextFormat;
    size_t cntTextLength;
    void *szInText;
} VE_INTEXT;

typedef uint32_t (*out_notify_t)(VE_HSAFE, void *, VE_CALLBACKMSG *);

typedef struct {
    void *userData;
    out_notify_t pfOutNotify;
} VE_OUTDEVINFO;

/* ---------------------------------------------------------------- exports */

static unsigned (*ve_startAuthorization)(void);
static unsigned (*ve_setLicenseKey)(const char *);
static unsigned (*ve_queryLicense)(VE_HSAFE *);
static unsigned (*ve_registerLicense)(VE_HSAFE *, const wchar_t *, int);
static unsigned (*ve_unregisterLicense)(VE_HSAFE *, const char *);
static unsigned (*ve_generateAuthorizationData)(VE_HSAFE *, const wchar_t *);
static unsigned (*ve_isActivationOffline)(void);
static unsigned (*ve_getInterfaces)(VE_INSTALL *, VPLATFORM_RESOURCES *);
static unsigned (*ve_initialize)(VE_INSTALL *, VE_HSAFE *);
static unsigned (*ve_uninitialize)(VE_HSAFE);
static unsigned (*ve_releaseInterfaces)(VE_INSTALL *);
static unsigned (*ve_getLanguages)(VE_HSAFE, VE_LANGUAGE *, uint16_t *);
static unsigned (*ve_getVoices)(VE_HSAFE, const char *, VE_VOICEINFO *, uint16_t *);
static unsigned (*ve_getSpeechDbs)(VE_HSAFE, const char *, const char *, VE_SPEECHDBINFO *, uint16_t *);
static unsigned (*ve_open)(VE_HSAFE, void *, void *, VE_HSAFE *);
static unsigned (*ve_close)(VE_HSAFE);
static unsigned (*ve_setCallback)(VE_HSAFE, VE_OUTDEVINFO *);
static unsigned (*ve_setParamList)(VE_HSAFE, VE_PARAM *, uint16_t);
static unsigned (*ve_getParamList)(VE_HSAFE, VE_PARAM *, uint16_t);
static unsigned (*ve_speak)(VE_HSAFE, VE_INTEXT *);
static unsigned (*ve_stop)(VE_HSAFE);
static const char *(*ve_getLastErrorMessage)(void);
static unsigned (*ve_getProductVersion)(void *);
static unsigned (*ve_getAdditionalProductInfo)(void *);

static int g_debug_level;

static void debug_log(int level, const char *fmt, ...)
{
    va_list ap;

    if (g_debug_level < level)
        return;
    fprintf(stderr, "bridge-debug%d: ", level);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static const char *errText(unsigned code, char *buf, size_t len)
{
    const char *last = NULL;
    if (ve_getLastErrorMessage)
        last = ve_getLastErrorMessage();
    if (last && last[0])
        snprintf(buf, len, "%s (0x%08x)", last, code);
    else
        snprintf(buf, len, "0x%08x", code);
    return buf;
}

#define CHECK(expr, what)                                                    \
    do {                                                                     \
        unsigned _rc = (expr);                                               \
        if (_rc != NUAN_OK && _rc != NUAN_E_TTS_USERSTOP) {                  \
            char _b[256];                                                    \
            fprintf(stderr, "%s failed: %s\n", (what), errText(_rc, _b, sizeof _b)); \
            return -1;                                                       \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------ path helpers */

/* Resolve the installation root from bin/wine-bridge-speakup-cerence.exe.
 * This makes the defaults independent of both Wine's working directory and
 * the directory in which the binaries were built. */
static int executable_root(wchar_t *root, size_t size)
{
    wchar_t *slash;
    DWORD length = GetModuleFileNameW(NULL, root, (DWORD)size);

    if (!length || length >= size)
        return -1;
    slash = wcsrchr(root, L'\\');
    if (!slash)
        slash = wcsrchr(root, L'/');
    if (!slash)
        return -1;
    *slash = L'\0'; /* bin */
    slash = wcsrchr(root, L'\\');
    if (!slash)
        slash = wcsrchr(root, L'/');
    if (!slash)
        return -1;
    *slash = L'\0';
    return 0;
}

/* Accept Linux paths from the caller and hand Wine a Z: drive path. */
static wchar_t *path_to_wine(const wchar_t *in)
{
    size_t i, n = wcslen(in);
    int unix_style = (n > 0 && in[0] == L'/');
    wchar_t *out = malloc((n + 4) * sizeof(wchar_t));
    if (!out)
        return NULL;
    if (unix_style) {
        out[0] = L'Z';
        out[1] = L':';
        for (i = 0; i < n; i++)
            out[i + 2] = (in[i] == L'/') ? L'\\' : in[i];
        out[n + 2] = 0;
    } else {
        for (i = 0; i <= n; i++)
            out[i] = in[i];
    }
    return out;
}

static char *utf16_to_utf8(const wchar_t *s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    char *out;
    if (n <= 0)
        return NULL;
    out = malloc((size_t)n);
    if (!out)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out, n, NULL, NULL);
    return out;
}

static wchar_t *utf8_to_utf16(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *out;
    if (n <= 0)
        return NULL;
    out = malloc((size_t)n * sizeof(wchar_t));
    if (!out)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n);
    return out;
}

/* ----------------------------------------------------------------- loading */

static HMODULE load_preloaded(const wchar_t *libdir, const wchar_t *name)
{
    wchar_t path[MAX_PATH * 2];
    _snwprintf(path, MAX_PATH * 2 - 1, L"%ls\\%ls", libdir, name);
    return LoadLibraryW(path);
}

static int file_exists(const wchar_t *libdir, const wchar_t *name)
{
    wchar_t path[MAX_PATH * 2];
    _snwprintf(path, MAX_PATH * 2 - 1, L"%ls\\%ls", libdir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static char *discover_addon_name(const wchar_t *libdir)
{
    static const wchar_t prefix[] = L"runatts_";
    static const wchar_t suffix[] = L".dll";
    wchar_t pattern[MAX_PATH * 2];
    wchar_t basename[MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE search;
    size_t name_len, prefix_len = wcslen(prefix), suffix_len = wcslen(suffix);

    _snwprintf(pattern, MAX_PATH * 2 - 1, L"%ls\\runatts_*.dll", libdir);
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return NULL;
    FindClose(search);

    name_len = wcslen(found.cFileName);
    if (name_len <= prefix_len + suffix_len ||
        wcsncmp(found.cFileName, prefix, prefix_len) != 0 ||
        _wcsicmp(found.cFileName + name_len - suffix_len, suffix) != 0)
        return NULL;
    name_len -= prefix_len + suffix_len;
    if (name_len >= MAX_PATH)
        return NULL;
    wmemcpy(basename, found.cFileName + prefix_len, name_len);
    basename[name_len] = L'\0';
    return utf16_to_utf8(basename);
}

static FARPROC need(HMODULE mod, const char *name)
{
    FARPROC p = GetProcAddress(mod, name);
    if (!p)
        fprintf(stderr, "export %s is missing\n", name);
    return p;
}

static int load_engine(const wchar_t *libdir, const char *libdir_display,
                       const char *addon_name)
{
    HMODULE engine;
    wchar_t dep[64];
    wchar_t *w;
    DWORD error;
    int missing = 0;

    /* Diagnose a missing or wrong-architecture payload before LoadLibrary,
     * whose failure code otherwise reads like a plain missing file. */
    w = utf8_to_utf16(addon_name);
    _snwprintf(dep, 63, L"runatts_%ls.dll", w ? w : L"");
    free(w);
    if (!file_exists(libdir, dep)) {
        fprintf(stderr, "no Cerence engine in %s/%ls\n",
                libdir_display, dep);
        return -1;
    }

    /* Load the license manager before the engine because the engine imports
     * it by name. The 64-bit Visual C++ runtime is supplied by Wine or the
     * configured Wine prefix rather than bundled beside the engine. */
    w = utf8_to_utf16(addon_name);
    _snwprintf(dep, 63, L"licenseManager_%ls.dll", w ? w : L"");
    free(w);
    if (!load_preloaded(libdir, dep)) {
        error = GetLastError();
        if (error == ERROR_BAD_EXE_FORMAT)
            fprintf(stderr, "%ls in %s does not match the host architecture\n",
                    dep, libdir_display);
        else
            fprintf(stderr, "cannot load %ls (error %lu)\n", dep, error);
        return -1;
    }

    w = utf8_to_utf16(addon_name);
    _snwprintf(dep, 63, L"runatts_%ls.dll", w ? w : L"");
    free(w);
    engine = load_preloaded(libdir, dep);
    if (!engine) {
        error = GetLastError();
        if (error == ERROR_BAD_EXE_FORMAT)
            fprintf(stderr, "%ls in %s does not match the host architecture\n",
                    dep, libdir_display);
        else
            fprintf(stderr, "cannot load %ls (error %lu)\n", dep, error);
        return -1;
    }

#define LOAD_EXPORT(var, name)                                               \
    do {                                                                     \
        FARPROC _p = need(engine, name);                                     \
        memcpy(&(var), &_p, sizeof(_p));                                     \
        if (!_p) missing++;                                                  \
    } while (0)

    LOAD_EXPORT(ve_startAuthorization, "startAuthorization");
    LOAD_EXPORT(ve_setLicenseKey, "setLicenseKey");
    LOAD_EXPORT(ve_queryLicense, "queryLicense");
    LOAD_EXPORT(ve_registerLicense, "registerLicense");
    LOAD_EXPORT(ve_unregisterLicense, "unregisterLicense");
    LOAD_EXPORT(ve_generateAuthorizationData, "generateAuthorizationData");
    LOAD_EXPORT(ve_isActivationOffline, "isActivationOffline");
    LOAD_EXPORT(ve_getInterfaces, "getInterfaces");
    LOAD_EXPORT(ve_initialize, "initialize");
    LOAD_EXPORT(ve_uninitialize, "uninitialize");
    LOAD_EXPORT(ve_releaseInterfaces, "releaseInterfaces");
    LOAD_EXPORT(ve_getLanguages, "getLanguages");
    LOAD_EXPORT(ve_getVoices, "getVoices");
    LOAD_EXPORT(ve_getSpeechDbs, "getSpeechDbs");
    LOAD_EXPORT(ve_open, "open");
    LOAD_EXPORT(ve_close, "close");
    LOAD_EXPORT(ve_setCallback, "setCallback");
    LOAD_EXPORT(ve_setParamList, "setParamList");
    LOAD_EXPORT(ve_getParamList, "getParamList");
    LOAD_EXPORT(ve_speak, "speak");
    LOAD_EXPORT(ve_stop, "stop");
    LOAD_EXPORT(ve_getLastErrorMessage, "getLastErrorMessage");
    LOAD_EXPORT(ve_getProductVersion, "getProductVersion");
    LOAD_EXPORT(ve_getAdditionalProductInfo, "getAdditionalProductInfo");
    (void)missing;

    if (missing) {
        fprintf(stderr, "engine DLL is missing required exports\n");
        return -1;
    }
    debug_log(1, "loaded license manager and runatts_%s.dll", addon_name);
    return 0;
}

/* ----------------------------------------------------------------- license */

static int license_start(void)
{
    if (ve_startAuthorization() != NUAN_OK) {
        fprintf(stderr, "startAuthorization failed\n");
        return -1;
    }
    return 0;
}

static int license_status(void)
{
    unsigned rc;
    if (license_start() < 0)
        return -1;
    if (ve_setLicenseKey("") != NUAN_OK) {
        printf("no-license-key\n");
        return 1;
    }
    rc = ve_queryLicense(NULL);
    if (rc == NUAN_OK) {
        printf("licensed\n");
        return 0;
    }
    printf("unlicensed\n");
    return 1;
}

/* The licence entry points take wide-character file paths: ctypes hands the
 * engine a Python str, which becomes wchar_t*, and the add-on relies on that.
 * Paths from the command line are converted to Wine form first. */
static int license_activate(const char *key, const char *license_file, const wchar_t *out_req)
{
    unsigned rc;
    if (license_start() < 0)
        return -1;
    ve_setLicenseKey(key ? key : "");

    if (out_req) {
        wchar_t *path = path_to_wine(out_req);
        rc = ve_generateAuthorizationData(NULL, path);
        free(path);
        if (rc != NUAN_OK) {
            char b[256];
            fprintf(stderr, "generateAuthorizationData failed: %s\n", errText(rc, b, sizeof b));
            return -1;
        }
        printf("wrote-request\n");
        return 0;
    }

    if (license_file) {
        wchar_t *path = path_to_wine(utf8_to_utf16(license_file));
        rc = ve_registerLicense(NULL, path, 1);
        free(path);
    } else {
        rc = ve_registerLicense(NULL, L"", 0);
    }
    if (rc != NUAN_OK) {
        char b[256];
        fprintf(stderr, "registerLicense failed: %s\n", errText(rc, b, sizeof b));
        return -1;
    }
    printf("activated\n");
    return 0;
}

/* ------------------------------------------------------------------ engine */

static VE_INSTALL g_install;
static VPLATFORM_RESOURCES g_platform;
static VE_HSAFE g_speech;
static HANDLE g_engine_mutex;

static int lock_engine(void)
{
    DWORD wait;

    g_engine_mutex = CreateMutexW(NULL, FALSE,
                                  L"Local\\speakup-cerence-engine-64");
    if (!g_engine_mutex) {
        fprintf(stderr, "cannot create the Cerence engine lock\n");
        return -1;
    }
    wait = WaitForSingleObject(g_engine_mutex, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        fprintf(stderr, "the Cerence engine is already in use by another "
                "speakup-cerence process\n");
        CloseHandle(g_engine_mutex);
        g_engine_mutex = NULL;
        return -1;
    }
    return 0;
}

static int engine_open_data(const wchar_t **dirs, size_t ndirs)
{
    size_t i;

    debug_log(1, "initializing engine with %llu data directories",
              (unsigned long long)ndirs);
    for (i = 0; i < ndirs; i++)
        debug_log(2, "data[%llu]=%ls", (unsigned long long)i, dirs[i]);
    memset(&g_install, 0, sizeof g_install);
    g_install.fmtVersion = VE_CURRENT_VERSION;
    memset(&g_platform, 0, sizeof g_platform);
    g_platform.fmtVersion = VPLATFORM_CURRENT_VERSION;
    g_platform.u16NbrOfDataInstall = (uint16_t)ndirs;
    g_platform.apDataInstall = dirs;

    CHECK(ve_startAuthorization(), "startAuthorization");
    ve_setLicenseKey("");
    CHECK(ve_getInterfaces(&g_install, &g_platform), "getInterfaces");
    memset(&g_speech, 0, sizeof g_speech);
    CHECK(ve_initialize(&g_install, &g_speech), "initialize");
    debug_log(1, "engine initialized");
    return 0;
}

static int engine_close(void)
{
    if (g_speech.pHandleData) {
        ve_uninitialize(g_speech);
        g_speech.pHandleData = NULL;
    }
    if (g_install.hHeap || g_install.hLog)
        ve_releaseInterfaces(&g_install);
    if (g_engine_mutex) {
        ReleaseMutex(g_engine_mutex);
        CloseHandle(g_engine_mutex);
        g_engine_mutex = NULL;
    }
    return 0;
}

static int cmd_list_languages(void)
{
    uint16_t n = 0;
    VE_LANGUAGE *langs;
    uint16_t i;
    CHECK(ve_getLanguages(g_speech, NULL, &n), "getLanguages(count)");
    langs = calloc(n ? n : 1, sizeof(VE_LANGUAGE));
    if (!langs)
        return -1;
    CHECK(ve_getLanguages(g_speech, langs, &n), "getLanguages(list)");
    for (i = 0; i < n; i++)
        printf("%s\t%s\n", langs[i].szLanguageTLW, langs[i].szLanguage);
    free(langs);
    return 0;
}

static int cmd_list_voices(const char *language)
{
    uint16_t n = 0, i, j, k, language_count = 0;
    VE_LANGUAGE *languages;
    VE_VOICEINFO *voices;
    char resolved_language[VE_MAX_STRING_LENGTH];

    resolved_language[0] = '\0';
    CHECK(ve_getLanguages(g_speech, NULL, &language_count), "getLanguages(count)");
    languages = calloc(language_count ? language_count : 1, sizeof(VE_LANGUAGE));
    if (!languages)
        return -1;
    CHECK(ve_getLanguages(g_speech, languages, &language_count), "getLanguages(list)");
    for (i = 0; i < language_count; i++) {
        if (!_stricmp(language, languages[i].szLanguageTLW) ||
            !_stricmp(language, languages[i].szLanguage)) {
            snprintf(resolved_language, sizeof resolved_language, "%s",
                     languages[i].szLanguage);
            break;
        }
    }
    free(languages);
    if (!resolved_language[0]) {
        fprintf(stderr, "language is not installed: %s\n", language);
        return -1;
    }

    language = resolved_language;
    CHECK(ve_getVoices(g_speech, language, NULL, &n), "getVoices(count)");
    voices = calloc(n ? n : 1, sizeof(VE_VOICEINFO));
    if (!voices)
        return -1;
    CHECK(ve_getVoices(g_speech, language, voices, &n), "getVoices(list)");
    for (i = 0; i < n; i++) {
        uint16_t nd = 0;
        VE_SPEECHDBINFO *dbs;
        CHECK(ve_getSpeechDbs(g_speech, language, voices[i].szVoiceName, NULL, &nd), "getSpeechDbs(count)");
        dbs = calloc(nd ? nd : 1, sizeof(VE_SPEECHDBINFO));
        if (!dbs) {
            free(voices);
            return -1;
        }
        CHECK(ve_getSpeechDbs(g_speech, language, voices[i].szVoiceName, dbs, &nd), "getSpeechDbs(list)");
        for (k = 0; k < nd; k++)
            printf("%s\t%s\t%s\t%s\t%s\n", language, voices[i].szVoiceName,
                   dbs[k].szVoiceOperatingPoint, voices[i].szVoiceType,
                   voices[i].szVoiceAge);
        free(dbs);
        (void)j;
    }
    free(voices);
    return 0;
}

/* ---------------------------------------------------------------- speaking */

struct sink {
    FILE *wav;
    FILE *pcm;
    int wrote_header;
    unsigned long frames;
    volatile long done;
    unsigned rate;
};

static struct sink g_sink;
static char g_pcm_buf[8192];
static VE_MARKINFO g_marks[100];

static uint32_t on_audio(VE_HSAFE instance, void *userData, VE_CALLBACKMSG *msg)
{
    VE_OUTDATA *out;
    (void)instance;
    (void)userData;
    if (!msg)
        return NUAN_OK;
    if (msg->eMessage == VE_MSG_OUTBUFREQ) {
        out = (VE_OUTDATA *)msg->pParam;
        out->pOutPcmBuf = g_pcm_buf;
        out->cntPcmBufLen = sizeof g_pcm_buf;
        out->pMrkList = g_marks;
        out->cntMrkListLen = sizeof g_marks;
    } else if (msg->eMessage == VE_MSG_OUTBUFDONE) {
        out = (VE_OUTDATA *)msg->pParam;
        if (out->cntPcmBufLen) {
            if (g_sink.wav)
                fwrite(out->pOutPcmBuf, 1, out->cntPcmBufLen, g_sink.wav);
            if (g_sink.pcm)
                fwrite(out->pOutPcmBuf, 1, out->cntPcmBufLen, g_sink.pcm);
            g_sink.frames += (unsigned long)(out->cntPcmBufLen / 2);
        }
    } else if (msg->eMessage == VE_MSG_ENDPROCESS) {
        InterlockedExchange(&g_sink.done, 1);
    }
    return NUAN_OK;
}

/* 44-byte canonical header for 16-bit mono PCM.  Both the RIFF and the data
 * chunk sizes start out as zero and are patched once the length is known. */
#define WAV_RIFF_SIZE_OFFSET 4
#define WAV_DATA_SIZE_OFFSET 40

static void write_wav_header(FILE *f, unsigned rate)
{
    unsigned char h[44];
    unsigned byte_rate = rate * 2;
    memset(h, 0, sizeof h);
    memcpy(h + 0, "RIFF", 4);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    h[16] = 16;
    h[20] = 1;                                   /* PCM */
    h[22] = 1;                                   /* channels */
    h[24] = (unsigned char)(rate & 0xff);
    h[25] = (unsigned char)((rate >> 8) & 0xff);
    h[26] = (unsigned char)((rate >> 16) & 0xff);
    h[27] = (unsigned char)((rate >> 24) & 0xff);
    h[28] = (unsigned char)(byte_rate & 0xff);
    h[29] = (unsigned char)((byte_rate >> 8) & 0xff);
    h[30] = (unsigned char)((byte_rate >> 16) & 0xff);
    h[31] = (unsigned char)((byte_rate >> 24) & 0xff);
    h[32] = 2;                                   /* block align */
    h[34] = 16;                                  /* bits per sample */
    memcpy(h + 36, "data", 4);
    fwrite(h, 1, sizeof h, f);
}

static void patch_wav_header(FILE *f, unsigned long frames, unsigned rate)
{
    unsigned data = (unsigned)(frames * 2);
    unsigned riff = data + 36;
    fseek(f, WAV_RIFF_SIZE_OFFSET, SEEK_SET);
    fwrite(&riff, 4, 1, f);
    fseek(f, WAV_DATA_SIZE_OFFSET, SEEK_SET);
    fwrite(&data, 4, 1, f);
    fseek(f, 0, SEEK_END);
    (void)rate;
}

static VE_PARAM *params_begin(size_t n, VE_PARAM *buf)
{
    memset(buf, 0, n * sizeof(VE_PARAM));
    return buf;
}

static void param_int(VE_PARAM *p, unsigned id, int value)
{
    p->eID = id;
    p->uValue.usValue = (uint16_t)value;
}

static void param_str(VE_PARAM *p, unsigned id, const char *value)
{
    size_t len = strlen(value);
    if (len > VE_MAX_STRING_LENGTH - 1)
        len = VE_MAX_STRING_LENGTH - 1;
    p->eID = id;
    memcpy(p->uValue.szStringValue, value, len);
    p->uValue.szStringValue[len] = '\0';
}

struct speak_opts {
    const char *voice;
    const char *vop;
    const char *language;
    const char *text;      /* UTF-8 */
    const char *wav_path;  /* may be NULL */
    const char *pcm_path;  /* may be NULL */
    int rate, pitch, volume, waitfactor;
    int have_rate, have_pitch, have_volume, have_waitfactor;
    int char_mode;
    int timeout_ms;
};

static int cmd_speak(const struct speak_opts *o)
{
    VE_PARAM params[16];
    VE_PARAM query[8];
    VE_INTEXT in_text;
    VE_OUTDEVINFO dev;
    VE_HSAFE instance;
    int n = 0;
    int rc = 0;
    int waited = 0;

    memset(&g_sink, 0, sizeof g_sink);
    g_sink.rate = 22050;

    if (o->wav_path) {
        wchar_t *w = path_to_wine(utf8_to_utf16(o->wav_path));
        g_sink.wav = _wfopen(w, L"wb");
        free(w);
        if (!g_sink.wav) {
            fprintf(stderr, "cannot write %s\n", o->wav_path);
            return -1;
        }
        write_wav_header(g_sink.wav, g_sink.rate);
    }
    if (o->pcm_path) {
        if (!strcmp(o->pcm_path, "-")) {
            g_sink.pcm = stdout;
        } else {
            wchar_t *w = path_to_wine(utf8_to_utf16(o->pcm_path));
            g_sink.pcm = _wfopen(w, L"wb");
            free(w);
            if (!g_sink.pcm) {
                fprintf(stderr, "cannot write %s\n", o->pcm_path);
                return -1;
            }
        }
    }

    memset(&instance, 0, sizeof instance);
    CHECK(ve_open(g_speech, g_install.hHeap, g_install.hLog, &instance), "open");

    memset(&dev, 0, sizeof dev);
    dev.pfOutNotify = on_audio;
    CHECK(ve_setCallback(instance, &dev), "setCallback");

    params_begin(9, params);
    param_str(&params[n++], PARAM_VOICE, o->voice);
    param_str(&params[n++], PARAM_VOICE_OPERATING_POINT, o->vop);
    if (o->language)
        param_str(&params[n++], PARAM_LANGUAGE, o->language);
    param_int(&params[n++], PARAM_MARKER_MODE, VE_MRK_ON);
    param_int(&params[n++], PARAM_INITMODE, VE_INITMODE_LOAD_ONCE_OPEN_ALL);
    param_int(&params[n++], PARAM_TEXTMODE, VE_TEXTMODE_STANDARD);
    param_int(&params[n++], PARAM_TYPE_OF_CHAR, VE_TYPE_OF_CHAR_UTF8);
    param_int(&params[n++], PARAM_READMODE, o->char_mode ? READMODE_CHAR : READMODE_SENT);
    param_int(&params[n++], PARAM_FREQUENCY, 22);
    param_int(&params[n++], PARAM_DISABLE_FINAL_SILENCE, 0);
    CHECK(ve_setParamList(instance, params, (uint16_t)n), "setParamList");

    n = 0;
    if (o->have_rate)
        param_int(&params[n++], PARAM_SPEECHRATE, o->rate);
    if (o->have_pitch)
        param_int(&params[n++], PARAM_PITCH, o->pitch);
    if (o->have_volume)
        param_int(&params[n++], PARAM_VOLUME, o->volume);
    if (o->have_waitfactor)
        param_int(&params[n++], PARAM_WAITFACTOR, o->waitfactor);
    if (n)
        CHECK(ve_setParamList(instance, params, (uint16_t)n), "setParamList(voice)");

    n = 0;
    param_int(&query[n++], PARAM_SPEECHRATE, 0);
    param_int(&query[n++], PARAM_PITCH, 0);
    param_int(&query[n++], PARAM_VOLUME, 0);
    param_int(&query[n++], PARAM_WAITFACTOR, 0);
    param_int(&query[n++], PARAM_FREQUENCY, 0);
    param_int(&query[n++], PARAM_TIMBRE, 0);
    if (ve_getParamList(instance, query, (uint16_t)n) == NUAN_OK)
        debug_log(1, "effective params rate=%u pitch=%u volume=%u "
                  "waitfactor=%u frequency=%u timbre=%u",
                  query[0].uValue.usValue, query[1].uValue.usValue,
                  query[2].uValue.usValue, query[3].uValue.usValue,
                  query[4].uValue.usValue, query[5].uValue.usValue);

    memset(&in_text, 0, sizeof in_text);
    in_text.eTextFormat = 0;
    in_text.cntTextLength = strlen(o->text);
    if (in_text.cntTextLength) {
        uint8_t *copy = malloc(in_text.cntTextLength);
        memcpy(copy, o->text, in_text.cntTextLength);
        in_text.szInText = copy;
        CHECK(ve_speak(instance, &in_text), "speak");
        free(copy);
    }

    while (!InterlockedCompareExchange(&g_sink.done, 1, 1) && waited < o->timeout_ms) {
        Sleep(20);
        waited += 20;
    }
    if (!g_sink.done)
        fprintf(stderr, "engine did not report end of processing\n");

    ve_stop(instance);
    ve_close(instance);

    if (g_sink.wav) {
        patch_wav_header(g_sink.wav, g_sink.frames, g_sink.rate);
        fclose(g_sink.wav);
    }
    if (g_sink.pcm && g_sink.pcm != stdout)
        fclose(g_sink.pcm);
    else if (g_sink.pcm)
        fflush(g_sink.pcm);
    fprintf(stderr, "ok frames=%lu\n", g_sink.frames);
    return rc;
}

/* The resident server shares the engine glue above rather than duplicating
 * it, so it is a separate file only for readability. */
#include "cerence-server.c"

/* --------------------------------------------------------------------- CLI */

static void usage(void)
{
    fprintf(stderr,
        "usage: wine-bridge-speakup-cerence.exe [--lib-dir DIR] [--data-dir DIR] [--store DIR] [options] COMMAND\n"
        "\n"
        "Loads the bundled Cerence Embedded engine.\n"
        "\n"
        "commands:\n"
        "  --list-languages            list installed language codes and names\n"
        "  --list-voices LANG          list voices and operating points for LANG\n"
        "  --speak TEXT                synthesize TEXT (UTF-8)\n"
        "  --serve                     stay resident and speak framed requests\n"
        "  --status                    report license state\n"
        "  --activate-trial            activate a zero-key online trial\n"
        "  --activate KEY              activate an online license key\n"
        "  --offline-request KEY FILE  write a machine identification file\n"
        "  --offline-activate KEY FILE activate with a license file\n"
        "\n"
        "options:\n"
        "  --lib-dir DIR               Cerence DLLs (default ../lib beside bin)\n"
        "  --data-dir DIR              common data (default ../lib/data beside bin)\n"
        "  --store DIR                 voices (default XDG data directory)\n"
        "  --voice NAME                voice name, default Tian-Tian\n"
        "  --vop OP                    operating point, default embedded-pro\n"
        "  --language LANG             force the engine language, e.g. Chinese Mandarin\n"
        "  --wav FILE                  write 16-bit mono 22050 Hz WAV output\n"
        "  --pcm FILE                  write headerless 16-bit mono PCM output\n"
        "                              (use - for standard output)\n"        "  --rate N --pitch N --volume N --waitfactor N\n"
        "  --char-mode                 read the input one character at a time\n"
        "  --timeout-ms N              synthesis timeout, default 60000\n"
        "  --addon-name NAME           engine DLL base name\n"
        "  --debug LEVEL               diagnostics level, 1 through 3\n"
        "\n"
        "All paths may be Unix paths; Wine exposes them through its Z: drive.\n");
}

int wmain(int argc, wchar_t **wargv)
{
    const char *lib_dir_arg = NULL;
    const char *data_dir_arg = NULL;
    const char *voices_dir = NULL;
    const char *addon_name = NULL;
    const char *command = NULL;
    struct speak_opts opts;
    wchar_t *libdir = NULL;
    wchar_t *lowlevel = NULL;
    wchar_t *data_dir = NULL;
    wchar_t *store = NULL;
    wchar_t root[4096], default_lib[4096], default_data[4096], default_store[4096];
    char default_store_unix[4096], libdir_display[4096];
    const wchar_t *dirs[3];
    size_t ndirs = 0;
    char *text_arg = NULL, *cmd_arg = NULL;
    int i, ret = 0;

    /* Wine opens standard output in text mode, which turns every newline into
     * CRLF.  Machine-readable listings and raw PCM both want the bytes to
     * survive untouched. */
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);

    memset(&opts, 0, sizeof opts);
    opts.voice = "Tian-Tian";
    opts.vop = "embedded-pro";
    opts.timeout_ms = 60000;

    for (i = 1; i < argc; i++) {
        const char *a = utf16_to_utf8(wargv[i]);
        const char *next = (i + 1 < argc) ? utf16_to_utf8(wargv[i + 1]) : NULL;
        int take = 1;

#define NEED()                                                              \
        do {                                                                \
            if (!next) {                                                    \
                fprintf(stderr, "%s needs a value\n", a);                   \
                return 2;                                                   \
            }                                                               \
        } while (0)

        if (!strcmp(a, "--lib-dir")) { NEED(); lib_dir_arg = next; }
        else if (!strcmp(a, "--data-dir")) { NEED(); data_dir_arg = next; }
        else if (!strcmp(a, "--store")) { NEED(); voices_dir = next; }
        else if (!strcmp(a, "--addon-name")) { NEED(); addon_name = next; }
        else if (!strcmp(a, "--voice")) { NEED(); opts.voice = next; }
        else if (!strcmp(a, "--vop")) { NEED(); opts.vop = next; }
        else if (!strcmp(a, "--language")) { NEED(); opts.language = next; }
        else if (!strcmp(a, "--wav")) { NEED(); opts.wav_path = next; }
        else if (!strcmp(a, "--pcm")) { NEED(); opts.pcm_path = next; }
        else if (!strcmp(a, "--rate")) { NEED(); opts.rate = atoi(next); opts.have_rate = 1; }
        else if (!strcmp(a, "--pitch")) { NEED(); opts.pitch = atoi(next); opts.have_pitch = 1; }
        else if (!strcmp(a, "--volume")) { NEED(); opts.volume = atoi(next); opts.have_volume = 1; }
        else if (!strcmp(a, "--waitfactor")) { NEED(); opts.waitfactor = atoi(next); opts.have_waitfactor = 1; }
        else if (!strcmp(a, "--timeout-ms")) { NEED(); opts.timeout_ms = atoi(next); }
        else if (!strcmp(a, "--debug")) {
            char *end;
            long level;
            NEED();
            level = strtol(next, &end, 10);
            if (*end || level < 1 || level > 3) {
                fprintf(stderr, "--debug level must be 1, 2, or 3\n");
                return 2;
            }
            g_debug_level = (int)level;
        }
        else if (!strcmp(a, "--char-mode")) { opts.char_mode = 1; take = 0; }
        else if (!strcmp(a, "--list-languages")) { command = "languages"; take = 0; }
        else if (!strcmp(a, "--list-voices")) { NEED(); command = "voices"; cmd_arg = (char *)next; }
        else if (!strcmp(a, "--status")) { command = "status"; take = 0; }
        else if (!strcmp(a, "--activate-trial")) { command = "trial"; take = 0; }
        else if (!strcmp(a, "--activate")) { NEED(); command = "activate"; text_arg = (char *)next; }
        else if (!strcmp(a, "--offline-request")) {
            if (i + 2 >= argc) {
                fprintf(stderr, "--offline-request needs a key and an output file\n");
                return 2;
            }
            command = "offline-request";
            text_arg = (char *)next;
            cmd_arg = utf16_to_utf8(wargv[i + 2]);
            take = 2;
        }
        else if (!strcmp(a, "--offline-activate")) {
            if (i + 2 >= argc) {
                fprintf(stderr, "--offline-activate needs a key and a licence file\n");
                return 2;
            }
            command = "offline-activate";
            text_arg = (char *)next;
            cmd_arg = utf16_to_utf8(wargv[i + 2]);
            take = 2;
        }
        else if (!strcmp(a, "--serve")) { command = "serve"; take = 0; }
        else if (!strcmp(a, "--speak")) {
            NEED();
            command = "speak";
            text_arg = (char *)next;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else {
            fprintf(stderr, "unknown option %s\n", a);
            usage();
            return 2;
        }
        i += take;
    }

    if (executable_root(root, sizeof root / sizeof root[0]) < 0) {
        fprintf(stderr, "cannot locate the installation root from this executable\n");
        return 1;
    }
    _snwprintf(default_lib, 4095, L"%ls\\lib", root);
    _snwprintf(default_data, 4095, L"%ls\\lib\\data", root);
    {
        const char *configured = getenv("SPEAKUP_CERENCE_VOICE_STORE");
        const char *data_home = getenv("WINE_HOST_XDG_DATA_HOME");
        const char *home = getenv("WINE_HOST_HOME");
        const wchar_t *local_app_data = _wgetenv(L"LOCALAPPDATA");

        if (!data_home || !*data_home)
            data_home = getenv("XDG_DATA_HOME");
        if (!home || !*home)
            home = getenv("HOME");

        if (configured && *configured) {
            snprintf(default_store_unix, sizeof default_store_unix, "%s", configured);
            store = path_to_wine(utf8_to_utf16(default_store_unix));
        } else if (data_home && *data_home) {
            snprintf(default_store_unix, sizeof default_store_unix,
                     "%s/speakup-cerence/voices", data_home);
            store = path_to_wine(utf8_to_utf16(default_store_unix));
        } else if (home && *home) {
            snprintf(default_store_unix, sizeof default_store_unix,
                     "%s/.local/share/speakup-cerence/voices", home);
            store = path_to_wine(utf8_to_utf16(default_store_unix));
        } else if (local_app_data && *local_app_data) {
            _snwprintf(default_store, 4095,
                       L"%ls\\speakup-cerence\\voices", local_app_data);
            store = _wcsdup(default_store);
        } else {
            _snwprintf(default_store, 4095, L"%ls\\voices", root);
            store = _wcsdup(default_store);
        }
    }

    libdir = lib_dir_arg ? path_to_wine(utf8_to_utf16(lib_dir_arg)) : _wcsdup(default_lib);
    lowlevel = libdir;
    data_dir = data_dir_arg ? path_to_wine(utf8_to_utf16(data_dir_arg)) : _wcsdup(default_data);
    if (voices_dir) {
        free(store);
        store = path_to_wine(utf8_to_utf16(voices_dir));
    }
    if (lib_dir_arg) {
        _snprintf(libdir_display, sizeof libdir_display, "%s", lib_dir_arg);
    } else {
        char *display = utf16_to_utf8(default_lib);
        _snprintf(libdir_display, sizeof libdir_display, "%s", display ? display : "../lib");
        free(display);
    }

    debug_log(1, "command=%s lib=%ls data=%ls voices=%ls",
              command ? command : "none", libdir, data_dir,
              store ? store : L"(none)");
    if (!addon_name) {
        addon_name = discover_addon_name(libdir);
        if (!addon_name) {
            fprintf(stderr, "no runatts engine DLL found in %s\n", libdir_display);
            return 1;
        }
    }
    if (load_engine(libdir, libdir_display, addon_name) < 0)
        return 1;

    /* Licensing never needs the voice data, and the engine refuses to open
     * any data until the licence manager reports a licence. */
    if (!strcmp(command ? command : "", "status"))
        return license_status() == 0 ? 0 : 1;
    if (!strcmp(command ? command : "", "trial"))
        return license_activate("", NULL, NULL) == 0 ? 0 : 1;
    if (!strcmp(command ? command : "", "activate"))
        return license_activate(text_arg, NULL, NULL) == 0 ? 0 : 1;
    if (!strcmp(command ? command : "", "offline-request"))
        return license_activate(text_arg, NULL, utf8_to_utf16(cmd_arg)) == 0 ? 0 : 1;
    if (!strcmp(command ? command : "", "offline-activate"))
        return license_activate(text_arg, cmd_arg, NULL) == 0 ? 0 : 1;

    if (!command) {
        usage();
        return 2;
    }

    dirs[ndirs++] = lowlevel;
    dirs[ndirs++] = data_dir;
    if (store)
        dirs[ndirs++] = store;

    debug_log(1, "using engine add-on %s", addon_name);
    if (lock_engine() < 0)
        return 1;
    if (engine_open_data(dirs, ndirs) < 0)
        return 1;

    if (!strcmp(command, "languages"))
        ret = cmd_list_languages() == 0 ? 0 : 1;
    else if (!strcmp(command, "serve"))
        ret = cmd_serve(opts.voice, opts.vop) == 0 ? 0 : 1;
    else if (!strcmp(command, "voices"))
        ret = cmd_list_voices(cmd_arg) == 0 ? 0 : 1;
    else if (!strcmp(command, "speak")) {
        opts.text = text_arg;
        ret = cmd_speak(&opts) == 0 ? 0 : 1;
    } else {
        fprintf(stderr, "command %s is not supported yet\n", command);
        ret = 2;
    }
    engine_close();
    return ret;
}
