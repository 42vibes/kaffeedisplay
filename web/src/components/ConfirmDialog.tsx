import type { ReactNode } from "react";
import { de } from "../i18n/de";
import { Modal } from "./Modal";

export function ConfirmDialog({
  open,
  title,
  body,
  confirmLabel = de.confirm,
  tone = "danger",
  busy = false,
  onConfirm,
  onClose,
  children,
}: {
  open: boolean;
  title: string;
  body: ReactNode;
  confirmLabel?: string;
  tone?: "danger" | "primary";
  busy?: boolean;
  onConfirm: () => void;
  onClose: () => void;
  children?: ReactNode;
}) {
  return (
    <Modal
      open={open}
      title={title}
      onClose={onClose}
      footer={
        <>
          <button className="btn" type="button" onClick={onClose} disabled={busy}>
            {de.cancel}
          </button>
          <button
            className={`btn ${tone === "danger" ? "btn-danger" : "btn-primary"}`}
            type="button"
            onClick={onConfirm}
            disabled={busy}
          >
            {busy ? de.saving : confirmLabel}
          </button>
        </>
      }
    >
      <div className="space-y-3 text-sm leading-relaxed text-cream-100/90">{body}</div>
      {children}
    </Modal>
  );
}
