# Fan Fold third-party notices

Fan Fold uses this bounded offline editor runtime; no CDN or network copy is loaded:

| Component | Exact material | License | Included license |
|---|---|---|---|
| Vditor 3.11.2 | `dist/index.min.js`, `dist/index.css`, English i18n, Ant icon sprite, light content CSS, loading image | MIT | `licenses/VDITOR-MIT.txt` |
| diff-match-patch 1.0.5 | bundled by Vditor's `index.min.js`; direct dependency declared by the published Vditor manifest | Apache-2.0 | `licenses/DIFF-MATCH-PATCH-APACHE-2.0.txt` |
| highlight.js 11.7.0 | minified core and GitHub CSS | BSD-3-Clause | `licenses/HIGHLIGHTJS-BSD-3-CLAUSE.txt` |
| highlightjs-solidity 2.0.5 | Solidity and Yul definitions in `third-languages.js` | WTFPL-2.0 | `licenses/HIGHLIGHTJS-SOLIDITY-WTFPL.txt` |
| highlightjs-sap-abap 0.2.0 | ABAP definition in `third-languages.js` | MIT | `licenses/HIGHLIGHTJS-ABAP-MIT.txt` |
| highlightjs-hlsl | HLSL definition in `third-languages.js`; upstream version is not embedded by Vditor | MIT | `licenses/HIGHLIGHTJS-HLSL-MIT.txt` |
| highlightjs-gdscript | GDScript definition in `third-languages.js`; upstream version is not embedded by Vditor | BSD-3-Clause | `licenses/HIGHLIGHTJS-GDSCRIPT-BSD-3-CLAUSE.txt` |
| Ant Design Icons | donor for Vditor's `ant` icon style; exact Vditor sprite retained from the pinned Vditor artifact | MIT, copyright Ant UED | `licenses/ANT-DESIGN-ICONS-MIT.txt` |
| Lute commit `fa3e64ef38527321722714fd15a3d97f1c03651e` | embedded by Vditor 3.11.2 as `dist/js/lute/lute.min.js` | MulanPSL-2.0 | `licenses/LUTE-MULAN-PSL-2.0.txt` |

The runtime files are copied byte-for-byte from the published npm artifact `vditor@3.11.2`, `https://registry.npmjs.org/vditor/-/vditor-3.11.2.tgz`, SHA-1 `612a405c74b71278a4eea188db3c7d93c0884c11`. The artifact's `package.json` and top-level `LICENSE` are retained under `third_party/vditor-3.11.2/` for provenance.

The Vditor release tag identifies commit `0ef9a84`. Its pinned `pnpm-lock.yaml`
resolves the manifest range `diff-match-patch: ^1.0.5` to exactly `1.0.5`; the
runtime bundle SHA-256 is
`8c5679db69c84ca06a61310b9737cdab967325fb4879de698b09802e254ad064`.

The exact retained Ant sprite is the Vditor 3.11.2 npm artifact file
`dist/js/icons/ant.js`, SHA-256
`a91e14e09dcd7b99f49b7b8dcc6301fed6774d62547fcf4e971757aa3a802c33`.
Vditor documents `ant` as its default icon style. Ant Design Icons' upstream
license is MIT, copyright Ant UED; its retained text SHA-256 is
`5d367fb0a07340571542eb4ee8eb1add62d37b71bad6786a57c2e9a86bf76c70`.
This pins the shipped Vditor origin and donor license. It does not claim that
every one of Vditor's custom toolbar symbols has an independently identified
Ant Design glyph/revision.

`third-languages.js` (SHA-256
`3bf29ef22f3ce4c4cd0047fa6f49acca9d1283d6766780e9c222bd68748bdefb`)
names its donor sources in-file: `highlightjs-solidity@2.0.5` for Solidity and
Yul, `highlightjs-sap-abap@0.2.0`, and the upstream HLSL and GDScript
repositories. The complete donor license texts listed above are retained. The
HLSL and GDScript comments do not embed a release or commit, so no exact donor
revision is asserted.

The Lute build matches `javascript/lute.min.js` at the exact commit above (SHA-256 `3c11d9ce4b58441dc945509d3764f57242ea877379b977a8a13c664aed2172a4`). The shipped MulanPSL-2.0 text also matches that commit's `LICENSE` byte-for-byte (SHA-256 `5a36a4ef787bb02ae700b92030d98411736c5ec3879b75b03fa839b415d1b7d0`).

Downstream inspection URLs:

- Vditor artifact: `https://registry.npmjs.org/vditor/-/vditor-3.11.2.tgz`
- Vditor tag and lock evidence: `https://github.com/Vanessa219/vditor/tree/v3.11.2` and `https://github.com/Vanessa219/vditor/blob/v3.11.2/pnpm-lock.yaml`
- Ant Design Icons license: `https://github.com/ant-design/ant-design-icons/blob/master/LICENSE`
- Solidity/Yul and ABAP pinned donor artifacts: `https://cdn.jsdelivr.net/npm/highlightjs-solidity@2.0.5/` and `https://cdn.jsdelivr.net/npm/highlightjs-sap-abap@0.2.0/`
- HLSL and GDScript donor repositories: `https://github.com/highlightjs/highlightjs-hlsl` and `https://github.com/highlightjs/highlightjs-gdscript`
- Lute pinned commit: `https://github.com/88250/lute/tree/fa3e64ef38527321722714fd15a3d97f1c03651e`

Vditor's optional math, diagram, chart, export, emoji and developer-definition trees are intentionally not installed. They are outside Fan Fold's accepted feature set and would introduce unneeded code and license surface. The package acceptance test rejects those paths.

The application also dynamically links the Qt 6 libraries supplied by the target Linux distribution; those system libraries are not copied into the Fan Fold package. The full-colour and symbolic Fan Fold icons are retained byte-for-byte as `io.github.preyevates.FanFold.svg` (SHA-256 `e6ddb4f6870f96cfc811bf4d6c0509de90c1f1f300bc37dfe6d6a7a4cc8a1be5`) and `io.github.preyevates.FanFold-symbolic.svg` (SHA-256 `faba15254ac5a4dbb4b942b1b3a14ee61b7af2918c41519fcff6fbac4e04cf3f`), and are original to this project. Fan Fold's source is licensed under the repository `LICENSE`.
