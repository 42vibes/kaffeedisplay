import type { ReactNode } from "react";

export function StatCard({
  label,
  value,
  hint,
  tone = "neutral",
}: {
  label: string;
  value: ReactNode;
  hint?: ReactNode;
  tone?: "neutral" | "good" | "warn" | "bad";
}) {
  const color =
    tone === "good"
      ? "text-sage-400"
      : tone === "warn"
        ? "text-amber-400"
        : tone === "bad"
          ? "text-rust-400"
          : "text-cream-50";
  return (
    <div className="card p-4 sm:p-5">
      <div className="label">{label}</div>
      <div className={`mono mt-2 text-2xl font-semibold tracking-tight sm:text-3xl ${color}`}>
        {value}
      </div>
      {hint ? <div className="muted mt-1 text-xs">{hint}</div> : null}
    </div>
  );
}
