// Cloudflare Worker entry point for the Book-Club 3000 nudge hub.
// Only the default export may live here: the Workers runtime treats any other
// named export of this module as a handler. The logic is in hub.js.
import { handle } from "./hub.js";

export default {
  fetch(request, env) {
    return handle(request, env, Date.now());
  },
};
