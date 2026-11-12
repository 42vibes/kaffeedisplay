import { useEffect, useState } from "react";
import { Navigate, Route, Routes } from "react-router-dom";
import { currentSession, me, setSession } from "./api/client";
import type { Session } from "./api/types";
import { Shell } from "./components/Shell";
import { DashboardPage } from "./pages/DashboardPage";
import { LoginPage } from "./pages/LoginPage";
import { PlaceholderPage } from "./pages/PlaceholderPage";
import { de } from "./i18n/de";

export function App() {
  const [session, setSessionState] = useState<Session | null>(currentSession());
  const [checking, setChecking] = useState(true);

  useEffect(() => {
    let active = true;
    me()
      .then((result) => {
        if (active) setSessionState(result);
      })
      .catch(() => {
        if (active) {
          setSession(null);
          setSessionState(null);
        }
      })
      .finally(() => {
        if (active) setChecking(false);
      });
    return () => {
      active = false;
    };
  }, []);

  if (checking) {
    return (
      <div className="flex min-h-screen items-center justify-center">
        <div className="muted animate-pulse">{de.loading}</div>
      </div>
    );
  }

  if (!session) {
    return <LoginPage onLogin={setSessionState} />;
  }

  return (
    <Shell
      onLogout={() => {
        setSession(null);
        setSessionState(null);
      }}
    >
      <Routes>
        <Route path="/" element={<DashboardPage />} />
        <Route path="/users" element={<PlaceholderPage title={de.users} />} />
        <Route path="/products" element={<PlaceholderPage title={de.products} />} />
        <Route path="/expenses" element={<PlaceholderPage title={de.expenses} />} />
        <Route path="/reports" element={<PlaceholderPage title={de.reports} />} />
        <Route path="/cash" element={<PlaceholderPage title={de.cash} />} />
        <Route path="/settings" element={<PlaceholderPage title={de.settings} />} />
        <Route path="/system" element={<PlaceholderPage title={de.system} />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </Shell>
  );
}
