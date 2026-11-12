import { formatEuro } from "../lib/format";

export interface ChartPoint {
  label: string;
  value: number;
  title?: string;
}

/** Dependency-free bar chart (CSS bars) for daily consumption in cents. */
export function BarChart({
  points,
  label,
  height = 168,
}: {
  points: ChartPoint[];
  label: string;
  height?: number;
}) {
  const max = Math.max(1, ...points.map((point) => point.value));
  const total = points.reduce((sum, point) => sum + point.value, 0);
  const step = points.length > 20 ? 3 : points.length > 12 ? 2 : 1;

  return (
    <figure aria-label={`${label}: ${formatEuro(total)}`} className="w-full">
      <div className="flex items-end gap-1" style={{ height }}>
        {points.map((point, index) => {
          const ratio = point.value / max;
          return (
            <div
              key={`${point.label}-${index}`}
              className="group relative flex h-full flex-1 flex-col justify-end"
              title={`${point.title ?? point.label}: ${formatEuro(point.value)}`}
            >
              <div
                className="w-full rounded-t-[3px] bg-amber-400/80 transition group-hover:bg-amber-400"
                style={{
                  height: `${Math.max(point.value > 0 ? 3 : 0, ratio * 100)}%`,
                  minHeight: point.value > 0 ? 3 : 1,
                  opacity: point.value > 0 ? 1 : 0.25,
                }}
              />
              <span className="sr-only">
                {point.title ?? point.label}: {formatEuro(point.value)}
              </span>
            </div>
          );
        })}
      </div>
      <div className="mt-2 flex gap-1">
        {points.map((point, index) => (
          <div key={`label-${point.label}-${index}`} className="flex-1 text-center">
            <span className="text-[9px] text-cream-500">
              {index % step === 0 ? point.label : ""}
            </span>
          </div>
        ))}
      </div>
    </figure>
  );
}
