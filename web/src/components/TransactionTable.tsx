import type { Event } from "../api/types";
import { de } from "../i18n/de";
import { formatDateTime, formatSignedEuro } from "../lib/format";
import { eventLabel, eventTypeLabel } from "../lib/events";
import { DataTable, type Column } from "./DataTable";

const columns: Array<Column<Event>> = [
  {
    key: "ts",
    label: de.date,
    render: (event) => (
      <span className="whitespace-nowrap text-xs sm:text-sm">{formatDateTime(event.ts)}</span>
    ),
  },
  {
    key: "type",
    label: de.category,
    hideBelow: "sm",
    render: (event) => (
      <span className="whitespace-nowrap text-xs sm:text-sm">{eventTypeLabel(event)}</span>
    ),
  },
  {
    key: "detail",
    label: de.details,
    render: (event) => (
      <div className="min-w-0">
        <div className="truncate">{eventLabel(event)}</div>
        {event.user_name ? <div className="muted text-xs">{event.user_name}</div> : null}
      </div>
    ),
  },
  {
    key: "amount",
    label: de.amount,
    align: "right",
    render: (event) => {
      const amount = event.amount_cents ?? 0;
      const tone = amount > 0 ? "text-sage-400" : amount < 0 ? "text-cream-50" : "text-cream-500";
      return (
        <span className={`whitespace-nowrap ${tone} ${event.voided ? "line-through opacity-60" : ""}`}>
          {formatSignedEuro(amount)}
        </span>
      );
    },
  },
];

export function TransactionTable({
  events,
  empty,
}: {
  events: Event[];
  empty?: string;
}) {
  return (
    <DataTable
      columns={columns}
      rows={events}
      rowKey={(event) => `${event.id}-${event.seq}`}
      empty={empty ?? de.noTransactions}
    />
  );
}
