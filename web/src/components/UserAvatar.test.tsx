import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import { UserAvatar } from "./UserAvatar";

describe("UserAvatar", () => {
  it("falls back to the user colour when no preset is set", () => {
    render(<UserAvatar name="Ada Lovelace" color="#123456" />);
    const avatar = screen.getByText("AL").parentElement;
    expect(avatar).toHaveStyle({ backgroundColor: "#123456" });
  });

  it("uses one of the thirty fixed palettes independently of user colour", () => {
    render(<UserAvatar name="Ada Lovelace" color="#123456" avatarId={30} />);
    const avatar = screen.getByText("AL").parentElement;
    expect(avatar).toHaveStyle({ backgroundColor: "#E07A5F" });
  });

  it("renders each of the 30 presets without an image request", () => {
    for (let id = 1; id <= 30; id += 1) {
      const { container, unmount } = render(
        <UserAvatar name="Ada Lovelace" color="#123456" avatarId={id} />,
      );
      expect(screen.getByText("AL")).toBeInTheDocument();
      expect(container.querySelector("img")).toBeNull();
      unmount();
    }
  });

  it("treats unsupported preset numbers as initials only", () => {
    render(<UserAvatar name="Ada" color="#123456" avatarId={31} />);
    const avatar = screen.getByText("A").parentElement;
    expect(avatar).toHaveStyle({ backgroundColor: "#123456" });
  });
});
