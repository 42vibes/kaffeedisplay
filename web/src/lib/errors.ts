import { ApiRequestError } from "../api/client";
import { de } from "../i18n/de";

/** Map API/network failures to a German message (never leak English bodies). */
export function errorText(error: unknown): string {
  if (error instanceof ApiRequestError) {
    if (error.status === 401) return de.sessionExpired;
    if (error.status === 404) return de.notFound;
    if (error.status === 409) return de.conflict;
    if (error.status === 429) return de.tooManyRequests;
    if (error.status >= 500) return de.serverError;
    return de.error;
  }
  if (error instanceof TypeError) return de.networkError;
  if (error instanceof Error && error.message) return de.error;
  return de.error;
}
