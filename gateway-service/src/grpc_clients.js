const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');
const path = require('path');

const PROTO_DIR = path.join(__dirname, '..', 'proto');

const loaderOptions = {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
};

const playerDef = protoLoader.loadSync(path.join(PROTO_DIR, 'player.proto'), loaderOptions);
const matchDef = protoLoader.loadSync(path.join(PROTO_DIR, 'match.proto'), loaderOptions);

const playerProto = grpc.loadPackageDefinition(playerDef).game.player;
const matchProto = grpc.loadPackageDefinition(matchDef).game.match;

const PLAYER_SERVICE_ADDR = process.env.PLAYER_SERVICE_ADDR || 'localhost:50051';
const MATCH_SERVICE_ADDR = process.env.MATCH_SERVICE_ADDR || 'localhost:50052';

const playerClient = new playerProto.PlayerService(
  PLAYER_SERVICE_ADDR,
  grpc.credentials.createInsecure()
);

const matchClient = new matchProto.MatchService(
  MATCH_SERVICE_ADDR,
  grpc.credentials.createInsecure()
);

// Wraps a unary gRPC call with a deadline so a hung downstream service
// can't block the gateway's event loop indefinitely (fault tolerance).
function callWithDeadline(client, method, request, timeoutMs = 2000) {
  return new Promise((resolve, reject) => {
    const deadline = new Date(Date.now() + timeoutMs);
    client[method](request, { deadline }, (err, response) => {
      if (err) return reject(err);
      resolve(response);
    });
  });
}

module.exports = {
  playerClient,
  matchClient,
  callWithDeadline,
  PLAYER_SERVICE_ADDR,
  MATCH_SERVICE_ADDR,
};
