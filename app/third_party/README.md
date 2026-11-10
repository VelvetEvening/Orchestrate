# Math preview dependencies

- MicroTeX: https://github.com/NanoMichael/MicroTeX, commit `0e3707f6dafebb121d98b53c64364d16fefe481d` (MIT; font licenses in `microtex/res/fonts/licences`). Only the core and Qt backend are compiled.
- tinyxml2: https://github.com/leethomason/tinyxml2, tag `11.0.0`, commit `9148bdf719e997d1f474be6bcc7943881046dba1` (zlib).

Sources and fonts are vendored so builds and previews work offline. Local MicroTeX changes: Qt 6 static font database API; logical pixel sizing with unhinted fonts and large-em glyph outlines for accurate small-font rendering; fixed resource root without environment/disk discovery; external alphabets use system text fallback rather than loading XML from disk. Unused examples, backends and alphabet packs are excluded from version control. Formula execution is isolated in the short-lived OrchestrateMath worker. Application code restricts size, nesting, commands and output size; it does not accept user-defined macros, file inclusion or runtime configuration commands.

The portable package includes the original library and font notices. No TeX installation, browser engine or persistent rendering service is required.
