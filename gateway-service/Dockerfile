FROM node:20-slim

WORKDIR /app

COPY gateway-service/package.json ./
RUN npm install --omit=dev

COPY gateway-service/src ./src
COPY proto ./proto
COPY gateway-service/public ./public
COPY proto ./proto

EXPOSE 8080

CMD ["node", "src/server.js"]
