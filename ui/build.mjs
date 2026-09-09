import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import ts from "typescript";

const root = path.dirname(fileURLToPath(import.meta.url));
const source = path.join(root, "src", "app.ts");
const outputDir = path.join(root, "dist");
const output = path.join(outputDir, "app.js");

const input = fs.readFileSync(source, "utf8");
const result = ts.transpileModule(input, {
  compilerOptions: {
    target: ts.ScriptTarget.ES2020,
    module: ts.ModuleKind.ES2020,
    removeComments: false
  },
  fileName: "app.ts"
});

fs.mkdirSync(outputDir, { recursive: true });
fs.writeFileSync(output, result.outputText, "utf8");
console.log(`Built ${output}`);
