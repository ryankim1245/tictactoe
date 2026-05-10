#!/bin/bash

BROKER="localhost"

TOPIC_BOARD="tictactoe/game/board"
TOPIC_TURN="tictactoe/game/turn"
TOPIC_RESULT="tictactoe/game/result"
TOPIC_O_MOVE="tictactoe/player/o/move"

BOARD="1 2 3 4 5 6 7 8 9"

echo "Random O player started."
echo "Listening for board and turn updates..."

while read -r TOPIC MESSAGE
do
    echo "Received: $TOPIC $MESSAGE"

    if [ "$TOPIC" = "$TOPIC_RESULT" ]; then
        if echo "$MESSAGE" | grep -q "wins\|Draw"; then
            echo "Game over detected. Bot exiting."
            exit 0
        fi
    fi

    if [ "$TOPIC" = "$TOPIC_BOARD" ]; then
        BOARD="$MESSAGE"
    fi

    if [ "$TOPIC" = "$TOPIC_TURN" ]; then
        if echo "$MESSAGE" | grep -q "O"; then
            echo "It is O's turn."

            AVAILABLE=$(echo "$BOARD" | tr ' ' '\n' | grep '^[1-9]$')

            if [ -z "$AVAILABLE" ]; then
                echo "No available moves. Bot exiting."
                exit 0
            fi

            MOVE=$(echo "$AVAILABLE" | shuf -n 1)

            echo "Bot chose position $MOVE"

            sleep 1

            mosquitto_pub -h "$BROKER" -t "$TOPIC_O_MOVE" -m "{\"position\":$MOVE,\"turn\":\"O\"}"
        fi
    fi
done < <(mosquitto_sub -h "$BROKER" -t "$TOPIC_BOARD" -t "$TOPIC_TURN" -t "$TOPIC_RESULT" -v)
