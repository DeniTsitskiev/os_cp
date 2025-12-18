#include "func.h"

#include <ctype.h>
#include <unistd.h>

#define SERV "tcp://localhost:5555"

void game_play(void *s, const char *u, const char *g);

int send_msg(void *s, Msg *m) {
    zmq_send(s, "", 0, ZMQ_SNDMORE);
    return zmq_send(s, m, sizeof(Msg), 0);
}

int recv_msg(void *s, Msg *m) {
    char sep[10];
    zmq_recv(s, sep, 10, 0);
    return zmq_recv(s, m, sizeof(Msg), 0);
}

// Отображает правила игры: механика быков и коров, последовательность действия, примеры
void show_rules() {
    printf("\n==============================\n");
    printf("   ИГРА: УГАДАЙ СЛОВО\n");
    printf("==============================\n");
    printf("Цель: угадать 5-буквенное слово\n\n");
    printf("БЫК  - буква и позиция угаданы верно\n");
    printf("КОРОВА - буква верна, позиция нет\n\n");
    printf("Пример:\n");
    printf("  Секрет: house\n");
    printf("  Попытка: heart -> 1 бык, 1 корова\n");
    printf("  Попытка: horse -> 4 быка, 0 коров\n");
    printf("  Попытка: house -> 5 быков (победа)\n");
    printf("==============================\n\n");
}

// Читает 5-буквенное слово у пользователя (любые буквы a-z)
// Параметры: w - буфер для сохранения
// На "quit" возвращаем -1, иначе 0 при ошибке, 1 при успехе
int get_word(char *w) {
    char buf[100];
    printf("Введите слово (или 'quit' для выхода): ");
    
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        return 0;
    }
    
    buf[strcspn(buf, "\n")] = 0;
    
    for (size_t i = 0; i < strlen(buf); i++) {
        buf[i] = tolower((unsigned char)buf[i]);
    }
    
    if (strcmp(buf, "quit") == 0) {
        return -1; // сигнал к выходу
    }
    
    // Проверяем только длину и буквы (не проверяем наличие в словаре)
    if (strlen(buf) != WORD_LENGTH) {
        printf("Слово должно быть ровно %d букв\n", WORD_LENGTH);
        return 0;
    }
    
    for (int i = 0; i < WORD_LENGTH; i++) {
        if (buf[i] < 'a' || buf[i] > 'z') {
            printf("Используйте только буквы a-z\n");
            return 0;
        }
    }
    
    strcpy(w, buf);
    return 1;
}

// Интерактивный диалог создания новой игры
// Параметры: s - сокет DEALER, u - имя пользователя
// Отправляем MSG_NEW_GAME серверу
void make_game(void *s, const char *u) {
    Msg r, p;
    msg_create(&r);
    
    r.cmd = MSG_NEW_GAME;
    strcpy(r.user_name, u);
    
    printf("\nНазвание игры: ");
    if (fgets(r.game_id, sizeof(r.game_id), stdin) == NULL) {
        printf("Input error\n");
        return;
    }
    r.game_id[strcspn(r.game_id, "\n")] = 0;
    
    printf("Максимум игроков (1-%d): ", MAX_GAME_PLAYERS);
    if (scanf("%d", &r.player_cnt) != 1) {
        printf("Ошибка ввода\n");
        while (getchar() != '\n');
        return;
    }
    while (getchar() != '\n'); // Очистка буфера
    
    if (r.player_cnt < 1 || r.player_cnt > MAX_GAME_PLAYERS) {
        printf("Некорректное число игроков\n");
        return;
    }
    
    printf("Отправка...\n");
    send_msg(s, &r);
    
    printf("Ожидание...\n");
    recv_msg(s, &p);
    
    if (p.cmd == MSG_FAIL) {
        printf("Ошибка: %s\n", p.msg);
        return;
    }
    
    printf("\nИгра '%s' создана!\n", p.game_id);
    printf("Игроков: %d\n", p.player_cnt);
    printf("[DEBUG] Секрет: %s\n", p.word);
    
    game_play(s, u, p.game_id);
}

// Присоединение к существующей игре по её имени
// Параметры: s - сокет DEALER, u - имя пользователя
// Отправляем MSG_JOIN_BY_ID серверу
void join_game(void *s, const char *u) {
    Msg r, p;
    msg_create(&r);
    
    r.cmd = MSG_JOIN_BY_ID;
    strcpy(r.user_name, u);
    
    printf("\nИмя игры: ");
    if (fgets(r.game_id, sizeof(r.game_id), stdin) == NULL) {
        printf("Ошибка ввода\n");
        return;
    }
    r.game_id[strcspn(r.game_id, "\n")] = 0;
    
    send_msg(s, &r);
    recv_msg(s, &p);
    
    if (p.cmd == MSG_FAIL) {
        printf("Ошибка: %s\n", p.msg);
        return;
    }
    
    printf("\nВы в игре '%s'!\n", p.game_id);
    printf("Игроков: %d\n", p.player_cnt);
    printf("[DEBUG] Секрет: %s\n", p.word);
    
    game_play(s, u, p.game_id);
}

// Показывает кол-во активных игр на сервере
// Параметры: s - сокет DEALER
void list_games(void *s) {
    Msg r, p;
    msg_create(&r);
    
    r.cmd = MSG_GET_GAMES;
    
    send_msg(s, &r);
    recv_msg(s, &p);
    
    printf("\nАктивных игр: %d\n", p.total_games);
}

// Основной игровой цикл
// Параметры: s - сокет, u - имя, g - имя игры
// Логика: цикл ввода слов - отправка - получение быков/коров - проверка победы
void game_play(void *s, const char *u, const char *g) {
    show_rules();
    
    printf("Начинаем игру!\n");
    printf("Введите 'quit' чтобы выйти\n\n");
    
    int tries = 0;
    while (1) {
        printf("\n--- Попытка %d ---\n", tries + 1);
        
        Msg r, p;
        msg_create(&r);
        
        r.cmd = MSG_MAKE_TRY;
        strcpy(r.user_name, u);
        strcpy(r.game_id, g);
        
        int gw = get_word(r.word);
        if (gw == -1) {
            break; // игрок решил выйти
        }
        if (gw == 0) {
            printf("Try again\n");
            continue;
        }
        
        send_msg(s, &r);
        recv_msg(s, &p);
        
        if (p.cmd == MSG_FAIL) {
            printf("Ошибка: %s\n", p.msg);
            break;
        }
        
        tries++;
        
         printf("\nРезультат: %d быков, %d коров\n", 
             p.res.bulls, p.res.cows);
        
        if (p.cmd == MSG_WIN) {
            printf("\n");
            printf("========================\n");
            printf("   ПОБЕДА!\n");
            printf("   %d попыток\n", tries);
            printf("========================\n");
            break;
        }
        
        printf("Попыток: %d\n", tries);
    }
    
    printf("\nИгра окончена.\n");
    
    Msg quit_r, quit_p;
    msg_create(&quit_r);
    quit_r.cmd = MSG_QUIT_GAME;
    strcpy(quit_r.user_name, u);
    strcpy(quit_r.game_id, g);
    send_msg(s, &quit_r);
    recv_msg(s, &quit_p);
}

// Отображает главное меню доступных действий
void menu() {
    printf("\n==============================\n");
    printf("  БЫКИ И КОРОВЫ (СЛОВА)\n");
    printf("==============================\n");
    printf("1. Создать игру\n");
    printf("2. Присоединиться к игре\n");
    printf("3. Список игр\n");
    printf("4. Выход\n");
    printf("==============================\n");
    printf("Выберите: ");
}

// Точка входа клиента: инициализация ZeroMQ DEALER сокета, подключение к серверу, основной цикл меню
// Пользователь может создавать/присоединяться к играм, просматривать активные игры, играть
int main() {
    char user_name[MAX_USERNAME];
    
    printf("==============================\n");
    printf("  КЛИЕНТ: БЫКИ И КОРОВЫ\n");
    printf("==============================\n");
    printf("\nВаше имя: ");
    
    if (fgets(user_name, sizeof(user_name), stdin) == NULL) {
        printf("Ошибка\n");
        return 1;
    }
    
    user_name[strcspn(user_name, "\n")] = 0;
    
    if (strlen(user_name) == 0) {
        printf("Имя не может быть пустым\n");
        return 1;
    }
    
    printf("Добро пожаловать, %s!\n", user_name);
    printf("Подключение...\n");
    
    void *ctx = zmq_ctx_new();
    void *sock = zmq_socket(ctx, ZMQ_DEALER);
    
    int rc = zmq_connect(sock, SERV);
    if (rc != 0) {
        printf("Ошибка подключения\n");
        return 1;
    }
    
    printf("Подключено!\n");
    
    int choice;
    while (1) {
        menu();
        
        if (scanf("%d", &choice) != 1) {
            printf("Ошибка ввода\n");
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n'); // Очистка буфера
        
        switch (choice) {
            case 1:
                make_game(sock, user_name);
                break;
            case 2:
                join_game(sock, user_name);
                break;
            case 3:
                list_games(sock);
                break;
            case 4:
                printf("До свидания!\n");
                zmq_close(sock);
                zmq_ctx_destroy(ctx);
                return 0;
            default:
                printf("Неверный выбор\n");
        }
    }
    
    return 0;
}