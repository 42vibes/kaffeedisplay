// Shared numeric preset convention with firmware/components/ui/src/page_book.cpp.
// Five palettes x six decorative motifs, no external assets or image decoding.
const palette = ["#D4A373", "#8FBC8F", "#7FA7C9", "#C9A0DC", "#E07A5F"] as const;

type AvatarProps = {
  name: string;
  color: string;
  avatarId?: number;
  size?: number;
};

export function UserAvatar({ name, color, avatarId = 0, size = 32 }: AvatarProps) {
  const valid = Number.isInteger(avatarId) && avatarId >= 1 && avatarId <= 30;
  const presetIndex = valid ? avatarId - 1 : 0;
  const motif = presetIndex % 6;
  const baseColor = valid ? palette[Math.floor(presetIndex / 6)] : color;
  const initials = name
    .trim()
    .split(/\s+/)
    .filter(Boolean)
    .slice(0, 2)
    .map((part) => Array.from(part)[0]?.toUpperCase() ?? "")
    .join("") || "?";

  const marks: Array<Array<{ left: string; top: string; width: string; height: string; round?: boolean }>> = [
    [{ left: "9%", top: "9%", width: "17%", height: "17%", round: true }],
    [{ left: "74%", top: "9%", width: "17%", height: "17%", round: true }],
    [
      { left: "9%", top: "74%", width: "17%", height: "17%", round: true },
      { left: "74%", top: "9%", width: "17%", height: "17%", round: true },
    ],
    [{ left: "9%", top: "9%", width: "34%", height: "8%" }],
    [{ left: "74%", top: "74%", width: "17%", height: "17%", round: true }],
    [
      { left: "9%", top: "9%", width: "17%", height: "17%", round: true },
      { left: "74%", top: "74%", width: "17%", height: "17%", round: true },
    ],
  ];

  return (
    <span
      className="relative inline-flex shrink-0 items-center justify-center overflow-hidden rounded-full font-semibold"
      style={{
        width: size,
        height: size,
        backgroundColor: baseColor,
        color: "#1A1412",
        fontSize: Math.max(12, Math.round(size * 0.38)),
      }}
      aria-hidden="true"
    >
      {valid && marks[motif].map((mark, i) => (
        <span
          key={i}
          className="absolute"
          style={{
            left: mark.left,
            top: mark.top,
            width: mark.width,
            height: mark.height,
            borderRadius: mark.round ? "50%" : 1,
            backgroundColor: "rgba(26,20,18,.4)",
          }}
        />
      ))}
      <span className="relative">{initials}</span>
    </span>
  );
}
