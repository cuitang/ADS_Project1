# Porter C module

Upstream: https://tartarus.org/martin/PorterStemmer/c_thread_safe.txt

Author: Martin Porter. Thread-safe ANSI C, Release 4, 25 March 2014.

Retrieved: 2026-10-14. The distribution page states these implementations may be used free of charge for any purpose: https://tartarus.org/martin/PorterStemmer/ . Original comments and attribution are retained; no alternate license is asserted.

- `porter_original.c`: original complete download, including upstream demo main.
- `porter.c`: prefix before the upstream `stemmer definition ends here` marker, so it can be linked as a module. No algorithm changes.
- `porter.h`: declarations for create_stemmer, free_stemmer, stem.
- `voc.txt`, `output.txt`: upstream paired regression vocabulary, 23,531 entries.

Compile porter.c only, not porter_original.c. See REFERENCES_A.md for citations. Separate contexts make independent stemming state possible; do not concurrently share a single context.
