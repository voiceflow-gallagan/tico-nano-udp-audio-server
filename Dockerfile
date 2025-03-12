FROM node:18-slim

WORKDIR /app

COPY package*.json ./
RUN npm install

COPY . .

# Expose both UDP and TCP ports
EXPOSE 6980/udp
EXPOSE 12345/tcp

CMD ["node", "server.js"]
