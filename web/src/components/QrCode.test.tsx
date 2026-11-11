import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import { QrCode } from "./QrCode";

describe("QrCode", () => {
  it("renders a Wi-Fi payload as an accessible local SVG", () => {
    render(<QrCode value="WIFI:T:WPA;S:Kaffeekasse;P:kaffee1234;;" label="QR-Code: WLAN beitreten" />);
    expect(
      screen.getByRole("img", { name: "QR-Code: WLAN beitreten" }),
    ).toBeInTheDocument();
  });
});
