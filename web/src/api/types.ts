export type Cents = number;

export interface User {
  id: string;
  name: string;
  color: string;
  active: boolean;
  created_at: number;
  archived_at?: number;
}

export interface UserWithBalance extends User {
  balance_cents: Cents;
  last_booking_at?: number;
}

export interface Product {
  id: string;
  name: string;
  category: string;
  icon: string;
  color: string;
  price_cents: Cents;
  sort: number;
  active: boolean;
  created_at: number;
  updated_at: number;
}

export interface ProductInput {
  name: string;
  category: string;
  icon?: string;
  color?: string;
  price_cents: Cents;
  sort?: number;
  active?: boolean;
}

export type EventType =
  | "user_created"
  | "user_updated"
  | "user_archived"
  | "product_created"
  | "product_updated"
  | "product_archived"
  | "product_deleted"
  | "purchase"
  | "guest_sale"
  | "topup"
  | "adjustment"
  | "void"
  | "expense_created"
  | "expense_updated"
  | "expense_voided"
  | "cash_count"
  | "settings_updated"
  | "audit";

export interface Event {
  seq: number;
  id: string;
  ts: number;
  time_source: "synced" | "restored" | "unset";
  type: EventType;
  user_id?: string;
  user_name?: string;
  product_id?: string;
  product_name?: string;
  product_category?: string;
  unit_amount_cents?: Cents;
  amount_cents?: Cents;
  payment?: "balance" | "cash" | "";
  note?: string;
  actor?: string;
  request_id?: string;
  reverses?: string;
  voided?: boolean;
  balance_after_cents?: Cents;
}

export interface Expense {
  id: string;
  ts: number;
  category: string;
  description: string;
  vendor: string;
  amount_cents: Cents;
  note: string;
  voided: boolean;
}

export interface ExpenseInput {
  category: string;
  description: string;
  vendor?: string;
  amount_cents: Cents;
  note?: string;
}

export interface CashCount {
  id: string;
  ts: number;
  counted_cents: Cents;
  expected_cents: Cents;
  difference_cents: Cents;
  note: string;
}

export interface Summary {
  cash_expected_cents: Cents;
  prepaid_cents: Cents;
  debts_cents: Cents;
  net_liabilities_cents: Cents;
  equity_cents: Cents;
  member_sales_cents: Cents;
  guest_sales_cents: Cents;
  topups_cents: Cents;
  expenses_cents: Cents;
  member_items: number;
  guest_items: number;
}

export interface LeaderboardEntry {
  user_id: string;
  name: string;
  color: string;
  count: number;
}

export interface Leaderboard {
  from: number;
  to: number;
  guest_count: number;
  entries: LeaderboardEntry[];
}

export interface DisplaySettings {
  rotation: 0 | 90 | 180 | 270;
  brightness_active: number;
  brightness_idle: number;
  dim_timeout_s: number;
  screen_off_timeout_s: number;
}

export interface Settings {
  display: DisplaySettings;
  network: {
    ap_ssid: string;
    ap_window_s: number;
    hostname: string;
    sta_enabled: boolean;
    sta_ssid: string;
    napt_enabled: boolean;
  };
  time: {
    timezone: string;
    sntp_enabled: boolean;
    sntp_server: string;
  };
  app: {
    currency: string;
    locale: string;
    guest_enabled: boolean;
    max_open_debt_cents: Cents;
    low_balance_warning_cents: Cents;
  };
}

export interface TimeInfo {
  now: number;
  source: "synced" | "restored" | "unset";
  last_sync: number;
  last_source: string;
  trusted: boolean;
}

export interface ApStatus {
  active: boolean;
  remaining_seconds: number;
  ssid: string;
  password: string;
  ip: string;
  url: string;
  wifi_qr: string;
  url_qr: string;
}

export interface NetworkStatus {
  ap_active: boolean;
  ap_starting: boolean;
  ap_remaining_s: number;
  ap_ssid: string;
  ap_password: string;
  ap_ip: string;
  sta_connected: boolean;
  sta_ip: string;
  sta_rssi: number;
}

export interface DeviceStatus {
  firmware_version: string;
  build_date: string;
  uptime_s: number;
  free_heap: number;
  free_psram: number;
  storage_used: number;
  storage_total: number;
  temperature_c: number;
  temperature_valid: boolean;
  mac: string;
  network: NetworkStatus;
  time: TimeInfo;
  drinks_today: number;
}

export interface Session {
  username: string;
  expires_at: number;
  csrf_token: string;
}

export type ReportKind =
  | "overview"
  | "consumption"
  | "deposits"
  | "expenses"
  | "cashflow"
  | "balances"
  | "transactions";

export type ReportPeriod = "week" | "month" | "year" | "all" | "custom";
export type ReportFormat = "json" | "csv_excel" | "csv_raw";

export interface ReportQuery {
  kind: ReportKind;
  period?: ReportPeriod;
  from?: number;
  to?: number;
  format?: ReportFormat;
}

export interface ApiError {
  code: string;
  message: string;
}
