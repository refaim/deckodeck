# Rust notice sources

`LICENSES-resvg.txt` is the plaintext notice aggregate shipped as `LICENSES.txt`.
The two upstream HTML files in this directory remain unchanged in the repository
as provenance. They are not shipped as separate package documents.

## Checked-in provenance files

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `RUST-COPYRIGHT-library.html` | 1,499,465 | `5647BE074C8EDF7339FD863055923A8FC80BC5610A8D4661EC3B767B9D392C27` |
| `RUST-COPYRIGHT.html` | 14,423,589 | `D518AE2D76BC7A1F0CBF0069B8DEAFB56EFC39C2614CAB74A859EF0B467F8373` |

## Sources copied into the aggregate

- Rust source release 1.99.1, `rust-1.99.1.tar.gz`, SHA-256
  `A351EAA4615A30F504778AE810290CE2725E21B54F85FC6F679CC6E80943B7FA`.
  Selected files: `library/compiler-builtins/LICENSE.txt` (15,078 bytes,
  `AB6EEC6CAF0FA5775E411C7A8BC6A45C4EF2956B0980B157AB74FC5CD62A928B`),
  `library/compiler-builtins/libm/LICENSE.txt` (14,088 bytes,
  `3823DDA7CF046602F4B4E77EC8E227863DC4736037CC85BB33D9F19FEBE16BB7`),
  and legal comment sections from every `.rs` file under
  `library/compiler-builtins/compiler-builtins/src/**` and
  `library/compiler-builtins/libm/src/**`.
- `compiler-rt/LICENSE.TXT` from the `llvm-project` submodule pinned by Rust
  1.99.1 at `1b9c0d5ff9bbe7634aead059efe6b11a7eeba145`: 16,708 bytes,
  `1A8F1058753F1BA890DE984E48F0242A3A5C29A6A8F2ED9FD813F36985387E8D`.
- `compiler-rt/CREDITS.TXT` from the same submodule revision: 1,049 bytes,
  `A9901F47A089DA41E4690682D00CE4CEDAA2BAF41FEDBE79BEEE366D43AC2461`.

## Conversion scope

The Rust standard-library HTML was converted locally with the Windows MSHTML
parser. Text and link destinations were retained, including internal section
anchors. The raw 14 MB broader toolchain HTML is not copied into the plaintext
aggregate.

The compiler-builtins and libm source comment inventory covers every
architecture and target found upstream; it is deliberately conservative and is
not a claim about which files the Windows build selects. The compiler-rt
materials are also included conservatively. This file records provenance and
scope, not a legal certification of linked components.
