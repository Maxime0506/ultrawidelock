import { createHash } from "node:crypto";
import { readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig, type Plugin } from "vite";

const here = dirname(fileURLToPath(import.meta.url));
const TWIN = resolve(here, "..", "web-twin");
const DIST = resolve(here, "dist");

/* The single line this build adds to the twin. Everything else about
 * index.html must survive byte-for-byte, and the check below proves it. */
const BOOT_TAG = '<script defer src="discord-boot.js"></script>\n';
const ANCHOR = "</head>";

type LockEntry = { bytes: number; sha256: string };
type Lock = { files: Record<string, LockEntry> };

const sha256 = (b: Buffer) => createHash("sha256").update(b).digest("hex");

/* twin.js carries the WASM module inline as a byte array, so it is a binary
 * file wearing a .js extension. Vite must never see it as source: bundling,
 * minifying or re-encoding it corrupts the firmware. It is copied with fs, and
 * the copy is compared back against the original before the build succeeds. */
function twinPassthrough(): Plugin {
  let lock: Lock;

  return {
    name: "openaliro-twin-passthrough",
    apply: "build",

    /* Fail before doing any work, so a drifted twin never half-builds. */
    buildStart() {
      lock = JSON.parse(readFileSync(resolve(here, "twin.lock.json"), "utf8"));
      for (const [name, want] of Object.entries(lock.files)) {
        const buf = readFileSync(resolve(TWIN, name));
        const got = { bytes: buf.byteLength, sha256: sha256(buf) };
        if (got.bytes !== want.bytes || got.sha256 !== want.sha256) {
          this.error(
            `web-twin/${name} does not match twin.lock.json.\n` +
              `  expected ${want.bytes} B  sha256 ${want.sha256}\n` +
              `  actual   ${got.bytes} B  sha256 ${got.sha256}\n` +
              `If you rebuilt the twin on purpose, run \`npm run lock\` and commit the result.`,
          );
        }
      }
    },

    writeBundle() {
      const html = readFileSync(resolve(TWIN, "index.html"));
      const js = readFileSync(resolve(TWIN, "twin.js"));

      const text = html.toString("utf8");
      const at = text.indexOf(ANCHOR);
      if (at < 0) this.error(`web-twin/index.html has no ${ANCHOR} to inject before`);
      if (text.indexOf(ANCHOR, at + 1) >= 0) {
        this.error(`web-twin/index.html has more than one ${ANCHOR}; injection point is ambiguous`);
      }
      const injected = Buffer.from(text.slice(0, at) + BOOT_TAG + text.slice(at), "utf8");

      writeFileSync(resolve(DIST, "index.html"), injected);
      writeFileSync(resolve(DIST, "twin.js"), js);

      /* Prove the deliverable's claim rather than asserting it: twin.js is
       * identical, and index.html differs by exactly the boot tag and nothing
       * else. Deleting the tag from the output must reproduce the source. */
      const outJs = readFileSync(resolve(DIST, "twin.js"));
      if (!outJs.equals(js)) this.error("dist/twin.js is not byte-identical to web-twin/twin.js");

      const outHtml = readFileSync(resolve(DIST, "index.html")).toString("utf8");
      if (outHtml.replace(BOOT_TAG, "") !== text) {
        this.error("dist/index.html differs from web-twin/index.html by more than the boot tag");
      }

      /* Static pages that are not the twin: the Privacy Policy and Terms of
       * Service, which Discord requires to be publicly hosted before an app can
       * be listed. Copied verbatim, same as everything else here. */
      const staticDir = resolve(here, "static");
      let copied = 0;
      for (const name of readdirSync(staticDir)) {
        const buf = readFileSync(resolve(staticDir, name));
        writeFileSync(resolve(DIST, name), buf);
        if (!readFileSync(resolve(DIST, name)).equals(buf)) {
          this.error(`dist/${name} is not byte-identical to static/${name}`);
        }
        copied++;
      }

      const n = BOOT_TAG.length;
      this.info?.(
        `twin copied verbatim; index.html +${n} B (boot tag only), twin.js +0 B; ` +
          `${copied} static page(s)`,
      );
    },
  };
}

export default defineConfig({
  plugins: [twinPassthrough()],
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2022",
    /* Library mode so the only thing Vite compiles is our own boot script;
     * there is no index.html entry for it to crawl and rewrite. */
    lib: {
      entry: resolve(here, "src", "discord-boot.ts"),
      formats: ["iife"],
      name: "OpenaliroDiscordBoot",
      fileName: () => "discord-boot.js",
    },
  },
  preview: {
    port: 5173,
    /* A quick tunnel arrives with a Host header Vite has never seen, and Vite
     * rejects unknown hosts by default. Without this the Activity gets Vite's
     * "host not allowed" page instead of the twin. */
    allowedHosts: [".trycloudflare.com"],
  },
});
