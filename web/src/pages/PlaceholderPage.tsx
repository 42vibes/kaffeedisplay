import { de } from "../i18n/de";

export function PlaceholderPage({ title }: { title: string }) {
  return (
    <div>
      <h1 className="text-2xl font-semibold tracking-tight">{title}</h1>
      <div className="card mt-6 p-8 text-center">
        <div className="muted">{de.noData}</div>
      </div>
    </div>
  );
}
