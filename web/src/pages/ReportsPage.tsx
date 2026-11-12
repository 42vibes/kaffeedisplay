import { useMemo, useState } from "react";
import { api, type ReportRequest } from "../api/endpoints";
import { downloadText } from "../api/client";
import type { ReportColumn, ReportKind, ReportPayload, ReportPeriod, ReportSection } from "../api/types";
import { Alert } from "../components/Alert";
import { DataTable } from "../components/DataTable";
import { PageHeader } from "../components/PageHeader";
import { de } from "../i18n/de";
import { errorText } from "../lib/errors";
import {
  dateInputValue,
  formatDateInZone,
  formatDateTimeInZone,
  formatEuro,
  formatInteger,
} from "../lib/format";
import { monthKeyInZone, periodRange } from "../lib/period";
import { reportSummary, timeQualityState } from "../lib/report";
import { useAsync } from "../lib/useAsync";

const kinds: Array<{ kind: ReportKind; label: string }> = [
  { kind: "overview", label: de.reportOverview },
  { kind: "consumption", label: de.reportConsumption },
  { kind: "deposits", label: de.reportDeposits },
  { kind: "expenses", label: de.reportExpenses },
  { kind: "cashflow", label: de.reportCashflow },
  { kind: "balances", label: de.reportBalances },
  { kind: "transactions", label: de.reportTransactions },
];

const periods: Array<{ period: ReportPeriod; label: string }> = [
  { period: "week", label: de.week },
  { period: "month", label: de.month },
  { period: "year", label: de.year },
  { period: "all", label: de.allTime },
  { period: "custom", label: de.custom },
];

export function ReportsPage() {
  const [kind, setKind] = useState<ReportKind>("overview");
  const [period, setPeriod] = useState<ReportPeriod>("month");
  const [customFrom, setCustomFrom] = useState(() => {
    const now = new Date();
    return dateInputValue(new Date(now.getFullYear(), now.getMonth(), 1));
  });
  const [customTo, setCustomTo] = useState(() => dateInputValue());

  const range = useMemo(
    () => periodRange(period, customFrom, customTo),
    [period, customFrom, customTo],
  );
  const invalidRange =
    period === "custom" &&
    range.from !== undefined &&
    range.to !== undefined &&
    range.from >= range.to;

  const params: ReportRequest = {
    kind,
    period,
    from: period === "custom" ? range.from : undefined,
    to: period === "custom" ? range.to : undefined,
  };
  const requestKey = `${kind}|${period}|${period === "custom" ? `${customFrom}|${customTo}` : ""}`;

  const report = useAsync<ReportPayload>(
    () => api.report(params),
    [requestKey],
  );

  const [downloading, setDownloading] = useState<"csv_excel" | "csv_raw" | "">("");
  const [notice, setNotice] = useState("");

  async function download(format: "csv_excel" | "csv_raw") {
    if (invalidRange) {
      setNotice(de.invalidRange);
      return;
    }
    setDownloading(format);
    setNotice("");
    try {
      const text = await api.reportCsv(params, format);
      const filenameSeconds = range.from ?? Math.floor(Date.now() / 1000);
      const filename = `kaffeekasse_${monthKeyInZone(filenameSeconds)}_${kind}.csv`;
      const body = format === "csv_excel" && !text.startsWith("\uFEFF") ? `\uFEFF${text}` : text;
      downloadText(filename, body, "text/csv;charset=utf-8");
      setNotice(de.exportSuccess);
    } catch (err) {
      setNotice(errorText(err));
    } finally {
      setDownloading("");
    }
  }

  const retry = () => {
    report.reload();
  };

  return (
    <div className="space-y-5">
      <PageHeader
        title={de.reports}
        subtitle={de.reportHint}
        actions={
          <>
            <button
              className="btn"
              type="button"
              onClick={() => download("csv_excel")}
              disabled={downloading !== ""}
            >
              {downloading === "csv_excel" ? de.downloading : de.csvExcel}
            </button>
            <button
              className="btn"
              type="button"
              onClick={() => download("csv_raw")}
              disabled={downloading !== ""}
            >
              {downloading === "csv_raw" ? de.downloading : de.csvRaw}
            </button>
          </>
        }
      />

      {notice ? <Alert tone={notice === de.exportSuccess ? "good" : "warn"}>{notice}</Alert> : null}

      <section className="card space-y-4 p-4 sm:p-5">
        <div>
          <div className="label mb-2">{de.reportPeriod}</div>
          <div className="flex flex-wrap gap-1.5">
            {periods.map((item) => (
              <button
                key={item.period}
                type="button"
                className={`rounded-xl border px-3 py-2 text-sm ${
                  period === item.period
                    ? "border-amber-400 bg-amber-400 font-semibold text-espresso-950"
                    : "border-cream-100/10 text-cream-100/80 hover:bg-cream-100/10"
                }`}
                onClick={() => setPeriod(item.period)}
              >
                {item.label}
              </button>
            ))}
          </div>
        </div>

        {period === "custom" ? (
          <div className="grid gap-3 sm:grid-cols-2">
            <label className="block space-y-1.5">
              <span className="label">{de.customFrom}</span>
              <input
                className="input"
                type="date"
                value={customFrom}
                onChange={(event) => setCustomFrom(event.target.value)}
              />
            </label>
            <label className="block space-y-1.5">
              <span className="label">{de.customTo}</span>
              <input
                className="input"
                type="date"
                value={customTo}
                onChange={(event) => setCustomTo(event.target.value)}
              />
            </label>
          </div>
        ) : null}

        {invalidRange ? <Alert tone="bad">{de.invalidRange}</Alert> : null}

        <div>
          <div className="label mb-2">{de.category}</div>
          <div className="flex gap-1.5 overflow-x-auto pb-1">
            {kinds.map((item) => (
              <button
                key={item.kind}
                type="button"
                className={`shrink-0 rounded-xl border px-3 py-2 text-sm ${
                  kind === item.kind
                    ? "border-amber-400 bg-amber-400/10 text-amber-400"
                    : "border-cream-100/10 text-cream-100/80 hover:bg-cream-100/10"
                }`}
                onClick={() => setKind(item.kind)}
              >
                {item.label}
              </button>
            ))}
          </div>
        </div>
      </section>

      {report.error ? (
        <Alert tone="bad" action={<button className="btn" onClick={retry} type="button">{de.retry}</button>}>
          {report.error}
        </Alert>
      ) : null}

      {report.loading ? <div className="muted">{de.loading}</div> : null}

      {report.data && !report.loading && !report.error && !invalidRange ? (
        <ReportView payload={report.data} />
      ) : null}
    </div>
  );
}

function ReportView({ payload }: { payload: ReportPayload }) {
  const from = typeof payload.from === "number" ? payload.from : undefined;
  const to = typeof payload.to === "number" ? payload.to : undefined;
  const quality = timeQualityState(payload.time_quality?.trusted);

  const sections =
    payload.sections && payload.sections.length > 0
      ? payload.sections
      : payload.rows
        ? [{ title: undefined, columns: payload.columns, rows: payload.rows, totals: payload.totals }]
        : [];

  const summaryValues = reportSummary(payload);
  const hasSummary = Object.keys(summaryValues).length > 0;

  return (
    <div className="space-y-4">
      <div className="card flex flex-wrap items-center justify-between gap-2 p-3 text-sm">
        <div className="muted">
          {from ? formatDateInZone(from) : de.allTime}
          {to ? ` – ${formatDateInZone(to - 1)}` : ""}
        </div>
        {quality === "trusted" ? (
          <span className="text-sage-400">{de.trustedTime}</span>
        ) : quality === "untrusted" ? (
          <span className="text-amber-400">{de.untrustedTime}</span>
        ) : (
          <span className="muted">{de.timeQualityUnknown}</span>
        )}
      </div>

      {sections.length === 0 && !hasSummary ? (
        <div className="card p-8 text-center muted">{de.reportEmpty}</div>
      ) : null}

      {sections.map((section, index) => (
        <SectionView key={`${section.title ?? "section"}-${index}`} section={section} />
      ))}

      {sections.length === 0 && hasSummary ? (
        <section className="card p-4 sm:p-5">
          <h2 className="font-medium">
            {Object.keys(summaryValues).length > Object.keys(payload.totals ?? {}).length
              ? de.reportMetrics
              : de.reportTotals}
          </h2>
          <DefinitionGrid values={summaryValues} />
        </section>
      ) : null}
    </div>
  );
}

function SectionView({ section }: { section: ReportSection }) {
  const rows = section.rows ?? [];
  const columns = section.columns ?? deriveColumns(rows);
  return (
    <section className="card p-4 sm:p-5">
      {section.title ? <h2 className="font-medium">{section.title}</h2> : null}
      <div className={section.title ? "mt-3" : ""}>
        <DataTable
          columns={columns.map((column) => ({
            key: column.key,
            label: column.label || de.reportColumns[column.key] || column.key,
            align: column.type === "money" || column.type === "number" ? "right" : "left",
            render: (row: Record<string, unknown>) => (
              <Cell value={row[column.key]} type={column.type} columnKey={column.key} />
            ),
          }))}
          rows={rows}
          rowKey={(row) => String(row.id ?? row.seq ?? JSON.stringify(row))}
          empty={de.noReportRows}
        />
      </div>
      {section.totals && Object.keys(section.totals).length > 0 ? (
        <div className="mt-4">
          <h3 className="label">{de.reportTotals}</h3>
          <DefinitionGrid values={section.totals} />
        </div>
      ) : null}
    </section>
  );
}

function deriveColumns(rows: Array<Record<string, unknown>>): ReportColumn[] {
  const keys = new Set<string>();
  for (const row of rows.slice(0, 20)) {
    for (const key of Object.keys(row)) keys.add(key);
  }
  return Array.from(keys).map((key) => ({ key }));
}

function Cell({
  value,
  type,
  columnKey,
}: {
  value: unknown;
  type?: ReportColumn["type"];
  columnKey: string;
}) {
  if (value === null || value === undefined || value === "") return <span className="text-cream-500">–</span>;
  if (type === "money" || columnKey.endsWith("_cents")) {
    return <span>{formatEuro(Number(value))}</span>;
  }
  if (type === "number") return <span>{formatInteger(Number(value))}</span>;
  if (type === "date") return <span>{formatDateInZone(Number(value))}</span>;
  if (type === "datetime") return <span>{formatDateTimeInZone(Number(value))}</span>;
  if (type === "boolean") return <span>{value ? de.yes : de.no}</span>;
  if (columnKey === "ts") return <span>{formatDateTimeInZone(Number(value))}</span>;
  if (typeof value === "boolean") return <span>{value ? de.yes : de.no}</span>;
  if (typeof value === "object") return <span>{JSON.stringify(value)}</span>;
  return <span>{String(value)}</span>;
}

function DefinitionGrid({ values }: { values: Record<string, unknown> }) {
  const entries = Object.entries(values).filter(
    ([, value]) => typeof value !== "object" || value === null,
  );
  return (
    <dl className="mt-3 grid gap-2 sm:grid-cols-2 xl:grid-cols-3">
      {entries.map(([key, value]) => (
        <div key={key} className="flex items-baseline justify-between gap-3 rounded-xl border border-cream-100/10 px-3 py-2">
          <dt className="muted text-xs">{de.reportColumns[key] ?? key}</dt>
          <dd className="mono text-sm">
            {key.endsWith("_cents") && typeof value === "number"
              ? formatEuro(value)
              : typeof value === "number"
                ? formatInteger(value)
                : typeof value === "boolean"
                  ? value
                    ? de.yes
                    : de.no
                  : String(value ?? "–")}
          </dd>
        </div>
      ))}
    </dl>
  );
}
