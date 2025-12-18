#ifndef FUNC_H
#define FUNC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <zmq.h>

#define MAX_GAME_ID 64
#define MAX_USERNAME 32
#define MAX_GAME_PLAYERS 10
#define WORD_LENGTH 5
#define MAX_ATTEMPTS 100

// Сообщения от клиента
typedef enum {
    MSG_NEW_GAME = 1,
    MSG_JOIN_BY_ID = 2,
    MSG_MAKE_TRY = 3,
    MSG_QUIT_GAME = 4,
    MSG_GET_GAMES = 5,
    
    // Ответы сервера
    MSG_GAME_OK = 10,
    MSG_JOINED_OK = 11,
    MSG_TRY_RESULT = 12,
    MSG_WIN = 13,
    MSG_GAMES_LIST = 14,
    MSG_FAIL = 20,
} MsgType;

// Результат попытки
typedef struct {
    int bulls;
    int cows;
    int try_num;
    char who[MAX_USERNAME];
} TryRes;

// Сообщение
typedef struct {
    MsgType cmd;
    char game_id[MAX_GAME_ID];
    char user_name[MAX_USERNAME];
    int player_cnt;
    char word[WORD_LENGTH + 1];
    TryRes res;
    char msg[256];
    int total_games;
} Msg;

// Функции
void msg_create(Msg *m);
int msg_send(void *sock, Msg *m);
int msg_recv(void *sock, Msg *m);

void gen_word(char *word);
void check_word(const char *secret, const char *guess, int *bulls, int *cows);
int word_ok(const char *word);

#endif