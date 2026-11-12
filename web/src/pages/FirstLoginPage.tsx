import { useState } from "react";
import { api } from "../api/endpoints";
import { de } from "../i18n/de";
import { errorText } from "../lib/errors";
import { useWriteId } from "../lib/useWriteId";

/**
 * Forced first-login flow. The device API rejects every other authenticated
 * route while the initial admin password is unchanged, so the SPA shows this
 * form before the normal shell.
 */
export function FirstLoginPage({ onDone }: { onDone: () => void }) {
  const [currentPassword, setCurrentPassword] = useState("");
  const [newPassword, setNewPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const write = useWriteId();

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    if (newPassword.length < 10) {
      setError(de.passwordTooShort);
      return;
    }
    if (newPassword !== confirmPassword) {
      setError(de.passwordMismatch);
      return;
    }
    setBusy(true);
    setError("");
    try {
      await api.changePassword(currentPassword, newPassword, write.id);
      write.renew();
      onDone();
    } catch (err) {
      setError(errorText(err) || de.firstLoginFailed);
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
          <h1 className="text-lg font-medium">{de.firstLoginTitle}</h1>
          <p className="muted text-sm leading-relaxed">{de.firstLoginBody}</p>
          <label className="block space-y-1.5">
            <span className="label">{de.firstLoginCurrent}</span>
            <input
              className="input"
              type="password"
              value={currentPassword}
              onChange={(event) => setCurrentPassword(event.target.value)}
              autoComplete="current-password"
              required
              autoFocus
            />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.newPassword}</span>
            <input
              className="input"
              type="password"
              value={newPassword}
              onChange={(event) => setNewPassword(event.target.value)}
              autoComplete="new-password"
              required
            />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.confirmPassword}</span>
            <input
              className="input"
              type="password"
              value={confirmPassword}
              onChange={(event) => setConfirmPassword(event.target.value)}
              autoComplete="new-password"
              required
            />
          </label>
          {error ? (
            <div className="text-sm text-rust-400" role="alert">
              {error}
            </div>
          ) : null}
          <button className="btn btn-primary w-full" disabled={busy} type="submit">
            {busy ? de.saving : de.firstLoginSubmit}
          </button>
          <p className="muted text-xs leading-relaxed">{de.passwordHint}</p>
        </form>
      </div>
    </div>
  );
}
