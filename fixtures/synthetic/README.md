# Synthetic DBPF bytes (not EA)

Generated, tiny, for decoder tests. Not game content.

| File | Description |
| --- | --- |
| `empty.bin` | Valid TS3 header, 0 resources, `indexType=0` |
| `single-blob.bin` | One uncompressed payload `Hello SXPE\n` |
| `refpack-hello.bin` | RefPack `10 FB` + stop + `Hi` |
| `minimal.sims3pack` | TS3Pack + XML + one embedded synthetic DBPF |

Regenerate: `python fixtures/synthetic/make_synthetic.py`
