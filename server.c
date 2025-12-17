#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <zmq.h>
#include <ctype.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>
#include <errno.h>

#define MAX_GAMES 100
#define MAX_PLAYERS 10
#define MAX_NAME_LEN 64
#define MAX_WORD_LEN 64 /* max characters (wide) for secret word */
#define MAX_WORD_BYTES 256 /* max bytes for UTF-8 conversions */
#define BUFFER_SIZE 1024

// Структура игрока
typedef struct {
    char name[MAX_NAME_LEN];
    int active;
    int attempts;
} Player;

// Структура игры
typedef struct {
    char game_name[MAX_NAME_LEN];
    wchar_t secret_word[MAX_WORD_LEN];
    int max_players;
    int current_players;
    Player players[MAX_PLAYERS];
    int active;
    int winner_found;
    pthread_mutex_t lock;
} Game;

// Глобальные переменные
Game games[MAX_GAMES];
int games_count = 0;
pthread_mutex_t games_lock = PTHREAD_MUTEX_INITIALIZER;
void *context;
void *responder;

// Словарь для игры
static const wchar_t *word_dictionary[] = {
    L"программа", L"компьютер", L"клавиатура", L"монитор", L"система",
    L"процессор", L"память", L"диск", L"файл", L"папка",
    L"сеть", L"интернет", L"браузер", L"курсор", L"принтер",
    L"сканер", L"мышка", L"колонки", L"микрофон", L"камера"
};
const int dictionary_size = 20;

// Функция для получения случайного слова
/* Конвертация много-байтовой строки (UTF-8) в wide string */
static int utf8_to_wcs(const char *src, wchar_t *dst, size_t dst_size) {
    if (!src || !dst) return -1;
    size_t ret = mbstowcs(dst, src, dst_size);
    if (ret == (size_t)-1) return -1;
    if (ret < dst_size) dst[ret] = L'\0';
    else dst[dst_size-1] = L'\0';
    return 0;
}

/* Конвертация wide string в много-байтовую UTF-8 строку */
static int wcs_to_utf8(const wchar_t *src, char *dst, size_t dst_size) {
    if (!src || !dst) return -1;
    size_t ret = wcstombs(dst, src, dst_size);
    if (ret == (size_t)-1) return -1;
    if (ret < dst_size) dst[ret] = '\0';
    else dst[dst_size-1] = '\0';
    return 0;
}

/* Получить случайное слово (wide) */
void get_random_word(wchar_t *word) {
    if (!word) return;
    const wchar_t *src = word_dictionary[rand() % dictionary_size];
    wcsncpy(word, src, MAX_WORD_LEN - 1);
    word[MAX_WORD_LEN-1] = L'\0';
}

// Функция для подсчета быков и коров (wide strings)
void count_bulls_and_cows(const wchar_t *secret, const wchar_t *guess, int *bulls, int *cows) {
    *bulls = 0;
    *cows = 0;
    size_t secret_len = wcslen(secret);
    size_t guess_len = wcslen(guess);

    if (secret_len != guess_len) {
        return;
    }

    int secret_used[MAX_WORD_LEN] = {0};
    int guess_used[MAX_WORD_LEN] = {0};

    // Подсчет быков
    for (size_t i = 0; i < secret_len; i++) {
        if (secret[i] == guess[i]) {
            (*bulls)++;
            secret_used[i] = 1;
            guess_used[i] = 1;
        }
    }

    // Подсчет коров
    for (size_t i = 0; i < guess_len; i++) {
        if (!guess_used[i]) {
            for (size_t j = 0; j < secret_len; j++) {
                if (!secret_used[j] && secret[j] == guess[i]) {
                    (*cows)++;
                    secret_used[j] = 1;
                    break;
                }
            }
        }
    }
}

// Создание новой игры
int create_game(const char *game_name, int max_players, const char *creator_name) {
    pthread_mutex_lock(&games_lock);
    
    if (games_count >= MAX_GAMES) {
        pthread_mutex_unlock(&games_lock);
        return -1;
    }
    
    // Проверка, что игра с таким именем не существует
    for (int i = 0; i < games_count; i++) {
        if (strcmp(games[i].game_name, game_name) == 0 && games[i].active) {
            pthread_mutex_unlock(&games_lock);
            return -2;
        }
    }
    
    Game *game = &games[games_count];
    strncpy(game->game_name, game_name, MAX_NAME_LEN - 1);
    game->max_players = max_players;
    game->current_players = 1;
    game->active = 1;
    game->winner_found = 0;
    get_random_word(game->secret_word);
    
    // Добавляем создателя как первого игрока
    strncpy(game->players[0].name, creator_name, MAX_NAME_LEN - 1);
    game->players[0].active = 1;
    game->players[0].attempts = 0;
    
    pthread_mutex_init(&game->lock, NULL);
    
    int game_id = games_count;
    games_count++;
    
    pthread_mutex_unlock(&games_lock);
    return game_id;
}

// Присоединение к игре
int join_game(const char *game_name, const char *player_name) {
    pthread_mutex_lock(&games_lock);
    
    for (int i = 0; i < games_count; i++) {
        if (strcmp(games[i].game_name, game_name) == 0 && games[i].active) {
            Game *game = &games[i];
            pthread_mutex_lock(&game->lock);
            
            // Ищем неактивный слот для повторного использования
            int slot = -1;
            for (int s = 0; s < game->current_players; s++) {
                if (!game->players[s].active) {
                    slot = s;
                    break;
                }
            }

            if (slot == -1) {
                // Если слотов еще нет и игра не заполнена — добавляем в конец
                if (game->current_players >= game->max_players) {
                    pthread_mutex_unlock(&game->lock);
                    pthread_mutex_unlock(&games_lock);
                    return -1; // Игра заполнена
                }
                slot = game->current_players;
                game->current_players++;
            }

            // Добавляем игрока в найденный слот
            strncpy(game->players[slot].name, player_name, MAX_NAME_LEN - 1);
            game->players[slot].name[MAX_NAME_LEN-1] = '\0';
            game->players[slot].active = 1;
            game->players[slot].attempts = 0;
            
            pthread_mutex_unlock(&game->lock);
            pthread_mutex_unlock(&games_lock);
            return i;
        }
    }
    
    pthread_mutex_unlock(&games_lock);
    return -2; // Игра не найдена
}

// Получение списка активных игр
void get_games_list(char *buffer) {
    pthread_mutex_lock(&games_lock);
    
    buffer[0] = '\0';
    strcat(buffer, "Активные игры:\n");
    
    int found = 0;
    for (int i = 0; i < games_count; i++) {
        if (games[i].active) {
            char line[256];
            snprintf(line, sizeof(line), "- %s (%d/%d игроков)\n", 
                     games[i].game_name, games[i].current_players, games[i].max_players);
            strcat(buffer, line);
            found = 1;
        }
    }
    
    if (!found) {
        strcat(buffer, "Нет активных игр\n");
    }
    
    pthread_mutex_unlock(&games_lock);
}

// Обработка попытки угадать слово
void make_guess(int game_id, const char *player_name, const char *guess, char *response) {
    if (game_id < 0 || game_id >= games_count) {
        strcpy(response, "ERROR:Игра не найдена");
        return;
    }
    
    Game *game = &games[game_id];
    pthread_mutex_lock(&game->lock);
    
    if (!game->active) {
        strcpy(response, "ERROR:Игра неактивна");
        pthread_mutex_unlock(&game->lock);
        return;
    }
    
    if (game->winner_found) {
        strcpy(response, "ERROR:Игра уже завершена");
        pthread_mutex_unlock(&game->lock);
        return;
    }
    
    // Находим игрока
    int player_idx = -1;
    for (int i = 0; i < game->current_players; i++) {
        if (strcmp(game->players[i].name, player_name) == 0) {
            player_idx = i;
            break;
        }
    }
    
    if (player_idx == -1) {
        strcpy(response, "ERROR:Игрок не найден в игре");
        pthread_mutex_unlock(&game->lock);
        return;
    }
    
    game->players[player_idx].attempts++;

    /* Конвертируем угадываемое слово в wide string */
    wchar_t wguess[MAX_WORD_LEN];
    if (utf8_to_wcs(guess, wguess, MAX_WORD_LEN) != 0) {
        strcpy(response, "ERROR:Некорректная кодировка слова");
        pthread_mutex_unlock(&game->lock);
        return;
    }

    /* Приводим ввод к нижнему регистру (wide) для корректного сравнения */
    for (size_t i = 0; i < wcslen(wguess); i++) {
        wguess[i] = towlower(wguess[i]);
    }

    size_t secret_len = wcslen(game->secret_word);
    size_t guess_len = wcslen(wguess);

    if (guess_len != secret_len) {
        snprintf(response, BUFFER_SIZE, "ERROR:Слово должно содержать %zu букв", secret_len);
        pthread_mutex_unlock(&game->lock);
        return;
    }

    int bulls, cows;
    count_bulls_and_cows(game->secret_word, wguess, &bulls, &cows);

    if (bulls == (int)secret_len) {
        game->winner_found = 1;
        game->active = 0;
        char secret_mb[MAX_WORD_BYTES];
        if (wcs_to_utf8(game->secret_word, secret_mb, sizeof(secret_mb)) != 0) {
            strcpy(secret_mb, "(ошибка кодировки)");
        }
        snprintf(response, BUFFER_SIZE, "WIN:Поздравляем! Вы угадали слово '%s' за %d попыток!", 
                 secret_mb, game->players[player_idx].attempts);
    } else {
        snprintf(response, BUFFER_SIZE, "RESULT:Быки: %d, Коровы: %d (попытка %d)", 
                 bulls, cows, game->players[player_idx].attempts);
    }
    
    pthread_mutex_unlock(&game->lock);
}

// Выход игрока из игры
void leave_game(int game_id, const char *player_name, char *response) {
    if (game_id < 0 || game_id >= games_count) {
        strcpy(response, "ERROR:Игра не найдена");
        return;
    }
    
    Game *game = &games[game_id];
    pthread_mutex_lock(&game->lock);
    
    // Находим игрока и помечаем как неактивного
    int player_idx = -1;
    for (int i = 0; i < game->current_players; i++) {
        if (strcmp(game->players[i].name, player_name) == 0) {
            player_idx = i;
            game->players[i].active = 0;
            break;
        }
    }
    
    if (player_idx == -1) {
        strcpy(response, "ERROR:Игрок не найден в игре");
        pthread_mutex_unlock(&game->lock);
        return;
    }
    
    // Проверяем, остались ли активные игроки
    int active_players = 0;
    for (int i = 0; i < game->current_players; i++) {
        if (game->players[i].active) {
            active_players++;
        }
    }
    
    if (active_players == 0) {
        game->active = 0;
        strcpy(response, "OK:Вы вышли из игры. Игра завершена (нет активных игроков)");
    } else {
        strcpy(response, "OK:Вы вышли из игры. Игра продолжается");
    }
    
    pthread_mutex_unlock(&game->lock);
}

// Основной цикл сервера
void run_server() {
    context = zmq_ctx_new();
    responder = zmq_socket(context, ZMQ_REP);
    int rc = zmq_bind(responder, "tcp://*:5555");
    
    if (rc != 0) {
        fprintf(stderr, "Ошибка привязки сокета: %s\n", zmq_strerror(errno));
        return;
    }
    
    printf("Сервер запущен на порту 5555\n");
    printf("Ожидание подключений...\n\n");
    
    while (1) {
        char buffer[BUFFER_SIZE];
        int size = zmq_recv(responder, buffer, BUFFER_SIZE - 1, 0);
        
        if (size == -1) {
            continue;
        }
        
        buffer[size] = '\0';
        printf("Получено: %s\n", buffer);
        
        char response[BUFFER_SIZE];
        char command[64], arg1[MAX_NAME_LEN], arg2[MAX_NAME_LEN], arg3[MAX_NAME_LEN];
        
        sscanf(buffer, "%s %s %s %s", command, arg1, arg2, arg3);
        
        if (strcmp(command, "LIST") == 0) {
            get_games_list(response);
        } 
        else if (strcmp(command, "CREATE") == 0) {
            // CREATE <game_name> <max_players> <creator_name>
            int max_players = atoi(arg2);
            if (max_players <= 0 || max_players > MAX_PLAYERS) {
                max_players = 2;
            }
            
            int game_id = create_game(arg1, max_players, arg3);
            if (game_id == -1) {
                strcpy(response, "ERROR:Достигнуто максимальное количество игр");
            } else if (game_id == -2) {
                strcpy(response, "ERROR:Игра с таким именем уже существует");
            } else {
                size_t wlen = wcslen(games[game_id].secret_word);
                snprintf(response, BUFFER_SIZE, "OK:Игра '%s' создана (ID: %d, загаданное слово: %zu букв)", 
                         arg1, game_id, wlen);
            }
        } 
        else if (strcmp(command, "JOIN") == 0) {
            // JOIN <game_name> <player_name>
            int game_id = join_game(arg1, arg2);
            if (game_id == -1) {
                strcpy(response, "ERROR:Игра заполнена");
            } else if (game_id == -2) {
                strcpy(response, "ERROR:Игра не найдена");
            } else {
                size_t wlen = wcslen(games[game_id].secret_word);
                snprintf(response, BUFFER_SIZE, "OK:Присоединились к игре '%s' (ID: %d, загаданное слово: %zu букв)", 
                         arg1, game_id, wlen);
            }
        } 
        else if (strcmp(command, "GUESS") == 0) {
            // GUESS <game_id> <player_name> <word>
            int game_id = atoi(arg1);
            make_guess(game_id, arg2, arg3, response);
        } 
        else if (strcmp(command, "LEAVE") == 0) {
            // LEAVE <game_id> <player_name>
            int game_id = atoi(arg1);
            leave_game(game_id, arg2, response);
        } 
        else {
            strcpy(response, "ERROR:Неизвестная команда");
        }
        
        printf("Отправлено: %s\n\n", response);
        zmq_send(responder, response, strlen(response), 0);
    }
    
    zmq_close(responder);
    zmq_ctx_destroy(context);
}

int main() {
    /* Устанавливаем локаль для корректной работы с UTF-8/wide-strings */
    setlocale(LC_CTYPE, "");
    /* Инициализируем генератор случайных чисел один раз */
    srand((unsigned int)time(NULL));

    printf("=== Сервер игры 'Быки и Коровы' ===\n\n");
    run_server();
    return 0;
}
