FROM node:20-slim

WORKDIR /app

COPY gateway-service/package.json ./
RUN npm install --omit=dev

COPY proto /app/proto
COPY gateway-service/src ./src
COPY gateway-service/public ./public

EXPOSE 8080
CMD ["node", "src/server.js"]
