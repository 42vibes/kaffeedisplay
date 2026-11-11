import { readFileSync } from "node:fs";
import { expect, test, type Page } from "@playwright/test";

const PASSWORD = "kaffee";

async function login(page: Page) {
  await page.goto("/");
  await page.getByLabel("Passwort").fill(PASSWORD);
  await page.getByRole("button", { name: "Anmelden" }).click();
  await expect(page.getByRole("heading", { name: "Übersicht" })).toBeVisible();
}

test("login rejects a wrong password and accepts the admin", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "Admin-Anmeldung" })).toBeVisible();

  await page.getByLabel("Passwort").fill("falsch");
  await page.getByRole("button", { name: "Anmelden" }).click();
  await expect(page.getByRole("alert")).toContainText("Passwort falsch");

  await page.getByLabel("Passwort").fill(PASSWORD);
  await page.getByRole("button", { name: "Anmelden" }).click();
  await expect(page.getByRole("heading", { name: "Übersicht" })).toBeVisible();
  await expect(page.getByText("Kassenstand")).toBeVisible();

  await page.locator("aside button:visible", { hasText: "Abmelden" }).click();
  await expect(page.getByRole("heading", { name: "Admin-Anmeldung" })).toBeVisible();
});

test("creates a user", async ({ page }) => {
  await login(page);
  await page.goto("/users");
  await page.getByRole("button", { name: "Benutzer anlegen" }).click();

  const dialog = page.getByRole("dialog");
  await dialog.getByLabel("Name").fill("Testuser");
  await dialog.getByRole("button", { name: "Speichern" }).click();

  await expect(page.getByText("Benutzer angelegt.")).toBeVisible();
  await expect(page.getByRole("listitem").filter({ hasText: "Testuser" })).toBeVisible();
});

test("records a topup for a member", async ({ page }) => {
  await login(page);
  await page.goto("/users");

  const ada = page.getByRole("listitem").filter({ hasText: "Ada" }).first();
  await ada.getByRole("button", { name: "Einzahlung buchen" }).click();

  const dialog = page.getByRole("dialog");
  await dialog.getByPlaceholder("0,00").fill("5,00");
  await dialog.getByRole("button", { name: "Speichern" }).click();

  await expect(page.getByText("Einzahlung gebucht.")).toBeVisible();
  await expect(page.getByRole("listitem").filter({ hasText: "Ada" }).first()).toContainText("17,50 €");
});

test("downloads an Excel-compatible CSV report", async ({ page }) => {
  await login(page);
  await page.goto("/reports");
  await expect(page.getByRole("heading", { name: "Berichte" })).toBeVisible();

  const downloadPromise = page.waitForEvent("download");
  await page.getByRole("button", { name: "CSV (Excel)" }).click();
  const download = await downloadPromise;

  expect(download.suggestedFilename()).toMatch(/^kaffeekasse_\d{4}-\d{2}_overview\.csv$/);
  const path = await download.path();
  expect(path).toBeTruthy();
  const content = readFileSync(path!, "utf8");
  expect(content).toContain("sep=;");
  expect(content).toContain("Kassenstand");
});

test("renders the dashboard at 360 px", async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 780 });
  await login(page);
  await expect(page.getByText("Kassenstand")).toBeVisible();
  const hasHorizontalOverflow = await page.evaluate(
    () => document.documentElement.scrollWidth > document.documentElement.clientWidth + 1,
  );
  expect(hasHorizontalOverflow).toBe(false);
});
