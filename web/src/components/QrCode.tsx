import { useMemo } from "react";
import qrcode from "qrcode-generator";

/**
 * Render a QR payload locally. The real device API returns plain `WIFI:...`
 * strings and URLs, so the SPA must not depend on a server-rendered image.
 */
export function QrCode({
  value,
  label,
  className = "h-full w-full",
}: {
  value: string;
  label: string;
  className?: string;
}) {
  const qr = useMemo(() => {
    if (!value) return null;
    try {
      const code = qrcode(0, "M");
      code.addData(value);
      code.make();
      const count = code.getModuleCount();
      let path = "";
      for (let row = 0; row < count; row += 1) {
        for (let col = 0; col < count; col += 1) {
          if (code.isDark(row, col)) path += `M${col} ${row}h1v1h-1z`;
        }
      }
      return { count, path };
    } catch {
      return null;
    }
  }, [value]);

  if (!qr) return null;

  return (
    <svg
      role="img"
      aria-label={label}
      viewBox={`0 0 ${qr.count} ${qr.count}`}
      shapeRendering="crispEdges"
      className={className}
    >
      <rect width={qr.count} height={qr.count} fill="#ffffff" />
      <path d={qr.path} fill="#120e0c" />
    </svg>
  );
}
