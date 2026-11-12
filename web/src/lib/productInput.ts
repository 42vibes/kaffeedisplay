import type { Product, ProductInput } from "../api/types";

/**
 * PATCH /products/{id} accepts a full ProductInput, not a partial patch.
 * Reordering, archiving and restoring must therefore resend every field.
 */
export function toProductInput(
  product: Product,
  overrides: Partial<ProductInput> = {},
): ProductInput {
  return {
    name: product.name,
    category: product.category,
    price_cents: product.price_cents,
    icon: product.icon,
    color: product.color,
    sort: product.sort,
    active: product.active,
    ...overrides,
  };
}
