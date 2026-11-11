import type { ApiError, Session } from "./types";

const BASE = "/api/v1";

export class ApiRequestError extends Error {
  readonly status: number;
  readonly code: string;

  constructor(status: number, code: string, message: string) {
    super(message);
    this.status = status;
    this.code = code;
  }
}

let csrfToken = "";
let session: Session | null = null;

function uuid(): string {
  if (typeof crypto !== "undefined" && "randomUUID" in crypto) {
    return crypto.randomUUID();
  }
  return `${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

async function request<T>(
  path: string,
  options: RequestInit & { skipCsrf?: boolean } = {},
): Promise<T> {
  const headers = new Headers(options.headers);
  headers.set("Accept", "application/json");
  if (options.body && !headers.has("Content-Type")) {
    headers.set("Content-Type", "application/json");
  }
  if (csrfToken && !options.skipCsrf) headers.set("X-CSRF-Token", csrfToken);
  headers.set("X-Request-Id", uuid());

  const response = await fetch(`${BASE}${path}`, {
    ...options,
    headers,
    credentials: "include",
  });

  if (response.status === 204) return undefined as T;
  const contentType = response.headers.get("Content-Type") ?? "";
  const body = contentType.includes("application/json")
    ? await response.json()
    : await response.text();

  if (!response.ok) {
    const err = body as Partial<ApiError>;
    throw new ApiRequestError(
      response.status,
      err.code ?? "http_error",
      err.message ?? `HTTP ${response.status}`,
    );
  }
  return body as T;
}

export function setSession(next: Session | null) {
  session = next;
  csrfToken = next?.csrf_token ?? "";
}

export function currentSession(): Session | null {
  return session;
}

export async function login(username: string, password: string): Promise<Session> {
  const result = await request<Session>("/auth/login", {
    method: "POST",
    body: JSON.stringify({ username, password }),
    skipCsrf: true,
  });
  setSession(result);
  await syncTimeFromBrowser();
  return result;
}

export async function logout(): Promise<void> {
  await request<void>("/auth/logout", { method: "POST" });
  setSession(null);
}

export async function me(): Promise<Session> {
  const result = await request<Session>("/auth/me");
  setSession(result);
  return result;
}

export async function syncTimeFromBrowser(): Promise<void> {
  if (!session) return;
  const timezone = Intl.DateTimeFormat().resolvedOptions().timeZone || "Europe/Berlin";
  await request("/time/sync", {
    method: "POST",
    body: JSON.stringify({ epoch_ms: Date.now(), timezone }),
  });
}

export { request, BASE };
