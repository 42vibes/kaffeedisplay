import { useState } from "react";
import { ApiRequestError, login } from "../api/client";
import type { Session } from "../api/types";
import { de } from "../i18n/de";

export function LoginPage({
  onLogin,
  notice,
}: {
  onLogin: (session: Session) => void;
  notice?: string;
}) {
  const [username, setUsername] = useState("admin");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    setBusy(true);
    setError("");
    try {
      onLogin(await login(username.trim(), password));
    } catch (err) {
      if (err instanceof ApiRequestError) {
        if (err.status === 401) setError(de.wrongPassword);
        else if (err.status === 429) setError(de.tooManyRequests);
        else setError(de.loginFailed);
      } else {
        setError(de.loginFailed);
      }
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="flex min-h-screen items-center justify-center p-4">
      <div className="w-full max-w-sm">
        <div className="mb-6 text-center">
          <div className="text-3xl font-semibold tracking-tight">{de.app}</div>
          <div className="muted mt-2 text-sm">{de.tagline}</div>
        </div>
        <form className="card space-y-4 p-5" onSubmit={submit}>
          <h1 className="text-lg font-medium">{de.login}</h1>
          {notice ? (
            <div className="rounded-xl border border-amber-400/30 bg-amber-400/10 px-3 py-2 text-sm text-amber-400">
              {notice}
            </div>
          ) : null}
          <label className="block space-y-1.5">
            <span className="label">{de.username}</span>
            <input
              className="input"
              value={username}
              onChange={(event) => setUsername(event.target.value)}
              autoComplete="username"
              autoCapitalize="none"
              spellCheck={false}
              required
            />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.password}</span>
            <input
              className="input"
              type="password"
              value={password}
              onChange={(event) => setPassword(event.target.value)}
              autoComplete="current-password"
              required
              autoFocus
            />
          </label>
          {error ? <div className="text-sm text-rust-400" role="alert">{error}</div> : null}
          <button className="btn btn-primary w-full" disabled={busy} type="submit">
            {busy ? de.loading : de.signIn}
          </button>
          <p className="muted text-xs leading-relaxed">{de.loginHint}</p>
        </form>
      </div>
    </div>
  );
}
