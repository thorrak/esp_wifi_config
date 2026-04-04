const http = require("http");
const crypto = require("crypto");
const { execFile } = require("child_process");

const PORT = 9000;
const SECRET = process.env.WEBHOOK_SECRET;
const DEPLOY_SCRIPT = process.env.HOME + "/deploy.sh";

let deploying = false;

function verify(signature, body) {
  if (!signature) return false;
  const expected =
    "sha256=" +
    crypto.createHmac("sha256", SECRET).update(body).digest("hex");
  return crypto.timingSafeEqual(Buffer.from(signature), Buffer.from(expected));
}

const server = http.createServer((req, res) => {
  if (req.method !== "POST" || req.url !== "/hooks/deploy") {
    res.writeHead(404);
    return res.end("Not found");
  }

  let body = "";
  req.on("data", (chunk) => (body += chunk));
  req.on("end", () => {
    if (!verify(req.headers["x-hub-signature-256"], body)) {
      console.log(`[${new Date().toISOString()}] Invalid signature, rejecting`);
      res.writeHead(401);
      return res.end("Unauthorized");
    }

    let payload;
    try {
      payload = JSON.parse(body);
    } catch {
      res.writeHead(400);
      return res.end("Bad request");
    }

    if (payload.ref !== "refs/heads/main") {
      console.log(
        `[${new Date().toISOString()}] Push to ${payload.ref}, ignoring`
      );
      res.writeHead(200);
      return res.end("Not main branch, skipping");
    }

    if (deploying) {
      console.log(`[${new Date().toISOString()}] Deploy already in progress`);
      res.writeHead(409);
      return res.end("Deploy already in progress");
    }

    deploying = true;
    console.log(`[${new Date().toISOString()}] Deploy triggered`);
    res.writeHead(200);
    res.end("Deploy started");

    execFile("/bin/bash", [DEPLOY_SCRIPT], (error) => {
      deploying = false;
      if (error) {
        console.error(`[${new Date().toISOString()}] Deploy failed:`, error.message);
      } else {
        console.log(`[${new Date().toISOString()}] Deploy completed`);
      }
    });
  });
});

server.listen(PORT, "0.0.0.0", () => {
  console.log(`Webhook listener running on 0.0.0.0:${PORT}`);
});
