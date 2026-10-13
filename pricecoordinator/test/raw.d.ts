// Vite ?raw query imports are not resolvable by tsc — this ambient module
// declaration makes `import x from "../file.jsonc?raw"` typecheck. Without
// this file, `npm run typecheck` fails on the config test (that failure
// mode is why the file exists).
declare module "*?raw" {
  const content: string;
  export default content;
}
