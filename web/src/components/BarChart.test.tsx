import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import { BarChart } from "./BarChart";

describe("BarChart", () => {
  it("renders accessible values for every bar", () => {
    render(
      <BarChart
        label="Verbrauch"
        points={[
          { label: "10.11.", value: 250, title: "10.11.2026" },
          { label: "11.11.", value: 50, title: "11.11.2026" },
        ]}
      />,
    );
    expect(screen.getByRole("figure", { name: /Verbrauch/ })).toBeInTheDocument();
    expect(screen.getByText("10.11.2026: 2,50 €")).toBeInTheDocument();
    expect(screen.getByText("11.11.2026: 0,50 €")).toBeInTheDocument();
  });
});
