const icons: Record<string, string> = {
  cup: "M4 8h12v6a5 5 0 0 1-5 5H9a5 5 0 0 1-5-5V8Zm12 1h2a3 3 0 0 1 0 6h-2M7 3v2m4-2v2m4-2v2",
  espresso: "M5 9h11v4a5 5 0 0 1-5 5H10a5 5 0 0 1-5-5V9Zm11 1h2a2.5 2.5 0 0 1 0 5h-2M8 4c0 1 1 1 1 2m3-2c0 1 1 1 1 2",
  cocoa: "M5 7h14l-1 13H6L5 7Zm2-3h10v3H7V4Zm3 7v6m4-6v6",
  tea: "M5 8h11v5a5 5 0 0 1-5 5H10a5 5 0 0 1-5-5V8Zm11 1h2a2 2 0 0 1 0 4h-2M9 3c-1 2 1 2 0 4m4-4c-1 2 1 2 0 4",
  water: "M12 3c3 4 6 7 6 11a6 6 0 0 1-12 0c0-4 3-7 6-11Z",
  soda: "M7 4h10l-1 17H8L7 4Zm2 5h6",
  beer: "M7 5h8v15H7V5Zm8 3h3v5h-3M9 8v9m3-9v9",
  snack: "M4 12a8 8 0 0 1 16 0M4 12h16v3a5 5 0 0 1-5 5H9a5 5 0 0 1-5-5v-3Z",
};

export function ProductIcon({ name, className = "h-6 w-6" }: { name?: string; className?: string }) {
  const path = icons[name ?? ""] ?? icons.cup;
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.6"
      strokeLinecap="round"
      strokeLinejoin="round"
      className={className}
      aria-hidden="true"
    >
      <path d={path} />
    </svg>
  );
}

export const productIcons = Object.keys(icons);
