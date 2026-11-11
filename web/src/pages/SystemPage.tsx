import { useState } from "react";
import { api } from "../api/endpoints";
import { downloadText } from "../api/client";
import type { ApStatus, DeviceStatus, LogsResponse } from "../api/types";
import { Alert } from "../components/Alert";
import { ConfirmDialog } from "../components/ConfirmDialog";
import { Modal } from "../components/Modal";
import { PageHeader } from "../components/PageHeader";
import { QrCode } from "../components/QrCode";
import { StatCard } from "../components/StatCard";
import { de } from "../i18n/de";
import { errorText } from "../lib/errors";
import {
  dateInputValue,
  formatBytes,
  formatDateTime,
  formatDuration,
  formatInteger,
  formatPercent,
} from "../lib/format";
import { useAsync } from "../lib/useAsync";
import { useWriteId } from "../lib/useWriteId";

interface SystemData {
  device: DeviceStatus;
  ap: ApStatus;
  logs: LogsResponse;
}

// The firmware route is not part of the OpenAPI contract yet (see PR notes).
// Keep the UI honest instead of firing a request that always 404s.
const FACTORY_RESET_AVAILABLE = false;

export function SystemPage() {
  const [notice, setNotice] = useState("");
  const [error, setError] = useState("");
  const [modalError, setModalError] = useState("");
  const [resetError, setResetError] = useState("");
  const [busy, setBusy] = useState<"ap" | "time" | "backup" | "restore" | "ota" | "reset" | "">("");
  const [extendOpen, setExtendOpen] = useState(false);
  const [extendMinutes, setExtendMinutes] = useState("10");
  const [stopConfirmOpen, setStopConfirmOpen] = useState(false);
  const [restoreValue, setRestoreValue] = useState<unknown>(null);
  const [restoreName, setRestoreName] = useState("");
  const [restoreConfirmOpen, setRestoreConfirmOpen] = useState(false);
  const [otaFile, setOtaFile] = useState<File | null>(null);
  const [otaConfirmOpen, setOtaConfirmOpen] = useState(false);
  const [resetOpen, setResetOpen] = useState(false);
  const [resetText, setResetText] = useState("");

  const data = useAsync<SystemData>(async () => {
    const [device, ap, logs] = await Promise.all([api.device(), api.ap(), api.logs(200)]);
    return { device, ap, logs };
  }, []);

  const writeId = useWriteId();
  const resetWrite = useWriteId();

  function success(message: string) {
    setError("");
    setModalError("");
    setResetError("");
    setNotice(message);
  }

  function failure(err: unknown) {
    setNotice("");
    setError(errorText(err));
  }

  async function startAp() {
    setBusy("ap");
    try {
      await api.startAp(writeId.id);
      writeId.renew();
      success(de.apStarted);
      data.reload();
    } catch (err) {
      failure(err);
    } finally {
      setBusy("");
    }
  }

  async function stopAp() {
    setBusy("ap");
    try {
      await api.stopAp(writeId.id);
      writeId.renew();
      success(de.apStopped);
      setStopConfirmOpen(false);
      data.reload();
    } catch (err) {
      failure(err);
    } finally {
      setBusy("");
    }
  }

  async function extendAp() {
    const minutes = Number.parseInt(extendMinutes, 10) || 10;
    setBusy("ap");
    try {
      await api.extendAp(Math.max(60, minutes * 60), writeId.id);
      writeId.renew();
      setExtendOpen(false);
      success(de.apExtended);
      data.reload();
    } catch (err) {
      failure(err);
    } finally {
      setBusy("");
    }
  }

  async function syncTime() {
    setBusy("time");
    try {
      const timezone = Intl.DateTimeFormat().resolvedOptions().timeZone || "Europe/Berlin";
      await api.syncTime(Date.now(), timezone);
      success(de.timeSynced);
      data.reload();
    } catch (err) {
      failure(err);
    } finally {
      setBusy("");
    }
  }

  async function backup() {
    setBusy("backup");
    try {
      const payload = await api.backup();
      downloadText(
        `kaffeekasse-backup-${dateInputValue()}.json`,
        JSON.stringify(payload, null, 2),
        "application/json",
      );
      success(de.backupSuccess);
    } catch (err) {
      failure(err);
    } finally {
      setBusy("");
    }
  }

  async function restore() {
    setBusy("restore");
    setModalError("");
    try {
      await api.restore(restoreValue, writeId.id);
      writeId.renew();
      setRestoreValue(null);
      setRestoreName("");
      setRestoreConfirmOpen(false);
      success(de.restoreSuccess);
      data.reload();
    } catch (err) {
      setModalError(errorText(err));
    } finally {
      setBusy("");
    }
  }

  async function uploadFirmware() {
    if (!otaFile) return;
    setBusy("ota");
    setModalError("");
    try {
      await api.uploadFirmware(otaFile, writeId.id);
      writeId.renew();
      setOtaFile(null);
      setOtaConfirmOpen(false);
      success(de.otaSuccess);
    } catch (err) {
      setModalError(errorText(err));
    } finally {
      setBusy("");
    }
  }

  async function factoryReset() {
    if (resetText !== de.factoryResetConfirm) return;
    setBusy("reset");
    setResetError("");
    try {
      await api.factoryReset(resetWrite.id);
      resetWrite.renew();
      setResetOpen(false);
      setResetText("");
      success(de.factoryResetSuccess);
    } catch {
      setResetError(de.factoryResetFailed);
    } finally {
      setBusy("");
    }
  }

  const device = data.data?.device;
  const ap = data.data?.ap;
  const logs = data.data?.logs;

  return (
    <div className="space-y-5">
      <PageHeader
        title={de.system}
        subtitle={device ? `${de.firmware} ${device.firmware_version} · ${device.build_date}` : undefined}
        actions={
          <button className="btn" onClick={data.reload} type="button" disabled={data.loading}>
            {data.loading ? de.loading : de.refresh}
          </button>
        }
      />

      {notice ? <Alert tone="good">{notice}</Alert> : null}
      {error ? <Alert tone="bad">{error}</Alert> : null}
      {data.error ? (
        <Alert tone="bad" action={<button className="btn" onClick={data.reload} type="button">{de.retry}</button>}>
          {data.error}
        </Alert>
      ) : null}

      {!device ? <div className="muted">{de.loading}</div> : null}

      {device ? (
        <>
          <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
            <StatCard label={de.uptime} value={formatDuration(device.uptime_s)} />
            <StatCard label={de.freeHeap} value={formatBytes(device.free_heap)} />
            <StatCard label={de.freePsram} value={formatBytes(device.free_psram)} />
            <StatCard
              label={de.chipTemp}
              value={device.temperature_valid ? `${device.temperature_c.toFixed(1)} °C` : "–"}
            />
          </div>

          <section className="card p-4 sm:p-5">
            <h2 className="font-medium">{de.deviceStats}</h2>
            <dl className="mt-3 grid gap-2 sm:grid-cols-2 xl:grid-cols-3">
              <Stat label={de.mac} value={device.mac} />
              <Stat label={de.ipAddress} value={device.network?.sta_ip || "–"} />
              <Stat
                label={de.rssi}
                value={
                  device.network?.sta_connected
                    ? `${device.network.sta_rssi ?? "–"} dBm`
                    : "–"
                }
              />
              <Stat
                label={de.storage}
                value={`${formatBytes(device.storage_used)} / ${formatBytes(device.storage_total)} (${formatPercent(
                  device.storage_total > 0 ? device.storage_used / device.storage_total : 0,
                )})`}
              />
              <Stat label={de.drinksToday} value={formatInteger(device.drinks_today)} />
              <Stat label={de.lastBackup} value={device.last_backup_at ? formatDateTime(device.last_backup_at) : de.backupNever} />
            </dl>
          </section>

          <section className="card space-y-3 p-4 sm:p-5">
            <div className="flex flex-wrap items-center justify-between gap-2">
              <h2 className="font-medium">{de.timeStatus}</h2>
              <button className="btn min-h-9 px-3 py-1 text-sm" type="button" onClick={syncTime} disabled={busy !== ""}>
                {busy === "time" ? de.loading : de.timeSync}
              </button>
            </div>
            <div className="flex flex-wrap items-center gap-2 text-sm">
              <span className={device.time.trusted ? "text-sage-400" : "text-amber-400"}>
                {device.time.trusted ? de.timeTrusted : de.timeUntrusted}
              </span>
              <span className="muted">
                {formatDateTime(device.time.now)} · {de.timeSource}:{" "}
                {de.timeSourceLabels[device.time.source] ?? de.timeSourceUnset} · {de.lastSync}:{" "}
                {formatDateTime(device.time.last_sync)}
              </span>
            </div>
          </section>

          <section className="card space-y-4 p-4 sm:p-5">
            <div className="flex flex-wrap items-center justify-between gap-2">
              <div>
                <h2 className="font-medium">{de.apSection}</h2>
                <div className={`mt-1 text-sm ${ap?.active ? "text-sage-400" : "muted"}`}>
                  {ap?.active ? de.apActive : de.apInactive}
                  {ap?.active ? ` · ${de.apRemaining}: ${formatDuration(ap.remaining_seconds)}` : ""}
                </div>
              </div>
              <div className="flex flex-wrap gap-2">
                {ap?.active ? (
                  <>
                    <button className="btn min-h-9 px-3 py-1 text-sm" type="button" onClick={() => setExtendOpen(true)} disabled={busy !== ""}>
                      {de.apExtend}
                    </button>
                    <button className="btn btn-danger min-h-9 px-3 py-1 text-sm" type="button" onClick={() => setStopConfirmOpen(true)} disabled={busy !== ""}>
                      {de.apStop}
                    </button>
                  </>
                ) : (
                  <button className="btn btn-primary min-h-9 px-3 py-1 text-sm" type="button" onClick={startAp} disabled={busy !== ""}>
                    {busy === "ap" ? de.loading : de.apStart}
                  </button>
                )}
              </div>
            </div>

            {ap?.active ? (
              <div className="grid gap-4 sm:grid-cols-2">
                <div>
                  <div className="label">{de.apQrWifi}</div>
                  <QrImage value={ap.wifi_qr} label={de.apQrWifi} />
                  <div className="muted mt-1 text-xs">
                    {ap.ssid} · {ap.password || "–"}
                  </div>
                </div>
                <div>
                  <div className="label">{de.apQrUrl}</div>
                  <QrImage value={ap.url_qr} label={de.apQrUrl} />
                  <div className="muted mt-1 text-xs">
                    {de.apUrl}: {ap.url || ap.ip}
                  </div>
                </div>
              </div>
            ) : null}
          </section>
        </>
      ) : null}

      <section className="card space-y-3 p-4 sm:p-5">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <h2 className="font-medium">{de.backupSection}</h2>
          <button className="btn min-h-9 px-3 py-1 text-sm" type="button" onClick={backup} disabled={busy !== ""}>
            {busy === "backup" ? de.downloading : de.backupDownload}
          </button>
        </div>
        <p className="muted text-sm">{de.backupHint}</p>

        <div className="border-t border-cream-100/10 pt-3">
          <h3 className="font-medium">{de.restoreSection}</h3>
          <p className="muted mt-1 text-sm">{de.restoreHint}</p>
          <div className="mt-2 flex flex-col gap-2 sm:flex-row sm:items-center">
            <label className="btn cursor-pointer">
              <input
                type="file"
                className="sr-only"
                accept="application/json,.json"
                onChange={async (event) => {
                  const file = event.target.files?.[0];
                  if (!file) return;
                  try {
                    const parsed = JSON.parse(await file.text()) as unknown;
                    setRestoreValue(parsed);
                    setRestoreName(file.name);
                    setError("");
                  } catch {
                    setRestoreValue(null);
                    setRestoreName("");
                    setError(de.restoreInvalid);
                  }
                }}
              />
              {de.chooseFile}
            </label>
            <div className="muted min-w-0 flex-1 truncate text-xs">
              {restoreName || de.noFileChosen}
            </div>
            <button
              className="btn"
              type="button"
              onClick={() => {
                setModalError("");
                setRestoreConfirmOpen(true);
              }}
              disabled={!restoreValue || busy !== ""}
            >
              {busy === "restore" ? de.saving : de.restoreUpload}
            </button>
          </div>
        </div>
      </section>

      <section className="card space-y-3 p-4 sm:p-5">
        <h2 className="font-medium">{de.otaSection}</h2>
        <p className="muted text-sm">{de.otaHint}</p>
        <div className="flex flex-col gap-2 sm:flex-row sm:items-center">
          <label className="btn cursor-pointer">
            <input
              type="file"
              className="sr-only"
              accept=".bin,application/octet-stream"
              onChange={(event) => setOtaFile(event.target.files?.[0] ?? null)}
            />
            {de.chooseFile}
          </label>
          <div className="muted min-w-0 flex-1 truncate text-xs">
            {otaFile ? `${otaFile.name} · ${formatBytes(otaFile.size)}` : de.noFileChosen}
          </div>
          <button
            className="btn btn-primary"
            type="button"
            onClick={() => {
              setModalError("");
              setOtaConfirmOpen(true);
            }}
            disabled={!otaFile || busy !== ""}
          >
            {busy === "ota" ? de.uploading : de.otaUpload}
          </button>
        </div>
        <p className="muted text-xs">{de.rebootHint}</p>
      </section>

      <section className="card space-y-3 p-4 sm:p-5">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <h2 className="font-medium">{de.logs}</h2>
          <button className="btn min-h-9 px-3 py-1 text-sm" type="button" onClick={data.reload} disabled={data.loading}>
            {de.logsRefresh}
          </button>
        </div>
        {logs && logs.lines.length > 0 ? (
          <pre className="mono max-h-80 overflow-auto rounded-xl border border-cream-100/10 bg-espresso-950/60 p-3 text-xs leading-relaxed text-cream-100/85">
            {logs.lines.join("\n")}
          </pre>
        ) : (
          <div className="muted text-sm">{de.logsEmpty}</div>
        )}
      </section>

      <section className="rounded-2xl border border-rust-400/30 bg-rust-400/5 p-4 sm:p-5">
        <h2 className="font-medium text-rust-400">{de.dangerZone}</h2>
        <p className="muted mt-1 text-sm">{de.factoryResetBody}</p>
        <button
          className="btn btn-danger mt-3"
          type="button"
          onClick={() => setResetOpen(true)}
          disabled={!FACTORY_RESET_AVAILABLE}
          title={de.factoryResetGap}
        >
          {FACTORY_RESET_AVAILABLE ? de.factoryReset : de.factoryResetUnavailable}
        </button>
        <p className="muted mt-2 text-xs">{de.factoryResetGap}</p>
      </section>

      <Modal
        open={extendOpen}
        title={de.apExtend}
        onClose={() => setExtendOpen(false)}
        footer={
          <>
            <button className="btn" type="button" onClick={() => setExtendOpen(false)} disabled={busy !== ""}>
              {de.cancel}
            </button>
            <button className="btn btn-primary" type="button" onClick={extendAp} disabled={busy !== ""}>
              {busy === "ap" ? de.saving : de.apExtend}
            </button>
          </>
        }
      >
        <p className="text-sm text-cream-100/80">{de.apExtendBody}</p>
        <label className="mt-3 block space-y-1.5">
          <span className="label">{de.minutes}</span>
          <input
            className="input mono"
            type="number"
            min={1}
            max={60}
            value={extendMinutes}
            onChange={(event) => setExtendMinutes(event.target.value)}
          />
        </label>
      </Modal>

      <ConfirmDialog
        open={stopConfirmOpen}
        title={de.apStopTitle}
        body={<p>{de.apStopBody}</p>}
        confirmLabel={de.apStop}
        busy={busy === "ap"}
        onConfirm={stopAp}
        onClose={() => setStopConfirmOpen(false)}
      />

      <ConfirmDialog
        open={restoreConfirmOpen}
        title={de.restoreSection}
        body={
          <>
            <p>{de.restoreConfirm}</p>
            <p className="font-medium text-cream-50">{restoreName}</p>
            {modalError ? (
              <p className="text-rust-400" role="alert">
                {modalError}
              </p>
            ) : null}
          </>
        }
        confirmLabel={de.restoreUpload}
        busy={busy === "restore"}
        onConfirm={restore}
        onClose={() => {
          setRestoreConfirmOpen(false);
          setRestoreValue(null);
          setRestoreName("");
          setModalError("");
        }}
      />

      <ConfirmDialog
        open={otaConfirmOpen}
        title={de.otaSection}
        body={
          <>
            <p>{de.otaHint}</p>
            <p className="font-medium text-cream-50">{otaFile?.name}</p>
            <p className="text-xs">{de.rebootHint}</p>
            {modalError ? (
              <p className="text-rust-400" role="alert">
                {modalError}
              </p>
            ) : null}
          </>
        }
        confirmLabel={de.otaUpload}
        tone="primary"
        busy={busy === "ota"}
        onConfirm={uploadFirmware}
        onClose={() => {
          setOtaConfirmOpen(false);
          setModalError("");
        }}
      />

      <Modal
        open={resetOpen}
        title={de.factoryResetTitle}
        onClose={() => {
          setResetOpen(false);
          setResetText("");
          setResetError("");
        }}
        footer={
          <>
            <button
              className="btn"
              type="button"
              onClick={() => {
                setResetOpen(false);
                setResetText("");
                setResetError("");
              }}
              disabled={busy !== ""}
            >
              {de.cancel}
            </button>
            <button
              className="btn btn-danger"
              type="button"
              onClick={factoryReset}
              disabled={resetText !== de.factoryResetConfirm || busy !== ""}
            >
              {busy === "reset" ? de.saving : de.factoryReset}
            </button>
          </>
        }
      >
        <p className="text-sm leading-relaxed text-cream-100/90">{de.factoryResetBody}</p>
        <label className="mt-4 block space-y-1.5">
          <span className="label">{de.factoryResetConfirmHint}</span>
          <input
            className="input mono"
            value={resetText}
            onChange={(event) => setResetText(event.target.value)}
            placeholder={de.factoryResetConfirm}
            autoComplete="off"
          />
        </label>
        {resetError ? (
          <p className="mt-3 text-sm text-rust-400" role="alert">
            {resetError}
          </p>
        ) : null}
      </Modal>
    </div>
  );
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex items-baseline justify-between gap-3 rounded-xl border border-cream-100/10 px-3 py-2">
      <dt className="muted text-xs">{label}</dt>
      <dd className="mono truncate text-sm">{value}</dd>
    </div>
  );
}

function QrImage({ value, label }: { value: string; label: string }) {
  if (!value) {
    return <div className="muted mt-2 text-xs">{de.apQrUnavailable}</div>;
  }
  if (value.startsWith("data:image/")) {
    return <img src={value} alt={label} className="mt-2 h-40 w-40 rounded-xl bg-white p-2" />;
  }
  return (
    <div className="mt-2 h-40 w-40 rounded-xl bg-white p-2">
      <QrCode value={value} label={label} className="h-full w-full" />
    </div>
  );
}
