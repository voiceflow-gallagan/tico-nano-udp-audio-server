FROM node:20-slim

# Install lame package
RUN apt-get update && \
    apt-get install -y lame && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY package*.json ./
RUN npm install

COPY . .

# Expose both UDP and TCP ports
EXPOSE 6980/udp
EXPOSE 12345/tcp

CMD ["node", "server.js"]
