import { useMemo, useState } from "react";
import { Link } from "react-router-dom";
import { api } from "../api/endpoints";
import type { DeviceStatus, Event, Summary } from "../api/types";
import { Alert } from "../components/Alert";
import { BarChart } from "../components/BarChart";
import { PageHeader } from "../components/PageHeader";
import { StatCard } from "../components/StatCard";
import { TransactionTable } from "../components/TransactionTable";
import { de } from "../i18n/de";
import { errorText } from "../lib/errors";
import { formatDate, formatDateShort, formatDateTime, formatEuro } from "../lib/format";
import { isConsumption } from "../lib/events";
import { lastDays } from "../lib/period";
import { useAsync } from "../lib/useAsync";

interface DashboardData {
  summary: Summary;
  device: DeviceStatus;
  events: Event[];
}

export function DashboardPage() {
  const [chartDays, setChartDays] = useState(14);
  const [notice, setNotice] = useState("");
  const [syncing, setSyncing] = useState(false);

  const { data, error, loading, reload } = useAsync<DashboardData>(async () => {
    const [summary, device, events] = await Promise.all([
      api.summary(),
      api.device(),
      api.transactions({ limit: 500 }),
    ]);
    return { summary, device, events };
  }, []);

  const chartPoints = useMemo(() => {
    if (!data) return [];
    return lastDays(chartDays).map((day) => {
      const start = Math.floor(day.getTime() / 1000);
      const end = start + 86400;
      const value = data.events
        .filter((event) => event.ts >= start && event.ts < end && isConsumption(event))
        .reduce((sum, event) => sum + Math.abs(event.amount_cents ?? 0), 0);
      return {
        label: formatDateShort(start),
        title: formatDate(start),
        value,
      };
    });
  }, [data, chartDays]);

  if (error) {
    return (
      <div className="space-y-4">
        <PageHeader title={de.dashboard} />
        <Alert tone="bad" action={<button className="btn" onClick={reload} type="button">{de.retry}</button>}>
          {error}
        </Alert>
      </div>
    );
  }

  if (!data) {
    return <div className="muted">{loading ? de.loading : de.noData}</div>;
  }

  const { summary, device, events } = data;
  const backupAgeDays = device.last_backup_at
    ? Math.floor((Date.now() / 1000 - device.last_backup_at) / 86400)
    : null;
  const storageRatio = device.storage_total > 0 ? device.storage_used / device.storage_total : 0;

  async function syncTime() {
    setSyncing(true);
    setNotice("");
    try {
      const timezone = Intl.DateTimeFormat().resolvedOptions().timeZone || "Europe/Berlin";
      await api.syncTime(Date.now(), timezone);
      setNotice(de.timeSynced);
      reload();
    } catch (err) {
      setNotice(errorText(err));
    } finally {
      setSyncing(false);
    }
  }

  return (
    <div className="space-y-6">
      <PageHeader
        title={de.dashboard}
        subtitle={`${de.firmware} ${device.firmware_version} · ${device.mac}`}
        actions={
          <button className="btn" onClick={reload} type="button" disabled={loading}>
            {loading ? de.loading : de.refresh}
          </button>
        }
      />

      {notice ? <Alert tone="info">{notice}</Alert> : null}

      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
        <StatCard
          label={de.cashExpected}
          value={formatEuro(summary.cash_expected_cents)}
          hint={`Stand ${formatDateTime(device.time.now)}`}
        />
        <StatCard label={de.prepaid} value={formatEuro(summary.prepaid_cents)} tone="good" />
        <StatCard
          label={de.debts}
          value={formatEuro(summary.debts_cents)}
          tone={summary.debts_cents > 0 ? "bad" : "neutral"}
          hint={`${de.netLiabilities}: ${formatEuro(summary.net_liabilities_cents)}`}
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
          hint={`${summary.member_items} ${de.drinks}`}
        />
        <StatCard
          label={de.guestSales}
          value={formatEuro(summary.guest_sales_cents)}
          hint={`${summary.guest_items} ${de.drinks}`}
        />
        <StatCard label={de.topups} value={formatEuro(summary.topups_cents)} />
        <StatCard label={de.expensesTotal} value={formatEuro(summary.expenses_cents)} />
      </div>

      <section className="card p-4 sm:p-5">
        <h2 className="font-medium">{de.alerts}</h2>
        <div className="mt-3 space-y-2">
          {!device.time.trusted ? (
            <Alert
              tone="warn"
              action={
                <button className="btn min-h-9 px-3 py-1 text-sm" onClick={syncTime} type="button" disabled={syncing}>
                  {syncing ? de.loading : de.timeSync}
                </button>
              }
            >
              {de.alertTimeUntrusted}
            </Alert>
          ) : null}
          {summary.debts_cents > 0 ? (
            <Alert tone="warn" action={<Link className="btn min-h-9 px-3 py-1 text-sm" to="/users">{de.users}</Link>}>
              {de.alertDebts} {formatEuro(summary.debts_cents)}
            </Alert>
          ) : null}
          {backupAgeDays === null ? (
            <Alert tone="warn" action={<Link className="btn min-h-9 px-3 py-1 text-sm" to="/system">{de.system}</Link>}>
              {de.alertBackupMissing}
            </Alert>
          ) : backupAgeDays > 7 ? (
            <Alert tone="warn" action={<Link className="btn min-h-9 px-3 py-1 text-sm" to="/system">{de.system}</Link>}>
              {de.alertBackupOld} ({backupAgeDays} {de.days})
            </Alert>
          ) : null}
          {storageRatio > 0.85 ? (
            <Alert tone="bad">
              {de.alertStorage} ({Math.round(storageRatio * 100)} %)
            </Alert>
          ) : null}
          {device.time.trusted && summary.debts_cents === 0 && backupAgeDays !== null && backupAgeDays <= 7 && storageRatio <= 0.85 ? (
            <Alert tone="good">{de.noAlerts}</Alert>
          ) : null}
        </div>
      </section>

      <div className="grid gap-4 lg:grid-cols-3">
        <section className="card p-4 sm:p-5 lg:col-span-2">
          <div className="flex flex-wrap items-center justify-between gap-2">
            <h2 className="font-medium">{de.consumptionChart}</h2>
            <div className="flex gap-1 rounded-xl border border-cream-100/10 p-1">
              <button
                type="button"
                className={`rounded-lg px-3 py-1 text-sm ${chartDays === 14 ? "bg-amber-400 font-semibold text-espresso-950" : "text-cream-100/80"}`}
                onClick={() => setChartDays(14)}
              >
                {de.last14Days}
              </button>
              <button
                type="button"
                className={`rounded-lg px-3 py-1 text-sm ${chartDays === 30 ? "bg-amber-400 font-semibold text-espresso-950" : "text-cream-100/80"}`}
                onClick={() => setChartDays(30)}
              >
                {de.last30Days}
              </button>
            </div>
          </div>
          <div className="mt-4">
            <BarChart points={chartPoints} label={de.consumptionChart} />
          </div>
        </section>

        <section className="card p-4 sm:p-5">
          <h2 className="font-medium">{de.quickActions}</h2>
          <div className="mt-3 grid gap-2 sm:grid-cols-2 lg:grid-cols-1">
            <Link className="btn justify-start" to="/users">
              {de.addUser}
            </Link>
            <Link className="btn justify-start" to="/products">
              {de.addProduct}
            </Link>
            <Link className="btn justify-start" to="/expenses">
              {de.addExpense}
            </Link>
            <Link className="btn justify-start" to="/reports">
              {de.viewReports}
            </Link>
          </div>
        </section>
      </div>

      <section className="card p-4 sm:p-5">
        <div className="flex items-center justify-between gap-2">
          <h2 className="font-medium">{de.recentActivity}</h2>
          <Link className="btn min-h-9 px-3 py-1 text-sm" to="/reports">
            {de.viewReports}
          </Link>
        </div>
        <div className="mt-3">
          <TransactionTable events={events.slice(0, 8)} />
        </div>
      </section>
    </div>
  );
}
