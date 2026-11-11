import { useEffect, useState } from "react";
import { request } from "../api/client";
import type { DeviceStatus, Summary } from "../api/types";
import { StatCard } from "../components/StatCard";
import { de } from "../i18n/de";
import { formatDateTime, formatEuro } from "../lib/format";

export function DashboardPage() {
  const [summary, setSummary] = useState<Summary | null>(null);
  const [device, setDevice] = useState<DeviceStatus | null>(null);
  const [error, setError] = useState("");

  useEffect(() => {
    let active = true;
    Promise.all([request<Summary>("/stats/summary"), request<DeviceStatus>("/device")])
      .then(([s, d]) => {
        if (!active) return;
        setSummary(s);
        setDevice(d);
      })
      .catch((err: Error) => {
        if (active) setError(err.message);
      });
    return () => {
      active = false;
    };
  }, []);

  if (error) {
    return <div className="card p-6 text-rust-400">{error}</div>;
  }

  if (!summary) {
    return <div className="muted">{de.loading}</div>;
  }

  const timeWarning = device && !device.time.trusted;

  return (
    <div className="space-y-6">
      <header className="flex flex-wrap items-end justify-between gap-3">
        <div>
          <h1 className="text-2xl font-semibold tracking-tight">{de.dashboard}</h1>
          <p className="muted mt-1 text-sm">
            {device ? `${de.firmware} ${device.firmware_version} · ${device.mac}` : de.loading}
          </p>
        </div>
        {timeWarning ? (
          <div className="rounded-xl border border-amber-400/30 bg-amber-400/10 px-3 py-2 text-xs text-amber-400">
            {de.timeUnknown}
          </div>
        ) : null}
      </header>

      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
        <StatCard
          label={de.cashExpected}
          value={formatEuro(summary.cash_expected_cents)}
          hint={device ? `Stand ${formatDateTime(device.time.now)}` : undefined}
        />
        <StatCard label={de.prepaid} value={formatEuro(summary.prepaid_cents)} tone="good" />
        <StatCard
          label={de.debts}
          value={formatEuro(summary.debts_cents)}
          tone={summary.debts_cents > 0 ? "bad" : "neutral"}
        />
        <StatCard
          label={de.equity}
          value={formatEuro(summary.equity_cents)}
          tone={summary.equity_cents < 0 ? "warn" : "neutral"}
        />
      </div>

      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
        <StatCard
          label={de.memberSales}
          value={formatEuro(summary.member_sales_cents)}
          hint={`${summary.member_items} Getränke`}
        />
        <StatCard
          label={de.guestSales}
          value={formatEuro(summary.guest_sales_cents)}
          hint={`${summary.guest_items} Getränke`}
        />
        <StatCard label={de.topups} value={formatEuro(summary.topups_cents)} />
        <StatCard label={de.expensesTotal} value={formatEuro(summary.expenses_cents)} />
      </div>

      <div className="grid gap-3 lg:grid-cols-2">
        <div className="card p-5">
          <h2 className="font-medium">{de.quickActions}</h2>
          <div className="mt-4 grid gap-2 sm:grid-cols-2">
            <a className="btn" href="/users">
              {de.addUser}
            </a>
            <a className="btn" href="/products">
              {de.addProduct}
            </a>
            <a className="btn" href="/expenses">
              {de.addExpense}
            </a>
            <a className="btn" href="/reports">
              {de.viewReports}
            </a>
          </div>
        </div>
        <div className="card p-5">
          <h2 className="font-medium">{de.system}</h2>
          <dl className="mt-4 space-y-2 text-sm">
            <div className="flex justify-between gap-4">
              <dt className="muted">{de.uptime}</dt>
              <dd className="mono">{device ? `${Math.floor(device.uptime_s / 3600)} h` : "–"}</dd>
            </div>
            <div className="flex justify-between gap-4">
              <dt className="muted">{de.freeHeap}</dt>
              <dd className="mono">
                {device ? `${Math.round(device.free_heap / 1024)} KB` : "–"}
              </dd>
            </div>
            <div className="flex justify-between gap-4">
              <dt className="muted">{de.chipTemp}</dt>
              <dd className="mono">
                {device?.temperature_valid ? `${device.temperature_c.toFixed(1)} °C` : "–"}
              </dd>
            </div>
            <div className="flex justify-between gap-4">
              <dt className="muted">{de.timeSource}</dt>
              <dd>{device?.time.last_source || "–"}</dd>
            </div>
          </dl>
        </div>
      </div>
    </div>
  );
}
