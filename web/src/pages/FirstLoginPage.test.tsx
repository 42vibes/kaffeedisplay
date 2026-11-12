import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import { FirstLoginPage } from "./FirstLoginPage";
import { de } from "../i18n/de";

describe("FirstLoginPage", () => {
  it("renders the forced first-login password form", () => {
    render(<FirstLoginPage onDone={vi.fn()} />);
    expect(screen.getByRole("heading", { name: de.firstLoginTitle })).toBeInTheDocument();
    expect(screen.getByText(de.firstLoginBody)).toBeInTheDocument();
    expect(screen.getByLabelText(de.firstLoginCurrent)).toBeInTheDocument();
    expect(screen.getByLabelText(de.newPassword)).toBeInTheDocument();
    expect(screen.getByLabelText(de.confirmPassword)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: de.firstLoginSubmit })).toBeInTheDocument();
  });
});
