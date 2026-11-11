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
let unauthorizedHandler: (() => void) | null = null;

/** Generate a UUID that is reused for retries of the same logical write. */
export function newRequestId(): string {
  if (typeof crypto !== "undefined" && "randomUUID" in crypto) {
    return crypto.randomUUID();
  }
  return `${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

export function setUnauthorizedHandler(handler: (() => void) | null) {
  unauthorizedHandler = handler;
}

async function request<T>(
  path: string,
  options: RequestInit & {
    skipCsrf?: boolean;
    requestId?: string;
    skipUnauthorizedHandler?: boolean;
    accept?: string;
  } = {},
): Promise<T> {
  const headers = new Headers(options.headers);
  headers.set("Accept", options.accept ?? "application/json");
  const isFormData = typeof FormData !== "undefined" && options.body instanceof FormData;
  if (options.body && !isFormData && !headers.has("Content-Type")) {
    headers.set("Content-Type", "application/json");
  }
  if (csrfToken && !options.skipCsrf) headers.set("X-CSRF-Token", csrfToken);
  headers.set("X-Request-Id", options.requestId ?? newRequestId());

  const response = await fetch(`${BASE}${path}`, {
    ...options,
    headers,
    credentials: "include",
  });

  if (response.status === 401 && !options.skipUnauthorizedHandler) {
    unauthorizedHandler?.();
  }

  if (response.status === 204) return undefined as T;
  const contentType = response.headers.get("Content-Type") ?? "";
  const body = contentType.includes("application/json")
    ? await response.json()
    : await response.text();

  if (!response.ok) {
    const err = body as Partial<ApiError>;
    throw new ApiRequestError(
      response.status,
      typeof err?.code === "string" ? err.code : "http_error",
      typeof err?.message === "string" ? err.message : `HTTP ${response.status}`,
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
    skipUnauthorizedHandler: true,
  });
  setSession(result);
  // The login response may omit the CSRF token; /auth/me is the canonical
  // source and must be refreshed before the first write.
  let active = result;
  try {
    active = await request<Session>("/auth/me", { skipUnauthorizedHandler: true });
    setSession(active);
  } catch {
    // Keep the login response if the follow-up session check fails.
  }
  try {
    await syncTimeFromBrowser();
  } catch {
    // Login succeeded; a failed clock sync must not block the session.
  }
  return active;
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

/** Trigger a browser download for text content (CSV, JSON backups). */
export function downloadText(filename: string, content: string, mime: string) {
  const blob = new Blob([content], { type: mime });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
}

export { request, BASE };
