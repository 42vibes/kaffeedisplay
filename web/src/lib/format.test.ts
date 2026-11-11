import { describe, expect, it } from "vitest";
import { centsFromInput, formatEuro, inputFromCents, monthKey } from "./format";

describe("money formatting", () => {
  it("formats cents as German euros", () => {
    expect(formatEuro(50).replace(/\u00a0/g, " ")).toBe("0,50 €");
    expect(formatEuro(123456).replace(/\u00a0/g, " ")).toBe("1.234,56 €");
  });

  it("parses German decimal input", () => {
    expect(centsFromInput("1,50")).toBe(150);
    expect(centsFromInput("0.50")).toBe(50);
    expect(centsFromInput("1.234,56")).toBe(123456);
    expect(centsFromInput("")).toBe(0);
  });

  it("round-trips cents input", () => {
    expect(inputFromCents(150)).toBe("1,50");
  });

  it("builds a stable month key", () => {
    expect(monthKey(Date.UTC(2026, 10, 11) / 1000)).toBe("2026-11");
  });
});
