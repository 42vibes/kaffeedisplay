import type { ReactNode } from "react";
import { de } from "../i18n/de";

export interface Column<T> {
  key: string;
  label: string;
  render?: (row: T) => ReactNode;
  align?: "left" | "right";
  hideBelow?: "sm" | "md" | "lg";
}

const hideClass = {
  sm: "hidden sm:table-cell",
  md: "hidden md:table-cell",
  lg: "hidden lg:table-cell",
} as const;

export function DataTable<T>({
  columns,
  rows,
  rowKey,
  empty,
  onRowClick,
}: {
  columns: Array<Column<T>>;
  rows: T[];
  rowKey: (row: T) => string;
  empty?: ReactNode;
  onRowClick?: (row: T) => void;
}) {
  if (rows.length === 0) {
    return (
      <div className="rounded-xl border border-cream-100/10 px-4 py-8 text-center text-sm text-cream-500">
        {empty ?? de.noData}
      </div>
    );
  }

  return (
    <div className="-mx-1 overflow-x-auto">
      <table className="w-full min-w-[520px] border-collapse text-left text-sm">
        <thead>
          <tr className="border-b border-cream-100/10">
            {columns.map((column) => (
              <th
                key={column.key}
                scope="col"
                className={`label whitespace-nowrap px-2 py-2 font-normal ${
                  column.align === "right" ? "text-right" : ""
                } ${column.hideBelow ? hideClass[column.hideBelow] : ""}`}
              >
                {column.label}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map((row) => (
            <tr
              key={rowKey(row)}
              className={`border-b border-cream-100/5 last:border-0 ${
                onRowClick ? "cursor-pointer hover:bg-cream-100/5" : ""
              }`}
              onClick={onRowClick ? () => onRowClick(row) : undefined}
            >
              {columns.map((column) => (
                <td
                  key={column.key}
                  className={`px-2 py-2.5 align-middle ${
                    column.align === "right" ? "text-right mono" : ""
                  } ${column.hideBelow ? hideClass[column.hideBelow] : ""}`}
                >
                  {column.render ? column.render(row) : String((row as Record<string, unknown>)[column.key] ?? "–")}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
