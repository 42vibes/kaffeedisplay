import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import { UserAvatar } from "./UserAvatar";

describe("UserAvatar", () => {
  it("shows the original initials-only fallback for ID 0", () => {
    render(<UserAvatar name="Ada Lovelace" color="#123456" />);
    const avatar = screen.getByText("AL").parentElement;
    expect(avatar).toHaveStyle({ backgroundColor: "#123456" });
    expect(avatar?.querySelector("img")).toBeNull();
  });

  it("renders embedded cartoon artwork with an initials badge", () => {
    render(<UserAvatar name="Ada Lovelace" color="#123456" avatarId={30} />);
    const avatar = screen.getByText("AL").parentElement;
    expect(avatar?.querySelector("img")).toHaveAttribute(
      "src",
      expect.stringMatching(/^data:image\/png;base64,/),
    );
  });

  it("has a distinct embedded image for all 30 presets", () => {
    const sources = new Set<string>();
    for (let id = 1; id <= 30; id += 1) {
      const { container, unmount } = render(
        <UserAvatar name="Ada Lovelace" color="#123456" avatarId={id} />,
      );
      expect(screen.getByText("AL")).toBeInTheDocument();
      const image = container.querySelector("img");
      expect(image).not.toBeNull();
      sources.add(image?.getAttribute("src") ?? "");
      unmount();
    }
    expect(sources.size).toBe(30);
  });

  it("falls back to initials for unsupported IDs", () => {
    render(<UserAvatar name="Ada" color="#123456" avatarId={31} />);
    const avatar = screen.getByText("A").parentElement;
    expect(avatar).toHaveStyle({ backgroundColor: "#123456" });
    expect(avatar?.querySelector("img")).toBeNull();
  });
});
