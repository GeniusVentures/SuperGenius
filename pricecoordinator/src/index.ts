// Worker entrypoint — stub router (plan 01-01).
// The real /v1/prices routing, validation, and cache read-through land in
// plans 01-02/01-04. The DO class MUST be re-exported here: wrangler's `main`
// must export every Durable Object class named in wrangler.jsonc or workerd
// startup fails.
export { PriceCoordinator } from "./coordinator";

export interface Env {
  PRICE_COORDINATOR: DurableObjectNamespace;
  COINGECKO_API_KEY?: string;
}

export default {
  fetch(
    _request: Request,
    _env: Env,
    _ctx: ExecutionContext,
  ): Response {
    return new Response("ok");
  },
};
