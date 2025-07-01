console.log("✅ Server ready! Open dashboard.html in your browser"); 
const WebSocket = require("ws");
const fs = require("fs");
const path = require("path");

const PORT = 8080;
const CSV_FILE = "arbitrage_opportunities.csv";

// Create WebSocket server
const wss = new WebSocket.Server({ port: PORT });

console.log(`🚀 ArbiSim WebSocket Bridge running on ws://localhost:${PORT}`);
console.log(`📄 Watching file: ${CSV_FILE}`);

let connectedClients = [];
let lastPosition = 0;

// Track latest prices for each exchange
let exchangePrices = {
  binance: 50000,
  coinbase: 50000,
  kraken: 50000,
  bybit: 50000,
};

function watchCSVFile() {
  if (!fs.existsSync(CSV_FILE)) {
    console.log(`⚠️  ${CSV_FILE} not found. Waiting...`);
    return;
  }

  const stats = fs.statSync(CSV_FILE);
  if (stats.size > lastPosition) {
    const stream = fs.createReadStream(CSV_FILE, { start: lastPosition });
    let buffer = "";

    stream.on("data", (chunk) => {
      buffer += chunk.toString();
      const lines = buffer.split("\n");
      buffer = lines.pop(); // Keep incomplete line

      lines.forEach((line) => {
        if (line.trim() && !line.startsWith("timestamp")) {
          const opportunity = parseCSVLine(line);
          if (opportunity) {
            broadcastOpportunity(opportunity);
            updateExchangePrices(opportunity);
          }
        }
      });
    });

    stream.on("end", () => {
      lastPosition = stats.size;
    });
  }
}

function updateExchangePrices(opportunity) {
  // Update exchange prices based on opportunity data
  exchangePrices[opportunity.opportunity.buy_exchange] =
    opportunity.opportunity.buy_price;
  exchangePrices[opportunity.opportunity.sell_exchange] =
    opportunity.opportunity.sell_price;
}

function parseCSVLine(line) {
  const parts = line.split(",");
  if (parts.length < 10) return null;

  try {
    return {
      type: "opportunity",
      opportunity: {
        symbol: parts[1],
        buy_exchange: parts[2],
        sell_exchange: parts[3],
        buy_price: parseFloat(parts[4]),
        sell_price: parseFloat(parts[5]),
        profit_bps: parseFloat(parts[6]),
        net_profit_bps: parseFloat(parts[7]) || parseFloat(parts[6]) - 20,
        latency_ns: parseInt(parts[8]),
        approved: parts[9] === "0", // 0 = APPROVED in your system
        detected_at_ns: parseInt(parts[0]),
      },
    };
  } catch (error) {
    console.log("❌ Error parsing CSV line:", error);
    return null;
  }
}

function broadcastOpportunity(opportunity) {
  const message = JSON.stringify(opportunity);
  console.log(
    `📤 Broadcasting opportunity: ${
      opportunity.opportunity.symbol
    } ${opportunity.opportunity.profit_bps.toFixed(1)}bps`
  );

  connectedClients.forEach((ws, index) => {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(message);
    } else {
      console.log(`🔌 Removing dead client ${index}`);
      connectedClients.splice(index, 1);
    }
  });
}

function broadcastPriceUpdate(exchange, price) {
  const message = JSON.stringify({
    type: "price_update",
    exchange: exchange,
    price: price,
  });

  connectedClients.forEach((ws) => {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(message);
    }
  });
}

function sendMetricsUpdate() {
  const message = JSON.stringify({
    type: "metrics",
    metrics: {
      total_opportunities: Math.floor(Math.random() * 1000) + 500,
      avg_latency: 15 + Math.random() * 20,
      daily_pnl: Math.random() * 2000,
      win_rate: 80 + Math.random() * 15,
    },
  });

  connectedClients.forEach((ws) => {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(message);
    }
  });
}

// Handle WebSocket connections
wss.on("connection", function connection(ws) {
  console.log("📱 Dashboard client connected");
  connectedClients.push(ws);

  // Send welcome message
  ws.send(
    JSON.stringify({
      type: "test",
      message: "Connected to ArbiSim Node.js bridge!",
    })
  );

  // Send current exchange prices immediately
  Object.keys(exchangePrices).forEach((exchange) => {
    const priceMessage = JSON.stringify({
      type: "price_update",
      exchange: exchange,
      price: exchangePrices[exchange],
    });
    ws.send(priceMessage);
    console.log(
      `📈 Sent ${exchange} price: ${exchangePrices[exchange].toFixed(2)}`
    );
  });

  // Handle client disconnect
  ws.on("close", function close() {
    console.log("📱 Dashboard client disconnected");
    const index = connectedClients.indexOf(ws);
    if (index > -1) {
      connectedClients.splice(index, 1);
    }
  });

  ws.on("error", function error(err) {
    console.log("❌ WebSocket error:", err);
  });
});

// Watch CSV file every 500ms
setInterval(watchCSVFile, 500);

// Send metrics updates every 3 seconds when clients are connected
setInterval(() => {
  if (connectedClients.length > 0) {
    sendMetricsUpdate();
    console.log(`📊 Sent metrics update to ${connectedClients.length} clients`);
  }
}, 3000);

// Send price updates for exchanges not getting opportunities
setInterval(() => {
  Object.keys(exchangePrices).forEach((exchange) => {
    // Add small random movement
    exchangePrices[exchange] += (Math.random() - 0.5) * 10;
    broadcastPriceUpdate(exchange, exchangePrices[exchange]);
  });
}, 3000);

// Send a test opportunity every 5 seconds when clients are connected
setInterval(() => {
  if (connectedClients.length > 0) {
    const testOpportunity = {
      type: "opportunity",
      opportunity: {
        symbol: "BTCUSDT",
        buy_exchange: "binance",
        sell_exchange: "coinbase",
        buy_price: 49000 + Math.random() * 1000,
        sell_price: 50000 + Math.random() * 1000,
        profit_bps: 20 + Math.random() * 50,
        net_profit_bps: 10 + Math.random() * 30,
        latency_ns: 15000 + Math.random() * 10000,
        approved: Math.random() > 0.5,
        detected_at_ns: Date.now() * 1000000,
      },
    };

    broadcastOpportunity(testOpportunity);
    console.log(`🧪 Sent test opportunity to dashboard`);
  }
}, 5000);

// Log client count every 10 seconds
setInterval(() => {
  console.log(`👥 Connected clients: ${connectedClients.length}`);
  if (connectedClients.length > 0) {
    console.log("📡 Dashboard is connected - sending live data");
  } else {
    console.log("⚠️  No dashboard connected - opportunities being missed");
  }
}, 10000);
