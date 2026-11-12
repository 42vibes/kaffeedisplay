import { de } from "../i18n/de";
import { formatSignedEuro, parseMoneyInput } from "../lib/format";

/** Text input that keeps the raw German decimal string while typing. */
export function MoneyInput({
  value,
  onChange,
  id,
  placeholder = "0,00",
  autoFocus = false,
  className = "",
  disabled = false,
}: {
  value: string;
  onChange: (value: string) => void;
  id?: string;
  placeholder?: string;
  autoFocus?: boolean;
  className?: string;
  disabled?: boolean;
}) {
  return (
    <div className={`relative ${className}`}>
      <input
        id={id}
        className="input mono pr-9"
        inputMode="decimal"
        autoComplete="off"
        value={value}
        onChange={(event) => onChange(event.target.value)}
        placeholder={placeholder}
        autoFocus={autoFocus}
        disabled={disabled}
      />
      <span className="pointer-events-none absolute inset-y-0 right-3 flex items-center text-sm text-cream-500">
        €
      </span>
    </div>
  );
}

export function MoneyPreview({
  value,
  allowNegative = false,
}: {
  value: string;
  allowNegative?: boolean;
}) {
  if (!value.trim()) return null;
  const money = parseMoneyInput(value);
  if (!money.valid) {
    return <div className="mt-1 text-xs text-rust-400">{de.invalidAmount}</div>;
  }
  if (!allowNegative && money.cents < 0) return null;
  return <div className="muted mt-1 text-xs">{formatSignedEuro(money.cents)}</div>;
}
