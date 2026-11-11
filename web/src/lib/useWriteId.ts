import { useCallback, useState } from "react";
import { newRequestId } from "../api/client";

/**
 * Stable request id for one logical write. Retries reuse the id; call `renew`
 * after a successful write so the next action gets a fresh one.
 */
export function useWriteId(): { id: string; renew: () => string } {
  const [id, setId] = useState<string>(() => newRequestId());
  const renew = useCallback(() => {
    const next = newRequestId();
    setId(next);
    return next;
  }, []);
  return { id, renew };
}
