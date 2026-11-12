import { afterEach, describe, expect, it, vi } from "vitest";
import { currentSession, login, request, setSession } from "./client";
import { api } from "./endpoints";

interface Call {
  url: string;
  init?: RequestInit;
}

function response(body: unknown, contentType = "application/json", status = 200) {
  return {
    status,
    ok: status < 400,
    headers: {
      get: (name: string) =>
        name.toLowerCase() === "content-type" ? contentType : null,
    },
    json: async () => body,
    text: async () => (typeof body === "string" ? body : JSON.stringify(body)),
  } as unknown as Response;
}

function installFetch(
  handler: (url: string, init?: RequestInit) => Response,
): Call[] {
  const calls: Call[] = [];
  vi.stubGlobal(
    "fetch",
    vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
      const url = String(input);
      calls.push({ url, init });
      return handler(url, init);
    }),
  );
  return calls;
}

afterEach(() => {
  setSession(null);
  vi.unstubAllGlobals();
});

describe("API client session handling", () => {
  it("refreshes the CSRF token from /auth/me when login omits it", async () => {
    const calls = installFetch((url) => {
      if (url.endsWith("/auth/login")) {
        return response({ username: "admin", expires_at: 123 });
      }
      if (url.endsWith("/auth/me")) {
        return response({
          username: "admin",
          expires_at: 123,
          csrf_token: "fresh-token",
        });
      }
      if (url.endsWith("/time/sync")) {
        return response({
          now: 1,
          source: "synced",
          last_sync: 1,
          last_source: "browser",
          trusted: true,
        });
      }
      return response({});
    });

    const session = await login("admin", "kaffee");
    expect(session.csrf_token).toBe("fresh-token");
    expect(currentSession()?.csrf_token).toBe("fresh-token");
    expect(calls.map((call) => call.url)).toEqual([
      "/api/v1/auth/login",
      "/api/v1/auth/me",
      "/api/v1/time/sync",
    ]);
  });

  it("sends the refreshed CSRF token and keeps request IDs for retries", async () => {
    installFetch((url) => {
      if (url.endsWith("/auth/login")) {
        return response({ username: "admin", expires_at: 123 });
      }
      if (url.endsWith("/auth/me")) {
        return response({
          username: "admin",
          expires_at: 123,
          csrf_token: "fresh-token",
        });
      }
      return response({});
    });
    await login("admin", "kaffee");

    const calls = installFetch(() => response({}));
    await request("/users", {
      method: "POST",
      body: JSON.stringify({ name: "Ada" }),
      requestId: "stable-id",
    });
    const headers = new Headers(calls[0]?.init?.headers);
    expect(headers.get("X-CSRF-Token")).toBe("fresh-token");
    expect(headers.get("X-Request-Id")).toBe("stable-id");
  });

  it("asks for text/csv on report downloads", async () => {
    const calls = installFetch(() => response("Kennzahl;Wert\r\n", "text/csv"));
    const csv = await api.reportCsv(
      { kind: "overview", period: "month" },
      "csv_excel",
    );
    expect(csv).toContain("Kennzahl");
    const headers = new Headers(calls[0]?.init?.headers);
    expect(headers.get("Accept")).toBe("text/csv");
  });
});
