import type { ReactNode } from "react";

export function Alert({
  tone = "info",
  children,
  action,
  className = "",
}: {
  tone?: "info" | "good" | "warn" | "bad";
  children: ReactNode;
  action?: ReactNode;
  className?: string;
}) {
  const styles = {
    info: "border-cream-100/15 bg-cream-100/5 text-cream-100",
    good: "border-sage-400/30 bg-sage-400/10 text-sage-400",
    warn: "border-amber-400/30 bg-amber-400/10 text-amber-400",
    bad: "border-rust-400/35 bg-rust-400/10 text-rust-400",
  }[tone];

  return (
    <div
      role={tone === "bad" || tone === "warn" ? "alert" : "status"}
      className={`flex flex-wrap items-center justify-between gap-3 rounded-xl border px-3 py-2 text-sm ${styles} ${className}`}
    >
      <div className="min-w-0">{children}</div>
      {action ? <div className="flex shrink-0 items-center gap-2">{action}</div> : null}
    </div>
  );
}
