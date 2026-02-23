/*
===========================================================
  epoll + タスクキュー + workerスレッドプール版 TCPサーバ
===========================================================

設計思想：
・epollスレッドは「受付専用」
・重い処理（ファイルI/O / API呼び出し）はworkerで処理
・100接続想定
・1リクエスト1レスポンス型
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

#define PORT 5000
#define MAX_EVENTS 100
#define BUFFER_SIZE 1024
#define WORKER_COUNT 4
#define QUEUE_SIZE 1000

/* ==============================
   タスク構造体
   ============================== */
typedef struct {
    int client_fd;                 // 返信先ソケット
    char data[BUFFER_SIZE];        // 受信データ
} task_t;

/* ==============================
   タスクキュー
   ============================== */
task_t task_queue[QUEUE_SIZE];
int head = 0;
int tail = 0;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

/* ==============================
   ノンブロッキング設定
   ============================== */
void set_nonblocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

/* ==============================
   キューへ追加（enqueue）
   ============================== */
void enqueue(task_t task) {

    pthread_mutex_lock(&queue_mutex);

    task_queue[tail] = task;
    tail = (tail + 1) % QUEUE_SIZE;

    pthread_cond_signal(&queue_cond);  // workerに通知

    pthread_mutex_unlock(&queue_mutex);
}

/* ==============================
   キューから取得（dequeue）
   ============================== */
task_t dequeue() {

    pthread_mutex_lock(&queue_mutex);

    while (head == tail) {
        // キューが空なら待機
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }

    task_t task = task_queue[head];
    head = (head + 1) % QUEUE_SIZE;

    pthread_mutex_unlock(&queue_mutex);

    return task;
}

/* ==============================
   実際の処理（将来重くなる想定）
   ============================== */
void handle_request(char* buffer, char* response) {

    /*
     将来ここに：
     ・DBアクセス
     ・ファイルI/O
     ・外部API呼び出し
     を追加可能
    */

    if (strstr(buffer, "processA") != NULL) {
        sprintf(response, "{\"status\":\"OK\",\"result\":{\"message\":\"Processed A\"}}");
    }
    else if (strstr(buffer, "processB") != NULL) {
        sprintf(response, "{\"status\":\"OK\",\"result\":{\"message\":\"Processed B\"}}");
    }
    else {
        sprintf(response, "{\"status\":\"ERROR\"}");
    }
}

/* ==============================
   workerスレッド処理
   ============================== */
void* worker_thread(void* arg) {

    while (1) {

        // キューからタスク取得
        task_t task = dequeue();

        char response[BUFFER_SIZE] = {0};

        // 重い処理はここで行う
        handle_request(task.data, response);

        // クライアントへ返信
        send(task.client_fd, response, strlen(response), 0);

        close(task.client_fd);
    }

    return NULL;
}

/* ==============================
   メイン（epoll受付専用）
   ============================== */
int main() {

    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, SOMAXCONN);

    set_nonblocking(server_fd);

    int epfd = epoll_create1(0);

    struct epoll_event ev, events[MAX_EVENTS];

    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev);

    /* ==========================
       workerスレッド起動
       ========================== */
    pthread_t workers[WORKER_COUNT];

    for (int i = 0; i < WORKER_COUNT; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    printf("epoll + worker TCP Server started on port %d\n", PORT);

    while (1) {

        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);

        for (int i = 0; i < nfds; i++) {

            if (events[i].data.fd == server_fd) {

                // 新規接続受付
                int client_fd;
                while ((client_fd = accept(server_fd,
                        (struct sockaddr *)&address,
                        (socklen_t*)&addrlen)) > 0) {

                    set_nonblocking(client_fd);

                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
                }
            }
            else {

                int client_fd = events[i].data.fd;

                char buffer[BUFFER_SIZE] = {0};

                int n = read(client_fd, buffer, BUFFER_SIZE);

                if (n <= 0) {
                    close(client_fd);
                    continue;
                }

                printf("Received: %s\n", buffer);

                // ★ ここでは処理せずキューへ渡す
                task_t task;
                task.client_fd = client_fd;
                strncpy(task.data, buffer, BUFFER_SIZE);

                enqueue(task);
            }
        }
    }

    return 0;
}