import { describe, expect, it } from "vitest";
import {
  centsFromInput,
  formatBytes,
  formatDateInZone,
  formatDateTimeInZone,
  formatDuration,
  formatEuro,
  formatSignedEuro,
  inputFromCents,
  monthKey,
  parseMoneyInput,
} from "./format";

const plain = (value: string) => value.replace(/\u00a0/g, " ");

describe("money formatting", () => {
  it("formats cents as German euros", () => {
    expect(plain(formatEuro(50))).toBe("0,50 €");
    expect(plain(formatEuro(123456))).toBe("1.234,56 €");
    expect(plain(formatEuro(-350))).toBe("-3,50 €");
  });

  it("formats signed ledger amounts", () => {
    expect(plain(formatSignedEuro(150))).toBe("+1,50 €");
    expect(plain(formatSignedEuro(-50))).toBe("−0,50 €");
    expect(plain(formatSignedEuro(0))).toBe("0,00 €");
  });

  it("parses German decimal input", () => {
    expect(centsFromInput("1,50")).toBe(150);
    expect(centsFromInput("0.50")).toBe(50);
    expect(centsFromInput("1.234,56")).toBe(123456);
    expect(centsFromInput("12")).toBe(1200);
    expect(centsFromInput("")).toBe(0);
  });

  it("parses currency symbols, spaces and negative adjustments", () => {
    expect(centsFromInput(" 1,50 € ")).toBe(150);
    expect(centsFromInput("-2,25")).toBe(-225);
    expect(centsFromInput("−2,25")).toBe(-225);
    expect(centsFromInput("abc")).toBe(0);
  });

  it("distinguishes valid money from junk and empty input", () => {
    expect(parseMoneyInput("1,50")).toEqual({ cents: 150, valid: true });
    expect(parseMoneyInput("1.234,56")).toEqual({ cents: 123456, valid: true });
    expect(parseMoneyInput("0")).toEqual({ cents: 0, valid: true });
    expect(parseMoneyInput("")).toEqual({ cents: 0, valid: false });
    expect(parseMoneyInput("abc")).toEqual({ cents: 0, valid: false });
    expect(parseMoneyInput("1,234")).toEqual({ cents: 0, valid: false });
    expect(parseMoneyInput("12.")).toEqual({ cents: 0, valid: false });
  });

  it("round-trips cents input", () => {
    expect(inputFromCents(150)).toBe("1,50");
    expect(inputFromCents(-225)).toBe("-2,25");
  });

  it("builds a stable month key", () => {
    expect(monthKey(Date.UTC(2026, 10, 11) / 1000)).toBe("2026-11");
  });

  it("does not throw on sentinel or invalid timestamps", () => {
    expect(formatDateInZone(Number.MAX_SAFE_INTEGER)).toBe("–");
    expect(formatDateTimeInZone(Number.MAX_SAFE_INTEGER)).toBe("–");
    expect(formatDateInZone(Number.NaN)).toBe("–");
    expect(formatDateInZone(0)).toBe("01.01.1970");
    expect(formatDateTimeInZone(0)).toBe("–");
  });
});

describe("system formatting", () => {
  it("formats byte sizes", () => {
    expect(formatBytes(512)).toBe("512 B");
    expect(formatBytes(2048)).toBe("2.0 KB");
    expect(formatBytes(9437184)).toBe("9.0 MB");
  });

  it("formats durations", () => {
    expect(formatDuration(45)).toBe("45 s");
    expect(formatDuration(3660)).toBe("1 h 1 min");
  });
});
