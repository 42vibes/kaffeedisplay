import type { ReactNode } from "react";
import { NavLink } from "react-router-dom";
import { de } from "../i18n/de";

const nav = [
  { to: "/", label: de.dashboard, end: true },
  { to: "/users", label: de.users },
  { to: "/products", label: de.products },
  { to: "/expenses", label: de.expenses },
  { to: "/reports", label: de.reports },
  { to: "/cash", label: de.cash },
  { to: "/settings", label: de.settings },
  { to: "/system", label: de.system },
];

export function Shell({ children, onLogout }: { children: ReactNode; onLogout: () => void }) {
  return (
    <div className="min-h-screen lg:grid lg:grid-cols-[240px_1fr]">
      <aside className="border-b border-cream-100/10 bg-espresso-900/70 p-4 lg:min-h-screen lg:border-b-0 lg:border-r">
        <div className="flex items-center justify-between lg:block">
          <div>
            <div className="text-xl font-semibold tracking-tight">{de.app}</div>
            <div className="muted mt-1 text-xs">{de.tagline}</div>
          </div>
          <button className="btn lg:hidden" onClick={onLogout} type="button">
            {de.signOut}
          </button>
        </div>
        <nav className="mt-4 flex gap-2 overflow-x-auto pb-1 lg:mt-8 lg:flex-col lg:overflow-visible">
          {nav.map((item) => (
            <NavLink
              key={item.to}
              to={item.to}
              end={item.end}
              className={({ isActive }) =>
                `rounded-xl px-3 py-2 text-sm whitespace-nowrap transition ${
                  isActive
                    ? "bg-amber-400 text-espresso-950 font-semibold"
                    : "text-cream-100/80 hover:bg-cream-100/10"
                }`
              }
            >
              {item.label}
            </NavLink>
          ))}
        </nav>
        <button className="btn mt-6 hidden w-full lg:flex" onClick={onLogout} type="button">
          {de.signOut}
        </button>
      </aside>
      <main className="mx-auto w-full max-w-6xl p-4 sm:p-6 lg:p-10">{children}</main>
    </div>
  );
}
