#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <ctype.h>
#include <locale.h>

#define BUFFER_SIZE 1024
#define MAX_NAME_LEN 64

void *context;
void *requester;
char player_name[MAX_NAME_LEN];
int current_game_id = -1;

// Извлечь числовой ID из ответа сервера, возвращает -1 если не найден
static int extract_id_from_response(const char *response) {
    if (!response) return -1;
    const char *p = strstr(response, "ID:");
    if (!p) return -1;
    int id = -1;
    if (sscanf(p, "ID: %d", &id) == 1) return id;
    if (sscanf(p, "ID:%d", &id) == 1) return id;
    return -1;
}

// Функция для отправки запроса и получения ответа
int send_request(const char *request, char *response) {
    zmq_send(requester, request, strlen(request), 0);
    int size = zmq_recv(requester, response, BUFFER_SIZE - 1, 0);
    
    if (size == -1) {
        strcpy(response, "ERROR:Ошибка получения ответа");
        return -1;
    }
    
    response[size] = '\0';
    return 0;
}

// Функция для очистки ввода
void clear_input() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// Функция для получения строки ввода
void get_input(char *buffer, int size) {
    if (fgets(buffer, size, stdin) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';
    }
}

// Функция для отображения главного меню
void show_main_menu() {
    printf("\n=== Игра 'Быки и Коровы' ===\n");
    printf("1. Посмотреть список игр\n");
    printf("2. Создать новую игру\n");
    printf("3. Присоединиться к игре\n");
    printf("4. Выход\n");
    printf("Выберите действие: ");
}

// Функция для отображения игрового меню
void show_game_menu() {
    printf("\n=== Игровое меню ===\n");
    printf("1. Угадать слово\n");
    printf("2. Выйти из игры\n");
    printf("Выберите действие: ");
}

// Функция для просмотра списка игр
void list_games() {
    char request[BUFFER_SIZE] = "LIST";
    char response[BUFFER_SIZE];
    
    send_request(request, response);
    printf("\n%s\n", response);
}

// Функция для создания игры
void create_game() {
    char game_name[MAX_NAME_LEN];
    int max_players;
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    printf("\nВведите название игры: ");
    get_input(game_name, MAX_NAME_LEN);
    
    printf("Введите максимальное количество игроков (1-10): ");
    scanf("%d", &max_players);
    clear_input();
    
    if (max_players < 1 || max_players > 10) {
        printf("Некорректное количество игроков. Установлено 2.\n");
        max_players = 2;
    }
    
    snprintf(request, BUFFER_SIZE, "CREATE %s %d %s", game_name, max_players, player_name);
    send_request(request, response);
    
    if (strncmp(response, "OK:", 3) == 0) {
        current_game_id = extract_id_from_response(response);
        printf("\n%s\n", response + 3);
        if (current_game_id != -1) {
            printf("Вы в игре! ID игры: %d\n", current_game_id);
        } else {
            printf("Не удалось разобрать ID игры из ответа сервера.\n");
        }
    } else {
        printf("\nОшибка: %s\n", response + 6);
    }
}

// Функция для присоединения к игре
void join_game() {
    char game_name[MAX_NAME_LEN];
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    printf("\nВведите название игры: ");
    get_input(game_name, MAX_NAME_LEN);
    
    snprintf(request, BUFFER_SIZE, "JOIN %s %s", game_name, player_name);
    send_request(request, response);
    
    if (strncmp(response, "OK:", 3) == 0) {
        current_game_id = extract_id_from_response(response);
        printf("\n%s\n", response + 3);
        if (current_game_id != -1) {
            printf("Вы в игре! ID игры: %d\n", current_game_id);
        } else {
            printf("Не удалось разобрать ID игры из ответа сервера.\n");
        }
    } else {
        printf("\nОшибка: %s\n", response + 6);
    }
}

// Функция для угадывания слова
void guess_word() {
    char word[MAX_NAME_LEN];
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    printf("\nВведите слово: ");
    get_input(word, MAX_NAME_LEN);
    
    // Передаём слово серверу в исходном виде (сервер выполнит коррекцию регистра)
    
    snprintf(request, BUFFER_SIZE, "GUESS %d %s %s", current_game_id, player_name, word);
    send_request(request, response);
    
    if (strncmp(response, "WIN:", 4) == 0) {
        printf("\n🎉 %s\n", response + 4);
        current_game_id = -1; // Выходим из игры
    } else if (strncmp(response, "RESULT:", 7) == 0) {
        printf("\n%s\n", response + 7);
    } else if (strncmp(response, "ERROR:", 6) == 0) {
        printf("\nОшибка: %s\n", response + 6);
    }
}

// Функция для выхода из игры
void leave_game() {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    snprintf(request, BUFFER_SIZE, "LEAVE %d %s", current_game_id, player_name);
    send_request(request, response);
    
    if (strncmp(response, "OK:", 3) == 0) {
        printf("\n%s\n", response + 3);
        current_game_id = -1;
    } else {
        printf("\nОшибка: %s\n", response + 6);
    }
}

// Игровой цикл
void game_loop() {
    while (current_game_id != -1) {
        show_game_menu();
        
        int choice;
        scanf("%d", &choice);
        clear_input();
        
        switch (choice) {
            case 1:
                guess_word();
                break;
            case 2:
                leave_game();
                break;
            default:
                printf("Неверный выбор\n");
        }
    }
}

// Основной цикл клиента
void run_client() {
    int running = 1;
    
    while (running) {
        if (current_game_id == -1) {
            show_main_menu();
            
            int choice;
            scanf("%d", &choice);
            clear_input();
            
            switch (choice) {
                case 1:
                    list_games();
                    break;
                case 2:
                    create_game();
                    if (current_game_id != -1) {
                        game_loop();
                    }
                    break;
                case 3:
                    join_game();
                    if (current_game_id != -1) {
                        game_loop();
                    }
                    break;
                case 4:
                    running = 0;
                    printf("До свидания!\n");
                    break;
                default:
                    printf("Неверный выбор\n");
            }
        }
    }
}

int main() {
    /* Устанавливаем локаль для корректной работы с UTF-8 ввода */
    setlocale(LC_CTYPE, "");

    printf("=== Клиент игры 'Быки и Коровы' ===\n");
    printf("Введите ваше имя: ");
    get_input(player_name, MAX_NAME_LEN);

    printf("Подключение к серверу...\n");

    context = zmq_ctx_new();
    requester = zmq_socket(context, ZMQ_REQ);
    zmq_connect(requester, "tcp://localhost:5555");

    printf("Подключено к серверу!\n");

    run_client();
    
    zmq_close(requester);
    zmq_ctx_destroy(context);
    
    return 0;
}
