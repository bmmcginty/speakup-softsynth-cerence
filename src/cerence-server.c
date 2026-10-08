/*
 * Cerence server: keep the engine and a voice resident and stream audio
 * to a long-lived client.
 *
 * This file is included by host.c, because it shares that file's engine glue,
 * structure definitions and parameter helpers rather than duplicating them.
 *
 * The one-shot --speak path pays for Wine process start, DLL loading and
 * engine initialisation on every utterance, which measured about 300 ms.  The
 * engine itself synthesizes at roughly a hundred times real time and opens a
 * voice in about 14 ms, so a resident engine turns that into roughly 15 ms.
 *
 * The protocol is a stream of frames, each a one byte type followed by a four
 * byte little-endian length and that many bytes of payload.  It is
 * deliberately binary-safe so PCM and UTF-8 text can share one pipe.
 *
 *   client -> host   'S' text          synthesize this text
 *                    'P' key=value\n   set voice, vop, rate, pitch, volume or
 *                                      waitfactor; applied before the next
 *                                      utterance
 *                    'L'               list installed languages and voices
 *                    'C'               cancel the utterance in progress
 *                    'Q'               quit
 *
 *   host -> client   'R' rate          engine is ready, in samples per second
 *                    'A' pcm           raw 16-bit mono PCM at that rate
 *                    'M' name          a bookmark mark was reached
 *                    'D'               utterance finished
 *                    'V' lang\tvoice\toperating point
 *                    'L'               end of the voice list
 *                    'E' message       error
 *
 * Cancellation has to come from a thread other than the one inside speak(),
 * because the audio callback runs on the speaking thread and speak() does not
 * return until the utterance ends.  The reader thread therefore only flips a
 * flag, and the callback returns NUAN_E_TTS_USERSTOP when it sees it, which is
 * the same mechanism the NVDA driver uses.
 */

#define SERVE_FRAME_READY 'R'
#define SERVE_FRAME_AUDIO 'A'
#define SERVE_FRAME_MARK  'M'
#define SERVE_FRAME_DONE  'D'
#define SERVE_FRAME_VOICE 'V'
#define SERVE_FRAME_LIST  'L'
#define SERVE_FRAME_ERROR 'E'

#define SERVE_CMD_SPEAK   'S'
#define SERVE_CMD_PARAMS  'P'
#define SERVE_CMD_LIST    'L'
#define SERVE_CMD_CANCEL  'C'
#define SERVE_CMD_QUIT    'Q'

/* 2048 bytes is 46 ms at 22050 Hz.  The engine reports bookmark marks
 * alongside the audio it has just produced, so a small buffer is what keeps
 * "the index was reached" close to what the listener hears. */
#define SERVE_PCM_BUF 2048
#define SERVE_MARK_BUF 64

struct serve_state {
    CRITICAL_SECTION lock;
    HANDLE wake;                /* auto-reset, one queued command at a time */
    HANDLE thread;
    int quit;
    char *pending_text;         /* owned by the worker once queued */
    int pending_list;
    volatile LONG cancel;

    char voice[VE_MAX_STRING_LENGTH];
    char vop[VE_MAX_STRING_LENGTH];
    int rate, pitch, volume, waitfactor;
    int have_rate, have_pitch, have_volume, have_waitfactor;

    VE_HSAFE instance;
    int instance_open;
    char open_voice[VE_MAX_STRING_LENGTH];
    char open_vop[VE_MAX_STRING_LENGTH];

    unsigned utterance;
    unsigned callbacks;
    unsigned markers;
    unsigned long long audio_bytes;
    ULONGLONG synth_started;
};

static struct serve_state g_serve;
static char g_serve_pcm[SERVE_PCM_BUF];
static VE_MARKINFO g_serve_marks[SERVE_MARK_BUF];

static int frame_write(unsigned char type, const void *payload, uint32_t len)
{
    unsigned char header[5];
    header[0] = type;
    header[1] = (unsigned char)(len & 0xff);
    header[2] = (unsigned char)((len >> 8) & 0xff);
    header[3] = (unsigned char)((len >> 16) & 0xff);
    header[4] = (unsigned char)((len >> 24) & 0xff);
    if (fwrite(header, 1, 5, stdout) != 5)
        return -1;
    if (len && fwrite(payload, 1, len, stdout) != len)
        return -1;
    if (fflush(stdout) != 0)
        return -1;
    return 0;
}

static void frame_error(const char *message)
{
    frame_write(SERVE_FRAME_ERROR, message, (uint32_t)strlen(message));
}

static int frame_read(unsigned char *type, char **payload, uint32_t *len)
{
    unsigned char header[5];
    size_t got;

    got = fread(header, 1, 5, stdin);
    if (got != 5) {
        debug_log(1, "command header read failed: got=%llu eof=%d error=%d",
                  (unsigned long long)got, feof(stdin), ferror(stdin));
        return -1;
    }
    *type = header[0];
    *len = (uint32_t)header[1] | ((uint32_t)header[2] << 8) |
           ((uint32_t)header[3] << 16) | ((uint32_t)header[4] << 24);
    *payload = NULL;
    if (*len) {
        char *buffer = malloc((size_t)*len + 1);
        if (!buffer)
            return -1;
        got = fread(buffer, 1, *len, stdin);
        if (got != *len) {
            debug_log(1, "command payload read failed: type=%c length=%u "
                      "got=%llu eof=%d error=%d", *type, *len,
                      (unsigned long long)got, feof(stdin), ferror(stdin));
            free(buffer);
            return -1;
        }
        buffer[*len] = '\0';
        *payload = buffer;
    }
    return 0;
}

static uint32_t serve_on_audio(VE_HSAFE instance, void *userData, VE_CALLBACKMSG *msg)
{
    VE_OUTDATA *out;
    size_t i;
    (void)instance;
    (void)userData;

    if (!msg)
        return NUAN_OK;
    g_serve.callbacks++;
    debug_log(3, "callback message=0x%08x value=%d", msg->eMessage,
              msg->lValue);
    if (msg->eMessage == VE_MSG_OUTBUFREQ) {
        out = (VE_OUTDATA *)msg->pParam;
        out->pOutPcmBuf = g_serve_pcm;
        out->cntPcmBufLen = sizeof g_serve_pcm;
        out->pMrkList = g_serve_marks;
        out->cntMrkListLen = SERVE_MARK_BUF;
        return NUAN_OK;
    }
    if (msg->eMessage != VE_MSG_OUTBUFDONE)
        return NUAN_OK;

    if (InterlockedCompareExchange(&g_serve.cancel, 0, 0))
        return NUAN_E_TTS_USERSTOP;

    out = (VE_OUTDATA *)msg->pParam;
    g_serve.audio_bytes += out->cntPcmBufLen;
    debug_log(3, "audio bytes=%llu markers=%llu",
              (unsigned long long)out->cntPcmBufLen,
              (unsigned long long)out->cntMrkListLen);
    if (out->cntPcmBufLen) {
        if (frame_write(SERVE_FRAME_AUDIO, out->pOutPcmBuf,
                        (uint32_t)out->cntPcmBufLen) != 0)
            return NUAN_E_TTS_USERSTOP;
    }
    /* The engine reports how many entries the list holds, not how many bytes,
     * which is the opposite of how the list length was requested. */
    for (i = 0; i < out->cntMrkListLen && i < SERVE_MARK_BUF; i++) {
        VE_MARKINFO *mark = &out->pMrkList[i];
        g_serve.markers++;
        debug_log(3, "marker[%llu] type=%u src=%llu+%llu dest=%llu+%u "
                  "us=%u ul=%u",
                  (unsigned long long)i, mark->eMrkType,
                  (unsigned long long)mark->cntSrcPos,
                  (unsigned long long)mark->cntSrcTextLen,
                  (unsigned long long)mark->cntDestPos, mark->cntDestLen,
                  mark->usValue, mark->ulValue);
        if (mark->eMrkType != VE_MRK_BOOKMARK || !mark->szValue)
            continue;
        if (frame_write(SERVE_FRAME_MARK, mark->szValue,
                        (uint32_t)strlen(mark->szValue)) != 0)
            return NUAN_E_TTS_USERSTOP;
    }
    if (InterlockedCompareExchange(&g_serve.cancel, 0, 0))
        return NUAN_E_TTS_USERSTOP;
    return NUAN_OK;
}

static void serve_apply_params(void)
{
    VE_PARAM params[10];
    VE_PARAM query[6];
    VE_OUTDEVINFO dev;
    unsigned rc;
    int n = 0;

    if (g_serve.instance_open &&
        (strcmp(g_serve.open_voice, g_serve.voice) ||
         strcmp(g_serve.open_vop, g_serve.vop))) {
        debug_log(1, "closing voice=%s vop=%s", g_serve.open_voice,
                  g_serve.open_vop);
        ve_close(g_serve.instance);
        g_serve.instance_open = 0;
    }

    if (!g_serve.instance_open) {
        debug_log(1, "opening voice=%s vop=%s", g_serve.voice, g_serve.vop);
        rc = ve_open(g_speech, g_install.hHeap, g_install.hLog,
                     &g_serve.instance);
        if (rc != NUAN_OK) {
            char buffer[256];
            frame_error(errText(rc, buffer, sizeof buffer));
            debug_log(1, "engine open failed: %s",
                      errText(rc, buffer, sizeof buffer));
            return;
        }
        g_serve.instance_open = 1;
        snprintf(g_serve.open_voice, sizeof g_serve.open_voice, "%s", g_serve.voice);
        snprintf(g_serve.open_vop, sizeof g_serve.open_vop, "%s", g_serve.vop);

        params_begin(10, params);
        param_str(&params[n++], PARAM_VOICE, g_serve.voice);
        param_str(&params[n++], PARAM_VOICE_OPERATING_POINT, g_serve.vop);
        param_int(&params[n++], PARAM_MARKER_MODE, VE_MRK_ON);
        param_int(&params[n++], PARAM_INITMODE, VE_INITMODE_LOAD_ONCE_OPEN_ALL);
        param_int(&params[n++], PARAM_TEXTMODE, VE_TEXTMODE_STANDARD);
        param_int(&params[n++], PARAM_TYPE_OF_CHAR, VE_TYPE_OF_CHAR_UTF8);
        param_int(&params[n++], PARAM_READMODE, READMODE_SENT);
        param_int(&params[n++], PARAM_FREQUENCY, 22);
        param_int(&params[n++], PARAM_DISABLE_FINAL_SILENCE, 0);
        rc = ve_setParamList(g_serve.instance, params, (uint16_t)n);
        if (rc != NUAN_OK) {
            char buffer[256];
            frame_error(errText(rc, buffer, sizeof buffer));
            debug_log(1, "engine voice setup failed: %s",
                      errText(rc, buffer, sizeof buffer));
        }

        memset(&dev, 0, sizeof dev);
        dev.pfOutNotify = serve_on_audio;
        rc = ve_setCallback(g_serve.instance, &dev);
        if (rc != NUAN_OK) {
            char buffer[256];
            frame_error(errText(rc, buffer, sizeof buffer));
            debug_log(1, "engine callback setup failed: %s",
                      errText(rc, buffer, sizeof buffer));
        }
    }

    n = 0;
    if (g_serve.have_rate)
        param_int(&params[n++], PARAM_SPEECHRATE, g_serve.rate);
    if (g_serve.have_pitch)
        param_int(&params[n++], PARAM_PITCH, g_serve.pitch);
    if (g_serve.have_volume)
        param_int(&params[n++], PARAM_VOLUME, g_serve.volume);
    if (g_serve.have_waitfactor)
        param_int(&params[n++], PARAM_WAITFACTOR, g_serve.waitfactor);
    debug_log(1, "requested params rate=%s%d pitch=%s%d volume=%s%d "
              "waitfactor=%s%d", g_serve.have_rate ? "" : "unset/",
              g_serve.rate, g_serve.have_pitch ? "" : "unset/", g_serve.pitch,
              g_serve.have_volume ? "" : "unset/", g_serve.volume,
              g_serve.have_waitfactor ? "" : "unset/", g_serve.waitfactor);
    if (n) {
        rc = ve_setParamList(g_serve.instance, params, (uint16_t)n);
        if (rc != NUAN_OK) {
            char buffer[256];
            frame_error(errText(rc, buffer, sizeof buffer));
            debug_log(1, "engine parameter setup failed: %s",
                      errText(rc, buffer, sizeof buffer));
        }
    }
    if (g_debug_level) {
        n = 0;
        params_begin(6, query);
        param_int(&query[n++], PARAM_SPEECHRATE, 0);
        param_int(&query[n++], PARAM_PITCH, 0);
        param_int(&query[n++], PARAM_VOLUME, 0);
        param_int(&query[n++], PARAM_WAITFACTOR, 0);
        param_int(&query[n++], PARAM_FREQUENCY, 0);
        param_int(&query[n++], PARAM_TIMBRE, 0);
        rc = ve_getParamList(g_serve.instance, query, (uint16_t)n);
        if (rc == NUAN_OK)
            debug_log(1, "effective params rate=%u pitch=%u volume=%u "
                      "waitfactor=%u frequency=%u timbre=%u",
                      query[0].uValue.usValue, query[1].uValue.usValue,
                      query[2].uValue.usValue, query[3].uValue.usValue,
                      query[4].uValue.usValue, query[5].uValue.usValue);
        else {
            char buffer[256];
            debug_log(1, "reading effective params failed: %s",
                      errText(rc, buffer, sizeof buffer));
        }
    }
}

static void serve_list_voices(void)
{
    uint16_t n = 0, i, j, k;
    VE_LANGUAGE *langs;
    char line[3 * VE_MAX_STRING_LENGTH + 8];

    if (ve_getLanguages(g_speech, NULL, &n) != NUAN_OK)
        return;
    langs = calloc(n ? n : 1, sizeof(VE_LANGUAGE));
    if (!langs)
        return;
    if (ve_getLanguages(g_speech, langs, &n) != NUAN_OK) {
        free(langs);
        return;
    }
    for (i = 0; i < n; i++) {
        uint16_t nv = 0;
        VE_VOICEINFO *voices;
        if (ve_getVoices(g_speech, langs[i].szLanguage, NULL, &nv) != NUAN_OK)
            continue;
        voices = calloc(nv ? nv : 1, sizeof(VE_VOICEINFO));
        if (!voices)
            continue;
        if (ve_getVoices(g_speech, langs[i].szLanguage, voices, &nv) != NUAN_OK) {
            free(voices);
            continue;
        }
        for (j = 0; j < nv; j++) {
            uint16_t nd = 0;
            VE_SPEECHDBINFO *dbs;
            if (ve_getSpeechDbs(g_speech, langs[i].szLanguage, voices[j].szVoiceName,
                                NULL, &nd) != NUAN_OK)
                continue;
            dbs = calloc(nd ? nd : 1, sizeof(VE_SPEECHDBINFO));
            if (!dbs)
                continue;
            if (ve_getSpeechDbs(g_speech, langs[i].szLanguage, voices[j].szVoiceName,
                                dbs, &nd) == NUAN_OK) {
                for (k = 0; k < nd; k++) {
                    snprintf(line, sizeof line, "%s\t%s\t%s",
                             langs[i].szLanguageTLW, voices[j].szVoiceName,
                             dbs[k].szVoiceOperatingPoint);
                    frame_write(SERVE_FRAME_VOICE, line, (uint32_t)strlen(line));
                }
            }
            free(dbs);
        }
        free(voices);
    }
    free(langs);
    frame_write(SERVE_FRAME_LIST, "", 0);
}

static DWORD WINAPI serve_engine_thread(LPVOID param)
{
    (void)param;
    for (;;) {
        char *text;
        int list;

        if (WaitForSingleObject(g_serve.wake, INFINITE) != WAIT_OBJECT_0)
            break;

        EnterCriticalSection(&g_serve.lock);
        text = g_serve.pending_text;
        list = g_serve.pending_list;
        g_serve.pending_text = NULL;
        g_serve.pending_list = 0;
        if (g_serve.quit) {
            LeaveCriticalSection(&g_serve.lock);
            free(text);
            break;
        }
        LeaveCriticalSection(&g_serve.lock);

        if (list) {
            debug_log(2, "listing installed voices");
            serve_list_voices();
            continue;
        }
        if (!text)
            continue;

        g_serve.utterance++;
        g_serve.callbacks = 0;
        g_serve.markers = 0;
        g_serve.audio_bytes = 0;
        g_serve.synth_started = GetTickCount64();
        debug_log(2, "utterance %u start text-bytes=%llu", g_serve.utterance,
                  (unsigned long long)strlen(text));
        serve_apply_params();
        if (g_serve.instance_open) {
            VE_INTEXT in_text;
            unsigned rc;
            /* Anything a cancel referred to is finished or abandoned by now,
             * so the flag is cleared before this utterance starts. */
            InterlockedExchange(&g_serve.cancel, 0);
            memset(&in_text, 0, sizeof in_text);
            in_text.eTextFormat = 0;
            in_text.cntTextLength = strlen(text);
            in_text.szInText = text;
            rc = ve_speak(g_serve.instance, &in_text);
            if (rc != NUAN_OK && rc != NUAN_E_TTS_USERSTOP) {
                char buffer[256];
                frame_error(errText(rc, buffer, sizeof buffer));
            }
            debug_log(2, "utterance %u end rc=0x%08x elapsed-ms=%llu "
                      "audio-bytes=%llu callbacks=%u markers=%u",
                      g_serve.utterance, rc,
                      (unsigned long long)(GetTickCount64() - g_serve.synth_started),
                      g_serve.audio_bytes, g_serve.callbacks, g_serve.markers);
        }
        free(text);
        frame_write(SERVE_FRAME_DONE, "", 0);
    }
    return 0;
}

static int serve_params_set(char *payload)
{
    char *line = payload;
    while (line && *line) {
        char *next = strchr(line, '\n');
        char *eq;
        if (next)
            *next++ = '\0';
        eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            if (!strcmp(line, "voice"))
                snprintf(g_serve.voice, sizeof g_serve.voice, "%s", eq + 1);
            else if (!strcmp(line, "vop"))
                snprintf(g_serve.vop, sizeof g_serve.vop, "%s", eq + 1);
            else if (!strcmp(line, "rate")) {
                g_serve.rate = atoi(eq + 1);
                g_serve.have_rate = 1;
            } else if (!strcmp(line, "pitch")) {
                g_serve.pitch = atoi(eq + 1);
                g_serve.have_pitch = 1;
            } else if (!strcmp(line, "volume")) {
                g_serve.volume = atoi(eq + 1);
                g_serve.have_volume = 1;
            } else if (!strcmp(line, "waitfactor")) {
                g_serve.waitfactor = atoi(eq + 1);
                g_serve.have_waitfactor = 1;
            }
        }
        line = next;
    }
    debug_log(2, "parameters voice=%s vop=%s rate=%s%d pitch=%s%d "
              "volume=%s%d waitfactor=%s%d", g_serve.voice, g_serve.vop,
              g_serve.have_rate ? "" : "unset/", g_serve.rate,
              g_serve.have_pitch ? "" : "unset/", g_serve.pitch,
              g_serve.have_volume ? "" : "unset/", g_serve.volume,
              g_serve.have_waitfactor ? "" : "unset/", g_serve.waitfactor);
    return 0;
}

static int cmd_serve(const char *voice, const char *vop)
{
    unsigned char type;
    char *payload;
    uint32_t len;

    memset(&g_serve, 0, sizeof g_serve);
    InitializeCriticalSection(&g_serve.lock);
    g_serve.wake = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_serve.wake) {
        fprintf(stderr, "cannot create the wake event\n");
        return -1;
    }
    snprintf(g_serve.voice, sizeof g_serve.voice, "%s", voice ? voice : "Tian-Tian");
    snprintf(g_serve.vop, sizeof g_serve.vop, "%s", vop ? vop : "embedded-pro");
    debug_log(1, "resident server starting default-voice=%s default-vop=%s",
              g_serve.voice, g_serve.vop);

    /* The ready frame goes out before the worker starts, so nothing else can
     * interleave with it on standard output. */
    frame_write(SERVE_FRAME_READY, "22050", 5);

    g_serve.thread = CreateThread(NULL, 0, serve_engine_thread, NULL, 0, NULL);
    if (!g_serve.thread) {
        fprintf(stderr, "cannot create the engine thread\n");
        return -1;
    }

    while (frame_read(&type, &payload, &len) == 0) {
        debug_log(3, "command type=%c length=%u", type, len);
        if (type == SERVE_CMD_QUIT) {
            free(payload);
            break;
        }
        if (type == SERVE_CMD_CANCEL) {
            InterlockedExchange(&g_serve.cancel, 1);
            free(payload);
            continue;
        }
        if (type == SERVE_CMD_PARAMS) {
            if (payload)
                serve_params_set(payload);
            free(payload);
            continue;
        }
        if (type == SERVE_CMD_LIST) {
            EnterCriticalSection(&g_serve.lock);
            g_serve.pending_list = 1;
            LeaveCriticalSection(&g_serve.lock);
            SetEvent(g_serve.wake);
            free(payload);
            continue;
        }
        if (type == SERVE_CMD_SPEAK) {
            EnterCriticalSection(&g_serve.lock);
            free(g_serve.pending_text);
            g_serve.pending_text = payload ? payload : strdup("");
            LeaveCriticalSection(&g_serve.lock);
            SetEvent(g_serve.wake);
            continue;
        }
        free(payload);
    }

    EnterCriticalSection(&g_serve.lock);
    g_serve.quit = 1;
    LeaveCriticalSection(&g_serve.lock);
    SetEvent(g_serve.wake);
    WaitForSingleObject(g_serve.thread, 10000);
    CloseHandle(g_serve.thread);
    CloseHandle(g_serve.wake);
    if (g_serve.instance_open)
        ve_close(g_serve.instance);
    DeleteCriticalSection(&g_serve.lock);
    debug_log(1, "resident server stopped");
    return 0;
}
