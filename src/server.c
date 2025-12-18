#include "func.h"
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#define ADDR "tcp://*:5555"
#define MAX_THREAD 50

typedef struct {
    char login[MAX_USERNAME];
    int ok;
    int tries_cnt;
} User;

typedef struct {
    char title[MAX_GAME_ID];
    char secret[WORD_LENGTH + 1];
    int slots;
    int users_cnt;
    User team[MAX_GAME_PLAYERS];
    int run;
} Play;

Play all_plays[50];
int play_cnt = 0;
int srv_on = 1;
pthread_mutex_t srv_lock = PTHREAD_MUTEX_INITIALIZER;
void *zmq_ctx = NULL;

typedef struct {
    void *s;
    Msg req;
    char id[256];
    int id_len;
} Task;

// Обработчик сигналов для корректного завершения сервера
// При SIGINT/SIGTERM устанавливаем srv_on=0, на следующем сдвиге цикла сервер выйдет
void sig_handler(int n) {
    printf("\nПолучен сигнал %d. Остановка сервера\n", n);
    srv_on = 0;
}

Play* get_play(const char *name) {
    pthread_mutex_lock(&srv_lock);
    Play *res = NULL;
    
    for (int i = 0; i < play_cnt; i++) {
        if (strcmp(all_plays[i].title, name) == 0) {
            res = &all_plays[i];
            break;
        }
    }
    
    pthread_mutex_unlock(&srv_lock);
    return res;
}

// Обрабатывает запрос на создание новой игры (MSG_NEW_GAME)
// Параметры: req - полученные данные от клиента, res - сообщение для ответа
// Логика: проверяет лимиты, генерирует слово, сохраняет игру в списке
void do_new_play(Msg *req, Msg *res) {
    pthread_mutex_lock(&srv_lock);
    
    if (play_cnt >= 50) {
        res->cmd = MSG_FAIL;
        strcpy(res->msg, "Server full");
        pthread_mutex_unlock(&srv_lock);
        return;
    }
    
    // Check exist
    for (int i = 0; i < play_cnt; i++) {
        if (strcmp(all_plays[i].title, req->game_id) == 0) {
            res->cmd = MSG_FAIL;
            strcpy(res->msg, "Game exists");
            pthread_mutex_unlock(&srv_lock);
            return;
        }
    }
    
    if (req->player_cnt < 1 || req->player_cnt > MAX_GAME_PLAYERS) {
        res->cmd = MSG_FAIL;
        strcpy(res->msg, "Bad players count");
        pthread_mutex_unlock(&srv_lock);
        return;
    }
    
    Play *p = &all_plays[play_cnt++];
    strcpy(p->title, req->game_id);
    p->slots = req->player_cnt;
    p->users_cnt = 1;
    p->run = 1;
    
    strcpy(p->team[0].login, req->user_name);
    p->team[0].ok = 1;
    p->team[0].tries_cnt = 0;
    
    gen_word(p->secret);
    
    printf("Создана игра '%s', секрет: %s\n", p->title, p->secret);
    
    res->cmd = MSG_GAME_OK;
    strcpy(res->game_id, p->title);
    res->player_cnt = p->users_cnt;
    strcpy(res->word, p->secret);  // Отправляем секрет для debug
    
    pthread_mutex_unlock(&srv_lock);
}

// Обрабатывает присоединение к существующей игре (MSG_JOIN_BY_ID)
// Параметры: req - данные игрока, res - ответ
// Логика: поиск игры по имени, проверка места, добавление игрока в список
void do_join(Msg *req, Msg *res) {
    pthread_mutex_lock(&srv_lock);
    
    Play *p = NULL;
    for (int i = 0; i < play_cnt; i++) {
        if (strcmp(all_plays[i].title, req->game_id) == 0) {
            p = &all_plays[i];
            break;
        }
    }
    
    if (p == NULL) {
        res->cmd = MSG_FAIL;
        strcpy(res->msg, "Game not found");
        pthread_mutex_unlock(&srv_lock);
        return;
    }
    
    if (!p->run) {
        res->cmd = MSG_FAIL;
        strcpy(res->msg, "Game ended");
        pthread_mutex_unlock(&srv_lock);
        return;
    }
    
    if (p->users_cnt >= p->slots) {
        res->cmd = MSG_FAIL;
        strcpy(res->msg, "Game full");
        pthread_mutex_unlock(&srv_lock);
        return;
    }
    
    // Check already joined
    for (int i = 0; i < p->users_cnt; i++) {
        if (strcmp(p->team[i].login, req->user_name) == 0) {
            res->cmd = MSG_FAIL;
            strcpy(res->msg, "Already in");
            pthread_mutex_unlock(&srv_lock);
            return;
        }
    }
    
    int idx = p->users_cnt++;
    strcpy(p->team[idx].login, req->user_name);
    p->team[idx].ok = 1;
    p->team[idx].tries_cnt = 0;
    
        printf("Игрок '%s' присоединился к '%s' (%d/%d)\n", req->user_name, p->title, 
            p->users_cnt, p->slots);
    
    res->cmd = MSG_JOINED_OK;
    strcpy(res->game_id, p->title);
    res->player_cnt = p->users_cnt;
    strcpy(res->word, p->secret);  // Отправляем секрет для debug
    
    pthread_mutex_unlock(&srv_lock);
}

// Обрабатывает попытку угадать слово (MSG_MAKE_TRY)
// Параметры: req - слово и инфо от клиента, res - ответ
// Логика: проверка слова, подсчёт быков/коров, проверка победы
void do_try(Msg *req, Msg *res) {
    pthread_mutex_lock(&srv_lock);
    
    Play *p = NULL;
    for (int i = 0; i < play_cnt; i++) {
        if (strcmp(all_plays[i].title, req->game_id) == 0) {
            p = &all_plays[i];
            break;
        }
    }
    
    if (p == NULL) {
        res->cmd = MSG_FAIL;
        strcpy(res->msg, "No game");
        pthread_mutex_unlock(&srv_lock);
        return;
    }
    
    if (!p->run) {
        res->cmd = MSG_FAIL;
        strcpy(res->msg, "Game done");
        pthread_mutex_unlock(&srv_lock);
        return;
    }
    
    // Find user
    User *u = NULL;
    for (int i = 0; i < p->users_cnt; i++) {
        if (strcmp(p->team[i].login, req->user_name) == 0) {
            u = &p->team[i];
            break;
        }
    }
    
    if (u == NULL) {
        res->cmd = MSG_FAIL;
        strcpy(res->msg, "User not in game");
        pthread_mutex_unlock(&srv_lock);
        return;
    }
    
    // Проверяем только длину и буквы (не словарь)
    if (strlen(req->word) != WORD_LENGTH) {
        res->cmd = MSG_FAIL;
        strcpy(res->msg, "Bad length");
        pthread_mutex_unlock(&srv_lock);
        return;
    }
    
    for (int i = 0; i < WORD_LENGTH; i++) {
        if (req->word[i] < 'a' || req->word[i] > 'z') {
            res->cmd = MSG_FAIL;
            strcpy(res->msg, "Bad chars");
            pthread_mutex_unlock(&srv_lock);
            return;
        }
    }
    
    u->tries_cnt++;
    
    int b = 0, c = 0;
    check_word(p->secret, req->word, &b, &c);
    
        printf("Игрок '%s' в '%s': попытка %d - %s -> %dБ %dК\n",
            req->user_name, p->title, u->tries_cnt, req->word, b, c);
    
    res->res.bulls = b;
    res->res.cows = c;
    res->res.try_num = u->tries_cnt;
    strcpy(res->res.who, u->login);
    
    if (b == WORD_LENGTH) {
        // Игрок выиграл - помечаем его неактивным
        u->ok = 0;
        res->cmd = MSG_WIN;
        printf("Победитель: '%s' в игре '%s'\n", u->login, p->title);
        
        // Проверяем, осталось ли активных игроков
        int active_cnt = 0;
        for (int j = 0; j < p->users_cnt; j++) {
            if (p->team[j].ok) {
                active_cnt++;
            }
        }
        
        // Если активных игроков больше нет - завершаем игру
        if (active_cnt == 0) {
            p->run = 0;
            printf("Игра '%s' завершена (все угадали или вышли)\n", p->title);
        }
    } else {
        res->cmd = MSG_TRY_RESULT;
    }
    
    strcpy(res->game_id, p->title);
    
    pthread_mutex_unlock(&srv_lock);
}

// Обрабатывает выход игрока из игры (MSG_QUIT_GAME)
// Параметры: req - имя игры и игрока, res - ответ
// Логика: помечает игрока как неактивного; если активных не осталось - игра завершается
void do_quit(Msg *req, Msg *res) {
    pthread_mutex_lock(&srv_lock);
    
    Play *p = NULL;
    for (int i = 0; i < play_cnt; i++) {
        if (strcmp(all_plays[i].title, req->game_id) == 0) {
            p = &all_plays[i];
            break;
        }
    }
    
    if (p == NULL) {
        res->cmd = MSG_FAIL;
        strcpy(res->msg, "Game not found");
        pthread_mutex_unlock(&srv_lock);
        return;
    }
    
    // Find user and mark inactive
    for (int i = 0; i < p->users_cnt; i++) {
        if (strcmp(p->team[i].login, req->user_name) == 0) {
            p->team[i].ok = 0;
            printf("Игрок '%s' вышел из игры '%s'\n", req->user_name, p->title);
            
            // Check if any active players left
            int active_cnt = 0;
            for (int j = 0; j < p->users_cnt; j++) {
                if (p->team[j].ok) {
                    active_cnt++;
                }
            }
            
            if (active_cnt == 0) {
                p->run = 0;
                printf("Игра '%s' завершена (нет активных игроков)\n", p->title);
            }
            
            break;
        }
    }
    
    res->cmd = MSG_GAME_OK;
    strcpy(res->game_id, p->title);
    
    pthread_mutex_unlock(&srv_lock);
}

// Обрабатывает запрос списка активных игр (MSG_GET_GAMES)
// Параметры: req - обыкно пустое сообщение, res - ответ
// Логика: просто считаем кол-во активных игр и возвращаем
void do_list(Msg *req, Msg *res) {
    (void)req;
    pthread_mutex_lock(&srv_lock);
    
    res->cmd = MSG_GAMES_LIST;
    res->total_games = 0;
    
    for (int i = 0; i < play_cnt; i++) {
        if (all_plays[i].run) {
            res->total_games++;
        }
    }
    
    printf("Список игр: активных %d\n", res->total_games);
    
    pthread_mutex_unlock(&srv_lock);
}

// Диспетчер команд: рамбует всех виды сообщений на конкретные обработчики
// Параметры: req - полученное месседж, res - для составления ответа
void work_msg(Msg *req, Msg *res) {
    msg_create(res);
    
    switch (req->cmd) {
        case MSG_NEW_GAME:
            do_new_play(req, res);
            break;
        case MSG_JOIN_BY_ID:
            do_join(req, res);
            break;
        case MSG_MAKE_TRY:
            do_try(req, res);
            break;
        case MSG_QUIT_GAME:
            do_quit(req, res);
            break;
        case MSG_GET_GAMES:
            do_list(req, res);
            break;
        default:
            res->cmd = MSG_FAIL;
            strcpy(res->msg, "Unknown cmd");
    }
}

// Поток для обработки одного клиентского запроса
// Параметры: arg - указатель на Task (запрос с данными и сокетом)
// Логика: вызываем work_msg, отправляем ответ назад клиенту
void* client_thread(void* arg) {
    Task *t = (Task*)arg;
    Msg res;
    
    work_msg(&t->req, &res);
    
    zmq_send(t->s, t->id, t->id_len, ZMQ_SNDMORE);
    zmq_send(t->s, "", 0, ZMQ_SNDMORE);
    zmq_send(t->s, &res, sizeof(Msg), 0);
    
    free(t);
    return NULL;
}

// Точка входа сервера: инициализация ZeroMQ ROUTER сокета, основной цикл приема сообщений
// Запускает обработку на слушающем порте tcp://*:5555, создает потоки для каждого клиента
// Завершается при SIGINT/SIGTERM
int main() {
    printf("==============================\n");
    printf("  Быки и Коровы (слова)\n");
    printf("==============================\n\n");
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    zmq_ctx = zmq_ctx_new();
    void *sock = zmq_socket(zmq_ctx, ZMQ_ROUTER);
    
    int rc = zmq_bind(sock, ADDR);
    if (rc != 0) {
        printf("Bind error\n");
        return 1;
    }
    
    printf("Сервер на %s\n", ADDR);
    printf("Потоки: до %d\n", MAX_THREAD);
    printf("Ожидание клиентов...\n\n");
    
    int timeout = 1000;
    zmq_setsockopt(sock, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    pthread_t threads[MAX_THREAD];
    int thread_idx = 0;
    
    while (srv_on) {
        Task *task = malloc(sizeof(Task));
        if (task == NULL) {
            continue;
        }
        
        task->s = sock;
        task->id_len = zmq_recv(sock, task->id, 256, ZMQ_DONTWAIT);
        
        if (task->id_len == -1) {
            free(task);
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100000000; // 100ms
            nanosleep(&ts, NULL);
            continue;
        }
        
        char delim[10];
        zmq_recv(sock, delim, 10, 0);
        zmq_recv(sock, &task->req, sizeof(Msg), 0);
        
        if (thread_idx >= MAX_THREAD) {
            printf("Достигнут лимит потоков\n");
            free(task);
            continue;
        }
        
        pthread_create(&threads[thread_idx], NULL, client_thread, task);
        thread_idx++;
        
        if (thread_idx >= MAX_THREAD) {
            thread_idx = 0;
        }
    }
    
    zmq_close(sock);
    zmq_ctx_destroy(zmq_ctx);
    
    printf("Сервер остановлен\n");
    return 0;
}
