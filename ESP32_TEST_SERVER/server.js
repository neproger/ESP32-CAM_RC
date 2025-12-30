const express = require('express');
const WebSocket = require('ws');
const http = require('http');
const screenshot = require('screenshot-desktop');

const app = express();
const port = 8888;
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

wss.on('connection', (ws) => {
    console.log('[WS] Подключено устройство управления');
    var currentControls = [0, 0, 0, 0, 0]; // Вперед, Назад, Влево, Вправо, Стоп
    ws.on('message', (data) => {
        // Если данные бинарные (наш массив состояния)
        if (Buffer.isBuffer(data)) {
            const state = Array.from(data);
            if (currentControls.toString() !== state.toString()) {
                currentControls = state;
                // state[0] - Вперед, [1] - Назад, [2] - Влево, [3] - Вправо, [4] - Стоп
                console.log(`[RC State] UP:${state[0]} DOWN:${state[1]} LEFT:${state[2]} RIGHT:${state[3]} STOP:${state[4]}`);
            }
        } else {
            console.log(`[WS Text] ${data}`);
        }
    });

    let isSending = false;
    const sendScreen = () => {
        if (ws.readyState === WebSocket.OPEN && !isSending) {
            isSending = true;
            screenshot({ format: 'jpg' }).then((img) => {
                ws.send(img, () => {
                    isSending = false;
                    setTimeout(sendScreen, 200);
                });
            }).catch(() => {
                isSending = false;
                setTimeout(sendScreen, 1000);
            });
        }
    };
    sendScreen();
});

server.listen(port, '0.0.0.0', () => {
    console.log(`🚀 RC Сервер запущен на порту ${port}`);
});
