import { useCallback, useEffect, useState, type DependencyList } from "react";
import { errorText } from "./errors";

export interface AsyncState<T> {
  data: T | null;
  error: string;
  loading: boolean;
  reload: () => void;
  setData: (data: T | null) => void;
}

/** Small data-fetching hook with loading/error states and manual reload. */
export function useAsync<T>(loader: () => Promise<T>, deps: DependencyList = []): AsyncState<T> {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(true);
  const [version, setVersion] = useState(0);

  useEffect(() => {
    let active = true;
    setLoading(true);
    setError("");
    loader()
      .then((result) => {
        if (active) setData(result);
      })
      .catch((err: unknown) => {
        if (active) setError(errorText(err));
      })
      .finally(() => {
        if (active) setLoading(false);
      });
    return () => {
      active = false;
    };
    // The loader is intentionally not a dependency: callers pass explicit deps.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [...deps, version]);

  const reload = useCallback(() => setVersion((current) => current + 1), []);

  return { data, error, loading, reload, setData };
}
