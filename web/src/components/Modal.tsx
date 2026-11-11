import { useEffect, type ReactNode } from "react";
import { de } from "../i18n/de";

export function Modal({
  open,
  title,
  children,
  footer,
  onClose,
  wide = false,
}: {
  open: boolean;
  title: string;
  children: ReactNode;
  footer?: ReactNode;
  onClose: () => void;
  wide?: boolean;
}) {
  useEffect(() => {
    if (!open) return;
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") onClose();
    };
    document.addEventListener("keydown", onKey);
    const previous = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    return () => {
      document.removeEventListener("keydown", onKey);
      document.body.style.overflow = previous;
    };
  }, [open, onClose]);

  if (!open) return null;

  return (
    <div
      className="fixed inset-0 z-50 flex items-end justify-center overflow-y-auto bg-espresso-950/75 p-0 backdrop-blur-sm sm:items-center sm:p-4"
      role="presentation"
      onMouseDown={(event) => {
        if (event.target === event.currentTarget) onClose();
      }}
    >
      <div
        className={`card w-full ${wide ? "sm:max-w-3xl" : "sm:max-w-lg"} rounded-b-none sm:rounded-2xl`}
        role="dialog"
        aria-modal="true"
        aria-label={title}
      >
        <div className="flex items-center justify-between gap-4 border-b border-cream-100/10 px-4 py-3 sm:px-5">
          <h2 className="text-lg font-medium">{title}</h2>
          <button className="btn min-h-9 px-3 py-1 text-sm" type="button" onClick={onClose}>
            {de.close}
          </button>
        </div>
        <div className="max-h-[70vh] overflow-y-auto px-4 py-4 sm:px-5">{children}</div>
        {footer ? (
          <div className="flex flex-wrap justify-end gap-2 border-t border-cream-100/10 px-4 py-3 sm:px-5">
            {footer}
          </div>
        ) : null}
      </div>
    </div>
  );
}
