import { useMemo, useState } from "react";
import { api } from "../api/endpoints";
import type { Expense, ExpenseInput } from "../api/types";
import { Alert } from "../components/Alert";
import { ConfirmDialog } from "../components/ConfirmDialog";
import { DataTable, type Column } from "../components/DataTable";
import { MoneyInput, MoneyPreview } from "../components/MoneyInput";
import { Modal } from "../components/Modal";
import { PageHeader } from "../components/PageHeader";
import { de } from "../i18n/de";
import { errorText } from "../lib/errors";
import { formatDate, formatEuro, inputFromCents, parseMoneyInput } from "../lib/format";
import { useAsync } from "../lib/useAsync";
import { useWriteId } from "../lib/useWriteId";

type Dialog =
  | { kind: "create" }
  | { kind: "edit"; expense: Expense }
  | { kind: "void"; expense: Expense }
  | null;

export function ExpensesPage() {
  const [search, setSearch] = useState("");
  const [showVoided, setShowVoided] = useState(true);
  const [dialog, setDialog] = useState<Dialog>(null);
  const [notice, setNotice] = useState("");
  const expenses = useAsync<Expense[]>(() => api.expenses(true), []);

  const filtered = useMemo(() => {
    const query = search.trim().toLowerCase();
    return (expenses.data ?? [])
      .filter((expense) => {
        if (!showVoided && expense.voided) return false;
        if (!query) return true;
        return (
          (expense.description ?? "").toLowerCase().includes(query) ||
          (expense.category ?? "").toLowerCase().includes(query) ||
          (expense.vendor ?? "").toLowerCase().includes(query)
        );
      })
      .sort((a, b) => b.ts - a.ts);
  }, [expenses.data, search, showVoided]);

  const total = filtered
    .filter((expense) => !expense.voided)
    .reduce((sum, expense) => sum + expense.amount_cents, 0);

  function saved(message: string) {
    setNotice(message);
    setDialog(null);
    expenses.reload();
  }

  const columns: Array<Column<Expense>> = [
    {
      key: "ts",
      label: de.date,
      render: (expense) => <span className="whitespace-nowrap">{formatDate(expense.ts)}</span>,
    },
    {
      key: "description",
      label: de.description,
      render: (expense) => (
        <div className={`min-w-0 ${expense.voided ? "line-through opacity-60" : ""}`}>
          <div className="truncate">{expense.description || "–"}</div>
          <div className="muted text-xs">{expense.category || "–"}</div>
        </div>
      ),
    },
    {
      key: "vendor",
      label: de.vendor,
      hideBelow: "md",
      render: (expense) => <span className="text-sm">{expense.vendor || "–"}</span>,
    },
    {
      key: "amount",
      label: de.amount,
      align: "right",
      render: (expense) => (
        <span className={expense.voided ? "line-through opacity-60" : ""}>
          {formatEuro(expense.amount_cents)}
        </span>
      ),
    },
    {
      key: "status",
      label: de.status,
      hideBelow: "sm",
      render: (expense) =>
        expense.voided ? (
          <span className="rounded-full border border-rust-400/30 px-2 py-0.5 text-[11px] text-rust-400">{de.voided}</span>
        ) : (
          <span className="rounded-full border border-sage-400/30 px-2 py-0.5 text-[11px] text-sage-400">{de.booked}</span>
        ),
    },
    {
      key: "actions",
      label: de.actions,
      render: (expense) => (
        <div className="flex flex-wrap gap-1.5">
          {!expense.voided ? (
            <>
              <button className="btn min-h-8 px-2 py-0.5 text-xs" type="button" onClick={() => setDialog({ kind: "edit", expense })}>
                {de.edit}
              </button>
              <button className="btn btn-danger min-h-8 px-2 py-0.5 text-xs" type="button" onClick={() => setDialog({ kind: "void", expense })}>
                {de.void}
              </button>
            </>
          ) : null}
        </div>
      ),
    },
  ];

  return (
    <div className="space-y-5">
      <PageHeader
        title={de.expenses}
        subtitle={`${de.total} ${formatEuro(total)}`}
        actions={
          <button className="btn btn-primary" type="button" onClick={() => setDialog({ kind: "create" })}>
            {de.addExpense}
          </button>
        }
      />

      {notice ? <Alert tone="good">{notice}</Alert> : null}
      {expenses.error ? (
        <Alert tone="bad" action={<button className="btn" onClick={expenses.reload} type="button">{de.retry}</button>}>
          {expenses.error}
        </Alert>
      ) : null}

      <div className="flex flex-col gap-2 sm:flex-row sm:items-center">
        <label className="flex-1">
          <span className="sr-only">{de.searchExpenses}</span>
          <input
            className="input"
            value={search}
            onChange={(event) => setSearch(event.target.value)}
            placeholder={`${de.searchExpenses} …`}
            type="search"
          />
        </label>
        <label className="flex min-h-11 items-center gap-2 rounded-xl border border-cream-100/10 px-3 text-sm">
          <input
            type="checkbox"
            className="h-4 w-4 accent-amber-400"
            checked={showVoided}
            onChange={(event) => setShowVoided(event.target.checked)}
          />
          {de.showVoided}
        </label>
      </div>

      {expenses.loading && !expenses.data ? <div className="muted">{de.loading}</div> : null}
      {expenses.data ? <DataTable columns={columns} rows={filtered} rowKey={(expense) => expense.id} empty={de.noExpenses} /> : null}

      {dialog?.kind === "create" ? (
        <ExpenseFormDialog onClose={() => setDialog(null)} onSaved={() => saved(de.expenseCreated)} />
      ) : null}
      {dialog?.kind === "edit" ? (
        <ExpenseFormDialog expense={dialog.expense} onClose={() => setDialog(null)} onSaved={() => saved(de.expenseUpdated)} />
      ) : null}
      {dialog?.kind === "void" ? (
        <VoidDialog expense={dialog.expense} onClose={() => setDialog(null)} onSaved={() => saved(de.expenseVoided)} />
      ) : null}
    </div>
  );
}

function ExpenseFormDialog({
  expense,
  onClose,
  onSaved,
}: {
  expense?: Expense;
  onClose: () => void;
  onSaved: () => void;
}) {
  const [category, setCategory] = useState(expense?.category ?? "Maschine");
  const [description, setDescription] = useState(expense?.description ?? "");
  const [vendor, setVendor] = useState(expense?.vendor ?? "");
  const [amount, setAmount] = useState(expense ? inputFromCents(expense.amount_cents) : "");
  const [note, setNote] = useState(expense?.note ?? "");  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const writeId = useWriteId();

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    const money = parseMoneyInput(amount);
    if (!description.trim() || !category.trim()) {
      setError(de.required);
      return;
    }
    if (!money.valid || money.cents <= 0) {
      setError(de.amountPositive);
      return;
    }
    const input: ExpenseInput = {
      category: category.trim(),
      description: description.trim(),
      vendor: vendor.trim(),
      amount_cents: money.cents,
      note: note.trim(),
    };
    setBusy(true);
    setError("");
    try {
      if (expense) {
        await api.updateExpense(expense.id, input, writeId.id);
      } else {
        await api.createExpense(input, writeId.id);
      }
      writeId.renew();
      onSaved();
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      title={expense ? de.editExpenseTitle : de.createExpenseTitle}
      onClose={onClose}
      footer={
        <>
          <button className="btn" type="button" onClick={onClose} disabled={busy}>
            {de.cancel}
          </button>
          <button className="btn btn-primary" type="submit" form="expense-form" disabled={busy}>
            {busy ? de.saving : de.save}
          </button>
        </>
      }
    >
      <form id="expense-form" className="space-y-4" onSubmit={submit}>
        <div className="grid gap-3 sm:grid-cols-2">
          <label className="block space-y-1.5">
            <span className="label">{de.expenseDescription}</span>
            <input
              className="input"
              value={description}
              onChange={(event) => setDescription(event.target.value)}
              maxLength={120}
              required
              autoFocus
            />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.expenseCategory}</span>
            <input
              className="input"
              value={category}
              onChange={(event) => setCategory(event.target.value)}
              maxLength={40}
              list="expense-categories"
              required
            />
            <datalist id="expense-categories">
              {de.expenseCategories.map((option) => (
                <option key={option} value={option} />
              ))}
            </datalist>
          </label>
        </div>

        <div className="grid gap-3 sm:grid-cols-2">
          <label className="block space-y-1.5">
            <span className="label">{de.expenseAmount}</span>
            <MoneyInput value={amount} onChange={setAmount} />
            <MoneyPreview value={amount} />
          </label>
          <label className="block space-y-1.5">
            <span className="label">{de.expenseVendor} ({de.optional})</span>
            <input
              className="input"
              value={vendor}
              onChange={(event) => setVendor(event.target.value)}
              maxLength={80}
            />
          </label>
        </div>

        <label className="block space-y-1.5">
          <span className="label">{de.expenseNote} ({de.optional})</span>
          <textarea
            className="input min-h-20"
            value={note}
            onChange={(event) => setNote(event.target.value)}
            maxLength={300}
          />
        </label>

        {error ? <div className="text-sm text-rust-400" role="alert">{error}</div> : null}
      </form>
    </Modal>
  );
}

function VoidDialog({
  expense,
  onClose,
  onSaved,
}: {
  expense: Expense;
  onClose: () => void;
  onSaved: () => void;
}) {
  const [reason, setReason] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const writeId = useWriteId();

  async function run() {
    if (reason.trim().length < 3) {
      setError(de.noteRequired);
      return;
    }
    setBusy(true);
    setError("");
    try {
      await api.voidExpense(expense.id, reason.trim(), writeId.id);
      writeId.renew();
      onSaved();
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  }

  return (
    <ConfirmDialog
      open
      title={de.voidExpenseTitle}
      body={
        <>
          <p>{de.voidExpenseBody}</p>
          <p className="font-medium text-cream-50">
            {expense.description} · {formatEuro(expense.amount_cents)}
          </p>
        </>
      }
      confirmLabel={de.void}
      busy={busy}
      onConfirm={run}
      onClose={onClose}
    >
      <label className="mt-3 block space-y-1.5">
        <span className="label">{de.voidReason}</span>
        <input
          className="input"
          value={reason}
          onChange={(event) => setReason(event.target.value)}
          maxLength={200}
          required
        />
      </label>
      {error ? <p className="mt-2 text-sm text-rust-400" role="alert">{error}</p> : null}
    </ConfirmDialog>
  );
}
