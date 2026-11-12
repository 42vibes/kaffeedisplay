import http from "node:http";

const PORT = Number(process.env.MOCK_PORT ?? 8787);
const now = Math.floor(Date.now() / 1000);

const users = [
  { id: "usr_ada", name: "Ada", color: "#D4A373", active: true, created_at: now - 86400 * 90 },
  { id: "usr_bob", name: "Bob", color: "#8FBC8F", active: true, created_at: now - 86400 * 60 },
  { id: "usr_cara", name: "Cara", color: "#7FA7C9", active: true, created_at: now - 86400 * 30 },
];

const products = [
  {
    id: "prd_kaffee",
    name: "Kaffee",
    category: "Kaffee",
    icon: "cup",
    color: "#D4A373",
    price_cents: 50,
    sort: 0,
    active: true,
    created_at: now,
    updated_at: now,
  },
  {
    id: "prd_espresso",
    name: "Espresso",
    category: "Kaffee",
    icon: "espresso",
    color: "#B5835A",
    price_cents: 40,
    sort: 1,
    active: true,
    created_at: now,
    updated_at: now,
  },
  {
    id: "prd_kakao",
    name: "Kakao",
    category: "Tee",
    icon: "cocoa",
    color: "#9B6B43",
    price_cents: 80,
    sort: 2,
    active: true,
    created_at: now,
    updated_at: now,
  },
  {
    id: "prd_tee",
    name: "Tee",
    category: "Tee",
    icon: "tea",
    color: "#8FBC8F",
    price_cents: 30,
    sort: 3,
    active: true,
    created_at: now,
    updated_at: now,
  },
];

const balances = { usr_ada: 1250, usr_bob: -350, usr_cara: 0 };
const expenses = [
  {
    id: "exp_machine",
    ts: now - 86400 * 20,
    category: "Maschine",
    description: "Kaffeemaschine",
    vendor: "Kaffee Shop",
    amount_cents: 45000,
    note: "",
    voided: false,
  },
  {
    id: "exp_service",
    ts: now - 86400 * 5,
    category: "Service",
    description: "Wartung",
    vendor: "Technik GmbH",
    amount_cents: 8000,
    note: "",
    voided: false,
  },
];

const events = [
  {
    seq: 12,
    id: "evt_12",
    ts: now - 60,
    time_source: "synced",
    type: "purchase",
    user_id: "usr_ada",
    user_name: "Ada",
    product_id: "prd_kaffee",
    product_name: "Kaffee",
    product_category: "Kaffee",
    unit_amount_cents: 50,
    amount_cents: -50,
    payment: "balance",
    note: "",
    actor: "ui",
    voided: false,
  },
  {
    seq: 11,
    id: "evt_11",
    ts: now - 300,
    time_source: "synced",
    type: "purchase",
    user_id: "usr_bob",
    user_name: "Bob",
    product_id: "prd_tea",
    product_name: "Tee",
    product_category: "Tee",
    unit_amount_cents: 30,
    amount_cents: -30,
    payment: "balance",
    note: "",
    actor: "ui",
    voided: false,
  },
  {
    seq: 10,
    id: "evt_10",
    ts: now - 600,
    time_source: "synced",
    type: "guest_sale",
    product_id: "prd_kakao",
    product_name: "Kakao",
    product_category: "Tee",
    unit_amount_cents: 80,
    amount_cents: 80,
    payment: "cash",
    note: "",
    actor: "ui",
    voided: false,
  },
  {
    seq: 9,
    id: "evt_9",
    ts: now - 7200,
    time_source: "synced",
    type: "topup",
    user_id: "usr_ada",
    user_name: "Ada",
    amount_cents: 1000,
    payment: "cash",
    note: "Bar eingezahlt",
    actor: "admin",
    voided: false,
  },
];

const summary = {
  cash_expected_cents: 5200,
  prepaid_cents: 1250,
  debts_cents: 350,
  net_liabilities_cents: 900,
  equity_cents: 4300,
  member_sales_cents: 80,
  guest_sales_cents: 80,
  topups_cents: 1000,
  expenses_cents: 53000,
  member_items: 2,
  guest_items: 1,
};

const device = {
  firmware_version: "0.1.0-dev",
  build_date: "2026-11-11",
  uptime_s: 86400 * 3 + 5211,
  free_heap: 184320,
  free_psram: 3400000,
  storage_used: 48210,
  storage_total: 9437184,
  temperature_c: 41.5,
  temperature_valid: true,
  mac: "f8:5b:1b:90:b8:34",
  network: {
    ap_active: false,
    ap_starting: false,
    ap_remaining_s: 0,
    ap_ssid: "Kaffeekasse-B834",
    ap_password: "",
    ap_ip: "192.168.4.1",
    sta_connected: true,
    sta_ip: "192.168.1.42",
    sta_rssi: -57,
  },
  time: {
    now,
    source: "synced",
    last_sync: now - 120,
    last_source: "browser",
    trusted: true,
  },
  drinks_today: 14,
};

const settings = {
  display: {
    rotation: 0,
    brightness_active: 100,
    brightness_idle: 20,
    dim_timeout_s: 60,
    screen_off_timeout_s: 600,
  },
  network: {
    ap_ssid: "Kaffeekasse-B834",
    ap_window_s: 900,
    hostname: "kaffeekasse",
    sta_enabled: true,
    sta_ssid: "Werkstatt",
    napt_enabled: false,
  },
  time: { timezone: "Europe/Berlin", sntp_enabled: true, sntp_server: "pool.ntp.org" },
  app: {
    currency: "EUR",
    locale: "de-DE",
    guest_enabled: true,
    max_open_debt_cents: 0,
    low_balance_warning_cents: -1000,
  },
};

function json(res, status, body) {
  const data = JSON.stringify(body);
  res.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(data),
  });
  res.end(data);
}

function csv(res, filename, body) {
  const data = "\uFEFFsep=;\r\n" + body;
  res.writeHead(200, {
    "Content-Type": "text/csv; charset=utf-8",
    "Content-Disposition": `attachment; filename="${filename}"`,
  });
  res.end(data);
}

function readBody(req) {
  return new Promise((resolve) => {
    let data = "";
    req.on("data", (chunk) => (data += chunk));
    req.on("end", () => {
      try {
        resolve(data ? JSON.parse(data) : {});
      } catch {
        resolve({});
      }
    });
  });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url ?? "/", `http://${req.headers.host}`);
  const path = url.pathname;
  const method = req.method ?? "GET";

  if (path === "/api/v1/health") return json(res, 200, { status: "ok", version: "mock" });

  if (path === "/api/v1/auth/login" && method === "POST") {
    const body = await readBody(req);
    if (body.password !== "kaffee") return json(res, 401, { code: "auth", message: "invalid" });
    res.setHeader("Set-Cookie", "kaffee_session=mock; Path=/; HttpOnly; SameSite=Strict");
    return json(res, 200, {
      username: "admin",
      expires_at: now + 3600,
      csrf_token: "mock-csrf-token",
    });
  }
  if (path === "/api/v1/auth/me") {
    return json(res, 200, {
      username: "admin",
      expires_at: now + 3600,
      csrf_token: "mock-csrf-token",
    });
  }
  if (path === "/api/v1/auth/logout") {
    res.setHeader("Set-Cookie", "kaffee_session=; Path=/; Max-Age=0");
    res.writeHead(204);
    return res.end();
  }

  if (path === "/api/v1/stats/summary") return json(res, 200, summary);
  if (path === "/api/v1/device") return json(res, 200, device);
  if (path === "/api/v1/time") return json(res, 200, device.time);
  if (path === "/api/v1/time/sync" && method === "POST") {
    const body = await readBody(req);
    return json(res, 200, {
      ...device.time,
      now: Math.floor((body.epoch_ms ?? Date.now() * 1000) / 1000),
      last_source: "browser",
      trusted: true,
    });
  }
  if (path === "/api/v1/ap") {
    return json(res, 200, {
      active: false,
      remaining_seconds: 0,
      ssid: settings.network.ap_ssid,
      password: "",
      ip: "192.168.4.1",
      url: "http://192.168.4.1",
      wifi_qr: "",
      url_qr: "",
    });
  }
  if (path === "/api/v1/settings") return json(res, 200, settings);
  if (path === "/api/v1/settings/display") return json(res, 200, settings.display);

  if (path === "/api/v1/users" && method === "GET") {
    return json(
      res,
      200,
      users.map((u) => ({ ...u, balance_cents: balances[u.id] ?? 0, last_booking_at: now - 60 })),
    );
  }
  if (path === "/api/v1/users" && method === "POST") {
    const body = await readBody(req);
    const user = {
      id: `usr_${Math.random().toString(16).slice(2, 8)}`,
      name: body.name ?? "Neu",
      color: body.color ?? "#D4A373",
      active: true,
      created_at: now,
    };
    users.push(user);
    balances[user.id] = 0;
    return json(res, 201, user);
  }
  if (path.match(/^\/api\/v1\/users\/[^/]+\/topup$/) && method === "POST") {
    const id = path.split("/")[4];
    const body = await readBody(req);
    balances[id] = (balances[id] ?? 0) + Number(body.amount_cents ?? 0);
    return json(res, 200, { id: "evt_mock", type: "topup", amount_cents: body.amount_cents });
  }

  if (path === "/api/v1/products" && method === "GET") return json(res, 200, products);
  if (path === "/api/v1/expenses" && method === "GET") return json(res, 200, expenses);
  if (path === "/api/v1/transactions" && method === "GET") return json(res, 200, events);
  if (path === "/api/v1/cash-counts" && method === "GET") return json(res, 200, []);

  if (path === "/api/v1/stats/leaderboard") {
    const month = url.searchParams.get("month") ?? "";
    return json(res, 200, {
      from: now - 86400 * 10,
      to: now,
      guest_count: 4,
      entries: [
        { user_id: "usr_ada", name: "Ada", color: "#D4A373", count: 23 },
        { user_id: "usr_bob", name: "Bob", color: "#8FBC8F", count: 17 },
        { user_id: "usr_cara", name: "Cara", color: "#7FA7C9", count: 9 },
      ],
      month,
    });
  }

  if (path.startsWith("/api/v1/reports/")) {
    const kind = path.split("/")[4];
    const format = url.searchParams.get("format") ?? "json";
    if (format === "csv_excel" || format === "csv_raw") {
      return csv(
        res,
        `kaffeekasse_2026-11_${kind}.csv`,
        `Datum;Benutzer;Produkt;Menge;Betrag (EUR)\r\n11.11.2026;Ada;Kaffee;1;0,50\r\n`,
      );
    }
    return json(res, 200, { kind, rows: [], totals: summary });
  }

  return json(res, 404, { code: "not_found", message: `Mock has no route ${method} ${path}` });
});

server.listen(PORT, () => {
  console.log(`Kaffeekasse mock API on http://localhost:${PORT}/api/v1`);
});
