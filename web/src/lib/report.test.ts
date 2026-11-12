import { describe, expect, it } from "vitest";
import { reportSummary, timeQualityState } from "./report";

describe("report summaries", () => {
  it("renders flat scalar overviews", () => {
    expect(
      reportSummary({
        kind: "overview",
        period: "month",
        from: 1,
        to: 2,
        time_quality: { trusted: true },
        cash_expected_cents: 5200,
        debts_cents: 350,
      }),
    ).toEqual({ cash_expected_cents: 5200, debts_cents: 350 });
  });

  it("merges totals and ignores tables/meta", () => {
    expect(
      reportSummary({
        kind: "consumption",
        columns: [{ key: "x" }],
        rows: [{ x: 1 }],
        totals: { revenue_cents: 200 },
        extra_cents: 100,
      }),
    ).toEqual({ extra_cents: 100, revenue_cents: 200 });
  });

  it("treats unknown time quality as unknown, never green", () => {
    expect(timeQualityState(true)).toBe("trusted");
    expect(timeQualityState(false)).toBe("untrusted");
    expect(timeQualityState(undefined)).toBe("unknown");
  });
});
