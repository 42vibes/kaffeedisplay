import { useState } from "react";
import { api } from "../api/endpoints";
import type { CashCount, Summary } from "../api/types";
import { Alert } from "../components/Alert";
import { DataTable, type Column } from "../components/DataTable";
import { MoneyInput, MoneyPreview } from "../components/MoneyInput";
import { PageHeader } from "../components/PageHeader";
import { StatCard } from "../components/StatCard";
import { de } from "../i18n/de";
import { errorText } from "../lib/errors";
import { formatDateTime, formatSignedEuro, formatEuro, parseMoneyInput } from "../lib/format";
import { useAsync } from "../lib/useAsync";
import { useWriteId } from "../lib/useWriteId";

interface CashData {
  summary: Summary;
  counts: CashCount[];
}

export function CashPage() {
  const [counted, setCounted] = useState("");
  const [note, setNote] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const writeId = useWriteId();
  const data = useAsync<CashData>(
    async () => {
      const [summary, counts] = await Promise.all([api.summary(), api.cashCounts()]);
      return { summary, counts };
    },
    [],
  );

  const parsedCount = counted.trim() ? parseMoneyInput(counted) : null;
  const countedCents = parsedCount?.valid ? parsedCount.cents : null;
  const expected = data.data?.summary.cash_expected_cents ?? 0;
  const difference = countedCents === null ? null : countedCents - expected;

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    if (!parsedCount?.valid || parsedCount.cents < 0) {
      setError(de.countedRequired);
      return;
    }
    setBusy(true);
    setError("");
    setNotice("");
    try {
      await api.createCashCount(parsedCount.cents, note.trim(), writeId.id);
      writeId.renew();
      setCounted("");
      setNote("");
      setNotice(de.countSuccess);
      data.reload();
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  }

  const columns: Array<Column<CashCount>> = [
    {
      key: "ts",
      label: de.date,
      render: (count) => <span className="whitespace-nowrap">{formatDateTime(count.ts)}</span>,
    },
    {
      key: "expected",
      label: de.reportColumns.expected_cents,
      align: "right",
      render: (count) => formatEuro(count.expected_cents),
    },
    {
      key: "counted",
      label: de.reportColumns.counted_cents,
      align: "right",
      render: (count) => formatEuro(count.counted_cents),
    },
    {
      key: "difference",
      label: de.difference,
      align: "right",
      render: (count) => (
        <span className={count.difference_cents === 0 ? "text-cream-500" : count.difference_cents > 0 ? "text-sage-400" : "text-rust-400"}>
          {formatSignedEuro(count.difference_cents)}
        </span>
      ),
    },
    {
      key: "note",
      label: de.note,
      hideBelow: "sm",
      render: (count) => <span className="text-sm">{count.note || "–"}</span>,
    },
  ];

  return (
    <div className="space-y-5">
      <PageHeader title={de.cash} subtitle={de.cashHint} />

      {notice ? <Alert tone="good">{notice}</Alert> : null}
      {data.error ? (
        <Alert tone="bad" action={<button className="btn" onClick={data.reload} type="button">{de.retry}</button>}>
          {data.error}
        </Alert>
      ) : null}

      <div className="grid gap-3 sm:grid-cols-3">
        <StatCard label={de.expectedCash} value={formatEuro(expected)} />
        <StatCard
          label={de.countedCash}
          value={countedCents === null ? "–" : formatEuro(countedCents)}
        />
        <StatCard
          label={de.difference}
          value={difference === null ? "–" : formatSignedEuro(difference)}
          tone={difference === null ? "neutral" : difference === 0 ? "good" : "bad"}
        />
      </div>

      <form className="card space-y-4 p-4 sm:p-5" onSubmit={submit}>
        <h2 className="font-medium">{de.cashCountTitle}</h2>
        <div className="grid gap-3 sm:grid-cols-2">
          <label className="block space-y-1.5">
            <span className="label">{de.countedCash}</span>
            <MoneyInput value={counted} onChange={setCounted} autoFocus />
            <MoneyPreview value={counted} />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.countNote} ({de.optional})</span>
            <input className="input" value={note} onChange={(event) => setNote(event.target.value)} maxLength={200} />
          </label>
        </div>
        {difference !== null && difference !== 0 ? (
          <Alert tone={difference > 0 ? "warn" : "bad"}>
            {de.difference}: {formatSignedEuro(difference)}.{" "}
            {difference < 0 ? de.cashHint : ""}
          </Alert>
        ) : null}
        {error ? <div className="text-sm text-rust-400" role="alert">{error}</div> : null}
        <button className="btn btn-primary" type="submit" disabled={busy}>
          {busy ? de.saving : de.recordCount}
        </button>
      </form>

      <section className="card p-4 sm:p-5">
        <h2 className="font-medium">{de.cashHistory}</h2>
        <div className="mt-3">
          {data.data ? (
            <DataTable
              columns={columns}
              rows={data.data.counts.slice().sort((a, b) => b.ts - a.ts)}
              rowKey={(count) => count.id}
              empty={de.noCounts}
            />
          ) : (
            <div className="muted">{de.loading}</div>
          )}
        </div>
      </section>
    </div>
  );
}
