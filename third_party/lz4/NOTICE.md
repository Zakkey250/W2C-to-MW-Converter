# Pinned dependency

LZ4 1.10.0: `lib/lz4.c`, `lib/lz4.h`, `lib/LICENSE` from the official
[lz4 v1.10.0 repository](https://github.com/lz4/lz4/tree/v1.10.0/lib).
See the retained BSD 2-Clause [LICENSE](LICENSE). File SHA256 values are
recorded in `evidence/v2-20261110/lz4-provenance.json` and the frozen compiler
source inventory. Repacker and independent validator link these C sources
statically; no existing game MOD is replaced.

Each geometry cluster is an independent block following the official
[block format](https://github.com/lz4/lz4/blob/v1.10.0/doc/lz4_Block_format.md).
The outer page supplies stored/decoded lengths, codec identity and CRCs.
No dictionary, cross-block history, frame wrapper, or unbounded decode is used.
