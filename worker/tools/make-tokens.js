// Prints a fresh UNITS secret: one random token per name.
//   npm run tokens -- Pete Dave Sam
import { randomBytes } from "node:crypto";

const names = process.argv.slice(2);
if (!names.length) {
  console.error("usage: npm run tokens -- Pete Dave Sam");
  process.exit(1);
}
const units = Object.fromEntries(names.map((n) => [n, randomBytes(18).toString("base64url")]));
console.log(JSON.stringify(units));
