import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import ts from "typescript";

const root = path.dirname(fileURLToPath(import.meta.url));

function compile(sourceRelative, outputRelative, moduleKind) {
  const source = path.join(root, sourceRelative);
  const output = path.join(root, outputRelative);
  const input = fs.readFileSync(source, "utf8");
  const result = ts.transpileModule(input, {
    compilerOptions: {
      target: ts.ScriptTarget.ES2020,
      module: moduleKind,
      removeComments: false,
      esModuleInterop: true
    },
    fileName: path.basename(source)
  });

  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, result.outputText, "utf8");
  console.log(`Built ${output}`);
}

compile("src/app.ts", "dist/app.js", ts.ModuleKind.ES2020);
compile("src/similar-cad.ts", "dist/similar-cad.js", ts.ModuleKind.ES2020);
compile("electron/main.ts", "dist-electron/main.js", ts.ModuleKind.CommonJS);
compile("electron/preload.ts", "dist-electron/preload.js", ts.ModuleKind.CommonJS);
