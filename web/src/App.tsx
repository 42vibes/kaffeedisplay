import { useEffect, useState } from "react";
import { Navigate, Route, Routes } from "react-router-dom";
import {
  currentSession,
  logout,
  me,
  setSession,
  setUnauthorizedHandler,
  syncTimeFromBrowser,
} from "./api/client";
import type { Session } from "./api/types";
import { Shell } from "./components/Shell";
import { de } from "./i18n/de";
import { CashPage } from "./pages/CashPage";
import { DashboardPage } from "./pages/DashboardPage";
import { ExpensesPage } from "./pages/ExpensesPage";
import { LoginPage } from "./pages/LoginPage";
import { ProductsPage } from "./pages/ProductsPage";
import { ReportsPage } from "./pages/ReportsPage";
import { SettingsPage } from "./pages/SettingsPage";
import { SystemPage } from "./pages/SystemPage";
import { UsersPage } from "./pages/UsersPage";

const TIME_SYNC_INTERVAL_MS = 5 * 60 * 1000;

export function App() {
  const [session, setSessionState] = useState<Session | null>(currentSession());
  const [checking, setChecking] = useState(true);
  const [notice, setNotice] = useState("");

  useEffect(() => {
    setUnauthorizedHandler(() => {
      setSession(null);
      setSessionState(null);
      setNotice(de.sessionExpired);
    });
    return () => setUnauthorizedHandler(null);
  }, []);

  useEffect(() => {
    let active = true;
    me()
      .then((result) => {
        if (active) {
          setSessionState(result);
          setNotice("");
        }
      })
      .catch(() => {
        if (active) {
          setSession(null);
          setSessionState(null);
          setNotice("");
        }
      })
      .finally(() => {
        if (active) setChecking(false);
      });
    return () => {
      active = false;
    };
  }, []);

  // Browser time is the only trusted clock: sync on login and every 5 minutes.
  useEffect(() => {
    if (!session) return;
    const timer = window.setInterval(() => {
      syncTimeFromBrowser().catch(() => {
        // The next interval or a manual sync will retry.
      });
    }, TIME_SYNC_INTERVAL_MS);
    return () => window.clearInterval(timer);
  }, [session]);

  if (checking) {
    return (
      <div className="flex min-h-screen items-center justify-center">
        <div className="muted animate-pulse">{de.loading}</div>
      </div>
    );
  }

  if (!session) {
    return (
      <LoginPage
        notice={notice}
        onLogin={(next) => {
          setNotice("");
          setSessionState(next);
        }}
      />
    );
  }

  return (
    <Shell
      username={session.username}
      onLogout={() => {
        void logout().catch(() => undefined);
        setSession(null);
        setSessionState(null);
        setNotice("");
      }}
    >
      <Routes>
        <Route path="/" element={<DashboardPage />} />
        <Route path="/users" element={<UsersPage />} />
        <Route path="/products" element={<ProductsPage />} />
        <Route path="/expenses" element={<ExpensesPage />} />
        <Route path="/reports" element={<ReportsPage />} />
        <Route path="/cash" element={<CashPage />} />
        <Route path="/settings" element={<SettingsPage />} />
        <Route path="/system" element={<SystemPage />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </Shell>
  );
}
