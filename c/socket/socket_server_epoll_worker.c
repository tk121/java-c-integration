/*
===========================================================
  epoll + task queue + worker thread pool
  + [4byte length][JSON] protocol (complete version)
===========================================================

要件：
- Java(Tomcat) から JSON を受信 (requestId, threadId を含む)
- 送受信プロトコル： [4byte length(big-endian)][UTF-8 JSON bytes]
- C側は 1プロセスで epoll により多数接続を監視
- 実処理(重い処理/外部API/ファイルI/O)は worker で並列化
- 1リクエスト=1接続（処理後 close）

注意：
- 本番では JSON 解析は cJSON など推奨
  ここでは検証用に "requestId":"..." / "threadId":123 を簡易抽出

===========================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#define PORT 5000
#define MAX_EVENTS 100
#define WORKER_COUNT 4

/* 受信JSONの最大サイズ（必要に応じて増やす） */
#define MAX_PAYLOAD (1024 * 1024)   // 1MB

/* タスクキュー */
#define QUEUE_SIZE 1000

/* ====== ログが混ざらないようにする ====== */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ====== 時刻(ms) ====== */
static long long now_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec*1000LL + ts.tv_nsec/1000000LL;
}

/* =========================================================
   [必須] ノンブロッキング設定（epollで詰まらせない）
   ========================================================= */
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) flags = 0;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* workerで送信するときだけブロッキングに戻す（簡単で堅い） */
static void set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) flags = 0;
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

/* =========================================================
   接続状態（分割受信対応のため fd ごとに状態を持つ）
   ========================================================= */
typedef struct {
    int fd;

    /* ヘッダ（4byte長さ）受信状態 */
    uint8_t  hdr[4];
    int      hdr_read;          // 0..4

    /* ボディ（JSON）受信状態 */
    uint32_t body_len;          // length
    uint32_t body_read;         // 0..body_len
    char*    body;              // malloc(body_len+1)
} ConnState;

/* fd -> ConnState の簡易マップ（上限は環境により調整可） */
#define FD_MAP_SIZE 65536
static ConnState* g_conns[FD_MAP_SIZE];

/* =========================================================
   タスク（workerに渡す）
   ========================================================= */
typedef struct {
    int   client_fd;
    char* json;         // mallocされたJSON文字列（workerがfree）
    uint32_t json_len;
} task_t;

/* =========================================================
   キュー（スレッドセーフ）
   ========================================================= */
static task_t task_queue[QUEUE_SIZE];
static int q_head = 0;
static int q_tail = 0;
static int q_count = 0;

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  queue_cond  = PTHREAD_COND_INITIALIZER;

static int enqueue(task_t task) {
    pthread_mutex_lock(&queue_mutex);

    if (q_count >= QUEUE_SIZE) {
        pthread_mutex_unlock(&queue_mutex);
        return -1;
    }
    task_queue[q_tail] = task;
    q_tail = (q_tail + 1) % QUEUE_SIZE;
    q_count++;

    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
    return 0;
}

static task_t dequeue(void) {
    pthread_mutex_lock(&queue_mutex);
    while (q_count == 0) {
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    task_t t = task_queue[q_head];
    q_head = (q_head + 1) % QUEUE_SIZE;
    q_count--;
    pthread_mutex_unlock(&queue_mutex);
    return t;
}

/* =========================================================
   簡易JSON抽出（検証用）
   - requestId : "requestId":"xxx"
   - threadId  : "threadId":123 or "threadId":"123"
   ========================================================= */
static int json_get_string(const char* json, const char* key, char* out, size_t outsz) {
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);

    const char* p = strstr(json, pat);
    if (!p) return 0;

    p = strchr(p, ':');
    if (!p) return 0;
    p++;

    while (*p==' ' || *p=='\t') p++;
    if (*p != '\"') return 0;
    p++;

    const char* q = strchr(p, '\"');
    if (!q) return 0;

    size_t n = (size_t)(q - p);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 1;
}

static int json_get_long(const char* json, const char* key, long* out) {
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);

    const char* p = strstr(json, pat);
    if (!p) return 0;

    p = strchr(p, ':');
    if (!p) return 0;
    p++;

    while (*p==' ' || *p=='\t') p++;
    if (*p == '\"') p++;

    char* endptr = NULL;
    long v = strtol(p, &endptr, 10);
    if (endptr == p) return 0;

    *out = v;
    return 1;
}

/* =========================================================
   write_all（ブロッキング前提：workerで使用）
   ========================================================= */
static int write_all(int fd, const void* buf, size_t len) {
    const uint8_t* p = (const uint8_t*)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

/* =========================================================
   レスポンス作成（requestId/threadIdを必ず返す）
   ========================================================= */
static void build_response_json(const char* req_json,
                               const char* requestId,
                               long threadId,
                               unsigned long workerTid,
                               char* out,
                               size_t outsz)
{
    const char* msg;
    if (strstr(req_json, "processA") != NULL) msg = "Processed A";
    else if (strstr(req_json, "processB") != NULL) msg = "Processed B";
    else msg = "ERROR";

    snprintf(out, outsz,
             "{\"requestId\":\"%s\",\"threadId\":%ld,"
             "\"status\":\"OK\",\"result\":{\"message\":\"%s\"},"
             "\"workerTid\":%lu}",
             requestId, threadId, msg, workerTid);
}

/* =========================================================
   workerスレッド
   ========================================================= */
static void* worker_thread(void* arg) {
    (void)arg;

    while (1) {
        task_t task = dequeue();

        char requestId[128] = "UNKNOWN";
        long threadId = -1;

        (void)json_get_string(task.json, "requestId", requestId, sizeof(requestId));
        (void)json_get_long(task.json, "threadId", &threadId);

        unsigned long workerTid = (unsigned long)pthread_self();
        long long t0 = now_ms();

        pthread_mutex_lock(&log_mutex);
        printf("[WORKER START] t=%lldms workerTid=%lu fd=%d requestId=%s threadId=%ld\n",
               t0, workerTid, task.client_fd, requestId, threadId);
        pthread_mutex_unlock(&log_mutex);

        /* 疑似重処理（並列確認用：不要なら削除） */
        usleep(800 * 1000);

        /* レスポンスJSONを作成 */
        char resp_json[2048];
        memset(resp_json, 0, sizeof(resp_json));
        build_response_json(task.json, requestId, threadId, workerTid, resp_json, sizeof(resp_json));

        /* [4byte length][JSON] で返信する */
        uint32_t resp_len = (uint32_t)strlen(resp_json);
        uint32_t net_len  = htonl(resp_len);

        /* workerがそのfdを専有するので、送信だけブロッキングに戻して確実に送る */
        set_blocking(task.client_fd);

        if (write_all(task.client_fd, &net_len, 4) == 0 &&
            write_all(task.client_fd, resp_json, resp_len) == 0) {
            // OK
        } else {
            pthread_mutex_lock(&log_mutex);
            printf("[WORKER SEND ERROR] fd=%d errno=%d\n", task.client_fd, errno);
            pthread_mutex_unlock(&log_mutex);
        }

        close(task.client_fd);
        free(task.json);

        long long t1 = now_ms();
        pthread_mutex_lock(&log_mutex);
        printf("[WORKER END]   t=%lldms workerTid=%lu requestId=%s threadId=%ld elapsed=%lldms\n",
               t1, workerTid, requestId, threadId, (t1 - t0));
        pthread_mutex_unlock(&log_mutex);
    }
    return NULL;
}

/* =========================================================
   ConnStateの作成/破棄
   ========================================================= */
static ConnState* conn_create(int fd) {
    ConnState* c = (ConnState*)calloc(1, sizeof(ConnState));
    if (!c) return NULL;
    c->fd = fd;
    c->hdr_read = 0;
    c->body_len = 0;
    c->body_read = 0;
    c->body = NULL;
    return c;
}

static void conn_destroy(ConnState* c) {
    if (!c) return;
    if (c->body) free(c->body);
    free(c);
}

/* =========================================================
   [必須] epoll受付ループで分割受信を進める関数
   - 4byteヘッダを読み切る
   - 長さ分ボディを読み切る
   - 読み切ったら task として enqueue し、fdはepollから外す
   ========================================================= */
static void on_client_readable(int epfd, int fd) {
    if (fd < 0 || fd >= FD_MAP_SIZE) { close(fd); return; }

    ConnState* c = g_conns[fd];
    if (!c) {
        // 念のため（通常 accept 時に作成される）
        c = conn_create(fd);
        g_conns[fd] = c;
    }

    while (1) {
        /* 1) まずヘッダ(4byte)を読み切る */
        if (c->hdr_read < 4) {
            ssize_t n = read(fd, c->hdr + c->hdr_read, 4 - c->hdr_read);
            if (n > 0) {
                c->hdr_read += (int)n;
                if (c->hdr_read < 4) {
                    // まだヘッダが足りないので次回へ
                    return;
                }

                // ヘッダ完成 → body_len確定
                uint32_t net_len;
                memcpy(&net_len, c->hdr, 4);
                c->body_len = ntohl(net_len);

                if (c->body_len == 0 || c->body_len > MAX_PAYLOAD) {
                    pthread_mutex_lock(&log_mutex);
                    printf("[PROTO ERROR] fd=%d invalid body_len=%u\n", fd, c->body_len);
                    pthread_mutex_unlock(&log_mutex);
                    close(fd);
                    conn_destroy(c);
                    g_conns[fd] = NULL;
                    return;
                }

                c->body = (char*)malloc(c->body_len + 1);
                if (!c->body) {
                    close(fd);
                    conn_destroy(c);
                    g_conns[fd] = NULL;
                    return;
                }
                c->body_read = 0;
                c->body[c->body_len] = '\0';
            } else if (n == 0) {
                // 切断
                close(fd);
                conn_destroy(c);
                g_conns[fd] = NULL;
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return; // 次回
                if (errno == EINTR) continue;
                close(fd);
                conn_destroy(c);
                g_conns[fd] = NULL;
                return;
            }
        }

        /* 2) ボディ(JSON)を読み切る */
        if (c->hdr_read == 4 && c->body_read < c->body_len) {
            ssize_t n = read(fd, c->body + c->body_read, c->body_len - c->body_read);
            if (n > 0) {
                c->body_read += (uint32_t)n;
                if (c->body_read < c->body_len) {
                    // まだ足りない
                    continue; // 可能なら続けて読む
                }

                // 3) 受信完了 → タスク化してworkerへ
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);

                task_t task;
                task.client_fd = fd;
                task.json_len = c->body_len;

                // workerがfreeするので所有権移譲（c->body をそのまま渡す）
                task.json = c->body;
                c->body = NULL; // conn_destroyでfreeされないように

                // ConnStateはもう不要（この接続はworkerで送ってcloseする）
                conn_destroy(c);
                g_conns[fd] = NULL;

                if (enqueue(task) != 0) {
                    // キュー満杯なら切断（本番ならエラー応答が望ましい）
                    close(fd);
                    free(task.json);
                }
                return;
            } else if (n == 0) {
                close(fd);
                conn_destroy(c);
                g_conns[fd] = NULL;
                return;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                if (errno == EINTR) continue;
                close(fd);
                conn_destroy(c);
                g_conns[fd] = NULL;
                return;
            }
        }
    }
}

int main(void) {
    int server_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    /* サーバソケット作成 */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    /* 再起動時にbind失敗しにくくする */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen"); return 1;
    }

    /* [必須] listen fd をノンブロッキング化 */
    set_nonblocking(server_fd);

    /* [必須] epollインスタンス作成 */
    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); return 1; }

    struct epoll_event ev, events[MAX_EVENTS];

    /* [必須] server_fd をepollに登録（acceptイベント検知） */
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("epoll_ctl ADD server_fd"); return 1;
    }

    /* worker起動 */
    pthread_t workers[WORKER_COUNT];
    for (int i = 0; i < WORKER_COUNT; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    printf("server started: port=%d workers=%d protocol=[4byte len][json]\n", PORT, WORKER_COUNT);

    while (1) {
        /* [必須] epoll_wait でイベント待ち */
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                /* [必須] 新規接続を取り尽くす（ノンブロッキングaccept） */
                while (1) {
                    int client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept"); break;
                    }

                    set_nonblocking(client_fd);

                    if (client_fd >= 0 && client_fd < FD_MAP_SIZE) {
                        g_conns[client_fd] = conn_create(client_fd);
                        if (!g_conns[client_fd]) {
                            close(client_fd);
                            continue;
                        }
                    } else {
                        close(client_fd);
                        continue;
                    }

                    /* [必須] client_fd をepollに登録（受信イベント検知） */
                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                        perror("epoll_ctl ADD client_fd");
                        conn_destroy(g_conns[client_fd]);
                        g_conns[client_fd] = NULL;
                        close(client_fd);
                        continue;
                    }
                }
            } else {
                /* client_fd から受信（分割受信を進める） */
                on_client_readable(epfd, fd);
            }
        }
    }

    close(epfd);
    close(server_fd);
    return 0;
}