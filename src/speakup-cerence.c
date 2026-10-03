#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "voice-list.h"

#define RATE 22050
#define AUDIO_CAP (RATE * 2 * 30)
#define FRAME_AUDIO 'A'
#define FRAME_MARK 'M'
#define FRAME_DONE 'D'
#define FRAME_VOICE 'V'
#define FRAME_LIST 'L'
#define FRAME_ERROR 'E'
#define CMD_SPEAK 'S'
#define CMD_PARAMS 'P'
#define CMD_LIST 'L'
#define CMD_CANCEL 'C'
#define CMD_QUIT 'Q'

static volatile sig_atomic_t stopping;
static int synth_fd = -1;
static char installation_root[4096];
static struct voice_list voices;
static const struct voice_choice *active_voice;

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("speakup-cerence: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static void find_installation_root(void)
{
    ssize_t length = readlink("/proc/self/exe", installation_root,
                              sizeof installation_root - 1);
    char *slash;

    if (length < 0)
        die("cannot locate this executable: %s", strerror(errno));
    installation_root[length] = '\0';
    slash = strrchr(installation_root, '/');
    if (!slash)
        die("executable path has no parent directory");
    *slash = '\0'; /* bin */
    slash = strrchr(installation_root, '/');
    if (!slash)
        die("executable is not inside an installation directory");
    *slash = '\0';
}

static const char *root_path(const char *env, const char *suffix, char *buf, size_t size)
{
    const char *value = getenv(env);
    if (value && *value)
        return value;
    if (snprintf(buf, size, "%s/%s", installation_root, suffix) >= (int)size)
        die("installation path is too long");
    return buf;
}

struct filters { const char *lang, *quality; };

static const char *quality_name(const char *value)
{
    if (!value) return NULL;
    if (!strcasecmp(value, "compact") || !strcasecmp(value, "embedded-compact") || !strcasecmp(value, "lowest")) return "lowest";
    if (!strcasecmp(value, "pro") || !strcasecmp(value, "embedded-pro") || !strcasecmp(value, "intermediate")) return "intermediate";
    if (!strcasecmp(value, "high") || !strcasecmp(value, "embedded-high") || !strcasecmp(value, "enhanced")) return "enhanced";
    if (!strcasecmp(value, "premium") || !strcasecmp(value, "embedded-premium") || !strcasecmp(value, "premium-high") || !strcasecmp(value, "highest")) return "highest";
    return value;
}

static const char *voice_store_path(char *buf, size_t size)
{
    const char *store = getenv("SPEAKUP_CERENCE_VOICE_STORE");
    const char *data_home;
    const char *home;

    if (store && *store)
        return store;
    data_home = getenv("XDG_DATA_HOME");
    if (data_home && *data_home) {
        if (snprintf(buf, size, "%s/speakup-cerence/voices", data_home) >= (int)size)
            die("voice store path is too long");
        return buf;
    }
    home = getenv("HOME");
    if (!home || !*home)
        die("HOME is unset; set SPEAKUP_CERENCE_VOICE_STORE explicitly");
    if (snprintf(buf, size, "%s/.local/share/speakup-cerence/voices", home) >= (int)size)
        die("voice store path is too long");
    return buf;
}

/* -------------------------------------------------------------- host IPC */
struct host { pid_t pid; int in, out; pthread_mutex_t lock; } host = { -1, -1, -1, PTHREAD_MUTEX_INITIALIZER };
static int write_all(int fd, const void *p, size_t n) { const char *s=p; while(n){ssize_t r=write(fd,s,n);if(r<0){if(errno==EINTR)continue;return -1;}s+=r;n-=r;}return 0; }
static int read_all(int fd, void *p, size_t n) { char *s=p; while(n){ssize_t r=read(fd,s,n);if(r<0){if(errno==EINTR)continue;return -1;}if(!r)return -1;s+=r;n-=r;}return 0; }
static int send_frame(char type, const void *data, uint32_t n) { unsigned char h[5]={type,n,n>>8,n>>16,n>>24}; int rc; pthread_mutex_lock(&host.lock); rc=write_all(host.in,h,5)||(n&&write_all(host.in,data,n)); pthread_mutex_unlock(&host.lock); return rc?-1:0; }
static int read_frame(char *type, char **data, uint32_t *n) { unsigned char h[5]; if(read_all(host.out,h,5))return -1;*type=h[0];*n=h[1]|h[2]<<8|h[3]<<16|h[4]<<24;*data=malloc(*n+1);if(!*data)die("out of memory");if(*n&&read_all(host.out,*data,*n)){free(*data);return -1;}(*data)[*n]=0;return 0; }

static void spawn_host(void)
{
    int a[2],b[2]; char exe[4096],lib[4096],data[4096],store[4096]; const char *wine=getenv("WINE"); char type,*payload;uint32_t n;
    if (!wine || !*wine)
        wine = "wine";
    root_path("SPEAKUP_CERENCE_BRIDGE", "bin/wine-bridge-speakup-cerence.exe", exe, sizeof exe);
    root_path("SPEAKUP_CERENCE_LIB", "lib", lib, sizeof lib);
    root_path("SPEAKUP_CERENCE_DATA", "lib/data", data, sizeof data);
    voice_store_path(store, sizeof store);
    if (pipe(a) || pipe(b))
        die("pipe: %s", strerror(errno));
    host.pid = fork();
    if (host.pid < 0)
        die("fork: %s", strerror(errno));
    if(!host.pid){dup2(a[0],0);dup2(b[1],1);close(a[0]);close(a[1]);close(b[0]);close(b[1]);setenv("WINEDEBUG","-all",0);execlp(wine,wine,exe,"--lib-dir",lib,"--data-dir",data,"--store",store,"--serve",NULL);_exit(127);}
    close(a[0]);close(b[1]);host.in=a[1];host.out=b[0]; if(read_frame(&type,&payload,&n)||type!='R')die("Wine bridge did not become ready");free(payload);
}

/* ------------------------------------------------------------- PipeWire */
struct mark { uint64_t at; int value; struct mark *next; };
struct audio_state { struct pw_thread_loop *loop; struct pw_context *context; struct pw_core *core; struct pw_stream *stream; struct spa_hook listener; pthread_mutex_t lock; pthread_cond_t space; unsigned char data[AUDIO_CAP]; size_t rd, used; uint64_t written, played; struct mark *marks; } audio = { .lock=PTHREAD_MUTEX_INITIALIZER, .space=PTHREAD_COND_INITIALIZER };

static void audio_process(void *unused)
{
    struct pw_buffer *pb=pw_stream_dequeue_buffer(audio.stream); struct spa_buffer *buf; struct spa_data *d; size_t want,first; (void)unused;
    if (!pb)
        return;
    buf = pb->buffer;
    d = &buf->datas[0];
    want = d->maxsize;
    pthread_mutex_lock(&audio.lock);
    if (want > audio.used)
        want = audio.used;
    first = want;
    if (first > AUDIO_CAP - audio.rd)
        first = AUDIO_CAP - audio.rd;
    memcpy(d->data, audio.data + audio.rd, first);
    memcpy((char *)d->data + first, audio.data, want - first);
    audio.rd = (audio.rd + want) % AUDIO_CAP;
    audio.used -= want;
    audio.played += want / 2;
    d->chunk->offset = 0;
    d->chunk->stride = 2;
    d->chunk->size = want;
    while(audio.marks&&audio.marks->at<=audio.played){struct mark*m=audio.marks;audio.marks=m->next;if(synth_fd>=0){char s[24];int n=snprintf(s,sizeof s,"%d",m->value);write(synth_fd,s,n);}free(m);}pthread_cond_broadcast(&audio.space);pthread_mutex_unlock(&audio.lock);pw_stream_queue_buffer(audio.stream,pb);
}
static const struct pw_stream_events stream_events={PW_VERSION_STREAM_EVENTS,.process=audio_process};
static void audio_init(int *argc,char ***argv)
{
    struct spa_audio_info_raw info={.format=SPA_AUDIO_FORMAT_S16_LE,.rate=RATE,.channels=1,.position={SPA_AUDIO_CHANNEL_MONO}};uint8_t buffer[1024];struct spa_pod_builder b=SPA_POD_BUILDER_INIT(buffer,sizeof buffer);const struct spa_pod *params[1];
    pw_init(argc,argv);audio.loop=pw_thread_loop_new("speakup-cerence",NULL);audio.context=pw_context_new(pw_thread_loop_get_loop(audio.loop),NULL,0);audio.core=pw_context_connect(audio.context,NULL,0);audio.stream=pw_stream_new(audio.core,"Speakup Cerence",pw_properties_new(PW_KEY_MEDIA_TYPE,"Audio",PW_KEY_MEDIA_CATEGORY,"Playback",PW_KEY_MEDIA_ROLE,"Accessibility",NULL));pw_stream_add_listener(audio.stream,&audio.listener,&stream_events,NULL);params[0]=spa_format_audio_raw_build(&b,SPA_PARAM_EnumFormat,&info);if(pw_stream_connect(audio.stream,PW_DIRECTION_OUTPUT,PW_ID_ANY,PW_STREAM_FLAG_AUTOCONNECT|PW_STREAM_FLAG_MAP_BUFFERS,params,1)<0)die("cannot connect to PipeWire");if(pw_thread_loop_start(audio.loop)<0)die("cannot start PipeWire");
}
static int audio_write(const void *p,size_t n,unsigned generation,volatile unsigned *current)
{const unsigned char*s=p;while(n){size_t room,take,end;pthread_mutex_lock(&audio.lock);while(audio.used==AUDIO_CAP&&generation==*current&&!stopping)pthread_cond_wait(&audio.space,&audio.lock);if(generation!=*current||stopping){pthread_mutex_unlock(&audio.lock);return -1;}room=AUDIO_CAP-audio.used;take=n<room?n:room;end=(audio.rd+audio.used)%AUDIO_CAP;if(take>AUDIO_CAP-end)take=AUDIO_CAP-end;memcpy(audio.data+end,s,take);audio.used+=take;audio.written+=take/2;pthread_mutex_unlock(&audio.lock);s+=take;n-=take;}return 0;}
static void audio_mark(int value){struct mark*m=calloc(1,sizeof*m),**tail;pthread_mutex_lock(&audio.lock);m->at=audio.written;m->value=value;tail=&audio.marks;while(*tail)tail=&(*tail)->next;*tail=m;pthread_mutex_unlock(&audio.lock);}
static void audio_flush(void){struct mark*m,*next;pthread_mutex_lock(&audio.lock);audio.rd=(audio.rd+audio.used)%AUDIO_CAP;audio.used=0;audio.played=audio.written;for(m=audio.marks;m;m=next){next=m->next;free(m);}audio.marks=NULL;pthread_cond_broadcast(&audio.space);pthread_mutex_unlock(&audio.lock);}

/* -------------------------------------------------------------- synthesis */
enum item_type { ITEM_TEXT, ITEM_MARK, ITEM_PARAMS };
struct item { enum item_type type; char *text; int value; struct item *next; };
static struct item *head,*tail;static pthread_mutex_t queue_lock=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t queue_ready=PTHREAD_COND_INITIALIZER;static volatile unsigned generation;
static void enqueue(enum item_type type,const char*text,int value){struct item*i=calloc(1,sizeof*i);i->type=type;i->text=text?strdup(text):NULL;i->value=value;pthread_mutex_lock(&queue_lock);if(tail)tail->next=i;else head=i;tail=i;pthread_cond_signal(&queue_ready);pthread_mutex_unlock(&queue_lock);}
static void clear_queue(void){struct item*i,*n;pthread_mutex_lock(&queue_lock);for(i=head;i;i=n){n=i->next;free(i->text);free(i);}head=tail=NULL;generation++;pthread_cond_broadcast(&queue_ready);pthread_mutex_unlock(&queue_lock);audio_flush();send_frame(CMD_CANCEL,"",0);}
static void *worker(void *unused){(void)unused;while(!stopping){struct item*i;unsigned gen;pthread_mutex_lock(&queue_lock);while(!head&&!stopping)pthread_cond_wait(&queue_ready,&queue_lock);i=head;if(i){head=i->next;if(!head)tail=NULL;}gen=generation;pthread_mutex_unlock(&queue_lock);if(!i)continue;if(i->type==ITEM_MARK)audio_mark(i->value);else if(i->type==ITEM_PARAMS)send_frame(CMD_PARAMS,i->text,strlen(i->text));else{char type,*p;uint32_t n;send_frame(CMD_SPEAK,i->text,strlen(i->text));do{if(read_frame(&type,&p,&n)){stopping=1;break;}if(type==FRAME_AUDIO)audio_write(p,n,gen,&generation);else if(type==FRAME_MARK)audio_mark(atoi(p));else if(type==FRAME_ERROR)fprintf(stderr,"Cerence: %s\n",p);free(p);}while(type!=FRAME_DONE&&!stopping);}free(i->text);free(i);}return NULL;}

static int quality_rank(const char *quality)
{
    return !strcmp(quality, "highest") ? 3 :
           !strcmp(quality, "enhanced") ? 2 :
           !strcmp(quality, "intermediate") ? 1 : 0;
}

static void activate_voice(int number)
{
    const struct voice_choice *choice = voice_list_select(&voices, number);
    char params[300];

    if (!choice || choice == active_voice)
        return;
    snprintf(params, sizeof params, "voice=%s\nvop=%s\n", choice->name,
             choice->operating_point);
    enqueue(ITEM_PARAMS, params, 0);
    active_voice = choice;
    fprintf(stderr, "Using voice %zu: %s (%s)\n",
            (size_t)(choice - voices.items) + 1, choice->name,
            choice->operating_point);
}

static void select_voices(const struct filters *filters)
{
    char type, *payload;
    uint32_t length;

    send_frame(CMD_LIST, "", 0);
    while (!read_frame(&type, &payload, &length)) {
        if (type == FRAME_LIST) {
            free(payload);
            break;
        }
        if (type == FRAME_VOICE) {
            char *name = strchr(payload, '\t');
            char *operating_point = name ? strchr(name + 1, '\t') : NULL;
            if (name && operating_point) {
                const char *quality;
                *name++ = '\0';
                *operating_point++ = '\0';
                quality = quality_name(operating_point);
                if ((!filters->lang || !strcasecmp(payload, filters->lang)) &&
                    (!filters->quality ||
                     !strcasecmp(quality, quality_name(filters->quality))) &&
                    voice_list_add(&voices, payload, name, operating_point,
                                   quality_rank(quality)) < 0)
                    die("out of memory");
            }
        }
        free(payload);
    }
    if (!voices.count)
        die("no installed voice has all requested attributes");
    voice_list_sort(&voices);
    activate_voice(1);
}

static void process_bytes(char *buf,ssize_t n)
{ssize_t i=0,start=0;while(i<n){unsigned char c=buf[i];if(c==0x18){if(i>start){char save=buf[i];buf[i]=0;enqueue(ITEM_TEXT,buf+start,0);buf[i]=save;}clear_queue();i++;start=i;continue;}if(c==1){ssize_t j=i+1;int sign=0,value=0;if(i>start){char save=buf[i];buf[i]=0;enqueue(ITEM_TEXT,buf+start,0);buf[i]=save;}if(j<n&&(buf[j]=='+'||buf[j]=='-'))sign=buf[j++];while(j<n&&buf[j]>='0'&&buf[j]<='9')value=value*10+buf[j++]-'0';if(j>=n)break;switch(buf[j]){case'i':enqueue(ITEM_MARK,NULL,value);break;case's':{char p[64];int r=value*100/5;if(r<50)r=50;if(r>400)r=400;snprintf(p,sizeof p,"rate=%d\n",r);enqueue(ITEM_PARAMS,p,0);break;}case'p':{char p[64];int v=50+value*3/2;if(v>200)v=200;snprintf(p,sizeof p,"pitch=%d\n",v);enqueue(ITEM_PARAMS,p,0);break;}case'v':{char p[64];int v=value*2;if(v>100)v=100;snprintf(p,sizeof p,"volume=%d\n",v);enqueue(ITEM_PARAMS,p,0);break;}case'o':activate_voice(value);break;case'P':clear_queue();break;default:break;}(void)sign;i=j+1;start=i;continue;}i++;}if(i>start){char *text=strndup(buf+start,i-start);enqueue(ITEM_TEXT,text,0);free(text);}}

static void on_signal(int sig){(void)sig;stopping=1;}
static void usage(FILE*f){fprintf(f,"usage: speakup-cerence [--lang CODE] [--quality QUALITY]\n");}
int main(int argc, char **argv)
{
    struct filters f = {0};
    int i;
    pthread_t thread;

    find_installation_root();

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--lang") && i + 1 < argc) f.lang = argv[++i];
        else if (!strcmp(argv[i], "--quality") && i + 1 < argc) f.quality = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(stdout); return 0; }
        else { usage(stderr); return 2; }
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    spawn_host();
    audio_init(&argc, &argv);
    pthread_create(&thread, NULL, worker, NULL);
    select_voices(&f);

    synth_fd = open("/dev/softsynthu", O_RDWR | O_NONBLOCK);
    if (synth_fd < 0 && errno == ENOENT)
        synth_fd = open("/dev/softsynth", O_RDWR | O_NONBLOCK);
    if (synth_fd < 0)
        die("cannot open /dev/softsynthu: %s", strerror(errno));

    while (!stopping) {
        struct pollfd p = {synth_fd, POLLIN, 0};
        char buf[16385];
        int rc = poll(&p, 1, 250);
        if (rc > 0 && (p.revents & POLLIN)) {
            ssize_t n = read(synth_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = 0;
                process_bytes(buf, n);
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                break;
            }
        }
    }

    stopping = 1;
    pthread_cond_broadcast(&queue_ready);
    clear_queue();
    pthread_join(thread, NULL);
    send_frame(CMD_QUIT, "", 0);
    close(host.in);
    close(host.out);
    waitpid(host.pid, NULL, 0);
    pw_thread_loop_stop(audio.loop);
    pw_stream_destroy(audio.stream);
    pw_core_disconnect(audio.core);
    pw_context_destroy(audio.context);
    pw_thread_loop_destroy(audio.loop);
    pw_deinit();
    voice_list_destroy(&voices);
    return 0;
}
