import type { Event } from "../api/types";
import { de } from "../i18n/de";
import { formatDateTime } from "./format";

export function eventLabel(event: Event): string {
  if (event.type === "purchase" || event.type === "guest_sale") {
    return event.product_name ?? de.eventType[event.type] ?? event.type;
  }
  if (event.type === "expense_created" || event.type === "expense_voided") {
    return event.expense_description || event.expense_category || de.eventType[event.type];
  }
  if (event.type === "cash_count") return de.eventType.cash_count;
  return de.eventType[event.type] ?? event.type;
}

export function eventTypeLabel(event: Event): string {
  return de.eventType[event.type] ?? event.type;
}

export function eventPayment(event: Event): string {
  if (event.payment === "balance") return de.paymentBalance;
  if (event.payment === "cash") return de.paymentCash;
  return "–";
}

export function eventAmount(event: Event): number {
  if (event.type === "cash_count") {
    return event.counted_cents ?? 0;
  }
  return event.amount_cents ?? 0;
}

export function eventTime(event: Event): string {
  return formatDateTime(event.ts);
}

export function isConsumption(event: Event): boolean {
  return (event.type === "purchase" || event.type === "guest_sale") && !event.voided;
}
