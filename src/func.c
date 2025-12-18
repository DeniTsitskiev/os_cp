#include "func.h"

// Инициализирует сообщение: обнуляет всю память структуры
// Параметры: m - указатель на структуру Msg
// Возвращает: ничего (void)
void msg_create(Msg *m) {
    memset(m, 0, sizeof(Msg));
}

// Отправляет структуру сообщения через ZeroMQ сокет
// Параметры: sock - ZeroMQ сокет, m - указатель на Msg
// Возвращает: результат zmq_send (количество байт или -1)
int msg_send(void *sock, Msg *m) {
    return zmq_send(sock, m, sizeof(Msg), 0);
}

// Получает структуру сообщения из ZeroMQ сокета
// Параметры: sock - ZeroMQ сокет, m - указатель на Msg для заполнения
// Возвращает: результат zmq_recv (количество байт или -1)
int msg_recv(void *sock, Msg *m) {
    return zmq_recv(sock, m, sizeof(Msg), 0);
}

const char *words_pool[] = {
    "house", "plant", "water", "music", "stone",
    "bread", "beach", "cloud", "dream", "earth",
    "field", "flame", "frost", "glass", "happy",
    "horse", "light", "magic", "metal", "night",
    "ocean", "peace", "queen", "river", "sound",
    "study", "sugar", "table", "video", "world",
    "young", "zebra", "alien", "beast", "chain",
    "delta", "eagle", "faith", "ghost", "heart"
};

// Генерирует случайное 5-буквенное слово из встроенного словаря
// Параметры: word - буфер для результата (минимум 6 байт)
// Использует rand() для выбора индекса из words_pool
void gen_word(char *word) {
    srand(time(NULL) ^ (unsigned int)(uintptr_t)word);
    int idx = rand() % (sizeof(words_pool) / sizeof(words_pool[0]));
    strcpy(word, words_pool[idx]);
}

// Считает быков (точное совпадение позиции) и коров (буква есть но позиция другая)
// Параметры: secret - загаданное слово, guess - попытка, bulls - указатель на счетчик, cows - указатель на счетчик
// Логика: быки считаются по прямому совпадению, коровы - по частотам букв минус быки
void check_word(const char *secret, const char *guess, int *bulls, int *cows) {
    *bulls = 0;
    *cows = 0;
    
    int secret_cnt[26] = {0};
    int guess_cnt[26] = {0};
    
    // Сначала подсчитываем быки и убираем эти буквы
    for (int i = 0; i < WORD_LENGTH; i++) {
        if (secret[i] == guess[i]) {
            (*bulls)++;
        } else {
            secret_cnt[secret[i] - 'a']++;
            guess_cnt[guess[i] - 'a']++;
        }
    }
    
    // Теперь подсчитываем коров
    for (int i = 0; i < 26; i++) {
        int common = secret_cnt[i] < guess_cnt[i] ? secret_cnt[i] : guess_cnt[i];
        *cows += common;
    }
}

// Проверяет корректность слова: длина 5, все буквы a-z, наличие в словаре
// Параметры: word - проверяемое слово
// Возвращает 1 если слово валидно, 0 иначе
int word_ok(const char *word) {
    if (strlen(word) != WORD_LENGTH) {
        return 0;
    }
    
    for (int i = 0; i < WORD_LENGTH; i++) {
        if (word[i] < 'a' || word[i] > 'z') {
            return 0;
        }
    }
    
    // Проверяем, есть ли слово в пуле
    for (size_t i = 0; i < sizeof(words_pool) / sizeof(words_pool[0]); i++) {
        if (strcmp(word, words_pool[i]) == 0) {
            return 1;
        }
    }
    
    return 0;
}