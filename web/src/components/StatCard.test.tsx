import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import { StatCard } from "./StatCard";

describe("StatCard", () => {
  it("renders label, value and hint", () => {
    render(<StatCard label="Kassenstand" value="52,00 €" hint="Stand 11.11.2026" />);
    expect(screen.getByText("Kassenstand")).toBeInTheDocument();
    expect(screen.getByText("52,00 €")).toBeInTheDocument();
    expect(screen.getByText("Stand 11.11.2026")).toBeInTheDocument();
  });

  it("uses the warning colour for negative equity", () => {
    render(<StatCard label="Differenz" value="-6,70 €" tone="warn" />);
    expect(screen.getByText("-6,70 €")).toHaveClass("text-amber-400");
  });
});
