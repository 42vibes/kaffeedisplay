import { describe, expect, it } from "vitest";
import {
  addCalendarDays,
  monthKeyInZone,
  periodRange,
  zonedMidnightSeconds,
} from "./period";

const utc = (year: number, month: number, day: number, hour = 0) =>
  Date.UTC(year, month - 1, day, hour) / 1000;

describe("Europe/Berlin report midnights", () => {
  it("handles the spring DST transition (2026-03-29)", () => {
    expect(zonedMidnightSeconds("2026-03-29")).toBe(utc(2026, 3, 28, 23));
  });

  it("handles the autumn DST transition (2026-10-25)", () => {
    expect(zonedMidnightSeconds("2026-10-25")).toBe(utc(2026, 10, 24, 22));
  });

  it("is independent of the browser local time zone", () => {
    expect(zonedMidnightSeconds("2026-03-29", "America/New_York")).toBe(
      utc(2026, 3, 29, 4),
    );
    expect(zonedMidnightSeconds("2026-03-29", "Asia/Tokyo")).toBe(
      utc(2026, 3, 28, 15),
    );
  });

  it("creates DST-safe custom ranges (47 h and 49 h days)", () => {
    const spring = periodRange("custom", "2026-03-28", "2026-03-29");
    expect(spring.from).toBe(utc(2026, 3, 27, 23));
    expect(spring.to).toBe(utc(2026, 3, 29, 22));
    expect((spring.to ?? 0) - (spring.from ?? 0)).toBe(47 * 3600);

    const autumn = periodRange("custom", "2026-10-24", "2026-10-25");
    expect(autumn.from).toBe(utc(2026, 10, 23, 22));
    expect(autumn.to).toBe(utc(2026, 10, 25, 23));
    expect((autumn.to ?? 0) - (autumn.from ?? 0)).toBe(49 * 3600);
  });

  it("adds calendar days without fixed 86400 arithmetic", () => {
    expect(addCalendarDays("2026-03-28", 1)).toBe("2026-03-29");
    expect(addCalendarDays("2026-10-31", 1)).toBe("2026-11-01");
    expect(addCalendarDays("2026-12-31", 1)).toBe("2027-01-01");
  });

  it("returns no bounds for all time", () => {
    expect(periodRange("all")).toEqual({});
  });

  it("derives the report filename month in Berlin", () => {
    expect(monthKeyInZone(Date.UTC(2026, 4, 31, 22, 30) / 1000)).toBe("2026-06");
    expect(monthKeyInZone(Date.UTC(2026, 4, 31, 21, 30) / 1000)).toBe("2026-05");
  });
});
