const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const path = require('path');
const fs = require('fs');
const crypto = require('crypto');

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

app.use(express.static(path.join(__dirname, 'public')));
server.listen(8080, () => {
  console.log('Server running on http://localhost:8080');
});

const agentClients = new Map(); // agentId -> { socket, name, connectedAt, ip }
const browserClients = new Set();
const agentTime = new Map(); // agentId -> totalSeconds

function getAgentId(user, ip) {
  return crypto.createHash('md5').update(`${user}/${ip}`).digest('hex');
}

function broadcastToBrowsers(data) {
  const message = JSON.stringify(data);
  browserClients.forEach((ws) => {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(message);
    }
  });
}

wss.on('connection', (ws, req) => {
  const ip = req.socket.remoteAddress;
  let isBrowser = false;

  ws.on('message', (data) => {
    let msg;
    try {
      msg = JSON.parse(data);
    } catch (err) {
      console.error('Invalid message:', err);
      return;
    }

    if (msg.type === 'viewer') {
      isBrowser = true;
      browserClients.add(ws);
      console.log("[Browser] connected");

      for (const [agentId, info] of agentClients.entries()) {
        broadcastToBrowsers({
          type: "connected",
          id: agentId,
          connectedAt: info.connectedAt.toISOString(),
          name: `${info.name}/${info.ip}`
        });
      }
      return;
    }

    if (msg.type === 'hello') {
      const name = msg.user || 'Пользователь';
      const agentId = getAgentId(name, ip);
      const connectedAt = new Date();

      console.log(`[Agent ${agentId}] says: ${msg.message}`);

      agentClients.set(agentId, {
        socket: ws,
        name,
        connectedAt,
        ip
      });

      broadcastToBrowsers({
        type: 'connected',
        id: agentId,
        connectedAt: connectedAt.toISOString(),
        name: `${name}/${ip}`
      });
    }

    if (msg.type === 'screenshot') {
      for (const [agentId, info] of agentClients.entries()) {
        if (info.socket === ws) {
          const buffer = Buffer.from(msg.image, 'base64');
          fs.writeFileSync(`screenshot_${agentId}.png`, buffer);

          broadcastToBrowsers({
            type: 'screenshot',
            id: agentId,
            image: msg.image
          });
        }
      }
    }

    if (msg.type === 'request_screenshot') {
      const targetId = msg.id;
      const targetInfo = agentClients.get(targetId);
      if (targetInfo && targetInfo.socket.readyState === WebSocket.OPEN) {
        targetInfo.socket.send(JSON.stringify({ type: 'request_screenshot' }));
      }
    }
  });

  ws.on('close', () => {
    if (isBrowser) {
      browserClients.delete(ws);
    } else {
      for (const [agentId, info] of agentClients.entries()) {
        if (info.socket === ws) {
          const duration = Math.floor((Date.now() - info.connectedAt.getTime()) / 1000);
          const prev = agentTime.get(agentId) || 0;
          const total = prev + duration;
          agentTime.set(agentId, total);

          fs.writeFileSync(`time_${agentId}.txt`, `User: ${info.name}\nTotal time: ${total} seconds`);

          agentClients.delete(agentId);

          broadcastToBrowsers({
            type: "disconnected",
            id: agentId,
            duration,
            total
          });

          console.log(`[Agent ${agentId}] отключён по ws.close. Сессия: ${duration} сек. Всего: ${total} сек`);
        }
      }
    }
  });
});
