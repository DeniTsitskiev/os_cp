#!/bin/bash

# Скрипт для запуска и тестирования приложения

set -e

echo "========================================="
echo "  Тестирование игры 'Быки и Коровы (слова)'"
echo "========================================="
echo ""

# Проверка сборки
echo "[1] Проверка сборки..."
if [ ! -f "src/server" ] || [ ! -f "src/client" ]; then
    echo "Скомпилированные файлы не найдены. Компилирую..."
    (cd src && make clean && make all)
else
    echo "✓ Исполняемые файлы найдены"
fi

# Запуск сервера (логируем вывод для извлечения секрета игры)
echo ""
echo "[2] Запуск сервера..."
LOG_FILE="$PWD/server_test.log"
> "$LOG_FILE"
(cd src && ./server) > "$LOG_FILE" 2>&1 &
SERVER_PID=$!
echo "✓ Сервер запущен (PID: $SERVER_PID, лог: $LOG_FILE)"
trap 'kill $SERVER_PID 2>/dev/null; wait $SERVER_PID 2>/dev/null' EXIT

# Даем серверу время на инициализацию
sleep 2

# Функция для запуска клиента с автоматическим вводом
run_client() {
    local player_name=$1
    local game_name=$2
    local action=$3
    
    echo ""
    echo "--- Тестирование клиента: $player_name ---"
    
    case $action in
        "create")
            # Создание игры (русские подсказки)
            (
                sleep 0.5; echo "$player_name"
                sleep 0.5; echo "1"
                sleep 0.5; echo "$game_name"
                sleep 0.5; echo "2"
                sleep 1; echo "house"
                sleep 0.5; echo "quit"
                sleep 0.5; echo "4"
            ) | (cd src && ./client)
            ;;
        "join")
            # Присоединение к игре
            (
                sleep 0.5; echo "$player_name"
                sleep 0.5; echo "2"
                sleep 0.5; echo "$game_name"
                sleep 1; echo "heart"
                sleep 0.5; echo "quit"
                sleep 0.5; echo "4"
            ) | (cd src && ./client)
            ;;
        "list")
            # Список игр
            (
                sleep 0.5; echo "$player_name"
                sleep 0.5; echo "3"
                sleep 0.5; echo "4"
            ) | (cd src && ./client)
            ;;
    esac
}

# Ожидание секрета созданной игры по логам сервера
wait_secret() {
    local game_name=$1
    local secret=""
    for _ in {1..100}; do
        secret=$(grep -m1 "Создана игра '$game_name'" "$LOG_FILE" | sed -E 's/.*секрет: ([a-z]+)/\1/')
        if [ -n "$secret" ]; then
            echo "$secret"
            return 0
        fi
        sleep 0.2
    done
    return 1
}

# Тест выигрыша: создаём игру, ждём секрет из лога, угадываем его и выходим
win_game() {
    local player_name=$1
    local game_name=${2:-WinGame_$RANDOM}

    coproc CPROC { cd src && ./client; }
    exec 3>&${CPROC[1]}
    exec 4<&${CPROC[0]}

    # Ввод имени и создание игры на 2 слота
    printf "%s\n" "$player_name" >&3; sleep 0.4
    printf "1\n" >&3; sleep 0.4
    printf "%s\n" "$game_name" >&3; sleep 0.4
    printf "2\n" >&3; sleep 0.6

    # Ждём секрет из лога
    local secret
    secret=$(wait_secret "$game_name") || { echo "Не удалось получить секрет для $game_name"; kill ${CPROC_PID:-0} 2>/dev/null; return 1; }

    # Угадываем секрет с первой попытки
    printf "%s\n" "$secret" >&3; sleep 0.4
    printf "quit\n" >&3; sleep 0.4
    printf "4\n" >&3; sleep 0.4

    exec 3>&-
    exec 4<&-
    wait ${CPROC_PID:-0} 2>/dev/null || true
}

# Запуск тестов
echo ""
echo "[3] Запуск тестов..."

# Тест 1: Создание игры
run_client "Player1" "TestGame" "create"

# Тест 2: Присоединение к игре
run_client "Player2" "TestGame" "join"

# Тест 3: Список игр
run_client "Player3" "DummyGame" "list"

# Тест 4: Победа (5 быков) в новой игре
win_game "PlayerWin" "WinGame"

# Автопоиска больше нет

# Остановка сервера
echo ""
echo "[4] Остановка сервера..."
kill $SERVER_PID 2>/dev/null || true
sleep 1

cd ..

echo ""
echo "========================================="
echo "  Тестирование завершено"
echo "========================================="
